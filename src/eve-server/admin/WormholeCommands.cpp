/**
 * @name WormholeCommands.cpp
 *     GM commands for wormholes
 *
 * @date:          15 October 2026
 *
 */

#include "eve-server.h"

#include "Client.h"
#include "admin/AllCommands.h"
#include "admin/CommandDB.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/WormholeMgr.h"

PyResult Command_wormhole(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (!sWHMgr.IsEnabled())
        throw CustomError ("Wormholes are switched off (WormHoleEnabled in the cosmic section of the server config).");
    SystemManager* pSys(pClient->SystemMgr());
    if (pSys == nullptr)
        throw CustomError ("You don't appear to be in a solar system.");

    std::string cmd(args.argCount() < 2 ? "info" : args.arg(1));
    if (cmd == "info")
        return new PyString(sWHMgr.GetInfo(pSys->GetID()));
    if (cmd == "spawn") {
        if ((args.argCount() < 3) or !args.isNumber(2))
            throw CustomError ("Usage: /wormhole spawn typeID  (a wormhole type, not K162)");
        if (pClient->IsDocked() or (pClient->GetShipSE() == nullptr))
            throw CustomError ("You must be in space.");
        GPoint pos(pClient->GetShipSE()->GetPosition());
        pos.MakeRandomPointOnSphereLayer(15000, 20000);
        uint32 itemID(sWHMgr.SpawnAt(pSys, atoi(args.arg(2).c_str()), pos));
        if (itemID == 0)
            throw CustomError ("Could not open a wormhole of type %s here.", args.arg(2).c_str());
        return new PyString("Opened wormhole " + std::to_string(itemID) + " about 15 km from you.  Its far end opens when a ship warps to it.");
    }
    if (cmd == "collapse") {
        if ((args.argCount() < 3) or !args.isNumber(2))
            throw CustomError ("Usage: /wormhole collapse itemID  (either end)");
        if (!sWHMgr.CollapseByItem(atoi(args.arg(2).c_str())))
            throw CustomError ("%s is not an open wormhole.", args.arg(2).c_str());
        return new PyString("Collapsed.");
    }
    throw CustomError ("Unknown option '%s'.  Use info, spawn or collapse.", cmd.c_str());
}
