 /**
  * @name CynoModule.h
  *   Cynosural field generator module class
  * @Author: James
  * @date:   8 October 2021
  */

#ifndef _EVE_SHIP_MODULES_CYNO_MODULE_H_
#define _EVE_SHIP_MODULES_CYNO_MODULE_H_

#include "ship/modules/ActiveModule.h"
#include "system/SystemEntity.h"
#include "Client.h"

/* the field a cynosural field generator lights.  a temp item that lives as long as the cycle that lit it (it outlives the
 *  generating ship), and keeps the fleet's beacon registry in step when it goes */
class CynoFieldSE
: public ItemSystemEntity
{
public:
    CynoFieldSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data, uint32 charID, uint32 lifeMS);
    virtual ~CynoFieldSE();

    /* SystemEntity interface */
    virtual void                Process();              // removes itself when its life ends
    virtual void                MakeDamageState(DoDestinyDamageState& into);    // the type has no shield/armor attributes

    // another cycle of the generator keeps the field up
    void                        Extend(uint32 lifeMS)   { m_lifeTimer.Start(lifeMS); }

private:
    uint32 m_charID;            // pilot who lit it (beacon registry key)
    Timer m_lifeTimer;
};

class CynoModule: public ActiveModule
{
public:
    CynoModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~CynoModule()                                 { /* do nothing here */ }

    virtual CynoModule*       GetCynoModule()             { return this; }
    virtual bool        IsCynoModule() const        { return true; }

    /* ActiveModule overrides */
    virtual void Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    virtual void DeactivateCycle(bool abort=false);
    virtual uint32      DoCycle();

    // this is a check for those active modules that need it (mining, weapons) and overridden as needed
    virtual bool CanActivate();

    // covert cynosural field generator (isCovert):  covert field, not jammed, only black ops can jump to it
    bool IsCovert()                                 { return (m_modRef->GetAttribute(AttrIsCovert) > EvilZero); }

private:
    uint32 m_fieldID;           // itemID of the field this module lit (0 = none)
    uint32 m_systemID;          // where it was lit
    uint32 m_charID;            // who lit it
    bool m_firstRun;

    void CreateCyno();
    void RemoveCyno();
};

#endif  //_EVE_SHIP_MODULES_CYNO_MODULE_H_
