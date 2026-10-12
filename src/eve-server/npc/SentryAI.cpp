/**
 * @name SentryAI.h
 *   Sentry AI class
 * @Author:     Allan
 * @date:       23 April 2017
 * @version     0.11
 */


#include "eve-server.h"

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "npc/Sentry.h"
#include "npc/SentryAI.h"
#include "system/DestinyManager.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/CrimeWatch.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/NPCEwar.h"
#include "npc/ai/NpcProfile.h"
#include "ship/modules/TurretFormulas.h"
#include "EVEServerConfig.h"

SentryAI::SentryAI(Sentry* who)
: m_state(State::Idle),
m_npc(who),
m_primaryID(0),
m_ewar(nullptr)
{
    m_ewarChecked = false;
    m_profile = NpcProfile::Get(who->GetSelf()->type());

    // 'sight' range
    m_sightRange = 20000;
    if (m_profile->attackRange > m_sightRange)
        m_sightRange = static_cast<uint32>(m_profile->attackRange *2);

    // one look around per second (bug B1:  this used to wait for the turret cycle, and never came for sentries without one)
    m_sched.Start(NpcAct::Think, Now(), 1000 + (who->GetID() % 4) * 250, 1000);
}

SentryAI::~SentryAI()
{
    SafeDelete(m_ewar);
}

int64 SentryAI::Now()
{
    return GetSteadyTime();
}

void SentryAI::OnKilled()
{
    if (m_ewar != nullptr)
        m_ewar->StopAll();
}

