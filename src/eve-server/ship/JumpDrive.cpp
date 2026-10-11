/**
 * @name JumpDrive.cpp
 *   shared checks and helpers for jump drives, cynosural fields and jump portals (titan / black ops bridges)
 *
 * @Author:  AzediZ
 * @date:    11 October 2026
 */

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "fleet/FleetService.h"
#include "inventory/Inventory.h"
#include "ship/JumpDrive.h"
#include "ship/modules/CynoModule.h"
#include "ship/modules/JumpPortalModule.h"
#include "ship/modules/ModuleManager.h"
#include "ship/Ship.h"
#include "system/SystemDB.h"
#include "system/sov/SovereigntyDataMgr.h"

CynoModule* JumpDrive::GetActiveCyno(Client* pClient)
{
    if ((pClient == nullptr) or !pClient->IsInSpace())
        return nullptr;
    ShipItemRef sRef = pClient->GetShip();
    if (sRef.get() == nullptr)
        return nullptr;

    std::vector<GenericModule*> modules;
    sRef->GetModuleManager()->GetModulesInBank(flagHiSlot0, modules);
    for (auto cur : modules)
        if (cur->IsCynoModule() and cur->GetCynoModule()->IsFieldUp())
            return cur->GetCynoModule();

    return nullptr;
}

JumpPortalModule* JumpDrive::GetPortalModule(Client* pClient)
{
    if ((pClient == nullptr) or !pClient->IsInSpace())
        return nullptr;
    ShipItemRef sRef = pClient->GetShip();
    if (sRef.get() == nullptr)
        return nullptr;

    std::vector<GenericModule*> modules;
    sRef->GetModuleManager()->GetModulesInBank(flagHiSlot0, modules);
    for (auto cur : modules)
        if ((cur->GetJumpPortalModule() != nullptr) and cur->isOnline())
            return cur->GetJumpPortalModule();

    return nullptr;
}

bool JumpDrive::FindFleetBeacon(Client* pClient, uint32 charID, uint32 beaconID, Beacon& beacon)
{
    Client* pOwner = sEntityList.FindClientByCharID(charID);
    if ((pOwner == nullptr) or !pClient->InFleet() or (pOwner->GetFleetID() != pClient->GetFleetID())) {
        pClient->SendNotifyMsg("That cynosural field is no longer available to your fleet.");
        return false;
    }

    // the client remembers the field from when it was lit, so check it is still there
    CynoModule* pCyno = GetActiveCyno(pOwner);
    if ((pCyno == nullptr) or (pCyno->GetFieldID() != beaconID)) {
        pClient->SendNotifyMsg("%s no longer has a cynosural field lit.", pOwner->GetName());
        return false;
    }

    beacon.item = pCyno->GetField();
    beacon.systemID = pOwner->GetSystemID();
    beacon.charID = charID;
    beacon.covert = pCyno->IsCovert();
    return true;
}

bool JumpDrive::FindAllianceBeacon(Client* pClient, uint32 beaconID, Beacon& beacon)
{
    InventoryItemRef iRef = sItemFactory.GetItemRefFromID(beaconID);
    if ((iRef.get() == nullptr) or (iRef->groupID() != EVEDB::invGroups::Cynosural_Generator_Array)) {
        pClient->SendNotifyMsg("That beacon is no longer available.");
        return false;
    }

    // an online generator array registers itself as its system's beacon
    SovereigntyData sovData = svDataMgr.GetSovereigntyData(iRef->locationID());
    if ((sovData.beaconID != beaconID) or (sovData.allianceID == 0) or ((int32)sovData.allianceID != pClient->GetAllianceID())) {
        pClient->SendNotifyMsg("That beacon is not online for your alliance.");
        return false;
    }

    beacon.item = iRef;
    beacon.systemID = iRef->locationID();
    beacon.charID = 0;
    beacon.covert = false;
    return true;
}

double JumpDrive::GetDistanceLY(uint32 fromSystemID, uint32 toSystemID)
{
    GVector heading(SystemDB::GetSolarSystemPosition(fromSystemID), SystemDB::GetSolarSystemPosition(toSystemID));
    return EvEMath::Units::MetersToLightYears(heading.length());
}

