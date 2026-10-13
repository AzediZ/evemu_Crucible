
 /**
  * @name Phase6Test.cpp
  *   the phase 6 acceptance tests (PLAN 11, phase 6), on the harness:  the same world and seed run twice, without a shock
  *   (the control) and with a moon material's supply removed, so the removal's effect is the shocked run against the
  *   control, day by day, free of the noise both share
  *     moon removal    the harvesters bring in none of one raw moon material (the one the most component value is made
  *                     of) for 21 days:  what is made of it rises, the reactions' products, the components and the T2
  *                     made of them (each group's V, weighted by its calibrated isk, against the control's)
  *     T2 price / BOM  at steady state (the control, day 60 to the end) the median of V / (bill at its inputs' V, materials at
  *                     perfect material level:  PLAN 4.1's bill) per category at the first hub is within 20% of PLAN 4.1:
  *                     T2 modules 2.73, charges 2.26, drones 2.06, ships 1.44, components 1.28.  the calibration's own medians
  *                     (refPrice over the bill at refPrice) are printed alongside
  *     invention       the successes of every attempt (all nodes, the control) are within 2 sigma of the configured chance
  *                     (the sum of the chances the attempts were made at)
  *     the chains run  every chain made something:  moon materials harvested, reactions, planetary goods, components, T2
  *                     (invented, then built), fuel burnt
  *     books           no residual in the audit, no negative money or stock, every day of both runs
  *
  * @date:          16Oct26
  *
  */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "HarnessData.h"
#include "HarnessFacilities.h"
#include "HarnessMarket.h"
#include "market/sim/SimChains.h"
#include "market/sim/SimWorld.h"

using namespace MktSim;
using namespace MktHarness;

namespace {

    const int64_t StartTime = 1325462400;       // 2012-01-02 00:00 UTC (as Harness.cpp)
    const int ShockDay = 40;
    const int MoonDays = 21;
    const int SteadyFrom = 60;

    struct Run6 {
        std::map<uint32_t, std::vector<double>> v;      // cell index -> V each day (the first hub)
        std::map<uint8_t, Units> made;                  // by supply kind:  units made over the run (all nodes)
        Units harvested = 0, extracted = 0, invented = 0, fuel = 0;
        World::InventionStats inventions;
        int residualDays = 0;
        int negatives = 0;
        std::string firstNegative;
        double seconds = 0.0;
    };

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

    double Median(std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        std::sort(v.begin(), v.end());
        size_t n(v.size());
        return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    }

    // PLAN 4.1's bill of a T2 item or a component:  its materials at perfect material level (no waste) and its extras
    bool PlanBill(const StaticData& data, uint16_t typeID, std::vector<std::pair<uint16_t, double>>& into)
    {
        std::map<uint16_t, ChainBill>::const_iterator b = data.bills.find(typeID);
        if (b == data.bills.end())
            return false;
        into = b->second.base;
        into.insert(into.end(), b->second.extra.begin(), b->second.extra.end());
        return !into.empty();
    }

    const char* RatioCategory(const TypeData& t)
    {
        if (t.simClass == SimClass::Component)
            return "components";
        if (t.simClass != SimClass::MfgT2)
            return nullptr;
        switch (t.categoryID) {
            case Category::Module:  return "modules";
            case Category::Charge:  return "charges";
            case Category::Ship:    return "ships";
            case Category::Drone:   return "drones";
        }
        return nullptr;
    }

