 /**
  * @name SentryAI.h
  *   Sentry AI class
  * @Author:    Allan
  * @date:      23 April 2017
  *
  *   npc ai plan phases 0-1:  the type's data comes from its NpcProfile, every cycle from one drift-free NpcSchedule
  *    (the think cadence, not the turret cycle:  bug B1), an explicit primary, locks capped by maxLockedTargets (B9),
  *    a float damage multiplier (B9), law sentries never lock back an innocent (B9).  phase 4 unifies this with the npc
  *    combat core (PLAN.md 6.6)
  */

#ifndef __NPC_SENTRY_AI_H
#define __NPC_SENTRY_AI_H

#include "system/SystemEntity.h"
#include "npc/ai/NpcSchedule.h"

class NPCEwar;
class Sentry;
class SystemEntity;
class EvilNumber;
struct NpcProfile;

class SentryAI {
protected:
    enum State {
        Idle        = 1,  // not doing anything....idle.
        Engaged     = 2,  // actively fighting
        Signaling   = 3   // calling for help
    };

public:
    SentryAI(Sentry *who);
    ~SentryAI();

    void Process();

    void Target(SystemEntity *by_who);
    void Targeted(SystemEntity *by_who);
    void TargetLost(SystemEntity *by_who);
    // TargetManager finished a lock we started
    void OnLockComplete(SystemEntity* pTarget);

    void ClearAllTargets();

    // called from Sentry::Killed()
    void OnKilled();

    bool IsIdle()                                       { return (m_state == State::Idle); }
    bool IsFighting()                                   { return (m_state != State::Idle); }
    uint32 GetPrimaryID()                               { return m_primaryID; }
    const NpcProfile& GetProfile() const                { return *m_profile; }

    // state, schedule, locks and profile for /npcai
    std::string Dump(SystemEntity* pViewer, bool verbose);

protected:
    static int64 Now();

    void SetIdle();
    void Attack(SystemEntity* pTarget);
    void SetEngaged(SystemEntity* pTarget);
    void ClearTarget(SystemEntity* pTarget);
    void AttackTarget(SystemEntity* pTarget);
    void CheckDistance(SystemEntity* pTarget);
    void FireMissile(SystemEntity* pTarget);

    double GetTargetTime(SystemEntity* pTarget);

    // our primary among our targets, or a new one when it's gone (law sentries:  only law targets)
    SystemEntity* GetPrimary();
    // other sentries: the nearest player ship in sight
    SystemEntity* FindTarget();
    // law sentries: a pilot (or their drone) with a global criminal countdown, or flagged to our owner
    SystemEntity* FindLawTarget();
    bool IsLawTarget(SystemEntity* pTarget);
    // pods only where npc.TargetPod allows them
    bool PodRuleAllows(SystemEntity* pTarget);

    State m_state;

    static std::string GetStateName(State name);

private:
    bool m_ewarChecked : 1;

    uint32 m_sightRange;
    uint32 m_primaryID;     // weapons target

    Sentry* m_npc;
    NPCEwar* m_ewar;            // we own this.  non-law sentries with ewar only
    std::shared_ptr<const NpcProfile> m_profile;

    NpcSchedule m_sched;
};

#endif
