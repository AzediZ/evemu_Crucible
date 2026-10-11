 /**
  * @name NPCEwar.cpp
  *   electronic warfare used by npcs and non-law sentries
  *
  *   effect (id)                   chance                      duration (default)      range / falloff       values
  *   warpScrambleForEntity (563)   entityWarpScrambleChance    warpScrambleDuration    warpScrambleRange     warpScrambleStrength
  *   modifyTargetSpeed2 (575)      modifyTargetSpeedChance     modifyTargetSpeedDur.   modifyTargetSpeedRange  speedFactor
  *   entityTargetJam (1871)        entityTargetJamDurChance    entityTargetJamDur.     936 / 953             scan*StrengthBonus
  *   entityCapacitorDrain (1872)   entityCapDrainDurChance     entityCapDrainDur.      937 / -               entityCapacitorDrainAmount
  *   entitySensorDampen (1878)     entitySensorDampenDurChance entitySensorDampenDur.  938 / 950             maxTargetRange/scanResolutionMultiplier
  *   entityTrackingDisruptOld(1877) entityTrackingDisruptDurCh. entityTrackingDisrDur. 940 / 951             entityTrackingDisruptMultiplier
  *   entityTargetPaint (1879)      entityTargetPaintDurChance  entityTargetPaintDur.   941 / 954             signatureRadiusBonus
  *
  *   chances are 0..1 ("chance of npc effect to be activated each duration"); a few types use percent (> 1)
  */

#include "eve-server.h"

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "npc/NPCEwar.h"
#include "ship/modules/EwarModule.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemEntity.h"
#include "system/SystemManager.h"


