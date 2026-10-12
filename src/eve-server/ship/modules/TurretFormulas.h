
 /**
  * @name TurretFormulas.h
  *   forumlas for turret tracking, to hit, and other specific things
  * @Author:         Allan
  * @date:   10 June 2015
  *
  *   every shooter uses the one Crucible turret formula in npc/ai/NpcMath (owner decision, npc ai plan 5.1):  surface
  *   distance for range, transversal velocity only, signature resolution inside the roll, and a wrecking shot
  *   (rates.WreckingChance) only inside a hit.  these are the adapters that gather each shooter's inputs.
  */

#ifndef _EVE_SHIP_MOD_FORMULAS_H_
#define _EVE_SHIP_MOD_FORMULAS_H_

#include "ship/Ship.h"

class NPC;
class DroneSE;
class TurretModule;
class TurretFormulas {
public:
    //  returns damage modifier from hit, based on calculations made about source, item, and target.
    //    return 0 is missed, 3 a wrecking shot, else the hit quality 0.49 .. 1.49
    float GetToHit(ShipItemRef shipRef, TurretModule* pMod, SystemEntity* pTarget);
    float GetNPCToHit(NPC* pNPC, SystemEntity* pTarget);
    float GetDroneToHit(DroneSE* pDrone, SystemEntity* pTarget);
    // pirate and deadspace sentries:  maxRange is their optimal (gate and station guns always hit, SentryAI)
    float GetSentryToHit(Sentry* pSentry, SystemEntity* pTarget);
    // starbase turrets:  the standard formula with the battery's (tower-bonused) optimal and falloff, tracking and signature
    //  resolution against the target's transversal and signature radius
    float GetStructureToHit(SystemEntity* pSource, float optimal, float falloff, float tracking, float sigRes, SystemEntity* pTarget);

    /* the shared roll:  geometry between shooter and target, chance to hit, shot.  optimal and tracking already include
     *  tracking disruptors on the shooter.  logType gets one line with every term (DAMAGE__TRACE, DAMAGE__TRACE_NPC) */
    static float Roll(SystemEntity* pShooter, SystemEntity* pTarget, double optimal, double falloff, double tracking,
                      double sigRes, double u, LogType logType, const char* who);
};


#endif  //_EVE_SHIP_MOD_FORMULAS_H_
