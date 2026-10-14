/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.
    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
    Author:     Zhur
    Rewrite:    Allan
    Updates:    James
*/

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "account/AccountService.h"
#include "chat/LSCService.h"
#include "exploration/Probes.h"
#include "fleet/FleetService.h"
#include "map/MapData.h"
#include "map/MapDB.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/Sentry.h"
#include "npc/SentryAI.h"
#include "packets/Destiny.h"
#include "planet/Planet.h"
#include "planet/Moon.h"
#include "planet/CustomsOffice.h"
#include "planet/PlanetDB.h"
#include "pos/Array.h"
#include "pos/Battery.h"
#include "pos/Module.h"
#include "pos/Structure.h"
#include "pos/Tower.h"
#include "pos/sovStructures/TCU.h"
#include "pos/sovStructures/SBU.h"
#include "pos/sovStructures/IHub.h"
#include "pos/JumpBridge.h"
#include "station/Outpost.h"
#include "pos/Weapon.h"
#include "ship/Missile.h"
#include "ship/Ship.h"
#include "station/Station.h"
#include "system/Asteroid.h"
#include "system/Container.h"
#include "system/Damage.h"
#include "system/DestinyCollision.h"
#include "system/DestinyManager.h"
#include "system/SolarSystem.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/WarpInPoint.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/cosmicMgrs/DungeonMgr.h"
#include "system/cosmicMgrs/SpawnMgr.h"
#include "npc/LawEnforcement.h"
#include "npc/ambient/AmbientNPC.h"
#include "npc/ambient/AmbientSystem.h"
#include "system/CrimeWatch.h"
#include "system/Interdiction.h"
#include "ConsoleCommands.h"
#include "station/Outpost.h"
#include "services/ServiceManager.h"

SystemManager::SystemManager(uint32 systemID, EVEServiceManager &svc)
:m_services(svc),
m_bountyTimer(0),
m_minutetimer(0, true),
m_anomMgr(new AnomalyMgr(this, svc)),
m_beltMgr(new BeltMgr(this, svc)),
m_dungMgr(new DungeonMgr(this, svc)),
m_spawnMgr(new SpawnMgr(this, svc)),
m_law(new LawEnforcement(this, svc)),
m_ambient(nullptr),
m_interdiction(new InterdictionMgr(this)),
m_loaded(false),
m_entityChanged(false),
m_docked(0),
m_players(0),
m_beltCount(0),
m_gateCount(0),
m_activityTime(0),
m_activeRatSpawns(0),
m_activeGateSpawns(0),
m_activeRoidSpawns(0),
m_secValue(1.1f),
m_tutorialSiteID(0),
m_droneIndexStamp(0),
m_droneIndexBusy(0),
m_droneIndexDirty(true),
m_aiTick(0),
m_inSpaceTick(0),
m_inSpace(0)
{
    m_ambient = new AmbientSystem(this);
    m_minutes = 0;

    m_npcs.clear();
    m_clients.clear();
    m_jumpMap.clear();
    m_moonMap.clear();
    m_entities.clear();
    m_planetMap.clear();
    m_gateMap.clear();
    m_ratBubbles.clear();
    m_beltVector.clear();
    m_roidBubbles.clear();
    m_ticEntities.clear();
    m_staticEntities.clear();
    m_opStaticEntities.clear();

    // zero-init our data containers
    m_data = SystemData();
    m_killData = SystemKillData();

    sDataMgr.GetSystemData(systemID, m_data);   // system data is now an internal memory (cached) object.  db is hit once at system boot.
    m_secValue -= m_data.securityRating;  // range is 0.1 for 1.0 system to 2.0 for -0.9 system

    _log(COMMON__MESSAGE, "Created SystemManager %p for System %s(%u)", this, m_data.name.c_str(), m_data.systemID);

    this->m_lsc = svc.Lookup <LSCService>("LSC");
}

SystemManager::~SystemManager() {
    if (m_players or !m_clients.empty()) {
        _log(COMMON__ERROR, "D'tor called for System %u with %u players and/or %lu clients in mmaps", m_data.systemID, m_players, m_clients.size());
        for (auto cur : m_clients)
            sEntityList.Remove(cur.second);
    }

    if (m_loaded)
        UnloadSystem();
    // a system that couldn't be unloaded still has its bubbles:  free their marker cans while we're here to remove them
    //  (after a real unload there are no bubbles left, and this does nothing)
    sBubbleMgr.RemoveMarkers(m_data.systemID);

    SafeDelete(m_dungMgr);
    SafeDelete(m_anomMgr);
    SafeDelete(m_beltMgr);
    SafeDelete(m_spawnMgr);
    SafeDelete(m_law);
    SafeDelete(m_ambient);
    SafeDelete(m_interdiction);
}

bool SystemManager::BootSystem() {
    // dont fuck with this order...

    m_solarSystemRef = sItemFactory.GetSolarSystemRef(m_data.systemID);
    assert(m_solarSystemRef.get() != nullptr);

    if (!LoadSystemStatics()) {
        _log(SERVICE__ERROR, "Unable to load System Statics during boot of system %u.", m_data.systemID);
        return false;
    }

    if (!LoadCosmicMgrs()) {
        _log(SERVICE__ERROR, "Unable to load Cosmic Managers during boot of system %u.", m_data.systemID);
        return false;
    }

    if (!LoadSystemDynamics()) {
        _log(SERVICE__ERROR, "Unable to load System Dynamics during boot of system %u.", m_data.systemID);
        return false;
    }

    if (!LoadPlayerDynamics()) {
        _log(SERVICE__ERROR, "Unable to load System Dynamics during boot of system %u.", m_data.systemID);
        return false;
    }

    // check for operational static entities which need to be initialized (such as sovereignty structures)
    for (auto cur: m_opStaticEntities)
        if (cur.second ->IsOperSE())
            cur.second->GetPOSSE()->Init();
    // sovereignty blockade:  the claim's count and its TCU/hub vulnerability from the SBUs actually here
    svDataMgr.UpdateBlockade(this, 0, false);

    // gate and station guns
    m_law->Init();

    // system is loaded.  check for items that need initialization
    for (auto cur : m_ticEntities)
        if (cur.second->IsPOSSE())
            cur.second->GetPOSSE()->Init();

    // Crucible:  every planet has a customs office.  the npc office is spawned once per planet; one that was destroyed
    //  stays gone (players anchor their own)
    if (sConfig.cosmic.PIEnabled and sConfig.cosmic.PISpawnAllCustomsOffices and !m_planetMap.empty()) {
        std::vector<uint32> planetIDs;
        for (auto cur : m_planetMap)
            if (!cur.second->GetPlanetSE()->HasCOSE())
                planetIDs.push_back(cur.first);
        std::set<uint32> spawned;
        PlanetDB::GetSpawnedCustomsOffices(planetIDs, spawned);
        for (auto cur : planetIDs)
            if (spawned.find(cur) == spawned.end())
                m_planetMap[cur]->GetPlanetSE()->CreateCustomsOffice();
    }

    if (sConfig.server.BountyPayoutDelayed)
        m_bountyTimer.Start(sConfig.server.BountyPayoutTimer * 60 * 1000);

    //create our chat channels
    this->m_lsc->CreateSystemChannel(m_data.regionID);
    this->m_lsc->CreateSystemChannel(m_data.constellationID);
    this->m_lsc->CreateSystemChannel(m_data.systemID);

    // set system active for system status page
    MapDB::SetSystemActive(m_data.systemID, true);

    // load dynamic map data
    MapDB::LoadDynamicData(m_data.systemID, m_killData);

    //start minute timer
    m_minutetimer.Start(60000);

    return (m_loaded = true);
}

bool SystemManager::LoadCosmicMgrs()
{
    if (m_beltCount)
        m_beltMgr->Init();  //nothing to check for in this init.

    if (!m_spawnMgr->Init()) {
        _log(SERVER__INIT_ERR, "Unable to load Spawn Manager during boot of system %u.", m_data.systemID);
        return false;
    }

    if (!m_dungMgr->Init(m_anomMgr, m_spawnMgr)) {
        _log(SERVER__INIT_ERR, "Unable to load Dungeon Manager during boot of system %u.", m_data.systemID);
        return false;
    }

    if (!m_anomMgr->Init(m_beltMgr, m_dungMgr, m_spawnMgr)) {
        _log(SERVER__INIT_ERR, "Unable to load Anomaly Manager during boot of system %u.", m_data.systemID);
        return false;
    }

    return true;
}

//called once per second from EntityList. (1Hz Tic)
bool SystemManager::ProcessTic() {
    double profileStartTime(GetTimeUSeconds());

    // npcs and sentries that left space last tick (killed, GM /unspawn):  nothing holds them now
    if (!m_removedNPCs.empty())
        FreeRemovedNPCs();
    // npc ai:  the level of detail's clock, the budget and telemetry of this tick (npc/ai/NpcLod.h, NpcTelemetry.h)
    ++m_aiTick;
    if (m_npcStats.windowStart == 0)
        m_npcStats.Reset(GetSteadyTime());
    m_npcStats.BeginTick();

    /* the idea here is entities map NEVER has invalid items in it, but our iterator may become invalid
     *      when SE->Process() returns because Process() will add/remove from the map as needed
     *      (new objects, destroyed objects, moved objects, etc)
     *  std::map internally orders items by key(itemID here), so use an int var to hold last-processed itemID (mLast).
     *  when iteration starts over, increment until cur > mLast and continue from there to end of list.
     */
    std::map<uint32, SystemEntity*>::iterator itr = m_ticEntities.begin();
    uint32 mLast(0);
    while (itr != m_ticEntities.end()) {
        if (mLast >= itr->first) {
            ++itr;
            continue;
        }

         /* main process call. */
        mLast = itr->first;     // not sure if this will slow this down or not.  check profile
        itr->second->Process();

        if (m_entityChanged) {
            // the map changed under us:  carry on after the last one we processed (bug N7:  restarting from begin()
            //  walked the whole map again for every missile, wreck or npc added or removed in a tick)
            m_entityChanged = false;
            itr = m_ticEntities.upper_bound(mLast);
            continue;
        }
        ++itr;
    }

    // tic for sov structures (as they aren't in ticEntities)
    for (auto cur : m_opStaticEntities)
        if (cur.second->IsOperSE())
            cur.second->GetPOSSE()->Process();
    // check bounty timer
    if (m_bountyTimer.Check(sConfig.server.BountyPayoutDelayed))
        PayBounties();
    /* the following are coded for single-tic calls */
    m_anomMgr->Process();
    if (m_beltCount)
        m_beltMgr->Process();
    m_dungMgr->Process();
    m_spawnMgr->Process();
    m_law->Process();
    // ambient npcs (one branch while no player is in space here)
    m_ambient->Process();

    // customs office and gantry timers (anchoring, upgrading, unanchoring, reinforcement)
    for (auto cur : m_planetMap) {
        CustomsSE* pCO(cur.second->GetPlanetSE()->GetCustomsOffice());
        if ((pCO != nullptr) and pCO->IsInTimedState())
            pCO->ProcessState();
    }

    // drones that reached their ship during this tick
    ProcessDroneReturns();
    // self-destruct timers (outside the entity loop, as a ship may explode here)
    ProcessSelfDestructs();

    // npc ai telemetry (npc ai plan phase 5):  one NPC__AI_STATS line per system every npc.StatsInterval
    m_npcStats.EndTick();
    if (sConfig.npc.StatsInterval > 0) {
        int64 now(GetSteadyTime());
        if ((now - m_npcStats.windowStart) >= (static_cast<int64>(sConfig.npc.StatsInterval) * 1000)) {
            if (!m_npcStats.Empty() and is_log_enabled(NPC__AI_STATS))
                _log(NPC__AI_STATS, "%s", m_npcStats.Format(m_data.name.c_str(), m_data.systemID, now).c_str());
            m_npcStats.Reset(now);
        }
    }

    // process planets for PI
    if (m_minutetimer.Check()) {
        //++m_minutes;  // not used at this time
        for (auto cur : m_planetMap)
            cur.second->Process();
    }

    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(Profile::system, GetTimeUSeconds() - profileStartTime);

    return SystemActivity();
}

/* runs one tick of movement for every entity, the same way the client's Ballpark::Evolve() does:
 *  every entity's thrust is worked out from the same snapshot before any entity moves, then all are moved.
 *  this tick's evolve starts from the previous stamp, as EntityList has already advanced to the current one.
 *  EntityList calls this for every system before anything else runs for the new stamp, so commands issued during
 *  this stamp are applied to the state at this stamp, as they are on the client.
 */
void SystemManager::ProcessDestiny() {
    int32 tick(static_cast<int32>(sEntityList.GetStamp()) - 1);

    std::vector<std::pair<uint32, DestinyManager*>> movers;
    movers.reserve(m_ticEntities.size());
    for (auto cur : m_ticEntities) {
        if ((cur.second == nullptr) or cur.second->IsDead())
            continue;
        DestinyManager* pDestiny(cur.second->DestinyMgr());
        if (pDestiny != nullptr)
            movers.emplace_back(cur.first, pDestiny);
    }

    for (auto cur : movers)
        cur.second->EvolveThrust(tick);
    // collisions see every ball's thrust and position from the start of the tick, so they go between the two passes
    double profileStartTime(GetTimeUSeconds());
    Destiny::BallparkSnapshot ballpark(this);
    for (auto cur : movers)
        cur.second->EvolveCollide(ballpark);
    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(Profile::collision, GetTimeUSeconds() - profileStartTime);
    for (auto cur : movers)
        cur.second->EvolveMove(tick);

    // post-move work can add or remove entities, so look each one up again
    for (auto cur : movers) {
        SystemEntity* pSE(GetSE(cur.first));
        if ((pSE != nullptr) and (pSE->DestinyMgr() == cur.second))
            cur.second->PostEvolve();
    }
}

