 /**
  * @name LawBrain.h
  *   CONCORD, faction police and faction navy (doc/code_and_design_notes/planning/npc-ai/PLAN.md 4.8, 4.9, 6.4;  phase 4).
  *   LawEnforcement (npc/LawEnforcement.h) sends them after one ship (NPC::GetLawTarget) and moves them when its pilot boards
  *   another one.  they go after that ship only:  they never pick anyone on their own, never answer anyone else, never join a
  *   group, never sleep and the leash doesn't hold them.  their tackle is their data ewar (NPCEwar for police and navy;
  *   CONCORD's concord* effects in LawEnforcement), see LawEnforcement::UsesTypeEwar()
  */

#ifndef _EVE_NPC_AI_LAWBRAIN_H_
#define _EVE_NPC_AI_LAWBRAIN_H_

#include "npc/ai/NpcBrain.h"

class LawBrain : public NpcBrain {
public:
    virtual const char* GetName() const                 { return "Law"; }
    // the ship we were sent for, when it's here and visible
    virtual SystemEntity* Acquire(NpcContext& ctx);
    // only the ship we were sent for
    virtual SystemEntity* OnAggressed(NpcContext& ctx, SystemEntity* pBy);
    virtual void OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor) { }
    virtual SystemEntity* RepickPrimary(NpcContext& ctx, SystemEntity* pLost);
    virtual bool CanSleep() const                       { return false; }

    // pSE is the ship this law npc was sent for, and we can go after it
    static bool IsOurTarget(NpcContext& ctx, SystemEntity* pSE);
};

#endif  // _EVE_NPC_AI_LAWBRAIN_H_
