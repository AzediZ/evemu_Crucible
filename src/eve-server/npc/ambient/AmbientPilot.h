 /**
  * @name AmbientPilot.h
  *   the pilot of one ambient ship (doc/code_and_design_notes/ambient-npc-plan.md section 8):  it flies the ship's itinerary leg
  *   by leg (hold, warp, approach a gate and jump, approach a dock and dock, loiter, mine, visit a miner, hunt) as a small timed
  *   state machine, with the reference hull's speed and warp speed.  fights belong to the npc's ai (NPCAIMgr with an
  *   AmbientBrain):  while it isn't idle the pilot waits, and resumes its leg when the fight is over.  civilians flee when hurt.
  *   commands go out only on leg changes (no per-tick destiny traffic, risk R5)
  */

#ifndef _EVE_NPC_AMBIENT_AMBIENTPILOT_H_
#define _EVE_NPC_AMBIENT_AMBIENTPILOT_H_

#include <string>
#include <vector>

#include "npc/ai/NpcRng.h"
#include "npc/ambient/AmbientTypes.h"

class AmbientNPC;
class AmbientSystem;
class SystemEntity;

class AmbientPilot {
public:
    AmbientPilot(AmbientNPC* pNPC, AmbientSystem* pSystem, uint64_t seed);

    // the itinerary, from leg 'first' on
    void SetLegs(const std::vector<Ambient::LegData>& legs, size_t first = 0);
    // in space now (after SystemManager::AddNPC):  start the way the ship came (Ambient::Start), at this node
    void Begin(uint8 start, uint32 startNode, uint8 startNodeKind, int64 now);
    // from AmbientNPC::Process(), after the ai and movement of this tick
    void Tick(int64 now);

    /* NPCAIMgr hooks (4.3) */
    // an npc target the brain may go after (only ambient ships and criminally flagged npcs)
    bool MayTarget(SystemEntity* pSE);
    // the ai went back to idle:  resume the leg we were on
    void OnCombatEnded()                                { m_resume = true; }

    /* AmbientSystem */
    void OnKilled();
    // the system takes us out of space (beams stop while the ball is still there)
    void OnRemoving();
    bool IsRemoving() const                             { return m_removing; }
    void SetRemoving()                                  { m_removing = true; }
    // a ship that stays on its grid (mining, loitering, hunting, squad):  removed after ambient.GridLingerSeconds unwatched
    bool IsStationary() const;
    // the itinerary from 'from' on, as an off-grid traveller at the node we are at
    Ambient::Traveller MakeTraveller(size_t from, int64 now) const;
    size_t GetLegIndex() const                          { return m_leg; }
    uint32 GetNodeID() const                            { return m_nodeID; }

    /* squads (8.3):  the squad gives the orders */
    void SquadLoiter(uint32 objectID, int64 holdMs, int64 now);
    // warp to node (kind:  AmbientRules::Node, or Node 255 = an entity) after delayMs, then loiter there
    void SquadWarp(uint32 nodeID, uint8 nodeKind, int64 delayMs, int64 now);
    void SquadEngage(SystemEntity* pTarget);
    bool IsBusy() const;

    /* miners (8.2) */
    void ResendBeams(int64 now);
    // a miner placed by a warm start:  some cycles done already, so they don't all leave together
    void WarmMiner();
    uint32 GetRockID() const                            { return (m_mineState == MineCycling ? m_rockID : 0); }

    std::string Describe() const;

private:
    enum MineState : uint8 {
        MinePick        = 0,
        MineApproach    = 1,
        MineCycling     = 2
    };

    void StartLeg(int64 now);
    void NextLeg(int64 now);
    // landed (or arrived by a short hop) at the current WarpTo leg's node
    void Arrived(int64 now);
    // out of space:  reason is AmbientSystem's Removal
    void Finish(uint8 reason, int64 delayMs = 0);

    void DoWarp(const Ambient::LegData& leg, int64 now);
    bool UpdateWarp(int64 now);
    void DoJumpOut(uint32 gateID, int64 now);
    // the remaining itinerary becomes:  dock at a station of the system, else jump out
    void BuildLeave(int64 now);

    // civilians:  hurt, so leave (dock if a station is close, else warp off grid)
    void Flee(int64 now);
    // criminals:  flag, flee when hurt or CONCORD is here, leave when there is no prey
    void CriminalTick(int64 now);
    void CriminalLeave(int64 now, const char* why);

    // miners
    void MineTick(int64 now);
    bool PickRock(int64 now);
    void StartBeams(int64 now);
    void StopBeams();

    void SetSpeed();
    SystemEntity* GetSE(uint32 id) const;

    AmbientNPC* m_npc;              // we do not own this (it owns us)
    AmbientSystem* m_sys;           // we do not own this
    NpcRng m_rng;

    std::vector<Ambient::LegData> m_legs;
    size_t m_leg;

    bool m_legStarted       :1;
    bool m_warpSeen         :1;     // the warp of this leg went into warp proper
    bool m_shortHop         :1;     // the WarpTo leg was too short to warp:  flying
    bool m_removing         :1;
    bool m_fleeing          :1;
    bool m_paused           :1;     // the ai was fighting
    bool m_resume           :1;     // the ai went idle:  restart our leg
    bool m_flagged          :1;     // criminal:  our crime is on record
    bool m_waitUncloak      :1;     // gate arrival:  the first leg starts after the uncloak

    int64 m_until;                  // ms:  the current timed part is done
    int64 m_legStart;               // ms
    int64 m_uncloakAt;              // ms (0 = not cloaked by us)
    int64 m_lastDamage;             // ms:  the ai's last damage we reacted to
    int64 m_noPreySince;            // ms (0 = we have prey or a fight)
    int64 m_nextFleeRoll;           // ms
    uint32 m_nodeID;                // the node we are at (or last reached)
    uint8 m_nodeKind;
    GPoint m_goal;                  // short hop / approach point

    /* miners */
    uint8 m_mineState;
    uint32 m_rockID;
    uint32 m_cycles;
    uint32 m_fullAt;
    int64 m_cycleEnd;               // ms
    bool m_beams;                   // beams were started on m_rockID
    std::vector<int64> m_beamAt;    // ms each strip miner starts (0 = started)
};

#endif  // _EVE_NPC_AMBIENT_AMBIENTPILOT_H_
