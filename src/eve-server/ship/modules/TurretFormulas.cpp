
 /**
  * @name TurretFormulas.cpp
  *   formulas for turret tracking, to hit, and other specific things
  * @Author:         Allan
  * @date:   10 June 2015
  */

 /* default crit chances - can change in server config
  *      NPC  - 1.5%
  *   Player  - 2%
  *   Sentry  - 2%
  *    Drone  - 3%
  *  Concord  - 5%
  */

#include "character/Character.h" // this sets compat includes for this file
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/Drone.h"
#include "npc/Sentry.h"
#include "ship/modules/TurretFormulas.h"
#include "ship/modules/TurretModule.h"


namespace {
    /* turret hit chance as CCP's destiny works it out:  Ballpark::GetAccuracy() in the open-sourced Carbon destiny
     *  (github.com/carbonengine/destiny, src/Ballpark.cpp, the same in its first 2014 snapshot as today)
     *
     *   chance = 0.5 ^ ( ((angular / tracking) * (sigResolution / targetSig))^2 + (max(0, range - optimal) / falloff)^2 )
     *
     *  angular velocity uses only the part of the relative velocity across the line between the two centres, divided by
     *  the centre-to-centre distance; moving straight towards or away from the target doesn't hurt tracking.
     *  range is surface to surface (centre distance less both radii).  the signature ratio only scales the tracking
     *  term; there is no damage cut for a big gun on a small target.
     *  destiny gives no chance at all for a weapon with no tracking.  for a weapon with no falloff in its data this
     *  hits normally inside optimal and misses outside it, like the drone formula below.
     */
    double TurretHitChance(SystemEntity* pShooter, SystemEntity* pTarget, double optimal, double falloff,
                           double tracking, double sigResolution, double& range, double& angular, double& targetSig)
    {
        range = 0.0;
        angular = 0.0;
        targetSig = 0.0;
        if (pTarget->GetSelf()->HasAttribute(AttrSignatureRadius))
            targetSig = pTarget->GetSelf()->GetAttribute(AttrSignatureRadius).get_double();
        if (targetSig < 0.01)
            targetSig = pTarget->GetRadius() / 10.0;
        if (targetSig < 0.01)
            targetSig = 25.0;

        GVector los(pShooter->GetPosition(), pTarget->GetPosition());
        double centres(los.length());
        range = centres - pShooter->GetRadius() - pTarget->GetRadius();
        if (range < 0.0)
            range = 0.0;
        if ((centres <= 0.0) or (tracking <= 0.0))
            return 0.0;

        los = GVector(los.x / centres, los.y / centres, los.z / centres);
        GVector rel(pTarget->GetVelocity() - pShooter->GetVelocity());
        double radial(rel.x * los.x + rel.y * los.y + rel.z * los.z);
        GVector transversal(rel.x - los.x * radial, rel.y - los.y * radial, rel.z - los.z * radial);
        angular = transversal.length() / centres;
        double trackingTerm((angular / tracking) * (sigResolution / targetSig));

        double rangeTerm(0.0);
        double outside(range - optimal);
        if (outside > 0.0) {
            if (falloff <= 0.0)
                return 0.0;
            rangeTerm = outside / falloff;
        }

        return std::pow(0.5, trackingTerm * trackingTerm + rangeTerm * rangeTerm);
    }

    // the roll: a miss returns 0, a wrecking shot 3, any other hit (roll + 0.49).  a wrecking shot has to hit first.
    float TurretHitRoll(double chance, float critChance, float& rNum)
    {
        rNum = MakeRandomFloat(0.0, 1.0);
        if (rNum >= chance)
            return 0;
        if (rNum <= critChance)
            return 3.0f;
        return (rNum + 0.49);
    }
}

