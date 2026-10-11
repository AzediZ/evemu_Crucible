 /**
  * @name JumpPortalModule.h
  *   Jump Portal Generator (titan and black ops bridges)
  *
  * @Author:        EVEmu Team
  * @date:          11 October 2026
  */

#ifndef _EVE_SHIP_MODULES_JUMP_PORTAL_MODULE_H_
#define _EVE_SHIP_MODULES_JUMP_PORTAL_MODULE_H_

#include "fleet/FleetData.h"
#include "ship/modules/ActiveModule.h"

/* the generator only runs when a bridge is opened through the Bridge To menus (beyonce CmdBridgeToMember/CmdBridgeToStructure).
 *  one cycle (disallowRepeatingActivation), during which the portal is open and the ship can't move */
class JumpPortalModule
: public ActiveModule
{
public:
    JumpPortalModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~JumpPortalModule();

    virtual JumpPortalModule*   GetJumpPortalModule()   { return this; }
    virtual bool                IsJumpPortalModule() const { return true; }

    /* ActiveModule overrides */
    virtual void                Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    virtual void                DeactivateCycle(bool abort=false);
    virtual bool                CanActivate();

    // activates the generator with this bridge.  throws like any module activation.  true when the portal opened
    bool                        OpenBridge(const BridgeData& data);
    // jumpHarmonics:  0 titan portal, 5 covert (black ops) portal
    int8                        GetHarmonics();
    // jumpPortalGeneration (2152) or jumpPortalGenerationBO (3674)
    uint16                      GetPortalEffectID();
    // effects.JumpDriveOut/In or their BO versions for ships using this portal
    bool                        IsCovertPortal()        { return (GetHarmonics() > 0); }

private:
    BridgeData m_bridge;
    EvilNumber m_maxVelocity;
    bool m_bridgeRequested;     // set by OpenBridge() for the activation it makes
    bool m_bridgeOpen;
};

#endif  // _EVE_SHIP_MODULES_JUMP_PORTAL_MODULE_H_
