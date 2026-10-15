 /**
  * @name IncursionSites.cpp
  *     Sansha incursion sites for EVEmu: pockets, acceleration gates, waves and each site's own mechanics
  *
  * @date:          12 October 2026
  *
  *  every site is a global beacon on the overview with one or two acceleration gates next to it.  the gates lead to
  *  pockets about a million km away, each with its own beacon.  a pocket's ships spawn when the first pilot gets
  *  there.  what brings the next wave and what completes the site follow each site's EVE University wiki page:
  *   - Nation Commander Outpost, Nation Rebirth Facility: clear every wave
  *   - Nation Mining Colony: mine the Lyavite (mining can bring more ships).  clearing wave 2 uncovers the Nation Ore
  *       Refinery; 255 Lyavite in it destroys the battle station
  *   - Override Transfer Array: the Remote Logistics Station repairs the Sansha until a Logistics Control Array is
  *       hacked (6 minutes).  Eysturs keep coming back and don't count
  *   - Nation Commander Stronghold: five waves; killing the Citizen or Slave in the last ends it and the rest warp off
  *   - Nation Consolidation Network: the beacon grid has a cruiser gate and a battleship gate.  each side's gate to the
  *       Fleet Staging Area opens when the other side's pocket is clear.  clear the staging area
  *   - Overwhelmed Civilian Facility: the last ship of each wave drops 15 Captured Civilians.  killing most of wave 4
  *       turns the Obelisk into the Emergency Evacuation Freighter; 40 civilians in it completes the site
  *   - True Creations Research Center: shield transfer arrays keep the Battletower at full health.  MTACs from the
  *       CONCORD MTAC Factory, dropped into the Shield Transfer Control Tower, stop them for 90 s each.  the first
  *       uncloaked pilot brings three timed waves; most groups respawn
  *   - True Power Provisional Headquarters: three pockets.  each pocket's gate appears once it's clear.  in the last,
  *       the next wave comes when the last Ostingele dies, and the Mobile Headquarters replaces the station once
  *       every ship is dead
  *   - The Kundalini Manifest, Uroborus: the mothership, with escorts and bombers on a random timer.  hacking the
  *       Logistics Control Array turns Sansha logistics onto the pilots.  a Tactical Response Transmitter from the
  *       Central Control Nexus, dropped into the Sansha Control Relay, sends a third of the escorts away for a while
  *  completed sites lose their beacon at once, and their pockets go when the last pilot has left
  */

#include <algorithm>
#include <cmath>
#include <ctime>

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "EVE_Incursion.h"
#include "StaticDataMgr.h"
#include "inventory/ItemFactory.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/ai/AggroGroup.h"
#include "npc/ai/NpcProfile.h"
#include "ship/Ship.h"
#include "system/Asteroid.h"
#include "system/BubbleManager.h"
#include "system/Celestial.h"
#include "system/Container.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/cosmicMgrs/SpawnMgr.h"
#include "system/cosmicMgrs/IncursionMgr.h"

// customInfo for every site item.  DBCleaner removes '%livedungeon%' items on server start,
//  and ManagerDB::ClearDungeons() removes them when their system unloads
static const char* IncursionCustomInfo = "livedungeon_incursion";

static const double LANDING_SPREAD  = 2500;     // pilots land within this of a pocket's beacon
static const double GATE_RANGE      = 2500;     // gates work within this of their surface
static const uint32 LINGER_EMPTY    = 60;       // seconds a completed site stays once its pilots are gone
static const uint32 LINGER_MAX      = 1800;     // and at most this long after it was completed
static const uint32 WARP_OFF_TIME   = 12;       // seconds a ship warping off is kept before it's removed

namespace IncType {
    enum {
        CapturedCivilians       = 3483,
        MTAC                    = 3501,
        Transmitter             = 3507,
        Lyavite                 = 28620,
        // containers
        LogisticsArray          = 3261,
        MTACFactory             = 3498,
        ControlTower            = 3495,
        OreRefinery             = 3502,
        EvacFreighter           = 3503,
        ControlNexus            = 3506,
        ControlRelay            = 3508,
        CivilianWreck           = 27275,
        // scenery
        BattletowerLCO          = 2234,
        Obelisk                 = 32137,
        LogisticsStation        = 3510,
        // ships
        Antem                   = 2191,
        Auga                    = 2936,
        Deltole                 = 2933,
        Mara                    = 2931,
        Ostingele               = 2932,
        Renyn                   = 2190,
        Romi                    = 2859,
        Vylade                  = 2207
    };
}

static uint32 TimeNow()
{
    return (uint32)std::time(nullptr);
}

// random index in [0, count)
static size_t RandomIndex(size_t count)
{
    if (count < 2)
        return 0;
    size_t idx = (size_t)MakeRandomFloat(0.0, (double)count);
    return (idx < count ? idx : count - 1);
}

static bool IsContainerGroup(uint16 groupID)
{
    return ((groupID == EVEDB::invGroups::Spawn_Container) or (groupID == EVEDB::invGroups::Mission_Container)
         or (groupID == EVEDB::invGroups::Cargo_Container));
}

// units of a type in a container
static uint32 CountType(SystemEntity* pSE, uint16 typeID)
{
    if ((pSE == nullptr) or (pSE->GetSelf()->GetMyInventory() == nullptr))
        return 0;
    std::map<uint32, InventoryItemRef> items;
    pSE->GetSelf()->GetMyInventory()->GetInventoryMap(items);
    uint32 count(0);
    for (auto cur : items)
        if (cur.second->typeID() == typeID)
            count += cur.second->quantity();
    return count;
}

// take up to qty units of a type out of a container and destroy them.  returns how many went
static uint32 ConsumeType(SystemEntity* pSE, uint16 typeID, uint32 qty)
{
    if ((pSE == nullptr) or (pSE->GetSelf()->GetMyInventory() == nullptr))
        return 0;
    std::map<uint32, InventoryItemRef> items;
    pSE->GetSelf()->GetMyInventory()->GetInventoryMap(items);
    uint32 taken(0);
    for (auto cur : items) {
        if (taken >= qty)
            break;
        InventoryItemRef iRef(cur.second);
        if (iRef->typeID() != typeID)
            continue;
        uint32 want = qty - taken;
        if (iRef->quantity() > want) {
            iRef->SetQuantity(iRef->quantity() - want, true);
            taken += want;
        } else {
            taken += iRef->quantity();
            iRef->Delete();
        }
    }
    return taken;
}

// put qty units of a type into a site container, owned by the container's owner so it lists them
static void AddType(SystemEntity* pSE, uint16 typeID, uint32 qty)
{
    if (pSE == nullptr)
        return;
    ItemData data(typeID, pSE->GetSelf()->ownerID(), locTemp, flagNone, qty);
    InventoryItemRef iRef = sItemFactory.SpawnItem(data);
    if (iRef.get() != nullptr)
        iRef->Move(pSE->GetID(), flagNone, true);
}

// aggro group (and npc remote repair) key of a site's room.  bit 62 keeps it apart from DungeonMgr's (instanceID << 8 | room)
static uint64_t RoomKey(uint32 siteID, uint8 room)
{
    return ((1ULL << 62) | (static_cast<uint64_t>(siteID) << 8) | room);
}

/* a hacked logistics control array at a mothership:  each Sansha ship with remote repair gives its cycles to the pilots
 *  in its bubble instead of its friends (its own repairs are off, NPCAIMgr::SetRemoteRepairsOff).  its type's amount, cycle,
 *  range and number of targets, the most damaged pilots first */
