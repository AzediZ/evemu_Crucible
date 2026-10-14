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
    the law enforcement rules of doc/code_and_design_notes/planning/npc-ai/PLAN.md 4.8 - 4.10 (phase 4)
*/

#include "eve-test.h"

#include <cmath>
#include <cstdint>
#include <string>

#include "LawRules.h"
#include "NpcMath.h"

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
}

int npcai_LawRulesTest( int argc, char* argv[] )
{
    using namespace LawRules;

    ::printf("\n CONCORD response by security level (4.8, owner decision 3)\n");
    {
        const std::string def("2,5,10,15,20,25");
        Equal("1.0 -> 2s", ConcordDelaySeconds(def, 1.0f), 2);
        Equal("0.9 -> 5s", ConcordDelaySeconds(def, 0.9f), 5);
        Equal("0.8 -> 10s", ConcordDelaySeconds(def, 0.8f), 10);
        Equal("0.7 -> 15s", ConcordDelaySeconds(def, 0.7f), 15);
        Equal("0.6 -> 20s", ConcordDelaySeconds(def, 0.6f), 20);
        Equal("0.5 -> 25s", ConcordDelaySeconds(def, 0.5f), 25);
        Equal("below 0.5 counts as 0.5", ConcordDelaySeconds(def, 0.3f), 25);
        Equal("custom table (the old default) 0.5 -> 19s", ConcordDelaySeconds("6,6,7,10,14,19", 0.5f), 19);
        Equal("short table -> default", ConcordDelaySeconds("1,2,3", 0.8f), 10);
        Equal("empty table -> default", ConcordDelaySeconds("", 1.0f), 2);
        Equal("spaces are fine", ConcordDelaySeconds("1, 2, 3, 4, 5, 6", 0.6f), 5);
    }

    ::printf("\n faction police thresholds (4.9)\n");
    {
        True("1.0: -2.0 is wanted", PoliceWanted(-2.0f, 1.0f));
        True("1.0: -1.9 is not", !PoliceWanted(-1.9f, 1.0f));
        True("0.7: -3.5 is wanted (float rounding of the threshold)", PoliceWanted(-3.5f, 0.7f));
        True("0.7: -3.4 is not", !PoliceWanted(-3.4f, 0.7f));
        True("0.5: -4.5 is wanted", PoliceWanted(-4.5f, 0.5f));
        True("0.5: -4.46 shows as -4.5: wanted", PoliceWanted(-4.46f, 0.5f));
        True("0.5: -4.44 shows as -4.4: not", !PoliceWanted(-4.44f, 0.5f));
        True("navy: -5.0 is wanted at -5.0", NavyWanted(-5.0f, -5.0f));
        True("navy: -4.99 is not", !NavyWanted(-4.99f, -5.0f));
    }

    ::printf("\n Crimewatch 1 sentries (4.10)\n");
    {
        SentryCandidate c = SentryCandidate();
            c.inRange = true;
        True("innocent:  no", !SentryMayShoot(c));
        c.criminal = true;
        True("criminal:  yes", SentryMayShoot(c));
        c.pod = true;
        True("criminal in a pod:  no", !SentryMayShoot(c));
        c.pod = false;
        c.hidden = true;
        True("criminal cloaked or warping:  no", !SentryMayShoot(c));
        c.hidden = false;
        c.inRange = false;
        True("criminal out of range:  no", !SentryMayShoot(c));
        c = SentryCandidate();
        c.inRange = true;
        c.objectAggression = true;
        True("aggression with the gate/station:  yes", SentryMayShoot(c));
        c.objectAggression = false;
        c.factionAggression = true;
        True("npc faction aggression timer:  yes", SentryMayShoot(c));

        const int64_t s(10000000);      // filetime second
        True("aggression live inside 900s", Live(100 * s, 900 * s, 999 * s));
        True("aggression over at 900s", !Live(100 * s, 900 * s, 1000 * s));
        True("never stamped:  not live", !Live(0, 900 * s, 1));
        True("duration 0:  never live", !Live(100 * s, 0, 100 * s));

        Equal("pick of 3 at u 0", static_cast<long long>(PickIndex(3, 0.0)), 0);
        Equal("pick of 3 at u 0.5", static_cast<long long>(PickIndex(3, 0.5)), 1);
        Equal("pick of 3 at u 0.999999", static_cast<long long>(PickIndex(3, 0.999999)), 2);
        Equal("pick of 3 at u 1 (clamped)", static_cast<long long>(PickIndex(3, 1.0)), 2);
        Equal("pick of 0", static_cast<long long>(PickIndex(0, 0.5)), 0);
    }

    ::printf("\n law data (section 3)\n");
    {
        // gate guns:  (16 + 16 + 28 + 28) x 5 / 2.5s, every shot a hit with quality 1.0
        True("gate sentry 176 dps", std::fabs(NpcMath::TurretDps(16 + 16 + 28 + 28, 5, 2500) - 176.0) < 1e-9);
        // CONCORD tracking 100 rad/s and optimal 200 km:  effectively always hits (4.8)
        NpcMath::TurretInputs in = NpcMath::TurretInputs();
            in.angular = 0.5;           // 2500 m/s at 5 km
            in.surface = 5000;
            in.optimal = 200000;
            in.falloff = 50000;
            in.tracking = 100;
            in.sigRes = 100;
            in.targetSig = 25;
        True("CONCORD commander vs a fast frigate:  chance > 0.999", NpcMath::TurretChanceToHit(in) > 0.999);
        // CONCORD's web:  -95%
        double web(0.05);
        True("CONCORD web leaves 5% speed", std::fabs(NpcMath::StackedProduct(&web, 1) - 0.05) < 1e-9);
    }

    ::printf("\n%d failure(s)\n", s_failures);
    return (s_failures == 0 ? 0 : 1);
}
