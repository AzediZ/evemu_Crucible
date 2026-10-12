
 /**
  * @name Phase4Test.cpp
  *   the phase 4 acceptance tests (PLAN 11, phase 4:  logistics and route risk), on the harness.  per seed, the same world
  *   runs as given (the control), with RiskLowSec x10, and with the other hubs' calibrated prices pulled apart (each
  *   type's region ratio x e^(0.35 N), wider than the observed spread:  arbitrage has to bring them in)
  *     hub ratios      in the last half of the run, per other hub:  each type's median ratio of the hub's daily average
  *                     price to the first hub's (the Forge) on days both traded, over the types with 10 such days;  the
  *                     median of those in [0.97, 1.08] and their quartiles inside the observed IQR (PLAN 4.1:  Domain
  *                     0.85-1.20, Sinq Laison 0.79-1.23, Heimatar 0.83-1.25, Metropolis 0.79-1.37, other regions
  *                     0.81-1.67), in the control.  in the pulled-apart run the arbitrage narrows the spread:  the IQR at
  *                     most 85% of the calibration's (a hauler takes a node's stock down to half its target at most, so
  *                     a cheap node's price rises at most (1/2)^-theta:  the cheap tail of a spread that wide stays)
  *     route risk      RiskLowSec x10 widens the high-end mineral premium at high-sec hubs:  over the minerals a hub gets
  *                     only from its sites (the remote miners' share at least 90%:  the node's own ores can't stand in)
  *                     and mostly through low-sec (routes that pass a low-sec system:  at least half;  a null-sec site next
  *                     to high-sec doesn't count), their fair value
  *                     against the control's in the last two thirds of the run, the median at least x1.005 (above the
  *                     runs' own noise:  a mineral the risk doesn't reach moves about 0.3%)
  *     losses          shipments are lost (the audit's goods.sink.transport is above 0 over the run) and the haulers buy
  *                     industrials to replace the hulls (bought at least as many as half the shipments lost, and more
  *                     than none), in the RiskLowSec x10 run;  the control's are reported
  *     books           every run's daily audit closes to the cent and the unit, no negative money or stock
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "HarnessData.h"
#include "HarnessFacilities.h"
#include "HarnessMarket.h"
#include "market/sim/SimEconomy.h"
#include "market/sim/SimRng.h"
#include "market/sim/SimWorld.h"

using namespace MktSim;
using namespace MktHarness;

namespace {

    const int64_t StartTime = 1325462400;       // 2012-01-02 00:00 UTC (as Harness.cpp)
    const size_t MinCommonDays = 10;
    const double SpreadSigma = 0.35;            // the pulled-apart run's extra log spread of the region ratios

    struct Run {
        // (regionID, typeID) -> day -> the day's average price
        std::map<std::pair<uint32_t, uint16_t>, std::map<int32_t, double>> avg;
        // (nodeID << 16 | typeID) -> V per day, of the minerals
        std::map<uint32_t, std::vector<double>> minerals;
        std::map<uint32_t, double> remoteShare;  // (nodeID << 16 | typeID) -> the remote miners' share
        std::map<uint32_t, double> lowSecShare;  // ... that comes through low-sec (its route passes a low-sec system)
        Units    transportLost      = 0;
        uint64_t tripsLost          = 0;
        uint64_t tripsArrived       = 0;
        Units    hullsBought        = 0;
        Units    destroyed          = 0;
        Cents    freight            = 0;
        int      residualDays       = 0;
        int      negatives          = 0;
        std::string firstProblem;
        double   seconds            = 0.0;
    };

    struct Quartiles {
        double p25 = 0.0, p50 = 0.0, p75 = 0.0;
        size_t n = 0;
    };

    double Percentile(std::vector<double> v, double q)
    {
        if (v.empty())
            return 0.0;
        std::sort(v.begin(), v.end());
        double pos(q * (double)(v.size() - 1));
        size_t i((size_t)std::floor(pos));
        double f(pos - (double)i);
        return (i + 1 < v.size()) ? v[i] * (1.0 - f) + v[i + 1] * f : v[i];
    }

    Quartiles Quart(const std::vector<double>& v)
    {
        Quartiles q;
        q.n = v.size();
        q.p25 = Percentile(v, 0.25);
        q.p50 = Percentile(v, 0.5);
        q.p75 = Percentile(v, 0.75);
        return q;
    }

    std::string F(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    bool Check(bool ok, const std::string& what, int& failures)
    {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        if (!ok)
            ++failures;
        return ok;
    }

    // PLAN 4.1 regional spread:  the observed IQR of the same-day price ratio against the Forge
    void ObservedIqr(uint32_t regionID, double& lo, double& hi)
    {
        switch (regionID) {
            case 10000043:  lo = 0.85; hi = 1.20; return;     // Domain (Amarr)
            case 10000032:  lo = 0.79; hi = 1.23; return;     // Sinq Laison (Dodixie)
            case 10000030:  lo = 0.83; hi = 1.25; return;     // Heimatar (Rens)
            case 10000042:  lo = 0.79; hi = 1.37; return;     // Metropolis (Hek)
        }
        lo = 0.81;
        hi = 1.67;                                              // non-hub regions
    }

    Run RunOne(const StaticData& data, Params p, uint64_t seed, int days)
    {
        Run out;
        p.seed = seed;
        p.pauseOnAuditResidual = false;
        std::chrono::steady_clock::time_point started(std::chrono::steady_clock::now());
        HarnessMarket market(data, p.salesTaxRate);
        World world(data, p, market);
        market.SetWorld(&world);
        // the manufacturers on the hubs' station lines (phase 5), as in the phase 3 tests
        HarnessCapacity lines(data.nodes, p);
        world.SetCapacity(lines.Capacity());
        world.Build(StartTime);
        world.Seed(StartTime);
        world.Start(StartTime, false);
        market.SetNow(StartTime);
        for (auto& c : world.Cells()) {
            if (c.supply != Supply::Refined)
                continue;
            out.remoteShare[(c.nodeID << 16) | c.typeID] = world.RemoteShare(c.nodeID, c.typeID);
            for (uint8_t band = SecBand::Low; band <= SecBand::Null; ++band) {
                const Route* r(world.RemoteRoute(c.nodeID, band));
                if ((r != nullptr) and (r->bandJumps[SecBand::Low] > 0))
                    out.lowSecShare[(c.nodeID << 16) | c.typeID] += world.RemoteShare(c.nodeID, c.typeID, band);
            }
        }
        world.SetDayHook([&](const DayReport& rep) {
            out.transportLost += rep.audit.goods[Goods::Transport];
            out.destroyed += rep.audit.goods[Goods::Destroyed];
            out.freight += rep.audit.isk[Isk::Freight];
            if ((rep.audit.iskResidual != 0) or (rep.audit.goodsResidualTypes != 0)) {
                if (out.residualDays++ == 0)
                    out.firstProblem = F("day %d:  isk residual %lld, goods residual in %zu types", rep.day, (long long)rep.audit.iskResidual,
                                         rep.audit.goodsResidualTypes);
            }
        });
        for (int day = 0; day < days; ++day) {
            for (int tick = 0; tick < 96; ++tick) {
                int64_t t(StartTime + (int64_t)day * SecondsPerDay + tick * 900);
                market.SetNow(t);
                for (int i = 0; i < 100000; ++i) {
                    uint64_t before(world.CellVisits());
                    world.Step(t);
                    if ((world.Backlog(t) == 0) or (world.CellVisits() == before))
                        break;
                }
            }
            std::vector<HistoryRow> rows;
            world.TakeFinishedHistory(rows);
            for (auto& r : rows)
                if (r.volume > 0)
                    out.avg[std::make_pair(r.regionID, r.typeID)][r.day] = (double)r.sumValue / (double)r.volume / 100.0;
            for (auto& c : world.Cells())
                if (c.supply == Supply::Refined)
                    out.minerals[(c.nodeID << 16) | c.typeID].push_back(std::exp(c.lnV));
            for (auto& cur : world.GetLedger().Accounts())
                if ((cur.second.wallet < 0) or (cur.second.reserved < 0)) {
                    if ((out.negatives++ == 0) and out.firstProblem.empty())
                        out.firstProblem = F("day %d:  actor %u money", day, cur.first);
                }
            for (auto& cur : world.GetLedger().Stocks())
                if ((cur.second.qty < 0) or (cur.second.reserved < 0)) {
                    if ((out.negatives++ == 0) and out.firstProblem.empty())
                        out.firstProblem = F("day %d:  actor %u stock of type %u", day, Ledger::KeyActor(cur.first), Ledger::KeyType(cur.first));
                }
        }
        out.tripsLost = world.TripsLost();
        out.tripsArrived = world.TripsArrived();
        for (auto& cur : world.HaulHullsBought())
            out.hullsBought += cur.second;
        out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return out;
    }

    // per other node:  the quartiles over types of the median same-day ratio against the first node, from day 'from'
    std::map<uint32_t, Quartiles> HubRatios(const StaticData& data, const Run& r, int32_t fromDay)
    {
        std::map<uint32_t, Quartiles> out;
        uint32_t forge(data.nodes[0].regionID);
        std::map<uint32_t, std::vector<double>> perRegion;
        for (auto& cur : r.avg) {
            if (cur.first.first != forge)
                continue;
            for (size_t n = 1; n < data.nodes.size(); ++n) {
                std::map<std::pair<uint32_t, uint16_t>, std::map<int32_t, double>>::const_iterator other
                    = r.avg.find(std::make_pair(data.nodes[n].regionID, cur.first.second));
                if (other == r.avg.end())
                    continue;
                std::vector<double> ratios;
                for (auto& d : other->second) {
                    if (d.first < fromDay)
                        continue;
                    std::map<int32_t, double>::const_iterator f = cur.second.find(d.first);
                    if ((f != cur.second.end()) and (f->second > 0.0))
                        ratios.push_back(d.second / f->second);
                }
                if (ratios.size() >= MinCommonDays)
                    perRegion[data.nodes[n].regionID].push_back(Percentile(ratios, 0.5));
            }
        }
        for (auto& cur : perRegion)
            out[cur.first] = Quart(cur.second);
        return out;
    }

    // the calibration's own spread of the region ratios (types with a cell at both)
    Quartiles CalRatios(const StaticData& data, uint32_t regionID)
    {
        std::vector<double> v;
        for (auto& cur : data.types)
            if (SimClass::HasCells(cur.second.simClass) and (cur.second.refPrice > 0.0) and data.HasRegionRow(regionID, cur.first))
                v.push_back(data.RegionPrice(regionID, cur.first));
        return Quart(v);
    }

    /* narrowing false:  the median in [0.97, 1.08] and the quartiles inside the observed IQR.  true (prices pulled apart
     *  wider than observed):  the arbitrage narrows the spread, the IQR at most 0.85 of the calibration's */
    int CheckRatios(const char* what, const StaticData& data, const Run& r, int days, bool narrowing, int& failures)
    {
        int32_t fromDay((int32_t)(StartTime / SecondsPerDay) + days / 2);
        std::map<uint32_t, Quartiles> q(HubRatios(data, r, fromDay));
        for (size_t n = 1; n < data.nodes.size(); ++n) {
            uint32_t regionID(data.nodes[n].regionID);
            double lo(0.0), hi(0.0);
            ObservedIqr(regionID, lo, hi);
            Quartiles cal(CalRatios(data, regionID));
            std::map<uint32_t, Quartiles>::const_iterator itr = q.find(regionID);
            if ((itr == q.end()) or (itr->second.n < 10)) {
                std::printf("  [skip] %s:  region %u has too few types traded at both hubs\n", what, regionID);
                continue;
            }
            const Quartiles& s(itr->second);
            if (narrowing) {
                double width(s.p75 - s.p25), calWidth(cal.p75 - cal.p25);
                Check(width <= 0.85 * calWidth,
                      F("%s:  region %u / the Forge over %zu types:  IQR %.3f..%.3f, %.0f%% of the calibration's %.3f..%.3f (allowed at most 85%%);"
                        "  median %.3f;  the observed IQR %.2f..%.2f", what, regionID, s.n, s.p25, s.p75, 100.0 * width / std::max(1e-9, calWidth),
                        cal.p25, cal.p75, s.p50, lo, hi), failures);
                continue;
            }
            Check((s.p50 >= 0.97) and (s.p50 <= 1.08) and (s.p25 >= lo) and (s.p75 <= hi),
                  F("%s:  region %u / the Forge over %zu types:  median %.3f (allowed 0.97..1.08), IQR %.3f..%.3f (observed %.2f..%.2f);"
                    "  the calibration's ratios %.3f, %.3f..%.3f", what, regionID, s.n, s.p50, s.p25, s.p75, lo, hi, cal.p50, cal.p25, cal.p75),
                  failures);
        }
        return 0;
    }
}

