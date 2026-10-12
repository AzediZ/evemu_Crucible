/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
*/

/* Slash commands the Crucible client's GM menus send, which the server didn't have.
 *  /loop               GM: /fit me this type  ->  /loop <n> /fit me <typeID>
 *  /gag /ungag         character menu > Gag User
 *  /gagiskspammer      character menu > Gag ISK Spammer (also offered to legioneers)
 *  /baniskspammer      character menu > Ban ISK Spammer
 *  /nukem              benchmark script
 *  /entity deploy      WM: /Entity deploy this type
 *  /heat               testing: shows or sets rack heat
 *  /crime              testing: commits a crime, to bring CONCORD
 *  /missioncomplete    testing: completes a mission's kill objective (MissionObjectives::ForceComplete)
 *  /untrash           restores a trashed item from the junkyard, or lists an owner's trashed items  (F20)
 */

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "admin/AllCommands.h"
#include "missions/MissionObjectives.h"
#include "admin/CommandDB.h"
#include "admin/CommandDispatcher.h"
#include "admin/SlashService.h"
#include "admin/CommandHelper.h"
#include "chat/LSCService.h"
#include "ship/Ship.h"
#include "npc/LawEnforcement.h"
#include "npc/NPC.h"
#include "system/CrimeWatch.h"
#include "faction/WarManager.h"
#include "system/SystemManager.h"
#include "EVEServerConfig.h"
#include "services/ServiceManager.h"
#include "inventory/InventoryDB.h"
#include "inventory/ItemDB.h"

namespace {
    // a character by ID, ship or name.  only online characters
    Client* FindCharacter(const Seperator& args, uint32 index) {
        return cmd_find_client(args, index, nullptr);
    }

    void GagCharacter(Client* pGM, Client* pTarget, int64 minutes, const std::string& reason) {
        int64 until(GetFileTimeNow() + minutes * Win32Time_Minute);
        LSCService::Gag(pTarget->GetCharacterID(), until, reason, pGM->GetCharacterID());
        pTarget->SendNotifyMsg("You have been gagged for %lli minutes: %s", minutes, reason.c_str());
    }
}

PyResult Command_loop(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if ((args.argCount() < 3) or !args.isNumber(1))
        throw CustomError ("Correct Usage: /loop (count) (command...)");
    int count(atoi(args.arg(1).c_str()));
    if (count < 1)
        return nullptr;
    if (count > 100)
        count = 100;

    // rebuild the rest of the line as a command, keeping quoted arguments quoted
    std::string cmd;
    for (uint32 i = 2; i < args.argCount(); ++i) {
        std::string arg(args.arg(i));
        if (i == 2 and (arg.empty() or ((arg[0] != '/') and (arg[0] != '.'))))
            cmd += "/";
        if (arg.find(' ') != std::string::npos)
            arg = "\"" + arg + "\"";
        cmd += arg;
        if (i + 1 < args.argCount())
            cmd += " ";
    }
    Seperator first(&cmd.c_str()[1]);
    if (first.argCount() and (strcasecmp(first.arg(0).c_str(), "loop") == 0))
        throw CustomError ("/loop can't run /loop");

    SlashService* pSlash(services.Lookup <SlashService>("slash"));
    for (int i = 0; i < count; ++i)
        pSlash->SlashCommand(pClient, cmd);

    return new PyString("Done.");
}

PyResult Command_gag(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    // /gag <charID> "<reason>" <minutes>
    Client* pTarget(FindCharacter(args, 1));
    if (pTarget == nullptr)
        throw CustomError ("Correct Usage: /gag (online charID|name) [\"reason\"] [minutes]");
    std::string reason("Gagged by a GM");
    int64 minutes(30);
    if (args.argCount() > 2) {
        if (args.isNumber(2)) {
            minutes = atoll(args.arg(2).c_str());
        } else {
            reason = args.arg(2);
            if ((args.argCount() > 3) and args.isNumber(3))
                minutes = atoll(args.arg(3).c_str());
        }
    }
    if (minutes < 1)
        minutes = 1;
    GagCharacter(pClient, pTarget, minutes, reason);
    return new PyString("Gagged.");
}

PyResult Command_ungag(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    // online or offline, by ID or name
    uint32 charID(cmd_find_char_id(db, args, 1, nullptr));
    if (charID == 0)
        throw CustomError ("Correct Usage: /ungag (charID|name)");
    LSCService::Ungag(charID);
    Client* pTarget(sEntityList.FindClientByCharID(charID));
    if (pTarget != nullptr)
        pTarget->SendNotifyMsg("You are no longer gagged.");
    return new PyString("Ungagged.");
}

