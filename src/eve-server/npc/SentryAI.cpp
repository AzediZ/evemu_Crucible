/**
 * @name SentryAI.cpp
 *   Sentry AI class
 * @Author:     Allan
 * @date:       23 April 2017
 * @version     0.12
 *
 *   npc ai plan phase 4:  the brain (npc/ai/SentryBrain) decides whom, this does the locks, schedule and weapons.  see SentryAI.h
 */


#include "eve-server.h"

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "npc/Sentry.h"
#include "npc/SentryAI.h"
#include "system/DestinyManager.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/NPCEwar.h"
#include "npc/ai/NpcProfile.h"
#include "npc/ai/SentryBrain.h"
#include "EVEServerConfig.h"

SentryAI::SentryAI(Sentry* who)
: m_state(State::Idle),
m_primaryID(0),
m_pendingAggroID(0),
m_npc(who),
m_ewar(nullptr),
m_brain(nullptr)
{
    m_ewarChecked = false;
    m_profile = NpcProfile::Get(who->GetSelf()->type());

    // every roll this sentry makes.  a fixed npc.RngSeed makes them repeatable
    uint64_t seed(sConfig.npc.RngSeed);
    if (seed == 0)
        seed = static_cast<uint64_t>(GetSteadyTime()) ^ (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) << 16);
    m_rng.Seed(seed, who->GetSelf()->itemID());

    // one look around per second (bug B1:  this used to wait for the turret cycle, and never came for sentries without one)
    m_sched.Start(NpcAct::Think, Now(), 1000 + (who->GetID() % 4) * 250, 1000);
}

SentryAI::~SentryAI()
{
    SafeDelete(m_ewar);
    SafeDelete(m_brain);
}

int64 SentryAI::Now()
{
    return GetSteadyTime();
}

void SentryAI::CreateBrain()
{
    if (m_brain != nullptr)
        return;
    m_brain = SentryBrain::Create(m_npc, *m_profile);
    _log(NPC__AI_TRACE, "%s(%u): %s brain.", m_npc->GetName(), m_npc->GetID(), m_brain->GetName());
}

SentryContext SentryAI::MakeContext()
{
    if (m_brain == nullptr)
        CreateBrain();
    SentryContext ctx = { m_npc, this, *m_profile, m_rng, Now() };
    return ctx;
}

void SentryAI::OnKilled()
{
    if (m_ewar != nullptr)
        m_ewar->StopAll();
}

