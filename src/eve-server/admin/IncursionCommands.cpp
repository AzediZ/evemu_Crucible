/**
 * @name IncursionCommands.cpp
 *     GM commands for Sansha incursions
 *
 * @date:          12 October 2026
 *
 */

#include "eve-server.h"

#include "admin/AllCommands.h"
#include "admin/CommandDB.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/IncursionMgr.h"

// constellation named by args[idx]: 'here', a constellationID, or 0 when the arg is missing
static uint32 GetConstellationArg(Client* pClient, const Seperator& args, size_t idx)
{
    if (args.argCount() <= idx)
        return 0;
    if (args.arg(idx) == "here") {
        if (pClient->SystemMgr() == nullptr)
            throw CustomError ("You don't appear to be in a solar system.");
        return pClient->SystemMgr()->GetConstellationID();
    }
    if (args.isNumber(idx))
        return atoi(args.arg(idx).c_str());
    throw CustomError ("'%s' is not 'here' or a constellationID.", args.arg(idx).c_str());
}

PyResult Command_incursion(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() < 2)
        throw CustomError ("Usage: /incursion start [here|constellationID] | end [here|constellationID] | info | influence (0-100) [here|constellationID] | mothership [here|constellationID]");

    const std::string& cmd = args.arg(1);
    if (cmd == "start")
        return new PyString(sIncursionMgr.Start(GetConstellationArg(pClient, args, 2)));
    if (cmd == "end")
        return new PyString(sIncursionMgr.End(GetConstellationArg(pClient, args, 2)));
    if (cmd == "info")
        return new PyString(sIncursionMgr.Info());
    if (cmd == "mothership")
        return new PyString(sIncursionMgr.Mothership(GetConstellationArg(pClient, args, 2)));
    if (cmd == "influence") {
        if ((args.argCount() < 3) or !args.isNumber(2))
            throw CustomError ("Usage: /incursion influence (0-100) [here|constellationID]");
        return new PyString(sIncursionMgr.SetInfluence(GetConstellationArg(pClient, args, 3), atof(args.arg(2).c_str())));
    }

    throw CustomError ("Unknown option '%s'.  Use start, end, info, influence or mothership.", cmd.c_str());
}
