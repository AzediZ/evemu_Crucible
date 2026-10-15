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
    Author:        Zhur
    Rewrite:    Allan
*/

#ifndef __SYSTEMMANAGER_H_INCL__
#define __SYSTEMMANAGER_H_INCL__

#include "system/BubbleManager.h"
#include "system/SolarSystem.h"
#include "system/SystemDB.h"
#include "chat/LSCService.h"
#include "npc/ai/NpcTelemetry.h"


class PyRep;
class PyDict;
class PyTuple;
class PyList;
class Client;
class NPC;
class InventoryItem;
class SystemEntity;
class SystemBubble;
class SetState;
class DestinyManager;
class DroneSE;

class AmbientSystem;
class AnomalyMgr;
class BeltMgr;
class DungeonMgr;
class SpawnMgr;
class LawEnforcement;
class InterdictionMgr;
class EVEServiceManager;

namespace SelfDestruct {
    // how a running self-destruct ended without exploding
    namespace Cancel {
        enum {
            Silent      = 0,
            Warp        = 1,    // SelfDestructCancelledWarp
            External    = 2     // SelfDestructCancelledExternal (pilot left the ship, docked, jumped...)
        };
    }
}

class DynamicEntityFactory {
public:
    // you MUST call (your SystemManager)->AddEntity([this returned object]) after this to actually put the entity in space
    static SystemEntity* BuildEntity(SystemManager& pSysMgr, const DBSystemDynamicEntity& entity);
};

class OutpostSE;
class SystemManager
{
public:
    SystemManager(uint32 systemID, EVEServiceManager &svc);//, ItemData idata);
    ~SystemManager();

    bool ProcessTic();          // called at 1Hz.
    void ProcessDestiny();      // called at 1Hz, before anything else is processed for the new stamp.  moves all entities one tick
    bool BootSystem();
    void UnloadSystem();
    void UpdateData();          // called from EntityList every 5m for active systems

    bool IsLoaded()                                     { return m_loaded; }

    SystemEntity* GetSE(uint32 entityID) const;
    NPC* GetNPCSE(uint32 entityID) const;
    const std::map<uint32, SystemEntity*>& GetTicEntities() const { return m_ticEntities; }
    ShipItemRef GetShipFromInventory(uint32 shipID);
    StationItemRef GetStationFromInventory(uint32 stationID);
    CargoContainerRef GetContainerFromInventory(uint32 contID);


    uint32 GetID() const                                { return m_data.systemID; }
    uint32 GetRegionID()                                { return m_data.regionID; }
    uint32 GetConstellationID()                         { return m_data.constellationID; }
    const std::string& GetNameStr() const               { return m_data.name; }
    const char* GetName() const                         { return m_data.name.c_str(); }
    const char* GetSystemSecurityClass()                { return m_data.securityClass.c_str(); }
    const float GetSystemSecurityRating()               { return m_data.securityRating; }

    EVEServiceManager& GetServiceMgr()                  { return m_services; }
    Inventory* GetSystemInv()                           { return m_solarSystemRef->GetMyInventory(); }
    SolarSystemRef GetSystemRef()                       { return m_solarSystemRef; }

    // for spawn system     -allan 15July15     (not complete)
    typedef std::map<uint32, SystemBubble*> SpawnBubbleMap;
    void RemoveSpawnBubble(SystemBubble* pBubble);
    void MoveSpawnBubble(SystemBubble* pOld, SystemBubble* pNew);
    void GetSpawnBubbles(SpawnBubbleMap* bubbleMap);
    void IncRatSpawnCount()                             { ++m_activeRatSpawns; }
    void DecRatSpawnCount()                             { --m_activeRatSpawns; }
    void IncGateSpawnCount()                            { ++m_activeGateSpawns; }
    void DecGateSpawnCount()                            { --m_activeGateSpawns; }
    void IncRoidSpawnCount()                            { ++m_activeRoidSpawns; }
    void DecRoidSpawnCount()                            { --m_activeRoidSpawns; }
    uint8 BeltCount()                                   { return m_beltCount; }
    uint8 GetRatSpawnCount()                            { return m_activeRatSpawns; }
    uint16 GetRoidSpawnCount()                          { return m_activeRoidSpawns; }
    uint32 PlayerCount()                                { return m_players; }
    // players whose ship is in space here (PlayerCount() counts docked pilots too).  counted once per tick
    uint32 PlayersInSpace();
    uint32 GetSysNPCCount()                             { return m_npcs.size(); }

