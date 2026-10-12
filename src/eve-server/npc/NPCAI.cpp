/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
    Author:        Zhur
    Rewrite:    Allan
    AI Version: 0.58
*/

/*  Crucible-era npc ai:
 *   regular rats: nearest ship in sight, keep the primary until it's gone, group aggro, chase speed timers,
 *    now and then a drone that hurt them, ewar from their type (npc/NPCEwar.h), idle groups warp between belts.
 *   advanced ai (types with AI_ShouldUseTargetSwitching > 0: sleepers, incursion sansha, officers): threat/signature
 *    scoring incl. drones, timed target switching, ewar on a secondary target, remote repair of friends, evasive orbit, leash.
 *   rats neither flee nor call for help (Fleeing/Signaling states are unused).
 *
 *  doc/code_and_design_notes/npc-ai/PLAN.md phases 0-1:  the type's data is its NpcProfile (dogma defaults for missing
 *   attributes, weapons from effects), the policy its brain (npc/ai/NpcBrain.h), every cycle runs on a drift-free
 *   NpcSchedule and every roll on the npc's own NpcRng.  weapons fire only at a completed lock on the primary, inside
 *   entityAttackRange.  idle npcs in bubbles without players sleep (npc.SleepEmptyBubbles).
 */

#include "eve-server.h"

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/LawEnforcement.h"
#include "npc/NPCEwar.h"
#include "npc/ai/NpcBrain.h"
#include "npc/ai/NpcProfile.h"
#include "ship/Missile.h"
#include "ship/modules/TurretFormulas.h"
#include "system/DestinyManager.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"

NPCAIMgr::NPCAIMgr(NPC* who)
: m_state(NPCAI::State::Idle),
  m_npc(who),
  m_destiny(who->DestinyMgr()),
  m_self(who->GetSelf()),
  m_secondaryID(0),
  m_switchToID(0),
  m_moveTargetID(0),
  m_isWandering(false),
  m_ewarChecked(false),
  m_chaseFast(true),
  m_dormant(false),
  m_primaryID(0),
  m_pendingAggroID(0),
  m_lastGoto(NULL_ORIGIN),
  m_home(NULL_ORIGIN),
  m_ewar(nullptr),
  m_brain(nullptr)
{
    assert(m_self.get() != nullptr);
    m_threat.Clear();
    m_profile = NpcProfile::Get(m_self->type());

    // every roll this npc makes.  a fixed npc.RngSeed makes them repeatable
    uint64_t seed(sConfig.npc.RngSeed);
    if (seed == 0)
        seed = static_cast<uint64_t>(GetSteadyTime()) ^ (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) << 16);
    m_rng.Seed(seed, m_self->itemID());

    /* movement:  today's numbers (phase 2 moves to the profile's orbitRange and boostRange, PLAN.md 5.5) */
    // chase speed (maxVelocity, their mwd) is used outside entityChaseMaxDistance (m_boostRange):
    //  entityChaseMaxDelay        - most time stalled before chase speed kicks in
    //  entityChaseMaxDelayChance  - chance the npc stalls first
    //  entityChaseMaxDuration     - most time chase speed is used before stalling again
    //  entityChaseMaxDurationChance - chance a chase lasts that long (else it's shorter)
    m_chaseMaxDelay = static_cast<uint32>(EvE::max(m_profile->chaseMaxDelay));
    m_chaseMaxDuration = static_cast<uint32>(EvE::max(m_profile->chaseMaxDuration));
    if ((m_chaseMaxDuration > 0) and (m_chaseMaxDuration < 1000))
        m_chaseMaxDuration = 1000;
    if (m_chaseMaxDelay > 60000)
        m_chaseMaxDelay = 60000;
    // Orbit Range, Follow Range  - npc tries to stay at this distance from active target
    m_flyRange = static_cast<uint32>(EvE::max(m_profile->flyRange));
    // distance for Speed Boost activation  (this needs to be revisited)
    m_boostRange = static_cast<uint32>(EvE::max(m_profile->chaseMaxDistance));
    // some npcs have flyRange > boostRange.  this corrects it. (extends boost range)
    if (m_flyRange > m_boostRange)
        m_boostRange += m_boostRange + m_flyRange;
    // 'sight' range:  by size, or twice entityAttackRange when that's more (phase 2: R_acq, PLAN.md 5.4)
    m_sightRange = m_profile->sightRange;
    // how far group aggro (and remote repair) reaches
    m_assistRange = m_self->GetAttribute(AttrNpcAssistanceRange).get_uint32();

    /* self repair.  an npc reps when its type has a repair amount.  the delay chance is rolled every cycle:
     *  on success that cycle is skipped.  the timers start when we take damage and stop once we're whole again */
    m_armorRepairDuration = 0;
    m_armorRepairDelayChance = 0;
    if (m_self->GetAttribute(AttrEntityArmorRepairAmount).get_float() > 0) {
        m_armorRepairDuration = m_self->GetAttribute(AttrEntityArmorRepairDuration).get_uint32();
        if (m_self->HasAttribute(AttrEntityArmorRepairDelayChance)) {
            m_armorRepairDelayChance = m_self->GetAttribute(AttrEntityArmorRepairDelayChance).get_float();
        } else if (m_self->HasAttribute(AttrEntityArmorRepairDelayChanceSmall)) {
            m_armorRepairDelayChance = m_self->GetAttribute(AttrEntityArmorRepairDelayChanceSmall).get_float();
        } else if (m_self->HasAttribute(AttrEntityArmorRepairDelayChanceMedium)) {
            m_armorRepairDelayChance = m_self->GetAttribute(AttrEntityArmorRepairDelayChanceMedium).get_float();
        } else if (m_self->HasAttribute(AttrEntityArmorRepairDelayChanceLarge)) {
            m_armorRepairDelayChance = m_self->GetAttribute(AttrEntityArmorRepairDelayChanceLarge).get_float();
        }
    }

    m_shieldBoosterDuration = 0;
    m_shieldBoosterDelayChance = 0;
    if (m_self->GetAttribute(AttrEntityShieldBoostAmount).get_float() > 0) {
        m_shieldBoosterDuration = m_self->GetAttribute(AttrEntityShieldBoostDuration).get_uint32();
        if (m_self->HasAttribute(AttrEntityShieldBoostDelayChance)) {
            m_shieldBoosterDelayChance = m_self->GetAttribute(AttrEntityShieldBoostDelayChance).get_float();
        } else if (m_self->HasAttribute(AttrEntityShieldBoostDelayChanceSmall)) {
            m_shieldBoosterDelayChance = m_self->GetAttribute(AttrEntityShieldBoostDelayChanceSmall).get_float();
        } else if (m_self->HasAttribute(AttrEntityShieldBoostDelayChanceMedium)) {
            m_shieldBoosterDelayChance = m_self->GetAttribute(AttrEntityShieldBoostDelayChanceMedium).get_float();
        } else if (m_self->HasAttribute(AttrEntityShieldBoostDelayChanceLarge)) {
            m_shieldBoosterDelayChance = m_self->GetAttribute(AttrEntityShieldBoostDelayChanceLarge).get_float();
        }
    }

    /* advanced ai (level one).  the AI_* flags count by value, and 1648 = 0 switches 1650-1652 off (bug B3, profile) */
    if (m_profile->l1SignatureRadius) {
        // "if it is 0 or below it uses the ship's signature radius instead"
        m_preferedSigRadius = m_profile->l1PreferredSig;
        if (m_preferedSigRadius <= 0)
            m_preferedSigRadius = m_profile->sigRadius;
    } else {
        m_preferedSigRadius = 0;
    }
    m_switchTargChance = (m_profile->IsLevelOne() ? (1.0f - m_profile->l1ChanceToNotSwitch) : 0.0f);
    // how often the advanced ai looks for a better target (phase 3: AI_TargetSwitchTimer as the switch cooldown)
    m_switchTime = sConfig.npc.TargetSwitchTime *1000;
    if (m_self->HasAttribute(AttrTargetSwitchTimer))
        m_switchTime = m_self->GetAttribute(AttrTargetSwitchTimer).get_uint32();
    if (m_switchTime < 1000)
        m_switchTime = 1000;

    // remote repair of friends (sleeper wardens, preservers, upholders, keepers).  defaults are the dgm defaults
    m_rrAmount = 0;
    if (m_self->type().HasEffect(EVEEffectID::NPCRemoteArmorRepair) or m_self->HasAttribute(AttrNpcRemoteArmorRepairAmount))
        m_rrAmount = m_self->GetAttribute(AttrNpcRemoteArmorRepairAmount).get_float();
    m_rrChance = (m_self->HasAttribute(AttrNpcRemoteArmorRepairChance) ? m_self->GetAttribute(AttrNpcRemoteArmorRepairChance).get_float() : 1.0f);
    m_rrDuration = (m_self->HasAttribute(AttrNpcRemoteArmorRepairDuration) ? m_self->GetAttribute(AttrNpcRemoteArmorRepairDuration).get_uint32() : 10000);
    m_rrThreshold = (m_self->HasAttribute(AttrNpcRemoteArmorRepairThreshold) ? m_self->GetAttribute(AttrNpcRemoteArmorRepairThreshold).get_float() : 0.25f);
    m_rrMaxTargets = (m_self->HasAttribute(AttrNpcRemoteArmorRepairMaxTargets) ? m_self->GetAttribute(AttrNpcRemoteArmorRepairMaxTargets).get_uint32() : 1);
    m_rsAmount = 0;
    if (m_self->type().HasEffect(EVEEffectID::NPCRemoteShieldBoost) or m_self->HasAttribute(AttrNpcRemoteShieldBoostAmount))
        m_rsAmount = (m_self->HasAttribute(AttrNpcRemoteShieldBoostAmount) ? m_self->GetAttribute(AttrNpcRemoteShieldBoostAmount).get_float() : 50.0f);
    m_rsChance = (m_self->HasAttribute(AttrNpcRemoteShieldBoostChance) ? m_self->GetAttribute(AttrNpcRemoteShieldBoostChance).get_float() : 1.0f);
    m_rsDuration = (m_self->HasAttribute(AttrNpcRemoteShieldBoostDuration) ? m_self->GetAttribute(AttrNpcRemoteShieldBoostDuration).get_uint32() : 20000);
    m_rsThreshold = (m_self->HasAttribute(AttrNpcRemoteShieldBoostThreshold) ? m_self->GetAttribute(AttrNpcRemoteShieldBoostThreshold).get_float() : 0.75f);
    m_rsMaxTargets = (m_self->HasAttribute(AttrNpcRemoteShieldBoostMaxTargets) ? m_self->GetAttribute(AttrNpcRemoteShieldBoostMaxTargets).get_uint32() : 1);
    if (m_rrDuration < 1000)
        m_rrDuration = 1000;
    if (m_rsDuration < 1000)
        m_rsDuration = 1000;
    if (m_rrMaxTargets < 1)
        m_rrMaxTargets = 1;
    if (m_rsMaxTargets < 1)
        m_rsMaxTargets = 1;

    // ewar (scram, web, jam, neut, damp, td, paint) is in NPCEwar.  it's made on the first Process(), after LawEnforcement set our role
    //  (so is the brain)

    /* target search on the think cadence (1s), staggered so a spawn doesn't all think on the same tick (bug B1:
     *  it used to wait for the turret cycle, and npcs without one never looked for anything) */
    m_sched.Start(NpcAct::Think, Now(), 1000 + (m_self->itemID() % 4) * 250, 1000);
}

