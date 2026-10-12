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
#include "npc/ai/RecentDamage.h"
#include "npc/ai/ThreatTable.h"

namespace NPCAI {
    // doc/code_and_design_notes/npc-ai/PLAN.md 6.3.  (dormant is a flag:  IsDormant())
    namespace State {
        enum {
            Invalid     = -1,
            Idle        = 1,  // nothing to do:  wander (belts) or hold (dungeons), look for targets on the think cadence
            Acquiring   = 2,  // primary chosen, locking it.  moving already
            Engaged     = 4,  // primary locked:  movement, weapons, ewar, brain
            Fleeing     = 5,  // hauler or commander warping away (PLAN.md 4.4)
            Returning   = 6,  // the leash pulled us back home (soft:  keeps its locks for 5-10s;  hard:  drops everything)
            WarpOut     = 7,  // group warp to another belt, or the hard leash's warp home
            WarpFollow  = 8   // following a target through warp (npc.WarpFollowChance, never in Crucible)
        };
    }
    // what the spawn made us (SpawnMgr, from the spawn class row:  hauler, commander ship classes)
    namespace Role {
        enum : uint8 {
            None        = 0,
            Hauler      = 1,  // warps off HaulerFleeDelay after a player arrives, or at once when hurt;  gone on arrival
            Commander   = 2   // warps to another belt when its escorts are dead (npc.CommanderFlee)
        };
    }
}

class AggroGroup;
class NPC;
class NPCEwar;
class NpcBrain;
class NpcMovement;
class SystemEntity;
struct NpcContext;
struct NpcProfile;

/* the npc ai state machine (doc/code_and_design_notes/npc-ai/PLAN.md 6.3).  the policy (whom to go after) is the
 *  npc's brain (npc/ai/NpcBrain.h), the data its profile (npc/ai/NpcProfile.h), the timing its schedule
 *  (npc/ai/NpcSchedule.h), the rolls its own rng (npc/ai/NpcRng.h, seeded from npc.RngSeed and its itemID),
 *  how it flies its movement (npc/ai/NpcMovement.h) and who it fights with its aggro group (npc/ai/AggroGroup.h) */
class NPCAIMgr {
public:
    NPCAIMgr(NPC *who);
    ~NPCAIMgr();

    // this is called from NPC::Process() which is called from SystemManager::Process()
    void Process();

    // go after pTargSE:  lock it and start moving.  joined:  a group join that already waited its reaction delay
    void Target(SystemEntity* pTargSE, bool joined = false);
    void Targeted(SystemEntity* pTargSE);
    void TargetLost(SystemEntity* pTargSE);
    // TargetManager finished a lock we started
    void OnLockComplete(SystemEntity* pTargSE);

    void DisableRepTimers(bool shield=true, bool armor=true);

    // called from SystemEntity::ApplyDamage() when we take damage and survive
    void OnDamaged(SystemEntity* pSE, float amount);
    // for ewar modules (and neuts) used on us by players
    void OnEwarBy(SystemEntity* pSE);
    // a member of our aggro group was attacked by (or went after) pSE.  we join in if we're idle
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
    NpcMovement* GetMovement()                          { return m_move; }

    /* spawn setup (SpawnMgr, DungeonMgr, GM /spawn) */
    // our aggro group (we join it;  nullptr leaves the old one)
    void SetGroup(const std::shared_ptr<AggroGroup>& group);
    AggroGroup* GetGroup()                              { return m_group.get(); }
    void SetRole(uint8 role)                            { m_role = role; }
    uint8 GetRole() const                               { return m_role; }
    // we came with a dungeon wave:  our first target may be the weakest ship (npc.WaveWeakestChance)
    void SetWave(bool wave = true)                      { m_wave = wave; }
    bool IsWave() const                                 { return m_wave; }

