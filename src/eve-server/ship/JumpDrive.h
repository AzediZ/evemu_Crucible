 /**
  * @name JumpDrive.h
  *   shared rules and maths for capital jump drives and jump portals (cyno jumps, titan/black ops bridges)
  *
  * @Author:        EVEmu Team
  * @date:          11 October 2026
  */

#ifndef _EVE_SHIP_JUMP_DRIVE_H_
#define _EVE_SHIP_JUMP_DRIVE_H_

#include "eve-server.h"
#include "fleet/FleetData.h"
#include "ship/Ship.h"

class Client;
class GenericModule;

namespace JumpDrive {
    // light years between two solar systems (mapSolarSystems positions)
    double LightYears(uint32 fromSystemID, uint32 toSystemID);
    // the item's jumpHarmonics (capitals -5, black ops and covert cynos 5), 0 when it has none
    int8 Harmonics(InventoryItem* pItem);
    /* jump fuel, as the client shows it (capitalnavigation.py, posConst.GetJumpFuelConsumption):  truncated, at least 1.
     *  bridges add massFactor = jumping ship's mass x bridging ship's jumpPortalConsumptionMassFactor */
    uint32 FuelNeeded(double ly, double perLy, double massFactor = 1.0);
    // units of typeID in the ship's fuel bay and cargo hold
    uint32 CountFuel(ShipItemRef ship, uint16 typeID);
    // takes qty units of typeID, fuel bay first, then the cargo hold.  check CountFuel() first
    void ConsumeFuel(ShipItemRef ship, uint16 typeID, uint32 qty);

    /* throws when the pilot's jump drive can't be used now:  an active cyno, jump portal or siege module (pIgnore is skipped),
     *  the doomsday's jump lockout, warp scramble or a warp disruption field */
    void CheckDriveFree(Client* pClient, GenericModule* pIgnore = nullptr);
    // throws for a jump within one system, out of or into wormhole space
    void CheckDestination(uint32 fromSystemID, uint32 toSystemID);
    /* finds the beacon the client wants to jump (or bridge) to and fills out.  throws when it's not usable.
     *  fleet: the active cyno charID lit, in the caller's fleet.  alliance: the Cynosural Generator Array of the caller's alliance in systemID */
    void ResolveBeacon(Client* pClient, bool alliance, uint32 charID, uint32 beaconID, uint32 systemID, BeaconData& out);
}

#endif  // _EVE_SHIP_JUMP_DRIVE_H_
