
 /**
  * @name MarketSimDB.h
  *   market simulation:  the database side.  the calibration (mktCal*) and static data the sim reads, its state
  *   (mktSim*, PLAN 8) and its orders (mktOrders rows, batched)
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_DB_H__
#define _EVE_SERVER_MARKET_SIM_DB_H__

#include <map>
#include <set>
#include <string>
#include <vector>

#include "eve-server.h"

#include "market/sim/SimWorld.h"

class MarketSimDB {
public:
    // the mktSim* tables (and the calibration's mktCalType) exist.  why says what is missing
    static bool TablesReady(std::string& why);
    static int  AutoIncLockMode();

    /* static data */
    // mktCalType with the types' static data, mktCalRegion (for these regions), mktCalRegionDefault, mktCalGroup.
    //  returns the number of calibrated types
    static size_t LoadCalibration(MktSim::StaticData& data, const std::set<uint32>& regions);
    // bills of materials (invTypeMaterials and what a blueprint consumes) and refine outputs, per unit
    static void LoadMaterials(MktSim::StaticData& data);
    // npc vendor offers:  mode hubsOnly | corpHomeRegion | all
    static void LoadVendorOffers(MktSim::StaticData& data, const std::string& mode, const std::vector<uint32>& hubs);
    // per npc corp:  faction and the number of types it trades per category (crpNPCCorporationTrades)
    static void LoadCorpTrades(std::map<uint32, uint32>& corpFaction, std::map<uint32, std::map<uint8, uint32>>& tradesByCategory);
    // the regional nodes 'auto' picks:  one station per empire region with at least 1% of the isk traded
    static void AutoRegionalStations(const std::set<uint32>& skipRegions, std::vector<uint32>& stations);

    /* state */
    static void LoadState(std::map<std::string, std::string>& into);
    struct NodeRow { uint32 nodeID, stationID, systemID, regionID; uint8 kind; bool enabled; };
    static void LoadNodes(std::vector<NodeRow>& into);
    struct ActorRow { uint32 actorID; uint8 kind, sector; uint32 corpID, regionID, nodeID; int64 wallet, reserved; };
    static void LoadActors(std::vector<ActorRow>& into);
    struct StockRow { uint32 actorID, nodeID; uint16 typeID; int64 qty, reserved; };
    static void LoadInventory(std::vector<StockRow>& into);
    struct CellRow { uint32 nodeID; uint16 typeID; double lnV, lnAnchor, dBar, targetStock, lastClear; int64 lastTime, nextDue; uint32 flags; uint16 clampDays; };
    static void LoadCells(std::vector<CellRow>& into);
    struct OrderRow { uint32 orderID, actorID, nodeID; uint16 typeID; bool bid; uint8 level; bool live; uint32 stationID, regionID, qty; int64 priceCents, issued; };
    static void LoadOrders(std::vector<OrderRow>& into);
    static void LoadIntake(int32 fromDay, std::vector<std::tuple<int32, uint32, uint16, int64, int64>>& into);
    static void LoadHistoryDay(int32 day, std::vector<MktSim::HistoryRow>& into);

    // the write-behind snapshot (one transaction):  nodes, actors, changed stock and cells, history rows, state, audit
    static bool Flush(MktSim::World& world, const std::vector<uint32>& cells, const std::vector<MktSim::HistoryRow>& history,
                      const std::map<std::string, std::string>& state, const std::vector<MktSim::AuditLine>& audit);
    static void ClearState();

    /* at once (player boundary crossings, PLAN 8) */
    static void ActorDelta(uint32 actorID, int64 dWallet, int64 dReserved);
    static void StockDelta(uint32 actorID, uint32 nodeID, uint16 typeID, int64 dQty, int64 dReserved);
    static void IntakeAdd(int32 day, uint32 ownerID, uint16 typeID, int64 qty, int64 isk);
    static void ForgetOrder(uint32 orderID);

    /* orders */
    // posts, alters and cancels in one transaction.  ids gets the new orderIDs.  multiRow:  posts in multi-row inserts
    //  (consecutive auto-increment ids, innodb_autoinc_lock_mode 0 or 1);  else one insert per order
    static bool ApplyOrders(const MktSim::OrderBatch& batch, std::vector<uint32>& ids, bool multiRow);
    // the region/type pairs with open player orders, and those orders
    static void PlayerOrderCells(std::set<std::pair<uint32, uint16>>& into);
    static void PlayerOrders(uint32 regionID, uint16 typeID, std::vector<MktSim::PlayerOrder>& asks, std::vector<MktSim::PlayerOrder>& bids);
    // npc orders nothing backs (the old seed scripts, Trader Joe):  not players', not the sim's
    static uint32 CountLegacyOrders();
    static uint32 PurgeLegacyOrders(std::set<std::pair<uint32, uint16>>& touched);

    /* history */
    static bool HasHistoryBefore(uint32 regionID, uint16 typeID, int64 fileTime);
    static void InsertBackfill(const std::vector<MktSim::HistoryRow>& rows);

    // unix seconds and FILETIME
    static int64 ToFileTime(int64 unixSeconds)          { return unixSeconds * 10000000LL + 116444736000000000LL; }
    static int64 ToUnix(int64 fileTime)                 { return (fileTime - 116444736000000000LL) / 10000000LL; }
    static int64 DayToFileTime(int32 day)               { return ToFileTime((int64)day * 86400LL); }
};

#endif  // _EVE_SERVER_MARKET_SIM_DB_H__
