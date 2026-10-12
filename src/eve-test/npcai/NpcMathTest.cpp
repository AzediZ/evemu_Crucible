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
    the worked values and edge cases of doc/code_and_design_notes/npc-ai/PLAN.md section 5 and its appendix
*/

#include "eve-test.h"

#include <cmath>

#include "NpcMath.h"
#include "NpcRng.h"
#include "NpcSchedule.h"
#include "RecentDamage.h"
#include "SeenList.h"
#include "ThreatTable.h"

namespace {
    int s_failures = 0;

    void Near(const char* what, double got, double want, double tolerance)
    {
        bool ok(std::isfinite(got) and (std::fabs(got - want) <= tolerance));
        ::printf("  %-58s %s  got %.6f want %.6f\n", what, (ok ? "ok  " : "FAIL"), got, want);
        if (!ok)
            ++s_failures;
    }

    void True(const char* what, bool ok)
    {
        ::printf("  %-58s %s\n", what, (ok ? "ok  " : "FAIL"));
        if (!ok)
            ++s_failures;
    }

    double Cth(double angular, double surface, double optimal, double falloff, double tracking, double sigRes, double sig,
               double radius = 0.0)
    {
        NpcMath::TurretInputs in = NpcMath::TurretInputs();
            in.angular = angular;
            in.surface = surface;
            in.optimal = optimal;
            in.falloff = falloff;
            in.tracking = tracking;
            in.sigRes = sigRes;
            in.targetSig = sig;
            in.targetRadius = radius;
        return NpcMath::TurretChanceToHit(in);
    }
}