static void RepairPilots(IncLiveSite& site, SystemManager* pSys, const std::vector<uint32>& ships)
{
    int64 now(GetTimeMSeconds());
    for (auto id : ships) {
        NPC* pNPC = pSys->GetNPCSE(id);
        if ((pNPC == nullptr) or pNPC->IsDead() or (pNPC->GetAIMgr() == nullptr) or (pNPC->SysBubble() == nullptr)
        or (pNPC->DestinyMgr() == nullptr))
            continue;
        const NpcProfile& profile(pNPC->GetAIMgr()->GetProfile());
        // the Sansha are shield tanked:  shield transfers first
        bool shield(profile.remoteShield.effectID != 0);
        if (!shield and (profile.remoteArmor.effectID == 0))
            continue;
        const NpcProfile::RemoteRepair& rr(shield ? profile.remoteShield : profile.remoteArmor);
        int64& due(site.repairAt[id]);
        if (now < due)
            continue;
        due = now + std::max<int64>(rr.cycleMs, 1000);

        std::vector<Client*> players;
        pNPC->SysBubble()->GetPlayers(players);
        std::vector<std::pair<double, ShipSE*>> hurt;
        for (auto pClient : players) {
            ShipSE* pShip(pClient->GetShipSE());
            if ((pShip == nullptr) or pShip->IsDead())
                continue;
            double distance(pNPC->GetPosition().distance(pShip->GetPosition()) - pNPC->GetRadius() - pShip->GetRadius());
            if (distance > profile.assistRange)
                continue;
            InventoryItemRef iRef(pShip->GetSelf());
            if (shield) {
                double capacity(iRef->GetAttribute(AttrShieldCapacity).get_double());
                double charge(iRef->GetAttribute(AttrShieldCharge).get_double());
                if ((capacity > 0) and (charge < capacity))
                    hurt.push_back(std::make_pair(charge / capacity, pShip));
            } else {
                double hp(iRef->GetAttribute(AttrArmorHP).get_double());
                double damage(iRef->GetAttribute(AttrArmorDamage).get_double());
                if ((hp > 0) and (damage > 0))
                    hurt.push_back(std::make_pair(1.0 - damage / hp, pShip));
            }
        }
        std::sort(hurt.begin(), hurt.end(), [](const std::pair<double, ShipSE*>& a, const std::pair<double, ShipSE*>& b) {
            return a.first < b.first; });
        size_t count(std::min(hurt.size(), static_cast<size_t>(rr.maxTargets > 0 ? rr.maxTargets : 1)));
        for (size_t i = 0; i < count; ++i) {
            ShipSE* pShip(hurt[i].second);
            InventoryItemRef iRef(pShip->GetSelf());
            if (shield) {
                double capacity(iRef->GetAttribute(AttrShieldCapacity).get_double());
                double charge(iRef->GetAttribute(AttrShieldCharge).get_double() + rr.amount);
                iRef->SetAttribute(AttrShieldCharge, (charge > capacity ? capacity : charge));
            } else {
                double damage(iRef->GetAttribute(AttrArmorDamage).get_double() - rr.amount);
                iRef->SetAttribute(AttrArmorDamage, (damage < 0 ? 0.0 : damage));
            }
            pShip->SendDamageStateChanged();
            pNPC->DestinyMgr()->SendSpecialEffect(id, id, pNPC->GetSelf()->typeID(), pShip->GetID(), 0,
                    (shield ? "effects.ShieldTransfer" : "effects.RemoteArmourRepair"), false, true, true,
                    static_cast<int32>(rr.cycleMs), 1);
        }
    }
}

GPoint IncursionMgr::RoomPoint(IncLiveSite& site, uint8 room, const GPoint& offset)
{
    GPoint pos(site.rooms[room].center);
    pos += offset;
    return pos;
}

bool IncursionMgr::PilotsInRoom(IncLiveSite& site, SystemManager* pSys, uint8 room, std::vector<Client*>* into/*nullptr*/)
{
    std::map<uint8, IncLiveRoom>::iterator itr = site.rooms.find(room);
    if (itr == site.rooms.end())
        return false;
    SystemBubble* pBubble = sBubbleMgr.FindBubble(site.systemID, itr->second.center);
    if (pBubble == nullptr)
        return false;
    std::vector<Client*> list;
    pBubble->GetPlayers(list);
    bool found(false);
    for (auto cur : list) {
        if ((cur == nullptr) or (cur->GetShipSE() == nullptr))
            continue;
        found = true;
        if (into != nullptr)
            into->push_back(cur);
    }
    return found;
}

void IncursionMgr::TellRoom(IncLiveSite& site, SystemManager* pSys, uint8 room, const std::string& msg)
{
    std::vector<Client*> list;
    PilotsInRoom(site, pSys, room, &list);
    for (auto cur : list)
        cur->SendNotifyMsg("%s", msg.c_str());
}

SystemEntity* IncursionMgr::FindItem(IncLiveSite& site, SystemManager* pSys, uint16 typeID)
{
    for (auto& cur : site.items)
        if (cur.typeID == typeID) {
            SystemEntity* pSE = pSys->GetSE(cur.itemID);
            if (pSE != nullptr)
                return pSE;
        }
    return nullptr;
}

SystemEntity* IncursionMgr::SpawnItem(IncLiveSite& site, SystemManager* pSys, uint8 room, uint8 role, uint16 typeID, const std::string& name, const GPoint& pos, bool global/*false*/)
{
    const ItemType* pType = sItemFactory.GetType(typeID);
    if (pType == nullptr) {
        _log(COSMIC_MGR__ERROR, "IncursionMgr::SpawnItem() - type %u not found", typeID);
        return nullptr;
    }
    uint32 corpID = sDataMgr.GetFactionCorp(factionSanshas);
    std::string itemName(name.empty() ? pType->name() : name);
    ItemData data(typeID, corpID, site.systemID, flagNone, itemName.c_str(), pos, IncursionCustomInfo);

    SystemEntity* pSE(nullptr);
    if (IsContainerGroup(pType->groupID())) {
        CargoContainerRef cRef = sItemFactory.SpawnCargoContainer(data);
        if (cRef.get() == nullptr)
            return nullptr;
        FactionData fData = FactionData();
            fData.allianceID = factionSanshas;
            fData.corporationID = corpID;
            fData.factionID = factionSanshas;
            fData.ownerID = corpID;
        ContainerSE* cSE = new ContainerSE(cRef, pSys->GetServiceMgr(), pSys, fData);
        cRef->SetMySE(cSE);
        pSys->AddEntity(cSE);
        // anchored: no decay timer, and nobody scoops it
        cSE->AnchorContainer();
        /* the client data gives some of these an access difficulty, which makes them exploration containers.  only the
         *  logistics control arrays are hacked; the rest open as they are.  none gets exploration loot */
        if (typeID != IncType::LogisticsArray)
            cSE->Unlock();
        cSE->SetLootGenerated();
        pSE = cSE;
    } else {
        InventoryItemRef iRef = sItemFactory.SpawnItem(data);
        if (iRef.get() == nullptr)
            return nullptr;
        // beacons are global when spawned.  only the overview beacon stays that way
        if (!global and iRef->HasAttribute(AttrIsGlobal))
            iRef->SetAttribute(AttrIsGlobal, EvilZero, false);
        pSE = new CelestialSE(iRef, pSys->GetServiceMgr(), pSys);
        pSys->AddEntity(pSE, false);
    }

    if (pSE->SysBubble() != nullptr)
        pSE->SysBubble()->SetIncursion(true);   // keeps Sansha from warping off
    IncLiveItem item = IncLiveItem();
        item.itemID = pSE->GetID();
        item.typeID = typeID;
        item.room = room;
        item.role = role;
    site.items.push_back(item);
    if ((role == Incursion::Role::Gate) or (role == Incursion::Role::HiddenGate) or (role == Incursion::Role::Container))
        m_index[item.itemID] = site.siteID;
    return pSE;
}

