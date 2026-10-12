 /**
  * @name NpcMovement.cpp
  *   how an npc flies while it fights (PLAN.md 5.5).  see NpcMovement.h
  */

#include "eve-server.h"

#include "EVEServerConfig.h"
#include "npc/NPC.h"
#include "npc/ai/NpcMath.h"
#include "npc/ai/NpcMovement.h"
#include "npc/ai/NpcProfile.h"
#include "npc/ai/NpcRng.h"
#include "npc/ai/NpcSchedule.h"
#include "ship/modules/ActiveModule.h"
#include "system/DestinyManager.h"
#include "system/TargetManager.h"

namespace {
    // evasive approach:  the side of the line of sight changes this often (ms)
    const int64 c_evasiveFlipMs = 5000;
}

NpcMovement::NpcMovement(NPC* pNPC, const NpcProfile& profile, NpcRng& rng, NpcSchedule& sched)
: m_npc(pNPC),
  m_profile(profile),
  m_rng(rng),
  m_sched(sched),
  m_mode(NpcMath::Move::None),
  m_boosting(false),
  m_boostPhase(false),
  m_blocked(false),
  m_evasive(false),
  m_fastOrbit(false),
  m_evasiveSide(1),
  m_targetID(0),
  m_rangeOverride(0.0f),
  m_evasiveFlipAt(0),
  m_lastGoto(NULL_ORIGIN)
{
}

const char* NpcMovement::GetModeName() const
{
    switch (m_mode) {
        case NpcMath::Move::Orbit:  return "orbit";
        case NpcMath::Move::Follow: return "follow";
        case NpcMath::Move::Chase:  return (m_boosting ? "chase (boosting)" : (m_blocked ? "chase (mwd blocked)" : "chase (stalling)"));
    }
    return "none";
}

double NpcMovement::OrbitRange() const
{
    return ((m_rangeOverride > 0.0f) ? m_rangeOverride : m_profile.orbitRange);
}

double NpcMovement::BoostRange() const
{
    return std::max(static_cast<double>(m_profile.boostRange), OrbitRange());
}

void NpcMovement::Engage(SystemEntity* pTarget, int64 now)
{
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    if ((pTarget == nullptr) or (pDestiny == nullptr) or pDestiny->IsWarping())
        return;

    double distance(EvE::max(m_npc->GetPosition().distance(pTarget->GetPosition()) - m_npc->GetRadius() - pTarget->GetRadius()));
    double orbit(OrbitRange());
    uint8 mode(NpcMath::MoveMode(distance, orbit, BoostRange()));
    bool newTarget(pTarget->GetID() != m_targetID);
    uint8 oldMode(m_mode);
    m_targetID = pTarget->GetID();
    m_mode = mode;

    switch (mode) {
        case NpcMath::Move::Orbit: {
            EndChase();
            pDestiny->SetMaxVelocity(m_fastOrbit ? m_profile.maxVelocity : m_profile.cruiseSpeed);
            pDestiny->Orbit(pTarget, static_cast<uint32>(orbit));      // no update when nothing changed
        } break;
        case NpcMath::Move::Follow: {
            EndChase();
            pDestiny->SetMaxVelocity(m_fastOrbit ? m_profile.maxVelocity : m_profile.cruiseSpeed);
            pDestiny->Follow(pTarget, static_cast<uint32>(orbit));
        } break;
        case NpcMath::Move::Chase: {
            if (newTarget or (oldMode != NpcMath::Move::Chase)) {
                StartStall(now);
            } else if (m_sched.Due(NpcAct::ChaseDelay, now)) {
                StartBoost(now);
            } else if (m_sched.Due(NpcAct::ChaseBoost, now)) {
                // boosted long enough:  stall again (and look again next tick)
                StartStall(now);
            }
            // a player's scrambler shuts the 'mwd' off (effect 3725)
            if (m_boostPhase) {
                bool blocked(CheckMWDBlock());
                if (blocked != m_blocked) {
                    m_blocked = blocked;
                    _log(NPC__AI_TRACE, "%s(%u): chase boost %s by a warp scrambler.", m_npc->GetName(), m_npc->GetID(),
                            (blocked ? "stopped" : "free again"));
                }
                ApplyBoost(!m_blocked);
            }
            if (m_evasive) {
                Evade(pTarget, distance, now);
            } else {
                pDestiny->Follow(pTarget, static_cast<uint32>(orbit));
            }
        } break;
    }

    if (oldMode != m_mode)
        _log(NPC__AI_TRACE, "%s(%u): movement %s on %s(%u) at %.0fm (r_o %.0f, r_b %.0f).", m_npc->GetName(), m_npc->GetID(),
                GetModeName(), pTarget->GetName(), pTarget->GetID(), distance, orbit, BoostRange());
}

