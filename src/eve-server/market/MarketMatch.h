
 /**
  * @name MarketMatch.h
  *   which resting orders an incoming order takes, and how much of each.  header only and free of server types, so the
  *   matching rules are shared by MarketMgr and the offline market tests (src/eve-market-sim)
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_MATCH_H__
#define _EVE_SERVER_MARKET_MATCH_H__

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Market {
namespace Match {

    // a resting order as matching sees it.  books are sorted best price first, then oldest first
    struct Resting {
        uint32_t orderID;
        uint32_t stationID;
        int32_t  range;         // Market::Range or jumps (buy orders only)
        uint32_t quantity;      // volRemaining
        uint32_t minVolume;     // buy orders only
        double   price;
    };

    struct Fill {
        size_t   index;         // into the book
        uint32_t qty;
    };

    /* the quantity an incoming order takes from one resting order, 0 to pass it over.
     *  takerSells:  the taker sells into resting buy orders.  the buy order's range around its own station must reach
     *    the seller's station, and it is filled by at least its minimum volume (unless it has less than that left)
     *  else:        the taker buys from resting sell orders.  the taker's range around its station must reach the
     *    sell order's station
     *  limit:       the taker's price limit (a seller's minimum, a buyer's maximum)
     *  useRange:    false matches orders in the taker's station only (market.UseOrderRange)
     *  covers(range, fromStation, toStation):  does an order at fromStation with range reach toStation */
    template <typename Covers>
    uint32_t FillQty(bool takerSells, const Resting& o, uint32_t remaining, double limit,
                     uint32_t takerStationID, int32_t takerRange, bool useRange, Covers& covers)
    {
        if ((remaining == 0) or (o.quantity == 0))
            return 0;
        if (takerSells ? (o.price < limit) : (o.price > limit))
            return 0;
        if (useRange) {
            bool reach(takerSells ? covers(o.range, o.stationID, takerStationID)
                                  : covers(takerRange, takerStationID, o.stationID));
            if (!reach)
                return 0;
        } else if (o.stationID != takerStationID) {
            return 0;
        }
        uint32_t fill(remaining < o.quantity ? remaining : o.quantity);
        // a buy order's minimum volume, unless it has less than that left
        if (takerSells and (fill < o.minVolume) and (o.quantity >= o.minVolume))
            return 0;
        return fill;
    }

    // every fill an incoming order would get from 'book' (sorted best first), when each of them executes
    template <typename Covers>
    std::vector<Fill> Plan(bool takerSells, const std::vector<Resting>& book, uint32_t qty, double limit,
                           uint32_t takerStationID, int32_t takerRange, bool useRange, Covers& covers)
    {
        std::vector<Fill> fills;
        uint32_t remaining(qty);
        for (size_t i = 0; (i < book.size()) and (remaining > 0); ++i) {
            uint32_t fill(FillQty(takerSells, book[i], remaining, limit, takerStationID, takerRange, useRange, covers));
            if (fill == 0)
                continue;
            fills.push_back(Fill{i, fill});
            remaining -= fill;
        }
        return fills;
    }

    // what a limit order leaves to rest after crossing the book, 0 when it filled completely
    inline uint32_t Remainder(uint32_t qty, const std::vector<Fill>& fills)
    {
        uint32_t filled(0);
        for (auto& cur : fills)
            filled += cur.qty;
        return (filled >= qty) ? 0 : qty - filled;
    }

}
}

#endif  // _EVE_SERVER_MARKET_MATCH_H__