    // CosmicMgr interface
    BeltMgr* GetBeltMgr()                               { return m_beltMgr; }
    SpawnMgr* GetSpawnMgr()                             { return m_spawnMgr; }
    AnomalyMgr* GetAnomMgr()                            { return m_anomMgr; }
    DungeonMgr* GetDungMgr()                            { return m_dungMgr; }
    // CONCORD, faction police, sentries, logged-off ships
    LawEnforcement* GetLaw()                            { return m_law; }
    // ambient npcs:  traffic, patrols, miners, criminals (npc/ambient/AmbientSystem.h)
    AmbientSystem* GetAmbient()                         { return m_ambient; }
    // warp disruption fields (bubbles) and the warp rules
    InterdictionMgr* GetInterdictionMgr()               { return m_interdiction; }

    // range is 0.1 for 1.0 system to 2.0 for -0.9 system
    float GetSecValue()                                 { return m_secValue; }

    // this create a SE with given data and add to system
    // addSignal false:  not listed on the scanner (dungeon objects)
    bool BuildDynamicEntity(const DBSystemDynamicEntity& entity, uint32 launcherID=0, bool addSignal=true);

    void AddNPC(NPC* pNPC);
    void RemoveNPC(NPC* pNPC);
    // an NPC that was destroyed (and so already removed from space) is going away
    void RemoveDeadNPC(NPC* pNPC);
    /* npc ai (doc/code_and_design_notes/planning/npc-ai/PLAN.md phase 5):  npcs and sentries that leave space (killed, GM /unspawn)
     *  without an owner freeing them are freed at the start of the next tick.  their destructor calls this (someone else
     *  freed them first:  forget them, here and in the npc list) */
    void ForgetRemovedNPC(SystemEntity* pSE);
    // this system's npc ai telemetry window (npc/ai/NpcTelemetry.h) and tick counter (level of detail)
    NpcStats& GetNpcStats()                             { return m_npcStats; }
    uint64_t GetAiTick() const                          { return m_aiTick; }
    void AddEntity(SystemEntity* pSE, bool addSignal=true);    // add entity to system, and (optionally) add signal to AnomalyMgr
    void RemoveEntity(SystemEntity* pSE);   // this also removes SE* from bubble and sig from AnomalyMgr (if applicable)
    void AddClient(Client* pClient, bool count=false, bool jump=false);
    void AddMarker(SystemEntity* pSE, bool sendBall=false, bool addSignal=false);    // rather specific here.
    /* the removal of a ball AddMarker(sendBall) gave every client here, for the clients outside its bubble (the bubble
     *  tells its own when the entity leaves it).  call while the entity is still in its bubble */
    void RemoveMarkerBall(SystemEntity* pSE);
    void RemoveClient(Client* pClient, bool count=false, bool jump=false);
    void SetDockCount(Client* pClient, bool docked=false);

    void AddItemToInventory(InventoryItemRef item);
    void RemoveItemFromInventory(InventoryItemRef item);
    void DoSpawnForBubble(SystemBubble* pBubble);

    /* this is to send new global ball added to system AFTER SetState has been sent */
    void SendStaticBall(SystemEntity* pSE);
    void MakeSetState(const SystemBubble* pBubble, SetState& into) const;
    // the balls a client in this bubble knows about (used for collisions)
    void GetBallparkEntities(const SystemBubble* pBubble, std::map<uint32, SystemEntity*>& into) const;

    /* drones in this system */
    const std::map<uint32, DroneSE*>& GetDrones() const { return m_drones; }
    /* the drones that may answer an attack on shipID (flown by charID):  the ship's drones, and drones guarding charID.
     *  each drone once, without walking every drone in the system (npc ai plan 6.7:  this runs on every hit).
     *  the index is rebuilt at most once per tick and whenever a drone enters or leaves space;  callers check the drone */
    template <class F>
    void ForEachAggressionDrone(uint32 shipID, uint32 charID, const F& fn) {
        UpdateDroneIndex();
        ++m_droneIndexBusy;     // a nested call (fn -> damage -> aggression) mustn't rebuild what we're walking
        std::unordered_map<uint32, std::vector<DroneSE*>>::const_iterator itr(m_dronesByController.find(shipID));
        if (itr != m_dronesByController.end())
            for (auto cur : itr->second)
                fn(cur);
        if (charID != 0) {
            itr = m_dronesByGuardChar.find(charID);
            if (itr != m_dronesByGuardChar.end())
                for (auto cur : itr->second)
                    if (!IsDroneControlledBy(cur, shipID))   // already visited
                        fn(cur);
        }
        --m_droneIndexBusy;
    }
    // a drone has reached its ship's drone bay.  it is moved there after this tick's processing
    void QueueDroneReturn(uint32 droneID)               { m_droneReturns.push_back(droneID); }
    // SetState droneState: the drones this pilot owns or controls, anywhere in the system
    PyObject* MakeDroneState(uint32 charID) const;