void SentryAI::Process() {
    if (!m_ewarChecked) {
        // gate and station guns never use ewar.  LawEnforcement sets that role after we're made
        m_ewarChecked = true;
        if (sConfig.npc.UseEwar and !m_npc->IsLawSentry()) {
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

    /* NPC::State definitions   -allan 25July15  (UD 1June16)
    *   Idle,       // not doing anything, nothing in sight....idle.
    *   Engaged,    // actively fighting.
    *   Signaling   // calling for help
    */
    switch(m_state) {
        case State::Idle: {
            // law sentries:  criminals and those flagged to our owner.  pirate and deadspace sentries:  the nearest ship in sight
            SystemEntity* pTarget(m_npc->IsLawSentry() ? FindLawTarget() : FindTarget());
            if (pTarget != nullptr)
                Target(pTarget);
        } break;
        case State::Engaged: {
            if (m_npc->TargetMgr()->HasNoTargets()) {
                _log(NPC__AI_TRACE, "%s(%u): Stopped %s, HasNoTargets = true.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state).c_str());
                SetIdle();
                return;
            }
            SystemEntity* pTarget(GetPrimary());
            if (pTarget == nullptr) {
                _log(NPC__AI_TRACE, "%s(%u): Stopped %s, no primary target.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state).c_str());
                SetIdle();
                return;
            }
            if (pTarget->SysBubble() == nullptr) {
                ClearTarget(pTarget);
                return;
            }
            CheckDistance(pTarget);
        } break;
        case State::Signaling: {
            _log(NPC__AI_TRACE, "%s(%u): Called %s, needs to be completed.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state).c_str());
            // not sure how im gonna do this
        } break;
    }
}

SystemEntity* SentryAI::GetPrimary()
{
    if (m_primaryID != 0) {
        SystemEntity* pTarget(m_npc->TargetMgr()->GetTarget(m_primaryID, false));
        if (pTarget != nullptr) {
            // gate and station guns let go of anyone who's no longer a criminal or flagged to our owner
            if (m_npc->IsLawSentry() and !IsLawTarget(pTarget)) {
                m_primaryID = 0;
                ClearTarget(pTarget);
                return nullptr;
            }
            return pTarget;
        }
        m_primaryID = 0;
    }
    // the nearest of what we have locked (or are locking)
    SystemEntity* pBest(nullptr);
    double nearest(0.0);
    m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        if (cur->IsDead() or (m_npc->IsLawSentry() and !IsLawTarget(cur)) or !PodRuleAllows(cur))
            return;
        double distance(m_npc->GetPosition().distance(cur->GetPosition()));
        if ((pBest == nullptr) or (distance < nearest)) {
            pBest = cur;
            nearest = distance;
        }
    });
    if (pBest != nullptr)
        m_primaryID = pBest->GetID();
    return pBest;
}

void SentryAI::SetIdle() {
    if (m_state == State::Idle) return;
    // not doing anything....idle.
    _log(NPC__AI_TRACE, "%s(%u): Idle: returning to idle.", \
    m_npc->GetName(), m_npc->GetID());
    m_state = State::Idle;
    m_primaryID = 0;

    m_sched.Stop(NpcAct::Turret);
    m_sched.Stop(NpcAct::Missile);
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
}

void SentryAI::CheckDistance(SystemEntity* pTarget)
{
    double dist = m_npc->GetPosition().distance(pTarget->GetPosition());
    if ((dist > m_sightRange) and (!m_npc->TargetMgr()->IsTargetedBy(pTarget))) {
        _log(NPC__AI_TRACE, "%s(%u): CheckDistance: %s(%u) is too far away (%.0fm).  Return to Idle.", \
                m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID(), dist);
        if (m_state != State::Idle) {
            // target is no longer in npc's "sight range" and is NOT targeting this npc.  unlock target and return to idle.
            ClearTarget(pTarget);
        }
        return;
    }

    SetEngaged(pTarget);

    int64 now(Now());
    if (m_profile->hasTurret)
        m_sched.Ensure(NpcAct::Turret, now, m_profile->turretCycleMs, m_profile->turretCycleMs);
    if (m_profile->hasMissile)
        m_sched.Ensure(NpcAct::Missile, now, m_profile->missileCycleMs, m_profile->missileCycleMs);

    uint32 targetID(pTarget->GetID());
    Attack(pTarget);

    // the attack may have destroyed or dropped the target
    if (m_state == State::Idle)
        return;
    pTarget = m_npc->SystemMgr()->GetSE(targetID);
    if ((pTarget == nullptr) or pTarget->IsDead())
        return;
    FireMissile(pTarget);
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
    // only at a completed lock on this target (bug B6)
    if (m_npc->TargetMgr()->GetTarget(pTarget->GetID(), true) == nullptr) {
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
    //m_npc->TargetMgr()->OnTarget(pTarget, TargMgr::Mode::Lost);

    if (m_npc->TargetMgr()->HasNoTargets())
        SetIdle();
}

void SentryAI::ClearAllTargets() {
    m_primaryID = 0;
    m_npc->TargetMgr()->ClearAllTargets();
    //m_npc->TargetMgr()->OnTarget(nullptr, TargMgr::Mode::Clear, TargMgr::Msg::ClientReq);
}

void SentryAI::Target(SystemEntity* pTarget) {
    bool chase = false;

    // maxLockedTargets caps our locks (bug B9:  it used maxAttackTargets, 1)
    if (!m_npc->TargetMgr()->StartTargeting(pTarget, GetTargetTime(pTarget), m_profile->maxLockedTargets, m_sightRange, chase)) {
        if (!chase) {
            _log(NPC__AI_TRACE, "%s(%u): Targeting of %s(%u) failed.  Clear Target and Return to Idle.", \
                    m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
            SetIdle();
        }
        return;
    }
    if (m_primaryID == 0)
        m_primaryID = pTarget->GetID();
    CheckDistance(pTarget);
}

void SentryAI::Targeted(SystemEntity* pAgressor) {
    if (pAgressor == nullptr)
        return;
    // gate and station guns never lock back an innocent (bug B9).  nothing locks back a pod unless npc.TargetPod says so
    if (m_npc->IsLawSentry() and !IsLawTarget(pAgressor))
        return;
    if (!PodRuleAllows(pAgressor))
        return;
    switch(m_state) {
        case State::Idle: {
            _log(NPC__AI_TRACE, "%s(%u): Targeted by %s(%u) in Idle. Begin Targeting sequence.", \
                    m_npc->GetName(), m_npc->GetID(), pAgressor->GetName(), pAgressor->GetID());
            Target(pAgressor);
        } break;
        /** @todo  determine if new targetedby entity is weaker than current target. use optimalSigRadius to test for 'optimal' target */
        case State::Engaged: {
            _log(NPC__AI_TRACE, "%s(%u): Targeted by %s(%u) while engaged.", \
                    m_npc->GetName(), m_npc->GetID(), pAgressor->GetName(), pAgressor->GetID());
        } break;
        case State::Signaling: {
            _log(NPC__AI_TRACE, "%s(%u): Targeted by %s(%u) while signaling.", \
                    m_npc->GetName(), m_npc->GetID(), pAgressor->GetName(), pAgressor->GetID());
        } break;
    }
}

void SentryAI::TargetLost(SystemEntity* pTarget) {
    if (pTarget->GetID() == m_primaryID)
        m_primaryID = 0;
    if (m_state == State::Engaged) {
        if (m_npc->TargetMgr()->HasNoTargets()) {
            _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) lost. No targets remain.  Return to Idle.", \
                    m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
            SetIdle();
        } else {
            _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) lost, but more targets remain.", \
                    m_npc->GetName(), m_npc->GetID(), pTarget->GetName(), pTarget->GetID());
            /** @todo engage weakest target in current list */
        }
    }
}

void SentryAI::OnLockComplete(SystemEntity* pTarget)
{
    // from TargetManager::Process() while it walks our targets:  don't add or remove targets here
    if ((pTarget != nullptr) and (m_primaryID == 0))
        m_primaryID = pTarget->GetID();
}

void SentryAI::Attack(SystemEntity* pTarget)
{
    if (!pTarget or !m_profile->hasTurret)
        return;
    int64 now(Now());
    if (!m_sched.IsDue(NpcAct::Turret, now))
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
    // only at a completed lock on this target (bug B6b:  it used to be any lock).  the shot waits, it doesn't pile up
    if (m_npc->TargetMgr()->GetTarget(pTarget->GetID(), true) == nullptr) {
        m_sched.Defer(NpcAct::Turret, now);
        return;
    }
    m_sched.Due(NpcAct::Turret, now);
    AttackTarget(pTarget);
}

//also check for special effects and write code to implement them
//modifyTargetSpeedRange, modifyTargetSpeedChance
//entityWarpScrambleChance
void SentryAI::AttackTarget(SystemEntity* pTarget) {
    std::string guid = "effects.Laser";
    // sentry does NOT have a destiny manager...use target's destiny manager for sending fx
    pTarget->DestinyMgr()->SendSpecialEffect(m_npc->GetSelf()->itemID(),
                                             m_npc->GetSelf()->itemID(),
                                             m_npc->GetSelf()->typeID(), //m_npc->GetSelf()->GetAttribute(AttrGfxTurretID).get_int(),
                                             pTarget->GetID(),
                                             0,guid,1,1,1,m_profile->turretCycleMs,0);

    // gate and station guns ignore transversal and signature radius.  others use the turret formula with maxRange as optimal
    TurretFormulas formula;
    Damage d(m_npc,
             m_npc->GetSelf(),
             m_npc->GetKinetic(),
             m_npc->GetThermal(),
             m_npc->GetEM(),
             m_npc->GetExplosive(),
             (m_npc->IsLawSentry() ? 1.0f : formula.GetSentryToHit(m_npc, pTarget)),
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
        case State::Signaling: return "Signaling";
    }
    return "Undefined";
}

bool SentryAI::PodRuleAllows(SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or !pTarget->HasPilot() or !pTarget->GetPilot()->InPod())
        return true;
    if (!sConfig.npc.TargetPod)
        return false;
    return (m_npc->SystemMgr()->GetSystemSecurityRating() <= sConfig.npc.TargetPodSec);
}

bool SentryAI::IsLawTarget(SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or pTarget->IsDead() or (pTarget->DestinyMgr() == nullptr))
        return false;
    if (pTarget->DestinyMgr()->IsCloaked() or pTarget->DestinyMgr()->IsWarping())
        return false;
    // an NPC made a criminal (GM testing)
    if (pTarget->IsNPCSE())
        return ((m_npc->GetPosition().distance(pTarget->GetPosition()) <= sConfig.crime.SentryRange)
            and sCrimeWatch.IsCriminal(pTarget->GetID()));
    uint32 charID(0);
    if (pTarget->IsShipSE()) {
        if (pTarget->GetPilot() != nullptr) {
            if (pTarget->GetPilot()->InPod())
                return false;
            charID = pTarget->GetPilot()->GetCharacterID();
        } else {
            charID = pTarget->GetSelf()->ownerID();     // a ship left in space by a pilot who logged off
        }
    } else if (pTarget->IsDroneSE()) {
        charID = pTarget->GetSelf()->ownerID();
    }
    if (!IsCharacterID(charID))
        return false;
    if (m_npc->GetPosition().distance(pTarget->GetPosition()) > sConfig.crime.SentryRange)
        return false;
    return (sCrimeWatch.IsCriminal(charID) or sCrimeWatch.IsFlaggedTo(charID, m_npc->GetSelf()->ownerID()));
}

SystemEntity* SentryAI::FindTarget()
{
    if (m_npc->SysBubble() == nullptr)
        return nullptr;
    SystemEntity* pTarget(nullptr);
    double nearest(m_sightRange);
    m_npc->SysBubble()->ForEachPlayer([&](Client* cur) {
        if (cur->IsInvul() or (cur->GetShipSE() == nullptr))
            return;
        if (cur->InPod()) {
            if (!sConfig.npc.TargetPod)
                return;
            if (m_npc->SystemMgr()->GetSystemSecurityRating() > sConfig.npc.TargetPodSec)
                return;
        }
        DestinyManager* pDestiny(cur->GetShipSE()->DestinyMgr());
        if ((pDestiny == nullptr) or pDestiny->IsCloaked() or pDestiny->IsWarping())
            return;
        if (cur->GetShipSE()->SysBubble() != m_npc->SysBubble())
            return;
        double distance(m_npc->GetPosition().distance(cur->GetShipSE()->GetPosition()));
        if (distance > nearest)
            return;
        nearest = distance;
        pTarget = cur->GetShipSE();
    });
    return pTarget;
}

SystemEntity* SentryAI::FindLawTarget()
{
    if (m_npc->SysBubble() == nullptr)
        return nullptr;
    SystemEntity* pFound(nullptr);
    m_npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        if (pFound != nullptr)
            return;
        if ((cur->IsShipSE() or cur->IsDroneSE() or cur->IsNPCSE())
        and !(cur->IsNPCSE() and cur->GetNPCSE()->IsLawEnforcer()) and IsLawTarget(cur))
            pFound = cur;
    });
    return pFound;
}

