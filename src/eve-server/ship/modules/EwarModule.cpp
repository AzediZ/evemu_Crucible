 /**
  * @name EwarModule.cpp
  *   electronic warfare helpers shared by modules, drones, npcs and structures
  */

#include "eve-server.h"

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "npc/Drone.h"
#include "ship/Ship.h"
#include "system/CrimeWatch.h"
#include "ship/modules/EwarModule.h"
#include "system/SystemBubble.h"
#include "system/SystemEntity.h"
#include "system/SystemManager.h"


const char* Ewar::GetEwarType(uint16 groupID)
{
    switch (groupID) {
        case EVEDB::invGroups::Warp_Scrambler:          return "warpScrambler";
        case EVEDB::invGroups::Stasis_Web:              return "webify";
        case EVEDB::invGroups::ECM:                     return "electronic";
        case EVEDB::invGroups::Remote_Sensor_Damper:    return "ewRemoteSensorDamp";
        case EVEDB::invGroups::Tracking_Disruptor:      return "ewTrackingDisrupt";
        case EVEDB::invGroups::Target_Painter:          return "ewTargetPaint";
        case EVEDB::invGroups::Energy_Vampire:          return "ewEnergyVampire";
        case EVEDB::invGroups::Energy_Destabilizer:     return "ewEnergyNeut";
    }
    return nullptr;
}

const char* Ewar::GetKindEwarType(uint8 kind)
{
    switch (kind) {
        case Ewar::Scramble:        return "warpScrambler";
        case Ewar::Web:             return "webify";
        case Ewar::Jam:             return "electronic";
        case Ewar::Damp:            return "ewRemoteSensorDamp";
        case Ewar::TrackDisrupt:    return "ewTrackingDisrupt";
        case Ewar::Paint:           return "ewTargetPaint";
    }
    return nullptr;
}

SystemEntity* Ewar::FindDecloaker(SystemEntity* pSE)
{
    if ((pSE == nullptr) or (pSE->SysBubble() == nullptr))
        return nullptr;

    std::vector<SystemEntity*> entities;
    pSE->SysBubble()->GetStaticEntities(entities);
    std::map<uint32, SystemEntity*> dynamics;
    pSE->SysBubble()->GetAllEntities(dynamics);
    for (auto cur : dynamics)
        entities.push_back(cur.second);

    const GPoint& pos(pSE->GetPosition());
    for (auto cur : entities) {
        if ((cur == nullptr) or (cur == pSE) or cur->IsDead())
            continue;
        if (cur->IsMissileSE() or cur->IsProbeSE() or cur->IsFieldSE() or cur->IsBeltSE() or cur->IsAnomalySE()
        or  cur->IsInterdictionSphereSE())      // interdiction spheres don't decloak
            continue;
        // something warping through doesn't
        if ((cur->DestinyMgr() != nullptr) and cur->DestinyMgr()->IsWarping())
            continue;
        if ((pos.distance(cur->GetPosition()) - pSE->GetRadius() - cur->GetRadius()) < DecloakRange)
            return cur;
    }
    return nullptr;
}

bool Ewar::IsOffensiveModifier(uint16 groupID)
{
    // neuts, nos and weapons are not modifiers, so they still work on ewar-immune targets
    switch (groupID) {
        case EVEDB::invGroups::ECM:
        case EVEDB::invGroups::Remote_Sensor_Damper:
        case EVEDB::invGroups::Tracking_Disruptor:
        case EVEDB::invGroups::Target_Painter:
        case EVEDB::invGroups::Stasis_Web:
        case EVEDB::invGroups::Warp_Scrambler:
            return true;
    }
    return false;
}

Client* Ewar::GetController(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return nullptr;
    if (pSE->HasPilot())
        return pSE->GetPilot();
    if (pSE->IsDroneSE())
        return pSE->GetDroneSE()->GetControllerClient();
    return nullptr;
}

void Ewar::SendEwarStart(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget, const char* ewarType)
{
    if (pSource == nullptr)
        return;
    SendEwarNotify(true, pSource->GetID(), moduleID, pTarget, ewarType);
}

void Ewar::SendEwarEnd(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget, const char* ewarType)
{
    if (pSource == nullptr)
        return;
    SendEwarNotify(false, pSource->GetID(), moduleID, pTarget, ewarType);
}

