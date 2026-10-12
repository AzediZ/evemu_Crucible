 /**
  * @name NpcBrain.h
  *   npc ai policy (PLAN.md 6.4):  WHO an npc goes after.  the state machine in NPCAIMgr (npc/NPCAI.h) does HOW
  *   (locking, movement, weapons, ewar, repairs, schedule, sleep) and is shared by every brain.
  *
  *   extension point for the later phases of the plan:
  *    - NpcBrain::Create() picks the brain for an npc from its profile tier (npc/ai/NpcProfile.h) once, on its first
  *      Process() (when LawEnforcement has set its role).  add a brain by adding a class here and a case there.
  *    - every hook gets an NpcContext:  the npc, its ai manager, its profile, its rng and the time.  brains keep their
  *      own state (timers, seen lists) as members;  one brain object per npc.  the threat table is the ai manager's
  *      (NPCAIMgr::GetThreatTable), the brain decides what goes in.
  *    - the ai manager's 'brain helpers' (NPCAIMgr public section) are the mechanics a brain may use:  candidate
  *      tests, target choice, primary repick, locks, queueing an aggressor.
  *   brains:
  *    StandardBrain belt, gate, mission and deadspace rats (phase 2, PLAN.md 4.2, 4.3):  nearest player ship or drone in
  *                  R_acq (passive groups:  only pilots that aggroed them), a wave's first pick may be the weakest ship,
  *                  retaliation on the drone itself (npc.DroneRetaliation), group joins after the reaction delay on the
  *                  same target with probability entityReactionFactor, focus until the primary is gone, the drone roll
  *    LevelOneBrain sleepers, incursion sansha, asteroid officers and 5 unique npcs (phase 3, PLAN.md 4.6, 4.7, 5.7):
  *                  threat and score, timed switching, secondary ewar target, jam response (npc/ai/LevelOneBrain.h)
  *    LawBrain      CONCORD, faction police and navy (phase 4, npc/ai/LawBrain.h)
  *    PassiveBrain  no weapons (haulers, customs officials, convoys):  never acquires or retaliates (unless ewar is all it
  *                  has:  then it picks targets for its ewar).  the hauler flee is the state machine's (NPCAI::Role)
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

    /* threat.  the standard rule:  damage is threat (a drone's pilot gets half of it), an ewar cycle is 50 */
    // pSource removed hp from us (after resistances)
    virtual void OnDamage(NpcContext& ctx, SystemEntity* pSource, float hp);
    // pSource ran an ewar cycle on us (effect:  NpcMath::Effect).  jammed:  that cycle jammed us (ECM, after the cycle's own call)
    virtual void OnEwar(NpcContext& ctx, SystemEntity* pSource, uint8 effect, bool jammed);
    // pRepairer repaired hp on pPatient, a ship we have locked or threat on (remote repair, shield transfer, repair drones)
    virtual void OnAssist(NpcContext& ctx, SystemEntity* pRepairer, SystemEntity* pPatient, float hp) { }
    // a member of our aggro group shares amount of its threat on pSource with us (level one, 5.7)
    virtual void OnGroupThreat(NpcContext& ctx, SystemEntity* pSource, float amount) { }
    // our threat outlives going idle (a jam breaks every lock;  the jammer stays the threat)
    virtual bool KeepsThreat() const                    { return false; }

    // jammed (we can't lock anything):  pJammer (nullptr = gone) jams us from up to jammerRange (its optimal + falloff)
    virtual void WhileJammed(NpcContext& ctx, SystemEntity* pJammer, double jammerRange) { }
    /* where our ewar goes (5.7 secondary target):  up to max completed locks other than the primary, best first.  each effect
     *  takes the first of them within its range, else the primary.  0 = everything on the primary */
    virtual size_t EwarTargets(NpcContext& ctx, SystemEntity* pPrimary, SystemEntity** out, size_t max) { return 0; }
    // may this npc go dormant in a bubble without players (npc.SleepEmptyBubbles)
    virtual bool CanSleep() const                       { return true; }
    // false:  no ai at all (no targeting, no lock-back, no weapons)
    virtual bool RunsAI() const                         { return true; }
    // extra lines for /npcai (html, each ending in <br>)
    virtual std::string Describe(NpcContext& ctx)       { return std::string(); }

    // the brain for an npc of this profile (lawEnforcer:  CONCORD or faction police).  the caller owns it
    static NpcBrain* Create(const NpcProfile& profile, bool lawEnforcer);
};

/* the standard ai (4.2, 4.3) */
class StandardBrain : public NpcBrain {
public:
    virtual const char* GetName() const                 { return "Standard"; }
    virtual SystemEntity* Acquire(NpcContext& ctx);
    virtual SystemEntity* OnAggressed(NpcContext& ctx, SystemEntity* pBy);
    virtual void OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor);
    virtual SystemEntity* RepickPrimary(NpcContext& ctx, SystemEntity* pLost);
    virtual void Think(NpcContext& ctx);
};

/* no weapons:  haulers, transports, customs officials never acquire or retaliate.  a type whose only means is ewar
 *  (effects, e.g. a jammer without guns) still picks the nearest ship, so its ewar has someone to work on */
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
    virtual void OnDamage(NpcContext& ctx, SystemEntity* pSource, float hp) { }
    virtual void OnEwar(NpcContext& ctx, SystemEntity* pSource, uint8 effect, bool jammed) { }
    virtual bool RunsAI() const                         { return false; }
};

#endif  // _EVE_NPC_AI_NPCBRAIN_H_
