
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
    // rows of locationID, allianceID, stationCount, militaryPoints, industrialPoints, claimedFor (days) for the
    //  systems of a constellation, the constellations of a region, the regions of the world (0) or a single system
    PyRep *GetCurrentSovData(uint32 locationID);
    PyRep *GetAllianceBeacons(uint32 allianceID);
    uint32 GetSystemAllianceID(uint32 systemID);
    void AddSovClaim(SovereigntyData data);
    void RemoveSovClaim(uint32 systemID);
    void MarkContested(uint32 systemID, bool contested);
    SovereigntyData GetSovereigntyData(uint32 systemID);
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

protected:
    void Populate();
    void LoadDevIndices();

private:
    static void ReadRow(DBResultRow& row, SovereigntyData& data);
    DevIndexData& GetDevIndex(uint32 systemID);
    // decay one index entry up to 'now'.  hourly also closes the gain period ('increasing' flags)
    void Decay(DevIndexData& data, int64 now, bool hourly);

    std::unordered_map<uint32, DevIndexData> m_devIndex;    // systemID/data

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