bool SystemManager::SystemActivity() {
    if (m_activityTime == 0)
        return true;
    if ((sEntityList.GetStamp() - m_activityTime) > 60)
        return false;

    return true;
}

// called from EntityList::Process() and EntityList::Close()
void SystemManager::UnloadSystem() {
    if (!m_loaded or !SafeToUnload())
        return;

    sLog.Magenta("    SystemManager", "UnloadSystem() called for %s(%u).", m_data.name.c_str(), m_data.systemID);

    // .bubbletrack marker cans go the normal way first (their temp items too).  the global center marker isn't in
    //  m_staticEntities, so the entity loop below can't take it
    sBubbleMgr.RemoveMarkers(m_data.systemID);

    // system is being unloaded.  pay bounties now (PayBounties doesn't throw)
    PayBounties();
    
    // law enforcement forgets its entities (they're deleted below)
    m_law->Close();
    // so do the ambient npcs
    m_ambient->Close();
    // so do the warp disruption fields
    m_interdiction->Clear();

    // unload belts, which saves and removes roids from system
    m_beltMgr->ClearAll();
    // close anomaly mgr, which saves and removes sigs from system
    m_anomMgr->Close();
    // remove live dungeons (their items don't outlive the system)
    m_dungMgr->Close();

    // drones are deleted with the rest of the entities below.  nothing may look them up meanwhile
    m_drones.clear();
    m_dronesByController.clear();
    m_dronesByGuardChar.clear();
    m_droneIndexStamp = 0;
    m_droneIndexDirty = true;
    m_droneReturns.clear();

    // npcs and sentries already out of space (killed, removed):  ours to free
    FreeRemovedNPCs();

    // remove static and dynamic entities
    std::map<uint32, SystemEntity*>::iterator itr = m_entities.begin();
    SystemEntity* pSE(nullptr);
    while (itr != m_entities.end()) {
        if ((itr->first == 0) or (itr->second == nullptr)) {
            itr = m_entities.erase(itr);
            continue;
        }
        pSE = itr->second;

        if (pSE->TargetMgr() != nullptr)
            pSE->TargetMgr()->Unload();

        if (pSE->IsStaticEntity() or pSE->isGlobal()) {
            if (pSE->IsStationSE()) {
                pSE->GetStationSE()->UnloadStation();
                sEntityList.RemoveStation(itr->first);
            }
            m_staticEntities.erase(m_staticEntities.find(itr->first));
        } else if (pSE->IsShipSE()) {
            pSE->GetShipSE()->GetShipItemRef()->LogOut();
        } else if (pSE->IsNPCSE()) {
            sEntityList.RemoveNPC();    // this is for loaded npc count.
            pSE->GetSelf()->Delete();
        } else if (pSE->IsProbeSE()) {
            sEntityList.RemoveProbe(itr->first);
        }

        if (pSE->IsOperSE()) { //Remove operational statics from list
            m_opStaticEntities.erase(m_opStaticEntities.find(itr->first));
        }

        sItemFactory.RemoveItem(itr->first);
        itr = m_entities.erase(itr);
        sBubbleMgr.Remove(pSE);
        SafeDelete(pSE);
    }

    // save items, then remove from system inventory, item factory and decrement item count
    m_solarSystemRef->GetMyInventory()->Unload();
    _log(PHYSICS__MESSAGE, "SystemManager::UnloadSystem() - map count after unload: %lu npcs, %lu entities, %lu statics.", \
                m_npcs.size(), m_entities.size(), m_staticEntities.size());

    // this is dupe container. contents unloaded in another call
    m_npcs.clear();
    // at this point, system entity list should be clear...but just in case, hit it again
    m_entities.clear();
    // this is dupe container. contents unloaded in another call
    m_ticEntities.clear();
    // at this point, system static entity list should be clear...but just in case, hit it again
    m_staticEntities.clear();
    // clear operational static entity list too
    m_opStaticEntities.clear();

    // this still needs some work... seems ok to me.  26Dec18
    sBubbleMgr.ClearSystemBubbles(m_data.systemID);

    // remove dungeon shit for this system?
    //  are we saving it for later or is it make-on-boot?
    ManagerDB::ClearDungeons(m_data.systemID);

    // set system inactive for system status page
    MapDB::SetSystemActive(m_data.systemID, false);

    /** @todo finish this for lsc */
    this->m_lsc->SystemUnload(m_data.systemID, m_data.constellationID, m_data.regionID);

    // remove solar system item from ItemFactory
    sItemFactory.RemoveItem(m_data.systemID);
    m_loaded = false;
}

bool SystemManager::LoadSystemStatics() {
    std::vector<DBSystemEntity> entities;
    entities.clear();
    m_entities.clear();
    m_staticEntities.clear();
    if (!SystemDB::LoadSystemStaticEntities(m_data.systemID, entities)) {
        sLog.Error( "SystemManager::LoadSystemStatics()", "Unable to load celestial entities during boot of %s(%u).", m_data.name.c_str(), m_data.systemID);
        return false;
    }

    SystemEntity* pSE(nullptr);
    for (auto cur : entities) {
        switch (cur.groupID) {
            case EVEDB::invGroups::Station: {
                /** @todo (Allan) outposts are group::station - may need to hack this */
                /*  types 12242 - 22298 in group 15 are outposts */
                /*  types 29323 - 29390 in group 15 are wrecked stations */
                StationItemRef itemRef = sItemFactory.GetStationRef(cur.itemID);
                StationSE *pSSE(nullptr);
                if (IsOutpost(cur.itemID)) {
                    // player outposts:  their services, slots and lines follow their upgrades (also fixes outposts built before)
                    pSSE = new OutpostSE(itemRef, GetServiceMgr(), this);
                    OutpostSE::Refresh(cur.itemID);
                } else {
                    pSSE = new StationSE(itemRef, GetServiceMgr(), this);
                }
                sEntityList.AddStation(cur.itemID, itemRef);
                pSE = pSSE;
            } break;
            case EVEDB::invGroups::Asteroid_Belt: {
                CelestialObjectRef itemRef = sItemFactory.GetCelestialRef(cur.itemID);
                BeltSE *pBSE = new BeltSE(itemRef, GetServiceMgr(), this);
                pBSE->SetBeltMgr(m_beltMgr);
                ++m_beltCount;
                pSE = pBSE;
            } break;
            case EVEDB::invGroups::Stargate: {
                CelestialObjectRef itemRef = sItemFactory.GetCelestialRef(cur.itemID);
                itemRef->SetAttribute(AttrRadius, cur.radius, false);
                StargateSE *pSSE = new StargateSE(itemRef, GetServiceMgr(), this);
                m_gateMap.insert(std::pair<uint32, SystemEntity*>(cur.itemID, pSSE));
                ++m_gateCount;
                pSE = pSSE;
            } break;
            case EVEDB::invGroups::Planet: {
                CelestialObjectRef itemRef = sItemFactory.GetCelestialRef(cur.itemID);
                itemRef->SetAttribute(AttrRadius, cur.radius, false);
                PlanetSE *pPSE = new PlanetSE(itemRef, GetServiceMgr(), this);
                m_planetMap.insert(std::pair<uint32, SystemEntity*>(cur.itemID, pPSE));
                pSE = pPSE;
            } break;
            case EVEDB::invGroups::Moon: {
                CelestialObjectRef itemRef = sItemFactory.GetCelestialRef(cur.itemID);
                itemRef->SetAttribute(AttrRadius, cur.radius, false);
                MoonSE *pMSE = new MoonSE(itemRef, GetServiceMgr(), this);
                m_moonMap.insert(std::pair<uint32, SystemEntity*>(cur.itemID, pMSE));
                pSE = pMSE;
            } break;
            case EVEDB::invGroups::Sun: {    // suns dont have anything special, so they are generic SSEs
                CelestialObjectRef itemRef = sItemFactory.GetCelestialRef(cur.itemID);
                itemRef->SetAttribute(AttrRadius, cur.radius, false);
                StaticSystemEntity *pSSE = new StaticSystemEntity(itemRef, GetServiceMgr(), this);
                pSE = pSSE;
            } break;
            default: {
                sLog.Error( "SystemManager::LoadSystemStatics()", "create static entity called for unhandled item %u (grp: %u, type %u)", cur.itemID, cur.groupID, cur.typeID);
                continue;
            }
        }
        if (pSE == nullptr) {
            sLog.Error( "SystemManager::LoadSystemStatics()", "Failed to create entity for item %u (grp: %u, type %u)", cur.itemID, cur.groupID, cur.typeID);
            continue;
        }
        if (pSE->IsGateSE() or pSE->IsStationSE())
            sBubbleMgr.Add(pSE);
        if (pSE->IsBeltSE()) {
            sBubbleMgr.Add(pSE);
            m_beltVector.push_back(cur.itemID);
        }
        if (!pSE->LoadExtras())
            _log(INV__WARNING, "SystemManager::LoadSystemStatics() - Failed to load additional data for entity %u. Continuing.", cur.itemID);

        m_entities[cur.itemID] = pSE;
        m_staticEntities[cur.itemID] = pSE;
        AddItemToInventory(pSE->GetSelf());
    }

    _log(SERVER__INIT, "SystemManager::LoadSystemStatics() - %u Static System entities loaded for %s(%u)", entities.size(), m_data.name.c_str(), m_data.systemID);
    entities.clear();
    return true;
}

bool SystemManager::LoadSystemDynamics() {
    std::vector<DBSystemDynamicEntity> entities;
    entities.clear();
    if (!SystemDB::LoadSystemDynamicEntities(m_data.systemID, entities)) {
        sLog.Error( "SystemManager::LoadSystemDynamics()", "Unable to load dynamic entities during boot of %s(%u).", m_data.name.c_str(), m_data.systemID);
        return false;
    }

    SystemEntity* pSE(nullptr);
    for (auto cur : entities) {
        pSE = DynamicEntityFactory::BuildEntity(*this, cur);
        if (pSE == nullptr) {
            sLog.Error( "SystemManager::LoadSystemDynamics()", "Failed to create entity for item %u (grp: %u, type %u)",
                                cur.itemID, cur.groupID, cur.typeID);
            continue;
        }
        _log(ITEM__TRACE, "SystemManager::LoadSystemDynamics() - Loaded dynamic entity %u of type %u for %s(%u)", \
                    cur.itemID, cur.typeID, m_data.name.c_str(), m_data.systemID);
        if (pSE->GetPosition().isZero())
            pSE->SetPosition(sMapData.GetRandPointOnPlanet(m_data.systemID));
        //pSE->SetPosition(sMapData.GetRandPointOnMoon(m_data.systemID));
        if (pSE->GetTypeID() == EVEDB::invGroups::Wormhole) { //Wormholes don't need anomaly signals
            AddEntity(pSE, false);
        } else {
            AddEntity(pSE);
        }

    }
    _log(SERVER__INIT, "SystemManager::LoadSystemDynamics - %u Dynamic System entities loaded for %s(%u)", entities.size(), m_data.name.c_str(),m_data.systemID);

    return true;
}

bool SystemManager::LoadPlayerDynamics() {
    std::vector<DBSystemDynamicEntity> entities;
    entities.clear();
    if (!SystemDB::LoadPlayerDynamicEntities(m_data.systemID, entities)) {
        sLog.Error( "SystemManager::LoadPlayerDynamics()", "Unable to load player dynamic entities in %s(%u).", m_data.name.c_str(), m_data.systemID);
        return false;
    }

    SystemEntity* pSE(nullptr);
    for (auto cur : entities) {
        // starbase force fields are made by their (online) towers.  one left over from before a restart goes
        if (cur.groupID == EVEDB::invGroups::Force_Field) {
            InventoryItemRef fRef(sItemFactory.GetItemRef(cur.itemID));
            if (fRef.get() != nullptr)
                fRef->Delete();
            continue;
        }
        pSE = DynamicEntityFactory::BuildEntity(*this, cur);
        if (pSE == nullptr) {
            sLog.Error( "SystemManager::LoadPlayerDynamics()", "Failed to create entity for item %u (grp: %u, type %u)", cur.itemID, cur.groupID, cur.typeID);
            continue;
        }
        _log(ITEM__TRACE, "SystemManager::LoadPlayerDynamics() - Loaded dynamic entity %u of type %u for %s(%u)", \
                    cur.itemID, cur.typeID, m_data.name.c_str(),m_data.systemID);
        if (pSE->GetPosition().isZero())
            pSE->SetPosition(sMapData.GetRandPointOnMoon(m_data.systemID));
            //pSE->SetPosition(sMapData.GetRandPointOnPlanet(m_data.systemID));
        AddEntity(pSE);
    }
    _log(SERVER__INIT, "SystemManager::LoadPlayerDynamics() - %u Dynamic Player entities loaded for %s(%u)", \
                entities.size(), m_data.name.c_str(),m_data.systemID);

    return true;
}

bool SystemManager::BuildDynamicEntity(const DBSystemDynamicEntity& entity, uint32 launcherID/*0*/, bool addSignal/*true*/) {
    SystemEntity* pSE = DynamicEntityFactory::BuildEntity(*this, entity);
    if (pSE == nullptr) {
        sLog.Error( "SystemManager::BuildDynamicEntity()", "Failed to create entity for item %u (grp: %u, type %u)", entity.itemID, entity.groupID, entity.typeID);
        return false;
    }

    _log(ITEM__TRACE, "SystemManager::BuildDynamicEntity() - Created dynamic entity %u of type %u for %s(%u)", \
                entity.itemID, entity.typeID, m_data.name.c_str(),m_data.systemID );
    AddEntity(pSE, addSignal);

    // this is only used for wrecks...
    if (launcherID) {
        WreckSE* pWE = pSE->GetWreckSE();
        pWE->SetLaunchedByID(launcherID);
        if (IsCharacterID(entity.ownerID)) {
            Client* pClient = sEntityList.FindClientByCharID(entity.ownerID);
            if (pClient->InFleet())
                pWE->SetFleetID(pClient->GetFleetID());
        }
    }
    return true;
}

