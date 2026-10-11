
/**
 * @name POS_AI.cpp
 *   Class for POS Weapon Artificial Intelligence.
 *
 * @Author:          Allan
 * @date:   28 December 17
 * @version:          0.10
 */

/*
 * POS__ERROR
 * POS__WARNING
 * POS__MESSAGE
 * POS__DEBUG
 * POS__TRACE
 */

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "inventory/AttributeEnum.h"
#include "inventory/Inventory.h"
#include "npc/NPCAI.h"
#include "pos/POS_AI.h"
#include "pos/Structure.h"
#include "pos/Tower.h"
#include "ship/modules/EwarModule.h"
#include "ship/modules/TurretFormulas.h"
#include "system/Damage.h"
#include "system/DestinyManager.h"
#include "system/EwarState.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"

/* notes for this ai class...
 *
    tower 'see' distance = AttrProximityRange  250000

 *  battery attributes (Crucible data):  speed (51) rate of fire, maxRange (54) optimal, falloff (158), trackingSpeed (160),
 *   optimalSigRadius (620), damageMultiplier (64), scanResolution (564), maxLockedTargets (192),
 *   missileLaunchDuration (506), entityFlyRange (416), duration (73) for ewar, warpScrambleRange (103)/Strength (105),
 *   speedFactor (20), maxTargetRangeMultiplier (237)/scanResolutionMultiplier (565), scan*StrengthBonus (238-241),
 *   energyDestabilizationAmount (97)/Range (98), signatureRadiusBonus (554)
 *  tower bonuses (percent):  laser damage 728 / optimal 750, hybrid damage 766 / optimal 751, projectile optimal 752 /
 *   falloff 753 / rof 754, missile rof 755 (velocity 792), ewar rof 764
 */

POS_AI::POS_AI(StructureSE* pSE)
: m_self(pSE),
m_selfRef(pSE->GetSelf()),
m_kind(Kind::Turret),
m_ewarKind(Ewar::None),
m_guid("effects.Laser"),
m_scanTimer(5000),
m_fireTimer(0),
m_targetID(0),
m_ewarTargetID(0),
m_manualTargetID(0),
m_manualFire(false)
{
    m_fireTimer.Disable();

    switch (m_selfRef->groupID()) {
        case EVEDB::invGroups::Mobile_Missile_Sentry: {
            m_kind = Kind::Launcher;
            m_guid = "";
        } break;
        case EVEDB::invGroups::Mobile_Projectile_Sentry: {
            m_guid = "effects.ProjectileFired";
        } break;
        case EVEDB::invGroups::Mobile_Hybrid_Sentry: {
            m_guid = "effects.HybridFire";
        } break;
        case EVEDB::invGroups::Mobile_Laser_Sentry: {
            m_guid = "effects.Laser";
        } break;
        case EVEDB::invGroups::Electronic_Warfare_Battery: {
            m_kind = Kind::Ewar;
            m_ewarKind = Ewar::Jam;
            m_guid = "effects.ElectronicAttributeModifyTarget";
        } break;
        case EVEDB::invGroups::Sensor_Dampening_Battery: {
            m_kind = Kind::Ewar;
            m_ewarKind = Ewar::Damp;
            m_guid = "effects.ElectronicAttributeModifyTarget";
        } break;
        case EVEDB::invGroups::Stasis_Webification_Battery: {
            m_kind = Kind::Ewar;
            m_ewarKind = Ewar::Web;
            m_guid = "effects.ModifyTargetSpeed";
        } break;
        case EVEDB::invGroups::Warp_Scrambling_Battery: {
            m_kind = Kind::Ewar;
            m_ewarKind = Ewar::Scramble;
            m_guid = "effects.WarpScramble";
        } break;
        case EVEDB::invGroups::Energy_Neutralizing_Battery: {
            m_kind = Kind::Ewar;
            m_ewarKind = Neut;
            m_guid = "effects.EnergyVampire";
        } break;
        case EVEDB::invGroups::Target_Painting_Battery: {
            m_kind = Kind::Ewar;
            m_ewarKind = Ewar::Paint;
            m_guid = "effects.TargetPaint";
        } break;
    }
}

POS_AI::~POS_AI()
{
    // Stop() is the owner's job while it is still in space
}

