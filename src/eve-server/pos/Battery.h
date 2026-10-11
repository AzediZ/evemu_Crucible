
/**
 * @name Battery.h
 *   Class for POS Battery Modules.
 *
 * @Author:         Allan
 * @date:   28 December 17
 */


#ifndef EVEMU_POS_BATTERY_H_
#define EVEMU_POS_BATTERY_H_


#include "pos/Structure.h"
#include "pos/POS_AI.h"


class BatterySE
: public StructureSE
{
public:
    BatterySE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& data);
    virtual ~BatterySE();

    /* class type pointer querys. */
    virtual BatterySE*          GetBatterySE()          { return this; }

    /* class type tests. */
    virtual bool                IsBatterySE()           { return true; }

    /* SystemEntity interface */
    virtual void                Process();

    /* virtual functions default to base class and overridden as needed */
    virtual void Init();
    virtual void SetOffline();
    virtual void Killed(Damage &damage);
    // charges (flagHiSlot0) of the battery's charge groups
    virtual void ValidateAddItem(Client* pClient, InventoryItemRef iRef, EVEItemFlags toFlag);

    // the defenses' brain (automatic and manual fire)
    POS_AI*                     GetAI()                 { return m_ai; }

private:
    POS_AI* m_ai;


};

#endif  // EVEMU_POS_BATTERY_H_