PyResult Command_gagiskspammer(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    Client* pTarget(FindCharacter(args, 1));
    if (pTarget == nullptr)
        throw CustomError ("Correct Usage: /gagiskspammer (online charID)");
    GagCharacter(pClient, pTarget, 30 * 24 * 60, "ISK spamming");
    return new PyString("Gagged for 30 days.");
}

PyResult Command_baniskspammer(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    Client* pTarget(FindCharacter(args, 1));
    if (pTarget == nullptr)
        throw CustomError ("Correct Usage: /baniskspammer (online charID)");
    LSCService::Gag(pTarget->GetCharacterID(), GetFileTimeNow() + 365 * Win32Time_Day, "ISK spamming", pClient->GetCharacterID());
    pTarget->BanClient();
    pTarget->DisconnectClient();
    return new PyString("Banned.");
}

PyResult Command_nukem(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    Seperator cmd("killallnpcs");
    return Command_killallnpcs(pClient, db, services, cmd);
}

// /entity deploy <count> <typeID|"type name">
PyResult Command_entity(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if ((args.argCount() < 4) or (strcasecmp(args.arg(1).c_str(), "deploy") != 0) or !args.isNumber(2))
        throw CustomError ("Correct Usage: /entity deploy (count) (typeID|\"type name\")");

    uint32 typeID(0);
    if (args.isNumber(3)) {
        typeID = atoi(args.arg(3).c_str());
    } else {
        // this hits the db directly, so refuse anything that looks like sql
        for (const auto cur : badCharsSearch)
            if (EvE::icontains(args.arg(3), cur))
                throw CustomError ("Name contains invalid characters");
        std::map<uint32_t, std::string> matches;
        if (!db->ItemSearch(args.arg(3).c_str(), matches))
            throw CustomError ("No type named %s", args.arg(3).c_str());
        for (auto cur : matches)
            if (strcasecmp(cur.second.c_str(), args.arg(3).c_str()) == 0)
                typeID = cur.first;
        if ((typeID == 0) and (matches.size() == 1))
            typeID = matches.begin()->first;
        if (typeID == 0)
            throw CustomError ("'%s' matches several types.  Use the full name or the typeID", args.arg(3).c_str());
    }

    std::string cmd("spawn " + std::to_string(typeID) + " " + args.arg(2));
    Seperator spawn(cmd.c_str());
    return Command_spawn(pClient, db, services, spawn);
}

