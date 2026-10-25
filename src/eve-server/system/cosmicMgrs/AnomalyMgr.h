 /**
  * @name AnomalyMgr.h
  *     Anomaly management system for EVEmu
  *
  * @Author:        Allan
  * @date:          12 December 2015
  *
  */

#ifndef EVEMU_SYSTEM_ANOMALYMGR_H_
#define EVEMU_SYSTEM_ANOMALYMGR_H_

/*  this class is in charge of creating/destroying and maintaining
 * anomaly types in it's system.
 *
 *  a new iteration of this class is created for each system as that system is booted.
 */


#include "system/cosmicMgrs/ManagerDB.h"

class DungeonMgr;
class BeltMgr;
class EVEServiceManager;
class SpawnMgr;
class SystemManager;

class AnomalyMgr
{
  public:
    AnomalyMgr(SystemManager* mgr, EVEServiceManager& svc);
    ~AnomalyMgr();

    bool Init(BeltMgr* beltMgr, DungeonMgr* dungMgr, SpawnMgr* spawnMgr);
    void Close();
    void Process();

    // a site of this type.  GMs can name the dungeon and where it goes (/ded).  returns the signature's itemID, 0 on failure
    uint32 CreateAnomaly(int8 typeID, uint32 dungeonID = 0, const GPoint* position = nullptr);
    void SaveAnomaly(CosmicSignature& sig);
    void LoadAnomalies();

    //  assign sigID and add to anom list to allow showing on scanner
    void AddSignal(SystemEntity* pSE, uint32 id = 0);
    // a structure that's found like a combat site (a factional warfare control bunker):  on the scanner as an anomaly
    void AddSiteSignal(SystemEntity* pSE, const std::string& name);
    void RemoveSignal(uint32 itemID);
    // list for ship scanner
    void GetAnomalyList(std::vector< CosmicSignature >& sig);
    // list for probe
    void GetSignatureList(std::vector< CosmicSignature >& sig);

    uint32 GetAnomalyID(std::string& sigID);
    GPoint GetAnomalyPos(std::string& sigID);

    const char* GetScanGroupName(uint8 groupID=0);

    // a wormhole end WormholeMgr made here:  on the scanner, and saved
    void RegisterWormhole(CosmicSignature &sig);
    // a site of this type turns up after the delay (a wandering wormhole that collapsed)
    void QueueSite(uint8 dungeonType, uint32 delaySeconds);

    // a random site position in this system (inside its edge, off other grids)
    GPoint GetSitePosition();
    // a dungeon site was completed or expired:  off the scanner, and a replacement is queued
    void OnSiteCompleted(const CosmicSignature& sig);
    // infrastructure hub upgrade levels (SovereigntyDataMgr):  pirate detection, survey networks, ore prospecting
    void SetUpgradeBonus(uint8 combatLevel, uint8 exploreLevel, uint8 oreLevel = 0);

protected:
    ManagerDB m_mdb;
    ServiceDB m_sdb;

    uint8 GetDungeonType();
    // w-space:  the Sleeper sites of this system's class (m_whClass) and a wandering wormhole now and then
    void InitWSpace();

private:
    /* we do not own any of these (our sysmgr does) */
    BeltMgr* m_beltMgr;
    DungeonMgr* m_dungMgr;
    SpawnMgr* m_spawnMgr;
    SystemManager* m_system;
    EVEServiceManager& m_services;

    Timer m_spawnTimer;
    Timer m_procTimer;

    bool m_initalized;
    bool m_firstSpawn;

    // internal data counters   hard-capped at 256/128
    uint8 m_maxSigs;    // max total for this system
    uint8 m_Sigs; // total probe-needed items, hard-capped at 256
    // these should be fine soft-capped at 128
    int8 m_WH;    // < 0 means "not allowed"
    int8 m_Grav; // < 0 means "not allowed"
    int8 m_Mag; // < 0 means "not allowed"
    int8 m_Ladar; // < 0 means "not allowed"
    int8 m_Radar; // < 0 means "not allowed"
    // simple combat sites, no probe needed
    int8 m_Unrated;  // < 0 means "not allowed"
    // DED sites
    int8 m_Complex;  // < 0 means "not allowed"
    // system total, including pos, wrecks, ships.  65535 *should* be large enough
    uint16 m_Anoms; // this counts signals added thru sysmgr also

    std::vector<uint8> m_typeList; // List of pregenerated signature types for this system
    std::multimap<int64, uint8> m_respawnQueue;     // due time -> site type replacing a completed one
    std::map<uint8, uint8> m_skip;                  // site type -> completed sites not to replace (upgrade went offline)
    uint8 m_bonusCombat;    // extra sites from sov upgrades
    uint8 m_bonusExplore;
    uint8 m_bonusOre;
    uint8 m_whClass;        // w-space class 1-6 (Sleeper sites), 0 = known space

    std::map<uint32, CosmicSignature> m_sigByItemID;            // signatures in system - need probes to scan down
    std::map<uint32, CosmicSignature> m_anomByItemID;           // anomalies in system - no probes needed
    std::map<std::string, CosmicSignature> m_sigBySigID;        // map for all signatures in system
};

#endif  // EVEMU_SYSTEM_ANOMALYMGR_H_