    /* ship self-destruct.  the pilot's current ship explodes SelfDestructDelay seconds after starting it */
    void StartSelfDestruct(Client* pClient);    // throws the client's errors
    bool AbortSelfDestruct(Client* pClient);    // the pilot changed their mind.  true if one was running
    void CancelSelfDestruct(uint32 shipID, uint8 cancelType);   // SelfDestruct::Cancel
    bool IsSelfDestructing(uint32 shipID) const         { return (m_selfDestructs.find(shipID) != m_selfDestructs.end()); }

    uint32 GetRandBeltID();
    uint32 GetClosestPlanetID(const GPoint& myPos);

    // system bounty timer system.  20m delay
    void AddBounty(uint32 charID, BountyData& data);

    SystemEntity* GetClosestMoonSE(const GPoint& myPos);
    SystemEntity* GetClosestPlanetSE(const GPoint& myPos);
    SystemEntity* GetClosestGateSE(const GPoint& myPos);
    // moons whose surface is within range (m) of pos
    void GetMoonsInRange(const GPoint& pos, double range, std::vector<SystemEntity*>& into);

    // this returns entities in range for display on dscan.
    void DScan(int64 range, const GPoint& pos, std::vector< SystemEntity* >& vector);
    // this returns entities in system for display on Groove's Entity Map in client
    PyRep* GetCurrentEntities();
    // this returns entities in system for display on ship scanner when enabled.
    void GetAllEntities(std::vector<CosmicSignature>& vector);

    std::map<uint32, SystemEntity*> GetOperationalStatics() { return m_opStaticEntities; }
    std::map<uint32, SystemEntity*> GetGates() { return m_gateMap; }
    // stargates in the system (the client's map.GetNumberOfStargates:  one per jump)
    uint32 GetGateCount() const                         { return (uint32)m_gateMap.size(); }

    SystemEntity* GetEntityByID(uint32 itemID) { return m_entities.find(itemID)->second; }

    void GetClientList(std::vector<Client*>& cVec);
    // an outpost built here (sieges are processed every tic)
    void AddOutpost(OutpostSE* pOSE)                    { m_outposts.push_back(pOSE); }
    const std::vector<OutpostSE*>& GetOutposts()        { return m_outposts; }

    std::map< uint32, SystemEntity* > GetEntities()     { return m_entities; }
    const std::map<uint32, SystemEntity*>& GetMoonMap() const { return m_moonMap; }

    SystemEntity* GetPlanet(uint32 planetID);

    // the tutorials' warp-to site in a rookie system: a dust cloud near the first planet.  made on first use, kept in the db
    SystemEntity* GetTutorialSite();
    // the effect beacon whose system effects (category 7) every ship here has:  a w-space phenomenon, or an incursion's
    //  penalty.  null when none
    InventoryItemRef GetEffectBeacon() const            { return m_effectBeacon; }
    // IncursionMgr:  the penalty beacon for this system's incursion role (typeID 0 = none), its figures times scale (influence)
    void SetIncursionBeacon(uint16 typeID, double scale);


protected:
    /** @todo  this needs more work */
    void PayBounties();

    /* hack to avoid errors with dock count */
    void GetDockedCount();
    void GetPlayerCount();

    bool LoadCosmicMgrs();
    bool LoadSystemStatics();
    bool LoadSystemDynamics();
    bool LoadPlayerDynamics();
    // a w-space phenomenon's effect beacon, at its secondary sun
    void SpawnEffectBeacon();

private:
    AnomalyMgr* m_anomMgr;      //we own this, never NULL.
    BeltMgr* m_beltMgr;         //we own this, never NULL.
    DungeonMgr* m_dungMgr;      //we own this, never NULL.
    SpawnMgr* m_spawnMgr;       //we own this, never NULL.
    LawEnforcement* m_law;      //we own this, never NULL.
    AmbientSystem* m_ambient;   //we own this, never NULL.
    InterdictionMgr* m_interdiction;    //we own this, never NULL.

    EVEServiceManager& m_services;
    LSCService* m_lsc;
    SolarSystemRef m_solarSystemRef;
    InventoryItemRef m_effectBeacon;
    double m_beaconScale;       // the incursion beacon's influence

    // static system data
    SystemData m_data;

    float m_secValue;  // range is 0.1 for 1.0 system to 2.0 for -0.9 system

    // for dynamic data system  -allan 10June2019
    SystemKillData m_killData;
    uint16 m_docked;
    void ManipulateTimeData();
    std::map<uint32, uint8> m_jumpMap;  // timestamp/jumps
    // may have to do kill data like jumps above

