 /**
  * @name NPCEwar.cpp
  *   electronic warfare used by npcs and non-law sentries
  *
  *   effect (id)                   chance                      duration (default)      range / falloff       values
  *   warpScrambleForEntity (563)   entityWarpScrambleChance(u) warpScrambleDuration    warpScrambleRange     warpScrambleStrength
  *   modifyTargetSpeed2 (575)      modifyTargetSpeedChance(u)  modifyTargetSpeedDur.   modifyTargetSpeedRange  speedFactor
  *   entityTargetJam (1871)        entityTargetJamDurChance    entityTargetJamDur.     936 / 953             scan*StrengthBonus
  *   entityCapacitorDrain (1872)   entityCapDrainDurChance     entityCapDrainDur.      937 / -               entityCapacitorDrainAmount
  *   entitySensorDampen (1878)     entitySensorDampenDurChance entitySensorDampenDur.  938 / 950             maxTargetRange/scanResolutionMultiplier
  *   entityTrackingDisruptOld(1877) entityTrackingDisruptDurCh. entityTrackingDisrDur. 940 / 951             entityTrackingDisruptMultiplier
  *   entityTargetPaint (1879)      entityTargetPaintDurChance  entityTargetPaintDur.   941 / 954             signatureRadiusBonus
  *
  *   (u):  usage chance (npc.EwarUsageRoll), the others are activation chances rolled every duration ("chance of npc effect
  *   to be activated each duration").  chances are 0..1;  a few types use percent (> 1)
  */

#include "eve-server.h"

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "npc/NPCEwar.h"
#include "npc/ai/NpcMath.h"
#include "npc/ai/NpcRng.h"
#include "ship/modules/EwarModule.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemEntity.h"
#include "system/SystemManager.h"


NPCEwar::NPCEwar(SystemEntity* pSelf, NpcRng* rng/*nullptr*/)
: m_self(pSelf),
m_rng(rng),
m_selfRef(pSelf->GetSelf()),
m_tackleMult(1.0f)
{
    m_effects.clear();

    // scram.  an npc scram with no strength data still scrams
    float strength(Attr(AttrWarpScrambleStrength, 1.0f));
    if (strength <= 0.0f)
        strength = 1.0f;
    AddEffect(EVEEffectID::warpScrambleForEntity, Ewar::Scramble, "effects.WarpScramble", AttrEntityWarpScrambleChance, 0,
              AttrWarpScrambleDuration, 8000, AttrWarpScrambleRange, 0, 0, strength, 0);

    // web.  only real webs, and nothing that would turn the target's speed negative
    float speedFactor(m_selfRef->GetAttribute(AttrSpeedFactor).get_float());
    if ((speedFactor < 0.0f) and (speedFactor > -100.0f))
        AddEffect(EVEEffectID::modifyTargetSpeed2, Ewar::Web, "effects.ModifyTargetSpeed", AttrModifyTargetSpeedChance, 0,
                  AttrModifyTargetSpeedDuration, 5000, AttrModifyTargetSpeedRange, 20000, 0, speedFactor, 0);

    // ecm.  the effect has no guid; the client shows the player ecm beam
    AddEffect(EVEEffectID::entityTargetJam, Ewar::Jam, "effects.ElectronicAttributeModifyTarget", 0, AttrEntityTargetJamDurationChance,
              AttrEntityTargetJamDuration, 30000, AttrEntityTargetJamMaxRange, 0, AttrEntityTargetJamFallOff, 0, 0);

    // energy neutralizer (GJ per duration)
    float amount(Attr(AttrEntityCapacitorDrainAmount, 0.0f));
    if (amount > 0.0f)
        AddEffect(EVEEffectID::entityCapacitorDrain, Neut, "effects.EnergyVampire", 0, AttrEntityCapacitorDrainDurationChance,
                  AttrEntityCapacitorDrainDuration, 30000, AttrEntityCapacitorDrainMaxRange, 0, AttrEntityCapacitorFallOff, amount, 0);

    // sensor dampener: target range and scan resolution multipliers
    float rangeMult(Attr(AttrMaxTargetRangeMultiplier, Attr(AttrEntitySensorDampenMultiplier, 1.0f)));
    float scanMult(Attr(AttrScanResolutionMultiplier, Attr(AttrEntitySensorDampenMultiplier, 1.0f)));
    if (rangeMult <= 0.0f)
        rangeMult = 1.0f;
    if (scanMult <= 0.0f)
        scanMult = 1.0f;
    if ((rangeMult < 1.0f) or (scanMult < 1.0f))
        AddEffect(EVEEffectID::entitySensorDampen, Ewar::Damp, "effects.ElectronicAttributeModifyTarget", 0, AttrEntitySensorDampenDurationChance,
                  AttrEntitySensorDampenDuration, 30000, AttrEntitySensorDampenMaxRange, 0, AttrEntitySensorDampenFallOff, rangeMult, scanMult);

    // tracking disruptor: turret tracking and optimal range
    float trackMult(Attr(AttrEntityTrackingDisruptMultiplier, 1.0f));
    if ((trackMult > 0.0f) and (trackMult < 1.0f))
        AddEffect(EVEEffectID::entityTrackingDisruptOld, Ewar::TrackDisrupt, "effects.ElectronicAttributeModifyTarget", 0, AttrEntityTrackingDisruptDurationChance,
                  AttrEntityTrackingDisruptDuration, 30000, AttrEntityTrackingDisruptMaxRange, 0, AttrEntityTrackingDisruptFallOff, trackMult, trackMult);

    // target painter: signature radius (bonus in %)
    float sigBonus(Attr(AttrSignatureRadiusBonus, 0.0f));
    float sigMult(sigBonus > 0.0f ? (1.0f + (sigBonus / 100.0f)) : Attr(AttrEntityTargetPaintMultiplier, 1.0f));
    if (sigMult > 1.0f)
        AddEffect(EVEEffectID::entityTargetPaint, Ewar::Paint, "effects.TargetPaint", 0, AttrEntityTargetPaintDurationChance,
                  AttrEntityTargetPaintDuration, 30000, AttrEntityTargetPaintMaxRange, 0, AttrEntityTargetPaintFallOff, sigMult, 0);

    if (!m_effects.empty())
        _log(NPC__AI_TRACE, "%s(%u): NPCEwar - %u ewar effects loaded.", m_self->GetName(), m_self->GetID(), (uint32)m_effects.size());
}

