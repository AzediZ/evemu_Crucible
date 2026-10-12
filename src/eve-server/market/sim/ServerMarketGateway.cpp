
 /**
  * @name ServerMarketGateway.cpp
  *   market simulation:  the gateway on the real market
  *
  * @date:          13Oct26
  *
  */

#include <cmath>

#include "EVEServerConfig.h"
#include "market/MarketMgr.h"
#include "market/sim/MarketSimDB.h"
#include "market/sim/ServerMarketGateway.h"

/*
 * MARKET__SIM
 * MARKET__SIM_TRACE
 */

ServerMarketGateway::ServerMarketGateway()
: m_multiRow(false),
  m_writes(0)
{
}

void ServerMarketGateway::Init()
{
    // a multi-row insert gets consecutive ids with 'consecutive' (1) or 'traditional' (0) auto-increment locking
    int mode(MarketSimDB::AutoIncLockMode());
    m_multiRow = (mode <= 1);
    if (!m_multiRow)
        _log(MARKET__SIM, "Market sim:  innodb_autoinc_lock_mode is %i.  Orders are posted one row at a time.", mode);
    m_playerCells.clear();
    MarketSimDB::PlayerOrderCells(m_playerCells);
}

bool ServerMarketGateway::ApplyOrders(const MktSim::OrderBatch& batch, std::vector<uint32_t>& newIDs)
{
    std::vector<uint32> ids;
    bool ok(MarketSimDB::ApplyOrders(batch, ids, m_multiRow));
    newIDs.assign(ids.begin(), ids.end());
    m_writes += batch.Size();
    // the client's caches of the order books that changed
    std::set<std::pair<uint32, uint16>> touched;
    for (auto& n : batch.posts)
        touched.insert(std::make_pair(n.regionID, n.typeID));
    for (auto& a : batch.alters)
        touched.insert(std::make_pair(a.regionID, a.typeID));
    for (auto& c : batch.cancels)
        touched.insert(std::make_pair(c.regionID, c.typeID));
    std::set<uint32> regions;
    for (auto& cur : touched) {
        sMktMgr.InvalidateOrdersCache(cur.first, cur.second);
        regions.insert(cur.first);
    }
    for (auto r : regions)
        sMktMgr.MarkAsksDirty(r);
    return ok;
}

void ServerMarketGateway::PlayerOrders(uint32_t regionID, uint16_t typeID, std::vector<MktSim::PlayerOrder>& asks, std::vector<MktSim::PlayerOrder>& bids)
{
    std::pair<uint32, uint16> key(regionID, typeID);
    if (m_playerCells.find(key) == m_playerCells.end())
        return;
    MarketSimDB::PlayerOrders(regionID, typeID, asks, bids);
    if (asks.empty() and bids.empty())
        m_playerCells.erase(key);
}

uint32_t ServerMarketGateway::TakePlayerAsk(const MktSim::PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID)
{
    // the order as it is now
    Market::OrderInfo oInfo = Market::OrderInfo();
    if (!MarketDB::GetOrderInfo(order.orderID, oInfo) or oInfo.isBuy or !MarketMgr::IsPlayerOrder(oInfo)
    or ((MktSim::Cents)std::llround(oInfo.price * 100.0) != order.price) or (oInfo.quantity == 0))
        return 0;
    uint32 fill(std::min<uint32>(qty, oInfo.quantity));
    try {
        if (sMktMgr.FillAgainstSellOrder(oInfo, Market::Party::ForSim(actorID, corpID), fill)) {
            _log(MARKET__SIM_TRACE, "Market sim bought %u of %u from player order #%u at %.2f.", fill, oInfo.typeID, oInfo.orderID, oInfo.price);
            return fill;
        }
    } catch (...) {
        _log(MARKET__SIM, "Market sim:  buying from order #%u failed.", oInfo.orderID);
    }
    return 0;
}

uint32_t ServerMarketGateway::TakePlayerBid(const MktSim::PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID, uint32_t stationID)
{
    Market::OrderInfo oInfo = Market::OrderInfo();
    if (!MarketDB::GetOrderInfo(order.orderID, oInfo) or !oInfo.isBuy or !MarketMgr::IsPlayerOrder(oInfo)
    or ((MktSim::Cents)std::llround(oInfo.price * 100.0) != order.price) or (oInfo.quantity == 0))
        return 0;
    if (sConfig.market.UseOrderRange ? !Market::RangeCovers(oInfo.orderRange, oInfo.stationID, stationID) : (oInfo.stationID != stationID))
        return 0;
    uint32 fill(std::min<uint32>(qty, oInfo.quantity));
    if ((fill < oInfo.minVolume) and (oInfo.quantity >= oInfo.minVolume))
        return 0;
    try {
        if (sMktMgr.FillAgainstBuyOrder(oInfo, Market::Party::ForSim(actorID, corpID), InventoryItemRef(nullptr), fill, stationID)) {
            _log(MARKET__SIM_TRACE, "Market sim sold %u of %u into player order #%u at %.2f.", fill, oInfo.typeID, oInfo.orderID, oInfo.price);
            return fill;
        }
    } catch (...) {
        _log(MARKET__SIM, "Market sim:  selling into order #%u failed.", oInfo.orderID);
    }
    return 0;
}

bool ServerMarketGateway::RangeCovers(int32_t range, uint32_t fromStationID, uint32_t toStationID)
{
    return Market::RangeCovers(range, fromStationID, toStationID);
}

double ServerMarketGateway::InsuranceNet(uint16_t shipTypeID)
{
    // platinum (InsuranceService):  pays 100% of the insurance value for a premium of 30%
    double value(sMktMgr.GetInsuranceValue(shipTypeID));
    return (value > 0.0) ? 0.7 * value : 0.0;
}
