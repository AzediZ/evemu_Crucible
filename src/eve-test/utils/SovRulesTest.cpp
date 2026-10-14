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

#include <cstring>

#include "EVE_Sov.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    // mapSolarSystems rows (Crucible static data)
    static constexpr uint32_t Jita = 30000142,          CaldariState = 500001;  // 0.95
    static constexpr uint32_t Tama = 30002813;                                  // 0.28, FW, Caldari owned
    static constexpr uint32_t VenalSystem = 30001290,   Guristas = 500010;     // Venal (NPC null)
    static constexpr uint32_t GE8JV = 30001198;                                 // Catch, no faction
    static constexpr uint32_t J100820 = 31002479;                               // w-space
    static constexpr uint32_t SomeAlliance = 99000001;
}

int utils_SovRulesTest( int /*argc*/, char* /*argv*/[] )
{
    using namespace SovRules;
    ::printf("\nSovereignty rules\n");

    // what kind of space
    Check(Classify(Jita, CaldariState, 0.946) == SpaceKind::Empire, "Jita is empire space");
    Check(Classify(Tama, CaldariState, 0.283) == SpaceKind::Empire, "a low-sec FW system is empire space");
    Check(Classify(VenalSystem, Guristas, -0.25) == SpaceKind::NpcNull, "a Venal system is npc null-sec");
    Check(Classify(VenalSystem, Guristas, 0.0) == SpaceKind::NpcNull, "a 0.0 faction system is npc null-sec");
    Check(Classify(GE8JV, 0, -0.21) == SpaceKind::Claimable, "a faction-free null system is claimable");
    Check(Classify(J100820, 0, -0.99) == SpaceKind::Wormhole, "w-space is w-space");
    Check(Classify(J100820, Guristas, -0.99) == SpaceKind::Wormhole, "w-space stays w-space whatever its faction");

    // where alliances can claim
    Check(!IsClaimable(SpaceKind::Empire), "empire can't be claimed");
    Check(!IsClaimable(SpaceKind::NpcNull), "npc null-sec can't be claimed");
    Check(IsClaimable(SpaceKind::Claimable), "faction-free null-sec can be claimed");
    Check(!IsClaimable(SpaceKind::Wormhole), "w-space can't be claimed");

    // sovMgr.GetSystemSovereigntyInfo is None unless an alliance holds claimable space
    Check(!ReportsClaim(SpaceKind::Claimable, 0), "unclaimed system reports None");
    Check(ReportsClaim(SpaceKind::Claimable, SomeAlliance), "claimed system reports the claim");
    Check(!ReportsClaim(SpaceKind::Empire, SomeAlliance), "a stray claim in empire reports None");
    Check(!ReportsClaim(SpaceKind::NpcNull, SomeAlliance), "a stray claim in npc null-sec reports None");
    Check(!ReportsClaim(SpaceKind::Wormhole, SomeAlliance), "a stray claim in w-space reports None");

    // the sovereign the client shows
    Check(DisplayedHolder(SpaceKind::Empire, CaldariState, 0) == CaldariState, "Jita shows Caldari State");
    Check(DisplayedHolder(SpaceKind::NpcNull, Guristas, 0) == Guristas, "Venal shows Guristas Pirates");
    Check(DisplayedHolder(SpaceKind::NpcNull, Guristas, SomeAlliance) == Guristas, "an npc faction can't be displaced by a claim");
    Check(DisplayedHolder(SpaceKind::Claimable, 0, SomeAlliance) == SomeAlliance, "a claimed system shows its alliance");
    Check(DisplayedHolder(SpaceKind::Claimable, 0, 0) == 0, "an unclaimed system shows nobody");
    Check(DisplayedHolder(SpaceKind::Wormhole, 0, SomeAlliance) == 0, "w-space shows nobody");

    // sovereignty structure launches
    Check(::strlen(LaunchRefusal(SpaceKind::Claimable)) == 0, "sov structures launch in claimable space");
    Check(::strlen(LaunchRefusal(SpaceKind::Empire)) > 0, "no sov structures in empire");
    Check(::strlen(LaunchRefusal(SpaceKind::NpcNull)) > 0, "no sov structures in npc null-sec");
    Check(::strlen(LaunchRefusal(SpaceKind::Wormhole)) > 0, "no sov structures in w-space");

    ::printf("%d failure(s)\n", failures);
    return (failures == 0 ? 0 : 1);
}