void Ewar::SendEwarNotify(bool start, uint32 sourceID, uint32 moduleID, SystemEntity* pTarget, const char* ewarType)
{
    // overview.py and tactical.py only act when targetBallID is our ship, so only the target's pilot needs it
    if ((ewarType == nullptr) or (pTarget == nullptr) or !pTarget->HasPilot())
        return;
    PyTuple* tuple = new PyTuple(4);
        tuple->SetItem(0, new PyInt(sourceID));
        tuple->SetItem(1, new PyInt(moduleID));
        tuple->SetItem(2, new PyInt(pTarget->GetID()));
        tuple->SetItem(3, new PyString(ewarType));
    pTarget->GetPilot()->SendNotification((start ? "OnEwarStart" : "OnEwarEnd"), "charid", &tuple);
}

float Ewar::DrainCapacitor(SystemEntity* pTarget, float amount)
{
    if ((pTarget == nullptr) or (amount <= 0.0f))
        return 0.0f;
    InventoryItemRef tRef(pTarget->GetSelf());
    if (!tRef->HasAttribute(AttrCapacitorCharge))
        return 0.0f;
    float cap(tRef->GetAttribute(AttrCapacitorCharge).get_float());
    if (cap <= 0.0f)
        return 0.0f;
    if (amount > cap)
        amount = cap;
    tRef->SetAttribute(AttrCapacitorCharge, cap - amount);
    _log(EFFECTS__TRACE, "Ewar::DrainCapacitor() - %s(%u) loses %.1f GJ (%.1f left)", pTarget->GetName(), pTarget->GetID(), amount, cap - amount);
    return amount;
}

float Ewar::LeechCapacitor(InventoryItemRef srcRef, SystemEntity* pTarget, float amount)
{
    if ((srcRef.get() == nullptr) or (pTarget == nullptr) or (amount <= 0.0f))
        return 0.0f;
    InventoryItemRef tRef(pTarget->GetSelf());
    if (!tRef->HasAttribute(AttrCapacitorCharge))
        return 0.0f;
    // the nosferatu only drains while we have less energy than the target (absolute GJ, since the 2007 change)
    float mine(srcRef->GetAttribute(AttrCapacitorCharge).get_float());
    float theirs(tRef->GetAttribute(AttrCapacitorCharge).get_float());
    if (mine >= theirs)
        return 0.0f;
    float room(srcRef->GetAttribute(AttrCapacitorCapacity).get_float() - mine);
    if (amount > theirs)
        amount = theirs;
    if (amount > room)
        amount = room;
    if (amount <= 0.0f)
        return 0.0f;
    tRef->SetAttribute(AttrCapacitorCharge, theirs - amount);
    srcRef->SetAttribute(AttrCapacitorCharge, mine + amount);
    _log(EFFECTS__TRACE, "Ewar::LeechCapacitor() - %s(%u) takes %.1f GJ from %s(%u)", srcRef->name(), srcRef->itemID(), amount, pTarget->GetName(), pTarget->GetID());
    return amount;
}

void Ewar::SendJamNotify(bool start, uint32 sourceID, uint32 moduleID, SystemEntity* pTarget, int64 startTime/*0*/, uint32 durationMs/*0*/)
{
    if (pTarget == nullptr)
        return;

    std::vector<Client*> to;
    if (pTarget->HasPilot())
        to.push_back(pTarget->GetPilot());
    if (pTarget->SystemMgr() != nullptr) {
        Client* pSource(GetController(pTarget->SystemMgr()->GetSE(sourceID)));
        if ((pSource != nullptr) and (std::find(to.begin(), to.end(), pSource) == to.end()))
            to.push_back(pSource);
    }

    for (auto pClient : to) {
        // shipui.py and target.py OnJamStart(sourceBallID, moduleID, targetBallID, jammingType, startTime, duration)
        //  startTime is filetime, duration is ms
        PyTuple* tuple = new PyTuple(start ? 6 : 4);
            tuple->SetItem(0, new PyInt(sourceID));
            tuple->SetItem(1, new PyInt(moduleID));
            tuple->SetItem(2, new PyInt(pTarget->GetID()));
            tuple->SetItem(3, new PyString("electronic"));
        if (start) {
            tuple->SetItem(4, new PyLong(startTime));
            tuple->SetItem(5, new PyInt(durationMs));
        }
        pClient->SendNotification((start ? "OnJamStart" : "OnJamEnd"), "charid", &tuple);
    }
}