bool JumpDrive::CheckRange(Client* pClient, ShipItemRef sRef, uint32 toSystemID, const char* what)
{
    // includes Jump Drive Calibration
    double range = sRef->GetAttribute(AttrJumpDriveRange).get_double();
    if (range <= 0) {
        _log(SHIP__WARNING, "JumpDrive::CheckRange() - %s has no jump drive range.  Not checking range.", sRef->name());
        return true;
    }

    double distance = GetDistanceLY(pClient->GetSystemID(), toSystemID);
    if (distance > range) {
        pClient->SendNotifyMsg("That system is %.2f light years away, but your %s only reaches %.2f light years.", distance, what, range);
        return false;
    }

    return true;
}

uint32 JumpDrive::GetQuantity(InventoryItemRef holder, EVEItemFlags flag, uint16 typeID)
{
    if ((holder.get() == nullptr) or (holder->GetMyInventory() == nullptr))
        return 0;

    std::vector<InventoryItemRef> items;
    holder->GetMyInventory()->GetItemsByFlag(flag, items);
    uint32 quantity(0);
    for (auto cur : items)
        if (cur->typeID() == typeID)
            quantity += cur->quantity();

    return quantity;
}

bool JumpDrive::TakeItems(InventoryItemRef holder, EVEItemFlags flag, uint16 typeID, uint32 qty)
{
    if (qty == 0)
        return true;
    if (GetQuantity(holder, flag, typeID) < qty)
        return false;

    std::vector<InventoryItemRef> items;
    holder->GetMyInventory()->GetItemsByFlag(flag, items);
    uint32 left(qty);
    for (auto cur : items) {
        if (left == 0)
            break;
        if (cur->typeID() != typeID)
            continue;
        uint32 stack = cur->quantity();
        if (stack > left) {
            cur->AlterQuantity(-((int32)left), true);
            left = 0;
        } else {
            // use up the whole stack
            left -= stack;
            cur->SetQuantity(0, true, true);
        }
    }

    return true;
}

bool JumpDrive::DriveJump(Client* pClient, const Beacon& beacon)
{
    ShipItemRef sRef = pClient->GetShip();
    uint16 fuelType = sRef->GetAttribute(AttrJumpDriveConsumptionType).get_uint32();
    if (fuelType == 0) {
        pClient->SendNotifyMsg("Your ship doesn't have a jump drive.");
        return false;
    }
    if (beacon.systemID == pClient->GetSystemID()) {
        pClient->SendNotifyMsg("That cynosural field is in this system.");
        return false;
    }
    if (beacon.covert and (sRef->groupID() != EVEDB::invGroups::BlackOps)) {
        pClient->SendNotifyMsg("Only Black Ops ships can jump to a covert cynosural field.");
        return false;
    }
    if (pClient->IsJump())
        return false;
    if (!CheckRange(pClient, sRef, beacon.systemID, "jump drive"))
        return false;

    // capacitor.  jumpDriveCapacitorNeed is a fraction of capacitor capacity (Jump Drive Operation lowers it)
    float capacity = sRef->GetAttribute(AttrCapacitorCapacity).get_float();
    float capNow = sRef->GetAttribute(AttrCapacitorCharge).get_float();
    float capNeed = sRef->GetAttribute(AttrJumpDriveCapacitorNeed).get_float() * capacity;
    if (capNow < capNeed) {
        pClient->SendNotifyMsg("This jump needs %.0f GJ of capacitor, but you only have %.0f GJ.", capNeed, capNow);
        return false;
    }

    // fuel, by distance.  Jump Fuel Conservation saves 10% a level, and Jump Freighters another 10% a level in a jump freighter
    double distance = GetDistanceLY(pClient->GetSystemID(), beacon.systemID);
    double fuelBase = sRef->GetAttribute(AttrJumpDriveConsumptionAmount).get_double();
    double fuelFactor = 1 - (0.1 * pClient->GetChar()->GetSkillLevel(EvESkill::JumpFuelConservation));
    if (sRef->groupID() == EVEDB::invGroups::JumpFreighter)
        fuelFactor *= 1 - (0.1 * pClient->GetChar()->GetSkillLevel(EvESkill::JumpFreighters));
    uint32 fuelNeed = uint32(ceil(distance * fuelBase * fuelFactor));
    if (!TakeItems(sRef, flagFuelBay, fuelType, fuelNeed)) {
        pClient->SendNotifyMsg("This jump needs %u units of %s in your fuel bay.", fuelNeed, sItemFactory.GetType(fuelType)->name().c_str());
        return false;
    }

    if ((capNeed > 0) and (capacity > 0))
        sRef->SetShipCapacitorLevel((capNow - capNeed) / capacity);

    _log(SHIP__MESSAGE, "%s jump drive: %.2f ly to %s(%u) in system %u, %u fuel, %.0f GJ", pClient->GetName(), distance, \
            beacon.item->name(), beacon.item->itemID(), beacon.systemID, fuelNeed, capNeed);
    pClient->CynoJump(beacon.item);
    return true;
}