void IncursionMgr::SpawnObject(IncLiveSite& site, SystemManager* pSys, const IncSiteObject& obj)
{
    GPoint pos(RoomPoint(site, obj.room, obj.offset));
    switch (obj.role) {
        case Incursion::Role::Structure:
        case Incursion::Role::Container: {
            SpawnItem(site, pSys, obj.room, obj.role, obj.typeID, obj.name, pos);
        } break;
        case Incursion::Role::Gate:
        case Incursion::Role::HiddenGate: {
            SystemEntity* pSE = SpawnItem(site, pSys, obj.room, obj.role, obj.typeID, obj.name, pos);
            if (pSE == nullptr)
                break;
            IncGate gate = IncGate();
                gate.room = obj.room;
                gate.destRoom = (uint8)obj.param;
                gate.lockRoom = (obj.role == Incursion::Role::Gate ? obj.wave : 0);
            site.gates[pSE->GetID()] = gate;
        } break;
        case Incursion::Role::Asteroid: {
            // a temp asteroid on the system's belt manager, like anomaly belts
            AsteroidData adata = AsteroidData();
                adata.beltID = 0;
                adata.systemID = site.systemID;
                adata.typeID = obj.typeID;
                adata.quantity = (obj.param > 0 ? obj.param : 1000);
                // the mining code's radius-to-quantity relation
                adata.radius = std::exp((adata.quantity + 112404.8) / 25000);
                adata.position = pos;
            ItemData idata(obj.typeID, ownerSystem, site.systemID, flagNone, "", pos);
            InventoryItemRef iRef = AsteroidItem::SpawnTemp(idata, adata);
            if (iRef.get() == nullptr)
                break;
            AsteroidSE* pASE = new AsteroidSE(iRef, pSys->GetServiceMgr(), pSys);
            pASE->SetMgr(pSys->GetBeltMgr(), 0);
            pSys->AddEntity(pASE, false);
            IncLiveItem item = IncLiveItem();
                item.itemID = pASE->GetID();
                item.typeID = obj.typeID;
                item.room = obj.room;
                item.role = obj.role;
            site.items.push_back(item);
            m_index[item.itemID] = site.siteID;
        } break;
        case Incursion::Role::Objective: {
            // a scripted objective (the True Power Mobile Headquarters)
            NPC* pNPC = SpawnShip(site, pSys, obj.room, obj.typeID, pos, obj.name);
            if (pNPC == nullptr)
                break;
            site.rooms[obj.room].others.push_back(pNPC->GetID());
            site.objectiveID = pNPC->GetID();
            site.objectiveRoom = obj.room;
        } break;
    }
}

void IncursionMgr::SpawnRoomObjects(IncLiveSite& site, SystemManager* pSys, uint8 room)
{
    std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(site.siteTypeID);
    if (itr == m_siteTypes.end())
        return;
    IncSiteType& type = itr->second;

    // each pocket has its own beacon, seen only there
    if (room > 0) {
        SystemEntity* pSE = SpawnItem(site, pSys, room, Incursion::Role::Structure, EVEDB::invTypes::CelestialBeacon, type.name, site.rooms[room].center);
        if (pSE != nullptr)
            site.rooms[room].beaconID = pSE->GetID();
    }

    for (auto& obj : type.objects) {
        if (obj.room != room)
            continue;
        switch (obj.role) {
            case Incursion::Role::Structure:
            case Incursion::Role::Gate:
            case Incursion::Role::Asteroid: {
                if (obj.wave != Incursion::Wave::Scripted)
                    SpawnObject(site, pSys, obj);
            } break;
            case Incursion::Role::Container: {
                if (obj.wave == 0)
                    SpawnObject(site, pSys, obj);
            } break;
        }
    }
}

NPC* IncursionMgr::SpawnShip(IncLiveSite& site, SystemManager* pSys, uint8 room, uint16 typeID, const GPoint& pos, const std::string& name/*""*/)
{
    float strength = sConfig.incursion.Strength / 100.0f;
    if (strength < 0.05f)
        strength = 0.05f;

    uint32 corpID = sDataMgr.GetFactionCorp(factionSanshas);
    FactionData data = FactionData();
        data.allianceID = factionSanshas;   // wreck salvage tests for faction
        data.corporationID = corpID;
        data.factionID = factionSanshas;
        data.ownerID = corpID;

    ItemData idata(typeID, corpID, site.systemID, flagNone, name.c_str(), pos);
    InventoryItemRef iRef = sItemFactory.SpawnItem(idata);
    if (iRef.get() == nullptr) {
        _log(COSMIC_MGR__ERROR, "IncursionMgr::SpawnShip() - unable to spawn NPC type %u", typeID);
        return nullptr;
    }

    // must happen before the NPC is built; it reads these in its constructor
    if (strength != 1.0f) {
        const uint16 scaled[] = { AttrShieldCapacity, AttrArmorHP, AttrHP, AttrEntityShieldBoostAmount,
                                  AttrDamageMultiplier, AttrMissileDamageMultiplier, AttrNpcRemoteShieldBoostAmount };
        for (auto attr : scaled)
            if (iRef->HasAttribute(attr))
                iRef->SetAttribute(attr, iRef->GetAttribute(attr).get_float() * strength, false);
    }

    // no spawn manager:  incursion ships don't warp off to belts
    NPC* pNPC = new NPC(iRef, pSys->GetServiceMgr(), pSys, data);
    if (!pNPC->Load()) {
        _log(COSMIC_MGR__ERROR, "IncursionMgr::SpawnShip() - failed to load NPC %u of type %u", pNPC->GetID(), typeID);
        pNPC->Delete();
        SafeDelete(pNPC);
        return nullptr;
    }
    pSys->AddNPC(pNPC);
    pNPC->DestinyMgr()->SetPosition(pos);
    if (pNPC->SysBubble() != nullptr)
        pNPC->SysBubble()->SetIncursion(true);
    /* every ship of a pocket is one aggro group:  they fight together, their logistics repair each other and a Vylade
     *  Dien's shield resist link covers them (npc/ai/NpcSupport.h) */
    if ((pSys->GetSpawnMgr() != nullptr) and (pNPC->GetAIMgr() != nullptr)) {
        uint64_t roomKey(RoomKey(site.siteID, room));
        pNPC->GetAIMgr()->SetGroup(pSys->GetSpawnMgr()->GetAggroGroups().Get(NpcAI::GroupKind::Incursion, roomKey,
                NpcAI::AggroMode::Proactive, 0, 0, roomKey));
        // a hacked logistics control array:  the mothership's logistics repair the pilots for now (RepairPilots)
        if (site.repairsOff and (site.siteTypeID >= Incursion::Site::KundaliniManifest))
            pNPC->GetAIMgr()->SetRemoteRepairsOff(true);
    }
    return pNPC;
}

