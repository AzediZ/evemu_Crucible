 /**
  * @name NpcLod.h
  *   level of detail, cpu budget and difficulty presets of the npc ai (doc/code_and_design_notes/npc-ai/PLAN.md 6.7, phase 5)
  *
  *   level of detail (per npc and tick):
  *    Near   a player is in the npc's bubble:  the ai runs every tick (1 Hz)
  *    Far    players are in the system, not in the bubble:  an idle npc thinks every npc.LodFarInterval (4s), staggered
  *           by itemID;  a busy one (fighting, going home, warping, law) every tick
  *    Empty  no player in the system:  an idle npc doesn't think at all (npc.LodEmptySystem);  a busy one every tick
  *   movement (destiny) always runs.  the ai of npcs that aren't Near also waits for the next tick once the system used
  *   npc.TickBudget us of npc ai in this one, so the bubbles with players come first.
  *   difficulty (npc.Difficulty, private servers):  d > 1 harder.  damage x d, lock and reaction time / d;  each part can
  *   be set on its own (DifficultyDamage, DifficultyLockTime, DifficultyReactionTime;  0 = from d).
  *   header only and dependency free (also used by eve-test, npcai/NpcPolishTest)
  */

#ifndef _EVE_NPC_AI_NPCLOD_H_
#define _EVE_NPC_AI_NPCLOD_H_

#include <cmath>
#include <cstdint>

namespace NpcLod {

namespace Tier {
    enum : uint8_t { Near = 0, Far = 1, Empty = 2 };
}

inline uint8_t TierOf(bool playersInBubble, bool playersInSystem)
{
    if (playersInBubble)
        return Tier::Near;
    return (playersInSystem ? Tier::Far : Tier::Empty);
}

/* does an npc's ai run this tick (tick:  a counter that goes up by one each second)?  busy:  not idle, law enforcement,
 *  a brain that never sleeps, or something queued for it.  farEvery:  Far npcs think every farEvery ticks (0 or 1 = every
 *  tick), staggered by itemID.  emptySkips:  idle npcs in a system without players don't think */
inline bool RunsThisTick(uint8_t tier, bool busy, uint64_t tick, uint32_t itemID, uint32_t farEvery, bool emptySkips)
{
    if (busy or (tier == Tier::Near))
        return true;
    if ((tier == Tier::Empty) and emptySkips)
        return false;
    if (farEvery <= 1)
        return true;
    return (((tick + itemID) % farEvery) == 0);
}

/* the cpu budget:  npcs that aren't Near wait for the next tick once the system's npc ai used budgetUs in this one.
 *  budgetUs 0 = no budget */
inline bool OverBudget(uint8_t tier, double usedUs, uint32_t budgetUs)
{
    if ((tier == Tier::Near) or (budgetUs == 0))
        return false;
    return (usedUs >= static_cast<double>(budgetUs));
}

// the overall difficulty (<= 0 or not finite counts as 1)
inline double Overall(double difficulty)
{
    return ((std::isfinite(difficulty) and (difficulty > 0.0)) ? difficulty : 1.0);
}
// npc damage multiplier:  the part's own value when > 0, else the overall difficulty
inline double DamageMult(double difficulty, double damage)
{
    if (std::isfinite(damage) and (damage > 0.0))
        return damage;
    return Overall(difficulty);
}
// lock time or reaction delay multiplier:  the part's own value when > 0, else 1 / the overall difficulty
inline double TimeMult(double difficulty, double part)
{
    if (std::isfinite(part) and (part > 0.0))
        return part;
    return (1.0 / Overall(difficulty));
}

}   // namespace NpcLod

#endif  // _EVE_NPC_AI_NPCLOD_H_