bool JumpDrive::PortalJump(Client* pClient, Client* pOwner, JumpPortalModule* pPortal)
{
    ShipSE* pShipSE = pClient->GetShipSE();
    ShipSE* pOwnerSE = pOwner->GetShipSE();
    if ((pShipSE == nullptr) or (pOwnerSE == nullptr) or (pClient->GetSystemID() != pOwner->GetSystemID())) {
        pClient->SendNotifyMsg("You must be in the same system as the jump portal.");
        return false;
    }

    double distance = pShipSE->GetPosition().distance(pOwnerSE->GetPosition()) - pShipSE->GetRadius() - pOwnerSE->GetRadius();
    if (distance > PortalUseRange) {
        pClient->SendNotifyMsg("You are %.0f m from %s's jump portal.  You need to be within %.0f m to use it.", distance, pOwner->GetName(), PortalUseRange);
        return false;
    }

    // the beacon may have gone out since the portal opened
    const Beacon& beacon = pPortal->GetBeacon();
    Beacon check;
    if (beacon.charID != 0) {
        if (!FindFleetBeacon(pClient, beacon.charID, beacon.item->itemID(), check))
            return false;
    } else {
        SovereigntyData sovData = svDataMgr.GetSovereigntyData(beacon.systemID);
        if (sovData.beaconID != beacon.item->itemID()) {
            pClient->SendNotifyMsg("The beacon at the other end of the jump portal is no longer online.");
            return false;
        }
    }

    ShipItemRef sRef = pClient->GetShip();
    uint32 groupID = sRef->groupID();
    if (pPortal->IsCovert()) {
        // covert ops cloak hulls.  the invGroups table has no name for force recon ships (833)
        if ((groupID != EVEDB::invGroups::CovertOps) and (groupID != EVEDB::invGroups::StealthBomber) and (groupID != 833)) {
            pClient->SendNotifyMsg("Only covert ops ships, stealth bombers and force recon ships can use a covert jump portal.");
            return false;
        }
    } else {
        switch (groupID) {
            case EVEDB::invGroups::Titan:
            case EVEDB::invGroups::Supercarrier:
            case EVEDB::invGroups::Carrier:
            case EVEDB::invGroups::Dreadnought:
            case EVEDB::invGroups::CapitalIndustrialShip:
            case EVEDB::invGroups::Freighter:
            case EVEDB::invGroups::JumpFreighter:
            case EVEDB::invGroups::BlackOps: {
                pClient->SendNotifyMsg("Capital ships, Black Ops and freighters can't use a jump portal.");
                return false;
            }
        }
    }
    if (pClient->IsJump())
        return false;

    // fuel comes from the portal ship's fuel bay: jumping ship's mass x the generator's mass factor x distance
    ShipItemRef oRef = pOwner->GetShip();
    uint16 fuelType = oRef->GetAttribute(AttrJumpDriveConsumptionType).get_uint32();
    double massFactor = pPortal->GetAttribute(AttrJumpPortalConsumptionMassFactor).get_double();
    double ly = GetDistanceLY(pOwner->GetSystemID(), beacon.systemID);
    uint32 fuelNeed = uint32(ceil(sRef->GetAttribute(AttrMass).get_double() * massFactor * ly));
    if ((fuelType != 0) and !TakeItems(oRef, flagFuelBay, fuelType, fuelNeed)) {
        const char* fuelName = sItemFactory.GetType(fuelType)->name().c_str();
        pClient->SendNotifyMsg("Your jump through the portal needs %u units of %s, but %s's fuel bay doesn't have enough.", fuelNeed, fuelName, pOwner->GetName());
        if (pOwner != pClient)
            pOwner->SendNotifyMsg("%s couldn't jump through your portal: it needs %u units of %s from your fuel bay.", pClient->GetName(), fuelNeed, fuelName);
        return false;
    }

    _log(SHIP__MESSAGE, "%s jump portal (%s): %.2f ly to %s(%u) in system %u, %u fuel from %s", pClient->GetName(), pOwner->GetName(), ly, \
            beacon.item->name(), beacon.item->itemID(), beacon.systemID, fuelNeed, oRef->name());
    pClient->CynoJump(beacon.item);
    return true;
}

