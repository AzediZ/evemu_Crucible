 /**
  * @name DungeonMgr.cpp
  *     Dungeon management system for EVEmu
  *
  * @Author:        James
  * @date:          13 December 2022
  */
 
#include "eve-server.h"

#include "EVEServerConfig.h"

#include "StaticDataMgr.h"
#include "dungeon/DungeonDB.h"
#include "Client.h"
#include "EntityList.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/Sentry.h"
#include "npc/SentryAI.h"
#include "ship/Ship.h"
#include "system/Asteroid.h"
#include "system/Container.h"
#include "system/Damage.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/TargetManager.h"
#include "fleet/FleetService.h"
#include "map/MapData.h"
#include "system/cosmicMgrs/SpawnMgr.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/cosmicMgrs/DungeonMgr.h"

/* Dungeon flow:
 *  DungeonDataMgr loads the static dungeons (dunDungeons, rooms, objects, spawn groups, triggers/events, gates, loot,
 *   escalation rules) at server start.
 *  AnomalyMgr (combat anomalies, signature sites), missions, incursions and escalations ask their system's DungeonMgr
 *   for an instance:  room 0 at the site, further rooms 1/3 AU away reached through acceleration gates.  objects and
 *   the room-created npc groups spawn right away, other rooms' groups when first reached.
 *  kills, damage, mining and object events queue triggers;  Process() runs them (waves, gates, messages, loot,
 *   counters, completion), watches presence, conditions and expiry, and despawns finished instances.
 *  a completed site may escalate (dunEscalations) into a private site a few jumps away (chrEscalations, journal).
 */


DungeonDataMgr::DungeonDataMgr()
{
    m_dungeons.clear();
}

int DungeonDataMgr::Initialize()
{
    double start = GetTimeMSeconds();
    // Loads all dungeons from database upon server initialisation
    Load(0);

    DBQueryResult res;
    DBResultRow row;
    DungeonDB::GetLoot(res);
    while (res.GetRow(row)) {
        Dungeon::LootEntry entry = Dungeon::LootEntry();
            entry.typeID = row.GetUInt(1);
            entry.minQty = row.GetUInt(2);
            entry.maxQty = row.GetUInt(3);
            entry.chance = row.GetDouble(4);
        m_loot.emplace(row.GetUInt(0), entry);
    }

    DungeonDB::GetEscalations(res);
    while (res.GetRow(row)) {
        Dungeon::EscalationDef esc = Dungeon::EscalationDef();
            esc.escalationID = row.GetUInt(0);
            esc.srcDungeonID = row.GetUInt(1);
            esc.srcArchetypeID = row.GetUInt(2);
            esc.srcMinDifficulty = row.GetUInt(3);
            esc.destDungeonID = row.GetUInt(4);
            esc.destArchetypeID = row.GetUInt(5);
            esc.destDifficultyDelta = row.GetInt(6);
            esc.chance = row.GetDouble(7);
            esc.minJumps = row.GetUInt(8);
            esc.maxJumps = row.GetUInt(9);
            esc.lowerSecOnly = row.GetBool(10);
            esc.expiryHours = row.GetUInt(11);
            esc.journalMessageID = row.GetUInt(12);
        m_escalations.push_back(esc);
    }

    sLog.Cyan("       DunDataMgr", "%lu dungeons, %lu gates, %lu loot entries and %lu escalation rules loaded in %.3fms.",
              m_dungeons.size(), m_gates.size(), m_loot.size(), m_escalations.size(), (GetTimeMSeconds() - start));
    sLog.Blue("       DunDataMgr", "Dungeon Data Manager Initialized.");
    return 1;
}

void DungeonDataMgr::UpdateDungeon(uint32 dungeonID)
{
    _log(DUNG__INFO, "UpdateDungeon() - Updating dungeon %u's object in DataMgr...", dungeonID);
    Load(dungeonID);
}

void DungeonDataMgr::ParseGroupList(const std::string& csv, std::vector<uint32>& into)
{
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        int32 groupID = atoi(item.c_str());
        if (groupID > 0)
            into.push_back(groupID);
    }
}

void DungeonDataMgr::Load(uint32 dungeonID)
{
    // forget what we had (live instances keep their own copy through the shared pointer)
    if (dungeonID == 0) {
        m_dungeons.clear();
        m_byArchetype.clear();
        m_gateDungeon.clear();
        m_gates.clear();
    } else {
        m_dungeons.erase(dungeonID);
        for (auto itr = m_byArchetype.begin(); itr != m_byArchetype.end(); )
            itr = ((itr->second == dungeonID) ? m_byArchetype.erase(itr) : ++itr);
        for (auto itr = m_gateDungeon.begin(); itr != m_gateDungeon.end(); ) {
            if (itr->second == dungeonID) {
                m_gates.erase(itr->first);
                itr = m_gateDungeon.erase(itr);
            } else {
                ++itr;
            }
        }
    }

    DBQueryResult res;
    DBResultRow row;
    DungeonDB::GetDungeonRows(res, dungeonID);
    while (res.GetRow(row)) {
        std::shared_ptr<Dungeon::Dungeon> dData(new Dungeon::Dungeon());
            dData->dungeonID = row.GetUInt(0);
            dData->name = row.GetText(1);
            dData->status = row.GetUInt(2);
            dData->factionID = row.GetUInt(3);
            dData->archetypeID = row.GetUInt(4);
            dData->nameID = row.GetUInt(5);
            dData->difficulty = row.GetUInt(6);
            dData->minSec = row.GetFloat(7);
            dData->maxSec = row.GetFloat(8);
            dData->weight = (row.GetInt(9) > 0 ? row.GetInt(9) : 0);
            dData->lifetimeHours = row.GetUInt(10);
            dData->hasCompletionEvent = false;
        m_dungeons[dData->dungeonID] = dData;
    }

    // rooms (ascending roomID, room index 0 is the entry room)
    std::map<uint32, Dungeon::Room*> rooms;
    DungeonDB::GetRoomRows(res, dungeonID);
    while (res.GetRow(row)) {
        auto itr = m_dungeons.find(row.GetUInt(2));
        if (itr == m_dungeons.end())
            continue;
        Dungeon::Room& room = itr->second->rooms[row.GetUInt(0)];
            room.roomID = row.GetUInt(0);
            room.roomName = row.GetText(1);
            room.arrival = GPoint(row.GetDouble(3), row.GetDouble(4), row.GetDouble(5));
        itr->second->roomOrder.push_back(room.roomID);
        rooms[room.roomID] = &room;
    }

    DungeonDB::GetObjectRows(res, dungeonID);
    while (res.GetRow(row)) {
        auto itr = rooms.find(row.GetUInt(1));
        if (itr == rooms.end())
            continue;
        Dungeon::RoomObject oData = Dungeon::RoomObject();
            oData.objectID = row.GetUInt(0);
            oData.roomID = row.GetUInt(1);
            oData.typeID = row.GetUInt(2);
            oData.groupID = row.GetUInt(3);
            oData.x = row.GetDouble(4);
            oData.y = row.GetDouble(5);
            oData.z = row.GetDouble(6);
            oData.yaw = row.GetDouble(7);
            oData.pitch = row.GetDouble(8);
            oData.roll = row.GetDouble(9);
            oData.radius = row.GetDouble(10);
        itr->second->objects.push_back(oData);
    }

    DungeonDB::GetSpawnGroups(res, dungeonID);
    while (res.GetRow(row)) {
        auto itr = rooms.find(row.GetUInt(1));
        if (itr == rooms.end())
            continue;
        Dungeon::SpawnGroupDef group = Dungeon::SpawnGroupDef();
            group.spawnGroupID = row.GetUInt(0);
            group.roomID = row.GetUInt(1);
            group.waveNumber = row.GetUInt(2);
            group.spawnMode = row.GetUInt(3);
            group.npcClassType = row.GetUInt(4);
            group.npcClassSub = row.GetUInt(5);
            group.factionID = row.GetUInt(6);
            group.x = row.GetDouble(7);
            group.y = row.GetDouble(8);
            group.z = row.GetDouble(9);
            group.spread = row.GetDouble(10);
            group.warpIn = row.GetBool(11);
            group.isOverseer = row.GetBool(12);
            group.lootGroupID = row.GetUInt(13);
            // column 14 is r.dungeonID.  npc ai phase 2:  aggro mode, range and link (all proactive by default)
            group.aggroMode = row.GetUInt(15);
            group.aggroRange = row.GetUInt(16);
            group.aggroLink = row.GetUInt(17);
        itr->second->groups.push_back(group);
    }
    // the group vectors are complete now, so pointers into them stay valid
    std::map<uint32, Dungeon::SpawnGroupDef*> groups;
    for (auto& cur : rooms)
        for (auto& group : cur.second->groups)
            groups[group.spawnGroupID] = &group;

    DungeonDB::GetSpawnGroupTypes(res, dungeonID);
    while (res.GetRow(row)) {
        auto itr = groups.find(row.GetUInt(0));
        if (itr == groups.end())
            continue;
        itr->second->types.emplace_back(row.GetUInt(1), (row.GetInt(2) > 0 ? row.GetInt(2) : 1));
    }

    DungeonDB::GetTriggers(res, dungeonID);
    while (res.GetRow(row)) {
        auto itr = rooms.find(row.GetUInt(1));
        if (itr == rooms.end())
            continue;
        Dungeon::TriggerDef trigger = Dungeon::TriggerDef();
            trigger.triggerID = row.GetUInt(0);
            trigger.roomID = row.GetUInt(1);
            trigger.triggerType = row.GetUInt(2);
            trigger.spawnGroupID = row.GetUInt(3);
            trigger.objectID = row.GetUInt(4);
            trigger.value = row.GetDouble(5);
            trigger.counterID = row.GetUInt(6);
            trigger.repeatable = row.GetBool(7);
        itr->second->triggers.push_back(trigger);
    }
    std::map<uint32, Dungeon::TriggerDef*> triggers;
    for (auto& cur : rooms)
        for (auto& trigger : cur.second->triggers)
            triggers[trigger.triggerID] = &trigger;

    // events come sorted by trigger and sequence
    DungeonDB::GetTriggerEvents(res, dungeonID);
    while (res.GetRow(row)) {
        auto itr = triggers.find(row.GetUInt(1));
        if (itr == triggers.end())
            continue;
        Dungeon::EventDef event = Dungeon::EventDef();
            event.eventID = row.GetUInt(0);
            event.triggerID = row.GetUInt(1);
            event.sequence = row.GetInt(2);
            event.eventType = row.GetUInt(3);
            event.delaySeconds = (row.GetInt(4) > 0 ? row.GetInt(4) : 0);
            event.spawnGroupID = row.GetUInt(5);
            event.objectID = row.GetUInt(6);
            event.messageType = row.GetUInt(7);
            event.messageID = row.GetUInt(8);
            event.messageText = row.GetText(9);
            event.typeID = row.GetUInt(10);
            event.quantity = row.GetInt(11);
            event.value = row.GetDouble(12);
            event.counterID = row.GetUInt(13);
        itr->second->events.push_back(event);
    }

    DungeonDB::GetGates(res, dungeonID);
    while (res.GetRow(row)) {
        Dungeon::GateDef gate = Dungeon::GateDef();
            gate.objectID = row.GetUInt(0);
            gate.destRoomID = row.GetUInt(1);
            gate.locked = row.GetBool(2);
            gate.requireClear = row.GetBool(3);
            gate.keyTypeID = row.GetUInt(4);
            gate.keyQuantity = (row.GetInt(5) > 0 ? row.GetInt(5) : 1);
            gate.keyConsumed = row.GetBool(6);
            gate.skillTypeID = row.GetUInt(7);
            gate.skillLevel = row.GetUInt(8);
            ParseGroupList(row.GetText(9), gate.allowedGroups);
            ParseGroupList(row.GetText(10), gate.deniedGroups);
            gate.musicUrl = row.GetText(11);
        m_gates[gate.objectID] = gate;
    }

    // per-dungeon bookkeeping
    for (auto& cur : m_dungeons) {
        if ((dungeonID != 0) and (cur.first != dungeonID))
            continue;
        m_byArchetype.emplace(cur.second->archetypeID, cur.first);
        for (auto& room : cur.second->rooms) {
            for (auto& object : room.second.objects) {
                auto gItr = m_gates.find(object.objectID);
                if (gItr == m_gates.end())
                    continue;
                m_gateDungeon[object.objectID] = cur.first;
                // a copy the dungeon's live instances keep when this dungeon is reloaded
                cur.second->gates[object.objectID] = gItr->second;
            }
            for (auto& trigger : room.second.triggers)
                for (auto& event : trigger.events)
                    if (event.eventType == Dungeon::Event::Trigger::DungeonCompletion)
                        cur.second->hasCompletionEvent = true;
        }
    }

    _log(DUNG__INFO, "DungeonDataMgr::Load(%u) - %lu dungeons loaded.", dungeonID, m_dungeons.size());
}

void DungeonDataMgr::GetRandomDungeon(Dungeon::Dungeon& dungeon, uint8 archetype) {
    auto range = m_byArchetype.equal_range(archetype);
    uint32 count = std::distance(range.first, range.second);
    // If there are no dungeons with the specified archetype, return an empty dungeon
    if (count == 0) {
        dungeon = Dungeon::Dungeon();
        _log(DUNG__WARNING, "GetRandomDungeon() - no dungeon with archetype %u", archetype);
        return;
    }
    auto it = range.first;
    std::advance(it, MakeRandomInt(0, count - 1));
    GetDungeon(dungeon, it->second);
}

void DungeonDataMgr::GetDungeon(Dungeon::Dungeon& dungeon, uint32 dungeonID) {
    auto it = m_dungeons.find(dungeonID);
    if (it != m_dungeons.end()) {
        dungeon = *it->second;
    } else {
        dungeon = Dungeon::Dungeon();
        _log(DUNG__ERROR, "GetDungeon() - Failed to find dungeon with id %u", dungeonID);
    }
}

std::shared_ptr<const Dungeon::Dungeon> DungeonDataMgr::GetDungeon(uint32 dungeonID)
{
    auto it = m_dungeons.find(dungeonID);
    if (it != m_dungeons.end())
        return it->second;
    return nullptr;
}

