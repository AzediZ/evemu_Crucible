
 /**
  * @name MatchTest.cpp
  *   crossing an incoming order with the book (market/MarketMatch.h):  price/time priority, partial fills, the
  *   remainder that rests, order ranges and minimum volumes
  *
  * @date:          13Oct26
  *
  */

#include <map>
#include <vector>

#include "market/MarketMatch.h"
#include "TestCheck.h"

using Market::Match::Resting;
using Market::Match::Fill;

namespace {
    // a line of stations:  station s is in system s / 10, systems are one jump apart.  ranges as the client sends them:
    //  -1 station, 0 system, n jumps, 32767 region
    struct LineCovers {
        bool operator()(int32_t range, uint32_t from, uint32_t to) const {
            if (from == to)
                return true;
            if (range == -1)
                return false;
            int32_t jumps = (int32_t)(from / 10) - (int32_t)(to / 10);
            if (jumps < 0)
                jumps = -jumps;
            return (range == 32767) or (jumps <= range);
        }
    };

    Resting R(uint32_t id, uint32_t station, double price, uint32_t qty, int32_t range = -1, uint32_t minVol = 1)
    {
        Resting r = Resting();
        r.orderID = id;
        r.stationID = station;
        r.price = price;
        r.quantity = qty;
        r.range = range;
        r.minVolume = minVol;
        return r;
    }
}

int MatchTest()
{
    LineCovers covers;

    // a limit buy at or above the best ask fills right away, cheapest first; the remainder rests
    {
        std::vector<Resting> asks = { R(1, 100, 10.00, 5), R(2, 100, 10.50, 5), R(3, 100, 11.00, 5) };
        std::vector<Fill> fills = Market::Match::Plan(false, asks, 12, 10.50, 100, -1, true, covers);
        T_CHECK_EQ(fills.size(), 2);
        T_CHECK_EQ(fills[0].index, 0);
        T_CHECK_EQ(fills[0].qty, 5);
        T_CHECK_EQ(fills[1].index, 1);
        T_CHECK_EQ(fills[1].qty, 5);
        T_CHECK_EQ(Market::Match::Remainder(12, fills), 2);
    }
    // below the best ask nothing happens and everything rests
    {
        std::vector<Resting> asks = { R(1, 100, 10.00, 5) };
        std::vector<Fill> fills = Market::Match::Plan(false, asks, 3, 9.99, 100, -1, true, covers);
        T_CHECK(fills.empty());
        T_CHECK_EQ(Market::Match::Remainder(3, fills), 3);
    }
    // partial fill of the resting order:  the buyer wants less than the ask holds
    {
        std::vector<Resting> asks = { R(1, 100, 10.00, 50) };
        std::vector<Fill> fills = Market::Match::Plan(false, asks, 7, 10.00, 100, -1, true, covers);
        T_CHECK_EQ(fills.size(), 1);
        T_CHECK_EQ(fills[0].qty, 7);
        T_CHECK_EQ(Market::Match::Remainder(7, fills), 0);
    }
    // a limit sell at or below the best bid sells into the bids, best price first
    {
        std::vector<Resting> bids = { R(1, 100, 12.00, 4, -1), R(2, 100, 11.00, 10, -1), R(3, 100, 9.00, 10, -1) };
        std::vector<Fill> fills = Market::Match::Plan(true, bids, 10, 10.00, 100, -1, true, covers);
        T_CHECK_EQ(fills.size(), 2);
        T_CHECK_EQ(fills[0].qty, 4);
        T_CHECK_EQ(fills[1].index, 1);
        T_CHECK_EQ(fills[1].qty, 6);
        T_CHECK_EQ(Market::Match::Remainder(10, fills), 0);
    }
    // the buyer's range around its station decides which asks it reaches
    {
        std::vector<Resting> asks = { R(1, 300, 9.00, 5), R(2, 120, 9.50, 5), R(3, 100, 10.00, 5) };
        // station range:  only station 100
        std::vector<Fill> st = Market::Match::Plan(false, asks, 15, 10.00, 100, -1, true, covers);
        T_CHECK_EQ(st.size(), 1);
        T_CHECK_EQ(st[0].index, 2);
        // 2 jumps:  station 120 (system 12, 2 jumps from 10) and 100
        std::vector<Fill> j2 = Market::Match::Plan(false, asks, 15, 10.00, 100, 2, true, covers);
        T_CHECK_EQ(j2.size(), 2);
        T_CHECK_EQ(j2[0].index, 1);
        // region:  all three, cheapest first
        std::vector<Fill> rg = Market::Match::Plan(false, asks, 15, 10.00, 100, 32767, true, covers);
        T_CHECK_EQ(rg.size(), 3);
        T_CHECK_EQ(rg[0].index, 0);
        // market.UseOrderRange off:  station only, whatever the range
        std::vector<Fill> off = Market::Match::Plan(false, asks, 15, 10.00, 100, 32767, false, covers);
        T_CHECK_EQ(off.size(), 1);
    }
    // a seller is reached by the bid's own range around the bid's station
    {
        std::vector<Resting> bids = { R(1, 150, 12.00, 5, 3), R(2, 160, 11.00, 5, -1), R(3, 100, 10.00, 5, 0) };
        std::vector<Fill> fills = Market::Match::Plan(true, bids, 15, 10.00, 130, -1, true, covers);
        // #1 (3 jumps from system 15 reaches 13), not #2 (station only), not #3 (system 10 only)
        T_CHECK_EQ(fills.size(), 1);
        T_CHECK_EQ(fills[0].index, 0);
    }
    // a bid's minimum volume:  a smaller sale passes it over, unless the bid has less than that left
    {
        std::vector<Resting> bids = { R(1, 100, 12.00, 100, -1, 20), R(2, 100, 11.00, 10, -1, 20), R(3, 100, 10.00, 50, -1, 1) };
        std::vector<Fill> fills = Market::Match::Plan(true, bids, 15, 10.00, 100, -1, true, covers);
        // 15 < 20 skips #1, #2 has only 10 left (< its minimum) so it takes them, #3 the other 5
        T_CHECK_EQ(fills.size(), 2);
        T_CHECK_EQ(fills[0].index, 1);
        T_CHECK_EQ(fills[0].qty, 10);
        T_CHECK_EQ(fills[1].index, 2);
        T_CHECK_EQ(fills[1].qty, 5);
    }
    // price/time:  equal prices keep the book's (oldest first) order
    {
        std::vector<Resting> asks = { R(7, 100, 10.00, 3), R(4, 100, 10.00, 3) };
        std::vector<Fill> fills = Market::Match::Plan(false, asks, 4, 10.00, 100, -1, true, covers);
        T_CHECK_EQ(fills.size(), 2);
        T_CHECK_EQ(asks[fills[0].index].orderID, 7);
        T_CHECK_EQ(fills[1].qty, 1);
    }
    return MktTest::failures;
}
