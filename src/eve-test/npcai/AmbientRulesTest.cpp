/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
    the ambient npc rules of doc/code_and_design_notes/planning/ambient-npc-plan.md section 6 (tests U1 - U22, section 13)
*/

#include "eve-test.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "AmbientRules.h"
#include "AmbientSchedule.h"

namespace {
    int s_failures = 0;

    void True(const char* what, bool ok)
    {
        ::printf("  %-62s %s\n", what, (ok ? "ok  " : "FAIL"));
        if (!ok)
            ++s_failures;
    }

    void Equal(const char* what, long long got, long long want)
    {
        bool ok(got == want);
        ::printf("  %-62s %s  got %lld want %lld\n", what, (ok ? "ok  " : "FAIL"), got, want);
        if (!ok)
            ++s_failures;
    }

    void Near(const char* what, double got, double want, double tolerance)
    {
        bool ok(std::fabs(got - want) <= tolerance);
        ::printf("  %-62s %s  got %.5f want %.5f\n", what, (ok ? "ok  " : "FAIL"), got, want);
        if (!ok)
            ++s_failures;
    }
}

int npcai_AmbientRulesTest( int argc, char* argv[] )
{
    using namespace AmbientRules;

    ::printf("\n U1 security band (6.1)\n");
    {
        Equal("1.0 is high", GetBand(1.0f, 10000002), Band::High);
        Equal("0.5 is high", GetBand(0.5f, 10000002), Band::High);
        Equal("0.4 is low", GetBand(0.4f, 10000002), Band::Low);
        Equal("0.1 is low", GetBand(0.1f, 10000002), Band::Low);
        Equal("0.0 is none", GetBand(0.0f, 10000002), Band::None);
        Equal("-0.5 is none", GetBand(-0.5f, 10000002), Band::None);
        Equal("a wormhole region is none at 1.0", GetBand(1.0f, 11000001), Band::None);
    }

    ::printf("\n U2 market node score (6.2)\n");
    {
        Near("the biggest node is 1", NodeScore(1.0e12, 1.0e12, 1), 1.0, 1e-9);
        Near("a hub with tiny turnover is floored at 0.6", NodeScore(10.0, 1.0e12, 1), 0.6, 1e-9);
        Near("a regional is capped at 0.5", NodeScore(9.0e11, 1.0e12, 2), 0.5, 1e-9);
        Near("a site is ignored", NodeScore(1.0e12, 1.0e12, 3), 0.0, 1e-9);
        Near("a regional with no turnover is 0", NodeScore(0.0, 1.0e12, 2), 0.0, 1e-9);
        Near("region share sqrt(0.25)", RegionShare(0.25, 1.0), 0.5, 1e-9);
    }

    ::printf("\n U3 busyness (6.3)\n");
    {
        Near("Jita", Busyness(1, 1, 1, 1), 1.0, 0.02);
        Near("Amarr", Busyness(0.85, 0.85, 0.8, 1), 0.88, 0.02);
        Near("Perimeter", Busyness(0, 0.67, 1, 1), 0.47, 0.02);
        Near("high sec dead end", Busyness(0, 0, 0.2, StationTerm(1)), 0.10, 0.02);
        True("clamped to [0,1]", (Busyness(5, 5, 5, 5) == 1.0) and (Busyness(-5, -5, -5, -5) == 0.0));
        bool mono(true);
        for (int i = 0; i < 10; ++i) {
            double a(i * 0.1), b(a + 0.1);
            mono = mono and (Busyness(b, 0.5, 0.5, 0.5) >= Busyness(a, 0.5, 0.5, 0.5))
                        and (Busyness(0.5, b, 0.5, 0.5) >= Busyness(0.5, a, 0.5, 0.5))
                        and (Busyness(0.5, 0.5, b, 0.5) >= Busyness(0.5, 0.5, a, 0.5))
                        and (Busyness(0.5, 0.5, 0.5, b) >= Busyness(0.5, 0.5, 0.5, a));
        }
        True("monotonic in every term", mono);
    }

    ::printf("\n U4 hub proximity (6.3)\n");
    {
        Near("0 jumps is h", HubProximity(0.8, 0, 2.5), 0.8, 1e-9);
        Near("2.5 jumps is h/e", HubProximity(0.8, 2, 2.0), 0.8 / std::exp(1.0), 1e-9);
        Near("no route is 0", HubProximity(0.8, -1, 2.5), 0.0, 1e-9);
    }

    ::printf("\n U5 diurnal factor (6.4)\n");
    {
        Near("peak hour", Diurnal(19.0, 0.25, 19.0), 1.25, 1e-9);
        Near("12 hours off peak", Diurnal(7.0, 0.25, 19.0), 0.75, 1e-9);
        Near("amplitude 0", Diurnal(3.0, 0.0, 19.0), 1.0, 1e-9);
    }

    ::printf("\n U6 traffic rate (6.4)\n");
    {
        Near("Jita at the defaults", TrafficRate(1.0, 1.5, 24.0, 0.2, 1.0, 1.0, 1.0), 24.0, 1e-9);
        Near("a backwater gets the floor", TrafficRate(0.04, 1.5, 24.0, 0.2, 1.0, 1.0, 1.0), 0.2, 1e-9);
        Near("low sec x 0.25", TrafficRate(0.3, 1.5, 24.0, 0.2, 0.25, 1.0, 1.0),
             0.25 * TrafficRate(0.3, 1.5, 24.0, 0.2, 1.0, 1.0, 1.0), 1e-9);
        Near("density x 2 doubles it", TrafficRate(0.5, 1.5, 24.0, 0.2, 1.0, 2.0, 1.0),
             2.0 * TrafficRate(0.5, 1.5, 24.0, 0.2, 1.0, 1.0, 1.0), 1e-9);
    }

    ::printf("\n U7 exponential inter-arrival times (6.4)\n");
    {
        NpcRng rng(12345, 1);
        const int n(100000);
        const double lambda(2.0);    // per minute:  mean 30000 ms
        double sum(0.0), sum2(0.0);
        int64_t lowest(Never);
        for (int i = 0; i < n; ++i) {
            int64_t ms(NextArrivalMs(rng, lambda));
            sum += static_cast<double>(ms);
            sum2 += static_cast<double>(ms) * static_cast<double>(ms);
            if (ms < lowest)
                lowest = ms;
        }
        double mean(sum / n), sd(std::sqrt((sum2 / n) - (mean * mean)));
        Near("mean within 2% of 60000/lambda", mean / 30000.0, 1.0, 0.02);
        Near("coefficient of variation about 1", sd / mean, 1.0, 0.05);
        True("never under 250 ms", lowest >= 250);
        True("lambda 0 is never", NextArrivalMs(rng, 0.0) == Never);
    }

    ::printf("\n U8 Poisson counts\n");
    {
        NpcRng rng(12345, 2);
        for (double m : {0.5, 3.0, 15.0, 40.0}) {
            const int n(60000);
            double sum(0.0), sum2(0.0);
            for (int i = 0; i < n; ++i) {
                double k(PoissonCount(rng, m));
                sum += k;
                sum2 += k * k;
            }
            double mean(sum / n), var((sum2 / n) - (mean * mean));
            char what[80];
            snprintf(what, sizeof(what), "mean %.1f:  mean within 3%%", m);
            Near(what, mean / m, 1.0, 0.03);
            snprintf(what, sizeof(what), "mean %.1f:  variance within 3%%", m);
            Near(what, var / m, 1.0, 0.03);
        }
        Equal("mean 0 is 0", PoissonCount(rng, 0.0), 0);
    }

    ::printf("\n U9 itinerary kinds (6.5)\n");
    {
        KindArray w(KindWeights(1, 0, 0.0));
        True("1 gate, 0 stations:  nothing", (w[0] + w[1] + w[2] + w[3] + w[4]) == 0.0);
        w = KindWeights(2, 0, 0.0);
        Near("2 gates, 0 stations:  through only", w[Kind::Through], 1.0, 1e-9);
        w = KindWeights(4, 6, 1.0);
        Near("Jita-like (H = 1):  through 0.20", w[Kind::Through], 0.20, 1e-9);
        Near("weights sum to 1", w[0] + w[1] + w[2] + w[3] + w[4], 1.0, 1e-9);
        True("arrive and undock balance", w[Kind::ArriveDock] == w[Kind::UndockLeave]);
    }

    ::printf("\n U10 gate and station weights (6.5)\n");
    {
        True("toward the hub weighs more", GateWeight(0.4, true, false) > GateWeight(0.4, false, false));
        Near("null sec destination x 0.3", GateWeight(0.4, false, true), 0.3 * GateWeight(0.4, false, false), 1e-9);
        Near("hub station 7", StationWeight(1.0), 7.0, 1e-9);
        Near("plain station 1", StationWeight(0.0), 1.0, 1e-9);
    }

    ::printf("\n U11 weighted picks\n");
    {
        NpcRng rng(12345, 3);
        std::vector<double> w = {1.0, 0.0, 3.0, 6.0};
        std::vector<int> count(4, 0);
        const int n(100000);
        for (int i = 0; i < n; ++i)
            ++count[PickWeighted(rng, w)];
        Near("weight 1 of 10", count[0] / double(n), 0.1, 0.01);
        Equal("weight 0 never", count[1], 0);
        Near("weight 3 of 10", count[2] / double(n), 0.3, 0.01);
        Near("weight 6 of 10", count[3] / double(n), 0.6, 0.01);
        std::vector<double> zero = {0.0, 0.0};
        True("all zero is npos", PickWeighted(rng, zero) == npos);
    }

    ::printf("\n U12 itineraries (6.5)\n");
    {
        NpcRng rng(12345, 4);
        std::vector<double> gates = {1.0, 2.0, 0.5};
        std::vector<double> stations = {7.0, 1.0};
        KindArray kinds(KindWeights(3, 2, 0.5));
        bool exitOk(true), hopOk(true);
        for (int i = 0; i < 20000; ++i) {
            Itinerary it;
            if (!PickItinerary(rng, kinds, gates, stations, it))
                continue;
            if ((it.kind == Kind::Through) and (it.entryGate == it.exitGate))
                exitOk = false;
            if ((it.kind == Kind::StationHop) and (it.stationA == it.stationB))
                hopOk = false;
        }
        True("the exit gate is never the entry gate", exitOk);
        True("a station hop never stays at one station", hopOk);
        NpcRng a(777, 9), b(777, 9);
        Itinerary ia, ib;
        PickItinerary(a, kinds, gates, stations, ia);
        PickItinerary(b, kinds, gates, stations, ib);
        True("a seed gives the same itinerary", (ia.kind == ib.kind) and (ia.entryGate == ib.entryGate)
             and (ia.exitGate == ib.exitGate) and (ia.stationA == ib.stationA) and (ia.stationB == ib.stationB));
        std::vector<double> oneGate = {1.0};
        Itinerary ic;
        KindArray through;
        through.fill(0.0);
        through[Kind::Through] = 1.0;
        True("through with one gate fails", !PickItinerary(a, through, oneGate, stations, ic));
    }

    ::printf("\n U13 hull classes (6.5)\n");
    {
        Near("no freighters in low sec", ClassWeights(Band::Low, 1.0)[Hull::Freighter], 0.0, 1e-12);
        True("freighter share rises with H", ClassWeights(Band::High, 1.0)[Hull::Freighter] > ClassWeights(Band::High, 0.2)[Hull::Freighter]);
        HullArray h(ClassWeights(Band::High, 0.5));
        Near("weights sum to 1", h[0] + h[1] + h[2] + h[3], 1.0, 1e-9);
    }

    ::printf("\n U14 caps (6.5)\n");
    {
        Equal("hub grid 25", GridCapFor(true, 15, 25), 25);
        Equal("other grid 15", GridCapFor(false, 15, 25), 15);
        True("room everywhere", CapAllows(3, 15, 10, 60, 100, 800));
        True("full grid blocks", !CapAllows(15, 15, 10, 60, 100, 800));
        True("full system blocks", !CapAllows(3, 15, 60, 60, 100, 800));
        True("full server blocks", !CapAllows(3, 15, 10, 60, 800, 800));
    }

    ::printf("\n U15 miners (6.6)\n");
    {
        Near("high sec A = 1", MinerTarget(3.0, 1.0, Band::High, 0.6, false, 0.5, 1.0, 1.0), 3.0, 1e-9);
        Near("high sec A = 0", MinerTarget(3.0, 0.0, Band::High, 0.6, false, 0.5, 1.0, 1.0), 1.2, 1e-9);
        Near("low sec x 0.6", MinerTarget(3.0, 1.0, Band::Low, 0.6, false, 0.5, 1.0, 1.0), 1.8, 1e-9);
        Near("ice x 0.5", MinerTarget(3.0, 1.0, Band::High, 0.6, true, 0.5, 1.0, 1.0), 1.5, 1e-9);
        NpcRng rng(12345, 5);
        bool inRange(true), sawMin(false), sawMax(false);
        for (int i = 0; i < 10000; ++i) {
            uint32_t c(MinerCycles(rng, 4, 10));
            inRange = inRange and (c >= 4) and (c <= 10);
            sawMin = sawMin or (c == 4);
            sawMax = sawMax or (c == 10);
        }
        True("cycles always in [4, 10], both ends reached", inRange and sawMin and sawMax);
    }

    ::printf("\n U16 patrol squads (6.7)\n");
    {
        bool ok(true);
        for (int i = 0; i <= 10; ++i) {
            uint32_t n(ConcordSquads(Band::High, i * 0.1, 1.0f, 1.0, 4, 1.0));
            ok = ok and (n >= 1) and (n <= 4);
        }
        True("CONCORD:  at least 1, at most the max in high sec", ok);
        Equal("CONCORD:  none in low sec", ConcordSquads(Band::Low, 1.0, 0.4f, 1.0, 4, 1.0), 0);
        Equal("navy:  none in low sec", NavySquads(Band::Low, true, 1.0, 0.5, 1.0), 0);
        Equal("navy:  none without a navy", NavySquads(Band::High, false, 1.0, 0.5, 1.0), 0);
        Equal("navy:  A = 1 at the defaults", NavySquads(Band::High, true, 1.0, 0.5, 1.0), 1);
    }

    ::printf("\n U17 criminal rate (6.7)\n");
    {
        Near("1.0 system", CriminalRatePerHour(Band::High, 1.0f, 0.5, 1.0, 3.0, 1.0), 0.1, 1e-6);
        Near("0.5 system", CriminalRatePerHour(Band::High, 0.5f, 0.5, 1.0, 3.0, 1.0), 1.1, 1e-6);
        Near("low sec uses its base", CriminalRatePerHour(Band::Low, 0.3f, 0.5, 1.0, 3.0, 1.0), 3.0, 1e-6);
        Near("density scales", CriminalRatePerHour(Band::High, 0.5f, 0.5, 1.0, 3.0, 2.0), 2.2, 1e-6);
        Near("none in null", CriminalRatePerHour(Band::None, 0.0f, 0.5, 1.0, 3.0, 1.0), 0.0, 1e-12);
        // players only while crime is on, then the band's toggle (8.5)
        True("crime off:  never players", !CriminalsAttackPlayers(false, false, true, true)
                                          and !CriminalsAttackPlayers(false, true, true, true));
        True("crime on, low sec:  the low sec toggle", CriminalsAttackPlayers(true, false, false, true)
                                                     and !CriminalsAttackPlayers(true, false, true, false));
        True("crime on, high sec:  the high sec toggle", CriminalsAttackPlayers(true, true, true, false)
                                                       and !CriminalsAttackPlayers(true, true, false, true));
    }

    ::printf("\n U18 landing distances (8)\n");
    {
        NpcRng rng(12345, 6);
        bool ok(true);
        for (int i = 0; i < 10000; ++i) {
            double g(LandingDistance(rng, Node::Gate, 5000.0));
            double s(LandingDistance(rng, Node::Station, 20000.0));
            double b(LandingDistance(rng, Node::Belt, 0.0));
            double p(LandingDistance(rng, Node::Planet, 0.0));
            ok = ok and (g >= 5500.0) and (g <= 7500.0) and (s >= 2000.0) and (s <= 12000.0)
                    and (b >= 5000.0) and (b <= 25000.0) and (p >= 10000.0) and (p <= 40000.0);
        }
        True("always inside the documented ranges", ok);
    }

    ::printf("\n U19 reference hull warp factor (7.2, EVE_Warp.h)\n");
    {
        Equal("Bestower 4.5 AU/s", WarpFactorFor(1.5, 1.0), 45);
        Equal("Providence 0.75 AU/s", WarpFactorFor(0.25, 0.0), 8);
        Equal("cruiser without 600 (default 1):  3 AU/s", WarpFactorFor(1.0, 1.0), 30);
        Equal("matches EVE_Warp.h", WarpFactorFor(2.0, 1.0), WarpSpeed::ShipFactor(2.0, 1.0));
    }

    ::printf("\n U20 the event queue\n");
    {
        AmbientSchedule q;
        q.Push(300, 1, 7, 1);
        q.Push(100, 1, 8, 2);
        q.Push(200, 1, 7, 3);
        q.Push(200, 2, 9, 4);
        q.Push(200, 3, 8, 5);
        AmbientSchedule::Event e;
        std::vector<uint32_t> order;
        while (q.PopDue(1000, e))
            order.push_back(e.payload);
        True("by time, ties in push order", (order.size() == 5) and (order[0] == 2) and (order[1] == 3) and (order[2] == 4)
             and (order[3] == 5) and (order[4] == 1));
        q.Push(100, 1, 7);
        q.Push(150, 1, 8);
        q.Push(120, 1, 7);
        Equal("Cancel takes only that grid's events", q.Cancel(7), 2);
        Equal("the other grid's stays", q.Size(), 1);
        True("nothing due before its time", !q.PopDue(149, e) and q.PopDue(150, e));
        True("an empty queue's next is never", q.NextDue() == Never);
    }

    ::printf("\n U21 arrival suppression (6.8)\n");
    {
        True("watched paired gate in an active system", SuppressArrival(true, true));
        True("not watched", !SuppressArrival(false, true));
        True("not active", !SuppressArrival(true, false));
    }

    ::printf("\n U22 jump transit delay (6.8)\n");
    {
        NpcRng rng(12345, 7);
        bool ok(true);
        for (int i = 0; i < 10000; ++i) {
            int64_t ms(TransitDelayMs(rng, 8, 15));
            ok = ok and (ms >= 8000) and (ms <= 15000);
        }
        True("in [8, 15] s", ok);
    }

    ::printf("\n criminals (6.7)\n");
    {
        NpcRng rng(12345, 8);
        int size[4] = {0, 0, 0, 0};
        int cls[3] = {0, 0, 0};
        const int n(100000);
        for (int i = 0; i < n; ++i) {
            ++size[CriminalGroupSize(rng)];
            ++cls[CriminalClass(rng, Band::High)];
        }
        Near("group of 1:  70%", size[1] / double(n), 0.70, 0.01);
        Near("group of 3:  5%", size[3] / double(n), 0.05, 0.01);
        Equal("no cruisers in high sec", cls[2], 0);
    }

    ::printf("\n %s (%d failed)\n\n", (s_failures == 0 ? "all ambient rules tests passed" : "AMBIENT RULES TESTS FAILED"), s_failures);
    return (s_failures == 0 ? 0 : 1);
}
