
 /**
  * @name IMarketGateway.h
  *   market simulation:  everything the economy core needs from a market.  the server's ServerMarketGateway writes real
  *   mktOrders rows and trades through MarketMgr;  the harness has an in-memory book with scripted players
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_GATEWAY_H__
#define _EVE_SERVER_MARKET_SIM_GATEWAY_H__

#include <vector>

#include "market/sim/SimTypes.h"

namespace MktSim {

    class IMarketGateway {
    public:
        virtual ~IMarketGateway()                       { }

        /* writes the batch (posts, alters, cancels) at once.  newIDs gets the orderID of every post, in order (0 for one
         *  that could not be written).  false when nothing could be written */
        virtual bool ApplyOrders(const OrderBatch& batch, std::vector<uint32_t>& newIDs) = 0;

        /* the open player (character and player corp) orders of a type in a region.  asks cheapest first, bids dearest
         *  first, older first at the same price */
        virtual void PlayerOrders(uint32_t regionID, uint16_t typeID, std::vector<PlayerOrder>& asks, std::vector<PlayerOrder>& bids) = 0;

        /* the sim takes a player's order at its price, as actorID trading as corpID.  returns the quantity traded (0 when
         *  the order changed or went away).  the player is paid / gets the items, taxed and journaled as usual.
         *  TakePlayerAsk:  the sim buys;  the items are not delivered (they go to the sim).
         *  TakePlayerBid:  the sim sells, from stationID (the bid's range must reach it);  the items are made for the buyer */
        virtual uint32_t TakePlayerAsk(const PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID) = 0;
        virtual uint32_t TakePlayerBid(const PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID, uint32_t stationID) = 0;

        // does an order at fromStation with range (Market::Range or jumps) reach toStation
        virtual bool RangeCovers(int32_t range, uint32_t fromStationID, uint32_t toStationID) = 0;

        // what a platinum insurance pays out net of its premium for a ship type, 0 when it can't be insured
        virtual double InsuranceNet(uint16_t shipTypeID) = 0;
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_GATEWAY_H__
