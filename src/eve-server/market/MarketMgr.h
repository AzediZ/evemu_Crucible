
 /**
  * @name MarketMgr.h
  *   singleton object for storing, manipulating and managing in-game market data
  *
  * @Author:         Allan
  * @date:          19Dec17
  *
  */


#ifndef _EVE_SERVER_MARKET_MANAGER_H__
#define _EVE_SERVER_MARKET_MANAGER_H__


#include "../eve-server.h"

#include "EntityList.h"
#include "market/MarketDB.h"
#include "cache/ObjCacheService.h"

class Client;

namespace Market {
    // marketsvc.GetStationDistance():  -1 = the client's station, 0 = its system, else stargate jumps from the client's system.
    //  Range::Region for another region (or no route)
    int32 GetStationDistance(Client* pClient, uint32 stationID);
    // marketsvc.jumpsPerSkillLevel {0:-1, 1:0, 2:5, 3:10, 4:20, 5:50} (Marketing, Procurement, Daytrading, Visibility)
    int32 SkillRangeLimit(uint8 level);
    bool InSkillRange(int32 distance, uint8 level);
    // does an order at orderStationID with orderRange (Range or jumps) reach targetStationID
    bool RangeCovers(int32 orderRange, uint32 orderStationID, uint32 targetStationID);

    /* one side of a trade.  the money is ownerID's:  a character, a player corp's wallet division accountKey, or the
     *  npc corp a market sim actor trades as.  client is the acting character when it is online */
    struct Party {
        uint32 ownerID;
        uint32 charID;          // acting character (the member for corp trades).  the npc corp itself for the sim
        bool isCorp;            // ownerID is a player corp wallet
        uint16 accountKey;
        Client* client;
        uint32 simActorID;      // the market sim actor, 0 for players

        bool IsSim() const                              { return simActorID != 0; }
        // the client's character, or its corp's division accountKey when useCorp
        static Party ForClient(Client* pClient, bool useCorp, uint16 accountKey);
        static Party ForSim(uint32 actorID, uint32 corpID);
    };
}