NPCAIMgr::~NPCAIMgr()
{
    SafeDelete(m_ewar);
    SafeDelete(m_brain);
}

int64 NPCAIMgr::Now()
{
    return GetSteadyTime();
}

void NPCAIMgr::CreateBrain()
{
    if (m_brain != nullptr)
        return;
    m_brain = NpcBrain::Create(*m_profile, m_npc->IsLawEnforcer());
    _log(NPC__AI_TRACE, "%s(%u): %s brain (%s profile).", m_npc->GetName(), m_npc->GetID(), m_brain->GetName(),
            NpcAI::GetTierName(m_profile->tier));
}

NpcContext NPCAIMgr::MakeContext()
{
    if (m_brain == nullptr)
        CreateBrain();
    NpcContext ctx = { m_npc, this, *m_profile, m_rng, Now() };
    return ctx;
}

void NPCAIMgr::Process() {
    if (m_brain == nullptr)
        CreateBrain();
    // overseer structures, belongings and props:  nothing to do (bug B11)
    if (!m_brain->RunsAI())
        return;

    if (!m_ewarChecked) {
        // law enforcement doesn't use the ewar on its type (CONCORD has its own tackle in LawEnforcement)
        m_ewarChecked = true;
        if (sConfig.npc.UseEwar and !m_npc->IsLawEnforcer()) {
            m_ewar = new NPCEwar(m_npc);
            if (!m_ewar->HasAny())
                SafeDelete(m_ewar);
        }
    }

    if (m_destiny->IsWarping())
        return;

    int64 now(Now());
    if (m_sched.Due(NpcAct::WarpOut, now)) {
        // an idle spawn moves on after a while.  not while its dead members are waiting for a respawn (chained)
        if (IsIdle() and (m_npc->GetSpawnMgr() != nullptr) and (m_npc->SysBubble() != nullptr)
        and !m_npc->GetSpawnMgr()->IsChaining(m_npc->SysBubble()->GetID()))
            WarpOut();
    }

    // asleep where no player is (movement still runs in SystemEntity::Process)
    if (CheckDormant())
        return;

    NpcContext ctx(MakeContext());

    /* NPCAI::State definitions   -allan 25July15  (UD 1June16)
     *   Idle,       // not doing anything, nothing in sight....idle.  call Wander() to loosely orbit random object in bubble ~10-20k at 1/2 orbit speed
     *   Chasing,    // target within npc sight range.  attacking begins here.  use m_maxSpeed to get within falloff
     *   Following,  // between optimal and falloff.  try to get closer, but still orbiting and attacking
     *   Engaged,    // actively fighting (in orbit).  use m_orbitSpeed.
     *   Fleeing,    // running away....use m_maxSpeed then warp away when out of range	(does this make sense??)
     *   Signaling   // calling for help..use m_orbitSpeed *2 to speed tank while calling for reinforcements
     */
    switch(m_state) {
        case NPCAI::State::Idle: {
            if (m_npc->IsLawEnforcer()) {
                // CONCORD and faction police only go after the ship they were sent for (LawEnforcement moves them if it leaves)
                SystemEntity* pTarget(m_npc->SystemMgr()->GetSE(m_npc->GetLawTarget()));
                if ((pTarget == nullptr) or (pTarget->SysBubble() != m_npc->SysBubble()) or (pTarget->DestinyMgr() == nullptr))
                    break;
                if (pTarget->DestinyMgr()->IsCloaked() or pTarget->DestinyMgr()->IsWarping())
                    break;
                Target(pTarget);
                break;
            }
            // a jammed npc can't lock anything (EwarState already broke its locks)
            if ((m_npc->GetEwar() != nullptr) and m_npc->GetEwar()->IsJammed())
                break;
            if (IsReturning()) {
                m_pendingAggroID = 0;
                break;
            }
            // someone attacked us or our group
            if (m_pendingAggroID != 0) {
                SystemEntity* pAggressor(m_npc->SystemMgr()->GetSE(m_pendingAggroID));
                m_pendingAggroID = 0;
                if (CanAggro(pAggressor)) {
                    Target(pAggressor);
                    break;
                }
            }
            // look around on the think cadence
            if (!m_sched.Active(NpcAct::Think))
                m_sched.StartPeriodic(NpcAct::Think, now, 1000);
            if (m_sched.Due(NpcAct::Think, now)) {
                SystemEntity* pTarget(m_brain->Acquire(ctx));
                if (pTarget != nullptr) {
                    Target(pTarget);
                    return;
                }
                if (sConfig.npc.IdleWander)
                    if (!m_isWandering)
                        SetWander();
            }
            StartWarpOutTimer();
        } break;
        case NPCAI::State::Chasing:
        case NPCAI::State::Following:
        case NPCAI::State::Engaged: {
            if (m_npc->TargetMgr()->HasNoTargets()) {
                _log(NPC__AI_TRACE, "%s(%u): Stopped %s - HasNoTargets = true.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state));
                SetIdle(false);
                return;
            }
            SystemEntity* pSE = GetPrimary();
            if (pSE == nullptr) {
                _log(NPC__AI_TRACE, "%s(%u): Stopped %s - no primary target.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state));
                SetIdle();
                return;
            }
            if (pSE->SysBubble() == nullptr) {
                ClearTarget(pSE);
                return;
            }
            uint32 targetID(pSE->GetID());
            ProcessChase();
            CheckDistance(pSE);
            // the attack may have destroyed or dropped the target
            if (IsIdle())
                break;
            pSE = m_npc->SystemMgr()->GetSE(targetID);
            if ((pSE == nullptr) or pSE->IsDead())
                break;
            // ewar and missiles need a completed lock on the primary (bug B6a).  sleepers put their ewar on a secondary target
            SystemEntity* pLocked(m_npc->TargetMgr()->GetTarget(targetID, true));
            if (m_ewar != nullptr)
                m_ewar->Process(pLocked, m_brain->EwarTarget(ctx, pLocked));
            if (pLocked != nullptr)
                FireMissile(pLocked);
            m_brain->Think(ctx);
            CheckLeash();
        } break;
        case NPCAI::State::WarpOut:
        case NPCAI::State::WarpFollow:{
            // still aligning.  (in warp we returned above; WarpOutComplete() ends this state)
            if (m_destiny->IsAligningForWarp())
                break;
            // the warp was stopped (scrambled while aligning).  we stay here
            _log(NPC__AI_TRACE, "%s(%u): %s was cut short.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state));
            SetIdle();
            if ((m_npc->GetSpawnMgr() != nullptr) and (m_npc->SysBubble() != nullptr))
                m_npc->GetSpawnMgr()->MoveSpawn(m_npc, m_npc->SysBubble());
        } break;
        default: {
            // Fleeing and Signaling aren't used (Crucible rats neither fled nor called for help; group aggro covers the latter)
            SetIdle();
        } break;
    }

    ProcessRepairs();
    if (!m_npc->IsLawEnforcer() and ((m_rrAmount > 0) or (m_rsAmount > 0)))
        ProcessRemoteRepairs();
}

bool NPCAIMgr::CheckDormant()
{
    SystemBubble* pBubble(m_npc->SysBubble());
    if (m_dormant) {
        // a player arrived, or something is happening to us
        if (!sConfig.npc.SleepEmptyBubbles or (pBubble == nullptr) or pBubble->HasPlayers()) {
            Wake("player in bubble");
        } else if ((m_pendingAggroID != 0) or !m_npc->TargetMgr()->HasNoTargets() or m_npc->TargetMgr()->IsTargeted()) {
            Wake("aggression");
        }
        return m_dormant;
    }
    if (!sConfig.npc.SleepEmptyBubbles or !IsIdle() or m_npc->IsLawEnforcer() or !m_brain->CanSleep())
        return false;
    if ((pBubble == nullptr) or pBubble->HasPlayers())
        return false;
    if ((m_pendingAggroID != 0) or !m_npc->TargetMgr()->HasNoTargets() or m_npc->TargetMgr()->IsTargeted())
        return false;
    // finish going home and repairing first
    if (m_sched.Active(NpcAct::Leash) or m_sched.Active(NpcAct::ShieldRep) or m_sched.Active(NpcAct::ArmorRep))
        return false;
    m_dormant = true;
    _log(NPC__AI_TRACE, "%s(%u): Dormant:  no players in bubble %u.", m_npc->GetName(), m_npc->GetID(), pBubble->GetID());
    return true;
}

void NPCAIMgr::Wake(const char* reason)
{
    if (!m_dormant)
        return;
    m_dormant = false;
    // look around right away
    m_sched.Start(NpcAct::Think, Now(), 0, 1000);
    _log(NPC__AI_TRACE, "%s(%u): Awake (%s).", m_npc->GetName(), m_npc->GetID(), reason);
}

void NPCAIMgr::ProcessRepairs()
{
    // each cycle may be skipped on the type's delay chance
    int64 now(Now());
    if (m_sched.Due(NpcAct::ShieldRep, now))
        if (m_rng.Uniform() >= m_shieldBoosterDelayChance)
            m_npc->UseShieldRecharge();

    if (m_sched.Due(NpcAct::ArmorRep, now))
        if (m_rng.Uniform() >= m_armorRepairDelayChance)
            m_npc->UseArmorRepairer();
}

void NPCAIMgr::OnDamaged(SystemEntity* pSE, float amount)
{
    NpcContext ctx(MakeContext());
    if (!m_brain->RunsAI())
        return;

    // self repair starts on damage (not on being locked) and runs until we're whole again
    if (m_shieldBoosterDuration > 0)
        m_sched.Ensure(NpcAct::ShieldRep, ctx.now, m_shieldBoosterDuration, m_shieldBoosterDuration);
    if (m_armorRepairDuration > 0)
        m_sched.Ensure(NpcAct::ArmorRep, ctx.now, m_armorRepairDuration, m_armorRepairDuration);

    if ((pSE == nullptr) or (pSE == m_npc) or m_npc->IsLawEnforcer())
        return;

    Wake("damaged");
    AddThreat(pSE, amount, NpcAI::ThreatKind::Damage);
    // we're inside the attacker's ApplyDamage() call here, so the answer waits for our next Process()
    if (IsIdle() and (m_pendingAggroID == 0))
        QueueAggro(m_brain->OnAggressed(ctx, pSE));
    AlertGroup(pSE);

    // sleepers share what they know, so the group focuses together
    if (m_profile->IsLevelOne() and (m_npc->SysBubble() != nullptr)) {
        float share(amount * 0.5f);
        m_npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
            if ((cur == m_npc) or !cur->IsNPCSE())
                return;
            NPC* pNPC(cur->GetNPCSE());
            if (pNPC->IsLawEnforcer() or (pNPC->GetCorporationID() != m_npc->GetCorporationID()))
                return;
            pNPC->GetAIMgr()->ShareThreat(pSE, share);
        });
    }
}

void NPCAIMgr::ShareThreat(SystemEntity* pSE, float amount)
{
    if (!m_profile->IsLevelOne() or m_npc->IsLawEnforcer())
        return;
    AddThreat(pSE, amount, NpcAI::ThreatKind::Damage);
}

float NPCAIMgr::Score(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return 0.0f;
    float threat(GetThreat(pSE->GetID()));
    float score(1.0f + threat);
    // targets smaller than we like are worth less
    if (m_profile->l1SignatureRadius and (m_preferedSigRadius > 0)) {
        float sig(pSE->GetSignatureRadius());
        if (sig < m_preferedSigRadius)
            score *= (sig > 0 ? sig / m_preferedSigRadius : 0.1f);
    }
    // drones that hurt us get extra attention
    if (pSE->IsDroneSE() and (threat > 0))
        score *= 1.25f;
    return score;
}

SystemEntity* NPCAIMgr::ChooseAdvancedTarget(double range)
{
    // ships and drones in the bubble, best score.  ties go to the nearest
    if (m_npc->SysBubble() == nullptr)
        return nullptr;
    SystemEntity* pBest(nullptr);
    float bestScore(0.0f);
    double bestDistance(0.0);
    m_npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        if (!IsCandidate(cur, true))
            return;
        double distance(m_npc->GetPosition().distance(cur->GetPosition()));
        if (distance > range)
            return;
        float score(Score(cur));
        if ((pBest == nullptr) or (score > bestScore) or ((score == bestScore) and (distance < bestDistance))) {
            pBest = cur;
            bestScore = score;
            bestDistance = distance;
        }
    });
    return pBest;
}

