 /**
  * @name SentryBrain.h
  *   sentry gun policy (doc/code_and_design_notes/npc-ai/PLAN.md 4.10, 4.11, 6.4, 6.6;  phase 4):  WHO a sentry shoots.
  *   SentryAI (npc/SentryAI.h) does HOW:  locking, the primary, the drift-free turret and missile schedule, ewar.  sentries
  *   don't move, so they have their own small brain interface next to the npcs' NpcBrain (npc/ai/NpcBrain.h).
  *
  *   LawSentryBrain     gate and station guns, Crimewatch 1 (EVElopedia "Sentry guns", Dec 2011):
  *                       - shoot pilots with a global criminal countdown, pilots with aggression against what the gun guards
  *                         (its gate or station, or its guns:  CrimeWatch::HasSentryAggression), and with
  *                         crime.NpcFactionAggression pilots who attacked one of the gun's faction's npcs.  their ships and
  *                         drones (fighters too), within crime.SentryRange.  never pods.  -5 security alone isn't enough
  *                       - pick a random eligible target, and pick again every crime.SentryRetarget (30s).  being picked
  *                         renews the pilot's aggression with the guarded object (crime.SentryAggression, 900s)
  *                       - every shot hits with quality 1.0:  no transversal, signature radius or wrecking shots
  *                       - never lock back an innocent, never use ewar
  *   PirateSentryBrain  pirate and deadspace sentries:  the nearest player ship within min(entityAttackRange, bubble), lock back
  *                       whoever locks or attacks them (a drone:  the drone, plan decision 7), the shared turret formula with
  *                       maxRange as optimal, their data ewar.  members of their dungeon group or room (npc/ai/AggroGroup.h):
  *                       group and room aggro come through SentryAI::OnGroupAlert, and a passive group's sentry acquires only
  *                       once the group was aggroed, and then only its aggressors.  a gun of the empire groups placed by a
  *                       dungeon is one of these (Sentry::SetLawSentry(false))
  */

#ifndef _EVE_NPC_AI_SENTRYBRAIN_H_
#define _EVE_NPC_AI_SENTRYBRAIN_H_

#include "eve-server.h"

class NpcRng;
class Sentry;
class SentryAI;
class SystemEntity;
struct NpcProfile;

struct SentryContext {
    Sentry* sentry;
    SentryAI* ai;
    const NpcProfile& profile;
    NpcRng& rng;
    int64 now;                  // ms (GetSteadyTime())
};

class SentryBrain {
public:
    virtual ~SentryBrain()                              { /* nothing to do */ }

    virtual const char* GetName() const = 0;

    // how far we look for targets and keep them, m
    virtual double AcquireRange(SentryContext& ctx) = 0;
    // idle (or our primary is gone), on the think cadence:  someone to lock on our own (nullptr = nobody)
    virtual SystemEntity* Acquire(SentryContext& ctx) = 0;
    // pBy locked, attacked or ewar'd us (or our room, SentryAI::OnGroupAlert) while we were idle:  whom we lock (nullptr = nobody)
    virtual SystemEntity* OnAggressed(SentryContext& ctx, SystemEntity* pBy) = 0;
    // may we keep pTarget locked and shoot it
    virtual bool MayShoot(SentryContext& ctx, SystemEntity* pTarget) = 0;
    // our primary is gone (pLost, may be nullptr):  the next one among our locks (nullptr = none)
    virtual SystemEntity* RepickPrimary(SentryContext& ctx, SystemEntity* pLost) = 0;
    // engaged, every RetargetMs():  a primary to switch to (may be the current one;  nullptr = keep it)
    virtual SystemEntity* Retarget(SentryContext& ctx, SystemEntity* pPrimary) { return nullptr; }
    // ms between Retarget() calls, 0 = never
    virtual int64 RetargetMs() const                    { return 0; }
    // pTarget became our primary
    virtual void OnPicked(SentryContext& ctx, SystemEntity* pTarget) { }
    // a turret shot at pTarget:  0 miss, 3 wrecking, else the hit quality 0.49 .. 1.49
    virtual float TurretHit(SentryContext& ctx, SystemEntity* pTarget) = 0;
    // may the type's ewar be used
    virtual bool UsesEwar() const                       { return true; }

    // the brain for this sentry (law or not is known on its first Process()).  the caller owns it
    static SentryBrain* Create(Sentry* pSentry, const NpcProfile& profile);
};

/* gate and station guns (Crimewatch 1) */
class LawSentryBrain : public SentryBrain {
public:
    virtual const char* GetName() const                 { return "LawSentry"; }
    virtual double AcquireRange(SentryContext& ctx);
    virtual SystemEntity* Acquire(SentryContext& ctx);
    virtual SystemEntity* OnAggressed(SentryContext& ctx, SystemEntity* pBy);
    virtual bool MayShoot(SentryContext& ctx, SystemEntity* pTarget);
    virtual SystemEntity* RepickPrimary(SentryContext& ctx, SystemEntity* pLost);
    virtual SystemEntity* Retarget(SentryContext& ctx, SystemEntity* pPrimary);
    virtual int64 RetargetMs() const;
    virtual void OnPicked(SentryContext& ctx, SystemEntity* pTarget);
    virtual float TurretHit(SentryContext& ctx, SystemEntity* pTarget) { return 1.0f; }
    virtual bool UsesEwar() const                       { return false; }

    // the character behind a ship, drone or fighter (0 for npcs and everything else).  pod:  it's a capsule
    static uint32 OwnerOf(SystemEntity* pTarget, bool& pod);

private:
    // a random eligible target in the bubble (nullptr = none)
    SystemEntity* PickRandom(SentryContext& ctx);

    std::vector<SystemEntity*> m_candidates;    // scratch, reused
};

/* pirate and deadspace sentries */
class PirateSentryBrain : public SentryBrain {
public:
    virtual const char* GetName() const                 { return "PirateSentry"; }
    virtual double AcquireRange(SentryContext& ctx);
    virtual SystemEntity* Acquire(SentryContext& ctx);
    virtual SystemEntity* OnAggressed(SentryContext& ctx, SystemEntity* pBy);
    virtual bool MayShoot(SentryContext& ctx, SystemEntity* pTarget);
    virtual SystemEntity* RepickPrimary(SentryContext& ctx, SystemEntity* pLost);
    virtual float TurretHit(SentryContext& ctx, SystemEntity* pTarget);

    // pods only where npc.TargetPod allows them
    static bool PodRuleAllows(SentryContext& ctx, SystemEntity* pTarget);
};

#endif  // _EVE_NPC_AI_SENTRYBRAIN_H_
