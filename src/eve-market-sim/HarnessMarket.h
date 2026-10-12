
 /**
  * @name HarnessMarket.h
  *   the offline harness' market:  an in-memory order book (the sim's orders as the gateway wrote them, and the
  *   scripted players' orders) and the players' wallets and goods.  it implements the gateway the server implements
  *   with mktOrders and MarketMgr, and the players trade with the sim through the same hooks MarketMgr calls
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_MARKET_SIM_HARNESS_MARKET_H__
#define _EVE_MARKET_SIM_HARNESS_MARKET_H__

#include <map>
#include <unordered_map>
#include <vector>

#include "market/sim/IMarketGateway.h"
#include "market/sim/SimWorld.h"

namespace MktHarness {

    using namespace MktSim;

    struct Player {
        uint32_t id         = 0;
        Cents    wallet     = 0;            // may go negative:  the harness measures net results, it doesn't refuse
        Cents    fromSim    = 0;            // isk the sim paid this player
        Cents    toSim      = 0;            // isk this player paid the sim
        Cents    taxes      = 0;
        std::map<uint16_t, Units> goods;
    };

    class HarnessMarket : public IMarketGateway {
    public:
        HarnessMarket(const StaticData& data, double salesTax);
        void SetWorld(World* world)                     { m_world = world; }
        void SetNow(int64_t now)                        { m_now = now; }

        // IMarketGateway
        bool ApplyOrders(const OrderBatch& batch, std::vector<uint32_t>& newIDs) override;
        void PlayerOrders(uint32_t regionID, uint16_t typeID, std::vector<PlayerOrder>& asks, std::vector<PlayerOrder>& bids) override;
        uint32_t TakePlayerAsk(const PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID) override;
        uint32_t TakePlayerBid(const PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID, uint32_t stationID) override;
        bool RangeCovers(int32_t range, uint32_t fromStationID, uint32_t toStationID) override;
        double InsuranceNet(uint16_t shipTypeID) override;

        Player& GetPlayer(uint32_t id);

        /* a player trading at a station.  immediate orders against the whole book (sim and players), price-time order,
         *  as MarketMgr does it.  return the units traded */
        Units Buy(uint32_t playerID, uint32_t stationID, uint16_t typeID, Units qty, Cents maxPrice);
        Units Sell(uint32_t playerID, uint32_t stationID, uint16_t typeID, Units qty, Cents minPrice);
        // a resting player order (crossing the book first, like PlaceCharOrder).  returns the orderID, 0 when it filled
        uint32_t Place(uint32_t playerID, uint32_t stationID, uint16_t typeID, bool bid, Cents price, Units qty);
        void CancelPlayerOrders(uint32_t playerID);

        // the sim's best ask / bid at a station (0 when none)
        Cents SimAsk(uint32_t stationID, uint16_t typeID) const;
        Cents SimBid(uint32_t stationID, uint16_t typeID) const;
        // the sim's order book must match the world's orders (prices and quantities):  mismatches found
        size_t CheckMirror(std::string& first) const;
        size_t SimOrderCount() const                    { return m_sim.size(); }
        uint64_t Writes() const                         { return m_writes; }

    private:
        struct BookOrder {
            uint32_t orderID;
            uint32_t ownerID;       // npc corp or player id
            bool     sim;
            uint16_t typeID;
            uint32_t stationID;
            uint32_t regionID;
            bool     bid;
            Cents    price;
            Units    qty;
            int64_t  issued;
            Cents    escrow;
        };
        uint32_t RegionOf(uint32_t stationID) const;
        void     Book(uint32_t regionID, uint16_t typeID, bool bid, std::vector<BookOrder*>& into);

        const StaticData& m_static;
        World*  m_world;
        double  m_tax;
        int64_t m_now;
        uint32_t m_nextID;
        uint64_t m_writes;
        std::map<uint32_t, BookOrder> m_sim;
        std::map<uint32_t, BookOrder> m_players;
        std::map<uint32_t, Player> m_people;
    };

}

#endif  // _EVE_MARKET_SIM_HARNESS_MARKET_H__
