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
    Updates:    Allan
*/

#ifndef __NPCAI_H_INCL__
#define __NPCAI_H_INCL__

#include "ship/Missile.h"
#include "ship/modules/TurretFormulas.h"

namespace NPCAI {
    namespace State {
        enum {
            Invalid     = -1,
            Idle        = 1,  // not doing anything....idle.
            Chasing     = 2,  // target out of range to attack/follow, but within npc sight range....use mwd/ab if equiped
            Following   = 3,  // too close to chase, but to far to engage
            Engaged     = 4,  // actively fighting
            Fleeing     = 5,  // running away
            Signaling   = 6,  // calling for help
            WarpOut     = 7,  // leaving bubble
            WarpFollow  = 8   // will follow warping ship to their destination (adv)
        };
    }
}

class NPC;
class NPCEwar;
class SystemEntity;
class Timer;
class EvilNumber;

class NPCAIMgr {
protected:
public:
    NPCAIMgr(NPC *who);
    ~NPCAIMgr();

    // this is called from NPC::Process() which is called from SystemManager::Process()
    void Process();

    void Target(SystemEntity* pTargSE);
    void Targeted(SystemEntity* pTargSE);
    void TargetLost(SystemEntity* pTargSE);

    void DisableRepTimers(bool shield=true, bool armor=true);

    // called from SystemEntity::ApplyDamage() when we take damage and survive
    void OnDamaged(SystemEntity* pSE, float amount);
    // for ewar modules (and neuts) used on us by players
    void OnEwarBy(SystemEntity* pSE);
    // a member of our spawn group was attacked by pSE.  we join in if we're idle
    void Aggro(SystemEntity* pSE);
    // another sleeper was hurt by pSE (advanced ai only)
    void ShareThreat(SystemEntity* pSE, float amount);
    // called from NPC::Killed()
    void OnKilled();

    // public methods to enable calls from other classes (namely, TurretFormulas.cpp)
    bool IsIdle()                                       { return (m_state == NPCAI::State::Idle); }
    bool IsFighting();
    uint32 GetOptimalRange()                            { return m_optimalRange; }
    uint16 GetSigRes()                                  { return m_sigResolution; }
    uint32 GetFalloff()                                 { return m_falloff; }
    uint32 GetAttackRange()                             { return m_maxAttackRange; }
    double GetTrackingSpeed()                           { return m_trackingSpeed; }
    uint32 GetPrimaryID()                               { return m_primaryID; }
    float GetThreat(uint32 entityID);

    // npcAI methods
    void DisableWarpOutTimer()                          { m_warpOutTimer.Disable(); }
    // from DestinyManager::OnWarpExited()
    void WarpOutComplete();
    // our spawn group is warping away (SpawnMgr::WarpOutSpawn()), or we follow a target through warp
    void BeginWarp(int8 state = NPCAI::State::WarpOut);
    // where we were spawned.  the leash pulls us back here
    void SetHome(const GPoint& home)                    { m_home = home; }

    void LaunchMissile(uint16 typeID, SystemEntity* pTargSE);   // us to them
    void MissileLaunched(Missile* pMissile); // them to us

protected:
    void Attack(SystemEntity* pTargSE);
    void SetIdle();
    void WarpOut();
    void SetWander();
    void SetChasing(SystemEntity* pTargSE);
    void SetEngaged(SystemEntity* pTargSE);
    void ClearTarget(SystemEntity* pTargSE);
    void SetFollowing(SystemEntity* pTargSE);
    void AttackTarget(SystemEntity* pTargSE);
    void CheckDistance(SystemEntity* pTargSE);

    float GetTargetTime();
    void ProcessRepairs();

    /* target selection */
    // a ship we may pick on our own (players' ships in sight, pod rules).  drones only for advanced ai
    bool IsCandidate(SystemEntity* pSE, bool drones);
    // something that attacked us (or our group) that we can go after
    bool CanAggro(SystemEntity* pSE);
    // best candidate in the bubble: nearest for regular rats
    SystemEntity* ChooseTarget();
    // whom to shoot back at: a drone's pilot when we can see him, else whoever it was
    SystemEntity* AggroTarget(SystemEntity* pSE);
    // the primary (weapons) target among our locks.  picks a new one if it's gone
    SystemEntity* GetPrimary();
    // next primary from our locks: most threat, then nearest
    SystemEntity* PickPrimary(SystemEntity* pExclude);
    void SetPrimary(SystemEntity* pSE);
    void AddThreat(SystemEntity* pSE, float amount);
    void AlertGroup(SystemEntity* pAggressor);
    void ProcessDroneAggro();

    /* advanced ai (types with AI_ShouldUseTargetSwitching: the sleepers) */
    // (1 + threat) x how well the target's signature suits us.  drones that hurt us +25%
    float Score(SystemEntity* pSE);
    SystemEntity* ChooseAdvancedTarget(double range);
    // threat decay, target switching, secondary (ewar) target
    void ProcessAdvanced();
    // NPCRemoteArmorRepair / NPCRemoteShieldBoost on friends
    void ProcessRemoteRepairs();
    void RemoteRepair(bool armor);

