 /**
  * @name NpcBrain.h
  *   npc ai policy (PLAN.md 6.4):  WHO an npc goes after.  the state machine in NPCAIMgr (npc/NPCAI.h) does HOW
  *   (locking, movement, weapons, ewar, repairs, schedule, sleep) and is shared by every brain.
  *
  *   extension point for the later phases of the plan:
  *    - NpcBrain::Create() picks the brain for an npc from its profile tier (npc/ai/NpcProfile.h) once, on its first
  *      Process() (when LawEnforcement has set its role).  add a brain by adding a class here and a case there.
  *    - every hook gets an NpcContext:  the npc, its ai manager, its profile, its rng and the time.  brains keep their
  *      own state (threat, timers) as members;  one brain object per npc.
  *    - the ai manager's 'brain helpers' (NPCAIMgr public section) are the mechanics a brain may use:  candidate
  *      tests, today's target choice, primary repick, queueing an aggressor.
  *   brains in this phase:
  *    LegacyBrain   today's Crucible-era rules for standard rats, level-one npcs (sleepers) and law enforcement, moved
  *                  behind this interface unchanged.  phase 2 replaces it with StandardBrain (4.2, 4.3), phase 3 with
  *                  LevelOneBrain (4.6, 4.7), phase 4 with LawBrain/SentryBrain (4.8-4.11)
  *    PassiveBrain  no weapons (haulers, customs officials, convoys):  never acquires or retaliates (unless ewar is all it
  *                  has:  then it picks targets for its ewar).  phase 2 adds the hauler flee timer
  *    InertBrain    no weapons, ewar or support and doesn't move (overseer structures, belongings, props):  no ai at all,
  *                  no lock-back (bug B11)
  */

#ifndef _EVE_NPC_AI_NPCBRAIN_H_
#define _EVE_NPC_AI_NPCBRAIN_H_

#include "eve-server.h"

class NPC;
class NPCAIMgr;
class NpcRng;
class SystemEntity;
struct NpcProfile;

namespace NpcAI {
    // what added threat (NpcBrain::OnThreat)
    namespace ThreatKind {
        enum : uint8 {
            Damage  = 0,    // hp removed
            Ewar    = 1,    // an ewar module or neut cycle on us
            Assist  = 2     // remote repair of someone we're fighting (phase 3)
        };
    }
}

struct NpcContext {
    NPC* npc;
    NPCAIMgr* ai;
    const NpcProfile& profile;
    NpcRng& rng;
    int64 now;                  // ms (GetSteadyTime())
};

class NpcBrain {
public:
    virtual ~NpcBrain()                                 { /* nothing to do */ }

    virtual const char* GetName() const = 0;

    // idle, on the think cadence:  someone to go after on our own (nullptr = nobody)
    virtual SystemEntity* Acquire(NpcContext& ctx) = 0;
    // pBy attacked, ewar'd or locked us while we were idle:  whom we go after (nullptr = ignore it)
    virtual SystemEntity* OnAggressed(NpcContext& ctx, SystemEntity* pBy) = 0;
    // a member of our group was attacked by pAggressor (NPCAIMgr::Aggro).  queue a target with ctx.ai->QueueAggro()
    virtual void OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor) = 0;
    // our primary is gone (pLost, may be nullptr):  the next one among our locks (nullptr = none, we go idle)
    virtual SystemEntity* RepickPrimary(NpcContext& ctx, SystemEntity* pLost) = 0;
    // engaged, every tick:  drone rolls, target switching, flee checks
    virtual void Think(NpcContext& ctx)                 { }
    // pSource added amount of threat (NpcAI::ThreatKind)
    virtual void OnThreat(NpcContext& ctx, SystemEntity* pSource, float amount, uint8 kind) { }
    // who our ewar goes on this tick when it isn't the primary (nullptr = the primary)
    virtual SystemEntity* EwarTarget(NpcContext& ctx, SystemEntity* pPrimary) { return nullptr; }
    // may this npc go dormant in a bubble without players (npc.SleepEmptyBubbles)
    virtual bool CanSleep() const                       { return true; }
    // false:  no ai at all (no targeting, no lock-back, no weapons)
    virtual bool RunsAI() const                         { return true; }

    // the brain for an npc of this profile (lawEnforcer:  CONCORD or faction police).  the caller owns it
    static NpcBrain* Create(const NpcProfile& profile, bool lawEnforcer);
};

/* today's rules, unchanged (see NPCAIMgr's brain helpers) */
class LegacyBrain : public NpcBrain {
public:
    virtual const char* GetName() const                 { return "Legacy"; }
    virtual SystemEntity* Acquire(NpcContext& ctx);
    virtual SystemEntity* OnAggressed(NpcContext& ctx, SystemEntity* pBy);
    virtual void OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor);
    virtual SystemEntity* RepickPrimary(NpcContext& ctx, SystemEntity* pLost);
    virtual void Think(NpcContext& ctx);
    virtual SystemEntity* EwarTarget(NpcContext& ctx, SystemEntity* pPrimary);
    virtual bool CanSleep() const                       { return !m_law; }

    LegacyBrain(bool law)                               : m_law(law) { }
private:
    bool m_law;
};

/* no weapons:  haulers, transports, customs officials never acquire or retaliate.  a type whose only means is ewar
 *  (effects, e.g. a jammer without guns) still picks targets like today's rats, so its ewar has someone to work on */
class PassiveBrain : public NpcBrain {
public:
    virtual const char* GetName() const                 { return "Passive"; }
    virtual SystemEntity* Acquire(NpcContext& ctx);
    virtual SystemEntity* OnAggressed(NpcContext& ctx, SystemEntity* pBy);
    virtual void OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor);
    virtual SystemEntity* RepickPrimary(NpcContext& ctx, SystemEntity* pLost);
};

/* overseer structures, belongings, props:  nothing at all */
class InertBrain : public NpcBrain {
public:
    virtual const char* GetName() const                 { return "Inert"; }
    virtual SystemEntity* Acquire(NpcContext& ctx)      { return nullptr; }
    virtual SystemEntity* OnAggressed(NpcContext& ctx, SystemEntity* pBy) { return nullptr; }
    virtual void OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor) { }
    virtual SystemEntity* RepickPrimary(NpcContext& ctx, SystemEntity* pLost) { return nullptr; }
    virtual bool RunsAI() const                         { return false; }
};

#endif  // _EVE_NPC_AI_NPCBRAIN_H_