uint32 DungeonDataMgr::PickDungeon(uint8 archetype, uint32 factionID, float trueSec, uint8 minDifficulty/*0*/)
{
    std::vector<std::pair<uint32, double>> exact, generic;
    auto range = m_byArchetype.equal_range(archetype);
    for (auto itr = range.first; itr != range.second; ++itr) {
        auto dItr = m_dungeons.find(itr->second);
        if (dItr == m_dungeons.end())
            continue;
        const Dungeon::Dungeon& dun = *dItr->second;
        if ((dun.weight == 0) or dun.rooms.empty())
            continue;
        if ((trueSec < dun.minSec) or (trueSec > dun.maxSec))
            continue;
        if (dun.difficulty < minDifficulty)
            continue;
        double weight(dun.weight);
        if (trueSec < 0.0f) {
            // null sec bands (CCP Greyscale, Mar 2011):  the hard sites concentrate in the low true sec systems
            if (dun.difficulty >= 4) {
                weight *= (1.0 + 4.0 * -trueSec);
            } else if ((dun.difficulty > 0) and (dun.difficulty <= 2)) {
                weight *= (1.0 + 4.0 * (1.0 + trueSec));
            }
        }
        if ((factionID == 0) or (dun.factionID == factionID)) {
            exact.emplace_back(dun.dungeonID, weight);
        } else if (dun.factionID == 0) {
            generic.emplace_back(dun.dungeonID, weight);
        }
    }

    std::vector<std::pair<uint32, double>>& list = (exact.empty() ? generic : exact);
    double total(0.0);
    for (auto& cur : list)
        total += cur.second;
    if (total <= 0.0)
        return 0;
    double roll(MakeRandomFloat(0.0, total));
    for (auto& cur : list) {
        if (roll < cur.second)
            return cur.first;
        roll -= cur.second;
    }
    return list.back().first;
}

const Dungeon::GateDef* DungeonDataMgr::GetGate(uint32 objectID)
{
    auto itr = m_gates.find(objectID);
    if (itr == m_gates.end())
        return nullptr;
    return &itr->second;
}

void DungeonDataMgr::GetLoot(uint32 lootGroupID, std::vector<Dungeon::LootEntry>& into)
{
    auto range = m_loot.equal_range(lootGroupID);
    for (auto itr = range.first; itr != range.second; ++itr)
        into.push_back(itr->second);
}

void DungeonDataMgr::GetEscalations(uint32 dungeonID, uint8 archetype, uint8 difficulty, std::vector<const Dungeon::EscalationDef*>& into)
{
    for (auto& cur : m_escalations) {
        if (cur.srcDungeonID != 0) {
            if (cur.srcDungeonID == dungeonID)
                into.push_back(&cur);
        } else if ((cur.srcArchetypeID != 0) and (cur.srcArchetypeID == archetype) and (difficulty >= cur.srcMinDifficulty)) {
            into.push_back(&cur);
        }
    }
}

bool DungeonDataMgr::IsCapitalGroup(uint32 groupID)
{
    switch (groupID) {
        case EVEDB::invGroups::Titan:
        case EVEDB::invGroups::Dreadnought:
        case EVEDB::invGroups::Freighter:
        case EVEDB::invGroups::Carrier:
        case EVEDB::invGroups::Supercarrier:
        case EVEDB::invGroups::CapitalIndustrialShip:
        case EVEDB::invGroups::JumpFreighter:
            return true;
    }
    return false;
}

void DungeonDataMgr::GetShipRestrictions(uint32 dungeonID, std::vector<uint32>& allowedGroups, std::vector<uint32>& deniedGroups)
{
    std::set<uint32> allowed, denied;
    for (auto& cur : m_gateDungeon) {
        if (cur.second != dungeonID)
            continue;
        auto itr = m_gates.find(cur.first);
        if (itr == m_gates.end())
            continue;
        allowed.insert(itr->second.allowedGroups.begin(), itr->second.allowedGroups.end());
        denied.insert(itr->second.deniedGroups.begin(), itr->second.deniedGroups.end());
    }
    // capitals can't use acceleration gates, so only single-room dungeons are open to them
    auto dItr = m_dungeons.find(dungeonID);
    if ((dItr == m_dungeons.end()) or (dItr->second->roomOrder.size() > 1)) {
        denied.insert(EVEDB::invGroups::Titan);
        denied.insert(EVEDB::invGroups::Dreadnought);
        denied.insert(EVEDB::invGroups::Freighter);
        denied.insert(EVEDB::invGroups::Carrier);
        denied.insert(EVEDB::invGroups::Supercarrier);
        denied.insert(EVEDB::invGroups::CapitalIndustrialShip);
        denied.insert(EVEDB::invGroups::JumpFreighter);
    }
    for (auto cur : allowed)
        if (denied.find(cur) == denied.end())
            allowedGroups.push_back(cur);
    deniedGroups.insert(deniedGroups.end(), denied.begin(), denied.end());
}

std::string DungeonDataMgr::GetDisplayName(const Dungeon::Dungeon& dungeon, uint32 factionID)
{
    // generated dungeon names already carry their faction
    if (!dungeon.name.empty())
        return dungeon.name;
    return GetDungeonType(dungeon.archetypeID);
}

const char* DungeonDataMgr::GetDungeonType(int8 typeID)
{
    // Return the string representation of the given dungeon type ID
    switch (typeID) {
        case Dungeon::Type::Mission:        return "Mission";
        case Dungeon::Type::Gravimetric:    return "Gravimetric";
        case Dungeon::Type::Magnetometric:  return "Magnetometric";
        case Dungeon::Type::Radar:          return "Radar";
        case Dungeon::Type::Ladar:          return "Ladar";
        case Dungeon::Type::Rated:          return "Rated";
        case Dungeon::Type::Anomaly:        return "Anomaly";
        case Dungeon::Type::Unrated:        return "Unrated";
        case Dungeon::Type::Escalation:     return "Escalation";
        case Dungeon::Type::Wormhole:       return "Wormhole";
        case Dungeon::Type::Incursion:      return "Incursion";
        default:                            return "Invalid";
    }
}


DungeonMgr::DungeonMgr(SystemManager* mgr, EVEServiceManager& svc)
: m_initalized(false),
m_anomMgr(nullptr),
m_spawnMgr(nullptr),
m_system(mgr),
m_services(svc),
m_nextLiveGroupID(1)
{
}

DungeonMgr::~DungeonMgr()
{
    // the entities are gone with the system (Close() ran during the unload).  listeners are not called from here
}

bool DungeonMgr::Init(AnomalyMgr* anomMgr, SpawnMgr* spawnMgr)
{
    if (!m_initalized)
    {
        m_anomMgr = anomMgr;
        m_spawnMgr = spawnMgr;

        if (m_anomMgr == nullptr) {
            _log(COSMIC_MGR__ERROR, "System Init Fault. anomMgr == nullptr.  Not Initializing Dungeon Manager for %s(%u)", m_system->GetName(), m_system->GetID());
            return m_initalized;
        }

        if (m_spawnMgr == nullptr) {
            _log(COSMIC_MGR__ERROR, "System Init Fault. spawnMgr == nullptr.  Not Initializing Dungeon Manager for %s(%u)", m_system->GetName(), m_system->GetID());
            return m_initalized;
        }

        if (!sConfig.cosmic.DungeonEnabled){
            _log(COSMIC_MGR__INIT, "Dungeon System Disabled.  Not Initializing Dungeon Manager for %s(%u)", m_system->GetName(), m_system->GetID());
            return true;
        }

        m_spawnMgr->SetDungMgr(this);

        _log(COSMIC_MGR__INIT, "DungeonMgr Initialized for %s(%u)", m_system->GetName(), m_system->GetID());

        m_initalized = true;
        // escalation sites granted for this system come back
        LoadEscalations();
        return true;
    }
    return false;
}

/* instance creation */

uint32 DungeonMgr::ResolveFaction(const Dungeon::SpawnParams& params, const Dungeon::Dungeon& dungeon)
{
    if (params.factionID != 0)
        return params.factionID;
    if (dungeon.factionID != 0)
        return dungeon.factionID;
    uint32 factionID(sDataMgr.GetRegionRatFaction(m_system->GetRegionID()));
    if (factionID != 0)
        return factionID;
    return factionRogueDrones;
}

GPoint DungeonMgr::NextRoomOrigin(const GPoint& from)
{
    // rooms are 1/3 AU apart in a random direction, inside the system and away from other grids
    SystemData sysData = SystemData();
    sDataMgr.GetSystemData(m_system->GetID(), sysData);
    double maxDist((sysData.radius > 0 ? sysData.radius : ONE_AU_IN_METERS *50.0) *0.9);
    GPoint pos(from);
    for (uint8 tries = 0; tries < 10; ++tries) {
        pos = from;
        pos.MakeRandomPointOnSphere(NEXT_DUNGEON_ROOM_DIST);
        if (pos.length() > maxDist) {
            GVector dir(pos);
            dir.normalize();
            pos = dir * (maxDist * MakeRandomFloat(0.6, 1.0));
        }
        if (sBubbleMgr.FindBubble(m_system->GetID(), pos) == nullptr)
            break;
    }
    return pos;
}

bool DungeonMgr::IsNPCType(uint32 typeID)
{
    const ItemType* pType(sItemFactory.GetType(typeID));
    if ((pType == nullptr) or (pType->categoryID() != EVEDB::invCategories::Entity))
        return false;
    // entities that are objects (containers, sentries, structures, deco)
    switch (pType->groupID()) {
        case EVEDB::invGroups::Spawn_Container:
        case EVEDB::invGroups::Sentry_Gun:
        case EVEDB::invGroups::Protective_Sentry_Gun:
        case EVEDB::invGroups::Destructible_Sentry_Gun:
        case EVEDB::invGroups::Mobile_Sentry_Gun:
        case EVEDB::invGroups::Deadspace_Overseer_s_Structure:
        case EVEDB::invGroups::Deadspace_Overseer_s_Sentry:
        case EVEDB::invGroups::Deadspace_Overseer_s_Belongings:
        case EVEDB::invGroups::Large_Collidable_Ship:
        case EVEDB::invGroups::Billboard:
        case EVEDB::invGroups::Temporary_Cloud:
        case EVEDB::invGroups::Capture_Point:
        case EVEDB::invGroups::Control_Bunker:
        case EVEDB::invGroups::Mission_Container:
            return false;
    }
    return true;
}

uint32 DungeonMgr::SpawnInstance(const Dungeon::SpawnParams& params, CosmicSignature* sigOut/*nullptr*/)
{
    if (!m_initalized)
        return 0;

    uint32 dungeonID(params.dungeonID);
    if (dungeonID == 0)
        dungeonID = sDunDataMgr.PickDungeon(params.archetype, params.factionID, m_system->GetSystemSecurityRating());
    std::shared_ptr<const Dungeon::Dungeon> pDungeon(sDunDataMgr.GetDungeon(dungeonID));
    if ((pDungeon.get() == nullptr) or pDungeon->roomOrder.empty()) {
        _log(DUNG__WARNING, "DungeonMgr::SpawnInstance() - no usable dungeon (id %u, archetype %u) in %s(%u)",
                dungeonID, params.archetype, m_system->GetName(), m_system->GetID());
        return 0;
    }
    if (pDungeon->roomOrder.size() > 255) {
        _log(DUNG__ERROR, "DungeonMgr::SpawnInstance() - dungeon %u has too many rooms", dungeonID);
        return 0;
    }

    Instance inst = Instance();
        inst.dungeon = pDungeon;
        inst.factionID = ResolveFaction(params, *pDungeon);
        inst.kind = params.kind;
        inst.ownerID = params.ownerID;
        inst.escalationID = params.escalationID;
        inst.name = sDunDataMgr.GetDisplayName(*pDungeon, inst.factionID);
        inst.listener = params.listener;
        inst.created = Win32TimeNow();

    GPoint origin(params.usePosition ? params.position : m_anomMgr->GetSitePosition());

    // anomalies and signatures have a scanner item at the site.  everything else is reached by other means
    bool scannable((params.kind == Dungeon::Kind::Anomaly) or (params.kind == Dungeon::Kind::Signature));
    if (scannable) {
        CosmicSignature sig = CosmicSignature();
        if (sigOut != nullptr)
            sig = *sigOut;
        if (sig.sigTypeID == 0) {
            if (params.kind == Dungeon::Kind::Anomaly) {
                sig.sigTypeID = EVEDB::invTypes::CosmicAnomaly;
                sig.sigGroupID = EVEDB::invGroups::Cosmic_Anomaly;
            } else {
                sig.sigTypeID = EVEDB::invTypes::DeadspaceSignature;
                sig.sigGroupID = EVEDB::invGroups::Cosmic_Signature;
            }
        }
        sig.systemID = m_system->GetID();
        sig.position = origin;
        sig.ownerID = inst.factionID;
        sig.sigName = inst.name;
        if (sig.dungeonType == 0)
            sig.dungeonType = pDungeon->archetypeID;

        ItemData iData(sig.sigTypeID, inst.factionID, m_system->GetID(), flagNone, inst.name.c_str(), origin, "livedungeon");
        InventoryItemRef iRef(sItemFactory.SpawnItem(iData));
        if (iRef.get() == nullptr) {
            _log(DUNG__ERROR, "DungeonMgr::SpawnInstance() - unable to spawn the signature item for %s", inst.name.c_str());
            return 0;
        }
        DBSystemDynamicEntity entity = DBSystemDynamicEntity();
            entity.itemID = iRef->itemID();
            entity.itemName = inst.name;
            entity.typeID = iRef->typeID();
            entity.groupID = iRef->groupID();
            entity.categoryID = iRef->categoryID();
            entity.ownerID = inst.factionID;
            entity.factionID = inst.factionID;
            entity.position = origin;
        // the signal is added by AnomalyMgr when this returns
        if (!m_system->BuildDynamicEntity(entity, 0, false)) {
            iRef->Delete();
            return 0;
        }
        SystemEntity* pSE(m_system->GetSE(iRef->itemID()));
        sig.sigItemID = iRef->itemID();
        sig.bubbleID = ((pSE != nullptr) and (pSE->SysBubble() != nullptr)) ? pSE->SysBubble()->GetID() : 0;
        inst.instanceID = sig.sigItemID;
        inst.sig = sig;
        inst.hasSignal = (sigOut != nullptr);
        if (sigOut != nullptr)
            *sigOut = sig;
    } else {
        inst.instanceID = sItemFactory.GetNextTempID();
    }

    // lifetime:  the caller's, the dungeon's, or the config default of its kind (0 = forever)
    int64 lifetime(params.lifetimeSec * EvE::Time::Second);
    if (lifetime == 0) {
        if (pDungeon->lifetimeHours > 0) {
            lifetime = pDungeon->lifetimeHours * EvE::Time::Hour;
        } else if (params.kind == Dungeon::Kind::Anomaly) {
            lifetime = sConfig.dungeon.AnomalyLifetime * EvE::Time::Hour;
        } else if (params.kind == Dungeon::Kind::Signature) {
            lifetime = sConfig.dungeon.SignatureLifetime * EvE::Time::Hour;
            lifetime = static_cast<int64>(lifetime * MakeRandomFloat(0.75, 1.25));
        }
    }
    inst.expires = (lifetime > 0 ? inst.created + lifetime : 0);

    // rooms:  room 0 at the site, the others 1/3 AU apart
    uint8 idx(0);
    for (auto roomID : pDungeon->roomOrder) {
        auto rItr = pDungeon->rooms.find(roomID);
        if (rItr == pDungeon->rooms.end())
            continue;
        LiveRoom room = LiveRoom();
            room.roomID = roomID;
            room.def = &rItr->second;
            room.origin = (idx == 0 ? origin : NextRoomOrigin(inst.rooms.back().origin));
        for (auto& group : rItr->second.groups) {
            LiveGroup live = LiveGroup();
                live.liveGroupID = m_nextLiveGroupID++;
                live.def = &group;
            room.groups.push_back(live);
        }
        for (auto& trigger : rItr->second.triggers)
            if (trigger.triggerType == Dungeon::Trigger::Attacked)
                inst.hasAttackTriggers = true;
        inst.rooms.push_back(room);
        ++idx;
    }

    uint32 instanceID(inst.instanceID);
    m_instances[instanceID] = inst;
    Instance& live = m_instances[instanceID];

    // objects of every room now (gates must exist before anyone arrives).  room 0's npcs now, the others' when first reached
    for (uint8 i = 0; i < live.rooms.size(); ++i) {
        LiveRoom& room = live.rooms[i];
        // npc behaviour (no warp out, leash) and SpawnMgr bookkeeping key off the room bubble
        SystemBubble* pBubble(sBubbleMgr.GetBubble(m_system, room.origin));
        if (pBubble != nullptr) {
            if (live.kind == Dungeon::Kind::Mission) {
                pBubble->SetMission();
            } else if (live.kind == Dungeon::Kind::Incursion) {
                pBubble->SetIncursion();
            } else {
                pBubble->SetAnomaly();
            }
        }
        for (auto& object : room.def->objects) {
            GPoint pos(room.origin + GPoint(object.x, object.y, object.z));
            if (IsNPCType(object.typeID)) {
                // an npc placed in the room is a one-ship group present from the start
                uint32 itemID(SpawnNPC(live, i, object.typeID, live.factionID, pos, false, 0));
                if (itemID != 0)
                    room.extraNPCs.insert(itemID);
                continue;
            }
            SpawnObject(live, i, object.typeID, pos, object.objectID, object.radius);
        }
    }
    PopulateRoom(live, 0);

    _log(DUNG__INFO, "DungeonMgr::SpawnInstance() - %s (dungeon %u, kind %u, faction %u) spawned as instance %u in %s(%u) with %u rooms",
            live.name.c_str(), dungeonID, live.kind, live.factionID, instanceID, m_system->GetName(), m_system->GetID(), live.rooms.size());
    return instanceID;
}

