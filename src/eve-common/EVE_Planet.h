
/*  EVE_Planet.h
 *    enumerators and other defines for PI system
 *
 *  Base PI system code by Comet0
 * Updates and rewrites by Allan
 */


#ifndef EVE_PLANET_H
#define EVE_PLANET_H
#include <map>
#include <set>
#include <vector>

#include "eve-common.h"

namespace PI {
    namespace Command {
        enum  {
            Invalid                 = 0,
            CreatePin               = 1,
            RemovePin               = 2,
            CreateLink              = 3,
            RemoveLink              = 4,
            SetLinkLevel            = 5,
            CreateRoute             = 6,
            RemoveRoute             = 7,
            SetSchematic            = 8,
            UpgradeCommandCenter    = 9,
            AddExtractorHead        = 10,
            KillExtractorHead       = 11,
            MoveExtractorHead       = 12,
            InstallProgram          = 13,
            PrioritizeRoute         = 14
        };
    }

    namespace Pin {
        namespace State {
            enum {
                Edit        = -2,
                Disabled    = -1,
                Idle        =  0,
                Active      =  1
            };
        }

        enum {
            Level0  = 0,
            Level1  = 1,
            Level2  = 2,
            Level3  = 3,
            Level4  = 4,
            Level5  = 5,
            // the following are only used by Planetary_Links
            Level6  = 6,
            Level7  = 7,
            Level8  = 8,
            Level9  = 9,
            Level10 = 10
        };

        // which of the client's pin classes a pin type is  (planetCommon.GetPinEntityType)
        namespace Kind {
            enum {
                None            = 0,
                CommandCenter   = 1,    // CommandPin
                Spaceport       = 2,    // SpaceportPin
                Storage         = 3,    // StoragePin
                Process         = 4,    // ProcessPin
                ECU             = 5,    // EcuPin
                Base            = 6     // capsuleer/mercenary bases (DUST, not supported)
            };
        }
    }

    namespace Route {
        enum {
            PriorityLow  = -1,
            PriorityNorm =  0,
            PriorityHi   =  1
        };
    }

    //piLaunchOrbitDecayTime = DAY * 5
    namespace Cargo {
        enum {
            InOrbit     = 0,
            Deployed    = 1,
            Claimed     = 2,
            Deleted     = 3
        };
    }

}

namespace Launch {
    struct Data {
        uint8 status;
        uint32 launchID;
        uint32 itemID;
        uint32 solarSystemID;
        uint32 planetID;
        int64 launchTime;
        double x;
        double y;
        double z;
    };
}

/** @todo these need their own namespace */
/* POD structure entries for PI data */
struct PlanetResourceData {
    uint16 type_1;
    uint16 type_2;
    uint16 type_3;
    uint16 type_4;
    uint16 type_5;
    float dist_1;
    float dist_2;
    float dist_3;
    float dist_4;
    float dist_5;
    std::string buffer_1;
    std::string buffer_2;
    std::string buffer_3;
    std::string buffer_4;
    std::string buffer_5;
};

/* colony data.  this is the state the client's shared planet code (common/script/planet/*) works on.
 *  ColonySim (planet/ColonySim.h) runs the client's simulation on it.
 *  times are FILETIME.  a time of 0 stands for the client's None.
 */
struct PI_Link {
    uint8 level;
    uint16 typeID;
    uint32 linkID;                      // item for this link
    uint32 endpoint1;                   // endpoint1 < endpoint2, always
    uint32 endpoint2;
    std::vector<uint32> routesTransiting;
};

struct PI_Route {
    uint16 commodityTypeID;
    uint32 commodityQuantity;
    uint32 routeID;
    std::vector<uint32> path;           // source ... destination
};

struct PI_Heads {
    uint16 typeID;                      // extractor head type for the running program (attr harvesterType = programType)
    uint32 ecuPinID;
    double latitude;
    double longitude;
};

struct PI_Schematic {
    uint16 schematicID;
    uint32 cycleTime;                   // in seconds
    // typeID, qty
    std::map<uint16, uint32> inputs;
    std::map<uint16, uint32> outputs;
};

