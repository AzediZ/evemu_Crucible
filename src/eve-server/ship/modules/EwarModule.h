 /**
  * @name EwarModule.h
  *   electronic warfare helpers shared by modules, drones, npcs and structures
  *   the state they create lives in system/EwarState.h
  */

#ifndef _EVE_SHIP_MODULES_EWAR_MODULE_H_
#define _EVE_SHIP_MODULES_EWAR_MODULE_H_

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
}

#endif  // _EVE_SHIP_MODULES_EWAR_MODULE_H_
