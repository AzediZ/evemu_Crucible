 /**
  * @name LawBrain.cpp
  *   CONCORD, faction police and faction navy policy (PLAN.md 4.8, 4.9).  see LawBrain.h
  */

#include "eve-server.h"

#include "npc/NPC.h"
#include "npc/ai/LawBrain.h"
#include "system/DestinyManager.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"

bool LawBrain::IsOurTarget(NpcContext& ctx, SystemEntity* pSE)
{
    if ((pSE == nullptr) or pSE->IsDead() or (pSE->GetID() != ctx.npc->GetLawTarget()))
        return false;
    if ((pSE->SysBubble() == nullptr) or (pSE->SysBubble() != ctx.npc->SysBubble()) or (pSE->DestinyMgr() == nullptr))
        return false;
    return !(pSE->DestinyMgr()->IsCloaked() or pSE->DestinyMgr()->IsWarping());
}

SystemEntity* LawBrain::Acquire(NpcContext& ctx)
{
    SystemEntity* pTarget(ctx.npc->SystemMgr()->GetSE(ctx.npc->GetLawTarget()));
    return (IsOurTarget(ctx, pTarget) ? pTarget : nullptr);
}

SystemEntity* LawBrain::OnAggressed(NpcContext& ctx, SystemEntity* pBy)
{
    return (IsOurTarget(ctx, pBy) ? pBy : nullptr);
}

SystemEntity* LawBrain::RepickPrimary(NpcContext& ctx, SystemEntity* pLost)
{
    // the ship we were sent for, if we have it (or are locking it)
    SystemEntity* pTarget(ctx.npc->TargetMgr()->GetTarget(ctx.npc->GetLawTarget(), false));
    if ((pTarget == nullptr) or (pTarget == pLost) or !IsOurTarget(ctx, pTarget))
        return nullptr;
    return pTarget;
}
