
 /**
  * @name Harness.cpp
  *   the offline harness (PLAN 11, phase 2):  runs the market sim against an in-memory market for a simulated year with
  *   scripted players, and checks the phase 2 acceptance tests
  *     (a) conservation:  the audit residual is exactly 0 cents and 0 units every day (and the ledger never goes negative,
  *         the reservations always match the live orders, the book matches the world)
  *     (b) stability:  the Laspeyres index of the top-500 isk basket stays in [0.8, 1.25] of day 0;  no cell sits at the
  *         Kmax clamp (V* at a bound, the sim's clampDays) for more than 3 consecutive days unless shocked;  V itself
  *         beyond the Kmax band is reported
  *     (c) money:  M/M* in [0.7, 1.4] after day 30
  *     (d) noise:  per category, the median volatility of the hub's daily average price is within x0.5..x2 of the
  *         calibrated volatility's median, both measured over the same 5-day horizon (HorizonVolatility, PLAN 4.1/4.4)
  *     (e) exploit bots:  a dumper earns no more than the intake caps allow;  a wash trader, a round-tripper, an
  *         insurance farmer and a reprocessing looper net <= 0
  *     (f) determinism:  the same seed gives identical audit csv
  *
  * @date:          13Oct26
  *
  */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "HarnessData.h"
#include "HarnessMarket.h"
#include "market/sim/SimEconomy.h"
#include "market/sim/SimWorld.h"

using namespace MktSim;
using namespace MktHarness;

namespace {

    const int64_t StartTime = 1325462400;       // 2012-01-02 00:00 UTC, a Monday in the calibration window
    const double MinTradesPerDay = 5.0;         // (d) compares cells with at least this many trades a day
    const int    VolatilityLag = (int)Econ::VolatilityHorizonDays;    // (d) the calibration's horizon (derive.py VOL_LAG)

    struct Options {
        std::string staticPath;                 // empty:  synthetic
        double      scale       = 1.0;
        int         days        = 365;
        std::vector<uint64_t> seeds;
        std::string scenario;
        std::string auditCsv;
        bool        bots        = true;
        bool        verbose     = false;
        uint16_t    trace       = 0;            // print this type's cell every day
        uint32_t    traceNode   = 1;            // at this node
        std::string exportCsv;                  // the first run's cells and audit at the end (World::ExportCsv)
        bool        phase6      = false;        // --phase6:  the phase 6 acceptance tests (Phase6Test.cpp) instead
        bool        actors      = false;        // print the phase 3 actors at the start and the end
        bool        phase3      = false;        // the phase 3 acceptance tests (Phase3Test.cpp) instead
    };

    struct ShockSpec {
        int    day;
        char   kind;
        uint32_t id;
        double mult;
        double days;
    };

    // a scenario file:  'key = value' lines (the Params names below) and 'shock = <day> <typeID|g:groupID|c:categoryID> <mult> <days>'
    bool ApplyScenario(const std::string& path, Params& p, Options& o, std::vector<ShockSpec>& shocks, std::string& err)
    {
        std::ifstream f(path.c_str());
        if (!f) {
            err = "can't open " + path;
            return false;
        }
        std::map<std::string, std::function<void(double)>> setters = {
            {"EconomyScale", [&](double v) { p.economyScale = v; }},
            {"QuoteLevels", [&](double v) { p.quoteLevels = (int)v; }},
            {"QuoteDepthDays", [&](double v) { p.quoteDepthDays = v; }},
            {"RequoteThreshold", [&](double v) { p.requoteThreshold = v; }},
            {"MinHalfSpread", [&](double v) { p.minHalfSpread = v; }},
            {"MaxPriceDeviation", [&](double v) { p.kMax = v; }},
            {"FairValueHalfLifeDays", [&](double v) { p.fairValueHalfLifeDays = v; }},
            {"MarketNoise", [&](double v) { p.marketNoise = v; }},
            {"PlayerTradeWeight", [&](double v) { p.playerTradeWeight = v; }},
            {"BuyBand", [&](double v) { p.buyBand = v; }},
            {"SellBand", [&](double v) { p.sellBand = v; }},
            {"IntakeShareCounterparty", [&](double v) { p.intakeShareCounterparty = v; }},
            {"MaxIskToCounterpartyPerDay", [&](double v) { p.maxIskToCounterpartyPerDay = v; }},
            {"IntakeDays", [&](double v) { p.intakeDays = v; }},
            {"SimPaysFees", [&](double v) { p.simPaysFees = (v != 0.0); }},
            {"MoneyDaysTarget", [&](double v) { p.moneyDaysTarget = v; }},
            {"ControllerKp", [&](double v) { p.controllerKp = v; }},
            {"ControllerKi", [&](double v) { p.controllerKi = v; }},
            {"SupplyFactor", [&](double v) { p.supplyFactor = v; }},
            {"HotCellMinutes", [&](double v) { p.hotCellMinutes = (int)v; }},
            {"ColdCellMinutes", [&](double v) { p.coldCellMinutes = (int)v; }},
            // phase 6
            {"EnableT2", [&](double v) { p.enableT2 = (v != 0.0); }},
            {"EnableMoonPI", [&](double v) { p.enableMoonPI = (v != 0.0); }},
            {"InventChance", [&](double v) { p.inventChance = v; }},
            {"HarvestSlack", [&](double v) { p.harvestSlack = v; }},
            {"PosFuelPerArrayHour", [&](double v) { p.posFuelPerArrayHour = v; }},
            {"Days", [&](double v) { o.days = (int)v; }},
        };
        std::string line;
        while (std::getline(f, line)) {
            size_t hash(line.find('#'));
            if (hash != std::string::npos)
                line.erase(hash);
            size_t eq(line.find('='));
            if (eq == std::string::npos)
                continue;
            std::string key(line.substr(0, eq)), val(line.substr(eq + 1));
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t\r") + 1);
            if (key == "shock") {
                std::istringstream in(val);
                ShockSpec s = ShockSpec();
                std::string what;
                in >> s.day >> what >> s.mult >> s.days;
                s.kind = 't';
                if ((what.size() > 2) and (what[1] == ':')) {
                    s.kind = what[0];
                    what.erase(0, 2);
                }
                s.id = (uint32_t)std::strtoul(what.c_str(), nullptr, 10);
                shocks.push_back(s);
                continue;
            }
            std::map<std::string, std::function<void(double)>>::iterator itr = setters.find(key);
            if (itr == setters.end()) {
                err = "unknown scenario key '" + key + "'";
                return false;
            }
            itr->second(std::strtod(val.c_str(), nullptr));
        }
        return true;
    }

