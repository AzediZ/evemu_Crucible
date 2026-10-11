 /**
  * @name GangLinkModule.h
  *   warfare link (gang coordinator) module class
  */

#ifndef _EVE_SHIP_MODULES_GANG_LINK_MODULE_H_
#define _EVE_SHIP_MODULES_GANG_LINK_MODULE_H_

#include "ship/modules/ActiveModule.h"

// one bonus of a link effect:  targetAttr on the ship (skillID 0) or on fitted modules needing skillID,
//  changed by PostPercent of the link module's srcAttr
struct LinkModifier {
    uint16 skillID;
    uint16 targetAttr;
    uint16 srcAttr;
};

/* the link's gang expressions (AGSM/AGRSM) are not applied to the booster's own ship.
 *  FleetService::UpdateLinks() reads them from here and applies them to the fleet members the booster boosts */
class GangLinkModule : public ActiveModule
{
public:
    GangLinkModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~GangLinkModule()                           { /* do nothing here */ }

    /* ActiveModule overrides */
    virtual bool        ApplyActiveEffects()            { return false; }
    virtual void        Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    virtual void        DeactivateCycle(bool abort=false);
    virtual uint32      DoCycle();

    // the gang bonuses of a link effect (cached)
    static const std::vector<LinkModifier>& GetModifiers(uint16 effectID);

private:
    void                UpdateFleet();
};

#endif  // _EVE_SHIP_MODULES_GANG_LINK_MODULE_H_
