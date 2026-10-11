 /**
  * @name EwarModule.cpp
  *   electronic warfare helpers shared by modules, drones, npcs and structures
  */

#include "eve-server.h"

#include "Client.h"
#include "npc/Drone.h"
#include "ship/modules/EwarModule.h"
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