    struct BotResult {
        const char* name;
        double net         = 0.0;   // isk over the run, goods left valued at the sim's bid
        double worstDay    = 0.0;   // dumper:  the largest day's revenue against its cap
        uint64_t trades    = 0;
    };

    struct RunResult {
        uint64_t seed               = 0;
        std::string auditCsv;
        // (a)
        int      residualDays       = 0;
        int      invariantFailures  = 0;
        uint64_t violations         = 0;
        std::string firstProblem;
        // (b)
        double   indexMin           = 1.0;
        double   indexMax           = 1.0;
        int      maxClampDays       = 0;    // consecutive day-ends with V* at a Kmax bound (a stock-out or a glut)
        std::string clampWhere;
        int      cellsOverClamp     = 0;
        int      maxVClampDays      = 0;    // the same for V beyond the Kmax band (info)
        std::string vClampWhere;
        int      cellsOverVClamp    = 0;
        // (c)
        double   moneyMin           = 1e9;
        double   moneyMax           = 0.0;
        // (d)
        struct Noise { double realised; double calibrated; size_t cells; };
        std::map<std::string, Noise> noise;    // category -> medians over the compared cells
        // (e)
        std::vector<BotResult> bots;
        // coverage
        uint64_t playerTrades       = 0;
        uint64_t cellVisits         = 0;
        uint64_t maxBacklog         = 0;    // seconds the due queue was behind after a tick's steps
        int      simDays            = 0;    // days the sim's books closed
        uint64_t orderWrites        = 0;
        size_t   liveOrders         = 0;
        double   seconds            = 0.0;
        double   undercutFilled     = 0.0;
        double   bidderFilled       = 0.0;
    };

    double Median(std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        std::sort(v.begin(), v.end());
        size_t n(v.size());
        return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    }

    // the calibration's volatility estimator (utils/market-calibration/derive.py horizon_volatility):  1.4826 x the
    //  median absolute deviation of ln(p[d + lag] / p[d]) over every pair of traded days 'lag' days apart, divided by
    //  sqrt(lag).  -1 with fewer than minPairs pairs.  Over a few days the bid/ask bounce of a day's few prints and the
    //  sticky asks of the real hub (unchanged on most days) both fade, leaving how far the price level moves
    double HorizonVolatility(const std::vector<std::pair<int32_t, double>>& series, int lag, size_t minPairs)
    {
        std::map<int32_t, double> at(series.begin(), series.end());
        std::vector<double> r;
        for (auto& cur : series) {
            std::map<int32_t, double>::const_iterator later = at.find(cur.first + lag);
            if (later != at.end())
                r.push_back(std::log(later->second / cur.second));
        }
        if (r.size() < minPairs)
            return -1.0;
        double med(Median(r));
        std::vector<double> dev;
        for (auto x : r)
            dev.push_back(std::fabs(x - med));
        return 1.4826 * Median(dev) / std::sqrt((double)lag);
    }

    const char* NoiseCategory(const TypeData& t)
    {
        switch (t.categoryID) {
            case Category::Ship:        return "Ships";
            case Category::Material:
            case Category::Asteroid:    return "Materials";
            case Category::PIResource:
            case Category::PICommodity: return "PI";
            case Category::Implant:     return "Implants";
            case Category::Module:      return "Modules";
            case Category::Commodity:   return "Commodities";
            case Category::Charge:      return "Charges";
        }
        return nullptr;
    }

