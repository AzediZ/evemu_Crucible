 /**
  * @name SentryAI.h
  *   Sentry AI class
  * @Author:    Allan
  * @date:      23 April 2017
  *
  *   npc ai plan (doc/code_and_design_notes/npc-ai/PLAN.md 6.6, phase 4):  the state machine of a gun that doesn't move.
  *   whom it shoots is its brain's call (npc/ai/SentryBrain.h:  Crimewatch 1 gate and station guns, pirate and deadspace
  *   sentries).  this class does the mechanics:  locks (capped by maxLockedTargets), an explicit primary, one drift-free
  *   NpcSchedule (think, turret, missile, retarget), the firing gates (completed lock, entityAttackRange), the type's ewar.
  *   the type's data comes from its NpcProfile and every roll from its own NpcRng (npc.RngSeed)
  *   pirate and deadspace sentries may be members of an aggro group (npc/ai/AggroGroup.h, plan 4.11, 6.5:  their dungeon
  *   spawn group, their room, a GM /spawn batch):  the group's alerts reach OnGroupAlert, and being locked, attacked, acquiring
  *   a target or dying alerts the group.  a passive group's sentry waits like its npcs (proximity, aggressors only)
  */

#ifndef __NPC_SENTRY_AI_H
#define __NPC_SENTRY_AI_H

#include "system/SystemEntity.h"
#include "npc/ai/NpcRng.h"
#include "npc/ai/NpcSchedule.h"

class AggroGroup;
class NPCEwar;
class Sentry;
class SentryBrain;
class SystemEntity;
struct NpcProfile;
struct SentryContext;

class SentryAI {
protected:
    enum State {
        Idle        = 1,  // not doing anything....idle.
        Engaged     = 2   // actively fighting
    };

public:
    SentryAI(Sentry *who);
    ~SentryAI();

    void Process();

    // lock pTarget and make it our primary
    void Target(SystemEntity* pTarget);
    // pBy locked us (TargetManager)
    void Targeted(SystemEntity* pBy);
    void TargetLost(SystemEntity* pTarget);
    // TargetManager finished a lock we started
    void OnLockComplete(SystemEntity* pTarget);
    // pBy damaged us and we survived (SystemEntity::ApplyDamage)
    void OnDamaged(SystemEntity* pBy, float amount);
    /* our room or group was attacked by pAggressor (phase 2's AggroGroup / DungeonMgr room aggro):  we join in if idle.
     *  law guns only take on eligible targets */
    void OnGroupAlert(SystemEntity* pAggressor);

    void ClearAllTargets();

    // called from Sentry::Killed().  pKiller:  the fatal blow's source (our group hears about it)
    void OnKilled(SystemEntity* pKiller);
    // we leave space without dying (GM /unspawn;  SystemManager::OnNPCRemoved):  ewar off, out of our group, no alert
    void OnRemoved();
    /* a player's ewar module or drone cycled on us (ActiveModule, DroneAI):  aggression like a lock or a shot.  we answer
     *  on our next think, our group (and linked groups) hear about it */
    void OnEwarBy(SystemEntity* pBy);

    // our aggro group (we join it;  nullptr leaves the old one).  DungeonMgr, GM /spawn
    void SetGroup(const std::shared_ptr<AggroGroup>& group);
    AggroGroup* GetGroup()                              { return m_group.get(); }

    bool IsIdle()                                       { return (m_state == State::Idle); }
    bool IsFighting()                                   { return (m_state != State::Idle); }
    uint32 GetPrimaryID()                               { return m_primaryID; }
    const NpcProfile& GetProfile() const                { return *m_profile; }
    // this sentry's random numbers (target picks, turret shots)
    NpcRng& GetRng()                                    { return m_rng; }

    // state, schedule, locks and profile for /npcai
    std::string Dump(SystemEntity* pViewer, bool verbose);

protected:
    static int64 Now();

    void SetIdle();
    void SetEngaged(SystemEntity* pTarget);
    void ClearTarget(SystemEntity* pTarget);
    // makes pTarget our primary (locking it when we don't have it yet).  false if it can't be locked
    bool SetPrimary(SentryContext& ctx, SystemEntity* pTarget);
    // validity, then weapons
    void CheckTarget(SentryContext& ctx, SystemEntity* pTarget);
    void Attack(SentryContext& ctx, SystemEntity* pTarget);
    void AttackTarget(SentryContext& ctx, SystemEntity* pTarget);
    void FireMissile(SystemEntity* pTarget);
    // pSE wants a response while we're idle (lock-back, damage, group alert):  queued for the next think
    void Respond(SystemEntity* pSE);
    // drops our locks on what the brain no longer lets us shoot
    void PruneLocks(SentryContext& ctx);
    // our group (and linked groups) hear about pAggressor (players' ships and drones only)
    void AlertGroup(SystemEntity* pAggressor);
    // passive groups:  a player ship or drone came within the group's aggro range of us.  true if we went after it
    bool CheckProximity(SentryContext& ctx);

    double GetTargetTime(SystemEntity* pTarget);
    // surface distance to pTarget
    double SurfaceDistance(SystemEntity* pTarget);

    // our primary among our targets, or a new one from our locks (or the bubble) when it's gone
    SystemEntity* GetPrimary(SentryContext& ctx);

    State m_state;

    static std::string GetStateName(State name);

private:
    SentryContext MakeContext();
    void CreateBrain();

    bool m_ewarChecked : 1;

    uint32 m_primaryID;     // weapons target
    uint32 m_pendingAggroID;// someone who locked, attacked or ewar'd us (or our room) while we were idle.  handled on the next think
    int64 m_lastLinkAlert;  // ms:  linked groups were last told

    Sentry* m_npc;
    NPCEwar* m_ewar;            // we own this.  sentries whose brain allows their type's ewar
    SentryBrain* m_brain;       // we own this.  made on the first Process() (LawEnforcement sets the law role after we're made)
    std::shared_ptr<const NpcProfile> m_profile;
    std::shared_ptr<AggroGroup> m_group;

    NpcSchedule m_sched;
    NpcRng m_rng;

    std::vector<SystemEntity*> m_stale;     // scratch, reused (PruneLocks)
};

#endif