namespace {
    // jammer bonus attribute -> target strength attribute (godma.py electronicAttributes)
    const uint16 jamPairs[4][2] = {
        { AttrScanGravimetricStrengthBonus,     AttrScanGravimetricStrength },
        { AttrScanLadarStrengthBonus,           AttrScanLadarStrength },
        { AttrScanMagnetometricStrengthBonus,   AttrScanMagnetometricStrength },
        { AttrScanRadarStrengthBonus,           AttrScanRadarStrength }
    };

    float RangeChance(InventoryItemRef jammerRef, const GPoint& from, double fromRadius, SystemEntity* pTarget, uint16 rangeAttr, uint16 falloffAttr)
    {
        double distance(from.distance(pTarget->GetPosition()) - fromRadius - pTarget->GetRadius());
        double range(jammerRef->GetAttribute(rangeAttr).get_double());
        if (distance <= range)
            return 1.0f;
        double falloff(jammerRef->GetAttribute(falloffAttr).get_double());
        if (falloff <= 0)
            return 0.0f;
        double over(distance - range);
        return (float)std::pow(2.0, -(over * over) / (falloff * falloff));
    }

    float StrengthChance(InventoryItemRef jammerRef, SystemEntity* pTarget)
    {
        InventoryItemRef tRef(pTarget->GetSelf());
        bool found(false);
        float maxA(0.0f), maxD(0.0f);
        for (uint8 i = 0; i < 4; ++i) {
            float a(jammerRef->GetAttribute(jamPairs[i][0]).get_float());
            if (a < 1.0f)
                continue;
            float d(tRef->GetAttribute(jamPairs[i][1]).get_float());
            if (!found or (d > maxD)) {
                maxA = a;
                maxD = d;
                found = true;
            } else if ((d == maxD) and (a > maxA)) {
                maxA = a;
            }
        }
        if (!found or (maxD <= 0.0f))
            return 1.0f;
        return maxA / maxD;
    }
}

float Ewar::JamChance(InventoryItemRef jammerRef, const GPoint& from, SystemEntity* pTarget, uint16 rangeAttr, uint16 falloffAttr, double fromRadius/*0*/)
{
    if ((jammerRef.get() == nullptr) or (pTarget == nullptr))
        return 0.0f;
    float chance(RangeChance(jammerRef, from, fromRadius, pTarget, rangeAttr, falloffAttr) * StrengthChance(jammerRef, pTarget));
    return (chance > 1.0f ? 1.0f : chance);
}

bool Ewar::RollJam(InventoryItemRef jammerRef, const GPoint& from, SystemEntity* pTarget, uint16 rangeAttr, uint16 falloffAttr, double fromRadius/*0*/)
{
    if ((jammerRef.get() == nullptr) or (pTarget == nullptr))
        return false;
    float rangeChance(RangeChance(jammerRef, from, fromRadius, pTarget, rangeAttr, falloffAttr));
    float strengthChance(StrengthChance(jammerRef, pTarget));
    bool success((MakeRandomFloat(0, 1) <= rangeChance) and (MakeRandomFloat(0, 1) <= strengthChance));
    _log(EFFECTS__TRACE, "Ewar::RollJam() - %s(%u) on %s(%u): range chance %.3f, strength chance %.3f - %s",
            jammerRef->name(), jammerRef->itemID(), pTarget->GetName(), pTarget->GetID(), rangeChance, strengthChance, (success ? "jammed" : "failed"));
    return success;
}

void Ewar::StartJam(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget, uint32 durationMs)
{
    if ((pSource == nullptr) or (pTarget == nullptr) or (pTarget->TargetMgr() == nullptr))
        return;
    pTarget->GetEwar(true)->Apply(pSource->GetID(), Ewar::Jam, durationMs, 0, 0, InventoryItemRef(nullptr), moduleID);
}

void Ewar::EndJam(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget)
{
    if ((pSource == nullptr) or (pTarget == nullptr) or (pTarget->GetEwar() == nullptr))
        return;
    pTarget->GetEwar()->Remove(pSource->GetID(), Ewar::Jam, moduleID);
}

void Ewar::BreakLocks(SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or (pTarget->TargetMgr() == nullptr))
        return;
    pTarget->TargetMgr()->LoseAllTargets();
}


EcmModule::EcmModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef)
{
}

