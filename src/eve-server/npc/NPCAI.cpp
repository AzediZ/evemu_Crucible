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

/*  Crucible-era npc ai (doc/code_and_design_notes/npc-ai/PLAN.md):
 *   standard rats (StandardBrain, phase 2):  nearest player ship or drone within R_acq, lock at once and fire after the
 *    data's reaction delay, keep the primary until it's gone, alert the aggro group (members join after their reaction
 *    delay, on the same target with probability entityReactionFactor), now and then turn on a drone that hurt them, orbit at
 *    entityFlyRange and chase-boost (with mwd signature bloom) beyond entityChaseMaxDistance, the data leash, ewar from their
 *    type, self-repair by effect.  idle belt groups warp between belts;  haulers flee, commanders run when alone.
 *   level-one ai (LevelOneBrain, phase 3:  types with AI_ShouldUseTargetSwitching > 0:  sleepers, incursion sansha, asteroid
 *    officers, 5 unique npcs):  threat from damage, ewar and remote repair, shared with the group, a score with signature,
 *    drone, range and lock terms, a switch at most every AI_TargetSwitchTimer, ewar on a secondary target, jam responses,
 *    evasive approach.  the movement, leash, reaction delay and groups above are shared.
 *   support (NpcSupport, any tier with the effect):  remote repair of the group by priority with reservations, group assists,
 *    the incursion capitals' remote ecm.
 *   law enforcement (CONCORD, police) only goes after the ship it was sent for (LawEnforcement, phase 4).
 *
 *  the type's data is its NpcProfile (dogma defaults for missing attributes, weapons from effects), the policy its brain
 *   (npc/ai/NpcBrain.h), every cycle runs on a drift-free NpcSchedule and every roll on the npc's own NpcRng.  weapons fire
 *   only at a completed lock on the primary, inside entityAttackRange.  idle npcs in bubbles without players sleep.
 */

#include "eve-server.h"

#include <iomanip>
#include <limits>

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/LawEnforcement.h"
#include "npc/NPCEwar.h"
#include "npc/ai/AggroGroup.h"
#include "npc/ai/NpcBrain.h"
#include "npc/ai/NpcLod.h"
#include "npc/ai/NpcMath.h"
#include "npc/ai/NpcMovement.h"
#include "npc/ai/NpcProfile.h"
#include "npc/ai/NpcSupport.h"
#include "ship/Missile.h"
#include "ship/modules/TurretFormulas.h"
#include "system/DestinyManager.h"
#include "system/Damage.h"
#include "system/Interdiction.h"
#include "system/SystemBubble.h"

namespace {
    // the hard leash flies (instead of warping) when home is closer than this (m)
    const double c_minWarpHome = 150000.0;
    // a wandering belt rat orbits the belt itself instead of an asteroid with this chance (phase 5 wander variety)
    const double c_wanderBeltChance = 0.25;
    /* the soft leash's cooldown (npc.SoftLeashCooldown) and the link alert throttle (npc.LinkAlertInterval), ms.
     *  [DD] phase 2's 20s and 5s */
    int64 SoftLeashCooldownMs()                 { return static_cast<int64>(sConfig.npc.SoftLeashCooldown) * 1000; }
    int64 LinkAlertMs()                         { return static_cast<int64>(sConfig.npc.LinkAlertInterval) * 1000; }
    // difficulty (npc.Difficulty, npc/ai/NpcLod.h).  law enforcement is never scaled
    double DifficultyDamage(NPC* pNPC) {
        return (pNPC->IsLawEnforcer() ? 1.0 : NpcLod::DamageMult(sConfig.npc.Difficulty, sConfig.npc.DifficultyDamage));
    }
}

NPCAIMgr::NPCAIMgr(NPC* who)
: m_state(NPCAI::State::Idle),
  m_isWandering(false),
  m_ewarChecked(false),
  m_dormant(false),
  m_wave(false),
  m_pendingJoined(false),
  m_returnHard(false),
  m_damaged(false),
  m_fleeWanted(false),
  m_hadEscorts(false),
  m_suppressLost(false),
  m_leashWarp(false),
  m_buffsActive(false),
  m_vanishing(false),
  m_telegraph(false),
  m_idleHome(false),
  m_role(NPCAI::Role::None),
  m_primaryID(0),
  m_pendingAggroID(0),
  m_secondaryID(0),
  m_switchToID(0),
  m_jammerID(0),
  m_jammerRange(0.0),
  m_pendingAt(0),
  m_softLeashAfter(0),
  m_lastLinkAlert(0),
  m_lastSwitch(0),
  m_lastDamagedAt(std::numeric_limits<int64>::min() / 2),
  m_home(NULL_ORIGIN),
  m_npc(who),
  m_ewar(nullptr),
  m_brain(nullptr),
  m_move(nullptr),
  m_support(nullptr),
  m_destiny(who->DestinyMgr()),
  m_self(who->GetSelf())
{
    assert(m_self.get() != nullptr);
    m_threat.Clear();
    m_profile = NpcProfile::Get(m_self->type());

    // every roll this npc makes.  a fixed npc.RngSeed makes them repeatable
    uint64_t seed(sConfig.npc.RngSeed);
    if (seed == 0)
        seed = static_cast<uint64_t>(GetSteadyTime()) ^ (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) << 16);
    m_rng.Seed(seed, m_self->itemID());

    // R_acq (5.4):  how far we look for targets, and our lock range
    m_acquireRange = m_profile->acquireRange;
    for (size_t i = 0; i < c_reservations; ++i)
        m_reserved[i] = Reservation();
    for (uint8 i = 0; i < NpcAI::Buff::Count; ++i) {
        m_buffs[i].mult = 1.0f;
        m_buffs[i].until = 0;
    }

    // orbit, follow, chase cycle and signature bloom (5.5)
    m_move = new NpcMovement(m_npc, *m_profile, m_rng, m_sched);

    // ewar (scram, web, jam, neut, damp, td, paint) is in NPCEwar.  it's made on the first Process(), after LawEnforcement set our role
    //  (so is the brain)

    /* target search on the think cadence (1s), staggered so a spawn doesn't all think on the same tick (bug B1:
     *  it used to wait for the turret cycle, and npcs without one never looked for anything) */
    m_sched.Start(NpcAct::Think, Now(), 1000 + (m_self->itemID() % 4) * 250, 1000);
}