float POS_AI::TowerBonus(uint16 attrID)
{
    TowerSE* pTower(m_self->GetControlTower());
    if (pTower == nullptr)
        return 1.0f;
    EvilNumber value(EvilZero);
    if (!pTower->GetSelf()->HasAttribute(attrID, value))
        return 1.0f;
    return (1.0f + (value.get_float() / 100.0f));
}

uint32 POS_AI::GetCycleTime()
{
    float time(0.0f);
    switch (m_kind) {
        case Kind::Turret: {
            time = m_selfRef->GetAttribute(AttrSpeed).get_float();
            if (m_selfRef->groupID() == EVEDB::invGroups::Mobile_Projectile_Sentry)
                time *= TowerBonus(AttrControlTowerProjectileROFBonus);
        } break;
        case Kind::Launcher: {
            time = m_selfRef->GetAttribute(AttrMissileLaunchDuration).get_float();
            time *= TowerBonus(AttrControlTowerMissileROFBonus);
        } break;
        case Kind::Ewar: {
            time = m_selfRef->GetAttribute(AttrDuration).get_float();
            time *= TowerBonus(AttrControlTowerEwRofBonus);
        } break;
    }
    if (time < 1000.0f)
        time = 1000.0f;
    return (uint32)time;
}

InventoryItemRef POS_AI::GetCharge()
{
    std::vector<InventoryItemRef> items;
    m_selfRef->GetMyInventory()->GetItemsByFlag(flagHiSlot0, items);
    for (auto cur : items)
        if ((cur->categoryID() == EVEDB::invCategories::Charge) and (cur->quantity() > 0))
            return cur;
    return InventoryItemRef(nullptr);
}

double POS_AI::GetRange()
{
    switch (m_kind) {
        case Kind::Turret: {
            float optimal(m_selfRef->GetAttribute(AttrMaxRange).get_float());
            float falloff(m_selfRef->GetAttribute(AttrFalloff).get_float());
            switch (m_selfRef->groupID()) {
                case EVEDB::invGroups::Mobile_Laser_Sentry:         optimal *= TowerBonus(AttrControlTowerLaserOptimalBonus); break;
                case EVEDB::invGroups::Mobile_Hybrid_Sentry:        optimal *= TowerBonus(AttrControlTowerHybridOptimalBonus); break;
                case EVEDB::invGroups::Mobile_Projectile_Sentry: {
                    optimal *= TowerBonus(AttrControlTowerProjectileOptimalBonus);
                    falloff *= TowerBonus(AttrControlTowerProjectileFallOffBonus);
                } break;
            }
            return (optimal + falloff);
        }
        case Kind::Launcher: {
            // how far the loaded missile flies, with the battery's missile multipliers
            InventoryItemRef charge(GetCharge());
            if (charge.get() != nullptr) {
                double velocity(charge->GetAttribute(AttrMaxVelocity).get_double());
                double flightTime(charge->GetAttribute(AttrExplosionDelay).get_double() / 1000.0);
                if (m_selfRef->HasAttribute(AttrMissileEntityVelocityMultiplier))
                    velocity *= m_selfRef->GetAttribute(AttrMissileEntityVelocityMultiplier).get_double();
                if (m_selfRef->HasAttribute(AttrMissileEntityFlightTimeMultiplier))
                    flightTime *= m_selfRef->GetAttribute(AttrMissileEntityFlightTimeMultiplier).get_double();
                if (velocity * flightTime > 0)
                    return (velocity * flightTime);
            }
            return m_selfRef->GetAttribute(AttrEntityFlyRange).get_double();
        }
        case Kind::Ewar: {
            switch (m_ewarKind) {
                case Ewar::Scramble:    return m_selfRef->GetAttribute(AttrWarpScrambleRange).get_double();
                case Neut:              return m_selfRef->GetAttribute(AttrEnergyDestabilizationRange).get_double();
                // the jammer keeps trying out to 3x falloff (Ewar::RollJam rolls the range)
                case Ewar::Jam:         return (m_selfRef->GetAttribute(AttrMaxRange).get_double() + 3 * m_selfRef->GetAttribute(AttrFalloff).get_double());
            }
            return m_selfRef->GetAttribute(AttrMaxRange).get_double();
        }
    }
    return 0.0;
}

