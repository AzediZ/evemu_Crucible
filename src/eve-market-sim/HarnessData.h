
 /**
  * @name HarnessData.h
  *   the harness' static data:  a deterministic synthetic world shaped on the plan's Crucible-era findings (PLAN 4.1),
  *   or the calibration snapshot utils/market-calibration/gen_market_calibration.py --export-json writes
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_MARKET_SIM_HARNESS_DATA_H__
#define _EVE_MARKET_SIM_HARNESS_DATA_H__

#include <string>

#include "market/sim/SimStaticData.h"

namespace MktHarness {

    // the five trade hubs (PLAN 6.1) as market nodes
    void AddHubs(MktSim::StaticData& data);

    // a synthetic world of about 475 types per unit of scale (minerals, ore, T1/T2 goods and ships, components, loot,
    //  faction, implants, PI, salvage, npc vendor skills).  the same seed gives the same world
    void BuildSynthetic(MktSim::StaticData& data, uint64_t seed, double scale);
    // phase 3:  blueprint times, ore volume, a Drake and mining barges, killmail losses and fits (BuildSynthetic calls it)
    void AddSyntheticIndustry(MktSim::StaticData& data, uint64_t seed);
    /* phase 4:  a small map (BuildSynthetic calls it):  the hubs joined by high-sec chains of about their Crucible jump
     *  counts and a low-sec shortcut, each hub with a low-sec pocket and null-sec beyond (a station with a refinery at the
     *  end of each), where the ores' belts are, and the industrials haulers fly */
    void AddSyntheticGeography(MktSim::StaticData& data, uint64_t seed);
    // phase 4:  the regional nodes the export lists (PLAN 6.1 'auto':  regions with 1% of the isk), except the hubs' regions
    size_t AddRegionalNodes(MktSim::StaticData& data);
    // phase 4:  the other nodes' calibrated price ratios pulled apart, each x e^(sigma N) (a test of the arbitrage)
    void PullApart(MktSim::StaticData& data, double sigma);

    // the --export-json snapshot (types, bills of materials, refine outputs, the mktCal* rows).  false and a message
    //  when the file can't be read
    bool LoadExport(const std::string& path, MktSim::StaticData& data, std::string& err);

}

#endif  // _EVE_MARKET_SIM_HARNESS_DATA_H__