void IncursionMgr::SpawnWave(IncLiveSite& site, SystemManager* pSys, uint8 room, uint8 wave, bool count/*true*/)
{
    std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(site.siteTypeID);
    if (itr == m_siteTypes.end())
        return;
    IncSiteType& type = itr->second;
    IncLiveRoom& live = site.rooms[room];
    if ((wave < Incursion::Wave::Mining) and (wave > live.wave))
        live.wave = wave;

    // scale the wave down for smaller fleets.  each entry is rounded up or down at random,
    //  so over a site the ship count matches the fleet size on average.  a wave never comes up empty.
    float ratio = sConfig.incursion.FleetScale / 100.0f;
    if (ratio > 1.0f)
        ratio = 1.0f;
    if (ratio < 0.01f)
        ratio = 0.01f;

    std::vector<std::pair<const IncSiteObject*, uint8>> spawn;
    std::vector<const IncSiteObject*> entries, pick;
    uint16 total(0), weight(0);
    for (auto& obj : type.objects) {
        if ((obj.room != room) or (obj.wave != wave) or (obj.role > Incursion::Role::Respawn))
            continue;
        if (obj.role == Incursion::Role::PickOne) {
            pick.push_back(&obj);
            continue;
        }
        entries.push_back(&obj);
        weight += obj.quantity;
        float want = obj.quantity * ratio;
        uint8 num = (uint8)want;
        if (MakeRandomFloat(0.0, 1.0) < (want - num))
            ++num;
        // objectives always show up in full
        if (obj.role == Incursion::Role::Objective)
            num = obj.quantity;
        if (num > 0) {
            spawn.push_back(std::make_pair(&obj, num));
            total += num;
        }
    }
    if (!pick.empty())
        spawn.push_back(std::make_pair(pick[RandomIndex(pick.size())], 1));
    if (entries.empty() and pick.empty())
        return;
    if ((total == 0) and pick.empty()) {
        // pick one ship, weighted by how many of each the full wave has
        float roll = MakeRandomFloat(0.0, (double)weight);
        const IncSiteObject* pObj = entries.back();
        for (auto obj : entries) {
            if (roll < obj->quantity) {
                pObj = obj;
                break;
            }
            roll -= obj->quantity;
        }
        spawn.push_back(std::make_pair(pObj, 1));
    }

    uint16 ships(0);
    for (auto cur : spawn) {
        const IncSiteObject* obj = cur.first;
        GPoint center(RoomPoint(site, room, obj->offset));
        IncRespawn group = IncRespawn();
            group.room = room;
            group.obj = obj;
            group.due = 0;
        for (uint8 i = 0; i < cur.second; ++i) {
            GPoint pos(center);
            pos.MakeRandomPointOnSphere(MakeRandomInt(1, 5) * 1000);
            NPC* pNPC = SpawnShip(site, pSys, room, obj->typeID, pos, obj->name);
            if (pNPC == nullptr)
                continue;
            uint32 id = pNPC->GetID();
            ++ships;
            switch (obj->role) {
                case Incursion::Role::Ship: {
                    live.npcs.push_back(id);
                    live.lastPos[id] = pos;
                } break;
                case Incursion::Role::Respawn: {
                    group.npcs.push_back(id);
                } break;
                case Incursion::Role::Objective:
                case Incursion::Role::PickOne: {
                    site.objectiveID = id;
                    site.objectiveRoom = room;
                    live.others.push_back(id);
                } break;
                default: {
                    live.others.push_back(id);
                } break;
            }
            if (count and (obj->role != Incursion::Role::Respawn) and (obj->role != Incursion::Role::PickOne))
                ++site.spawned;
        }
        if (!group.npcs.empty())
            site.respawns.push_back(group);
    }

    _log(COSMIC_MGR__MESSAGE, "IncursionMgr::SpawnWave() - %s in %s: pocket %u wave %u of %u, %u ships", \
            type.name.c_str(), pSys->GetName(), room, wave, type.waves[room], ships);
}

void IncursionMgr::StartRoom(IncLiveSite& site, SystemManager* pSys, uint8 room, uint32 now)
{
    site.rooms[room].started = true;
    SpawnWave(site, pSys, room, 1);
    switch (site.siteTypeID) {
        case Incursion::Site::ResearchCenter: {
            site.nextRefill = now;
        } break;
        case Incursion::Site::KundaliniManifest:
        case Incursion::Site::Uroborus: {
            site.nextReinforce = now + MakeRandomInt(60, 300);
            site.nextRefill = now + 60;
        } break;
        case Incursion::Site::OverrideTransferArray: {
            site.nextRefill = now + 5;      // the station's next repair
        } break;
    }
}

void IncursionMgr::WaveCleared(IncLiveSite& site, SystemManager* pSys, uint8 room, uint8 wave)
{
    std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(site.siteTypeID);
    if (itr == m_siteTypes.end())
        return;
    IncLiveRoom& live = site.rooms[room];
    if (wave <= live.waveDone)
        return;
    live.waveDone = wave;

    // containers waiting for this wave
    for (auto& obj : itr->second.objects)
        if ((obj.room == room) and (obj.role == Incursion::Role::Container) and (obj.wave == wave)) {
            SpawnObject(site, pSys, obj);
            if (obj.typeID == IncType::OreRefinery)
                TellRoom(site, pSys, room, "With the area clear, the Nation Ore Refinery has decloaked. Bring it the Lyavite.");
        }

    switch (site.siteTypeID) {
        case Incursion::Site::CivilianFacility: {
            // the last ship of each wave drops a container of 15 captured civilians where it died
            GPoint pos(live.lastKill);
            if (pos.isZero())
                pos = live.center;
            SystemEntity* pSE = SpawnItem(site, pSys, room, Incursion::Role::Container, IncType::CivilianWreck, "", pos);
            if (pSE != nullptr) {
                AddType(pSE, IncType::CapturedCivilians, 15);
                ++site.civilians;
                TellRoom(site, pSys, room, "A wrecked Sansha transport has spilled its cargo: 15 captured civilians.");
            }
        } break;
    }
}

void IncursionMgr::RoomCleared(IncLiveSite& site, SystemManager* pSys, uint8 room, uint32 now)
{
    std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(site.siteTypeID);
    if (itr == m_siteTypes.end())
        return;
    IncSiteType& type = itr->second;
    site.rooms[room].cleared = true;

    // gates waiting on this pocket.  locked ones open, hidden ones appear
    for (auto& cur : site.gates)
        if (cur.second.lockRoom == room) {
            cur.second.lockRoom = 0;
            TellRoom(site, pSys, cur.second.room, "The way is clear: the acceleration gate to the Fleet Staging Area is now active.");
        }
    for (auto& obj : type.objects)
        if ((obj.role == Incursion::Role::HiddenGate) and (obj.wave == room)) {
            SpawnObject(site, pSys, obj);
            TellRoom(site, pSys, room, "An acceleration gate has come online.");
        }

    switch (site.siteTypeID) {
        case Incursion::Site::CommanderOutpost:
        case Incursion::Site::RebirthFacility:
        case Incursion::Site::ConsolidationNetwork: {
            if (room == type.rooms)
                CompleteSite(site, pSys, room);
        } break;
        case Incursion::Site::OverrideTransferArray: {
            // the Eysturs don't stay for the end
            CompleteSite(site, pSys, room);
            std::vector<uint32> ships;
            for (auto& grp : site.respawns)
                ships.insert(ships.end(), grp.npcs.begin(), grp.npcs.end());
            for (auto id : ships)
                SendAway(site, pSys, pSys->GetNPCSE(id), now);
            site.respawns.clear();
        } break;
        case Incursion::Site::ProvisionalHQ: {
            if (room != type.rooms)
                break;
            // the station turns out to be the Mobile Headquarters, and now it can be destroyed
            for (auto& cur : site.items)
                if ((cur.room == room) and (cur.typeID == IncType::BattletowerLCO)) {
                    RemoveItem(site, pSys, cur.itemID);
                    break;
                }
            for (auto& obj : type.objects)
                if ((obj.room == room) and (obj.role == Incursion::Role::Objective) and (obj.wave == Incursion::Wave::Scripted))
                    SpawnObject(site, pSys, obj);
            TellRoom(site, pSys, room, "The last defenders are down. The True Power Mobile Headquarters is exposed.");
        } break;
    }
}

