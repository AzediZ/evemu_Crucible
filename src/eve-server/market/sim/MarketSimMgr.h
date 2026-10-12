
 /**
  * @name MarketSimMgr.h
  *   market simulation:  the server's singleton (sMktSim).  builds the economy core from the calibration and the static
  *   data, loads and saves its state (write-behind, player crossings at once), runs it in the 1 Hz loop within its
  *   budget, and is the hook MarketMgr calls when a player trades with a sim order (PLAN 9.1)
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_MGR_H__
#define _EVE_SERVER_MARKET_SIM_MGR_H__

#include <memory>

#include "eve-server.h"

#include "market/MarketMgr.h"
#include "market/sim/ServerMarketGateway.h"
#include "market/sim/SimWorld.h"

class Client;
class Seperator;

class MarketSimMgr
: public Singleton<MarketSimMgr>,
  public MktSim::IPersist
{
public:
    MarketSimMgr();
    ~MarketSimMgr();

    int Initialize();
    // 1 Hz.  the sim keeps to <marketSim> BudgetMsPerSecond
    void Process();
    // writes everything
    void Close();

    // the sim is loaded (its orders are backed);  running:  enabled and not paused
    bool IsLoaded() const                               { return m_world.get() != nullptr; }
    bool IsRunning() const;

    /* MarketMgr's hooks */
    bool IsSimOrder(uint32 orderID) const;
    /* the market's row of a sim order (oInfo, as MarketMgr read it) agrees with the sim's order:  same side and price, and
     *  at least qty left.  a trade is only made when it does, so the ledger pays and delivers exactly what the player got */
    bool CanFill(const Market::OrderInfo& oInfo, uint32 qty) const;
    // a player bought from / sold into a sim order (the money moved already).  qty at the order's price
    void OnSimAskFilled(const Market::OrderInfo& oInfo, uint32 qty, uint32 buyerOwnerID);
    void OnSimBidFilled(const Market::OrderInfo& oInfo, uint32 qty, uint32 sellerOwnerID);
    // how much of qty a player may sell into a sim bid (intake caps)
    uint32 AllowIntake(const Market::OrderInfo& oInfo, uint32 sellerOwnerID, uint32 qty);
    // a player placed or changed an order, or traded:  the type's cells in the region are processed soon
    void OnPlayerOrder(uint32 regionID, uint16 typeID);
    // the client asked for a type's history:  synthetic days before the sim started, once (PLAN 7.5)
    void EnsureBackfill(uint32 regionID, uint16 typeID);
    // the sim's history rows of finished days are written (before MarketMgr rolls the day up)
    void FlushHistory();

    // /mktsim
    std::string Command(Client* who, const Seperator& args);

    /* MktSim::IPersist */
    void PlayerFill(uint32_t actorID, MktSim::Cents dWallet, MktSim::Cents dReserved, uint32_t nodeID, uint16_t typeID,
                    MktSim::Units dQty, MktSim::Units dReservedQty) override;
    void IntakeAdd(int32_t day, uint32_t ownerID, uint16_t typeID, MktSim::Units qty, MktSim::Cents isk) override;
    void OrderGone(uint32_t orderID) override;

private:
    int64 Now() const;
    void  ReadParams();
    bool  LoadStatic(std::string& why);
    void  PickCorps();
    bool  LoadSaved(int64 now);
    std::map<std::string, std::string> StateMap() const;
    void  Flush();
    void  OnDay(const MktSim::DayReport& rep);
    void  RunUntilIdle(int64 now);

    MktSim::StaticData m_static;
    MktSim::Params m_params;
    std::unique_ptr<ServerMarketGateway> m_gw;
    std::unique_ptr<MktSim::World> m_world;
    std::vector<MktSim::AuditLine> m_pendingAudit;
    std::vector<MktSim::HistoryRow> m_unwrittenHistory;     // finished days' rows a failed flush didn't write

    bool  m_enabled;
    int64 m_offset;                 // /mktsim step (test servers):  seconds the sim runs ahead of the clock
    int64 m_lastFlush;
    int64 m_lastHistoryInvalidate;
    int32 m_startDay;               // the first day of the sim's own history (backfill ends there)
    bool  m_replaceInventory;       // actors or nodes were renumbered at boot:  the next flush rewrites the stock table
    std::vector<uint32> m_hubs;
};

//Singleton
#define sMktSim \
( MarketSimMgr::get() )

#endif  // _EVE_SERVER_MARKET_SIM_MGR_H__
