
 /**
  * @name SimTypes.h
  *   market simulation:  the plain data the economy core works on (doc/code_and_design_notes/market-sim/PLAN.md).
  *   nothing here (or anywhere in the core:  SimTypes, SimRng, SimClock, SimStaticData, SimEconomy, SimLedger,
  *   SimWorld) depends on the server, so the server and the offline harness (src/eve-market-sim) share it
  *
  *   money is int64 cents, quantities int64 units.  time is unix seconds (UTC)
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_TYPES_H__
#define _EVE_SERVER_MARKET_SIM_TYPES_H__

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace MktSim {

    typedef int64_t Cents;
    typedef int64_t Units;

    static const int64_t SecondsPerDay = 86400;

    // mktCalType.simClass (PLAN 4.4)
    namespace SimClass {
        enum : uint8_t {
            Excluded        = 0,
            Mined           = 1,
            Refined         = 2,
            MfgT1           = 3,
            MfgT2           = 4,
            MfgT3           = 5,
            Component       = 6,
            Reaction        = 7,
            PI              = 8,
            Loot            = 9,
            Faction         = 10,
            Salvage         = 11,
            NpcVendor       = 12,
            NotSimulated    = 13
        };
        // classes with cells (stock model).  vendor types get fixed price vendor orders instead
        inline bool HasCells(uint8_t c)                 { return (c >= Mined) and (c <= Salvage); }
        // classes that come from a rate bounded faucet (loot, LP, salvage) rather than production with a cost
        inline bool IsLoot(uint8_t c)                   { return (c == Loot) or (c == Faction) or (c == Salvage); }
    }

    namespace ActorKind {
        enum : uint8_t {
            MarketMaker     = 1,    // per node and sector:  holds the node's stock, owns its asks and bids
            Producer        = 2,    // per node and class:  the abstract supply of the stock model (a goods faucet with a cost)
            Consumer        = 3,    // per node region:  final demand.  its income is the only isk faucet
            Vendor          = 4,    // per npc corp:  fixed price orders of skills, blueprints and trade goods
            // phase 3 (PLAN 6.4):  the supply chain that replaces the abstract producers of ore, minerals, T1 and loot
            Miner           = 5,    // per node:  a mining consortium.  mines ore and ice with its fleet, refines or sells it
            Reprocessor     = 6,    // per node:  buys what refines for more than it costs (the refine floor), sells minerals
            Manufacturer    = 7,    // per node and sector (ships, goods):  T1 jobs from bought inputs, sells the output
            LootSource      = 8,    // per node:  loot, LP and salvage at their calibrated rates, plus dropped fits
            // phase 4 (PLAN 6.5):  logistics
            Hauler          = 9,    // per pair of nodes (sector = the other node):  arbitrage, buys where cheap, ships, sells
            RemoteMiner     = 10,   // per node and band (sector = SecBand):  mines the low/null-sec ores at a site, ships the minerals
            // phase 6 (PLAN 6.4, SimChains.cpp):  the advanced chains that replace the abstract producers of T2, moon and PI
            Inventor        = 11,    // per node:  invents T2 blueprint copies (datacores, decryptors) and builds T2 from them
            Harvester       = 12,   // per node:  moon harvesting arrays and gas harvesters (POS fuel)
            Reactor         = 13,   // per node:  moon reactions (POS fuel)
            Planetary       = 14,   // per node:  planetary extraction (P0) and factories (P1 to P4)
            Count           = 15
        };
        const char* Name(uint8_t kind);
    }

    // what a sim industry job does (mktSimJob.kind)
    namespace Activity {
        enum : uint8_t {
            Manufacturing   = 1,    // ramActivities 1
            // phase 6
            Invention       = 8,    // ramActivities 8:  an invention line
            Reaction        = 20,   // a POS reactor's cycles (no assembly line, the actor's own arrays)
            Planetary       = 21    // a planetary factory's cycles (no assembly line)
        };
    }

    namespace NodeKind {
        enum : uint8_t {
            Hub             = 1,
            Regional        = 2,
            Site            = 3
        };
    }

    // which market maker of a node quotes a type (and so whose name players see on the orders)
    namespace Sector {
        enum : uint8_t {
            Raw             = 0,    // ore, minerals, ice, moon, PI, components, salvage
            Ships           = 1,
            Goods           = 2,    // modules, charges, drones, deployables, subsystems, ...
            Loot            = 3,    // named, faction, LP items, implants
            Count           = 4
        };
    }

    // audited isk flows.  each is a faucet (+), a sink (-) or a player boundary crossing
    namespace Isk {
        enum : uint8_t {
            Income          = 0,    // faucet:  consumer income (money controller)
            Seed            = 1,    // faucet:  capital at the first start
            PlayerToSim     = 2,    // players paid the sim
            SimToPlayer     = 3,    // the sim paid players
            Production      = 4,    // sink:  the stock model's production cost
            BrokerFee       = 5,    // sink
            SalesTax        = 6,    // sink
            Upkeep          = 7,    // sink:  actor overhead
            VendorSales     = 8,    // sink:  what players pay npc vendors
            Reconcile       = 9,    // boot reconciliation (faucet when positive)
            LineCost        = 10,   // sink:  assembly line installation and rent (to the station's npc owner)
            Freight         = 11,   // sink:  haulers' hull upkeep per m3 and jump (phase 4)
            Count           = 12
        };
        int Sign(uint8_t flow);     // +1 faucet, -1 sink, 0 crossing (PlayerToSim +, SimToPlayer -)
        const char* Name(uint8_t flow);
    }

    // audited goods flows, per type
    namespace Goods {
        enum : uint8_t {
            Production      = 0,    // faucet:  stock model production and loot
            Seed            = 1,    // faucet:  initial stock
            Vendor          = 2,    // faucet:  npc vendor sales
            PlayerToSim     = 3,
            SimToPlayer     = 4,
            Consumption     = 5,    // sink:  final demand
            Reconcile       = 6,
            // phase 3
            Mined           = 7,    // faucet:  ore and ice the miners' fleets bring in
            Loot            = 8,    // faucet:  loot, LP and salvage at their calibrated rates
            Dropped         = 9,    // faucet:  the share of lost fits that drops (it was consumed with the ship)
            Manufactured    = 10,   // faucet:  the output of finished jobs
            Refined         = 11,   // faucet:  reprocessing output
            Materials       = 12,   // sink:  what jobs and reprocessing use up
            RefineTax       = 13,   // sink:  the units the station keeps when refining
            Destroyed       = 14,   // sink:  the actors' own ships lost
            Spoiled         = 15,   // sink:  loot nobody wanted for too long
            Transport       = 16,   // sink:  cargo lost on the way (phase 4 route risk)
            // phase 6
            Invented        = 17,   // faucet:  runs of T2 blueprint copies a successful invention gives (the blueprint type)
            Harvested       = 18,   // faucet:  moon materials and gas the harvesters bring in
            Extracted       = 19,   // faucet:  planetary resources (P0)
            Fuel            = 20,   // sink:  fuel blocks the POS towers burn
            Count           = 21
        };
        int Sign(uint8_t flow);
        const char* Name(uint8_t flow);
    }

    // a market node:  a station where the sim posts orders
    struct NodeData {
        uint32_t nodeID;            // index + 1, stable through mktSimNode
        uint32_t stationID;
        uint32_t systemID;
        uint32_t regionID;
        uint8_t  kind;
        uint32_t factionID;
        float    security;
        // the station's refinery (staStations):  base efficiency and the share the station keeps at neutral standing
        float    refineEfficiency = 0.5f;
        float    refineTake       = 0.05f;
    };

    // a sim industry job (mktSimJob).  the inputs were used up when it started;  the output appears when it ends
    struct SimJob {
        uint32_t jobID          = 0;
        uint32_t actorID        = 0;
        uint32_t nodeID         = 0;
        uint32_t stationID      = 0;    // where it runs (the node's station until phase 4 adds production sites)
        uint8_t  activity       = Activity::Manufacturing;
        uint16_t typeID         = 0;    // output
        uint32_t runs           = 0;
        Units    qtyOut         = 0;
        uint32_t lineID         = 0;    // the assembly line it holds (0:  virtual capacity, PLAN 6.6 mode false)
        int64_t  start          = 0;
        int64_t  end            = 0;
    };

    /* phase 4:  where the ledger keeps goods that are not at a market node (Ledger's nodeID).  a node's production sites
     *  (low/null-sec stations its remote miners work at) are SiteLoc(node, band);  goods on the way are at TransitLoc */
    static const uint16_t SiteLocBase = 0x4000;
    static const uint16_t TransitLoc = 0xFFFF;
    inline uint16_t SiteLoc(uint32_t nodeID, uint8_t band)  { return (uint16_t)(SiteLocBase + nodeID * 4 + (band & 3)); }
    inline bool     IsSiteLoc(uint32_t loc)             { return (loc >= SiteLocBase) and (loc < TransitLoc); }
    inline uint32_t SiteNode(uint32_t loc)              { return (loc - SiteLocBase) / 4; }
    inline uint8_t  SiteBand(uint32_t loc)              { return (uint8_t)((loc - SiteLocBase) & 3); }

    // a shipment (mktSimShipment, one row per cargo line):  a hull carrying goods from one location to another
    struct SimTrip {
        struct Line {
            uint16_t typeID     = 0;
            Units    qty        = 0;
            Cents    value      = 0;    // what it cost (or is worth) when it left
        };
        uint32_t tripID         = 0;
        uint32_t actorID        = 0;
        uint16_t from           = 0;    // ledger locations:  a node or a site
        uint16_t to             = 0;
        uint16_t hullTypeID     = 0;
        int64_t  depart         = 0;
        int64_t  arrive         = 0;
        double   lossRoll       = 0.0;  // the hash's draw at departure
        bool     lost           = false;// decided at departure (lossRoll < the route's loss chance):  lost when it would arrive
        std::vector<Line> lines;
    };

    // a fixed price npc vendor order:  corp sells typeID at stationID for basePrice
    struct VendorOffer {
        uint32_t corpID;
        uint16_t typeID;
        uint32_t stationID;
        uint32_t systemID;
        uint32_t regionID;
    };

    // where a sim order lives in the world
    struct SimOrder {
        uint32_t orderID;
        uint32_t actorID;
        uint32_t cell;              // cell index, or VendorCell for vendor orders
        uint32_t vendorSlot;        // vendor slot index (vendor orders)
        uint16_t typeID;
        uint32_t stationID;
        uint32_t regionID;
        bool     bid;
        uint8_t  level;
        Cents    price;             // per unit
        uint32_t qty;               // volRemaining
        int64_t  issued;
        // what the market (mktOrders) holds.  internal trades change price/qty first, the next ApplyOrders catches up
        Cents    dbPrice;
        uint32_t dbQty;
        int64_t  dbIssued;
    };
    static const uint32_t VendorCell = 0xFFFFFFFFu;

    // an order of a player (character or player corp) as the sim sees it
    struct PlayerOrder {
        uint32_t orderID;
        uint32_t ownerID;
        uint16_t typeID;
        uint32_t stationID;
        uint32_t systemID;
        uint32_t regionID;
        bool     bid;
        Cents    price;
        uint32_t qty;
        uint32_t minVolume;
        int32_t  range;
        int64_t  issued;
    };

    // order changes the world hands the gateway in one go (end of a Step)
    struct NewOrder {
        uint32_t actorID;
        uint32_t ownerID;           // the actor's npc corp
        uint32_t cell;              // or VendorCell
        uint32_t vendorSlot;
        uint8_t  level;
        uint16_t typeID;
        uint32_t nodeID;            // 0 for vendor orders
        uint32_t stationID;
        uint32_t systemID;
        uint32_t regionID;
        bool     bid;
        Cents    price;
        uint32_t qty;
        uint32_t minVolume;
        int32_t  range;
        int64_t  issued;
    };
    struct AlterOrder {
        uint32_t orderID;
        uint16_t typeID;
        uint32_t regionID;
        Cents    price;
        uint32_t qty;
        int64_t  issued;
    };
    struct CancelOrder {
        uint32_t orderID;
        uint16_t typeID;
        uint32_t regionID;
    };
    struct OrderBatch {
        std::vector<NewOrder>    posts;
        std::vector<AlterOrder>  alters;
        std::vector<CancelOrder> cancels;
        size_t Size() const                             { return posts.size() + alters.size() + cancels.size(); }
        bool Empty() const                              { return Size() == 0; }
        void Clear()                                    { posts.clear(); alters.clear(); cancels.clear(); }
    };

    // one region/type/day of the sim's internal trades (mktSimHistory)
    struct HistoryRow {
        uint32_t regionID;
        uint16_t typeID;
        int32_t  day;               // unix day
        Cents    low;
        Cents    high;
        Cents    sumValue;
        Units    volume;
        uint32_t trades;
    };

    // one line of a day's audit (mktSimAudit)
    struct AuditLine {
        int32_t     day;
        std::string category;
        int64_t     amount;
    };

    // day index and weekday (0 = Monday) of a unix time
    inline int32_t DayOf(int64_t t)                     { return (int32_t)((t >= 0) ? (t / SecondsPerDay) : ((t - SecondsPerDay + 1) / SecondsPerDay)); }
    inline int     WeekdayOf(int32_t day)               { return (int)(((day % 7) + 7 + 3) % 7); }   // 1970-01-01 was a Thursday

}

#endif  // _EVE_SERVER_MARKET_SIM_TYPES_H__
