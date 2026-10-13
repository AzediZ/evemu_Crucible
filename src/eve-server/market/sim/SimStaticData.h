
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

#include <array>
#include <map>
#include <string>
#include <tuple>
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
        // manufacturing:  the blueprint's productionTime per run (one run makes portionSize units).  0 = unknown
        double   buildSeconds   = 0.0;
        // phase 4:  the cargo hold of a hauling hull (invTypes.capacity, m3).  0 = unknown
        double   capacity       = 0.0;
    };

    // a ship lost:  what was fitted to it (mktCalFit), per loss
    struct FitItem {
        uint16_t typeID;
        float    qtyPerLoss;
        float    destroyedShare;            // the rest drops (loot)
    };

    // mktCalLoss.secBand
    namespace SecBand {
        enum : uint8_t { High = 0, Low = 1, Null = 2, WSpace = 3, Count = 4 };
    }

    // invGroups the actors care about
    namespace Group {
        enum : uint16_t {
            MiningBarge     = 463,
            Exhumer         = 543,
            Ice             = 465
        };
    }

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

    // phase 4 (PLAN 6.1, 6.5):  the map.  a k-space system (mapSolarSystems)
    struct MapSystem {
        float    security       = 0.0f;
        uint32_t regionID       = 0;
    };
    // an npc station with a refinery, where actors can work (production sites;  staStations)
    struct MapStation {
        uint32_t stationID      = 0;
        uint32_t systemID       = 0;
        uint32_t regionID       = 0;
        float    refineEfficiency = 0.5f;
        float    refineTake     = 0.05f;
    };

    /* phase 6:  how the advanced chains make things (SimChains.cpp).  quantities are units (reactions:  invTypeReactions
     *  quantity x the type's moonMiningAmount, as TowerSE::ProcessUnits) */
    namespace RecipeKind {
        enum : uint8_t {
            Invention       = 1,    // a T1 blueprint copy and datacores (and a decryptor) -> runs of a T2 blueprint copy, by chance
            Reaction        = 2,    // a POS reaction cycle (invTypeReactions)
            Planetary       = 3     // a planetary factory cycle (piSchematics, piTypeMap)
        };
    }
    struct Recipe {
        uint8_t  kind           = 0;
        uint16_t id             = 0;    // the T1 blueprint (invention), the reaction type, the schematic
        std::vector<std::pair<uint16_t, double>> inputs;    // used up per run (data interfaces and skills aren't)
        std::vector<std::pair<uint16_t, double>> outputs;   // per run (invention:  the T2 blueprint, runs of it per success)
        double   seconds        = 0.0;  // per run (invention:  the T1 blueprint's researchTechTime)
        double   chance         = 1.0;  // invention:  the output blueprint's chanceOfRE (the base chance, before skills)
        uint16_t decryptorGroup = 0;    // invention:  the decryptors its data interface takes (0:  none)
        uint32_t maxRuns        = 0;    // invention:  the output blueprint's maxProductionLimit (caps a decryptor's runs)
    };
    /* a manufacturing job's materials (T2, components):  per unit made (a run makes portionSize), the base materials with
     *  the recycled T1 item's taken off and no waste (they take the blueprint's waste at the copy's material level), and
     *  the extra materials (the T1 item, components, R.A.M.:  quantity x damagePerJob) */
    struct ChainBill {
        std::vector<std::pair<uint16_t, double>> base;
        std::vector<std::pair<uint16_t, double>> extra;
        double   wasteFactor    = 0.1;  // invBlueprintTypes.wasteFactor / 100
    };
    struct Decryptor {
        uint16_t typeID         = 0;
        uint16_t groupID        = 0;
        double   chance         = 1.0;  // inventionPropabilityMultiplier
        double   runs           = 0.0;  // inventionMaxRunModifier
        double   me             = 0.0;  // inventionMEModifier
        double   pe             = 0.0;  // inventionPEModifier
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
        // the npc corp of a node's phase 3 actors (ActorKind, sector).  missing:  the market maker's of the matching sector
        std::map<std::tuple<uint32_t, uint8_t, uint8_t>, uint32_t> actorCorp;  // (nodeID, kind, sector)
        // killmails (PLAN 4.2):  public losses per day by security band, and the fits lost with them
        std::map<uint16_t, std::array<float, SecBand::Count>> losses;      // hullTypeID -> losses per day
        std::map<uint16_t, std::vector<FitItem>>            fits;           // hullTypeID -> fit
        // phase 4:  the jump graph (k-space), the stations sites can be at, and where each ore's belts are
        std::map<uint32_t, MapSystem>                       systems;
        std::vector<std::pair<uint32_t, uint32_t>>          jumps;          // (from, to) solar systems
        std::vector<MapStation>                             stations;
        std::map<uint16_t, uint8_t>                         oreBands;       // ore groupID -> bands with its belts (1 << SecBand)
        // phase 6:  the advanced chains (PLAN 6.4).  a product they can't make stays with the stock model
        std::vector<Recipe>                                 recipes;        // invention, reactions, planetary schematics
        std::map<uint16_t, ChainBill>                       bills;          // T2 and components -> their job's materials
        std::map<uint16_t, uint16_t>                        blueprints;     // T2 product -> its blueprint (a job uses a run of a copy)
        std::vector<Decryptor>                              decryptors;
        std::map<uint16_t, double>                          moonAmounts;    // moonMiningAmount:  units of a harvesting batch
        // tuning (the harness's parameter sweeps, phase 7):  World::Build multiplies every cell's half spread, stock
        //  elasticity and demand elasticity (the calibration's or the plan's default) by these.  1 on the server
        float halfSpreadScale       = 1.0f;
        float stockElasticityScale  = 1.0f;
        float elasticityScale       = 1.0f;

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
        uint32_t ActorCorp(uint32_t nodeID, uint8_t kind, uint8_t sector) const;

        // a hull the miners fly (their losses are the mining consortia's, not the consumers')
        static bool IsMiningHull(const TypeData& t)     { return (t.categoryID == Category::Ship) and ((t.groupID == Group::MiningBarge) or (t.groupID == Group::Exhumer)); }
        // ore and ice as they come out of a belt:  ore refines in batches (compressed ore, which is made, not mined,
        //  refines per unit)
        static bool IsMineable(const TypeData& t)       { return (t.simClass == SimClass::Mined) and !t.refine.empty() and (t.volume > 0.0)
                                                                   and ((t.groupID == Group::Ice) or (t.portionSize >= 100)); }
        // public losses per day over all bands
        float LossesPerDay(uint16_t hullTypeID) const;
        /* phase 4:  the bands (1 << SecBand) where a mineable type's belts are (roidDistribution).  ice, and everything
         *  when the map has no ore data, is anywhere;  an ore missing from roidDistribution (Mercoxit) is null-sec */
        uint8_t OreBands(const TypeData& t) const;

        // problems a loader should report (refPrice <= 0 and the like).  returns the number found
        size_t Validate(std::vector<std::string>& problems) const;
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_STATIC_DATA_H__