    // reservations behind the live orders must be exactly the ledger's reserved columns
    int CheckInvariants(World& w, std::string& first)
    {
        int bad(0);
        std::map<uint32_t, Cents> isk;
        std::map<Ledger::StockKey, Units> qty;
        for (auto& cur : w.Orders()) {
            const SimOrder& o(cur.second);
            if (o.cell == VendorCell)
                continue;
            const Cell& c(w.Cells()[o.cell]);
            if (o.bid)
                isk[o.actorID] += o.price * (Cents)o.qty;
            else
                qty[Ledger::Key(o.actorID, (uint16_t)c.nodeID, c.typeID)] += o.qty;
        }
        for (auto& cur : w.GetLedger().Accounts()) {
            if ((cur.second.wallet < 0) or (cur.second.reserved < 0) or (cur.second.reserved != isk[cur.first])) {
                if (bad++ == 0)
                    first = "actor " + std::to_string(cur.first) + " reserved " + std::to_string(cur.second.reserved)
                          + " but its bids hold " + std::to_string(isk[cur.first]) + " (wallet " + std::to_string(cur.second.wallet) + ")";
            }
        }
        for (auto& cur : w.GetLedger().Stocks()) {
            if ((cur.second.qty < 0) or (cur.second.reserved < 0) or (cur.second.reserved != qty[cur.first])) {
                if (bad++ == 0)
                    first = "stock of actor " + std::to_string(Ledger::KeyActor(cur.first)) + " type " + std::to_string(Ledger::KeyType(cur.first))
                          + " reserved " + std::to_string(cur.second.reserved) + " but its asks hold " + std::to_string(qty[cur.first]);
            }
        }
        return bad;
    }

