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
#include "npc/ai/NpcRng.h"
#include "npc/ai/NpcSchedule.h"
#include "npc/ai/ThreatTable.h"

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
class NpcBrain;
class SystemEntity;
struct NpcContext;
struct NpcProfile;

/* the npc ai state machine (doc/code_and_design_notes/npc-ai/PLAN.md 6.3).  the policy (whom to go after) is the
 *  npc's brain (npc/ai/NpcBrain.h), the data its profile (npc/ai/NpcProfile.h), the timing its schedule
 *  (npc/ai/NpcSchedule.h) and the rolls its own rng (npc/ai/NpcRng.h, seeded from npc.RngSeed and its itemID) */
class NPCAIMgr {
public:
    NPCAIMgr(NPC *who);
    ~NPCAIMgr();

    // this is called from NPC::Process() which is called from SystemManager::Process()
    void Process();

    void Target(SystemEntity* pTargSE);
    void Targeted(SystemEntity* pTargSE);
    void TargetLost(SystemEntity* pTargSE);
    // TargetManager finished a lock we started
    void OnLockComplete(SystemEntity* pTargSE);

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

    bool IsIdle()                                       { return (m_state == NPCAI::State::Idle); }
    bool IsFighting();
    // asleep in a bubble without players (npc.SleepEmptyBubbles)
    bool IsDormant() const                              { return m_dormant; }
    uint32 GetPrimaryID()                               { return m_primaryID; }
    float GetThreat(uint32 entityID)                    { return m_threat.Get(entityID); }
    const NpcProfile& GetProfile() const                { return *m_profile; }
    // this npc's random numbers (every ai roll, turret shots too)
    NpcRng& GetRng()                                    { return m_rng; }

    // npcAI methods
    void DisableWarpOutTimer()                          { m_sched.Stop(NpcAct::WarpOut); }
    // from DestinyManager::OnWarpExited()
    void WarpOutComplete();
    // our spawn group is warping away (SpawnMgr::WarpOutSpawn()), or we follow a target through warp
    void BeginWarp(int8 state = NPCAI::State::WarpOut);
    // where we were spawned.  the leash pulls us back here
    void SetHome(const GPoint& home)                    { m_home = home; }

    void LaunchMissile(uint16 typeID, SystemEntity* pTargSE);   // us to them
    void MissileLaunched(Missile* pMissile); // them to us

    // the state, schedule, locks and profile for /npcai (pViewer:  lock time to them)
    std::string Dump(SystemEntity* pViewer, bool verbose);
    static const char* GetStateName(int8 stateID);

    /* brain helpers (npc/ai/NpcBrain.h).  the mechanics a brain may use;  the policy is the brain's */
    // a ship we may pick on our own (players' ships in sight, pod rules).  drones only for advanced ai
    bool IsCandidate(SystemEntity* pSE, bool drones);
    // something that attacked us (or our group) that we can go after.  never pods unless npc.TargetPod allows them
    bool CanAggro(SystemEntity* pSE);
    // best candidate in the bubble: nearest for regular rats, best score for advanced ai
    SystemEntity* ChooseTarget();
    // whom to shoot back at: a drone's pilot when we can see him, else whoever it was
    SystemEntity* AggroTarget(SystemEntity* pSE);
    // next primary from our locks: most threat, then nearest
    SystemEntity* PickPrimary(SystemEntity* pExclude);
    // go after pSE on our next idle tick (first one wins)
    void QueueAggro(SystemEntity* pSE);
    // threat decay, target switching, secondary (ewar) target  (advanced ai)
    void ProcessAdvanced();
    // regular rats now and then turn on a drone that hurt them
    void ProcessDroneAggro();
    // (1 + threat) x how well the target's signature suits us.  drones that hurt us +25%
    float Score(SystemEntity* pSE);
    // the advanced ai's ewar target (a completed lock), nullptr if none
    SystemEntity* GetSecondary();

protected:
    // ms clock for the schedule
    static int64 Now();

