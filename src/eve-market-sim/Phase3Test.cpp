
 /**
  * @name Phase3Test.cpp
  *   the phase 3 acceptance tests (PLAN 11, phase 3), on the harness:  the same world and seed run three times, without
  *   a shock (the control) and with each shock, so a shock's effect is the shocked run against the control, day by day,
  *   free of the noise both share (the random numbers are counters of cell, time and purpose)
  *     mineral shock   the miners' fleets bring in half for 7 days:  the minerals rise 20-60% (the most raised one's peak, at
  *                     any node:  where a fleet ran short the scarcest mineral takes the shortfall;  Tritanium at the first
  *                     hub, in nearly every ore, rises, often the most),
  *                     the T1 ships' prices follow through their cost (some of the cost rise, within 10 days of the
  *                     shock), and both are back (Tritanium within 10%, ships within 5%) 30 days after it ended
  *     Drake shock     consumers lose twice the Drakes for 14 days:  the Drake's price rises (1% at least:  the yards keep up), more Drakes
  *                     are built (20% at least, over the shock and a week after), the minerals of its bill rise (derived
  *                     demand), and everything is back (Drake within 10%, the minerals within 3%) 30 days after
  *     no negatives    no actor's money or stock goes negative, every day of every run
  *     T1 price / BOM  at steady state (the control, day 60 to the end) the median of V / (bill of materials at the
  *                     minerals' V) per category at the first hub is within 10% of PLAN 4.1:  modules 0.97, charges
  *                     1.18, ships 1.24, drones 1.15.  the calibration's own medians (refPrice / its bill at refPrice)
  *                     are printed alongside
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
#include "HarnessMarket.h"
#include "market/sim/SimWorld.h"

using namespace MktSim;
using namespace MktHarness;

namespace {

    const int64_t StartTime = 1325462400;       // 2012-01-02 00:00 UTC (as Harness.cpp)
    const uint16_t Tritanium = 34;
    const uint16_t Drake = 24698;
    const int ShockDay = 40;
    const int MiningShockDays = 7;
    const int LossShockDays = 14;
    const int RecoveryDays = 30;
    const int SteadyFrom = 60;

    struct Daily {
        std::vector<double> trit;           // V of Tritanium at the first hub
        std::map<uint32_t, std::vector<double>> minerals;   // V of each mineral (nodeID << 16 | typeID), every node
        std::vector<double> ships;          // the T1 ships' V, a d0-weighted sum, at the first hub
        std::vector<double> shipCost;       // their bills at the minerals' V, the same weights
        std::vector<double> drake;          // the Drake's V at the first hub
        std::vector<double> drakeBill;      // the Drake's bill at the minerals' V (its derived demand)
        std::vector<double> drakeMade;      // Drakes built, all nodes, cumulative
        std::map<std::string, std::vector<double>> ratio;   // per category:  the day's median V / bill
        int negatives = 0;
        std::string firstNegative;
        double seconds = 0.0;
    };

    const char* RatioCategory(const TypeData& t)
    {
        switch (t.categoryID) {
            case Category::Module:  return "modules";
            case Category::Charge:  return "charges";
            case Category::Ship:    return "ships";
            case Category::Drone:   return "drones";
        }
        return nullptr;
    }

    double Median(std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        std::sort(v.begin(), v.end());
        size_t n(v.size());
        return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    }

    // the bill of materials of t at the node, at the inputs' V (0 when an input has no cell)
    double Bill(World& w, const TypeData& t, uint32_t nodeID)
    {
        double cost(0.0);
        for (auto& in : t.bom) {
            Cell* ic(w.FindCell(nodeID, in.first));
            if (ic == nullptr)
                return 0.0;
            cost += in.second * std::exp(ic->lnV);
        }
        return cost;
    }

    Daily RunOne(const StaticData& data, Params p, uint64_t seed, int days, std::function<void(World&, int, int64_t)> shock)
    {
        Daily out;
        p.seed = seed;
        p.pauseOnAuditResidual = false;
        std::chrono::steady_clock::time_point started(std::chrono::steady_clock::now());
        HarnessMarket market(data, p.salesTaxRate);
        World world(data, p, market);
        market.SetWorld(&world);
        world.Build(StartTime);
        world.Seed(StartTime);
        world.Start(StartTime, false);
        market.SetNow(StartTime);
        uint32_t hub(data.nodes[0].nodeID);

        // the first hub's built T1 ships, weighted by their calibrated trade
        std::vector<std::pair<uint32_t, double>> ships;
        for (auto& c : world.Cells())
            if ((c.nodeID == hub) and (c.supply == Supply::Manufactured) and (data.Type(c.typeID)->categoryID == Category::Ship))
                ships.push_back(std::make_pair(c.index, c.d0 * c.refLocal));

        for (int day = 0; day < days; ++day) {
            for (int tick = 0; tick < 96; ++tick) {
                int64_t t(StartTime + (int64_t)day * SecondsPerDay + tick * 900);
                if (tick == 0)
                    shock(world, day, t);
                market.SetNow(t);
                // step until the due queue has caught up (as Harness.cpp)
                for (int i = 0; i < 100000; ++i) {
                    uint64_t before(world.CellVisits());
                    world.Step(t);
                    if ((world.Backlog(t) == 0) or (world.CellVisits() == before))
                        break;
                }
            }
            Cell* trit(world.FindCell(hub, Tritanium));
            out.trit.push_back((trit != nullptr) ? std::exp(trit->lnV) : 0.0);
            for (auto& c : world.Cells())
                if (c.supply == Supply::Refined)
                    out.minerals[(c.nodeID << 16) | c.typeID].push_back(std::exp(c.lnV));
            double sv(0.0), sc(0.0);
            for (auto& s : ships) {
                const Cell& c(world.Cells()[s.first]);
                sv += s.second * std::exp(c.lnV) / c.refLocal;
                sc += s.second * Bill(world, *data.Type(c.typeID), hub) / std::max(1e-9, c.costRef);
            }
            out.ships.push_back(sv);
            out.shipCost.push_back(sc);
            Cell* drake(world.FindCell(hub, Drake));
            out.drake.push_back((drake != nullptr) ? std::exp(drake->lnV) : 0.0);
            out.drakeBill.push_back((data.Type(Drake) != nullptr) ? Bill(world, *data.Type(Drake), hub) : 0.0);
            std::map<uint16_t, Units>::const_iterator made = world.Made().find(Drake);
            out.drakeMade.push_back((made == world.Made().end()) ? 0.0 : (double)made->second);
            // the day's V / bill medians
            std::map<std::string, std::vector<double>> r;
            for (auto& c : world.Cells()) {
                if (c.nodeID != hub)
                    continue;
                const TypeData& t(*data.Type(c.typeID));
                const char* cat(RatioCategory(t));
                if ((cat == nullptr) or (t.simClass != SimClass::MfgT1) or t.bom.empty())
                    continue;
                double bill(Bill(world, t, hub));
                if (bill > 0.0)
                    r[cat].push_back(std::exp(c.lnV) / bill);
            }
            for (auto& cur : r)
                out.ratio[cur.first].push_back(Median(cur.second));
            // nothing negative
            for (auto& cur : world.GetLedger().Accounts())
                if ((cur.second.wallet < 0) or (cur.second.reserved < 0)) {
                    if (out.negatives++ == 0)
                        out.firstNegative = "day " + std::to_string(day) + ":  actor " + std::to_string(cur.first) + " money";
                }
            for (auto& cur : world.GetLedger().Stocks())
                if ((cur.second.qty < 0) or (cur.second.reserved < 0)) {
                    if (out.negatives++ == 0)
                        out.firstNegative = "day " + std::to_string(day) + ":  actor " + std::to_string(Ledger::KeyActor(cur.first))
                                          + " stock of type " + std::to_string(Ledger::KeyType(cur.first));
                }
        }
        out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return out;
    }

    // shocked / control on a day (1 when the control is 0)
    double Rel(const std::vector<double>& s, const std::vector<double>& c, int day)
    {
        if ((day < 0) or (day >= (int)s.size()) or !(c[day] > 0.0))
            return 1.0;
        return s[day] / c[day];
    }

    double MaxRel(const std::vector<double>& s, const std::vector<double>& c, int from, int to, int* at = nullptr)
    {
        double best(0.0);
        for (int d = from; (d <= to) and (d < (int)s.size()); ++d) {
            double r(Rel(s, c, d));
            if (r > best) {
                best = r;
                if (at != nullptr)
                    *at = d;
            }
        }
        return best;
    }

    double MeanRel(const std::vector<double>& s, const std::vector<double>& c, int from, int to)
    {
        double sum(0.0);
        int n(0);
        for (int d = from; (d <= to) and (d < (int)s.size()); ++d, ++n)
            sum += Rel(s, c, d);
        return (n > 0) ? sum / n : 1.0;
    }

    bool Check(bool ok, const std::string& what, int& failures)
    {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        if (!ok)
            ++failures;
        return ok;
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
}

int Phase3Main(const StaticData& data, const Params& p, const std::vector<uint64_t>& seeds, int days)
{
    int failures(0);
    if (days < ShockDay + LossShockDays + RecoveryDays + 5)
        days = ShockDay + LossShockDays + RecoveryDays + 5;
    std::printf("market sim phase 3 acceptance tests:  %d days, shocks on day %d, %zu seed(s)\n", days, ShockDay, seeds.size());

    // PLAN 4.1:  price / mineral cost medians of T1
    std::map<std::string, double> plan41 = { {"modules", 0.97}, {"charges", 1.18}, {"ships", 1.24}, {"drones", 1.15} };
    // the calibration's own medians:  refPrice / its bill at refPrice (first hub)
    std::map<std::string, std::vector<double>> calRatios;
    for (auto& cur : data.types) {
        const TypeData& t(cur.second);
        const char* cat(RatioCategory(t));
        if ((cat == nullptr) or (t.simClass != SimClass::MfgT1) or t.bom.empty() or !(t.refPrice > 0.0))
            continue;
        double bill(0.0);
        for (auto& in : t.bom) {
            const TypeData* it(data.Type(in.first));
            bill += in.second * ((it != nullptr) ? it->refPrice : 0.0);
        }
        if (bill > 0.0)
            calRatios[cat].push_back(t.refPrice / bill);
    }

    for (auto seed : seeds) {
        Daily control(RunOne(data, p, seed, days, [](World&, int, int64_t) { }));
        Daily mining(RunOne(data, p, seed, days, [](World& w, int day, int64_t t) {
            if (day == ShockDay)
                w.ShockMining(0.5, MiningShockDays, t);
        }));
        Daily losses(RunOne(data, p, seed, days, [](World& w, int day, int64_t t) {
            if (day == ShockDay)
                w.ShockLosses(Drake, 2.0, LossShockDays, t);
        }));
        std::printf("\nseed %llu:  %.0fs + %.0fs + %.0fs\n", (unsigned long long)seed, control.seconds, mining.seconds, losses.seconds);

        /* the mineral shock.  Tritanium is in nearly every ore:  when the fleets can't mine the node's needs, they keep the
         *  bulk mineral coming and the shortfall goes to the scarcest one, which may be another;  the minerals' peak is
         *  the most raised of them (Tritanium is reported) */
        int s(ShockDay), end(ShockDay + MiningShockDays), tritAt(s), minAt(s);
        double tritPeak(MaxRel(mining.trit, control.trit, s, end + 14, &tritAt));
        double minPeak(0.0);
        uint32_t minType(0);
        for (auto& cur : control.minerals) {
            int at(s);
            double peak(MaxRel(mining.minerals[cur.first], cur.second, s, end + 14, &at));
            if (peak > minPeak) {
                minPeak = peak;
                minType = cur.first;
                minAt = at;
            }
        }
        Check((minPeak >= 1.2) and (minPeak <= 1.6) and (tritPeak > 1.0),
              F("mineral shock:  the minerals peak at x%.3f of the control (type %u at node %u, day %+d of the shock), Tritanium at the"
                " first hub at x%.3f (day %+d) (allowed x1.2..x1.6, Tritanium up)", minPeak, minType & 0xFFFF, minType >> 16, minAt - s,
                tritPeak, tritAt - s), failures);
        int shipAt(s), costAt(s);
        double shipPeak(MaxRel(mining.ships, control.ships, s, end + 21, &shipAt));
        double costPeak(MaxRel(mining.shipCost, control.shipCost, s, end + 21, &costAt));
        int lag(-1);
        for (int d = s; d <= end + 21; ++d)
            if (Rel(mining.ships, control.ships, d) - 1.0 >= 0.5 * (shipPeak - 1.0)) {
                lag = d - s;
                break;
            }
        Check((shipPeak - 1.0 >= 0.3 * 0.9 * (costPeak - 1.0)) and (costPeak > 1.0) and (lag >= 0) and (lag <= 10),
              F("mineral shock:  the T1 ships follow their cost:  cost x%.3f (day %+d), ships x%.3f (day %+d), half way on day %+d"
                " (allowed:  at least 30%% of 0.9 x the cost rise, half way within 10 days)", costPeak, costAt - s, shipPeak, shipAt - s, lag),
              failures);
        double tritBack(MeanRel(mining.trit, control.trit, end + RecoveryDays - 3, end + RecoveryDays + 3));
        double shipsBack(MeanRel(mining.ships, control.ships, end + RecoveryDays - 3, end + RecoveryDays + 3));
        Check((std::fabs(tritBack - 1.0) <= 0.10) and (std::fabs(shipsBack - 1.0) <= 0.05),
              F("mineral shock:  %d days after it, Tritanium x%.3f and the ships x%.3f of the control (allowed 10%%, 5%%)",
                RecoveryDays, tritBack, shipsBack), failures);

        // the Drake shock
        if (data.Type(Drake) != nullptr) {
            int lend(ShockDay + LossShockDays), drakeAt(s);
            double drakePeak(MaxRel(losses.drake, control.drake, s, lend + 7, &drakeAt));
            Check(drakePeak >= 1.01, F("Drake shock:  the Drake peaks at x%.3f of the control on day %+d (allowed at least x1.01)",
                  drakePeak, drakeAt - s), failures);
            double madeS(losses.drakeMade[lend + 7] - losses.drakeMade[s - 1]);
            double madeC(control.drakeMade[lend + 7] - control.drakeMade[s - 1]);
            double madeRel((madeC > 0.0) ? madeS / madeC : 0.0);
            Check(madeRel >= 1.2, F("Drake shock:  %.0f Drakes built over the shock and a week after, against %.0f (x%.2f, allowed at least x1.2)",
                  madeS, madeC, madeRel), failures);
            double billRel(MeanRel(losses.drakeBill, control.drakeBill, s + 3, lend + 7));
            Check(billRel > 1.0, F("Drake shock:  its minerals (its bill at their V) x%.4f of the control over the shock (allowed above 1)",
                  billRel), failures);
            double drakeBack(MeanRel(losses.drake, control.drake, lend + RecoveryDays - 3, lend + RecoveryDays + 3));
            double billBack(MeanRel(losses.drakeBill, control.drakeBill, lend + RecoveryDays - 3, lend + RecoveryDays + 3));
            Check((std::fabs(drakeBack - 1.0) <= 0.10) and (std::fabs(billBack - 1.0) <= 0.03),
                  F("Drake shock:  %d days after it, the Drake x%.3f and its minerals x%.4f of the control (allowed 10%%, 3%%)",
                    RecoveryDays, drakeBack, billBack), failures);
        } else {
            std::printf("  [skip] Drake shock:  the static data has no Drake\n");
        }

        // nothing negative
        int negatives(control.negatives + mining.negatives + losses.negatives);
        std::string first(!control.firstNegative.empty() ? control.firstNegative
                          : (!mining.firstNegative.empty() ? mining.firstNegative : losses.firstNegative));
        Check(negatives == 0, F("no negative money or stock:  %d found%s%s", negatives, first.empty() ? "" : ", first ", first.c_str()), failures);

        // T1 price / BOM at steady state
        for (auto& cur : plan41) {
            std::map<std::string, std::vector<double>>::const_iterator r = control.ratio.find(cur.first);
            if ((r == control.ratio.end()) or ((int)r->second.size() <= SteadyFrom)) {
                std::printf("  [skip] T1 %s:  none built at the first hub\n", cur.first.c_str());
                continue;
            }
            std::vector<double> tail(r->second.begin() + SteadyFrom, r->second.end());
            double sim(Median(tail)), cal(Median(calRatios[cur.first]));
            Check(std::fabs(sim / cur.second - 1.0) <= 0.10,
                  F("T1 %s:  median price / bill %.3f at steady state;  PLAN 4.1 %.2f (x%.3f, allowed 10%%);  the calibration's %.3f",
                    cur.first.c_str(), sim, cur.second, sim / cur.second, cal), failures);
        }
    }
    std::printf("\n%s (%d failure%s)\n", failures ? "PHASE 3 ACCEPTANCE TESTS FAILED" : "all phase 3 acceptance tests passed", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
