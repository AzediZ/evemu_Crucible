/**
 * @name ColonySim.h
 *   the client's shared planetary interaction code (eve/common/script/planet/*) ported to the server.
 *   the Crucible server ran the same code as the client; the client re-runs it from the state we send.
 *   this works on a PI_CCPin only: no db, no client, no python objects.
 *
 *   method names are the names of their python counterparts:
 *     pin classes (BasePin, StoragePin, SpaceportPin, CommandPin, ProcessPin, EcuPin) are static functions switching on PI_Pin::kind,
 *     ColonyData (indices, prime/remove) and BaseColony (simulation, routing, validation) are members.
 *
 * @Author:         Allan (original PI code), F19 port
 * @date:   October 2026
 */


#ifndef EVEMU_PLANET_COLONYSIM_H_
#define EVEMU_PLANET_COLONYSIM_H_

#include <functional>

#include "eve-server.h"
#include "../../eve-common/EVE_Planet.h"


namespace PI {
    namespace Sim {
        // planetCommon / ecuPin constants
        const uint8 LinkMaxUpgrade                  = 10;
        const uint8 MaxWaypoints                    = 5;
        const uint8 EcuMaxHeads                     = 10;
        const double RadiusDrillAreaMax             = 0.05;
        const double RadiusDrillAreaMin             = 0.01;
        const double RadiusDrillAreaDiff            = RadiusDrillAreaMax - RadiusDrillAreaMin;
        const int64 ProgramInstallationCooldown     = EvE::Time::Minute;
        const int64 LaunchCycleTime                 = EvE::Time::Minute;
        const uint16 LinkTypeID                     = 2280;
        // planetCommon.commandCenterInfoPerLevel
        const uint8 MaxCommandUpgradeLevel          = 5;
        int32 GetCPUOutput(uint8 level);
        int32 GetPowerOutput(uint8 level);
        // planetCommon.GetUpgradeCost
        int64 GetUpgradeCost(uint8 currentLevel, uint8 desiredLevel);
        // planetCommon.GetProgramLengthFromHeadRadius / GetCycleTimeFromProgramLength
        double GetProgramLengthFromHeadRadius(double headRadius);
        double GetCycleTimeFromProgramLength(double programLength);
        // planetCommon.GetBandwidth
        double GetBandwidth(double commodityVolume, int64 cycleTime);
        // planetCommon.GetExpeditedTransferTime
        int64 GetExpeditedTransferTime(double linkBandwidth, const std::map<uint16, uint32>& commodities);
        // SurfacePoint(theta=longitude, phi=latitude).GetAngleBetween()
        double GetAngleBetween(double lat1, double long1, double lat2, double long2);
    }
}

class ColonySim
{
public:
    // value of a resource at a surface point (builder.GetValueAt(sh, theta, phi)).  azimuth = 2pi - longitude, polar = latitude
    typedef std::function<double(double, double)> ResourceField;
    // one head of an extraction program:  headID, latitude, longitude
    struct Head {
        uint16 id;
        double latitude;
        double longitude;
    };

    ColonySim(PI_CCPin& colony, double planetRadius, uint16 planetTypeID);

    PI_CCPin& Data()                                    { return m_colony; }
    PI_Pin* GetPin(uint32 pinID);
    PI_Link* GetLink(uint32 endpoint1, uint32 endpoint2);
    PI_Route* GetRoute(uint32 routeID);
    bool HasCommandPin()                                { return (m_colony.ccPinID != 0) and (GetPin(m_colony.ccPinID) != nullptr); }