void IncursionMgr::SendAway(IncLiveSite& site, SystemManager* pSys, NPC* pNPC, uint32 now, bool returning/*false*/)
{
    if ((pNPC == nullptr) or (pNPC->GetAIMgr() == nullptr))
        return;
    uint32 id = pNPC->GetID();
    for (auto& cur : site.leaving)
        if (cur.first == id)
            return;
    for (auto& cur : site.rooms) {
        cur.second.npcs.erase(std::remove(cur.second.npcs.begin(), cur.second.npcs.end(), id), cur.second.npcs.end());
        cur.second.others.erase(std::remove(cur.second.others.begin(), cur.second.others.end(), id), cur.second.others.end());
    }
    for (auto& grp : site.respawns)
        grp.npcs.erase(std::remove(grp.npcs.begin(), grp.npcs.end(), id), grp.npcs.end());

    GVector dir(MakeRandomFloat(-1.0, 1.0), MakeRandomFloat(-0.2, 0.2), MakeRandomFloat(-1.0, 1.0));
    if (dir.length() < 0.01)
        dir = GVector(0, 0, 1);
    dir.normalize();
    GPoint to(pNPC->GetPosition());
    to += dir * (ONE_AU_IN_METERS * 5);
    // drops its targets, ewar and repairs on the way out
    pNPC->GetAIMgr()->BeginWarp();
    pNPC->DestinyMgr()->WarpTo(to, 0);
    site.leaving.push_back(std::make_pair(id, now + WARP_OFF_TIME));
    if (returning)
        site.away.push_back(std::make_pair(pNPC->GetSelf()->typeID(), now + 120));
}

void IncursionMgr::RemoveItem(IncLiveSite& site, SystemManager* pSys, uint32 itemID)
{
    if (itemID == 0)
        return;
    site.items.erase(std::remove_if(site.items.begin(), site.items.end(), [itemID](const IncLiveItem& i) { return i.itemID == itemID; }), site.items.end());
    site.gates.erase(itemID);
    m_index.erase(itemID);
    if (pSys == nullptr)
        return;
    SystemEntity* pSE = pSys->GetSE(itemID);
    if (pSE == nullptr)
        return;
    // the overview beacon was sent to every pilot in the system.  the bubble tells its own pilots
    if (pSE->isGlobal())
        pSys->RemoveMarkerBall(pSE);
    if (pSE->IsContainerSE()) {
        // CargoContainer::Delete() takes the entity out of space first
        pSE->GetSelf()->Delete();
    } else {
        pSE->Delete();
    }
    SafeDelete(pSE);
}

/* one site, once per tick.  returns true when the site should go */
bool IncursionMgr::ProcessSite(uint32 siteID, IncLiveSite& site, SystemManager* pSys, uint32 now)
{
    std::map<uint16, IncSiteType>::iterator tItr = m_siteTypes.find(site.siteTypeID);
    if (tItr == m_siteTypes.end())
        return true;
    IncSiteType& type = tItr->second;

    // ships warping off, once they're out of sight
    for (std::vector<std::pair<uint32, uint32>>::iterator itr = site.leaving.begin(); itr != site.leaving.end();) {
        if (now < itr->second) {
            ++itr;
            continue;
        }
        NPC* pNPC = pSys->GetNPCSE(itr->first);
        if (pNPC != nullptr) {
            if (pNPC->TargetMgr() != nullptr)
                pNPC->TargetMgr()->ClearFromTargets();
            pSys->RemoveNPC(pNPC);
        }
        itr = site.leaving.erase(itr);
    }

    // a finished site waits for its pilots to leave, gate grid included
    if (site.completed) {
        bool pilots(false);
        for (auto& cur : site.rooms)
            if (PilotsInRoom(site, pSys, cur.first)) {
                pilots = true;
                break;
            }
        if (pilots) {
            site.emptySince = 0;
        } else if (site.emptySince == 0) {
            site.emptySince = now;
        }
        return (((site.emptySince > 0) and (now - site.emptySince >= LINGER_EMPTY)) or (now - site.completedAt >= LINGER_MAX));
    }

    // pockets start when the first pilot arrives.  then track the dead
    for (auto& cur : site.rooms) {
        uint8 r = cur.first;
        IncLiveRoom& room = cur.second;
        if (r == 0)
            continue;
        if (!room.started) {
            if (!PilotsInRoom(site, pSys, r))
                continue;
            StartRoom(site, pSys, r, now);
        }
        for (std::vector<uint32>::iterator itr = room.npcs.begin(); itr != room.npcs.end();) {
            SystemEntity* pSE = pSys->GetSE(*itr);
            if (pSE == nullptr) {
                room.lastKill = room.lastPos[*itr];
                room.lastPos.erase(*itr);
                itr = room.npcs.erase(itr);
            } else {
                room.lastPos[*itr] = pSE->GetPosition();
                ++itr;
            }
        }
        room.others.erase(std::remove_if(room.others.begin(), room.others.end(),
                          [pSys](uint32 id) { return pSys->GetSE(id) == nullptr; }), room.others.end());
    }

    // groups that come back after they've been wiped out
    for (auto& grp : site.respawns) {
        grp.npcs.erase(std::remove_if(grp.npcs.begin(), grp.npcs.end(),
                       [pSys](uint32 id) { return pSys->GetSE(id) == nullptr; }), grp.npcs.end());
        if (!grp.npcs.empty())
            continue;
        if (grp.due == 0) {
            grp.due = now + (grp.obj->param > 10 ? grp.obj->param : 10);
            continue;
        }
        if (now < grp.due)
            continue;
        grp.due = 0;
        float ratio = sConfig.incursion.FleetScale / 100.0f;
        uint8 num = (uint8)(grp.obj->quantity * (ratio > 1.0f ? 1.0f : ratio) + 0.5f);
        if (num < 1)
            num = 1;
        GPoint center(RoomPoint(site, grp.room, grp.obj->offset));
        for (uint8 i = 0; i < num; ++i) {
            GPoint pos(center);
            pos.MakeRandomPointOnSphere(MakeRandomInt(1, 5) * 1000);
            NPC* pNPC = SpawnShip(site, pSys, grp.room, grp.obj->typeID, pos, grp.obj->name);
            if (pNPC != nullptr)
                grp.npcs.push_back(pNPC->GetID());
        }
    }

    // the objective is down: the site is done
    if ((site.objectiveID != 0) and (pSys->GetSE(site.objectiveID) == nullptr)) {
        site.objectiveID = 0;
        uint8 room = site.objectiveRoom;
        if (site.siteTypeID == Incursion::Site::CommanderStronghold) {
            // with their commander gone, the rest scatter
            std::vector<uint32> ships(site.rooms[room].npcs);
            ships.insert(ships.end(), site.rooms[room].others.begin(), site.rooms[room].others.end());
            for (auto id : ships)
                SendAway(site, pSys, pSys->GetNPCSE(id), now);
            TellRoom(site, pSys, room, "The Nation commander is dead. The remaining Sansha forces are fleeing.");
        }
        CompleteSite(site, pSys, room);
        return false;
    }

    ScriptTick(site, pSys, now);
    if (site.completed)
        return false;

    // the next wave comes when the last ship holding up this one dies
    for (auto& cur : site.rooms) {
        uint8 r = cur.first;
        IncLiveRoom& room = cur.second;
        if ((r == 0) or !room.started or room.cleared)
            continue;
        // these run on timers and their own triggers
        if ((r == 1) and ((site.siteTypeID == Incursion::Site::ResearchCenter) or (site.siteTypeID >= Incursion::Site::KundaliniManifest)))
            continue;
        if (!room.npcs.empty())
            continue;
        if (room.waveDone < room.wave)
            WaveCleared(site, pSys, r, room.wave);
        if (room.wave < type.waves[r]) {
            SpawnWave(site, pSys, r, room.wave + 1);
            continue;
        }
        // in the provisional headquarters' last pocket the waves follow the Ostingeles, but every ship has to die
        if ((site.siteTypeID == Incursion::Site::ProvisionalHQ) and (r == type.rooms) and !room.others.empty())
            continue;
        RoomCleared(site, pSys, r, now);
        if (site.completed)
            return false;
    }
    return false;
}

