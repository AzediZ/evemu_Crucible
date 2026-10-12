 /**
  * @name LevelOneBrain.h
  *   the level-one ai (doc/code_and_design_notes/npc-ai/PLAN.md 4.6, 4.7, 5.7;  phase 3):  every type with
  *   AI_ShouldUseTargetSwitching (1648) > 0.  the 30 sleepers, the 32 incursion sansha (2 capitals), the 20 asteroid officers
  *   and 5 unique npcs (2943 Hybrid Slaver Hounds has 1648 = 2:  treated as 1, logged by its profile).
  *
  *   threat (the npc's ThreatTable, NPCAIMgr):
  *    damage          hp removed x AI_TankingModifier (1657, ships) or AI_TankingModifierDrone (1656, drones);  a drone's
  *                    pilot also gets npc.L1DroneControllerShare of it
  *    ewar            (1652 > 0) each cycle on us adds weight x E, E = npc.L1EffectThreat x our max hp:  ECM 5 (x2 more when
  *                    it jams), damp 3, TD 2, neut/nos 2, web/scram/painter 1
  *    remote repair   (1652 > 0) npc.L1RepairThreat x the hp a pilot repairs on a ship we have locked or threat on
  *    group share     npc.L1ThreatShare x every addition goes to the rest of our aggro group (not passed on again)
  *    decay           x npc.L1ThreatDecay every npc.L1ThreatDecayInterval, below 1 forgotten
  *    newcomers       (incursion only) a ship coming into sight that wasn't seen for npc.L1NewcomerMemory gets at least
  *                    npc.L1NewcomerThreat x the primary's threat
  *   score = (1 + threat) x S_sig (1650:  smaller than 1655 or our own signature:  max(0.25, sqrt(sig/pref))) x 1.25 for a
  *    drone that hurt us in the last 30s x 0.5 outside entityAttackRange (0 outside R_acq) x 1.1 when already locked.
  *   every npc.L1EvalInterval:  switch to the best candidate when it beats the primary by npc.L1SwitchMargin, the switch
  *    timer (1416, npc.TargetSwitchTime) has passed since the last switch, and a draw beats AI_ChanceToNotTargetSwitch (1651).
  *    an unlocked one is locked first (the switch happens when the lock completes).  the locks follow the best candidates up
  *    to maxLockedTargets (incursion sansha "keep you locked up after they've stopped shooting you").  losing the primary is
  *    an immediate re-pick and doesn't count as a switch.  drones are ordinary candidates.
  *   1649:  ewar goes on the best-scoring locked ship that isn't the primary and is in the effect's range, else the primary.
  *   jammed by X:  battleships (signature >= 300) orbit X at 5km, the others move out to X's ecm optimal + falloff + 5km.
  *   damped:  candidates count out to the undamped R_acq;  the state machine closes in until the target is lockable.
  *   evasive approach (1414) is NpcMovement's.  sleepers that see a capital ship tell their dungeon (capital escalation:
  *   the reinforcements are phase 5's, DungeonMgr::OnCapitalSighted).
  */

#ifndef _EVE_NPC_AI_LEVELONEBRAIN_H_
#define _EVE_NPC_AI_LEVELONEBRAIN_H_

#include "npc/ai/NpcBrain.h"
#include "npc/ai/SeenList.h"

class LevelOneBrain : public NpcBrain {
public:
    LevelOneBrain(const NpcProfile& profile);

    virtual const char* GetName() const                 { return "LevelOne"; }
    // the best-scoring candidate in R_acq (a passive group:  only pilots it remembers)
    virtual SystemEntity* Acquire(NpcContext& ctx);
    virtual SystemEntity* OnAggressed(NpcContext& ctx, SystemEntity* pBy);
    // join after the reaction delay, on our own best pick (the shared threat makes that the aggressor most of the time)
    virtual void OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor);
    // the best-scoring lock
    virtual SystemEntity* RepickPrimary(NpcContext& ctx, SystemEntity* pLost);
    virtual void Think(NpcContext& ctx);

    virtual void OnDamage(NpcContext& ctx, SystemEntity* pSource, float hp);
    virtual void OnEwar(NpcContext& ctx, SystemEntity* pSource, uint8 effect, bool jammed);
    virtual void OnAssist(NpcContext& ctx, SystemEntity* pRepairer, SystemEntity* pPatient, float hp);
    virtual void OnGroupThreat(NpcContext& ctx, SystemEntity* pSource, float amount);
    virtual bool KeepsThreat() const                    { return true; }

    virtual void WhileJammed(NpcContext& ctx, SystemEntity* pJammer, double jammerRange);
    virtual size_t EwarTargets(NpcContext& ctx, SystemEntity* pPrimary, SystemEntity** out, size_t max);
    virtual std::string Describe(NpcContext& ctx);

    // (1 + threat) x signature x drone x range x lock (5.7)
    double Score(NpcContext& ctx, SystemEntity* pSE);

private:
    // threat on pSource, and npc.L1ThreatShare of it for our group (share false:  it came from the group)
    void AddThreat(NpcContext& ctx, SystemEntity* pSource, float amount, bool share);
    // threat fades (npc.L1ThreatDecay every npc.L1ThreatDecayInterval)
    void Decay(NpcContext& ctx);
    // every npc.L1EvalInterval while engaged:  newcomers, switch, locks
    void Evaluate(NpcContext& ctx);
    // who came into sight (incursion newcomers, sleeper capital escalation)
    void LookAround(NpcContext& ctx, SystemEntity* pPrimary);
    // switch to a better target (5.7 switching)
    void Switch(NpcContext& ctx, SystemEntity* pPrimary);
    // keep our locks on the best candidates (up to maxLockedTargets)
    void RefreshLocks(NpcContext& ctx, SystemEntity* pPrimary);
    // makes room for one more lock:  drops our worst lock that isn't the primary, the pending switch or in keep
    bool MakeRoom(NpcContext& ctx, SystemEntity* pPrimary, SystemEntity* const* keep, size_t keepCount);
    // the best-scoring candidates (ships and player drones within the undamped R_acq), best first.  returns the count
    size_t Rank(NpcContext& ctx, SystemEntity** out, double* scores, size_t max);
    // the switch timer in use (ms)
    static int64 SwitchTimerMs(const NpcProfile& profile);

    static const size_t c_maxRanked = 16;

    double m_preferredSig;      // 1655, else our own signature (0 = 1650 is off)
    int64 m_nextEval;           // ms
    int64 m_nextDecay;          // ms
    uint32 m_jamGotoTarget;     // the jammer we last moved away from (or cuddled)
    bool m_jamCuddle;
    GPoint m_jamGoto;
    SeenList m_seen;

    // scratch (no allocation per tick)
    SystemEntity* m_ranked[c_maxRanked];
    double m_rankScore[c_maxRanked];
};

#endif  // _EVE_NPC_AI_LEVELONEBRAIN_H_