    /* pin classes */
    static bool IsStorage(const PI_Pin& pin);
    static bool IsSpaceport(const PI_Pin& pin);
    static bool IsCommandCenter(const PI_Pin& pin);
    static bool IsConsumer(const PI_Pin& pin);
    static bool IsProcessor(const PI_Pin& pin);
    static bool IsProducer(const PI_Pin& pin);
    static bool IsExtractor(const PI_Pin& pin);
    static bool IsActive(const PI_Pin& pin);
    static bool CanActivate(const PI_Pin& pin);
    static bool HasEnoughInputs(const PI_Pin& pin);
    // -1 is None
    static int64 GetCycleTime(const PI_Pin& pin);
    // false is None
    static bool GetNextRunTime(const PI_Pin& pin, int64& nextRunTime);
    static bool CanRun(const PI_Pin& pin, int64 runTime);
    static bool HasCapacity(const PI_Pin& pin);
    static double GetCapacity(const PI_Pin& pin);
    static double GetPinFreeSpace(const PI_Pin& pin);
    static double GetInputBufferState(const PI_Pin& pin);
    static int64 CanAccept(const PI_Pin& pin, uint16 typeID, int64 quantity);
    static uint32 AddCommodity(PI_Pin& pin, uint16 typeID, int64 quantity);
    static uint32 RemoveCommodity(PI_Pin& pin, uint16 typeID, int64 quantity);
    static void SetContents(PI_Pin& pin, const std::map<uint16, uint32>& contents);
    static void Run(PI_Pin& pin, int64 runTime, std::map<uint16, uint32>& products);
    static void GetProducts(const PI_Pin& pin, std::map<uint16, uint32>& products);
    static void GetProductMaxOutput(const PI_Pin& pin, std::map<uint16, double>& products);
    // EcuPin
    static uint32 GetProgramOutput(const PI_Pin& pin, int64 currentTime, int64 baseValue, int64 cycleTime, int64 startTime);
    static double GetMaxOutput(const PI_Pin& pin, int64 baseOutput, int64 cycleTime);
    static bool CanInstallProgram(const PI_Pin& pin, int64 runTime);     // throws CantInstallProgramNeedsCooldown
    static void EcuInstallProgram(PI_Pin& pin, uint16 programType, int64 cycleTime, int64 expiryTime, uint32 qtyPerCycle, double headRadius, int64 runTime);
    static void ClearProgram(PI_Pin& pin);
    // ProcessPin
    static void SetSchematic(PI_Pin& pin, const PI_Schematic& schematic);
    static void SetLatitude(PI_Pin& pin, double latitude);
    static void SetLongitude(PI_Pin& pin, double longitude);

    /* ColonyData */
    void RebuildIndices();
    PI_Pin* PrimePin(uint32 pinID, uint16 typeID, uint32 ownerID, double latitude, double longitude);
    void RemovePin(uint32 pinID);
    PI_Link* PrimeLink(uint16 linkTypeID, uint32 endpoint1, uint32 endpoint2, uint8 level=0, uint32 linkID=0);
    void RemoveLink(uint32 endpoint1, uint32 endpoint2);
    void PrimeRoute(uint32 routeID, const std::vector<uint32>& path, uint16 typeID, uint32 quantity);
    void RemoveRoute(uint32 routeID);
    void SetLevel(uint8 level)                          { m_colony.level = level; }
    void SetLinkLevel(uint32 endpoint1, uint32 endpoint2, uint8 newLevel);
    void AddExtractorHead(uint32 pinID, uint16 headID, double latitude, double longitude);
    void RemoveExtractorHead(uint32 pinID, uint16 headID);
    void MoveExtractorHead(uint32 pinID, uint16 headID, double latitude, double longitude);
    // ColonyData.InstallProgram (the program is made from the field).  typeID 0 clears the program
    void InstallProgram(uint32 pinID, uint16 typeID, double headRadius, const ResourceField& field, int64 runTime);
    void GetObsoleteRoutesForPin(uint32 pinID, std::vector<uint32>& routeIDs);
    // the colony's cpu and power, summed from pins and links
    void GetResourceUsage(double& cpuUsage, double& cpuSupply, double& powerUsage, double& powerSupply);
    double GetLinkDistance(const PI_Link& link);
    double GetLinkTotalBandwidth(const PI_Link& link);
    double GetLinkBandwidthUsage(const PI_Link& link);
    double GetRouteBandwidthUsage(const PI_Route& route);
    int64 GetLinkCpuUsage(uint16 typeID, double length, uint8 level);
    int64 GetLinkPowerUsage(uint16 typeID, double length, uint8 level);
    double GetPinCpuUsage(const PI_Pin& pin, int32 numHeads = -1);
    double GetPinPowerUsage(const PI_Pin& pin, int32 numHeads = -1);
    double GetPinCpuOutput(const PI_Pin& pin);
    double GetPinPowerOutput(const PI_Pin& pin);

