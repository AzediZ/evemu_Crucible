
 /**
  * @name SimStaticData.h
  *   market simulation:  what the economy knows about the game.  types (with their calibration, mktCalType), the bill of
  *   materials and refine outputs, group defaults (mktCalGroup), regional ratios (mktCalRegion, mktCalRegionDefault),
  *   the market nodes, npc vendor offers and the npc corps the actors trade as.
  *   the server fills it from the database and its static data managers (MarketSimMgr), the harness from a json export
  *   or a synthetic world.  where the calibration leaves a value unset, the plan's category default is used
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_STATIC_DATA_H__
#define _EVE_SERVER_MARKET_SIM_STATIC_DATA_H__

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "market/sim/SimTypes.h"

namespace MktSim {

    // invCategories used by the defaults
    namespace Category {
        enum : uint8_t {
            Material        = 4,
            Ship            = 6,
            Module          = 7,
            Charge          = 8,
            Blueprint       = 9,
            Skill           = 16,
            Commodity       = 17,
            Drone           = 18,
            Implant         = 20,
            Deployable      = 22,
            Structure       = 23,
            Asteroid        = 25,
            Apparel         = 30,
            Subsystem       = 32,
            PIResource      = 42,
            PICommodity     = 43
        };
    }

    struct TypeData {
        uint16_t typeID         = 0;
        uint16_t groupID        = 0;
        uint8_t  categoryID     = 0;
        uint8_t  simClass       = SimClass::Excluded;
        uint8_t  priceSource    = 0;
        uint8_t  metaGroupID    = 0;
        uint32_t portionSize    = 1;
        uint32_t lotSize        = 1;        // typical order size (mktCalType.lotSize)
        double   volume         = 0.0;      // m3
        double   basePrice      = 0.0;
        // mktCalType
        double   refPrice       = 0.0;      // Forge median daily average (ISK)
        double   priceP10       = 0.0;
        double   priceP90       = 0.0;
        double   unitsPerDay    = 0.0;      // TQ, all regions
        double   buildCostRef   = 0.0;      // the generator's bill of materials cost (0 = not built)
        float    confidence     = 0.0f;
        float    volatility     = 0.0f;     // daily log-return sd
        float    activeShare    = 0.0f;     // share of days with trades (Forge)
        float    consumeShare   = 0.0f;
        float    elasticity     = 0.0f;
        // per unit made:  materials and the components a job consumes (already divided by portionSize)
        std::vector<std::pair<uint16_t, double>> bom;
        // per unit refined:  perfect yield outputs (invTypeMaterials / portionSize)
        std::vector<std::pair<uint16_t, double>> refine;
    };

    // mktCalGroup.  NaN = not set (the plan's default is used)
    struct GroupCal {
        float elasticity, stockElasticity, halfSpread, coverDays, consumeShare;
        float volA, volB, volResid, costMarkupT1, costMarkupT2;
        float wd[7];                        // Mon..Sun
        GroupCal();
    };

    struct RegionTypeCal {
        float priceRatio;                   // region average / Forge average, same day
        float unitsShare;                   // the region's share of the type's units
        float activeShare;
    };

    struct RegionDefault {
        float priceRatio;
        float unitsShare;
        float iskShare;
    };

    class StaticData {
    public:
        std::map<uint16_t, TypeData>                        types;
        std::map<uint16_t, GroupCal>                        groups;
        std::map<std::pair<uint32_t, uint16_t>, RegionTypeCal> region;     // (regionID, typeID)
        std::map<uint32_t, RegionDefault>                   regionDefault;
        std::vector<NodeData>                               nodes;
        std::vector<VendorOffer>                            vendors;
        // the npc corp of a node's market maker per sector, and of its producers per class.  0 or missing = the node
        //  station's owner (the loaders always fill them, this is only a safety net)
        std::map<std::pair<uint32_t, uint8_t>, uint32_t>    mmCorp;         // (nodeID, Sector)
        std::map<std::pair<uint32_t, uint8_t>, uint32_t>    producerCorp;   // (nodeID, SimClass)
        std::map<uint32_t, uint32_t>                        stationOwner;   // node stationID -> owner corp

        const TypeData* Type(uint16_t typeID) const;
        const GroupCal* Group(uint16_t groupID) const;

        // per type parameters:  the calibration's value where it has one, else the plan's default for the class/category
        float HalfSpread(const TypeData& t) const;          // h before fee floors (PLAN 6.2)
        float StockElasticity(const TypeData& t) const;     // theta
        float Elasticity(const TypeData& t) const;          // eta
        float CoverDays(const TypeData& t) const;           // S* = demand * coverDays
        float CostPassThrough(const TypeData& t) const;     // w_c
        float Weekday(const TypeData& t, int weekday) const;
        float RegionPrice(uint32_t regionID, uint16_t typeID) const;
        float RegionUnits(uint32_t regionID, uint16_t typeID) const;
        float RegionActive(uint32_t regionID, uint16_t typeID) const;
        bool  HasRegionRow(uint32_t regionID, uint16_t typeID) const;

        static uint8_t SectorOf(const TypeData& t);
        uint32_t MarketMakerCorp(uint32_t nodeID, uint8_t sector) const;
        uint32_t ProducerCorp(uint32_t nodeID, uint8_t simClass) const;

        // problems a loader should report (refPrice <= 0 and the like).  returns the number found
        size_t Validate(std::vector<std::string>& problems) const;
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_STATIC_DATA_H__