float POS_AI::GetLockTime(SystemEntity* pTarget)
{
    // the client's lock time:  40000 / (scanResolution * asinh(signatureRadius)^2) seconds
    float scanRes(m_selfRef->GetAttribute(AttrScanResolution).get_float());
    if (m_self->GetEwar() != nullptr)
        scanRes *= m_self->GetEwar()->GetScanResMult();
    float sig(pTarget->GetSignatureRadius());
    if (sig < 1.0f)
        sig = 1.0f;
    if (scanRes < 1.0f)
        scanRes = 1.0f;
    float asinhSig(std::log(sig + std::sqrt(sig * sig + 1.0f)));
    float time(40000000.0f / (scanRes * asinhSig * asinhSig));
    if (time < 1000.0f)
        time = 1000.0f;
    return time;
}

bool POS_AI::IsEngageable(SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or pTarget->IsDead() or (pTarget->SysBubble() == nullptr) or (pTarget->DestinyMgr() == nullptr))
        return false;
    if (pTarget->SystemMgr() != m_self->SystemMgr())
        return false;
    if (pTarget->DestinyMgr()->IsCloaked() or pTarget->DestinyMgr()->IsWarping())
        return false;
    if (pTarget->HasPilot() and pTarget->GetPilot()->IsInvul())
        return false;
    // ewar works on ships only (not pods)
    if (m_kind == Kind::Ewar) {
        if (!pTarget->IsShipSE())
            return false;
        if (pTarget->HasPilot() and pTarget->GetPilot()->InPod())
            return false;
        // sieged/triaged ships and supercapitals are immune
        if (pTarget->GetSelf()->GetAttribute(AttrDisallowOffensiveModifiers).get_bool())
            return false;
    }
    double distance(m_self->GetPosition().distance(pTarget->GetPosition()) - pTarget->GetRadius());
    return (distance <= GetRange());
}

SystemEntity* POS_AI::FindTarget(TowerSE* pTower)
{
    // the closest hostile in reach
    std::map<uint32, SystemEntity*> entities;
    m_self->SysBubble()->GetEntities(entities);
    SystemEntity* pBest(nullptr);
    double best(-1);
    for (auto cur : entities) {
        SystemEntity* pSE(cur.second);
        if ((pSE == nullptr) or !(pSE->IsShipSE() or pSE->IsDroneSE() or pSE->IsNPCSE()))
            continue;
        if (!IsEngageable(pSE) or !pTower->IsHostile(pSE))
            continue;
        double distance(m_self->GetPosition().distance(pSE->GetPosition()));
        if ((best < 0) or (distance < best)) {
            best = distance;
            pBest = pSE;
        }
    }
    return pBest;
}

void POS_AI::Process()
{
    TowerSE* pTower(m_self->GetControlTower());
    bool controlled(m_self->GetControllerID() != 0);
    // an online battery of a working tower.  a controlled battery fights for its controller only
    if ((pTower == nullptr) or !m_self->IsOnline() or !pTower->IsPowered()
    or (!controlled and !sConfig.pos.GunsEnabled) or (m_self->SysBubble() == nullptr)) {
        if (m_targetID != 0)
            Stop();
        return;
    }

    SystemEntity* pTarget(nullptr);
    if (controlled) {
        // the controller's locked target, while they want it fired at
        pTarget = (m_manualTargetID == 0 ? nullptr : m_self->TargetMgr()->GetTarget(m_manualTargetID, true));
        if ((pTarget != nullptr) and !IsEngageable(pTarget))
            pTarget = nullptr;
        if ((pTarget == nullptr) or !m_manualFire) {
            if (m_ewarTargetID != 0)
                EndEwar();
            m_fireTimer.Disable();
            return;
        }
        m_targetID = pTarget->GetID();
    } else {
        // the target we have, while it stays a valid one
        if (m_targetID != 0) {
            pTarget = m_self->SystemMgr()->GetSE(m_targetID);
            if ((pTarget == nullptr) or !IsEngageable(pTarget) or !pTower->IsHostile(pTarget)) {
                if (pTarget != nullptr)
                    m_self->TargetMgr()->ClearTarget(pTarget);
                if (m_ewarTargetID != 0)
                    EndEwar();
                m_targetID = 0;
                m_fireTimer.Disable();
                pTarget = nullptr;
            }
        }
        if (pTarget == nullptr) {
            if (!m_scanTimer.Check())
                return;
            pTarget = FindTarget(pTower);
            if (pTarget == nullptr)
                return;
            bool chase(false);
            int64 lockable(m_selfRef->GetAttribute(AttrMaxLockedTargets).get_int());
            uint8 maxLocked(lockable > 1 ? (uint8)lockable : 1);
            if (!m_self->TargetMgr()->StartTargeting(pTarget, GetLockTime(pTarget), maxLocked, GetRange() + pTarget->GetRadius(), chase))
                return;
            m_targetID = pTarget->GetID();
            _log(POS__TRACE, "POS_AI - %s(%u) is locking %s(%u).", m_self->GetName(), m_self->GetID(), pTarget->GetName(), pTarget->GetID());
            return;
        }
        // locked yet?
        if (m_self->TargetMgr()->GetTarget(m_targetID, true) == nullptr)
            return;
    }

    if (!m_fireTimer.Enabled()) {
        // first shot right away
        m_fireTimer.Start(GetCycleTime());
        Fire(pTarget);
        return;
    }
    if (m_fireTimer.Check())
        Fire(pTarget);
}

