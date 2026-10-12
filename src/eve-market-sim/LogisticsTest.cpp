
 /**
  * @name LogisticsTest.cpp
  *   unit tests of the market sim's logistics (SimLogistics, SimHaulers.cpp):  the risk formulas, routes on a small map,
  *   a short world run with haulers and remote miners, and the shipments' reload after a restart
  *
  * @date:          15Oct26
  *
  */

#include <cmath>
#include <map>
#include <vector>

#include "HarnessData.h"
#include "HarnessMarket.h"
#include "TestCheck.h"
#include "market/sim/SimLedger.h"
#include "market/sim/SimLogistics.h"
#include "market/sim/SimWorld.h"

using namespace MktSim;

namespace {
    bool Near(double a, double b, double tol = 1e-12)  { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
}

int LogisticsTest()
{
    // bands
    T_CHECK_EQ(Logi::Band(1.0f), SecBand::High);
    T_CHECK_EQ(Logi::Band(0.45f), SecBand::High);
    T_CHECK_EQ(Logi::Band(0.44f), SecBand::Low);
    T_CHECK_EQ(Logi::Band(0.01f), SecBand::Low);
    T_CHECK_EQ(Logi::Band(0.0f), SecBand::Null);
    T_CHECK_EQ(Logi::Band(-0.5f), SecBand::Null);

    // per jump loss (PLAN 6.5):  high-sec follows the cargo's value, low and null-sec don't
    RiskParams risk;
    T_CHECK(Near(Logi::JumpLoss(risk, SecBand::High, 1e9), 2e-5));
    T_CHECK(Near(Logi::JumpLoss(risk, SecBand::High, 5e8), 2e-5 * std::pow(0.5, 0.7)));
    T_CHECK(Near(Logi::JumpLoss(risk, SecBand::High, 0.0), 0.0));
    T_CHECK(Near(Logi::JumpLoss(risk, SecBand::Low, 1.0), 2e-3));
    T_CHECK(Near(Logi::JumpLoss(risk, SecBand::Null, 1e12), 5e-3));
    T_CHECK(Near(Logi::JumpLoss(risk, SecBand::Low, 1.0, 0.2), 4e-4));
    uint16_t jumps[SecBand::Count] = { 10, 3, 2, 0 };
    double p(1.0 - std::pow(1.0 - 2e-5, 10) * std::pow(1.0 - 2e-3, 3) * std::pow(1.0 - 5e-3, 2));
    T_CHECK(Near(Logi::TripLoss(risk, jumps, 1e9), p, 1e-9));
    RiskParams high(risk);
    high.lowSec *= 10.0;
    T_CHECK(Logi::TripLoss(high, jumps, 1e9) > 4.0 * Logi::TripLoss(risk, jumps, 1e9));

    // packaged volumes:  ships by group (the client's), the rest as they are
    TypeData t;
    t.categoryID = Category::Ship;
    t.groupID = 28;
    t.volume = 250000.0;
    T_CHECK(Near(Logi::PackagedVolume(t), 20000.0));
    t.groupID = 27;
    T_CHECK(Near(Logi::PackagedVolume(t), 50000.0));
    t.categoryID = Category::Module;
    t.volume = 5.0;
    T_CHECK(Near(Logi::PackagedVolume(t), 5.0));

    /* routes:  1 - 2 - 3 - 4 through high-sec (3 jumps), 1 - 5 - 4 through a low-sec system (2 jumps).  shortest goes
     *  through low-sec, balanced and safest around it */
    StaticData map;
    auto sys = [&map](uint32_t id, float sec) { MapSystem s; s.security = sec; s.regionID = 1; map.systems[id] = s; };
    sys(1, 0.9f); sys(2, 0.9f); sys(3, 0.9f); sys(4, 0.9f); sys(5, 0.2f); sys(6, -0.3f);
    map.jumps = { {1, 2}, {2, 3}, {3, 4}, {1, 5}, {5, 4}, {5, 6} };
    RouteMap routes;
    routes.Build(map, risk, 1000.0);
    T_CHECK_EQ(routes.Systems(), 6);
    T_CHECK_EQ(routes.Edges(), 6);
    const Route& shortest(routes.Get(1, 4, 0));
    T_CHECK(shortest.ok);
    T_CHECK_EQ(shortest.jumps, 2);
    T_CHECK_EQ(shortest.bandJumps[SecBand::Low], 1);
    const Route& balanced(routes.Get(1, 4, 1));
    T_CHECK_EQ(balanced.jumps, 3);
    T_CHECK_EQ(balanced.bandJumps[SecBand::Low], 0);
    T_CHECK_EQ(routes.Get(1, 4, 2).jumps, 3);
    T_CHECK(routes.Get(1, 1, 0).ok);
    T_CHECK_EQ(routes.Get(1, 1, 0).jumps, 0);
    T_CHECK(!routes.Get(1, 99, 0).ok);
    // a cheap bulky cargo takes the short way, a dear one the safe way
    T_CHECK_EQ(routes.Best(1, 4, 1e6, 1e3, 1.5, risk).jumps, 2);
    T_CHECK_EQ(routes.Best(1, 4, 100.0, 1e9, 1.5, risk).jumps, 3);
    // the nearest station of a band
    std::vector<std::pair<uint32_t, uint32_t>> stations = { {100, 3}, {200, 5}, {300, 6} };
    uint16_t away(0);
    T_CHECK_EQ(routes.Nearest(1, SecBand::Low, stations, &away), 200);
    T_CHECK_EQ(away, 1);
    T_CHECK_EQ(routes.Nearest(1, SecBand::Null, stations, &away), 300);
    T_CHECK_EQ(away, 2);
    T_CHECK_EQ(routes.Nearest(1, SecBand::High, stations, &away), 100);

    // locations:  a site follows its node's number, goods on the way stay
    T_CHECK(IsSiteLoc(SiteLoc(3, SecBand::Null)));
    T_CHECK_EQ(SiteNode(SiteLoc(3, SecBand::Null)), 3);
    T_CHECK_EQ(SiteBand(SiteLoc(3, SecBand::Null)), SecBand::Null);
    std::function<uint32_t(uint32_t)> remap([](uint32_t n) { return (n == 3) ? 1u : n; });
    T_CHECK_EQ(World::MapLocation(SiteLoc(3, SecBand::Low), remap), SiteLoc(1, SecBand::Low));
    T_CHECK_EQ(World::MapLocation(3, remap), 1);
    T_CHECK_EQ(World::MapLocation(TransitLoc, remap), TransitLoc);

    // the ledger moves an actor's goods between locations with no flow (the audit closes)
    Ledger ledger;
    ledger.BeginPeriod(0);
    ledger.GoodsIn(1, 1, 34, 100, Goods::Mined);
    T_CHECK(ledger.Relocate(1, 1, TransitLoc, 34, 60));
    T_CHECK(!ledger.Relocate(1, 1, TransitLoc, 34, 41));
    T_CHECK(ledger.GoodsOut(1, TransitLoc, 34, 10, Goods::Transport, false));
    Ledger::Result r(ledger.Audit());
    T_CHECK_EQ(r.goodsResidualTypes, 0);
    T_CHECK_EQ(r.goods[Goods::Transport], 10);
    T_CHECK_EQ(ledger.GoodsOf(34), 90);

    /* a short world run:  sites, remote miners and haulers are built, shipments arrive and the books close.  then the
     *  shipments go into a second world (as a restart loads them) and agree with its books */
    StaticData data;
    MktHarness::BuildSynthetic(data, 7, 0.25);
    Params prm;
    prm.pauseOnAuditResidual = false;
    prm.risk.lowSec = 0.05;                         // losses to see in a short run
    MktHarness::HarnessMarket market(data, prm.salesTaxRate);
    World world(data, prm, market);
    market.SetWorld(&world);
    const int64_t t0(1325462400);
    world.Build(t0);
    world.Seed(t0);
    world.Start(t0, false);
    std::vector<int64_t> residuals;
    Units transport(0);
    world.SetDayHook([&](const DayReport& rep) {
        residuals.push_back(rep.audit.iskResidual);
        residuals.push_back((int64_t)rep.audit.goodsResidualTypes);
        transport += rep.audit.goods[Goods::Transport];
    });
    T_CHECK_EQ(world.Sites().size(), 2 * data.nodes.size());
    uint32_t haulers(0), remote(0);
    for (auto& a : world.Actors()) {
        haulers += (a.kind == ActorKind::Hauler);
        remote += (a.kind == ActorKind::RemoteMiner);
    }
    T_CHECK_EQ(haulers, 10);                        // every pair of the five hubs
    T_CHECK(remote >= data.nodes.size());
    T_CHECK(world.RemoteShare(1, 40) > 0.5);        // Megacyte comes from null-sec
    // twelve days, then on until shipments are on the way
    int64_t now(t0);
    for (int tick = 0; (tick < 14 * 96) and ((tick < 12 * 96) or world.Trips().empty()); ++tick) {
        now = t0 + tick * 900;
        market.SetNow(now);
        world.Step(now);
    }
    T_CHECK(world.TripsArrived() > 0);
    T_CHECK(world.TripsLost() > 0);
    T_CHECK(transport > 0);
    T_CHECK(!residuals.empty());
    for (auto v : residuals)
        T_CHECK_EQ(v, 0);
    T_CHECK_EQ(world.GetLedger().Violations(), 0);
    Units hulls(0);
    for (auto& cur : world.HaulHullsBought())
        hulls += cur.second;
    T_CHECK(hulls >= (Units)world.TripsLost() / 2);

    // the restart:  the books and the shipments into a new world;  nothing to fix
    T_CHECK(!world.Trips().empty());
    if (world.Trips().empty())
        return MktTest::failures;
    World again(data, prm, market);
    again.Build(t0);
    for (auto& cur : world.GetLedger().Accounts())
        again.GetLedger().Load(cur.first, cur.second.wallet, cur.second.reserved);
    for (auto& cur : world.GetLedger().Stocks())
        again.GetLedger().LoadStock(cur.first, cur.second.qty, cur.second.reserved);
    uint32_t loaded(0);
    Units onTheWay(0);
    for (auto& cur : world.Trips()) {
        loaded += again.LoadTrip(cur.second) ? 1 : 0;
        for (auto& l : cur.second.lines)
            onTheWay += l.qty;
    }
    T_CHECK_EQ(loaded, world.Trips().size());
    std::string summary(again.ReconcileTransit());
    T_CHECK(summary.find("0 cargo lines cut") != std::string::npos);
    T_CHECK(summary.find("0 returned home") != std::string::npos);
    T_CHECK(!again.TakeTripsChanged());             // nothing was fixed (the server marks them for its next flush)
    // a cargo line the books don't hold (a crash between the writes) is cut to what they hold
    const SimTrip& first(world.Trips().begin()->second);
    World third(data, prm, market);
    third.Build(t0);
    for (auto& cur : world.GetLedger().Stocks())
        third.GetLedger().LoadStock(cur.first, cur.second.qty, cur.second.reserved);
    Units held(third.GetLedger().Inv(first.actorID, TransitLoc, first.lines[0].typeID).qty - 1);
    third.GetLedger().LoadStock(Ledger::Key(first.actorID, TransitLoc, first.lines[0].typeID), held, 0);
    for (auto& cur : world.Trips())
        third.LoadTrip(cur.second);
    summary = third.ReconcileTransit();
    T_CHECK(summary.find("1 cargo lines cut") != std::string::npos);
    Units carried(0);
    for (auto& cur : third.Trips())
        for (auto& l : cur.second.lines)
            if ((cur.second.actorID == first.actorID) and (l.typeID == first.lines[0].typeID))
                carried += l.qty;
    T_CHECK_EQ(carried, held);
    T_CHECK(onTheWay > 0);
    (void)now;
    return MktTest::failures;
}