bool DungeonMgr::MakeDungeon(CosmicSignature& sig, uint32 dungeonID)
{
    Dungeon::SpawnParams params;
        params.dungeonID = dungeonID;
        params.archetype = sig.dungeonType;
        params.factionID = sig.ownerID;
        params.usePosition = true;
        params.position = sig.position;
    if (IsCharacterID(sig.ownerID)) {
        // dungeon editor playtest
        params.kind = Dungeon::Kind::Test;
        params.factionID = 0;
        params.ownerID = sig.ownerID;
        return (SpawnInstance(params) != 0);
    }
    params.kind = ((sig.dungeonType == Dungeon::Type::Anomaly) ? Dungeon::Kind::Anomaly : Dungeon::Kind::Signature);
    return (SpawnInstance(params, &sig) != 0);
}

uint32 DungeonMgr::SpawnObject(Instance& inst, uint8 roomIdx, uint32 typeID, const GPoint& pos, uint32 objectID, double radius,
                               uint32 liveGroupID/*0*/)
{
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr) {
        _log(DUNG__WARNING, "DungeonMgr::SpawnObject() - invalid type %u in dungeon %u", typeID, inst.dungeon->dungeonID);
        return 0;
    }
    LiveRoom& room = inst.rooms[roomIdx];

    // ore and gas are minable rocks of a temp belt per room.  the object's radius field holds the quantity
    if ((pType->categoryID() == EVEDB::invCategories::Asteroid) or (pType->groupID() == EVEDB::invGroups::Harvestable_Cloud)) {
        if (room.beltID == 0) {
            room.beltID = sItemFactory.GetNextTempID();
            uint32 instanceID(inst.instanceID);
            m_system->GetBeltMgr()->SetMinedCallback(room.beltID, [this, instanceID, roomIdx](AsteroidSE* pASE, double units, bool depleted) {
                Ctx ctx;
                    ctx.entityID = pASE->GetID();
                    ctx.typeID = pASE->GetTypeID();
                    ctx.pos = pASE->GetPosition();
                QueueTrigger(instanceID, roomIdx, Dungeon::Trigger::Mined, ctx);
                if (depleted)
                    QueueTrigger(instanceID, roomIdx, Dungeon::Trigger::RoomMinedOut, ctx);
            });
        }
        double quantity(radius > 0 ? radius : 10000);
        AsteroidSE* pASE(m_system->GetBeltMgr()->SpawnResource(room.beltID, typeID, quantity, pos));
        if (pASE == nullptr)
            return 0;
        room.asteroids.insert(pASE->GetID());
        if (objectID != 0)
            room.objects[objectID] = pASE->GetID();
        Index(pASE->GetID(), inst.instanceID, roomIdx);
        return pASE->GetID();
    }

    uint32 ownerID(sDataMgr.GetFactionCorp(inst.factionID));
    if (ownerID == 0)
        ownerID = ownerSystem;
    std::string info("livedungeon_" + std::to_string(inst.instanceID));
    ItemData iData(typeID, ownerID, m_system->GetID(), flagNone, pType->name().c_str(), pos, info.c_str());
    InventoryItemRef iRef(sItemFactory.SpawnItem(iData));
    if (iRef.get() == nullptr) {
        _log(DUNG__ERROR, "DungeonMgr::SpawnObject() - unable to spawn %s(%u) for %s", pType->name().c_str(), typeID, inst.name.c_str());
        return 0;
    }
    iRef->SaveItem();

    DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.itemID = iRef->itemID();
        entity.itemName = iRef->itemName();
        entity.typeID = typeID;
        entity.groupID = pType->groupID();
        entity.categoryID = pType->categoryID();
        entity.ownerID = ownerID;
        entity.corporationID = ownerID;
        entity.allianceID = inst.factionID;
        entity.factionID = ((inst.factionID == factionRogueDrones) ? 0 : inst.factionID);
        entity.position = pos;

    uint32 itemID(iRef->itemID());
    // a gate's state must exist before its entity does (its slim item is sent when it enters its bubble).
    //  its rules come from the instance's own copy of the dungeon, which a reload of the dungeon doesn't touch
    if (pType->groupID() == EVEDB::invGroups::Warp_Gate) {
        auto gItr = inst.dungeon->gates.find(objectID);
        LiveGate gate = LiveGate();
            gate.instanceID = inst.instanceID;
            gate.roomIdx = roomIdx;
            gate.objectID = objectID;
            gate.def = ((gItr != inst.dungeon->gates.end()) ? &gItr->second : nullptr);
            gate.open = ((gate.def == nullptr) or !gate.def->locked);
            gate.openUntil = (gate.open ? Win32TimeNow() : 0);
            gate.shownOpen = (gate.open and ((gate.def == nullptr) or !gate.def->requireClear));
        m_gates[itemID] = gate;
    }

    // dungeon objects don't show on the scanner
    if (!m_system->BuildDynamicEntity(entity, 0, false)) {
        m_gates.erase(itemID);
        iRef->Delete();
        return 0;
    }

    if (objectID != 0) {
        room.objects[objectID] = itemID;
    } else if (liveGroupID == 0) {
        room.extraObjects.insert(itemID);
    }   // a spawn group's objects are its members (LiveGroup.alive)
    Index(itemID, inst.instanceID, roomIdx);

    /* a sentry defends the pocket (npc ai plan 4.11):  a pirate gun even when it's an empire type (group 99), and a member of
     *  its spawn group's aggro group or of its room's (group and room aggro reach it, it alerts them) */
    SystemEntity* pSE(m_system->GetSE(itemID));
    if ((pSE != nullptr) and pSE->IsSentrySE()) {
        Sentry* pSentry(pSE->GetSentrySE());
        pSentry->SetLawSentry(false);
        const Dungeon::SpawnGroupDef* pDef(nullptr);
        pSentry->GetAIMgr()->SetGroup(GetAggroGroup(inst, roomIdx, liveGroupID, pDef));
    }
    return itemID;
}

uint32 DungeonMgr::SpawnNPC(Instance& inst, uint8 roomIdx, uint32 typeID, uint32 factionID, const GPoint& pos, bool warpIn, uint32 liveGroupID)
{
    NPC* pNPC(m_spawnMgr->SpawnDungeonNPC(sBubbleMgr.GetBubble(m_system, pos), typeID, factionID, pos, warpIn, (liveGroupID != 0 ? liveGroupID : inst.instanceID)));
    if (pNPC == nullptr)
        return 0;
    // its bubble may be new (the room's empty bubble was dropped meanwhile).  npc ai keys off these flags
    SystemBubble* pBubble(pNPC->SysBubble());
    if (pBubble != nullptr) {
        if (inst.kind == Dungeon::Kind::Mission) {
            pBubble->SetMission();
        } else if (inst.kind == Dungeon::Kind::Incursion) {
            pBubble->SetIncursion();
        } else {
            pBubble->SetAnomaly();
        }
    }
    Index(pNPC->GetID(), inst.instanceID, roomIdx);

    // its aggro group (npc ai plan 4.3, 6.5):  the live group, or the room's placed npcs
    const Dungeon::SpawnGroupDef* pDef(nullptr);
    pNPC->GetAIMgr()->SetGroup(GetAggroGroup(inst, roomIdx, liveGroupID, pDef));
    /* a wave's first pick may be the weakest ship (4.3).  waves are the trigger-spawned groups (waveNumber isn't usable:
     *  the anomaly seed numbers its first wave 1, the mission builder 0) */
    if ((pDef != nullptr) and (pDef->spawnMode == Dungeon::SpawnMode::TriggerOnly))
        pNPC->GetAIMgr()->SetWave();
    return pNPC->GetID();
}

std::shared_ptr<AggroGroup> DungeonMgr::GetAggroGroup(Instance& inst, uint8 roomIdx, uint32 liveGroupID, const Dungeon::SpawnGroupDef*& pDef)
{
    /* the live group (with the group's aggro mode, range and link), or the room's placed npcs and sentries.  rooms are one
     *  group key per instance and room;  links count inside the instance only */
    AggroGroupRegistry& groups(m_spawnMgr->GetAggroGroups());
    uint64_t roomKey((static_cast<uint64_t>(inst.instanceID) << 8) | roomIdx);
    pDef = nullptr;
    if (liveGroupID != 0)
        for (auto& group : inst.rooms[roomIdx].groups)
            if (group.liveGroupID == liveGroupID)
                pDef = group.def;
    if (pDef == nullptr)
        return groups.Get(NpcAI::GroupKind::DungeonRoom, roomKey, NpcAI::AggroMode::Proactive, 0, 0, roomKey);
    uint64_t linkKey((pDef->aggroLink != 0) ? ((static_cast<uint64_t>(inst.instanceID) << 32) | pDef->aggroLink) : 0);
    return groups.Get(NpcAI::GroupKind::Dungeon, liveGroupID,
            (pDef->aggroMode == Dungeon::AggroMode::Passive ? NpcAI::AggroMode::Passive : NpcAI::AggroMode::Proactive),
            pDef->aggroRange, linkKey, roomKey);
}

void DungeonMgr::PopulateRoom(Instance& inst, uint8 roomIdx)
{
    LiveRoom& room = inst.rooms[roomIdx];
    if (room.populated)
        return;
    room.populated = true;
    for (auto& group : room.groups)
        if (!group.spawned and (group.def->spawnMode == Dungeon::SpawnMode::RoomCreated))
            SpawnGroup(inst, roomIdx, group);
}

void DungeonMgr::SpawnGroup(Instance& inst, uint8 roomIdx, LiveGroup& group)
{
    if (group.spawned)
        return;
    group.spawned = true;
    LiveRoom& room = inst.rooms[roomIdx];
    const Dungeon::SpawnGroupDef& def = *group.def;
    uint32 factionID(def.factionID != 0 ? def.factionID : inst.factionID);

    std::vector<::SpawnGroup> types;
    if (!def.types.empty()) {
        for (auto& cur : def.types) {
            ::SpawnGroup entry = ::SpawnGroup();
                entry.typeID = cur.first;
                entry.quantity = cur.second;
            types.push_back(entry);
        }
    } else if (def.npcClassType != 0) {
        m_spawnMgr->ResolveClassTypes(factionID, def.npcClassType, def.npcClassSub, types);
    }

    GPoint center(room.origin + GPoint(def.x, def.y, def.z));
    for (auto& cur : types)
        for (uint16 i = 0; i < cur.quantity; ++i) {
            GPoint pos(center);
            pos.MakeRandomPointOnSphere(MakeRandomFloat(0.0, def.spread));
            /* spawn groups hold sentries too (about 350 rows of groups 99 and 383):  they're objects (Sentry, npc ai plan 6.6),
             *  not npcs, placed where they stand.  still members of the group (its waves and completion wait for them) */
            uint32 itemID(IsNPCType(cur.typeID)
                          ? SpawnNPC(inst, roomIdx, cur.typeID, factionID, pos, def.warpIn, group.liveGroupID)
                          : SpawnObject(inst, roomIdx, cur.typeID, pos, 0, 0, group.liveGroupID));
            if (itemID != 0)
                group.alive.insert(itemID);
        }

    _log(DUNG__MESSAGE, "DungeonMgr::SpawnGroup() - %s room %u: group %u (wave %u) spawned %u npcs", inst.name.c_str(), room.roomID,
            def.spawnGroupID, def.waveNumber, group.alive.size());

    // nothing to spawn counts as spawned and destroyed, so the waves go on
    if (group.alive.empty()) {
        group.destroyed = true;
        Ctx ctx;
            ctx.liveGroupID = group.liveGroupID;
            ctx.groupWiped = true;
            ctx.pos = center;
        QueueTrigger(inst.instanceID, roomIdx, Dungeon::Trigger::Exploding, ctx);
    }
}

