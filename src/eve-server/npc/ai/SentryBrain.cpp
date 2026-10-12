 /**
  * @name SentryBrain.cpp
  *   sentry gun policy (PLAN.md 4.10, 4.11, 6.6).  see SentryBrain.h
  */

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "npc/NPC.h"
#include "npc/Sentry.h"
#include "npc/ai/LawRules.h"
#include "npc/ai/NpcProfile.h"
#include "npc/ai/NpcRng.h"
#include "npc/ai/SentryBrain.h"
#include "ship/modules/TurretFormulas.h"
#include "system/BubbleManager.h"
#include "system/CrimeWatch.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"

SentryBrain* SentryBrain::Create(Sentry* pSentry, const NpcProfile& profile)
{
    if (pSentry->IsLawSentry())
        return new LawSentryBrain();
    return new PirateSentryBrain();
}

/* ------------------------------------------------------------------------------------------------------------------
 *  LawSentryBrain:  gate and station guns, Crimewatch 1
 * ------------------------------------------------------------------------------------------------------------------ */
uint32 LawSentryBrain::OwnerOf(SystemEntity* pTarget, bool& pod)
{
    pod = false;
    if (pTarget == nullptr)
        return 0;
    if (pTarget->IsShipSE()) {
        if (pTarget->HasPilot()) {
            pod = pTarget->GetPilot()->InPod();
            return pTarget->GetPilot()->GetCharacterID();
        }
        // a ship left in space (ejected, or its pilot logged off) is still its owner's
        pod = (pTarget->GetSelf()->groupID() == EVEDB::invGroups::Capsule);
        return pTarget->GetSelf()->ownerID();
    }
    // drones and fighters
    if (pTarget->IsDroneSE())
        return pTarget->GetSelf()->ownerID();
    return 0;
}

double LawSentryBrain::AcquireRange(SentryContext& ctx)
{
    return sConfig.crime.SentryRange;
}

bool LawSentryBrain::MayShoot(SentryContext& ctx, SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or pTarget->IsDead() or (pTarget->DestinyMgr() == nullptr) or (pTarget->SysBubble() == nullptr))
        return false;
    LawRules::SentryCandidate c = LawRules::SentryCandidate();
        c.hidden = (pTarget->DestinyMgr()->IsCloaked() or pTarget->DestinyMgr()->IsWarping());
        c.inRange = (ctx.sentry->GetPosition().distance(pTarget->GetPosition()) <= AcquireRange(ctx));
    if (c.hidden or !c.inRange)
        return false;

    // an npc made a criminal (GM testing).  never law enforcement
    if (pTarget->IsNPCSE()) {
        if (pTarget->GetNPCSE()->IsLawEnforcer())
            return false;
        c.criminal = sCrimeWatch.IsCriminal(pTarget->GetID());
        return LawRules::SentryMayShoot(c);
    }

    uint32 charID(OwnerOf(pTarget, c.pod));
    if (!IsCharacterID(charID) or c.pod)
        return false;
    if (pTarget->HasPilot() and pTarget->GetPilot()->IsInvul())
        return false;
    c.criminal = sCrimeWatch.IsCriminal(charID);
    if (!c.criminal and (sConfig.crime.SentryAggression > 0))
        c.objectAggression = sCrimeWatch.HasSentryAggression(charID, ctx.sentry->GetProtectedID());
    if (!c.criminal and !c.objectAggression and sConfig.crime.NpcFactionAggression)
        c.factionAggression = sCrimeWatch.HasFactionAggression(charID, ctx.sentry->GetWarFactionID());
    return LawRules::SentryMayShoot(c);
}

SystemEntity* LawSentryBrain::PickRandom(SentryContext& ctx)
{
    SystemBubble* pBubble(ctx.sentry->SysBubble());
    if (pBubble == nullptr)
        return nullptr;
    m_candidates.clear();
    pBubble->ForEachDynamic([&](SystemEntity* cur) {
        if ((cur->IsShipSE() or cur->IsDroneSE() or cur->IsNPCSE()) and MayShoot(ctx, cur))
            m_candidates.push_back(cur);
    });
    if (m_candidates.empty())
        return nullptr;
    return m_candidates[LawRules::PickIndex(m_candidates.size(), ctx.rng.Uniform())];
}

SystemEntity* LawSentryBrain::Acquire(SentryContext& ctx)
{
    return PickRandom(ctx);
}

SystemEntity* LawSentryBrain::OnAggressed(SentryContext& ctx, SystemEntity* pBy)
{
    // no lock-back of innocents (bug B9).  whoever really attacked us has aggression with what we guard by now (CrimeWatch)
    return (MayShoot(ctx, pBy) ? pBy : nullptr);
}

