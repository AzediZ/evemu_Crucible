
 /**
  * @name SimChains.h
  *   market simulation:  the advanced chains of phase 6 (PLAN 6.4, 11 phase 6), the pure part.  the formulas the chains
  *   share with the server's industry (invention chance and runs, blueprint waste), and the static groups they know.
  *   World's chain members (the inventors, harvesters, reactors and planetary producers) are in SimChains.cpp
  *
  * @date:          16Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_CHAINS_H__
#define _EVE_SERVER_MARKET_SIM_CHAINS_H__

#include <utility>
#include <vector>

#include "market/sim/SimStaticData.h"

namespace MktSim {
namespace Chains {

    // invGroups the chains know
    namespace Group {
        enum : uint16_t {
            MoonMaterials       = 427,      // raw moon materials (harvested by a POS array)
            HarvestableCloud    = 711,      // gas (harvested by ships)
            DataInterface       = 716,      // an invention tool (not used up:  FactoryDB::GetRAMRequirements)
            CapitalComponents   = 873,
            AdvancedCapitalComponents = 913,
            FuelBlock           = 1136
        };
    }

    // the invention skills of the sim's inventors (every one at V, like the other actors' skills)
    const int InventorSkillLevel = 5;
    // an invented copy's material and productivity levels (EvERam::BaseInventionME / PE)
    const double BaseInventionME = -4.0;
    // the material level of the component blueprints the sim's manufacturers use (bought originals, unresearched)
    const double ComponentMaterialLevel = 0.0;

    /* the Crucible invention chance, EvEMath::RAM::InventionChance (eve-common, the pre-Phoebe formula):
     *  base x (1 + 0.01 encryption) x (1 + (science1 + science2) x 0.1 / (5 - metaLevel)) x decryptor */
    double InventionChance(double base, int encryption, int science1, int science2, int metaLevel, double decryptor);

    /* runs of an invented copy, EvEMath::RAM::InventionRuns:
     *  min(max(floor(t1Runs / t1Max x t2Max / 10), 1) + decryptorRuns, t2Max), at least 1 */
    int InventionRuns(int t1Runs, int t1Max, int t2Max, int decryptorRuns);

    /* a blueprint's waste at a material level (Blueprint::GetME, RamMethods::GetMaterialQuantity):  wf / (1 + ME), and
     *  wf x (1 - ME) below 0 (an invented copy at -4:  x5) */
    double Waste(double wasteFactor, double materialLevel);

    // the materials of a bill per unit at a material level:  base x (1 + waste) + extra (the same type summed)
    void BillAt(const ChainBill& bill, double materialLevel, std::vector<std::pair<uint16_t, double>>& into);

    // a recipe's inputs and outputs with what goes in and comes out again netted (a booster reaction's water)
    void NetRecipe(const Recipe& r, std::vector<std::pair<uint16_t, double>>& inputs, std::vector<std::pair<uint16_t, double>>& outputs);

    // successes of n attempts at chance p, deterministic for the hash (a Bernoulli draw each, a normal one above 2000)
    Units Successes(Units attempts, double p, uint64_t hash);

}
}

#endif  // _EVE_SERVER_MARKET_SIM_CHAINS_H__
