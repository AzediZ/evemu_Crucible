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

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "EVE_Warp.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Near(double a, double b, double eps = 1e-9)
    {
        return (std::fabs(a - b) <= eps);
    }

    // a rig or implant:  PostPercent on the ship's warpSpeedMultiplier (600)
    double PostPercent(double value, double percent)
    {
        return value * (1.0 + percent / 100.0);
    }
}

int ship_WarpSpeedTest( int /*argc*/, char* /*argv*/[] )
{
    ::printf("\nWarp speed\n");
    using namespace WarpSpeed;

    // the client's formula (infosvc.GetBaseWarpSpeed):  max(1, baseWarpSpeed) x warpSpeedMultiplier x 3 AU/s
    // Bustard:  warpSpeedMultiplier 1, baseWarpSpeed 1.  Show Info says 3 AU/s
    Check(Near(AUPerSecond(1.0, 1.0), 3.0), "Bustard: 3 AU/s");
    Check(ShipFactor(1.0, 1.0) == 30, "Bustard: warpFactor 30 (destiny flies it at 30 x 0.1 AU/s)");
    // a heavy assault ship, recon or logistics cruiser:  warpSpeedMultiplier 1.25
    Check(Near(AUPerSecond(1.25, 1.0), 3.75), "warpSpeedMultiplier 1.25: 3.75 AU/s");
    Check(ShipFactor(1.25, 1.0) == 38, "warpSpeedMultiplier 1.25: warpFactor 38 (to the nearest tenth of an AU/s)");
    // a +20% warp speed rig on a ship with warpSpeedMultiplier 1
    double rigged(PostPercent(1.0, 20.0));
    Check(Near(rigged, 1.2), "+20% rig: warpSpeedMultiplier 1.2");
    Check(Near(AUPerSecond(rigged, 1.0), 3.6), "+20% rig: 3.6 AU/s");
    Check(ShipFactor(rigged, 1.0) == 36, "+20% rig: warpFactor 36");
    // as the server holds it (EvilNumber float):  1.2f is a hair over, 0.9f a hair under.  rounding keeps both
    Check(ShipFactor(static_cast<float>(rigged), 1.0) == 36, "+20% rig, single precision: warpFactor 36");
    Check(ShipFactor(static_cast<float>(0.9), 1.0) == 27, "Orca (0.9), single precision: warpFactor 27");
    // baseWarpSpeed over 1 multiplies (no ship in the Crucible data has one, but the client's formula allows it)
    Check(Near(AUPerSecond(1.0, 2.0), 6.0), "baseWarpSpeed 2: 6 AU/s");
    Check(Near(AUPerSecond(1.25, 1.5), 5.625), "baseWarpSpeed 1.5, warpSpeedMultiplier 1.25: 5.625 AU/s");
    Check(ShipFactor(1.25, 1.5) == 56, "baseWarpSpeed 1.5, warpSpeedMultiplier 1.25: warpFactor 56");
    // ... and under 1 counts as 1:  baseWarpSpeed's dgm default is 0
    Check(Near(AUPerSecond(1.0, 0.0), 3.0), "baseWarpSpeed unset (0): 3 AU/s");
    Check(Near(AUPerSecond(2.0, 0.5), 6.0), "baseWarpSpeed 0.5 counts as 1");
    Check(Near(AUPerSecond(1.0), 3.0), "no baseWarpSpeed given: 3 AU/s");
    // the Crucible spread:  freighters 0.75, carriers 1.5, battleships 3, frigates 6, covert ops 13.5 AU/s
    Check(ShipFactor(0.25, 1.0) == 8, "freighter (0.25): 0.75 AU/s goes as warpFactor 8");
    Check(ShipFactor(0.5, 1.0) == 15, "carrier (0.5): warpFactor 15");
    Check(ShipFactor(2.0, 1.0) == 60, "frigate (2): warpFactor 60");
    Check(ShipFactor(4.5, 1.0) == 135, "covert ops (4.5): warpFactor 135");
    // fleet warps take the slowest ship's warpFactor
    Check(std::min<int32_t>(ShipFactor(1.0, 1.0), ShipFactor(0.5, 1.0)) == 15, "Bustard with a carrier: the fleet warps at 1.5 AU/s");
    // guards:  destiny never takes a warpFactor under 1
    Check(Near(AUPerSecond(0.0, 1.0), 0.0), "no warpSpeedMultiplier, no speed");
    Check(ShipFactor(0.0, 1.0) == 1, "no warpSpeedMultiplier: warpFactor 1");
    Check(ShipFactor(-1.0, 1.0) == 1, "negative warpSpeedMultiplier: warpFactor 1");
    Check(ShipFactor(std::nan(""), 1.0) == 1, "NaN warpSpeedMultiplier: warpFactor 1");
    Check(Near(AUPerSecond(1.0, std::nan("")), 3.0), "NaN baseWarpSpeed counts as 1");
    Check(Factor(0.01) == 1, "0.01 AU/s: warpFactor 1");

    ::printf("%s\n", (failures ? "WarpSpeedTest FAILED" : "WarpSpeedTest passed"));
    return (failures ? 1 : 0);
}