    Run6 RunOne(const StaticData& data, Params p, uint64_t seed, int days, std::function<void(World&, int, int64_t)> shock)
    {
        Run6 out;
        p.seed = seed;
        p.pauseOnAuditResidual = false;
        std::chrono::steady_clock::time_point started(std::chrono::steady_clock::now());
        HarnessMarket market(data, p.salesTaxRate);
        World world(data, p, market);
        market.SetWorld(&world);
        // the manufacturers and inventors on the hubs' station lines (phase 5), as in the phase 3 and 4 tests
        HarnessCapacity lines(data.nodes, p);
        world.SetCapacity(lines.Capacity());
        world.SetDayHook([&out](const DayReport& r) {
            if ((r.audit.iskResidual != 0) or (r.audit.goodsResidualTypes != 0))
                ++out.residualDays;
            out.harvested += r.audit.goods[Goods::Harvested];
            out.extracted += r.audit.goods[Goods::Extracted];
            out.invented += r.audit.goods[Goods::Invented];
            out.fuel += r.audit.goods[Goods::Fuel];
        });
        world.Build(StartTime);
        world.Seed(StartTime);
        world.Start(StartTime, false);
        market.SetNow(StartTime);
        uint32_t hub(data.nodes[0].nodeID);
        for (int day = 0; day < days; ++day) {
            for (int tick = 0; tick < 96; ++tick) {
                int64_t t(StartTime + (int64_t)day * SecondsPerDay + tick * 900);
                if (tick == 0)
                    shock(world, day, t);
                market.SetNow(t);
                for (int i = 0; i < 100000; ++i) {
                    uint64_t before(world.CellVisits());
                    world.Step(t);
                    if ((world.Backlog(t) == 0) or (world.CellVisits() == before))
                        break;
                }
            }
            for (auto& c : world.Cells())
                if (c.nodeID == hub)
                    out.v[c.index].push_back(std::exp(c.lnV));
            for (auto& cur : world.GetLedger().Accounts())
                if ((cur.second.wallet < 0) or (cur.second.reserved < 0))
                    if (out.negatives++ == 0)
                        out.firstNegative = "day " + std::to_string(day) + ":  actor " + std::to_string(cur.first) + " money";
            for (auto& cur : world.GetLedger().Stocks())
                if ((cur.second.qty < 0) or (cur.second.reserved < 0))
                    if (out.negatives++ == 0)
                        out.firstNegative = "day " + std::to_string(day) + ":  actor " + std::to_string(Ledger::KeyActor(cur.first))
                                          + " stock of type " + std::to_string(Ledger::KeyType(cur.first));
        }
        // what was made, by how (World::Made is per type over all nodes:  each type once, at the supply of its first cell)
        std::set<uint16_t> seen;
        for (auto& c : world.Cells()) {
            if (!seen.insert(c.typeID).second)
                continue;
            std::map<uint16_t, Units>::const_iterator m = world.Made().find(c.typeID);
            if (m != world.Made().end())
                out.made[c.supply] += m->second;
        }
        out.inventions = world.Inventions();
        out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return out;
    }

    // the day's V of a group of cells, weighted, shocked against control
    double GroupRel(const Run6& s, const Run6& c, const std::vector<std::pair<uint32_t, double>>& group, int day)
    {
        double num(0.0), den(0.0);
        for (auto& g : group) {
            std::map<uint32_t, std::vector<double>>::const_iterator vs = s.v.find(g.first), vc = c.v.find(g.first);
            if ((vs == s.v.end()) or (vc == c.v.end()) or (day >= (int)vs->second.size()) or (day >= (int)vc->second.size()))
                continue;
            num += g.second * vs->second[day] / std::max(1e-12, vc->second[day]);
            den += g.second;
        }
        return (den > 0.0) ? num / den : 1.0;
    }

    double GroupPeak(const Run6& s, const Run6& c, const std::vector<std::pair<uint32_t, double>>& group, int from, int to, int* at)
    {
        double best(0.0);
        for (int d = from; d <= to; ++d) {
            double r(GroupRel(s, c, group, d));
            if (r > best) {
                best = r;
                *at = d;
            }
        }
        return best;
    }
}

