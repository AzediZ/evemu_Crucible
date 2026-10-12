 /**
  * @name NpcBrain.cpp
  *   npc ai policy (PLAN.md 6.4).  see NpcBrain.h
  */

#include "eve-server.h"

#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/ai/LawBrain.h"
#include "npc/ai/NpcBrain.h"
#include "npc/ai/NpcProfile.h"

NpcBrain* NpcBrain::Create(const NpcProfile& profile, bool lawEnforcer)
{
    if (profile.tier == NpcAI::Tier::Inert)
        return new InertBrain();
    // CONCORD, faction police and navy (phase 4, npc/ai/LawBrain.h)
    if (lawEnforcer)
        return new LawBrain();
    if (profile.tier == NpcAI::Tier::Passive)
        return new PassiveBrain();
    // Standard and LevelOne (the legacy rules switch on the profile's level-one flag)
    return new LegacyBrain(false);
}

/* LegacyBrain:  the rules NPCAIMgr had before the brain interface */
SystemEntity* LegacyBrain::Acquire(NpcContext& ctx)
{
    // law enforcement goes only after the ship it was sent for (the state machine handles that)
    if (m_law)
        return nullptr;
    return ctx.ai->ChooseTarget();
}

SystemEntity* LegacyBrain::OnAggressed(NpcContext& ctx, SystemEntity* pBy)
{
    // a drone's pilot when we can see him, else whoever it was
    return ctx.ai->AggroTarget(pBy);
}

void LegacyBrain::OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor)
{
    if (m_law)
        return;
    ctx.ai->QueueAggro(ctx.ai->AggroTarget(pAggressor));
}

SystemEntity* LegacyBrain::RepickPrimary(NpcContext& ctx, SystemEntity* pLost)
{
    // most threat (sleepers:  best score), then nearest
    return ctx.ai->PickPrimary(pLost);
}

void LegacyBrain::Think(NpcContext& ctx)
{
    if (m_law)
        return;
    if (ctx.profile.IsLevelOne())
        ctx.ai->ProcessAdvanced();
    ctx.ai->ProcessDroneAggro();
}

SystemEntity* LegacyBrain::EwarTarget(NpcContext& ctx, SystemEntity* pPrimary)
{
    // sleepers put their ewar on a secondary target
    if (ctx.profile.IsLevelOne() and ctx.profile.l1SecondaryTarget)
        return ctx.ai->GetSecondary();
    return nullptr;
}

/* PassiveBrain */
SystemEntity* PassiveBrain::Acquire(NpcContext& ctx)
{
    if (ctx.profile.ewarEffects.empty())
        return nullptr;
    return ctx.ai->ChooseTarget();
}

SystemEntity* PassiveBrain::OnAggressed(NpcContext& ctx, SystemEntity* pBy)
{
    if (ctx.profile.ewarEffects.empty())
        return nullptr;
    return ctx.ai->AggroTarget(pBy);
}

void PassiveBrain::OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor)
{
    if (!ctx.profile.ewarEffects.empty())
        ctx.ai->QueueAggro(ctx.ai->AggroTarget(pAggressor));
}

SystemEntity* PassiveBrain::RepickPrimary(NpcContext& ctx, SystemEntity* pLost)
{
    return ctx.ai->PickPrimary(pLost);
}