// /heat [hi|med|low|all] [percent] [me|charID]
PyResult Command_heat(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    const char* usage("Correct Usage: /heat [hi|med|low|all] [percent] [me|charID]");
    Client* pTarget(pClient);
    // /heat (me|charID) only shows that pilot's heat
    bool show(false);
    if (args.argCount() == 2) {
        std::string rack(args.arg(1));
        if ((rack == "me") or args.isNumber(1)) {
            show = true;
            pTarget = cmd_find_client(args, 1, pClient);
            if (pTarget == nullptr)
                throw CustomError ("Character %s is not online", rack.c_str());
        }
    }
    if ((args.argCount() > 3) and (args.arg(3) != "me")) {
        pTarget = FindCharacter(args, 3);
        if (pTarget == nullptr)
            throw CustomError ("Character %s is not online", args.arg(3).c_str());
    }
    if (!pTarget->IsInSpace())
        throw CustomError ("%s is not in space", pTarget->GetName());
    ShipItemRef sRef(pTarget->GetShip());

    if ((args.argCount() > 1) and !show) {
        int8 first(0), last(2);
        std::string rack(args.arg(1));
        if (strcasecmp(rack.c_str(), "hi") == 0) {
            first = last = 0;
        } else if ((strcasecmp(rack.c_str(), "med") == 0) or (strcasecmp(rack.c_str(), "mid") == 0)) {
            first = last = 1;
        } else if ((strcasecmp(rack.c_str(), "low") == 0) or (strcasecmp(rack.c_str(), "lo") == 0)) {
            first = last = 2;
        } else if (strcasecmp(rack.c_str(), "all") != 0) {
            throw CustomError (usage);
        }
        if ((args.argCount() < 3) or !args.isNumber(2))
            throw CustomError (usage);
        double percent(strtod(args.arg(2).c_str(), nullptr));
        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;
        const uint16 capacity[3] = { AttrHeatCapacityHi, AttrHeatCapacityMed, AttrHeatCapacityLow };
        for (int8 i = first; i <= last; ++i)
            sRef->SetRackHeat(i, percent / 100.0 * sRef->GetAttribute(capacity[i]).get_double());
    }

    const uint16 capacity[3] = { AttrHeatCapacityHi, AttrHeatCapacityMed, AttrHeatCapacityLow };
    double pct[3];
    for (int8 i = 0; i < 3; ++i) {
        double cap(sRef->GetAttribute(capacity[i]).get_double());
        pct[i] = (cap > 0 ? sRef->GetRackHeat(i) / cap * 100.0 : 0);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s rack heat: high %.1f%%, medium %.1f%%, low %.1f%%", sRef->name(), pct[0], pct[1], pct[2]);
    return new PyString(buf);
}

/* /crime (me|charID|entityID) [victimID]
 *  starts CONCORD the way the game does, from the first act:
 *   a pilot: an attack on the victim, the same call a weapon cycle makes (CrimeWatch::OffensiveAct), so the normal rules
 *     decide whether it's a crime.  with no victim the victim is you, or a civilian (nobody, never a legal target) if
 *     the pilot is you.  a crime gives the criminal flag, the security status hit, and CONCORD in high-sec.
 *   an NPC or any other entity: NPCs never commit crimes, so it's marked a criminal as if it had attacked the victim.
 *     CONCORD comes for it in high-sec, and sentries shoot it in empire space.
 */
PyResult Command_crime(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    const char* usage("Correct Usage: /crime (me|charID|entityID) [victimID]");
    if (!sConfig.crime.Enabled)
        throw CustomError ("CrimeWatch is disabled (<crime><Enabled> in the server config).");
    if (args.argCount() < 2)
        throw CustomError (usage);

    Client* pAggressor(nullptr);
    SystemEntity* pEntity(nullptr);
    if (args.arg(1) == "me") {
        pAggressor = pClient;
    } else if (args.isNumber(1)) {
        uint32 id(atoi(args.arg(1).c_str()));
        if (IsCharacterID(id)) {
            pAggressor = sEntityList.FindClientByCharID(id);
            if (pAggressor == nullptr)
                throw CustomError ("Character %u is not online.", id);
        } else if (pClient->SystemMgr() != nullptr) {
            pEntity = pClient->SystemMgr()->GetSE(id);
            if (pEntity == nullptr)
                throw CustomError ("%u is not in space in your system.", id);
            if (pEntity->IsStaticEntity())
                throw CustomError ("%s can't commit crimes.", pEntity->GetName());
            // a ship with a pilot is the pilot's crime
            if (pEntity->HasPilot())
                pAggressor = pEntity->GetPilot();
        }
    } else {
        throw CustomError (usage);
    }

    SystemManager* pSystem(pAggressor != nullptr ? pAggressor->SystemMgr() : pEntity->SystemMgr());
    if (pSystem == nullptr)
        throw CustomError ("The aggressor isn't in a solar system.");

    SystemEntity* pVictim(nullptr);
    if (args.argCount() > 2) {
        if (!args.isNumber(2))
            throw CustomError (usage);
        pVictim = pSystem->GetSE(atoi(args.arg(2).c_str()));
        if (pVictim == nullptr)
            throw CustomError ("Victim %s is not in space in the aggressor's system.", args.arg(2).c_str());
    } else if ((pAggressor != pClient) and (pClient->GetShipSE() != nullptr) and (pClient->SystemMgr() == pSystem)) {
        pVictim = pClient->GetShipSE();
    }

    float trueSec(pSystem->GetSystemSecurityRating());
    uint8 secClass(CrimeWatch::SecurityClass(trueSec));
    std::string where(pSystem->GetNameStr() + " (" + std::to_string(CrimeWatch::SecurityLevel(trueSec)).substr(0, 3) + ")");
    if (secClass == Crime::SecClass::Null)
        throw CustomError ("%s is null-sec.  There are no crimes there.", where.c_str());

    std::string who, outcome;
    bool criminal(false);
    if (pAggressor != nullptr) {
        who = pAggressor->GetName();
        if (pVictim != nullptr) {
            sCrimeWatch.OffensiveAct(pAggressor, pVictim);
        } else {
            sCrimeWatch.SimulateCrime(pAggressor);
        }
        criminal = sCrimeWatch.IsCriminal(pAggressor->GetCharacterID());
        if (!criminal)
            outcome = " The attack on " + std::string(pVictim != nullptr ? pVictim->GetName() : "the victim") + " was legal, so it's no crime.";
    } else {
        who = pEntity->GetName();
        sCrimeWatch.EntityCrime(pEntity, pVictim);
        criminal = sCrimeWatch.IsCriminal(pEntity->GetID());
    }

    char buf[256];
    if (!criminal) {
        snprintf(buf, sizeof(buf), "%s is not a criminal.%s", who.c_str(), outcome.c_str());
    } else if ((secClass == Crime::SecClass::High) and sConfig.crime.Concord) {
        snprintf(buf, sizeof(buf), "%s is now a criminal in %s.  CONCORD arrives in about %us.", who.c_str(), where.c_str(),
                pSystem->GetLaw()->ConcordDelay());
    } else {
        snprintf(buf, sizeof(buf), "%s is now a criminal in %s.  No CONCORD in low-sec, but the sentries will shoot.", who.c_str(), where.c_str());
    }
    return new PyString(buf);
}

PyResult Command_missioncomplete(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (!args.isNumber(1))
        throw CustomError ("Correct Usage: /missioncomplete (agentID) [charID]");
    uint32 agentID(atoi(args.arg(1).c_str()));
    uint32 charID(pClient->GetCharacterID());
    if (args.isNumber(2))
        charID = atoi(args.arg(2).c_str());
    if (!MissionObjectives::ForceComplete(charID, agentID))
        throw CustomError ("No accepted kill mission of %u with agent %u.", charID, agentID);
    return new PyString("Mission objective completed.");
}

PyResult Command_untrash(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() < 2)
        throw CustomError ("Correct Usage: /untrash (itemID) | list (me|charID|corpID)");

    if (args.arg(1) == "list") {
        uint32 ownerID(pClient->GetCharacterID());
        if (args.isNumber(2))
            ownerID = atoi(args.arg(2).c_str());
        DBQueryResult res;
        InventoryDB::ListTrash(ownerID, res);
        std::string reply("Trashed items of ");
        reply += std::to_string(ownerID);
        reply += " (newest first):<br>";
        DBResultRow row;
        uint16 count(0);
        while (res.GetRow(row)) {
            const ItemType* pType(sItemFactory.GetType(row.GetUInt(1)));
            reply += std::to_string(row.GetUInt(0)) + "  " + (pType == nullptr ? "Unknown" : pType->name())
                   + "  x" + std::to_string(row.GetInt(2) < 1 ? 1 : row.GetInt(2))
                   + "  " + Win32TimeToString(row.GetInt64(3)) + "<br>";
            ++count;
        }
        if (count == 0)
            reply += "nothing";
        return new PyString(reply);
    }

    if (!args.isNumber(1))
        throw CustomError ("Correct Usage: /untrash (itemID) | list (me|charID|corpID)");
    uint32 itemID(atoi(args.arg(1).c_str()));
    uint32 ownerID(0), locationID(0);
    uint16 flag(0);
    if (!InventoryDB::GetTrashLog(itemID, ownerID, locationID, flag))
        throw CustomError ("%u is not in the trash.", itemID);

    InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
    if (iRef.get() == nullptr)
        throw CustomError ("Trashed item %u can't be loaded.", itemID);
    if (iRef->locationID() != locJunkyard) {
        InventoryDB::DeleteTrashLog(itemID);
        throw CustomError ("%s (%u) isn't in the junkyard any more.", iRef->name(), itemID);
    }

    // back where it was.  this saves it and tells the owner
    iRef->Move(locationID, (EVEItemFlags)flag, true);
    InventoryDB::DeleteTrashLog(itemID);
    return new PyString("Restored.");
}

