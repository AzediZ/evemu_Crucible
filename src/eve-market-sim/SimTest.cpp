
 /**
  * @name SimTest.cpp
  *   unit tests of the market sim core:  the formulas (SimEconomy), the ledger and its audit (SimLedger), and a short
  *   world run with players trading through the hooks
  *
  * @date:          13Oct26
  *
  */

#include <cmath>
#include <map>
#include <vector>

#include "HarnessData.h"
#include "HarnessMarket.h"
#include "TestCheck.h"
#include "market/sim/SimEconomy.h"
#include "market/sim/SimLedger.h"
#include "market/sim/SimProduction.h"
#include "market/sim/SimRng.h"
#include "market/sim/SimWorld.h"

using namespace MktSim;

int EconomyTest()
{
    // rounding:  4 significant digits, whole cents, asks up and bids down
    T_CHECK_EQ(Econ::RoundPrice(4.5675, true), 457);
    T_CHECK_EQ(Econ::RoundPrice(4.5675, false), 456);
    T_CHECK_EQ(Econ::RoundPrice(123456.0, true), 12350000);
    T_CHECK_EQ(Econ::RoundPrice(123456.0, false), 12340000);
    T_CHECK_EQ(Econ::RoundPrice(0.001, true), 1);
    // the ladder keeps its order and the spread
    Cents asks[3], bids[3];
    Econ::QuoteLadder(100.0, 0.06, 3, asks, bids);
    T_CHECK(asks[0] == 10600);
    T_CHECK(bids[0] == 9400);
    T_CHECK((asks[1] > asks[0]) and (asks[2] > asks[1]));
    T_CHECK((bids[1] < bids[0]) and (bids[2] < bids[1]));
    Econ::QuoteLadder(0.02, 0.015, 3, asks, bids);
    T_CHECK(bids[0] < asks[0]);
    T_CHECK(bids[2] >= 1);
    // anchor:  no pass-through without a cost, full pass-through follows the cost
    T_CHECK(std::fabs(Econ::Anchor(100.0, 0.0, 200.0, 100.0) - 100.0) < 1e-9);
    T_CHECK(std::fabs(Econ::Anchor(100.0, 1.0, 120.0, 100.0) - 120.0) < 1e-9);
    T_CHECK(std::fabs(Econ::Anchor(100.0, 0.5, 120.0, 100.0) - 110.0) < 1e-9);
    // stock pressure:  more stock, lower target;  empty stock and a glut hit the clamp
    bool clamped(false);
    T_CHECK(std::fabs(Econ::TargetValue(100.0, 10.0, 10.0, 0.35, 2.0, &clamped) - 100.0) < 1e-9);
    T_CHECK(!clamped);
    T_CHECK(Econ::TargetValue(100.0, 20.0, 10.0, 0.35, 2.0) < 100.0);
    T_CHECK(std::fabs(Econ::TargetValue(100.0, 0.0, 10.0, 0.35, 2.0, &clamped) - 200.0) < 1e-9);
    T_CHECK(clamped);
    T_CHECK(std::fabs(Econ::TargetValue(100.0, 1e9, 10.0, 0.35, 2.0) - 50.0) < 1e-9);
    // OU:  no noise converges by the half life, long steps don't overshoot
    double k(std::log(2.0) / 3.0);
    T_CHECK(std::fabs(Econ::OUStep(1.0, 0.0, k, 3.0, 0.0, 0.0) - 0.5) < 1e-9);
    T_CHECK(std::fabs(Econ::OUStep(1.0, 0.0, k, 1000.0, 0.0, 0.0)) < 1e-9);
    // the OU sigma for a calibrated volatility:  the stationary 5-day move is volatility^2 x 5;  no reversion, no change
    double hs(Econ::HorizonSigma(0.02, k));
    T_CHECK(std::fabs(hs * hs * (1.0 - std::exp(-5.0 * k)) / k - 0.02 * 0.02 * 5.0) < 1e-12);
    T_CHECK((hs > 0.02 * 1.29) and (hs < 0.02 * 1.31));
    T_CHECK(std::fabs(Econ::HorizonSigma(0.02, 0.0) - 0.02) < 1e-12);
    // demand:  elasticity at the reference ask, budget scales it
    T_CHECK(std::fabs(Econ::DemandRate(10.0, 1.0, 1.0, 1.0, 110.0, 110.0, 1.0) - 10.0) < 1e-9);
    T_CHECK(std::fabs(Econ::DemandRate(10.0, 1.0, 1.0, 1.0, 220.0, 110.0, 1.0) - 5.0) < 1e-9);
    T_CHECK(std::fabs(Econ::DemandRate(10.0, 1.0, 1.0, 0.5, 110.0, 110.0, 1.0) - 5.0) < 1e-9);
    T_CHECK(std::fabs(Econ::BudgetFactor(0.0, 100.0) - 0.2) < 1e-9);
    T_CHECK(std::fabs(Econ::BudgetFactor(1000.0, 100.0) - 1.2) < 1e-9);
    // the controller pays more when money is short, less when there is too much, within [0.5, 2]
    T_CHECK(Econ::ControllerFactor(50.0, 100.0, 0.0, 0.3, 0.02) > 1.0);
    T_CHECK(Econ::ControllerFactor(200.0, 100.0, 0.0, 0.3, 0.02) < 1.0);
    T_CHECK(Econ::ControllerFactor(1.0, 100.0, 50.0, 0.3, 0.02) <= 2.0);
    // overflow guard and lots
    T_CHECK(Econ::MulCents(INT64_MAX / 2, 3) == -1);
    T_CHECK_EQ(Econ::RoundLot(2500.0, 1000), 2000);
    T_CHECK_EQ(Econ::RoundLot(600.0, 1000), 1000);
    T_CHECK_EQ(Econ::RoundLot(400.0, 1000), 0);
    // the counter rng:  same inputs, same draws;  sensible moments
    T_CHECK(Rng::Hash64(1, 2, 3) == Rng::Hash64(1, 2, 3));
    T_CHECK(Rng::Hash64(1, 2, 3) != Rng::Hash64(1, 2, 4));
    double sum(0.0), sq(0.0), psum(0.0);
    for (uint64_t i = 0; i < 20000; ++i) {
        double x(Rng::Normal(Rng::Hash64(7, i)));
        sum += x;
        sq += x * x;
        psum += (double)Rng::Poisson(3.5, Rng::Hash64(8, i));
    }
    T_CHECK(std::fabs(sum / 20000.0) < 0.03);
    T_CHECK(std::fabs(sq / 20000.0 - 1.0) < 0.05);
    T_CHECK(std::fabs(psum / 20000.0 - 3.5) < 0.1);
    return MktTest::failures;
}