SystemEntity* DynamicEntityFactory::BuildEntity(SystemManager& sysMgr, const DBSystemDynamicEntity& entity)
{
    FactionData data = FactionData();
        data.allianceID = entity.allianceID;
        data.corporationID = entity.corporationID;
        data.factionID = entity.factionID;
        data.ownerID = entity.ownerID;

    switch (entity.categoryID) {
        case EVEDB::invCategories::Asteroid: {
            InventoryItemRef asteroid = sItemFactory.GetItemRef( entity.itemID );
            if (asteroid.get() == nullptr) {
                _log(ITEM__ERROR, "DynamicEntityFactory::BuildEntity() - unable to load asteroid %s (%u)", entity.itemName.c_str(), entity.itemID);
                return nullptr;
            }
            // built outside a belt:  no belt manager (AsteroidSE handles that)
            AsteroidSE* aSE = new AsteroidSE(asteroid, sysMgr.GetServiceMgr(), &sysMgr);
            _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making AsteroidSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
            return aSE;
        } break;
        case EVEDB::invCategories::Ship: {
            ShipItemRef ship = sItemFactory.GetShipRef( entity.itemID );
            if (ship.get() == nullptr)
                return nullptr;
            /** @todo make error msg here */
            ShipSE* sSE = new ShipSE(ship, sysMgr.GetServiceMgr(), &sysMgr, data);
            _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making ShipSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
            return sSE;
        } break;
        case EVEDB::invCategories::Deployable: {
            InventoryItemRef deployable = sItemFactory.GetItemRef( entity.itemID );
            if (deployable.get() == nullptr)
                return nullptr;
            /** @todo make error msg here */
            deployable->SetAttribute(AttrRadius, deployable->type().radius());     // Can you set this somehow from the type class ?
            DeployableSE* dSE = new DeployableSE(deployable, sysMgr.GetServiceMgr(), &sysMgr, data);
            // anchored mobile warp disruptors come back with their field
            dSE->Init();
            _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making DeployableSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
            return dSE;
        } break;
        //  these should go into m_staticEntities
        case EVEDB::invCategories::StructureUpgrade: // SOV upgrade structures   these may need their own class one day.
        case EVEDB::invCategories::Structure: {         // POS items
            StructureItemRef structure = sItemFactory.GetStructureRef( entity.itemID );
            if (structure.get() == nullptr)
                return nullptr;
            /** @todo make error msg here */
            StructureSE* pSSE(nullptr);
            switch(entity.groupID) {
                case EVEDB::invGroups::Control_Tower: {
                    TowerSE* tSE = new TowerSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making TowerSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    pSSE = tSE;
                } break;
                case EVEDB::invGroups::Mobile_Missile_Sentry:
                case EVEDB::invGroups::Mobile_Projectile_Sentry:
                case EVEDB::invGroups::Mobile_Laser_Sentry:
                case EVEDB::invGroups::Mobile_Hybrid_Sentry: {
                    WeaponSE* wSE = new WeaponSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making WeaponSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    pSSE =  wSE;
                } break;
                case EVEDB::invGroups::Electronic_Warfare_Battery:
                case EVEDB::invGroups::Sensor_Dampening_Battery:
                case EVEDB::invGroups::Stasis_Webification_Battery:
                case EVEDB::invGroups::Warp_Scrambling_Battery:
                case EVEDB::invGroups::Energy_Neutralizing_Battery:
                case EVEDB::invGroups::Target_Painting_Battery: {
                    BatterySE* bSE = new BatterySE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making BatterySE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    pSSE = bSE;
                } break;
                case EVEDB::invGroups::Refining_Array:
                case EVEDB::invGroups::Ship_Maintenance_Array:
                case EVEDB::invGroups::Assembly_Array:
                case EVEDB::invGroups::Mobile_Laboratory:
                case EVEDB::invGroups::Shield_Hardening_Array:
                case EVEDB::invGroups::Force_Field_Array:
                case EVEDB::invGroups::Corporate_Hangar_Array:
                case EVEDB::invGroups::Stealth_Emitter_Array:
                case EVEDB::invGroups::Scanner_Array:
                case EVEDB::invGroups::Logistics_Array:
                case EVEDB::invGroups::Structure_Repair_Array: {
                    ArraySE* aSE = new ArraySE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making ArraySE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    pSSE = aSE;
                } break;
                case EVEDB::invGroups::Silo:
                case EVEDB::invGroups::Moon_Mining:
                case EVEDB::invGroups::Mobile_Reactor: {
                    ReactorSE* rSE = new ReactorSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making ReactorSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    pSSE = rSE;
                } break;
                case EVEDB::invGroups::Jump_Portal_Array: {
                    JumpBridgeSE* jbSE = new JumpBridgeSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making JumpBridgeSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    pSSE = jbSE;
                } break;
                default: {
                    StructureSE* sSE = new StructureSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making StructureSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    pSSE = sSE;
                } break;
            }
            return pSSE;
        } break;
        case EVEDB::invCategories::SovereigntyStructure: {// SOV structures
            //Create item ref
            StructureItemRef structure = sItemFactory.GetStructureRef( entity.itemID );
            if (structure.get() == nullptr)
                return nullptr;
            StructureSE* sSSE(nullptr);
            //Test for different types of sov structures
            switch(entity.groupID) {
                case EVEDB::invGroups::Territorial_Claim_Units: {
                    TCUSE* sSE = new TCUSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making TCUSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    sSSE = sSE;
                } break;
                case EVEDB::invGroups::Sovereignty_Blockade_Units: {
                    SBUSE* sSE = new SBUSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making SBUSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    sSSE = sSE;
                } break;
                case EVEDB::invGroups::Infrastructure_Hubs: {
                    IHubSE* sSE = new IHubSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making IHubSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    sSSE = sSE;
                } break;
                default: { //Should never be called, therefore print an error log
                    StructureSE* sSE = new StructureSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__ERROR, "DynamicEntityFactory::BuildEntity() Default sovereignty StructureSE created for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    sSSE = sSE;
                } break;
            }
            return sSSE;
        } break;
        case EVEDB::invCategories::Orbitals: {           // planet orbitals   these should go into m_staticEntities
            StructureItemRef structure = sItemFactory.GetStructureRef( entity.itemID );
            if (structure.get() == nullptr)
                return nullptr;
                /** @todo make error msg here */
            CustomsSE* pCoSE(nullptr);
            switch(entity.groupID) {
                case EVEDB::invGroups::Test_Orbitals:
                case EVEDB::invGroups::Orbital_Construction_Platform:
                case EVEDB::invGroups::Orbital_Infrastructure: {
                    pCoSE = new CustomsSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    //structure->GetMyInventory()->LoadContents();  this is called during structureItem creation
                    if ((entity.planetID) and (entity.groupID != EVEDB::invGroups::Test_Orbitals)) {
                        pCoSE->SetPlanet(entity.planetID);
                        SystemEntity* pPE = sysMgr.GetSE(entity.planetID);
                        if ((pPE != nullptr) and pPE->IsPlanetSE())
                            pPE->GetPlanetSE()->SetCustomsOffice(pCoSE);
                    }
                    pCoSE->Init();
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making CustomsSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                } break;
            }
            return pCoSE;
        } break;
        case EVEDB::invCategories::Celestial: {
            // TODO: (just use CelestialEntity class for these until their own classes are written)
            // * WarpGateEntity  <-- Warp_Gate
            // * WormholeEntity  <-- Wormhole
            switch (entity.groupID) {
                case EVEDB::invGroups::Wreck: {
                    WreckContainerRef wreck = sItemFactory.GetWreckContainer( entity.itemID );
                    if (wreck.get() == nullptr)
                        return nullptr;
                    /** @todo make error msg here */
                    WreckSE* wSE = new WreckSE(wreck, sysMgr.GetServiceMgr(), &sysMgr, data);
                    wreck->GetMyInventory()->LoadContents();
                    wreck->SetMySE(wSE);
                    _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making WreckSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return wSE;
                } break;
                case EVEDB::invGroups::Audit_Log_Secure_Container:
                case EVEDB::invGroups::Secure_Cargo_Container:
                case EVEDB::invGroups::Cargo_Container:
                case EVEDB::invGroups::Freight_Container:
                case EVEDB::invGroups::Shipping_Crates: {
                    CargoContainerRef contRef = sItemFactory.GetCargoRef( entity.itemID );
                    if (contRef.get() == nullptr)
                        return nullptr;
                    /** @todo make error msg here */
                    ContainerSE* cSE = new ContainerSE(contRef, sysMgr.GetServiceMgr(), &sysMgr, data);
                    contRef->GetMyInventory()->LoadContents();
                    contRef->SetMySE(cSE);
                    _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making ContainerSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return cSE;
                } break;
            /** @todo (Allan)  need to separate these by class to create proper SE (started) */
                case EVEDB::invGroups::Biomass:
                case EVEDB::invGroups::Ring:/*wtf*/
                case EVEDB::invGroups::Secondary_Sun:
                case EVEDB::invGroups::Large_Collidable_Object:
                case EVEDB::invGroups::Large_Collidable_Structure:
                case EVEDB::invGroups::Large_Collidable_Ship:  // this will not hit here.  category 11, entity
                case EVEDB::invGroups::Cloud:
                case EVEDB::invGroups::Landmark:
                case EVEDB::invGroups::Comet:
                case EVEDB::invGroups::Destructable_Station_Services:
                /* test these to see if they are POS types */
                case EVEDB::invGroups::Global_Warp_Disruptor:
                case EVEDB::invGroups::Force_Field:  // <<<  this one is POS type but it IS a plain CSE
                /* these will get their own class eventually */
                case EVEDB::invGroups::Effect_Beacon:
                case EVEDB::invGroups::Beacon:
                case EVEDB::invGroups::Covert_Beacon:
                case EVEDB::invGroups::Planetary_Cloud: {
                    CelestialObjectRef celestial = sItemFactory.GetCelestialRef( entity.itemID );
                    if (celestial.get() == nullptr)
                        return nullptr;
                    /** @todo make error msg here */
                    CelestialSE* cSE = new CelestialSE(celestial, sysMgr.GetServiceMgr(), &sysMgr);
                    _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making CelestialSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return cSE;
                } break;
                case EVEDB::invGroups::Harvestable_Cloud: {
                    // gas clouds are harvested like asteroids (gas cloud harvesters need an AsteroidSE with a quantity)
                    InventoryItemRef cloud = sItemFactory.GetItemRef( entity.itemID );
                    if (cloud.get() == nullptr)
                        return nullptr;
                    if (cloud->GetAttribute(AttrQuantity).get_double() <= 0) {
                        double radius(cloud->HasAttribute(AttrRadius) ? cloud->GetAttribute(AttrRadius).get_double() : cloud->type().radius());
                        cloud->SetAttribute(AttrQuantity, AsteroidSE::QuantityForRadius(cloud->categoryID(), cloud->groupID(), cloud->type().radius(), radius), false);
                    }
                    AsteroidSE* aSE = new AsteroidSE(cloud, sysMgr.GetServiceMgr(), &sysMgr);
                    _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making AsteroidSE (gas cloud) for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return aSE;
                } break;
                case EVEDB::invGroups::Construction_Platform:
                case EVEDB::invGroups::Station_Upgrade_Platform:
                case EVEDB::invGroups::Station_Improvement_Platform: {
                    StructureItemRef structure = sItemFactory.GetStructureRef( entity.itemID );
                    if (structure.get() == nullptr)
                        return nullptr;
                    PlatformSE* pSE = new PlatformSE(structure, sysMgr.GetServiceMgr(), &sysMgr, data);
                    _log(POS__TRACE, "DynamicEntityFactory::BuildEntity() making PlatformSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return pSE;
                }
                case EVEDB::invGroups::Wormhole: {
                    CelestialObjectRef celestial = sItemFactory.GetCelestialRef( entity.itemID );
                    if (celestial.get() == nullptr)
                        return nullptr;
                    /** @todo make error msg here */
                    WormholeSE* wSE = new WormholeSE(celestial, sysMgr.GetServiceMgr(), &sysMgr);
                    _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making WormholeSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return wSE;
                } break;
                case EVEDB::invGroups::Cosmic_Anomaly:
                case EVEDB::invGroups::Cosmic_Signature: {
                    CelestialObjectRef celestial = sItemFactory.GetCelestialRef( entity.itemID );
                    if (celestial.get() == nullptr)
                        return nullptr;
                    /** @todo make error msg here */
                    AnomalySE* aSE = new AnomalySE(celestial, sysMgr.GetServiceMgr(), &sysMgr);
                    _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making AnomalySE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return aSE;
                } break;
                case EVEDB::invGroups::Warp_Gate: { //accel gate
                    // does this need own item, or celestial, or generic or other?
                    InventoryItemRef iRef = sItemFactory.GetItemRef( entity.itemID );
                    //CelestialObjectRef celestial = sItemFactory.GetCelestialRef( entity.itemID );
                    if (iRef.get() == nullptr)
                        return nullptr;
                    /** @todo make error msg here */
                    AccelGateSE* gSE = new AccelGateSE(iRef, sysMgr.GetServiceMgr(), &sysMgr);
                    _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making AccelGateSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return gSE;
                } break;
            } break;
        } break;
        case EVEDB::invCategories::Entity: {            // Entities
            if (entity.groupID == EVEDB::invGroups::Spawn_Container ) {     // these are destructible objects found in dungeons
                // For category=Entity, group=Spawn Container, create a CargoContainer object:
                /** @todo  this needs its own class....there are 477 types, spawning everything..rats, modules, items, etc. */
                CargoContainerRef contRef = sItemFactory.GetCargoRef( entity.itemID );
                if (contRef.get() == nullptr)
                    return nullptr;
                /** @todo make error msg here */
                ContainerSE* cSE = new ContainerSE(contRef, sysMgr.GetServiceMgr(), &sysMgr, data);
                contRef->GetMyInventory()->LoadContents();
                contRef->SetMySE(cSE);
                _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making ContainerSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                return cSE;
            } else if ((entity.groupID == EVEDB::invGroups::Sentry_Gun) or (entity.groupID == EVEDB::invGroups::Protective_Sentry_Gun)
                or (entity.groupID == EVEDB::invGroups::Destructible_Sentry_Gun) or (entity.groupID == EVEDB::invGroups::Mobile_Sentry_Gun)
                or (entity.groupID == EVEDB::invGroups::Deadspace_Overseer_s_Sentry))   // a gun, not a ship (bug B11)
            {
                InventoryItemRef sentryRef = sItemFactory.GetItemRef( entity.itemID );
                if (sentryRef.get() == nullptr)
                    return nullptr;
                /** @todo make error msg here */
                Sentry* SentrySE = new Sentry(sentryRef, sysMgr.GetServiceMgr(), &sysMgr, data);
                _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making SentrySE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                return SentrySE;
            }
            // Check for NPC ships/drones here (category 11):   NOT player drones (different category [18])
            else if ((entity.groupID == EVEDB::invGroups::Police_Drone) or (entity.groupID == EVEDB::invGroups::Pirate_Drone) or (entity.groupID == EVEDB::invGroups::LCO_Drone)
                or (entity.groupID == EVEDB::invGroups::Tutorial_Drone) or (entity.groupID == EVEDB::invGroups::Rogue_Drone) or (entity.groupID == EVEDB::invGroups::Faction_Drone)
                or (entity.groupID == EVEDB::invGroups::Convoy) or (entity.groupID == EVEDB::invGroups::Convoy_Drone) or (entity.groupID == EVEDB::invGroups::Concord_Drone)
                or (entity.groupID == EVEDB::invGroups::Mission_Drone) or (entity.groupID == EVEDB::invGroups::Deadspace_Overseer) or (entity.groupID == EVEDB::invGroups::Customs_Official)
                or (entity.groupID == EVEDB::invGroups::Deadspace_Overseer_s_Structure)
                or (entity.groupID == EVEDB::invGroups::Deadspace_Overseer_s_Belongings) or (entity.groupID == EVEDB::invGroups::Storyline_Frigate)
                or (entity.groupID == EVEDB::invGroups::Storyline_Cruiser) or (entity.groupID == EVEDB::invGroups::Storyline_Battleship) or (entity.groupID == EVEDB::invGroups::Storyline_Mission_Frigate)
                or (entity.groupID == EVEDB::invGroups::Storyline_Mission_Cruiser) or (entity.groupID == EVEDB::invGroups::Storyline_Mission_Battleship)
                or ((entity.groupID >= EVEDB::invGroups::Asteroid_Angel_Cartel_Frigate) and (entity.groupID <= EVEDB::invGroups::Asteroid_Serpentis_BattleCruiser))
                or ((entity.groupID >= EVEDB::invGroups::Deadspace_Angel_Cartel_BattleCruiser) and (entity.groupID <= EVEDB::invGroups::Deadspace_Angel_Cartel_Frigate))
                or ((entity.groupID >= EVEDB::invGroups::Deadspace_Blood_Raiders_BattleCruiser) and (entity.groupID <= EVEDB::invGroups::Deadspace_Blood_Raiders_Frigate))
                or ((entity.groupID >= EVEDB::invGroups::Deadspace_Guristas_BattleCruiser) and (entity.groupID <= EVEDB::invGroups::Deadspace_Guristas_Frigate))
                or ((entity.groupID >= EVEDB::invGroups::Deadspace_Sanshas_Nation_BattleCruiser) and (entity.groupID <= EVEDB::invGroups::Deadspace_Sanshas_Nation_Frigate))
                or ((entity.groupID >= EVEDB::invGroups::Deadspace_Serpentis_BattleCruiser) and (entity.groupID <= EVEDB::invGroups::Deadspace_Serpentis_Frigate))
                or ((entity.groupID >= EVEDB::invGroups::Mission_Amarr_Empire_Frigate) and (entity.groupID <= EVEDB::invGroups::Mission_Minmatar_Republic_Battleship))
                or (entity.groupID == EVEDB::invGroups::Destructible_Agents_In_Space)
                or ((entity.groupID >= EVEDB::invGroups::Asteroid_Rogue_Drone_Battlecruiser) and (entity.groupID <= EVEDB::invGroups::Asteroid_Rogue_Drone_Swarm))
                or (entity.groupID == EVEDB::invGroups::Large_Collidable_Ship) //  this is wreck?  abandoned ship?
                or ((entity.groupID >= EVEDB::invGroups::Asteroid_Angel_Cartel_Commander_Frigate) and (entity.groupID <= EVEDB::invGroups::Asteroid_Serpentis_Commander_Frigate))
                or ((entity.groupID >= EVEDB::invGroups::Mission_Generic_Battleships) and (entity.groupID <= EVEDB::invGroups::Mission_Generic_Destroyers))
                or ((entity.groupID >= EVEDB::invGroups::Asteroid_Rogue_Drone_Commander_Battlecruiser) and (entity.groupID <= EVEDB::invGroups::Asteroid_Serpentis_Commander_Battleship))
                or (entity.groupID == EVEDB::invGroups::Mission_Fighter_Drone)
                or ((entity.groupID >= EVEDB::invGroups::Mission_Amarr_Empire_Carrier) and (entity.groupID <= EVEDB::invGroups::Mission_Minmatar_Republic_Carrier))
                or (entity.groupID == EVEDB::invGroups::Mission_Faction_Transports) or (entity.groupID == EVEDB::invGroups::Mission_Faction_Industrials)
                or (entity.groupID == EVEDB::invGroups::Deadspace_Sleeper_Sleepless_Sentinel) or (entity.groupID == EVEDB::invGroups::Deadspace_Sleeper_Awakened_Sentinel)
                or (entity.groupID == EVEDB::invGroups::Deadspace_Sleeper_Emergent_Sentinel)
                or ((entity.groupID >= EVEDB::invGroups::Deadspace_Sleeper_Sleepless_Defender) and (entity.groupID <= EVEDB::invGroups::Deadspace_Sleeper_Emergent_Patroller))
                or (entity.groupID == EVEDB::invGroups::Mission_Faction_Cruiser) or (entity.groupID == EVEDB::invGroups::Mission_Faction_Frigate)
                or (entity.groupID == EVEDB::invGroups::Incursion_Sanshas_Nation_Industrial) or (entity.groupID == EVEDB::invGroups::Incursion_Sanshas_Nation_Capital)
                or (entity.groupID == EVEDB::invGroups::Incursion_Sanshas_Nation_Frigate) or (entity.groupID == EVEDB::invGroups::Incursion_Sanshas_Nation_Cruiser)
                or (entity.groupID == EVEDB::invGroups::Incursion_Sanshas_Nation_Battleship))
            {
                InventoryItemRef npcRef = sItemFactory.GetItemRef( entity.itemID );
                if (npcRef.get() == nullptr)
                    return nullptr;
                /** @todo make error msg here */
                NPC* npcSE = new NPC(npcRef, sysMgr.GetServiceMgr(), &sysMgr, data);
                npcSE->Load();
                sEntityList.AddNPC();
                _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making NPCSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                return npcSE;
            }
            // may have to create unique class for Billboard (EVEDB::invGroups::Billboard)
            else {
                InventoryItemRef iRef = sItemFactory.GetItemRef( entity.itemID );
                if (iRef.get() == nullptr)
                    return nullptr;
                /** @todo make error msg here */
                ItemSystemEntity* cSE = new ItemSystemEntity(iRef, sysMgr.GetServiceMgr(), &sysMgr);
                _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making ISE item for %s (%u)", entity.itemName.c_str(), entity.itemID);
                return cSE;
            }
        } break;
        case EVEDB::invCategories::Drone: {             // Player Drones
            InventoryItemRef drone = sItemFactory.GetItemRef( entity.itemID );
            if (drone.get() == nullptr)
                return nullptr;
            /** @todo make error msg here */
            DroneSE* dSE = new DroneSE(drone, sysMgr.GetServiceMgr(), &sysMgr, data);
            _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making DroneSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
            return dSE;
        } break;
        case EVEDB::invCategories::Charge: {
            switch (entity.groupID) {
                case EVEDB::invGroups::Scanner_Probe: {
                    ProbeItemRef pRef = sItemFactory.GetProbeRef(entity.itemID);
                    if (pRef.get() == nullptr)
                        return nullptr;
                        /** @todo make error msg here */
                    // make sure these are owned by eve system
                    pRef->ChangeOwner(1);
                    //these probes are abandoned and offline.  give them 5h lifetime
                    ProbeSE* pSE = new ProbeSE(pRef, sysMgr.GetServiceMgr(), &sysMgr);
                    _log(ITEM__TRACE, "DynamicEntityFactory::BuildEntity() making ProbeSE for %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return pSE;
                } break;
                case EVEDB::invGroups::Survey_Probe: {
                    sLog.Warning("BuildEntity", "Called for Survey_Probe");
                } break;
                case EVEDB::invGroups::Warp_Disruption_Probe: {
                    // launched spheres are temp items with a 2 minute life.  one in the db is stale
                    InventoryItemRef iRef(sItemFactory.GetItemRef(entity.itemID));
                    if (iRef.get() != nullptr)
                        iRef->Delete();
                    _log(ITEM__WARNING, "DynamicEntityFactory::BuildEntity() - deleted stale warp disrupt probe %s (%u)", entity.itemName.c_str(), entity.itemID);
                    return nullptr;
                } break;
            }
        } break;
    }
    sLog.Warning("BuildEntity", "Unhandled dynamic entity category %u for item %u of type %u", entity.categoryID, entity.itemID, entity.typeID);
    EvE::traceStack();
    return nullptr;
}

void SystemManager::AddClient(Client* pClient, bool count/*false*/, bool jump/*false*/) {
    //called from Client::MoveToLocation() on login and when changing systems
    if (pClient == nullptr)
        return;
    if (m_clients.find(pClient->GetCharacterID()) == m_clients.end()) {
        m_clients[pClient->GetCharacterID()] = pClient;
        _log(PLAYER__TRACE, "%s(%u): Added to system manager for %s(%u) - %u clients now in system. count %s", \
                    pClient->GetName(), pClient->GetCharacterID(), m_data.name.c_str(), m_data.systemID, m_clients.size(), count?"true":"false");
    } else {
        // error for player already in client map
        _log(PLAYER__ERROR, "%s(%u): Already in player map for %s(%u)", \
            pClient->GetName(), pClient->GetCharacterID(), m_data.name.c_str(), m_data.systemID);
    }

    m_activityTime = 0;

    if (count) {
        ++m_players;
        _log(PLAYER__INFO, "%s(%u): Added to player count for %s(%u) - new count: %u", \
                pClient->GetName(), pClient->GetCharacterID(), m_data.name.c_str(), m_data.systemID, m_players);
    }
    if (jump) {
        //add jump in this system
        MapDB::AddJump(m_data.systemID);

        _log(PLAYER__INFO, "%s(%u): Add Jump to %s(%u)", \
        pClient->GetName(), pClient->GetCharacterID(), m_data.name.c_str(), m_data.systemID);

        uint16 stamp = sEntityList.GetStamp();
        std::map<uint32, uint8>::iterator itr = m_jumpMap.find(stamp);
        if (itr != m_jumpMap.end()) {
            ++(itr->second);
        } else {
            m_jumpMap.emplace(stamp, 1);
        }
    }
}

void SystemManager::RemoveClient(Client* pClient, bool count/*false*/, bool jump/*false*/) {
    //called from Client::~Client() and Client::MoveToLocation() when changing systems
    if (pClient == nullptr)
        return;
    m_clients.erase(pClient->GetCharacterID());
    _log(PLAYER__TRACE, "%s(%u): Removed from system manager for %s(%u) - %u clients still in system.", \
            pClient->GetName(), pClient->GetCharacterID(), m_data.name.c_str(), m_data.systemID, m_clients.size());

    if (count) {
        --m_players;
        if (m_players < 0) {
            m_players = 0;
            m_clients.clear();  // redundant but safe
            _log(PLAYER__ERROR, "player count for %s(%u) is < 0", m_data.name.c_str(), m_data.systemID);
        }

        _log(PLAYER__INFO, "%s(%u): Removed from player count for %s(%u) - new count: %u", \
                pClient->GetName(), pClient->GetCharacterID(), m_data.name.c_str(), m_data.systemID, m_players);
    }
    if (jump) {
        //add jump in this system
        MapDB::AddJump(m_data.systemID);

        _log(PLAYER__INFO, "%s(%u): Add Jump to %s(%u)", \
                pClient->GetName(), pClient->GetCharacterID(), m_data.name.c_str(), m_data.systemID);

        uint16 stamp = sEntityList.GetStamp();
        std::map<uint32, uint8>::iterator itr = m_jumpMap.find(stamp);
        if (itr != m_jumpMap.end()) {
            ++(itr->second);
        } else {
            m_jumpMap.emplace(stamp, 1);
        }
    }
}

void SystemManager::SetDockCount(Client* pClient, bool docked/*false*/)
{
    if (docked) {
        ++m_docked;
    } else {
        if (m_docked > 0) {
            --m_docked;
        } else {
            m_docked = 0;
            _log(PLAYER__ERROR, "docked count for %s(%u) is <0.  Setting to 0.", m_data.name.c_str(), m_data.systemID);
        }
    }

    if (m_players > sEntityList.GetPlayerCount())
        GetPlayerCount();
    if (m_docked > m_players)
        GetDockedCount();

    MapDB::UpdatePilotCount(m_data.systemID, m_docked, (m_players - m_docked));

    _log(PLAYER__INFO, "%s(%u): %s docked count for %s(%u) - new count: %u", \
            pClient->GetName(), pClient->GetCharacterID(), docked ? "Added to" : "Removed from",  m_data.name.c_str(), m_data.systemID, m_docked);
}

void SystemManager::AddNPC(NPC* pNPC) {
    if ( pNPC == nullptr)
        return;
    uint32 itemID = pNPC->GetID();
    if (m_npcs.find(itemID) != m_npcs.end()) {
        _log(ITEM__WARNING, "%s(%u): Called AddNPC(), but they're already in %s(%u).  Check bubble.", pNPC->GetName(), itemID, m_data.name.c_str(), m_data.systemID);
    } else {
        m_npcs[itemID] = pNPC;
    }

    _log(NPC__TRACE, "%s(%u): Added to system manager for %s(%u)", pNPC->GetName(), pNPC->GetID(), m_data.name.c_str(), m_data.systemID);
    AddEntity(pNPC, false);
    sEntityList.AddNPC();
}

void SystemManager::RemoveNPC(NPC* pNPC) {
    if ( pNPC == nullptr)
        return;
    auto itr = m_npcs.find(pNPC->GetID());
    if (itr != m_npcs.end())
        m_npcs.erase(itr);

    _log(NPC__TRACE, "%s(%u): Removed from system manager for %s(%u)", pNPC->GetName(), pNPC->GetID(), m_data.name.c_str(), m_data.systemID);
    RemoveEntity(pNPC);
    sEntityList.RemoveNPC();    // this is for loaded npc count.
    pNPC->RemoveNPC();   // this deletes NPC from DB.  NPC's dont jump, so no reason to remove from system unless killed
}

void SystemManager::RemoveDeadNPC(NPC* pNPC) {
    if (pNPC == nullptr)
        return;
    auto itr = m_npcs.find(pNPC->GetID());
    if (itr != m_npcs.end()) {
        m_npcs.erase(itr);
        sEntityList.RemoveNPC();
    }
}

void SystemManager::AddEntity(SystemEntity* pSE, bool addSignal/*true*/) {
    if (pSE == nullptr)
        return;
    uint32 itemID(pSE->GetID());
    if (m_entities.find(itemID) != m_entities.end()) {
        _log(ITEM__WARNING, "%s(%u): Called AddEntity(), but they're already in %s(%u).  Check bubble.", pSE->GetName(), itemID, m_data.name.c_str(), m_data.systemID);
        return;
    } else {
        _log(ITEM__TRACE, "%s(%u): Added to system manager for %s(%u)", pSE->GetName(), itemID, m_data.name.c_str(), m_data.systemID);
        m_entities[itemID] = pSE;

        if ((pSE->IsCOSE())
        or  (pSE->isGlobal())) {
            m_staticEntities[itemID] = pSE;
            if (pSE->IsOperSE()) //Entities which need to be acted upon while nobody is in the system
                m_opStaticEntities[itemID] = pSE;
            if (m_loaded)   // only update when system is already loaded
                SendStaticBall(pSE); 
        } else if (pSE->IsProbeSE()) {
            // probes are now running sub-hz tics, so dont add to proc list.
            addSignal = false;  // redundant...called with AddSignal=false
            sEntityList.AddProbe(itemID, pSE->GetProbeSE());
        } else if (!IsStaticItem(itemID)) {
            // *most* dynamic items need proc tics.  add to proc list
            m_entityChanged = true;
            m_ticEntities[itemID] = pSE;
        } else {
            addSignal = false;
        }

        // Add Entity's Item Ref to Solar System Dynamic Inventory:
        m_solarSystemRef->AddItemToInventory( pSE->GetSelf() );

        if (pSE->IsDroneSE()) {
            m_drones[itemID] = pSE->GetDroneSE();
            m_droneIndexDirty = true;
        }
        // every npc is in the npc list (GetNPCSE()), the ones GM /spawn and the database make too (AddNPC() isn't used there)
        if (pSE->IsNPCSE())
            m_npcs[itemID] = pSE->GetNPCSE();
    }
    sBubbleMgr.Add(pSE);
    // add item to our AnomalyMgr
    if (addSignal)
        m_anomMgr->AddSignal(pSE);
}

void SystemManager::RemoveEntity(SystemEntity* pSE) {
    /** @note  this does not remove static balls (bubble center markers) and no clue why */
    if (pSE == nullptr)
        return;
    // a ship leaving space (docking, jumping, logging off, destroyed) loses control of its drones
    if (pSE->IsShipSE())
        DroneSE::ReleaseDronesOf(this, pSE->GetID());
    // ...and stops its self-destruct (the self-destruct itself has already removed its entry)
    if (pSE->IsShipSE() and IsSelfDestructing(pSE->GetID()))
        CancelSelfDestruct(pSE->GetID(), (pSE->HasPilot() ? SelfDestruct::Cancel::External : SelfDestruct::Cancel::Silent));
    if (m_drones.erase(pSE->GetID()) > 0)
        m_droneIndexDirty = true;
    // a field source leaving space (probe expired, deployable destroyed or scooped, HIC docked/jumped/died) takes its field along
    m_interdiction->RemoveField(pSE->GetID());
    /* an npc or sentry:  out of its group, spawn and dungeon, freed next tick (npc ai plan phase 5).  while it is still in its
     *  bubble, so the end of its ewar and beams reaches the clients there */
    if ((pSE->IsNPCSE() or pSE->IsSentrySE()) and (m_entities.find(pSE->GetID()) != m_entities.end()))
        OnNPCRemoved(pSE);
    sBubbleMgr.Remove(pSE);
    // Remove Entity's Item Ref from Solar System Dynamic Inventory:
    RemoveItemFromInventory(pSE->GetSelf());
    // remove entity from our maps
    uint32 itemID(pSE->GetID());
    m_entityChanged = true;
    m_ticEntities.erase(itemID);
    m_staticEntities.erase(itemID);
    m_opStaticEntities.erase(itemID);

    // remove from anomaly map, if exists
    m_anomMgr->RemoveSignal(itemID);
}

void SystemManager::OnNPCRemoved(SystemEntity* pSE)
{
    /* killed:  its OnKilled() alerted its group and left it, SpawnKilled() and the dungeon's OnEntityDestroyed() ran, its
     *  target manager is gone.  anything else (GM /unspawn, a despawn, a hauler that got away) leaves quietly here:  no
     *  alert, and its spawn entry and dungeon membership go (commanders' escort counts, waves and completion stop waiting
     *  for it).  owners that free their npcs at once (DungeonMgr, SpawnMgr, LawEnforcement) do that after this;  the
     *  destructor takes it off our list again (ForgetRemovedNPC) */
    uint32 itemID(pSE->GetID());
    bool killed(pSE->IsDead());
    bool owned(false);
    if (pSE->IsNPCSE()) {
        NPC* pNPC(pSE->GetNPCSE());
        if (!killed) {
            if (pNPC->GetAIMgr() != nullptr)
                pNPC->GetAIMgr()->OnRemoved();
            if (pNPC->GetSpawnMgr() != nullptr)
                pNPC->GetSpawnMgr()->SpawnRemoved(itemID);
            m_dungMgr->OnEntityRemoved(itemID);
        }
        // law enforcement frees its own ships (LawEnforcement::PruneNPCs, ForgetLawNPC)
        owned = pNPC->IsLawEnforcer();
    } else {
        Sentry* pSentry(pSE->GetSentrySE());
        if (!killed) {
            if (pSentry->GetAIMgr() != nullptr)
                pSentry->GetAIMgr()->OnRemoved();
            m_dungMgr->OnEntityRemoved(itemID);
        }
    }
    // an ambient npc:  its system forgets it (the system frees it below, like any other)
    if (pSE->IsNPCSE() and (pSE->GetNPCSE()->GetAmbientNPC() != nullptr))
        m_ambient->OnNPCRemoved(itemID, killed);
    // the modules on it stop, its locks go (both ways):  nothing points at it when it is freed
    if (pSE->TargetMgr() != nullptr) {
        pSE->TargetMgr()->ClearModules();
        pSE->TargetMgr()->ClearAllTargets(false);
    }
    if (!owned)
        m_removedNPCs[itemID] = pSE;
    _log(NPC__TRACE, "%s(%u): %s, out of space%s.", pSE->GetName(), itemID, (killed ? "killed" : "removed"),
            (owned ? "" : " (freed next tick)"));
}

void SystemManager::FreeRemovedNPCs()
{
    std::map<uint32, SystemEntity*> removed;
    removed.swap(m_removedNPCs);
    for (auto& cur : removed) {
        // back in space meanwhile (nothing does that today):  not freed while it is there (its destructor takes it off the list)
        if (m_entities.find(cur.first) != m_entities.end()) {
            m_removedNPCs.insert(cur);
            continue;
        }
        if (cur.second->IsNPCSE())
            RemoveDeadNPC(cur.second->GetNPCSE());
        delete cur.second;
    }
}

void SystemManager::ForgetRemovedNPC(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return;
    auto itr = m_removedNPCs.find(pSE->GetID());
    if ((itr != m_removedNPCs.end()) and (itr->second == pSE))
        m_removedNPCs.erase(itr);
    // freed without RemoveNPC() / RemoveDeadNPC():  the npc list mustn't keep it
    auto nItr = m_npcs.find(pSE->GetID());
    if ((nItr != m_npcs.end()) and (static_cast<SystemEntity*>(nItr->second) == pSE))
        m_npcs.erase(nItr);
}

void SystemManager::AddMarker(SystemEntity* pSE, bool sendBall/*false*/, bool addSignal/*false*/) {
    if (pSE == nullptr)
        return;

    m_entities[pSE->GetID()] = pSE;
    // Add Entity's Item Ref to Solar System Dynamic Inventory:
    //m_solarSystemRef->AddItemToInventory(pSE->GetSelf());

    sBubbleMgr.Add(pSE);
    if (addSignal)
        m_anomMgr->AddSignal(pSE);
    if (!sendBall)
        return;

    // modified from SendStaticBall()
    if (m_clients.empty())
        return;

    Buffer* destinyBuffer = new Buffer();
    //create AddBalls header
    Destiny::AddBall_header head = Destiny::AddBall_header();
        head.packet_type = 1;   // 0 = full state   1 = balls
        head.stamp = sEntityList.GetStamp();
    destinyBuffer->Append( head );

    AddBalls2 addballs2;
        addballs2.stateStamp = sEntityList.GetStamp();
        addballs2.extraBallData = new PyList();

    PyTuple* balls = new PyTuple(2);
        balls->SetItem(0, pSE->MakeSlimItem());
        balls->SetItem(1, pSE->MakeDamageState());
    addballs2.extraBallData->AddItem(balls);

    pSE->EncodeDestiny(*destinyBuffer);

    addballs2.state = new PyBuffer(&destinyBuffer); //consumed
    SafeDelete( destinyBuffer );

    if (is_log_enabled(DESTINY__BALL_DUMP))
        addballs2.Dump( DESTINY__BALL_DUMP, "    " );

    //send the update
    PyTuple* up = addballs2.Encode();
    for (auto cur : m_clients) {
        PyIncRef(up);
        cur.second->QueueDestinyUpdate(&up, true);
    }

    //cleanup
    PySafeDecRef(up);
}

void SystemManager::RemoveMarkerBall(SystemEntity* pSE) {
    // (at shutdown the clients are gone, though they may still be listed)
    if ((pSE == nullptr) or !m_loaded or sConsole.IsShutdown() or m_clients.empty())
        return;

    std::vector<Client*> inBubble;
    if (pSE->SysBubble() != nullptr)
        pSE->SysBubble()->GetPlayers(inBubble);

    RemoveBallsFromBP removeball;
        removeball.balls.push_back(pSE->GetID());
    PyTuple* up = removeball.Encode();
    for (auto cur : m_clients) {
        if (std::find(inBubble.begin(), inBubble.end(), cur.second) != inBubble.end())
            continue;
        PyIncRef(up);
        cur.second->QueueDestinyUpdate(&up);
    }
    PySafeDecRef(up);
}


void SystemManager::AddBounty(uint32 charID, BountyData& data) {
    // data comes from the npc killed:  fromID is its typeID, amount this pilot's share
    _log(CLIENT__TEXT, "AddBounty called for charID %u in system %s(%u).", charID, m_data.name.c_str(), m_data.systemID);
    BountyData& acc = m_bountyMap[charID];     // value-initialised on first use
    if (acc.toID == 0) {
        acc = data;
        acc.toID = charID;
        acc.amount = 0;
    }
    acc.amount += data.amount;
    ++m_ratMap[charID][static_cast<uint16>(data.fromID)];
}

void SystemManager::PayBounties() {
    /* one 'Bounty Prizes' journal entry per pilot per tic (Crucible paid every 20 minutes):
     *  referenceID = solarSystemID (the client shows the location)
     *  description = yaml dict {NBL: {typeID: count, ...}} plus NBLT when the list is truncated
     *  (client wallet._ParseDescription lists '<type name> x <count>' per entry and a 'truncated' line)
     */
    _log(CLIENT__TEXT, "PayBounties called for system %s(%u).", m_data.name.c_str(), m_data.systemID);
    static const size_t NBL_MAX = 20;

    for (auto& cur : m_bountyMap) {
        std::string desc;
        std::map<uint32, RatDataMap>::iterator itr = m_ratMap.find(cur.first);
        if ((itr != m_ratMap.end()) and !itr->second.empty()) {
            // most killed types first
            std::vector<std::pair<uint16, uint16>> rats(itr->second.begin(), itr->second.end());
            std::stable_sort(rats.begin(), rats.end(),
                    [](const std::pair<uint16, uint16>& a, const std::pair<uint16, uint16>& b) { return a.second > b.second; });
            desc = "{NBL: {";
            for (size_t n = 0; (n < rats.size()) and (n < NBL_MAX); ++n) {
                if (n > 0)
                    desc += ", ";
                desc += std::to_string(rats[n].first);
                desc += ": ";
                desc += std::to_string(rats[n].second);
            }
            desc += "}";
            if (rats.size() > NBL_MAX)
                desc += ", NBLT: 1";
            desc += "}";
        }

        if (cur.second.amount < 0.01)
            continue;
        try {
            AccountService::TransferFunds(corpCONCORD, cur.first, cur.second.amount, desc, Journal::EntryType::BountyPrizes, m_data.systemID);
        } catch (...) {
            _log(CLIENT__ERROR, "PayBounties() - unable to pay %.2f to %u in %s(%u)", cur.second.amount, cur.first, m_data.name.c_str(), m_data.systemID);
        }
    }

    m_ratMap.clear();
    m_bountyMap.clear();
}

void SystemManager::DoSpawnForBubble(SystemBubble* pBubble)
{
    if (!m_spawnMgr->IsInitialized())
        return;

    if (m_beltCount < 1)
        return;

    if (is_log_enabled(SPAWN__MESSAGE))
        _log(SPAWN__MESSAGE, "Spawn called for bubble %u(%u) in %s(%u)[%.4f], region %u.", \
             pBubble->GetID(), sBubbleMgr.GetBeltID(pBubble->GetID()), m_data.name.c_str(), \
             m_data.systemID, m_data.securityRating, m_data.regionID);

    /* belt spawns at once:  about half the belt count (npc ai plan phase 5 spawn generation, [Era, med];  npc.BeltSpawnCap,
     *  0 = one per belt as before) */
    uint32 cap(m_beltCount);
    if (sConfig.npc.BeltSpawnCap > 0.0f)
        cap = std::max<uint32>(1, std::min<uint32>(m_beltCount, static_cast<uint32>(std::ceil(m_beltCount * sConfig.npc.BeltSpawnCap))));
    if ((m_activeRatSpawns < cap) or (pBubble->IsGate())) {
        if (m_spawnMgr->DoSpawnForBubble(pBubble)) {
            m_ratBubbles.emplace(pBubble->GetID(), pBubble);
            if (is_log_enabled(SPAWN__TRACE))
                _log(SPAWN__TRACE, "SystemManager::DoSpawnForBubble() completed for %s(%u) in bubble %u.  %u items in m_ratBubbles", \
                        m_data.name.c_str(), m_data.systemID, pBubble->GetID(), m_ratBubbles.size());
        } else {
            if (is_log_enabled(SPAWN__TRACE))
                _log(SPAWN__TRACE, "SystemManager::DoSpawnForBubble() returned false for bubble %u.", pBubble->GetID());
        }
    }
}

void SystemManager::GetSpawnBubbles(SpawnBubbleMap* bubbleMap)
{
    if (is_log_enabled(SPAWN__MESSAGE))
        _log(SPAWN__MESSAGE, "SystemManager::GetSpawnBubbles() - called for %s(%u)", m_data.name.c_str(), m_data.systemID);
    for (auto cur : m_ratBubbles)
        bubbleMap->emplace(cur.first, cur.second);
}

void SystemManager::RemoveSpawnBubble(SystemBubble* pBubble)
{
    // only spawns we counted are uncounted (a spawn can be removed again after it moved)
    if (m_ratBubbles.erase(pBubble->GetID()) > 0) {
        if (pBubble->IsBelt()) {
            if (m_activeRatSpawns > 0)
                --m_activeRatSpawns;
        } else if (pBubble->IsGate()) {
            if (m_activeGateSpawns > 0)
                --m_activeGateSpawns;
        }
    }
    if (is_log_enabled(SPAWN__MESSAGE))
        _log(SPAWN__MESSAGE, "SystemManager::RemoveSpawnBubble() - called for bubbleID %u in %s(%u).", pBubble->GetID(), m_data.name.c_str(), m_data.systemID);
}

void SystemManager::MoveSpawnBubble(SystemBubble* pOld, SystemBubble* pNew)
{
    // a rat spawn moved to another bubble (npc group warp)
    if ((pOld == nullptr) or (pNew == nullptr) or (pOld == pNew))
        return;
    if (m_ratBubbles.find(pOld->GetID()) == m_ratBubbles.end())
        return;
    RemoveSpawnBubble(pOld);
    if (m_ratBubbles.emplace(pNew->GetID(), pNew).second) {
        if (pNew->IsBelt()) {
            ++m_activeRatSpawns;
        } else if (pNew->IsGate()) {
            ++m_activeGateSpawns;
        }
    }
}

uint32 SystemManager::GetRandBeltID()
{
    if (m_beltVector.empty())
        return 0;
    // MakeRandomInt() can return its upper bound
    uint32 index(MakeRandomInt(0, m_beltVector.size()));
    if (index >= m_beltVector.size())
        index = m_beltVector.size() - 1;
    return m_beltVector.at(index);
}

void SystemManager::ProcessDroneReturns() {
    if (m_droneReturns.empty())
        return;
    std::vector<uint32> returns;
    returns.swap(m_droneReturns);
    for (auto droneID : returns) {
        std::map<uint32, DroneSE*>::iterator itr = m_drones.find(droneID);
        if (itr == m_drones.end())
            continue;
        DroneSE* pDrone(itr->second);
        if (pDrone->FinishReturnToBay())
            SafeDelete(pDrone);     // no longer in space
    }
}

/* self-destruct.  "1 Minute 49 Seconds", as the SelfDestructTimer message captured from the live server */
static std::string FormatSelfDestructTime(uint32 seconds)
{
    uint32 minutes(seconds / 60);
    seconds %= 60;
    std::string time;
    if (minutes > 0)
        time = std::to_string(minutes) + (minutes == 1 ? " Minute" : " Minutes");
    if ((seconds > 0) or (minutes == 0)) {
        if (!time.empty())
            time += " ";
        time += std::to_string(seconds) + (seconds == 1 ? " Second" : " Seconds");
    }
    return time;
}

void SystemManager::SendSelfDestructToBubble(SystemEntity* pShipSE, uint32 charID, const char* msgKey, PyDict* args)
{
    // the *Other messages are for the other pilots around the ship (the client can mute them)
    std::vector<Client*> players;
    if ((pShipSE != nullptr) and (pShipSE->SysBubble() != nullptr))
        pShipSE->SysBubble()->GetPlayers(players);
    for (auto pClient : players) {
        if ((pClient == nullptr) or (pClient->GetCharacterID() == charID))
            continue;
        PyIncRef(args);
        pClient->SendRemoteMessage(msgKey, args);
    }
    PyDecRef(args);
}

void SystemManager::StartSelfDestruct(Client* pClient)
{
    ShipSE* pShipSE(pClient->GetShipSE());
    if (pShipSE == nullptr)
        return;
    uint32 charID(pClient->GetCharacterID()), shipTypeID(pShipSE->GetTypeID());
    int64 now(GetFileTimeNow());
    std::map<uint32, int64>::iterator itr = m_selfDestructRetry.find(charID);
    if (itr != m_selfDestructRetry.end()) {
        if (itr->second > now)
            throw UserError("SelfDestructTooEarly")
                    .AddFormatValue("shipTypeID", new PyInt(shipTypeID))
                    .AddFormatValue("timeRemaining", new PyLong(itr->second - now));
        m_selfDestructRetry.erase(itr);
    }

    SelfDestructData data = SelfDestructData();
        data.charID = charID;
        data.shipTypeID = shipTypeID;
        data.endTime = now + (int64)sConfig.world.SelfDestructDelay * EvE::Time::Second;
        data.nextMsg = now + 10 * EvE::Time::Second;
    m_selfDestructs[pShipSE->GetID()] = data;

    PyDict* args = new PyDict();
        args->SetItemString("what", Client::TypeNameArg(shipTypeID));
        args->SetItemString("when", new PyInt(sConfig.world.SelfDestructDelay));
    pClient->SendRemoteMessage("SelfDestructInitiated", args);

    args = new PyDict();
        args->SetItemString("charID", new PyInt(charID));
        args->SetItemString("shipTypeID", new PyInt(shipTypeID));
        args->SetItemString("timeRemaining", new PyLong(data.endTime - now));
    SendSelfDestructToBubble(pShipSE, charID, "SelfDestructInitiatedOther", args);

    _log(SHIP__MESSAGE, "%s started the self-destruct of %s(%u).", pClient->GetName(), pShipSE->GetName(), pShipSE->GetID());
}

bool SystemManager::AbortSelfDestruct(Client* pClient)
{
    ShipSE* pShipSE(pClient->GetShipSE());
    if (pShipSE == nullptr)
        return false;
    std::map<uint32, SelfDestructData>::iterator itr = m_selfDestructs.find(pShipSE->GetID());
    if (itr == m_selfDestructs.end())
        return false;
    int64 now(GetFileTimeNow());
    int64 left(itr->second.endTime - now);
    if (left < 0)
        left = 0;
    uint32 charID(itr->second.charID);
    m_selfDestructs.erase(itr);
    m_selfDestructRetry[charID] = now + (int64)sConfig.world.SelfDestructRetryDelay * EvE::Time::Second;

    PyDict* args = new PyDict();
        args->SetItemString("timeLeft", new PyLong(left));
    pClient->SendRemoteMessage("SelfDestructAborted2", args);

    args = new PyDict();
        args->SetItemString("charID", new PyInt(charID));
        args->SetItemString("timeLeft", new PyLong(left));
    SendSelfDestructToBubble(pShipSE, charID, "SelfDestructAbortedOther2", args);
    return true;
}

void SystemManager::CancelSelfDestruct(uint32 shipID, uint8 cancelType)
{
    std::map<uint32, SelfDestructData>::iterator itr = m_selfDestructs.find(shipID);
    if (itr == m_selfDestructs.end())
        return;
    uint32 charID(itr->second.charID), shipTypeID(itr->second.shipTypeID);
    m_selfDestructs.erase(itr);
    if (cancelType == SelfDestruct::Cancel::Silent)
        return;
    Client* pClient(sEntityList.FindClientByCharID(charID));
    if (pClient == nullptr)
        return;
    PyDict* args = new PyDict();
        args->SetItemString("what", Client::TypeNameArg(shipTypeID));
    pClient->SendRemoteMessage((cancelType == SelfDestruct::Cancel::Warp ? "SelfDestructCancelledWarp" : "SelfDestructCancelledExternal"), args);
}

void SystemManager::ProcessSelfDestructs()
{
    if (m_selfDestructs.empty())
        return;
    // destroying a ship changes the map, so work from a copy of the keys
    std::vector<uint32> shipIDs;
    for (auto cur : m_selfDestructs)
        shipIDs.push_back(cur.first);

    int64 now(GetFileTimeNow());
    for (auto shipID : shipIDs) {
        std::map<uint32, SelfDestructData>::iterator itr = m_selfDestructs.find(shipID);
        if (itr == m_selfDestructs.end())
            continue;
        SystemEntity* pSE(GetSE(shipID));
        if ((pSE == nullptr) or !pSE->IsShipSE() or pSE->IsDead()) {
            m_selfDestructs.erase(itr);
            continue;
        }
        // the pilot left the ship (eject, board)
        if (!pSE->HasPilot() or (pSE->GetPilot()->GetCharacterID() != itr->second.charID)) {
            CancelSelfDestruct(shipID, SelfDestruct::Cancel::External);
            continue;
        }
        Client* pClient(pSE->GetPilot());
        if (now < itr->second.endTime) {
            if (now >= itr->second.nextMsg) {
                itr->second.nextMsg += 10 * EvE::Time::Second;
                uint32 left((uint32)((itr->second.endTime - now + EvE::Time::Second - 1) / EvE::Time::Second));
                PyDict* args = new PyDict();
                    args->SetItemString("what", Client::TypeNameArg(itr->second.shipTypeID));
                    args->SetItemString("time", new PyString(FormatSelfDestructTime(left)));
                pClient->SendRemoteMessage("SelfDestructTimer", args);
            }
            continue;
        }
        // GM /unkillable
        if (SystemEntity::IsUnkillable(shipID)) {
            CancelSelfDestruct(shipID, SelfDestruct::Cancel::External);
            continue;
        }

        uint32 charID(itr->second.charID), shipTypeID(itr->second.shipTypeID);
        m_selfDestructs.erase(itr);

        PyDict* args = new PyDict();
            args->SetItemString("shipTypeID", new PyInt(shipTypeID));
        pClient->SendRemoteMessage("SelfDestructImmediate", args);
        args = new PyDict();
            args->SetItemString("shipTypeID", new PyInt(shipTypeID));
            args->SetItemString("charID", new PyInt(charID));
        SendSelfDestructToBubble(pSE, charID, "SelfDestructImmediateOther", args);

        _log(SHIP__MESSAGE, "%s(%u) of %s self-destructs.", pSE->GetName(), shipID, pClient->GetName());
        // no killer: the pilot is the final blow, aggressors get nothing (CrimeWatch), insurance still pays
        Damage d(pSE, true);
            d.weaponRef = pSE->GetSelf();
            d.selfDestruct = true;
        pSE->Destroy(d);
    }
}

/* the client keeps this rowset for its own drones.  rows for drones whose ball it doesn't have are listed as
 *  "in distant space".  owned drones that aren't listed are offered for reconnection, so uncontrolled drones are left out.
 *  lines must be lists (the client edits them in place), and the header is needed even with no rows
 */
void SystemManager::UpdateDroneIndex()
{
    uint32 stamp(sEntityList.GetStamp());
    if ((m_droneIndexBusy > 0) or (!m_droneIndexDirty and (m_droneIndexStamp == stamp)))
        return;
    m_droneIndexDirty = false;
    m_droneIndexStamp = stamp;
    m_dronesByController.clear();
    m_dronesByGuardChar.clear();
    for (auto cur : m_drones) {
        DroneSE* pDrone(cur.second);
        if ((pDrone == nullptr) or !pDrone->IsControlled())
            continue;
        m_dronesByController[pDrone->GetControllerID()].push_back(pDrone);
        if (pDrone->GetAI()->GetOrder() == DroneAI::Order::Guard)
            m_dronesByGuardChar[pDrone->GetAI()->GetActivityCharID()].push_back(pDrone);
    }
}

bool SystemManager::IsDroneControlledBy(DroneSE* pDrone, uint32 shipID)
{
    return ((pDrone != nullptr) and pDrone->IsControlled() and (pDrone->GetControllerID() == shipID));
}

PyObject* SystemManager::MakeDroneState(uint32 charID) const {
    PyList* header = new PyList(7);
        header->SetItemString(0, "droneID");
        header->SetItemString(1, "ownerID");
        header->SetItemString(2, "controllerID");
        header->SetItemString(3, "activityState");
        header->SetItemString(4, "typeID");
        header->SetItemString(5, "controllerOwnerID");
        header->SetItemString(6, "targetID");
    PyList* lines = new PyList();
    for (auto cur : m_drones) {
        DroneSE* pDrone(cur.second);
        if (!pDrone->IsControlled() or (charID == 0))
            continue;
        if ((pDrone->GetOwnerID() != charID) and (pDrone->GetControllerOwnerID() != charID))
            continue;
        uint32 targetID(pDrone->GetTargetID());
        PyList* line = new PyList(7);
            line->SetItem(0, new PyInt(cur.first));
            line->SetItem(1, new PyInt(pDrone->GetOwnerID()));
            line->SetItem(2, new PyInt(pDrone->GetControllerID()));
            line->SetItem(3, new PyInt(pDrone->GetState()));
            line->SetItem(4, new PyInt(pDrone->GetSelf()->typeID()));
            line->SetItem(5, new PyInt(pDrone->GetControllerOwnerID()));
            line->SetItem(6, (targetID == 0 ? PyStatic.NewNone() : new PyInt(targetID)));
        lines->AddItem(line);
    }

    PyDict* dict = new PyDict();
        dict->SetItemString("header", header);
        dict->SetItemString("RowClass", new PyToken("util.Row"));
        dict->SetItemString("lines", lines);
    return new PyObject("util.Rowset", dict);
}

// the balls a client in this bubble knows about: the system's static balls plus the bubble's visible balls.  keep in step with MakeSetState()
void SystemManager::GetBallparkEntities(const SystemBubble* pBubble, std::map<uint32, SystemEntity*>& into) const {
    for (auto cur : m_staticEntities)
        into.emplace(cur.first, cur.second);
    for (auto cur : m_opStaticEntities)
        into.emplace(cur.first, cur.second);
    if (pBubble != nullptr)
        pBubble->GetEntities(into);
}

void SystemManager::MakeSetState(const SystemBubble* pBubble,  SetState& into) const {
    using namespace Destiny;
    Buffer* stateBuffer(new Buffer());

    AddBall_header head = AddBall_header();
        head.packet_type = 0;   // 0 = full state   1 = balls
        head.stamp = into.stamp;
    stateBuffer->Append( head );

    std::map<uint32, SystemEntity*> visibleEntities;

    // get all static entities for this system
    for (auto cur : m_staticEntities)
        visibleEntities.emplace(cur.first, cur.second);

    // get all operational static entities and add to visibleEntities map
    for (auto cur : m_opStaticEntities)
        visibleEntities.emplace(cur.first, cur.second);

    // get our ship.  bubble->GetEntities() does not include cloaked items
    std::map<uint32, SystemEntity*>::const_iterator itr = m_ticEntities.find(into.ego);
    if (itr != m_ticEntities.end())
       visibleEntities.emplace(itr->first, itr->second);

    // query bubble to get dynamic entities
    pBubble->GetEntities(visibleEntities);

    into.slims = new PyList();
    into.slims->clear();
    into.effectStates = new PyList();
    into.effectStates->clear();
    into.allianceBridges = new PyList();
    into.allianceBridges->clear();  //activeBeacon and activeBridge data found in fleetSvc.py

    //go through all visible entities and gather the info we need...
    for (auto cur : visibleEntities) {
        if (!cur.second->IsMissileSE() or !cur.second->IsFieldSE())
            into.damageState[ cur.first ] = cur.second->MakeDamageState();

        into.slims->AddItem( new PyObject( "foo.SlimItem", cur.second->MakeSlimItem()));

        //append the destiny binary data...
        cur.second->EncodeDestiny( *stateBuffer );

        // get tower effect state (if applicable)
        if (cur.second->IsTowerSE())
            cur.second->GetTowerSE()->GetEffectState(*(into.effectStates));


        /** @todo (allan)  to be written   -jumpbridges is a PyList */
        //  if (cur.second->IsJumpBridgeSE)
        //ss.allianceBridges -- jumpbridges et al.
        //  [for shipID, toSolarsystemID, toBeaconID in bag.allianceBridges:]
    }

    into.destiny_state = new PyBuffer( &stateBuffer );
    {
        uint32 charID(0), allianceID(0);
        std::map<uint32, SystemEntity*>::const_iterator ego = m_entities.find(into.ego);
        if ((ego != m_entities.end()) and ego->second->HasPilot()) {
            charID = ego->second->GetPilot()->GetCharacterID();
            allianceID = ego->second->GetPilot()->GetAllianceID();
        }
        into.droneState = MakeDroneState(charID);
        // titan/black ops alliance jump portals open in this system (michelle -> pwn.OnAllianceBridgeModeChange)
        sFltSvc.GetAllianceBridges(m_data.systemID, allianceID, into.allianceBridges);
    }

    /* SolarSystem info.  this avoids the old way of a DB hit for every call.  */
    PyPackedRow* row = new PyPackedRow(sDataMgr.CreateHeader());
        row->SetField("itemID",        new PyLong(m_data.systemID));
        row->SetField("typeID",        new PyInt(5));
        row->SetField("ownerID",       PyStatic.NewOne());  // should this be owning factionID?  yes
        row->SetField("locationID",    new PyInt(m_data.constellationID));
        row->SetField("flagID",        PyStatic.NewZero());
        row->SetField("quantity",      PyStatic.NewNegOne());
        row->SetField("groupID",       new PyInt(5));
        row->SetField("categoryID",    new PyInt(2));
        row->SetField("customInfo",    new PyString(""));
    into.solItem = row;

    if (is_log_enabled(DESTINY__SETSTATE)) {
        _log( DESTINY__SETSTATE, "Current State of %s", m_data.name.c_str() );
        into.Dump( DESTINY__SETSTATE, "    " );
    }

    if (is_log_enabled(DESTINY__SETSTATE_DECODE)) {
        _log( DESTINY__SETSTATE_DECODE, "    Decoded:" );
        Destiny::DumpUpdate( DESTINY__SETSTATE_DECODE, &( into.destiny_state->content() )[0], (uint32)into.destiny_state->content().size() );
    }

    // aggression flags of the pilots in this system: {aggressorID: {victimID or None: time}}
    sCrimeWatch.GetAggressors(const_cast<SystemManager*>(this), into.aggressors);
}

void SystemManager::SendStaticBall(SystemEntity* pSE)
{
    if (m_clients.empty())
        return;

    if (is_log_enabled(DESTINY__MESSAGE)) {
        if (pSE->SysBubble() != nullptr) { //Don't attempt to log if bubble is null (ie, on static structure initial launch)
            GPoint bCenter(pSE->SysBubble()->GetCenter());
            _log(DESTINY__MESSAGE, "SystemManager::SendStaticBall() - Adding static entity %s(%u) to bubble %u.  Dist to center: %.2f", \
            pSE->GetName(), pSE->GetID(), pSE->SysBubble()->GetID(), bCenter.distance(pSE->GetPosition()));
        }
    }

    Buffer* destinyBuffer = new Buffer();
    //create AddBalls header
    Destiny::AddBall_header head = Destiny::AddBall_header();
        head.packet_type = 1;   // 0 = full state   1 = balls
        head.stamp = sEntityList.GetStamp();
    destinyBuffer->Append( head );

    AddBalls2 addballs2;
    addballs2.stateStamp = sEntityList.GetStamp();
    addballs2.extraBallData = new PyList();

    if (pSE->IsContainerSE()) {
        addballs2.extraBallData->AddItem(pSE->MakeSlimItem());
    } else {
        PyTuple* balls = new PyTuple(2);
        balls->SetItem(0, pSE->MakeSlimItem());
        balls->SetItem(1, pSE->MakeDamageState());
        addballs2.extraBallData->AddItem(balls);
    }

    if (addballs2.extraBallData->size() < 1) {
        SafeDelete( destinyBuffer );
        return;
    }

    pSE->EncodeDestiny(*destinyBuffer);
    addballs2.state = new PyBuffer(&destinyBuffer); //consumed

    if (is_log_enabled(DESTINY__BALL_DUMP))
        addballs2.Dump( DESTINY__BALL_DUMP, "    " );
    _log( DESTINY__BALL_DECODE, "    Ball Decoded:" );
    if (is_log_enabled(DESTINY__BALL_DECODE))
        Destiny::DumpUpdate( DESTINY__BALL_DECODE, &( addballs2.state->content() )[0], (uint32)addballs2.state->content().size() );

    //send the update
    PyTuple* rsp = addballs2.Encode();
    // does this need to be incremented?  the others do...
    for (auto cur : m_clients) {
        PyIncRef(rsp);
        cur.second->QueueDestinyUpdate(&rsp, true);
    }

    //cleanup
    PySafeDecRef(rsp);
    SafeDelete(destinyBuffer);
}

void SystemManager::AddItemToInventory(InventoryItemRef iRef)
{
    m_solarSystemRef->AddItemToInventory( iRef );
}

void SystemManager::RemoveItemFromInventory(InventoryItemRef iRef)
{
    // just in case this is called from elsewhere (which it may be), make sure we remove entity from our map.
    auto itr = m_entities.find(iRef->itemID());
    if (itr != m_entities.end()) {
        _log(ITEM__TRACE, "%s(%u): Removed from system manager for %s(%u)", iRef->name(), iRef->itemID(), m_data.name.c_str(), m_data.systemID);
        m_entities.erase(itr);
    } else {
        _log(ITEM__WARNING, "%s(%u): Called RemoveEntity(), but they weren\'t found in system manager for %s(%u)", \
                iRef->name(), iRef->itemID(), m_data.name.c_str(), m_data.systemID);
    }

    m_solarSystemRef->RemoveItemFromInventory( iRef );
}

SystemEntity* SystemManager::GetSE(uint32 entityID) const {
    std::map<uint32, SystemEntity*>::const_iterator itr = m_entities.find(entityID);
    if (itr == m_entities.end())
        return nullptr;
    return itr->second;
}

NPC* SystemManager::GetNPCSE(uint32 entityID) const
{
    std::map<uint32, NPC*>::const_iterator itr = m_npcs.find(entityID);
    if (itr == m_npcs.end())
        return nullptr;
    return itr->second;
}

ShipItemRef SystemManager::GetShipFromInventory(uint32 shipID)
{
    return ShipItemRef::StaticCast( m_solarSystemRef->GetMyInventory()->GetByID( shipID ) );
}

CargoContainerRef SystemManager::GetContainerFromInventory(uint32 contID)
{
    return CargoContainerRef::StaticCast( m_solarSystemRef->GetMyInventory()->GetByID( contID ) );
}

StationItemRef SystemManager::GetStationFromInventory(uint32 stationID)
{
    return StationItemRef::StaticCast( m_solarSystemRef->GetMyInventory()->GetByID( stationID ) );
}

static const uint16 tutorialSiteTypeID = 10067;     // Dust Cloud (group Cloud, which the tutorial looks for)

SystemEntity* SystemManager::GetTutorialSite()
{
    if (m_tutorialSiteID != 0) {
        SystemEntity* pSE(GetSE(m_tutorialSiteID));
        if (pSE != nullptr)
            return pSE;
        m_tutorialSiteID = 0;
    }
    // made before (it is saved with the system's other dynamic items)
    for (auto cur : m_entities) {
        if ((cur.second == nullptr) or (cur.second->GetTypeID() != tutorialSiteTypeID))
            continue;
        if (cur.second->GetSelf()->itemName() == "Tutorial Site") {
            m_tutorialSiteID = cur.first;
            return cur.second;
        }
    }

    // 50km from the warp-in point of the planet nearest the sun
    if (m_planetMap.empty())
        return nullptr;
    SystemEntity* pPlanet(GetPlanet(GetClosestPlanetID(NULL_ORIGIN)));
    if (pPlanet == nullptr)
        return nullptr;
    GPoint pos(WarpIn::Planet(pPlanet->GetID(), pPlanet->GetPosition(), pPlanet->GetRadius()));
    pos.MakeRandomPointOnSphere(50000);

    ItemData idata(tutorialSiteTypeID, 1/*EVE System*/, m_data.systemID, flagNone, "Tutorial Site", pos);
    InventoryItemRef iRef(sItemFactory.SpawnItem(idata));
    if (iRef.get() == nullptr)
        return nullptr;
    DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.itemID = iRef->itemID();
        entity.itemName = iRef->itemName();
        entity.typeID = iRef->typeID();
        entity.groupID = iRef->groupID();
        entity.categoryID = iRef->categoryID();
        entity.ownerID = 1;
        entity.position = pos;
    if (!BuildDynamicEntity(entity)) {
        iRef->Delete();
        return nullptr;
    }
    m_tutorialSiteID = iRef->itemID();
    _log(SERVICE__MESSAGE, "Tutorial site %u made in %s", m_tutorialSiteID, GetName());
    return GetSE(m_tutorialSiteID);
}

SystemEntity* SystemManager::GetPlanet(uint32 planetID)
{
    std::map<uint32, SystemEntity*>::iterator itr = m_planetMap.find(planetID);
    if (itr != m_planetMap.end())
        return itr->second;
    return nullptr;
}

uint32 SystemManager::GetClosestPlanetID(const GPoint& myPos)
{
    std::map<double, SystemEntity*> sorted;
    for (auto cur : m_planetMap)
        sorted.insert(std::pair<double, SystemEntity*>(myPos.distance(cur.second->GetPosition()), cur.second));

    // 0 when the system has no planets
    if (sorted.empty())
        return 0;
    std::map<double, SystemEntity*>::iterator itr = sorted.begin();

    return itr->second->GetID();
}

SystemEntity* SystemManager::GetClosestPlanetSE(const GPoint& myPos)
{
    // nullptr when the system has none
    SystemEntity* pClosest(nullptr);
    double best(-1);
    for (auto cur : m_planetMap) {
        double dist(myPos.distance(cur.second->GetPosition()));
        if ((best < 0) or (dist < best)) {
            best = dist;
            pClosest = cur.second;
        }
    }
    return pClosest;
}

SystemEntity* SystemManager::GetClosestGateSE(const GPoint& myPos)
{
    // nullptr when the system has none
    SystemEntity* pClosest(nullptr);
    double best(-1);
    for (auto cur : m_gateMap) {
        double dist(myPos.distance(cur.second->GetPosition()));
        if ((best < 0) or (dist < best)) {
            best = dist;
            pClosest = cur.second;
        }
    }
    return pClosest;
}

SystemEntity* SystemManager::GetClosestMoonSE(const GPoint& myPos)
{
    // nullptr when the system has none
    SystemEntity* pClosest(nullptr);
    double best(-1);
    for (auto cur : m_moonMap) {
        double dist(myPos.distance(cur.second->GetPosition()));
        if ((best < 0) or (dist < best)) {
            best = dist;
            pClosest = cur.second;
        }
    }
    return pClosest;
}

void SystemManager::GetMoonsInRange(const GPoint& pos, double range, std::vector<SystemEntity*>& into)
{
    // moons whose surface is within range of pos
    for (auto cur : m_moonMap)
        if ((pos.distance(cur.second->GetPosition()) - cur.second->GetRadius()) <= range)
            into.push_back(cur.second);
}

uint32 SystemManager::PlayersInSpace()
{
    if ((m_inSpaceTick == m_aiTick) and (m_aiTick != 0))
        return m_inSpace;
    m_inSpaceTick = m_aiTick;
    m_inSpace = 0;
    for (auto& cur : m_clients)
        if ((cur.second != nullptr) and cur.second->IsInSpace() and (cur.second->GetShipSE() != nullptr))
            ++m_inSpace;
    return m_inSpace;
}

void SystemManager::GetClientList(std::vector< Client* >& cVec)
{
    for (auto cur : m_clients)
        cVec.push_back(cur.second);
}

void SystemManager::DScan(int64 range, const GPoint& pos, std::vector<SystemEntity*>& vector )
{
    /* Crucible directional scanner shows everything in range that isnt cloaked:
     *   ships (incl capsules, covert ops and recons - d-scan immunity came in 2013), npc ships and sentries,
     *   drones, scanner probes, celestials (sun, planets, moons, belts, gates, stations), structures, customs offices,
     *   deployables, force fields, cyno fields, warp disruption bubbles, wrecks and containers.
     * not shown:  cloaked ships, missiles, site beacons (cosmic signatures/anomalies), bubble markers and temp clouds.
     *   asteroids only with server.AsteroidsOnDScan.
     */
    SystemEntity* pSE(nullptr);
    for (auto cur : m_entities) {
        pSE = cur.second;
        if (pSE == nullptr)
            continue;
        if (pSE->IsMissileSE())
            continue;
        if (pSE->IsAsteroidSE() or IsAsteroidID(cur.first))
            if (!sConfig.server.AsteroidsOnDScan)
                continue;
        if (pSE->IsAnomalySE()
        or (pSE->GetGroupID() == EVEDB::invGroups::Cosmic_Signature)
        or (pSE->GetGroupID() == EVEDB::invGroups::Cosmic_Anomaly))
            continue;
        // temp containers are bubble/debug markers
        if (IsTempItem(cur.first))
            if (pSE->IsContainerSE() or (pSE->GetGroupID() == EVEDB::invGroups::Temporary_Cloud))
                continue;
        if (pSE->DestinyMgr() != nullptr)
            if (pSE->DestinyMgr()->IsCloaked())
                continue;
        // made it this far.  add item to scan list
        if (pos.distance(pSE->GetPosition()) < range)
            vector.push_back(pSE);
    }
}

PyRep* SystemManager::GetCurrentEntities()
{
    /*  return list of dict
     * itemID, typeID, catID, name, pos[x,y,z]
     *
     * already have statics, so add players, empty ships, pos', npcs, drones  (anything that requires a tic)
     */

    PyList* list = new PyList();
    for (auto cur : m_ticEntities) {
        PyDict* dict = new PyDict();
            dict->SetItemString("itemID", new PyInt(cur.first));
            dict->SetItemString("ownerName", new PyString(sDataMgr.GetOwnerName(cur.second->GetOwnerID())));
            dict->SetItemString("typeID", new PyInt(cur.second->GetTypeID()));
            dict->SetItemString("catID", new PyInt(cur.second->GetCategoryID()));
            dict->SetItemString("name", new PyString(cur.second->GetName()));
            dict->SetItemString("x", new PyLong(cur.second->x()));
            dict->SetItemString("y", new PyLong(cur.second->y()));
            dict->SetItemString("z", new PyLong(cur.second->z()));
        list->AddItem(dict);
    }
    return list;
}

void SystemManager::GetAllEntities(std::vector< CosmicSignature >& vector)
{
    /** @todo this will need to put entity's sigID into anomaly map for Scan::WarpTo object */
    /** @todo this should be updated/current/correct in system's AnomalyMgr.  try to get data from there for this list  */
    for (auto cur : m_ticEntities) {
        CosmicSignature sig = CosmicSignature();
        sig.dungeonType = Dungeon::Type::Anomaly;
        sig.ownerID = cur.second->GetOwnerID();
        sig.sigID = sEntityList.GetAnomalyID();         // result.id
        sig.sigItemID = cur.first;
        sig.sigStrength = 0.9f; // these arent warpable yet
        sig.systemID = m_data.systemID;
        sig.position = cur.second->GetPosition();
        sig.sigGroupID = cur.second->GetGroupID();      // result.groupID
        sig.sigTypeID = cur.second->GetTypeID();        // result.typeID
        // if scanGroupID is anom or sig, use scanAttributeID to determine site type (in client code)
        // scanGroupID must be one of the 5 groups coded in client (sig, anom, ship, drone, structure)
        // scanGroupID of sig and anom are cached on client side
        switch (cur.second->GetCategoryID()) {
            case EVEDB::invCategories::Drone:
            case EVEDB::invCategories::Charge: { // probes, missiles (at time of scan), and ??
                sig.scanAttributeID = AttrScanStrengthDronesProbes;   // result.strengthAttributeID
                sig.scanGroupID = Scanning::Group::DroneOrProbe;
            } break;
            case EVEDB::invCategories::Orbitals:
            case EVEDB::invCategories::Structure:
            case EVEDB::invCategories::StructureUpgrade:
            case EVEDB::invCategories::SovereigntyStructure: {
                sig.scanAttributeID = AttrScanStrengthStructures;   // result.strengthAttributeID
                sig.scanGroupID = Scanning::Group::Structure;
            } break;
            case EVEDB::invCategories::Ship: {
                sig.scanAttributeID = AttrScanStrengthShips;   // result.strengthAttributeID
                sig.scanGroupID = Scanning::Group::Ship;
            } break;
            case EVEDB::invCategories::Entity: {
                sig.scanAttributeID = AttrScanStrengthSignatures;       // result.strengthAttributeID
                sig.scanGroupID = Scanning::Group::Signature;    // Scrap(1) is for filter only
                sig.sigName = cur.second->GetName(); // result.DungeonName  -  only used when scanGroupID is sig or anom
            } break;
            case EVEDB::invCategories::Asteroid:
            case EVEDB::invCategories::Celestial:
            case EVEDB::invCategories::Deployable: // mobile warp disruptor
            default: {
                sig.scanAttributeID = AttrScanAllStrength;     // result.strengthAttributeID (Unknown)
                sig.scanGroupID = Scanning::Group::Anomaly; // Celestial(64) is only for filter
                sig.sigName = cur.second->GetName(); // result.DungeonName  -  only used when scanGroupID is sig or anom
            } break;
        }
        vector.push_back(sig);
    }
}


//  time related methods to manipulate hour/24hour map data
void SystemManager::UpdateData()
{
    MapDB::UpdatePilotCount(m_data.systemID, m_docked, (m_players - m_docked));

    uint16 jumps = 0;
    uint16 stamp = sEntityList.GetStamp() -60;
    std::map<uint32, uint8>::iterator itr = m_jumpMap.begin();
    while (itr != m_jumpMap.end()) {
        if (itr->first < stamp) {
            itr = m_jumpMap.erase(itr);
            continue;
        } else {
            jumps += itr->second;
            ++itr;
        }
    }

    // this is jumps/hour data
    //MapDB::UpdateJumps(m_data.systemID, jumps);

    // if system and jumpmap are both empty, set activity time for unload timer
    if (SafeToUnload())
        if (m_activityTime == 0)
            if (m_clients.empty())
                if (m_jumpMap.empty())
                    m_activityTime = sEntityList.GetStamp() -50;
    ManipulateTimeData();
}

// checks for if it is safe to mark the system for unloading
bool SystemManager::SafeToUnload()
{
    // CONCORD on its way, or ships left in space by pilots who logged off
    if (m_law->IsBusy() and !sConsole.IsShutdown())
        return false;
    for (auto cur: GetEntities() ) {
        // starbases run in real time (fuel, reinforcement, moon mining):  systems with online towers stay loaded
        if (sConfig.pos.KeepTowerSystemsLoaded and !sConsole.IsShutdown()
        and cur.second->IsTowerSE() and cur.second->GetTowerSE()->IsPowered())
            return false;
        //If there are any ongoing operations by operational static structures, we don't want to unload the system until this is complete
        if (cur.second->IsPOSSE()) {
            if ((cur.second->GetPOSSE()->GetProcState() == EVEPOS::ProcState::Unanchoring) or
            (cur.second->GetPOSSE()->GetProcState() == EVEPOS::ProcState::Anchoring) or
            (cur.second->GetPOSSE()->GetProcState() == EVEPOS::ProcState::Offlining) or
            (cur.second->GetPOSSE()->GetProcState() == EVEPOS::ProcState::Onlining)) {
                return false;
            }
        }
        // colonies don't keep a system loaded:  they are simulated when they are looked at (Colony::SimulateToNow)
        //  customs office state changes do (an anchoring gantry, a reinforced office)
        if (cur.second->IsCOSE() and cur.second->GetCOSE()->IsInTimedState())
            return false;
    }
    return true; //by default, its always safe to unload
}

// not sure how to do this one yet...
void SystemManager::ManipulateTimeData()
{
    int64 timeNow = GetFileTimeNow();
    timeNow += EvE::Time::Hour;

    //if (m_killData.killsDateTime < timeNow)

    MapDB::UpdateKillData(m_data.systemID, m_killData);
}

void SystemManager::GetDockedCount()
{
    m_docked = 0;
    for (auto cur : m_clients)
        if (cur.second->IsDocked())
            ++m_docked;
}

void SystemManager::GetPlayerCount()
{
    // not used yet
}

/*
    uint16 killsHour;
    uint16 kills24Hour;
    uint16 factionKills;
    uint16 factionKills24Hour;
    uint16 podKillsHour;
    uint16 podKills24Hour;

    int64 killsDateTime;
    int64 kills24DateTime;
    int64 factionDateTime;
    int64 faction24DateTime;
    int64 podDateTime;
    int64 pod24DateTime;
    */


bool SystemManager::IsNull(std::map<uint32, SystemEntity*>::iterator& i)
{
    /* you have to change this parameter to the type of the container
     * you are using and the type of the element inside the container.
     *   if this finds use again, it should be changed to a template.
     */

    uint8 buffer[sizeof(i)];
    memset(buffer, 0, sizeof(i));
    memcpy(buffer, &i, sizeof(i));
    return *buffer == 0;
    /* I found that the size of any iterator is 12 bytes long.
     * I also found that if the first byte of the iterator that
     * is copy to the buffer is zero, then the iterator is invalid.
     * Otherwise it is valid. I like to call invalid iterators also as "null iterators".
     */
}