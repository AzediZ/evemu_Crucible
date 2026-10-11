 /**
  * @name SiegeModule.h
  *   siege and triage module class
  */

#ifndef _EVE_SHIP_MODULES_SIEGE_MODULE_H_
#define _EVE_SHIP_MODULES_SIEGE_MODULE_H_

#include "ship/modules/ActiveModule.h"

class SiegeModule : public ActiveModule
{
public:
    SiegeModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~SiegeModule()                              { /* do nothing here */ }

    /* ActiveModule overrides */
    virtual void        Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    virtual void        DeactivateCycle(bool abort=false);

private:
    /* the effects processor can't undo the mode's assignments (maxLockedTargets, disallowOffensiveModifiers) or its
     *  -100% speed, so the ship's values from before activation are put back when it ends */
    EvilNumber          m_maxLockedTargets;
    EvilNumber          m_maxVelocity;
    EvilNumber          m_disallowOffensiveModifiers;
    EvilNumber          m_disallowAssistance;
    bool                m_hasSnapshot;
};

#endif  // _EVE_SHIP_MODULES_SIEGE_MODULE_H_