PyResult Command_war(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    const char* usage("Correct Usage: /war list [ownerID] | declare (declarerID) (againstID) | retract (warID) | mutual (warID) (0|1) | start (warID) | end (warID)");
    if (args.argCount() < 2)
        throw CustomError (usage);

    const std::string& what(args.arg(1));
    if (what == "list") {
        uint32 ownerID(pClient->GetAllianceID() != 0 ? pClient->GetAllianceID() : pClient->GetCorporationID());
        if ((args.argCount() > 2) and args.isNumber(2))
            ownerID = atoi(args.arg(2).c_str());
        std::string reply;
        sWarMgr.GMList(ownerID, reply);
        return new PyString(reply);
    }

    if ((args.argCount() < 3) or !args.isNumber(2))
        throw CustomError (usage);
    uint32 id(atoi(args.arg(2).c_str()));

    if (what == "declare") {
        if ((args.argCount() < 4) or !args.isNumber(3))
            throw CustomError (usage);
        uint32 warID(sWarMgr.DeclareWar(id, atoi(args.arg(3).c_str()), pClient->GetCharacterID(), false));
        return new PyString("War " + std::to_string(warID) + " declared.  It is hostile in 24 hours (/war start " + std::to_string(warID) + " to skip that).");
    } else if (what == "retract") {
        sWarMgr.GMRetractWar(id);
        return new PyString("Retracted.  The war can be fought for 24 more hours.");
    } else if (what == "mutual") {
        if ((args.argCount() < 4) or !args.isNumber(3))
            throw CustomError (usage);
        // as the defender would
        sWarMgr.GMSetMutual(id, atoi(args.arg(3).c_str()) != 0);
        return new PyString("Done.");
    } else if (what == "start") {
        sWarMgr.GMSkipSpoolUp(id);
        return new PyString("The war is hostile now.");
    } else if (what == "end") {
        sWarMgr.GMEndWar(id);
        return new PyString("Ended.  The war can be fought for 24 more hours.");
    }
    throw CustomError (usage);
}