void NpcMovement::StartStall(int64 now)
{
    m_sched.Stop(NpcAct::ChaseBoost);
    m_boostPhase = false;
    m_blocked = false;
    ApplyBoost(false);
    double stall(NpcMath::ChasePhaseMs(m_profile.chaseMaxDelay, m_profile.chaseMaxDelayChance, m_rng.Uniform(), m_rng.Uniform()));
    if (stall < 1.0) {
        StartBoost(now);
        return;
    }
    m_sched.Start(NpcAct::ChaseDelay, now, static_cast<int64>(stall));
}

void NpcMovement::StartBoost(int64 now)
{
    m_sched.Stop(NpcAct::ChaseDelay);
    m_boostPhase = true;
    m_blocked = CheckMWDBlock();
    ApplyBoost(!m_blocked);
    // entityChaseMaxDuration 0:  no limit (as before);  otherwise the full time with chance 583, else U(0, 582)
    if (m_profile.chaseMaxDuration <= 0.0f) {
        m_sched.Stop(NpcAct::ChaseBoost);
        return;
    }
    double boost(NpcMath::ChasePhaseMs(m_profile.chaseMaxDuration, m_profile.chaseMaxDurationChance, m_rng.Uniform(), m_rng.Uniform()));
    m_sched.Start(NpcAct::ChaseBoost, now, std::max<int64>(1, static_cast<int64>(boost)));
}

void NpcMovement::EndChase()
{
    m_sched.Stop(NpcAct::ChaseDelay);
    m_sched.Stop(NpcAct::ChaseBoost);
    m_boostPhase = false;
    m_blocked = false;
    ApplyBoost(false);
}

void NpcMovement::ApplyBoost(bool boost)
{
    m_boosting = boost;
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    if (pDestiny != nullptr)
        pDestiny->SetMaxVelocity(boost ? m_profile.maxVelocity : m_profile.cruiseSpeed);
    // "used to fake MWD sig radius increase" (1133)
    float mult(1.0f);
    if (boost and sConfig.npc.ChaseSigBloom and (m_profile.sigBloom > 0.0f))
        mult = m_profile.sigBloom;
    m_npc->SetSigMult(mult);
}

bool NpcMovement::CheckMWDBlock()
{
    TargetManager* pTargMgr(m_npc->TargetMgr());
    if ((pTargMgr == nullptr) or !pTargMgr->HasActiveModules())
        return false;
    m_modules.clear();
    pTargMgr->GetTargetModules(m_modules);
    bool blocked(false);
    for (auto pMod : m_modules)
        if ((pMod != nullptr) and pMod->IsActive()
        and pMod->GetSelf()->type().HasEffect(EVEEffectID::warpScrambleTargetMWDBlockActivation)) {
            blocked = true;
            break;
        }
    m_modules.clear();
    return blocked;
}

void NpcMovement::Evade(SystemEntity* pTarget, double distance, int64 now)
{
    /* evasive approach (1414):  aim beside the target, d/2 off the line of sight, and change sides every 5s.
     *  the goto point is sent again only when the side flips or the target moved away from it */
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    if (now >= m_evasiveFlipAt) {
        m_evasiveSide = -m_evasiveSide;
        m_evasiveFlipAt = now + c_evasiveFlipMs;
        m_lastGoto = NULL_ORIGIN;
    }
    GVector los(m_npc->GetPosition(), pTarget->GetPosition());
    if (los.isZero()) {
        pDestiny->Follow(pTarget, static_cast<uint32>(OrbitRange()));
        return;
    }
    los.normalize();
    // any direction across the line of sight:  up x los, or x-axis x los when they are parallel
    GVector side(GVector(0, 1, 0).crossProduct(los));
    if (side.isZero())
        side = GVector(1, 0, 0).crossProduct(los);
    side.normalize();
    GPoint point(pTarget->GetPosition() + (side * (0.5 * distance * m_evasiveSide)));
    if (m_lastGoto.isZero() or (m_lastGoto.distance(point) > 0.1 * distance)) {
        m_lastGoto = point;
        pDestiny->GotoPoint(point);
    }
}

void NpcMovement::GoHome(const GPoint& home)
{
    EndChase();
    m_mode = NpcMath::Move::None;
    m_targetID = 0;
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    if (pDestiny == nullptr)
        return;
    pDestiny->SetMaxVelocity(m_profile.maxVelocity);
    pDestiny->GotoPoint(home);
}

void NpcMovement::Calm()
{
    EndChase();
    m_mode = NpcMath::Move::None;
    m_targetID = 0;
    m_lastGoto = NULL_ORIGIN;
}
