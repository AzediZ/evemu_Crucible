
/**
 * @name DungeonCommands.cpp
 *     Dungeon Command system for EVEmu
 *
 * @Author:        Allan
 * @date:          20 August 2019
 *
 */


#include <stdio.h>
#include <algorithm>
#include <map>
#include "eve-server.h"

#include "admin/AllCommands.h"
#include "admin/CommandDB.h"

#include "system/SolarSystem.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/cosmicMgrs/DungeonMgr.h"
#include "system/cosmicMgrs/SpawnMgr.h"
#include "StaticDataMgr.h"



PyResult Command_savedungeon(Client* pClient, CommandDB* db, EVEServiceManager& services, const Seperator& args)
{
    /**  the idea here is to save a dungeon room template while ingame.
     *  the client calling this is used as "warp-in point" which is the origin
     *  all items in bubble will be queried and their positions calculated, then saved.
     * we should expect a roomID here, and check for existing roomIDs to avoid overwrite.
     *      - maybe later when system matures, we can allow overwrite.  u/k at this time.
     */

    if (pClient == nullptr) // should never hit,
        throw CustomError ("SaveDungeon: Invalid Caller - Client sent NULL.");

    if (args.argCount() < 2)
        throw CustomError ("SaveDungeon: Missing Arguments (use '.savedungeon help' for usage)");


    if (args.argCount() == 2) { //
        if (args.isNumber(1)) {
            // not used yet
        }
    }

    if (strcmp(args.arg(1).c_str(), "help") == 0) {
        //  {.savedungeon help}  will display the following information in notification window
        std::ostringstream str; // for 'help' printing
        str << ".savedungeon [groupID] [group name/description]<br>"; //55
        int size = 55;
        char* reply = Memory::Allocator::NewArray<char>(&sAllocators.tickAllocator, size);
        snprintf(reply, size, str.str().c_str());
        pClient->SendInfoModalMsg(reply);
        return nullptr;
    } else if (args.isNumber(1)) {
        // not used yet
    }

    throw CustomError ("SaveDungeon: - This command is currently incomplete.");
}

PyResult Command_printdungeon(Client* pClient, CommandDB* db, EVEServiceManager& services, const Seperator& args)
{
    /*  this will print all items in current dungeon, along with it's templateID and roomID
     *
     */

    throw CustomError ("PrintDungeon: - This command is currently incomplete.");
}

// this is outdated
PyResult Command_spawndungeon(Client* pClient, CommandDB* db, EVEServiceManager& services, const Seperator& args) {
    /* this command is used to test dungeon spawn system - wip.   -allan 21Feb15
     */

    if (args.argCount() != 2)
        throw CustomError ("Correct Usage: .spawndungeon <dungeonTemplateID>");

    if (!args.isNumber(1))
        throw CustomError ("Argument must be a template ID.");

    // pClient->SystemMgr()->GetDungMgr()->Create(args.arg(1));   // this is missing sig data.

    throw CustomError ("SpawnDungeon: - This command is currently incomplete.");
    return nullptr;
}

// this was never completed
PyResult Command_removedungeon(Client* pClient, CommandDB* db, EVEServiceManager& services, const Seperator& args) {
    /* this command is used to test dungeon spawn system - wip.   -allan 21Feb15
     *
     * upon execution, this command will spawn a random dungeon from db in callers solarSystem,
     *   then create a bookmark in their PnP/BM window
     */
    throw CustomError ("RemoveDungeon: - This command is currently incomplete.");
    return nullptr;
}

/* DED complexes (archetype 10, difficulty = the rating):  /ded lists them, /ded spawn puts one near the caller's ship as a
 *  scannable signature, so it can be tested without probing for one */
PyResult Command_ded(Client* pClient, CommandDB* db, EVEServiceManager& services, const Seperator& args)
{
    SystemManager* pSys(pClient->SystemMgr());
    if (pSys == nullptr)
        throw CustomError ("You don't appear to be in a solar system.");
    std::vector<uint32> list;
    sDunDataMgr.GetByArchetype(Dungeon::Type::Rated, list);
    if (list.empty())
        throw CustomError ("There are no DED complexes in the database.  Has the ded-complexes migration been applied?");

    std::string cmd(args.argCount() < 2 ? "list" : args.arg(1));
    if (cmd == "list") {
        std::map<std::pair<uint8, std::string>, uint32> sorted;
        for (auto id : list) {
            std::shared_ptr<const Dungeon::Dungeon> pDun(sDunDataMgr.GetDungeon(id));
            if (pDun.get() != nullptr)
                sorted[std::make_pair(pDun->difficulty, pDun->name)] = id;
        }
        std::string msg;
        for (auto& cur : sorted)
            msg += std::to_string(cur.first.first) + "/10  " + cur.first.second + "  (" + std::to_string(cur.second) + ")<br>";
        return new PyString(msg);
    }
    if (cmd == "spawn") {
        if ((args.argCount() < 3) or !args.isNumber(2))
            throw CustomError ("Usage: /ded spawn rating|dungeonID");
        if (pClient->IsDocked() or (pClient->GetShipSE() == nullptr))
            throw CustomError ("You must be in space.");
        uint32 arg(atoi(args.arg(2).c_str()));
        uint32 dungeonID(0);
        if (arg <= 10) {
            // this region's pirates if they have a complex of that rating, else anyone's
            uint32 faction(sDataMgr.GetRegionRatFaction(pSys->GetRegionID()));
            std::vector<uint32> mine, any;
            for (auto id : list) {
                std::shared_ptr<const Dungeon::Dungeon> pDun(sDunDataMgr.GetDungeon(id));
                if ((pDun.get() == nullptr) or (pDun->difficulty != arg))
                    continue;
                any.push_back(id);
                if (pDun->factionID == faction)
                    mine.push_back(id);
            }
            std::vector<uint32>& from(mine.empty() ? any : mine);
            if (from.empty())
                throw CustomError ("There is no %u/10 complex.", arg);
            dungeonID = from[std::min<size_t>(MakeRandomInt(0, from.size()), from.size() - 1)];
        } else if (std::find(list.begin(), list.end(), arg) != list.end()) {
            dungeonID = arg;
        } else {
            throw CustomError ("%u is not a DED complex.  /ded list shows them.", arg);
        }
        GPoint pos(pClient->GetShipSE()->GetPosition());
        pos.MakeRandomPointOnSphereLayer(18000, 22000);
        uint32 itemID(pSys->GetAnomMgr()->CreateAnomaly(Dungeon::Type::Rated, dungeonID, &pos));
        if (itemID == 0)
            throw CustomError ("The complex could not be spawned here (see the server log).");
        std::shared_ptr<const Dungeon::Dungeon> pDun(sDunDataMgr.GetDungeon(dungeonID));
        return new PyString("Spawned " + (pDun.get() != nullptr ? pDun->name : std::string("the complex")) + " about 20 km from you (signature " + std::to_string(itemID) + ").  Its entry gate is on your grid.");
    }
    throw CustomError ("Unknown option '%s'.  Use list or spawn.", cmd.c_str());
}