    // for spawn systems     -allan 15July15
    uint8 m_beltCount;
    uint8 m_gateCount;
    uint8 m_activeRatSpawns;
    uint8 m_activeGateSpawns;
    uint16 m_activeRoidSpawns;
    std::vector<uint32> m_beltVector;
    SpawnBubbleMap m_ratBubbles;  // map of id/bubble with rat spawns  - not actually used yet
    SpawnBubbleMap m_roidBubbles;  // map of id/bubble with roid spawns  - not actually used yet

    // for POS system       -allan 23July17
    std::map<uint32, SystemEntity*> m_moonMap;        // our container, but we DONT own the SE*
    std::map<uint32, SystemEntity*> m_planetMap;      // our container, but we DONT own the SE*
    std::map<uint32, SystemEntity*> m_gateMap;      // our container, but we DONT own the SE*

    // for grid Unloading system  -allan  27June2015
    bool m_loaded;
    bool SystemActivity();
    bool SafeToUnload();
    uint16 m_players;           // current total count
    uint32 m_activityTime;

    // system entity lists:
    bool m_entityChanged :1;
    std::map<uint32, NPC*> m_npcs;
    std::map<uint32, Client*> m_clients;
    std::map<uint32, SystemEntity*> m_entities;         // this list is all entities in this system.  we own these.
    std::map<uint32, SystemEntity*> m_ticEntities;      // this list is for entities that need process tics (objects, npc, client ships)
    std::map<uint32, SystemEntity*> m_staticEntities;   // this list is for static entities to send in setstate
    std::vector<OutpostSE*> m_outposts;                 // outposts and conquerable stations (sieges)
    std::map<uint32, SystemEntity*> m_opStaticEntities; // this list is for static entities which are operational and need to be initialized and operated upon even when system is empty
    std::map<uint32, DroneSE*> m_drones;                // all drones in space here.  we do not own these (they are also in m_entities)
    // ForEachAggressionDrone():  drones by controlling shipID and by the charID they guard
    std::unordered_map<uint32, std::vector<DroneSE*>> m_dronesByController;
    std::unordered_map<uint32, std::vector<DroneSE*>> m_dronesByGuardChar;
    uint32 m_droneIndexStamp;
    uint32 m_droneIndexBusy;
    bool m_droneIndexDirty;
    void UpdateDroneIndex();
    static bool IsDroneControlledBy(DroneSE* pDrone, uint32 shipID);
    std::vector<uint32> m_droneReturns;                 // drones to move into their ship's drone bay after this tick
    void ProcessDroneReturns();

    /* npc ai (npc ai plan phase 5):  npcs and sentries out of space that nobody else frees (killed, GM /unspawn).  we own
     *  these;  freed at the start of the next tick (FreeRemovedNPCs), when nothing of this tick holds them any more */
    std::map<uint32, SystemEntity*> m_removedNPCs;
    // an npc or sentry leaves space:  out of its group, spawn and dungeon, its locks and the modules on it gone, then freed later
    void OnNPCRemoved(SystemEntity* pSE);
    void FreeRemovedNPCs();
    NpcStats m_npcStats;                                // telemetry window (NPC__AI_STATS every npc.StatsInterval)
    uint64_t m_aiTick;                                  // ticks so far (npc level of detail)
    uint64_t m_inSpaceTick;                             // PlayersInSpace():  the tick its count is for
    uint32 m_inSpace;

    // running self-destructs
    struct SelfDestructData {
        uint32 charID;          // pilot who started it
        uint32 shipTypeID;
        int64 endTime;          // filetime
        int64 nextMsg;          // filetime of the next SelfDestructTimer message
    };
    std::map<uint32, SelfDestructData> m_selfDestructs;     // shipID -> data
    std::map<uint32, int64> m_selfDestructRetry;            // charID -> earliest next attempt after an abort (filetime)

    uint32 m_tutorialSiteID;                            // GetTutorialSite()
    void ProcessSelfDestructs();
    void SendSelfDestructToBubble(SystemEntity* pShipSE, uint32 charID, const char* msgKey, PyDict* args);

    // for bounty processing (20m timer)
    Timer m_bountyTimer;
    typedef std::map<uint16, uint16> RatDataMap;  // typeID/amt
    std::map<uint32, BountyData> m_bountyMap;  // charID/data
    std::map<uint32, RatDataMap> m_ratMap;  // charID/rat data

    // for PI timing
    Timer m_minutetimer;
    uint32 m_minutes;

    // check for null iterator.  this will need to be moved to a memory code file eventually.
    // unfortunately, this is very specific for which iterators it can check.  see notes in code.
    bool IsNull(std::map<uint32, SystemEntity*>::iterator& i);
};

#endif