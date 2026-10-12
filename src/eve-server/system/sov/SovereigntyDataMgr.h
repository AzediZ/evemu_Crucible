
/**
  * @name SovereigntyDataMgr.h
  *   memory object caching system for managing and saving ingame static and dynamic sovereignty data
  *
  * @Author:         James
  * @date:           21Apr13
  *
  */

#ifndef _EVE_SERVER_SOVEREIGNTY_DATAMANAGER_H__
#define _EVE_SERVER_SOVEREIGNTY_DATAMANAGER_H__

#include "../../eve-server.h"

#include "system/sov/SovereigntyDB.h"

//Shiny multi-index containers
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

/* development indices  (client devIndexUtil.py)
 *  military and industrial indices run in bands of devIndexDecayRate * devIndexDecayTime points (levels 0..5)
 *  the strategic index is the time since the system was claimed
 */
namespace DevIndex {
    const int64 LevelSize   = 9600000;          // devIndexDecayRate(100000) * devIndexDecayTime(96)
    const int64 MaxPoints   = LevelSize * 6;    // top of the level 5 band
    // gain modifiers by current band
    const float MilitaryMod[6]   = {5.0f, 2.0f, 1.0f, 0.75f, 0.5f, 0.25f};
    const float IndustrialMod[6] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f};
    // days of sovereignty for strategic levels 1..5  (timeIndexLevels)
    const uint8 TimeLevelDays[6] = {0, 7, 21, 35, 65, 100};

    // level used for upgrade requirements:  count of band tops strictly below points, max 5  (sovSvc.GetDevIndexLevel)
    uint8 LevelForPoints(int64 points);
    // band the points are in, for gain modifiers
    uint8 BandForPoints(int64 points);
    // highest strategic level reached after this many whole days  (GetTimeIndexLevelForDays)
    uint8 LevelForClaimDays(int64 days);
}

namespace Sov {
    namespace Upgrade {
        // military/industrial upgrade chains, levels 1..5
        enum Family : uint8 {
            PirateDetection = 0,    // combat anomalies
            Entrapment,             // DED complexes
            QuantumFlux,            // wormholes
            OreProspecting,         // gravimetric sites
            SurveyNetworks,         // radar/magnetometric sites
            FamilyCount
        };
        // strategic upgrades
        enum : uint16 {
            CynosuralSuppression        = 2001,
            CynosuralNavigation         = 2008,
            SupercapitalConstruction    = 2009,
            AdvancedLogisticsNetwork    = 32422
        };
    }

    // why an upgrade can't be installed / stay online
    enum class UpgradeBlock : uint8 {
        None,
        DevIndex,               // military/industrial index too low
        SovTime,                // system not held long enough
        MissingRequired,        // required upgrade not installed and online
        Blocking,               // a blocking upgrade is installed
        Outpost                 // needs an upgraded outpost (not supported)
    };

    namespace AudioEvent {
        // const.sovAudioEvent*  (OnSovereigntyAudioEvent)
        enum : uint8 {
            StopOnline          = 0,
            StopDestroyed       = 1,
            FlagVulnerable      = 2,
            FlagDestroyed       = 3,    // {owner, alliance}
            FlagClaimed         = 4,    // {owner, system, alliance}
            OutpostReinforced   = 5,
            OutpostCaptured     = 6,
            HubReinforced       = 7,
            HubDestroyed        = 8,    // {owner}
            OutpostAttacked     = 9
        };
    }
}