struct PI_Pin {
    PI_Pin()
    : kind(0), state(0), hasReceivedInputs(true), receivedInputsLastCycle(true), typeID(0), schematicID(0), programType(0),
      ownerID(0), qtyPerCycle(0), lastRunTime(0), lastLaunchTime(0), cycleTime(0), installTime(0), expiryTime(0),
      latitude(0), longitude(0), capacityUsed(0), headRadius(0)
    { }

    uint8 kind;                         // PI::Pin::Kind
    int8 state;                         // PI::Pin::State
    bool hasReceivedInputs;             // Process
    bool receivedInputsLastCycle;       // Process
    uint16 typeID;
    uint16 schematicID;                 // Process  (0 = none)
    uint16 programType;                 // ECU - extracted resource typeID  (0 = none)
    uint32 ownerID;
    uint32 qtyPerCycle;                 // ECU
    int64 lastRunTime;
    int64 lastLaunchTime;               // Command Center and Spaceports
    int64 cycleTime;                    // Process (from schematic) and ECU (program)
    int64 installTime;                  // ECU
    int64 expiryTime;                   // ECU
    double latitude;
    double longitude;
    double capacityUsed;                // m3 of contents
    double headRadius;                  // ECU  (0 = none)

    std::map<uint16, uint32> contents;  // typeID, qty
    std::map<uint16, uint32> demands;   // Process - schematic inputs
    std::map<uint16, uint32> products;  // Process - schematic outputs
    std::map<uint16, PI_Heads> heads;   // ECU - headID, head
};


class PI_CCPin {
public:
    PI_CCPin()                                          { Init(); }
    ~PI_CCPin()                                         { /* do nothing here */ }

    void Clear()
    {
        pins.clear();
        links.clear();
        routes.clear();
        megaRoutesBySource.clear();
        routesBySource.clear();
        routesByDestination.clear();
        linksByEndpoint.clear();
    }
    void Init()
    {
        Clear();
        level = 0;
        ccPinID = 0;
        ownerID = 0;
        planetID = 0;
        currentSimTime = 0;
    }

    uint8 GetLevel()                                    { return level; }
    uint32 GetPinID()                                   { return ccPinID; }

    uint8 level;
    uint32 ccPinID;
    uint32 ownerID;
    uint32 planetID;
    int64 currentSimTime;               // 0 = None

    // pinID, pinData
    std::map<uint32, PI_Pin> pins;
    // (endpoint1, endpoint2), linkData
    std::map<std::pair<uint32, uint32>, PI_Link> links;
    // routeID, routeData.  ascending routeID is the client's route order
    std::map<uint32, PI_Route> routes;

    /* derived indices, rebuilt by ColonySim::RebuildIndices() */
    // sourceID, ((destID, typeID), qty)
    std::map<uint32, std::map<std::pair<uint32, uint16>, uint32>> megaRoutesBySource;
    // sourceID, routeIDs
    std::map<uint32, std::set<uint32>> routesBySource;
    // destID, routeIDs in creation order
    std::map<uint32, std::vector<uint32>> routesByDestination;
    // pinID, linked pinIDs
    std::map<uint32, std::vector<uint32>> linksByEndpoint;
};

/*  these are internal client state events
enum PlanetEvents {
    EVENT_NORMAL = 0,
    EVENT_BUILDPIN = 1,
    EVENT_CREATELINKSTART = 2,
    EVENT_CREATELINKEND = 3,
    EVENT_CREATEROUTE = 4,
    EVENT_SURVEY = 5,
    SUBEVENT_NORMAL = 6,
    SUBEVENT_MOVEEXTRACTIONHEAD = 7
}; */

                    /*
            Extractors = 1026,
            Command_Centers = 1027,
            Processors = 1028,
            Storage_Facilities = 1029,
            Spaceports = 1030,
            Planetary_Resources = 1031,
            Planet_Solid = 1032,
            Planet_Liquid_Gas = 1033,
            Refined_Commodities = 1034,
            Planet_Organic = 1035,
            Planetary_Links = 1036,
            Specialized_Commodities = 1040,
            Advanced_Commodities = 1041,
            Basic_Commodities = 1042,
            */