void NPCAIMgr::ProcessAdvanced()
{
    int64 now(Now());
    // threat fades (10% every 10s)
    m_sched.Ensure(NpcAct::ThreatDecay, now, 10000, 10000);
    if (m_sched.Due(NpcAct::ThreatDecay, now))
        m_threat.Decay(0.9f, 1.0f);

    // a switch we decided on happens once the new lock completes (OnLockComplete() usually gets there first)
    if (m_switchToID != 0) {
        SystemEntity* pNew(m_npc->TargetMgr()->GetTarget(m_switchToID, true));
        if (pNew != nullptr) {
            _log(NPC__AI_TRACE, "%s(%u): Switching to %s(%u).", m_npc->GetName(), m_npc->GetID(), pNew->GetName(), pNew->GetID());
            SetPrimary(pNew);
            m_switchToID = 0;
        } else if (m_npc->TargetMgr()->GetTarget(m_switchToID, false) == nullptr) {
            m_switchToID = 0;   // the lock failed
        }
    }

    // look for a better target now and then
    m_sched.Ensure(NpcAct::SwitchEval, now, m_switchTime, m_switchTime);
    if (m_sched.Due(NpcAct::SwitchEval, now) and (m_switchToID == 0) and (m_rng.Uniform() < m_switchTargChance)) {
        // only switch to targets within ThreatRadius x our targeting range
        SystemEntity* pBest(ChooseAdvancedTarget(m_sightRange * sConfig.npc.ThreatRadius));
        SystemEntity* pPrimary(GetPrimary());
        if ((pBest != nullptr) and (pBest != pPrimary) and ((pPrimary == nullptr) or (Score(pBest) > (Score(pPrimary) * 1.1f)))) {
            if (m_npc->TargetMgr()->GetTarget(pBest->GetID(), false) != nullptr) {
                // already locked (or locking)
                m_switchToID = pBest->GetID();
            } else {
                // make room: drop our least interesting lock that isn't the primary
                if (m_npc->TargetMgr()->TargetCount() >= m_profile->maxLockedTargets) {
                    SystemEntity* pWorst(nullptr);
                    float worstScore(0.0f);
                    m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
                        if (cur == pPrimary)
                            return;
                        float score(Score(cur));
                        if ((pWorst == nullptr) or (score < worstScore)) {
                            pWorst = cur;
                            worstScore = score;
                        }
                    });
                    if (pWorst != nullptr)
                        m_npc->TargetMgr()->ClearTarget(pWorst);
                }
                bool chase = false;
                if (m_npc->TargetMgr()->StartTargeting(pBest, GetTargetTime(pBest), m_profile->maxLockedTargets, m_sightRange, chase))
                    m_switchToID = pBest->GetID();
            }
        }
    }

    // ewar goes on the most interesting ship we have locked that isn't our primary
    m_secondaryID = 0;
    if (m_profile->l1SecondaryTarget and (m_ewar != nullptr)) {
        float bestScore(0.0f);
        double range(m_ewar->GetMaxRange());
        m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
            if (!locked or (cur->GetID() == m_primaryID) or !cur->IsShipSE() or !cur->HasPilot() or cur->GetPilot()->InPod())
                return;
            if (m_npc->GetPosition().distance(cur->GetPosition()) > range)
                return;
            float score(Score(cur));
            if ((m_secondaryID == 0) or (score > bestScore)) {
                m_secondaryID = cur->GetID();
                bestScore = score;
            }
        });
    }
}

SystemEntity* NPCAIMgr::GetSecondary()
{
    if (m_secondaryID == 0)
        return nullptr;
    return m_npc->TargetMgr()->GetTarget(m_secondaryID, true);
}

void NPCAIMgr::ProcessRemoteRepairs()
{
    int64 now(Now());
    if (m_rrAmount > 0)
        m_sched.Ensure(NpcAct::RemoteArmor, now, m_rrDuration, m_rrDuration);
    if (m_rsAmount > 0)
        m_sched.Ensure(NpcAct::RemoteShield, now, m_rsDuration, m_rsDuration);

    if ((m_rrAmount > 0) and m_sched.Due(NpcAct::RemoteArmor, now) and (m_rng.Uniform() < m_rrChance))
        RemoteRepair(true);
    if ((m_rsAmount > 0) and m_sched.Due(NpcAct::RemoteShield, now) and (m_rng.Uniform() < m_rsChance))
        RemoteRepair(false);
}