DungeonMgr::LiveGroup* DungeonMgr::FindGroup(Instance& inst, uint32 spawnGroupID, uint8& roomIdx)
{
    for (uint8 i = 0; i < inst.rooms.size(); ++i)
        for (auto& group : inst.rooms[i].groups)
            if (group.def->spawnGroupID == spawnGroupID) {
                roomIdx = i;
                return &group;
            }
    return nullptr;
}

/* removal */

void DungeonMgr::RemoveEntity(uint32 itemID, bool explode)
{
    SystemEntity* pSE(m_system->GetSE(itemID));
    if (pSE == nullptr) {
        // already gone (mined out, scooped, removed elsewhere)
        m_entityIndex.erase(itemID);
        return;
    }
    if (pSE->IsDead())
        return;

    if (explode) {
        // a real kill (wreck, Exploding triggers through OnEntityDestroyed)
        Damage d(pSE, true);
        d.weaponRef = pSE->GetSelf();
        SystemEntity::SetUnkillable(itemID, false);
        pSE->Destroy(d);
        return;
    }

    m_entityIndex.erase(itemID);
    if (pSE->IsNPCSE()) {
        NPC* pNPC(pSE->GetNPCSE());
        if (pNPC->TargetMgr() != nullptr)
            pNPC->TargetMgr()->ClearFromTargets();
        m_system->RemoveNPC(pNPC);
        delete pNPC;
        return;
    }
    if (pSE->IsContainerSE()) {
        // the container item takes its entity out of space and deletes its contents
        InventoryItemRef iRef(pSE->GetSelf());
        iRef->Delete();
    } else {
        pSE->Delete();
    }
    SafeDelete(pSE);
}

uint32 DungeonMgr::CountLiveNPCs(const LiveRoom& room)
{
    uint32 count(room.extraNPCs.size());
    for (auto& group : room.groups)
        count += group.alive.size();
    return count;
}

void DungeonMgr::DespawnInstance(uint32 instanceID, bool notifyAnomMgr/*true*/)
{
    auto itr = m_instances.find(instanceID);
    if (itr == m_instances.end())
        return;
    Instance& inst = itr->second;

    _log(DUNG__INFO, "DungeonMgr::DespawnInstance() - removing %s (instance %u) from %s(%u)", inst.name.c_str(), instanceID, m_system->GetName(), m_system->GetID());

    for (auto& room : inst.rooms) {
        if (room.beltID != 0)
            m_system->GetBeltMgr()->ClearMinedCallback(room.beltID);
        std::set<uint32> items(room.extraNPCs);
        items.insert(room.extraObjects.begin(), room.extraObjects.end());
        items.insert(room.asteroids.begin(), room.asteroids.end());
        for (auto& cur : room.objects)
            items.insert(cur.second);
        for (auto& group : room.groups)
            items.insert(group.alive.begin(), group.alive.end());
        for (auto cur : items) {
            m_gates.erase(cur);
            RemoveEntity(cur, false);
        }
    }
    // anything else still indexed for this instance (warped away npcs)
    for (auto eItr = m_entityIndex.begin(); eItr != m_entityIndex.end(); ) {
        if (eItr->second.first == instanceID) {
            uint32 itemID(eItr->first);
            ++eItr;
            RemoveEntity(itemID, false);
            continue;
        }
        ++eItr;
    }

    if (inst.sig.sigItemID != 0) {
        if (inst.hasSignal) {
            if (notifyAnomMgr) {
                m_anomMgr->OnSiteCompleted(inst.sig);
            } else {
                m_anomMgr->RemoveSignal(inst.sig.sigItemID);
            }
        }
        SystemEntity* pSE(m_system->GetSE(inst.sig.sigItemID));
        if (pSE != nullptr) {
            pSE->Delete();
            SafeDelete(pSE);
        } else {
            InventoryItemRef iRef(sItemFactory.GetItemRef(inst.sig.sigItemID));
            if (iRef.get() != nullptr)
                iRef->Delete();
        }
    }

    Ctx ctx;
    Notify(inst, Dungeon::Engine::Despawned, 0, ctx);
    m_instances.erase(itr);
}

void DungeonMgr::Close()
{
    // the items of live dungeons don't survive the system.  their entities are torn down here, before the system unload
    std::vector<uint32> ids;
    for (auto& cur : m_instances)
        ids.push_back(cur.first);
    for (auto id : ids)
        DespawnInstance(id, false);
    // nobody gets called after this
    FlushNotices();
    m_pending.clear();
    m_delayed.clear();
    m_returns.clear();
    m_warpedAway.clear();
    m_gates.clear();
    m_entityIndex.clear();
}

void DungeonMgr::ClearListener(Dungeon::DungeonListener* pListener)
{
    for (auto& cur : m_instances)
        if (cur.second.listener == pListener)
            cur.second.listener = nullptr;
    for (auto& cur : m_notices)
        if (cur.first == pListener)
            cur.first = nullptr;
}

/* queries */

uint32 DungeonMgr::GetInstanceOf(uint32 itemID)
{
    auto itr = m_entityIndex.find(itemID);
    if (itr == m_entityIndex.end())
        return 0;
    return itr->second.first;
}

bool DungeonMgr::IsInDungeon(const GPoint& pos)
{
    for (auto& cur : m_instances)
        for (auto& room : cur.second.rooms)
            if (room.origin.distance(pos) < BUBBLE_RADIUS_METERS)
                return true;
    return false;
}

bool DungeonMgr::IsDeadspace(const GPoint& pos)
{
    for (auto& cur : m_instances)
        for (uint8 i = 0; i < cur.second.rooms.size(); ++i) {
            if (cur.second.rooms[i].origin.distance(pos) >= BUBBLE_RADIUS_METERS)
                continue;
            if ((i > 0) or (cur.second.kind != Dungeon::Kind::Anomaly))
                return true;
        }
    return false;
}

bool DungeonMgr::RedirectWarp(const GPoint& dest, GPoint& into)
{
    for (auto& cur : m_instances)
        for (uint8 i = 1; i < cur.second.rooms.size(); ++i)
            if (cur.second.rooms[i].origin.distance(dest) < BUBBLE_RADIUS_METERS) {
                into = cur.second.rooms[0].origin;
                return true;
            }
    return false;
}

/* hooks.  these only queue triggers;  Process() runs them */

void DungeonMgr::QueueTrigger(uint32 instanceID, uint8 roomIdx, uint16 triggerType, const Ctx& ctx)
{
    PendingTrigger pending;
        pending.instanceID = instanceID;
        pending.roomIdx = roomIdx;
        pending.triggerType = triggerType;
        pending.ctx = ctx;
    m_pending.push_back(pending);
}

uint32 DungeonMgr::FindWreck(uint32 deadItemID, const GPoint& pos)
{
    SystemBubble* pBubble(sBubbleMgr.FindBubble(m_system->GetID(), pos));
    if (pBubble == nullptr)
        return 0;
    std::map<uint32, SystemEntity*> entities;
    pBubble->GetEntities(entities);
    for (auto& cur : entities)
        if (cur.second->IsWreckSE() and (cur.second->GetWreckSE()->GetLaunchedByID() == deadItemID))
            return cur.first;
    return 0;
}

void DungeonMgr::DropLoot(uint32 lootGroupID, uint32 wreckID, const GPoint& pos, uint32 ownerID)
{
    std::vector<Dungeon::LootEntry> loot;
    sDunDataMgr.GetLoot(lootGroupID, loot);
    if (loot.empty())
        return;
    if (ownerID == 0)
        ownerID = ownerSystem;

    InventoryItemRef containerRef(nullptr);
    if (wreckID != 0)
        containerRef = sItemFactory.GetWreckContainer(wreckID);
    for (auto& cur : loot) {
        if (MakeRandomFloat() >= cur.chance)
            continue;
        uint32 quantity(cur.minQty);
        if (cur.maxQty > cur.minQty)
            quantity = std::min<uint32>(MakeRandomInt(cur.minQty, cur.maxQty + 1), cur.maxQty);
        if (quantity == 0)
            continue;
        if (containerRef.get() == nullptr) {
            // no wreck:  a cargo container at the spot
            ItemData cData(EVEDB::invTypes::CargoContainer, ownerID, m_system->GetID(), flagNone, "Cargo Container", pos);
            CargoContainerRef cargoRef(sItemFactory.SpawnCargoContainer(cData));
            if (cargoRef.get() == nullptr)
                return;
            DBSystemDynamicEntity entity = DBSystemDynamicEntity();
                entity.itemID = cargoRef->itemID();
                entity.itemName = cargoRef->itemName();
                entity.typeID = cargoRef->typeID();
                entity.groupID = cargoRef->groupID();
                entity.categoryID = cargoRef->categoryID();
                entity.ownerID = ownerID;
                entity.position = pos;
            if (!m_system->BuildDynamicEntity(entity)) {
                cargoRef->Delete();
                return;
            }
            containerRef = cargoRef;
        }
        ItemData iLoot(cur.typeID, ownerID, containerRef->itemID(), flagNone, quantity);
        InventoryItemRef iRef(sItemFactory.SpawnItem(iLoot));
        if (iRef.get() == nullptr) {
            _log(DUNG__WARNING, "DungeonMgr::DropLoot() - unable to spawn type %u of loot group %u", cur.typeID, lootGroupID);
            continue;
        }
        containerRef->AddItem(iRef);
    }
}

void DungeonMgr::OnEntityDestroyed(SystemEntity* pSE, Damage& d)
{
    auto iItr = m_entityIndex.find(pSE->GetID());
    if (iItr == m_entityIndex.end())
        return;
    uint32 instanceID(iItr->second.first);
    uint8 roomIdx(iItr->second.second);
    m_entityIndex.erase(iItr);
    auto itr = m_instances.find(instanceID);
    if (itr == m_instances.end())
        return;
    Instance& inst = itr->second;
    LiveRoom& room = inst.rooms[roomIdx];

    Ctx ctx;
        ctx.entityID = pSE->GetID();
        ctx.typeID = pSE->GetTypeID();
        ctx.pos = pSE->GetPosition();
    // the killer:  a pilot, or a drone's owner
    if (d.srcSE != nullptr) {
        if (d.srcSE->HasPilot()) {
            ctx.charID = d.srcSE->GetPilot()->GetCharacterID();
        } else if (d.srcSE->IsDroneSE() and IsCharacterID(d.srcSE->GetSelf()->ownerID())) {
            ctx.charID = d.srcSE->GetSelf()->ownerID();
        }
    }
    if (ctx.charID != 0)
        inst.lastCharID = ctx.charID;

    for (auto oItr = room.objects.begin(); oItr != room.objects.end(); ++oItr)
        if (oItr->second == ctx.entityID) {
            ctx.objectID = oItr->first;
            room.objects.erase(oItr);
            break;
        }
    room.extraNPCs.erase(ctx.entityID);
    room.extraObjects.erase(ctx.entityID);
    room.asteroids.erase(ctx.entityID);
    m_gates.erase(ctx.entityID);
    for (auto& group : room.groups) {
        if (group.alive.erase(ctx.entityID) == 0)
            continue;
        ctx.liveGroupID = group.liveGroupID;
        if (group.alive.empty()) {
            group.destroyed = true;
            ctx.groupWiped = true;
        }
        // the group's loot goes into each member's wreck (the wreck exists by now)
        if (group.def->lootGroupID != 0)
            DropLoot(group.def->lootGroupID, FindWreck(ctx.entityID, ctx.pos), ctx.pos, ctx.charID);
        break;
    }

    QueueTrigger(instanceID, roomIdx, Dungeon::Trigger::Exploding, ctx);
}

void DungeonMgr::OnRoomAggression(SystemEntity* pSE, SystemEntity* pAttacker)
{
    /* "attacking such structures causes all ships in the pocket to target and attack you" (npc ai plan 4.3):  an overseer
     *  structure or a sentry of a room was shot.  every aggro group of that room aggroes (even if the shot kills it) */
    if ((pSE == nullptr) or (pAttacker == nullptr) or (!pAttacker->IsShipSE() and !pAttacker->IsDroneSE()))
        return;
    if (!pSE->IsSentrySE() and (pSE->GetGroupID() != EVEDB::invGroups::Deadspace_Overseer_s_Structure)
    and (pSE->GetGroupID() != EVEDB::invGroups::Deadspace_Overseer_s_Sentry))
        return;
    auto iItr = m_entityIndex.find(pSE->GetID());
    if (iItr == m_entityIndex.end())
        return;
    uint64_t roomKey((static_cast<uint64_t>(iItr->second.first) << 8) | iItr->second.second);
    m_spawnMgr->GetAggroGroups().AlertRoom(roomKey, pAttacker);
}

void DungeonMgr::OnEntityAttacked(SystemEntity* pSE, SystemEntity* pAttacker)
{
    auto iItr = m_entityIndex.find(pSE->GetID());
    if (iItr == m_entityIndex.end())
        return;
    auto itr = m_instances.find(iItr->second.first);
    if ((itr == m_instances.end()) or !itr->second.hasAttackTriggers)
        return;
    Ctx ctx;
        ctx.entityID = pSE->GetID();
        ctx.typeID = pSE->GetTypeID();
        ctx.pos = pSE->GetPosition();
    if ((pAttacker != nullptr) and pAttacker->HasPilot())
        ctx.charID = pAttacker->GetPilot()->GetCharacterID();
    for (auto& cur : itr->second.rooms[iItr->second.second].objects)
        if (cur.second == ctx.entityID)
            ctx.objectID = cur.first;
    for (auto& group : itr->second.rooms[iItr->second.second].groups)
        if (group.alive.find(ctx.entityID) != group.alive.end())
            ctx.liveGroupID = group.liveGroupID;
    QueueTrigger(itr->first, iItr->second.second, Dungeon::Trigger::Attacked, ctx);
}

