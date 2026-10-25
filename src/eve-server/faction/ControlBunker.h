/**
 * @name ControlBunker.h
 *   a factional warfare system's control bunker in space (FacWarMgr.h)
 *
 * @date:          22 October 2026
 */

#ifndef EVEMU_FACTION_CONTROLBUNKER_H_
#define EVEMU_FACTION_CONTROLBUNKER_H_

#include "system/SystemEntity.h"

/* a war zone system's control bunker.  it can be shot only while its system is vulnerable (FacWarMgr::BunkerVulnerable());
 *  what's left of it is FacWarMgr's (the system's row), so a bunker put up again is the same bunker.  destroyed, the system goes to its attacker at the next downtime */
class ControlBunkerSE
: public ObjectSystemEntity
{
public:
    ControlBunkerSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data);
    virtual ~ControlBunkerSE()                          { }

    /* class type pointer querys. */
    virtual ControlBunkerSE*    GetControlBunkerSE()    { return this; }
    /* class type tests. */
    virtual bool                IsControlBunkerSE()     { return true; }

    /* SystemEntity interface */
    virtual bool                IsInvul();
    virtual void                Killed(Damage& damage);

    // a hit we took (Damage.cpp):  FacWarMgr keeps what's left
    void                        OnDamaged(SystemEntity* pAttacker);
    // shield, armor and structure left (1 = whole)
    void                        SetHealth(double shield, double armor, double hull);
};

#endif  // EVEMU_FACTION_CONTROLBUNKER_H_