    // npcAI methods
    void DisableWarpOutTimer()                          { m_sched.Stop(NpcAct::WarpOut); }
    // from DestinyManager::OnWarpExited()
    void WarpOutComplete();
    // our spawn group is warping away (SpawnMgr::WarpOutSpawn()), we flee, or we follow a target through warp
    void BeginWarp(int8 state = NPCAI::State::WarpOut);
    // where we were spawned.  the leash pulls us back here
    void SetHome(const GPoint& home)                    { m_home = home; }

    void LaunchMissile(uint16 typeID, SystemEntity* pTargSE);   // us to them
    void MissileLaunched(Missile* pMissile); // them to us

    // the state, schedule, locks and profile for /npcai (pViewer:  lock time to them)
    std::string Dump(SystemEntity* pViewer, bool verbose);
    static const char* GetStateName(int8 stateID);

    /* brain helpers (npc/ai/NpcBrain.h).  the mechanics a brain may use;  the policy is the brain's */
    // a ship (or player drone) we may pick on our own within R_acq (pod rules)
    bool IsCandidate(SystemEntity* pSE, bool drones);
    // something that attacked us (or our group) that we can go after.  never pods unless npc.TargetPod allows them
    bool CanAggro(SystemEntity* pSE);
    // the legacy pick:  nearest ship for regular rats, best score for advanced ai (LegacyBrain)
    SystemEntity* ChooseTarget();
    /* the standard pick (4.2, 4.3):  the nearest player ship (or player drone when drones) within R_acq, or the weakest ship
     *  (least shield + armor + hull left, never a drone).  aggressorsOnly:  only pilots that group remembers */
    SystemEntity* ChooseStandardTarget(bool drones, bool weakest, AggroGroup* aggressorsOnly);
    // whom to shoot back at: a drone's pilot when we can see him, else whoever it was
    SystemEntity* AggroTarget(SystemEntity* pSE);
    // next primary from our locks: most threat, then nearest
    SystemEntity* PickPrimary(SystemEntity* pExclude);
    /* go after pSE on our next idle tick at or after atMs (first one wins).  joined:  this is a group join that waited
     *  its reaction delay already (no second delay before the first shot) */
    void QueueAggro(SystemEntity* pSE, int64 atMs = 0, bool joined = false);
    // threat decay, target switching, secondary (ewar) target  (advanced ai)
    void ProcessAdvanced();
    /* the standard drone rule (4.2, N2):  every npc.DroneAggroInterval, each drone that hurt us in the last 30s (most damage
     *  first) gets a DroneAggroChance roll;  the first success becomes our primary.  not while our primary's pilot hurt us */
    void ProcessDroneRoll();
    // (1 + threat) x how well the target's signature suits us.  drones that hurt us +25%
    float Score(SystemEntity* pSE);
    // the advanced ai's ewar target (a completed lock), nullptr if none
    SystemEntity* GetSecondary();
    // reaction delay for a first activation or a group join (5.6), ms.  0 when npc.UseAttackDelay is off
    double DrawReactionDelay();
    // damage pSE did to us in the last 30s
    float RecentDamageBy(uint32 entityID);

protected:
    // ms clock for the schedule
    static int64 Now();

    void Attack(SystemEntity* pTargSE);
    // clearLocks false:  only from TargetLost when nothing is left to clear
    void SetIdle(bool clearLocks = true);
    void WarpOut();
    void SetWander();
    void ClearTarget(SystemEntity* pTargSE);
    void AttackTarget(SystemEntity* pTargSE);
    // too far (R_acq) or move toward it, then shoot (turrets)
    void CheckDistance(SystemEntity* pTargSE);
    // locked:  we have a completed lock on pTargSE
    void FireMissile(SystemEntity* pTargSE, bool locked);
    // the first activation of this fight waited its reaction delay (5.6)
    bool Reacted(int64 now);

    float GetTargetTime(SystemEntity* pTargSE);
    void ProcessRepairs();