SystemEntity* LawSentryBrain::RepickPrimary(SentryContext& ctx, SystemEntity* pLost)
{
    // another of our locks, at random
    m_candidates.clear();
    ctx.sentry->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        if ((cur != pLost) and MayShoot(ctx, cur))
            m_candidates.push_back(cur);
    });
    if (m_candidates.empty())
        return nullptr;
    return m_candidates[LawRules::PickIndex(m_candidates.size(), ctx.rng.Uniform())];
}

SystemEntity* LawSentryBrain::Retarget(SentryContext& ctx, SystemEntity* pPrimary)
{
    // a random eligible target, the current one included
    return PickRandom(ctx);
}

int64 LawSentryBrain::RetargetMs() const
{
    return (int64)sConfig.crime.SentryRetarget * 1000;
}

void LawSentryBrain::OnPicked(SentryContext& ctx, SystemEntity* pTarget)
{
    // being picked renews the pilot's aggression with what we guard
    if (sConfig.crime.SentryAggression == 0)
        return;
    bool pod(false);
    uint32 charID(OwnerOf(pTarget, pod));
    if (IsCharacterID(charID))
        sCrimeWatch.SentryAggression(charID, ctx.sentry->GetProtectedID());
}

/* ------------------------------------------------------------------------------------------------------------------
 *  PirateSentryBrain:  pirate and deadspace sentries
 * ------------------------------------------------------------------------------------------------------------------ */
bool PirateSentryBrain::PodRuleAllows(SentryContext& ctx, SystemEntity* pTarget)
{
    bool pod(pTarget->HasPilot() ? pTarget->GetPilot()->InPod()
                                 : (pTarget->IsShipSE() and (pTarget->GetSelf()->groupID() == EVEDB::invGroups::Capsule)));
    if (!pod)
        return true;
    if (!sConfig.npc.TargetPod)
        return false;
    return (ctx.sentry->SystemMgr()->GetSystemSecurityRating() <= sConfig.npc.TargetPodSec);
}

double PirateSentryBrain::AcquireRange(SentryContext& ctx)
{
    // static guns with long range:  their whole bubble, or entityAttackRange if that's shorter (PLAN.md 5.4)
    double range(ctx.profile.attackRange);
    if ((range <= 0.0) or (range > BUBBLE_RADIUS_METERS))
        range = BUBBLE_RADIUS_METERS;
    return range;
}

bool PirateSentryBrain::MayShoot(SentryContext& ctx, SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or pTarget->IsDead() or (pTarget->DestinyMgr() == nullptr))
        return false;
    // player ships and drones (npcs are friends)
    if (!pTarget->IsShipSE() and !pTarget->IsDroneSE())
        return false;
    if ((pTarget->SysBubble() == nullptr) or (pTarget->SysBubble() != ctx.sentry->SysBubble()))
        return false;
    if (pTarget->DestinyMgr()->IsCloaked() or pTarget->DestinyMgr()->IsWarping())
        return false;
    if (pTarget->HasPilot() and pTarget->GetPilot()->IsInvul())
        return false;
    if (!PodRuleAllows(ctx, pTarget))
        return false;
    return (ctx.sentry->GetPosition().distance(pTarget->GetPosition()) <= AcquireRange(ctx));
}

SystemEntity* PirateSentryBrain::Acquire(SentryContext& ctx)
{
    // the nearest player ship
    SystemBubble* pBubble(ctx.sentry->SysBubble());
    if (pBubble == nullptr)
        return nullptr;
    SystemEntity* pBest(nullptr);
    double nearest(0.0);
    pBubble->ForEachPlayer([&](Client* cur) {
        SystemEntity* pShip(cur->GetShipSE());
        if ((pShip == nullptr) or !MayShoot(ctx, pShip))
            return;
        double distance(ctx.sentry->GetPosition().distance(pShip->GetPosition()));
        if ((pBest == nullptr) or (distance < nearest)) {
            pBest = pShip;
            nearest = distance;
        }
    });
    return pBest;
}

SystemEntity* PirateSentryBrain::OnAggressed(SentryContext& ctx, SystemEntity* pBy)
{
    // whoever it was:  a drone gets shot itself (plan decision 7)
    return (MayShoot(ctx, pBy) ? pBy : nullptr);
}

SystemEntity* PirateSentryBrain::RepickPrimary(SentryContext& ctx, SystemEntity* pLost)
{
    // the nearest of what we have locked (or are locking)
    SystemEntity* pBest(nullptr);
    double nearest(0.0);
    ctx.sentry->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        if ((cur == pLost) or !MayShoot(ctx, cur))
            return;
        double distance(ctx.sentry->GetPosition().distance(cur->GetPosition()));
        if ((pBest == nullptr) or (distance < nearest)) {
            pBest = cur;
            nearest = distance;
        }
    });
    return pBest;
}

float PirateSentryBrain::TurretHit(SentryContext& ctx, SystemEntity* pTarget)
{
    // the shared turret formula with maxRange as optimal (the draw comes from the sentry's own rng)
    TurretFormulas formula;
    return formula.GetSentryToHit(ctx.sentry, pTarget);
}
