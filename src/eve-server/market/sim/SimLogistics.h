
 /**
  * @name SimLogistics.h
  *   market simulation:  routes and route risk (PLAN 6.5, phase 4).  the k-space jump graph (mapSolarSystemJumps) with
  *   each system's security (mapSolarSystems.security), Dijkstra from a system for three risk profiles (shortest,
  *   balanced, safest), the loss probability of a cargo along a route and the packaged volume a ship travels as.
  *     edge weight     entering system s costs 1 + lambda x RouteRiskScale x r(s),  r(s) = -ln(1 - p_jump(s, 1e9 ISK)),
  *                     lambda 0 / 1 / 5 (PLAN 6.5;  the plan's r alone is a few thousandths of a jump, so the risk is
  *                     counted in units of RouteRiskScale (1000):  a 0.1% chance of loss weighs as much as a jump at
  *                     lambda 1)
  *     p_jump          high-sec (>= 0.45) RiskHighSec x (value / 1e9)^RiskHighSecBeta (suicide ganks follow the cargo's
  *                     value), low-sec RiskLowSec, null-sec RiskNullSec
  *     a cargo         takes the profile's route with the lowest expected cost:  freight x m3 x jumps + P(loss) x value
  *   routes are computed on demand and cached per (from, to, profile).  pure, no server dependencies
  *
  * @date:          15Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_LOGISTICS_H__
#define _EVE_SERVER_MARKET_SIM_LOGISTICS_H__

#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "market/sim/SimStaticData.h"

namespace MktSim {

    // per jump loss probabilities (PLAN 6.5, 9.6 RiskHighSec, RiskHighSecBeta, RiskLowSec, RiskNullSec)
    struct RiskParams {
        double highSec      = 2e-5;     // at a cargo of 1e9 ISK
        double highSecBeta  = 0.7;
        double lowSec       = 2e-3;
        double nullSec      = 5e-3;
    };

    namespace Logi {
        static const int Profiles = 3;                  // shortest, balanced, safest
        static const double ProfileLambda[Profiles] = { 0.0, 1.0, 5.0 };
        static const double RouteRefValue = 1e9;        // the cargo value the profiles' weights assume

        // SecBand of a system's security:  high >= 0.45, low > 0, null otherwise
        uint8_t Band(float security);
        // one jump into a system of the band, for a cargo worth value ISK (hullFactor:  blockade runners 0.2, PLAN 6.5)
        double JumpLoss(const RiskParams& risk, uint8_t band, double value, double hullFactor = 1.0);
        // the chance a cargo worth value is lost on a route with these jumps per band:  1 - prod (1 - p_jump)
        double TripLoss(const RiskParams& risk, const uint16_t bandJumps[SecBand::Count], double value, double hullFactor = 1.0);
        /* m3 a type travels as:  ships packaged (the client's hard-coded volumes per group, as InventoryItem::
         *  GetPackagedVolume), everything else its volume */
        double PackagedVolume(const TypeData& t);
    }

    struct Route {
        bool     ok         = false;            // reachable
        uint8_t  profile    = 0;
        uint16_t jumps      = 0;
        uint16_t bandJumps[SecBand::Count] = {};    // the systems it passes per band (high, low, null, w-space):  the one it
                                                    //  leaves from and each jump's destination
    };

    class RouteMap {
    public:
        // the graph:  data.systems (k-space) and data.jumps.  risk and riskScale set the profiles' weights
        void Build(const StaticData& data, const RiskParams& risk, double riskScale);

        bool     Has(uint32_t systemID) const           { return m_index.find(systemID) != m_index.end(); }
        size_t   Systems() const                        { return m_ids.size(); }
        size_t   Edges() const;
        uint8_t  BandOf(uint32_t systemID) const;

        // the route of a profile (cached).  from == to:  ok with no jumps
        const Route& Get(uint32_t fromSystem, uint32_t toSystem, uint8_t profile);
        // of the profiles' routes, the one a cargo of m3 and value ISK is cheapest on:  freight x m3 x jumps + P x value
        const Route& Best(uint32_t fromSystem, uint32_t toSystem, double m3, double value, double freightPerM3Jump, const RiskParams& risk);
        static double Cost(const Route& r, double m3, double value, double freightPerM3Jump, const RiskParams& risk);

        /* the station nearest to a system (fewest jumps, then lowest stationID) in a system of the band, among
         *  candidates (stationID, systemID).  0 when none can be reached;  jumps gets its distance */
        uint32_t Nearest(uint32_t fromSystem, uint8_t band, const std::vector<std::pair<uint32_t, uint32_t>>& candidates, uint16_t* jumps = nullptr);

    private:
        // Dijkstra from a system for a profile:  the predecessors of every system (cached per source and profile)
        const std::vector<int32_t>& Tree(uint32_t fromIdx, uint8_t profile);

        std::vector<uint32_t>                       m_ids;          // index -> systemID
        std::unordered_map<uint32_t, uint32_t>      m_index;        // systemID -> index
        std::vector<std::vector<uint32_t>>          m_adj;
        std::vector<uint8_t>                        m_band;
        std::vector<double>                         m_enter[Logi::Profiles];   // the cost of a jump into the system
        std::map<std::pair<uint32_t, uint8_t>, std::vector<int32_t>> m_trees;
        std::map<std::tuple<uint32_t, uint32_t, uint8_t>, Route> m_cache;
        Route                                       m_none;
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_LOGISTICS_H__
