 /**
  * @name EwarModule.h
  *   electronic warfare helpers shared by modules, drones, npcs and structures
  *   the state they create lives in system/EwarState.h
  */

#ifndef _EVE_SHIP_MODULES_EWAR_MODULE_H_
#define _EVE_SHIP_MODULES_EWAR_MODULE_H_

#include "ship/modules/ActiveModule.h"
#include "system/EwarState.h"

class Client;
class SystemEntity;

namespace Ewar {
    // client 'ewarType' strings (state.py ewarStates) for module groups.  nullptr when the group isn't ewar
    const char* GetEwarType(uint16 groupID);
    // groups that are offensive modifiers (blocked by AttrDisallowOffensiveModifiers on the target)
    bool IsOffensiveModifier(uint16 groupID);

    // the client to tell about something this entity does or receives (pilot, or a drone's controller)
    Client* GetController(SystemEntity* pSE);

    /* OnEwarStart/OnEwarEnd(sourceBallID, moduleID, targetBallID, ewarType) to the target's pilot (overview and tactical icons) */
    void SendEwarStart(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget, const char* ewarType);
    void SendEwarEnd(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget, const char* ewarType);
    void SendEwarNotify(bool start, uint32 sourceID, uint32 moduleID, SystemEntity* pTarget, const char* ewarType);
    /* OnJamStart(sourceBallID, moduleID, targetBallID, 'electronic', startTime, durationMs) / OnJamEnd(...)
     *  to the jammed pilot (ship ui timer) and the jammer's pilot or drone controller (target bracket timer) */
    void SendJamNotify(bool start, uint32 sourceID, uint32 moduleID, SystemEntity* pTarget, int64 startTime = 0, uint32 durationMs = 0);

    /* capacitor warfare.  works on anything with a capacitor attribute (ships, npcs, structures)
     *  neutralizer: takes amount from the target, floored at 0.  returns the amount drained */
    float DrainCapacitor(SystemEntity* pTarget, float amount);
    /* nosferatu: moves up to amount from the target to the source, only while the source has less capacitor (GJ) than the target.
     *  returns the amount moved */
    float LeechCapacitor(InventoryItemRef srcRef, SystemEntity* pTarget, float amount);

    /* ecm.  the client's own formula (godma.py CheckApplyModifiers):
     *  range: past rangeAttr, chance = 2^(-((d - range)^2) / falloff^2), d = surface distance from 'from' (with fromRadius) to the target
     *  strength: of the scan*StrengthBonus values >= 1 on the jammer, take the one against the target's highest scan*Strength.
     *   chance = bonus / strength (always succeeds when there's no pair or the strength is 0)
     * RollJam rolls the two separately, like the client.  JamChance is their product */
    float JamChance(InventoryItemRef jammerRef, const GPoint& from, SystemEntity* pTarget, uint16 rangeAttr, uint16 falloffAttr, double fromRadius = 0);
    bool RollJam(InventoryItemRef jammerRef, const GPoint& from, SystemEntity* pTarget, uint16 rangeAttr, uint16 falloffAttr, double fromRadius = 0);
    /* jam: the target loses all its locks and can't lock anything for durationMs.  OnJamStart/OnJamEnd go to both pilots (EwarState) */
    void StartJam(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget, uint32 durationMs);
    void EndJam(SystemEntity* pSource, uint32 moduleID, SystemEntity* pTarget);
    /* ecm burst: the target drops every lock it has.  no lasting jam */
    void BreakLocks(SystemEntity* pTarget);
}

/* ECM (targeted jammer), ECM Burst (area around the ship) and Remote ECM Burst (area around the target)
 *  jams are handled here and in TargetManager/EwarState, never through dogma: the jam effect's
 *  'maxLockedTargets x ewTargetJam(0)' target modifier would zero the target's attribute for good */
class EcmModule : public ActiveModule
{
public:
    EcmModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~EcmModule()                                { /* do nothing here */ }

    /* ActiveModule overrides */
    virtual bool        ApplyTargetEffects()            { return false; }
    virtual void        Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    virtual void        DeactivateCycle(bool abort=false);
    virtual uint32      DoCycle();

private:
    // breaks the locks of everything around center that the roll beats, and tells the pilot how it went
    void                Burst(const GPoint& center, double centerRadius, SystemEntity* pCenterSE);
};

#endif  // _EVE_SHIP_MODULES_EWAR_MODULE_H_