NPCEwar::NPCEwar(SystemEntity* pSelf)
: m_self(pSelf),
m_selfRef(pSelf->GetSelf())
{
    m_effects.clear();

    // scram.  an npc scram with no strength data still scrams
    float strength(Attr(AttrWarpScrambleStrength, 1.0f));
    if (strength <= 0.0f)
        strength = 1.0f;
    AddEffect(EVEEffectID::warpScrambleForEntity, Ewar::Scramble, "effects.WarpScramble", AttrEntityWarpScrambleChance,
              AttrWarpScrambleDuration, 8000, AttrWarpScrambleRange, 0, 0, strength, 0);

    // web.  only real webs, and nothing that would turn the target's speed negative
    float speedFactor(m_selfRef->GetAttribute(AttrSpeedFactor).get_float());
    if ((speedFactor < 0.0f) and (speedFactor > -100.0f))
        AddEffect(EVEEffectID::modifyTargetSpeed2, Ewar::Web, "effects.ModifyTargetSpeed", AttrModifyTargetSpeedChance,
                  AttrModifyTargetSpeedDuration, 5000, AttrModifyTargetSpeedRange, 20000, 0, speedFactor, 0);

    // ecm.  the effect has no guid; the client shows the player ecm beam
    AddEffect(EVEEffectID::entityTargetJam, Ewar::Jam, "effects.ElectronicAttributeModifyTarget", AttrEntityTargetJamDurationChance,
              AttrEntityTargetJamDuration, 30000, AttrEntityTargetJamMaxRange, 0, AttrEntityTargetJamFallOff, 0, 0);

    // energy neutralizer (GJ per duration)
    float amount(Attr(AttrEntityCapacitorDrainAmount, 0.0f));
    if (amount > 0.0f)
        AddEffect(EVEEffectID::entityCapacitorDrain, Neut, "effects.EnergyVampire", AttrEntityCapacitorDrainDurationChance,
                  AttrEntityCapacitorDrainDuration, 30000, AttrEntityCapacitorDrainMaxRange, 0, AttrEntityCapacitorFallOff, amount, 0);

    // sensor dampener: target range and scan resolution multipliers
    float rangeMult(Attr(AttrMaxTargetRangeMultiplier, Attr(AttrEntitySensorDampenMultiplier, 1.0f)));
    float scanMult(Attr(AttrScanResolutionMultiplier, Attr(AttrEntitySensorDampenMultiplier, 1.0f)));
    if (rangeMult <= 0.0f)
        rangeMult = 1.0f;
    if (scanMult <= 0.0f)
        scanMult = 1.0f;
    if ((rangeMult < 1.0f) or (scanMult < 1.0f))
        AddEffect(EVEEffectID::entitySensorDampen, Ewar::Damp, "effects.ElectronicAttributeModifyTarget", AttrEntitySensorDampenDurationChance,
                  AttrEntitySensorDampenDuration, 30000, AttrEntitySensorDampenMaxRange, 0, AttrEntitySensorDampenFallOff, rangeMult, scanMult);

    // tracking disruptor: turret tracking and optimal range
    float trackMult(Attr(AttrEntityTrackingDisruptMultiplier, 1.0f));
    if ((trackMult > 0.0f) and (trackMult < 1.0f))
        AddEffect(EVEEffectID::entityTrackingDisruptOld, Ewar::TrackDisrupt, "effects.ElectronicAttributeModifyTarget", AttrEntityTrackingDisruptDurationChance,
                  AttrEntityTrackingDisruptDuration, 30000, AttrEntityTrackingDisruptMaxRange, 0, AttrEntityTrackingDisruptFallOff, trackMult, trackMult);

    // target painter: signature radius (bonus in %)
    float sigBonus(Attr(AttrSignatureRadiusBonus, 0.0f));
    float sigMult(sigBonus > 0.0f ? (1.0f + (sigBonus / 100.0f)) : Attr(AttrEntityTargetPaintMultiplier, 1.0f));
    if (sigMult > 1.0f)
        AddEffect(EVEEffectID::entityTargetPaint, Ewar::Paint, "effects.TargetPaint", AttrEntityTargetPaintDurationChance,
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

void NPCEwar::AddEffect(uint16 effectID, uint8 kind, const char* guid, uint16 chanceAttr, uint16 durationAttr, uint32 defDuration,
                        uint16 rangeAttr, double defRange, uint16 falloffAttr, float v1, float v2)
{
    if (!m_selfRef->type().HasEffect(effectID)) {
        // types without any effect data fall back to their attributes
        if (m_selfRef->type().HasEffects())
            return;
        if (Attr(chanceAttr, 0.0f) <= 0.0f)
            return;
    }

    // the effect is there but the chance isn't: the effect always runs
    float chance(Attr(chanceAttr, 1.0f));
    if (chance > 1.0f)
        chance /= 100.0f;      // a few types use percent
    if (chance <= 0.0f)
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
        fx.chance = chance;
        fx.duration = duration;
        fx.range = range;
        fx.falloff = (falloffAttr == 0 ? 0.0 : Attr(falloffAttr, 0.0f));
        fx.v1 = v1;
        fx.v2 = v2;
        fx.targetID = 0;
    fx.timer.Disable();
    m_effects.push_back(fx);

    _log(NPC__AI_TRACE, "%s(%u): NPCEwar - effect %u kind %u: chance %.3f, duration %ums, range %.0f/%.0f, values %.3f/%.3f",
            m_self->GetName(), m_self->GetID(), effectID, kind, chance, duration, range, fx.falloff, v1, v2);
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
        if ((cur.range + cur.falloff) > range)
            range = cur.range + cur.falloff;
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

bool NPCEwar::InRange(const Effect& fx, SystemEntity* pTarget) const
{
    double distance(m_self->GetPosition().distance(pTarget->GetPosition()) - m_self->GetRadius() - pTarget->GetRadius());
    if (distance <= fx.range)
        return true;
    if (fx.falloff <= 0.0)
        return false;
    // the jam roll handles falloff itself (Ewar::RollJam).  the jammer keeps trying out to 3x falloff, like a module
    if (fx.kind == Ewar::Jam)
        return (distance <= (fx.range + (3 * fx.falloff)));
    // the client's range chance past optimal: 2^(-((d - range) / falloff)^2)
    double over(distance - fx.range);
    return (MakeRandomFloat() <= std::pow(2.0, -(over * over) / (fx.falloff * fx.falloff)));
}

void NPCEwar::Process(SystemEntity* pPrimary, SystemEntity* pSecondary/*nullptr*/)
{
    SystemEntity* pTarget(pSecondary != nullptr ? pSecondary : pPrimary);
    if (!IsEwarTarget(pTarget))
        pTarget = nullptr;

    for (auto& fx : m_effects) {
        // target changed or gone
        if ((fx.targetID != 0) and ((pTarget == nullptr) or (fx.targetID != pTarget->GetID())))
            EndSession(fx);
        if (pTarget == nullptr) {
            fx.timer.Disable();
            continue;
        }
        // the first roll comes right away, then every duration
        if (fx.timer.Enabled()) {
            if (!fx.timer.Check())
                continue;
        } else {
            fx.timer.Start(fx.duration);
        }
        Cycle(fx, pTarget);
    }
}

void NPCEwar::Cycle(Effect& fx, SystemEntity* pTarget)
{
    if ((MakeRandomFloat() >= fx.chance) or !InRange(fx, pTarget)) {
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
        fx.timer.Disable();
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