class SovereigntyDataMgr
    : public Singleton<SovereigntyDataMgr>
{
public:
    SovereigntyDataMgr();
    ~SovereigntyDataMgr();

    int Initialize();

    void Clear();
    void Close();
    void GetInfo();

    PyRep *GetSystemSovereignty(uint32 systemID);
    PyRep *GetAllianceSystems();
    // KeyVals (solarSystemID, allianceID, claimID) of the systems an alliance holds (alliance window 'settled systems')
    PyRep *GetSystemsForAlliance(uint32 allianceID);
    // rows of locationID, allianceID, stationCount, militaryPoints, industrialPoints, claimedFor (days) for the
    //  systems of a constellation, the constellations of a region, the regions of the world (0) or a single system
    PyRep *GetCurrentSovData(uint32 locationID);
    PyRep *GetAllianceBeacons(uint32 allianceID);
    uint32 GetSystemAllianceID(uint32 systemID);
    void AddSovClaim(SovereigntyData data);
    void RemoveSovClaim(uint32 systemID);
    /* the alliance loses its claim on systemID (TCU gone or offline, unpaid sovereignty bill, claiming corp left, alliance closed):
     *  the claim (with its TCU and hub) is removed, the hub's upgrades go offline, every client gets the map change and the
     *  alliance (and the claiming corp when no longer a member) get the claim lost notifications.
     *  allianceID 0 drops whoever holds the claim, else only that alliance's.  returns false when there was nothing to drop
     */
    bool DropClaim(uint32 systemID, uint32 allianceID = 0);
    // every claim of allianceID, or only those claimed by corpID (0 = all)
    void DropAllianceClaims(uint32 allianceID, uint32 corpID = 0);
    void MarkContested(uint32 systemID, bool contested);
    SovereigntyData GetSovereigntyData(uint32 systemID);
    // every claim (sovereignty bills, F21)
    void GetClaims(std::vector<SovereigntyData>& into);
    void UpdateSystemHubID(uint32 systemID, uint32 hubID);
    void UpdateSystemJammerID(uint32 systemID, uint32 jammerID);
    void UpdateSystemBeaconID(uint32 systemID, uint32 beaconID);
    void UpdateClaim(uint32 systemID);

    /* development indices */
    // systems without a faction outside w-space can be claimed and gather indices
    bool IsClaimable(uint32 systemID);
    // npc kill in systemID, adds the bounty's military points
    void AddMilitaryActivity(uint32 systemID, double bountyISK);
    // m3 of ore or ice mined in systemID, adds industrial points
    void AddIndustrialActivity(uint32 systemID, double m3);
    // hourly index decay
    void Process();
    void SaveDevIndices();
    int64 GetMilitaryPoints(uint32 systemID);
    int64 GetIndustrialPoints(uint32 systemID);
    uint8 GetMilitaryLevel(uint32 systemID)             { return DevIndex::LevelForPoints(GetMilitaryPoints(systemID)); }
    uint8 GetIndustrialLevel(uint32 systemID)           { return DevIndex::LevelForPoints(GetIndustrialPoints(systemID)); }
    // whole days since the current claim, 0 if unclaimed
    uint32 GetClaimDays(uint32 systemID);
    uint8 GetStrategicLevel(uint32 systemID)            { return DevIndex::LevelForClaimDays(GetClaimDays(systemID)); }
    // devIndexManager.GetDevelopmentIndicesForSystem:  {attributeID: KeyVal(points, increasing)} for military and industrial
    PyRep* GetDevIndicesForSystem(uint32 systemID);
    // devIndexManager.GetAllDevelopmentIndices:  [KeyVal(solarSystemID, militaryPoints, industrialPoints, claimedFor)]
    PyRep* GetAllDevIndices();

    // OnSovereigntyAudioEvent to the online members of allianceID.  consumes textParams
    void SendAudioEvent(uint32 allianceID, uint8 eventID, PyDict* textParams);

    /* infrastructure hub upgrades */
    /* checks typeID's index, strategic and required/blocking upgrade requirements in systemID.
     *  installed:  typeID/online of the upgrades in the hub.  otherTypeID is set to the required or blocking upgrade
     *  blocking upgrades only count for installs
     */
    Sov::UpgradeBlock CheckUpgradeRequirements(uint32 systemID, uint16 typeID, const std::map<uint16, bool>& installed,
                                               bool forInstall, uint16& otherTypeID);
    // highest online level of an upgrade family, 0 if none
    uint8 GetActiveUpgradeLevel(uint32 systemID, uint8 family);
    bool HasActiveUpgrade(uint32 systemID, uint16 typeID);
    // an upgrade was added to hubID's inventory; the hub's SE refreshes the flags on its next tic
    void MarkUpgradesDirty(uint32 hubID)                { m_upgradeDirty.insert(hubID); }
    bool TakeUpgradesDirty(uint32 hubID)                { return (m_upgradeDirty.erase(hubID) > 0); }
    // sets hubID's upgrades online/offline from the current requirements and hub state
    void RefreshUpgrades(uint32 systemID, uint32 hubID);
    // every online hub (hourly, after decay)
    void RefreshAllUpgrades();
    // the hub is gone:  upgrades are destroyed with it
    void RemoveHub(uint32 systemID, uint32 hubID);
    // clear a system's beacon/jammer only if it is still this structure
    void ClearSystemBeaconID(uint32 systemID, uint32 beaconID);
    void ClearSystemJammerID(uint32 systemID, uint32 jammerID);
    // systemID/{online cyno generator count} for map.GetBeaconCount
    void GetBeaconCounts(std::map<uint32, uint32>& counts);

protected:
    void Populate();
    void LoadDevIndices();

private:
    static void ReadRow(DBResultRow& row, SovereigntyData& data);
    DevIndexData& GetDevIndex(uint32 systemID);
    // decay one index entry up to 'now'.  hourly also closes the gain period ('increasing' flags)
    void Decay(DevIndexData& data, int64 now, bool hourly);

    std::unordered_map<uint32, DevIndexData> m_devIndex;    // systemID/data
    std::map<uint32, std::set<uint16>> m_activeUpgrades;     // systemID/online upgrade typeIDs
    std::set<uint32> m_upgradeDirty;                        // hubIDs
    void LoadActiveUpgrades();

    //Multi-index container for sov data
    typedef boost::multi_index_container<
        SovereigntyData,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SovDataBySolarSystem>,
                boost::multi_index::member<SovereigntyData, uint32, &SovereigntyData::solarSystemID>
                >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SovDataByConstellation>,
                boost::multi_index::member<SovereigntyData, uint32, &SovereigntyData::constellationID>
                >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SovDataByRegion>,
                boost::multi_index::member<SovereigntyData, uint32, &SovereigntyData::regionID>
                >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SovDataByClaim>,
                boost::multi_index::member<SovereigntyData, uint32, &SovereigntyData::claimID>
                >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SovDataByAlliance>,
                boost::multi_index::member<SovereigntyData, uint32, &SovereigntyData::allianceID>
                >>> SovDataContainer;

    SovDataContainer m_sovData;
};

//Singleton
#define svDataMgr \
    (SovereigntyDataMgr::get())

#endif // _EVE_SERVER_SOVEREIGNTY_DATAMANAGER_H__
