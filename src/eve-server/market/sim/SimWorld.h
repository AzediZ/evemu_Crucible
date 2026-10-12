
 /**
  * @name SimWorld.h
  *   market simulation:  the stock model of PLAN 5 and 11 (phase 2).  per market node and type a cell holds a fair value
  *   V, an anchor and the node's stock.  actors:
  *     market maker (node, sector)  holds the stock, quotes asks and bids around V as real orders
  *     producer (node, class)       the abstract supply:  sells into the market maker's bids up to its capacity, paying a
  *                                  production cost (a goods faucet with an isk sink).  loot comes without a cost
  *     consumer (node region)       final demand at the calibrated rate and elasticity;  lifts the cheapest asks, player or
  *                                  sim.  its income, metered by the money controller, is the only isk faucet
  *     vendor (npc corp)            fixed price orders of skills, blueprints and trade goods (an official goods faucet and
  *                                  isk sink, outside the closed loop)
  *   the sim also fills player orders (consumers lift player asks, market makers sell into player bids) and players fill
  *   the sim's orders (the hooks).  everything goes through the ledger, which the daily audit checks to the cent.
  *   work is scheduled per cell (hot cells every 15 minutes, cold ones every 2 hours, cells with player activity at once)
  *   and time sliced:  Step() stops when its budget is spent
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_WORLD_H__
#define _EVE_SERVER_MARKET_SIM_WORLD_H__

#include <functional>
#include <map>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "market/sim/IMarketGateway.h"
#include "market/sim/SimLedger.h"
#include "market/sim/SimStaticData.h"

namespace MktSim {

    static const int MaxLevels = 5;

    // the tunables (eve-server.xml <marketSim>, the harness scenario).  defaults are the plan's (PLAN 9.6) unless noted
    struct Params {
        uint64_t seed               = 20111129;     // Crucible's release date
        double   economyScale       = 0.01;         // fraction of TQ volumes and income
        int      tickMinutes        = 15;
        int      hotCellMinutes     = 15;
        int      coldCellMinutes    = 120;
        double   hotCellShare       = 0.05;         // top share of a node's cells by isk turnover that are hot
        int      dirtyCellSeconds   = 10;           // a cell with player activity is processed this soon
        int      quoteLevels        = 3;
        int      maxOrdersPerCellSide = 3;
        uint32_t simMaxOrders       = 200000;
        double   quoteDepthDays     = 1.0;
        double   requoteThreshold   = 0.01;
        double   requoteQtyThreshold = 0.2;
        double   minHalfSpread      = 0.015;
        double   kMax               = 2.0;
        double   fairValueHalfLifeDays = 3.0;
        double   marketNoise        = 1.0;          // persistent noise of V:  sigma = MarketNoise x volatility (plan 0.5;  1.0 meets AT (d), see the harness)
        double   transientNoise     = 1.0;          // the quotes' daily bounce:  min(TransientNoise x volatility, 0.75 h)
        double   lambdaExec         = 0.3;
        double   playerTradeWeight  = 0.1;
        double   buyBand            = 0.10;         // consumers lift player asks up to V x (1 + max(BuyBand, h))
        double   sellBand           = 0.10;
        double   intakeShareCounterparty = 0.25;
        double   maxIskToCounterpartyPerDay = 0.02;  // x M*
        double   intakeDays         = 1.0;
        bool     simPaysFees        = true;
        double   brokerFeeRate      = 0.01;         // the sim's broker fee (neutral standing, no skills)
        double   salesTaxRate       = 0.01;         // market.SalesTax / 100
        double   moneyDaysTarget    = 30.0;
        double   controllerKp       = 0.3;
        double   controllerKi       = 0.02;
        double   supplyFactor       = 1.0;          // producer capacity = supplyFactor x calibrated demand
        double   tradesPerUnit      = 2.0;          // a consumed unit trades twice in the stock model (to the market maker, to the consumer)
        double   restockDays        = 1.0;
        double   bidStockFactor     = 2.0;          // market makers bid until their stock is this x the target          // producers can also refill an empty target stock this fast
        double   producerCostShare  = 0.9;          // production cost = anchor x (1 - h) x this
        double   sigmaWtp           = 0.15;
        double   arbitrageMargin    = 0.02;         // epsilon of the no-arbitrage floors (PLAN 7.6)
        double   demandHalfLifeDays = 5.0;          // EWMA of realised demand
        double   upkeepPerDay       = 0.002;        // of an actor's money
        double   marketMakerCapitalDays = 3.0;      // working capital, in days of its cells' turnover
        uint32_t vendorStock        = 1000;         // npc vendor orders are restocked to this hourly
        bool     unitsPerTradingDay = false;        // mktCalType.unitsPerDay is the mean of trading days (x activeShare)
        int      maxCatchUpHours    = 48;
        uint32_t maxOrderOpsPerStep = 2000;
        bool     pauseOnAuditResidual = true;
    };

    struct Actor {
        uint32_t actorID        = 0;    // index + 1
        uint8_t  kind           = 0;
        uint8_t  sector         = 0;    // market maker:  Sector;  producer:  SimClass
        uint32_t ownerCorpID    = 0;
        uint32_t regionID       = 0;
        uint32_t nodeID         = 0;    // 0 for vendors
        double   target         = 0.0;  // cents.  consumer:  wallet target;  market maker:  working capital;  producer:  cash kept
        double   share          = 0.0;  // consumer:  share of the income
        uint8_t  state          = 0;
    };

    namespace CellFlag {
        enum : uint32_t {
            Dirty       = 0x01,     // player activity, process soon
            Backfilled  = 0x02,     // synthetic history written
            Clamped     = 0x04,     // V* at a Kmax bound at the last visit (stock-out or glut)
            Disabled    = 0x08,     // node or type left the sim:  no processing, no orders
            Hot         = 0x10,
            Pending     = 0x20      // has orders waiting for the next flush (not persisted)
        };
    }

    struct Cell {
        uint32_t index          = 0;
        uint32_t nodeID         = 0;
        uint16_t typeID         = 0;
        uint8_t  sector         = 0;
        uint8_t  simClass       = 0;
        uint32_t mm             = 0;    // actorIDs
        uint32_t producer       = 0;
        uint32_t consumer       = 0;
        // persisted state (mktSimCell)
        double   lnV            = 0.0;
        double   lnAnchor       = 0.0;
        double   dBar           = 0.0;  // EWMA of realised demand, units/day
        double   targetStock    = 0.0;
        double   lastClear      = 0.0;  // isk
        int64_t  lastTime       = 0;
        int64_t  nextDue        = 0;
        uint32_t flags          = 0;
        uint16_t clampDays      = 0;    // consecutive days ending at a Kmax bound
        // derived from the static data and the params (not persisted)
        double   refLocal       = 0.0;  // refPrice x regionRatio
        double   d0             = 0.0;  // calibrated traded volume at this node, units/day (before weekday)
        double   dc             = 0.0;  // the consumption behind it:  d0 / TradesPerUnit
        double   h              = 0.0;
        double   theta          = 0.0;
        double   eta            = 0.0;
        double   wc             = 0.0;
        double   coverDays      = 0.0;
        double   costRef        = 0.0;  // bill of materials at calibration prices
        double   sigma          = 0.0;  // OU noise, per sqrt(day)
        double   sigmaT         = 0.0;  // transient daily noise of the quotes
        uint32_t lot            = 1;
        double   quoteMid       = 0.0;  // the V the current quotes were made from
        double   vStar          = 0.0;  // the stock pressure target at the last visit
        uint8_t  levels         = 1;
        bool     shocked        = false;
        // GM overrides (persisted in mktSimState)
        double   shockMult      = 1.0;
        int64_t  shockUntil     = 0;
        double   anchorOverride = 0.0;
        // the cell's orders by level, 0 = none
        uint32_t ask[MaxLevels] = {};
        uint32_t bid[MaxLevels] = {};
        // today's internal trades (mktSimHistory) and intake
        int32_t  day            = 0;
        Cents    dLow           = 0;
        Cents    dHigh          = 0;
        Cents    dSum           = 0;
        Units    dVol           = 0;
        uint32_t dTrades        = 0;
        Units    dIntake        = 0;    // units bought from players today
        Units    dPlayerVol     = 0;    // units traded with players today
        std::map<uint32_t, Units> dByPlayer;
    };

    // a fixed price npc vendor order slot
    struct VendorSlot {
        VendorOffer offer;
        uint32_t actorID        = 0;
        uint32_t orderID        = 0;
        Cents    price          = 0;
    };

    // what the world reports once a day (the harness' acceptance tests, the server's log)
    struct DayReport {
        int32_t        day;
        Ledger::Result audit;
        Cents          money;
        double         moneyTarget;
        double         incomeFactor;
        Cents          incomeToday;
        uint32_t       liveOrders;
        uint32_t       clampedCells;
    };

    // writes the server does at once (player boundary crossings), so a crash never loses or doubles them
    class IPersist {
    public:
        virtual ~IPersist()                             { }
        virtual void PlayerFill(uint32_t actorID, Cents dWallet, Cents dReserved, uint32_t nodeID, uint16_t typeID, Units dQty, Units dReservedQty) = 0;
        virtual void IntakeAdd(int32_t day, uint32_t ownerID, uint16_t typeID, Units qty, Cents isk) = 0;
        // a sim order a player filled completely (its mktOrders row is gone already)
        virtual void OrderGone(uint32_t orderID) = 0;
        /* around a trade the sim takes with a player order:  the player's side (through the gateway) and the ledger's
         *  writes above are saved together */
        virtual void FillBegin()                        { }
        virtual void FillEnd()                          { }
    };

    class World {
    public:
        World(const StaticData& data, const Params& params, IMarketGateway& gateway);

        const Params& GetParams() const                 { return m_p; }
        void SetPersist(IPersist* persist)              { m_persist = persist; }
        void SetDayHook(std::function<void(const DayReport&)> fn)   { m_onDay = fn; }

        /* building.  Build() makes the nodes' actors and cells from the static data (fresh or before loading state),
         *  Seed() gives a fresh world (or one node) its starting money and stock (one-off faucets), Start() schedules the
         *  work.  a loaded world calls Build(), the Load* functions, Reconcile(), then Start() */
        void Build(int64_t now);
        // freshOnly:  only cells that were never visited and actors without money (a loaded world that grew)
        void Seed(int64_t now, uint32_t nodeID = 0, bool freshOnly = false);
        void Start(int64_t now, bool catchUp);

        // processes due work up to now.  budgetUs <= 0:  no limit.  usNow:  a microsecond clock for the budget
        void Step(int64_t now, double budgetUs = 0.0, std::function<double()> usNow = nullptr);
        // anything waiting for the gateway (Step does this itself)
        void FlushOrders();

        /* the hooks:  a player traded with a sim order (MarketMgr, after the money moved).  price is the order's */
        bool IsSimOrder(uint32_t orderID) const         { return m_orders.find(orderID) != m_orders.end(); }
        const SimOrder* FindOrder(uint32_t orderID) const;
        void OnAskFilled(uint32_t orderID, uint32_t qty, uint32_t buyerOwnerID, int64_t now);
        void OnBidFilled(uint32_t orderID, uint32_t qty, uint32_t sellerOwnerID, int64_t now);
        // how much of qty a player may sell into a sim bid now (the intake caps of PLAN 7.6)
        uint32_t AllowIntake(uint32_t orderID, uint32_t sellerOwnerID, uint32_t qty, int64_t now);
        // a player placed, changed or traded an order of the type in the region:  its cells are processed soon
        void MarkDirty(uint32_t regionID, uint16_t typeID, int64_t now);

        /* GM */
        void Pause(bool on)                             { m_paused = on; }
        bool Paused() const                             { return m_paused; }
        std::string Status(int64_t now) const;
        std::string CellInfo(uint16_t typeID, uint32_t stationID) const;
        std::string ActorInfo(uint32_t actorID) const;
        std::string ActorList(int kind) const;
        // anchor override for the type's cells (in a region, or all when 0).  price <= 0 removes it.  returns cells changed
        uint32_t SetAnchor(uint16_t typeID, double price, uint32_t regionID, int64_t now);
        // demand x mult for days, for a type, group (g:) or category (c:).  returns cells changed
        uint32_t Shock(char kind, uint32_t id, double mult, double days, int64_t now);
        uint32_t Requote(uint16_t typeID, int64_t now);         // 0 = all
        // cancels every sim order and frees what was reserved for them (the orders go with the next flush)
        uint32_t PurgeOrders();
        bool ExportCsv(const std::string& path) const;

        /* history the client sees */
        // synthetic daily rows for the days before firstDay (PLAN 7.5).  false when the region/type has no cell
        bool BackfillSeries(uint32_t regionID, uint16_t typeID, int days, int32_t firstDay, std::vector<HistoryRow>& into);
        void MarkBackfilled(uint32_t regionID, uint16_t typeID);
        // finished day rows not handed out yet, and the rows of today (all cells with trades)
        void TakeFinishedHistory(std::vector<HistoryRow>& into);
        void TodayHistory(std::vector<HistoryRow>& into) const;
        // region/type pairs whose 'today' history changed since the last call (the client's history cache)
        void TakeHistoryTouched(std::vector<std::pair<uint32_t, uint16_t>>& into);
        // cells whose state changed since the last call (the server's write-behind)
        void TakeChangedCells(std::vector<uint32_t>& into);
        void MarkAllCellsChanged();

        /* state for persistence */
        Ledger& GetLedger()                             { return m_ledger; }
        const Ledger& GetLedger() const                 { return m_ledger; }
        std::vector<Actor>& Actors()                    { return m_actors; }
        std::vector<Cell>& Cells()                      { return m_cells; }
        const std::vector<Cell>& Cells() const          { return m_cells; }
        std::unordered_map<uint32_t, SimOrder>& Orders() { return m_orders; }
        std::vector<VendorSlot>& VendorSlots()          { return m_vendorSlots; }
        Cell* FindCell(uint32_t nodeID, uint16_t typeID);
        const NodeData* Node(uint32_t nodeID) const;
        const std::vector<NodeData>& Nodes() const      { return m_static.nodes; }
        const StaticData& Static() const                { return m_static; }
        double ControllerIntegral() const               { return m_integral; }
        void SetControllerIntegral(double v)            { m_integral = v; }
        double IncomeFactor() const                     { return m_incomeFactor; }
        void SetIncomeFactor(double v)                  { m_incomeFactor = v; }
        int64_t LastIncomeTime() const                  { return m_lastIncome; }
        void SetLastIncomeTime(int64_t t)               { m_lastIncome = t; }
        double MoneyTarget() const                      { return m_moneyTarget; }
        Cents Income0() const                           { return m_income0; }
        std::vector<AuditLine>& AuditLines()            { return m_auditLines; }
        // intake already taken today (persisted per day/owner/type)
        void LoadIntake(int32_t day, uint32_t ownerID, uint16_t typeID, Units qty, Cents isk);
        // an order loaded from mktSimOrder + mktOrders
        void LoadOrder(const SimOrder& order);
        /* boot reconciliation (PLAN 8):  reservations are rebuilt from the live orders, orders without enough behind
         *  them are cut down.  returns a summary */
        std::string Reconcile(int64_t now);
        // the vendor orders the world wants that are not posted yet (posted with the next flush)
        void PostMissingVendorOrders(int64_t now);

        uint32_t LiveOrders() const                     { return (uint32_t)m_orders.size(); }
        uint64_t CellVisits() const                     { return m_cellVisits; }
        uint64_t TradesWithPlayers() const              { return m_playerTrades; }
        size_t   Backlog(int64_t now) const;

    private:
        struct Due {
            int64_t  t;
            uint8_t  kind;          // 0 day, 1 hour, 2 cell
            uint32_t index;
            bool operator>(const Due& o) const {
                if (t != o.t) return t > o.t;
                if (kind != o.kind) return kind > o.kind;
                return index > o.index;
            }
        };
        enum : uint8_t { DueDay = 0, DueHour = 1, DueCell = 2 };

        uint64_t Key(const Cell& c) const               { return ((uint64_t)c.nodeID << 16) | c.typeID; }
        uint64_t H(const Cell& c, int64_t t, uint64_t purpose, uint64_t extra = 0) const;
        double   Value(const Cell& c) const;
        double   Mid(const Cell& c) const;          // the V the current quotes were made from
        double   AnchorOf(const Cell& c) const;
        double   CostNow(const Cell& c) const;
        double   TargetStock(const Cell& c) const;
        double   RefineValue(const Cell& c) const;  // perfect refine outputs at the node's fair values
        int64_t  Period(const Cell& c) const;
        void     Schedule(Cell& c, int64_t t);
        void     RollDay(Cell& c, int64_t t);
        void     Changed(const Cell& c);
        void     Print(Cell& c, Cents price, Units qty);
        void     PlayerFeedback(Cell& c, uint32_t ownerID, Cents price, Units qty);

        void     ProcessCell(Cell& c, int64_t t);
        void     ConsumerDemand(Cell& c, int64_t t, double dt, double anchor);
        void     SellToPlayerBids(Cell& c, int64_t t);
        void     ProducerSupply(Cell& c, int64_t t, double dt, double anchor);
        void     RequoteCell(Cell& c, int64_t t);
        void     QuoteSide(Cell& c, bool bid, const Cents* prices, const Units* qtys, int64_t t);
        void     Daily(int64_t t);
        void     Hourly(int64_t t);

        // order bookkeeping.  all of them only touch memory and the batch
        void     PostOrder(Cell* c, uint32_t vendorSlot, uint32_t actorID, bool bid, uint8_t level, Cents price, uint32_t qty, int64_t t);
        bool     ChangeOrder(SimOrder& o, Cents price, uint32_t qty, int64_t t);
        void     CancelOrderNow(uint32_t orderID);
        void     QueueAlter(SimOrder& o);
        Cents    BrokerFee(Cents value) const;
        Cents    ProducerCost(const Cell& c, double anchor) const;

        Units    IntakeLeft(const Cell& c, uint32_t ownerID, Cents price, int64_t t) const;
        void     RecordIntake(Cell& c, uint32_t ownerID, Units qty, Cents value, int64_t t);
        Cents    BidOf(uint32_t nodeID, uint16_t typeID) const;   // the sim's best bid at a node (or 0)

        const StaticData&   m_static;
        Params              m_p;
        IMarketGateway&     m_gw;
        IPersist*           m_persist;
        Ledger              m_ledger;

        std::vector<Actor>  m_actors;
        std::vector<Cell>   m_cells;
        std::map<uint64_t, uint32_t> m_cellIndex;               // (nodeID << 16 | typeID) -> cell
        std::map<std::pair<uint32_t, uint16_t>, std::vector<uint32_t>> m_regionCells;  // (regionID, typeID) -> cells
        std::unordered_map<uint32_t, SimOrder> m_orders;
        std::vector<VendorSlot> m_vendorSlots;

        std::priority_queue<Due, std::vector<Due>, std::greater<Due>> m_due;
        OrderBatch          m_batch;
        std::vector<std::pair<uint32_t, uint32_t>> m_pendingVendor;  // post index -> vendor slot
        std::vector<uint32_t> m_rewrite;    // orders whose last alter was not written (the gateway failed):  written again

        // money controller (PLAN 6.8)
        Cents    m_income0;                 // consumer spend per day at reference prices
        double   m_moneyTarget;             // M*, cents
        double   m_integral;
        double   m_incomeFactor;
        int64_t  m_lastIncome;

        // intake caps:  (day, owner, type) units and (day, owner) isk
        std::map<std::tuple<int32_t, uint32_t, uint16_t>, Units> m_intakeUnits;
        std::map<std::pair<int32_t, uint32_t>, Cents> m_intakeIsk;

        std::vector<HistoryRow> m_historyDone;
        std::map<std::pair<uint32_t, uint16_t>, bool> m_historyTouched;
        std::vector<AuditLine> m_auditLines;
        std::vector<uint32_t> m_changedCells;
        std::vector<uint8_t>  m_cellChanged;
        std::function<void(const DayReport&)> m_onDay;

        bool     m_paused;
        bool     m_started;
        int32_t  m_auditDay;
        uint64_t m_cellVisits;
        uint64_t m_playerTrades;
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_WORLD_H__