class MarketMgr
: public Singleton< MarketMgr >
{
public:
    MarketMgr();
    ~MarketMgr();

    int Initialize(EVEServiceManager& svc);

    void Close();
    void GetInfo();
    // ~5m.  expires player orders and rolls up the price history once a day
    void Process();

    void SystemStartup(SolarSystemData &data);
    void SystemShutdown(SolarSystemData &data);

    // rolls every finished day not yet in mktHistory up from the transactions
    void UpdatePriceHistory();

    /* immediate orders.  fill the best resting orders first (several orders when needed) and return the quantity filled.
     *  sells:  iRef holds at least qty and stays with the seller for whatever isn't sold.  the item's station is stationID
     *  buys:   items are delivered in the sell order's station
     *  money comes from / goes to the corp wallet division accountKey when useCorp */
    uint32 SellToBuyOrders(Client* seller, InventoryItemRef iRef, uint32 qty, double minPrice, uint32 stationID, bool useCorp, uint16 accountKey);
    uint32 BuyFromSellOrders(Client* buyer, uint16 typeID, uint32 qty, double maxPrice, uint32 stationID, int32 orderRange, bool useCorp, uint16 accountKey);

    // fill qty of a buy order with iRef (exactly qty, in the seller's station 'stationID').  false when nothing happened
    bool ExecuteBuyOrder(Client* seller, const Market::OrderInfo& oInfo, InventoryItemRef iRef, uint32 qty, uint32 stationID, bool useCorp, uint16 accountKey);
    // buy qty from a sell order.  throws (NotEnoughMoney) before anything changed when the buyer can't pay
    bool ExecuteSellOrder(Client* buyer, const Market::OrderInfo& oInfo, uint32 qty, bool useCorp, uint16 accountKey);

    /* the trade bodies behind ExecuteBuyOrder / ExecuteSellOrder.  fees, taxes, journals, transactions and the order
     *  owner's notifications happen here and only here, for players and for the market sim alike.
     *  FillAgainstBuyOrder:  seller sells qty into a resting buy order in stationID.  a player hands over iRef (exactly
     *    qty), the sim hands over nothing (iRef null) and the items are made from its stock
     *  FillAgainstSellOrder: buyer takes qty from a resting sell order.  items are delivered to players; the sim gets none
     *    (they go to its stock).  throws NotEnoughMoney before anything changed when a player can't pay
     *  npc orders are only filled when something backs them (the market sim's ledger):  an npc wallet holds no isk and
     *  an npc sell order has no items.  false when nothing happened */
    bool FillAgainstBuyOrder(const Market::OrderInfo& oInfo, const Market::Party& seller, InventoryItemRef iRef, uint32 qty, uint32 stationID);
    bool FillAgainstSellOrder(const Market::OrderInfo& oInfo, const Market::Party& buyer, uint32 qty);
    // the order is owned by an npc and backed by the market sim
    bool IsBackedNpcOrder(const Market::OrderInfo& oInfo);
    // the order's owner is a character or a player corp
    static bool IsPlayerOrder(const Market::OrderInfo& oInfo);

    // removes an order and settles it:  a buy order's remaining escrow goes back to its wallet, a sell order's remaining
    //  items to its owner's hangar (corp: the office's deliveries hangar).  the owner is told with 'Expiry'.  false if the order is gone
    bool CancelOrderInternal(const Market::OrderInfo& oInfo, const char* reason);
    void ExpireOrder(uint32 orderID);

    //forces a refresh of market data.
    void SendOnOwnOrderChanged(Client* pClient, uint32 orderID, uint8 action, bool isCorp = false, PyRep* order = nullptr);
    // tells the order's owner (character, or the corp's traders and accountants).  'order' is consumed
    void NotifyOrderOwner(const Market::OrderInfo& oInfo, uint8 action, PyRep* order = nullptr);

    void InvalidateOrdersCache(uint32 regionID, uint32 typeID);

    // player order modification delay (market.OrderModificationDelay, Crucible const.mktModificationDelay)
    // issued = order's issue date.  the last-modified stamps are kept in memory only
    bool CanModifyOrder(uint32 orderID, int64 issued);
    void SetOrderModified(uint32 orderID)               { m_orderModified[orderID] = GetFileTimeNow(); }
    void ClearOrderModified(uint32 orderID)             { m_orderModified.erase(orderID); }

    // where market items for an owner end up in a station:  characters -> item hangar,
    //  corps -> the office's deliveries hangar, or the member's own hangar when the corp has no office there
    static void GetDeliveryLocation(uint32 ownerID, bool isCorp, uint32 memberID, uint32 stationID, uint32& itemOwnerID, uint32& locationID, EVEItemFlags& flag);
    // wallet balance of a character (online or not) or a corp division
    static double GetWalletBalance(uint32 ownerID, uint16 accountKey);
    // throws NotEnoughMoney / NotEnoughMoneyCorp when the wallet holds less than amount
    static void CheckFunds(uint32 walletOwnerID, uint16 accountKey, double amount);

    bool NeedsUpdate()                                  { return m_historyDay + EvE::Time::Day <= GetMidnight(); }

    PyRep* GetMarketGroups()                            { PyIncRef(m_marketGroups); return m_marketGroups; }
    // cached until a trade of the type happens in the region or the day ends
    PyRep* GetNewPriceHistory(uint32 regionID, uint32 typeID);
    // cached for the day
    PyRep* GetOldPriceHistory(uint32 regionID, uint32 typeID);


    // base price update method
    void SetBasePrice();         // this uses current mineral values to estimate base price of item
    void UpdateMineralPrice();
    void GetCruPrices();

    /* insurance value (Tyrannis dynamic insurance):  the ship's build materials at their market index price, times the
     *  config multiplier of its tech level.  0 for rookie ships, capsules and non-ships */
    double GetInsuranceValue(uint16 shipTypeID);
    // trimmed mean sell price of a ship build material over market.InsuranceIndexDays, or its base price
    double GetMaterialIndexPrice(uint16 typeID);
    // rebuilds the material index from the transactions (daily, with the price history)
    void UpdateMaterialIndex();

    static int64 GetMidnight()                          { int64 now(GetFileTimeNow()); return now - (now % EvE::Time::Day); }

protected:
    void Populate();
    void ProcessOrderExpiry();
    void InvalidateHistoryCache(uint32 regionID, uint32 typeID);
    // records both sides of a trade.  recordBuyer false leaves out the buy side row (the sell side row is the one the
    //  price history uses, so it is always written)
    void RecordTrade(uint16 typeID, uint32 qty, double price, uint32 stationID,
                     uint32 sellerOwnerID, bool sellerIsCorp, uint32 sellerCharID, uint16 sellerKey,
                     uint32 buyerOwnerID, bool buyerIsCorp, uint32 buyerCharID, uint16 buyerKey, bool recordBuyer = true);

private:
    MarketDB m_db;
    ObjCacheService* m_cache;

    PyRep* m_marketGroups;  // static market group data

    int64 m_historyDay;     // first day (midnight) not rolled up into mktHistory yet

    std::map<uint32, int64> m_orderModified;    // orderID/time of last modification
    std::map<std::string, int64> m_historyCacheDay; // history cache name/day it was built for
    std::set<uint16> m_indexTypes;                  // ship build materials
    std::map<uint16, double> m_materialIndex;       // typeID/market index price
    std::map<uint16, double> m_insuranceValue;      // ship typeID/insurance value (memo, cleared with the index)
    bool m_indexReady;

    // markets are regional.  there are 66 regions.
    // market orders are stored as {regionID/typeID}
    //  load market data by region, sorted by system/station.
    //  station data will also store StationRef to owning/containing station for easier access later
    //  will be able to implement 'jumps' and other market conditionals
    //  ** this could take ~16m for average market data per region
};