float TurretFormulas::GetToHit(ShipItemRef shipRef, TurretModule* pMod, SystemEntity* pTarget)
{
    if (pTarget == nullptr)
        return 0;
    double optimal(pMod->GetAttribute(AttrMaxRange).get_double());
    double falloff(pMod->GetAttribute(AttrFalloff).get_double());
    double tracking(pMod->GetAttribute(AttrTrackingSpeed).get_double());
    double sigRes(pMod->GetAttribute(AttrOptimalSigRadius).get_double());
    double range(0.0), angular(0.0), targSig(0.0);
    double chance(TurretHitChance(shipRef->GetPilot()->GetShipSE(), pTarget, optimal, falloff, tracking, sigRes, range, angular, targSig));
    float rNum(0.0f);
    float result(TurretHitRoll(chance, sConfig.rates.PlayerCritChance, rNum));
    _log(DAMAGE__TRACE, "Turret::GetToHit - range:%.0f (opt %.0f, falloff %.0f) angular:%.5f tracking:%.5f sigRes:%.0f targetSig:%.1f chance:%.4f rand:%.4f - %s",
         range, optimal, falloff, angular, tracking, sigRes, targSig, chance, rNum, (result == 0 ? "Miss" : (result == 3.0f ? "Crit" : "Hit")));
    return result;
}

float TurretFormulas::GetNPCToHit(NPC* pNPC, SystemEntity* pTarget)
{
    if (pTarget == nullptr)
        return 0;
    double optimal(pNPC->GetAIMgr()->GetOptimalRange());
    double falloff(pNPC->GetAIMgr()->GetFalloff());
    double tracking(pNPC->GetAIMgr()->GetTrackingSpeed());
    double sigRes(pNPC->GetAIMgr()->GetSigRes());
    double range(0.0), angular(0.0), targSig(0.0);
    double chance(TurretHitChance(pNPC, pTarget, optimal, falloff, tracking, sigRes, range, angular, targSig));
    float rNum(0.0f);
    float result(TurretHitRoll(chance, sConfig.rates.NpcCritChance, rNum));
    _log(DAMAGE__TRACE_NPC, "NPC::GetToHit - range:%.0f (opt %.0f, falloff %.0f) angular:%.5f tracking:%.5f sigRes:%.0f targetSig:%.1f chance:%.4f rand:%.4f - %s",
         range, optimal, falloff, angular, tracking, sigRes, targSig, chance, rNum, (result == 0 ? "Miss" : (result == 3.0f ? "Crit" : "Hit")));
    return result;
}

/* standard turret hit chance, with the drone's skill-adjusted stats:
 *   chance = 0.5 ^ ( ((angular / tracking) * (sigResolution / targetSig))^2 + (max(0, range - optimal) / falloff)^2 )
 *  angular velocity uses only the part of the relative velocity across the line of sight.  ranges are surface to surface,
 *  as for the drone's other range checks.  returns the damage modifier: 3 for a wrecking shot, 0 for a miss
 */