void JumpDrive::SendBeaconChange(Client* pOwner, uint32 systemID, uint32 fieldID, bool active)
{
    if (!pOwner->InFleet())
        return;

    _log(MODULE__DEBUG, "Sending OnJumpBeaconChange for %s (field %u, active = %s)", pOwner->GetName(), fieldID, active ? "true" : "false");
    std::vector<Client*> members = sFltSvc.GetFleetClients(pOwner->GetFleetID());
    for (auto cur : members) {
        if (cur == nullptr)
            continue;
        PyTuple* data = new PyTuple(4);
            data->SetItem(0, new PyInt(pOwner->GetCharacterID()));
            data->SetItem(1, new PyInt(systemID));
            data->SetItem(2, new PyInt(fieldID));
            data->SetItem(3, new PyBool(active));
        cur->SendNotification("OnJumpBeaconChange", "clientID", &data);
    }
}

void JumpDrive::SendBridgeChange(Client* pOwner, uint32 shipID, uint32 systemID, uint32 beaconID, bool active)
{
    if (!pOwner->InFleet())
        return;

    _log(MODULE__DEBUG, "Sending OnBridgeModeChange for %s (ship %u, beacon %u, active = %s)", pOwner->GetName(), shipID, beaconID, active ? "true" : "false");
    std::vector<Client*> members = sFltSvc.GetFleetClients(pOwner->GetFleetID());
    for (auto cur : members) {
        if (cur == nullptr)
            continue;
        PyTuple* data = new PyTuple(4);
            data->SetItem(0, new PyInt(shipID));
            data->SetItem(1, new PyInt(systemID));
            data->SetItem(2, new PyInt(beaconID));
            data->SetItem(3, new PyBool(active));
        cur->SendNotification("OnBridgeModeChange", "clientID", &data);
    }
}

void JumpDrive::SendActiveBeacons(Client* pClient)
{
    if ((pClient == nullptr) or !pClient->InFleet())
        return;

    std::vector<Client*> members = sFltSvc.GetFleetClients(pClient->GetFleetID());
    for (auto cur : members) {
        if (cur == nullptr)
            continue;
        CynoModule* pCyno = GetActiveCyno(cur);
        if (pCyno != nullptr) {
            PyTuple* data = new PyTuple(4);
                data->SetItem(0, new PyInt(cur->GetCharacterID()));
                data->SetItem(1, new PyInt(cur->GetSystemID()));
                data->SetItem(2, new PyInt(pCyno->GetFieldID()));
                data->SetItem(3, new PyBool(true));
            pClient->SendNotification("OnJumpBeaconChange", "clientID", &data);
        }
        JumpPortalModule* pPortal = GetPortalModule(cur);
        if ((pPortal != nullptr) and pPortal->IsOpen()) {
            PyTuple* data = new PyTuple(4);
                data->SetItem(0, new PyInt(cur->GetShipID()));
                data->SetItem(1, new PyInt(pPortal->GetBeacon().systemID));
                data->SetItem(2, new PyInt(pPortal->GetBeacon().item->itemID()));
                data->SetItem(3, new PyBool(true));
            pClient->SendNotification("OnBridgeModeChange", "clientID", &data);
        }
    }
}