int Phase6Main(const StaticData& data, const Params& p, const std::vector<uint64_t>& seeds, int days)
{
    int failures(0);
    if (days < ShockDay + MoonDays + 20)
        days = ShockDay + MoonDays + 20;
    std::printf("market sim phase 6 acceptance tests:  %d days, a moon material removed on day %d for %d days, %zu seed(s)\n", days,
                ShockDay, MoonDays, seeds.size());
    std::map<std::string, double> plan41 = { {"modules", 2.73}, {"charges", 2.26}, {"drones", 2.06}, {"ships", 1.44}, {"components", 1.28} };

    // the calibration's own medians:  refPrice over PLAN 4.1's bill at refPrice
    std::map<std::string, std::vector<double>> calRatios;
    for (auto& cur : data.types) {
        const char* cat(RatioCategory(cur.second));
        std::vector<std::pair<uint16_t, double>> bill;
        if ((cat == nullptr) or !PlanBill(data, cur.first, bill))
            continue;
        double cost(0.0);
        for (auto& in : bill) {
            const TypeData* it(data.Type(in.first));
            cost += in.second * ((it != nullptr) ? std::max(it->refPrice, it->basePrice) : 0.0);
        }
        if (cost > 0.0)
            calRatios[cat].push_back(cur.second.refPrice / cost);
    }

    /* the moon material to remove:  the raw one the most component value is made of (through the reactions), and what
     *  depends on it (the products of reactions that use it, again and again, the components of them, the T2 of those) */
    std::map<uint16_t, std::set<uint16_t>> madeOf;      // input -> what reactions make of it
    for (auto& r : data.recipes)
        if (r.kind == RecipeKind::Reaction)
            for (auto& in : r.inputs)
                for (auto& out : r.outputs)
                    madeOf[in.first].insert(out.first);
    auto downstream = [&](uint16_t raw) {
        std::set<uint16_t> seen, todo = { raw };
        while (!todo.empty()) {
            uint16_t x(*todo.begin());
            todo.erase(todo.begin());
            for (auto y : madeOf[x])
                if (seen.insert(y).second)
                    todo.insert(y);
        }
        return seen;
    };
    auto usesAny = [&](uint16_t typeID, const std::set<uint16_t>& of) {
        std::map<uint16_t, ChainBill>::const_iterator b = data.bills.find(typeID);
        if (b == data.bills.end())
            return false;
        for (auto& in : b->second.base)
            if (of.count(in.first) > 0)
                return true;
        for (auto& in : b->second.extra)
            if (of.count(in.first) > 0)
                return true;
        return false;
    };
    uint16_t moon(0);
    double moonValue(0.0);
    std::set<uint16_t> reacted, components, t2;
    for (auto& cur : data.types) {
        const TypeData& t(cur.second);
        if ((t.simClass != SimClass::Reaction) or (t.groupID != Chains::Group::MoonMaterials))
            continue;
        std::set<uint16_t> down(downstream(t.typeID));
        double value(0.0);
        for (auto& c : data.types)
            if ((c.second.simClass == SimClass::Component) and usesAny(c.first, down))
                value += c.second.refPrice * c.second.unitsPerDay;
        if (value > moonValue) {
            moonValue = value;
            moon = t.typeID;
        }
    }
    if (moon == 0) {
        std::printf("  [FAIL] the static data has no moon material that components are made of\n");
        return 1;
    }
    reacted = downstream(moon);
    for (auto& c : data.types)
        if ((c.second.simClass == SimClass::Component) and usesAny(c.first, reacted))
            components.insert(c.first);
    for (auto& c : data.types)
        if ((c.second.simClass == SimClass::MfgT2) and usesAny(c.first, components))
            t2.insert(c.first);
    std::printf("  moon material %u:  %zu reaction products, %zu components and %zu T2 types made of it\n", moon, reacted.size(),
                components.size(), t2.size());

    for (auto seed : seeds) {
        Run6 control(RunOne(data, p, seed, days, [](World&, int, int64_t) { }));
        Run6 removed(RunOne(data, p, seed, days, [moon](World& w, int day, int64_t t) {
            if (day == ShockDay)
                w.ShockSupply(moon, 0.0, MoonDays, t);
        }));
        std::printf("\nseed %llu:  %.0fs + %.0fs\n", (unsigned long long)seed, control.seconds, removed.seconds);

        // the first hub's cells of each group, weighted by their calibrated isk (the same world builds the same cells)
        HarnessMarket market(data, p.salesTaxRate);
        Params pp(p);
        pp.seed = seed;
        World layout(data, pp, market);
        layout.Build(StartTime);
        uint32_t hub(data.nodes[0].nodeID);
        std::vector<std::pair<uint32_t, double>> gMoon, gReacted, gComponents, gT2;
        std::map<std::string, std::vector<uint32_t>> ratioCells;
        for (auto& c : layout.Cells()) {
            if (c.nodeID != hub)
                continue;
            double w(std::max(1e-9, c.d0 * c.refLocal));
            if (c.typeID == moon)
                gMoon.push_back(std::make_pair(c.index, w));
            else if (reacted.count(c.typeID) > 0)
                gReacted.push_back(std::make_pair(c.index, w));
            else if (components.count(c.typeID) > 0)
                gComponents.push_back(std::make_pair(c.index, w));
            else if (t2.count(c.typeID) > 0)
                gT2.push_back(std::make_pair(c.index, w));
            const char* cat(RatioCategory(*data.Type(c.typeID)));
            if ((cat != nullptr) and (data.bills.count(c.typeID) > 0) and ((c.supply == Supply::Invented) or (c.supply == Supply::Manufactured)))
                ratioCells[cat].push_back(c.index);
        }

        // the moon removal
        int s(ShockDay), e(ShockDay + MoonDays + 10), atM(s), atR(s), atC(s), atT(s);
        double peakM(GroupPeak(removed, control, gMoon, s, e, &atM)), peakR(GroupPeak(removed, control, gReacted, s, e, &atR));
        double peakC(GroupPeak(removed, control, gComponents, s, e, &atC)), peakT(GroupPeak(removed, control, gT2, s, e, &atT));
        Check((peakR >= 1.05) and (peakC >= 1.03) and (peakT >= 1.005),
              F("moon removal:  %u itself x%.3f (day %+d);  its reaction products x%.3f (day %+d), the components x%.3f (day %+d), the T2"
                " x%.3f (day %+d) of the control (allowed:  products and components up 5%% and 3%% at least, T2 up 0.5%%)",
                moon, peakM, atM - s, peakR, atR - s, peakC, atC - s, peakT, atT - s), failures);
        double backC(0.0), backT(0.0);
        int n(0);
        for (int d = days - 5; d < days; ++d, ++n) {
            backC += GroupRel(removed, control, gComponents, d);
            backT += GroupRel(removed, control, gT2, d);
        }
        std::printf("  [info] %d days after the removal ended:  the components x%.3f, the T2 x%.3f of the control\n",
                    days - 3 - (ShockDay + MoonDays), backC / std::max(1, n), backT / std::max(1, n));

        // T2 price / bill at steady state
        for (auto& cur : plan41) {
            std::map<std::string, std::vector<uint32_t>>::const_iterator rc = ratioCells.find(cur.first);
            if ((rc == ratioCells.end()) or rc->second.empty()) {
                std::printf("  [skip] T2 %s:  none made at the first hub\n", cur.first.c_str());
                continue;
            }
            std::vector<double> daily;
            for (int d = SteadyFrom; d < days; ++d) {
                std::vector<double> r;
                for (auto idx : rc->second) {
                    const Cell& c(layout.Cells()[idx]);
                    std::vector<std::pair<uint16_t, double>> bill;
                    PlanBill(data, c.typeID, bill);
                    double cost(0.0);
                    bool ok(true);
                    for (auto& in : bill) {
                        Cell* ic(layout.FindCell(hub, in.first));
                        std::map<uint32_t, std::vector<double>>::const_iterator vi = (ic != nullptr) ? control.v.find(ic->index) : control.v.end();
                        if (vi != control.v.end()) {
                            cost += in.second * vi->second[d];
                        } else {
                            const TypeData* it(data.Type(in.first));
                            if ((it == nullptr) or !(std::max(it->refPrice, it->basePrice) > 0.0))
                                ok = false;
                            else
                                cost += in.second * std::max(it->refPrice, it->basePrice);
                        }
                    }
                    if (ok and (cost > 0.0))
                        r.push_back(control.v[idx][d] / cost);
                }
                if (!r.empty())
                    daily.push_back(Median(r));
            }
            double sim(Median(daily)), cal(Median(calRatios[cur.first]));
            Check(std::fabs(sim / cur.second - 1.0) <= 0.20,
                  F("T2 %s:  median price / bill %.3f at steady state (%zu cells);  PLAN 4.1 %.2f (x%.3f, allowed 20%%);  the calibration's %.3f",
                    cur.first.c_str(), sim, rc->second.size(), cur.second, sim / cur.second, cal), failures);
        }

        // invention
        const World::InventionStats& inv(control.inventions);
        double sigma(std::sqrt(std::max(1e-9, inv.variance)));
        double z(((double)inv.successes - inv.expected) / sigma);
        Check((inv.attempts > 0) and (std::fabs(z) <= 2.0),
              F("invention:  %lld successes of %lld attempts, %.1f expected at the configured chances (mean %.3f), sigma %.1f:  %+.2f sigma"
                " (allowed 2)", (long long)inv.successes, (long long)inv.attempts, inv.expected,
                inv.expected / std::max<double>(1.0, (double)inv.attempts), sigma, z), failures);

        // every chain ran
        Units invented(control.made[Supply::Invented]), comps(control.made[Supply::Manufactured]), react(control.made[Supply::Reacted]);
        Units processed(control.made[Supply::Processed]);
        Check((control.harvested > 0) and (control.extracted > 0) and (react > 0) and (processed > 0) and (invented > 0)
              and (control.invented > 0) and (control.fuel > 0),
              F("the chains ran:  harvested %lld, extracted %lld, reacted %lld, planetary goods %lld, T2 built %lld (of %lld copy runs"
                " invented), manufactured (T1, components, fuel blocks) %lld, fuel blocks burnt %lld", (long long)control.harvested,
                (long long)control.extracted, (long long)react, (long long)processed, (long long)invented, (long long)control.invented,
                (long long)comps, (long long)control.fuel), failures);

        // the books
        int negatives(control.negatives + removed.negatives);
        Check((control.residualDays == 0) and (removed.residualDays == 0) and (negatives == 0),
              F("books:  %d + %d days with an audit residual, %d negative money or stock%s%s", control.residualDays, removed.residualDays,
                negatives, negatives ? ", first " : "", negatives ? (!control.firstNegative.empty() ? control.firstNegative : removed.firstNegative).c_str() : ""),
              failures);
    }
    std::printf("\n%s (%d failure%s)\n", failures ? "PHASE 6 ACCEPTANCE TESTS FAILED" : "all phase 6 acceptance tests passed", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
