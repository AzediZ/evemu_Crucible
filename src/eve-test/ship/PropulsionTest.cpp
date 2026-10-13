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

#include "EVE_Propulsion.h"

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

int ship_PropulsionTest( int /*argc*/, char* /*argv*/[] )
{
    ::printf("\nPropulsion (afterburner / microwarpdrive)\n");

    // Rifter (mass 1,067,000 kg, maxVelocity 353 m/s) with 1MN modules (thrust 1,500,000, massAddition 500,000)
    const double rifterMass(1067000.0 + 500000.0);
    double ab(Propulsion::SpeedMultiplier(112.5, 1500000.0, rifterMass));
    Check(Near(ab, 1.0 + 1.125 * 1500000.0 / rifterMass), "1MN AB I on a Rifter:  1 + 1.125 x thrust / (mass + massAddition)");
    Check(Near(353.0 * ab, 733.1452, 1e-3), "1MN AB I Rifter:  353 -> 733.1 m/s");
    double mwd(Propulsion::SpeedMultiplier(500.0, 1500000.0, rifterMass));
    Check(Near(353.0 * mwd, 2042.534, 1e-3), "1MN MWD I Rifter:  353 -> 2042.5 m/s");
    // a module sized for its hull gives about its speedFactor, an oversized one far more
    Check((mwd > 5.5) and (mwd < 6.0), "1MN MWD on a frigate is just under +500%");
    double big(Propulsion::SpeedMultiplier(500.0, 15000000.0, 1067000.0 + 5000000.0));
    Check(big > mwd * 2.0, "10MN MWD on a frigate is much faster");
    // Acceleration Control V (+25% speedFactor) scales the bonus part only
    Check(Near(Propulsion::SpeedMultiplier(112.5 * 1.25, 1500000.0, rifterMass) - 1.0, (ab - 1.0) * 1.25), "speedFactor scales the bonus part");
    // a heavier ship (plates, warp field generator mass reductions undone) gets less
    Check(Propulsion::SpeedMultiplier(112.5, 1500000.0, rifterMass * 2.0) < ab, "more mass, less boost");
    // guards
    Check(Near(Propulsion::SpeedMultiplier(500.0, 1500000.0, 0.0), 1.0), "no mass, no boost");
    Check(Near(Propulsion::SpeedMultiplier(500.0, 0.0, rifterMass), 1.0), "no thrust, no boost");
    Check(Near(Propulsion::SpeedMultiplier(-500.0, 1500000.0, 1.0), 1.0), "never stops or reverses the ship");

    // signature radius bloom:  +500% on a tech 1 mwd (x6), +50% on a warp disruption field generator
    Check(Near(Propulsion::SignatureMultiplier(500.0), 6.0), "MWD signature bloom x6");
    Check(Near(35.0 * Propulsion::SignatureMultiplier(500.0), 210.0), "Rifter 35 m -> 210 m");
    Check(Near(Propulsion::SignatureMultiplier(0.0), 1.0), "afterburner: no bloom");

    ::printf("%s\n", (failures ? "PropulsionTest FAILED" : "PropulsionTest passed"));
    return (failures ? 1 : 0);
}
