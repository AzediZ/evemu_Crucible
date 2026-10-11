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
    AI Version: 0.57
*/

/*  Crucible-era npc ai:
 *   regular rats: nearest ship in sight, keep the primary until it's gone, group aggro, chase speed timers,
 *    now and then a drone that hurt them, ewar from their type (npc/NPCEwar.h), idle groups warp between belts.
 *   advanced ai (types with AI_ShouldUseTargetSwitching, the sleepers): threat/signature scoring incl. drones,
 *    timed target switching, ewar on a secondary target, remote repair of friends, evasive orbit, leash.
 *   rats neither flee nor call for help (Fleeing/Signaling states are unused).
 */

#include "eve-server.h"

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/LawEnforcement.h"
#include "npc/NPCEwar.h"
#include "ship/Missile.h"
#include "system/DestinyManager.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"

NPCAIMgr::NPCAIMgr(NPC* who)
: m_state(NPCAI::State::Idle),
  m_npc(who),
  m_destiny(who->DestinyMgr()),
  m_self(who->GetSelf()),
  m_processTimer(0),
  m_mainAttackTimer(0),
  m_missileTimer(0),
  m_warpOutTimer(0),
  m_shieldBoosterTimer(0),
  m_armorRepairTimer(0),
  m_beginFindTarget(0),
  m_chaseDelayTimer(0),
  m_chaseTimer(0),
  m_droneAggroTimer(0),
  m_leashTimer(0),
  m_switchTimer(0),
  m_threatDecayTimer(0),
  m_remoteArmorTimer(0),
  m_remoteShieldTimer(0),
  m_secondaryID(0),
  m_switchToID(0),
  m_moveTargetID(0),
  m_missileTypeID(0),
  m_isWandering(false),
  m_ewarChecked(false),
  m_chaseFast(true),
  m_primaryID(0),
  m_pendingAggroID(0),
  m_lastGoto(NULL_ORIGIN),
  m_home(NULL_ORIGIN),
  m_ewar(nullptr)
{
    m_threat.clear();
    assert(m_self.get() != nullptr);
    m_damageMultiplier = m_self->GetAttribute(AttrDamageMultiplier).get_float();

    /* set npc ship data */
    m_sigResolution = m_self->GetAttribute(AttrOptimalSigRadius).get_uint32();
    m_attackSpeed = m_self->GetAttribute(AttrSpeed).get_uint32();
    m_sigRadius = m_self->GetAttribute(AttrSignatureRadius).get_uint32();
    m_launcherCycleTime = m_self->GetAttribute(AttrMissileLaunchDuration).get_uint32();
    if (m_launcherCycleTime > 100)
        m_missileTypeID = m_self->GetAttribute(AttrEntityMissileTypeID).get_uint32();

    //  AttrEntityDefenderChance = 497,  <<< for defender missiles

    /** @todo  all of these need to be verified and/or updated */

    // ship speeds
    // absolute (boosted) Max Ship Speed
    m_maxSpeed = m_self->GetAttribute(AttrMaxVelocity).get_uint32();
    // Orbit Velocity
    m_orbitSpeed = m_self->GetAttribute(AttrEntityCruiseSpeed).get_uint32();   // ship speed when not chasing target
    // chase speed (m_maxSpeed, their mwd) is used outside entityChaseMaxDistance (m_boostRange):
    //  entityChaseMaxDelay        - most time stalled before chase speed kicks in
    //  entityChaseMaxDelayChance  - chance the npc stalls first
    //  entityChaseMaxDuration     - most time chase speed is used before stalling again
    //  entityChaseMaxDurationChance - chance a chase lasts that long (else it's shorter)
    m_chaseMaxDelay = m_self->GetAttribute(AttrEntityChaseMaxDelay).get_uint32();
    m_chaseDelayChance = m_self->GetAttribute(AttrEntityChaseMaxDelayChance).get_float();
    m_chaseMaxDuration = m_self->GetAttribute(AttrEntityChaseMaxDuration).get_uint32();
    m_chaseDurationChance = m_self->GetAttribute(AttrEntityChaseMaxDurationChance).get_float();
    if ((m_chaseMaxDuration > 0) and (m_chaseMaxDuration < 1000))
        m_chaseMaxDuration = 1000;
    if (m_chaseMaxDelay > 60000)
        m_chaseMaxDelay = 60000;

    // ship distances
    //AttrEntityMaxWanderRange
    // Optimal Range  - TODO: test for 0
    m_optimalRange = m_self->GetAttribute(AttrMaxRange).get_uint32();  // distance which npc starts using weapons
    // Accuracy falloff  (distance past optimal range at which accuracy has fallen by half) - TODO: test for 0
    m_falloff = m_self->GetAttribute(AttrFalloff).get_uint32();
    m_trackingSpeed = m_self->GetAttribute(AttrTrackingSpeed).get_double();  //rad/sec
    // Orbit Range, Follow Range  - npc tries to stay at this distance from active target
    m_flyRange = m_self->GetAttribute(AttrEntityFlyRange).get_uint32();    //AttrOrbitRange is 0 for npc
    if (!m_flyRange)
        m_flyRange = 0;
    // distance for Speed Boost activation  (this needs to be revisited)
    m_boostRange = m_self->GetAttribute(AttrEntityChaseMaxDistance).get_uint32();
    if (!m_boostRange)
        m_boostRange = 0;
    // some npcs have flyRange > boostRange.  this corrects it. (extends boost range)
    if (m_flyRange > m_boostRange)
        m_boostRange += m_boostRange + m_flyRange;
    // max firing range   default:10000  (lowest in db is 1000)
    m_maxAttackRange = m_self->GetAttribute(AttrEntityAttackRange).get_uint32();
    // this should be set according to npc size.
    if (m_maxAttackRange < 1000)
        m_maxAttackRange = 10000;

    // 'sight' range (undefined in db)
    float radius = m_self->GetAttribute(AttrRadius).get_float();
    if (radius < 30) {
        m_sightRange = 2500;
    } else if (radius < 60) {
        m_sightRange = 5000;
    } else if (radius < 150) {
        m_sightRange = 8000;
    } else if (radius < 280) {
        m_sightRange = 12000;
    } else if (radius < 550) {
        m_sightRange = 15000;
    } else {
        m_sightRange = 20000;
    }
    if (m_maxAttackRange > m_sightRange)
        m_sightRange = m_maxAttackRange *2;

    // how far group aggro (and remote repair) reaches
    m_assistRange = m_self->GetAttribute(AttrNpcAssistanceRange).get_uint32();

    // ship targets
    m_maxAttackTargets = m_self->GetAttribute(AttrMaxAttackTargets).get_uint32();
    if (m_maxAttackTargets < 1)
        m_maxAttackTargets = 1;
    m_maxLockedTargets = m_self->GetAttribute(AttrMaxLockedTargets).get_uint32();
    if (m_maxLockedTargets < 1) {
        if (m_maxAttackTargets > 1) {
            m_maxLockedTargets = m_maxAttackTargets;
        } else {
            m_maxLockedTargets = 1;
        }
    }

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

    // advanced AI variables  only used by sleepers for now (and on live).  will update advanced npcs to use these also
    if (m_self->HasAttribute(AttrAI_ShouldUseTargetSwitching)) {
        m_useTargSwitching = true;
    } else {
        m_useTargSwitching = false;
    }
    if (m_self->HasAttribute(AttrAI_ShouldUseSecondaryTarget)) {
        m_useSecondTarget = true;
    } else {
        m_useSecondTarget = false;
    }
    if (m_self->HasAttribute(AttrAI_ShouldUseSignatureRadius)) {
        m_useSigRadius = true;
        // "if it is 0 or below it uses the ship's signature radius instead"
        m_preferedSigRadius = m_self->GetAttribute(AttrAI_PreferredSignatureRadius).get_float();
        if (m_preferedSigRadius <= 0)
            m_preferedSigRadius = m_sigRadius;
    } else {
        m_useSigRadius = false;
        m_preferedSigRadius = 0;
    }
    if (m_self->HasAttribute(AttrAI_ChanceToNotTargetSwitch)) {
        m_switchTargChance = 1.0 - m_self->GetAttribute(AttrAI_ChanceToNotTargetSwitch).get_float();
    } else {
        m_switchTargChance = 0;
    }
    // how often the advanced ai looks for a better target
    m_switchTime = sConfig.npc.TargetSwitchTime *1000;
    if (m_self->HasAttribute(AttrTargetSwitchTimer))
        m_switchTime = m_self->GetAttribute(AttrTargetSwitchTimer).get_uint32();
    if (m_switchTime < 1000)
        m_switchTime = 1000;
    // fast orbit (emergent sleepers)
    m_evasive = (m_self->GetAttribute(AttrEvasiveManeuverLevel).get_float() > 0);

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

    /*
    AttrEntityEquipmentMin = 456,
    AttrEntityEquipmentMax = 457,
    */

    // ewar (scram, web, jam, neut, damp, td, paint) is in NPCEwar.  it's made on the first Process(), after LawEnforcement set our role

    // does this need to be running if there are no players in bubble?
    //  yes...npcs will warp out when no targets in sight range, but need a process tic to do that.
   // m_processTimer.Start(m_attackSpeed);

    // maybe this can be used to tell spawnMgr to respawn this npc as required....
    //    AttrEntityGroupRespawnChance = 640,
}