    // the primary (weapons) target among our locks.  picks a new one if it's gone
    SystemEntity* GetPrimary();
    void SetPrimary(SystemEntity* pSE);
    void AddThreat(SystemEntity* pSE, float amount, uint8 kind);
    // our group (and linked groups) hear about pAggressor
    void AlertGroup(SystemEntity* pAggressor);
    // passive groups:  a player ship or drone came too close to us
    bool CheckProximity();

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
    /* the data leash (5.4):  soft past entityMaxWanderRange x LeashSoftScale, hard past x LeashHardScale.  true if we turned back */
    bool CheckLeash(int64 now);
    void BeginReturn(bool hard, int64 now);
    void ProcessReturn(int64 now);
    // the soft leash is over:  back to the fight
    void EndReturn(int64 now);

    /* haulers and commanders (4.4) */
    void CheckFlee(int64 now);
    // warp away:  haulers off grid (and gone), commanders to another belt
    bool Flee();
    // a belt bubble without a spawn to warp to (not ours).  false if there is none
    bool FindBelt(SystemBubble*& pDest, GPoint& where);

    // surface distance to pSE
    double SurfaceDistance(SystemEntity* pSE);

    int8 m_state;

private:
    NpcContext MakeContext();
    void CreateBrain();

    bool m_isWandering      :1;
    bool m_ewarChecked      :1;
    bool m_dormant          :1;
    bool m_wave             :1;     // a dungeon wave's npc, before its first target
    bool m_pendingJoined    :1;     // the queued aggro is a group join (its reaction delay is done)
    bool m_returnHard       :1;     // Returning:  the hard leash
    bool m_damaged          :1;     // hurt since we spawned (haulers flee at once)
    bool m_fleeWanted       :1;     // a hauler's flee timer ran out (or it was hurt)
    bool m_hadEscorts       :1;     // a commander saw escorts in its group
    bool m_suppressLost     :1;     // we drop a lock ourselves (drone switch):  TargetLost leaves us alone

    uint8 m_role;                   // NPCAI::Role

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

    uint32 m_acquireRange;  // R_acq (5.4)
    uint32 m_assistRange;   // npcAssistanceRange.  how far remote repair reaches.  0 = R_acq

    uint32 m_primaryID;     // weapons target
    uint32 m_pendingAggroID;// someone who attacked us (or our group) while we were idle.  handled in Process()
    uint32 m_secondaryID;   // advanced ai ewar target
    uint32 m_switchToID;    // new primary once its lock completes (advanced ai switch, standard drone switch)

    int64 m_pendingAt;      // ms:  the queued aggro happens at or after this
    int64 m_softLeashAfter; // ms:  no soft leash before this (it turned back a moment ago)
    int64 m_lastLinkAlert;  // ms:  linked groups were last told

    GPoint m_home;          // where we spawned (or last arrived by warp)

    ThreatTable m_threat;   // entityID -> damage (and ewar) done to us
    RecentDamage m_recent;  // entityID -> damage in the last 30s

    NPC* m_npc;
    NPCEwar* m_ewar;            // we own this.  nullptr when our type has no ewar (or we're law enforcement)
    NpcBrain* m_brain;          // we own this.  made on the first Process()
    NpcMovement* m_move;        // we own this
    DestinyManager* m_destiny;
    InventoryItemRef m_self;
    std::shared_ptr<const NpcProfile> m_profile;
    std::shared_ptr<AggroGroup> m_group;

    NpcSchedule m_sched;
    NpcRng m_rng;

    // scratch space, reused (no allocation per tick)
    struct Patient { NPC* pNPC; float priority; float fraction; };
    std::vector<Patient> m_patients;
};

/* fires an npc-type missile (launcher = npc or sentry, self = its item for the missile multipliers) */
void LaunchEntityMissile(SystemEntity* pLauncher, InventoryItemRef self, uint16 typeID, SystemEntity* pTarget);

#endif
