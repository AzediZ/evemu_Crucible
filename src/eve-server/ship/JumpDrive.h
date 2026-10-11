/**
 * @name JumpDrive.h
 *   shared checks and helpers for jump drives, cynosural fields and jump portals (titan / black ops bridges)
 *
 * @Author:  AzediZ
 * @date:    11 October 2026
 */

#ifndef _EVE_SHIP_JUMP_DRIVE_H_
#define _EVE_SHIP_JUMP_DRIVE_H_

#include "inventory/InventoryItem.h"

class Client;
class CynoModule;
class JumpPortalModule;

namespace JumpDrive {
    // how close (surface to surface) a ship must be to a titan or black ops to use its jump portal
    const float PortalUseRange = 2500.0f;

    // a lit cynosural field or an online alliance beacon (cynosural generator array) that ships can jump to
    struct Beacon {
        InventoryItemRef item;
        uint32 systemID = 0;
        uint32 charID = 0;      // the character with the cyno lit (0 for an alliance beacon)
        bool covert = false;    // covert cynosural field, only black ops can use it
    };

    // the client's active (lit) cynosural field generator, or nullptr
    CynoModule* GetActiveCyno(Client* pClient);
    // the client's fitted and online jump portal generator, or nullptr
    JumpPortalModule* GetPortalModule(Client* pClient);

    // finds the cyno that charID has lit, and checks charID is in pClient's fleet.
    //  tells pClient why and returns false when there is no such cyno
    bool FindFleetBeacon(Client* pClient, uint32 charID, uint32 beaconID, Beacon& beacon);
    // finds an online alliance beacon in pClient's alliance.  tells pClient why and returns false when not found
    bool FindAllianceBeacon(Client* pClient, uint32 beaconID, Beacon& beacon);

    // distance between two solar systems in light years
    double GetDistanceLY(uint32 fromSystemID, uint32 toSystemID);
    // checks toSystemID is within the ship's jump drive range.  tells pClient and returns false when not
    bool CheckRange(Client* pClient, ShipItemRef sRef, uint32 toSystemID, const char* what);

    // total quantity of typeID in holder's flag
    uint32 GetQuantity(InventoryItemRef holder, EVEItemFlags flag, uint16 typeID);
    // removes qty of typeID from holder's flag, across as many stacks as needed.  returns false (and removes nothing) when there isn't enough
    bool TakeItems(InventoryItemRef holder, EVEItemFlags flag, uint16 typeID, uint32 qty);

    // jumps pClient's ship to the beacon with its own jump drive (capitals, black ops, jump freighters).  checks the ship
    //  has a jump drive, range, capacitor and fuel, then uses the capacitor and fuel.  tells pClient why and returns false when it can't
    bool DriveJump(Client* pClient, const Beacon& beacon);
    // jumps pClient's ship through pOwner's open jump portal.  checks distance to the portal ship, which ships may use the
    //  portal and the portal ship's fuel, then uses the fuel.  tells pClient why and returns false when it can't
    bool PortalJump(Client* pClient, Client* pOwner, JumpPortalModule* pPortal);

    // tells fleet members a cyno was lit or went out (OnJumpBeaconChange)
    void SendBeaconChange(Client* pOwner, uint32 systemID, uint32 fieldID, bool active);
    // tells fleet members a jump portal opened or closed (OnBridgeModeChange)
    void SendBridgeChange(Client* pOwner, uint32 shipID, uint32 systemID, uint32 beaconID, bool active);
    // tells pClient about every cyno lit and jump portal open in its fleet.  for members joining a fleet after these started
    void SendActiveBeacons(Client* pClient);
}

#endif  // _EVE_SHIP_JUMP_DRIVE_H_
