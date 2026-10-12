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
    fighters warping after their controller and after a warping target (FighterWarpRules.h)
*/

#include "eve-test.h"

#include "FighterWarpRules.h"

namespace {
    int s_failures = 0;

    void True(const char* what, bool ok)
    {
        ::printf("  %-62s %s\n", what, (ok ? "ok  " : "FAIL"));
        if (!ok)
            ++s_failures;
    }

    // a fighter that can warp, anchor elsewhere and landed, 5 AU away
    FighterWarp::Situation Base()
    {
        FighterWarp::Situation s;
            s.canWarp = true;
            s.sameBubble = false;
            s.anchorAligning = false;
            s.anchorWarping = false;
            s.destinationBlocked = false;
            s.distance = 5.0 * 149597870700.0;
        return s;
    }
}

int npcai_FighterWarpTest( int argc, char* argv[] )
{
    using namespace FighterWarp;

    ::printf("\n when to warp\n");
    {
        Situation s(Base());
        True("anchor landed in another bubble:  warp to it", Decide(s) == Step::Warp);
        s.sameBubble = true;
        True("anchor in our bubble, not warping:  fly (stay a drone)", Decide(s) == Step::Stay);
        s.anchorWarping = true;
        True("anchor warping off from our bubble:  warp after it", Decide(s) == Step::Warp);
        s = Base();
        s.anchorAligning = true;
        True("anchor aligning in another bubble:  wait for its warp", Decide(s) == Step::Wait);
        s.sameBubble = true;
        True("anchor aligning in our bubble:  carry on until it goes", Decide(s) == Step::Stay);
        s = Base();
        s.canWarp = false;
        True("warp scrambled (or not a fighter):  stay", Decide(s) == Step::Stay);
        s = Base();
        s.destinationBlocked = true;
        True("destination past an acceleration gate:  stay", Decide(s) == Step::Stay);
        s.anchorWarping = true;
        True("...also when the anchor is warping there", Decide(s) == Step::Stay);
    }

    ::printf("\n arriving first\n");
    {
        Situation s(Base());
        s.anchorWarping = true;
        s.distance = WarpInRange * 2.0;
        True("anchor warping to about where we are:  wait for it", Decide(s) == Step::Wait);
        s.distance = WarpInRange * 2.0 + 1.0;
        True("...a bit further:  warp", Decide(s) == Step::Warp);
        s.anchorWarping = false;
        s.distance = 1000.0;
        True("landed anchor in another bubble close by:  warp (destiny makes it a goto)", Decide(s) == Step::Warp);
    }

    ::printf("\n not repeating a warp\n");
    {
        True("nothing under way:  send it", NeedsNewWarp(false, 0.0));
        True("under way to the same point:  leave it", !NeedsNewWarp(true, 0.0));
        True("under way, anchor moved a little:  leave it", !NeedsNewWarp(true, SameDestination));
        True("under way, anchor moved further:  send a new one", NeedsNewWarp(true, SameDestination + 1.0));
    }

    ::printf("\n attack and follow\n");
    {
        True("fighter with the setting on keeps its target", FollowsTarget(true, true));
        True("fighter with the setting off drops it", !FollowsTarget(true, false));
        True("ordinary drone never follows", !FollowsTarget(false, true));
        True("landing range is the 15 km reported", WarpInRange == 15000.0);
    }

    return (s_failures == 0 ? 0 : 1);
}