void NPCAIMgr::RemoteRepair(bool armor)
{
    if ((m_npc->SysBubble() == nullptr) or (m_destiny == nullptr) or m_destiny->IsWarping())
        return;
    double range(m_assistRange > 0 ? m_assistRange : m_sightRange);
    float threshold(armor ? m_rrThreshold : m_rsThreshold);

    // friends (our corporation) in the bubble that need it: highest npcAssistancePriority, then the most damaged
    m_patients.clear();
    m_npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        if ((cur == m_npc) or !cur->IsNPCSE() or cur->IsDead())
            return;
        NPC* pNPC(cur->GetNPCSE());
        if (pNPC->IsLawEnforcer() or (pNPC->GetCorporationID() != m_npc->GetCorporationID()))
            return;
        if (m_npc->GetPosition().distance(pNPC->GetPosition()) > range)
            return;
        InventoryItemRef iRef(pNPC->GetSelf());
        float fraction(1.0f);
        if (armor) {
            float hp(iRef->GetAttribute(AttrArmorHP).get_float());
            if (hp <= 0)
                return;
            fraction = 1.0f - (iRef->GetAttribute(AttrArmorDamage).get_float() / hp);
        } else {
            float capacity(iRef->GetAttribute(AttrShieldCapacity).get_float());
            if (capacity <= 0)
                return;
            fraction = iRef->GetAttribute(AttrShieldCharge).get_float() / capacity;
        }
        // "0.1 means: must be below 90% armor to get repairs"
        if (fraction >= (1.0f - threshold))
            return;
        Patient p = Patient();
            p.pNPC = pNPC;
            p.priority = iRef->GetAttribute(AttrNpcAssistanceProirity).get_float();
            p.fraction = fraction;
        m_patients.push_back(p);
    });
    if (m_patients.empty())
        return;
    std::sort(m_patients.begin(), m_patients.end(), [](const Patient& a, const Patient& b) {
        if (a.priority != b.priority)
            return (a.priority > b.priority);
        return (a.fraction < b.fraction);
    });

    uint8 count(0);
    uint8 maxTargets(armor ? m_rrMaxTargets : m_rsMaxTargets);
    for (auto& cur : m_patients) {
        if (count >= maxTargets)
            break;
        ++count;
        InventoryItemRef iRef(cur.pNPC->GetSelf());
        if (armor) {
            float damage(iRef->GetAttribute(AttrArmorDamage).get_float() - m_rrAmount);
            iRef->SetAttribute(AttrArmorDamage, (damage < 0 ? 0 : damage));
        } else {
            float charge(iRef->GetAttribute(AttrShieldCharge).get_float() + m_rsAmount);
            float capacity(iRef->GetAttribute(AttrShieldCapacity).get_float());
            iRef->SetAttribute(AttrShieldCharge, (charge > capacity ? capacity : charge));
        }
        m_destiny->SendSpecialEffect(m_npc->GetID(), m_npc->GetID(), m_self->typeID(), cur.pNPC->GetID(), 0,
                                     (armor ? "effects.RemoteArmourRepair" : "effects.ShieldTransfer"), false, true, true,
                                     (armor ? m_rrDuration : m_rsDuration), 1);
        cur.pNPC->SendDamageStateChanged();
        _log(NPC__AI_TRACE, "%s(%u): Remote %s repair on %s(%u) (%.0f%%).", m_npc->GetName(), m_npc->GetID(),
                (armor ? "armor" : "shield"), cur.pNPC->GetName(), cur.pNPC->GetID(), cur.fraction * 100);
    }
    m_patients.clear();
}

bool NPCAIMgr::IsFighting() {
    // more to this here....
    return (m_state != NPCAI::State::Idle);
}

void NPCAIMgr::StartWarpOutTimer()
{
    // anomaly, incursion and mission rats stay.  so does anything not from a spawn manager
    if ((sConfig.npc.WarpOut == 0) or (m_npc->GetSpawnMgr() == nullptr) or m_npc->IsLawEnforcer())
        return;
    SystemBubble* pBubble(m_npc->SysBubble());
    if ((pBubble == nullptr) or pBubble->IsAnomaly() or pBubble->IsIncursion() or pBubble->IsMission())
        return;
    m_sched.Ensure(NpcAct::WarpOut, Now(), sConfig.npc.WarpOut *1000); // s to ms
}

void NPCAIMgr::WarpOut()
{
    m_sched.Stop(NpcAct::WarpOut);
    if ((m_npc->GetSpawnMgr() == nullptr) or m_npc->IsLawEnforcer() or (m_npc->SysBubble() == nullptr))
        return;
    if ((m_state == NPCAI::State::WarpOut) or (m_state == NPCAI::State::WarpFollow))
        return;
    // a player has us pointed.  try again later
    if (m_npc->IsWarpScrambled()) {
        m_sched.Start(NpcAct::WarpOut, Now(), 30000);
        return;
    }

    SystemManager* pSys = m_npc->SystemMgr();

    /** @todo  eventually, this will check with anomaly mgr for possible npc hideouts in system
     * based on npc faction, system, players in system, players in bubble, and *more later*
     * to determine a warpto target for this npc, or this group
     *
     * for now, if there are players in system, the whole spawn group warps to another belt that has no spawn.
     * if there are no players in this system, avoid using proc tics on npcs
     */
    if (pSys->PlayerCount() < 1) {
        StartWarpOutTimer();
        return;
    }

    SystemBubble* pBubble(m_npc->SysBubble());
    uint32 curBeltID(sBubbleMgr.GetBeltID(pBubble->GetID()));
    for (uint8 i = 0; i < 3; ++i) {
        uint32 beltID(pSys->GetRandBeltID());
        if ((beltID == 0) or (beltID == curBeltID))
            continue;
        SystemEntity* pBelt(pSys->GetSE(beltID));
        if (pBelt == nullptr)
            continue;
        SystemBubble* pDest(sBubbleMgr.FindBubble(pBelt));
        if (pDest == nullptr)
            pDest = sBubbleMgr.GetBubble(pSys, pBelt->GetPosition());
        if ((pDest == nullptr) or (pDest == pBubble))
            continue;
        // another group lives there
        if (m_npc->GetSpawnMgr()->HasSpawn(pDest->GetID()))
            continue;
        _log(NPC__AI_TRACE, "%s(%u): Warping my group out to %s(%u).", m_npc->GetName(), m_npc->GetID(), pBelt->GetName(), beltID);
        if (m_npc->GetSpawnMgr()->WarpOutSpawn(m_npc, pDest, pBelt->GetPosition()))
            return;
    }
    // nowhere to go.  try again later
    StartWarpOutTimer();
}

void NPCAIMgr::BeginWarp(int8 state/*NPCAI::State::WarpOut*/)
{
    // leaving: drop everything
    if (m_ewar != nullptr)
        m_ewar->StopAll();
    m_npc->TargetMgr()->ClearTargets();
    m_sched.Stop(NpcAct::Missile);
    m_sched.Stop(NpcAct::Turret);
    m_sched.Stop(NpcAct::ChaseDelay);
    m_sched.Stop(NpcAct::ChaseBoost);
    m_sched.Stop(NpcAct::DroneRoll);
    m_sched.Stop(NpcAct::WarpOut);
    m_sched.Stop(NpcAct::SwitchEval);
    m_sched.Stop(NpcAct::ThreatDecay);
    m_primaryID = 0;
    m_secondaryID = 0;
    m_switchToID = 0;
    m_moveTargetID = 0;
    m_pendingAggroID = 0;
    m_threat.Clear();
    m_isWandering = false;
    m_dormant = false;
    m_destiny->SetMaxVelocity(m_profile->maxVelocity);
    m_state = state;
}