int npcai_NpcMathTest( int argc, char* argv[] )
{
    using namespace NpcMath;

    ::printf("\n turret hit chance (5.1)\n");
    Near("A: w 0.06, track 0.1, sigRes 125, sig 40, in optimal", Cth(0.06, 1000, 5000, 1000, 0.1, 125, 40), 0.087436, 1e-6);
    Near("B: w 0, at optimal + falloff", Cth(0.0, 15000, 10000, 5000, 0.1, 125, 40), 0.5, 1e-9);
    Near("C: battleship gun vs sig 400", Cth(0.0075, 19000, 24000, 12000, 0.033, 400, 400), 0.964830, 1e-6);
    Near("C: battleship gun vs sig 40", Cth(0.0075, 19000, 24000, 12000, 0.033, 400, 40), 0.027867, 1e-6);
    Near("tracking 0, target not moving across: hits", Cth(0.0, 0, 1000, 1000, 0.0, 125, 40), 1.0, 1e-9);
    Near("tracking 0, angular > 0: miss", Cth(0.01, 0, 1000, 1000, 0.0, 125, 40), 0.0, 0.0);
    Near("falloff 0 inside optimal", Cth(0.0, 900, 1000, 0, 0.1, 125, 40), 1.0, 1e-9);
    Near("falloff 0 past optimal: miss", Cth(0.0, 1100, 1000, 0, 0.1, 125, 40), 0.0, 0.0);
    Near("signature 0 uses the radius", Cth(0.06, 0, 5000, 1000, 0.1, 125, 0, 40), 0.087436, 1e-6);
    Near("signature and radius 0: no NaN", Cth(0.06, 0, 5000, 1000, 0.1, 125, 0, 0), std::pow(0.5, 75.0 * 75.0), 1e-12);
    Near("NaN angular: no NaN", Cth(std::nan(""), 0, 5000, 1000, 0.1, 125, 40), 1.0, 1e-9);

    ::printf("\n turret geometry\n");
    {
        Vec3 sp = {0, 0, 0}, sv = {0, 0, 0}, tp = {5000, 0, 0}, tv = {300, 400, 0};
        TurretGeometry g(Geometry(sp, sv, 50, tp, tv, 50));
        Near("centre distance", g.centre, 5000, 1e-9);
        Near("surface distance", g.surface, 4900, 1e-9);
        Near("transversal only (radial 300 ignored)", g.transversal, 400, 1e-9);
        Near("angular", g.angular, 0.08, 1e-12);
        TurretGeometry z(Geometry(sp, sv, 50, sp, tv, 50));
        Near("same position: angular 0", z.angular, 0.0, 0.0);
        Near("same position: surface 0", z.surface, 0.0, 0.0);
    }

    ::printf("\n turret shot resolution (wrecking only inside a hit)\n");
    Near("CTH 0.005, u 0.004: wrecking", ResolveTurretShot(0.005, 0.004, 0.01), 3.0, 0.0);
    Near("CTH 0.005, u 0.006: miss", ResolveTurretShot(0.005, 0.006, 0.01), 0.0, 0.0);
    Near("CTH 0.5, u 0.3: 0.79", ResolveTurretShot(0.5, 0.3, 0.01), 0.79, 1e-12);
    Near("CTH 0, u 0: miss", ResolveTurretShot(0.0, 0.0, 0.01), 0.0, 0.0);

    ::printf("\n missiles (5.2)\n");
    Near("S 40, E 125, Ve 81, V 400, DRF 3.2, DRS 5.5", MissileDamageFactor(40, 125, 81, 400, 3.2, 5.5), 0.154574, 1e-6);
    Near("S 400, E 125, Ve 121.5, V 150", MissileDamageFactor(400, 125, 121.5, 150, 3.2, 5.5), 1.0, 1e-12);
    Near("target speed 0 counts as 1", MissileDamageFactor(400, 125, 121.5, 0, 3.2, 5.5), 1.0, 1e-12);
    Near("no explosion radius: full damage", MissileDamageFactor(40, 0, 81, 400, 3.2, 5.5), 1.0, 0.0);
    Near("DRS 1: size term only", MissileDamageFactor(40, 125, 81, 400, 3.2, 1.0), 0.32, 1e-12);
    Near("flight range 3750 m/s x 4000 ms", MissileRange(3750, 4000), 15000, 1e-9);

    ::printf("\n lock time (5.3)\n");
    Near("scanRes 875, sig 400", std::round(LockTimeMs(875, 400)), 1023, 0.0);
    // the plan's appendix lists 22223 here;  40e6 / (100 x asinh(35)^2) is 22159 (its 5.3 table's 22.2s agrees)
    Near("scanRes 100, sig 35", std::round(LockTimeMs(100, 35)), 22159, 0.0);
    Near("scanRes 100, sig 400", std::round(LockTimeMs(100, 400)), 8952, 0.0);
    Near("scanRes 100, sig 125", std::round(LockTimeMs(100, 125)), 13120, 0.0);
    Near("clamped to 1s", LockTimeMs(100000, 400), 1000, 0.0);
    Near("clamped to 180s", LockTimeMs(0.5, 1), 180000, 0.0);
    Near("no clamp (players)", LockTimeMs(100000, 400, 0, 0), 40000000.0 / (100000 * std::pow(std::log(400.0 + std::sqrt(160001.0)), 2)), 1e-9);

    ::printf("\n reaction delay (5.6)\n");
    Near("min 8000, max 9000, factor 0.3, u 0", ReactionDelayMs(8000, 9000, 0.3, 1.0, 0.0), 2400, 1e-9);
    Near("min 8000, max 9000, factor 0.3, u 1", ReactionDelayMs(8000, 9000, 0.3, 1.0, 1.0), 2700, 1e-9);
    Near("min and max 0: no delay", ReactionDelayMs(0, 0, 0.3, 1.0, 0.5), 0, 0.0);
    Near("scale 2", ReactionDelayMs(1000, 1000, 1.0, 2.0, 0.5), 2000, 1e-9);

    ::printf("\n chase phases (5.5)\n");
    Near("u1 < chance: the full duration", ChasePhaseMs(5000, 0.5, 0.2, 0.9), 5000, 0.0);
    Near("u1 >= chance: U(0, max)", ChasePhaseMs(5000, 0.5, 0.7, 0.2), 1000, 1e-9);
    Near("chance 1: always the max", ChasePhaseMs(5000, 1.0, 0.999, 0.0), 5000, 0.0);

    ::printf("\n level-one score and switching (5.7)\n");
    {
        L1ScoreInputs s = L1ScoreInputs();
            s.threat = 99;
            s.targetSig = 100;
            s.preferredSig = 400;
            s.useSigRadius = true;
            s.inAttackRange = true;
            s.inAcquireRange = true;
        Near("sig 100 vs preferred 400: x sqrt(0.25)", L1Score(s), 50, 1e-9);
        s.targetSig = 10;
        Near("tiny target floor 0.25", L1Score(s), 25, 1e-9);
        s.targetSig = 1000;
        Near("bigger targets aren't penalised", L1Score(s), 100, 1e-9);
        s.droneRecentDamager = true;
        s.locked = true;
        Near("drone 1.25 x locked 1.1", L1Score(s), 137.5, 1e-9);
        s.inAttackRange = false;
        Near("outside R_atk 0.5", L1Score(s), 68.75, 1e-9);
        s.inAcquireRange = false;
        Near("outside R_acq 0", L1Score(s), 0, 0.0);
        True("switch: beats margin, timer passed", L1ShouldSwitch(120, 100, 1.1, 70000, 0, 60000, 0.5, 0.0));
        True("no switch: within margin", !L1ShouldSwitch(110, 100, 1.1, 70000, 0, 60000, 0.5, 0.0));
        True("no switch: timer not passed", !L1ShouldSwitch(200, 100, 1.1, 59999, 0, 60000, 0.5, 0.0));
        True("no switch: draw below chance to not switch", !L1ShouldSwitch(200, 100, 1.1, 70000, 0, 60000, 0.2, 0.3));
    }

    ::printf("\n ewar range and stacking (5.8)\n");
    Near("inside optimal", EwarRangeChance(1000, 2000, 1000), 1.0, 0.0);
    Near("optimal + falloff", EwarRangeChance(3000, 2000, 1000), 0.5, 1e-12);
    Near("past optimal without falloff", EwarRangeChance(3000, 2000, 0), 0.0, 0.0);
    Near("stacking 1st", StackingFactor(0), 1.0, 0.0);
    Near("stacking 2nd", StackingFactor(1), 0.869120, 1e-6);
    Near("stacking 3rd", StackingFactor(2), 0.570583, 1e-6);
    {
        double one[1] = {0.4};
        Near("one 60% web", StackedProduct(one, 1), 0.4, 1e-12);
        double two[2] = {0.5, 0.4};
        Near("60% and 50% webs (strongest first)", StackedProduct(two, 2), 0.4 * (1.0 - (0.5 * 0.869120)), 1e-6);
        Near("nothing", StackedProduct(nullptr, 0), 1.0, 0.0);
    }
    Near("remote ecm at threshold", RemoteEcmDurationMs(60000, 0.9, 5, 5, 1, 20000), 60000, 1e-9);
    Near("remote ecm 2 players over", RemoteEcmDurationMs(60000, 0.9, 7, 5, 1, 20000), 60000 * 0.81, 1e-6);
    Near("remote ecm floor", RemoteEcmDurationMs(60000, 0.5, 50, 5, 1, 20000), 20000, 1e-9);
    Near("gate sentry dps (16+16+28+28) x 5 / 2.5s", TurretDps(16 + 16 + 28 + 28, 5, 2500), 176, 1e-9);

    ::printf("\n schedule (B13)\n");
    {
        NpcSchedule s;
        s.StartPeriodic(NpcAct::Turret, 0, 2500);
        int shots(0);
        for (int64_t t = 1000; t <= 600000; t += 1000)
            if (s.Due(NpcAct::Turret, t))
                ++shots;
        Near("2500ms gun on a 1s tick over 10 min: 240 shots", shots, 240, 0.0);
        s.StartPeriodic(NpcAct::Missile, 0, 1000);
        True("1000ms: due at exactly 1000 (>=, not >)", s.Due(NpcAct::Missile, 1000));
        True("not due again in the same tick", !s.Due(NpcAct::Missile, 1000));
        True("lag over 2 periods resets instead of bursting", s.Due(NpcAct::Missile, 10000) and !s.Due(NpcAct::Missile, 10000)
             and !s.Due(NpcAct::Missile, 10999) and s.Due(NpcAct::Missile, 11000));
        s.StartPeriodic(NpcAct::Turret, 0, 2500);
        s.Defer(NpcAct::Turret, 9000);      // due since 2500, held (no lock) until 9000
        True("deferred: due when allowed", s.IsDue(NpcAct::Turret, 9000) and s.Due(NpcAct::Turret, 9000));
        True("deferred: a full period after it happened", !s.Due(NpcAct::Turret, 10000) and !s.Due(NpcAct::Turret, 11000)
             and s.Due(NpcAct::Turret, 11500) and !s.Due(NpcAct::Turret, 13000) and s.Due(NpcAct::Turret, 14000));
        s.Start(NpcAct::Reaction, 0, 500);
        True("one shot: due once", s.Due(NpcAct::Reaction, 600) and !s.Active(NpcAct::Reaction));
        True("stopped is never due", !s.Due(NpcAct::Flee, 1000000));
    }

    ::printf("\n rng (6.8)\n");
    {
        NpcRng a(42, 1000001), b(42, 1000001), c(42, 1000002);
        bool same(true), differs(false), inRange(true);
        for (int i = 0; i < 1000; ++i) {
            double x(a.Uniform()), y(b.Uniform()), z(c.Uniform());
            same = same and (x == y);
            differs = differs or (x != z);
            inRange = inRange and (x >= 0.0) and (x < 1.0);
        }
        True("same seed and item: same draws", same);
        True("other item: other draws", differs);
        True("draws in [0, 1)", inRange);
    }

    ::printf("\n threat table (6.7)\n");
    {
        ThreatTable t;
        t.Add(1, 10.0f);
        t.Add(1, 5.0f);
        Near("adds up", t.Get(1), 15.0, 1e-6);
        for (uint32_t id = 2; id <= ThreatTable::Capacity; ++id)
            t.Add(id, static_cast<float>(id));
        True("full", t.Size() == ThreatTable::Capacity);
        t.Add(100, 1.0f);
        True("smaller newcomer doesn't evict", t.Get(100) == 0.0f);
        t.Add(101, 50.0f);
        True("larger newcomer evicts the lowest (2)", (t.Get(101) == 50.0f) and (t.Get(2) == 0.0f));
        True("top", (t.Top() != nullptr) and (t.Top()->id == 101));
        t.Decay(0.1f, 1.0f);
        True("decay drops entries below 1", (t.Get(3) == 0.0f) and (t.Get(101) > 4.9f));

        ThreatTable f;
        for (uint32_t id = 1; id <= ThreatTable::Capacity; ++id)
            f.Add(id, 100.0f);
        True("full again", f.Full());
        f.RemoveIf([](uint32_t id) { return ((id % 2) == 0); });
        True("remove-if drops what has left", (f.Size() == ThreatTable::Capacity / 2) and (f.Get(2) == 0.0f) and (f.Get(3) == 100.0f));
        f.Add(200, 1.0f);
        True("room for a small newcomer again", f.Get(200) == 1.0f);
    }

    ::printf("\n movement mode (5.5)\n");
    {
        // orbit 5000, boost 20000
        True("on the orbit:  orbit", NpcMath::MoveMode(5000, 5000, 20000) == NpcMath::Move::Orbit);
        True("1.5 x orbit:  still orbit", NpcMath::MoveMode(7500, 5000, 20000) == NpcMath::Move::Orbit);
        True("past 1.5 x orbit:  follow", NpcMath::MoveMode(7600, 5000, 20000) == NpcMath::Move::Follow);
        True("at the boost range:  follow", NpcMath::MoveMode(20000, 5000, 20000) == NpcMath::Move::Follow);
        True("past the boost range:  chase", NpcMath::MoveMode(20001, 5000, 20000) == NpcMath::Move::Chase);
        // entityChaseMaxDistance 0:  boost range = orbit range, so anything past 1.5 x orbit chases
        True("665 = 0:  chase past 1.5 x orbit", NpcMath::MoveMode(7600, 5000, 5000) == NpcMath::Move::Chase);
        True("NaN distance:  orbit (no move)", NpcMath::MoveMode(std::nan(""), 5000, 20000) == NpcMath::Move::Orbit);
    }

    ::printf("\n leash (5.4)\n");
    {
        True("inside:  none", NpcMath::LeashState(99000, 100000, 1.0, 2.0) == NpcMath::Leash::None);
        True("past 100km:  soft", NpcMath::LeashState(100001, 100000, 1.0, 2.0) == NpcMath::Leash::Soft);
        True("past 200km:  hard", NpcMath::LeashState(200001, 100000, 1.0, 2.0) == NpcMath::Leash::Hard);
        True("LeashRange override 150km:  hard", NpcMath::LeashState(150001, 100000, 1.0, 2.0, 150000) == NpcMath::Leash::Hard);
        True("override below soft:  no soft", NpcMath::LeashState(90000, 100000, 1.0, 2.0, 80000) == NpcMath::Leash::Hard);
        True("no wander range:  none", NpcMath::LeashState(1e9, 0, 1.0, 2.0) == NpcMath::Leash::None);
    }

    ::printf("\n self-repair skip chance (5.9)\n");
    {
        Near("0.5 x 1", NpcMath::RepairSkipChance(0.5, 1.0), 0.5, 1e-9);
        Near("0.5 x 1.5", NpcMath::RepairSkipChance(0.5, 1.5), 0.75, 1e-9);
        Near("0.9 x 2 clamps to 1", NpcMath::RepairSkipChance(0.9, 2.0), 1.0, 1e-9);
        Near("bad multiplier counts as 1", NpcMath::RepairSkipChance(0.4, -3.0), 0.4, 1e-9);
    }

    ::printf("\n recent damage (4.2 drone rule)\n");
    {
        RecentDamage r;
        r.Add(7, 100.0f, 1000);
        r.Add(7, 50.0f, 20000);
        Near("adds up inside the window", r.Get(7, 25000), 150.0, 1e-6);
        Near("gone 30s after the last hit", r.Get(7, 50001), 0.0, 1e-6);
        r.Add(7, 10.0f, 60000);
        Near("starts over after a gap", r.Get(7, 60000), 10.0, 1e-6);
        r.Add(8, 500.0f, 60000);
        uint32_t ids[RecentDamage::Capacity];
        size_t n(r.Sorted(60000, ids, RecentDamage::Capacity));
        True("sorted by damage", (n == 2) and (ids[0] == 8) and (ids[1] == 7));
    }

    ::printf("\n level-one threat (5.7, phase 3)\n");
    {
        using namespace NpcMath;
        // the plan's sanity check:  a sleeper battleship with ~40k hp, E = 0.02 x 40000 = 800
        Near("ECM cycle 5 x E", L1EffectThreat(Effect::Ecm, false, 0.02, 40000), 4000, 1e-6);
        Near("ECM jam adds x2 more (12000 in all)", L1EffectThreat(Effect::Ecm, false, 0.02, 40000)
             + L1EffectThreat(Effect::Ecm, true, 0.02, 40000), 12000, 1e-6);
        Near("damp 3 x E", L1EffectThreat(Effect::Damp, false, 0.02, 40000), 2400, 1e-6);
        Near("TD 2 x E", L1EffectThreat(Effect::TrackDisrupt, false, 0.02, 40000), 1600, 1e-6);
        Near("neut 2 x E", L1EffectThreat(Effect::Neut, false, 0.02, 40000), 1600, 1e-6);
        Near("web 1 x E", L1EffectThreat(Effect::Web, false, 0.02, 40000), 800, 1e-6);
        Near("scram 1 x E", L1EffectThreat(Effect::Scramble, false, 0.02, 40000), 800, 1e-6);
        Near("painter 1 x E", L1EffectThreat(Effect::Paint, false, 0.02, 40000), 800, 1e-6);
        Near("unknown 1 x E", L1EffectThreat(Effect::None, false, 0.02, 40000), 800, 1e-6);
        Near("no hp:  nothing", L1EffectThreat(Effect::Ecm, false, 0.02, 0), 0, 0.0);
        Near("newcomer:  1.2 x primary 1000", L1NewcomerThreat(0, 1000, 1.2), 1200, 1e-9);
        Near("newcomer that has 500 already", L1NewcomerThreat(500, 1000, 1.2), 700, 1e-9);
        Near("newcomer above it already:  nothing", L1NewcomerThreat(1500, 1000, 1.2), 0, 0.0);
        bool cuddle(false);
        Near("jam response, frigate:  ecm reach + 5km", L1JamDistance(35, 60000, cuddle), 65000, 1e-9);
        True("  no cuddle", !cuddle);
        Near("jam response, battleship (sig 400):  5km", L1JamDistance(400, 60000, cuddle), 5000, 1e-9);
        True("  cuddle", cuddle);
        True("sig 300 counts as a battleship", (L1JamDistance(300, 1000, cuddle) == 5000) and cuddle);
        // the plan's acceptance case 2:  drones switch once drone threat x 1.25 beats the player's score x 1.1
        L1ScoreInputs drone = L1ScoreInputs(), ship = L1ScoreInputs();
            drone.threat = 1100; drone.droneRecentDamager = true; drone.inAttackRange = true; drone.inAcquireRange = true;
            ship.threat = 1000; ship.inAttackRange = true; ship.inAcquireRange = true; ship.locked = true;
        True("drone 1100 (x1.25) vs locked ship 1000 (x1.1):  switch", L1ShouldSwitch(L1Score(drone), L1Score(ship), 1.1, 70000, 0, 60000, 0.5, 0.0));
        drone.threat = 1100; drone.droneRecentDamager = false;
        True("the same drone without recent damage:  no switch", !L1ShouldSwitch(L1Score(drone), L1Score(ship), 1.1, 70000, 0, 60000, 0.5, 0.0));
    }

    ::printf("\n remote repair (5.9, phase 3)\n");
    {
        using namespace NpcMath;
        True("armor 50% damaged, threshold 0.5:  repair", ArmorRepairNeeded(1000, 500, 0, 0.5));
        True("armor 49% damaged:  not yet", !ArmorRepairNeeded(1000, 490, 0, 0.5));
        True("a reserved repair covers it", !ArmorRepairNeeded(1000, 800, 400, 0.5));
        True("reserved, still below:  repair", ArmorRepairNeeded(1000, 900, 300, 0.5));
        True("no armor hp:  never", !ArmorRepairNeeded(0, 10, 0, 0.1));
        True("shield half gone, threshold 0.5:  boost", ShieldRepairNeeded(1000, 500, 0.5));
        True("shield full:  no", !ShieldRepairNeeded(1000, 1000, 0.1));
        RepairOrder hiPrio = { 2, false, 0.9 }, fire = { 0, true, 0.6 }, low = { 0, false, 0.2 }, lower = { 0, false, 0.1 };
        True("priority first", RepairBefore(hiPrio, fire) and !RepairBefore(fire, hiPrio));
        True("then under fire", RepairBefore(fire, low) and !RepairBefore(low, fire));
        True("then the lowest layer", RepairBefore(lower, low) and !RepairBefore(low, lower));
        Near("assist resist -20:  resonance x0.8", AssistResonanceMult(-20), 0.8, 1e-9);
        Near("assist speed -20:  x1.2", AssistBonusMult(-20), 1.2, 1e-9);
        Near("assist speed +20 (test npc):  x0.8", AssistBonusMult(20), 0.8, 1e-9);
        Near("kundalini remote ecm, 30 players:  5 min", RemoteEcmDurationMs(300000, 0.8, 30, 60, 5, 60000), 300000, 1e-9);
        Near("70 players:  x0.8^2", RemoteEcmDurationMs(300000, 0.8, 70, 60, 5, 60000), 300000 * 0.64, 1e-6);
        Near("200 players:  the 1 min floor", RemoteEcmDurationMs(300000, 0.8, 200, 60, 5, 60000), 60000, 1e-9);
    }

    ::printf("\n seen list (4.7 newcomers)\n");
    {
        SeenList seen;
        True("first sight:  newcomer", seen.Touch(7, 1000, 300000));
        True("seen again soon:  not", !seen.Touch(7, 2000, 300000));
        True("after 5 min away:  newcomer again", seen.Touch(7, 2000 + 300001, 300000));
        for (uint32_t i = 100; i < 100 + SeenList::Capacity; ++i)
            seen.Touch(i, 400000 + i, 300000);
        True("full:  the oldest made room", seen.Size() == SeenList::Capacity);
        True("the evicted one is new again", seen.Touch(7, 500000, 300000));
        True("0 is never anyone", !seen.Touch(0, 500000, 300000));
    }

    ::printf("\n%d failure(s)\n", s_failures);
    return (s_failures == 0 ? 0 : 1);
}