float NPCEwar::Attr(uint16 attrID, float def) const
{
    EvilNumber value;
    if (m_selfRef->HasAttribute(attrID, value))
        return value.get_float();
    return def;
}

double NPCEwar::Rand()
{
    if (m_rng != nullptr)
        return m_rng->Uniform();
    return MakeRandomFloat();
}

void NPCEwar::AddEffect(uint16 effectID, uint8 kind, const char* guid, uint16 usageAttr, uint16 chanceAttr, uint16 durationAttr,
                        uint32 defDuration, uint16 rangeAttr, double defRange, uint16 falloffAttr, float v1, float v2)
{
    if (!m_selfRef->type().HasEffect(effectID)) {
        // types without any effect data fall back to their attributes
        if (m_selfRef->type().HasEffects())
            return;
        if (Attr((usageAttr != 0 ? usageAttr : chanceAttr), 0.0f) <= 0.0f)
            return;
    }

    // the effect is there but a chance isn't:  that chance is 1.  a few types use percent
    auto chanceOf = [this](uint16 attrID) -> float {
        if (attrID == 0)
            return 1.0f;
        float value(Attr(attrID, 1.0f));
        if (value > 1.0f)
            value /= 100.0f;
        return value;
    };
    float usage(chanceOf(usageAttr)), chance(chanceOf(chanceAttr));
    if ((usage <= 0.0f) or (chance <= 0.0f))
        return;

    double range(Attr(rangeAttr, defRange));
    if (range <= 0.0)
        return;

    uint32 duration((uint32)Attr(durationAttr, (float)defDuration));
    if (duration == 0)
        duration = defDuration;
    // process ticks are 1s
    if (duration < 1000)
        duration = 1000;

    Effect fx = Effect();
        fx.effectID = effectID;
        fx.kind = kind;
        fx.guid = guid;
        fx.usage = usage;
        fx.chance = chance;
        // "this rat carries a scram":  the usage chance is rolled once, now (npc.EwarUsageRoll 0, owner decision 6)
        fx.carried = (Rand() < usage);
        fx.usable = false;
        fx.usableFor = 0;
        fx.duration = duration;
        fx.range = range;
        fx.falloff = (falloffAttr == 0 ? 0.0 : Attr(falloffAttr, 0.0f));
        fx.v1 = v1;
        fx.v2 = v2;
        fx.nextAt = 0;
        fx.targetID = 0;
    m_effects.push_back(fx);

    _log(NPC__AI_TRACE, "%s(%u): NPCEwar - effect %u kind %u: usage %.3f (%s), chance %.3f, duration %ums, range %.0f/%.0f, values %.3f/%.3f",
            m_self->GetName(), m_self->GetID(), effectID, kind, usage, (fx.carried ? "carried" : "not carried"), chance, duration,
            range, fx.falloff, v1, v2);
}

bool NPCEwar::HasTackle() const
{
    for (auto& cur : m_effects)
        if ((cur.kind == Ewar::Scramble) or (cur.kind == Ewar::Web))
            return true;
    return false;
}

