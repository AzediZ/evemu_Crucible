 /**
  * @name SentryAI.h
  *   Sentry AI class
  * @Author:    Allan
  * @date:      23 April 2017
  */

#ifndef __NPC_SENTRY_AI_H
#define __NPC_SENTRY_AI_H

#include "ship/modules/TurretFormulas.h"
#include "system/SystemEntity.h"

class NPCEwar;
class Sentry;
class SystemEntity;
class Timer;
class EvilNumber;

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

    void ClearTargets();
    void ClearAllTargets();

    void DisableRepTimers();

    // called from Sentry::Killed()
    void OnKilled();

    // public methods to enable calls from other classes (namely, TurretFormulas.cpp)
    bool IsIdle()                                       { return (m_state == State::Idle); }
    bool IsFighting()                                   { return (m_state != State::Idle); }
    uint16 GetOptimalRange()                            { return m_optimalRange; }
    uint32 GetFalloff()                                 { return m_falloff; }
    uint32 GetAttackRange()                             { return m_maxAttackRange; }
    double GetTrackingSpeed()                           { return m_trackingSpeed; }

protected:
    void SetIdle();
    void Attack(SystemEntity* pTarget);
    void SetEngaged(SystemEntity* pTarget);
    void ClearTarget(SystemEntity* pTarget);
    void AttackTarget(SystemEntity* pTarget);
    void SetSignaling(SystemEntity* pTarget);
    void CheckDistance(SystemEntity* pTarget);

    double GetTargetTime();

    // other sentries: the nearest player ship in sight
    SystemEntity* FindTarget();
    // law sentries: a pilot (or their drone) with a global criminal countdown, or flagged to our owner
    SystemEntity* FindLawTarget();
    bool IsLawTarget(SystemEntity* pTarget);

    State m_state;

    std::string GetStateName(State name);

private:
    bool m_ewarChecked : 1;
    bool m_useSigRadius : 1;
    bool m_useTargSwitching : 1;
    bool m_useSecondTarget : 1;
    float m_switchTargChance;   //fuzzy logic
    uint16 m_preferedSigRadius;
    //these attributes are cached to reduce access times. (much faster but uses more memory)
    uint16 m_attackSpeed;
    uint16 m_targetRange;   // max targeting range  default: m_maxAttackRange (unused)
    uint16 m_optimalRange;
    uint16 m_damageMultiplier;
    uint16 m_missileTypeID;
    uint32 m_launcherCycleTime;

    uint32 m_sigRadius;
    uint32 m_falloff;// distance past maximum range at which accuracy has fallen by half
    uint32 m_sightRange;
    uint32 m_maxAttackRange;// max firing range   default:15000

    double m_trackingSpeed;

    Sentry* m_npc;
    NPCEwar* m_ewar;            // we own this.  non-law sentries with ewar only

    TurretFormulas m_formula;

    Timer m_processTimer;
    Timer m_mainAttackTimer;
    Timer m_beginFindTarget;
    Timer m_missileTimer;
};

#endif