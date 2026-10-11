 /**
  * @name JumpDrive.cpp
  *   shared rules and maths for capital jump drives and jump portals (cyno jumps, titan/black ops bridges)
  *
  * @Author:        EVEmu Team
  * @date:          11 October 2026
  */

#include "eve-server.h"

#include "Client.h"
#include "fleet/FleetService.h"
#include "ship/JumpDrive.h"
#include "inventory/Inventory.h"
#include "ship/modules/GenericModule.h"
#include "ship/modules/ModuleManager.h"
#include "system/Interdiction.h"
#include "system/SystemDB.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"

double JumpDrive::LightYears(uint32 fromSystemID, uint32 toSystemID)
{
    GVector heading(SystemDB::GetSolarSystemPosition(fromSystemID), SystemDB::GetSolarSystemPosition(toSystemID));
    return EvEMath::Units::MetersToLightYears(heading.length());
}

int8 JumpDrive::Harmonics(InventoryItem* pItem)
{
    if ((pItem == nullptr) or !pItem->HasAttribute(AttrJumpHarmonics))
        return 0;
    return static_cast<int8>(pItem->GetAttribute(AttrJumpHarmonics).get_int());
}

uint32 JumpDrive::FuelNeeded(double ly, double perLy, double massFactor/*1.0*/)
{
    int64 qty(static_cast<int64>(ly * perLy * massFactor));
    return static_cast<uint32>(EvE::max(qty, (int64)1));
}

uint32 JumpDrive::CountFuel(ShipItemRef ship, uint16 typeID)
{
    uint32 count(0);
    std::vector<InventoryItemRef> items;
    ship->GetMyInventory()->GetItemsByFlag(flagFuelBay, items);
    ship->GetMyInventory()->GetItemsByFlag(flagCargoHold, items);
    for (auto cur : items)
        if (cur->typeID() == typeID)
            count += cur->quantity();
    return count;
}

void JumpDrive::ConsumeFuel(ShipItemRef ship, uint16 typeID, uint32 qty)
{
    std::vector<InventoryItemRef> items;
    ship->GetMyInventory()->GetItemsByFlag(flagFuelBay, items);
    ship->GetMyInventory()->GetItemsByFlag(flagCargoHold, items);
    for (auto cur : items) {
        if (qty == 0)
            break;
        if (cur->typeID() != typeID)
            continue;
        if (cur->quantity() > qty) {
            cur->AlterQuantity(-((int32)qty), true);
            qty = 0;
        } else {
            qty -= cur->quantity();
            // removes the stack
            cur->SetQuantity(0, true, true);
        }
    }
    if (qty > 0)
        _log(SHIP__WARNING, "JumpDrive::ConsumeFuel() - %s(%u) was %u units of %u short", ship->name(), ship->itemID(), qty, typeID);
}

void JumpDrive::CheckDriveFree(Client* pClient, GenericModule* pIgnore/*nullptr*/)
{
    ShipItemRef ship(pClient->GetShip());
    ShipSE* pShipSE(pClient->GetShipSE());
    if ((ship.get() == nullptr) or (pShipSE == nullptr))
        throw CustomError("You are not in space.");

    // "Your jump drive is unusable while {[item]moduleType.name} is active."  (cyno, jump portal, siege/triage)
    ModuleManager* pModMgr(ship->GetModuleManager());
    uint16 groups[3] = { EVEDB::invGroups::Cynosural_Field_Generator, EVEDB::invGroups::Jump_Portal_Generator, EVEDB::invGroups::Siege_Module };
    for (int i = 0; i < 3; ++i) {
        GenericModule* pMod(pModMgr->GetActiveModuleByGroup(groups[i]));
        if ((pMod != nullptr) and (pMod != pIgnore))
            throw UserError("CantJumpWhileModuleActive")
                    .AddFormatValue("moduleType", new PyInt(pMod->typeID()));
    }

    // "Your Super Weapon has recently been fired. You will not be able to activate your jump engines for another {time}."
    int64 lockout(pShipSE->JumpLockoutRemaining());
    if (lockout > 0)
        throw UserError("JumpSuperweaponActive")
                .AddFormatValue("time", new PyLong(lockout));

    // no jump drive while the ship can't warp
    if (pShipSE->IsWarpScrambled())
        throw CustomError("Your jump drive cannot be activated while your ship is warp scrambled.");
    if (pShipSE->SystemMgr() != nullptr)
        pShipSE->SystemMgr()->GetInterdictionMgr()->CheckCanWarp(pShipSE);
}

void JumpDrive::CheckDestination(uint32 fromSystemID, uint32 toSystemID)
{
    if (fromSystemID == toSystemID)
        throw CustomError("You cannot jump to a cynosural field in the solar system you are in.");
    // jump drives don't work in or into wormhole space
    if (IsWSpaceID(fromSystemID) or IsWSpaceID(toSystemID))
        throw UserError("JumpBeaconInWormholeSpace");
}

void JumpDrive::ResolveBeacon(Client* pClient, bool alliance, uint32 charID, uint32 beaconID, uint32 systemID, BeaconData& out)
{
    if (alliance) {
        // a Cynosural Generator Array registered for our alliance (sov data)
        uint32 allianceID(pClient->GetAllianceID());
        if (allianceID == 0)
            throw CustomError("You must be in an alliance to use alliance cynosural beacons.");
        SovereigntyData sovData(svDataMgr.GetSovereigntyData(systemID));
        if ((sovData.beaconID == 0) or (sovData.beaconID != beaconID) or (sovData.allianceID != allianceID))
            throw CustomError("That cynosural beacon is no longer active.");
        InventoryItemRef beaconRef(sItemFactory.GetItemRefFromID(beaconID));
        if (beaconRef.get() == nullptr)
            throw CustomError("That cynosural beacon is no longer active.");
        out = BeaconData();
        out.charID = 0;
        out.fleetID = 0;
        out.systemID = systemID;
        out.beaconID = beaconID;
        out.moduleID = 0;
        out.harmonics = -5;    // a generator array is a normal field
        out.position = beaconRef->position();
        return;
    }

    // a cyno lit by a member of our fleet
    if (!pClient->InFleet() or !sFltSvc.GetBeacon(charID, out)
    or (out.fleetID != pClient->GetFleetID()) or (out.beaconID != beaconID) or (out.systemID != systemID))
        throw CustomError("That cynosural beacon is no longer active.");
}