void IncursionMgr::ScriptTick(IncLiveSite& site, SystemManager* pSys, uint32 now)
{
    switch (site.siteTypeID) {
        case Incursion::Site::MiningColony: {
            // enough Lyavite in the refinery sets off the battle station
            SystemEntity* pRefinery = FindItem(site, pSys, IncType::OreRefinery);
            if ((pRefinery == nullptr) or (CountType(pRefinery, IncType::Lyavite) < 255))
                break;
            ConsumeType(pRefinery, IncType::Lyavite, 255);
            for (auto& cur : site.items)
                if (cur.typeID == IncType::BattletowerLCO) {
                    RemoveItem(site, pSys, cur.itemID);
                    break;
                }
            TellRoom(site, pSys, 1, "The Lyavite detonates during refining. The chain reaction tears the Sansha battle station apart.");
            CompleteSite(site, pSys, 1);
        } break;

        case Incursion::Site::OverrideTransferArray: {
            if (!site.rooms[1].started)
                break;
            if (site.repairsOff and (now >= site.repairsOffUntil)) {
                site.repairsOff = false;
                TellRoom(site, pSys, 1, "Sansha logistics systems are back online.");
            }
            if (site.repairsOff or (now < site.nextRefill))
                break;
            site.nextRefill = now + 5;
            // the Remote Logistics Station repairs the three most damaged Sansha ships near it
            SystemEntity* pStation = FindItem(site, pSys, IncType::LogisticsStation);
            if (pStation == nullptr)
                break;
            std::vector<std::pair<double, NPC*>> hurt;
            std::vector<uint32> ships(site.rooms[1].npcs);
            ships.insert(ships.end(), site.rooms[1].others.begin(), site.rooms[1].others.end());
            for (auto id : ships) {
                NPC* pNPC = pSys->GetNPCSE(id);
                if ((pNPC == nullptr) or (pNPC->GetPosition().distance(pStation->GetPosition()) > 150000))
                    continue;
                double capacity = pNPC->GetSelf()->GetAttribute(AttrShieldCapacity).get_double();
                if (capacity <= 0)
                    continue;
                double frac = pNPC->GetSelf()->GetAttribute(AttrShieldCharge).get_double() / capacity;
                if (frac < 0.95)
                    hurt.push_back(std::make_pair(frac, pNPC));
            }
            std::sort(hurt.begin(), hurt.end(), [](const std::pair<double, NPC*>& a, const std::pair<double, NPC*>& b) { return a.first < b.first; });
            for (size_t i = 0; (i < hurt.size()) and (i < 3); ++i) {
                NPC* pNPC(hurt[i].second);
                InventoryItemRef iRef(pNPC->GetSelf());
                double capacity(iRef->GetAttribute(AttrShieldCapacity).get_double());
                double charge(iRef->GetAttribute(AttrShieldCharge).get_double() + 1000.0 * sConfig.incursion.Strength / 100.0);
                iRef->SetAttribute(AttrShieldCharge, (charge > capacity ? capacity : charge));
                pNPC->SendDamageStateChanged();
                // the station's beam, through the ship's bubble
                if (pNPC->DestinyMgr() != nullptr)
                    pNPC->DestinyMgr()->SendSpecialEffect(pStation->GetID(), pStation->GetID(), pStation->GetTypeID(), pNPC->GetID(), 0,
                                                          "effects.ShieldTransfer", false, true, true, 5000, 1);
            }
        } break;

        case Incursion::Site::CivilianFacility: {
            IncLiveRoom& room = site.rooms[1];
            if (!site.unlocked) {
                if (room.wave < 4)
                    break;
                // most of wave 4 down: every tackler, and either the Romis and Renyns or the Antems
                uint16 tackle(0), close(0), antem(0);
                for (auto id : room.npcs) {
                    SystemEntity* pSE = pSys->GetSE(id);
                    if (pSE == nullptr)
                        continue;
                    switch (pSE->GetSelf()->typeID()) {
                        case IncType::Deltole:
                        case IncType::Auga:     ++tackle; break;
                        case IncType::Romi:
                        case IncType::Renyn:    ++close; break;
                        case IncType::Antem:    ++antem; break;
                    }
                }
                if ((tackle > 0) or ((close > 0) and (antem > 0)))
                    break;
                site.unlocked = true;
                GPoint pos(room.center);
                for (auto& cur : site.items)
                    if (cur.typeID == IncType::Obelisk) {
                        SystemEntity* pSE = pSys->GetSE(cur.itemID);
                        if (pSE != nullptr)
                            pos = pSE->GetPosition();
                        RemoveItem(site, pSys, cur.itemID);
                        break;
                    }
                SpawnItem(site, pSys, 1, Incursion::Role::Container, IncType::EvacFreighter, "", pos);
                TellRoom(site, pSys, 1, "The Emergency Evacuation Freighter is open. Load 40 captured civilians to evacuate them.");
                break;
            }
            SystemEntity* pFreighter = FindItem(site, pSys, IncType::EvacFreighter);
            if ((pFreighter == nullptr) or (CountType(pFreighter, IncType::CapturedCivilians) < 40))
                break;
            ConsumeType(pFreighter, IncType::CapturedCivilians, 40);
            TellRoom(site, pSys, 1, "The freighter is full. It warps out with the civilians.");
            CompleteSite(site, pSys, 1);
        } break;

        case Incursion::Site::ResearchCenter: {
            IncLiveRoom& room = site.rooms[1];
            if (!room.started)
                break;
            // the first uncloaked pilot brings the rest: Deltoles, then frigates, then Outunis about 90 s later
            if (site.triggerTime == 0) {
                std::vector<Client*> list;
                PilotsInRoom(site, pSys, 1, &list);
                for (auto cur : list)
                    if (!cur->GetShipSE()->DestinyMgr()->IsCloaked()) {
                        site.triggerTime = now;
                        break;
                    }
            } else {
                const uint32 delay[] = { 5, 45, 90 };
                for (uint8 i = 0; i < 3; ++i)
                    if ((room.wave == i + 1) and (now >= site.triggerTime + delay[i]))
                        SpawnWave(site, pSys, 1, i + 2);
            }

            // the shield transfer arrays keep the Battletower whole, unless an MTAC has jammed their control tower
            if (site.repairsOff and (now >= site.repairsOffUntil)) {
                site.repairsOff = false;
                TellRoom(site, pSys, 1, "The shield transfer arrays are back online.");
            }
            NPC* pTower = pSys->GetNPCSE(site.objectiveID);
            if (!site.repairsOff and (pTower != nullptr)) {
                InventoryItemRef tRef = pTower->GetSelf();
                double capacity = tRef->GetAttribute(AttrShieldCapacity).get_double();
                if ((tRef->GetAttribute(AttrShieldCharge).get_double() < capacity)
                or (tRef->GetAttribute(AttrArmorDamage).get_double() > 0) or (tRef->GetAttribute(AttrDamage).get_double() > 0)) {
                    tRef->SetAttribute(AttrShieldCharge, capacity, false);
                    tRef->SetAttribute(AttrArmorDamage, EvilZero, false);
                    tRef->SetAttribute(AttrDamage, EvilZero, false);
                    pTower->SendDamageStateChanged();
                }
            }

            // the factory turns out a new MTAC 20 s after the last one was taken.  it holds one at a time
            SystemEntity* pFactory = FindItem(site, pSys, IncType::MTACFactory);
            if ((pFactory != nullptr) and (CountType(pFactory, IncType::MTAC) == 0)) {
                if (site.nextRefill == 0) {
                    site.nextRefill = now + 20;
                } else if (now >= site.nextRefill) {
                    AddType(pFactory, IncType::MTAC, 1);
                    site.nextRefill = 0;
                }
            }
            // each MTAC delivered to the control tower stops the arrays for 90 s
            SystemEntity* pControl = FindItem(site, pSys, IncType::ControlTower);
            uint32 mtacs = CountType(pControl, IncType::MTAC);
            if (mtacs > 0) {
                ConsumeType(pControl, IncType::MTAC, mtacs);
                site.repairsOffUntil = (site.repairsOff ? site.repairsOffUntil : now) + 90 * mtacs;
                site.repairsOff = true;
                char buf[160];
                snprintf(buf, sizeof(buf), "The MTAC overrides the shield transfer control tower. The Battletower's shield transfer arrays are offline for %u seconds.", site.repairsOffUntil - now);
                TellRoom(site, pSys, 1, buf);
            }
        } break;

        case Incursion::Site::KundaliniManifest:
        case Incursion::Site::Uroborus: {
            IncLiveRoom& room = site.rooms[1];
            if (!room.started)
                break;
            std::map<uint16, IncSiteType>::iterator tItr = m_siteTypes.find(site.siteTypeID);
            if (tItr == m_siteTypes.end())
                break;
            IncSiteType& type = tItr->second;

            // escorts and bombers on a random timer, while there's room for them
            if (now >= site.nextReinforce) {
                site.nextReinforce = now + MakeRandomInt(60, 300);
                float ratio = sConfig.incursion.FleetScale / 100.0f;
                size_t cap = (size_t)(type.designNPCs * (ratio > 1.0f ? 1.0f : ratio) * 1.5f);
                if (cap < 10)
                    cap = 10;
                if ((room.npcs.size() + room.others.size() < cap) and (type.waves[1] > 1))
                    SpawnWave(site, pSys, 1, MakeRandomInt(2, type.waves[1]), false);
            }

            // a hacked logistics array: Sansha logistics stop repairing each other and repair the pilots instead
            if (site.repairsOff and (now >= site.repairsOffUntil)) {
                site.repairsOff = false;
                TellRoom(site, pSys, 1, "Sansha logistics systems are back under Nation control.");
                std::vector<uint32> ships(room.npcs);
                ships.insert(ships.end(), room.others.begin(), room.others.end());
                for (auto id : ships) {
                    NPC* pNPC = pSys->GetNPCSE(id);
                    if ((pNPC != nullptr) and (pNPC->GetAIMgr() != nullptr))
                        pNPC->GetAIMgr()->SetRemoteRepairsOff(false);
                }
                site.repairAt.clear();
            } else if (site.repairsOff) {
                std::vector<uint32> ships(room.npcs);
                ships.insert(ships.end(), room.others.begin(), room.others.end());
                RepairPilots(site, pSys, ships);
            }

            // the nexus makes a transmitter two minutes after the last one was taken
            SystemEntity* pNexus = FindItem(site, pSys, IncType::ControlNexus);
            if ((pNexus != nullptr) and (CountType(pNexus, IncType::Transmitter) == 0)) {
                if (site.nextRefill == 0) {
                    site.nextRefill = now + 120;
                } else if (now >= site.nextRefill) {
                    AddType(pNexus, IncType::Transmitter, 1);
                    site.nextRefill = 0;
                }
            }
            // a transmitter in the relay sends a third of the escorts away for two minutes, Maras first
            SystemEntity* pRelay = FindItem(site, pSys, IncType::ControlRelay);
            if (CountType(pRelay, IncType::Transmitter) > 0) {
                ConsumeType(pRelay, IncType::Transmitter, 1);
                std::vector<NPC*> escorts;
                std::vector<uint32> ships(room.npcs);
                ships.insert(ships.end(), room.others.begin(), room.others.end());
                for (auto id : ships) {
                    NPC* pNPC = pSys->GetNPCSE(id);
                    if ((pNPC != nullptr) and (id != site.objectiveID))
                        escorts.push_back(pNPC);
                }
                std::stable_sort(escorts.begin(), escorts.end(), [](NPC* a, NPC* b) {
                    return (a->GetSelf()->typeID() == IncType::Mara) and (b->GetSelf()->typeID() != IncType::Mara); });
                size_t send = (escorts.size() + 2) / 3;
                for (size_t i = 0; i < send; ++i)
                    SendAway(site, pSys, escorts[i], now, true);
                char buf[160];
                snprintf(buf, sizeof(buf), "The Tactical Response Transmitter relays false orders: %u Sansha ships are leaving the area.", (uint32)send);
                TellRoom(site, pSys, 1, buf);
            }
            // and they come back
            for (std::vector<std::pair<uint16, uint32>>::iterator itr = site.away.begin(); itr != site.away.end();) {
                if (now < itr->second) {
                    ++itr;
                    continue;
                }
                GPoint pos(room.center);
                pos.MakeRandomPointOnSphereLayer(30000, 60000);
                NPC* pNPC = SpawnShip(site, pSys, 1, itr->first, pos);
                if (pNPC != nullptr)
                    room.others.push_back(pNPC->GetID());
                itr = site.away.erase(itr);
            }
        } break;
    }
}