void EcmModule::Activate(uint16 effectID, uint32 targetID/*0*/, int16 repeat/*0*/)
{
    ActiveModule::Activate(effectID, targetID, repeat);

    if (m_ModuleState != Module::State::Activated)
        return;
    if ((groupID() != EVEDB::invGroups::ECM) or (m_targetSE == nullptr))
        return;

    // the jammer's ewar icon stays on the target's overview while the module runs, jammed or not
    Ewar::SendEwarNotify(true, m_shipRef->itemID(), itemID(), m_targetSE, "electronic");

    if (m_shipRef->HasPilot()) {
        PyDict* args = new PyDict();
            args->SetItemString("jammedShipName", new PyString(m_targetSE->GetName()));
        m_shipRef->GetPilot()->SendRemoteMessage("TargetJammedSuccess", args);
    }
    if (m_targetSE->HasPilot()) {
        PyDict* args = new PyDict();
            args->SetItemString("jammerShipName", new PyString(m_shipRef->name()));
        m_targetSE->GetPilot()->SendRemoteMessage("TargetJammedBy", args);
    }
}

void EcmModule::DeactivateCycle(bool abort/*false*/)
{
    if (m_ModuleState < Module::State::Deactivating)
        return;

    if ((groupID() == EVEDB::invGroups::ECM) and (m_targetSE != nullptr) and m_shipRef->HasPilot()) {
        Ewar::EndJam(m_shipRef->GetPilot()->GetShipSE(), itemID(), m_targetSE);
        Ewar::SendEwarNotify(false, m_shipRef->itemID(), itemID(), m_targetSE, "electronic");
    }

    ActiveModule::DeactivateCycle(abort);
}

uint32 EcmModule::DoCycle()
{
    uint32 cycleTime(ActiveModule::DoCycle());
    if ((cycleTime == 0) or !m_shipRef->HasPilot())
        return cycleTime;

    ShipSE* pShip(m_shipRef->GetPilot()->GetShipSE());
    switch (groupID()) {
        case EVEDB::invGroups::ECM: {
            if (m_targetSE == nullptr)
                break;
            // each cycle rolls again.  the last cycle's jam ends here
            Ewar::EndJam(pShip, itemID(), m_targetSE);
            // a target that went into siege/triage since we started is immune
            if (m_targetSE->GetSelf()->GetAttribute(AttrDisallowOffensiveModifiers).get_bool())
                break;
            if (Ewar::RollJam(m_modRef, pShip->GetPosition(), m_targetSE, AttrMaxRange, AttrFalloff, pShip->GetRadius()))
                Ewar::StartJam(pShip, itemID(), m_targetSE, cycleTime);
        } break;
        case EVEDB::invGroups::ECM_Burst: {
            Burst(pShip->GetPosition(), pShip->GetRadius(), pShip);
        } break;
        case EVEDB::invGroups::Remote_ECM_Burst: {
            if (m_targetSE != nullptr)
                Burst(m_targetSE->GetPosition(), m_targetSE->GetRadius(), m_targetSE);
        } break;
    }

    return cycleTime;
}

void EcmModule::Burst(const GPoint& center, double centerRadius, SystemEntity* pCenterSE)
{
    if ((m_bubble == nullptr) or (m_sysMgr == nullptr))
        return;
    Client* pPilot(m_shipRef->GetPilot());
    ShipSE* pShip(pPilot->GetShipSE());
    double reach(GetAttribute(AttrEcmBurstRange).get_double() + (3 * GetAttribute(AttrFalloff).get_double()));

    // ships, drones, npcs and sentries, friend or foe.  only their locks are broken
    std::vector<uint32> ids;
    std::map<uint32, SystemEntity*> entities;
    m_bubble->GetEntities(entities);
    if ((pCenterSE != nullptr) and (pCenterSE->SysBubble() != nullptr) and (pCenterSE->SysBubble() != m_bubble))
        pCenterSE->SysBubble()->GetEntities(entities);
    for (auto cur : entities) {
        SystemEntity* pSE(cur.second);
        if ((pSE == pShip) or pSE->IsDead() or (pSE->TargetMgr() == nullptr))
            continue;
        if (!pSE->IsShipSE() and !pSE->IsNPCSE() and !pSE->IsDroneSE() and !pSE->IsSentrySE())
            continue;
        if ((center.distance(pSE->GetPosition()) - centerRadius - pSE->GetRadius()) > reach)
            continue;
        ids.push_back(cur.first);
    }

    uint16 targeters(0), jammedTargeters(0);
    std::string jammedNames;
    for (auto id : ids) {
        SystemEntity* pSE(m_sysMgr->GetSE(id));
        if (pSE == nullptr)
            continue;
        bool targeter(pShip->TargetMgr()->IsTargetedBy(pSE));
        if (targeter)
            ++targeters;
        // ewar immune (supercapitals, sieged/triaged ships)
        if (pSE->GetSelf()->GetAttribute(AttrDisallowOffensiveModifiers).get_bool())
            continue;
        if (!Ewar::RollJam(m_modRef, center, pSE, AttrEcmBurstRange, AttrFalloff, centerRadius))
            continue;
        if (targeter)
            ++jammedTargeters;
        if (!jammedNames.empty())
            jammedNames += ", ";
        jammedNames += pSE->GetName();
        // each one we affect is an offensive act (our own drones aren't)
        if (pSE->GetOwnerID() != (uint32)pPilot->GetCharacterID())
            sCrimeWatch.OffensiveAct(pPilot, pSE);
        Ewar::BreakLocks(pSE);
    }

    // [item]moduleID localization variables take a typeID
    PyDict* args = new PyDict();
        args->SetItemString("moduleID", new PyInt(typeID()));
    if (targeters == 0) {
        pPilot->SendRemoteMessage("TargetJammingWithoutTargeters", args);
        return;
    }
    args->SetItemString("jammedNames", new PyString(jammedNames));
    if (jammedTargeters == targeters) {
        pPilot->SendRemoteMessage("TargetJammingFullSuccess", args);
    } else {
        args->SetItemString("targetCount", new PyInt(targeters));
        pPilot->SendRemoteMessage("TargetJammingPartialSuccess", args);
    }
}