    RunResult Run(const StaticData& data, Params p, uint64_t seed, const Options& opt, const std::vector<ShockSpec>& shocks)
    {
        RunResult res;
        res.seed = seed;
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
        auto printActors = [&](const char* when) {
            if (!opt.actors)
                return;
            std::printf("  actors %s:\n%s", when, world.ActorList(0).c_str());
            for (auto& a : world.Actors())
                if ((a.kind == ActorKind::Miner) or (a.kind == ActorKind::Manufacturer) or (a.kind >= ActorKind::Inventor))
                    std::printf("%s", world.ActorInfo(a.actorID).c_str());
        };
        printActors("at the start");

        std::ostringstream csv;
        int dayNo(0);
        std::vector<double> v0;              // day 0 values of the index basket
        std::vector<uint32_t> basket;
        {
            std::vector<std::pair<double, uint32_t>> order;
            for (auto& c : world.Cells())
                order.push_back(std::make_pair(c.d0 * c.refLocal, c.index));
            std::sort(order.begin(), order.end(), [](const std::pair<double, uint32_t>& a, const std::pair<double, uint32_t>& b) {
                return (a.first != b.first) ? (a.first > b.first) : (a.second < b.second);
            });
            for (size_t i = 0; (i < order.size()) and (i < 500); ++i)
                basket.push_back(order[i].second);
        }

        world.SetDayHook([&](const DayReport& rep) {
            for (auto& line : world.AuditLines())
                csv << line.day << "," << line.category << "," << line.amount << "\n";
            if ((rep.audit.iskResidual != 0) or (rep.audit.goodsResidualTypes != 0)) {
                if (res.residualDays++ == 0)
                    res.firstProblem = "day " + std::to_string(rep.day) + ":  isk residual " + std::to_string(rep.audit.iskResidual)
                                     + ", goods residual in " + std::to_string(rep.audit.goodsResidualTypes) + " types";
            }
            ++dayNo;
            double m(rep.moneyTarget > 0.0 ? (double)rep.money / rep.moneyTarget : 0.0);
            if (dayNo > 30) {
                res.moneyMin = std::min(res.moneyMin, m);
                res.moneyMax = std::max(res.moneyMax, m);
            }
            if (opt.verbose)
                std::printf("  day %3d  M/M* %.3f  income x%.3f  orders %u  clamped %u  residual %lld/%zu\n", dayNo, m, rep.incomeFactor,
                            rep.liveOrders, rep.clampedCells, (long long)rep.audit.iskResidual, rep.audit.goodsResidualTypes);
        });

        // daily average prices of the first node (the Forge)
        uint32_t forge(data.nodes[0].regionID), jita(data.nodes[0].stationID);
        std::map<uint16_t, std::vector<std::pair<int32_t, double>>> series;
        std::map<uint16_t, uint64_t> seriesTrades;

        // the scripted players
        enum { Dumper = 101, RoundTrip = 102, WashA = 103, WashB = 104, Reprocess = 105, Insurance = 106, Undercut = 107, Bidder = 108 };
        std::vector<uint16_t> modules, ships, refinables;
        uint16_t dumpType(0);
        double dumpBest(0.0);
        // the bots trade the hub's busiest types of each kind
        std::vector<const Cell*> hub;
        for (auto& c : world.Cells())
            if (c.nodeID == data.nodes[0].nodeID)
                hub.push_back(&c);
        std::sort(hub.begin(), hub.end(), [](const Cell* a, const Cell* b) {
            return (a->dc != b->dc) ? (a->dc > b->dc) : (a->typeID < b->typeID);
        });
        for (auto c : hub) {
            const TypeData& t(*data.Type(c->typeID));
            if ((t.categoryID == Category::Material) and (c->d0 * c->refLocal > dumpBest)) {
                dumpBest = c->d0 * c->refLocal;
                dumpType = c->typeID;
            }
            if ((t.simClass == SimClass::MfgT1) and (t.categoryID == Category::Module) and (modules.size() < 6))
                modules.push_back(c->typeID);
            if ((t.simClass == SimClass::MfgT1) and (t.categoryID == Category::Ship) and (ships.size() < 4))
                ships.push_back(c->typeID);
            if (!t.refine.empty() and (t.categoryID != Category::Material) and (refinables.size() < 8))
                refinables.push_back(c->typeID);
        }
        BotResult dumper{"dumper"}, roundTrip{"round-tripper"}, wash{"wash trader"}, reproc{"reprocessing looper"}, insure{"insurance farmer"};
        double dumpCapWorst(0.0);
        double undercutSold(0.0), undercutListed(0.0), bidderBought(0.0), bidderBid(0.0);

        /* (b) counts the day-ends a cell's V* sits at a Kmax bound (the sim's clampDays).  V itself beyond the Kmax band
         *  (PLAN 11's literal wording) is reported as info:  V may wander to Kmax^2, and the high-volatility loot does */
        std::vector<int> vClamp(world.Cells().size(), 0);
        const double lnKmax(std::log(p.kMax));
        size_t shockIdx(0);
        for (int day = 0; day < opt.days; ++day) {
            for (; shockIdx < shocks.size() and (shocks[shockIdx].day <= day); ++shockIdx)
            {
                const ShockSpec& s(shocks[shockIdx]);
                int64_t when(StartTime + (int64_t)day * SecondsPerDay);
                // phase 3:  l:<hullTypeID> ship losses, m:0 the miners' fleets
                if (s.kind == 'l')
                    world.ShockLosses((uint16_t)s.id, s.mult, s.days, when);
                else if (s.kind == 'm')
                    world.ShockMining(s.mult, s.days, when);
                else
                    world.Shock(s.kind, s.id, s.mult, s.days, when);
            }
            for (int tick = 0; tick < 96; ++tick) {
                int64_t t(StartTime + (int64_t)day * SecondsPerDay + tick * 900);
                market.SetNow(t);
                /* the server steps every tick (MarketSimMgr::Process), so nothing due stays behind.  One step per 15
                 *  minutes, capped at maxOrderOpsPerStep, falls behind on the full calibration (about 400k order ops a
                 *  day):  the harness then simulated about half a day per day, and 120 'days' were 58.  Step until
                 *  the queue has caught up with t */
                for (int i = 0; i < 100000; ++i) {
                    uint64_t before(world.CellVisits());
                    world.Step(t);
                    if ((world.Backlog(t) == 0) or (world.CellVisits() == before))
                        break;
                }
                res.maxBacklog = std::max<uint64_t>(res.maxBacklog, world.Backlog(t));
                if ((tick == 48) and opt.bots) {
                    Player& before(market.GetPlayer(Dumper));
                    // (e) dumper:  ten days of demand into the sim's bids, at any price
                    if (dumpType != 0) {
                        Cell* c(world.FindCell(data.nodes[0].nodeID, dumpType));
                        Units q((Units)std::ceil(10.0 * c->dBar));
                        before.goods[dumpType] += q;
                        Cents was(before.fromSim);
                        Units sold(market.Sell(Dumper, jita, dumpType, q, 1));
                        Cents got(market.GetPlayer(Dumper).fromSim - was);
                        double cap(p.maxIskToCounterpartyPerDay * world.MoneyTarget());
                        double unitCap(p.intakeShareCounterparty * std::max(c->dBar, (double)c->lot) + c->lot);
                        dumpCapWorst = std::max(dumpCapWorst, std::max((double)got / cap, (double)sold / unitCap));
                        dumper.trades += (sold > 0);
                        dumper.net += Econ::ToIsk(got);
                        // the rest stays outside (it was never the sim's)
                        market.GetPlayer(Dumper).goods[dumpType] = 0;
                    }
                    // round-tripper:  buy a lot at the sim's ask, sell it straight back into its bids
                    for (auto typeID : modules) {
                        Cents ask(market.SimAsk(jita, typeID));
                        if (ask == 0)
                            continue;
                        Units got(market.Buy(RoundTrip, jita, typeID, 1, ask));
                        if (got > 0)
                            market.Sell(RoundTrip, jita, typeID, got, 1);
                        roundTrip.trades += (got > 0);
                    }
                    // wash trader:  two alts trade one module between them at twice the price, then sell to the sim
                    if (!modules.empty()) {
                        uint16_t typeID(modules[day % modules.size()]);
                        Cents ask(market.SimAsk(jita, typeID));
                        if ((ask > 0) and (market.Buy(WashA, jita, typeID, 1, ask) > 0)) {
                            market.Place(WashA, jita, typeID, false, ask * 2, 1);
                            market.Buy(WashB, jita, typeID, 1, ask * 2);
                            market.Place(WashB, jita, typeID, false, ask * 3, 1);
                            market.Buy(WashA, jita, typeID, 1, ask * 3);
                            Player& a(market.GetPlayer(WashA));
                            market.Sell(WashA, jita, typeID, a.goods[typeID], 1);
                            ++wash.trades;
                        }
                        market.CancelPlayerOrders(WashA);
                        market.CancelPlayerOrders(WashB);
                    }
                    // reprocessing looper:  buy at the sim's ask, refine at perfect yield, sell the minerals to its bids
                    for (auto typeID : refinables) {
                        Cents ask(market.SimAsk(jita, typeID));
                        if (ask == 0)
                            continue;
                        if (market.Buy(Reprocess, jita, typeID, 1, ask) == 0)
                            continue;
                        Player& me(market.GetPlayer(Reprocess));
                        me.goods[typeID] -= 1;
                        for (auto& out : data.Type(typeID)->refine) {
                            Units q((Units)std::floor(out.second));
                            me.goods[out.first] += q;
                            market.Sell(Reprocess, jita, out.first, me.goods[out.first], 1);
                        }
                        ++reproc.trades;
                    }
                    // insurance farmer:  buy a ship, insure it platinum, lose it
                    for (auto typeID : ships) {
                        Cents ask(market.SimAsk(jita, typeID));
                        if ((ask == 0) or (market.Buy(Insurance, jita, typeID, 1, ask) == 0))
                            continue;
                        Player& me(market.GetPlayer(Insurance));
                        me.goods[typeID] -= 1;
                        me.wallet += Econ::ToCents(market.InsuranceNet(typeID));
                        ++insure.trades;
                    }
                    // honest players:  an ask a cent under the sim's (consumers should lift it), a bid a little over the
                    //  sim's (the sim should sell into it)
                    if (!modules.empty()) {
                        uint16_t typeID(modules[(day + 1) % modules.size()]);
                        Player& u(market.GetPlayer(Undercut));
                        Cents ask(market.SimAsk(jita, typeID));
                        Cell* c(world.FindCell(data.nodes[0].nodeID, typeID));
                        if ((ask > 1) and (c != nullptr)) {
                            Units q(std::max<Units>(1, (Units)std::ceil(c->dBar * 0.2)));
                            u.goods[typeID] += q;
                            undercutListed += (double)q;
                            Units had(u.goods[typeID]);
                            market.Place(Undercut, jita, typeID, false, ask - 1, q);
                            (void)had;
                        }
                        uint16_t bidType(modules[(day + 2) % modules.size()]);
                        Cents bid(market.SimBid(jita, bidType));
                        Cell* bc(world.FindCell(data.nodes[0].nodeID, bidType));
                        if ((bid > 0) and (bc != nullptr)) {
                            Cents price((Cents)std::ceil(std::exp(bc->lnV) * (1.0 - bc->h / 4.0) * 100.0));
                            Units q(std::max<Units>(1, (Units)std::ceil(bc->dBar * 0.1)));
                            bidderBid += (double)q;
                            market.Place(Bidder, jita, bidType, true, price, q);
                        }
                    }
                }
                if ((tick == 95) and opt.bots) {
                    // the day's leftovers of the honest players are withdrawn
                    Player& u(market.GetPlayer(Undercut));
                    Units before(0);
                    for (auto& g : u.goods)
                        before += g.second;
                    market.CancelPlayerOrders(Undercut);
                    Units after(0);
                    for (auto& g : u.goods)
                        after += g.second;
                    undercutSold += 0.0 * (double)(after - before);
                    market.CancelPlayerOrders(Bidder);
                }
            }

            // end of the day:  the day's prices, the index, the clamp, the books
            std::vector<HistoryRow> rows;
            world.TakeFinishedHistory(rows);
            for (auto& r : rows)
                if ((r.regionID == forge) and (r.volume > 0))
                {
                    series[r.typeID].push_back(std::make_pair(r.day, (double)r.sumValue / (double)r.volume / 100.0));
                    seriesTrades[r.typeID] += r.trades;
                }
            double num(0.0), den(0.0);
            if (v0.empty())
                for (auto idx : basket)
                    v0.push_back(std::exp(world.Cells()[idx].lnV));
            for (size_t i = 0; i < basket.size(); ++i) {
                const Cell& c(world.Cells()[basket[i]]);
                num += std::exp(c.lnV) * c.d0;
                den += v0[i] * c.d0;
            }
            double index(den > 0.0 ? num / den : 1.0);
            if (opt.trace != 0) {
                Cell* tc(world.FindCell(opt.traceNode, opt.trace));
                if (tc != nullptr) {
                    const Ledger::Stock* s(world.GetLedger().FindInv(tc->mm, (uint16_t)tc->nodeID, tc->typeID));
                    double avg(0.0);
                    Units vol(0);
                    for (auto& r : rows)
                        if ((r.regionID == forge) and (r.typeID == opt.trace)) {
                            avg = (double)r.sumValue / (double)r.volume / 100.0;
                            vol = r.volume;
                        }
                    std::printf("  trace day %3d  V %.4f  V* %.4f  anchor %.4f  stock %lld (%lld on asks) of %.0f  dBar %.1f d0 %.1f  avg %.4f vol %lld  clamp %u"
                                "  lossWant %.2f (rate %.3f, drops %.3f, rest %.3f)\n",
                                day, std::exp(tc->lnV), tc->vStar, std::exp(tc->lnAnchor), (long long)(s ? s->qty + s->reserved : 0), (long long)(s ? s->reserved : 0),
                                tc->targetStock, tc->dBar, tc->d0, avg, (long long)vol, tc->clampDays, tc->lossWant, tc->lossRate, tc->dropRate, tc->dcGen);
                    // phase 6:  how a chain makes it, and its inputs at the node
                    std::printf("%s", world.ChainCellInfo(*tc).c_str());
                }
            }
            res.indexMin = std::min(res.indexMin, index);
            res.indexMax = std::max(res.indexMax, index);
            if (opt.verbose) {
                // the basket's biggest movers against day 0
                double worst(1.0);
                uint16_t worstType(0);
                for (size_t i = 0; i < basket.size(); ++i) {
                    const Cell& c(world.Cells()[basket[i]]);
                    double r(std::exp(c.lnV) / v0[i]);
                    if (std::fabs(std::log(r)) > std::fabs(std::log(worst))) {
                        worst = r;
                        worstType = c.typeID;
                    }
                }
                std::printf("  day %3d  price index %.4f  (biggest mover type %u x%.3f)\n", day, index, worstType, worst);
            }
            auto where = [&](const Cell& c) {
                const TypeData& ct(*data.Type(c.typeID));
                return "type " + std::to_string(c.typeID) + " (class " + std::to_string(ct.simClass) + ", category "
                     + std::to_string(ct.categoryID) + ") at node " + std::to_string(c.nodeID) + " on day " + std::to_string(day)
                     + ", demand " + std::to_string(c.dBar) + "/day, target stock " + std::to_string(c.targetStock);
            };
            for (size_t i = 0; i < world.Cells().size(); ++i) {
                const Cell& c(world.Cells()[i]);
                // V* at a Kmax bound:  a stock-out (or a glut) at the day's last visit
                if (c.clampDays > res.maxClampDays) {
                    res.maxClampDays = c.clampDays;
                    res.clampWhere = where(c);
                }
                // V itself beyond the band (info)
                int& n(vClamp[i]);
                n = (!c.shocked and (std::fabs(c.lnV - c.lnAnchor) >= lnKmax - 1e-9)) ? n + 1 : 0;
                if (n > res.maxVClampDays) {
                    res.maxVClampDays = n;
                    res.vClampWhere = where(c);
                }
            }
            std::string problem;
            int bad(CheckInvariants(world, problem));
            if (bad > 0) {
                if (res.invariantFailures++ == 0 and res.firstProblem.empty())
                    res.firstProblem = "day " + std::to_string(day) + ":  " + problem;
            }
            if (market.CheckMirror(problem) > 0) {
                if (res.invariantFailures++ == 0 and res.firstProblem.empty())
                    res.firstProblem = "day " + std::to_string(day) + ":  book and world differ at " + problem;
            }
        }
        for (auto& c : world.Cells())
            if (c.clampDays > 3)
                ++res.cellsOverClamp;
        for (auto n : vClamp)
            if (n > 3)
                ++res.cellsOverVClamp;
        res.violations = world.GetLedger().Violations();

        // (d) noise per category:  the volatility of the hub's daily average, measured as the calibration measures it
        std::map<std::string, std::vector<double>> realised, calibrated;
        for (auto& cur : series) {
            const TypeData& t(*data.Type(cur.first));
            const char* cat(NoiseCategory(t));
            // a daily average needs trades to average:  at EconomyScale 0.01 a typical ship sells a few times a
            //  week at a hub, and its 'daily average' is one print at the ask or the bid.  only cells with at least
            //  MinTradesPerDay internal trades on their trading days are compared
            if ((cat == nullptr) or (cur.second.size() < 30)
            or ((double)seriesTrades[cur.first] / (double)cur.second.size() < MinTradesPerDay))
                continue;
            double v(HorizonVolatility(cur.second, VolatilityLag, 20));
            if (v < 0.0)
                continue;
            realised[cat].push_back(v);
            calibrated[cat].push_back(t.volatility);
        }
        for (auto& cur : realised)
            if (cur.second.size() >= 3)
            res.noise[cur.first] = RunResult::Noise{Median(cur.second), Median(calibrated[cur.first]), cur.second.size()};

        // (e) the bots' net:  isk, plus what they hold at the sim's bid
        auto net = [&](std::initializer_list<uint32_t> ids) {
            double total(0.0);
            for (auto id : ids) {
                Player& pl(market.GetPlayer(id));
                total += Econ::ToIsk(pl.wallet);
                for (auto& g : pl.goods)
                    if (g.second > 0)
                        total += Econ::ToIsk(market.SimBid(jita, g.first)) * (double)g.second;
            }
            return total;
        };
        dumper.worstDay = dumpCapWorst;
        roundTrip.net = net({RoundTrip});
        wash.net = net({WashA, WashB});
        reproc.net = net({Reprocess});
        insure.net = net({Insurance});
        res.bots = {dumper, roundTrip, wash, reproc, insure};
        Player& u(market.GetPlayer(Undercut));
        res.undercutFilled = (undercutListed > 0.0) ? (double)u.fromSim : 0.0;
        res.bidderFilled = (double)market.GetPlayer(Bidder).toSim;
        (void)undercutSold;
        (void)bidderBought;

        printActors("at the end");
        if (opt.actors) {
            {
                std::map<uint16_t, double> expect;
                for (auto& c : world.Cells())
                    if (c.supply == Supply::Manufactured)
                        expect[c.typeID] += std::max(0.0, c.dcGen + c.lossRate + c.minerRate - c.dropRate);
                double sumE(0.0), sumM(0.0);
                for (auto& e : expect) {
                    std::map<uint16_t, Units>::const_iterator m = world.Made().find(e.first);
                    double made((m == world.Made().end()) ? 0.0 : (double)m->second / opt.days);
                    const TypeData& t(*data.Type(e.first));
                    sumE += e.second * t.refPrice;
                    sumM += made * t.refPrice;
                    if (t.categoryID == Category::Ship)
                        std::printf("    ship %u:  expected %.3g/day, made %.3g/day\n", e.first, e.second, made);
                }
                std::printf("  T1 made / expected (isk):  %.3f\n", sumM / std::max(1.0, sumE));
            }
            {
                // phase 6:  the chains' goods, what the reference says the sim makes and uses a day (all nodes) against
                //  what it made and its actors bought
                std::map<uint16_t, std::pair<double, double>> ref;      // type -> (made, used)
                for (auto& c : world.Cells())
                    if ((c.supply >= Supply::Invented) or (c.simClass == SimClass::Component)) {
                        ref[c.typeID].first += world.RefMade(c.nodeID, c.typeID);
                        ref[c.typeID].second += world.RefDerived(c.nodeID, c.typeID);
                    }
                // the first hub's chain cells that are sold out at the end, and why (at most 12)
                int shown(0);
                for (auto& c : world.Cells()) {
                    if ((c.nodeID != data.nodes[0].nodeID) or (c.supply < Supply::Invented) or (shown >= 12))
                        continue;
                    const Ledger::Stock* s(world.GetLedger().FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
                    if ((s != nullptr) and ((s->qty + s->reserved) > 0))
                        continue;
                    std::printf("  sold out:  %s", world.CellInfo(c.typeID, data.nodes[0].stationID).c_str());
                    ++shown;
                }
                std::printf("  chains per day over the run:  reference made / used, made, bought by actors\n");
                for (auto& r : ref) {
                    std::map<uint16_t, Units>::const_iterator m = world.Made().find(r.first), b = world.ActorBought().find(r.first);
                    std::printf("    type %u:  ref %.4g / %.4g, made %.4g, bought %.4g\n", r.first, r.second.first, r.second.second,
                                (m == world.Made().end()) ? 0.0 : (double)m->second / opt.days, (b == world.ActorBought().end()) ? 0.0 : (double)b->second / opt.days);
                }
            }
            std::printf("  per day over the run:  bought by actors (materials, ships), made\n");
            for (auto& cur : world.ActorBought()) {
                const TypeData* t(data.Type(cur.first));
                if ((t == nullptr) or ((t->categoryID != Category::Material) and (t->categoryID != Category::Ship)))
                    continue;
                std::map<uint16_t, Units>::const_iterator made = world.Made().find(cur.first);
                std::printf("    type %u:  bought %.4g, made %.4g\n", cur.first, (double)cur.second / opt.days,
                            (made == world.Made().end()) ? 0.0 : (double)made->second / opt.days);
            }
        }
        res.playerTrades = world.TradesWithPlayers();
        res.cellVisits = world.CellVisits();
        res.simDays = dayNo;
        res.orderWrites = market.Writes();
        res.liveOrders = world.LiveOrders();
        if (!opt.exportCsv.empty())
            world.ExportCsv(opt.exportCsv);
        res.auditCsv = csv.str();
        res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        return res;
    }

    bool Check(bool ok, const char* what, int& failures)
    {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++failures;
        return ok;
    }

    void Usage()
    {
        std::printf("usage: eve-market-sim [--static export.json | --synthetic [scale]] [--days N] [--seed N]... [--seeds N]\n"
                    "                      [--scenario file] [--audit-csv file] [--no-bots] [--verbose] [--actors] [--phase3]\n"
                    "       eve-market-sim --test <name>\n");
    }
}

int Phase3Main(const StaticData& data, const Params& p, const std::vector<uint64_t>& seeds, int days);

// phase 6 (Phase6Test.cpp)
int Phase6Main(const StaticData& data, const Params& p, const std::vector<uint64_t>& seeds, int days);
int HarnessMain(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--static")
            opt.staticPath = next();
        else if (a == "--synthetic") {
            if ((i + 1 < argc) and (argv[i + 1][0] != '-'))
                opt.scale = std::strtod(next(), nullptr);
        } else if (a == "--days")
            opt.days = std::atoi(next());
        else if (a == "--seed")
            opt.seeds.push_back(std::strtoull(next(), nullptr, 10));
        else if (a == "--seeds") {
            int n(std::atoi(next()));
            for (int k = 1; k <= n; ++k)
                opt.seeds.push_back((uint64_t)k);
        } else if (a == "--scenario")
            opt.scenario = next();
        else if (a == "--audit-csv")
            opt.auditCsv = next();
        else if (a == "--no-bots")
            opt.bots = false;
        else if (a == "--verbose")
            opt.verbose = true;
        else if (a == "--trace")
            opt.trace = (uint16_t)std::atoi(next());
        else if (a == "--trace-node")
            opt.traceNode = (uint32_t)std::atoi(next());
        else if (a == "--export")
            opt.exportCsv = next();
        else if (a == "--actors")
            opt.actors = true;
        else if (a == "--phase6")
            opt.phase6 = true;
        else if (a == "--phase3")
            opt.phase3 = true;
        else {
            Usage();
            return 2;
        }
    }
    if (opt.seeds.empty())
        opt.seeds.push_back(1);

