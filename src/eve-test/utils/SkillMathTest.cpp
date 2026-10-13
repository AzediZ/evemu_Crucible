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

#include <cstdio>

/* skill points <-> skill level (EvEMath::Skill), used by training, free skill points, gm gifts and clone loss.
 *  SP(level) = ceil(250 * rank * 2^(2.5 * (level - 1))), client skillUtil.GetSPForLevelRaw */
namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }
}

int utils_SkillMathTest( int /*argc*/, char* /*argv*/[] )
{
    ::printf("\nSkill math\n");

    // rank 1 table: 250, 1415, 8000, 45255, 256000
    Check(EvEMath::Skill::PointsAtLevel(0, 1) == 0, "level 0 is 0 sp");
    Check(EvEMath::Skill::PointsAtLevel(1, 1) == 250, "rank 1 level 1 is 250 sp");
    Check(EvEMath::Skill::PointsAtLevel(2, 1) == 1415, "rank 1 level 2 is 1415 sp");
    Check(EvEMath::Skill::PointsAtLevel(3, 1) == 8000, "rank 1 level 3 is 8000 sp");
    Check(EvEMath::Skill::PointsAtLevel(4, 1) == 45255, "rank 1 level 4 is 45255 sp");
    Check(EvEMath::Skill::PointsAtLevel(5, 1) == 256000, "rank 1 level 5 is 256000 sp");
    Check(EvEMath::Skill::PointsAtLevel(6, 1) == 256000, "levels past 5 are level 5");
    Check(EvEMath::Skill::PointsAtLevel(4, 3) == 135765, "rank 3 level 4 is 135765 sp");
    Check(EvEMath::Skill::PointsAtLevel(5, 16) == 4096000, "rank 16 level 5 is 4096000 sp");

    // the level for sp: exactly on a boundary is that level, one short is the level below
    bool exact(true), below(true), above(true);
    const float ranks[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 14, 16};
    for (float rank : ranks) {
        for (uint8 level = 1; level <= EvESkill::MAXSKILLLEVEL; ++level) {
            uint32 sp(EvEMath::Skill::PointsAtLevel(level, rank));
            if (EvEMath::Skill::LevelForPoints(sp, rank) != level) {
                ::printf("        rank %.0f: %u sp gave level %u, not %u\n", rank, sp, (unsigned)EvEMath::Skill::LevelForPoints(sp, rank), (unsigned)(level));
                exact = false;
            }
            if (EvEMath::Skill::LevelForPoints(sp - 1, rank) != level - 1) {
                ::printf("        rank %.0f: %u sp gave level %u, not %u\n", rank, sp - 1, (unsigned)EvEMath::Skill::LevelForPoints(sp - 1, rank), (unsigned)(level - 1));
                below = false;
            }
            if (EvEMath::Skill::LevelForPoints(sp + 1, rank) != level) {
                ::printf("        rank %.0f: %u sp gave level %u, not %u\n", rank, sp + 1, (unsigned)EvEMath::Skill::LevelForPoints(sp + 1, rank), (unsigned)(level));
                above = false;
            }
        }
    }
    Check(exact, "sp exactly at a level give that level (ranks 1-16, levels 1-5)");
    Check(below, "one sp short of a level gives the level below");
    Check(above, "one sp past a level gives that level");

    // free skill points crossing several levels at once, and the level 5 cap
    Check(EvEMath::Skill::LevelForPoints(0, 1) == 0, "0 sp is level 0");
    Check(EvEMath::Skill::LevelForPoints(250 + 9000, 1) == 3, "250 sp + 9000 free sp is level 3 (two levels at once)");
    Check(EvEMath::Skill::LevelForPoints(10000000, 1) == 5, "more than level 5 sp is level 5");
    Check(EvEMath::Skill::LevelForPoints(1000, 0) == 0, "rank 0 is level 0");

    ::printf("%s\n", (failures ? "Skill math: FAILED" : "Skill math: passed"));
    return (failures ? 1 : 0);
}
