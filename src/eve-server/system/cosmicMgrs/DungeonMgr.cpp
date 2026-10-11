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
#include "system/SystemBubble.h"
#include "system/cosmicMgrs/SpawnMgr.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/cosmicMgrs/DungeonMgr.h"

/*
Dungeon flow:
1. load dungeons from db on init
2. when signature/anomaly is created, spawn first room of dungeon at ship position (how to handle multi-room dungeons?)

Multi-room dungeons have each room with acceleration gate pointing to next room (accel gate is not implemented????)

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
            for (auto& object : room.second.objects)
                if (m_gates.find(object.objectID) != m_gates.end())
                    m_gateDungeon[object.objectID] = cur.first;
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
: m_system(mgr),
m_services(svc),
m_anomMgr(nullptr),
m_spawnMgr(nullptr),
m_initalized(false)
{
}

DungeonMgr::~DungeonMgr()
{
    // TODO: clean up and free any resources
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
        return true;
    }
    return false;
}

bool DungeonMgr::GetInstanceWarpIn(uint32 instanceID, uint32 charID, GPoint& into)
{
    /** @todo F13: per-character dungeon instances (escalations).  no instance exists before then */
    return false;
}

void DungeonMgr::Process()
{
    // TODO: process and update any active dungeons in the system
    if (!m_initalized)
        return;
}

bool DungeonMgr::MakeDungeon(CosmicSignature& sig, uint32 dungeonID)
{
    Dungeon::Dungeon dData;
    dData.dungeonID = 0;

    // If we are given a dungeonID, use it otherwise pick a random dungeon based on archetype
    if (dungeonID == 0) {
        sDunDataMgr.GetRandomDungeon(dData, sig.dungeonType);
    } else {
        sDunDataMgr.GetDungeon(dData, dungeonID);
    }

    if (dData.rooms.empty()) {
        _log(DUNG__WARNING, "DungeonMgr::MakeDungeon() - no dungeon with rooms for %s (type %u, dungeonID %u)", sig.sigName.c_str(), sig.dungeonType, dungeonID);
        return false;
    }
    if (!dData.name.empty())
        sig.sigName = dData.name;
    else
        sig.sigName = sDunDataMgr.GetDungeonType(sig.dungeonType);

    if ((sig.sigGroupID == EVEDB::invGroups::Cosmic_Signature) || (sig.sigGroupID == EVEDB::invGroups::Cosmic_Anomaly)) {
        // Create a new anomaly inventory item to track entire dungeon under
        ItemData iData(sig.sigTypeID, sig.ownerID, sig.systemID, flagNone, sig.sigName.c_str(), sig.position/*, info*/);

        InventoryItemRef iRef = sItemFactory.SpawnItem(iData);
        if (iRef.get() == nullptr)
            return false;
        iRef->SetCustomInfo(std::string("livedungeon").c_str());
        iRef->SaveItem();

        CelestialSE* cSE = new CelestialSE(iRef, m_system->GetServiceMgr(), m_system);

        if (cSE == nullptr)
            return false;

        // dont add signal thru sysMgr.  signal is added when this returns to anomMgr
        m_system->AddEntity(cSE, false);
        sig.sigItemID = iRef->itemID();
        sig.bubbleID = cSE->SysBubble()->GetID();

        _log(COSMIC_MGR__TRACE, "DungeonMgr::Create() - %s using dungeonID %u", sig.sigName.c_str(), dData.dungeonID);

        // Create the new live dungeon
        LiveDungeon newDungeon;
        newDungeon.anomalyID = iRef->itemID();
        newDungeon.systemID = m_system->GetID();

        // Iterate through rooms and handle item spawning for each room
        uint16 roomCounter = 0;
        for (auto const& room : dData.rooms) {
            LiveRoom newRoom;
            // Set room position
            // Handle first room differently as it will be at the origin point of signature
            if (roomCounter == 0) {
                newRoom.position = sig.position;
            } else {
                // The following rooms shall be 100M kilometers in x direction from previous room.
                GPoint pos;
                pos.x = newDungeon.rooms[roomCounter - 1].position.x + NEXT_DUNGEON_ROOM_DIST;
                pos.y = newDungeon.rooms[roomCounter - 1].position.y;
                pos.z = newDungeon.rooms[roomCounter - 1].position.z;
                newRoom.position = pos;
            }

            for (auto object : room.second.objects ) {
                GPoint pos;
                // Set position for each object
                pos.x = newRoom.position.x + object.x;
                pos.y = newRoom.position.y + object.y;
                pos.z = newRoom.position.z + object.z;

                // If invGroup is an NPC, we must spawn it as such
                Inv::TypeData objType;
                Inv::GrpData objGroup;
                sDataMgr.GetType(object.typeID, objType);
                sDataMgr.GetGroup(objType.groupID, objGroup);
                if (objGroup.catID == EVEDB::invCategories::Ship || objGroup.catID == EVEDB::invCategories::Drone) {
                    m_spawnMgr->DoSpawnForAnomaly(sBubbleMgr.GetBubble(m_system, pos), pos, GetRandLevel(), object.typeID, sig.ownerID);
                } 
                
                // Otherwise, spawn as a normal celestial object
                else {
                    // Define ItemData object for each RoomObject
                    ItemData dData(object.typeID, sig.ownerID, sig.systemID, flagNone, sDataMgr.GetTypeName(object.typeID), pos);

                    // Spawn the object (and persist it across system unloads)
                    iRef = sItemFactory.SpawnItem(dData);
                    if (iRef.get() == nullptr) {
                        _log(COSMIC_MGR__ERROR, "DungeonMgr::Create() - Unable to spawn item with type %s for room %u dungeon with anomaly itemID %u", sDataMgr.GetTypeName(object.typeID), roomCounter, newDungeon.anomalyID);
                        return false;
                    }
                    iRef->SetCustomInfo(("livedungeon_" + std::to_string(newDungeon.anomalyID)).c_str());
                    iRef->SaveItem();

                    cSE = new CelestialSE(iRef, m_system->GetServiceMgr(), m_system);
                    m_system->AddEntity(cSE, false);
                    newRoom.items.push_back(iRef->itemID());
                }
            }

            newDungeon.rooms.insert({roomCounter, newRoom});
            roomCounter++;
        }

        // Finally add the new dungeon to the system-wide list for tracking
        m_dungeonList.insert({newDungeon.anomalyID, newDungeon});
        return true;
    }
    return false;
}

