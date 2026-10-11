 /**
  * @name CloakModule.h
  *   cloaking device module class
  */

#ifndef _EVE_SHIP_MODULES_CLOAK_MODULE_H_
#define _EVE_SHIP_MODULES_CLOAK_MODULE_H_

#include "ship/modules/ActiveModule.h"

class CloakModule : public ActiveModule
{
public:
    CloakModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~CloakModule()                              { /* do nothing here */ }

    virtual CloakModule* GetCloakModule()               { return this; }
    virtual bool        IsCloakModule() const           { return true; }

    /* ActiveModule overrides */
    virtual void        Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    // cloaks drop at once.  they don't finish a cycle
    virtual void        Deactivate(std::string effect="");
    virtual void        DeactivateCycle(bool abort=false);
    virtual bool        CanActivate();

    // only the covert ops cloak (cloakingWarpSafe) may warp
    bool                IsWarpSafeCloak()               { return (m_effectID == EVEEffectID::cloakingWarpSafe); }
};

#endif  // _EVE_SHIP_MODULES_CLOAK_MODULE_H_