int LedgerTest()
{
    Ledger l;
    l.BeginPeriod(0);
    l.Faucet(1, 10000, Isk::Seed);
    l.Faucet(2, 5000, Isk::Income);
    T_CHECK(l.Transfer(2, 1, 2000));
    T_CHECK(!l.Transfer(2, 1, 999999));             // strict:  nothing happens
    T_CHECK(l.Reserve(1, 3000));
    T_CHECK(l.PayFromReserved(1, 2, 1000));
    T_CHECK(l.ToPlayer(1, 500, true));
    l.FromPlayer(2, 700);
    T_CHECK_EQ(l.Sink(2, 100, Isk::BrokerFee), 100);
    T_CHECK_EQ(l.Sink(2, 999999, Isk::Upkeep, true), 0);
    l.Unreserve(1, 1500);
    l.GoodsIn(1, 1, 34, 100, Goods::Seed);
    T_CHECK(l.ReserveGoods(1, 1, 34, 60));
    T_CHECK(!l.ReserveGoods(1, 1, 34, 41));
    T_CHECK(l.GoodsOut(1, 1, 34, 10, Goods::SimToPlayer, true));
    T_CHECK(l.MoveGoods(1, 2, 1, 34, 20, false));
    l.GoodsIn(2, 1, 34, 5, Goods::PlayerToSim);
    T_CHECK(l.GoodsOut(2, 1, 34, 25, Goods::Consumption, false));
    Ledger::Result r(l.Audit());
    T_CHECK_EQ(r.iskResidual, 0);
    T_CHECK_EQ(r.goodsResidualTypes, 0);
    T_CHECK_EQ(r.moneyEnd, 10000 + 5000 - 500 + 700 - 100);
    T_CHECK_EQ(l.GoodsOf(34), 100 - 10 + 5 - 25);
    T_CHECK_EQ(l.Violations(), 0);
    // a change made around the ledger is a residual
    l.Load(3, 42, 0);
    T_CHECK(l.Audit().iskResidual == 42);
    return MktTest::failures;
}