    Params p;
    std::vector<ShockSpec> shocks;
    std::string err;
    if (!opt.scenario.empty() and !ApplyScenario(opt.scenario, p, opt, shocks, err)) {
        std::printf("scenario:  %s\n", err.c_str());
        return 2;
    }
    StaticData data;
    if (!opt.staticPath.empty()) {
        if (!LoadExport(opt.staticPath, data, err)) {
            std::printf("static data:  %s\n", err.c_str());
            return 2;
        }
    } else {
        BuildSynthetic(data, 20111129, opt.scale);
    }
    std::vector<std::string> problems;
    data.Validate(problems);
    for (auto& s : problems)
        std::printf("static data:  %s\n", s.c_str());
    std::printf("market sim harness:  %zu types, %zu nodes, %zu vendor offers, %d days, %zu seed(s), EconomyScale %g\n",
                data.types.size(), data.nodes.size(), data.vendors.size(), opt.days, opt.seeds.size(), p.economyScale);

    if (opt.phase6)
        return Phase6Main(data, p, opt.seeds, opt.days);
    if (opt.phase3)
        return Phase3Main(data, p, opt.seeds, opt.days);

    int failures(0);
    std::vector<RunResult> results;
    for (auto seed : opt.seeds) {
        RunResult r(Run(data, p, seed, opt, shocks));
        std::printf("\nseed %llu:  %.1fs, %d days closed (queue at most %llus behind), %llu cell visits, %llu order writes, %zu live orders at the end, %llu trades with players\n",
                    (unsigned long long)seed, r.seconds, r.simDays, (unsigned long long)r.maxBacklog, (unsigned long long)r.cellVisits,
                    (unsigned long long)r.orderWrites, r.liveOrders, (unsigned long long)r.playerTrades);
        if (!r.firstProblem.empty())
            std::printf("  first problem:  %s\n", r.firstProblem.c_str());
        char buf[1024];
        std::snprintf(buf, sizeof(buf), "(a) conservation:  %d days with a residual, %d invariant failures, %llu ledger violations",
                      r.residualDays, r.invariantFailures, (unsigned long long)r.violations);
        Check((r.residualDays == 0) and (r.invariantFailures == 0) and (r.violations == 0), buf, failures);
        std::snprintf(buf, sizeof(buf), "(b) stability:  price index in [%.3f, %.3f] of day 0 (allowed [0.8, 1.25]);  longest clamp %d days, %d cells over 3",
                      r.indexMin, r.indexMax, r.maxClampDays, r.cellsOverClamp);
        if (r.maxClampDays > 3)
            std::printf("  longest clamp:  %s\n", r.clampWhere.c_str());
        Check((r.indexMin >= 0.8) and (r.indexMax <= 1.25) and (r.cellsOverClamp == 0) and (shocks.empty() ? (r.maxClampDays <= 3) : true), buf, failures);
        std::printf("  [info] V itself beyond the Kmax band (it may go to Kmax^2):  longest %d days, %d cells over 3 at the end%s%s\n",
                    r.maxVClampDays, r.cellsOverVClamp, (r.maxVClampDays > 3) ? ":  " : "", (r.maxVClampDays > 3) ? r.vClampWhere.c_str() : "");
        if (opt.days > 30) {
            std::snprintf(buf, sizeof(buf), "(c) money:  M/M* in [%.3f, %.3f] after day 30 (allowed [0.7, 1.4])", r.moneyMin, r.moneyMax);
            Check((r.moneyMin >= 0.7) and (r.moneyMax <= 1.4), buf, failures);
        }
        bool noiseOk(!r.noise.empty());
        std::string noise;
        for (auto& cur : r.noise) {
            double ratio(cur.second.calibrated > 0.0 ? cur.second.realised / cur.second.calibrated : 0.0);
            std::snprintf(buf, sizeof(buf), " %s %.3f/%.3f=x%.2f (%zu)", cur.first.c_str(), cur.second.realised, cur.second.calibrated,
                          ratio, cur.second.cells);
            noise += buf;
            if ((ratio < 0.5) or (ratio > 2.0))
                noiseOk = false;
        }
        std::snprintf(buf, sizeof(buf), "(d) noise (realised/calibrated %d-day volatility, cells, allowed x0.5..x2):%s", VolatilityLag, noise.c_str());
        Check(noiseOk, buf, failures);
        if (opt.bots) {
            for (auto& b : r.bots) {
                if (std::strcmp(b.name, "dumper") == 0) {
                    std::snprintf(buf, sizeof(buf), "(e) %s:  %.0f ISK over %llu days of dumping;  worst day at %.2f of its intake cap",
                                  b.name, b.net, (unsigned long long)b.trades, b.worstDay);
                    Check(b.worstDay <= 1.0 + 1e-9, buf, failures);
                } else {
                    std::snprintf(buf, sizeof(buf), "(e) %s:  net %.0f ISK over %llu rounds (%.0f ISK/day)", b.name, b.net,
                                  (unsigned long long)b.trades, b.net / std::max(1, opt.days));
                    Check(b.net <= 0.0, buf, failures);
                }
            }
            std::printf("  [info] consumers paid an undercutting player %.0f ISK;  the sim sold %.0f ISK into a player's bids\n",
                        r.undercutFilled / 100.0, r.bidderFilled / 100.0);
        }
        results.push_back(r);
    }
    // (f) determinism:  the first seed again
    {
        RunResult again(Run(data, p, opt.seeds[0], opt, shocks));
        char buf[256];
        std::snprintf(buf, sizeof(buf), "(f) determinism:  seed %llu twice gives %s audit csv (%zu bytes)", (unsigned long long)opt.seeds[0],
                      (again.auditCsv == results[0].auditCsv) ? "identical" : "DIFFERENT", results[0].auditCsv.size());
        Check(again.auditCsv == results[0].auditCsv, buf, failures);
    }
    if (!opt.auditCsv.empty()) {
        std::ofstream f(opt.auditCsv.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
        f << "day,category,amount\n" << results[0].auditCsv;
    }
    std::printf("\n%s (%d failure%s)\n", failures ? "ACCEPTANCE TESTS FAILED" : "all acceptance tests passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
