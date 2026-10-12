 /**
  * @name NpcBrain.cpp
  *   npc ai policy (PLAN.md 6.4).  see NpcBrain.h
  */

#include "eve-server.h"

#include "EVEServerConfig.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/ai/AggroGroup.h"
#include "npc/ai/LawBrain.h"
#include "npc/ai/LevelOneBrain.h"
#include "npc/ai/NpcBrain.h"
#include "npc/ai/NpcProfile.h"
#include "npc/ai/NpcRng.h"

NpcBrain* NpcBrain::Create(const NpcProfile& profile, bool lawEnforcer)
{
    if (profile.tier == NpcAI::Tier::Inert)
        return new InertBrain();
    // CONCORD, faction police and navy (phase 4, npc/ai/LawBrain.h)
    if (lawEnforcer)
        return new LawBrain();
    if (profile.tier == NpcAI::Tier::Passive)
        return new PassiveBrain();
    // sleepers, incursion sansha, asteroid officers, 5 unique npcs (phase 3, npc/ai/LevelOneBrain.h)
    if (profile.tier == NpcAI::Tier::LevelOne)
        return new LevelOneBrain(profile);
    return new StandardBrain();
}

/* the standard threat rule (4.2):  the next primary is the locked target that hurt us most */
void NpcBrain::OnDamage(NpcContext& ctx, SystemEntity* pSource, float hp)
{
    if ((pSource == nullptr) or !(hp > 0.0f))
        return;
    ctx.ai->AddThreat(pSource, hp);
    // a drone's pilot is behind it
    if (pSource->IsDroneSE()) {
        SystemEntity* pController(pSource->GetDroneSE()->GetController());
        if (pController != nullptr)
            ctx.ai->AddThreat(pController, hp * 0.5f);
    }
}

void NpcBrain::OnEwar(NpcContext& ctx, SystemEntity* pSource, uint8 effect, bool jammed)
{
    // a cycle of ewar on us counts like a little damage (the jam itself adds nothing more)
    if ((pSource != nullptr) and !jammed)
        ctx.ai->AddThreat(pSource, 50.0f);
}

/* StandardBrain */
SystemEntity* StandardBrain::Acquire(NpcContext& ctx)
{
    /* a passive group never picks anyone on its own.  once aggroed it goes after the pilots who did it (for
     *  npc.GroupAggroMemory).  a ship that comes too close wakes it (NPCAIMgr::CheckProximity) */
    AggroGroup* pGroup(ctx.ai->GetGroup());
    AggroGroup* pOnly(nullptr);
    if ((pGroup != nullptr) and pGroup->IsPassive()) {
        if (!pGroup->WasAlerted())
            return nullptr;
        pOnly = pGroup;
    }
    // a wave's npcs often go for the weakest ship first (4.3)
    if (ctx.ai->IsWave() and (ctx.rng.Uniform() < sConfig.npc.WaveWeakestChance)) {
        SystemEntity* pWeakest(ctx.ai->ChooseStandardTarget(false, true, pOnly));
        if (pWeakest != nullptr)
            return pWeakest;
    }
    // the nearest player ship, or player drone (npc.AcquireDrones)
    return ctx.ai->ChooseStandardTarget(sConfig.npc.AcquireDrones, false, pOnly);
}

SystemEntity* StandardBrain::OnAggressed(NpcContext& ctx, SystemEntity* pBy)
{
    // a drone that hit us gets shot (npc.DroneRetaliation 1, 4.2);  0 = its pilot when we can see him
    if ((pBy != nullptr) and pBy->IsDroneSE() and (sConfig.npc.DroneRetaliation == 1))
        return pBy;
    return ctx.ai->AggroTarget(pBy);
}

void StandardBrain::OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor)
{
    /* "the chance of an entity attacking the same person as its group members.  scales delay in joining in on fights too"
     *  (entityReactionFactor, 466).  we join after U(entityAttackDelayMin, Max) x factor, on the same target with that
     *  chance, else on our own pick */
    if (pAggressor == nullptr)
        return;
    double delay(ctx.ai->DrawReactionDelay());
    SystemEntity* pTarget(pAggressor);
    if (sConfig.npc.UseReactionFactor and (ctx.rng.Uniform() >= ctx.profile.reactionFactor)) {
        SystemEntity* pOwn(Acquire(ctx));
        if (pOwn != nullptr)
            pTarget = pOwn;
    }
    if (pTarget->IsDroneSE() and (sConfig.npc.DroneRetaliation != 1))
        pTarget = ctx.ai->AggroTarget(pTarget);
    ctx.ai->QueueAggro(pTarget, ctx.now + static_cast<int64>(delay), true);
}

SystemEntity* StandardBrain::RepickPrimary(NpcContext& ctx, SystemEntity* pLost)
{
    // the locked target that hurt us most, ties to the nearest.  none:  we look again (idle)
    return ctx.ai->PickPrimary(pLost);
}

void StandardBrain::Think(NpcContext& ctx)
{
    // the primary stays until it's gone.  now and then a drone that hurt us takes over (4.2, N2)
    ctx.ai->ProcessDroneRoll();
}

/* PassiveBrain */
SystemEntity* PassiveBrain::Acquire(NpcContext& ctx)
{
    if (ctx.profile.ewarEffects.empty())
        return nullptr;
    // the nearest player ship for its ewar
    return ctx.ai->ChooseStandardTarget(false, false, nullptr);
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