void SentryAI::Process() {
    if (m_brain == nullptr)
        CreateBrain();
    if (!m_ewarChecked) {
        // gate and station guns never use ewar (their brain says so).  LawEnforcement sets that role after we're made
        m_ewarChecked = true;
        if (sConfig.npc.UseEwar and m_brain->UsesEwar()) {
            m_ewar = new NPCEwar(m_npc);
            if (!m_ewar->HasAny())
                SafeDelete(m_ewar);
        }
    }

    SystemBubble* pBubble(m_npc->SysBubble());
    if (pBubble == nullptr)
        return;

    // ewar keeps its own timers, so it runs every tick while we're fighting
    if ((m_ewar != nullptr) and (m_state == State::Engaged))
        m_ewar->Process(m_npc->TargetMgr()->GetTarget(m_primaryID, true));

    if (!m_sched.Due(NpcAct::Think, Now()) or !pBubble->HasPlayers())
        return;

    SentryContext ctx(MakeContext());
    switch(m_state) {
        case State::Idle: {
            // someone locked, attacked or ewar'd us (or our room) since the last think
            if (m_pendingAggroID != 0) {
                SystemEntity* pBy(m_npc->SystemMgr()->GetSE(m_pendingAggroID));
                m_pendingAggroID = 0;
                SystemEntity* pTarget(pBy == nullptr ? nullptr : m_brain->OnAggressed(ctx, pBy));
                if (pTarget != nullptr) {
                    _log(NPC__AI_TRACE, "%s(%u): answering %s(%u).", m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
                    Target(pTarget);
                    break;
                }
            }
            // law guns:  a random eligible target.  pirate and deadspace sentries:  the nearest ship in range
            SystemEntity* pTarget(m_brain->Acquire(ctx));
            if (pTarget != nullptr)
                Target(pTarget);
        } break;
        case State::Engaged: {
            m_pendingAggroID = 0;
            // let go of locks on what we may no longer shoot (expired criminals and aggressors:  no innocent stays locked)
            PruneLocks(ctx);
            SystemEntity* pTarget(GetPrimary(ctx));
            if (pTarget == nullptr) {
                _log(NPC__AI_TRACE, "%s(%u): Stopped %s, no primary target.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state).c_str());
                SetIdle();
                return;
            }
            // law guns pick again (at random) every crime.SentryRetarget
            if (m_sched.Due(NpcAct::SwitchEval, ctx.now)) {
                SystemEntity* pPick(m_brain->Retarget(ctx, pTarget));
                if (pPick != nullptr) {
                    if (pPick != pTarget) {
                        _log(NPC__AI_TRACE, "%s(%u): retarget %s(%u) -> %s(%u).", m_npc->GetName(), m_npc->GetID(),
                                pTarget->GetName(), pTarget->GetID(), pPick->GetName(), pPick->GetID());
                        if (SetPrimary(ctx, pPick))
                            pTarget = pPick;
                    } else {
                        m_brain->OnPicked(ctx, pTarget);
                    }
                }
            }
            CheckTarget(ctx, pTarget);
        } break;
    }
}

SystemEntity* SentryAI::GetPrimary(SentryContext& ctx)
{
    SystemEntity* pLost(nullptr);
    if (m_primaryID != 0) {
        SystemEntity* pTarget(m_npc->TargetMgr()->GetTarget(m_primaryID, false));
        if (pTarget != nullptr) {
            if (m_brain->MayShoot(ctx, pTarget))
                return pTarget;
            // gate and station guns let go of anyone who's no longer a criminal or aggressor, cloaked, warping or out of range
            pLost = pTarget;
            m_primaryID = 0;
            ClearTarget(pTarget);
            if (m_state == State::Idle)
                return nullptr;
        }
        m_primaryID = 0;
    }
    // another of our locks, else someone new
    SystemEntity* pNext(m_brain->RepickPrimary(ctx, pLost));
    if (pNext == nullptr)
        pNext = m_brain->Acquire(ctx);
    if ((pNext != nullptr) and SetPrimary(ctx, pNext))
        return pNext;
    return nullptr;
}

bool SentryAI::SetPrimary(SentryContext& ctx, SystemEntity* pTarget)
{
    if (pTarget == nullptr)
        return false;
    if (m_npc->TargetMgr()->GetTarget(pTarget->GetID(), false) == nullptr) {
        // can we lock it at all (jammed, or damped below its range)?  nothing is given up for a lock that can't start
        if (m_npc->TargetMgr()->IsJammed())
            return false;
        double range(m_brain->AcquireRange(ctx));
        if (m_npc->GetEwar() != nullptr)
            range *= m_npc->GetEwar()->GetTargetRangeMult();
        if (m_npc->GetPosition().distance(pTarget->GetPosition()) > range)
            return false;
        // every lock slot taken:  another lock makes room, the primary only when it's the only one
        if (m_npc->TargetMgr()->TargetCount() >= m_profile->maxLockedTargets) {
            SystemEntity* pOld(nullptr);
            m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
                if ((pOld == nullptr) and (cur->GetID() != m_primaryID))
                    pOld = cur;
            });
            if ((pOld == nullptr) and (m_primaryID != 0))
                pOld = m_npc->TargetMgr()->GetTarget(m_primaryID, false);
            if (pOld != nullptr) {
                if (pOld->GetID() == m_primaryID)
                    m_primaryID = 0;
                m_npc->TargetMgr()->ClearTarget(pOld);
            }
        }
        bool chase(false);
        if (!m_npc->TargetMgr()->StartTargeting(pTarget, GetTargetTime(pTarget), m_profile->maxLockedTargets,
                                                m_brain->AcquireRange(ctx), chase)) {
            _log(NPC__AI_TRACE, "%s(%u): Targeting of %s(%u) failed.", m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
            return false;
        }
    }
    m_primaryID = pTarget->GetID();
    m_brain->OnPicked(ctx, pTarget);
    return true;
}

void SentryAI::SetIdle() {
    if (m_state == State::Idle) return;
    // not doing anything....idle.
    _log(NPC__AI_TRACE, "%s(%u): Idle: returning to idle.", \
    m_npc->GetName(), m_npc->GetID());
    m_state = State::Idle;
    m_primaryID = 0;
    // locks on what we no longer shoot (expired criminals, cloaked or warping ships) would fill our lock slots
    if (!m_npc->TargetMgr()->HasNoTargets())
        m_npc->TargetMgr()->ClearTargets();

    m_sched.Stop(NpcAct::Turret);
    m_sched.Stop(NpcAct::Missile);
    m_sched.Stop(NpcAct::SwitchEval);
    if (m_ewar != nullptr)
        m_ewar->StopAll();
}

void SentryAI::SetEngaged(SystemEntity* pTarget) {
    if (m_state == State::Engaged)
        return;
    _log(NPC__AI_TRACE, "%s(%u): Engaged: Begin engaging.  Target is %s(%u).", \
            m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
    // actively fighting
    m_state = State::Engaged;
    // law guns:  a random re-pick every crime.SentryRetarget
    int64 period(m_brain->RetargetMs());
    if (period > 0)
        m_sched.StartPeriodic(NpcAct::SwitchEval, Now(), period);
}

void SentryAI::CheckTarget(SentryContext& ctx, SystemEntity* pTarget)
{
    if (!m_brain->MayShoot(ctx, pTarget)) {
        _log(NPC__AI_TRACE, "%s(%u): %s(%u) is no longer a target.", m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
        ClearTarget(pTarget);
        return;
    }

    SetEngaged(pTarget);

    if (m_profile->hasTurret)
        m_sched.Ensure(NpcAct::Turret, ctx.now, m_profile->turretCycleMs, m_profile->turretCycleMs);
    if (m_profile->hasMissile)
        m_sched.Ensure(NpcAct::Missile, ctx.now, m_profile->missileCycleMs, m_profile->missileCycleMs);

    uint32 targetID(pTarget->GetID());
    Attack(ctx, pTarget);

    // the attack may have destroyed or dropped the target
    if (m_state == State::Idle)
        return;
    pTarget = m_npc->SystemMgr()->GetSE(targetID);
    if ((pTarget == nullptr) or pTarget->IsDead())
        return;
    FireMissile(pTarget);
}

double SentryAI::SurfaceDistance(SystemEntity* pTarget)
{
    double distance(m_npc->GetPosition().distance(pTarget->GetPosition()) - m_npc->GetRadius() - pTarget->GetRadius());
    return (distance > 0.0 ? distance : 0.0);
}

void SentryAI::FireMissile(SystemEntity* pTarget)
{
    if (!m_profile->hasMissile)
        return;
    int64 now(Now());
    if (!m_sched.IsDue(NpcAct::Missile, now))
        return;
    if ((pTarget->DestinyMgr() == nullptr) or pTarget->DestinyMgr()->IsCloaked())
        return;
    // only at a completed lock on this target (bug B6), inside min(entityAttackRange, 0.95 x flight range) (bug B7)
    if ((m_npc->TargetMgr()->GetTarget(pTarget->GetID(), true) == nullptr) or (SurfaceDistance(pTarget) > m_profile->MissileFireRange())) {
        m_sched.Defer(NpcAct::Missile, now);
        return;
    }
    m_sched.Due(NpcAct::Missile, now);
    LaunchEntityMissile(m_npc, m_npc->GetSelf(), m_profile->missileTypeID, pTarget);
}

void SentryAI::ClearTarget(SystemEntity* pTarget) {
    if (pTarget->GetID() == m_primaryID)
        m_primaryID = 0;
    m_npc->TargetMgr()->ClearTarget(pTarget);

    if (m_npc->TargetMgr()->HasNoTargets())
        SetIdle();
}

void SentryAI::ClearAllTargets() {
    m_primaryID = 0;
    m_npc->TargetMgr()->ClearAllTargets();
}

void SentryAI::Target(SystemEntity* pTarget) {
    if (pTarget == nullptr)
        return;
    SentryContext ctx(MakeContext());
    if (!SetPrimary(ctx, pTarget)) {
        if (m_npc->TargetMgr()->HasNoTargets())
            SetIdle();
        return;
    }
    CheckTarget(ctx, pTarget);
}

void SentryAI::Respond(SystemEntity* pSE)
{
    /* we're inside the other side's StartTargeting() or ApplyDamage() here:  the answer waits for our next think
     *  (the first one wins, like NPCAIMgr's pending aggressor) */
    if ((pSE == nullptr) or (pSE == m_npc) or (m_state != State::Idle) or (m_pendingAggroID != 0))
        return;
    m_pendingAggroID = pSE->GetID();
}

void SentryAI::PruneLocks(SentryContext& ctx)
{
    m_stale.clear();
    m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        if (!m_brain->MayShoot(ctx, cur))
            m_stale.push_back(cur);
    });
    // after the walk:  clearing changes the lock map
    for (auto cur : m_stale) {
        if (cur->GetID() == m_primaryID)
            m_primaryID = 0;
        m_npc->TargetMgr()->ClearTarget(cur);
    }
    m_stale.clear();
}

void SentryAI::Targeted(SystemEntity* pAgressor) {
    // gate and station guns never lock back an innocent (bug B9).  nothing locks back a pod unless npc.TargetPod says so
    Respond(pAgressor);
}

void SentryAI::OnDamaged(SystemEntity* pBy, float amount)
{
    Respond(pBy);
}

void SentryAI::OnGroupAlert(SystemEntity* pAggressor)
{
    Respond(pAggressor);
}

void SentryAI::TargetLost(SystemEntity* pTarget) {
    if (pTarget->GetID() == m_primaryID)
        m_primaryID = 0;
    if ((m_state == State::Engaged) and m_npc->TargetMgr()->HasNoTargets()) {
        _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) lost. No targets remain.  Return to Idle.", \
                m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
        SetIdle();
    }
    // with targets left, the next think picks the next primary (GetPrimary)
}