    /* movement */
    void StartWarpOutTimer();
    // maybe follow our last target through warp (<npc><WarpFollowChance>).  true if we went
    bool TryWarpFollow(SystemEntity* pSE);
    // too far from home: give up and go back
    void CheckLeash();
    // going home after the leash pulled us back.  we don't take on anyone until we're there (or 30s passed)
    bool IsReturning();

    /* chase speed: stall (cruise speed) up to entityChaseMaxDelay, then chase speed for up to entityChaseMaxDuration */
    void StartChaseDelay();
    void StartChaseBoost();
    void ProcessChase();
    uint16 CruiseSpeed()                                { return (m_orbitSpeed > 0 ? m_orbitSpeed : m_maxSpeed); }

    int8 m_state;

    std::string GetStateName(int8 stateID);

private:
    bool m_isWandering      :1;
    bool m_ewarChecked      :1;
    bool m_useSigRadius     :1;
    bool m_useTargSwitching :1;
    bool m_useSecondTarget  :1;
    bool m_chaseFast        :1;
    bool m_evasive          :1;

    float m_switchTargChance;   //fuzzy logic
    float m_preferedSigRadius;
    uint32 m_switchTime;    // ms between target re-evaluations (advanced ai)

    // remote repair (advanced ai)
    float m_rrAmount;
    float m_rrChance;
    float m_rrThreshold;
    uint32 m_rrDuration;
    uint8 m_rrMaxTargets;
    float m_rsAmount;
    float m_rsChance;
    float m_rsThreshold;
    uint32 m_rsDuration;
    uint8 m_rsMaxTargets;

    //these attributes are cached to reduce access times. (much faster but uses more memory)
    uint8 m_maxAttackTargets;
    uint8 m_maxLockedTargets;
    uint16 m_maxSpeed;
    uint16 m_attackSpeed;
    uint16 m_missileTypeID;
    uint16 m_launcherCycleTime;
    uint16 m_sigResolution;
    uint16 m_orbitSpeed;
    uint16 m_targetRange;   // max targeting range  default: m_maxAttackRange (unused)
    uint32 m_optimalRange;
    uint32 m_boostRange;    // distance for Speed Boost activation   default:2500
    uint32 m_armorRepairDuration;
    uint32 m_shieldBoosterDuration;
    uint32 m_chaseMaxDelay;
    uint32 m_chaseMaxDuration;
    uint32 m_assistRange;   // npcAssistanceRange.  how far group aggro (and remote repair) reaches.  0 = sight range

    uint32 m_sigRadius;
    uint32 m_falloff;// distance past maximum range at which accuracy has fallen by half
    uint32 m_flyRange;  // npc tries to stay at this distance from active target    default:500
    uint32 m_sightRange;
    uint32 m_maxAttackRange;// max firing range   default:15000

    uint32 m_primaryID;     // weapons target
    uint32 m_pendingAggroID;// someone who attacked us (or our group) while we were idle.  handled in Process()
    uint32 m_secondaryID;   // advanced ai ewar target
    uint32 m_switchToID;    // advanced ai: new primary once its lock completes
    uint32 m_moveTargetID;  // what we follow/orbit

    float m_armorRepairDelayChance;
    float m_shieldBoosterDelayChance;
    float m_chaseDelayChance;
    float m_chaseDurationChance;

    double m_trackingSpeed;
    float m_damageMultiplier;

    GPoint m_lastGoto;      // where we're chasing to
    GPoint m_home;          // where we spawned (or last arrived by warp)

    std::map<uint32, float> m_threat;   // entityID -> damage (and ewar) done to us

    NPC* m_npc;
    NPCEwar* m_ewar;            // we own this.  nullptr when our type has no ewar (or we're law enforcement)
    DestinyManager* m_destiny;
    InventoryItemRef m_self;

    TurretFormulas m_formula;

    Timer m_processTimer;
    Timer m_mainAttackTimer;
    Timer m_missileTimer;
    Timer m_shieldBoosterTimer;
    Timer m_armorRepairTimer;
    Timer m_beginFindTarget;
    Timer m_warpOutTimer;
    Timer m_chaseDelayTimer;
    Timer m_chaseTimer;
    Timer m_droneAggroTimer;
    Timer m_leashTimer;
    Timer m_switchTimer;
    Timer m_threatDecayTimer;
    Timer m_remoteArmorTimer;
    Timer m_remoteShieldTimer;
};

/* fires an npc-type missile (launcher = npc or sentry, self = its item for the missile multipliers) */
void LaunchEntityMissile(SystemEntity* pLauncher, InventoryItemRef self, uint16 typeID, SystemEntity* pTarget);

#endif