void DungeonMgr::OnPlayerKilled(ShipSE* pShip, uint32 charID)
{
    GPoint pos(pShip->GetPosition());
    for (auto& cur : m_instances)
        for (uint8 i = 0; i < cur.second.rooms.size(); ++i)
            if (cur.second.rooms[i].origin.distance(pos) < BUBBLE_RADIUS_METERS) {
                Ctx ctx;
                    ctx.entityID = pShip->GetID();
                    ctx.typeID = pShip->GetTypeID();
                    ctx.charID = charID;
                    ctx.pos = pos;
                QueueTrigger(cur.first, i, Dungeon::Trigger::PlayerKilled, ctx);
                return;
            }
}

void DungeonMgr::OnObjectEvent(uint32 itemID, uint16 triggerType, uint32 charID, uint32 typeID/*0*/)
{
    Ctx ctx;
        ctx.entityID = itemID;
        ctx.charID = charID;
        ctx.typeID = typeID;
    auto iItr = m_entityIndex.find(itemID);
    if (iItr != m_entityIndex.end()) {
        auto itr = m_instances.find(iItr->second.first);
        if (itr == m_instances.end())
            return;
        for (auto& cur : itr->second.rooms[iItr->second.second].objects)
            if (cur.second == itemID)
                ctx.objectID = cur.first;
        SystemEntity* pSE(m_system->GetSE(itemID));
        if (pSE != nullptr)
            ctx.pos = pSE->GetPosition();
        QueueTrigger(itr->first, iItr->second.second, triggerType, ctx);
        return;
    }

    // not a dungeon object (ItemInCargo names the pilot's ship):  the room the pilot is in
    Client* pClient(sEntityList.FindClientByCharID(charID));
    if ((pClient == nullptr) or (pClient->GetShipSE() == nullptr) or (pClient->SystemMgr() != m_system))
        return;
    ctx.pos = pClient->GetShipSE()->GetPosition();
    for (auto& cur : m_instances)
        for (uint8 i = 0; i < cur.second.rooms.size(); ++i)
            if (cur.second.rooms[i].origin.distance(ctx.pos) < BUBBLE_RADIUS_METERS) {
                QueueTrigger(cur.first, i, triggerType, ctx);
                return;
            }
}

/* triggers and events */

void DungeonMgr::FireTriggers(Instance& inst, uint8 roomIdx, uint16 triggerType, const Ctx& ctx)
{
    LiveRoom& room = inst.rooms[roomIdx];
    const LiveGroup* pGroup(nullptr);
    if (ctx.liveGroupID != 0)
        for (auto& group : room.groups)
            if (group.liveGroupID == ctx.liveGroupID)
                pGroup = &group;

    uint32 instanceID(inst.instanceID);
    for (auto& trigger : room.def->triggers) {
        if (trigger.triggerType != triggerType)
            continue;
        if (!trigger.repeatable and (room.firedTriggers.find(trigger.triggerID) != room.firedTriggers.end()))
            continue;

        // source filters
        if (trigger.spawnGroupID != 0) {
            if ((pGroup == nullptr) or (pGroup->def->spawnGroupID != trigger.spawnGroupID))
                continue;
            // a group's Exploding fires when the whole group is dead (the wave rule), or for each member with value 1
            if ((triggerType == Dungeon::Trigger::Exploding) and (trigger.value < 0.5) and !ctx.groupWiped)
                continue;
        }
        if ((trigger.objectID != 0) and (trigger.objectID != ctx.objectID))
            continue;

        switch (triggerType) {
            case Dungeon::Trigger::Exploding: {
                // an empty group "explodes" without an entity.  only group triggers react to that
                if ((ctx.entityID == 0) and (trigger.spawnGroupID == 0))
                    continue;
            } break;
            case Dungeon::Trigger::Attacked:
            case Dungeon::Trigger::ShieldConditionLevel:
            case Dungeon::Trigger::ArmorConditionLevel:
            case Dungeon::Trigger::StructureConditionLevel: {
                // once per entity
                std::pair<uint32, uint32> key(trigger.triggerID, ctx.entityID);
                if (room.conditionFired.find(key) != room.conditionFired.end())
                    continue;
                room.conditionFired.insert(key);
            } break;
            case Dungeon::Trigger::ItemPlacedInMissionContainer:
            case Dungeon::Trigger::ItemRemovedFromSpawnContainer:
            case Dungeon::Trigger::ItemInCargo: {
                // value names the item type, if set
                if ((trigger.value > 0) and (static_cast<uint32>(trigger.value) != ctx.typeID))
                    continue;
            } break;
            case Dungeon::Trigger::CounterEQ:
            case Dungeon::Trigger::CounterGT:
            case Dungeon::Trigger::CounterGE:
            case Dungeon::Trigger::CounterLT:
            case Dungeon::Trigger::CounterLE: {
                double counter(inst.counters[trigger.counterID]);
                bool hit(false);
                switch (triggerType) {
                    case Dungeon::Trigger::CounterEQ: hit = (std::fabs(counter - trigger.value) < 0.0001); break;
                    case Dungeon::Trigger::CounterGT: hit = (counter > trigger.value); break;
                    case Dungeon::Trigger::CounterGE: hit = (counter >= trigger.value); break;
                    case Dungeon::Trigger::CounterLT: hit = (counter < trigger.value); break;
                    case Dungeon::Trigger::CounterLE: hit = (counter <= trigger.value); break;
                }
                if (!hit)
                    continue;
            } break;
        }

        room.firedTriggers.insert(trigger.triggerID);
        _log(DUNG__TRACE, "DungeonMgr::FireTriggers() - %s room %u:  trigger %u (type %u) fired", inst.name.c_str(), room.roomID, trigger.triggerID, triggerType);

        for (auto& event : trigger.events) {
            if (event.delaySeconds > 0) {
                PendingEvent pending;
                    pending.instanceID = instanceID;
                    pending.roomIdx = roomIdx;
                    pending.event = &event;
                    pending.ctx = ctx;
                m_delayed.emplace(Win32TimeNow() + (event.delaySeconds * EvE::Time::Second), pending);
                continue;
            }
            RunEvent(inst, roomIdx, event, ctx);
            // the instance may be gone (despawned by an event)
            if (m_instances.find(instanceID) == m_instances.end())
                return;
        }
    }
}

void DungeonMgr::CheckCounters(Instance& inst, uint8 roomIdx, uint32 counterID)
{
    // counter triggers anywhere in the dungeon re-check after a change
    uint32 instanceID(inst.instanceID);
    Ctx ctx;
    for (uint8 i = 0; i < inst.rooms.size(); ++i)
        for (uint16 type = Dungeon::Trigger::CounterEQ; type <= Dungeon::Trigger::CounterLE; ++type) {
            FireTriggers(inst, i, type, ctx);
            if (m_instances.find(instanceID) == m_instances.end())
                return;
        }
}

void DungeonMgr::RunEvent(Instance& inst, uint8 roomIdx, const Dungeon::EventDef& event, const Ctx& ctx)
{
    LiveRoom& room = inst.rooms[roomIdx];
    _log(DUNG__TRACE, "DungeonMgr::RunEvent() - %s room %u:  event %u (type %u)", inst.name.c_str(), room.roomID, event.eventID, event.eventType);

    // the entities an event works on:  an object, a group, or the entity that fired the trigger
    auto targets = [&]() -> std::vector<uint32> {
        std::vector<uint32> into;
        if (event.objectID != 0) {
            for (auto& cur : inst.rooms)
                if (cur.objects.find(event.objectID) != cur.objects.end())
                    into.push_back(cur.objects[event.objectID]);
        } else if (event.spawnGroupID != 0) {
            uint8 idx(0);
            LiveGroup* pGroup(FindGroup(inst, event.spawnGroupID, idx));
            if (pGroup != nullptr)
                into.insert(into.end(), pGroup->alive.begin(), pGroup->alive.end());
        } else if ((ctx.entityID != 0) and (m_entityIndex.find(ctx.entityID) != m_entityIndex.end())) {
            into.push_back(ctx.entityID);
        }
        return into;
    };

    switch (event.eventType) {
        case Dungeon::Event::Trigger::ActivateGate: {
            for (auto& cur : m_gates) {
                if ((cur.second.instanceID != inst.instanceID) or (cur.second.roomIdx != roomIdx))
                    continue;
                if ((event.objectID != 0) and (cur.second.objectID != event.objectID))
                    continue;
                OpenGate(cur.first, cur.second, true);
            }
        } break;
        case Dungeon::Event::Trigger::SpawnGuards: {
            uint8 idx(0);
            LiveGroup* pGroup(FindGroup(inst, event.spawnGroupID, idx));
            if (pGroup != nullptr)
                SpawnGroup(inst, idx, *pGroup);
        } break;
        case Dungeon::Event::Trigger::SpawnGuardObject:
        case Dungeon::Event::Trigger::SpawnShip: {
            if (event.typeID == 0)
                break;
            GPoint center(room.origin);
            if (event.objectID != 0) {
                auto oItr = room.objects.find(event.objectID);
                SystemEntity* pSE((oItr != room.objects.end()) ? m_system->GetSE(oItr->second) : nullptr);
                if (pSE != nullptr)
                    center = pSE->GetPosition();
            }
            for (int32 i = 0; i < std::max<int32>(event.quantity, 1); ++i) {
                GPoint pos(center);
                pos.MakeRandomPointOnSphere(MakeRandomFloat(1000, 5000));
                if (IsNPCType(event.typeID)) {
                    uint32 itemID(SpawnNPC(inst, roomIdx, event.typeID, inst.factionID, pos, false, 0));
                    if (itemID != 0)
                        room.extraNPCs.insert(itemID);
                } else {
                    SpawnObject(inst, roomIdx, event.typeID, pos, 0, 0);
                }
            }
        } break;
        case Dungeon::Event::Trigger::Msg: {
            SendTriggerMessage(inst, roomIdx, event);
        } break;
        case Dungeon::Event::Trigger::DungeonCompletion: {
            CompleteInstance(inst, (ctx.charID != 0 ? ctx.charID : inst.lastCharID));
        } break;
        case Dungeon::Event::Trigger::MissionCompletion:
        case Dungeon::Event::Trigger::MissionFailure:
        case Dungeon::Event::Trigger::AgentTalkTo:
        case Dungeon::Event::Trigger::AgentMsg:
        case Dungeon::Event::Trigger::GrantGroupReward:
        case Dungeon::Event::Trigger::GrantDelayedGroupReward:
        case Dungeon::Event::Trigger::GrantGroupRewardLimitedRestrictions:
        case Dungeon::Event::Trigger::AdjustSystemInfluence:
        case Dungeon::Event::Trigger::FacWarVictoryPointsGranted: {
            // missions and incursions handle these
            Notify(inst, event.eventType, room.roomID, ctx, &event);
        } break;
        case Dungeon::Event::Trigger::ObjectDespawn:
        case Dungeon::Event::Trigger::EntityDespawn: {
            for (auto itemID : targets()) {
                ForgetEntity(inst, itemID);
                RemoveEntity(itemID, false);
            }
            CheckImplicitCompletion(inst);
        } break;
        case Dungeon::Event::Trigger::ObjectExplode:
        case Dungeon::Event::Trigger::EntityExplode: {
            for (auto itemID : targets())
                RemoveEntity(itemID, true);
        } break;
        case Dungeon::Event::Trigger::DropLoot: {
            uint32 lootGroupID(event.value > 0 ? static_cast<uint32>(event.value) : event.typeID);
            DropLoot(lootGroupID, FindWreck(ctx.entityID, ctx.pos), (ctx.entityID != 0 ? ctx.pos : room.origin), ctx.charID);
        } break;
        case Dungeon::Event::Trigger::SpawnItemInCargo: {
            Client* pClient(sEntityList.FindClientByCharID(ctx.charID));
            if ((pClient == nullptr) or (event.typeID == 0) or !pClient->IsInSpace() or (pClient->GetShip().get() == nullptr))
                break;
            ItemData iData(event.typeID, ctx.charID, pClient->GetShipID(), flagCargoHold, std::max<int32>(event.quantity, 1));
            InventoryItemRef iRef(sItemFactory.SpawnItem(iData));
            if (iRef.get() != nullptr)
                pClient->GetShip()->AddItemByFlag(flagCargoHold, iRef, pClient);
        } break;
        case Dungeon::Event::Trigger::CounterAdd:       inst.counters[event.counterID] += event.value; CheckCounters(inst, roomIdx, event.counterID); break;
        case Dungeon::Event::Trigger::CounterSubtract:  inst.counters[event.counterID] -= event.value; CheckCounters(inst, roomIdx, event.counterID); break;
        case Dungeon::Event::Trigger::CounterMultiply:  inst.counters[event.counterID] *= event.value; CheckCounters(inst, roomIdx, event.counterID); break;
        case Dungeon::Event::Trigger::CounterDivide: {
            if (event.value != 0)
                inst.counters[event.counterID] /= event.value;
            CheckCounters(inst, roomIdx, event.counterID);
        } break;
        case Dungeon::Event::Trigger::CounterSet:       inst.counters[event.counterID] = event.value; CheckCounters(inst, roomIdx, event.counterID); break;
        case Dungeon::Event::Trigger::WarpShipAwayDespawn:
        case Dungeon::Event::Trigger::WarpShipAwayAndComeBack: {
            for (auto itemID : targets())
                WarpAway(itemID, (event.eventType == Dungeon::Event::Trigger::WarpShipAwayAndComeBack), inst, roomIdx);
        } break;
        case Dungeon::Event::Trigger::SupressAllRespawn: {
            // dungeons never respawn anything
        } break;
        case Dungeon::Event::Trigger::Escalate: {
            TryEscalate(inst, (ctx.charID != 0 ? ctx.charID : inst.lastCharID));
        } break;
        default: {
            // ranged damage/healing, effect beacons (incursions), tutorials
            if (m_unsupportedLogged.insert(event.eventType).second)
                _log(DUNG__WARNING, "DungeonMgr::RunEvent() - event type %u is not supported (dungeon %u, event %u)",
                        event.eventType, inst.dungeon->dungeonID, event.eventID);
        } break;
    }
}

void DungeonMgr::ForgetEntity(Instance& inst, uint32 itemID)
{
    // an entity leaves the instance without dying (despawn, warp away)
    auto iItr = m_entityIndex.find(itemID);
    if (iItr == m_entityIndex.end())
        return;
    LiveRoom& room = inst.rooms[iItr->second.second];
    m_entityIndex.erase(iItr);
    room.extraNPCs.erase(itemID);
    room.extraObjects.erase(itemID);
    room.asteroids.erase(itemID);
    m_gates.erase(itemID);
    for (auto oItr = room.objects.begin(); oItr != room.objects.end(); ++oItr)
        if (oItr->second == itemID) {
            room.objects.erase(oItr);
            break;
        }
    for (auto& group : room.groups)
        if ((group.alive.erase(itemID) > 0) and group.alive.empty())
            group.destroyed = true;
}

