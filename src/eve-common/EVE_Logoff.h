 /**
  * @name EVE_Logoff.h
  *     a ship left in space by a pilot who logs off, and the warp back when they log in again (Crucible)
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  *  Crucible has no safe logoff (Rubicon, 2013):  Log Off and Quit Game just drop the connection (client
  *  eveCommands.py CmdLogOff -> appUtils.Reboot, CmdQuitGame -> bluepy.Terminate), so a logoff and a disconnect are the
  *  same thing to the server.
  *   - the ship stays in space.  if it is in warp it finishes that warp first.  then it makes one emergency warp to a
  *     random spot 1,000,000 km away, subject to warp disruption (scramblers, bubbles):  a tackled ship stays where it is.
  *   - it sits there for a minute and leaves space (crime.LogoffWarpTime;  crime.LogoffNPCTime if npcs were on it, a
  *     server choice:  no era source gives npc aggression its own time).  a pilot in a fight with players
  *     (const.aggressionTime, 15 minutes) leaves it there for 15 minutes, renewed while it is still being shot.
  *   - what is kept is where the ship was when the pilot logged off (or where it landed, for a logoff in warp).
  *   - on login the ship is in warp back to that point:  from where it sits if it is still in space, else from a random
  *     spot 1,000,000 km away.  the login warp ignores warp disruption.
  *  CCP news "Emergency warps" (2004), EVE Evolved aggression mechanics (2009), EVE forums (Dec 2011).
  *  deadspace pockets and wormholes are ordinary coordinates in a solar system, so the point always exists:  a pocket that
  *  has despawned leaves empty space there.  only a point that can't be used (not a number, the origin, far outside the
  *  system) falls back.
  */


#ifndef EVE_LOGOFF_H
#define EVE_LOGOFF_H

#include <cmath>
#include <cstdint>


namespace Logoff {

    // m.  the emergency warp at logoff, and where the warp back starts after the ship left space
    static constexpr double EmergencyWarpDistance = 1000000000.0;   // 1,000,000 km
    // m.  a point this far outside the system's radius times this factor is garbage
    static constexpr double MaxSystemRadiusFactor = 4.0;
    // m.  ... or this far from the sun when the system's radius isn't known (1000 AU)
    static constexpr double MaxDistanceNoRadius = 149597870700000.0;

    struct Vec {
        double x = 0.0, y = 0.0, z = 0.0;
    };

    inline double Distance(const Vec& a, const Vec& b)
    {
        double dx(a.x - b.x), dy(a.y - b.y), dz(a.z - b.z);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // a point the ship can be put at or warped to (systemRadius m, 0 = unknown)
    inline bool Usable(const Vec& p, double systemRadius)
    {
        if (!std::isfinite(p.x) or !std::isfinite(p.y) or !std::isfinite(p.z))
            return false;
        if ((p.x == 0.0) and (p.y == 0.0) and (p.z == 0.0))
            return false;
        double limit((systemRadius > 0.0) ? (systemRadius * MaxSystemRadiusFactor) : MaxDistanceNoRadius);
        return (Distance(p, Vec()) <= limit);
    }

    // the emergency warp's destination (dir:  a random unit vector)
    inline Vec EmergencyWarpPoint(const Vec& from, const Vec& dir)
    {
        Vec p;
        p.x = from.x + dir.x * EmergencyWarpDistance;
        p.y = from.y + dir.y * EmergencyWarpDistance;
        p.z = from.z + dir.z * EmergencyWarpDistance;
        return p;
    }

    // what the ship does when its pilot is gone
    enum class Exit : uint8_t {
        FinishWarp  = 1,    // it is warping (or aligning for a warp):  it lands first, then decides again
        Warp        = 2,    // emergency warp
        Stay        = 3     // scrambled:  it can't warp, and it isn't tried again
    };
    inline Exit OnLogoff(bool inWarp, bool scrambled)
    {
        if (inWarp)
            return Exit::FinishWarp;
        return (scrambled ? Exit::Stay : Exit::Warp);
    }

    /* when the ship leaves space (FILETIME units), counted from when it came to rest:  landed from its emergency warp, or
     *  found that it couldn't warp.  pvp:  the 15 minutes (the caller also keeps it while the pilot's timer is live) */
    inline int64_t LeaveSpaceAt(int64_t restedAt, bool pvp, int64_t pvpTime, int64_t time)
    {
        return restedAt + (pvp ? pvpTime : time);
    }

    struct LoginPlan {
        Vec start;          // where the ship comes into space
        Vec target;         // where it warps to
        bool warp = false;  // false:  it is already there
    };
    /* logoff:  where the pilot logged off (a Usable() point:  the caller falls back before this).
     *  stillInSpace:  the ship hasn't left space yet and sits at shipNow.  dir:  a random unit vector.
     *  minWarp:  the shortest warp the server takes (m)
     */
    inline LoginPlan PlanLogin(const Vec& logoff, bool stillInSpace, const Vec& shipNow, const Vec& dir, double minWarp)
    {
        LoginPlan plan;
        plan.target = logoff;
        if (stillInSpace) {
            plan.start = shipNow;
            plan.warp = (Distance(shipNow, logoff) >= minWarp);
            return plan;
        }
        plan.start = EmergencyWarpPoint(logoff, dir);
        plan.warp = true;
        return plan;
    }

}

#endif  // EVE_LOGOFF_H
