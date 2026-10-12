
 /**
  * @name ServerMarketGateway.h
  *   market simulation:  the gateway on the real market.  the sim's orders are mktOrders rows (batched, with their
  *   mktSimOrder rows, in one transaction), player orders are read from mktOrders, and the sim trades with players
  *   through MarketMgr's trade bodies, so fees, taxes, journals, transactions and notifications are the usual ones
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_SERVER_GATEWAY_H__
#define _EVE_SERVER_MARKET_SIM_SERVER_GATEWAY_H__

#include <set>

#include "market/sim/IMarketGateway.h"

class ServerMarketGateway : public MktSim::IMarketGateway {
public:
    ServerMarketGateway();

    void Init();
    // a player order of the type exists in the region now (PlaceCharOrder, ModifyCharOrder)
    void NotePlayerOrder(uint32_t regionID, uint16_t typeID)   { m_playerCells.insert(std::make_pair(regionID, typeID)); }

    bool ApplyOrders(const MktSim::OrderBatch& batch, std::vector<uint32_t>& newIDs) override;
    void PlayerOrders(uint32_t regionID, uint16_t typeID, std::vector<MktSim::PlayerOrder>& asks, std::vector<MktSim::PlayerOrder>& bids) override;
    uint32_t TakePlayerAsk(const MktSim::PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID) override;
    uint32_t TakePlayerBid(const MktSim::PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID, uint32_t stationID) override;
    bool RangeCovers(int32_t range, uint32_t fromStationID, uint32_t toStationID) override;
    double InsuranceNet(uint16_t shipTypeID) override;

    uint64_t Writes() const                               { return m_writes; }

private:
    bool m_multiRow;
    uint64_t m_writes;
    // region/type pairs that had player orders (a hint;  pairs found empty are dropped)
    std::set<std::pair<uint32_t, uint16_t>> m_playerCells;
};

#endif  // _EVE_SERVER_MARKET_SIM_SERVER_GATEWAY_H__