/* KeeperService::ActivateAccelerationGate().  pilots use a gate within range of its surface, not scrambled,
 *  and in the right class of ship on the Consolidation Network's split gates.  they land near the pocket's beacon
 */
bool IncursionMgr::UseGate(Client* pClient, uint32 gateID)
{
    std::map<uint32, uint32>::iterator iItr = m_index.find(gateID);
    if (iItr == m_index.end())
        return false;
    std::map<uint32, IncLiveSite>::iterator sItr = m_sites.find(iItr->second);
    if (sItr == m_sites.end())
        return false;
    IncLiveSite& site = sItr->second;
    std::map<uint32, IncGate>::iterator gItr = site.gates.find(gateID);
    if (gItr == site.gates.end())
        return false;
    IncGate& gate = gItr->second;

    ShipSE* pShipSE = pClient->GetShipSE();
    SystemManager* pSys = pClient->SystemMgr();
    if ((pShipSE == nullptr) or (pSys == nullptr))
        throw CustomError("You must be in space to use an acceleration gate.");
    SystemEntity* pGate = pSys->GetSE(gateID);
    if (pGate == nullptr)
        throw CustomError("That acceleration gate is no longer there.");
    double gap = pShipSE->GetPosition().distance(pGate->GetPosition()) - pGate->GetRadius() - pShipSE->GetRadius();
    if (gap > GATE_RANGE)
        throw CustomError("You are %.0f m from the acceleration gate. Get within %.0f m to use it.", gap, GATE_RANGE);
    if (pShipSE->GetSelf()->GetAttribute(AttrWarpScrambleStatus).get_float() > 0)
        throw CustomError("Your warp drive is disrupted. The acceleration gate cannot accelerate your ship.");
    if (gate.lockRoom != 0)
        throw CustomError("This acceleration gate is locked. Sansha forces still hold the other side of the Consolidation Network.");

    // the Consolidation Network splits the fleet by hull size (wiki class lists)
    if ((site.siteTypeID == Incursion::Site::ConsolidationNetwork) and (gate.room == 0) and sConfig.incursion.GateRestrictions) {
        static const std::set<uint16> cruiserSide = {
            EVEDB::invGroups::Frigate, EVEDB::invGroups::Cruiser, EVEDB::invGroups::Industrial, EVEDB::invGroups::Capsule,
            EVEDB::invGroups::Shuttle, EVEDB::invGroups::Rookieship, EVEDB::invGroups::AssaultShip, EVEDB::invGroups::HeavyAssaultShip,
            EVEDB::invGroups::TransportShip, EVEDB::invGroups::Battlecruiser, EVEDB::invGroups::Destroyer,
            EVEDB::invGroups::Interdictor, EVEDB::invGroups::CommandShip, EVEDB::invGroups::CovertOps,
            EVEDB::invGroups::Interceptor, EVEDB::invGroups::Logistics, 833 /* Force Recon Ship */,
            EVEDB::invGroups::StealthBomber, EVEDB::invGroups::ElecAttackShip, EVEDB::invGroups::HeavyInterdictors,
            EVEDB::invGroups::CombatRecon, EVEDB::invGroups::StrategicCruiser };
        static const std::set<uint16> battleshipSide = {
            EVEDB::invGroups::Battleship, EVEDB::invGroups::EliteBattleship, EVEDB::invGroups::BlackOps,
            EVEDB::invGroups::Marauder, EVEDB::invGroups::MiningBarge, EVEDB::invGroups::Exhumer,
            EVEDB::invGroups::Freighter, EVEDB::invGroups::JumpFreighter, EVEDB::invGroups::IndustrialCommandShip,
            EVEDB::invGroups::Capsule };
        uint16 groupID = pShipSE->GetSelf()->groupID();
        if ((gate.destRoom == 1) and !cruiserSide.count(groupID))
            throw CustomError("This gate only takes cruisers, battlecruisers, destroyers, frigates, logistics and industrials.");
        if ((gate.destRoom == 2) and !battleshipSide.count(groupID))
            throw CustomError("This gate only takes battleships, mining barges and large industrials.");
    }

    std::map<uint8, IncLiveRoom>::iterator rItr = site.rooms.find(gate.destRoom);
    if (rItr == site.rooms.end())
        throw CustomError("This acceleration gate leads nowhere.");

    pShipSE->DestinyMgr()->SendSpecialEffect10(gateID, 0, "effects.WarpGateEffect", 0, 1, 0);
    GPoint dest(rItr->second.center);
    dest.MakeRandomPointOnSphereLayer(500, LANDING_SPREAD);
    pShipSE->DestinyMgr()->WarpTo(dest, 0);
    return true;
}