    /* BaseColony - simulation */
    void RecalculateCurrentSimTime(int64 now);
    void PrimeSimulation(int64 simEndTime);
    int64 RunSimulation(int64 runSimUntil);
    void StimulateIdlePin(uint32 pinID);
    void RouteCommodityOutput(uint32 sourcePinID, std::map<uint16, uint32>& commodities);
    void RouteCommodityInput(uint32 destinationPinID);
    std::pair<uint16, uint32> ExecuteRoute(uint32 routeID, std::map<uint16, uint32>& commodities);
    std::pair<uint16, uint32> TransferCommodities(uint32 sourcePinID, uint32 destPinID, uint16 typeID, uint32 qty, std::map<uint16, uint32>& commodities, bool hasMaxAmount = false, double maxAmount = 0);
    void ExecuteExpeditedTransfer(uint32 sourceID, uint32 destinationID, const std::map<uint16, uint32>& commodities, double minBandwidth, int64 runTime);
    void InstallSchematicForPin(uint32 pinID, uint16 schematicID);
    void OnRouteCreated(uint32 routeID);

    /* BaseColony - validation.  these throw the client's UserErrors */
    void ValidateCreatePin(uint32 charID, uint16 typeID, double latitude, double longitude);
    void ValidateRemovePin(uint32 charID, uint32 pinID);
    void ValidateCreateLink(uint32 charID, uint32 endpoint1, uint32 endpoint2, uint16 linkTypeID, uint8 level);
    void ValidateRemoveLink(uint32 charID, uint32 endpoint1, uint32 endpoint2);
    void ValidateCreateRoute(uint32 charID, const std::vector<uint32>& path, uint16 typeID, uint32 quantity);
    void ValidateRemoveRoute(uint32 charID, uint32 routeID);
    void ValidateInstallSchematic(uint32 charID, uint32 pinID, uint16 schematicID);
    void ValidateSetLinkLevel(uint32 charID, uint32 endpoint1, uint32 endpoint2, uint8 newLevel);
    void ValidateCommandCenterUpgrade(uint8 level);
    double ValidateExpeditedTransfer(uint32 charID, const std::vector<uint32>& path, const std::map<uint16, uint32>& commodities, int64 runTime);
    void ValidateAddExtractorHead(uint32 pinID, uint16 headID, double latitude, double longitude);
    void ValidateRemoveExtractorHead(uint32 pinID, uint16 headID);
    void ValidateMoveExtractorHead(uint32 pinID, uint16 headID, double latitude, double longitude);
    void ValidateInstallProgram(uint32 pinID, uint16 typeID, double headRadius);

    /* BaseColony.CreateProgram.  ecuTypeID is the ecu's type (its attributes), ecuPinID is excluded from the other ecus' overlap */
    void CreateProgram(const ResourceField& field, uint32 ecuPinID, uint16 ecuTypeID, uint16 resourceTypeID, const std::vector<Head>& heads,
                       double headRadius, uint32& maxValue, int64& cycleTime, uint32& numCycles, std::map<uint16, double>* overlapModifiers = nullptr);

    // changes every pin and route ID (provisional to real) in the colony.  ids not in the maps stay
    void RemapIDs(const std::map<uint32, uint32>& pinIDs, const std::map<uint32, uint32>& routeIDs);

private:
    void AddTimer(uint32 pinID, int64 runTime);
    void SchedulePin(uint32 pinID);
    void EvaluatePin(uint32 pinID);
    void OnPinRemoved(uint32 pinID);

    PI_CCPin& m_colony;
    double m_planetRadius;
    uint16 m_planetTypeID;

    // the simulation queue:  (runTime, pinID), one entry per pin
    std::set<std::pair<int64, uint32>> m_simQueue;
    std::map<uint32, int64> m_scheduled;
};

#endif  // EVEMU_PLANET_COLONYSIM_H_
