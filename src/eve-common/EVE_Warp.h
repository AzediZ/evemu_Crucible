 /**
  * @name EVE_Warp.h
  *     warp speed and the capacitor cost of warping
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  */


#ifndef EVE_WARP_H
#define EVE_WARP_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>


namespace WarpCapacitor {

    static constexpr double AU = 149597870700.0;    // m

    /* the capacitor (GJ) one AU of warp costs:  ship mass (kg) x warpCapacitorNeed (153, GJ per kg per AU).  no other constant.
     *  "it's just how much cap per mass of the ship per AU" (CCP Hammer, oldforums threadID=332116, 2006), checked in game there
     *  as cap = mass x warpCapacitorNeed x AU.  Warp Drive Operation (-10%/level), the Eifyr and Co. 'Rogue' FY implants
     *  and the Warp Core Optimizer rigs lower warpCapacitorNeed through dogma, so pass the ship's modified value
     */
    inline double PerAU(double mass, double warpCapacitorNeed)
    {
        if (!(mass > 0.0) or !(warpCapacitorNeed > 0.0))
            return 0.0;
        return mass * warpCapacitorNeed;
    }

    // the capacitor a warp of distance (m) costs
    inline double Need(double mass, double warpCapacitorNeed, double distance)
    {
        return PerAU(mass, warpCapacitorNeed) * (std::max<double>(distance, 0.0) / AU);
    }

    // how far (m) the charge in the capacitor can warp the ship.  a ship with no warp need warps any distance
    inline double Reach(double charge, double mass, double warpCapacitorNeed)
    {
        double perAU(PerAU(mass, warpCapacitorNeed));
        if (!(perAU > 0.0))
            return std::numeric_limits<double>::infinity();
        return (std::max<double>(charge, 0.0) / perAU) * AU;
    }

}

namespace WarpSpeed {

    /* a ship's warp speed (AU/s):  max(1, baseWarpSpeed) x warpSpeedMultiplier x 3, as the Crucible client works it out for
     *  Show Info and the compare tool (infosvc.GetBaseWarpSpeed, compare.py) and as its warp effect assumes (Warp.py:
     *  3 x AU x warpSpeedMultiplier).  pass the ship's modified attributes:  warp speed rigs, implants and subsystems are
     *  PostPercent on warpSpeedMultiplier (600).  every ship has baseWarpSpeed (1281) 1;  its dgm default is 0, which the max()
     *  turns into 1.  warpSpeedMultiplier defaults to 1
     */
    static constexpr double BaseAUPerSecond = 3.0;

    inline double AUPerSecond(double warpSpeedMultiplier, double baseWarpSpeed = 1.0)
    {
        if (!std::isfinite(warpSpeedMultiplier) or !(warpSpeedMultiplier > 0.0))
            return 0.0;
        if (!std::isfinite(baseWarpSpeed))
            baseWarpSpeed = 1.0;
        return std::max<double>(1.0, baseWarpSpeed) * warpSpeedMultiplier * BaseAUPerSecond;
    }

    /* destiny's warpFactor:  the warp speed in tenths of an AU/s, a whole number (the WarpTo update and the ball's i64).
     *  destiny flies a warp at warpFactor x 0.1 AU/s.  rounded to the nearest tenth (1.25 x 3 = 3.75 AU/s goes as 38), never
     *  under 1 (destiny's own floor)
     */
    inline int32_t Factor(double auPerSecond)
    {
        if (!std::isfinite(auPerSecond) or !(auPerSecond > 0.0))
            return 1;
        double factor(std::floor(auPerSecond * 10.0 + 0.5));
        if (factor < 1.0)
            return 1;
        if (factor > static_cast<double>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        return static_cast<int32_t>(factor);
    }

    // the warpFactor of a ship with these (modified) attributes
    inline int32_t ShipFactor(double warpSpeedMultiplier, double baseWarpSpeed)
    {
        return Factor(AUPerSecond(warpSpeedMultiplier, baseWarpSpeed));
    }

}

#endif  // EVE_WARP_H
