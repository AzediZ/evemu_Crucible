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
  */

#ifndef __NPC_SENTRY_AI_H
#define __NPC_SENTRY_AI_H

#include "system/SystemEntity.h"
#include "npc/ai/NpcRng.h"
#include "npc/ai/NpcSchedule.h"

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

    // called from Sentry::Killed()
    void OnKilled();

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
    // pSE wants a response while we're idle (lock-back, damage, group alert)
    void Respond(SystemEntity* pSE);

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

    Sentry* m_npc;
    NPCEwar* m_ewar;            // we own this.  sentries whose brain allows their type's ewar
    SentryBrain* m_brain;       // we own this.  made on the first Process() (LawEnforcement sets the law role after we're made)
    std::shared_ptr<const NpcProfile> m_profile;

    NpcSchedule m_sched;
    NpcRng m_rng;
};

#endif
