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
    npc ai phase 5 (doc/code_and_design_notes/planning/npc-ai/PLAN.md phase 5, 6.7):  level of detail, cpu budget, difficulty
    presets, telemetry and the shot log
*/

#include "eve-test.h"

#include <cmath>
#include <string>
#include <cstdint>

#include "NpcLod.h"
#include "NpcSchedule.h"
#include "NpcTelemetry.h"

namespace {
    int s_failures = 0;

    void True(const char* what, bool ok)
    {
        ::printf("  %-62s %s\n", what, (ok ? "ok  " : "FAIL"));
        if (!ok)
            ++s_failures;
    }

    void Near(const char* what, double got, double want, double tolerance)
    {
        bool ok(std::isfinite(got) and (std::fabs(got - want) <= tolerance));
        ::printf("  %-62s %s  got %.6f want %.6f\n", what, (ok ? "ok  " : "FAIL"), got, want);
        if (!ok)
            ++s_failures;
    }

    bool Contains(const std::string& text, const char* part)
    {
        return (text.find(part) != std::string::npos);
    }
}

int npcai_NpcPolishTest( int argc, char* argv[] )
{
    using namespace NpcLod;

    ::printf("\n level of detail (6.7)\n");
    {
        True("players in the bubble:  near", TierOf(true, true) == Tier::Near);
        True("players in the system only:  far", TierOf(false, true) == Tier::Far);
        True("no players in the system:  empty", TierOf(false, false) == Tier::Empty);
        True("near runs every tick", RunsThisTick(Tier::Near, false, 7, 1234, 4, true));
        True("busy runs every tick, far", RunsThisTick(Tier::Far, true, 7, 1234, 4, true));
        True("busy runs every tick, empty system", RunsThisTick(Tier::Empty, true, 7, 1234, 4, true));
        True("idle in an empty system:  never", !RunsThisTick(Tier::Empty, false, 8, 1234, 4, true));
        True("idle in an empty system, LodEmptySystem off:  like far", RunsThisTick(Tier::Empty, false, 2, 2, 4, false));
        // far:  exactly one tick in every farEvery, each npc on its own one
        int runs(0), first(-1);
        for (uint64_t tick = 100; tick < 104; ++tick)
            if (RunsThisTick(Tier::Far, false, tick, 1235, 4, true)) {
                ++runs;
                if (first < 0)
                    first = static_cast<int>(tick);
            }
        True("far idle npc thinks once in 4 ticks", runs == 1);
        int other(-1);
        for (uint64_t tick = 100; tick < 104; ++tick)
            if (RunsThisTick(Tier::Far, false, tick, 1236, 4, true))
                other = static_cast<int>(tick);
        True("the next itemID thinks on another tick (staggered)", (other >= 0) and (other != first));
        True("LodFarInterval 0:  every tick", RunsThisTick(Tier::Far, false, 3, 5, 0, true) and RunsThisTick(Tier::Far, false, 4, 5, 0, true));
        True("LodFarInterval 1:  every tick", RunsThisTick(Tier::Far, false, 3, 5, 1, true));
    }

    ::printf("\n cpu budget (6.7)\n");
    {
        True("near never waits", !OverBudget(Tier::Near, false, 1e9, 50000));
        True("far under budget runs", !OverBudget(Tier::Far, false, 49999.0, 50000));
        True("far at budget waits", OverBudget(Tier::Far, false, 50000.0, 50000));
        True("empty system over budget waits", OverBudget(Tier::Empty, false, 60000.0, 50000));
        True("busy far npc never waits (no starving by itemID order)", !OverBudget(Tier::Far, true, 1e9, 50000));
        True("budget 0 = none", !OverBudget(Tier::Far, false, 1e9, 0));
    }

    ::printf("\n difficulty presets (phase 5)\n");
    {
        Near("Crucible:  damage x1", DamageMult(1.0, 0.0), 1.0, 1e-12);
        Near("difficulty 2:  damage x2", DamageMult(2.0, 0.0), 2.0, 1e-12);
        Near("difficulty 2, DifficultyDamage 1.5:  x1.5", DamageMult(2.0, 1.5), 1.5, 1e-12);
        Near("difficulty 0 counts as 1", DamageMult(0.0, 0.0), 1.0, 1e-12);
        Near("difficulty NaN counts as 1", DamageMult(std::nan(""), 0.0), 1.0, 1e-12);
        Near("difficulty 2:  lock and reaction time x0.5", TimeMult(2.0, 0.0), 0.5, 1e-12);
        Near("difficulty 0.5 (easier):  times x2", TimeMult(0.5, 0.0), 2.0, 1e-12);
        Near("DifficultyLockTime 0.8 wins over the difficulty", TimeMult(2.0, 0.8), 0.8, 1e-12);
        Near("negative difficulty counts as 1", TimeMult(-3.0, 0.0), 1.0, 1e-12);
    }

    ::printf("\n telemetry (NPC__AI_STATS)\n");
    {
        NpcStats s;
        s.Reset(1000);
        // 60 ticks with 10 npcs, 2 of them engaged (1 level-one), 4 engaged in one tick
        for (int i = 0; i < 60; ++i) {
            s.BeginTick();
            s.curNpcs = 10;
            s.curEngaged = (i == 30 ? 4 : 2);
            s.curEngagedL1 = 1;
            s.AddRun(10.0);
            s.AddRun(30.0);
            s.EndTick();
        }
        s.AddShot(0.0);
        s.AddShot(0.8);
        s.AddShot(3.0);
        s.AddShot(1.2);
        s.AddLock(2000.0);
        s.AddLock(4000.0);
        s.switchesL1 = 1;
        s.switchesStandard = 0;
        int64_t now(61000);
        Near("mean npcs", s.MeanNpcs(), 10.0, 1e-9);
        Near("mean engaged", s.MeanEngaged(), (59 * 2 + 4) / 60.0, 1e-9);
        True("max engaged 4", s.engagedMax == 4);
        Near("hit ratio 3/4", s.HitRatio(), 0.75, 1e-12);
        Near("wrecking ratio 1/4", s.WreckingRatio(), 0.25, 1e-12);
        Near("mean lock 3000ms", s.MeanLockMs(), 3000.0, 1e-9);
        Near("mean ai us per run", s.MeanAiUs(), 20.0, 1e-9);
        Near("tick max 40us", s.tickUsMax, 40.0, 1e-9);
        Near("window 60s", s.Seconds(now), 60.0, 1e-9);
        Near("level-one switches per engaged npc-minute:  1 in 60 npc-seconds", NpcStats::PerNpcMinute(s.switchesL1, s.engagedL1Ticks), 1.0, 1e-9);
        Near("standard switches per minute 0", s.PerMinute(s.switchesStandard, now), 0.0, 1e-12);
        std::string line(s.Format("Jita", 30000142, now));
        True("line names the system", Contains(line, "Jita(30000142) last 60s"));
        True("line has the hit ratio", Contains(line, "hits 75.0%"));
        True("line has the lock time", Contains(line, "locks 2 (mean 3000ms)"));
        True("line has the level-one switch rate", Contains(line, "(1.00 per engaged level-one npc)"));
        NpcStats e;
        e.Reset(5);
        True("a fresh window is empty", e.Empty());
        True("the empty window's ratios are 0", (e.HitRatio() == 0.0) and (e.MeanLockMs() == 0.0) and (e.MeanAiUs() == 0.0));
    }

    ::printf("\n shot log (fairness audit)\n");
    {
        NpcShotLog log;
        True("empty", log.Size() == 0);
        for (uint32_t i = 1; i <= 8; ++i) {
            NpcShotRecord r;
            r.targetID = i;
            r.quality = (i % 2 == 0 ? 0.0 : 1.0);
            log.Add(r);
        }
        True("keeps the last 6", log.Size() == NpcShotLog::Capacity);
        True("newest first", (log.Newest(0).targetID == 8) and (log.Newest(5).targetID == 3));
        NpcShotRecord r;
            r.atMs = 1000;
            r.targetID = 42;
            r.chance = 0.5;
            r.u = 0.3;
            r.quality = 0.79;
        std::string line(NpcShotLog::Format(r, 3000));
        True("line shows the result", Contains(line, "hit 0.790") and Contains(line, "2.0s ago -> 42"));
        r.quality = 0.0;
        True("a miss says so", Contains(NpcShotLog::Format(r, 3000), "miss"));
        r.quality = 3.0;
        True("a wrecking shot says so", Contains(NpcShotLog::Format(r, 3000), "wrecking"));
    }

    ::printf("\n schedule\n");
    {
        True("the wander activity has a name", std::string(NpcSchedule::Name(NpcAct::Wander)) == "wander");
        NpcSchedule sched;
        sched.Start(NpcAct::Wander, 0, 60000);
        True("wander re-pick due after its time", !sched.Due(NpcAct::Wander, 59999) and sched.Due(NpcAct::Wander, 60000));
        True("and once only", !sched.Active(NpcAct::Wander));
    }

    ::printf("\n%d failure(s)\n", s_failures);
    return (s_failures == 0 ? 0 : 1);
}
