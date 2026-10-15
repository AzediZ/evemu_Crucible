 /**
  * @name IncursionMgr.h
  *     Sansha incursion management for EVEmu
  *
  * @date:          12 October 2026
  *
  *   - one or more incursions, each covering a constellation (staging, vanguard, assault and headquarters systems)
  *   - every site is a beacon on the overview, warpable from anywhere in the system.  the beacon grid has one or two
  *       acceleration gates into the site's pockets, about a million km away (IncursionSites.cpp)
  *   - each site follows its EVE University wiki page: waves, triggers, hacking, ore and item deliveries, objectives.
  *       completing it pays CONCORD ISK to the pilots on grid and lowers Sansha influence.  LP goes into the
  *       incursion's pool, paid out when the mothership dies (DelayedLP)
  *   - at zero influence, once a headquarters site has been run, the mothership appears in the headquarters system.
  *       killing it ends the incursion.  otherwise it goes established, mobilizing, withdrawing, and ends
  *   - influence recovers slowly when no sites are completed.  a new incursion starts automatically after a delay
  *   - the constellation's system penalties (UpdatePenalties):  resists and damage by system role and influence, cynos jammed and
 *       bounties halved everywhere in it
 *  not done: staging sites, True Sansha loot, the most-damage payout rule
  */

#ifndef EVEMU_SYSTEM_INCURSIONMGR_H_
#define EVEMU_SYSTEM_INCURSIONMGR_H_

#include <set>

#include "utils/Singleton.h"
#include "POD_containers.h"

class PyList;
class PyRep;
class SystemManager;

namespace Incursion {
    // internal state numbering.  the client numbers them the other way round, see ClientState()
    namespace State {
        enum {
            Established = 0,
            Mobilizing  = 1,
            Withdrawing = 2
        };

        // the Journal's Incursions tab looks its status text up as [Withdrawing, Mobilizing, Established][state]
        //  (incursionjournal.py, seen in a client traceback), so the client's numbers are Withdrawing 0,
        //  Mobilizing 1, Established 2.  sending ours plus one gave 3 for Withdrawing, and the IndexError
        //  left the whole Journal window blank
        inline int ClientState(int state) { return Withdrawing - state; }
    }

    // incSiteObjects.role
    namespace Role {
        enum {
            Ship        = 0,    // wave ship.  the next wave comes when every Ship of the current wave is dead
            Free        = 1,    // wave ship that doesn't hold up the next wave
            Objective   = 2,    // ship or structure whose death completes the site
            PickOne     = 3,    // one of the PickOne entries of a wave spawns, as the objective
            Respawn     = 4,    // free ship that comes back param seconds after its whole group is dead
            Structure   = 5,    // scenery
            Gate        = 6,    // acceleration gate to room param.  locked until room 'wave' is cleared (0 = open)
            HiddenGate  = 7,    // acceleration gate to room param that appears once room 'wave' is cleared
            Container   = 8,    // site container (hacking, deliveries).  appears once wave 'wave' of its room is cleared (0 = from the start)
            Asteroid    = 9     // ore, param units
        };
    }

    // wave numbers with a special meaning
    namespace Wave {
        enum {
            Mining      = 254,  // ships that come when someone mines the site's ore
            Scripted    = 255   // spawned by the site script only
        };
    }

    // incSiteTypes.siteTypeID.  each has its own script
    namespace Site {
        enum {
            CommanderOutpost        = 1,
            MiningColony            = 2,
            OverrideTransferArray   = 3,
            CommanderStronghold     = 4,
            ConsolidationNetwork    = 5,
            CivilianFacility        = 6,
            RebirthFacility         = 7,
            ResearchCenter          = 8,
            ProvisionalHQ           = 9,
            KundaliniManifest       = 10,
            Uroborus                = 11
        };
    }
}

struct IncSiteObject {
    uint8 room;                 // 0 = beacon grid, 1+ = pockets behind gates
    uint8 wave;                 // see Incursion::Role
    uint8 role;
    uint8 quantity;
    uint16 typeID;
    int32 param;                // see Incursion::Role
    std::string name;           // item name, when not the type's
    GPoint offset;              // from the room's centre (the beacon)
};

