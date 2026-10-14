 /**
  * @name SimFacilityCommands.cpp
  *   /simfacility:  the market simulation's station lines (doc/code_and_design_notes/planning/market-sim/PLAN.md 6.6)
  *
  * @date:          15Oct26
  *
  */

#include "eve-server.h"

#include "Client.h"
#include "admin/AllCommands.h"
#include "market/sim/SimFacilityMgr.h"

PyResult Command_simfacility(Client* who, CommandDB* db, EVEServiceManager& services, const Seperator& args)
{
    return new PyString(sSimFacilities.Command(who, args));
}