void POS_AI::Fire(SystemEntity* pTarget)
{
    switch (m_kind) {
        case Kind::Turret:      FireTurret(pTarget);    break;
        case Kind::Launcher:    FireMissile(pTarget);   break;
        case Kind::Ewar:        CycleEwar(pTarget);     break;
    }
}

void POS_AI::FireTurret(SystemEntity* pTarget)
{
    InventoryItemRef charge(GetCharge());
    if (charge.get() == nullptr)
        return;     // no ammo, no shot

    float optimal(m_selfRef->GetAttribute(AttrMaxRange).get_float());
    float falloff(m_selfRef->GetAttribute(AttrFalloff).get_float());
    float damageBonus(1.0f);
    switch (m_selfRef->groupID()) {
        case EVEDB::invGroups::Mobile_Laser_Sentry: {
            optimal *= TowerBonus(AttrControlTowerLaserOptimalBonus);
            damageBonus = TowerBonus(AttrControlTowerLaserDamageBonus);
        } break;
        case EVEDB::invGroups::Mobile_Hybrid_Sentry: {
            optimal *= TowerBonus(AttrControlTowerHybridOptimalBonus);
            damageBonus = TowerBonus(AttrControlTowerHybridDamageBonus);
        } break;
        case EVEDB::invGroups::Mobile_Projectile_Sentry: {
            optimal *= TowerBonus(AttrControlTowerProjectileOptimalBonus);
            falloff *= TowerBonus(AttrControlTowerProjectileFallOffBonus);
        } break;
    }
    TurretFormulas formula;
    float toHit(formula.GetStructureToHit(m_self, optimal, falloff, m_selfRef->GetAttribute(AttrTrackingSpeed).get_float(),
                                          m_selfRef->GetAttribute(AttrOptimalSigRadius).get_float(), pTarget));

    // the shot (the battery has no destiny of its own that the target's bubble doesn't share)
    pTarget->DestinyMgr()->SendSpecialEffect(m_self->GetID(), m_self->GetID(), m_selfRef->typeID(), pTarget->GetID(),
                                             charge->typeID(), m_guid, 1, 1, 1, GetCycleTime(), 0);

    // frequency crystals last, ammunition is used up
    uint16 chargeTypeID(charge->typeID());
    if (m_selfRef->groupID() != EVEDB::invGroups::Mobile_Laser_Sentry) {
        if (charge->quantity() > 1) {
            charge->AlterQuantity(-1, true);
        } else {
            charge->SetQuantity(0, true, true);
        }
    }

    if (toHit <= 0.0f)
        return;
    const ItemType* pType(sItemFactory.GetType(chargeTypeID));
    if (pType == nullptr)
        return;
    float mult(m_selfRef->GetAttribute(AttrDamageMultiplier).get_float() * damageBonus);
    Damage d(m_self, m_selfRef,
             pType->GetAttribute(AttrKineticDamage).get_float(),
             pType->GetAttribute(AttrThermalDamage).get_float(),
             pType->GetAttribute(AttrEmDamage).get_float(),
             pType->GetAttribute(AttrExplosiveDamage).get_float(),
             toHit, EVEEffectID::targetAttack);
    d *= mult;
    pTarget->ApplyDamage(d);
}

void POS_AI::FireMissile(SystemEntity* pTarget)
{
    InventoryItemRef charge(GetCharge());
    if (charge.get() == nullptr)
        return;
    uint16 typeID(charge->typeID());
    if (charge->quantity() > 1) {
        charge->AlterQuantity(-1, true);
    } else {
        charge->SetQuantity(0, true, true);
    }
    // the tower's missile velocity bonus isn't applied (the entity missile helper has no multiplier for it)
    LaunchEntityMissile(m_self, m_selfRef, typeID, pTarget);
}