void DungeonMgr::Close()
{
    // the items of live dungeons don't survive the system.  their entities are torn down by the system unload after this
    for (auto& dungeon : m_dungeonList) {
        for (auto& room : dungeon.second.rooms)
            for (auto itemID : room.second.items) {
                SystemEntity* pSE(m_system->GetSE(itemID));
                if (pSE != nullptr) {
                    m_system->RemoveEntity(pSE);
                    SafeDelete(pSE);
                }
                InventoryItemRef iRef(sItemFactory.GetItemRef(itemID));
                if (iRef.get() != nullptr)
                    iRef->Delete();
            }
        SystemEntity* pSE(m_system->GetSE(dungeon.second.anomalyID));
        if (pSE != nullptr) {
            m_system->RemoveEntity(pSE);
            SafeDelete(pSE);
        }
        InventoryItemRef iRef(sItemFactory.GetItemRef(dungeon.second.anomalyID));
        if (iRef.get() != nullptr)
            iRef->Delete();
    }
    m_dungeonList.clear();
}

int8 DungeonMgr::GetFaction(uint32 factionID)
{
    switch (factionID) {
        case factionAngel:          return 2;
        case factionSanshas:        return 5;
        case factionGuristas:       return 4;
        case factionSerpentis:      return 1;
        case factionBloodRaider:    return 3;
        case factionRogueDrones:    return 6;
        case 0:                     return 7;
        // these are incomplete.  set to default (region rat)
        case factionAmarr:
        case factionAmmatar:
        case factionCaldari:
        case factionGallente:
        case factionMinmatar:
        default:
            return GetFaction(sDataMgr.GetRegionRatFaction(m_system->GetRegionID()));
    }
}

int8 DungeonMgr::GetRandLevel()
{
    double level = MakeRandomFloat();
    _log(DUNG__TRACE, "DungeonMgr::GetRandLevel() - level = %.2f", level);

    if (level < 0.15) {
        return 4;
    } else if (level < 0.25) {
        return 3;
    } else if (level < 0.50) {
        return 2;
    } else {
        return 1;
    }
}