std::string SentryAI::Dump(SystemEntity* pViewer, bool verbose)
{
    const NpcProfile& p(*m_profile);
    int64 now(Now());
    std::ostringstream str;
    str << std::fixed << std::setprecision(0);
    str << m_npc->GetName() << " (" << m_npc->GetID() << ")  type " << p.typeID << " group " << p.groupID << "<br>";
    str << "sentry" << (m_npc->IsLawSentry() ? " (law)" : "") << ", state " << GetStateName(m_state) << ", primary " << m_primaryID << "<br>";
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
            str << "  " << NpcSchedule::Name(i) << " " << m_sched.Remaining(i, now);
    str << "<br>weapons:  turret " << (p.hasTurret ? "yes" : "no") << " (" << p.turretCycleMs << "ms, x" << std::setprecision(2)
        << p.damageMultiplier << std::setprecision(0) << ", opt " << p.optimal << " fo " << p.falloff << ")"
        << ", missile " << (p.hasMissile ? "yes" : "no") << " (type " << p.missileTypeID << ", " << p.missileCycleMs << "ms)<br>";
    str << "ranges:  sight " << m_sightRange << ", R_atk " << p.attackRange;
    if (pViewer != nullptr)
        str << ", t_lock to " << pViewer->GetName() << " " << GetTargetTime(pViewer) << "ms";
    str << "<br>";
    if (verbose and (p.problems != 0))
        str << "data:  " << NpcProfile::ProblemList(p.problems) << "<br>";
    return str.str();
}