NPCAIMgr::~NPCAIMgr()
{
    SafeDelete(m_ewar);
}

void NPCAIMgr::Process() {
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

    if (m_warpOutTimer.Check(false)) {
        // an idle spawn moves on after a while.  not while its dead members are waiting for a respawn (chained)
        m_warpOutTimer.Disable();
        if (IsIdle() and (m_npc->GetSpawnMgr() != nullptr) and (m_npc->SysBubble() != nullptr)
        and !m_npc->GetSpawnMgr()->IsChaining(m_npc->SysBubble()->GetID()))
            WarpOut();
    }

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
            if (m_beginFindTarget.Check()) {
                SystemEntity* pTarget(ChooseTarget());
                if (pTarget != nullptr) {
                    Target(pTarget);
                    return;
                }
                if (sConfig.npc.IdleWander)
                    if (!m_isWandering)
                        SetWander();
            } else {
                if (!m_beginFindTarget.Enabled())
                    m_beginFindTarget.Start(m_attackSpeed);  //find target is based on npc attack speed.
            }
            if (!m_warpOutTimer.Enabled())
                StartWarpOutTimer();
        } break;
        case NPCAI::State::Chasing:
        case NPCAI::State::Following:
        case NPCAI::State::Engaged: {
            if (m_npc->TargetMgr()->HasNoTargets()) {
                _log(NPC__AI_TRACE, "%s(%u): Stopped %s - HasNoTargets = true.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state).c_str());
                SetIdle();
                return;
            }
            SystemEntity* pSE = GetPrimary();
            if (pSE == nullptr) {
                _log(NPC__AI_TRACE, "%s(%u): Stopped %s - no primary target.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state).c_str());
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
            // ewar needs a completed lock.  sleepers put theirs on a secondary target
            if (m_ewar != nullptr) {
                SystemEntity* pSecondary(nullptr);
                if (m_useSecondTarget and (m_secondaryID != 0))
                    pSecondary = m_npc->TargetMgr()->GetTarget(m_secondaryID, true);
                m_ewar->Process(m_npc->TargetMgr()->GetTarget(targetID, true), pSecondary);
            }
            if (m_missileTimer.Check())
                LaunchMissile(m_missileTypeID, pSE);
            if (m_useTargSwitching)
                ProcessAdvanced();
            ProcessDroneAggro();
            CheckLeash();
        } break;
        case NPCAI::State::WarpOut:
        case NPCAI::State::WarpFollow:{
            // still aligning.  (in warp we returned above; WarpOutComplete() ends this state)
            if (m_destiny->IsAligningForWarp())
                break;
            // the warp was stopped (scrambled while aligning).  we stay here
            _log(NPC__AI_TRACE, "%s(%u): %s was cut short.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state).c_str());
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

void NPCAIMgr::ProcessRepairs()
{
    // each cycle may be skipped on the type's delay chance
    if (m_shieldBoosterTimer.Check())
        if (MakeRandomFloat() >= m_shieldBoosterDelayChance)
            m_npc->UseShieldRecharge();

    if (m_armorRepairTimer.Check())
        if (MakeRandomFloat() >= m_armorRepairDelayChance)
            m_npc->UseArmorRepairer();
}

void NPCAIMgr::OnDamaged(SystemEntity* pSE, float amount)
{
    // self repair starts on damage (not on being locked) and runs until we're whole again
    if ((m_shieldBoosterDuration > 0) and !m_shieldBoosterTimer.Enabled())
        m_shieldBoosterTimer.Start(m_shieldBoosterDuration);
    if ((m_armorRepairDuration > 0) and !m_armorRepairTimer.Enabled())
        m_armorRepairTimer.Start(m_armorRepairDuration);

    if ((pSE == nullptr) or (pSE == m_npc) or m_npc->IsLawEnforcer())
        return;

    AddThreat(pSE, amount);
    // we're inside the attacker's ApplyDamage() call here, so the answer waits for our next Process()
    if (IsIdle() and (m_pendingAggroID == 0))
        m_pendingAggroID = AggroTarget(pSE)->GetID();
    AlertGroup(pSE);

    // sleepers share what they know, so the group focuses together
    if (m_useTargSwitching and (m_npc->SysBubble() != nullptr)) {
        std::map<uint32, SystemEntity*> entities;
        m_npc->SysBubble()->GetEntities(entities);
        for (auto cur : entities) {
            if ((cur.second == m_npc) or !cur.second->IsNPCSE())
                continue;
            NPC* pNPC(cur.second->GetNPCSE());
            if (pNPC->IsLawEnforcer() or (pNPC->GetCorporationID() != m_npc->GetCorporationID()))
                continue;
            pNPC->GetAIMgr()->ShareThreat(pSE, amount * 0.5f);
        }
    }
}

void NPCAIMgr::ShareThreat(SystemEntity* pSE, float amount)
{
    if (!m_useTargSwitching or m_npc->IsLawEnforcer())
        return;
    AddThreat(pSE, amount);
}

float NPCAIMgr::Score(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return 0.0f;
    float threat(GetThreat(pSE->GetID()));
    float score(1.0f + threat);
    // targets smaller than we like are worth less
    if (m_useSigRadius and (m_preferedSigRadius > 0)) {
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
    std::map<uint32, SystemEntity*> entities;
    m_npc->SysBubble()->GetEntities(entities);
    SystemEntity* pBest(nullptr);
    float bestScore(0.0f);
    double bestDistance(0.0);
    for (auto cur : entities) {
        if (!IsCandidate(cur.second, true))
            continue;
        double distance(m_npc->GetPosition().distance(cur.second->GetPosition()));
        if (distance > range)
            continue;
        float score(Score(cur.second));
        if ((pBest == nullptr) or (score > bestScore) or ((score == bestScore) and (distance < bestDistance))) {
            pBest = cur.second;
            bestScore = score;
            bestDistance = distance;
        }
    }
    return pBest;
}

void NPCAIMgr::ProcessAdvanced()
{
    // threat fades (10% every 10s)
    if (!m_threatDecayTimer.Enabled())
        m_threatDecayTimer.Start(10000);
    if (m_threatDecayTimer.Check()) {
        std::map<uint32, float>::iterator itr = m_threat.begin();
        while (itr != m_threat.end()) {
            itr->second *= 0.9f;
            if (itr->second < 1.0f) {
                itr = m_threat.erase(itr);
            } else {
                ++itr;
            }
        }
    }

    // a switch we decided on happens once the new lock completes
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
    if (!m_switchTimer.Enabled())
        m_switchTimer.Start(m_switchTime);
    if (m_switchTimer.Check() and (m_switchToID == 0) and (MakeRandomFloat() < m_switchTargChance)) {
        // only switch to targets within ThreatRadius x our targeting range
        SystemEntity* pBest(ChooseAdvancedTarget(m_sightRange * sConfig.npc.ThreatRadius));
        SystemEntity* pPrimary(GetPrimary());
        if ((pBest != nullptr) and (pBest != pPrimary) and ((pPrimary == nullptr) or (Score(pBest) > (Score(pPrimary) * 1.1f)))) {
            std::vector<SystemEntity*> targets;
            m_npc->TargetMgr()->GetTargetEntities(targets);
            if (std::find(targets.begin(), targets.end(), pBest) != targets.end()) {
                // already locked (or locking)
                m_switchToID = pBest->GetID();
            } else {
                // make room: drop our least interesting lock that isn't the primary
                if (targets.size() >= m_maxLockedTargets) {
                    SystemEntity* pWorst(nullptr);
                    for (auto cur : targets)
                        if ((cur != pPrimary) and ((pWorst == nullptr) or (Score(cur) < Score(pWorst))))
                            pWorst = cur;
                    if (pWorst != nullptr)
                        m_npc->TargetMgr()->ClearTarget(pWorst);
                }
                bool chase = false;
                if (m_npc->TargetMgr()->StartTargeting(pBest, GetTargetTime(), m_maxLockedTargets, m_sightRange, chase))
                    m_switchToID = pBest->GetID();
            }
        }
    }

    // ewar goes on the most interesting ship we have locked that isn't our primary
    m_secondaryID = 0;
    if (m_useSecondTarget and (m_ewar != nullptr)) {
        std::vector<SystemEntity*> targets;
        m_npc->TargetMgr()->GetTargetEntities(targets, true);
        float bestScore(0.0f);
        double range(m_ewar->GetMaxRange());
        for (auto cur : targets) {
            if ((cur->GetID() == m_primaryID) or !cur->IsShipSE() or !cur->HasPilot() or cur->GetPilot()->InPod())
                continue;
            if (m_npc->GetPosition().distance(cur->GetPosition()) > range)
                continue;
            float score(Score(cur));
            if ((m_secondaryID == 0) or (score > bestScore)) {
                m_secondaryID = cur->GetID();
                bestScore = score;
            }
        }
    }
}

void NPCAIMgr::ProcessRemoteRepairs()
{
    if ((m_rrAmount > 0) and !m_remoteArmorTimer.Enabled())
        m_remoteArmorTimer.Start(m_rrDuration);
    if ((m_rsAmount > 0) and !m_remoteShieldTimer.Enabled())
        m_remoteShieldTimer.Start(m_rsDuration);

    if ((m_rrAmount > 0) and m_remoteArmorTimer.Check() and (MakeRandomFloat() < m_rrChance))
        RemoteRepair(true);
    if ((m_rsAmount > 0) and m_remoteShieldTimer.Check() and (MakeRandomFloat() < m_rsChance))
        RemoteRepair(false);
}

void NPCAIMgr::RemoteRepair(bool armor)
{
    if ((m_npc->SysBubble() == nullptr) or (m_destiny == nullptr) or m_destiny->IsWarping())
        return;
    double range(m_assistRange > 0 ? m_assistRange : m_sightRange);
    float threshold(armor ? m_rrThreshold : m_rsThreshold);

    // friends (our corporation) in the bubble that need it: highest npcAssistancePriority, then the most damaged
    struct Patient { NPC* pNPC; float priority; float fraction; };
    std::vector<Patient> patients;
    std::map<uint32, SystemEntity*> entities;
    m_npc->SysBubble()->GetEntities(entities);
    for (auto cur : entities) {
        if ((cur.second == m_npc) or !cur.second->IsNPCSE() or cur.second->IsDead())
            continue;
        NPC* pNPC(cur.second->GetNPCSE());
        if (pNPC->IsLawEnforcer() or (pNPC->GetCorporationID() != m_npc->GetCorporationID()))
            continue;
        if (m_npc->GetPosition().distance(pNPC->GetPosition()) > range)
            continue;
        InventoryItemRef iRef(pNPC->GetSelf());
        float fraction(1.0f);
        if (armor) {
            float hp(iRef->GetAttribute(AttrArmorHP).get_float());
            if (hp <= 0)
                continue;
            fraction = 1.0f - (iRef->GetAttribute(AttrArmorDamage).get_float() / hp);
        } else {
            float capacity(iRef->GetAttribute(AttrShieldCapacity).get_float());
            if (capacity <= 0)
                continue;
            fraction = iRef->GetAttribute(AttrShieldCharge).get_float() / capacity;
        }
        // "0.1 means: must be below 90% armor to get repairs"
        if (fraction >= (1.0f - threshold))
            continue;
        Patient p = Patient();
            p.pNPC = pNPC;
            p.priority = iRef->GetAttribute(AttrNpcAssistanceProirity).get_float();
            p.fraction = fraction;
        patients.push_back(p);
    }
    if (patients.empty())
        return;
    std::sort(patients.begin(), patients.end(), [](const Patient& a, const Patient& b) {
        if (a.priority != b.priority)
            return (a.priority > b.priority);
        return (a.fraction < b.fraction);
    });

    uint8 count(0);
    uint8 maxTargets(armor ? m_rrMaxTargets : m_rsMaxTargets);
    for (auto& cur : patients) {
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
    m_warpOutTimer.Start(sConfig.npc.WarpOut *1000); // s to ms
}

void NPCAIMgr::WarpOut()
{
    m_warpOutTimer.Disable();
    if ((m_npc->GetSpawnMgr() == nullptr) or m_npc->IsLawEnforcer() or (m_npc->SysBubble() == nullptr))
        return;
    if ((m_state == NPCAI::State::WarpOut) or (m_state == NPCAI::State::WarpFollow))
        return;
    // a player has us pointed.  try again later
    if (m_npc->IsWarpScrambled()) {
        m_warpOutTimer.Start(30000);
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
    m_missileTimer.Disable();
    m_beginFindTarget.Disable();
    m_mainAttackTimer.Disable();
    m_chaseDelayTimer.Disable();
    m_chaseTimer.Disable();
    m_droneAggroTimer.Disable();
    m_warpOutTimer.Disable();
    m_primaryID = 0;
    m_secondaryID = 0;
    m_switchToID = 0;
    m_moveTargetID = 0;
    m_pendingAggroID = 0;
    m_switchTimer.Disable();
    m_threatDecayTimer.Disable();
    m_threat.clear();
    m_isWandering = false;
    m_destiny->SetMaxVelocity(m_maxSpeed);
    m_state = state;
}

void NPCAIMgr::WarpOutComplete()
{
    m_warpOutTimer.Disable();
    if ((m_state != NPCAI::State::WarpOut) and (m_state != NPCAI::State::WarpFollow))
        return;
    _log(NPC__AI_TRACE, "%s(%u): %s complete.", m_npc->GetName(), m_npc->GetID(), GetStateName(m_state).c_str());
    m_state = NPCAI::State::Idle;
    m_destiny->SetMaxVelocity(CruiseSpeed());
    // make sure our spawn manager has us in this bubble, and this is home now
    if ((m_npc->GetSpawnMgr() != nullptr) and (m_npc->SysBubble() != nullptr))
        m_npc->GetSpawnMgr()->MoveSpawn(m_npc, m_npc->SysBubble());
    m_home = m_npc->GetPosition();
    // look around right away
    m_beginFindTarget.Start(1000);
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
    if (MakeRandomFloat() >= sConfig.npc.WarpFollowChance)
        return false;

    GPoint destination(pSE->DestinyMgr()->GetTargetPoint());
    if (destination.isNaN() or (destination.distance(m_npc->GetPosition()) < 150000))
        return false;
    _log(NPC__AI_TRACE, "%s(%u): Following %s(%u) through warp.", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    BeginWarp(NPCAI::State::WarpFollow);
    m_destiny->WarpTo(destination, MakeRandomInt(2, 15) *1000);
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
    if (!m_useTargSwitching and !(pBubble->IsAnomaly() or pBubble->IsMission() or pBubble->IsIncursion()))
        return;
    if (m_npc->GetPosition().distance(m_home) <= sConfig.npc.LeashRange)
        return;

    _log(NPC__AI_TRACE, "%s(%u): Pulled %.0fm from home.  Giving up and going back.", \
            m_npc->GetName(), m_npc->GetID(), m_npc->GetPosition().distance(m_home));
    m_npc->TargetMgr()->ClearTargets();
    m_pendingAggroID = 0;
    SetIdle();
    m_destiny->SetMaxVelocity(m_maxSpeed);
    m_destiny->GotoPoint(m_home);
    m_leashTimer.Start(30000);
}

bool NPCAIMgr::IsReturning()
{
    if (!m_leashTimer.Enabled())
        return false;
    if (!m_leashTimer.Check(false) and (m_npc->GetPosition().distance(m_home) > 5000))
        return true;
    // home (or long enough).  back to normal
    m_leashTimer.Disable();
    m_destiny->Stop();
    m_destiny->SetMaxVelocity(CruiseSpeed());
    return false;
}

void NPCAIMgr::SetWander()
{
    if (m_npc->GetSpawnMgr() == nullptr)
        return;
    if (!m_isWandering) {
        _log(NPC__AI_TRACE, "%s(%u): Wandering:  No Targets within my sight range of %um", \
                m_npc->GetName(), m_npc->GetID(), m_sightRange);
        m_isWandering = true;
    }

    SystemBubble* pBubble = m_npc->SysBubble();

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
        uint16 orbitDistance = MakeRandomInt(10000, 20000);
        m_destiny->Orbit(pSE, orbitDistance);
        _log(NPC__AI_TRACE, "%s(%u):  Just for shits-n-giggles, I\'m gonna orbit %s(%u) at %um.", \
                m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), orbitDistance);
    } else {
        /** @todo  figure out a way for npc to wander 'aimlessly' around their bubble */
        m_destiny->Stop();
    }
}

void NPCAIMgr::SetIdle() {
    if (m_state == NPCAI::State::Idle)
        return;
    // not doing anything....idle.

    /** @todo need to clear out targets here */

    _log(NPC__AI_TRACE, "%s(%u): Idle: returning to idle.", m_npc->GetName(), m_npc->GetID());
    m_state = NPCAI::State::Idle;
    m_destiny->Stop();
    m_destiny->SetMaxVelocity(CruiseSpeed());

    // repair timers keep running until we're whole (they stop themselves)
    m_missileTimer.Disable();
    m_beginFindTarget.Disable();
    m_mainAttackTimer.Disable();
    m_chaseDelayTimer.Disable();
    m_chaseTimer.Disable();
    m_droneAggroTimer.Disable();
    if (m_ewar != nullptr)
        m_ewar->StopAll();
    m_primaryID = 0;
    m_secondaryID = 0;
    m_switchToID = 0;
    m_moveTargetID = 0;
    m_switchTimer.Disable();
    m_threatDecayTimer.Disable();
    m_threat.clear();

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
    m_warpOutTimer.Disable();
    if (wasChasing) {
        m_destiny->SetMaxVelocity(m_chaseFast ? m_maxSpeed : CruiseSpeed());
    } else {
        StartChaseDelay();
    }
}

void NPCAIMgr::StartChaseDelay()
{
    // may stall at cruise speed before chase speed kicks in
    m_chaseTimer.Disable();
    if ((m_chaseMaxDelay >= 1000) and (MakeRandomFloat() < m_chaseDelayChance)) {
        m_chaseFast = false;
        m_destiny->SetMaxVelocity(CruiseSpeed());
        m_chaseDelayTimer.Start(MakeRandomInt(1000, m_chaseMaxDelay));
        return;
    }
    StartChaseBoost();
}

void NPCAIMgr::StartChaseBoost()
{
    m_chaseDelayTimer.Disable();
    m_chaseFast = true;
    m_destiny->SetMaxVelocity(m_maxSpeed);
    if (m_chaseMaxDuration == 0)
        return;
    if (MakeRandomFloat() < m_chaseDurationChance) {
        m_chaseTimer.Start(m_chaseMaxDuration);
    } else {
        m_chaseTimer.Start(MakeRandomInt(m_chaseMaxDuration /4, m_chaseMaxDuration));
    }
}

void NPCAIMgr::ProcessChase()
{
    if (m_state != NPCAI::State::Chasing)
        return;
    if (m_chaseDelayTimer.Check(false)) {
        StartChaseBoost();
    } else if (m_chaseTimer.Check(false)) {
        // chase speed ran out.  stall again (or keep going)
        StartChaseDelay();
    }
}

void NPCAIMgr::SetFollowing(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    if ((m_state == NPCAI::State::Following) and (m_destiny->IsGoto() or m_destiny->IsFollowing()) and (m_moveTargetID == pSE->GetID()))
        return;
    _log(NPC__AI_TRACE, "%s(%u): Begin following.  Target is %s(%u).", \
         m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    // too close to chase, but to far to engage
    m_chaseDelayTimer.Disable();
    m_chaseTimer.Disable();
    m_moveTargetID = pSE->GetID();
    m_destiny->SetMaxVelocity(CruiseSpeed() *2);
    m_destiny->Follow(pSE, m_falloff);  //try to get inside falloff range
    m_state = NPCAI::State::Following;
    m_warpOutTimer.Disable();
}

void NPCAIMgr::SetEngaged(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    if ((m_state == NPCAI::State::Engaged) and m_destiny->IsOrbiting() and (m_moveTargetID == pSE->GetID()))
        return;
    _log(NPC__AI_TRACE, "%s(%u): Begin engaging.  Target is %s(%u).", \
         m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
    // actively fighting.  evasive npcs orbit fast
    m_chaseDelayTimer.Disable();
    m_chaseTimer.Disable();
    m_moveTargetID = pSE->GetID();
    m_destiny->SetMaxVelocity(m_evasive ? m_maxSpeed : CruiseSpeed());
    m_destiny->Orbit(pSE, m_optimalRange);  //try to get inside orbit range
    m_state = NPCAI::State::Engaged;
    m_warpOutTimer.Disable();
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
            m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), GetStateName(m_state).c_str(), dist, m_flyRange, m_boostRange);

    Attack(pSE);
}

void NPCAIMgr::Target(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    float targetTime = GetTargetTime();
    bool chase = false;

    if (!m_npc->TargetMgr()->StartTargeting(pSE, targetTime, m_maxLockedTargets, m_sightRange, chase)) {
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
    m_beginFindTarget.Disable();
    // a new fight, or our primary is gone
    if ((m_primaryID == 0) or (GetPrimary() == nullptr))
        SetPrimary(pSE);
    CheckDistance(pSE);

    if (!m_mainAttackTimer.Enabled())
        m_mainAttackTimer.Start(m_attackSpeed);

    if (!m_missileTimer.Enabled() and (m_launcherCycleTime > 100))
        m_missileTimer.Start(m_launcherCycleTime);
}

void NPCAIMgr::Targeted(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    if (m_npc->IsLawEnforcer() and (pSE->GetID() != m_npc->GetLawTarget()))
        return;
    double targetTime = GetTargetTime();

    _log(NPC__AI_TRACE, "%s(%u): Targeted by %s(%u) while %s.", \
            m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), GetStateName(m_state).c_str());

    switch(m_state) {
        case NPCAI::State::Idle: {
            if ((m_npc->GetEwar() != nullptr) and m_npc->GetEwar()->IsJammed())
                break;
            if (IsReturning())
                break;
            _log(NPC__AI_TRACE, "%s(%u): Begin Approaching and start Targeting sequence.", \
                    m_npc->GetName(), m_npc->GetID());
            SystemEntity* pTarget(AggroTarget(pSE));
            if (CanAggro(pTarget))
                Target(pTarget);
            AlertGroup(pSE);
        } break;
        case NPCAI::State::Chasing:
        case NPCAI::State::Following:
        case NPCAI::State::Engaged: {
            if (m_npc->IsLawEnforcer())
                break;
            // regular rats keep their primary.  lock the newcomer too, so we have someone to turn to
            if (!pSE->IsShipSE() or !CanAggro(pSE))
                break;
            bool chase = false;
            m_npc->TargetMgr()->StartTargeting(pSE, targetTime, m_maxLockedTargets, m_sightRange, chase);
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
                SetIdle();
            } else if (pSE->GetID() == m_primaryID) {
                // turn to whoever hurt us most of what we have locked
                SetPrimary(PickPrimary(pSE));
                _log(NPC__AI_TRACE, "%s(%u): Primary %s(%u) lost.  New primary is %u.", \
                        m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID(), m_primaryID);
            }
        }
    }
}

float NPCAIMgr::GetThreat(uint32 entityID)
{
    std::map<uint32, float>::iterator itr = m_threat.find(entityID);
    if (itr == m_threat.end())
        return 0.0f;
    return itr->second;
}

void NPCAIMgr::AddThreat(SystemEntity* pSE, float amount)
{
    if ((pSE == nullptr) or (amount <= 0.0f))
        return;
    m_threat[pSE->GetID()] += amount;
    // a drone's pilot is behind it
    if (pSE->IsDroneSE()) {
        SystemEntity* pController(pSE->GetDroneSE()->GetController());
        if (pController != nullptr)
            m_threat[pController->GetID()] += amount * 0.5f;
    }
}

void NPCAIMgr::OnEwarBy(SystemEntity* pSE)
{
    if ((pSE == nullptr) or m_npc->IsLawEnforcer())
        return;
    AddThreat(pSE, 50.0f);
    if (IsIdle() and (m_pendingAggroID == 0))
        m_pendingAggroID = AggroTarget(pSE)->GetID();
    AlertGroup(pSE);
}

void NPCAIMgr::Aggro(SystemEntity* pSE)
{
    if ((pSE == nullptr) or m_npc->IsLawEnforcer())
        return;
    if (IsIdle() and (m_pendingAggroID == 0))
        m_pendingAggroID = AggroTarget(pSE)->GetID();
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
    if (pSE->HasPilot() and pSE->GetPilot()->IsInvul())
        return false;
    return true;
}

bool NPCAIMgr::IsCandidate(SystemEntity* pSE, bool drones)
{
    if (!CanAggro(pSE))
        return false;
    if (pSE->IsDroneSE()) {
        if (!drones)
            return false;
    } else {
        // players' ships only
        if (!pSE->HasPilot())
            return false;
        if (pSE->GetPilot()->InPod()) {
            if (!sConfig.npc.TargetPod)
                return false;
            if (m_npc->SystemMgr()->GetSystemSecurityRating() > sConfig.npc.TargetPodSec)
                return false;
        }
    }
    return (m_npc->GetPosition().distance(pSE->GetPosition()) <= m_sightRange);
}

SystemEntity* NPCAIMgr::ChooseTarget()
{
    // advanced ai (sleepers) weighs threat and signature, and drones count
    if (m_useTargSwitching)
        return ChooseAdvancedTarget(m_sightRange);

    std::vector<Client*> clientVec;
    m_npc->SysBubble()->GetPlayers(clientVec);
    // regular rats go for the nearest ship they see
    SystemEntity* pTarget(nullptr);
    double nearest(0.0);
    for (auto cur : clientVec) {
        SystemEntity* pSE(cur->GetShipSE());
        if (!IsCandidate(pSE, false))
            continue;
        double distance(m_npc->GetPosition().distance(pSE->GetPosition()));
        if ((pTarget == nullptr) or (distance < nearest)) {
            pTarget = pSE;
            nearest = distance;
        }
    }
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
    std::vector<SystemEntity*> targets;
    m_npc->TargetMgr()->GetTargetEntities(targets);
    if (m_primaryID != 0)
        for (auto cur : targets)
            if (cur->GetID() == m_primaryID)
                return cur;
    // primary is gone
    SystemEntity* pSE(PickPrimary(nullptr));
    SetPrimary(pSE);
    return pSE;
}

SystemEntity* NPCAIMgr::PickPrimary(SystemEntity* pExclude)
{
    std::vector<SystemEntity*> targets;
    m_npc->TargetMgr()->GetTargetEntities(targets);
    SystemEntity* pBest(nullptr);
    float bestThreat(0.0f);
    double bestDistance(0.0);
    for (auto cur : targets) {
        if ((cur == pExclude) or cur->IsDead())
            continue;
        float threat(m_useTargSwitching ? Score(cur) : GetThreat(cur->GetID()));
        double distance(m_npc->GetPosition().distance(cur->GetPosition()));
        if ((pBest == nullptr) or (threat > bestThreat) or ((threat == bestThreat) and (distance < bestDistance))) {
            pBest = cur;
            bestThreat = threat;
            bestDistance = distance;
        }
    }
    return pBest;
}

void NPCAIMgr::SetPrimary(SystemEntity* pSE)
{
    uint32 newID(pSE == nullptr ? 0 : pSE->GetID());
    if (newID == m_primaryID)
        return;
    m_primaryID = newID;
    // a new target means a new chase (SetChasing() starts over on the next CheckDistance())
    m_chaseDelayTimer.Disable();
    m_chaseTimer.Disable();
    if (m_state == NPCAI::State::Chasing)
        m_state = NPCAI::State::Following;
}

void NPCAIMgr::AlertGroup(SystemEntity* pAggressor)
{
    if (!sConfig.npc.GroupAggro or (pAggressor == nullptr) or m_npc->IsLawEnforcer() or (m_npc->SysBubble() == nullptr))
        return;
    double range(m_assistRange > m_sightRange ? m_assistRange : m_sightRange);
    std::map<uint32, SystemEntity*> entities;
    m_npc->SysBubble()->GetEntities(entities);
    for (auto cur : entities) {
        if ((cur.second == m_npc) or !cur.second->IsNPCSE())
            continue;
        NPC* pNPC(cur.second->GetNPCSE());
        if (pNPC->IsLawEnforcer() or (pNPC->GetSpawnMgr() != m_npc->GetSpawnMgr()))
            continue;
        if ((m_npc->GetSpawnGroupID() != 0) and (pNPC->GetSpawnGroupID() != m_npc->GetSpawnGroupID()))
            continue;
        if (!pNPC->GetAIMgr()->IsIdle())
            continue;
        if (m_npc->GetPosition().distance(pNPC->GetPosition()) > range)
            continue;
        pNPC->GetAIMgr()->Aggro(pAggressor);
    }
}

void NPCAIMgr::ProcessDroneAggro()
{
    // Crucible rats now and then turned on drones that hurt them.  sleepers use threat instead
    if (m_useTargSwitching or (sConfig.npc.DroneAggroChance <= 0.0f) or m_npc->IsLawEnforcer())
        return;
    if (!m_droneAggroTimer.Enabled()) {
        m_droneAggroTimer.Start(30000);
        return;
    }
    if (!m_droneAggroTimer.Check())
        return;
    SystemEntity* pPrimary(GetPrimary());
    if ((pPrimary != nullptr) and pPrimary->IsDroneSE())
        return;
    for (auto cur : m_threat) {
        SystemEntity* pSE(m_npc->SystemMgr()->GetSE(cur.first));
        if ((pSE == nullptr) or !pSE->IsDroneSE() or !CanAggro(pSE))
            continue;
        if (MakeRandomFloat() >= sConfig.npc.DroneAggroChance)
            return;
        bool chase = false;
        if (m_npc->TargetMgr()->StartTargeting(pSE, GetTargetTime(), m_maxLockedTargets, m_sightRange, chase)) {
            _log(NPC__AI_TRACE, "%s(%u): Turning on drone %s(%u).", m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
            SetPrimary(pSE);
        }
        return;
    }
}

void NPCAIMgr::Attack(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return;
    if (m_mainAttackTimer.Check()) {
        // Check to see if the target still in the bubble (Client warped out)
        if (!m_npc->SysBubble()->InBubble(pSE->GetPosition())) {
            _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) no longer in bubble.  Clear target and move on",
                    m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
            m_missileTimer.Disable();
            ClearTarget(pSE);
            return;
        }
        if (pSE->DestinyMgr() == nullptr) {
            _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) has no destiny manager.  Clear target and move on",
                    m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
            m_missileTimer.Disable();
            ClearTarget(pSE);
            return;
        }
        // Check to see if the target is not cloaked:
        if (pSE->DestinyMgr()->IsCloaked()) {
            _log(NPC__AI_TRACE, "%s(%u): Target %s(%u) is cloaked.  Clear target and move on",
                    m_npc->GetName(), m_npc->GetID(), pSE->GetName(), pSE->GetID());
            m_missileTimer.Disable();
            ClearTarget(pSE);
            return;
        }
        if (m_npc->TargetMgr()->CanAttack())
            AttackTarget(pSE);
    }
}

void NPCAIMgr::ClearTarget(SystemEntity* pSE) {
    m_npc->TargetMgr()->ClearTarget(pSE);
    //m_npc->TargetMgr()->OnTarget(pSE, TargMgr::Mode::Lost);

    if (m_npc->TargetMgr()->HasNoTargets())
        SetIdle();
}

//also check for special effects and write code to implement them
//modifyTargetSpeedRange, modifyTargetSpeedChance
//entityWarpScrambleChance
void NPCAIMgr::AttackTarget(SystemEntity* pSE) {
    if (pSE == nullptr)
        return;
    // put checks here for point/tackle

    // effects are listed in EVE_Effects.h
    std::string guid = "effects.Laser"; // client looks for 'turret' in ship.ball.modules for 'effects.laser'
    uint32 gfxID = 0;
    if (m_self->HasAttribute(AttrGfxTurretID))// graphicID for turret for drone type ships
        gfxID = m_self->GetAttribute(AttrGfxTurretID).get_uint32();
    m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(),
                                 pSE->GetID(),0,guid,1,1,
                                 1,m_attackSpeed,0,gfxID);

    Damage d(m_npc,
             m_self,
             m_npc->GetKinetic(),
             m_npc->GetThermal(),
             m_npc->GetEM(),
             m_npc->GetExplosive(),
             m_formula.GetNPCToHit(m_npc, pSE),
             EVEEffectID::targetAttack
            );

    if (sConfig.npc.UseDamageMultiplier or m_npc->IsLawEnforcer())
        if (m_damageMultiplier > 0)
            d *= m_damageMultiplier;

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
    // ItemData( uint32 _typeID, uint32 _ownerID, uint32 _locationID, EVEItemFlags _flag, const char *_name = "", \
              const GPoint &_position = NULL_ORIGIN, const char *_customInfo = "", bool _contraband = false);
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
    if (MakeRandomFloat() < chance)
        LaunchMissile(265, pMissile); // defender missile
}

float NPCAIMgr::GetTargetTime()
{
    float targetTime = (m_self->GetAttribute(AttrScanSpeed).get_float());
    float radius = m_self->GetAttribute(AttrRadius).get_float();
    if (targetTime < 1) {
        if (radius < 30) {
            targetTime = 1500;
        } else if (radius < 60) {
            targetTime = 2500;
        } else if (radius < 150) {
            targetTime = 4000;
        } else if (radius < 280) {
            targetTime = 6000;
        } else if (radius < 550) {
            targetTime = 8000;
        } else {
            targetTime = 13000;
        }
    }
    return targetTime;
}

void NPCAIMgr::OnKilled()
{
    if (m_ewar != nullptr)
        m_ewar->StopAll();
}

void NPCAIMgr::DisableRepTimers(bool shield/*true*/, bool armor/*true*/)
{
    if (armor)
        m_armorRepairTimer.Disable();
    if (shield)
        m_shieldBoosterTimer.Disable();
}

std::string NPCAIMgr::GetStateName(int8 stateID)
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