int WorldTest()
{
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
    world.SetDayHook([&](const DayReport& r) {
        residuals.push_back(r.audit.iskResidual);
        residuals.push_back((int64_t)r.audit.goodsResidualTypes);
    });
    uint32_t jita(data.nodes[0].stationID);
    uint16_t trit(34);
    Units totalSold(0);
    for (int day = 0; day < 6; ++day) {
        for (int tick = 0; tick < 96; ++tick) {
            int64_t t(t0 + day * SecondsPerDay + tick * 900);
            market.SetNow(t);
            world.Step(t);
            if (tick == 40) {
                // a player buys from the sim and sells back into its bids
                Cents ask(market.SimAsk(jita, trit));
                T_CHECK(ask > 0);
                Units got(market.Buy(1, jita, trit, 1000, ask));
                T_CHECK(got > 0);
                // the market maker bids when its stock is under target (it is, after the purchase, from its next visit)
                totalSold += market.Sell(1, jita, trit, market.GetPlayer(1).goods[trit], 1);
                // the round trip loses money
                MktHarness::Player& me(market.GetPlayer(1));
                T_CHECK(me.fromSim < me.toSim);
            }
        }
    }
    T_CHECK(totalSold > 0);
    T_CHECK(!residuals.empty());
    for (auto r : residuals)
        T_CHECK_EQ(r, 0);
    std::string first;
    T_CHECK_EQ(market.CheckMirror(first), 0);
    T_CHECK(world.LiveOrders() > 0);
    T_CHECK_EQ(world.GetLedger().Violations(), 0);
    // purging cancels everything and frees the reservations
    world.PurgeOrders();
    world.FlushOrders();
    T_CHECK_EQ(world.LiveOrders(), 0);
    T_CHECK_EQ(market.SimOrderCount(), 0);
    for (auto& cur : world.GetLedger().Accounts())
        T_CHECK_EQ(cur.second.reserved, 0);
    for (auto& cur : world.GetLedger().Stocks())
        T_CHECK_EQ(cur.second.reserved, 0);
    return MktTest::failures;
}

int ProductionTest()
{
    // refining:  whole batches, the yield, the station's take
    T_CHECK(std::fabs(Prod::RefineYield(0.5) - 1.0) < 1e-12);
    T_CHECK(std::fabs(Prod::RefineYield(0.25) - (0.25 + 0.375 * 1.1 * 1.2 * 1.25)) < 1e-12);
    TypeData ore;
    ore.typeID = 1230;
    ore.portionSize = 333;
    ore.refine.push_back(std::make_pair((uint16_t)34, 1000.0 / 333.0));
    Prod::RefineResult r;
    Prod::Refine(ore, 1000, 1.0, 0.05, r);
    T_CHECK_EQ(r.used, 999);
    T_CHECK((r.gross.size() == 1) and (r.gross[0].second == 3000));
    T_CHECK((r.tax.size() == 1) and (r.tax[0].second == 150));
    Prod::Refine(ore, 332, 1.0, 0.05, r);
    T_CHECK_EQ(r.used, 0);
    // build time:  per unit, with the multiplier;  a default for unknown blueprints
    TypeData ship;
    ship.portionSize = 1;
    ship.buildSeconds = 7200.0;
    T_CHECK(std::fabs(Prod::BuildHours(ship, 0.8, 3600.0) - 1.6) < 1e-12);
    ship.buildSeconds = 0.0;
    T_CHECK(std::fabs(Prod::BuildHours(ship, 1.0, 3600.0) - 1.0) < 1e-12);

    // virtual lines:  a slot count, a cost per job
    Prod::VirtualCapacity cap(1000.0, 333.0);
    cap.SetSlots(7, 2);
    SimJob j;
    j.actorID = 7;
    j.start = 0;
    j.end = 3 * 3600;
    T_CHECK(cap.Reserve(j, 0));
    T_CHECK(cap.Reserve(j, 0));
    T_CHECK(!cap.Reserve(j, 0));
    T_CHECK_EQ(cap.Free(7, 1, Activity::Manufacturing, 0), 0);
    cap.Release(j);
    T_CHECK_EQ(cap.Free(7, 1, Activity::Manufacturing, 0), 1);
    T_CHECK(std::fabs(cap.JobCost(j) - (1000.0 + 3.0 * 333.0)) < 1e-9);

    // the mining mix.  ore 0:  10 Tritanium a m3;  ore 1:  2 Tritanium and 1 Pyerite a m3
    std::vector<std::vector<double>> y = { { 10.0, 0.0 }, { 2.0, 1.0 } };
    std::vector<double> need = { 100.0, 10.0 };
    std::vector<double> x(Prod::CoverMix(y, need, 1000.0, std::vector<bool>(2, true), 2000));
    T_CHECK((x[0] * 10.0 + x[1] * 2.0 >= 99.0) and (x[1] >= 9.9));
    T_CHECK(x[0] + x[1] <= 19.5);                   // no m3 for what is covered already
    x = Prod::CoverMix(y, need, 10.0, std::vector<bool>(2, true), 1000);
    T_CHECK(std::fabs(x[0] + x[1] - 10.0) < 1e-6);  // short of m3:  all of it is used
    x = Prod::CoverMix(y, need, 1000.0, std::vector<bool>{ true, false }, 1000);
    T_CHECK(x[1] == 0.0);
    // the least squares fit meets what it can
    std::vector<double> fit(Prod::FitMix(y, need, std::vector<double>{ 1.0, 1.0 }, 2000));
    T_CHECK(std::fabs(fit[0] * 10.0 + fit[1] * 2.0 - 100.0) < 1.0);
    T_CHECK(std::fabs(fit[1] - 10.0) < 0.2);
    return MktTest::failures;
}