void DungeonMgr::WarpAway(uint32 itemID, bool comeBack, Instance& inst, uint8 roomIdx)
{
    SystemEntity* pSE(m_system->GetSE(itemID));
    if ((pSE == nullptr) or !pSE->IsNPCSE() or pSE->IsDead())
        return;
    NPC* pNPC(pSE->GetNPCSE());
    GPoint home(pNPC->GetPosition());

    if (comeBack) {
        // it stays a member of its group while it is away, so the wave isn't over
        uint32 liveGroupID(0);
        for (auto& group : inst.rooms[roomIdx].groups)
            if (group.alive.find(itemID) != group.alive.end())
                liveGroupID = group.liveGroupID;
        ReturnEntry entry;
            entry.instanceID = inst.instanceID;
            entry.roomIdx = roomIdx;
            entry.typeID = pSE->GetTypeID();
            entry.liveGroupID = liveGroupID;
            entry.oldItemID = itemID;
            entry.pos = home;
        m_returns.emplace(Win32TimeNow() + (60 * EvE::Time::Second), entry);
        m_entityIndex.erase(itemID);
        inst.rooms[roomIdx].extraNPCs.erase(itemID);
    } else {
        ForgetEntity(inst, itemID);
        CheckImplicitCompletion(inst);
    }

    // off it goes, 1 AU away.  it is removed once out of sight
    GPoint away(home);
    away.MakeRandomPointOnSphere(ONE_AU_IN_METERS);
    if (pNPC->TargetMgr() != nullptr)
        pNPC->TargetMgr()->ClearTargets();
    pNPC->DestinyMgr()->WarpTo(away, 0);
    m_warpedAway[itemID] = Win32TimeNow() + (20 * EvE::Time::Second);
}

void DungeonMgr::SendTriggerMessage(Instance& inst, uint8 roomIdx, const Dungeon::EventDef& event)
{
    if (!sConfig.dungeon.TriggerMessages)
        return;
    // everyone in the room.  a message from an empty room goes to everyone in the dungeon
    std::set<uint32> chars(inst.rooms[roomIdx].playersInside);
    if (chars.empty())
        for (auto& room : inst.rooms)
            chars.insert(room.playersInside.begin(), room.playersInside.end());
    for (auto charID : chars) {
        Client* pClient(sEntityList.FindClientByCharID(charID));
        if (pClient == nullptr)
            continue;
        if (event.messageID != 0) {
            // the client echoes the localized message to local in the colour of its type
            PyTuple* payload = new PyTuple(2);
                payload->SetItem(0, new PyInt(event.messageType));
                payload->SetItem(1, new PyInt(event.messageID));
            pClient->SendNotification("OnDungeonTriggerMessage", "charid", payload, false);
        } else if (!event.messageText.empty()) {
            pClient->SendNotifyMsg("%s", event.messageText.c_str());
        }
    }
}

void DungeonMgr::Notify(Instance& inst, uint16 type, uint32 roomID, const Ctx& ctx, const Dungeon::EventDef* pEvent/*nullptr*/)
{
    if (inst.listener == nullptr)
        return;
    Dungeon::DungeonEvent ev = Dungeon::DungeonEvent();
        ev.instanceID = inst.instanceID;
        ev.dungeonID = inst.dungeon->dungeonID;
        ev.roomID = roomID;
        ev.type = type;
        ev.entityID = ctx.entityID;
        ev.charID = ctx.charID;
        ev.typeID = ctx.typeID;
    if (pEvent != nullptr) {
        if (pEvent->typeID != 0)
            ev.typeID = pEvent->typeID;
        ev.messageID = pEvent->messageID;
        ev.quantity = pEvent->quantity;
        ev.value = pEvent->value;
    }
    // listeners are called from Process() (they may spawn or despawn instances)
    m_notices.emplace_back(inst.listener, ev);
}

void DungeonMgr::FlushNotices()
{
    while (!m_notices.empty()) {
        std::deque<std::pair<Dungeon::DungeonListener*, Dungeon::DungeonEvent>> notices;
        notices.swap(m_notices);
        for (auto& cur : notices)
            if (cur.first != nullptr)
                cur.first->OnDungeonEvent(cur.second);
    }
}

void DungeonMgr::CheckImplicitCompletion(Instance& inst)
{
    // dungeons without a completion event are done when every room-created/room-entered group was spawned and destroyed
    if (inst.completed or inst.dungeon->hasCompletionEvent)
        return;
    bool any(false);
    for (auto& room : inst.rooms)
        for (auto& group : room.groups) {
            if (group.def->spawnMode == Dungeon::SpawnMode::TriggerOnly)
                continue;
            if (!group.spawned or !group.destroyed)
                return;
            any = true;
        }
    if (any)
        CompleteInstance(inst, inst.lastCharID);
}

void DungeonMgr::CompleteInstance(Instance& inst, uint32 charID)
{
    if (inst.completed)
        return;
    inst.completed = true;
    inst.completedAt = Win32TimeNow();
    if (charID != 0)
        inst.lastCharID = charID;

    _log(DUNG__INFO, "DungeonMgr::CompleteInstance() - %s (instance %u) in %s(%u) completed by %u",
            inst.name.c_str(), inst.instanceID, m_system->GetName(), m_system->GetID(), inst.lastCharID);

    Ctx ctx;
        ctx.charID = inst.lastCharID;
    Notify(inst, Dungeon::Engine::Completed, 0, ctx);

    // automatic escalations of this dungeon
    if ((inst.lastCharID != 0) and (inst.kind != Dungeon::Kind::Mission) and (inst.kind != Dungeon::Kind::Test))
        TryEscalate(inst, inst.lastCharID);

    // a completed site leaves the scanner now and is replaced elsewhere
    if (inst.hasSignal) {
        m_anomMgr->OnSiteCompleted(inst.sig);
        inst.hasSignal = false;
    }

    if (inst.escalationID != 0)
        OnEscalationCompleted(inst);
}

void DungeonMgr::ProcessPresence(Instance& inst, const std::vector<std::pair<uint32, GPoint>>& ships)
{
    uint32 instanceID(inst.instanceID);
    for (uint8 i = 0; i < inst.rooms.size(); ++i) {
        LiveRoom& room = inst.rooms[i];
        std::set<uint32> inside;
        for (auto& cur : ships)
            if (room.origin.distance(cur.second) < BUBBLE_RADIUS_METERS)
                inside.insert(cur.first);

        bool hadPlayers(!room.playersInside.empty());
        for (auto charID : inside) {
            if (room.playersInside.find(charID) != room.playersInside.end())
                continue;
            // the new arrival is in the room for its own triggers (their messages go to the room)
            room.playersInside.insert(charID);
            Ctx ctx;
                ctx.charID = charID;
            if (!room.entered) {
                room.entered = true;
                PopulateRoom(inst, i);
                for (auto& group : room.groups)
                    if (!group.spawned and (group.def->spawnMode == Dungeon::SpawnMode::RoomEntered))
                        SpawnGroup(inst, i, group);
                Notify(inst, Dungeon::Engine::RoomEntered, room.roomID, ctx);
                FireTriggers(inst, i, Dungeon::Trigger::RoomEntered, ctx);
                if (m_instances.find(instanceID) == m_instances.end())
                    return;
            }
            OnVisitor(inst, i, charID);
            FireTriggers(inst, i, Dungeon::Trigger::ShipsEnteredRoom, ctx);
            if (m_instances.find(instanceID) == m_instances.end())
                return;
        }
        room.playersInside = inside;
        if (hadPlayers and inside.empty()) {
            Ctx ctx;
            FireTriggers(inst, i, Dungeon::Trigger::ShipsLeftRoom, ctx);
            if (m_instances.find(instanceID) == m_instances.end())
                return;
        }

        // proximity of ships to a room object
        for (auto& trigger : room.def->triggers) {
            if ((trigger.triggerType != Dungeon::Trigger::ShipEnteredProximity) and (trigger.triggerType != Dungeon::Trigger::ShipLeftProximity))
                continue;
            auto oItr = room.objects.find(trigger.objectID);
            SystemEntity* pSE((oItr != room.objects.end()) ? m_system->GetSE(oItr->second) : nullptr);
            GPoint center(pSE != nullptr ? pSE->GetPosition() : room.origin);
            std::set<uint32>& before = room.proximity[trigger.triggerID];
            std::set<uint32> now;
            for (auto& cur : ships)
                if ((inside.find(cur.first) != inside.end()) and (center.distance(cur.second) <= trigger.value))
                    now.insert(cur.first);
            for (auto charID : now)
                if ((before.find(charID) == before.end()) and (trigger.triggerType == Dungeon::Trigger::ShipEnteredProximity)) {
                    Ctx ctx;
                        ctx.charID = charID;
                        ctx.objectID = trigger.objectID;
                    FireTriggers(inst, i, Dungeon::Trigger::ShipEnteredProximity, ctx);
                    if (m_instances.find(instanceID) == m_instances.end())
                        return;
                }
            for (auto charID : before)
                if ((now.find(charID) == now.end()) and (trigger.triggerType == Dungeon::Trigger::ShipLeftProximity)) {
                    Ctx ctx;
                        ctx.charID = charID;
                        ctx.objectID = trigger.objectID;
                    FireTriggers(inst, i, Dungeon::Trigger::ShipLeftProximity, ctx);
                    if (m_instances.find(instanceID) == m_instances.end())
                        return;
                }
            room.proximity[trigger.triggerID] = now;
        }
    }
}

void DungeonMgr::ProcessConditions(Instance& inst, uint8 roomIdx)
{
    LiveRoom& room = inst.rooms[roomIdx];
    uint32 instanceID(inst.instanceID);
    for (auto& trigger : room.def->triggers) {
        uint16 attrID(0), maxID(0);
        bool damage(true);
        switch (trigger.triggerType) {
            case Dungeon::Trigger::ShieldConditionLevel:    attrID = AttrShieldCharge; maxID = AttrShieldCapacity; damage = false; break;
            case Dungeon::Trigger::ArmorConditionLevel:     attrID = AttrArmorDamage; maxID = AttrArmorHP; break;
            case Dungeon::Trigger::StructureConditionLevel: attrID = AttrDamage; maxID = AttrHP; break;
            default: continue;
        }
        std::vector<uint32> sources;
        if (trigger.objectID != 0) {
            auto oItr = room.objects.find(trigger.objectID);
            if (oItr != room.objects.end())
                sources.push_back(oItr->second);
        } else if (trigger.spawnGroupID != 0) {
            for (auto& group : room.groups)
                if (group.def->spawnGroupID == trigger.spawnGroupID)
                    sources.insert(sources.end(), group.alive.begin(), group.alive.end());
        }
        for (auto itemID : sources) {
            SystemEntity* pSE(m_system->GetSE(itemID));
            if ((pSE == nullptr) or pSE->IsDead())
                continue;
            InventoryItemRef iRef(pSE->GetSelf());
            double max(iRef->GetAttribute(maxID).get_double());
            if (max <= 0)
                continue;
            double cur(iRef->GetAttribute(attrID).get_double());
            double fraction(damage ? 1.0 - (cur / max) : cur / max);
            if (fraction > trigger.value)
                continue;
            Ctx ctx;
                ctx.entityID = itemID;
                ctx.typeID = pSE->GetTypeID();
                ctx.objectID = trigger.objectID;
                ctx.pos = pSE->GetPosition();
            for (auto& group : room.groups)
                if (group.alive.find(itemID) != group.alive.end())
                    ctx.liveGroupID = group.liveGroupID;
            FireTriggers(inst, roomIdx, trigger.triggerType, ctx);
            if (m_instances.find(instanceID) == m_instances.end())
                return;
        }
    }
}

void DungeonMgr::ProcessPending(PendingTrigger& pending)
{
    auto itr = m_instances.find(pending.instanceID);
    if (itr == m_instances.end())
        return;
    Instance& inst = itr->second;
    if (pending.roomIdx >= inst.rooms.size())
        return;
    uint32 instanceID(inst.instanceID);
    const Ctx& ctx = pending.ctx;
    LiveRoom& room = inst.rooms[pending.roomIdx];

    switch (pending.triggerType) {
        case Dungeon::Trigger::Exploding: {
            if (ctx.entityID != 0)
                Notify(inst, Dungeon::Engine::EntityKilled, room.roomID, ctx);
            FireTriggers(inst, pending.roomIdx, Dungeon::Trigger::Exploding, ctx);
            if (m_instances.find(instanceID) == m_instances.end())
                return;
            // the room is clear once every npc that spawned in it is dead
            bool anySpawned(!room.extraNPCs.empty());
            for (auto& group : room.groups)
                anySpawned |= group.spawned;
            if (anySpawned and (CountLiveNPCs(room) == 0)) {
                FireTriggers(inst, pending.roomIdx, Dungeon::Trigger::RoomWipedOut, ctx);
                if (m_instances.find(instanceID) == m_instances.end())
                    return;
            }
            if (ctx.groupWiped)
                CheckImplicitCompletion(inst);
        } break;
        case Dungeon::Trigger::Mined: {
            // the first mining in a room is RoomMined
            FireTriggers(inst, pending.roomIdx, Dungeon::Trigger::Mined, ctx);
            if (m_instances.find(instanceID) == m_instances.end())
                return;
            FireTriggers(inst, pending.roomIdx, Dungeon::Trigger::RoomMined, ctx);
        } break;
        case Dungeon::Trigger::RoomMinedOut: {
            // fired for a depleted rock:  the room is mined out when no rock of it is left
            room.asteroids.erase(ctx.entityID);
            m_entityIndex.erase(ctx.entityID);
            bool left(false);
            for (auto rockID : room.asteroids)
                if (m_system->GetSE(rockID) != nullptr)
                    left = true;
            if (!left)
                FireTriggers(inst, pending.roomIdx, Dungeon::Trigger::RoomMinedOut, ctx);
        } break;
        default: {
            FireTriggers(inst, pending.roomIdx, pending.triggerType, ctx);
        } break;
    }
}

