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

#include "EVE_Charges.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Shares(int32 available, std::vector<int32> room, std::vector<int32> want)
    {
        std::vector<int32> share;
        Charges::SplitAcrossBank(available, room, share);
        return (share == want);
    }
}

int ship_ChargeRulesTest( int /*argc*/, char* /*argv*/[] )
{
    ::printf("\nCharge rules\n");

    // reload time: hybrids have 5000, everything else takes the 10s dogma default
    Check(Charges::ReloadTimeMS(5000) == 5000, "hybrid reloadTime 5000 ms");
    Check(Charges::ReloadTimeMS(0) == 10000, "no reloadTime attribute is 10 s");
    Check(Charges::ReloadTimeMS(-1) == 10000, "a negative reloadTime is 10 s");

    // room in a module
    Check(Charges::RoomFor(0.4, 0.01, 0) == 40, "0.4 m3 holds 40 rounds of 0.01 m3");
    Check(Charges::RoomFor(0.4, 0.01, 15) == 25, "15 loaded leaves 25");
    Check(Charges::RoomFor(0.4, 0.01, 40) == 0, "full module has no room");
    Check(Charges::RoomFor(0.4, 0.01, 55) == 0, "over-full module has no room");
    Check(Charges::RoomFor(1.0, 1.0, 0) == 1, "a laser takes one crystal");
    Check(Charges::RoomFor(0.6, 0.025, 0) == 24, "0.6 m3 holds 24 missiles of 0.025 m3");
    Check(Charges::RoomFor(1.0, 0.0, 0) == 0, "a charge without volume doesn't load");
    Check(Charges::RoomFor(0.0, 0.01, 0) == 0, "a module without capacity takes nothing");

    // splitting a reload over a weapon bank
    Check(Shares(160, {40, 40, 40, 40}, {40, 40, 40, 40}), "enough for everyone fills the bank");
    Check(Shares(500, {40, 40}, {40, 40}), "more than enough only fills the room");
    Check(Shares(100, {40, 40, 40, 40}, {25, 25, 25, 25}), "short ammo is split evenly");
    Check(Shares(10, {40, 40, 40}, {4, 3, 3}), "the remainder goes to the first modules");
    Check(Shares(2, {40, 40, 40}, {1, 1, 0}), "fewer charges than modules");
    Check(Shares(60, {10, 40, 40}, {10, 25, 25}), "a nearly full module's share goes to the others");
    Check(Shares(30, {0, 40}, {0, 30}), "a full module gets none");
    Check(Shares(0, {40, 40}, {0, 0}), "nothing to load");
    Check(Shares(5, {}, {}), "no modules");

    // crystals
    Check(!Charges::CrystalBurnsOut(0.99975, 1.0), "a crystal short of its hp survives");
    Check(Charges::CrystalBurnsOut(1.0, 1.0), "a crystal at its hp is destroyed");
    Check(Charges::CrystalBurnsOut(1.0, 0.0), "no hp means 1");

    ::printf("%d failure(s)\n", failures);
    return (failures == 0 ? 0 : 1);
}