struct IncSiteType {
    uint16 siteTypeID;
    uint32 sceneType;           // Incursion::scenesType.  boss = mothership
    uint8 designFleet;
    uint8 secBand;              // 0 any system, 1 high-sec only, 2 low/null-sec only, 3 high or null-sec, 4 low-sec only
    uint8 rooms;                // highest room number
    uint16 designNPCs;          // ships in the site's waves at full size (no respawns or reinforcements)
    uint32 iskReward;           // per pilot, full fleet
    uint32 lpReward;
    float influence;            // percent
    std::string name;
    std::map<uint8, uint8> waves;       // room, wave count
    std::vector<IncSiteObject> objects;
};

struct IncLiveItem {
    uint32 itemID;
    uint16 typeID;
    uint8 room;
    uint8 role;
};

struct IncGate {
    uint8 room;
    uint8 destRoom;
    uint8 lockRoom;             // locked until this room is cleared.  0 = open
};

struct IncRespawn {
    uint8 room;
    const IncSiteObject* obj;
    std::vector<uint32> npcs;
    uint32 due;                 // unix time the group comes back.  0 = still alive
};

struct IncLiveRoom {
    GPoint center;
    uint8 wave;                 // waves spawned so far
    uint8 waveDone;             // the last wave whose ships are all dead
    bool started;               // a pilot has been here
    bool cleared;
    uint32 beaconID;
    std::vector<uint32> npcs;   // ships holding up the next wave
    std::vector<uint32> others; // free, respawning and objective ships
    std::map<uint32, GPoint> lastPos;
    GPoint lastKill;            // where the last ship holding up the wave died
};

struct IncLiveSite {
    uint32 siteID;
    uint32 incursionID;
    uint32 systemID;
    uint16 siteTypeID;
    uint32 beaconID;            // the overview beacon
    GPoint position;
    uint16 spawned;             // wave ships spawned so far (for the payout)
    uint32 objectiveID;
    uint8 objectiveRoom;
    bool completed;
    uint32 completedAt;         // unix time
    uint32 emptySince;          // unix time the last pilot left a completed site.  0 = pilots still there
    std::map<uint8, IncLiveRoom> rooms;
    std::vector<IncLiveItem> items;
    std::map<uint32, IncGate> gates;
    std::vector<IncRespawn> respawns;
    // site script state
    uint32 repairsOffUntil;     // hostile repairs suppressed until (hacks, MTACs)
    uint32 nextRefill;          // next MTAC or transmitter
    uint32 nextReinforce;       // mothership escorts, timed waves
    uint32 nextMineSpawn;
    uint32 triggerTime;         // first uncloaked pilot in the research center
    uint8 civilians;            // civilian containers dropped
    bool unlocked;              // evacuation freighter open
    bool repairsOff;            // the hack's effect is applied to the ships
    std::set<uint32> hacked;
    std::map<uint32, int64> repairAt;                   // hacked mothership logistics:  ship, next cycle (ms)
    std::vector<std::pair<uint16, uint32>> away;        // ships sent off by the control relay: typeID, return time
    std::vector<std::pair<uint32, uint32>> leaving;     // ships warping off: itemID, time to remove them
};

struct IncData {
    uint32 incursionID;
    uint32 constellationID;
    uint32 stagingSystemID;
    uint8 state;
    double influence;           // Sansha influence, 1 = full
    uint32 startTime;           // unix time
    uint32 stateTime;
    uint32 lastProgress;
    bool hasBoss;               // the mothership is out
    bool hqRun;                 // a headquarters site has been completed
    bool endPending;            // mothership killed; end on the next tick
    std::map<uint32, uint8> systems;    // systemID, sceneType
};

class Client;
class NPC;
class SystemEntity;