/*
     COMMAND_CREATEPIN: 'CreatePin',
     COMMAND_REMOVEPIN: 'RemovePin',
     COMMAND_CREATELINK: 'CreateLink',
     COMMAND_REMOVELINK: 'RemoveLink',
     COMMAND_SETLINKLEVEL: 'SetLinkLevel',
     COMMAND_CREATEROUTE: 'CreateRoute',
     COMMAND_REMOVEROUTE: 'RemoveRoute',
     COMMAND_SETSCHEMATIC: 'SetSchematic',
     COMMAND_UPGRADECOMMANDCENTER: 'UpgradeCommandCenter',
     COMMAND_ADDEXTRACTORHEAD: 'AddExtractorHead',
     COMMAND_KILLEXTRACTORHEAD: 'KillExtractorHead',
     COMMAND_MOVEEXTRACTORHEAD: 'MoveExtractorHead',
     COMMAND_INSTALLPROGRAM: 'InstallProgram'}
*/
/*
   __identifiers__ = {COMMAND_CREATEPIN: ['pinID'],
     COMMAND_REMOVEPIN: ['pinID'],
     COMMAND_CREATELINK: ['endpoint1', 'endpoint2'],
     COMMAND_REMOVELINK: ['endpoint1', 'endpoint2'],
     COMMAND_SETLINKLEVEL: ['endpoint1', 'endpoint2', 'level'],
     COMMAND_CREATEROUTE: ['routeID'],
     COMMAND_REMOVEROUTE: ['routeID'],
     COMMAND_SETSCHEMATIC: ['pinID'],
     COMMAND_UPGRADECOMMANDCENTER: ['pinID', 'level'],
     COMMAND_ADDEXTRACTORHEAD: ['pinID', 'headID'],
     COMMAND_KILLEXTRACTORHEAD: ['pinID', 'headID'],
     COMMAND_MOVEEXTRACTORHEAD: ['pinID', 'headID'],
     COMMAND_INSTALLPROGRAM: ['pinID']}

   __arguments__ = {COMMAND_CREATEPIN: ['typeID', 'latitude', 'longitude'],
     COMMAND_REMOVEPIN: [],
     COMMAND_CREATELINK: ['level'],
     COMMAND_REMOVELINK: [],
     COMMAND_SETLINKLEVEL: [],
     COMMAND_CREATEROUTE: ['path', 'typeID', 'quantity'],
     COMMAND_REMOVEROUTE: [],
     COMMAND_SETSCHEMATIC: ['schematicID'],
     COMMAND_UPGRADECOMMANDCENTER: [],
     COMMAND_ADDEXTRACTORHEAD: ['latitude', 'longitude'],
     COMMAND_KILLEXTRACTORHEAD: [],
     COMMAND_MOVEEXTRACTORHEAD: ['latitude', 'longitude'],
     COMMAND_INSTALLPROGRAM: ['typeID', 'headRadius']}
*/
/*
            info.proximity = const.planetResourceProximityPlanet
            for i, scanRange in enumerate(const.planetResourceScanningRanges):
                if scanRange >= dist:
                    info.proximity = i
        minBand, maxBand = const.planetResourceProximityLimits[info.proximity]
        info.newBand = min(maxBand, minBand + info.planetology + info.advancedPlanetology * 2)
        requiredSkill = 5 - info.proximity

 * planetResourceScanDistance = 1000000000
 * planetResourceProximityDistant = 0
 * planetResourceProximityRegion = 1
 * planetResourceProximityConstellation = 2
 * planetResourceProximitySystem = 3
 * planetResourceProximityPlanet = 4
 * planetResourceProximityLimits = [(2, 6),
 * (4, 10),
 * (6, 15),
 * (10, 20),
 * (15, 30)]
 * planetResourceScanningRanges = [9.0,
 * 7.0,
 * 5.0,
 * 3.0,
 * 1.0]
 * planetResourceUpdateTime = 1 * HOUR
 * planetResourceMaxValue = 1.21
 */

#endif  // EVE_PLANET_H
