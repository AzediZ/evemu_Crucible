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

#include <cstdint>
#include <cstdio>
#include <vector>

#include "EVE_StructureTiming.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Plan(int8_t state, StructureTiming::GMAction action, std::vector<StructureTiming::Step> want)
    {
        return (StructureTiming::GMPlan(state, action) == want);
    }
}

int utils_StructureTimingTest( int /*argc*/, char* /*argv*/[] )
{
    using namespace StructureTiming;
    const int64_t ms(FileTimeMs);
    const int64_t now(130000000000000000LL);    // a FILETIME in 2012
    const int64_t anchoring(1800000);           // a large tower's anchoringDelay, 30 min
    const int64_t onlining(28800000);           // a TCU's onliningDelay, 8 h

    ::printf("\nStructureTiming: the change each posState counts down\n");
    Check(ChangeFor(1) == Change::Anchoring, "posState 1 (anchored) counts anchoringDelay");
    Check(ChangeFor(2) == Change::Onlining, "posState 2 (onlining) counts onliningDelay");
    Check(ChangeFor(0) == Change::Unanchoring, "posState 0 (unanchored) counts unanchoringDelay");
    Check((ChangeFor(4) == Change::None) and (ChangeFor(6) == Change::None) and (ChangeFor(9) == Change::None),
          "online, vulnerable and invulnerable count nothing");
    Check(ChangeFor(-1) == Change::None, "nor does incapacitated");

    ::printf("what the client shows:\n");
    Check(ClientCountsDown(1, true, now, anchoring, now), "anchoring that just started counts down");
    Check(ClientCountsDown(1, true, now, anchoring, now + (anchoring - 1000) * ms), "... until the delay is over");
    Check(!ClientCountsDown(1, true, now, anchoring, now + anchoring * ms), "then it is anchored");
    Check(!ClientCountsDown(1, false, 0, anchoring, now), "no posTimestamp, no countdown (the sov structures' bug)");
    Check(ClientCountsDown(2, true, now, onlining, now + 3600000 * ms), "onlining counts down too");
    Check(!ClientCountsDown(4, true, now, onlining, now), "online doesn't");
    Check(!ClientCountsDown(1, true, SettledStamp(now, anchoring), anchoring, now), "a settled stamp shows the change done");
    Check(!ClientCountsDown(0, true, SettledStamp(now, 900000), 900000, now), "... unanchored too");
    Check(SettledStamp(now, anchoring) == now - anchoring * ms, "settled:  now less the delay, in FILETIME");
    Check(!HasStamp(0) and HasStamp(now), "0 is no stamp (the slim item says None)");

    ::printf("time left:\n");
    Check(RemainingMs(now, anchoring, now) == anchoring, "all of it when it starts");
    Check(RemainingMs(now - 60000 * ms, anchoring, now) == anchoring - 60000, "a minute less a minute later");
    Check(RemainingMs(SettledStamp(now, anchoring), anchoring, now) == 0, "none when settled");

    ::printf("after a server restart:\n");
    Check(ResumeTimerMs(1, now - 600000 * ms, anchoring, now) == anchoring - 600000, "anchoring carries on for what is left");
    Check(ResumeTimerMs(0, now - 1000 * ms, 900000, now) == 899000, "so does unanchoring");
    Check(ResumeTimerMs(1, SettledStamp(now, anchoring), anchoring, now) == 0, "a finished anchoring isn't redone");
    Check(ResumeTimerMs(2, now - onlining * 2 * ms, onlining, now) == 1, "an onlining past its time finishes now");
    Check(ResumeTimerMs(2, now, onlining, now) == onlining, "a fresh one runs its full time");
    Check(ResumeTimerMs(4, now, onlining, now) == 0, "online:  nothing to resume");
    Check(ResumeTimerMs(1, 0, anchoring, now) == 0, "no stamp:  nothing to resume");

    ::printf("GM commands:\n");
    Check(ParseGMAction("anchor") == GMAction::Anchor, "anchor");
    Check(ParseGMAction("ONLINE") == GMAction::Online, "online (any case)");
    Check(ParseGMAction("Offline") == GMAction::Offline, "offline");
    Check(ParseGMAction("unanchor") == GMAction::Unanchor, "unanchor");
    Check(ParseGMAction("complete") == GMAction::Complete, "complete");
    Check((ParseGMAction("fuel") == GMAction::Invalid) and (ParseGMAction("onlin") == GMAction::Invalid)
          and (ParseGMAction("onlinex") == GMAction::Invalid) and (ParseGMAction(nullptr) == GMAction::Invalid),
          "anything else is invalid");

    Check(Plan(0, GMAction::Anchor, {Step::Anchor}), "anchor an unanchored structure");
    Check(Plan(1, GMAction::Anchor, {}), "anchoring an anchored one does nothing");
    Check(Plan(0, GMAction::Online, {Step::Anchor, Step::Online}), "online an unanchored one:  anchor, then online");
    Check(Plan(1, GMAction::Online, {Step::Online}), "online an anchored one");
    Check(Plan(4, GMAction::Online, {}) and Plan(6, GMAction::Online, {}), "online or vulnerable:  nothing to do");
    Check(Plan(4, GMAction::Offline, {Step::Offline}) and Plan(9, GMAction::Offline, {Step::Offline})
          and Plan(3, GMAction::Offline, {Step::Offline}), "offline an online, invulnerable or reinforced one");
    Check(Plan(1, GMAction::Offline, {}) and Plan(0, GMAction::Offline, {}), "an offline one stays offline");
    Check(Plan(4, GMAction::Unanchor, {Step::Offline, Step::Unanchor}), "unanchor an online one:  offline, then unanchor");
    Check(Plan(1, GMAction::Unanchor, {Step::Unanchor}), "unanchor an anchored one");
    Check(Plan(0, GMAction::Unanchor, {}), "an unanchored one stays so");
    Check(Plan(4, GMAction::Complete, {}), "complete only finishes the running change");
    Check(Plan(-1, GMAction::Online, {}) and Plan(-1, GMAction::Unanchor, {}), "incapacitated structures are left alone");

    ::printf("%s\n", (failures ? "StructureTimingTest FAILED" : "StructureTimingTest passed"));
    return failures;
}
