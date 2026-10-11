 /**
  * @name SmartBombModule.h
  *   smart bomb module class
  */

#ifndef _EVE_SHIP_MODULES_SMART_BOMB_MODULE_H_
#define _EVE_SHIP_MODULES_SMART_BOMB_MODULE_H_

#include "ship/modules/ActiveModule.h"

class SmartBombModule : public ActiveModule
{
public:
    SmartBombModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~SmartBombModule()                          { /* do nothing here */ }

    /* ActiveModule overrides */
    virtual bool        CanActivate();
    virtual uint32      DoCycle();

private:
    // a station, stargate or acceleration gate too close to use the bomb (nullptr if none)
    SystemEntity*       FindInterference(ShipSE* pShip);
    void                SendInterference();
};

#endif  // _EVE_SHIP_MODULES_SMART_BOMB_MODULE_H_