void NPCAIMgr::WarpOutComplete()
{
    m_sched.Stop(NpcAct::WarpOut);
    /* wherever an npc lands is home, the first warp-in of a belt spawn too:  those are created 1000+ km off grid,
     *  and a leashed npc (target switching, e.g. officers) would otherwise give up every fight to fly back there */
    m_home = m_npc->GetPosition();
    if ((m_state != NPCAI::State::WarpOut) and (m_state != NPCAI::State::WarpFollow))
        return;
    _log(NPC__AI_TRACE, "%s(%u): %s complete.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state));
    m_state = NPCAI::State::Idle;
    m_destiny->SetMaxVelocity(CruiseSpeed());
    // make sure our spawn manager has us in this bubble
    if ((m_npc->GetSpawnMgr() != nullptr) and (m_npc->SysBubble() != nullptr))
        m_npc->GetSpawnMgr()->MoveSpawn(m_npc, m_npc->SysBubble());
    // look around right away
    m_sched.Start(NpcAct::Think, Now(), 0, 1000);
}

bool NPCAIMgr::TryWarpFollow(SystemEntity* pSE)
{
    // Crucible rats never followed a ship through warp.  <npc><WarpFollowChance> > 0 lets belt and gate rats do it
    if ((sConfig.npc.WarpFollowChance <= 0.0f) or m_npc->IsLawEnforcer() or (m_npc->GetSpawnMgr() == nullptr))
        return false;
    if ((pSE == nullptr) or !pSE->IsShipSE() or !pSE->HasPilot() or (pSE->DestinyMgr() == nullptr))
        return false;
    if (!pSE->DestinyMgr()->IsWarping() and !pSE->DestinyMgr()->IsAligningForWarp())
        return false;
    SystemBubble* pBubble(m_npc->SysBubble());
    if ((pBubble == nullptr) or !(pBubble->IsBelt() or pBubble->IsGate()))
        return false;
    if (pBubble->IsAnomaly() or pBubble->IsIncursion() or pBubble->IsMission())
        return false;
    if (m_npc->IsWarpScrambled())
        return false;
    if (m_rng.Uniform() >= sConfig.npc.WarpFollowChance)
        return false;

    GPoint destination(pSE->DestinyMgr()->GetTargetPoint());
    if (destination.isNaN() or (destination.distance(m_npc->GetPosition()) < 150000))
        return false;
    _log(NPC__AI_TRACE, "%s(%u): Following %s(%u) through warp.", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    BeginWarp(NPCAI::State::WarpFollow);
    m_destiny->WarpTo(destination, static_cast<int32>(m_rng.Range(2, 16)) *1000);
    return true;
}

void NPCAIMgr::CheckLeash()
{
    // sleepers and deadspace/mission/incursion npcs don't let themselves be pulled away from home.  belt rats and law do
    if ((sConfig.npc.LeashRange == 0) or m_npc->IsLawEnforcer())
        return;
    SystemBubble* pBubble(m_npc->SysBubble());
    if (pBubble == nullptr)
        return;
    if (!m_profile->IsLevelOne() and !(pBubble->IsAnomaly() or pBubble->IsMission() or pBubble->IsIncursion()))
        return;
    if (m_npc->GetPosition().distance(m_home) <= sConfig.npc.LeashRange)
        return;

    _log(NPC__AI_TRACE, "%s(%u): Pulled %.0fm from home.  Giving up and going back.", \
            m_npc->GetName(), m_npc->GetID(), m_npc->GetPosition().distance(m_home));
    m_npc->TargetMgr()->ClearTargets();
    m_pendingAggroID = 0;
    SetIdle();
    m_destiny->SetMaxVelocity(m_profile->maxVelocity);
    m_destiny->GotoPoint(m_home);
    m_sched.Start(NpcAct::Leash, Now(), 30000);
}

bool NPCAIMgr::IsReturning()
{
    if (!m_sched.Active(NpcAct::Leash))
        return false;
    if (!m_sched.IsDue(NpcAct::Leash, Now()) and (m_npc->GetPosition().distance(m_home) > 5000))
        return true;
    // home (or long enough).  back to normal
    m_sched.Stop(NpcAct::Leash);
    m_destiny->Stop();
    m_destiny->SetMaxVelocity(CruiseSpeed());
    return false;
}

void NPCAIMgr::SetWander()
{
    if (m_npc->GetSpawnMgr() == nullptr)
        return;
    SystemBubble* pBubble = m_npc->SysBubble();
    if (pBubble == nullptr)
        return;
    if (!m_isWandering) {
        _log(NPC__AI_TRACE, "%s(%u): Wandering:  No Targets within my sight range of %um", \
                m_npc->GetName(), m_npc->GetID(), m_sightRange);
        m_isWandering = true;
    }

    // wandering.  nothing to shoot.  look for target.
    if (pBubble->IsAnomaly() or pBubble->IsIncursion() or pBubble->IsMission()) {
        return;
    } else if (pBubble->HasDynamics() and pBubble->IsBelt()) {
        // pick random entity and loosely orbit it.  if no entity found, orbit center of belt
        SystemEntity* pSE = pBubble->GetRandomEntity();
        if (pSE == nullptr)
            pSE = m_npc->SystemMgr()->GetSE(sBubbleMgr.GetBeltID(pBubble->GetID()));
        if (pSE == nullptr) {
            _log(NPC__ERROR, "%s(%u): Wandering:  No Target or beltSE found.", m_npc->GetName(), m_npc->GetID());
            // nothing here...leave bubble
            WarpOut();
            return;
        }
        m_destiny->SetMaxVelocity(CruiseSpeed());
        uint16 orbitDistance = static_cast<uint16>(m_rng.Range(10000, 20001));
        m_destiny->Orbit(pSE, orbitDistance);
        _log(NPC__AI_TRACE, "%s(%u):  Just for shits-n-giggles, I\'m gonna orbit %s(%u) at %um.", \
                m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), orbitDistance);
    } else {
        /** @todo  figure out a way for npc to wander 'aimlessly' around their bubble */
        m_destiny->Stop();
    }
}

void NPCAIMgr::SetIdle(bool clearLocks/*true*/) {
    if (m_state == NPCAI::State::Idle)
        return;
    // not doing anything....idle.

    _log(NPC__AI_TRACE, "%s(%u): Idle: returning to idle.", m_npc->GetName(), m_npc->GetID());
    m_state = NPCAI::State::Idle;
    m_destiny->Stop();
    m_destiny->SetMaxVelocity(CruiseSpeed());

    // locks we still hold (or are getting) on things we no longer fight go too (bug B16)
    if (clearLocks and !m_npc->TargetMgr()->HasNoTargets())
        m_npc->TargetMgr()->ClearTargets();

    // repair timers keep running until we're whole (they stop themselves)
    m_sched.Stop(NpcAct::Missile);
    m_sched.Stop(NpcAct::Turret);
    m_sched.Stop(NpcAct::ChaseDelay);
    m_sched.Stop(NpcAct::ChaseBoost);
    m_sched.Stop(NpcAct::DroneRoll);
    if (m_ewar != nullptr)
        m_ewar->StopAll();
    m_primaryID = 0;
    m_secondaryID = 0;
    m_switchToID = 0;
    m_moveTargetID = 0;
    m_sched.Stop(NpcAct::SwitchEval);
    m_sched.Stop(NpcAct::ThreatDecay);
    m_threat.Clear();

    //disallow warpout if anomaly, incursion or mission rat (and by config)
    StartWarpOutTimer();
}

void NPCAIMgr::SetChasing(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    if ((m_state == NPCAI::State::Chasing) and (m_destiny->IsGoto() or m_destiny->IsFollowing())) {
        // the target moves.  head for where it is now once it has gone a fair way from where we were going
        if (m_lastGoto.distance(pSE->GetPosition()) > 2000) {
            m_lastGoto = pSE->GetPosition();
            m_destiny->GotoPoint(m_lastGoto);
        }
        return;
    }
    _log(NPC__AI_TRACE, "%s(%u): Begin chasing.  Target is %s(%u).", \
         m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    // target out of range to attack/follow, but within npc sight range....use mwd/ab if equiped
    bool wasChasing(m_state == NPCAI::State::Chasing);
    m_lastGoto = pSE->GetPosition();
    m_destiny->GotoPoint(m_lastGoto);  //head towards target
    m_state = NPCAI::State::Chasing;
    m_sched.Stop(NpcAct::WarpOut);
    if (wasChasing) {
        m_destiny->SetMaxVelocity(m_chaseFast ? m_profile->maxVelocity : CruiseSpeed());
    } else {
        StartChaseDelay();
    }
}

void NPCAIMgr::StartChaseDelay()
{
    // may stall at cruise speed before chase speed kicks in
    m_sched.Stop(NpcAct::ChaseBoost);
    if ((m_chaseMaxDelay >= 1000) and (m_rng.Uniform() < m_profile->chaseMaxDelayChance)) {
        m_chaseFast = false;
        m_destiny->SetMaxVelocity(CruiseSpeed());
        m_sched.Start(NpcAct::ChaseDelay, Now(), static_cast<int64>(m_rng.Range(1000, m_chaseMaxDelay)));
        return;
    }
    StartChaseBoost();
}

void NPCAIMgr::StartChaseBoost()
{
    m_sched.Stop(NpcAct::ChaseDelay);
    m_chaseFast = true;
    m_destiny->SetMaxVelocity(m_profile->maxVelocity);
    if (m_chaseMaxDuration == 0)
        return;
    if (m_rng.Uniform() < m_profile->chaseMaxDurationChance) {
        m_sched.Start(NpcAct::ChaseBoost, Now(), m_chaseMaxDuration);
    } else {
        m_sched.Start(NpcAct::ChaseBoost, Now(), static_cast<int64>(m_rng.Range(m_chaseMaxDuration /4, m_chaseMaxDuration)));
    }
}

void NPCAIMgr::ProcessChase()
{
    if (m_state != NPCAI::State::Chasing)
        return;
    int64 now(Now());
    if (m_sched.Due(NpcAct::ChaseDelay, now)) {
        StartChaseBoost();
    } else if (m_sched.Due(NpcAct::ChaseBoost, now)) {
        // chase speed ran out.  stall again (or keep going)
        StartChaseDelay();
    }
}

float NPCAIMgr::CruiseSpeed()
{
    // entityCruiseSpeed, or maxVelocity when the type has none (the profile resolved that)
    return m_profile->cruiseSpeed;
}

void NPCAIMgr::SetFollowing(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    if ((m_state == NPCAI::State::Following) and (m_destiny->IsGoto() or m_destiny->IsFollowing()) and (m_moveTargetID == pSE->GetID()))
        return;
    _log(NPC__AI_TRACE, "%s(%u): Begin following.  Target is %s(%u).", \
         m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    // too close to chase, but to far to engage
    m_sched.Stop(NpcAct::ChaseDelay);
    m_sched.Stop(NpcAct::ChaseBoost);
    m_moveTargetID = pSE->GetID();
    m_destiny->SetMaxVelocity(CruiseSpeed() *2);
    m_destiny->Follow(pSE, static_cast<uint32>(EvE::max(m_profile->falloff)));  //try to get inside falloff range
    m_state = NPCAI::State::Following;
    m_sched.Stop(NpcAct::WarpOut);
}

void NPCAIMgr::SetEngaged(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    if ((m_state == NPCAI::State::Engaged) and m_destiny->IsOrbiting() and (m_moveTargetID == pSE->GetID()))
        return;
    _log(NPC__AI_TRACE, "%s(%u): Begin engaging.  Target is %s(%u).", \
         m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    // actively fighting.  evasive npcs orbit fast
    m_sched.Stop(NpcAct::ChaseDelay);
    m_sched.Stop(NpcAct::ChaseBoost);
    m_moveTargetID = pSE->GetID();
    m_destiny->SetMaxVelocity((m_profile->evasive > 0) ? m_profile->maxVelocity : CruiseSpeed());
    m_destiny->Orbit(pSE, static_cast<uint32>(EvE::max(m_profile->optimal)));  //try to get inside orbit range
    m_state = NPCAI::State::Engaged;
    m_sched.Stop(NpcAct::WarpOut);
}

void NPCAIMgr::CheckDistance(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return;
    double dist = m_npc->GetPosition().distance(pSE->GetPosition());
    if ((dist > m_sightRange) and (!m_npc->TargetMgr()->IsTargetedBy(pSE))) {
        _log(NPC__AI_TRACE, "%s(%u): CheckDistance: %s(%u) is too far away (%.0fm).  Return to Idle.", \
             m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), dist);
        if (m_state != NPCAI::State::Idle) {
            // target is no longer in npc's "sight range" and is NOT targeting this npc.  unlock target and return to idle.
            //   should we do anything else here?  search for another target?  wander around?  yes..later
            // if npc is targeted greater than this distance, it will chase
            ClearTarget(pSE);
        }
        return;
    }

    m_isWandering = false;

    if (dist < m_flyRange) {
        SetEngaged(pSE);
    } else if (dist < m_boostRange) {
        SetFollowing(pSE);
    } else {
        SetChasing(pSE);
    }

    _log(NPC__AI_TRACE, "%s(%u): CheckDistance:  target: %s(%u), state: %s, dist: %.0fm, flyRange: %u, boostRange: %u.", \
            m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), GetStateName(m_state), dist, m_flyRange, m_boostRange);

    Attack(pSE);
}

void NPCAIMgr::Target(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    bool chase = false;

    if (!m_npc->TargetMgr()->StartTargeting(pSE, GetTargetTime(pSE), m_profile->maxLockedTargets, m_sightRange, chase)) {
        if (chase) {
            _log(NPC__AI_TRACE, "%s(%u): Targeting of %s(%u) failed.  Begin Chasing.", \
                        m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
            SetChasing(pSE);
        } else {
            _log(NPC__AI_TRACE, "%s(%u): Targeting of %s(%u) failed.  Clear Target and Return to Idle.", \
                        m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
            SetIdle();
        }
        return;
    }
    // a new fight, or our primary is gone
    if ((m_primaryID == 0) or (GetPrimary() == nullptr))
        SetPrimary(pSE);
    CheckDistance(pSE);

    // the turret cycle runs from the start of the lock.  shots wait for the lock and the range (Attack())
    if (m_profile->hasTurret)
        m_sched.Ensure(NpcAct::Turret, Now(), m_profile->turretCycleMs, m_profile->turretCycleMs);
    if (m_profile->hasMissile)
        m_sched.Ensure(NpcAct::Missile, Now(), m_profile->missileCycleMs, m_profile->missileCycleMs);
}

void NPCAIMgr::Targeted(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    NpcContext ctx(MakeContext());
    // overseer structures and props never lock back (bug B11)
    if (!m_brain->RunsAI())
        return;
    if (m_npc->IsLawEnforcer() and (pSE->GetID() != m_npc->GetLawTarget()))
        return;
    Wake("targeted");

    _log(NPC__AI_TRACE, "%s(%u): Targeted by %s(%u) while %s.", \
            m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), GetStateName(m_state));

    switch(m_state) {
        case NPCAI::State::Idle: {
            if ((m_npc->GetEwar() != nullptr) and m_npc->GetEwar()->IsJammed())
                break;
            if (IsReturning())
                break;
            _log(NPC__AI_TRACE, "%s(%u): Begin Approaching and start Targeting sequence.", \
                    m_npc->GetName(), m_npc->GetID());
            SystemEntity* pTarget(m_brain->OnAggressed(ctx, pSE));
            if (CanAggro(pTarget))
                Target(pTarget);
            AlertGroup(pSE);
        } break;
        case NPCAI::State::Chasing:
        case NPCAI::State::Following:
        case NPCAI::State::Engaged: {
            if (m_npc->IsLawEnforcer())
                break;
            // regular rats keep their primary.  lock the newcomer too, so we have someone to turn to (never a pod, bug B10)
            if (!pSE->IsShipSE() or !CanAggro(pSE))
                break;
            bool chase = false;
            m_npc->TargetMgr()->StartTargeting(pSE, GetTargetTime(pSE), m_profile->maxLockedTargets, m_sightRange, chase);
        } break;
    }
}

void NPCAIMgr::TargetLost(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    switch(m_state) {
        case NPCAI::State::Chasing:
        case NPCAI::State::Following:
        case NPCAI::State::Engaged: {
            if (m_npc->TargetMgr()->HasNoTargets()) {
                // our last target warped off.  maybe we follow
                if ((pSE->GetID() == m_primaryID) and TryWarpFollow(pSE))
                    return;
                _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) lost. No targets remain.  Return to Idle.", \
                        m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
                SetIdle(false);
            } else if (pSE->GetID() == m_primaryID) {
                // turn to whoever hurt us most of what we have locked
                NpcContext ctx(MakeContext());
                SetPrimary(m_brain->RepickPrimary(ctx, pSE));
                _log(NPC__AI_TRACE, "%s(%u): Primary %s(%u) lost.  New primary is %u.", \
                        m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), m_primaryID);
            }
        }
    }
}

void NPCAIMgr::OnLockComplete(SystemEntity* pSE)
{
    // called from TargetManager::Process() while it walks our targets:  don't add or remove targets here
    if (pSE == nullptr)
        return;
    _log(NPC__AI_TRACE, "%s(%u): Lock on %s(%u) complete.", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    if (m_primaryID == 0) {
        SetPrimary(pSE);
    } else if (pSE->GetID() == m_switchToID) {
        // the switch the advanced ai decided on
        _log(NPC__AI_TRACE, "%s(%u): Switching to %s(%u).", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
        SetPrimary(pSE);
        m_switchToID = 0;
    }
}

void NPCAIMgr::AddThreat(SystemEntity* pSE, float amount, uint8 kind)
{
    if ((pSE == nullptr) or (amount <= 0.0f))
        return;
    m_threat.Add(pSE->GetID(), amount);
    // a drone's pilot is behind it
    if (pSE->IsDroneSE()) {
        SystemEntity* pController(pSE->GetDroneSE()->GetController());
        if (pController != nullptr)
            m_threat.Add(pController->GetID(), amount * 0.5f);
    }
    if (m_brain != nullptr) {
        NpcContext ctx(MakeContext());
        m_brain->OnThreat(ctx, pSE, amount, kind);
    }
}

void NPCAIMgr::OnEwarBy(SystemEntity* pSE)
{
    if ((pSE == nullptr) or m_npc->IsLawEnforcer())
        return;
    NpcContext ctx(MakeContext());
    if (!m_brain->RunsAI())
        return;
    Wake("ewar");
    AddThreat(pSE, 50.0f, NpcAI::ThreatKind::Ewar);
    if (IsIdle() and (m_pendingAggroID == 0))
        QueueAggro(m_brain->OnAggressed(ctx, pSE));
    AlertGroup(pSE);
}

void NPCAIMgr::Aggro(SystemEntity* pSE)
{
    if ((pSE == nullptr) or m_npc->IsLawEnforcer())
        return;
    NpcContext ctx(MakeContext());
    if (!m_brain->RunsAI())
        return;
    Wake("group alert");
    if (IsIdle() and (m_pendingAggroID == 0))
        m_brain->OnGroupAlert(ctx, pSE);
}

void NPCAIMgr::QueueAggro(SystemEntity* pSE)
{
    if ((pSE != nullptr) and (m_pendingAggroID == 0))
        m_pendingAggroID = pSE->GetID();
}

bool NPCAIMgr::CanAggro(SystemEntity* pSE)
{
    if ((pSE == nullptr) or (pSE == m_npc) or pSE->IsDead())
        return false;
    if (!pSE->IsShipSE() and !pSE->IsDroneSE())
        return false;
    if ((pSE->SysBubble() == nullptr) or (pSE->SysBubble() != m_npc->SysBubble()))
        return false;
    if ((pSE->DestinyMgr() == nullptr) or pSE->DestinyMgr()->IsCloaked() or pSE->DestinyMgr()->IsWarping())
        return false;
    if (pSE->HasPilot()) {
        if (pSE->GetPilot()->IsInvul())
            return false;
        // NPCs do not attack pods (EVElopedia).  npc.TargetPod is a GM/test override (bug B10)
        if (pSE->GetPilot()->InPod()) {
            if (!sConfig.npc.TargetPod)
                return false;
            if (m_npc->SystemMgr()->GetSystemSecurityRating() > sConfig.npc.TargetPodSec)
                return false;
        }
    }
    return true;
}

bool NPCAIMgr::IsCandidate(SystemEntity* pSE, bool drones)
{
    if (!CanAggro(pSE))
        return false;
    if (pSE->IsDroneSE()) {
        if (!drones)
            return false;
    } else if (!pSE->HasPilot()) {
        // players' ships only
        return false;
    }
    return (m_npc->GetPosition().distance(pSE->GetPosition()) <= m_sightRange);
}

SystemEntity* NPCAIMgr::ChooseTarget()
{
    // advanced ai (sleepers) weighs threat and signature, and drones count
    if (m_profile->IsLevelOne())
        return ChooseAdvancedTarget(m_sightRange);

    if (m_npc->SysBubble() == nullptr)
        return nullptr;
    // regular rats go for the nearest ship they see
    SystemEntity* pTarget(nullptr);
    double nearest(0.0);
    m_npc->SysBubble()->ForEachPlayer([&](Client* cur) {
        SystemEntity* pSE(cur->GetShipSE());
        if (!IsCandidate(pSE, false))
            return;
        double distance(m_npc->GetPosition().distance(pSE->GetPosition()));
        if ((pTarget == nullptr) or (distance < nearest)) {
            pTarget = pSE;
            nearest = distance;
        }
    });
    return pTarget;
}

SystemEntity* NPCAIMgr::AggroTarget(SystemEntity* pSE)
{
    if ((pSE == nullptr) or !pSE->IsDroneSE())
        return pSE;
    // shoot back at the drone's pilot when we can see him
    SystemEntity* pController(pSE->GetDroneSE()->GetController());
    if (CanAggro(pController) and (m_npc->GetPosition().distance(pController->GetPosition()) <= m_sightRange))
        return pController;
    return pSE;
}

SystemEntity* NPCAIMgr::GetPrimary()
{
    if (m_primaryID != 0) {
        SystemEntity* pSE(m_npc->TargetMgr()->GetTarget(m_primaryID, false));
        if (pSE != nullptr)
            return pSE;
    }
    // primary is gone
    NpcContext ctx(MakeContext());
    SystemEntity* pSE(m_brain->RepickPrimary(ctx, nullptr));
    SetPrimary(pSE);
    return pSE;
}

SystemEntity* NPCAIMgr::PickPrimary(SystemEntity* pExclude)
{
    SystemEntity* pBest(nullptr);
    float bestThreat(0.0f);
    double bestDistance(0.0);
    bool advanced(m_profile->IsLevelOne());
    m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        // never a pod, a cloaked, warping or invulnerable ship, or one that left our bubble (bug B10)
        if ((cur == pExclude) or !CanAggro(cur))
            return;
        float threat(advanced ? Score(cur) : GetThreat(cur->GetID()));
        double distance(m_npc->GetPosition().distance(cur->GetPosition()));
        if ((pBest == nullptr) or (threat > bestThreat) or ((threat == bestThreat) and (distance < bestDistance))) {
            pBest = cur;
            bestThreat = threat;
            bestDistance = distance;
        }
    });
    return pBest;
}

void NPCAIMgr::SetPrimary(SystemEntity* pSE)
{
    uint32 newID(pSE == nullptr ? 0 : pSE->GetID());
    if (newID == m_primaryID)
        return;
    m_primaryID = newID;
    // a new target means a new chase (SetChasing() starts over on the next CheckDistance())
    m_sched.Stop(NpcAct::ChaseDelay);
    m_sched.Stop(NpcAct::ChaseBoost);
    if (m_state == NPCAI::State::Chasing)
        m_state = NPCAI::State::Following;
}

void NPCAIMgr::AlertGroup(SystemEntity* pAggressor)
{
    if (!sConfig.npc.GroupAggro or (pAggressor == nullptr) or m_npc->IsLawEnforcer() or (m_npc->SysBubble() == nullptr))
        return;
    double range(m_assistRange > m_sightRange ? m_assistRange : m_sightRange);
    m_npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        if ((cur == m_npc) or !cur->IsNPCSE())
            return;
        NPC* pNPC(cur->GetNPCSE());
        if (pNPC->IsLawEnforcer() or (pNPC->GetSpawnMgr() != m_npc->GetSpawnMgr()))
            return;
        if ((m_npc->GetSpawnGroupID() != 0) and (pNPC->GetSpawnGroupID() != m_npc->GetSpawnGroupID()))
            return;
        if (!pNPC->GetAIMgr()->IsIdle())
            return;
        if (m_npc->GetPosition().distance(pNPC->GetPosition()) > range)
            return;
        pNPC->GetAIMgr()->Aggro(pAggressor);
    });
}

