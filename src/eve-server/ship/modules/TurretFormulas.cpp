
 /**
  * @name TurretFormulas.cpp
  *   formulas for turret tracking, to hit, and other specific things
  * @Author:         Allan
  * @date:   10 June 2015
  *
  *   every shooter uses the shared Crucible formula (npc/ai/NpcMath, npc ai plan 5.1):
  *     chance = 0.5 ^ ( ((angular / tracking) * (sigRes / targetSig))^2 + (max(0, surface - optimal) / falloff)^2 )
  *   angular velocity from the transversal part of the relative velocity only, range from surface to surface, and the
  *   signature resolution inside the roll (no separate 'big gun vs small ship' damage factor).  a draw u below the chance
  *   hits with quality u + 0.49;  below rates.WreckingChance (0.01) it's a wrecking 3x shot.  the per-shooter crit chances
  *   (rolled before the hit, so they landed at any range) are gone (bug B2)
  */

#include "character/Character.h" // this sets compat includes for this file
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/Drone.h"
#include "npc/Sentry.h"
#include "npc/SentryAI.h"
#include "npc/ai/NpcMath.h"
#include "npc/ai/NpcProfile.h"
#include "npc/ai/NpcTelemetry.h"
#include "ship/modules/TurretFormulas.h"
#include "ship/modules/TurretModule.h"
#include "system/EwarState.h"

namespace {
    // something to roll against:  in space with a destiny manager, or an outpost (stations have none, and stand still)
    bool Rollable(SystemEntity* pTarget)
    {
        return ((pTarget != nullptr) and ((pTarget->DestinyMgr() != nullptr) or pTarget->IsOutpostSE()));
    }

    NpcMath::Vec3 PositionOf(SystemEntity* pSE)
    {
        const GPoint& p(pSE->DestinyMgr() != nullptr ? pSE->DestinyMgr()->GetPosition() : pSE->GetPosition());
        NpcMath::Vec3 v = { p.x, p.y, p.z };
        return v;
    }

    NpcMath::Vec3 VelocityOf(SystemEntity* pSE)
    {
        const GVector& vel(pSE->GetVelocity());
        NpcMath::Vec3 v = { vel.x, vel.y, vel.z };
        return v;
    }
}

float TurretFormulas::Roll(SystemEntity* pShooter, SystemEntity* pTarget, double optimal, double falloff, double tracking,
                           double sigRes, double u, LogType logType, const char* who, NpcShotRecord* record/*nullptr*/)
{
    if ((pShooter == nullptr) or (pTarget == nullptr))
        return 0.0f;
    NpcMath::TurretGeometry g(NpcMath::Geometry(PositionOf(pShooter), VelocityOf(pShooter), pShooter->GetRadius(),
                                                PositionOf(pTarget), VelocityOf(pTarget), pTarget->GetRadius()));
    NpcMath::TurretInputs in = NpcMath::TurretInputs();
        in.angular = g.angular;
        in.surface = g.surface;
        in.optimal = optimal;
        in.falloff = falloff;
        in.tracking = tracking;
        in.sigRes = sigRes;
        in.targetSig = pTarget->GetSignatureRadius();   // includes target painters
        in.targetRadius = pTarget->GetRadius();
    NpcMath::TurretTerms terms = NpcMath::TurretTerms();
    double chance(NpcMath::TurretChanceToHit(in, &terms));
    double shot(NpcMath::ResolveTurretShot(chance, u, sConfig.rates.WreckingChance));
    if (record != nullptr) {
        record->atMs = GetSteadyTime();
        record->targetID = pTarget->GetID();
        record->surface = g.surface;
        record->optimal = optimal;
        record->falloff = falloff;
        record->transversal = g.transversal;
        record->angular = g.angular;
        record->tracking = tracking;
        record->sigRes = sigRes;
        record->sig = in.targetSig;
        record->track = terms.track;
        record->range = terms.range;
        record->chance = chance;
        record->u = u;
        record->quality = shot;
    }
    // (angular and tracking with 7 decimals:  utils/npcai/check_shots.py recomputes the chance from this line)
    if (is_log_enabled(logType))
        _log(logType, "%s::ToHit %s(%u) -> %s(%u):  surface %.0fm (optimal %.0f, falloff %.0f), transversal %.1fm/s, angular %.7f (tracking %.7f),"
                " sigRes %.0f vs sig %.1f -> track %.4f, range %.4f, chance %.5f, u %.5f -> %.3f (%s)",
                who, pShooter->GetName(), pShooter->GetID(), pTarget->GetName(), pTarget->GetID(), g.surface, optimal, falloff,
                g.transversal, g.angular, tracking, sigRes, in.targetSig, terms.track, terms.range, chance, u, shot,
                (shot <= 0.0 ? "miss" : (shot >= 3.0 ? "wrecking" : "hit")));
    return static_cast<float>(shot);
}

float TurretFormulas::GetToHit(ShipItemRef shipRef, TurretModule* pMod, SystemEntity* pTarget)
{
    if (!Rollable(pTarget) or (shipRef->GetPilot() == nullptr))
        return 0;
    SystemEntity* pShooter(shipRef->GetPilot()->GetShipSE());
    if (pShooter == nullptr)
        return 0;
    double optimal(pMod->GetAttribute(AttrMaxRange).get_double());
    double falloff(pMod->GetAttribute(AttrFalloff).get_double());
    double tracking(pMod->GetAttribute(AttrTrackingSpeed).get_double());
    // tracking disruptors on the shooter
    EwarState* pEwar(pShooter->GetEwar());
    if (pEwar != nullptr) {
        optimal *= pEwar->GetOptimalMult();
        falloff *= pEwar->GetFalloffMult();
        tracking *= pEwar->GetTrackingMult();
    }
    return Roll(pShooter, pTarget, optimal, falloff, tracking,
                pMod->GetAttribute(AttrOptimalSigRadius).get_double(), MakeRandomFloat(), DAMAGE__TRACE, "Turret");
}