void POS_AI::CycleEwar(SystemEntity* pTarget)
{
    uint32 duration(GetCycleTime());
    // a new target:  the old session ends, the beam starts
    if (m_ewarTargetID != pTarget->GetID()) {
        if (m_ewarTargetID != 0)
            EndEwar();
        m_ewarTargetID = pTarget->GetID();
        pTarget->DestinyMgr()->SendSpecialEffect(m_self->GetID(), m_self->GetID(), m_selfRef->typeID(), m_ewarTargetID, 0, m_guid,
                                                 true, true, true, duration, 1000);
        if (m_ewarKind == Neut)
            Ewar::SendEwarNotify(true, m_self->GetID(), m_self->GetID(), pTarget, "ewEnergyNeut");
        if (m_ewarKind == Ewar::Jam)
            Ewar::SendEwarNotify(true, m_self->GetID(), m_self->GetID(), pTarget, "electronic");
    }

    switch (m_ewarKind) {
        case Neut: {
            Ewar::DrainCapacitor(pTarget, m_selfRef->GetAttribute(AttrEnergyDestabilizationAmount).get_float());
        } break;
        case Ewar::Jam: {
            // every cycle rolls again against the target's sensors
            Ewar::EndJam(m_self, 0, pTarget);
            if (Ewar::RollJam(m_selfRef, m_self->GetPosition(), pTarget, AttrMaxRange, AttrFalloff, m_self->GetRadius()))
                Ewar::StartJam(m_self, 0, pTarget, duration);
        } break;
        case Ewar::Scramble: {
            float strength(m_selfRef->GetAttribute(AttrWarpScrambleStrength).get_float());
            pTarget->GetEwar(true)->Apply(m_self->GetID(), Ewar::Scramble, duration + 1000, (strength > 0.0f ? strength : 1.0f));
        } break;
        case Ewar::Web: {
            pTarget->GetEwar(true)->Apply(m_self->GetID(), Ewar::Web, duration + 1000, 0, 0, m_selfRef);
        } break;
        case Ewar::Damp: {
            float rangeMult(m_selfRef->GetAttribute(AttrMaxTargetRangeMultiplier).get_float());
            float scanMult(m_selfRef->GetAttribute(AttrScanResolutionMultiplier).get_float());
            pTarget->GetEwar(true)->Apply(m_self->GetID(), Ewar::Damp, duration + 1000,
                                          (rangeMult > 0.0f ? rangeMult : 1.0f), (scanMult > 0.0f ? scanMult : 1.0f));
        } break;
        case Ewar::Paint: {
            float bonus(m_selfRef->GetAttribute(AttrSignatureRadiusBonus).get_float());
            pTarget->GetEwar(true)->Apply(m_self->GetID(), Ewar::Paint, duration + 1000, 1.0f + (bonus / 100.0f));
        } break;
    }
}

void POS_AI::EndEwar()
{
    uint32 targetID(m_ewarTargetID);
    m_ewarTargetID = 0;
    if ((targetID == 0) or (m_self->SystemMgr() == nullptr))
        return;
    SystemEntity* pTarget(m_self->SystemMgr()->GetSE(targetID));
    if ((pTarget == nullptr) or (pTarget->DestinyMgr() == nullptr))
        return;
    pTarget->DestinyMgr()->SendSpecialEffect(m_self->GetID(), m_self->GetID(), m_selfRef->typeID(), targetID, 0, m_guid,
                                             true, false, false, 0, 0);
    switch (m_ewarKind) {
        case Neut: {
            Ewar::SendEwarNotify(false, m_self->GetID(), m_self->GetID(), pTarget, "ewEnergyNeut");
        } break;
        case Ewar::Jam: {
            Ewar::EndJam(m_self, 0, pTarget);
            Ewar::SendEwarNotify(false, m_self->GetID(), m_self->GetID(), pTarget, "electronic");
        } break;
        default: {
            if (pTarget->GetEwar() != nullptr)
                pTarget->GetEwar()->Remove(m_self->GetID(), m_ewarKind);
        } break;
    }
}

void POS_AI::Stop()
{
    if (m_ewarTargetID != 0)
        EndEwar();
    if (m_self->TargetMgr() != nullptr)
        m_self->TargetMgr()->ClearAllTargets();
    m_targetID = 0;
    m_fireTimer.Disable();
}