NPCAIMgr::~NPCAIMgr()
{
    if (m_group.get() != nullptr)
        m_group->Remove(m_npc);
    SafeDelete(m_ewar);
    SafeDelete(m_brain);
    SafeDelete(m_move);
    SafeDelete(m_support);
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
    if (!m_npc->IsLawEnforcer()) {
        // emergent sleepers, incursion frigates and officers approach evasively from afar (1414, 5.5)
        if (m_profile->IsLevelOne() and (m_profile->evasive > 0))
            m_move->SetEvasive(true);
        // remote repair, group assists, remote ecm (any tier with the effect)
        if (NpcSupport::Needed(*m_profile))
            m_support = new NpcSupport(m_npc, this, *m_profile, m_rng);
    }
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

void NPCAIMgr::SetGroup(const std::shared_ptr<AggroGroup>& group)
{
    if (m_group.get() == group.get())
        return;
    if (m_group.get() != nullptr)
        m_group->Remove(m_npc);
    m_group = group;
    if (m_group.get() != nullptr)
        m_group->Add(m_npc);
}

void NPCAIMgr::Process() {
    if (m_brain == nullptr)
        CreateBrain();
    // overseer structures, belongings and props:  nothing to do (bug B11)
    if (!m_brain->RunsAI())
        return;

    if (!m_ewarChecked) {
        // law enforcement's type ewar:  LawEnforcement::UsesTypeEwar() (npc ai plan N6, phase 4)
        m_ewarChecked = true;
        if (m_npc->IsLawEnforcer() ? LawEnforcement::UsesTypeEwar(m_npc) : sConfig.npc.UseEwar) {
            // the usage chances (scram, web) are rolled here, once per spawn (npc.EwarUsageRoll 0)
            m_ewar = new NPCEwar(m_npc, &m_rng);
            if (!m_ewar->HasAny())
                SafeDelete(m_ewar);
        }
    }

    if (m_destiny->IsWarping())
        return;

    int64 now(Now());
    if (m_sched.Due(NpcAct::WarpOut, now)) {
        /* an idle spawn moves on after a while.  not while its dead members are waiting for a respawn (chained), nor while it
         *  chases an attacker or waits to join its group (still idle then) */
        if (IsQuiet() and (m_npc->GetSpawnMgr() != nullptr) and (m_npc->SysBubble() != nullptr)
        and !m_npc->GetSpawnMgr()->IsChaining(m_npc->SysBubble()->GetID()))
            WarpOut();
    }

    // asleep where no player is (movement still runs in SystemEntity::Process)
    if (CheckDormant())
        return;

    NpcContext ctx(MakeContext());

    // haulers and commanders leave (4.4)
    if (m_role != NPCAI::Role::None) {
        CheckFlee(now);
        if (m_destiny->IsWarping())
            return;
    }

    switch(m_state) {
        case NPCAI::State::Idle: {
            if (m_npc->IsLawEnforcer()) {
                /* CONCORD, faction police and navy only go after the ship they were sent for (LawBrain;  LawEnforcement moves
                 *  them if it leaves).  a lock refused for range retries every 2s (Target() queued it), not every tick */
                if ((m_pendingAggroID != 0) and (now < m_pendingAt))
                    break;
                m_pendingAggroID = 0;
                m_pendingJoined = false;
                SystemEntity* pTarget(m_brain->Acquire(ctx));
                if (pTarget != nullptr)
                    Target(pTarget);
                break;
            }
            // a jammed npc can't lock anything (EwarState already broke its locks).  level-one npcs move (4.6)
            if ((m_npc->GetEwar() != nullptr) and m_npc->GetEwar()->IsJammed()) {
                if (m_jammerID != 0)
                    m_brain->WhileJammed(ctx, m_npc->SystemMgr()->GetSE(m_jammerID), m_jammerRange);
                break;
            }
            // someone attacked us or our group.  a group join waits for its reaction delay first (5.6)
            if (m_pendingAggroID != 0) {
                if (now < m_pendingAt)
                    break;
                SystemEntity* pAggressor(m_npc->SystemMgr()->GetSE(m_pendingAggroID));
                bool joined(m_pendingJoined);
                m_pendingAggroID = 0;
                m_pendingJoined = false;
                if (CanAggro(pAggressor)) {
                    Target(pAggressor, joined);
                    break;
                }
                // we turned toward someone who is gone now (npc.TelegraphJoin)
                if (m_telegraph) {
                    m_telegraph = false;
                    m_move->Calm();
                    m_destiny->Stop();
                }
            }
            // look around on the think cadence
            if (!m_sched.Active(NpcAct::Think))
                m_sched.StartPeriodic(NpcAct::Think, now, 1000);
            if (m_sched.Due(NpcAct::Think, now)) {
                /* out past our leash with nothing to do:  home first, instead of a new fight that the leash would cut short
                 *  at once (being shot still gets its answer, the queued aggressor above) */
                if (IdleGoHome())
                    break;
                // a passive group wakes up when someone comes too close
                if (CheckProximity())
                    return;
                SystemEntity* pTarget(m_brain->Acquire(ctx));
                if (pTarget != nullptr) {
                    Target(pTarget);
                    // our group hears about it (4.2:  "an NPC that opens fire on its own" alerts its group too)
                    if (!IsIdle())
                        AlertGroup(pTarget);
                    return;
                }
                // wander, and now and then pick something else to wander around (phase 5)
                if (sConfig.npc.IdleWander and !m_idleHome)
                    if (!m_isWandering or m_sched.Due(NpcAct::Wander, now))
                        SetWander();
            }
            StartWarpOutTimer();
        } break;
        case NPCAI::State::Acquiring:
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
            // pulled too far from home (5.4)
            if (CheckLeash(now))
                break;
            uint32 targetID(pSE->GetID());
            CheckDistance(pSE);
            // the attack may have destroyed or dropped the target
            if (IsIdle())
                break;
            pSE = m_npc->SystemMgr()->GetSE(targetID);
            if ((pSE == nullptr) or pSE->IsDead())
                break;
            // ewar and missiles need a completed lock on the primary (bug B6a) and the reaction delay (5.6).
            //  level-one npcs put their ewar on a secondary target (1649)
            SystemEntity* pLocked(m_npc->TargetMgr()->GetTarget(targetID, true));
            ProcessEwar(ctx, pLocked, now);
            FireMissile(pSE, (pLocked != nullptr));
            m_brain->Think(ctx);
        } break;
        case NPCAI::State::Returning: {
            ProcessReturn(now);
        } break;
        case NPCAI::State::Fleeing:
        case NPCAI::State::WarpOut:
        case NPCAI::State::WarpFollow:{
            // still aligning.  (in warp we returned above; WarpOutComplete() ends this state)
            if (m_destiny->IsAligningForWarp())
                break;
            // the warp was stopped (scrambled while aligning, or a warp disruption field).  we stay here
            _log(NPC__AI_TRACE, "%s(%u): %s was cut short.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state));
            if (m_leashWarp) {
                // the hard leash couldn't warp:  fly home instead (5.4)
                m_leashWarp = false;
                m_state = NPCAI::State::Returning;
                m_returnHard = true;
                m_move->GoHome(m_home);
                m_sched.Start(NpcAct::Leash, now, 30000);
                break;
            }
            bool fleeing(m_state == NPCAI::State::Fleeing);
            // a spawn that ran off and couldn't stays (it may roll again on its next warp-out)
            m_vanishing = false;
            SetIdle();
            if (fleeing)
                m_sched.Start(NpcAct::Flee, now, 10000);
            if ((m_npc->GetSpawnMgr() != nullptr) and (m_npc->SysBubble() != nullptr))
                m_npc->GetSpawnMgr()->MoveSpawn(m_npc, m_npc->SysBubble());
        } break;
        default: {
            SetIdle();
        } break;
    }

    ProcessRepairs();
    // remote repair, group assists, remote ecm
    if (m_support != nullptr)
        m_support->Process(now, ((m_state == NPCAI::State::Acquiring) or (m_state == NPCAI::State::Engaged)
                                or (m_state == NPCAI::State::Returning)));
    if (m_buffsActive)
        ProcessBuffs(now);
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
    // finish repairing first
    if (m_sched.Active(NpcAct::ShieldRep) or m_sched.Active(NpcAct::ArmorRep))
        return false;
    m_dormant = true;
    // the idle spawn still moves to another belt now and then (checked before the sleep test in Process())
    StartWarpOutTimer();
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
    // self-repair by effect (5.9):  each cycle may be skipped on the effect's delay chance
    int64 now(Now());
    if (m_sched.Due(NpcAct::ShieldRep, now))
        if (m_rng.Uniform() >= m_profile->selfShield.skipChance)
            m_npc->UseShieldRecharge();

    if (m_sched.Due(NpcAct::ArmorRep, now))
        if (m_rng.Uniform() >= m_profile->selfArmor.skipChance)
            m_npc->UseArmorRepairer();
}

void NPCAIMgr::OnDamaged(SystemEntity* pSE, float amount)
{
    NpcContext ctx(MakeContext());
    if (!m_brain->RunsAI())
        return;

    // self repair starts on damage (not on being locked) and runs until we're whole again
    if (m_profile->selfShield.effectID != 0)
        m_sched.Ensure(NpcAct::ShieldRep, ctx.now, m_profile->selfShield.cycleMs, m_profile->selfShield.cycleMs);
    if (m_profile->selfArmor.effectID != 0)
        m_sched.Ensure(NpcAct::ArmorRep, ctx.now, m_profile->selfArmor.cycleMs, m_profile->selfArmor.cycleMs);

    if ((pSE == nullptr) or (pSE == m_npc) or m_npc->IsLawEnforcer())
        return;

    Wake("damaged");
    m_damaged = true;
    m_lastDamagedAt = ctx.now;
    // threat:  the brain's rule (level one:  tanking modifiers, the group's share, 5.7)
    m_brain->OnDamage(ctx, pSE, amount);
    m_recent.Add(pSE->GetID(), amount, ctx.now);
    // we're inside the attacker's ApplyDamage() call here, so the answer waits for our next Process().
    //  being shot beats a group join that is still waiting for its delay
    if (IsIdle() and ((m_pendingAggroID == 0) or (m_pendingAt > ctx.now))) {
        m_pendingAggroID = 0;
        m_pendingJoined = false;
        QueueAggro(m_brain->OnAggressed(ctx, pSE));
    }
    AlertGroup(pSE);
}

void NPCAIMgr::ShareThreat(SystemEntity* pSE, float amount)
{
    if ((pSE == nullptr) or m_npc->IsLawEnforcer())
        return;
    NpcContext ctx(MakeContext());
    m_brain->OnGroupThreat(ctx, pSE, amount);
}

float NPCAIMgr::ReservedArmor(int64 now) const
{
    float sum(0.0f);
    for (size_t i = 0; i < c_reservations; ++i)
        if (m_reserved[i].until > now)
            sum += m_reserved[i].amount;
    return sum;
}

void NPCAIMgr::ReserveArmor(float amount, int64 until)
{
    // a free (or expired) slot, else the one that ends first
    int64 now(Now());
    size_t slot(0);
    for (size_t i = 0; i < c_reservations; ++i) {
        if (m_reserved[i].until <= now) {
            slot = i;
            break;
        }
        if (m_reserved[i].until < m_reserved[slot].until)
            slot = i;
    }
    m_reserved[slot].amount = amount;
    m_reserved[slot].until = until;
}

void NPCAIMgr::ApplyBuff(uint8 kind, float mult, uint32 durationMs)
{
    if ((kind >= NpcAI::Buff::Count) or !(mult > 0.0f) or (mult == 1.0f))
        return;
    int64 now(Now());
    // what ran out goes first (Process() may not have got there)
    if (m_buffsActive)
        ProcessBuffs(now);
    BuffState& buff(m_buffs[kind]);
    if (buff.until != 0) {
        if (buff.mult == mult) {
            buff.until = std::max(buff.until, now + static_cast<int64>(durationMs));
            return;
        }
        // another one of the same kind:  it replaces ours
        SetBuff(kind, buff.mult, false);
    }
    buff.mult = mult;
    buff.until = now + static_cast<int64>(durationMs);
    SetBuff(kind, mult, true);
    m_buffsActive = true;
}

void NPCAIMgr::ProcessBuffs(int64 now)
{
    bool any(false);
    for (uint8 i = 0; i < NpcAI::Buff::Count; ++i) {
        if (m_buffs[i].until == 0)
            continue;
        if (m_buffs[i].until > now) {
            any = true;
            continue;
        }
        SetBuff(i, m_buffs[i].mult, false);
        m_buffs[i].until = 0;
        m_buffs[i].mult = 1.0f;
    }
    m_buffsActive = any;
}

void NPCAIMgr::SetBuff(uint8 kind, float mult, bool on)
{
    switch (kind) {
        case NpcAI::Buff::ShieldResist:
        case NpcAI::Buff::ArmorResist: {
            static const uint16 shield[] = { AttrShieldEmDamageResonance, AttrShieldExplosiveDamageResonance,
                                             AttrShieldKineticDamageResonance, AttrShieldThermalDamageResonance };
            static const uint16 armor[] = { AttrArmorEmDamageResonance, AttrArmorExplosiveDamageResonance,
                                            AttrArmorKineticDamageResonance, AttrArmorThermalDamageResonance };
            const uint16* attrs(kind == NpcAI::Buff::ShieldResist ? shield : armor);
            for (uint8 i = 0; i < 4; ++i) {
                // a missing resonance is 1 (no resistance, SystemEntity::Resonance)
                float value(m_self->HasAttribute(attrs[i]) ? m_self->GetAttribute(attrs[i]).get_float() : 1.0f);
                value = (on ? value * mult : value / mult);
                m_self->SetAttribute(attrs[i], value, false);
            }
        } break;
        case NpcAI::Buff::Speed: {
            m_destiny->SetSpeedBuff(on ? mult : 1.0f);
        } break;
        case NpcAI::Buff::PropJam: {
            if (m_ewar != nullptr)
                m_ewar->SetTackleRangeMult(on ? mult : 1.0f);
        } break;
    }
    _log(NPC__AI_TRACE, "%s(%u): Group assist %u x%.2f %s.", m_npc->GetName(), m_npc->GetID(), kind, mult, (on ? "on" : "off"));
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

bool NPCAIMgr::FindBelt(SystemBubble*& pDest, GPoint& where)
{
    /** @todo  eventually, this will check with anomaly mgr for possible npc hideouts in system
     * based on npc faction, system, players in system, players in bubble, and *more later*
     * to determine a warpto target for this npc, or this group
     */
    pDest = nullptr;
    SystemBubble* pBubble(m_npc->SysBubble());
    if ((pBubble == nullptr) or (m_npc->GetSpawnMgr() == nullptr))
        return false;
    SystemManager* pSys = m_npc->SystemMgr();
    uint32 curBeltID(sBubbleMgr.GetBeltID(pBubble->GetID()));
    for (uint8 i = 0; i < 3; ++i) {
        uint32 beltID(pSys->GetRandBeltID());
        if ((beltID == 0) or (beltID == curBeltID))
            continue;
        SystemEntity* pBelt(pSys->GetSE(beltID));
        if (pBelt == nullptr)
            continue;
        SystemBubble* pFound(sBubbleMgr.FindBubble(pBelt));
        if (pFound == nullptr)
            pFound = sBubbleMgr.GetBubble(pSys, pBelt->GetPosition());
        if ((pFound == nullptr) or (pFound == pBubble))
            continue;
        // another group lives there
        if (m_npc->GetSpawnMgr()->HasSpawn(pFound->GetID()))
            continue;
        pDest = pFound;
        where = pBelt->GetPosition();
        return true;
    }
    return false;
}

void NPCAIMgr::WarpOut()
{
    m_sched.Stop(NpcAct::WarpOut);
    if ((m_npc->GetSpawnMgr() == nullptr) or m_npc->IsLawEnforcer() or (m_npc->SysBubble() == nullptr))
        return;
    if ((m_state == NPCAI::State::WarpOut) or (m_state == NPCAI::State::WarpFollow) or (m_state == NPCAI::State::Fleeing))
        return;
    // a player has us pointed.  try again later
    if (m_npc->IsWarpScrambled()) {
        m_sched.Start(NpcAct::WarpOut, Now(), 30000);
        return;
    }
    // if there are no players in this system, avoid using proc tics on npcs
    if (m_npc->SystemMgr()->PlayerCount() < 1) {
        StartWarpOutTimer();
        return;
    }
    /* now and then a belt spawn runs off for good instead (phase 5, era:  "run-away spawns", "1 in 25 vanish"):  the belt is
     *  free for a new spawn */
    if ((sConfig.npc.WarpOutVanishChance > 0.0f) and m_npc->SysBubble()->IsBelt()
    and (m_rng.Uniform() < sConfig.npc.WarpOutVanishChance) and m_npc->GetSpawnMgr()->VanishSpawn(m_npc))
        return;
    // the whole spawn group warps to another belt that has no spawn
    SystemBubble* pDest(nullptr);
    GPoint where;
    if (FindBelt(pDest, where)) {
        _log(NPC__AI_TRACE, "%s(%u): Warping my group out to bubble %u.", m_npc->GetName(), m_npc->GetID(), pDest->GetID());
        if (m_npc->GetSpawnMgr()->WarpOutSpawn(m_npc, pDest, where))
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
    m_move->Calm();
    if (m_support != nullptr)
        m_support->StopAll();
    m_sched.Stop(NpcAct::Missile);
    m_sched.Stop(NpcAct::Turret);
    m_sched.Stop(NpcAct::DroneRoll);
    m_sched.Stop(NpcAct::WarpOut);
    m_sched.Stop(NpcAct::SwitchEval);
    m_sched.Stop(NpcAct::ThreatDecay);
    m_sched.Stop(NpcAct::Reaction);
    m_sched.Stop(NpcAct::Leash);
    m_primaryID = 0;
    m_secondaryID = 0;
    m_switchToID = 0;
    m_pendingAggroID = 0;
    m_pendingJoined = false;
    m_returnHard = false;
    m_threat.Clear();
    m_isWandering = false;
    m_dormant = false;
    m_leashWarp = false;
    m_telegraph = false;
    m_sched.Stop(NpcAct::Wander);
    m_destiny->SetMaxVelocity(m_profile->maxVelocity);
    m_state = state;
}

void NPCAIMgr::WarpOutComplete()
{
    m_sched.Stop(NpcAct::WarpOut);
    /* wherever an npc lands is home, the first warp-in of a belt spawn too:  those are created 1000+ km off grid,
     *  and the leash would otherwise pull every fight back there.  not after the leash's own warp home (a warp disruption
     *  field may have pulled it short) */
    if (!m_leashWarp)
        m_home = m_npc->GetPosition();
    m_leashWarp = false;
    if ((m_state != NPCAI::State::WarpOut) and (m_state != NPCAI::State::WarpFollow) and (m_state != NPCAI::State::Fleeing))
        return;
    _log(NPC__AI_TRACE, "%s(%u): %s complete.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state));
    bool fled(m_state == NPCAI::State::Fleeing);
    m_state = NPCAI::State::Idle;
    m_move->Calm();
    m_destiny->SetMaxVelocity(m_profile->cruiseSpeed);
    // a hauler that got away (or a spawn that ran off for good) is gone (no kill, no respawn).  SpawnMgr removes it on its next tick
    if (fled and ((m_role == NPCAI::Role::Hauler) or m_vanishing) and (m_npc->GetSpawnMgr() != nullptr)) {
        m_npc->GetSpawnMgr()->SpawnFled(m_npc);
        return;
    }
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

bool NPCAIMgr::CheckLeash(int64 now)
{
    // law enforcement goes where its target goes
    if (m_npc->IsLawEnforcer())
        return false;
    double distance(m_npc->GetPosition().distance(m_home));
    uint8 leash(NpcMath::LeashState(distance, m_profile->wanderRange, sConfig.npc.LeashSoftScale, sConfig.npc.LeashHardScale,
                                    static_cast<double>(sConfig.npc.LeashRange)));
    if (leash == NpcMath::Leash::None)
        return false;
    // it turned back a moment ago:  chase on for a while first
    if ((leash == NpcMath::Leash::Soft) and (now < m_softLeashAfter))
        return false;
    BeginReturn(leash == NpcMath::Leash::Hard, now);
    return true;
}

bool NPCAIMgr::IdleGoHome()
{
    if (m_npc->IsLawEnforcer() or (m_destiny == nullptr) or m_destiny->IsWarping())
        return false;
    double distance(m_npc->GetPosition().distance(m_home));
    if (NpcMath::LeashState(distance, m_profile->wanderRange, sConfig.npc.LeashSoftScale, sConfig.npc.LeashHardScale,
                            static_cast<double>(sConfig.npc.LeashRange)) == NpcMath::Leash::None) {
        /* back inside:  we may look for targets again.  the goto runs on to the home point (wander waits for it, its Stop()
         *  would leave us at the leash's edge) unless another order replaced it */
        if (m_idleHome and ((distance <= 5000.0) or !m_destiny->IsGoto()))
            m_idleHome = false;
        return false;
    }
    // (again) when nothing takes us home:  a telegraph follow, a chase retry or a jam move replaced the goto
    if (!m_idleHome or !m_destiny->IsGoto() or (m_destiny->GetTargetPoint().distance(m_home) > 1.0)) {
        if (!m_idleHome)
            _log(NPC__AI_TRACE, "%s(%u): Idle %.0fm from home (wander range %.0f).  Going back.", m_npc->GetName(),
                    m_npc->GetID(), distance, m_profile->wanderRange);
        m_idleHome = true;
        m_isWandering = false;
        m_move->GoHome(m_home);
    }
    return true;
}

void NPCAIMgr::BeginReturn(bool hard, int64 now)
{
    double distance(m_npc->GetPosition().distance(m_home));
    _log(NPC__AI_TRACE, "%s(%u): %s leash:  %.0fm from home (wander range %.0f).  Going back.", m_npc->GetName(), m_npc->GetID(),
            (hard ? "hard" : "soft"), distance, m_profile->wanderRange);
    m_state = NPCAI::State::Returning;
    m_returnHard = hard;
    if (!hard) {
        // soft:  back for U(5, 10)s, keeping our locks, then resume (5.4)
        m_move->GoHome(m_home);
        m_sched.Start(NpcAct::Leash, now, static_cast<int64>(m_rng.Range(5000, 10000)));
        return;
    }

    /* hard:  drop everything and go home, ignoring aggression.  warp when we can (npc.LeashHardMode 1), else fly until
     *  within 5km or 30s passed.  (the state is Returning before the locks go, so TargetLost leaves us alone) */
    if (m_ewar != nullptr)
        m_ewar->StopAll();
    m_sched.Stop(NpcAct::Missile);
    m_sched.Stop(NpcAct::Turret);
    m_sched.Stop(NpcAct::DroneRoll);
    m_sched.Stop(NpcAct::Reaction);
    m_sched.Stop(NpcAct::SwitchEval);
    m_sched.Stop(NpcAct::ThreatDecay);
    m_primaryID = 0;
    m_secondaryID = 0;
    m_switchToID = 0;
    m_pendingAggroID = 0;
    m_pendingJoined = false;
    m_threat.Clear();
    m_npc->TargetMgr()->ClearTargets();
    // inside a warp disruption field we can't warp (npc.RespectInterdiction):  fly.  a refused or stopped warp flies too
    InterdictionMgr* pInterdict(m_npc->SystemMgr()->GetInterdictionMgr());
    bool disrupted((pInterdict != nullptr) and pInterdict->IsWarpDisrupted(m_npc));
    if ((sConfig.npc.LeashHardMode == 1) and !m_npc->IsWarpScrambled() and !disrupted and (distance >= c_minWarpHome)) {
        BeginWarp(NPCAI::State::WarpOut);
        m_leashWarp = true;
        m_destiny->WarpTo(m_home, 0);
        return;
    }
    m_move->GoHome(m_home);
    m_sched.Start(NpcAct::Leash, now, 30000);
}

void NPCAIMgr::ProcessReturn(int64 now)
{
    if (m_returnHard) {
        if ((m_npc->GetPosition().distance(m_home) > 5000) and !m_sched.Due(NpcAct::Leash, now))
            return;
        // home (or long enough).  back to normal
        _log(NPC__AI_TRACE, "%s(%u): Back home.", m_npc->GetName(), m_npc->GetID());
        m_sched.Stop(NpcAct::Leash);
        m_returnHard = false;
        m_move->Calm();
        m_destiny->Stop();
        m_state = NPCAI::State::Idle;
        StartWarpOutTimer();
        m_sched.Start(NpcAct::Think, now, 0, 1000);
        return;
    }

    // soft:  our locks stay, and what is in range still gets shot
    if (m_npc->TargetMgr()->HasNoTargets()) {
        m_sched.Stop(NpcAct::Leash);
        SetIdle(false);
        return;
    }
    SystemEntity* pPrimary(GetPrimary());
    if (pPrimary != nullptr) {
        uint32 targetID(pPrimary->GetID());
        Attack(pPrimary);
        pPrimary = m_npc->SystemMgr()->GetSE(targetID);
        if ((pPrimary != nullptr) and !pPrimary->IsDead() and (m_state == NPCAI::State::Returning)) {
            SystemEntity* pLocked(m_npc->TargetMgr()->GetTarget(targetID, true));
            // our ewar keeps going too (its effects and their fx would otherwise run on without cycles)
            NpcContext ctx(MakeContext());
            ProcessEwar(ctx, pLocked, now);
            FireMissile(pPrimary, (pLocked != nullptr));
        }
    }
    if ((m_state == NPCAI::State::Returning) and m_sched.Due(NpcAct::Leash, now))
        EndReturn(now);
}

void NPCAIMgr::EndReturn(int64 now)
{
    m_sched.Stop(NpcAct::Leash);
    m_softLeashAfter = now + SoftLeashCooldownMs();
    m_move->Calm();
    bool locked((m_primaryID != 0) and (m_npc->TargetMgr()->GetTarget(m_primaryID, true) != nullptr));
    m_state = (locked ? NPCAI::State::Engaged : NPCAI::State::Acquiring);
    _log(NPC__AI_TRACE, "%s(%u): Done turning back.  Back to %s.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state));
}

void NPCAIMgr::CheckFlee(int64 now)
{
    // belt spawns only, and not while we're on our way somewhere already
    if (m_npc->GetSpawnMgr() == nullptr)
        return;
    if ((m_state == NPCAI::State::Fleeing) or (m_state == NPCAI::State::WarpOut) or (m_state == NPCAI::State::WarpFollow)
    or ((m_state == NPCAI::State::Returning) and m_returnHard))
        return;
    SystemBubble* pBubble(m_npc->SysBubble());
    if ((pBubble == nullptr) or pBubble->IsAnomaly() or pBubble->IsMission() or pBubble->IsIncursion())
        return;

    if (m_role == NPCAI::Role::Hauler) {
        // "will escape at the first opportunity (typically within 30 seconds if not attacked)".  the clock starts when a
        //  player is around (we don't run while dormant);  hurt, we try at once and whenever we aren't scrambled
        if (!m_fleeWanted) {
            m_sched.Ensure(NpcAct::Flee, now, static_cast<int64>(sConfig.npc.HaulerFleeDelay) * 1000);
            if (!m_damaged and !m_sched.Due(NpcAct::Flee, now))
                return;
            m_fleeWanted = true;
            m_sched.Stop(NpcAct::Flee);
        } else if (m_sched.Active(NpcAct::Flee) and !m_sched.Due(NpcAct::Flee, now)) {
            return;
        }
    } else if (m_role == NPCAI::Role::Commander) {
        if (!sConfig.npc.CommanderFlee or (m_group.get() == nullptr))
            return;
        // every 2s:  are our escorts gone?
        if (m_sched.Active(NpcAct::Flee) and !m_sched.Due(NpcAct::Flee, now))
            return;
        m_sched.Start(NpcAct::Flee, now, 2000);
        uint32 escorts(0);
        m_group->ForEachMember([&](NPC* pNPC) {
            if ((pNPC != m_npc) and !pNPC->IsDead() and (pNPC->GetAIMgr() != nullptr)
            and (pNPC->GetAIMgr()->GetRole() != NPCAI::Role::Commander))
                ++escorts;
        });
        if (escorts > 0) {
            m_hadEscorts = true;
            return;
        }
        if (!m_hadEscorts)
            return;
    } else {
        return;
    }

    // pointed:  stay (and fight, if we can) and try again
    if (m_npc->IsWarpScrambled()) {
        m_sched.Start(NpcAct::Flee, now, 2000);
        return;
    }
    if (!Flee()) {
        m_sched.Start(NpcAct::Flee, now, 10000);
        return;
    }
    // a commander settles at its new belt (it runs once;  new escorts would make it run again)
    if (m_role == NPCAI::Role::Commander)
        m_hadEscorts = false;
}

bool NPCAIMgr::Flee()
{
    GPoint where(m_npc->GetPosition());
    if (m_role == NPCAI::Role::Hauler) {
        // off grid, anywhere:  it's gone when it lands
        where.MakeRandomPointOnSphere(m_rng.Range(0.3, 0.6) * ONE_AU_IN_METERS);
    } else {
        SystemBubble* pDest(nullptr);
        if (!FindBelt(pDest, where))
            return false;
    }
    _log(NPC__AI_TRACE, "%s(%u): Fleeing (%s).", m_npc->GetName(), m_npc->GetID(),
            (m_role == NPCAI::Role::Hauler ? "hauler" : "commander without escorts"));
    BeginWarp(NPCAI::State::Fleeing);
    m_destiny->WarpTo(where, (m_role == NPCAI::Role::Hauler ? 0 : static_cast<int32>(m_rng.Range(5, 30)) * 1000));
    return true;
}

void NPCAIMgr::Vanish()
{
    // off grid, anywhere:  gone when we land (WarpOutComplete -> SpawnMgr::SpawnFled)
    GPoint where(m_npc->GetPosition());
    where.MakeRandomPointOnSphere(m_rng.Range(0.3, 0.6) * ONE_AU_IN_METERS);
    _log(NPC__AI_TRACE, "%s(%u): My spawn leaves for good.", m_npc->GetName(), m_npc->GetID());
    BeginWarp(NPCAI::State::Fleeing);
    m_vanishing = true;
    m_destiny->WarpTo(where, static_cast<int32>(m_rng.Range(5, 30)) * 1000);
}

SystemEntity* NPCAIMgr::PickWanderTarget(SystemBubble* pBubble)
{
    /* a random asteroid of the bubble, or (now and then, or when there is none) the belt itself (phase 5 wander variety:  it
     *  used to be the first wreck or object in map order) */
    SystemEntity* pBelt(m_npc->SystemMgr()->GetSE(sBubbleMgr.GetBeltID(pBubble->GetID())));
    if ((pBelt != nullptr) and (m_rng.Uniform() < c_wanderBeltChance))
        return pBelt;
    SystemEntity* pPick(nullptr);
    uint32 seen(0);
    pBubble->ForEachDynamic([&](SystemEntity* cur) {
        if (!cur->IsAsteroidSE() or cur->IsDead())
            return;
        // reservoir sampling:  each asteroid with the same chance, no list
        ++seen;
        if (m_rng.Uniform() * seen < 1.0)
            pPick = cur;
    });
    return (pPick != nullptr ? pPick : pBelt);
}

void NPCAIMgr::SetWander()
{
    if (m_npc->GetSpawnMgr() == nullptr)
        return;
    SystemBubble* pBubble = m_npc->SysBubble();
    if (pBubble == nullptr)
        return;
    if (!m_isWandering) {
        _log(NPC__AI_TRACE, "%s(%u): Wandering:  No Targets within my acquisition range of %um", \
                m_npc->GetName(), m_npc->GetID(), m_acquireRange);
        m_isWandering = true;
    }

    // wandering.  nothing to shoot.  look for target.
    if (pBubble->IsAnomaly() or pBubble->IsIncursion() or pBubble->IsMission()) {
        // dungeon npcs hold their position (5.5)
        return;
    } else if (pBubble->IsBelt()) {
        // loosely orbit a random asteroid or the belt, and pick again after a while
        SystemEntity* pSE(PickWanderTarget(pBubble));
        if (pSE == nullptr) {
            _log(NPC__ERROR, "%s(%u): Wandering:  No Target or beltSE found.", m_npc->GetName(), m_npc->GetID());
            // nothing here...leave bubble
            WarpOut();
            return;
        }
        m_destiny->SetMaxVelocity(m_profile->cruiseSpeed);
        uint32 orbitDistance = static_cast<uint32>(m_rng.Range(10000, 20001));
        m_destiny->Orbit(pSE, orbitDistance);
        uint32 lo(std::min(sConfig.npc.WanderRepickMin, sConfig.npc.WanderRepickMax));
        uint32 hi(std::max(sConfig.npc.WanderRepickMin, sConfig.npc.WanderRepickMax));
        if (hi > 0) {
            m_sched.Start(NpcAct::Wander, Now(), static_cast<int64>(m_rng.Range(static_cast<double>(lo), static_cast<double>(hi)) * 1000.0));
        } else {
            m_sched.Stop(NpcAct::Wander);
        }
        _log(NPC__AI_TRACE, "%s(%u): Wandering around %s(%u) at %um.", m_npc->GetName(), m_npc->GetID(), pSE->GetName(),
                pSE->GetID(), orbitDistance);
    } else {
        /** @todo  figure out a way for npc to wander 'aimlessly' around their bubble */
        m_destiny->Stop();
    }
}

void NPCAIMgr::SetIdle(bool clearLocks/*true*/) {
    if (m_state == NPCAI::State::Idle)
        return;
    // not doing anything....idle.

    _log(NPC__AI_TRACE, "%s(%u): Idle: returning to idle from %s.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state));
    m_state = NPCAI::State::Idle;
    m_move->Calm();
    m_destiny->Stop();
    m_destiny->SetMaxVelocity(m_profile->cruiseSpeed);

    // locks we still hold (or are getting) on things we no longer fight go too (bug B16)
    if (clearLocks and !m_npc->TargetMgr()->HasNoTargets())
        m_npc->TargetMgr()->ClearTargets();

    // repair timers keep running until we're whole (they stop themselves)
    m_sched.Stop(NpcAct::Missile);
    m_sched.Stop(NpcAct::Turret);
    m_sched.Stop(NpcAct::DroneRoll);
    m_sched.Stop(NpcAct::Reaction);
    m_sched.Stop(NpcAct::Leash);
    if (m_ewar != nullptr)
        m_ewar->StopAll();
    m_primaryID = 0;
    m_secondaryID = 0;
    m_switchToID = 0;
    m_returnHard = false;
    m_sched.Stop(NpcAct::SwitchEval);
    m_sched.Stop(NpcAct::ThreatDecay);
    // level-one threat outlives a jam that broke every lock (it fades on its own)
    if ((m_brain == nullptr) or !m_brain->KeepsThreat())
        m_threat.Clear();
    // the fight may have ended out past our leash:  home, even if we fall asleep on the way (movement runs while dormant)
    m_idleHome = false;
    IdleGoHome();

    //disallow warpout if anomaly, incursion or mission rat (and by config)
    StartWarpOutTimer();
}

void NPCAIMgr::CheckDistance(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return;
    double dist = m_npc->GetPosition().distance(pSE->GetPosition());
    if ((dist > m_acquireRange) and (!m_npc->TargetMgr()->IsTargetedBy(pSE))) {
        _log(NPC__AI_TRACE, "%s(%u): CheckDistance: %s(%u) is too far away (%.0fm, R_acq %um).  Dropping it.", \
             m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), dist, m_acquireRange);
        // out of our sight and not locking us:  let it go (a target locking us is chased, out to the leash)
        if (m_state != NPCAI::State::Idle)
            ClearTarget(pSE);
        return;
    }

    m_isWandering = false;
    // orbit, follow or chase (5.5)
    m_move->Engage(pSE, Now());
    Attack(pSE);
}

void NPCAIMgr::Target(SystemEntity* pSE, bool joined/*false*/) {
    if (pSE == nullptr)
        return;
    int64 now(Now());
    bool fresh(m_state == NPCAI::State::Idle);
    bool chase = false;
    m_telegraph = false;

    if (!BeginLock(pSE, chase)) {
        _log(NPC__AI_TRACE, "%s(%u): Targeting of %s(%u) failed%s.", m_npc->GetName(), m_npc->GetID(), pSE->GetName(),
                pSE->GetID(), (chase ? " (out of range)" : ""));
        if (fresh and chase and CanAggro(pSE)) {
            /* out of our (maybe dampened) lock range:  close in and try again in 2s (a sniper that hit us, or damps on us).
             *  the retry is a queued aggro, so wander and acquisition wait for it */
            m_isWandering = false;
            m_destiny->SetMaxVelocity(m_profile->maxVelocity);
            m_destiny->Follow(pSE, static_cast<uint32>(m_move->OrbitRange()));
            QueueAggro(pSE, now + 2000, joined);
        }
        return;
    }
    if (fresh) {
        m_state = NPCAI::State::Acquiring;
        m_isWandering = false;
        m_wave = false;
        m_sched.Stop(NpcAct::WarpOut);
        /* reaction delay (5.6):  an npc going after someone itself locks at once, its first weapon or ewar activation waits
         *  U(entityAttackDelayMin, Max) x entityReactionFactor.  a group join waited that long before locking.
         *  law enforcement answers at once */
        double delay((joined or m_npc->IsLawEnforcer()) ? 0.0 : DrawReactionDelay());
        // the first pick counts as a switch:  the next one comes AI_TargetSwitchTimer later at the earliest (level one)
        m_lastSwitch = now;
        if (delay >= 1.0) {
            m_sched.Start(NpcAct::Reaction, now, static_cast<int64>(delay));
        } else {
            m_sched.Stop(NpcAct::Reaction);
        }
        _log(NPC__AI_TRACE, "%s(%u): Going after %s(%u)%s:  lock %.0fms, reaction %.0fms.", m_npc->GetName(), m_npc->GetID(),
                pSE->GetName(), pSE->GetID(), (joined ? " (group join)" : ""), GetTargetTime(pSE), delay);
    }
    // a new fight, or our primary is gone
    if ((m_primaryID == 0) or (GetPrimary() == nullptr))
        SetPrimary(pSE);
    CheckDistance(pSE);

    // the turret cycle runs from the start of the lock.  shots wait for the lock, the range and the reaction (Attack())
    if (m_profile->hasTurret)
        m_sched.Ensure(NpcAct::Turret, now, m_profile->turretCycleMs, m_profile->turretCycleMs);
    if (m_profile->hasMissile)
        m_sched.Ensure(NpcAct::Missile, now, m_profile->missileCycleMs, m_profile->missileCycleMs);
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
            SystemEntity* pTarget(m_brain->OnAggressed(ctx, pSE));
            if (CanAggro(pTarget))
                Target(pTarget);
            // a lock is aggression:  a passive group aggroes on it (4.3)
            AlertGroup(pSE);
        } break;
        case NPCAI::State::Returning:
            // the hard leash ignores everyone until it's home
            if (m_returnHard)
                break;
            // fall through
        case NPCAI::State::Acquiring:
        case NPCAI::State::Engaged: {
            if (m_npc->IsLawEnforcer())
                break;
            // regular rats keep their primary.  lock the newcomer too, so we have someone to turn to (never a pod, bug B10)
            if (!pSE->IsShipSE() or !CanAggro(pSE))
                break;
            bool chase = false;
            BeginLock(pSE, chase);
            AlertGroup(pSE);
        } break;
    }
}

void NPCAIMgr::TargetLost(SystemEntity* pSE) {
    if ((pSE == nullptr) or m_suppressLost)
        return;
    switch(m_state) {
        case NPCAI::State::Returning:
            if (m_returnHard)
                break;
            // fall through
        case NPCAI::State::Acquiring:
        case NPCAI::State::Engaged: {
            if (pSE->GetID() == m_switchToID)
                m_switchToID = 0;
            if (m_npc->TargetMgr()->HasNoTargets()) {
                // our last target warped off.  maybe we follow
                if ((pSE->GetID() == m_primaryID) and (m_state != NPCAI::State::Returning) and TryWarpFollow(pSE))
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
        } break;
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
        // the switch we decided on (level-one switch, standard drone roll)
        SwitchPrimary(pSE);
    }
    if ((pSE->GetID() == m_primaryID) and (m_state == NPCAI::State::Acquiring)) {
        m_state = NPCAI::State::Engaged;
        _log(NPC__AI_TRACE, "%s(%u): Engaged %s(%u).", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    }
}

void NPCAIMgr::AddThreat(SystemEntity* pSE, float amount)
{
    if ((pSE == nullptr) or !(amount > 0.0f))
        return;
    /* a full table only takes a newcomer whose single addition beats its lowest entry, so many small hits never got in.
     *  first forget what has left our bubble (killed, recalled, docked, jumped:  level-one threat outlives idle) */
    SystemBubble* pBubble(m_npc->SysBubble());
    // (not while we're between bubbles:  everything would count as gone)
    if (m_threat.Full() and (pBubble != nullptr) and (m_threat.Get(pSE->GetID()) == 0.0f)) {
        SystemManager* pSystem(m_npc->SystemMgr());
        m_threat.RemoveIf([&](uint32 id) {
            SystemEntity* pCur(pSystem->GetSE(id));
            return ((pCur == nullptr) or pCur->IsDead() or (pCur->SysBubble() != pBubble));
        });
    }
    m_threat.Add(pSE->GetID(), amount);
}

void NPCAIMgr::OnEwarBy(SystemEntity* pSE, uint8 effect/*0*/)
{
    if ((pSE == nullptr) or m_npc->IsLawEnforcer())
        return;
    NpcContext ctx(MakeContext());
    if (!m_brain->RunsAI())
        return;
    Wake("ewar");
    m_brain->OnEwar(ctx, pSE, effect, false);
    if (IsIdle() and ((m_pendingAggroID == 0) or (m_pendingAt > ctx.now))) {
        m_pendingAggroID = 0;
        m_pendingJoined = false;
        QueueAggro(m_brain->OnAggressed(ctx, pSE));
    }
    AlertGroup(pSE);
}

void NPCAIMgr::OnJammedBy(SystemEntity* pSE, uint32 durationMs, double jammerRange)
{
    // the cycle itself came through OnEwarBy();  this is the jam on top of it
    if ((pSE == nullptr) or m_npc->IsLawEnforcer())
        return;
    NpcContext ctx(MakeContext());
    if (!m_brain->RunsAI())
        return;
    m_jammerID = pSE->GetID();
    m_jammerRange = jammerRange;
    m_brain->OnEwar(ctx, pSE, NpcMath::Effect::Ecm, true);
    _log(NPC__AI_TRACE, "%s(%u): Jammed by %s(%u) for %ums (its reach %.0fm).", m_npc->GetName(), m_npc->GetID(), pSE->GetName(),
            pSE->GetID(), durationMs, jammerRange);
}

void NPCAIMgr::OnAssistObserved(SystemEntity* pRepairer, SystemEntity* pPatient, float hp)
{
    if ((pRepairer == nullptr) or (pPatient == nullptr) or m_npc->IsLawEnforcer() or m_dormant)
        return;
    NpcContext ctx(MakeContext());
    if (!m_brain->RunsAI())
        return;
    m_brain->OnAssist(ctx, pRepairer, pPatient, hp);
}

uint8 NPCAIMgr::EwarEffectOfGroup(uint16 groupID)
{
    switch (groupID) {
        case EVEDB::invGroups::ECM:
        case EVEDB::invGroups::ECM_Burst:
        case EVEDB::invGroups::Remote_ECM_Burst:        return NpcMath::Effect::Ecm;
        case EVEDB::invGroups::Remote_Sensor_Damper:    return NpcMath::Effect::Damp;
        case EVEDB::invGroups::Tracking_Disruptor:      return NpcMath::Effect::TrackDisrupt;
        case EVEDB::invGroups::Energy_Vampire:
        case EVEDB::invGroups::Energy_Destabilizer:     return NpcMath::Effect::Neut;
        case EVEDB::invGroups::Stasis_Web:              return NpcMath::Effect::Web;
        case EVEDB::invGroups::Warp_Scrambler:
        case EVEDB::invGroups::Warp_Disrupt_Field_Generator:    return NpcMath::Effect::Scramble;
        case EVEDB::invGroups::Target_Painter:          return NpcMath::Effect::Paint;
    }
    return NpcMath::Effect::None;
}

void NPCAIMgr::JamStarted(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget, uint32 durationMs)
{
    // players' ships and drones (npcs and structures jamming npcs aren't a fight we answer)
    if ((pSource == nullptr) or (pTarget == nullptr) or !pTarget->IsNPCSE() or (!pSource->IsShipSE() and !pSource->IsDroneSE()))
        return;
    NPCAIMgr* pAI(pTarget->GetNPCSE()->GetAIMgr());
    if (pAI == nullptr)
        return;
    // the jammer's reach:  its module's (or drone's) optimal + falloff
    InventoryItemRef jammer(((moduleID != 0) and (moduleID != pSource->GetID())) ? sItemFactory.GetItemRef(moduleID) : pSource->GetSelf());
    double range(0.0);
    if (jammer.get() != nullptr)
        range = jammer->GetAttribute(AttrMaxRange).get_double() + jammer->GetAttribute(AttrFalloff).get_double();
    pAI->OnJammedBy(pSource, durationMs, range);
}

void NotifyRemoteAssist(SystemEntity* pRepairer, SystemEntity* pPatient, float hp)
{
    if ((pRepairer == nullptr) or (pPatient == nullptr) or !(hp > 0.0f) or (pPatient->SysBubble() == nullptr))
        return;
    // players helping players.  only level-one npcs that watch for effects (1652) care
    if (pPatient->IsNPCSE() or pRepairer->IsNPCSE() or !(sConfig.npc.L1RepairThreat > 0.0f))
        return;
    uint32 patientID(pPatient->GetID());
    pPatient->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        if (!cur->IsNPCSE() or cur->IsDead())
            return;
        NPCAIMgr* pAI(cur->GetNPCSE()->GetAIMgr());
        if ((pAI == nullptr) or !pAI->GetProfile().l1EffectMultiplier)
            return;
        if ((cur->TargetMgr()->GetTarget(patientID, false) == nullptr) and !(pAI->GetThreat(patientID) > 0.0f))
            return;
        pAI->OnAssistObserved(pRepairer, pPatient, hp);
    });
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

void NPCAIMgr::QueueAggro(SystemEntity* pSE, int64 atMs/*0*/, bool joined/*false*/)
{
    if ((pSE == nullptr) or (m_pendingAggroID != 0))
        return;
    m_pendingAggroID = pSE->GetID();
    m_pendingAt = atMs;
    m_pendingJoined = joined;
    /* readability (phase 5):  a group member that joins after its reaction delay turns toward the target meanwhile, so the
     *  pilot sees it coming (an npc going after someone itself moves at once, Target()).  the lock waits for the delay */
    if (joined and sConfig.npc.TelegraphJoin and IsIdle() and (atMs > Now()) and !m_npc->IsLawEnforcer() and CanAggro(pSE)
    and !m_destiny->IsWarping() and ((m_npc->GetEwar() == nullptr) or !m_npc->GetEwar()->IsJammed())) {
        m_telegraph = true;
        m_isWandering = false;
        m_destiny->SetMaxVelocity(m_profile->cruiseSpeed);
        m_destiny->Follow(pSE, static_cast<uint32>(m_move->OrbitRange()));
    }
}

void NPCAIMgr::AlertGroup(SystemEntity* pAggressor)
{
    if ((pAggressor == nullptr) or (m_group.get() == nullptr) or m_npc->IsLawEnforcer())
        return;
    // players' ships and drones only (npcs never fight npcs)
    if (!pAggressor->IsShipSE() and !pAggressor->IsDroneSE())
        return;
    m_group->Alert(m_npc, pAggressor);
    // groups linked to ours (dunSpawnGroups.aggroLink) aggro with us
    if (m_group->GetLinkKey() != 0) {
        int64 now(Now());
        if ((now - m_lastLinkAlert) >= LinkAlertMs()) {
            m_lastLinkAlert = now;
            SpawnMgr* pSpawnMgr(m_npc->SystemMgr()->GetSpawnMgr());
            if (pSpawnMgr != nullptr)
                pSpawnMgr->GetAggroGroups().AlertLinked(m_group.get(), pAggressor);
        }
    }
}

bool NPCAIMgr::CheckProximity()
{
    // a passive group aggroes on a player ship or drone that comes within its aggroRange of any member (4.3)
    if ((m_group.get() == nullptr) or !m_group->IsPassive() or (m_npc->SysBubble() == nullptr))
        return false;
    double range(m_group->GetAggroRange());
    SystemEntity* pFound(nullptr);
    double nearest(0.0);
    m_npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        if (!IsCandidate(cur, true))
            return;
        double distance(SurfaceDistance(cur));
        if ((distance > range) or ((pFound != nullptr) and (distance >= nearest)))
            return;
        pFound = cur;
        nearest = distance;
    });
    if (pFound == nullptr)
        return false;
    _log(NPC__AI_TRACE, "%s(%u): %s(%u) came within %.0fm of my passive group.", m_npc->GetName(), m_npc->GetID(),
            pFound->GetName(), pFound->GetID(), nearest);
    Target(pFound);
    AlertGroup(pFound);
    return !IsIdle();
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

bool NPCAIMgr::IsCandidate(SystemEntity* pSE, bool drones, bool undamped/*false*/)
{
    if (!CanAggro(pSE))
        return false;
    if (pSE->IsDroneSE()) {
        // players' drones (abandoned ones too:  they are still the player's)
        if (!drones or !IsCharacterID(pSE->GetSelf()->ownerID()))
            return false;
    } else if (!pSE->HasPilot()) {
        // players' ships only
        return false;
    }
    // within what we can lock (R_acq, dampened like TargetManager::StartTargeting does), or could after closing in
    return (m_npc->GetPosition().distance(pSE->GetPosition()) <= (undamped ? static_cast<double>(m_acquireRange) : LockRange()));
}

double NPCAIMgr::LockRange()
{
    double range(m_acquireRange);
    if (m_npc->GetEwar() != nullptr)
        range *= m_npc->GetEwar()->GetTargetRangeMult();
    return range;
}

SystemEntity* NPCAIMgr::ChooseStandardTarget(bool drones, bool weakest, AggroGroup* aggressorsOnly)
{
    if (m_npc->SysBubble() == nullptr)
        return nullptr;
    int64 now(Now());
    SystemEntity* pBest(nullptr);
    double best(0.0);
    m_npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        // the weakest is always a ship
        if (!IsCandidate(cur, (drones and !weakest)))
            return;
        if ((aggressorsOnly != nullptr) and !aggressorsOnly->IsAggressor(cur, now))
            return;
        double value(0.0);
        if (weakest) {
            // shield + armor + hull left
            InventoryItemRef iRef(cur->GetSelf());
            value = iRef->GetAttribute(AttrShieldCharge).get_double()
                  + (iRef->GetAttribute(AttrArmorHP).get_double() - iRef->GetAttribute(AttrArmorDamage).get_double())
                  + (iRef->GetAttribute(AttrHP).get_double() - iRef->GetAttribute(AttrDamage).get_double());
            if (!std::isfinite(value))
                return;
        } else {
            value = m_npc->GetPosition().distance(cur->GetPosition());
        }
        if ((pBest == nullptr) or (value < best)) {
            pBest = cur;
            best = value;
        }
    });
    return pBest;
}

SystemEntity* NPCAIMgr::AggroTarget(SystemEntity* pSE)
{
    if ((pSE == nullptr) or !pSE->IsDroneSE())
        return pSE;
    // shoot back at the drone's pilot when we can see him
    SystemEntity* pController(pSE->GetDroneSE()->GetController());
    if (CanAggro(pController) and (m_npc->GetPosition().distance(pController->GetPosition()) <= m_acquireRange))
        return pController;
    return pSE;
}

SystemEntity* NPCAIMgr::GetPrimary()
{
    if (m_primaryID != 0) {
        SystemEntity* pSE(m_npc->TargetMgr()->GetTarget(m_primaryID, false));
        if ((pSE != nullptr) and m_npc->IsLawEnforcer() and (m_primaryID != m_npc->GetLawTarget())) {
            /* law enforcement only shoots the ship it was sent for.  the pilot boarded another one (LawEnforcement::Retarget):
             *  the old, empty ship is let go */
            m_primaryID = 0;
            ClearTarget(pSE);
            if (IsIdle())
                return nullptr;
            pSE = nullptr;
        }
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
    m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        // never a pod, a cloaked, warping or invulnerable ship, or one that left our bubble (bug B10)
        if ((cur == pExclude) or !CanAggro(cur))
            return;
        float threat(GetThreat(cur->GetID()));
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
    // the movement starts over on its own (NpcMovement::Engage() sees the new target)
    if ((pSE != nullptr) and (m_state == NPCAI::State::Engaged) and (m_npc->TargetMgr()->GetTarget(newID, true) == nullptr))
        m_state = NPCAI::State::Acquiring;
    else if ((pSE != nullptr) and (m_state == NPCAI::State::Acquiring) and (m_npc->TargetMgr()->GetTarget(newID, true) != nullptr))
        m_state = NPCAI::State::Engaged;
}

bool NPCAIMgr::StartLock(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return false;
    bool chase = false;
    return BeginLock(pSE, chase);
}

bool NPCAIMgr::BeginLock(SystemEntity* pSE, bool& chase)
{
    double lockMs(GetTargetTime(pSE));
    if (!m_npc->TargetMgr()->StartTargeting(pSE, static_cast<float>(lockMs), m_profile->maxLockedTargets, m_acquireRange, chase))
        return false;
    // telemetry:  the locks npcs start and how long they take (NPC__AI_STATS)
    m_npc->SystemMgr()->GetNpcStats().AddLock(lockMs);
    return true;
}

void NPCAIMgr::DropLock(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return;
    uint32 id(pSE->GetID());
    m_suppressLost = true;
    m_npc->TargetMgr()->ClearTarget(pSE);
    m_suppressLost = false;
    if (id == m_primaryID)
        m_primaryID = 0;
    if (id == m_switchToID)
        m_switchToID = 0;
}

void NPCAIMgr::SwitchPrimary(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return;
    _log(NPC__AI_TRACE, "%s(%u): Switching to %s(%u).", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    SetPrimary(pSE);
    m_switchToID = 0;
    m_lastSwitch = Now();
    // telemetry:  level-one switches per minute should stay at 1 per npc or below, standard ones near 0 (plan phase 5)
    NpcStats& stats(m_npc->SystemMgr()->GetNpcStats());
    if (m_profile->IsLevelOne()) {
        ++stats.switchesL1;
    } else {
        ++stats.switchesStandard;
    }
}

void NPCAIMgr::ProcessDroneRoll()
{
    // Crucible rats now and then turned on drones that hurt them ("rats switch to my drones unless each rat has been hit")
    if ((sConfig.npc.DroneAggroChance <= 0.0f) or m_npc->IsLawEnforcer() or (m_switchToID != 0))
        return;
    int64 now(Now());
    int64 interval(static_cast<int64>(std::max<uint32>(sConfig.npc.DroneAggroInterval, 1)) * 1000);
    if (!m_sched.Active(NpcAct::DroneRoll)) {
        m_sched.StartPeriodic(NpcAct::DroneRoll, now, interval);
        return;
    }
    if (!m_sched.Due(NpcAct::DroneRoll, now))
        return;
    SystemEntity* pPrimary(GetPrimary());
    if ((pPrimary == nullptr) or pPrimary->IsDroneSE())
        return;
    // our primary's pilot hurt us lately:  we stay on him
    if (m_recent.Get(pPrimary->GetID(), now) > 0.0f)
        return;

    uint32 ids[RecentDamage::Capacity];
    size_t count(m_recent.Sorted(now, ids, RecentDamage::Capacity));
    for (size_t i = 0; i < count; ++i) {
        SystemEntity* pSE(m_npc->SystemMgr()->GetSE(ids[i]));
        if ((pSE == nullptr) or !pSE->IsDroneSE() or !CanAggro(pSE))
            continue;
        // one we can lock (we may drop our primary for it below)
        if (m_npc->GetPosition().distance(pSE->GetPosition()) > LockRange())
            continue;
        if (m_rng.Uniform() >= sConfig.npc.DroneAggroChance)
            continue;
        _log(NPC__AI_TRACE, "%s(%u): Turning on drone %s(%u).", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
        // already locked (or locking):  it's the primary once the lock is there
        if (m_npc->TargetMgr()->GetTarget(pSE->GetID(), false) != nullptr) {
            if (m_npc->TargetMgr()->GetTarget(pSE->GetID(), true) != nullptr) {
                SetPrimary(pSE);
                ++m_npc->SystemMgr()->GetNpcStats().switchesStandard;
            } else {
                m_switchToID = pSE->GetID();
            }
            return;
        }
        // make room:  our least threatening lock that isn't the primary, else the primary itself (one-lock rats)
        if (m_npc->TargetMgr()->TargetCount() >= m_profile->maxLockedTargets) {
            SystemEntity* pDrop(nullptr);
            float lowest(0.0f);
            m_npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
                if (cur == pPrimary)
                    return;
                float threat(GetThreat(cur->GetID()));
                if ((pDrop == nullptr) or (threat < lowest)) {
                    pDrop = cur;
                    lowest = threat;
                }
            });
            if (pDrop == nullptr)
                pDrop = pPrimary;
            m_suppressLost = true;
            m_npc->TargetMgr()->ClearTarget(pDrop);
            m_suppressLost = false;
            if (pDrop == pPrimary)
                m_primaryID = 0;
        }
        bool chase = false;
        if (BeginLock(pSE, chase)) {
            if (m_primaryID == 0) {
                SetPrimary(pSE);
                ++m_npc->SystemMgr()->GetNpcStats().switchesStandard;
            } else {
                m_switchToID = pSE->GetID();
            }
        } else if (m_npc->TargetMgr()->HasNoTargets()) {
            SetIdle(false);
        }
        return;
    }
}

double NPCAIMgr::DrawReactionDelay()
{
    if (!sConfig.npc.UseAttackDelay)
        return 0.0;
    double factor(sConfig.npc.UseReactionFactor ? m_profile->reactionFactor : 1.0);
    // x npc.Difficulty's reaction part (private servers)
    double scale(sConfig.npc.AttackDelayScale * NpcLod::TimeMult(sConfig.npc.Difficulty, sConfig.npc.DifficultyReactionTime));
    return NpcMath::ReactionDelayMs(m_profile->attackDelayMin, m_profile->attackDelayMax, factor, scale, m_rng.Uniform());
}

float NPCAIMgr::RecentDamageBy(uint32 entityID)
{
    return m_recent.Get(entityID, Now());
}

void NPCAIMgr::ProcessEwar(NpcContext& ctx, SystemEntity* pLocked, int64 now)
{
    // ewar needs a completed lock on the primary (bug B6a) and the reaction delay (5.6).  level-one npcs put it on a secondary
    //  target (1649).  every session's fx starts with its first cycle and stops with it (NPCEwar)
    if ((m_ewar == nullptr) or !Reacted(now))
        return;
    SystemEntity* secondaries[8];
    size_t count(m_brain->EwarTargets(ctx, pLocked, secondaries, 8));
    m_secondaryID = (count > 0 ? secondaries[0]->GetID() : 0);
    if (count > 0) {
        m_ewar->Process(pLocked, secondaries, count);
    } else {
        m_ewar->Process(pLocked);
    }
}

bool NPCAIMgr::Reacted(int64 now)
{
    if (!m_sched.Active(NpcAct::Reaction))
        return true;
    if (!m_sched.IsDue(NpcAct::Reaction, now))
        return false;
    m_sched.Stop(NpcAct::Reaction);
    return true;
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
    // only at a completed lock on this target (bug B6b:  it used to be any lock), only inside entityAttackRange (B7) and only
    //  after our reaction delay (5.6).  otherwise the shot waits (the cycle doesn't burst when it's allowed again)
    if ((m_npc->TargetMgr()->GetTarget(pSE->GetID(), true) == nullptr) or (SurfaceDistance(pSE) > m_profile->attackRange)
    or !Reacted(now)) {
        m_sched.Defer(NpcAct::Turret, now);
        return;
    }
    m_sched.Due(NpcAct::Turret, now);
    AttackTarget(pSE);
}

void NPCAIMgr::FireMissile(SystemEntity* pSE, bool locked)
{
    if ((pSE == nullptr) or !m_profile->hasMissile)
        return;
    int64 now(Now());
    m_sched.Ensure(NpcAct::Missile, now, m_profile->missileCycleMs, m_profile->missileCycleMs);
    if (!m_sched.IsDue(NpcAct::Missile, now))
        return;
    // a completed lock on the primary, in range:  min(entityAttackRange, 0.95 x the missile's flight range) (PLAN.md 5.2),
    //  after our reaction delay.  otherwise the launch waits (no catch-up volleys after a long lock)
    if (!locked or (pSE->DestinyMgr() == nullptr) or pSE->DestinyMgr()->IsCloaked()
    or (SurfaceDistance(pSE) > m_profile->MissileFireRange()) or !Reacted(now)) {
        m_sched.Defer(NpcAct::Missile, now);
        return;
    }
    m_sched.Due(NpcAct::Missile, now);
    LaunchMissile(m_profile->missileTypeID, pSE);
}

void NPCAIMgr::ClearTarget(SystemEntity* pSE) {
    m_npc->TargetMgr()->ClearTarget(pSE);
    //m_npc->TargetMgr()->OnTarget(pSE, TargMgr::Mode::Lost);

    // TargetLost() may have started a warp after it (warp follow):  leave that alone
    if (m_npc->TargetMgr()->HasNoTargets() and (m_state != NPCAI::State::WarpOut) and (m_state != NPCAI::State::WarpFollow))
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

    // the shot, with every term for /npcai verbose (fairness audit) and the telemetry's hit ratio
    TurretFormulas formula;
    NpcShotRecord shot;
    float quality(formula.GetNPCToHit(m_npc, pSE, &shot));
    // (no roll without a destiny on the target:  nothing to show)
    if (shot.atMs != 0)
        m_shots.Add(shot);
    m_npc->SystemMgr()->GetNpcStats().AddShot(quality);
    Damage d(m_npc,
             m_self,
             m_npc->GetKinetic(),
             m_npc->GetThermal(),
             m_npc->GetEM(),
             m_npc->GetExplosive(),
             quality,
             EVEEffectID::targetAttack
            );

    if (sConfig.npc.UseDamageMultiplier or m_npc->IsLawEnforcer())
        if (m_profile->damageMultiplier > 0)
            d *= m_profile->damageMultiplier;
    // npc.Difficulty (private servers;  never law enforcement)
    float difficulty(static_cast<float>(DifficultyDamage(m_npc)));
    if (difficulty != 1.0f)
        d *= difficulty;

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
    LaunchEntityMissile(m_npc, m_self, typeID, pSE, static_cast<float>(DifficultyDamage(m_npc)));
}

void LaunchEntityMissile(SystemEntity* pLauncher, InventoryItemRef self, uint16 typeID, SystemEntity* pTarget, float damageMult/*1*/)
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
    // npc.Difficulty (private servers)
    if ((damageMult > 0.0f) and (damageMult != 1.0f)) {
        EvilNumber mult(damageMult);
        missileRef->MultiplyAttribute(AttrEmDamage, mult);
        missileRef->MultiplyAttribute(AttrThermalDamage, mult);
        missileRef->MultiplyAttribute(AttrKineticDamage, mult);
        missileRef->MultiplyAttribute(AttrExplosiveDamage, mult);
    }

    SystemManager* pSystem = pLauncher->SystemMgr();
    // Missile(InventoryItemRef self, PyServiceMgr &services, SystemManager* system, InventoryItemRef module, SystemEntity* target, SystemEntity* launcher);
    //  without a module, Missile() applies the launcher's missileDamageMultiplier (npcs and sentries, bug B12)
    Missile* pMissile = new Missile(missileRef, pSystem->GetServiceMgr(),  pSystem, self, pTarget, pLauncher);
    if (pMissile == nullptr)
        return; // make error here
    // the missile flies itself (see DestinyManager).  it hits when its path reaches the target
    pMissile->SetSpeed(missileRef->GetAttribute(AttrMaxVelocity).get_float());
    pMissile->DestinyMgr()->MakeMissile(pMissile);

    // telemetry (NPC__AI_STATS)
    ++pSystem->GetNpcStats().missiles;

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
    double ms(m_profile->LockTimeMs(sig, mult));
    // npc.Difficulty (private servers;  never law enforcement)
    if (!m_npc->IsLawEnforcer())
        ms *= NpcLod::TimeMult(sConfig.npc.Difficulty, sConfig.npc.DifficultyLockTime);
    return static_cast<float>(ms);
}

void NPCAIMgr::OnKilled(SystemEntity* pKiller)
{
    if (m_ewar != nullptr)
        m_ewar->StopAll();
    // a member killed in one volley still aggroes its group (a passive one too, 4.3)
    if ((pKiller != nullptr) and (m_brain != nullptr) and m_brain->RunsAI())
        AlertGroup(pKiller);
    // our group stops counting us now (commanders' escort count, alerts)
    if (m_group.get() != nullptr) {
        m_group->Remove(m_npc);
        m_group.reset();
    }
}

void NPCAIMgr::OnRemoved()
{
    // out of space without dying:  nothing of ours runs on, and nobody is alerted
    if (m_ewar != nullptr)
        m_ewar->StopAll();
    if (m_support != nullptr)
        m_support->StopAll();
    m_pendingAggroID = 0;
    m_pendingJoined = false;
    m_telegraph = false;
    if (m_group.get() != nullptr) {
        m_group->Remove(m_npc);
        m_group.reset();
    }
}

bool NPCAIMgr::IsQuiet()
{
    if (!IsIdle() or (m_pendingAggroID != 0) or m_telegraph)
        return false;
    return (m_npc->TargetMgr() == nullptr) or (m_npc->TargetMgr()->HasNoTargets() and !m_npc->TargetMgr()->IsTargeted());
}

bool NPCAIMgr::IsBusy()
{
    // the first Process() makes the brain;  law enforcement and brains that never sleep always run
    if ((m_brain == nullptr) or !m_brain->CanSleep() or m_npc->IsLawEnforcer())
        return true;
    if (!IsIdle() or (m_pendingAggroID != 0) or m_telegraph)
        return true;
    // something going on with us:  locks either way, a self repair to finish
    if (!m_npc->TargetMgr()->HasNoTargets() or m_npc->TargetMgr()->IsTargeted())
        return true;
    return (m_sched.Active(NpcAct::ShieldRep) or m_sched.Active(NpcAct::ArmorRep));
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
        case NPCAI::State::Acquiring:      return "Acquiring";
        case NPCAI::State::Engaged:        return "Engaged";
        case NPCAI::State::Fleeing:        return "Fleeing";
        case NPCAI::State::Returning:      return "Returning";
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
        << ", state " << GetStateName(m_state) << (m_state == NPCAI::State::Returning ? (m_returnHard ? " (hard)" : " (soft)") : "")
        << (m_dormant ? " (dormant)" : "") << (m_isWandering ? " (wandering)" : "")
        << (m_role == NPCAI::Role::Hauler ? ", hauler" : (m_role == NPCAI::Role::Commander ? ", commander" : ""))
        << (m_wave ? ", wave" : "") << "<br>";

    /* targets */
    str << "primary " << m_primaryID << ", secondary " << m_secondaryID << ", switching to " << m_switchToID
        << ", pending aggro " << m_pendingAggroID;
    if (m_pendingAggroID != 0)
        str << " in " << EvE::max(m_pendingAt - ctx.now) << "ms" << (m_pendingJoined ? " (group join)" : "");
    str << "<br>";
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
        }
        str << "<br>";
    }
    if (!m_recent.Empty()) {
        uint32 ids[RecentDamage::Capacity];
        size_t n(m_recent.Sorted(ctx.now, ids, 5));
        str << "damage in the last 30s:";
        for (size_t i = 0; i < n; ++i)
            str << "  " << ids[i] << " " << m_recent.Get(ids[i], ctx.now);
        str << "<br>";
    }

    /* schedule */
    str << "next due (ms):";
    for (uint8 i = 0; i < NpcAct::Count; ++i)
        if (m_sched.Active(i))
            str << "  " << NpcSchedule::Name(i) << " " << m_sched.Remaining(i, ctx.now)
                << (m_sched.Period(i) > 0 ? "/" : "") << (m_sched.Period(i) > 0 ? std::to_string(m_sched.Period(i)) : std::string());
    str << "<br>";

    /* movement (5.5) */
    str << "movement:  " << m_move->GetModeName() << ", r_o " << m_move->OrbitRange() << ", r_b " << m_move->BoostRange()
        << ", cruise " << p.cruiseSpeed << " max " << p.maxVelocity << "m/s, signature x" << std::setprecision(1)
        << m_npc->GetSigMult() << std::setprecision(0) << " (bloom " << std::setprecision(1) << p.sigBloom << std::setprecision(0)
        << (sConfig.npc.ChaseSigBloom ? "" : ", off") << ")" << (m_move->IsMWDBlocked() ? ", mwd blocked" : "") << "<br>";
    str << "leash:  " << m_npc->GetPosition().distance(m_home) << "m from home, soft " << (p.wanderRange * sConfig.npc.LeashSoftScale)
        << " hard " << (sConfig.npc.LeashRange > 0 ? static_cast<double>(sConfig.npc.LeashRange) : p.wanderRange * sConfig.npc.LeashHardScale)
        << (m_npc->IsLawEnforcer() ? " (law:  none)" : "") << "<br>";

    /* profile */
    str << "weapons:  turret " << (p.hasTurret ? "yes" : "no") << " (" << p.turretCycleMs << "ms, x" << std::setprecision(2)
        << p.damageMultiplier << ", opt " << std::setprecision(0) << p.optimal << " fo " << p.falloff << " trk "
        << std::setprecision(4) << p.tracking << std::setprecision(0) << " sigRes " << p.sigResolution << ")"
        << ", missile " << (p.hasMissile ? "yes" : "no") << " (type " << p.missileTypeID << ", " << p.missileCycleMs << "ms, x"
        << std::setprecision(2) << p.missileDamageMultiplier << std::setprecision(0) << ", flight " << p.missileRange << "m)<br>";
    str << "ranges:  R_acq " << m_acquireRange << ", R_atk " << p.attackRange << ", missile R " << p.MissileFireRange()
        << ", fly " << p.flyRange << ", chase " << p.chaseMaxDistance << ", wander " << p.wanderRange << ", assist " << p.assistRange << "<br>";
    str << "lock:  scanRes " << p.scanResolution << ", scanSpeed " << p.scanSpeed;
    if (pViewer != nullptr)
        str << ", t_lock to " << pViewer->GetName() << " " << GetTargetTime(pViewer) << "ms";
    str << "<br>";
    double factor(sConfig.npc.UseReactionFactor ? p.reactionFactor : 1.0);
    str << "reaction D:  " << p.attackDelayMin << "-" << p.attackDelayMax << "ms x " << std::setprecision(2) << factor
        << " x " << sConfig.npc.AttackDelayScale << std::setprecision(0) << " = "
        << NpcMath::ReactionDelayMs(p.attackDelayMin, p.attackDelayMax, factor, sConfig.npc.AttackDelayScale, 0.0) << "-"
        << NpcMath::ReactionDelayMs(p.attackDelayMin, p.attackDelayMax, factor, sConfig.npc.AttackDelayScale, 1.0) << "ms"
        << (sConfig.npc.UseAttackDelay ? "" : " (off)");
    if (m_sched.Active(NpcAct::Reaction))
        str << ", first shot in " << m_sched.Remaining(NpcAct::Reaction, ctx.now) << "ms";
    str << "<br>";
    str << "self-repair:  shield " << p.selfShield.effectID << " (" << p.selfShield.amount << "/" << p.selfShield.cycleMs << "ms, skip "
        << std::setprecision(2) << p.selfShield.skipChance << std::setprecision(0) << "), armor " << p.selfArmor.effectID << " ("
        << p.selfArmor.amount << "/" << p.selfArmor.cycleMs << "ms, skip " << std::setprecision(2) << p.selfArmor.skipChance
        << std::setprecision(0) << ")<br>";
    // the brain's own (level one:  scores, switch timer, newcomers)
    str << m_brain->Describe(ctx);
    if (m_support != nullptr)
        str << m_support->Describe(ctx.now);
    if (m_buffsActive) {
        static const char* names[NpcAI::Buff::Count] = { "shield resist", "speed", "prop jam", "armor resist" };
        str << "group assists on us:";
        for (uint8 i = 0; i < NpcAI::Buff::Count; ++i)
            if (m_buffs[i].until > ctx.now)
                str << "  " << names[i] << " x" << std::setprecision(2) << m_buffs[i].mult << std::setprecision(0)
                    << " for " << (m_buffs[i].until - ctx.now) << "ms";
        str << "<br>";
    }
    str << "group:  " << (m_group.get() != nullptr ? m_group->Describe(ctx.now) : std::string("none"))
        << (m_npc->GetSpawnMgr() != nullptr ? ", spawn manager" : "") << ", respawn chance " << std::setprecision(2)
        << p.groupRespawnChance << std::setprecision(0) << "<br>";
    str << "rng seed " << m_rng.GetSeed() << "<br>";
    /* phase 5:  level of detail, difficulty, readability */
    SystemBubble* pBubble(m_npc->SysBubble());
    uint8 tier(NpcLod::TierOf((pBubble != nullptr) and pBubble->HasPlayers(), m_npc->SystemMgr()->PlayerCount() > 0));
    static const char* tiers[] = { "near", "far", "empty system" };
    str << "level of detail:  " << tiers[tier < 3 ? tier : 0] << (IsBusy() ? ", busy (every tick)" : "") << ", difficulty damage x"
        << std::setprecision(2) << DifficultyDamage(m_npc) << " lock x"
        << (m_npc->IsLawEnforcer() ? 1.0 : NpcLod::TimeMult(sConfig.npc.Difficulty, sConfig.npc.DifficultyLockTime)) << " reaction x"
        << (m_npc->IsLawEnforcer() ? 1.0 : NpcLod::TimeMult(sConfig.npc.Difficulty, sConfig.npc.DifficultyReactionTime))
        << std::setprecision(0)
        << (m_telegraph ? ", turned toward its group's target" : "") << (m_vanishing ? ", leaving for good" : "") << "<br>";
    if (verbose) {
        str << "home " << m_home.x << "," << m_home.y << "," << m_home.z << "<br>";
        if (p.problems != 0)
            str << "data:  " << NpcProfile::ProblemList(p.problems) << "<br>";
        // the fairness audit:  every term of our last shots (PLAN.md 5.1;  DAMAGE__TRACE_NPC logs each one)
        if (m_shots.Size() > 0) {
            str << "last shots:<br>";
            for (size_t i = 0; i < m_shots.Size(); ++i)
                str << "  " << NpcShotLog::Format(m_shots.Newest(i), ctx.now) << "<br>";
        }
    }
    return str.str();
}