float TurretFormulas::GetNPCToHit(NPC* pNPC, SystemEntity* pTarget, NpcShotRecord* record/*nullptr*/)
{
    if (!Rollable(pTarget))
        return 0;
    const NpcProfile& p(pNPC->GetAIMgr()->GetProfile());
    double optimal(p.optimal), tracking(p.tracking);
    // tracking disruptors on the npc (in Crucible they don't touch npc falloff)
    if (pNPC->GetEwar() != nullptr) {
        optimal *= pNPC->GetEwar()->GetOptimalMult();
        tracking *= pNPC->GetEwar()->GetTrackingMult();
    }
    return Roll(pNPC, pTarget, optimal, p.falloff, tracking, p.sigResolution, pNPC->GetAIMgr()->GetRng().Uniform(),
                DAMAGE__TRACE_NPC, "NPC", record);
}

/* the drone's skill-adjusted stats */
float TurretFormulas::GetDroneToHit(DroneSE* pDrone, SystemEntity* pTarget, float* quality/*nullptr*/)
{
    if (quality != nullptr)
        *quality = 0.0f;
    if (!Rollable(pTarget))
        return 0;
    const DroneStats& stats(pDrone->Stats());
    /* fighter bombers (after AzediZ, Incursion rules):  their "fake missile" always hits a target within entityAttackRange
     *  (surface to surface), never one outside it.  the damage is a missile's explosion factor (NpcMath::MissileDamageFactor,
     *  min(1, S/E, ((S/E) * (Ve/Vt)) ^ (ln drf / ln drs)), S = target signature with painters, E = explosion radius,
     *  Ve = explosion velocity, Vt = target speed) times a turret-like quality roll, so they "can wreck or lightly hit"
     *  (NpcMath::ResolveBomberShot).  their optimalSigRadius (2500) is not used:  nothing says what it did after Incursion,
     *  and the explosion radius already gives the subcapital penalty CCP described
     */
    if (stats.bomber) {
        NpcMath::TurretGeometry g(NpcMath::Geometry(PositionOf(pDrone), VelocityOf(pDrone), pDrone->GetRadius(),
                                                    PositionOf(pTarget), VelocityOf(pTarget), pTarget->GetRadius()));
        double sig(pTarget->GetSignatureRadius()), speed(pTarget->GetVelocity().length()), u(MakeRandomFloat());
        NpcMath::BomberShot shot(NpcMath::ResolveBomberShot(g.surface, stats.optimalRange, sig, stats.aoeCloudSize, stats.aoeVelocity,
                                                            speed, stats.aoeDrf, stats.aoeDrs, u, sConfig.rates.WreckingChance));
        if (is_log_enabled(DAMAGE__TRACE))
            _log(DAMAGE__TRACE, "Drone::ToHit bomber %s(%u) -> %s(%u):  surface %.0fm (range %.0f), sig %.1f, explosion %.0f, speed %.1f"
                 " -> factor %.4f, u %.5f -> quality %.3f, damage x%.4f (%s)", pDrone->GetName(), pDrone->GetID(), pTarget->GetName(),
                 pTarget->GetID(), g.surface, stats.optimalRange, sig, stats.aoeCloudSize, speed, shot.factor, u, shot.quality, shot.damage,
                 (shot.quality <= 0.0 ? "out of range" : (shot.quality >= 3.0 ? "wrecking" : "hit")));
        if (quality != nullptr)
            *quality = static_cast<float>(shot.quality);
        return static_cast<float>(shot.damage);
    }
    float shot(Roll(pDrone, pTarget, stats.optimalRange, stats.falloff, stats.tracking, stats.sigResolution, MakeRandomFloat(),
                    DAMAGE__TRACE, "Drone"));
    if (quality != nullptr)
        *quality = shot;
    return shot;
}

float TurretFormulas::GetStructureToHit(SystemEntity* pSource, float optimal, float falloff, float tracking, float sigRes, SystemEntity* pTarget)
{
    if (!Rollable(pTarget))
        return 0;
    // tracking disruptors on the battery
    EwarState* pEwar(pSource->GetEwar());
    if (pEwar != nullptr) {
        tracking *= pEwar->GetTrackingMult();
        optimal *= pEwar->GetOptimalMult();
        falloff *= pEwar->GetFalloffMult();
    }
    return Roll(pSource, pTarget, optimal, falloff, tracking, sigRes, MakeRandomFloat(), DAMAGE__TRACE, "Structure");
}

float TurretFormulas::GetSentryToHit(Sentry* pSentry, SystemEntity* pTarget, NpcShotRecord* record/*nullptr*/)
{
    if (!Rollable(pTarget))
        return 0;
    // maxRange is the optimal (it used to be entityAttackRange, with the target's whole speed as transversal)
    const NpcProfile& p(pSentry->GetAIMgr()->GetProfile());
    double optimal(p.optimal), tracking(p.tracking);
    if (pSentry->GetEwar() != nullptr) {
        optimal *= pSentry->GetEwar()->GetOptimalMult();
        tracking *= pSentry->GetEwar()->GetTrackingMult();
    }
    return Roll(pSentry, pTarget, optimal, p.falloff, tracking, p.sigResolution, pSentry->GetAIMgr()->GetRng().Uniform(),
                DAMAGE__TRACE_NPC, "Sentry", record);
}
