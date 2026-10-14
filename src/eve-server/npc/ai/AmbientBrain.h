 /**
  * @name AmbientBrain.h
  *   the policy of ambient npcs (doc/code_and_design_notes/planning/ambient-npc-plan.md 8.6).  AmbientSystem makes it and hands it to
  *   the npc's ai (NPCAIMgr::SetBrain) before its first Process().  modes:
  *    Civilian  haulers, couriers, freighters, miners:  never acquire or retaliate (the pilot flees), never sleep
  *    Military  navy and CONCORD:  fight back, the squad joins in, and (ambient.PatrolsEngageCriminals) go after criminals on
  *              their grid:  flagged players (never pods), ambient criminals and npcs with a criminal flag
  *    Criminal  pirates:  prey on the ambient civilians of their grid first, players only while crime is on (crime.Enabled,
  *              /crime on|off) and the toggles say so (ambient.HighSecCriminalsAttackPlayers, LowSecCriminalsAttackPlayers),
  *              and shoot back at whoever shoots them
  */

#ifndef _EVE_NPC_AI_AMBIENTBRAIN_H_
#define _EVE_NPC_AI_AMBIENTBRAIN_H_

#include "npc/ai/NpcBrain.h"

class AmbientPilot;

class AmbientBrain : public NpcBrain {
public:
    enum Mode : uint8 {
        Civilian    = 0,
        Military    = 1,
        Criminal    = 2
    };

    AmbientBrain(uint8 mode, AmbientPilot* pPilot) : m_mode(mode), m_pilot(pPilot) { }

    virtual const char* GetName() const;
    virtual SystemEntity* Acquire(NpcContext& ctx);
    virtual SystemEntity* OnAggressed(NpcContext& ctx, SystemEntity* pBy);
    virtual void OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor);
    virtual SystemEntity* RepickPrimary(NpcContext& ctx, SystemEntity* pLost);
    virtual bool CanSleep() const                       { return false; }
    virtual std::string Describe(NpcContext& ctx);

    uint8 GetMode() const                               { return m_mode; }

    /* who this mode may go after (NPCAIMgr::CanAggro's npc rule, AmbientPilot::MayTarget):  an npc target is only ever an
     *  ambient ship or an npc with a criminal flag */
    static bool MayTarget(uint8 mode, SystemEntity* pSelf, SystemEntity* pTarget);

private:
    /* pBy locked or hurt us:  do we answer it?  real damage (threat on us), a criminal (flagged pilot, ambient criminal or
     *  flagged npc), or for criminals any pilot when the toggle of their band lets them attack players.  a lock alone is no
     *  reason for navies and CONCORD (an innocent pilot may look at them) */
    bool Answers(NpcContext& ctx, SystemEntity* pBy);
    // criminals:  do we go after players unprovoked here (AmbientRules::CriminalsAttackPlayers:  crime on, and the band's toggle)
    bool AttacksPlayers(NpcContext& ctx);
    // the nearest acceptable target in our bubble (players only when players is set)
    SystemEntity* Nearest(NpcContext& ctx, bool players, bool criminalsOnly);

    uint8 m_mode;
    AmbientPilot* m_pilot;      // we do not own this
};

#endif  // _EVE_NPC_AI_AMBIENTBRAIN_H_
