
 /**
  * @name SimLedger.h
  *   market simulation:  the actors' money and goods.  every change goes through a named faucet, sink, transfer or
  *   player boundary crossing, and is counted, so the daily audit can prove
  *     M(t) = M(t-1) + faucets - sinks + (player -> sim) - (sim -> player)
  *   exactly, in cents, and the same per type in units (PLAN P1, 6.7).  the money and stock totals are summed from the
  *   accounts themselves, not kept alongside, so a change made around the ledger shows up as a residual
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_LEDGER_H__
#define _EVE_SERVER_MARKET_SIM_LEDGER_H__

#include <array>
#include <map>
#include <set>
#include <unordered_map>

#include "market/sim/SimTypes.h"

namespace MktSim {

    class Ledger {
    public:
        struct Account {
            Cents wallet    = 0;    // free
            Cents reserved  = 0;    // escrow behind the actor's bids
        };
        struct Stock {
            Units qty       = 0;    // free
            Units reserved  = 0;    // behind the actor's asks
        };
        typedef uint64_t StockKey;

        static StockKey Key(uint32_t actorID, uint16_t nodeID, uint16_t typeID)
                                                        { return ((uint64_t)actorID << 32) | ((uint64_t)nodeID << 16) | typeID; }
        static uint32_t KeyActor(StockKey k)            { return (uint32_t)(k >> 32); }
        static uint16_t KeyNode(StockKey k)             { return (uint16_t)((k >> 16) & 0xFFFF); }
        static uint16_t KeyType(StockKey k)             { return (uint16_t)(k & 0xFFFF); }

        Account& Acct(uint32_t actorID);
        const Account* FindAcct(uint32_t actorID) const;
        Stock& Inv(uint32_t actorID, uint16_t nodeID, uint16_t typeID);
        const Stock* FindInv(uint32_t actorID, uint16_t nodeID, uint16_t typeID) const;

        /* isk.  amounts are >= 0.  the strict calls change nothing and return false when the money isn't there */
        void  Faucet(uint32_t actorID, Cents c, uint8_t flow);
        Cents Sink(uint32_t actorID, Cents c, uint8_t flow, bool strict = true);   // returns what was taken
        bool  Transfer(uint32_t fromID, uint32_t toID, Cents c);                   // wallet to wallet
        bool  Reserve(uint32_t actorID, Cents c);                                  // wallet to reserved
        void  Unreserve(uint32_t actorID, Cents c);                                // reserved to wallet (at most what is reserved)
        bool  PayFromReserved(uint32_t fromID, uint32_t toID, Cents c);            // a filled bid pays another actor
        bool  SinkFromReserved(uint32_t actorID, Cents c, uint8_t flow);
        void  FromPlayer(uint32_t actorID, Cents c);                               // a player paid the actor
        bool  ToPlayer(uint32_t actorID, Cents c, bool fromReserved);              // the actor paid a player

        /* goods, per actor, node and type */
        void  GoodsIn(uint32_t actorID, uint16_t nodeID, uint16_t typeID, Units q, uint8_t flow);    // faucet or from a player
        bool  GoodsOut(uint32_t actorID, uint16_t nodeID, uint16_t typeID, Units q, uint8_t flow, bool fromReserved);  // sink or to a player
        bool  MoveGoods(uint32_t fromID, uint32_t toID, uint16_t nodeID, uint16_t typeID, Units q, bool fromReserved);
        // an actor's own goods from one location to another (shipments:  a node or site to TransitLoc and on;  no flow)
        bool  Relocate(uint32_t actorID, uint16_t fromNode, uint16_t toNode, uint16_t typeID, Units q);
        bool  ReserveGoods(uint32_t actorID, uint16_t nodeID, uint16_t typeID, Units q);
        void  UnreserveGoods(uint32_t actorID, uint16_t nodeID, uint16_t typeID, Units q);

        // totals summed from the accounts
        Cents Money() const;
        Cents MoneyOf(uint32_t actorID) const;
        Units GoodsOf(uint16_t typeID) const;
        void  GoodsByType(std::map<uint16_t, Units>& into) const;

        /* audit periods.  BeginPeriod snapshots the totals and clears the flow counters */
        struct Result {
            int32_t day;
            Cents   moneyStart;
            Cents   moneyEnd;
            Cents   iskResidual;            // moneyEnd - moneyStart - net flows.  0 = the books close
            size_t  goodsResidualTypes;     // types whose units don't add up
            Units   goodsResidualUnits;     // sum of |residual|
            std::array<Cents, Isk::Count> isk;
            std::array<Units, Goods::Count> goods;  // summed over types
        };
        void   BeginPeriod(int32_t day);
        Result Audit() const;
        const std::array<Cents, Isk::Count>& IskFlows() const   { return m_isk; }
        // negative balances found and clamped (should stay 0)
        uint64_t Violations() const                     { return m_violations; }

        // rows changed since the last ClearDirty (the server's write-behind)
        const std::set<uint32_t>& DirtyAccounts() const { return m_dirtyAccts; }
        const std::set<StockKey>& DirtyStocks() const   { return m_dirtyStocks; }
        void ClearDirty()                               { m_dirtyAccts.clear(); m_dirtyStocks.clear(); }
        void MarkAllDirty();

        const std::unordered_map<uint32_t, Account>& Accounts() const  { return m_accounts; }
        const std::unordered_map<StockKey, Stock>& Stocks() const      { return m_stocks; }
        // loading persisted state (no flows counted).  BeginPeriod afterwards
        void Load(uint32_t actorID, Cents wallet, Cents reserved);
        void LoadStock(StockKey key, Units qty, Units reserved);

    private:
        void CountIsk(uint8_t flow, Cents c)            { m_isk[flow] += c; }
        void CountGoods(uint16_t typeID, uint8_t flow, Units q);

        std::unordered_map<uint32_t, Account> m_accounts;
        std::unordered_map<StockKey, Stock> m_stocks;
        std::set<uint32_t> m_dirtyAccts;
        std::set<StockKey> m_dirtyStocks;

        int32_t m_day = 0;
        Cents   m_moneyStart = 0;
        std::map<uint16_t, Units> m_goodsStart;
        std::array<Cents, Isk::Count> m_isk = {};
        std::map<uint16_t, std::array<Units, Goods::Count>> m_goods;
        uint64_t m_violations = 0;
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_LEDGER_H__
