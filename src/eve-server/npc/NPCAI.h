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
#include "npc/ai/NpcProfile.h"
#include "npc/ai/NpcRng.h"
#include "npc/ai/NpcSchedule.h"
#include "npc/ai/NpcTelemetry.h"
#include "npc/ai/RecentDamage.h"
#include "npc/ai/ThreatTable.h"

namespace NPCAI {
    // doc/code_and_design_notes/planning/npc-ai/PLAN.md 6.3.  (dormant is a flag:  IsDormant())
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
class AmbientPilot;
class NPC;
class NPCEwar;
class NpcBrain;
class NpcMovement;
class NpcSupport;
class SystemEntity;
struct NpcContext;
struct NpcProfile;

/* the npc ai state machine (doc/code_and_design_notes/planning/npc-ai/PLAN.md 6.3).  the policy (whom to go after) is the
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
    // a cycle of ewar (or a neut) on us by a player's module or drone.  effect:  NpcMath::Effect (None = unknown)
    void OnEwarBy(SystemEntity* pSE, uint8 effect = 0);
    // pSE's ecm (module or drone) jammed us for durationMs.  jammerRange:  its optimal + falloff (from Ewar::StartJam)
    void OnJammedBy(SystemEntity* pSE, uint32 durationMs, double jammerRange);
    // pRepairer repaired hp on pPatient, a ship we have locked or threat on (NotifyRemoteAssist)
    void OnAssistObserved(SystemEntity* pRepairer, SystemEntity* pPatient, float hp);
    // a member of our aggro group was attacked by (or went after) pSE.  we join in if we're idle
    void Aggro(SystemEntity* pSE);
    // a member of our aggro group shares amount of its threat on pSE (level one, PLAN.md 5.7)
    void ShareThreat(SystemEntity* pSE, float amount);
    // called from NPC::Killed().  pKiller:  the fatal blow's source (our group hears about it)
    void OnKilled(SystemEntity* pKiller);
    /* we leave space without dying (GM /unspawn, a despawn, a hauler that got away;  SystemManager::OnNPCRemoved):  our ewar
     *  and support stop, our group stops counting us (no alert) */
    void OnRemoved();
    // our idle spawn leaves for good (SpawnMgr::VanishSpawn, npc.WarpOutVanishChance):  off grid, gone when we land
    void Vanish();
    /* level of detail (npc/ai/NpcLod.h):  something to do that can't wait for the slow think of npcs away from players
     *  (not idle, law enforcement, something queued) */
    bool IsBusy();
    // our last turret shots with every term (fairness audit, /npcai verbose)
    const NpcShotLog& GetShotLog() const                { return m_shots; }

    bool IsIdle()                                       { return (m_state == NPCAI::State::Idle); }
    /* idle with nothing going on:  no queued aggressor or group join (a chase after an attacker out of lock range is idle
     *  too), no locks either way.  a spawn only moves on or vanishes when every member is (WarpOut, SpawnMgr) */
    bool IsQuiet();
    bool IsFighting();
    // asleep in a bubble without players (npc.SleepEmptyBubbles)
    bool IsDormant() const                              { return m_dormant; }
    uint32 GetPrimaryID()                               { return m_primaryID; }
    float GetThreat(uint32 entityID)                    { return m_threat.Get(entityID); }
    ThreatTable& GetThreatTable()                       { return m_threat; }
    const NpcProfile& GetProfile() const                { return *m_profile; }
    // this npc's random numbers (every ai roll, turret shots too)
    NpcRng& GetRng()                                    { return m_rng; }
    NpcMovement* GetMovement()                          { return m_move; }

    /* spawn setup (SpawnMgr, DungeonMgr, GM /spawn) */
    // our aggro group (we join it;  nullptr leaves the old one)
    void SetGroup(const std::shared_ptr<AggroGroup>& group);
    AggroGroup* GetGroup()                              { return m_group.get(); }
    void SetRole(uint8 role)                            { m_role = role; }
    /* ambient npcs (npc/ambient, ambient npc plan 4.3).  the brain (we own it) before our first Process();  the pilot flies the
     *  ship while we are idle:  no wander, leash, warp-out timer or sleep, and npc targets it allows */
    void SetBrain(NpcBrain* pBrain);
    void SetAmbient(AmbientPilot* pPilot)               { m_ambient = pPilot; }
    AmbientPilot* GetAmbient()                          { return m_ambient; }
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

    /* what other npcs do for us (npc/ai/NpcSupport.h) */
    // remote armor repair on its way to us (it lands when the repairer's cycle ends):  reserved, so no one else covers it again
    float ReservedArmor(int64 now) const;
    void ReserveArmor(float amount, int64 until);
    // a group assist (NpcAI::Buff) on us:  x mult for durationMs (a different one of the same kind replaces it)
    void ApplyBuff(uint8 kind, float mult, uint32 durationMs);
    // ms (GetSteadyTime) of the last damage we took
    int64 GetLastDamagedAt() const                      { return m_lastDamagedAt; }

