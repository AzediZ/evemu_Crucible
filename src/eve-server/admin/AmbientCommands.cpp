/**
 * @name AmbientCommands.cpp
 *     ambient npc observability and tests (doc/code_and_design_notes/planning/ambient-npc-plan.md 4.5)
 *
 *   /ambient                       this system:  active or not, busyness and its terms, rates, watched grids, ships by role,
 *                                   off-grid travellers, cap hits, the last events, us per tick
 *   /ambient profile [systemID]    the busyness terms (H, P, R, S) of a system (default:  yours)
 *   /ambient spawn <kind> [count]  force ambient ships on your grid
 *   /ambient clear                 every ambient ship of this system goes, silently
 *   /ambient global                totals over all systems, the transit queue and the global cap
 */

#include "eve-server.h"

#include "Client.h"
#include "admin/AllCommands.h"
#include "admin/CommandDB.h"
#include "npc/ambient/AmbientMgr.h"
#include "npc/ambient/AmbientSystem.h"
#include "system/SystemManager.h"

PyResult Command_ambient(Client* pClient, CommandDB* db, EVEServiceManager& services, const Seperator& args)
{
    if (pClient == nullptr)
        throw CustomError ("ambient: no caller.");
    std::string what(args.argCount() > 1 ? args.arg(1) : "");
    SystemManager* pSys(pClient->SystemMgr());

    if (what == "global") {
        std::string report(sAmbientMgr.DescribeGlobal());
        pClient->SendInfoModalMsg("%s", report.c_str());
        return new PyString("ambient global");
    }
    if (what == "profile") {
        uint32 systemID(pSys != nullptr ? pSys->GetID() : 0);
        if ((args.argCount() > 2) and args.isNumber(2))
            systemID = atoi(args.arg(2).c_str());
        if (systemID == 0)
            throw CustomError ("Correct Usage: /ambient profile [systemID]");
        std::string report(sAmbientMgr.DescribeProfile(systemID));
        pClient->SendInfoModalMsg("%s", report.c_str());
        return new PyString("ambient profile");
    }
    if ((pSys == nullptr) or (pSys->GetAmbient() == nullptr))
        throw CustomError ("ambient: you have to be in a solar system.");
    AmbientSystem* pAmb(pSys->GetAmbient());

    if (what == "spawn") {
        if (args.argCount() < 3)
            throw CustomError ("Correct Usage: /ambient spawn <hauler|courier|freighter|navy|concord|miner|criminal> [count]");
        uint32 count(1);
        if ((args.argCount() > 3) and args.isNumber(3))
            count = std::max(1, std::min(25, atoi(args.arg(3).c_str())));
        std::string why;
        uint32 made(pAmb->ForceSpawn(pClient, args.arg(2), count, why));
        if ((made == 0) and !why.empty())
            throw CustomError ("ambient: %s.", why.c_str());
        pClient->SendNotifyMsg("ambient:  %u %s spawned%s%s", made, args.arg(2).c_str(), (why.empty() ? "" : ".  "), why.c_str());
        return new PyString("ambient spawn");
    }
    if (what == "clear") {
        uint32 count(pAmb->ClearAll());
        pClient->SendNotifyMsg("ambient:  %u ships leave %s.", count, pSys->GetName());
        return new PyString("ambient clear");
    }
    if (!what.empty())
        throw CustomError ("Correct Usage: /ambient [profile [systemID] | spawn <kind> [count] | clear | global]");

    std::string report(pAmb->Describe());
    pClient->SendInfoModalMsg("%s", report.c_str());
    return new PyString("ambient");
}