    void Attack(SystemEntity* pTargSE);
    // clearLocks false:  only from TargetLost when nothing is left to clear
    void SetIdle(bool clearLocks = true);
    void WarpOut();
    void SetWander();
    void SetChasing(SystemEntity* pTargSE);
    void SetEngaged(SystemEntity* pTargSE);
    void ClearTarget(SystemEntity* pTargSE);
    void SetFollowing(SystemEntity* pTargSE);
    void AttackTarget(SystemEntity* pTargSE);
    void CheckDistance(SystemEntity* pTargSE);
    void FireMissile(SystemEntity* pTargSE);

    float GetTargetTime(SystemEntity* pTargSE);
    void ProcessRepairs();

    // the primary (weapons) target among our locks.  picks a new one if it's gone
    SystemEntity* GetPrimary();
    void SetPrimary(SystemEntity* pSE);
    void AddThreat(SystemEntity* pSE, float amount, uint8 kind);
    void AlertGroup(SystemEntity* pAggressor);

    SystemEntity* ChooseAdvancedTarget(double range);
    // NPCRemoteArmorRepair / NPCRemoteShieldBoost on friends
    void ProcessRemoteRepairs();
    void RemoteRepair(bool armor);

    /* sleep (npc.SleepEmptyBubbles):  true while dormant.  wakes when a player is in the bubble or we're locked or aggressed */
    bool CheckDormant();
    void Wake(const char* reason);

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
    float CruiseSpeed();

    // surface distance to pSE
    double SurfaceDistance(SystemEntity* pSE);

    int8 m_state;

private:
    NpcContext MakeContext();
    void CreateBrain();

    bool m_isWandering      :1;
    bool m_ewarChecked      :1;
    bool m_chaseFast        :1;
    bool m_dormant          :1;

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

    // today's movement numbers (phase 2 moves to the profile's orbitRange/boostRange, PLAN.md 5.5)
    uint32 m_armorRepairDuration;
    uint32 m_shieldBoosterDuration;
    uint32 m_chaseMaxDelay;
    uint32 m_chaseMaxDuration;
    uint32 m_boostRange;    // distance for Speed Boost activation
    uint32 m_flyRange;      // npc tries to stay at this distance from active target
    uint32 m_sightRange;
    uint32 m_assistRange;   // npcAssistanceRange.  how far group aggro (and remote repair) reaches.  0 = sight range

    uint32 m_primaryID;     // weapons target
    uint32 m_pendingAggroID;// someone who attacked us (or our group) while we were idle.  handled in Process()
    uint32 m_secondaryID;   // advanced ai ewar target
    uint32 m_switchToID;    // advanced ai: new primary once its lock completes
    uint32 m_moveTargetID;  // what we follow/orbit

    float m_armorRepairDelayChance;
    float m_shieldBoosterDelayChance;

    GPoint m_lastGoto;      // where we're chasing to
    GPoint m_home;          // where we spawned (or last arrived by warp)

    ThreatTable m_threat;   // entityID -> damage (and ewar) done to us

    NPC* m_npc;
    NPCEwar* m_ewar;            // we own this.  nullptr when our type has no ewar (or we're law enforcement)
    NpcBrain* m_brain;          // we own this.  made on the first Process()
    DestinyManager* m_destiny;
    InventoryItemRef m_self;
    std::shared_ptr<const NpcProfile> m_profile;

    NpcSchedule m_sched;
    NpcRng m_rng;

    // scratch space, reused (no allocation per tick)
    struct Patient { NPC* pNPC; float priority; float fraction; };
    std::vector<Patient> m_patients;
};

/* fires an npc-type missile (launcher = npc or sentry, self = its item for the missile multipliers) */
void LaunchEntityMissile(SystemEntity* pLauncher, InventoryItemRef self, uint16 typeID, SystemEntity* pTarget);

#endif
