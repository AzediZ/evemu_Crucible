
 /**
  * @name ChainsTest.cpp
  *   tests of the phase 6 chains (market/sim/SimChains):  the formulas shared with the server's industry, and a short run
  *   of the synthetic world with every chain
  *
  * @date:          16Oct26
  *
  */

#include <cmath>
#include <map>

#include "HarnessData.h"
#include "HarnessMarket.h"
#include "TestCheck.h"
#include "market/sim/SimChains.h"
#include "market/sim/SimWorld.h"

using namespace MktSim;

namespace {
    bool Near(double a, double b, double eps = 1e-9)    { return std::fabs(a - b) <= eps; }
}

int ChainsTest()
{
    /* the Crucible invention chance (EvEMath::RAM::InventionChance):  a module's 0.4 at encryption V, both sciences V,
     *  meta 0, no decryptor:  0.4 x 1.05 x 1.2;  a meta 4 item and a 1.2 decryptor:  x (1 + 10 x 0.1) x 1.2 */
    T_CHECK(Near(Chains::InventionChance(0.4, 5, 5, 5, 0, 1.0), 0.504));
    T_CHECK(Near(Chains::InventionChance(0.4, 5, 5, 5, 4, 1.2), 0.4 * 1.05 * 2.0 * 1.2));
    T_CHECK(Near(Chains::InventionChance(0.3, 0, 0, 0, 0, 1.0), 0.3));
    T_CHECK(Near(Chains::InventionChance(0.4, 5, 5, 5, 9, 1.0), Chains::InventionChance(0.4, 5, 5, 5, 4, 1.0)));
    // runs (EvEMath::RAM::InventionRuns):  a max-run T1 copy gives a tenth of the T2 maximum, at least 1, decryptors add
    T_CHECK_EQ(Chains::InventionRuns(300, 300, 100, 0), 10);
    T_CHECK_EQ(Chains::InventionRuns(15, 15, 10, 0), 1);
    T_CHECK_EQ(Chains::InventionRuns(1, 300, 10, 0), 1);
    T_CHECK_EQ(Chains::InventionRuns(15, 15, 10, 9), 10);
    T_CHECK_EQ(Chains::InventionRuns(300, 300, 100, -2), 8);
    // waste:  wf / (1 + ME), wf x (1 - ME) below 0
    T_CHECK(Near(Chains::Waste(0.1, 0.0), 0.1));
    T_CHECK(Near(Chains::Waste(0.1, 4.0), 0.02));
    T_CHECK(Near(Chains::Waste(0.1, -4.0), 0.5));
    // a bill at a level:  base takes the waste, extras don't, the same type summed
    ChainBill b;
    b.wasteFactor = 0.1;
    b.base = { {34, 100.0}, {11399, 2.0} };
    b.extra = { {34, 10.0}, {2046, 1.0} };
    std::vector<std::pair<uint16_t, double>> m;
    Chains::BillAt(b, -4.0, m);
    std::map<uint16_t, double> mm(m.begin(), m.end());
    T_CHECK_EQ(mm.size(), 3);
    T_CHECK(Near(mm[34], 160.0));
    T_CHECK(Near(mm[11399], 3.0));
    T_CHECK(Near(mm[2046], 1.0));
    // a recipe netted:  a booster reaction's water (100 in, 95 out) is 5 in
    Recipe r;
    r.inputs = { {3645, 100.0}, {25268, 20.0} };
    r.outputs = { {3645, 95.0}, {25237, 15.0} };
    std::vector<std::pair<uint16_t, double>> in, out;
    Chains::NetRecipe(r, in, out);
    std::map<uint16_t, double> ni(in.begin(), in.end()), no(out.begin(), out.end());
    T_CHECK_EQ(ni.size(), 2);
    T_CHECK(Near(ni[3645], 5.0));
    T_CHECK_EQ(no.size(), 1);
    T_CHECK(Near(no[25237], 15.0));
    // successes:  deterministic, within the binomial's spread, all or none at 1 and 0
    T_CHECK_EQ(Chains::Successes(50, 1.0, 7), 50);
    T_CHECK_EQ(Chains::Successes(50, 0.0, 7), 0);
    T_CHECK_EQ(Chains::Successes(400, 0.5, 11), Chains::Successes(400, 0.5, 11));
    Units total(0);
    for (uint64_t h = 1; h <= 200; ++h)
        total += Chains::Successes(50, 0.4, h);
    T_CHECK(std::fabs((double)total - 4000.0) < 4.0 * std::sqrt(10000.0 * 0.24));
    T_CHECK(std::llabs(Chains::Successes(100000, 0.3, 5) - 30000) < 600);

    /* a short run of the synthetic world:  every chain has its actor and makes something, the books close, nothing goes
     *  negative, the invention outcome follows the chance */
    StaticData data;
    MktHarness::BuildSynthetic(data, 7, 0.25);
    Params p;
    p.pauseOnAuditResidual = false;
    MktHarness::HarnessMarket market(data, p.salesTaxRate);
    World world(data, p, market);
    market.SetWorld(&world);
    const int64_t t0(1325462400);
    world.Build(t0);
    world.Seed(t0);
    world.Start(t0, false);
    std::vector<int64_t> residuals;
    std::map<uint8_t, Units> goods;
    world.SetDayHook([&](const DayReport& rep) {
        residuals.push_back(rep.audit.iskResidual);
        residuals.push_back((int64_t)rep.audit.goodsResidualTypes);
        for (uint8_t f = 0; f < Goods::Count; ++f)
            goods[f] += rep.audit.goods[f];
    });
    std::map<uint8_t, int> kinds;
    for (auto& a : world.Actors())
        ++kinds[a.kind];
    T_CHECK_EQ(kinds[ActorKind::Inventor], data.nodes.size());
    T_CHECK_EQ(kinds[ActorKind::Harvester], data.nodes.size());
    T_CHECK_EQ(kinds[ActorKind::Reactor], data.nodes.size());
    T_CHECK_EQ(kinds[ActorKind::Planetary], data.nodes.size());
    std::map<uint8_t, int> supply;
    for (auto& c : world.Cells())
        ++supply[c.supply];
    T_CHECK(supply[Supply::Invented] > 0);
    T_CHECK(supply[Supply::Harvested] > 0);
    T_CHECK(supply[Supply::Reacted] > 0);
    T_CHECK(supply[Supply::Extracted] > 0);
    T_CHECK(supply[Supply::Processed] > 0);
    // the reference rates reach down the chains:  a raw moon material is used by the reactions
    bool used(false);
    for (auto& c : world.Cells())
        if ((c.supply == Supply::Harvested) and (world.RefDerived(c.nodeID, c.typeID) > 0.0) and (world.RefMade(c.nodeID, c.typeID) > 0.0))
            used = true;
    T_CHECK(used);
    for (int day = 0; day < 12; ++day)
        for (int tick = 0; tick < 96; ++tick) {
            int64_t t(t0 + day * SecondsPerDay + tick * 900);
            market.SetNow(t);
            world.Step(t);
        }
    T_CHECK(!residuals.empty());
    for (auto x : residuals)
        T_CHECK_EQ(x, 0);
    T_CHECK(goods[Goods::Harvested] > 0);
    T_CHECK(goods[Goods::Extracted] > 0);
    T_CHECK(goods[Goods::Invented] > 0);
    T_CHECK(goods[Goods::Fuel] != 0);       // (fuel burnt, a sink)
    T_CHECK_EQ(world.GetLedger().Violations(), 0);
    for (auto& cur : world.GetLedger().Accounts())
        T_CHECK((cur.second.wallet >= 0) and (cur.second.reserved >= 0));
    for (auto& cur : world.GetLedger().Stocks())
        T_CHECK((cur.second.qty >= 0) and (cur.second.reserved >= 0));
    const World::InventionStats& inv(world.Inventions());
    T_CHECK(inv.attempts > 0);
    T_CHECK(std::fabs((double)inv.successes - inv.expected) <= 3.0 * std::sqrt(std::max(1.0, inv.variance)));
    // a chain job saved and loaded again belongs to its actor
    for (auto& cur : world.Jobs()) {
        if (world.Actors()[cur.second.actorID - 1].kind < ActorKind::Inventor)
            continue;
        SimJob j(cur.second);
        j.jobID = 0;
        World again(data, p, market);
        again.Build(t0);
        T_CHECK(again.LoadJob(j));
        T_CHECK_EQ(again.Jobs().size(), 1);
        break;
    }
    return MktTest::failures;
}