class IncursionMgr
: public Singleton<IncursionMgr>
{
public:
    IncursionMgr();
    ~IncursionMgr()                                     { /* nothing to do */ }

    void Initialize();
    // called at 1Hz from EntityList
    void Process();

    // called by SystemManager::UnloadSystem().  the system removes the site entities itself; we drop our records
    void SystemUnloading(uint32 systemID);

    // KeeperService::ActivateAccelerationGate().  false when the gate isn't an incursion gate.  throws to refuse
    bool UseGate(Client* pClient, uint32 gateID);
    // a data analyzer cycle succeeded on this container.  true when an incursion site used it
    bool Hacked(Client* pClient, SystemEntity* pTarget);
    // ore was taken from this asteroid (mining lasers and mining drones)
    void OreMined(SystemEntity* pRoid);
    // a warp to dest inside a site's pocket ends at the site's beacon instead (into).  false = no change (DungeonMgr::RedirectWarp)
    bool RedirectWarp(uint32 systemID, const GPoint& dest, GPoint& into);

    // the system's role in an incursion (Incursion::scenesType), 0 when none or when penalties are off.  for the cyno jam and bounties
    uint32 PenaltyRole(uint32 systemID);

    // map service (journal and starmap)
    PyRep* GetGlobalReport();
    PyRep* GetSystems();
    // rewardMgr: the character's LP in each incursion's pool
    PyRep* GetDelayedRewards(uint32 charID, PyRep* groupIDs);

    // GM commands.  each returns a message for the caller
    std::string Start(uint32 constellationID = 0);
    std::string End(uint32 constellationID = 0);
    std::string Info();
    std::string SetInfluence(uint32 constellationID, float percent);
    std::string Mothership(uint32 constellationID);

private:
    bool m_initialized;
    uint32 m_nextSiteID;
    uint32 m_nextAutoStart;     // unix time
    uint32 m_lastTick;
    uint32 m_lastSave;
    Timer m_procTimer;

    std::map<uint16, IncSiteType> m_siteTypes;
    std::map<uint32, IncData> m_incursions;         // incursionID
    std::map<uint32, IncLiveSite> m_sites;          // siteID
    std::map<uint32, uint32> m_siteCooldown;        // systemID, unix time a new site may spawn
    std::map<uint32, uint32> m_index;               // gate, container and asteroid itemIDs, siteID

    void LoadSiteTypes();
    void LoadIncursions();
    void Save(IncData& inc);
    IncData* FindByConstellation(uint32 constellationID);

    uint32 PickConstellation();
    bool AssignSystems(IncData& inc);

    void ProcessIncursion(IncData& inc, uint32 now, uint32 dt);
    void TopUpSites(IncData& inc, uint32 systemID, uint32 sceneType, uint32 now);
    void SpawnBoss(IncData& inc, uint32 now);
    bool SpawnSite(IncData& inc, SystemManager* pSys, uint16 siteTypeID);
    void CompleteSite(IncLiveSite& site, SystemManager* pSys, uint8 room);
    void RemoveSite(IncLiveSite& site, SystemManager* pSys);
    void EndIncursion(uint32 incursionID, bool killed = false);
    void UpdatePenalties(IncData& inc, bool clear = false);
    void PayPool(IncData& inc);

    // IncursionSites.cpp: rooms, gates, waves and the site scripts
    bool ProcessSite(uint32 siteID, IncLiveSite& site, SystemManager* pSys, uint32 now);
    void SpawnRoomObjects(IncLiveSite& site, SystemManager* pSys, uint8 room);
    void StartRoom(IncLiveSite& site, SystemManager* pSys, uint8 room, uint32 now);
    void SpawnWave(IncLiveSite& site, SystemManager* pSys, uint8 room, uint8 wave, bool count = true);
    NPC* SpawnShip(IncLiveSite& site, SystemManager* pSys, uint8 room, uint16 typeID, const GPoint& pos, const std::string& name = "");
    void SpawnObject(IncLiveSite& site, SystemManager* pSys, const IncSiteObject& obj);
    SystemEntity* SpawnItem(IncLiveSite& site, SystemManager* pSys, uint8 room, uint8 role, uint16 typeID, const std::string& name, const GPoint& pos, bool global = false);
    void WaveCleared(IncLiveSite& site, SystemManager* pSys, uint8 room, uint8 wave);
    void RoomCleared(IncLiveSite& site, SystemManager* pSys, uint8 room, uint32 now);
    void ScriptTick(IncLiveSite& site, SystemManager* pSys, uint32 now);
    void SendAway(IncLiveSite& site, SystemManager* pSys, NPC* pNPC, uint32 now, bool returning = false);
    void RemoveItem(IncLiveSite& site, SystemManager* pSys, uint32 itemID);
    SystemEntity* FindItem(IncLiveSite& site, SystemManager* pSys, uint16 typeID);
    bool PilotsInRoom(IncLiveSite& site, SystemManager* pSys, uint8 room, std::vector<Client*>* into = nullptr);
    void TellRoom(IncLiveSite& site, SystemManager* pSys, uint8 room, const std::string& msg);
    GPoint RoomPoint(IncLiveSite& site, uint8 room, const GPoint& offset);

    GPoint GetSitePoint(SystemManager* pSys);
    void Broadcast(const std::string& msg);
};

#define sIncursionMgr \
    ( IncursionMgr::get() )

#endif  // EVEMU_SYSTEM_INCURSIONMGR_H_