void NPCAIMgr::ProcessDroneAggro()
{
    // Crucible rats now and then turned on drones that hurt them.  sleepers use threat instead
    if (m_profile->IsLevelOne() or (sConfig.npc.DroneAggroChance <= 0.0f) or m_npc->IsLawEnforcer())
        return;
    int64 now(Now());
    if (!m_sched.Active(NpcAct::DroneRoll)) {
        m_sched.StartPeriodic(NpcAct::DroneRoll, now, 30000);
        return;
    }
    if (!m_sched.Due(NpcAct::DroneRoll, now))
        return;
    SystemEntity* pPrimary(GetPrimary());
    if ((pPrimary != nullptr) and pPrimary->IsDroneSE())
        return;
    for (size_t i = 0; i < m_threat.Size(); ++i) {
        SystemEntity* pSE(m_npc->SystemMgr()->GetSE(m_threat.At(i).id));
        if ((pSE == nullptr) or !pSE->IsDroneSE() or !CanAggro(pSE))
            continue;
        if (m_rng.Uniform() >= sConfig.npc.DroneAggroChance)
            return;
        bool chase = false;
        if (m_npc->TargetMgr()->StartTargeting(pSE, GetTargetTime(pSE), m_profile->maxLockedTargets, m_sightRange, chase)) {
            _log(NPC__AI_TRACE, "%s(%u): Turning on drone %s(%u).", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
            SetPrimary(pSE);
        }
        return;
    }
}

double NPCAIMgr::SurfaceDistance(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return 0.0;
    return EvE::max(m_npc->GetPosition().distance(pSE->GetPosition()) - m_npc->GetRadius() - pSE->GetRadius());
}

void NPCAIMgr::Attack(SystemEntity* pSE)
{
    if ((pSE == nullptr) or !m_profile->hasTurret)
        return;
    int64 now(Now());
    // the cycle runs from the start of the fight (Target()), or from now if we got here another way
    m_sched.Ensure(NpcAct::Turret, now, m_profile->turretCycleMs, m_profile->turretCycleMs);
    if (!m_sched.IsDue(NpcAct::Turret, now))
        return;
    if (m_npc->SysBubble() == nullptr)
        return;
    // Check to see if the target still in the bubble (Client warped out)
    if (!m_npc->SysBubble()->InBubble(pSE->GetPosition())) {
        _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) no longer in bubble.  Clear target and move on",
                m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
        m_sched.Stop(NpcAct::Missile);
        ClearTarget(pSE);
        return;
    }
    if (pSE->DestinyMgr() == nullptr) {
        _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) has no destiny manager.  Clear target and move on",
                m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
        m_sched.Stop(NpcAct::Missile);
        ClearTarget(pSE);
        return;
    }
    // Check to see if the target is not cloaked:
    if (pSE->DestinyMgr()->IsCloaked()) {
        _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) is cloaked.  Clear target and move on",
                m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
        m_sched.Stop(NpcAct::Missile);
        ClearTarget(pSE);
        return;
    }
    // only at a completed lock on this target (bug B6b:  it used to be any lock), and only inside entityAttackRange (B7).
    //  otherwise the shot waits (the cycle doesn't burst when it's allowed again)
    if ((m_npc->TargetMgr()->GetTarget(pSE->GetID(), true) == nullptr) or (SurfaceDistance(pSE) > m_profile->attackRange)) {
        m_sched.Defer(NpcAct::Turret, now);
        return;
    }
    m_sched.Due(NpcAct::Turret, now);
    AttackTarget(pSE);
}