void DungeonMgr::Process()
{
    if (!m_initalized)
        return;

    int64 now(Win32TimeNow());

    // triggers queued by hooks.  events may queue more (bounded, in case of a trigger loop)
    uint16 count(0);
    while (!m_pending.empty() and (++count < 1000)) {
        PendingTrigger pending(m_pending.front());
        m_pending.pop_front();
        ProcessPending(pending);
    }

    // delayed events
    while (!m_delayed.empty() and (m_delayed.begin()->first <= now)) {
        PendingEvent pending(m_delayed.begin()->second);
        m_delayed.erase(m_delayed.begin());
        auto itr = m_instances.find(pending.instanceID);
        if ((itr != m_instances.end()) and (pending.roomIdx < itr->second.rooms.size()))
            RunEvent(itr->second, pending.roomIdx, *pending.event, pending.ctx);
    }

    // ships that warped away are removed out of sight
    for (auto itr = m_warpedAway.begin(); itr != m_warpedAway.end(); ) {
        if (itr->second > now) {
            ++itr;
            continue;
        }
        uint32 itemID(itr->first);
        itr = m_warpedAway.erase(itr);
        m_entityIndex.erase(itemID);
        RemoveEntity(itemID, false);
    }
    // ...and some of them come back
    while (!m_returns.empty() and (m_returns.begin()->first <= now)) {
        ReturnEntry entry(m_returns.begin()->second);
        m_returns.erase(m_returns.begin());
        auto itr = m_instances.find(entry.instanceID);
        if (itr == m_instances.end())
            continue;
        Instance& inst = itr->second;
        LiveGroup* pGroup(nullptr);
        for (auto& group : inst.rooms[entry.roomIdx].groups)
            if (group.liveGroupID == entry.liveGroupID)
                pGroup = &group;
        if (pGroup != nullptr)
            pGroup->alive.erase(entry.oldItemID);
        uint32 itemID(SpawnNPC(inst, entry.roomIdx, entry.typeID, inst.factionID, entry.pos, true, entry.liveGroupID));
        if (itemID == 0) {
            if ((pGroup != nullptr) and pGroup->alive.empty()) {
                pGroup->destroyed = true;
                CheckImplicitCompletion(inst);
            }
            continue;
        }
        if (pGroup != nullptr) {
            pGroup->alive.insert(itemID);
        } else {
            inst.rooms[entry.roomIdx].extraNPCs.insert(itemID);
        }
    }

    // where the players are
    std::vector<std::pair<uint32, GPoint>> ships;
    if (!m_instances.empty()) {
        std::vector<Client*> clients;
        m_system->GetClientList(clients);
        for (auto pClient : clients) {
            ShipSE* pShip(pClient->GetShipSE());
            if ((pShip == nullptr) or !pClient->IsInSpace() or (pShip->DestinyMgr() == nullptr) or pShip->DestinyMgr()->IsWarping())
                continue;
            ships.emplace_back(pClient->GetCharacterID(), pShip->GetPosition());
        }
    }

    std::vector<uint32> despawn;
    std::vector<uint32> ids;
    for (auto& cur : m_instances)
        ids.push_back(cur.first);
    for (auto id : ids) {
        auto itr = m_instances.find(id);
        if (itr == m_instances.end())
            continue;
        ProcessPresence(itr->second, ships);
        for (uint8 i = 0; (m_instances.find(id) != m_instances.end()) and (i < itr->second.rooms.size()); ++i) {
            if (itr->second.rooms[i].groups.empty() and itr->second.rooms[i].objects.empty())
                continue;
            ProcessConditions(itr->second, i);
            if (m_instances.find(id) != m_instances.end())
                ProcessGates(itr->second, i);
        }
        itr = m_instances.find(id);
        if (itr == m_instances.end())
            continue;

        // completed sites go a while after completion.  expired ones when nobody is there (or half an hour later anyway)
        //  mission pockets stay until their mission is over (MissionInstanceMgr::Release despawns them)
        Instance& inst = itr->second;
        if (inst.completed and (inst.kind != Dungeon::Kind::Mission)
        and (now >= inst.completedAt + (sConfig.dungeon.CompletedDespawn * EvE::Time::Second))) {
            despawn.push_back(id);
            continue;
        }
        if ((inst.expires > 0) and (now >= inst.expires)) {
            bool occupied(false);
            for (auto& room : inst.rooms)
                occupied |= !room.playersInside.empty();
            if (!occupied or (now >= inst.expires + (30 * EvE::Time::Minute)))
                despawn.push_back(id);
        }
    }
    for (auto id : despawn)
        DespawnInstance(id, true);

    FlushNotices();
}

/* acceleration gates */

const DungeonMgr::LiveGate* DungeonMgr::GetGate(uint32 gateItemID)
{
    auto itr = m_gates.find(gateItemID);
    if (itr == m_gates.end())
        return nullptr;
    return &itr->second;
}

bool DungeonMgr::IsGateOpen(const LiveGate& gate)
{
    if (!gate.open)
        return false;
    if ((gate.def != nullptr) and gate.def->requireClear) {
        auto itr = m_instances.find(gate.instanceID);
        if ((itr != m_instances.end()) and (gate.roomIdx < itr->second.rooms.size()) and (CountLiveNPCs(itr->second.rooms[gate.roomIdx]) > 0))
            return false;
    }
    return true;
}

void DungeonMgr::OpenGate(uint32 gateItemID, LiveGate& gate, bool open)
{
    if (gate.open == open)
        return;
    gate.open = open;
    gate.openUntil = (open ? Win32TimeNow() : 0);
    ProcessGateState(gateItemID, gate);
}

void DungeonMgr::ProcessGateState(uint32 gateItemID, LiveGate& gate)
{
    // the gate model shows open/closed from the slim item
    bool shown(IsGateOpen(gate));
    if (shown == gate.shownOpen)
        return;
    gate.shownOpen = shown;
    if (shown and (gate.openUntil == 0))
        gate.openUntil = Win32TimeNow();
    SendGateSlimUpdate(gateItemID);
}

void DungeonMgr::ProcessGates(Instance& inst, uint8 roomIdx)
{
    for (auto& cur : m_gates)
        if ((cur.second.instanceID == inst.instanceID) and (cur.second.roomIdx == roomIdx))
            ProcessGateState(cur.first, cur.second);
}

void DungeonMgr::MakeGateSlim(uint32 gateItemID, PyDict* slim)
{
    auto itr = m_gates.find(gateItemID);
    const LiveGate* pGate((itr == m_gates.end()) ? nullptr : &itr->second);
    const Dungeon::GateDef* pDef((pGate == nullptr) ? nullptr : pGate->def);

    slim->SetItemString("dunObjectID", ((pGate != nullptr) and (pGate->objectID != 0)) ? new PyInt(pGate->objectID) : PyStatic.NewNone());
    slim->SetItemString("dunToGateID", PyStatic.NewNone());
    if ((pDef != nullptr) and (pDef->skillTypeID != 0)) {
        slim->SetItemString("dunSkillTypeID", new PyInt(pDef->skillTypeID));
        slim->SetItemString("dunSkillLevel", new PyInt(pDef->skillLevel));
    } else {
        slim->SetItemString("dunSkillTypeID", PyStatic.NewNone());
        slim->SetItemString("dunSkillLevel", PyStatic.NewNone());
    }
    if ((pDef != nullptr) and (pDef->keyTypeID != 0)) {
        slim->SetItemString("dunKeyTypeID", new PyInt(pDef->keyTypeID));
        slim->SetItemString("dunKeyQuantity", new PyInt(pDef->keyQuantity));
        slim->SetItemString("dunAmount", new PyFloat(pDef->keyQuantity));
    } else {
        slim->SetItemString("dunKeyTypeID", PyStatic.NewNone());
        slim->SetItemString("dunKeyQuantity", PyStatic.NewNone());
        slim->SetItemString("dunAmount", new PyFloat(0.0));
    }
    if ((pDef != nullptr) and !pDef->allowedGroups.empty()) {
        PyList* classList = new PyList();
        for (auto groupID : pDef->allowedGroups)
            classList->AddItem(new PyInt(groupID));
        slim->SetItemString("dunShipClasses", classList);
    } else {
        slim->SetItemString("dunShipClasses", PyStatic.NewNone());
    }
    slim->SetItemString("dunCloaked", new PyBool(false));
    slim->SetItemString("dunWipeNPC", new PyBool((pDef != nullptr) and pDef->requireClear));
    // None shows the gate's 'off' state
    if ((pGate != nullptr) and IsGateOpen(*pGate)) {
        slim->SetItemString("dunOpenUntil", new PyLong(pGate->openUntil != 0 ? pGate->openUntil : Win32TimeNow()));
    } else {
        slim->SetItemString("dunOpenUntil", PyStatic.NewNone());
    }
    if ((pDef != nullptr) and !pDef->musicUrl.empty()) {
        slim->SetItemString("dunMusicUrl", new PyString(pDef->musicUrl));
    } else {
        slim->SetItemString("dunMusicUrl", PyStatic.NewNone());
    }
    // the gate points at the room it leads to
    GVector dir(0, 0, 1);
    if (pGate != nullptr) {
        auto iItr = m_instances.find(pGate->instanceID);
        if (iItr != m_instances.end()) {
            uint8 dest(GetGateDestination(iItr->second, *pGate));
            if ((dest != pGate->roomIdx) and (dest < iItr->second.rooms.size())) {
                dir = GVector(iItr->second.rooms[pGate->roomIdx].origin, iItr->second.rooms[dest].origin);
                if (dir.length() > 0)
                    dir.normalize();
                else
                    dir = GVector(0, 0, 1);
            }
        }
    }
    PyList* dirList = new PyList();
        dirList->AddItem(new PyFloat(dir.x));
        dirList->AddItem(new PyFloat(dir.y));
        dirList->AddItem(new PyFloat(dir.z));
    slim->SetItemString("dunDirection", dirList);
    slim->SetItemString("dunSpawnID", PyStatic.NewNone());
    slim->SetItemString("dunScenarioID", PyStatic.NewNone());
    if (pGate != nullptr) {
        auto iItr = m_instances.find(pGate->instanceID);
        if ((iItr != m_instances.end()) and (pGate->roomIdx < iItr->second.rooms.size()))
            slim->SetItemString("dunRoomName", new PyString(iItr->second.rooms[pGate->roomIdx].def->roomName));
    }
}

uint8 DungeonMgr::GetGateDestination(Instance& inst, const LiveGate& gate)
{
    if ((gate.def != nullptr) and (gate.def->destRoomID != 0))
        for (uint8 i = 0; i < inst.rooms.size(); ++i)
            if (inst.rooms[i].roomID == gate.def->destRoomID)
                return i;
    // the next room (the last room's gate leads back to the first)
    if (gate.roomIdx + 1 < inst.rooms.size())
        return gate.roomIdx + 1;
    return 0;
}

void DungeonMgr::SendGateSlimUpdate(uint32 gateItemID)
{
    SystemEntity* pSE(m_system->GetSE(gateItemID));
    if ((pSE == nullptr) or (pSE->SysBubble() == nullptr))
        return;
    PyTuple* shipData = new PyTuple(2);
        shipData->SetItem(0, new PyLong(gateItemID));
        shipData->SetItem(1, new PyObject("foo.SlimItem", pSE->MakeSlimItem()));
    PyTuple* sItem = new PyTuple(2);
        sItem->SetItem(0, new PyString("OnSlimItemChange"));
        sItem->SetItem(1, shipData);
    pSE->SysBubble()->BubblecastDestinyUpdate(&sItem, "dungeon");   // consumed
}

void DungeonMgr::ActivateGate(Client* pClient, uint32 gateItemID)
{
    ShipSE* pShip(pClient->GetShipSE());
    if ((pShip == nullptr) or (pShip->DestinyMgr() == nullptr) or !pClient->IsInSpace())
        return;
    auto gItr = m_gates.find(gateItemID);
    SystemEntity* pGate(m_system->GetSE(gateItemID));
    if ((gItr == m_gates.end()) or (pGate == nullptr))
        throw CustomError("This acceleration gate is not active.");
    LiveGate& gate = gItr->second;
    auto iItr = m_instances.find(gate.instanceID);
    if ((iItr == m_instances.end()) or (gate.roomIdx >= iItr->second.rooms.size()))
        throw CustomError("This acceleration gate is not active.");
    Instance& inst = iItr->second;
    const Dungeon::GateDef* pDef(gate.def);
    // mission pockets' gates carry the mission level's ship restrictions (missions.GateRestrictions switches them off)
    const bool shipLists((inst.kind != Dungeon::Kind::Mission) or sConfig.missions.GateRestrictions);

    // the client approaches to 2500m (surface distance) before asking
    double distance(pShip->GetPosition().distance(pGate->GetPosition()) - pShip->GetRadius() - pGate->GetRadius());
    if (distance > maxStargateJumpingDistance + 500)
        throw UserError("DunGateTooFarAway").AddFormatValue("maxdist", new PyInt(static_cast<int32>(maxStargateJumpingDistance)));

    // ship rules:  no pods, no capitals, the gate's group lists
    uint32 shipTypeID(pShip->GetTypeID());
    uint32 shipGroupID(pShip->GetSelf()->groupID());
    if (shipGroupID == EVEDB::invGroups::Capsule)
        throw UserError("DunPodsCannotWarp");
    auto groupNames = [](const std::vector<uint32>& groups) -> std::string {
        std::string names;
        for (auto groupID : groups) {
            if (!names.empty())
                names += "<br>";
            names += sDataMgr.GetGroupName(groupID);
        }
        return names;
    };
    if (DungeonDataMgr::IsCapitalGroup(shipGroupID) or ((pDef != nullptr) and shipLists and
        (std::find(pDef->deniedGroups.begin(), pDef->deniedGroups.end(), shipGroupID) != pDef->deniedGroups.end())))
    {
        std::vector<uint32> denied, allowed;
        sDunDataMgr.GetShipRestrictions(inst.dungeon->dungeonID, allowed, denied);
        throw UserError("DunBlacklistCannotWarp")
                .AddFormatValue("ship", new PyInt(shipTypeID))
                .AddFormatValue("list", new PyString(groupNames(denied)));
    }
    if ((pDef != nullptr) and shipLists and !pDef->allowedGroups.empty()
    and (std::find(pDef->allowedGroups.begin(), pDef->allowedGroups.end(), shipGroupID) == pDef->allowedGroups.end()))
        throw UserError("DunShipCannotWarp")
                .AddFormatValue("ship", new PyInt(shipTypeID))
                .AddFormatValue("whitelist", new PyString(groupNames(pDef->allowedGroups)));

    // locked by a trigger, or by the hostiles still in the room
    if (!IsGateOpen(gate))
        throw UserError("DunGateNPCsAround");

    // acceleration gates won't fling a scrambled ship
    if (pShip->IsWarpScrambled())
        throw UserError("WarpScrambled");

    if ((pDef != nullptr) and (pDef->skillTypeID != 0) and (pClient->GetChar()->GetSkillLevel(pDef->skillTypeID) < pDef->skillLevel))
        throw UserError("DunGateNoSkill").AddFormatValue("skill", new PyInt(pDef->skillTypeID));

    // keys in the cargo hold
    if ((pDef != nullptr) and (pDef->keyTypeID != 0) and sConfig.dungeon.GateKeysRequired) {
        uint32 charID(pClient->GetCharacterID());
        Inventory* pInv(pClient->GetShip()->GetMyInventory());
        uint32 have((pInv != nullptr) ? pInv->GetTypeQtyForOwner(pDef->keyTypeID, flagCargoHold, charID) : 0);
        if (have < pDef->keyQuantity) {
            if (pDef->keyQuantity > 1)
                throw UserError("DunGateLocked_ManyKeys")
                        .AddFormatValue("quantity", new PyInt(pDef->keyQuantity))
                        .AddFormatValue("key", new PyInt(pDef->keyTypeID));
            if (pDef->keyConsumed)
                throw UserError("DunGateLocked_OneKey").AddFormatValue("key", new PyInt(pDef->keyTypeID));
            throw UserError("DunGateLocked_ZeroKey").AddFormatValue("key", new PyInt(pDef->keyTypeID));
        }
        if (pDef->keyConsumed) {
            std::vector<InventoryItemRef> keys;
            if (pInv->TakeTypeQtyForOwner(pDef->keyTypeID, flagCargoHold, charID, pDef->keyQuantity, keys))
                for (auto& cur : keys)
                    cur->Delete();
        }
    }

    // the destination room, populated now if nobody has been there yet
    uint8 dest(GetGateDestination(inst, gate));
    LiveRoom& room = inst.rooms[dest];
    PopulateRoom(inst, dest);

    // everyone near the gate sees it fire;  the activating ship hears the gate's music
    pShip->DestinyMgr()->SendSpecialEffect10(gateItemID, pShip->GetID(), "effects.WarpGateEffect", false, true, false);

    GPoint into(room.origin + room.def->arrival);
    into.MakeRandomPointOnSphere(MakeRandomFloat(0.0, maxStargateJumpingDistance));
    _log(DUNG__MESSAGE, "DungeonMgr::ActivateGate() - %s takes gate %u of %s from room %u to room %u", pClient->GetName(),
            gateItemID, inst.name.c_str(), inst.rooms[gate.roomIdx].roomID, room.roomID);
    pShip->DestinyMgr()->WarpTo(into, 0, false, nullptr, 0, true);
}

