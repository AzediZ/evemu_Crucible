
 /**
  * @name MarketSimCommands.cpp
  *   /mktsim:  the market simulation's GM command (doc/code_and_design_notes/market-sim/PLAN.md 9.5)
  *
  * @date:          13Oct26
  *
  */

#include "eve-server.h"

#include "Client.h"
#include "admin/AllCommands.h"
#include "market/sim/MarketSimMgr.h"

PyResult Command_mktsim(Client* who, CommandDB* db, EVEServiceManager& services, const Seperator& args)
{
    return new PyString(sMktSim.Command(who, args));
}
