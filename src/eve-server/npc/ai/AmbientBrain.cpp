 /**
  * @name AmbientBrain.cpp
  *   the policy of ambient npcs (doc/code_and_design_notes/ambient-npc-plan.md 8.4 - 8.6).  see AmbientBrain.h
  */

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/ai/AmbientBrain.h"
#include "npc/ai/AmbientRules.h"
#include "npc/ambient/AmbientNPC.h"
#include "npc/ambient/AmbientPilot.h"
#include "system/CrimeWatch.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"

const char* AmbientBrain::GetName() const
{
    switch (m_mode) {
        case Military:  return "Ambient Military";
        case Criminal:  return "Ambient Criminal";
        default:        return "Ambient Civilian";
    }
}

bool AmbientBrain::MayTarget(uint8 mode, SystemEntity* pSelf, SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or (pTarget == pSelf) or !pTarget->IsNPCSE())
        return false;
    NPC* pNPC(pTarget->GetNPCSE());
    AmbientNPC* pAmb(pNPC->GetAmbientNPC());
    switch (mode) {
        case Military:
            // ambient criminals, and any npc with a live criminal flag (never law enforcement)
            if (pAmb != nullptr)
                return pAmb->IsAmbientCriminal();
            return (!pNPC->IsLawEnforcer() and sCrimeWatch.IsCriminal(pNPC->GetID()));
        case Criminal:
            // ambient civilians and navies, and the law that came for us (it can't be beaten, but it gets shot at)
            if (pAmb != nullptr)
                return !pAmb->IsAmbientCriminal();
            return pNPC->IsLawEnforcer();
        default:
            return false;
    }
}

SystemEntity* AmbientBrain::Nearest(NpcContext& ctx, bool players, bool criminalsOnly)
{
    SystemBubble* pBubble(ctx.npc->SysBubble());
    if (pBubble == nullptr)
        return nullptr;
    double range(ctx.ai->LockRange());
    SystemEntity* pBest(nullptr);
    double best(0.0);
    pBubble->ForEachDynamic([&](SystemEntity* cur) {
        if ((cur == ctx.npc) or cur->IsDead())
            return;
        if (cur->IsNPCSE()) {
            if (!MayTarget(m_mode, ctx.npc, cur))
                return;
            // criminals go after civilians first:  navies only when they attack us (OnAggressed)
            AmbientNPC* pAmb(cur->GetNPCSE()->GetAmbientNPC());
            if ((m_mode == Criminal) and ((pAmb == nullptr) or !Ambient::IsCivilian(pAmb->GetRole())))
                return;
            if (!ctx.ai->CanAggro(cur))
                return;
        } else {
            if (!players or !cur->IsShipSE() or !cur->HasPilot())
                return;
            if (!ctx.ai->CanAggro(cur))
                return;
            if (criminalsOnly and !sCrimeWatch.IsCriminal(cur->GetPilot()->GetCharacterID()))
                return;
        }
        double distance(ctx.npc->GetPosition().distance(cur->GetPosition()));
        if (distance > range)
            return;
        if ((pBest == nullptr) or (distance < best)) {
            pBest = cur;
            best = distance;
        }
    });
    return pBest;
}

SystemEntity* AmbientBrain::Acquire(NpcContext& ctx)
{
    switch (m_mode) {
        case Military: {
            // 8.4:  criminals on our grid, players with the global criminal countdown (never pods) and npcs alike
            if (!sConfig.ambient.PatrolsEngageCriminals)
                return nullptr;
            return Nearest(ctx, true, true);
        }
        case Criminal: {
            // 8.5:  the ambient civilians first, players only when the toggle of this security band says so
            SystemEntity* pPrey(Nearest(ctx, false, false));
            if (pPrey != nullptr)
                return pPrey;
            float level(CrimeWatch::SecurityLevel(ctx.npc->SystemMgr()->GetSystemSecurityRating()));
            bool high(level >= 0.45f);
            bool players(high ? sConfig.ambient.HighSecCriminalsAttackPlayers : sConfig.ambient.LowSecCriminalsAttackPlayers);
            if (!players)
                return nullptr;
            return Nearest(ctx, true, false);
        }
        default:
            return nullptr;
    }
}

SystemEntity* AmbientBrain::OnAggressed(NpcContext& ctx, SystemEntity* pBy)
{
    // civilians never fight back:  their pilot flees (AmbientPilot).  navies, CONCORD and criminals shoot back
    if (m_mode == Civilian)
        return nullptr;
    return ctx.ai->AggroTarget(pBy);
}

void AmbientBrain::OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor)
{
    // a squad or criminal group joins its member's fight after the reaction delay (5.6), as rats do
    if ((m_mode == Civilian) or (pAggressor == nullptr))
        return;
    double delay(ctx.ai->DrawReactionDelay());
    ctx.ai->QueueAggro(ctx.ai->AggroTarget(pAggressor), ctx.now + static_cast<int64>(delay), true);
}

SystemEntity* AmbientBrain::RepickPrimary(NpcContext& ctx, SystemEntity* pLost)
{
    if (m_mode == Civilian)
        return nullptr;
    return ctx.ai->PickPrimary(pLost);
}

std::string AmbientBrain::Describe(NpcContext& ctx)
{
    std::string str("ambient ");
    str += GetName();
    str += "<br>";
    if (m_pilot != nullptr)
        str += m_pilot->Describe();
    return str;
}