void NPCAIMgr::FireMissile(SystemEntity* pSE)
{
    if ((pSE == nullptr) or !m_profile->hasMissile)
        return;
    int64 now(Now());
    m_sched.Ensure(NpcAct::Missile, now, m_profile->missileCycleMs, m_profile->missileCycleMs);
    if (!m_sched.IsDue(NpcAct::Missile, now))
        return;
    // the caller checked the lock.  in range:  min(entityAttackRange, 0.95 x the missile's flight range) (PLAN.md 5.2)
    if ((pSE->DestinyMgr() == nullptr) or pSE->DestinyMgr()->IsCloaked() or (SurfaceDistance(pSE) > m_profile->MissileFireRange())) {
        m_sched.Defer(NpcAct::Missile, now);
        return;
    }
    m_sched.Due(NpcAct::Missile, now);
    LaunchMissile(m_profile->missileTypeID, pSE);
}

void NPCAIMgr::ClearTarget(SystemEntity* pSE) {
    m_npc->TargetMgr()->ClearTarget(pSE);
    //m_npc->TargetMgr()->OnTarget(pSE, TargMgr::Mode::Lost);

    if (m_npc->TargetMgr()->HasNoTargets())
        SetIdle(false);
}

//also check for special effects and write code to implement them
//modifyTargetSpeedRange, modifyTargetSpeedChance
//entityWarpScrambleChance
void NPCAIMgr::AttackTarget(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    // put checks here for point/tackle

    // effects are listed in EVE_Effects.h.  effect 10's guid is effects.Laser:  the client draws the npc's own turrets
    std::string guid = "effects.Laser"; // client looks for 'turret' in ship.ball.modules for 'effects.laser'
    uint32 gfxID = 0;
    if (m_self->HasAttribute(AttrGfxTurretID))// graphicID for turret for drone type ships
        gfxID = m_self->GetAttribute(AttrGfxTurretID).get_uint32();
    m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(),
                                 pSE->GetID(),0,guid,1,1,
                                 1,m_profile->turretCycleMs,0,gfxID);

    TurretFormulas formula;
    Damage d(m_npc,
             m_self,
             m_npc->GetKinetic(),
             m_npc->GetThermal(),
             m_npc->GetEM(),
             m_npc->GetExplosive(),
             formula.GetNPCToHit(m_npc, pSE),
             EVEEffectID::targetAttack
            );

    if (sConfig.npc.UseDamageMultiplier or m_npc->IsLawEnforcer())
        if (m_profile->damageMultiplier > 0)
            d *= m_profile->damageMultiplier;

    // CONCORD scrambles, webs and jams its target first
    if (m_npc->IsConcord())
        m_npc->SystemMgr()->GetLaw()->OnLawAttack(m_npc, pSE);

    pSE->ApplyDamage(d);
}

/* missile shit..
 * //AttrEntityDefenderChance  - chance to shoot defender missile at incomming missile
 * //AttrMissileLaunchDuration  - missile cycle time
 * //AttrEntityMissileTypeID
 * //AttrMissileEntityVelocityMultiplier
 * //AttrMissileEntityFlightTimeMultiplier
 * //AttrMissileEntityAoeCloudSizeMultiplier
 * //AttrMissileEntityAoeVelocityMultiplier
 * //AttrMissileEntityAoeFalloffMultiplier
 */

void NPCAIMgr::LaunchMissile(uint16 typeID, SystemEntity* pSE)
{
    LaunchEntityMissile(m_npc, m_self, typeID, pSE);
}

