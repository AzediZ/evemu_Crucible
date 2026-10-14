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
#include <cstdio>

#include "EVE_Customs.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Near(double a, double b)
    {
        return std::fabs(a - b) < 1e-6;
    }

    // a configured office:  every rate different, so the test sees which one was picked
    CustomsRules::PlayerOffice Configured(bool allowAlliance, bool allowStandings, int8_t minStanding)
    {
        CustomsRules::PlayerOffice office = CustomsRules::PlayerOffice();
            office.allowAlliance    = allowAlliance;
            office.allowStandings   = allowStandings;
            office.minStanding      = minStanding;
            office.corporation      = 0.00f;
            office.alliance         = 0.02f;
            office.standingHorrible = 0.50f;
            office.standingBad      = 0.30f;
            office.standingNeutral  = 0.15f;
            office.standingGood     = 0.08f;
            office.standingHigh     = 0.04f;
        return office;
    }
}

int utils_CustomsRulesTest( int /*argc*/, char* /*argv*/[] )
{
    using namespace CustomsRules;

    ::printf("standing tiers (contact standings -10, -5, 0, 5, 10):\n");
    Check(StandingTier(false, 0) == StandingNeutral, "no standing is neutral");
    Check(StandingTier(false, -10) == StandingNeutral, "no standing is neutral whatever the value");
    Check(StandingTier(true, 10) == StandingHigh, "+10 is high");
    Check(StandingTier(true, 5) == StandingGood, "+5 is good");
    Check(StandingTier(true, 4.9) == StandingNeutral, "+4.9 is neutral");
    Check(StandingTier(true, 0) == StandingNeutral, "0 is neutral");
    Check(StandingTier(true, -4.9) == StandingNeutral, "-4.9 is neutral");
    Check(StandingTier(true, -5) == StandingBad, "-5 is bad");
    Check(StandingTier(true, -10) == StandingHorrible, "-10 is horrible");

    ::printf("a new office (Crucible 1.0.1:  5%%, only its corporation):\n");
    PlayerOffice fresh = PlayerOffice();
        fresh.corporation = fresh.alliance = fresh.standingHorrible = fresh.standingBad = (float)LaunchTaxRate;
        fresh.standingNeutral = fresh.standingGood = fresh.standingHigh = (float)LaunchTaxRate;
        fresh.minStanding = StandingNeutral;
    Check(Near(PlayerOfficeRate(fresh, true, false, StandingNeutral), 0.05), "the owner's corporation pays 5%");
    Check(PlayerOfficeRate(fresh, false, true, StandingNeutral) < 0, "its alliance is not let in");
    Check(PlayerOfficeRate(fresh, false, false, StandingHigh) < 0, "nor is anyone else");
    Check(!NeedsStanding(fresh, false, false), "no standing lookup when standings are off");

    ::printf("corporation and alliance rates:\n");
    PlayerOffice office = Configured(true, true, StandingBad);
    Check(Near(PlayerOfficeRate(office, true, true, StandingHorrible), 0.00), "the corporation rate first, whatever the standing");
    Check(Near(PlayerOfficeRate(office, false, true, StandingHorrible), 0.02), "then the alliance rate, when allowed");
    Check(!NeedsStanding(office, true, false), "no standing lookup for the corporation");
    Check(!NeedsStanding(office, false, true), "no standing lookup for the alliance when allowed");
    PlayerOffice noAlliance = Configured(false, true, StandingNeutral);
    Check(NeedsStanding(noAlliance, false, true), "alliance members go by standing when the alliance rate is off");
    Check(Near(PlayerOfficeRate(noAlliance, false, true, StandingGood), 0.08), "an alliance member at +5 pays the good rate");
    Check(PlayerOfficeRate(noAlliance, false, true, StandingBad) < 0, "an alliance member below the minimum is refused");
    Check(PlayerOfficeRate(Configured(false, false, StandingHorrible), false, true, StandingHigh) < 0,
          "with alliance and standings off only the corporation gets in");

    ::printf("standing rates, minimum bad:\n");
    Check(Near(PlayerOfficeRate(office, false, false, StandingHigh), 0.04), "+10 pays the high rate");
    Check(Near(PlayerOfficeRate(office, false, false, StandingGood), 0.08), "+5 pays the good rate");
    Check(Near(PlayerOfficeRate(office, false, false, StandingNeutral), 0.15), "neutral pays the neutral rate");
    Check(Near(PlayerOfficeRate(office, false, false, StandingBad), 0.30), "-5 pays the bad rate");
    Check(PlayerOfficeRate(office, false, false, StandingHorrible) < 0, "-10 is below the minimum:  refused");
    Check(Near(PlayerOfficeRate(Configured(true, true, StandingHorrible), false, false, StandingHorrible), 0.50),
          "-10 pays the horrible rate when the minimum is horrible");
    Check(PlayerOfficeRate(Configured(true, true, StandingHigh), false, false, StandingGood) < 0,
          "+5 is refused when the minimum is high");
    Check(Near(PlayerOfficeRate(Configured(true, true, StandingNeutral), false, false, StandingTier(false, 0)), 0.15),
          "a stranger with no standing pays the neutral rate");

    ::printf("gantries:\n");
    Check(!GantryAllowed(true), "not in high-sec (CONCORD's offices)");
    Check(GantryAllowed(false), "low-sec, null-sec (claimed or not) and w-space");

    ::printf("%s\n", (failures ? "CustomsRulesTest FAILED" : "CustomsRulesTest passed"));
    return failures;
}
