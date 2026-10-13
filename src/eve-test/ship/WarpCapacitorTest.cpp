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

#include "EVE_Warp.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Near(double a, double b, double eps = 1e-6)
    {
        return (std::fabs(a - b) <= eps);
    }
}

int ship_WarpCapacitorTest( int /*argc*/, char* /*argv*/[] )
{
    ::printf("\nWarp capacitor need\n");
    using namespace WarpCapacitor;

    // Bustard:  mass 20,000,000 kg, warpCapacitorNeed 8.13e-7, capacitorCapacity 1093.75
    const double mass(20000000.0), need(0.000000813);
    Check(Near(PerAU(mass, need), 16.26), "Bustard: 16.26 GJ per AU");
    Check(Near(Need(mass, need, 10.0 * AU), 162.6), "Bustard, 10 AU, no Warp Drive Operation: 162.6 GJ");
    // Warp Drive Operation:  warpCapacitorNeedBonus -10 x level, PostPercent on the ship's warpCapacitorNeed
    const double expect[6] = { 162.6, 146.34, 130.08, 113.82, 97.56, 81.3 };
    for (int lvl = 0; lvl <= 5; ++lvl) {
        double modified(need * (1.0 + (-10.0 * lvl) / 100.0));
        char what[96];
        ::snprintf(what, sizeof(what), "Bustard, 10 AU, Warp Drive Operation %d: %.2f GJ", lvl, expect[lvl]);
        Check(Near(Need(mass, modified, 10.0 * AU), expect[lvl]), what);
    }
    // linear in distance and mass
    Check(Near(Need(mass, need, 20.0 * AU), 2.0 * Need(mass, need, 10.0 * AU)), "twice the distance, twice the capacitor");
    Check(Near(Need(mass * 2.0, need, AU), 2.0 * Need(mass, need, AU)), "twice the mass, twice the capacitor");
    // what a full capacitor reaches
    Check(Near(Reach(1093.75, mass, need) / AU, 1093.75 / 16.26), "a full Bustard capacitor reaches 67.3 AU");
    Check(Near(Reach(Need(mass, need, 7.5 * AU), mass, need), 7.5 * AU, 1e-3), "Reach undoes Need");
    Check(Near(Reach(0.0, mass, need), 0.0), "an empty capacitor goes nowhere");
    Check(Near(Reach(-5.0, mass, need), 0.0), "a negative charge goes nowhere");
    // guards
    Check(Near(Need(mass, need, -AU), 0.0), "negative distance costs nothing");
    Check(Near(Need(mass, 0.0, AU), 0.0), "no warpCapacitorNeed, no cost");
    Check(Near(Need(0.0, need, AU), 0.0), "no mass, no cost");
    Check(std::isinf(Reach(10.0, mass, 0.0)), "no warpCapacitorNeed, any distance");

    ::printf("%s\n", (failures ? "WarpCapacitorTest FAILED" : "WarpCapacitorTest passed"));
    return (failures ? 1 : 0);
}