void LaunchEntityMissile(SystemEntity* pLauncher, InventoryItemRef self, uint16 typeID, SystemEntity* pTarget)
{
    if ((typeID == 0) or (pLauncher == nullptr) or (pTarget == nullptr))
        return;
    // Actually Launch a missile, creating a new Destiny object for it
    // ItemData( uint32 _typeID, uint32 _ownerID, uint32 _locationID, EVEItemFlags _flag, const char *_name = "",
    //           const GPoint &_position = NULL_ORIGIN, const char *_customInfo = "", bool _contraband = false);
    ItemData idata(typeID, pLauncher->GetID(), pLauncher->GetLocationID(), flagMissile, "NPC Missile", pLauncher->GetPosition());
    InventoryItemRef missileRef = sItemFactory.SpawnItem(idata);
    if (missileRef.get() == nullptr)
        return;  // make error here

    // modify missile based on npc attribs
    if (self->HasAttribute(AttrMissileEntityVelocityMultiplier))
        missileRef->MultiplyAttribute(AttrMaxVelocity, self->GetAttribute(AttrMissileEntityVelocityMultiplier));
    if (self->HasAttribute(AttrMissileEntityFlightTimeMultiplier))    // this may be wrong
        missileRef->MultiplyAttribute(AttrExplosionDelay, self->GetAttribute(AttrMissileEntityFlightTimeMultiplier));
    if (self->HasAttribute(AttrMissileEntityAoeVelocityMultiplier))
        missileRef->MultiplyAttribute(AttrAoeVelocity, self->GetAttribute(AttrMissileEntityAoeVelocityMultiplier));
    if (self->HasAttribute(AttrMissileEntityAoeCloudSizeMultiplier))
        missileRef->MultiplyAttribute(AttrAoeCloudSize, self->GetAttribute(AttrMissileEntityAoeCloudSizeMultiplier));
    if (self->HasAttribute(AttrMissileEntityAoeFalloffMultiplier))
        missileRef->MultiplyAttribute(AttrAoeFalloff, self->GetAttribute(AttrMissileEntityAoeFalloffMultiplier));

    SystemManager* pSystem = pLauncher->SystemMgr();
    // Missile(InventoryItemRef self, PyServiceMgr &services, SystemManager* system, InventoryItemRef module, SystemEntity* target, SystemEntity* launcher);
    //  without a module, Missile() applies the launcher's missileDamageMultiplier (npcs and sentries, bug B12)
    Missile* pMissile = new Missile(missileRef, pSystem->GetServiceMgr(),  pSystem, self, pTarget, pLauncher);
    if (pMissile == nullptr)
        return; // make error here
    // the missile flies itself (see DestinyManager).  it hits when its path reaches the target
    pMissile->SetSpeed(missileRef->GetAttribute(AttrMaxVelocity).get_float());
    pMissile->DestinyMgr()->MakeMissile(pMissile);

    // tell target a missile has been launched at them.. (defender missile trigger for ship, tower, pos, npc, others?)
    if (typeID != 265)  // but only if it's NOT a defender missile
        pTarget->MissileLaunched(pMissile);
}

void NPCAIMgr::MissileLaunched(Missile* pMissile)
{
    float chance = m_self->GetAttribute(AttrEntityDefenderChance).get_float();
    if (sConfig.npc.DefenderMissileChance)
        chance = sConfig.npc.DefenderMissileChance;
    // check chance to shoot defender missile at incomming missile (working, ??/??/??)
    if (m_rng.Uniform() < chance)
        LaunchMissile(265, pMissile); // defender missile
}

float NPCAIMgr::GetTargetTime(SystemEntity* pSE)
{
    /* the player formula with our scanResolution (owner decision, PLAN.md 5.3):  40,000,000 / (scanRes x asinh(sig)^2) ms,
     *  1-180s.  dampeners on us slow it.  types without scanResolution use scanSpeed, then the old radius table */
    double sig(0.0);
    if (pSE != nullptr) {
        sig = pSE->GetSignatureRadius();
        if (sig <= 0.0)
            sig = pSE->GetRadius();
    }
    double mult(m_npc->GetEwar() != nullptr ? m_npc->GetEwar()->GetScanResMult() : 1.0);
    return static_cast<float>(m_profile->LockTimeMs(sig, mult));
}

void NPCAIMgr::OnKilled()
{
    if (m_ewar != nullptr)
        m_ewar->StopAll();
}

void NPCAIMgr::DisableRepTimers(bool shield/*true*/, bool armor/*true*/)
{
    if (armor)
        m_sched.Stop(NpcAct::ArmorRep);
    if (shield)
        m_sched.Stop(NpcAct::ShieldRep);
}

const char* NPCAIMgr::GetStateName(int8 stateID)
{
    switch (stateID) {
        case NPCAI::State::Idle:           return "Idle";
        case NPCAI::State::Chasing:        return "Chasing";
        case NPCAI::State::Engaged:        return "Engaged";
        case NPCAI::State::Fleeing:        return "Fleeing";
        case NPCAI::State::Following:      return "Following";
        case NPCAI::State::Signaling:      return "Signaling";
        case NPCAI::State::WarpOut:        return "Warping Out";
        case NPCAI::State::WarpFollow:     return "Following Warp";
        default:                           return "Invalid";
    }
}

std::string NPCAIMgr::Dump(SystemEntity* pViewer, bool verbose)
{
    NpcContext ctx(MakeContext());
    const NpcProfile& p(*m_profile);
    std::ostringstream str;
    str << std::fixed << std::setprecision(0);
    str << m_npc->GetName() << " (" << m_npc->GetID() << ")  type " << p.typeID << " group " << p.groupID << "<br>";
    str << "tier " << NpcAI::GetTierName(m_npc->IsLawEnforcer() ? NpcAI::Tier::Law : p.tier) << ", brain " << m_brain->GetName()
        << ", state " << GetStateName(m_state) << (m_dormant ? " (dormant)" : "") << (m_isWandering ? " (wandering)" : "") << "<br>";

    /* targets */
    str << "primary " << m_primaryID << ", secondary " << m_secondaryID << ", switching to " << m_switchToID
        << ", pending aggro " << m_pendingAggroID << "<br>";
    str << "locks (" << m_npc->TargetMgr()->TargetCount() << "/" << (uint32)p.maxLockedTargets << "):";
    m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        str << "  " << cur->GetName() << "(" << cur->GetID() << ") ";
        if (locked) {
            str << "locked";
        } else {
            str << "locking " << m_npc->TargetMgr()->LockRemaining(cur) << "ms";
        }
        str << " at " << SurfaceDistance(cur) << "m";
    });
    str << "<br>";

    /* threat, top 5 */
    if (!m_threat.Empty()) {
        ThreatTable::Entry top[ThreatTable::Capacity];
        size_t n(m_threat.Size());
        for (size_t i = 0; i < n; ++i)
            top[i] = m_threat.At(i);
        std::sort(top, top + n, [](const ThreatTable::Entry& a, const ThreatTable::Entry& b) { return a.threat > b.threat; });
        str << "threat:";
        for (size_t i = 0; (i < n) and (i < 5); ++i) {
            SystemEntity* pSE(m_npc->SystemMgr()->GetSE(top[i].id));
            str << "  " << (pSE != nullptr ? pSE->GetName() : "?") << "(" << top[i].id << ") " << top[i].threat;
            if (p.IsLevelOne() and (pSE != nullptr))
                str << " score " << Score(pSE);
        }
        str << "<br>";
    }

    /* schedule */
    str << "next due (ms):";
    for (uint8 i = 0; i < NpcAct::Count; ++i)
        if (m_sched.Active(i))
            str << "  " << NpcSchedule::Name(i) << " " << m_sched.Remaining(i, ctx.now)
                << (m_sched.Period(i) > 0 ? "/" : "") << (m_sched.Period(i) > 0 ? std::to_string(m_sched.Period(i)) : std::string());
    str << "<br>";

    /* profile */
    str << "weapons:  turret " << (p.hasTurret ? "yes" : "no") << " (" << p.turretCycleMs << "ms, x" << std::setprecision(2)
        << p.damageMultiplier << ", opt " << std::setprecision(0) << p.optimal << " fo " << p.falloff << " trk "
        << std::setprecision(4) << p.tracking << std::setprecision(0) << " sigRes " << p.sigResolution << ")"
        << ", missile " << (p.hasMissile ? "yes" : "no") << " (type " << p.missileTypeID << ", " << p.missileCycleMs << "ms, x"
        << std::setprecision(2) << p.missileDamageMultiplier << std::setprecision(0) << ", flight " << p.missileRange << "m)<br>";
    str << "ranges:  R_acq (sight) " << m_sightRange << ", R_atk " << p.attackRange << ", missile R " << p.MissileFireRange()
        << ", r_o " << p.orbitRange << " (fly " << m_flyRange << "), r_b " << p.boostRange << " (now " << m_boostRange
        << "), wander " << p.wanderRange << ", assist " << p.assistRange << "<br>";
    str << "lock:  scanRes " << p.scanResolution << ", scanSpeed " << p.scanSpeed;
    if (pViewer != nullptr)
        str << ", t_lock to " << pViewer->GetName() << " " << GetTargetTime(pViewer) << "ms";
    str << "<br>";
    str << "reaction D:  " << p.attackDelayMin << "-" << p.attackDelayMax << "ms x " << std::setprecision(2) << p.reactionFactor
        << std::setprecision(0) << " = " << p.ReactionDelayMs(0.0) << "-" << p.ReactionDelayMs(1.0) << "ms (phase 2)<br>";
    if (p.IsLevelOne())
        str << "level one:  secondary " << p.l1SecondaryTarget << ", sig " << p.l1SignatureRadius << " (pref " << m_preferedSigRadius
            << "), effects " << p.l1EffectMultiplier << ", switch timer " << m_switchTime << "ms (data " << p.l1SwitchTimerMs
            << "), evasive " << p.evasive << "<br>";
    str << "group:  spawn group " << m_npc->GetSpawnGroupID() << (m_npc->GetSpawnMgr() != nullptr ? ", spawn manager" : "")
        << ", respawn chance " << std::setprecision(2) << p.groupRespawnChance << std::setprecision(0) << "<br>";
    str << "rng seed " << m_rng.GetSeed() << "<br>";
    if (verbose) {
        str << "home " << m_home.x << "," << m_home.y << "," << m_home.z << " (" << m_npc->GetPosition().distance(m_home) << "m away)<br>";
        if (p.problems != 0)
            str << "data:  " << NpcProfile::ProblemList(p.problems) << "<br>";
    }
    return str.str();
}