int Phase4Main(const StaticData& data, const Params& p, const std::vector<uint64_t>& seeds, int days)
{
    int failures(0);
    if (days < 60)
        days = 60;
    if (!p.logistics or !p.actors) {
        std::printf("the phase 4 tests need the actors and logistics (Actors, EnableLogistics)\n");
        return 2;
    }
    if (data.systems.empty()) {
        std::printf("the static data has no map (the export's 'logistics'):  the phase 4 tests can't run\n");
        return 2;
    }
    std::printf("market sim phase 4 acceptance tests:  %d days, %zu seed(s);  %zu systems, %zu jumps, %zu stations\n", days, seeds.size(),
                data.systems.size(), data.jumps.size(), data.stations.size());

    // the other hubs' calibrated prices pulled apart
    StaticData apart(data);
    PullApart(apart, SpreadSigma);

    for (auto seed : seeds) {
        Params risky(p);
        risky.risk.lowSec *= 10.0;
        Run control(RunOne(data, p, seed, days));
        Run risk(RunOne(data, risky, seed, days));
        Run spread(RunOne(apart, p, seed, days));
        std::printf("\nseed %llu:  %.0fs + %.0fs + %.0fs\n", (unsigned long long)seed, control.seconds, risk.seconds, spread.seconds);

        // hub ratios
        CheckRatios("hub ratios (control)", data, control, days, false, failures);
        CheckRatios("hub ratios (pulled apart)", apart, spread, days, true, failures);

        // route risk:  the high-end minerals' premium at the high-sec hubs
        std::vector<double> premia;
        std::string detail;
        int from(days / 3);
        for (auto& cur : control.minerals) {
            uint32_t nodeID(cur.first >> 16);
            uint16_t typeID((uint16_t)(cur.first & 0xFFFF));
            const NodeData& node(data.nodes[nodeID - 1]);
            // the high-end minerals:  all but nothing from the sites (the node's own ores can't stand in)
            if ((node.security < 0.45f) or (control.remoteShare[cur.first] < 0.9) or (control.lowSecShare[cur.first] < 0.5))
                continue;
            const std::vector<double>& c(cur.second);
            const std::vector<double>& r(risk.minerals[cur.first]);
            double sc(0.0), sr(0.0);
            for (int d = from; (d < (int)c.size()) and (d < (int)r.size()); ++d) {
                sc += c[d];
                sr += r[d];
            }
            if (!(sc > 0.0))
                continue;
            premia.push_back(sr / sc);
            detail += F(" %u@%u x%.3f", typeID, nodeID, sr / sc);
        }
        double median(Percentile(premia, 0.5));
        Check(!premia.empty() and (median >= 1.005),
              F("route risk:  RiskLowSec x10 raises the high-end minerals at the high-sec hubs x%.3f (median of %zu;  allowed at least x1.005):%s",
                median, premia.size(), detail.c_str()), failures);

        // losses
        std::printf("  [info] control:  %llu shipments arrived, %llu lost (%lld units), %lld hauling hulls bought, %.0f ISK freight\n",
                    (unsigned long long)control.tripsArrived, (unsigned long long)control.tripsLost, (long long)control.transportLost,
                    (long long)control.hullsBought, Econ::ToIsk(control.freight));
        Check((risk.transportLost > 0) and (risk.tripsLost > 0),
              F("losses:  with RiskLowSec x10 %llu of %llu shipments lost, %lld units in the audit's goods.sink.transport (allowed:  above 0)",
                (unsigned long long)risk.tripsLost, (unsigned long long)(risk.tripsLost + risk.tripsArrived), (long long)risk.transportLost),
              failures);
        Check((risk.hullsBought > 0) and ((double)risk.hullsBought >= 0.5 * (double)risk.tripsLost),
              F("losses:  the haulers bought %lld industrials (the control %lld) for %llu hulls lost (allowed:  at least half of them, more than none)",
                (long long)risk.hullsBought, (long long)control.hullsBought, (unsigned long long)risk.tripsLost), failures);

        // books
        int residual(control.residualDays + risk.residualDays + spread.residualDays);
        int negatives(control.negatives + risk.negatives + spread.negatives);
        std::string first(!control.firstProblem.empty() ? control.firstProblem
                          : (!risk.firstProblem.empty() ? risk.firstProblem : spread.firstProblem));
        Check((residual == 0) and (negatives == 0), F("books:  %d days with a residual, %d negatives%s%s", residual, negatives,
              first.empty() ? "" : ", first ", first.c_str()), failures);
    }
    std::printf("\n%s (%d failure%s)\n", failures ? "PHASE 4 ACCEPTANCE TESTS FAILED" : "all phase 4 acceptance tests passed", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