/* escalations */

namespace {
    // the client's util.Row with header and line
    PyObject* MakeRow(PyList* header, PyList* line)
    {
        PyDict* dict = new PyDict();
            dict->SetItemString("header", header);
            dict->SetItemString("line", line);
        return new PyObject("util.Row", dict);
    }
}

void DungeonMgr::OnVisitor(Instance& inst, uint8 roomIdx, uint32 charID)
{
    // the first time a pilot enters a site that isn't on the overview, its system map gets a bracket for it
    if ((roomIdx != 0) or !inst.visitors.insert(charID).second)
        return;
    bool escalation(inst.kind == Dungeon::Kind::Escalation);
    if (!escalation and (inst.kind != Dungeon::Kind::Signature))
        return;

    if (escalation)
        for (auto& cur : inst.escalationOwners)
            if (cur.second == charID)
                DungeonDB::SetEscalationState(cur.first, 1);

    Client* pClient(sEntityList.FindClientByCharID(charID));
    if (pClient == nullptr)
        return;
    PyList* header = new PyList();
        header->AddItem(new PyString("instanceID"));
        header->AddItem(new PyString("dungeonID"));
        header->AddItem(new PyString("name"));
        header->AddItem(new PyString("x"));
        header->AddItem(new PyString("y"));
        header->AddItem(new PyString("z"));
    PyList* line = new PyList();
        line->AddItem(new PyInt(inst.instanceID));
        line->AddItem(new PyInt(inst.dungeon->dungeonID));
        line->AddItem(new PyString(inst.name));
        line->AddItem(new PyFloat(inst.rooms[0].origin.x));
        line->AddItem(new PyFloat(inst.rooms[0].origin.y));
        line->AddItem(new PyFloat(inst.rooms[0].origin.z));
    PyTuple* payload = new PyTuple(1);
        payload->SetItem(0, MakeRow(header, line));
    pClient->SendNotification((escalation ? "OnEscalatingPathDungeonEntered" : "OnDistributionDungeonEntered"), "charid", payload, false);
}

void DungeonMgr::TryEscalate(Instance& inst, uint32 charID)
{
    if (!IsCharacterID(charID) or (inst.kind == Dungeon::Kind::Mission) or (inst.kind == Dungeon::Kind::Test))
        return;
    std::vector<const Dungeon::EscalationDef*> rules;
    sDunDataMgr.GetEscalations(inst.dungeon->dungeonID, inst.dungeon->archetypeID, inst.dungeon->difficulty, rules);
    float mySec(m_system->GetSystemSecurityRating());
    for (auto pRule : rules) {
        if (MakeRandomFloat() >= (pRule->chance * sConfig.dungeon.EscalationChance))
            continue;

        // where:  a k-space system within range (lower true sec if the rule says so) that has a fitting dungeon,
        //  the faction's own region preferred
        uint8 minDifficulty(std::max<int16>(0, inst.dungeon->difficulty + pRule->destDifficultyDelta));
        std::vector<std::pair<uint32, uint8>> systems;
        sMapData.GetSystemsWithinJumps(m_system->GetID(), pRule->maxJumps, systems);
        std::vector<std::pair<uint32, uint32>> own, other;     // system, dungeon
        for (auto& cur : systems) {
            if ((cur.second < pRule->minJumps) or !IsKSpaceID(cur.first))
                continue;
            SystemData sysData = SystemData();
            if (!sDataMgr.GetSystemData(cur.first, sysData))
                continue;
            if (pRule->lowerSecOnly and (sysData.securityRating > mySec))
                continue;
            uint32 dungeonID(pRule->destDungeonID);
            if (dungeonID != 0) {
                std::shared_ptr<const Dungeon::Dungeon> pDest(sDunDataMgr.GetDungeon(dungeonID));
                if ((pDest.get() == nullptr) or (sysData.securityRating < pDest->minSec) or (sysData.securityRating > pDest->maxSec))
                    continue;
            } else {
                dungeonID = sDunDataMgr.PickDungeon(pRule->destArchetypeID, inst.factionID, sysData.securityRating, minDifficulty);
                if (dungeonID == 0)
                    continue;
            }
            if (sDataMgr.GetRegionRatFaction(sysData.regionID) == inst.factionID) {
                own.emplace_back(cur.first, dungeonID);
            } else {
                other.emplace_back(cur.first, dungeonID);
            }
        }
        std::vector<std::pair<uint32, uint32>>& list = (own.empty() ? other : own);
        if (list.empty()) {
            _log(DUNG__MESSAGE, "DungeonMgr::TryEscalate() - rule %u of %s has no destination near %s(%u)",
                    pRule->escalationID, inst.name.c_str(), m_system->GetName(), m_system->GetID());
            continue;
        }
        std::pair<uint32, uint32> dest(list.at(std::min<size_t>(MakeRandomInt(0, list.size()), list.size() - 1)));

        // the site position, inside the destination system
        SystemData destData = SystemData();
        sDataMgr.GetSystemData(dest.first, destData);
        GPoint pos(sMapData.GetAnomalyPoint(dest.first));
        double maxDist(destData.radius * 0.9);
        if ((maxDist > 0) and (pos.length() > maxDist)) {
            GVector dir(pos);
            dir.normalize();
            pos = dir * (maxDist * MakeRandomFloat(0.33, 1.0));
        }

        int64 now(Win32TimeNow());
        EscalationRow row = EscalationRow();
            row.characterID = charID;
            row.escalationID = pRule->escalationID;
            row.srcDungeonID = inst.dungeon->dungeonID;
            row.destDungeonID = dest.second;
            row.factionID = inst.factionID;
            row.solarSystemID = dest.first;
            row.position = pos;
            row.creationTime = now;
            row.expiryTime = now + ((pRule->expiryHours > 0 ? pRule->expiryHours : sConfig.dungeon.EscalationExpiry) * EvE::Time::Hour);
            row.journalMessageID = pRule->journalMessageID;

        // the pilot (and, if shared, the fleet members here) get the journal entry
        std::vector<uint32> granted;
        std::vector<uint32> chars(1, charID);
        Client* pClient(sEntityList.FindClientByCharID(charID));
        std::vector<Client*> fleet;
        if ((pClient != nullptr) and pClient->InFleet())
            fleet = sFltSvc.GetFleetClients(pClient->GetFleetID());
        if (sConfig.dungeon.EscalationShareFleet)
            for (auto pMember : fleet)
                if ((pMember != pClient) and (pMember->GetSystemID() == m_system->GetID()))
                    chars.push_back(pMember->GetCharacterID());
        for (auto cur : chars) {
            row.characterID = cur;
            uint32 instanceID(DungeonDB::InsertEscalation(row));
            if (instanceID == 0)
                continue;
            granted.push_back(instanceID);
            Client* pOwner(sEntityList.FindClientByCharID(cur));
            if (pOwner != nullptr) {
                PyTuple* payload = new PyTuple(1);
                    payload->SetItem(0, new PyInt(instanceID));
                pOwner->SendNotification("OnEscalatingPathMessage", "charid", payload, false);
            }
        }
        if (granted.empty())
            return;

        // the rest of the fleet hears about it
        if (!sConfig.dungeon.EscalationShareFleet)
            for (auto pMember : fleet) {
                if (pMember == pClient)
                    continue;
                PyDict* step = new PyDict();
                    step->SetItemString("journalEntryTemplateID", new PyInt(pRule->journalMessageID));
                PyTuple* payload = new PyTuple(2);
                    payload->SetItem(0, new PyInt(charID));
                    payload->SetItem(1, new PyObject("util.KeyVal", step));
                pMember->SendNotification("OnFleetEscalatingPathMessage", "charid", payload, false);
            }

        _log(DUNG__INFO, "DungeonMgr::TryEscalate() - %s(%u) escalates %s to dungeon %u in %u (%u entries)", (pClient != nullptr ? pClient->GetName() : "?"),
                charID, inst.name.c_str(), dest.second, dest.first, granted.size());

        // a loaded destination system makes the site now, others when they boot
        SystemManager* pDest(sEntityList.FindSystem(dest.first));
        if (pDest != nullptr)
            for (auto instanceID : granted)
                pDest->GetDungMgr()->SpawnEscalation(instanceID);
        return;
    }
}

void DungeonMgr::LoadEscalations()
{
    std::vector<EscalationRow> rows;
    DungeonDB::GetEscalationsForSystem(m_system->GetID(), Win32TimeNow(), rows);
    for (auto& row : rows)
        SpawnEscalation(row.instanceID);
}

void DungeonMgr::SpawnEscalation(uint32 escalationInstanceID)
{
    if (!m_initalized or (GetEscalationInstance(escalationInstanceID) != 0))
        return;
    EscalationRow row = EscalationRow();
    if (!DungeonDB::GetEscalation(escalationInstanceID, row) or (row.solarSystemID != m_system->GetID()))
        return;
    int64 now(Win32TimeNow());
    if ((row.state > 1) or (row.expiryTime <= now))
        return;

    // fleet members granted the same site share it
    for (auto& cur : m_instances)
        if ((cur.second.kind == Dungeon::Kind::Escalation) and (cur.second.dungeon->dungeonID == row.destDungeonID)
        and !cur.second.rooms.empty() and (cur.second.rooms[0].origin.distance(row.position) < 1.0))
        {
            cur.second.escalationOwners[row.instanceID] = row.characterID;
            return;
        }

    Dungeon::SpawnParams params;
        params.dungeonID = row.destDungeonID;
        params.factionID = row.factionID;
        params.kind = Dungeon::Kind::Escalation;
        params.usePosition = true;
        params.position = row.position;
        params.ownerID = row.characterID;
        params.escalationID = row.instanceID;
        params.lifetimeSec = static_cast<uint32>(std::max<int64>(1, (row.expiryTime - now) / EvE::Time::Second));
    uint32 instanceID(SpawnInstance(params));
    if (instanceID == 0)
        return;
    m_instances[instanceID].escalationOwners[row.instanceID] = row.characterID;
}

void DungeonMgr::OnEscalationCompleted(Instance& inst)
{
    // every journal entry of the site is done (it shows as expired until the pilot removes it)
    int64 now(Win32TimeNow());
    for (auto& cur : inst.escalationOwners) {
        DungeonDB::SetEscalationState(cur.first, 2, now);
        Client* pClient(sEntityList.FindClientByCharID(cur.second));
        if (pClient != nullptr)
            pClient->SendNotification("OnEscalatingPathChange", "charid", new PyTuple(0), false);
    }
}

void DungeonMgr::OnEscalationRemoved(uint32 escalationInstanceID)
{
    // the pilot removed the journal entry.  nobody left to visit the site:  it goes
    for (auto& cur : m_instances) {
        if (cur.second.escalationOwners.erase(escalationInstanceID) == 0)
            continue;
        if (cur.second.escalationOwners.empty())
            DespawnInstance(cur.first, false);
        return;
    }
}

uint32 DungeonMgr::GetEscalationInstance(uint32 escalationInstanceID)
{
    for (auto& cur : m_instances)
        if (cur.second.escalationOwners.find(escalationInstanceID) != cur.second.escalationOwners.end())
            return cur.first;
    return 0;
}

bool DungeonMgr::GetInstanceWarpIn(uint32 escalationInstanceID, uint32 charID, GPoint& into)
{
    for (auto& cur : m_instances) {
        auto itr = cur.second.escalationOwners.find(escalationInstanceID);
        if ((itr == cur.second.escalationOwners.end()) or (itr->second != charID) or cur.second.rooms.empty())
            continue;
        into = cur.second.rooms[0].origin;
        return true;
    }
    return false;
}