    // the ewar effect kind (NpcMath::Effect) of a module group (None when it isn't ewar)
    static uint8 EwarEffectOfGroup(uint16 groupID);
    /* Ewar::StartJam():  pSource's ecm (moduleID, 0 = pSource itself) jammed pTarget.  an npc target is told who did it and
     *  how far that jammer reaches (level-one threat and jam response) */
    static void JamStarted(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget, uint32 durationMs);

    void LaunchMissile(uint16 typeID, SystemEntity* pTargSE);   // us to them
    void MissileLaunched(Missile* pMissile); // them to us

    // the state, schedule, locks and profile for /npcai (pViewer:  lock time to them)
    std::string Dump(SystemEntity* pViewer, bool verbose);
    static const char* GetStateName(int8 stateID);

    /* brain helpers (npc/ai/NpcBrain.h).  the mechanics a brain may use;  the policy is the brain's */
    /* a ship (or player drone) we may pick on our own within what we can lock (R_acq x the dampeners on us;  undamped:  R_acq,
     *  we'd close in first).  pod rules */
    bool IsCandidate(SystemEntity* pSE, bool drones, bool undamped = false);
    // something that attacked us (or our group) that we can go after.  never pods unless npc.TargetPod allows them
    bool CanAggro(SystemEntity* pSE);
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
    /* the standard drone rule (4.2, N2):  every npc.DroneAggroInterval, each drone that hurt us in the last 30s (most damage
     *  first) gets a DroneAggroChance roll;  the first success becomes our primary.  not while our primary's pilot hurt us */
    void ProcessDroneRoll();
    // reaction delay for a first activation or a group join (5.6), ms.  0 when npc.UseAttackDelay is off
    double DrawReactionDelay();
    // damage pSE did to us in the last 30s
    float RecentDamageBy(uint32 entityID);
    // m.  R_acq x the sensor dampeners on us:  how far we can lock
    double LockRange();
    // m.  R_acq (5.4)
    double AcquireRange() const                         { return m_acquireRange; }
    // threat on pSE, as it is (the brain decides how much, NpcBrain::OnDamage etc.)
    void AddThreat(SystemEntity* pSE, float amount);
    // start a lock on pSE (false:  can't, e.g. jammed, out of range or no free slot)
    bool StartLock(SystemEntity* pSE);
    // the same, telling why it can't (chase:  out of range) and counting it (telemetry)
    bool BeginLock(SystemEntity* pSE, bool& chase);
    // drop our lock on pSE without TargetLost's repick (the primary and pending switch go with it)
    void DropLock(SystemEntity* pSE);
    // a deliberate switch to pSE (a lock we have):  the switch timer starts over
    void SwitchPrimary(SystemEntity* pSE);
    // the new primary once its lock completes (0 = none)
    uint32 GetSwitchToID() const                        { return m_switchToID; }
    void SetSwitchTo(uint32 entityID)                   { m_switchToID = entityID; }
    // ms of the last deliberate switch (or the start of the fight)
    int64 GetLastSwitch() const                         { return m_lastSwitch; }
    // surface distance to pSE
    double SurfaceDistance(SystemEntity* pSE);

protected:
    // ms clock for the schedule
    static int64 Now();

    void Attack(SystemEntity* pTargSE);
    // clearLocks false:  only from TargetLost when nothing is left to clear
    void SetIdle(bool clearLocks = true);
    void WarpOut();
    void SetWander();
    // what an idle belt rat orbits:  a random asteroid of its bubble, or now and then the belt itself (phase 5)
    SystemEntity* PickWanderTarget(SystemBubble* pBubble);
    void ClearTarget(SystemEntity* pTargSE);
    void AttackTarget(SystemEntity* pTargSE);
    // too far (R_acq) or move toward it, then shoot (turrets)
    void CheckDistance(SystemEntity* pTargSE);
    // locked:  we have a completed lock on pTargSE
    void FireMissile(SystemEntity* pTargSE, bool locked);
    // the first activation of this fight waited its reaction delay (5.6)
    bool Reacted(int64 now);
    // our ewar on the primary (pLocked:  our completed lock on it, or nullptr) or the brain's secondaries
    void ProcessEwar(NpcContext& ctx, SystemEntity* pLocked, int64 now);

    float GetTargetTime(SystemEntity* pTargSE);
    void ProcessRepairs();

    // the primary (weapons) target among our locks.  picks a new one if it's gone
    SystemEntity* GetPrimary();
    void SetPrimary(SystemEntity* pSE);
    // our group (and linked groups) hear about pAggressor
    void AlertGroup(SystemEntity* pAggressor);
    // passive groups:  a player ship or drone came too close to us
    bool CheckProximity();