RemoteEwarModule::RemoteEwarModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef)
{
}

uint8 RemoteEwarModule::GetKind()
{
    switch (groupID()) {
        case EVEDB::invGroups::Target_Painter:          return Ewar::Paint;
        case EVEDB::invGroups::Remote_Sensor_Damper:    return Ewar::Damp;
        case EVEDB::invGroups::Tracking_Disruptor:      return Ewar::TrackDisrupt;
    }
    return Ewar::None;
}

uint32 RemoteEwarModule::DoCycle()
{
    uint32 cycleTime(ActiveModule::DoCycle());
    if ((cycleTime == 0) or (m_targetSE == nullptr) or !m_shipRef->HasPilot())
        return cycleTime;
    // a target that went into siege/triage since we started is immune
    if (m_targetSE->GetSelf()->GetAttribute(AttrDisallowOffensiveModifiers).get_bool())
        return cycleTime;

    /* the module's attributes already carry the pilot's skills and its script (charge effects).  bonuses are percentages:
     *  TP signatureRadiusBonus (+), damp maxTargetRangeBonus/scanResolutionBonus (-), TD trackingSpeedBonus/maxRangeBonus (-).
     *  Crucible TDs also had falloffBonus, which the ewar state has no multiplier for (optimal and tracking only)
     *  the entry lasts a cycle and a second, and is refreshed every cycle
     */
    float v1(1.0f), v2(0.0f);
    uint8 kind(GetKind());
    switch (kind) {
        case Ewar::Paint: {
            v1 = 1.0f + (GetAttribute(AttrSignatureRadiusBonus).get_float() / 100.0f);
        } break;
        case Ewar::Damp: {
            v1 = 1.0f + (GetAttribute(AttrMaxTargetRangeBonus).get_float() / 100.0f);
            v2 = 1.0f + (GetAttribute(AttrScanResolutionBonus).get_float() / 100.0f);
        } break;
        case Ewar::TrackDisrupt: {
            v1 = 1.0f + (GetAttribute(AttrTrackingSpeedBonus).get_float() / 100.0f);
            v2 = 1.0f + (GetAttribute(AttrMaxRangeBonus).get_float() / 100.0f);
        } break;
        default:
            return cycleTime;
    }
    m_targetSE->GetEwar(true)->Apply(m_shipRef->GetPilot()->GetShipSE()->GetID(), kind, cycleTime + 1000, v1, v2,
            InventoryItemRef(nullptr), itemID());
    return cycleTime;
}

void RemoteEwarModule::DeactivateCycle(bool abort/*false*/)
{
    if (m_ModuleState < Module::State::Deactivating)
        return;

    if ((m_targetSE != nullptr) and (m_targetSE->GetEwar() != nullptr))
        m_targetSE->GetEwar()->Remove(m_shipRef->itemID(), GetKind(), itemID());

    ActiveModule::DeactivateCycle(abort);
}
