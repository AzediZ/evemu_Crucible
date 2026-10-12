 /**
  * @name IncursionMgr.h
  *     Sansha incursion management for EVEmu
  *
  * @date:          12 October 2026
  *
  *  first playable cut:
  *   - one or more incursions, each covering a constellation (staging, vanguard, assault and headquarters systems)
  *   - vanguard sites appear as combat anomalies in loaded vanguard systems.  each site has waves of Sansha ships;
  *       the next wave spawns when the current one is dead.  clearing the last wave pays CONCORD ISK and LP
  *       to the pilots on grid and lowers Sansha influence
  *   - influence recovers slowly when no sites are completed.  at zero influence, or when the incursion is too old,
  *       it withdraws and then ends.  a new one starts automatically after a delay
  *  not done yet: Sansha EWAR, assault/HQ sites, the mothership, the system penalty, client UI
  */

#ifndef EVEMU_SYSTEM_INCURSIONMGR_H_
#define EVEMU_SYSTEM_INCURSIONMGR_H_

#include "utils/Singleton.h"
#include "POD_containers.h"

class SystemManager;

namespace Incursion {
    // internal state numbering (inferred)
    namespace State {
        enum {
            Established = 0,
            Mobilizing  = 1,
            Withdrawing = 2
        };
    }
}

struct IncSiteObject {
    uint8 wave;                 // 0 = structure for the whole site
    uint16 typeID;
    uint8 quantity;
    GPoint offset;              // from site beacon
};

struct IncSiteType {
    uint16 siteTypeID;
    uint8 sceneType;
    uint8 designFleet;
    uint8 waves;
    uint16 designNPCs;          // total ships over all waves at full size
    uint32 iskReward;           // per pilot, full fleet
    uint32 lpReward;
    float influence;            // percent
    std::string name;
    std::vector<IncSiteObject> objects;
};

struct IncLiveSite {
    uint32 incursionID;
    uint32 systemID;
    uint16 siteTypeID;
    uint8 wave;                 // current wave, 1-based
    uint16 spawned;             // ships spawned over all waves so far
    uint32 beaconID;
    GPoint position;
    std::string sigID;
    std::vector<uint32> structures;
    std::vector<uint32> npcs;   // live ships of the current wave
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
    std::map<uint32, uint8> systems;    // systemID, sceneType
};

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

    // GM commands.  each returns a message for the caller
    std::string Start(uint32 constellationID = 0);
    std::string End(uint32 constellationID = 0);
    std::string Info();
    std::string SetInfluence(uint32 constellationID, float percent);

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

    void LoadSiteTypes();
    void LoadIncursions();
    void Save(IncData& inc);
    IncData* FindByConstellation(uint32 constellationID);

    uint32 PickConstellation();
    bool AssignSystems(IncData& inc);

    void ProcessIncursion(IncData& inc, uint32 now, uint32 dt);
    void TopUpSites(IncData& inc, uint32 systemID, uint32 now);
    bool SpawnSite(IncData& inc, SystemManager* pSys, uint16 siteTypeID);
    void SpawnWave(IncLiveSite& site, SystemManager* pSys);
    void CompleteSite(IncLiveSite& site, SystemManager* pSys);
    void RemoveSite(IncLiveSite& site, SystemManager* pSys);
    void EndIncursion(uint32 incursionID);

    GPoint GetSitePoint(SystemManager* pSys);
    void Broadcast(const std::string& msg);
};

#define sIncursionMgr \
    ( IncursionMgr::get() )

#endif  // EVEMU_SYSTEM_INCURSIONMGR_H_