double NPCEwar::GetMaxRange() const
{
    double range(0.0);
    for (auto& cur : m_effects)
        if ((Optimal(cur) + cur.falloff) > range)
            range = Optimal(cur) + cur.falloff;
    return range;
}

bool NPCEwar::IsEwarTarget(SystemEntity* pTarget) const
{
    // ships only.  not pods, drones or structures
    if ((pTarget == nullptr) or pTarget->IsDead() or !pTarget->IsShipSE())
        return false;
    if ((pTarget->SysBubble() == nullptr) or (pTarget->SysBubble() != m_self->SysBubble()))
        return false;
    if (pTarget->HasPilot() and (pTarget->GetPilot()->InPod() or pTarget->GetPilot()->IsInvul()))
        return false;
    if ((pTarget->DestinyMgr() == nullptr) or pTarget->DestinyMgr()->IsCloaked() or pTarget->DestinyMgr()->IsWarping())
        return false;
    // sieged/triaged ships and supercapitals are immune
    if (pTarget->GetSelf()->GetAttribute(AttrDisallowOffensiveModifiers).get_bool())
        return false;
    return true;
}

double NPCEwar::Optimal(const Effect& fx) const
{
    if ((fx.kind == Ewar::Scramble) or (fx.kind == Ewar::Web))
        return fx.range * m_tackleMult;
    return fx.range;
}

bool NPCEwar::InRange(const Effect& fx, SystemEntity* pTarget)
{
    double distance(m_self->GetPosition().distance(pTarget->GetPosition()) - m_self->GetRadius() - pTarget->GetRadius());
    double optimal(Optimal(fx));
    if (distance <= optimal)
        return true;
    if (fx.falloff <= 0.0)
        return false;
    // the jam roll handles falloff itself (Ewar::RollJam).  the jammer keeps trying out to 3x falloff, like a module
    if (fx.kind == Ewar::Jam)
        return (distance <= (optimal + (3 * fx.falloff)));
    // the client's range chance past optimal: 2^(-((d - range) / falloff)^2)
    return (Rand() < NpcMath::EwarRangeChance(distance, optimal, fx.falloff));
}

void NPCEwar::Process(SystemEntity* pPrimary, SystemEntity* pSecondary/*nullptr*/)
{
    SystemEntity* pTarget(pSecondary != nullptr ? pSecondary : pPrimary);
    if (!IsEwarTarget(pTarget))
        pTarget = nullptr;

    int64 now(GetSteadyTime());
    for (auto& fx : m_effects)
        Step(fx, pTarget, now);
}

void NPCEwar::Process(SystemEntity* pPrimary, SystemEntity* const* pSecondaries, size_t count)
{
    if (!IsEwarTarget(pPrimary))
        pPrimary = nullptr;
    int64 now(GetSteadyTime());
    for (auto& fx : m_effects) {
        // the first secondary within this effect's reach (surface distance), else the primary
        SystemEntity* pTarget(pPrimary);
        double reach(Optimal(fx) + (fx.kind == Ewar::Jam ? 3 * fx.falloff : fx.falloff));
        for (size_t i = 0; i < count; ++i) {
            SystemEntity* pSE(pSecondaries[i]);
            if ((pSE == nullptr) or (pSE == pPrimary) or !IsEwarTarget(pSE))
                continue;
            double distance(m_self->GetPosition().distance(pSE->GetPosition()) - m_self->GetRadius() - pSE->GetRadius());
            if (distance <= reach) {
                pTarget = pSE;
                break;
            }
        }
        Step(fx, pTarget, now);
    }
}

void NPCEwar::Step(Effect& fx, SystemEntity* pTarget, int64 now)
{
    // target changed or gone
    if ((fx.targetID != 0) and ((pTarget == nullptr) or (fx.targetID != pTarget->GetID())))
        EndSession(fx);
    if (pTarget == nullptr) {
        fx.nextAt = 0;
        return;
    }
    // the first roll comes right away, then every duration (drift free, like NpcSchedule)
    if (fx.nextAt != 0) {
        if (now < fx.nextAt)
            return;
        fx.nextAt = (((now - fx.nextAt) > (2 * static_cast<int64>(fx.duration))) ? now : fx.nextAt) + fx.duration;
    } else {
        fx.nextAt = now + fx.duration;
    }
    Cycle(fx, pTarget);
}