void SentryAI::OnLockComplete(SystemEntity* pTarget)
{
    // from TargetManager::Process() while it walks our targets:  don't add or remove targets here
    if ((pTarget != nullptr) and (m_primaryID == 0))
        m_primaryID = pTarget->GetID();
}

void SentryAI::Attack(SentryContext& ctx, SystemEntity* pTarget)
{
    if (!pTarget or !m_profile->hasTurret)
        return;
    if (!m_sched.IsDue(NpcAct::Turret, ctx.now))
        return;
    if (m_npc->SysBubble() == nullptr)
        return;
    // Check to see if the target still in the bubble (Client warped out)
    if (!m_npc->SysBubble()->InBubble(pTarget->GetPosition())) {
        _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) no longer in bubble.  Clear target and move on",
             m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
        ClearTarget(pTarget);
        return;
    }
    if (!pTarget->DestinyMgr()) {
        _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) has no destiny manager.  Clear target and move on",
             m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
        ClearTarget(pTarget);
        return;
    }
    // Check to see if the target is not cloaked:
    if (pTarget->DestinyMgr()->IsCloaked()) {
        _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) is cloaked.  Clear target and move on",
             m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
        ClearTarget(pTarget);
        return;
    }
    // only at a completed lock on this target (bug B6b), inside entityAttackRange (bug B7).  the shot waits, it doesn't pile up
    if ((m_npc->TargetMgr()->GetTarget(pTarget->GetID(), true) == nullptr) or (SurfaceDistance(pTarget) > m_profile->attackRange)) {
        m_sched.Defer(NpcAct::Turret, ctx.now);
        return;
    }
    m_sched.Due(NpcAct::Turret, ctx.now);
    AttackTarget(ctx, pTarget);
}