float TurretFormulas::GetDroneToHit(DroneSE* pDrone, SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or (pTarget->DestinyMgr() == nullptr))
        return 0;
    const DroneStats& stats(pDrone->Stats());

    GVector los(pDrone->GetPosition(), pTarget->GetPosition());
    double centres(los.length());
    double range(centres - pDrone->GetRadius() - pTarget->GetRadius());
    if (range < 0.0)
        range = 0.0;

    double trackingTerm(0.0);
    if ((centres > 0.0) and (stats.tracking > 0.0)) {
        los = GVector(los.x / centres, los.y / centres, los.z / centres);
        GVector rel(pTarget->GetVelocity() - pDrone->GetVelocity());
        double radial(rel.x * los.x + rel.y * los.y + rel.z * los.z);
        GVector transversal(rel.x - los.x * radial, rel.y - los.y * radial, rel.z - los.z * radial);
        double angular(transversal.length() / centres);
        double targetSig(25.0);
        if (pTarget->GetSelf()->HasAttribute(AttrSignatureRadius))
            targetSig = pTarget->GetSelf()->GetAttribute(AttrSignatureRadius).get_double();
        if (targetSig <= 0.0)
            targetSig = 25.0;
        trackingTerm = (angular / stats.tracking) * (stats.sigResolution / targetSig);
    }

    /* fighter bombers: their missile always hits a target within range, never one outside it.  the damage is scaled
     *  like a missile's:  min(1, S/E, ((S/E) * (Ve/Vt)) ^ (ln(drf) / ln(drs)))
     *  S = target signature, E = explosion radius, Ve = explosion velocity, Vt = target speed
     */
    if (stats.bomber) {
        if (range > stats.optimalRange) {
            _log(DAMAGE__TRACE, "Drone::GetToHit - bomber out of range: %.0f > %.0f", range, stats.optimalRange);
            return 0;
        }
        double sig(25.0);
        if (pTarget->GetSelf()->HasAttribute(AttrSignatureRadius))
            sig = pTarget->GetSelf()->GetAttribute(AttrSignatureRadius).get_double();
        if (sig <= 0.0)
            sig = 25.0;
        double factor(1.0);
        if (stats.aoeCloudSize > 0.0) {
            double sigTerm(sig / stats.aoeCloudSize);
            factor = std::min(factor, sigTerm);
            double speed(pTarget->GetVelocity().length());
            if ((speed > 0.0) and (stats.aoeVelocity > 0.0))
                factor = std::min(factor, std::pow(sigTerm * stats.aoeVelocity / speed, stats.aoeExponent));
        }
        if (factor < 0.01)
            factor = 0.01;      // a hit always does something
        _log(DAMAGE__TRACE, "Drone::GetToHit - bomber hit at %.0f m: sig %.0f, explosion %.0f, speed %.0f, damage x%.3f",
             range, sig, stats.aoeCloudSize, pTarget->GetVelocity().length(), factor);
        return static_cast<float>(factor);
    }

    double rangeTerm(0.0);
    double outside(range - stats.optimalRange);
    if (outside > 0.0) {
        if (stats.falloff <= 0.0)
            return 0;
        rangeTerm = outside / stats.falloff;
    }

    double chance(std::pow(0.5, trackingTerm * trackingTerm + rangeTerm * rangeTerm));
    float rNum(MakeRandomFloat(0.0, 1.0));
    _log(DAMAGE__TRACE, "Drone::GetToHit - range:%.0f (opt %.0f, falloff %.0f) tracking term %.3f, range term %.3f, chance %.4f, rand %.4f",
         range, stats.optimalRange, stats.falloff, trackingTerm, rangeTerm, chance, rNum);
    if (rNum >= chance)
        return 0;
    if (rNum <= sConfig.rates.DroneCritChance)
        return 3.0f;
    return (rNum + 0.49);
}

float TurretFormulas::GetSentryToHit(Sentry* pSentry, SystemEntity* pTarget)
{
    if (pTarget == nullptr)
        return 0;
    double optimal(pSentry->GetSelf()->GetAttribute(AttrEntityAttackRange).get_double());
    double falloff(pSentry->GetSelf()->GetAttribute(AttrFalloff).get_double());
    double tracking(pSentry->GetSelf()->GetAttribute(AttrTrackingSpeed).get_double());
    double sigRes(pSentry->GetSelf()->GetAttribute(AttrOptimalSigRadius).get_double());
    double range(0.0), angular(0.0), targSig(0.0);
    double chance(TurretHitChance(pSentry, pTarget, optimal, falloff, tracking, sigRes, range, angular, targSig));
    float rNum(0.0f);
    float result(TurretHitRoll(chance, sConfig.rates.SentryCritChance, rNum));
    _log(DAMAGE__TRACE_NPC, "Sentry::GetToHit - range:%.0f (opt %.0f, falloff %.0f) angular:%.5f tracking:%.5f sigRes:%.0f targetSig:%.1f chance:%.4f rand:%.4f - %s",
         range, optimal, falloff, angular, tracking, sigRes, targSig, chance, rNum, (result == 0 ? "Miss" : (result == 3.0f ? "Crit" : "Hit")));
    return result;
}