    // group assists on us:  expire them
    void ProcessBuffs(int64 now);
    // turns a group assist's effect on (or off)
    void SetBuff(uint8 kind, float mult, bool on);

    /* sleep (npc.SleepEmptyBubbles):  true while dormant.  wakes when a player is in the bubble or we're locked or aggressed */
    bool CheckDormant();
    void Wake(const char* reason);

    /* movement */
    void StartWarpOutTimer();
    // maybe follow our last target through warp (<npc><WarpFollowChance>).  true if we went
    bool TryWarpFollow(SystemEntity* pSE);
    /* the data leash (5.4):  soft past entityMaxWanderRange x LeashSoftScale, hard past x LeashHardScale.  true if we turned back */
    bool CheckLeash(int64 now);
    /* idle out past the leash (the fight ended there:  the target warped off or outran us, a soft leash was cut short):
     *  fly home, still able to answer an attack.  true while we're on our way */
    bool IdleGoHome();
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
    bool m_leashWarp        :1;     // WarpOut is the hard leash's warp home (cut short:  fly;  landing:  home stays)
    bool m_buffsActive      :1;     // a group assist is on us
    bool m_vanishing        :1;     // our spawn leaves for good (Vanish()):  gone when we land
    bool m_telegraph        :1;     // we turned toward a group join's target while its reaction delay runs (npc.TelegraphJoin)
    bool m_idleHome         :1;     // idle out past our leash (the fight ended there):  flying home (5.4)

    uint8 m_role;                   // NPCAI::Role

    uint32 m_acquireRange;  // R_acq (5.4)

    uint32 m_primaryID;     // weapons target
    uint32 m_pendingAggroID;// someone who attacked us (or our group) while we were idle.  handled in Process()
    uint32 m_secondaryID;   // level-one ewar target (the first of the brain's EwarTargets, for /npcai)
    uint32 m_switchToID;    // new primary once its lock completes (level-one switch, standard drone switch)
    uint32 m_jammerID;      // who jammed us last (level-one jam response)
    double m_jammerRange;   // m.  its ecm optimal + falloff

    int64 m_pendingAt;      // ms:  the queued aggro happens at or after this
    int64 m_softLeashAfter; // ms:  no soft leash before this (it turned back a moment ago)
    int64 m_lastLinkAlert;  // ms:  linked groups were last told
    int64 m_lastSwitch;     // ms:  the last deliberate target switch (or the start of the fight)
    int64 m_lastDamagedAt;  // ms:  the last damage we took

    GPoint m_home;          // where we spawned (or last arrived by warp)

    ThreatTable m_threat;   // entityID -> damage (and ewar) done to us
    RecentDamage m_recent;  // entityID -> damage in the last 30s

    NPC* m_npc;
    NPCEwar* m_ewar;            // we own this.  nullptr when our type has no ewar (law enforcement:  LawEnforcement::UsesTypeEwar())
    NpcBrain* m_brain;          // we own this.  made on the first Process()
    NpcMovement* m_move;        // we own this
    NpcSupport* m_support;      // we own this.  nullptr when our type has no remote repair, group assist or remote ecm
    AmbientPilot* m_ambient;    // we do not own this (the ambient npc does).  nullptr for every other npc
    DestinyManager* m_destiny;
    InventoryItemRef m_self;
    std::shared_ptr<const NpcProfile> m_profile;
    std::shared_ptr<AggroGroup> m_group;

    NpcSchedule m_sched;
    NpcRng m_rng;
    NpcShotLog m_shots;

    // remote armor repairs reserved on us (5.9)
    struct Reservation { float amount; int64 until; };
    static constexpr size_t c_reservations = 4;
    Reservation m_reserved[c_reservations];
    // group assists on us (NpcAI::Buff)
    struct BuffState { float mult; int64 until; };
    BuffState m_buffs[NpcAI::Buff::Count];
};

/* fires an npc-type missile (launcher = npc or sentry, self = its item for the missile multipliers).  damageMult:  the
 *  difficulty preset's (npc.Difficulty) */
void LaunchEntityMissile(SystemEntity* pLauncher, InventoryItemRef self, uint16 typeID, SystemEntity* pTarget, float damageMult = 1.0f);

/* pRepairer (a player's ship or drone) repaired hp on pPatient (remote armor repair, shield transfer, remote hull repair,
 *  repair drones).  level-one npcs that have pPatient locked or threat on it count it as threat on pRepairer (PLAN.md 5.7,
 *  owner decision 15) */
void NotifyRemoteAssist(SystemEntity* pRepairer, SystemEntity* pPatient, float hp);

#endif