void SentryAI::AttackTarget(SentryContext& ctx, SystemEntity* pTarget) {
    std::string guid = "effects.Laser";
    // the target's bubble sees the shot through the target's destiny
    pTarget->DestinyMgr()->SendSpecialEffect(m_npc->GetSelf()->itemID(),
                                             m_npc->GetSelf()->itemID(),
                                             m_npc->GetSelf()->typeID(), //m_npc->GetSelf()->GetAttribute(AttrGfxTurretID).get_int(),
                                             pTarget->GetID(),
                                             0,guid,1,1,1,m_profile->turretCycleMs,0);

    /* gate and station guns:  every shot lands with quality 1.0, unaffected by transversal and signature radius (no wrecking).
     *  pirate and deadspace sentries:  the shared turret formula with maxRange as optimal */
    float toHit(m_brain->TurretHit(ctx, pTarget));
    Damage d(m_npc,
             m_npc->GetSelf(),
             m_npc->GetKinetic(),
             m_npc->GetThermal(),
             m_npc->GetEM(),
             m_npc->GetExplosive(),
             toHit,
             EVEEffectID::targetAttack
    );

    // the type's damageMultiplier as it is (bug B9:  it was read as an integer, 3.5 became 3)
    if (m_profile->damageMultiplier > 0)
        d *= m_profile->damageMultiplier;
    pTarget->ApplyDamage(d);
}