//Singleton
#define sMktMgr \
( MarketMgr::get() )


#endif  // _EVE_SERVER_MARKET_MANAGER_H__

/*
 *    def GetStationDistance(self, stationID, getFastestRoute = True):
 *        if session.stationid == stationID:
 *            return -1
 *        station = sm.GetService('ui').GetStation(stationID)
 *        solarSystemID = station.solarSystemID
 *        regionID = sm.GetService('map').GetRegionForSolarSystem(solarSystemID)
 *        if regionID != session.regionid:
 *            return const.rangeRegion
 *        if getFastestRoute:
 *            jumps = sm.StartService('pathfinder').GetShortestJumpCountFromCurrent(solarSystemID)
 *        else:
 *            jumps = sm.StartService('pathfinder').GetJumpCountFromCurrent(solarSystemID)
 *        if jumps >= 1:
 *            return jumps
 *        return 0
 */

/*
 *    def GetSkillLimits(self):
 *        limits = {}
 *        currentOpen = 0
 *        myskills = sm.GetService('skills').MySkillLevelsByID()
 *        retailLevel = myskills.get(const.typeRetail, 0)
 *        tradeLevel = myskills.get(const.typeTrade, 0)
 *        wholeSaleLevel = myskills.get(const.typeWholesale, 0)
 *        accountingLevel = myskills.get(const.typeAccounting, 0)
 *        brokerLevel = myskills.get(const.typeBrokerRelations, 0)
 *        tycoonLevel = myskills.get(const.typeTycoon, 0)
 *        marginTradingLevel = myskills.get(const.typeMarginTrading, 0)
 *        marketingLevel = myskills.get(const.typeMarketing, 0)
 *        procurementLevel = myskills.get(const.typeProcurement, 0)
 *        visibilityLevel = myskills.get(const.typeVisibility, 0)
 *        daytradingLevel = myskills.get(const.typeDaytrading, 0)
 *        maxOrderCount = 5 + tradeLevel * 4 + retailLevel * 8 + wholeSaleLevel * 16 + tycoonLevel * 32
 *        limits['cnt'] = maxOrderCount
 *        commissionPercentage = const.marketCommissionPercentage / 100.0
 *        commissionPercentage *= 1 - brokerLevel * 0.05
 *        transactionTax = const.mktTransactionTax / 100.0
 *        transactionTax *= 1 - accountingLevel * 0.1
 *        limits['fee'] = commissionPercentage
 *        limits['acc'] = transactionTax
 *        limits['ask'] = jumpsPerSkillLevel[marketingLevel]
 *        limits['bid'] = jumpsPerSkillLevel[procurementLevel]
 *        limits['vis'] = jumpsPerSkillLevel[visibilityLevel]
 *        limits['mod'] = jumpsPerSkillLevel[daytradingLevel]
 *        limits['esc'] = 0.75 ** marginTradingLevel
 *        return limits
 */
