
 /**
  * @name TurretModule.h
  *   turret module class
  * @Author:         Allan
  * @date:   10 June 2015   -UD/RW 02 April 2017
  */


#ifndef __EVESERVER_SHIPMODULES_ACTIVE_MODULES_TURRENTMODULE_H
#define __EVESERVER_SHIPMODULES_ACTIVE_MODULES_TURRENTMODULE_H

#include "ship/modules/ActiveModule.h"
#include "ship/modules/TurretFormulas.h"


class TurretModule : public ActiveModule
{
public:
    TurretModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~TurretModule()                            { /* do nothing here */ }

    virtual TurretModule*       GetTurretModule()       { return this; }
    //  class type helpers.  public for anyone to access.
    virtual bool                IsTurretModule()        { return true; }

    /* ActiveModule overrides */
    virtual void LoadCharge(InventoryItemRef charge, bool instant=false);
    virtual void UnloadCharge();

    //  functions to be handled in derived classes as needed
    virtual void ApplyDamage();

    // frequency crystals (lasers and the civilian turrets' ammo) and tech 2 laser crystals.  they aren't used up
    static bool IsCrystal(uint16 groupID);

protected:
    TurretFormulas m_formula;

    // a shot may damage the crystal in pMod (this module or one of its bank).  a crystal at its hp is destroyed
    void DamageCrystal(GenericModule* pMod);

};


#endif  // __EVESERVER_SHIPMODULES_ACTIVE_MODULES_TURRENTMODULE_H