double SentryAI::GetTargetTime(SystemEntity* pTarget)
{
    // the player formula with scanResolution where the type has it, else its scanSpeed, else the radius table
    double sig(0.0);
    if (pTarget != nullptr) {
        sig = pTarget->GetSignatureRadius();
        if (sig <= 0.0)
            sig = pTarget->GetRadius();
    }
    double mult(m_npc->GetEwar() != nullptr ? m_npc->GetEwar()->GetScanResMult() : 1.0);
    return m_profile->LockTimeMs(sig, mult);
}

std::string SentryAI::GetStateName(SentryAI::State name)
{
    switch (name) {
        case State::Idle:      return "Idle";
        case State::Engaged:   return "Engaged";
    }
    return "Undefined";
}

std::string SentryAI::Dump(SystemEntity* pViewer, bool verbose)
{
    const NpcProfile& p(*m_profile);
    SentryContext ctx(MakeContext());
    std::ostringstream str;
    str << std::fixed << std::setprecision(0);
    str << m_npc->GetName() << " (" << m_npc->GetID() << ")  type " << p.typeID << " group " << p.groupID << "<br>";
    str << "sentry, brain " << m_brain->GetName() << ", state " << GetStateName(m_state) << ", primary " << m_primaryID
        << ", rng seed " << m_rng.GetSeed() << "<br>";
    if (m_npc->IsLawSentry())
        str << "guards " << m_npc->GetProtectedID() << " (faction " << m_npc->GetWarFactionID() << ")<br>";
    str << "locks (" << m_npc->TargetMgr()->TargetCount() << "/" << (uint32)p.maxLockedTargets << "):";
    m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        str << "  " << cur->GetName() << "(" << cur->GetID() << ") ";
        if (locked) {
            str << "locked";
        } else {
            str << "locking " << m_npc->TargetMgr()->LockRemaining(cur) << "ms";
        }
    });
    str << "<br>next due (ms):";
    for (uint8 i = 0; i < NpcAct::Count; ++i)
        if (m_sched.Active(i))
            str << "  " << NpcSchedule::Name(i) << " " << m_sched.Remaining(i, ctx.now);
    str << "<br>weapons:  turret " << (p.hasTurret ? "yes" : "no") << " (" << p.turretCycleMs << "ms, x" << std::setprecision(2)
        << p.damageMultiplier << std::setprecision(0) << ", opt " << p.optimal << " fo " << p.falloff << ")"
        << ", missile " << (p.hasMissile ? "yes" : "no") << " (type " << p.missileTypeID << ", " << p.missileCycleMs << "ms)<br>";
    str << "ranges:  acquire " << m_brain->AcquireRange(ctx) << ", R_atk " << p.attackRange;
    if (pViewer != nullptr) {
        str << ", t_lock to " << pViewer->GetName() << " " << GetTargetTime(pViewer) << "ms";
        str << ", " << pViewer->GetName() << " is " << (m_brain->MayShoot(ctx, pViewer) ? "a target" : "not a target");
    }
    str << "<br>";
    if (verbose and (p.problems != 0))
        str << "data:  " << NpcProfile::ProblemList(p.problems) << "<br>";
    return str.str();
}