bool NPCEwar::Uses(Effect& fx, SystemEntity* pTarget)
{
    // the usage chance (504 scram, 512 web;  1 for the others), when npc.EwarUsageRoll says it's rolled
    switch (sConfig.npc.EwarUsageRoll) {
        case 1: {
            // per new target
            if (fx.usableFor != pTarget->GetID()) {
                fx.usableFor = pTarget->GetID();
                fx.usable = (Rand() < fx.usage);
            }
            return fx.usable;
        }
        case 2: {
            // every cycle (the phase 0-1 behaviour:  a 25% scrammer scrams almost surely within a minute)
            return (Rand() < fx.usage);
        }
    }
    // once per spawn (0, the default)
    return fx.carried;
}

void NPCEwar::Cycle(Effect& fx, SystemEntity* pTarget)
{
    if (!Uses(fx, pTarget) or (Rand() >= fx.chance) or !InRange(fx, pTarget)) {
        // not this time.  whatever we did last cycle ends now
        if (fx.targetID != 0)
            EndSession(fx);
        return;
    }

    if (fx.targetID == 0)
        StartSession(fx, pTarget);

    switch (fx.kind) {
        case Neut: {
            Ewar::DrainCapacitor(pTarget, fx.v1);
        } break;
        case Ewar::Jam: {
            // every cycle rolls again against the target's sensors
            Ewar::EndJam(m_self, 0, pTarget);
            if (Ewar::RollJam(m_selfRef, m_self->GetPosition(), pTarget, AttrEntityTargetJamMaxRange, AttrEntityTargetJamFallOff, m_self->GetRadius()))
                Ewar::StartJam(m_self, 0, pTarget, fx.duration);
        } break;
        default: {
            // one second of overlap keeps the effect from dropping between cycles
            pTarget->GetEwar(true)->Apply(m_self->GetID(), fx.kind, fx.duration + 1000, fx.v1, fx.v2,
                                          (fx.kind == Ewar::Web ? m_selfRef : InventoryItemRef(nullptr)));
        } break;
    }
}

void NPCEwar::StartSession(Effect& fx, SystemEntity* pTarget)
{
    fx.targetID = pTarget->GetID();
    SendFX(fx, fx.targetID, true);

    // EwarState tells the target's pilot about lasting kinds.  the jammer's and neut's icons are ours to send
    switch (fx.kind) {
        case Ewar::Jam: {
            Ewar::SendEwarNotify(true, m_self->GetID(), m_self->GetID(), pTarget, "electronic");
            if (pTarget->HasPilot()) {
                PyDict* args = new PyDict();
                    args->SetItemString("jammerShipName", new PyString(m_self->GetName()));
                pTarget->GetPilot()->SendRemoteMessage("TargetJammedBy", args);
            }
        } break;
        case Neut: {
            Ewar::SendEwarNotify(true, m_self->GetID(), m_self->GetID(), pTarget, "ewEnergyNeut");
        } break;
    }
}

void NPCEwar::EndSession(Effect& fx)
{
    uint32 targetID(fx.targetID);
    fx.targetID = 0;
    if (targetID == 0)
        return;

    SendFX(fx, targetID, false);

    SystemEntity* pTarget(m_self->SystemMgr() == nullptr ? nullptr : m_self->SystemMgr()->GetSE(targetID));
    if (pTarget == nullptr)
        return;
    switch (fx.kind) {
        case Ewar::Jam: {
            Ewar::EndJam(m_self, 0, pTarget);
            Ewar::SendEwarNotify(false, m_self->GetID(), m_self->GetID(), pTarget, "electronic");
        } break;
        case Neut: {
            Ewar::SendEwarNotify(false, m_self->GetID(), m_self->GetID(), pTarget, "ewEnergyNeut");
        } break;
        default: {
            if (pTarget->GetEwar() != nullptr)
                pTarget->GetEwar()->Remove(m_self->GetID(), fx.kind);
        } break;
    }
}

void NPCEwar::StopAll()
{
    for (auto& fx : m_effects) {
        if (fx.targetID != 0)
            EndSession(fx);
        fx.nextAt = 0;
    }
}

void NPCEwar::SendFX(const Effect& fx, uint32 targetID, bool start)
{
    // sentries have no destiny manager.  the target's bubble sees the same thing
    DestinyManager* pDestiny(m_self->DestinyMgr());
    if (pDestiny == nullptr) {
        SystemEntity* pTarget(m_self->SystemMgr() == nullptr ? nullptr : m_self->SystemMgr()->GetSE(targetID));
        if (pTarget != nullptr)
            pDestiny = pTarget->DestinyMgr();
    }
    if (pDestiny == nullptr)
        return;

    // offensive, so the client brackets us as attacking while it runs (tactical.py OnSpecialFX)
    pDestiny->SendSpecialEffect(m_self->GetID(), m_self->GetID(), m_selfRef->typeID(), targetID, 0, fx.guid,
                                true, start, start, (start ? fx.duration : 0), (start ? 1000 : 0));
}
