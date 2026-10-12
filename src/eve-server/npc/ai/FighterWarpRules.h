 /**
  * @name FighterWarpRules.h
  *   the pure rules of fighters (and fighter bombers) warping within a system:  after their controlling ship, and with the
  *   "Fighter attack and follow" setting (attribute 1283) after a target that warps off.  DroneAIMgr::WarpAfter() uses them.
  *   header only and dependency free (also used by eve-test, npcai/FighterWarpTest)
  *
  *   Crucible:  fighters "are capable of warping, and will pursue a target into warp -- the only escape from fighters is to
  *   jump system or to dock" (EVE University wiki, Drones, 2012-05-29, after EVElopedia's Fighters page).  ordinary drones
  *   never warp
  */

#ifndef _EVE_NPC_AI_FIGHTERWARPRULES_H_
#define _EVE_NPC_AI_FIGHTERWARPRULES_H_

#include <cstdint>

namespace FighterWarp {

/* fighters come out of warp this far short of the point they warp to ("they do come out 15Km short I belive", EVE forums,
 *  "FIGHTERS SHOULD WARP", 2008-09-24.  the only number found).  it is the warp-to range of their warp */
const double WarpInRange = 15000.0;
/* a warp (or the short-range goto destiny turns it into) already under way to within this of the new destination is left
 *  alone:  the anchor's exit point is fixed once it is in warp, and a landed anchor barely moves while we align */
const double SameDestination = 10000.0;

enum class Step : uint8_t {
    Stay    = 0,    // nothing to warp for (or we can't):  carry on as a drone
    Wait    = 1,    // the anchor is warping to about where we are already:  wait for it here
    Warp    = 2     // warp to the destination
};

struct Situation {
    bool canWarp;               // a fighter or bomber that isn't warp scrambled
    bool sameBubble;            // the anchor (controller or target) is in our bubble
    bool anchorAligning;        // the anchor is aligning to warp (destination not final yet)
    bool anchorWarping;         // the anchor is in warp proper:  the destination is its exit point
    bool destinationBlocked;    // the destination is past an acceleration gate (deadspace:  "Fighters will not follow you into DED space")
    double distance;            // from us to the destination
};

inline Step Decide(const Situation& s)
{
    if (!s.canWarp)
        return Step::Stay;
    /* an anchor aligning to warp:  in our bubble we carry on (orbiting it, fighting it) until it is gone.  elsewhere we wait
     *  for its warp, which tells us where it is going */
    if (s.anchorAligning)
        return (s.sameBubble ? Step::Stay : Step::Wait);
    // an anchor in our bubble that isn't warping is reached by flying
    if (!s.anchorWarping and s.sameBubble)
        return Step::Stay;
    if (s.destinationBlocked)
        return Step::Stay;
    // we got there first (fighters warp at 1 AU/s, carriers at 0.5)
    if (s.anchorWarping and (s.distance <= WarpInRange * 2.0))
        return Step::Wait;
    return Step::Warp;
}

/* a new warp command is needed unless one to (about) the same destination is under way.  underWay:  our ball is aligning to
 *  warp, in warp, or on the goto that destiny makes of a warp under 100 km */
inline bool NeedsNewWarp(bool underWay, double destinationShift)
{
    return (!underWay or (destinationShift > SameDestination));
}

/* engaged fighters and bombers whose controlling pilot has "Fighter attack and follow" on keep their target when it warps
 *  off (they warp after it) or leaves control range.  everything else drops it, as drones do */
inline bool FollowsTarget(bool fighter, bool attackAndFollow)
{
    return (fighter and attackAndFollow);
}

}   // namespace FighterWarp

#endif  // _EVE_NPC_AI_FIGHTERWARPRULES_H_