/* Prospector: a data analyzer cracked one of our containers */
bool IncursionMgr::Hacked(Client* pClient, SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or (pClient == nullptr))
        return false;
    std::map<uint32, uint32>::iterator iItr = m_index.find(pTarget->GetID());
    if (iItr == m_index.end())
        return false;
    std::map<uint32, IncLiveSite>::iterator sItr = m_sites.find(iItr->second);
    if (sItr == m_sites.end())
        return false;
    IncLiveSite& site = sItr->second;
    if (pTarget->GetSelf()->typeID() != IncType::LogisticsArray)
        return false;
    SystemManager* pSys = pClient->SystemMgr();
    if ((pSys == nullptr) or site.completed)
        return true;

    uint32 now = TimeNow();
    switch (site.siteTypeID) {
        case Incursion::Site::OverrideTransferArray: {
            // each array works once.  six minutes without the station's repairs
            if (site.hacked.count(pTarget->GetID())) {
                pClient->SendNotifyMsg("This logistics control array has already been subverted.");
                return true;
            }
            site.hacked.insert(pTarget->GetID());
            site.repairsOffUntil = now + 360;
            site.repairsOff = true;
            TellRoom(site, pSys, 1, "Local Sansha logistics systems subverted. Hostile logistics disabled.");
        } break;
        case Incursion::Site::KundaliniManifest:
        case Incursion::Site::Uroborus: {
            site.repairsOffUntil = now + 360;
            site.repairsOff = true;
            for (auto& cur : site.rooms) {
                std::vector<uint32> ships(cur.second.npcs);
                ships.insert(ships.end(), cur.second.others.begin(), cur.second.others.end());
                for (auto id : ships) {
                    NPC* pNPC = pSys->GetNPCSE(id);
                    if ((pNPC != nullptr) and (pNPC->GetAIMgr() != nullptr))
                        pNPC->GetAIMgr()->SetRemoteRepairsOff(true);
                }
            }
            TellRoom(site, pSys, 1, "Local Sansha logistics systems subverted. Sansha logistics are now repairing your fleet.");
        } break;
        default:
            return false;
    }
    return true;
}

/* mining lasers and mining drones took ore from one of our rocks.  each time, a chance of more ships */
void IncursionMgr::OreMined(SystemEntity* pRoid)
{
    if (pRoid == nullptr)
        return;
    std::map<uint32, uint32>::iterator iItr = m_index.find(pRoid->GetID());
    if (iItr == m_index.end())
        return;
    std::map<uint32, IncLiveSite>::iterator sItr = m_sites.find(iItr->second);
    if (sItr == m_sites.end())
        return;
    IncLiveSite& site = sItr->second;
    if (site.completed or (site.siteTypeID != Incursion::Site::MiningColony))
        return;
    uint32 now = TimeNow();
    if (now < site.nextMineSpawn)
        return;
    site.nextMineSpawn = now + 30;
    if (MakeRandomFloat(0.0, 1.0) > 0.2)
        return;
    SystemManager* pSys = pRoid->SystemMgr();
    if (pSys == nullptr)
        return;
    uint8 room(1);
    for (auto& cur : site.items)
        if (cur.itemID == pRoid->GetID())
            room = cur.room;
    SpawnWave(site, pSys, room, Incursion::Wave::Mining, false);
    TellRoom(site, pSys, room, "Your mining has drawn the attention of the colony's guards.");
}

/* pockets are deadspace:  bookmarks and fleet warps into one land at the site's beacon, and the gates are the way in */
bool IncursionMgr::RedirectWarp(uint32 systemID, const GPoint& dest, GPoint& into)
{
    for (auto& cur : m_sites) {
        IncLiveSite& site = cur.second;
        if (site.systemID != systemID)
            continue;
        for (auto& room : site.rooms) {
            if ((room.first == 0) or (room.second.center.distance(dest) >= BUBBLE_RADIUS_METERS))
                continue;
            into = site.position;
            into.MakeRandomPointOnSphere(2500);
            return true;
        }
    }
    return false;
}
