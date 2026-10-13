/**
 * @name JumpPortalModule.h
 *   jump portal generator module class (titan and black ops bridges)
 *
 * @Author:  AzediZ
 * @date:    11 October 2026
 */

#ifndef _EVE_SHIP_MODULES_JUMP_PORTAL_MODULE_H_
#define _EVE_SHIP_MODULES_JUMP_PORTAL_MODULE_H_

#include "ship/JumpDrive.h"
#include "ship/modules/ActiveModule.h"

/* a portal is opened from the client's "Bridge to" menu (BeyonceBound::CmdBridgeToMember / CmdBridgeToStructure),
 *  which calls OpenPortal().  it stays open for one cycle of the module, during which the ship can't move and fleet
 *  members near it can jump through (BeyonceBound::CmdJumpThroughFleet).
 */
class JumpPortalModule: public ActiveModule
{
public:
    JumpPortalModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~JumpPortalModule()                         { /* do nothing here */ }

    virtual JumpPortalModule* GetJumpPortalModule()     { return this; }

    /* ActiveModule overrides */
    virtual void        Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    virtual void        DeactivateCycle(bool abort=false);

    // opens a portal to the beacon.  returns true when the portal opened
    bool                OpenPortal(const JumpDrive::Beacon& beacon);
    bool                IsOpen() const                  { return m_open; }
    // black ops covert jump portal generator.  only covert ships can use these portals
    bool                IsCovert();
    // jumpPortalGeneration, or jumpPortalGenerationBO for the covert one
    uint16              PortalEffect();
    const JumpDrive::Beacon& GetBeacon() const          { return m_beacon; }

private:
    JumpDrive::Beacon   m_beacon;
    bool                m_open;
};

#endif  // _EVE_SHIP_MODULES_JUMP_PORTAL_MODULE_H_