int ActorsTest()
{
    // a short run of the synthetic world with the phase 3 actors:  the books close, the actors work, nothing goes negative
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
    world.SetDayHook([&](const DayReport& r) {
        residuals.push_back(r.audit.iskResidual);
        residuals.push_back((int64_t)r.audit.goodsResidualTypes);
    });
    std::map<uint8_t, int> kinds;
    for (auto& a : world.Actors())
        ++kinds[a.kind];
    T_CHECK(kinds[ActorKind::Miner] == (int)data.nodes.size());
    T_CHECK(kinds[ActorKind::Manufacturer] > 0);
    T_CHECK(kinds[ActorKind::Reprocessor] > 0);
    T_CHECK(kinds[ActorKind::LootSource] > 0);
    size_t mostJobs(0);
    for (int day = 0; day < 8; ++day) {
        for (int tick = 0; tick < 96; ++tick) {
            int64_t t(t0 + day * SecondsPerDay + tick * 900);
            market.SetNow(t);
            world.Step(t);
            mostJobs = std::max(mostJobs, world.Jobs().size());
        }
        if (day == 2)
            world.ShockMining(0.5, 2.0, t0 + day * SecondsPerDay);
    }
    T_CHECK(!residuals.empty());
    for (auto r : residuals)
        T_CHECK_EQ(r, 0);
    T_CHECK(mostJobs > 0);
    T_CHECK(!world.Made().empty());
    T_CHECK(!world.MinedUnits().empty());
    T_CHECK_EQ(world.GetLedger().Violations(), 0);
    for (auto& cur : world.GetLedger().Accounts())
        T_CHECK((cur.second.wallet >= 0) and (cur.second.reserved >= 0));
    for (auto& cur : world.GetLedger().Stocks())
        T_CHECK((cur.second.qty >= 0) and (cur.second.reserved >= 0));
    std::string first;
    T_CHECK_EQ(market.CheckMirror(first), 0);
    // a job saved and loaded again holds its line and ends with its output
    if (!world.Jobs().empty()) {
        SimJob j(world.Jobs().begin()->second);
        World again(data, p, market);
        again.Build(t0);
        j.jobID = 0;
        T_CHECK(again.LoadJob(j));
        T_CHECK_EQ(again.Jobs().size(), 1);
        j.actorID = 1;                              // not a manufacturer
        T_CHECK(!again.LoadJob(j));
    }
    return MktTest::failures;
}
