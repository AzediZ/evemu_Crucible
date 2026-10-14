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
*/

#include "eve-test.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "EVE_Logoff.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Near(double a, double b, double eps = 1e-3)
    {
        return (std::fabs(a - b) <= eps);
    }

    Logoff::Vec V(double x, double y, double z)
    {
        Logoff::Vec v;
        v.x = x;
        v.y = y;
        v.z = z;
        return v;
    }
}

int utils_LogoffRulesTest( int /*argc*/, char* /*argv*/[] )
{
    using namespace Logoff;
    const double AU(149597870700.0);
    const double radius(30.0 * AU);         // a system 30 AU across
    const double minWarp(130000.0);         // the server's minWarpDistance
    const int64_t second(10000000LL);       // FILETIME units

    ::printf("\nLogoff: emergency warp\n");
    Check(Near(EmergencyWarpDistance, 1.0e9), "the emergency warp is 1,000,000 km");
    Logoff::Vec from(V(1.0e11, -2.0e10, 3.0e9));
    Logoff::Vec dest(EmergencyWarpPoint(from, V(0.0, 0.0, -1.0)));
    Check(Near(Distance(from, dest), 1.0e9), "its destination is 1,000,000 km from where the pilot logged off");
    Check(Near(dest.z, 3.0e9 - 1.0e9) and Near(dest.x, from.x) and Near(dest.y, from.y), "along the random direction");

    ::printf("what the ship does:\n");
    Check(OnLogoff(true, false) == Exit::FinishWarp, "a ship in warp finishes its warp first");
    Check(OnLogoff(true, true) == Exit::FinishWarp, "even when scrambled (a warp under way can't be stopped)");
    Check(OnLogoff(false, false) == Exit::Warp, "a ship at rest makes its emergency warp");
    Check(OnLogoff(false, true) == Exit::Stay, "a scrambled ship stays where it is");

    ::printf("when it leaves space:\n");
    const int64_t rest(130000000000000000LL);
    Check(LeaveSpaceAt(rest, false, 900 * second, 60 * second) == rest + 60 * second, "a minute after it came to rest");
    Check(LeaveSpaceAt(rest, false, 900 * second, 120 * second) == rest + 120 * second, "the npc time when npcs were on it");
    Check(LeaveSpaceAt(rest, true, 900 * second, 60 * second) == rest + 900 * second, "15 minutes in a fight with players");

    ::printf("usable points:\n");
    Check(Usable(V(1.0e12, 0.0, -2.0e11), radius), "a point inside the system");
    Check(Usable(V(2.0 * radius, 0.0, 0.0), radius), "a deep safe outside the system's radius");
    Check(!Usable(V(0.0, 0.0, 0.0), radius), "not the origin (inside the sun:  never saved on purpose)");
    Check(!Usable(V(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), radius), "not NaN");
    Check(!Usable(V(std::numeric_limits<double>::infinity(), 0.0, 0.0), radius), "not infinite");
    Check(!Usable(V(5.0 * radius, 0.0, 0.0), radius), "not far outside the system");
    Check(Usable(V(50.0 * AU, 0.0, 0.0), 0.0), "the system's radius unknown:  up to 1000 AU");
    Check(!Usable(V(2000.0 * AU, 0.0, 0.0), 0.0), "... and no further");

    ::printf("login:\n");
    Logoff::Vec logoff(V(4.0e11, 1.0e9, -7.5e10));
    Logoff::Vec dir(V(0.6, 0.0, 0.8));
    LoginPlan gone(PlanLogin(logoff, false, logoff, dir, minWarp));
    Check(gone.warp, "a ship that left space is in warp back to its logoff point");
    Check(Near(Distance(gone.start, logoff), 1.0e9), "from 1,000,000 km away");
    Check(Near(gone.target.x, logoff.x) and Near(gone.target.y, logoff.y) and Near(gone.target.z, logoff.z),
          "to the logoff point itself, not the safe spot");
    Logoff::Vec safeSpot(EmergencyWarpPoint(logoff, V(-1.0, 0.0, 0.0)));
    LoginPlan parked(PlanLogin(logoff, true, safeSpot, dir, minWarp));
    Check(parked.warp, "a ship still at its safe spot is in warp back too");
    Check(Near(parked.start.x, safeSpot.x) and Near(parked.start.y, safeSpot.y) and Near(parked.start.z, safeSpot.z),
          "from where it sits");
    Check(Near(Distance(parked.target, logoff), 0.0), "to the logoff point");
    Logoff::Vec drifted(V(logoff.x + 20000.0, logoff.y, logoff.z));
    LoginPlan tackled(PlanLogin(logoff, true, drifted, dir, minWarp));
    Check(!tackled.warp, "a scrambled ship that never left:  no warp");
    Check(Near(tackled.start.x, drifted.x), "it is where it is");
    LoginPlan edge(PlanLogin(logoff, true, V(logoff.x + minWarp, logoff.y, logoff.z), dir, minWarp));
    Check(edge.warp, "the shortest warp the server takes is still a warp");

    ::printf("%s\n", (failures ? "LogoffRulesTest FAILED" : "LogoffRulesTest passed"));
    return failures;
}
