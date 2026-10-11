 /**
  * @name WarpInPoint.h
  *   where a warp to an object ends
  *
  *   CCP's warp-in rules ("Useful formulae", developers.eveonline.com), which match the Crucible client's own planet code:
  *     planets:        util.GetPlanetWarpInPoint (seeded with python's random.Random(planetID))
  *     large objects:  celestials (moons included) of 90km radius or more, except suns and wrecks
  *                     (x + (r + 5000000) * cos(r), y + 1.3r - 7500, z - (r + 5000000) * sin(r))
  *     everything else: the object's edge on the line from the warping ship
  */

#ifndef _EVE_SYSTEM_WARPINPOINT_H_
#define _EVE_SYSTEM_WARPINPOINT_H_

#include "math/gpoint.h"

class SystemEntity;

namespace WarpIn {
    // python 2.7 random.Random(seed).random()  (MT19937, init_by_array([seed]), 53-bit result)
    double PyRandom(uint32 seed);
    // the client's util.GetPlanetWarpInPoint, as an absolute position
    GPoint Planet(uint32 planetID, const GPoint& planetPos, double radius);
    // CCP's large-object formula
    GPoint Large(const GPoint& pos, double radius);
    // ordinary object: its edge on the line from 'from'
    GPoint Ordinary(const GPoint& from, const GPoint& pos, double radius);
    // picks one of the above for pSE, for a ship at 'from'
    GPoint ForEntity(SystemEntity* pSE, const GPoint& from);
    // the client's eveCfg.IsNewbieSystem (default list), where the tutorials' warp-to works
    bool IsNewbieSystem(uint32 systemID);
}

#endif  // _EVE_SYSTEM_WARPINPOINT_H_
