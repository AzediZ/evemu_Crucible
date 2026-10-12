
 /**
  * @name FactoryDB.h
  *   db query methods for R.A.M. activities
  *
  * @Author:         Allan
  * @date:          9Jan18
  */


#ifndef EVE_MANUF_FACTORYDB_H
#define EVE_MANUF_FACTORYDB_H

#include "ServiceDB.h"
#include "../eve-common/EVE_RAM.h"
#include "packets/Manufacturing.h"
#include "inventory/InventoryItem.h"

class Character;

class FactoryDB
: public ServiceDB
{
public:
    // client calls
    static PyRep* GetJobs2(const int32 ownerID, const bool completed);
    static PyRep* AssemblyLinesSelectPublic(const uint32 regionID);
    static PyRep* AssemblyLinesSelectPersonal(const uint32 charID);
    static PyRep* AssemblyLinesSelectPrivate(const uint32 charID);
    static PyRep* AssemblyLinesSelectCorporation(const uint32 corporationID);
    static PyRep* AssemblyLinesSelectAlliance(const int32 allianceID);
    static PyRep* AssemblyLinesGet(const uint32 containerID);
    // blueprints of ownerID at locationID (flag -1 = any) for the corp vote/lock windows
    static PyRep* GetBlueprintInformationAtLocation(uint32 locationID, uint32 ownerID, int32 flag = -1);
    static PyRep* GetMaterialCompositionOfItemType(const uint32 typeID);
    static void GetOutpostMaterialCompositionOfItemType(const uint32 typeID, DBQueryResult& res);

    // for static data mgr
    static bool IsRefinable(const uint16 typeID);
    static bool IsRecyclable(const uint16 typeID);
    static void GetSalvage(DBQueryResult& res);
    static void GetMinerals(DBQueryResult& res);
    static void GetCompounds(DBQueryResult& res);
    static void GetResources(DBQueryResult& res);
    static void GetComponents(DBQueryResult& res);
    static void GetCommodities(DBQueryResult& res);
    static void GetMiscCommodities(DBQueryResult& res);
    static void GetRAMMaterials(DBQueryResult& res);
    static void GetBlueprintType(DBQueryResult& res);
    static void GetRAMRequirements(DBQueryResult& res);
    static void GetMetaTypes(DBQueryResult& res);
    static void GetInstallationContents(DBQueryResult& res);
    static void GetAssemblyLineTypes(DBQueryResult& res);

    // InstallJob stuff
    static bool GetAssemblyLineProperties(const uint32 assemblyLineID, Character *pChar, Rsp_InstallJob &into, bool isCorpJob=false);
    static bool GetAssemblyLineRestrictions(const int32 assemblyLineID, EvERam::LineRestrictions& data);
    static uint32 InstallJob(const uint32 ownerID, const uint32 installerID, Call_InstallJob &args, const int64 beginTime, const int64 endTime,
                             const uint32 systemID, const uint32 outputLocationID, const uint32 containerTypeID, const uint16 installedTypeID,
                             const EvERam::InventionData* pInvData = nullptr);

    // CompleteJob stuff
    static bool GetJobProperties(const uint32 jobID, EvERam::JobProperties& data);
    static bool CompleteJob(const uint32 jobID, const int8 completedStatus);

    // misc queries
    static bool DeleteBlueprint(uint32 blueprintID);
    static bool GetBlueprintData(uint32 blueprintID, EvERam::bpData& into);
    static bool SaveBlueprintData(uint32 blueprintID, EvERam::bpData& data);
    // strict: the line type must list the product's category or group (structure lines)
    static bool IsProducableBy(const uint32 assemblyLineID, const ItemType *pType, bool strict = false);
    static bool GetMultipliers(const uint32 assemblyLineID, const ItemType *pType, Rsp_InstallJob &into);

    static uint32 CountManufacturingJobs(const uint32 installerID);
    static uint32 CountResearchJobs(const uint32 installerID);

    static int64 GetNextFreeTime(const uint32 assemblyLineID);

    // for calendar events
    static void SetJobEventID(const uint32 jobID, const uint32 eventID);

    // structure (POS) assembly lines
    // creates the lines of a structure (ramInstallationTypeContents) on first use, or sets the owner of existing ones
    static bool EnsureStructureAssemblyLines(uint32 structureID, uint16 typeID, uint32 ownerID);
    // offline structures pause their jobs (client shows 'Offline'), onlining resumes them with the lost time added
    static void PauseJobsInContainer(uint32 containerID);
    static void ResumeJobsInContainer(uint32 containerID);
    static void GetActiveJobsInContainer(uint32 containerID, std::vector<EvERam::JobProperties>& into);
    static bool UpdateAssemblyLine(uint32 assemblyLineID, const EvERam::LineConfig& data);

    /* the market sim's jobs on npc station lines (market/sim/SimFacilityMgr, market-sim PLAN 6.6) */
    struct SimLine {
        uint32 stationID;
        uint32 lineID;
        uint32 ownerID;
        int64  nextFreeTime;            // FILETIME
        double timeMultiplier;
        double costInstall;
        double costPerHour;             // at least the line type's minimum
    };
    /* the lines of the activity at the stations that an npc corp may use (the station owner's lines, no corp or alliance
     *  restriction, no standing above neutral needed, security limits around 0), by station and line */
    static bool SelectLinesForSim(const std::vector<uint32>& stationIDs, uint8 activityID, std::vector<SimLine>& into);
    // nextFreeTime = IF(nextFreeTime = guard, value, GREATEST(nextFreeTime, floor))  (no guard:  the GREATEST only)
    struct LineTimeWrite {
        uint32 lineID;
        bool   hasGuard;
        int64  guard;
        int64  value;
        int64  floor;
    };
    // batched:  one statement per 500 lines
    static bool SetNextFreeTimes(const std::vector<LineTimeWrite>& writes);
    // the stations of the station's constellation with npc lines (the station itself first, then its solar system's)
    static void StationsWithLinesNear(uint32 stationID, std::vector<uint32>& into);
    // lines' next free times, and whether a player job (not delivered) ends on them after nowTime.  lines gone are left out
    static bool GetLineStatesForSim(const std::vector<uint32>& lineIDs, int64 nowTime, std::map<uint32, std::pair<int64, bool>>& into);
};

#endif  // EVE_MANUF_FACTORYDB_H

