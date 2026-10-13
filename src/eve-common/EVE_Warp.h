 /**
  * @name EVE_Warp.h
  *     capacitor cost of warping
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  */


#ifndef EVE_WARP_H
#define EVE_WARP_H

#include <algorithm>
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

#endif  // EVE_WARP_H
