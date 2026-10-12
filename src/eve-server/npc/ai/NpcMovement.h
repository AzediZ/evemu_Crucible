 /**
  * @name NpcMovement.h
  *   how an npc flies while it fights (PLAN.md 5.5).  npcs only (sentries don't move)
  *
  *   by surface distance d to the primary, with r_o = entityFlyRange (416) and r_b = max(entityChaseMaxDistance (665), r_o):
  *    d <= 1.5 r_o   orbit at r_o, entityCruiseSpeed
  *    d <= r_b       follow at r_o, entityCruiseSpeed
  *    d >  r_b       follow at r_o with the chase cycle:
  *                    stall at cruise speed for entityChaseMaxDelay (580) with chance 581, else U(0, 580),
  *                    then boost at maxVelocity for entityChaseMaxDuration (582) with chance 583, else U(0, 582),
  *                    then look again.  582 = 0 boosts for as long as the npc is out there
  *   while boosting the npc's signature is x entityMaxVelocitySignatureRadiusMultiplier (1133, 6.0:  "used to fake MWD sig
  *   radius increase", npc.ChaseSigBloom), unless a player's warp scrambler (effect 3725) is on it:  that stops the boost.
  *   every speed goes through DestinyManager::SetMaxVelocity, which keeps webs on (bug B5).
  *
  *   phase 3 hooks (level-one ai):  SetEvasive() turns on the evasive approach (1414:  while d > r_b the goto point is
  *   offset sideways by d/2, alternating every 5s), SetRangeOverride() replaces r_o (jam/damp responses, "cuddle").
  */

#ifndef _EVE_NPC_AI_NPCMOVEMENT_H_
#define _EVE_NPC_AI_NPCMOVEMENT_H_

#include "eve-server.h"

class ActiveModule;
class NPC;
class NpcRng;
class NpcSchedule;
class SystemEntity;
struct NpcProfile;

class NpcMovement {
public:
    NpcMovement(NPC* pNPC, const NpcProfile& profile, NpcRng& rng, NpcSchedule& sched);

    /* fighting pTarget:  orbit, follow or chase it (every tick) */
    void Engage(SystemEntity* pTarget, int64 now);
    /* the leash:  straight home at maxVelocity, no bloom */
    void GoHome(const GPoint& home);
    /* not fighting:  cruise speed, no chase, no bloom.  where it goes is the caller's (wander, hold, warp) */
    void Calm();

    uint8 GetMode() const                               { return m_mode; }     // NpcMath::Move
    bool IsBoosting() const                             { return m_boosting; }
    bool IsMWDBlocked() const                           { return m_blocked; }
    const char* GetModeName() const;
    // r_o and r_b in use (m)
    double OrbitRange() const;
    double BoostRange() const;

    /* phase 3 hooks */
    void SetEvasive(bool on)                            { m_evasive = on; }
    // > 0 replaces the orbit range (and the follow range).  0 = the data
    void SetRangeOverride(float range)                  { m_rangeOverride = range; }
    // orbit and follow at maxVelocity (the phase 0-1 'evasive npcs orbit fast' rule, kept for level-one npcs until phase 3)
    void SetFastOrbit(bool on)                          { m_fastOrbit = on; }

private:
    void StartStall(int64 now);
    void StartBoost(int64 now);
    void EndChase();
    // speed and signature for the current phase
    void ApplyBoost(bool boost);
    // a player's warp scrambler with effect 3725 (warpScrambleTargetMWDBlockActivation) is on us
    bool CheckMWDBlock();
    // evasive approach:  head for a point beside the target
    void Evade(SystemEntity* pTarget, double distance, int64 now);

    NPC* m_npc;
    const NpcProfile& m_profile;
    NpcRng& m_rng;
    NpcSchedule& m_sched;

    uint8 m_mode;
    bool m_boosting;            // boost phase (and not blocked):  maxVelocity and bloom
    bool m_boostPhase;          // in the boost phase (maybe blocked)
    bool m_blocked;
    bool m_evasive;
    bool m_fastOrbit;
    int8 m_evasiveSide;
    uint32 m_targetID;
    float m_rangeOverride;
    int64 m_evasiveFlipAt;
    GPoint m_lastGoto;

    std::vector<ActiveModule*> m_modules;       // scratch (no allocation per tick)
};

#endif  // _EVE_NPC_AI_NPCMOVEMENT_H_
