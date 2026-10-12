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
    Author:        Zhur
    Updates:        Allan
*/

/** @todo this file needs to be updated... */


#include "eve-server.h"

#include "account/AccountService.h"
//#include "../../eve-common/EVE_Skills.h"

#include "Client.h"
#include "ConsoleCommands.h"
#include "admin/AllCommands.h"
#include "admin/CommandDB.h"
#include "inventory/AttributeEnum.h"
#include "inventory/InventoryDB.h"
#include "inventory/InventoryItem.h"
#include "manufacturing/Blueprint.h"
#include "map/MapConnections.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "system/Damage.h"
#include "system/DestinyManager.h"
#include "system/SystemManager.h"
#include "system/SystemBubble.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "corporation/LPService.h"
#include "corporation/CorporationDB.h"
#include "admin/CommandHelper.h"
#include "chat/LSCService.h"
#include "ship/Ship.h"
#include "station/StationDataMgr.h"
#include "ship/modules/GenericModule.h"
#include "ship/modules/ModuleManager.h"

// owners of a spawned entity, by what it is and where
static void SetSpawnOwner(Client* pClient, DBSystemDynamicEntity& entity)
{
    switch (entity.categoryID) {
        case EVEDB::invCategories::Entity: {
            // owned by system rats
            entity.factionID = sDataMgr.GetRegionRatFaction(pClient->GetRegionID());
            entity.allianceID = entity.factionID;
            entity.corporationID = sDataMgr.GetFactionCorp(entity.factionID);
            entity.ownerID = entity.corporationID;
        } break;
        case EVEDB::invCategories::Ship: {
            // owned by calling char
            entity.allianceID = pClient->GetAllianceID();
            entity.corporationID = pClient->GetCorporationID();
            entity.factionID = pClient->GetWarFactionID();
            entity.ownerID = pClient->GetCharacterID();
        } break;
        default: {
            //owned by system sov holder
            entity.factionID = sDataMgr.GetRegionFaction(pClient->GetRegionID());
            entity.allianceID = entity.factionID;
            entity.corporationID = sDataMgr.GetFactionCorp(entity.factionID);
            entity.ownerID = entity.corporationID;
        } break;
    }
}

PyResult Command_search(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    if (args.argCount() < 2) {
        throw CustomError ("Correct Usage: /search [text]");
    }

    const std::string& query = args.arg(1);

    //an empty query is a bad idea.
    if (query.length() == 0)
        throw CustomError ("Usage: /search [text]");

    std::map<uint32, std::string> matches;
    if (!db->ItemSearch(query.c_str(), matches))
        throw CustomError ("Failed to query DB.");

    std::string result(std::to_string(matches.size()));
    result += " matches found.<br>";

    std::map<uint32, std::string>::iterator cur, end;
    cur = matches.begin();
    end = matches.end();
    for(; cur != end; cur++) {
        result += std::to_string(cur->first);
        result += ": ";
        result += cur->second;
        result += "<br>";
    }

    if (10 < matches.size()) {
        //send the results in an evemail.
        std::string subject("Search results for ");
        subject += query;

        who->SelfEveMail(subject.c_str(), result.c_str());

        return new PyString("Results sent via evemail.");
    } else
        return new PyString(result);
}

PyResult Command_giveisk(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() < 3)
        throw CustomError ("Correct Usage: /giveisk [charID|me] [amount]");

    uint32 charID(0);
    if (args.arg(1) == "me") {
        charID = who->GetCharacterID();
    } else if (args.isNumber(1)) {
        charID = atol(args.arg(1).c_str());
    } else {
        throw CustomError ("Argument 1 should be a character ID ('me'=self)");
    }
    if (!IsCharacterID(charID))
        throw CustomError ("%u is not a character ID", charID);

    if (!args.isNumber(2))
        throw CustomError ("Argument 2 should be an amount of ISK");
    double amount(strtod(args.arg(2).c_str(), nullptr));
    if (amount == 0.0)
        throw CustomError ("Amount must not be zero");

    // works for offline characters too
    std::string reason("GM grant by ");
    reason += who->GetName();
    if (amount > 0.0) {
        AccountService::TransferFunds(corpSCC, charID, amount, reason, Journal::EntryType::GMCashTransfer);
    } else {
        AccountService::TransferFunds(charID, corpSCC, -amount, reason, Journal::EntryType::GMCashTransfer);
    }
    return new PyString("Operation successful.");
}

PyResult Command_freesp(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() < 3)
        throw CustomError ("Correct Usage: /freesp [charID|me] [amount]");

    uint32 charID(0);
    if (args.arg(1) == "me") {
        charID = who->GetCharacterID();
    } else if (args.isNumber(1)) {
        charID = atol(args.arg(1).c_str());
    } else {
        throw CustomError ("Argument 1 should be a character ID ('me'=self)");
    }
    if (!IsCharacterID(charID))
        throw CustomError ("%u is not a character ID", charID);

    int32 amount(atoi(args.arg(2).c_str()));
    if (amount == 0)
        throw CustomError ("Argument 2 should be a non-zero amount of skill points");

    // works for offline characters too
    CharacterRef cRef(sItemFactory.GetCharacterRef(charID));
    if (cRef.get() == nullptr)
        throw CustomError ("Character %u not found", charID);
    cRef->AddFreeSkillPoints(amount);
    return new PyString("Operation successful.");
}

PyResult Command_pop(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (4 != args.argCount())
        throw CustomError ("Correct Usage: /pop [message type] [key] [text]");

    //CustomNotify: notify
    //ServerMessage: msg
    //CustomError: error

    const std::string& msgType = args.arg(1);
    const std::string& key = args.arg(2);
    const std::string& text = args.arg(3);

    Notify_OnRemoteMessage n;
    n.msgType = msgType;
    n.args[ key ] = new PyString(text);

    PyTuple* t = n.Encode();
    who->SendNotification("OnRemoteMessage", "charid", &t);
    PySafeDecRef(t);

    return new PyString("Message sent.");
}

PyResult Command_spawnn(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    int typeID = 0;
    uint32 actualTypeID = 0;
    std::string actualTypeName = "";
    uint32 actualGroupID = 0;
    uint32 actualCategoryID = 0;
    double actualRadius = 0.0;
    InventoryItemRef item;
    ShipItemRef ship;

    // Updated(groove)
    // "/spawnn" arguments:  the client's "WM: /Spawn this type" sends '/spawnN <qty> 4000 <typeID>'
    // #1 = quantity
    // #2 = spawn spread, meters from the caller
    // #3 = typeID

    if ((args.argCount() < 4) || (args.argCount() > 4))
    {
        throw CustomError ("LOL, we don't know the correct usage of /spawnn, sorry you're S.O.L., BUT it should have 4 arguments.");
    }

    if (!args.isNumber(3))
        throw CustomError ("Argument 3 should be an item type ID");

    typeID = atoi(args.arg(3).c_str());
    if (typeID < 34)
        throw CustomError ("Invalid Type ID.");

    if (!who->IsInSpace())
        throw CustomError ("You must be in space to spawn things.");

    // Search for item type using typeID:
    if (!db->ItemSearch(typeID, actualTypeID, actualTypeName, actualGroupID, actualCategoryID, actualRadius)) {
        return new PyString("Unknown typeID or typeName returned no matches.");
    }

    int count(args.isNumber(1) ? atoi(args.arg(1).c_str()) : 1);
    if (count < 1)
        count = 1;
    if (count > 50)
        count = 50;
    double spread(args.isNumber(2) ? strtod(args.arg(2).c_str(), nullptr) : 0.0);
    for (int spawned = 0; spawned < count; ++spawned) {
    GPoint loc(who->GetShipSE()->GetPosition());
    double radius(spread);
    if (radius <= 0)
        // a radius equal to the radius of the ship/celestial being spawned times 5-15 for really good measure of separation:
        radius = (actualRadius * 5.0) * (double)(MakeRandomInt(1, 3));
    // keep clear of the caller's ship
    if (radius < actualRadius + who->GetShipSE()->GetRadius())
        radius = actualRadius + who->GetShipSE()->GetRadius();
    loc.MakeRandomPointOnSphere(radius);

    // Spawn the item:
    ItemData idata(
        actualTypeID,
        1, // owner is EVE System
        who->GetLocationID(),
        flagNone,
        actualTypeName.c_str(),
        loc
   );

    item = sItemFactory.SpawnItem(idata);
    if (item.get() == nullptr)
        throw CustomError ("Unable to spawn item of type %u.", typeID);

    DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.categoryID = actualCategoryID;
        entity.groupID = actualGroupID;
        entity.itemID = item->itemID();
        entity.itemName = actualTypeName;
        entity.typeID = actualTypeID;
        entity.position = loc;
        SetSpawnOwner(who, entity);

    // Actually do the spawn using SystemManager's BuildEntity:
    if (!who->SystemMgr()->BuildDynamicEntity(entity))
        return new PyString("Spawn Failed: typeID or typeName not supported.");
    }

    sLog.Yellow("Command", "%s: Spawned %u x%i.", who->GetName(), typeID, count);

    return new PyString("Spawn successful.");
}

PyResult Command_spawn(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    int spawnCount = 1;
    uint32 spawnIndex = 0;
    uint32 maximumSpawnCountAllowed = 100;
    uint32 actualTypeID = 0;
    std::string actualTypeName = "";
    uint32 actualGroupID = 0;
    uint32 actualCategoryID = 0;
    double actualRadius = 0.0;
    InventoryItemRef item;
    ShipItemRef ship;
    double radius;
    bool offsetLocationSet = false;
    std::string usage = "Correct Usage: <br><br> /spawn [typeID(int)/typeName(string)] <br><br>With optional spawn count: <br> /spawn [typeID(int)/typeName(string)] [count] <br><br>With optional count and (X,Y,Z) coordinate: <br> /spawn [typeID(int/typeName(string)] [count] [x(float)] [y(float)] [z(float)]";

    if (!pClient->IsInSpace())
        throw CustomError ("You must be in space to spawn things.");

    if (args.argCount() < 2) {
        throw CustomError (usage.c_str());
    }

    if (!args.isNumber(1))
        throw CustomError ("Argument 1 should be an item type ID");

    int typeID = atoi(args.arg(1).c_str());

    // Search for item type using typeID:
    if (!(db->ItemSearch(typeID, actualTypeID, actualTypeName, actualGroupID, actualCategoryID, actualRadius)))
    {
        return new PyString("Unknown typeID or typeName returned no matches.");
    }

    if (args.argCount() > 2)
    {
        if (!(args.isNumber(2)))
            throw CustomError ("Argument 3 should be the number of spawns of this type you want to create");

        spawnCount = atoi(args.arg(2).c_str());
        if (spawnCount > maximumSpawnCountAllowed)
            throw CustomError ("Argument 3, spawn count, is allowed to be no more than 100");
    }

    // Check to see if the X Y Z optional coordinates were supplied with the command:
    GPoint offsetLocation;
    if (args.argCount() > 3)
    {
        if (!(args.isNumber(3)))
            throw CustomError ("Argument 4 should be the X distance from your ship in meters you want the item spawned");

        if (args.argCount() > 4)
        {
            if (!(args.isNumber(4)))
                throw CustomError ("Argument 5 should be the Y distance from your ship in meters you want the item spawned");
        }
        else
            throw CustomError ("TOO FEW PARAMETERS: %s", usage.c_str());

        if (args.argCount() > 5)
        {
            if (!(args.isNumber(5)))
                throw CustomError ("Argument 6 should be the Z distance from your ship in meters you want the item spawned");
        }
        else
            throw CustomError ("TOO FEW PARAMETERS: %s", usage.c_str());

        offsetLocation.x = atoll(args.arg(3).c_str());
        offsetLocation.y = atoll(args.arg(4).c_str());
        offsetLocation.z = atoll(args.arg(5).c_str());
        offsetLocationSet = true;
    }

    GPoint loc;

    for(spawnIndex=0; spawnIndex < spawnCount; ++spawnIndex)
    {
        loc = pClient->GetShipSE()->GetPosition();

        if (offsetLocationSet)
        {
            // An X, Y, Z coordinate offset was specified along with the command, so use this to calculate
            // the final cooridnate of the newly spawned item:
            loc.x += offsetLocation.x;
            loc.y += offsetLocation.y;
            loc.z += offsetLocation.z;
        }
        else
        {
            // Calculate a random coordinate on the sphere centered on the player's position with
            // a radius equal to the radius of the ship/celestial being spawned times 10 for really good measure of separation:
            radius = (actualRadius * 5.0) * (double)(MakeRandomInt(1, 3));     // Scale the distance from player that the object will spawn to between 10x and 15x the object's radius
            loc.MakeRandomPointOnSphere(radius);
        }

        // Spawn the item:
        ItemData idata(
            actualTypeID,
            1, // owner is EVE System
            pClient->GetLocationID(),
            flagNone,
            actualTypeName.c_str(),
            loc
       );

        item = sItemFactory.SpawnItem(idata);
        if (item.get() == nullptr)
            throw CustomError ("Unable to spawn item of type %u.", typeID);

        DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.categoryID = actualCategoryID;
        entity.groupID = actualGroupID;
        entity.itemID = item->itemID();
        entity.itemName = actualTypeName;
        entity.typeID = actualTypeID;
        entity.position = loc;
        // set owner data according to type and location
        SetSpawnOwner(pClient, entity);

        // Actually do the spawn using SystemManager's BuildEntity:
        if (!pClient->SystemMgr()->BuildDynamicEntity(entity)) {
            return new PyString("Spawn Failed: typeID or typeName not supported.");
        }
    }

    sLog.Yellow("Command_spawn", "%s: Spawned %u in space, %u times", pClient->GetName(), typeID, spawnCount);

    return new PyString("Spawn successful.");
}

// command to modify blueprint's attributes, we have to give it blueprint's itemID ...
// isn't much comfortable, but I don't know about better solution ...
PyResult Command_setbpattr(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() < 6)
        throw CustomError ("Correct Usage: /setbpattr [blueprintID] [0 (not copy) or 1 (copy)] [material level] [productivity level] [remaining runs]");
    if (!args.isNumber(1))
        throw CustomError ("Argument 1 must be blueprint ID. (got %s)", args.arg(1).c_str());
    if ("0" != args.arg(2) && "1" != args.arg(2))
        throw CustomError ("Argument 2 must be 0 (original) or 1 (copy). (got %s)", args.arg(2).c_str());
    if (!args.isNumber(3))
        throw CustomError ("Argument 3 must be material level. (got %s)", args.arg(3).c_str());
    if (!args.isNumber(4))
        throw CustomError ("Argument 4 must be productivity level. (got %s)", args.arg(4).c_str());
    if (!args.isNumber(5))
        throw CustomError ("Argument 5 must be remaining licensed production runs. (got %s)", args.arg(5).c_str());

    int blueprintID = atoi(args.arg(1).c_str());
    BlueprintRef bp = sItemFactory.GetBlueprintRef(blueprintID);
    if (!bp)
        throw CustomError ("Failed to load blueprint %u.", blueprintID);

    // these need to check current settings to see if anything changed
    bp->SetCopy((atoi(args.arg(2).c_str()) ? true : false));
    bp->SetMLevel(atoi(args.arg(3).c_str()));
    bp->SetPLevel(atoi(args.arg(4).c_str()));
    bp->SetRuns(atoi(args.arg(5).c_str()));

    return new PyString("Properties modified.");
}

namespace {
    std::string FormatAttrValue(EvilNumber value) {
        char buf[64];
        if (value.isInt()) {
            snprintf(buf, sizeof(buf), "%lli", value.get_int());
        } else {
            snprintf(buf, sizeof(buf), "%g", value.get_double());
        }
        return buf;
    }

    // a number for an attribute.  whole numbers stay integers
    bool ParseAttrValue(const std::string& text, EvilNumber& value) {
        if (text.empty())
            return false;
        char* end(nullptr);
        double d(strtod(text.c_str(), &end));
        if ((end == text.c_str()) or (*end != '\0'))
            return false;
        if ((text.find_first_of(".eE") == std::string::npos) and (d >= -9.2e18) and (d <= 9.2e18)) {
            value = EvilNumber(static_cast<int64>(strtoll(text.c_str(), nullptr, 10)));
        } else {
            value = EvilNumber(d);
        }
        return true;
    }

    /* sets a live attribute value, tells the owner's client, and refreshes anything that caches it.
     * this is transient, like CCP's dogma tool:  the next full effects rebuild (undock, board, refit, relog) reloads the
     *  attribute from the type and the db.  returns a note for the reply
     */
    std::string ApplyGMAttribute(Client* pClient, InventoryItemRef iRef, uint16 attrID, EvilNumber value)
    {
        iRef->SetAttribute(attrID, value, true);

        std::string note;
        uint8 catID(iRef->categoryID());
        if ((catID == EVEDB::invCategories::Ship) or (catID == EVEDB::invCategories::Entity)
        or  (catID == EVEDB::invCategories::Drone)) {
            // the ball caches mass, agility, speed and warp data
            SystemEntity* pSE(nullptr);
            Client* pPilot(sEntityList.FindClientByCharID(iRef->ownerID()));
            if ((pPilot != nullptr) and (pPilot->GetShipID() == iRef->itemID()) and pPilot->IsInSpace())
                pSE = pPilot->GetShipSE();
            if ((pSE == nullptr) and (pClient->SystemMgr() != nullptr))
                pSE = pClient->SystemMgr()->GetSE(iRef->itemID());
            if ((pSE != nullptr) and (pSE->DestinyMgr() != nullptr)
            and (pSE->IsShipSE() or pSE->IsNPCSE() or pSE->IsDroneSE())) {
                pSE->DestinyMgr()->UpdateShipVariables();
                if (attrID == AttrMaxVelocity)
                    pSE->DestinyMgr()->SetMaxVelocity(value.get_float());
                note = "  Movement updated.";
            }
        } else if ((catID == EVEDB::invCategories::Module) or (catID == EVEDB::invCategories::Subsystem)
               or  (catID == EVEDB::invCategories::Charge)) {
            // a fitted module re-applies its pilot's skill bonuses.  a loaded charge is found by its slot
            Client* pPilot(sEntityList.FindClientByCharID(iRef->ownerID()));
            if ((pPilot != nullptr) and (pPilot->GetShipID() == iRef->locationID()) and (pPilot->GetShip().get() != nullptr)) {
                ModuleManager* pModMgr(pPilot->GetShip()->GetModuleManager());
                GenericModule* pMod(nullptr);
                if (pModMgr != nullptr)
                    pMod = (catID == EVEDB::invCategories::Charge ? pModMgr->GetModule(iRef->flag()) : pModMgr->GetModule(iRef->itemID()));
                if (pMod != nullptr) {
                    pMod->Update();
                    note = "  Module updated (values its cycle caches may need a refit).";
                }
            }
        }
        return note + "  Resets on refit/undock.";
    }

    // the reply of /setattr and /dogma
    std::string SetAttrReply(Client* pClient, CommandDB* db, InventoryItemRef iRef, uint16 attrID, EvilNumber value)
    {
        std::string old(iRef->HasAttribute(attrID) ? FormatAttrValue(iRef->GetAttribute(attrID)) : std::string("not set"));
        std::string note(ApplyGMAttribute(pClient, iRef, attrID, value));
        char buf[384];
        snprintf(buf, sizeof(buf), "%s of %s (%u) set to %s (was %s).%s", db->GetAttributeName(attrID).c_str(), iRef->name(),
                 iRef->itemID(), FormatAttrValue(value).c_str(), old.c_str(), note.c_str());
        sLog.Yellow("Command", "%s: %s", pClient->GetName(), buf);
        return buf;
    }
}

PyResult Command_getattr(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() < 3)
        throw CustomError ("Correct Usage: /getattr (itemID|me|char) (attributeID|attributeName)");

    InventoryItemRef iRef(cmd_find_item(who, args.arg(1)));
    if (iRef.get() == nullptr)
        throw CustomError ("Unable to find item %s", args.arg(1).c_str());
    uint16 attrID(cmd_find_attribute(db, args.arg(2)));
    if (attrID == 0)
        throw CustomError ("Unknown attribute %s", args.arg(2).c_str());

    std::string attrName(db->GetAttributeName(attrID));
    std::string def(FormatAttrValue(iRef->GetDefaultAttribute(attrID)));
    char buf[256];
    if (!iRef->HasAttribute(attrID)) {
        snprintf(buf, sizeof(buf), "%s(%u) of %s (%u): the item doesn't have it.  type default %s", attrName.c_str(), attrID,
                 iRef->name(), iRef->itemID(), def.c_str());
    } else {
        snprintf(buf, sizeof(buf), "%s(%u) of %s (%u): current %s, type default %s", attrName.c_str(), attrID,
                 iRef->name(), iRef->itemID(), FormatAttrValue(iRef->GetAttribute(attrID)).c_str(), def.c_str());
    }
    return new PyString(buf);
}

PyResult Command_setattr(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() < 4)
        throw CustomError ("Correct Usage: /setattr (itemID|me|char) (attributeID|attributeName) (value)");

    InventoryItemRef iRef(cmd_find_item(who, args.arg(1)));
    if (iRef.get() == nullptr)
        throw CustomError ("Unable to find item %s", args.arg(1).c_str());
    uint16 attrID(cmd_find_attribute(db, args.arg(2)));
    if (attrID == 0)
        throw CustomError ("Unknown attribute %s", args.arg(2).c_str());
    EvilNumber value;
    if (!ParseAttrValue(args.arg(3), value))
        throw CustomError ("The value must be a number (got %s)", args.arg(3).c_str());

    return new PyString(SetAttrReply(who, db, iRef, attrID, value));
}

PyResult Command_fit(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if ((args.argCount() < 3) or !args.isNumber(2))
        throw CustomError ("Correct Usage: /fit [me|charID] [typeID]");

    Client* pTarget(who);
    if (args.arg(1) != "me") {
        if (!args.isNumber(1))
            throw CustomError ("Argument 1 should be 'me' or a character ID");
        pTarget = sEntityList.FindClientByCharID(atoi(args.arg(1).c_str()));
        if (pTarget == nullptr)
            throw CustomError ("Character %s is not online", args.arg(1).c_str());
    }
    if (pTarget->InPod())
        throw CustomError ("You can't fit modules to a capsule");

    uint32 typeID(atoi(args.arg(2).c_str()));
    const ItemType* pType(sItemFactory.GetType(typeID));
    if ((pType == nullptr) or ((pType->categoryID() != EVEDB::invCategories::Module) and (pType->categoryID() != EVEDB::invCategories::Subsystem)))
        throw CustomError ("%u is not a module", typeID);

    ShipItemRef sRef(pTarget->GetShip());
    ItemData idata(typeID, pTarget->GetCharacterID(), locTemp, flagNone, 1);
    InventoryItemRef iRef(sItemFactory.SpawnItem(idata));
    if (iRef.get() == nullptr)
        throw CustomError ("Unable to create item of type %u", typeID);

    EVEItemFlags flag(sRef->FindAvailableModuleSlot(iRef));
    if (flag == flagIllegal) {
        iRef->Delete();
        throw CustomError ("No free slot for the %s", pType->name().c_str());
    }
    try {
        sRef->VerifyHoldType(flag, iRef, pTarget);
        sRef->TryModuleLimitChecks(flag, iRef);
    } catch (...) {
        iRef->Delete();
        throw;
    }
    if (sRef->AddItemByFlag(flag, iRef, pTarget) < 1) {
        iRef->Delete();
        throw CustomError ("Could not fit the %s", pType->name().c_str());
    }
    return new PyString("Fitted.");
}

PyResult Command_giveallskills(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    uint8 level = 5;            // Ensure that ALL skills are trained to level 5
    CharacterRef character;
    uint32 ownerID = 0;
    Client * pTarget = NULL;

    if (args.argCount() >= 2) {
        // me, a charID or a (quoted) name.  online characters only
        pTarget = cmd_find_client(args, 1, who);
        if (pTarget == nullptr)
            throw CustomError ("Character %s is not online", args.arg(1).c_str());
        ownerID = pTarget->GetCharacterID();
        character = pTarget->GetChar();
    } else
        throw CustomError ("Correct Usage: /giveallskills [me|charID|\"Character Name\"]");

    // Make sure character reference is not NULL before trying to use it:
    if (character.get()) {
        CommandDB::charSkillStates skillList;
        // Query Database to get the list of those skills, not trained to 5 (either not injected yet or having level up to 4)
        CommandDB::NotFullyLearnedSkillList(skillList, ownerID);

        SkillRef skill;
        uint16 skillID;

        for (auto cur: skillList) {
            skillID = cur.first;
            if (cur.second >= 0) {  // the same check as character->HasSkill(skillID)
                skill = character->GetCharSkillRef(skillID);
                skill->SetAttribute(AttrSkillLevel, level);
                skill->SetAttribute(AttrSkillPoints, skill->GetSPForLevel(level));
                character->RemoveFromQueue(skill);
            } else {
                std::ostringstream str;
                str << "Skill Gift by ";
                str << who->GetCharName();
                ItemData idata(skillID, ownerID, pTarget->GetLocationID(), flagSkill, 1, str.str().c_str());
                skill = sItemFactory.SpawnSkill(idata);

                if (skill.get() == nullptr)
                    throw CustomError ("Unable to create item of type %u.", skillID);

                skill->SetAttribute(AttrSkillLevel, level);
                skill->SetAttribute(AttrSkillPoints, skill->GetSPForLevel(level));
                // this will inform the client that the skill got injected into their brains!
                skill->ChangeSingleton(true);
                skill->Move(ownerID, flagSkill, true);
            }

            OnAdminSkillChange oasc;
            oasc.skillItemID = skill->itemID();
            oasc.skillTypeID = skillID;
            oasc.newSP = skill->GetSPForLevel(level);
            PyTuple* tmp = oasc.Encode();
            pTarget->QueueDestinyEvent(&tmp);

            //  save gm skill gift in history  -allan
            //  maybe not for this....WAAAAYYY  to much DB traffic for this.
            character->SaveSkillHistory(EvESkill::Event::GMGift, Win32TimeNow(), ownerID, skillID, level, \
            skill->GetAttribute(AttrSkillPoints).get_double());
        }
        // END LOOP
        // pTarget->SendErrorMsg("You need to relog for skills to get saved and show in character sheet.");
        return new PyString ("Gifting skills complete");
    }

    return new PyString ("Skill Gifting Failure");
}

PyResult Command_giveskills(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    //pass to command_giveskill
    Command_giveskill(who, db, services, args);
    return nullptr;
}

PyResult Command_giveskill(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    uint32 ownerID = 0, skillID = 0, level = 0;
    uint32 newPoints = 0;
    CharacterRef character;
    Client *pTarget(nullptr);

    if (args.argCount() == 4) {
        // me, a charID or a (quoted) name.  online characters only
        pTarget = cmd_find_client(args, 1, who);
        if (pTarget == nullptr)
            throw CustomError ("Character %s is not online", args.arg(1).c_str());
        ownerID = pTarget->GetCharacterID();
        character = pTarget->GetChar();

        if (!args.isNumber(2))
            throw CustomError ("Argument 2 must be type ID.");
        skillID = atoi(args.arg(2).c_str());

        if (!args.isNumber(3))
            throw CustomError ("Argument 3 must be level");
        level = atoi(args.arg(3).c_str());

        if (level > EvESkill::MAXSKILLLEVEL)
            level = EvESkill::MAXSKILLLEVEL;
    } else
        throw CustomError ("Correct Usage: /giveskill [me|charID|\"Character Name\"] [skillID] [level]");

    // Make sure references are not NULL before trying to use them
    if ((pTarget == nullptr) or (character.get() == nullptr))
        throw CustomError ("ERROR: Unable to validate character object.");

    SkillRef skill;
    if (character->HasSkillTrainedToLevel(skillID, level)) {
        // already trained to requested level
        return PyStatic.NewNone();
    } else if (character->HasSkill(skillID)) {
        // has skill injected, so update level
        skill = character->GetCharSkillRef(skillID);
        if (skill.get() == nullptr){
            throw CustomError ("Unable to get item for skillID %u.", skillID);
            return new PyString ("Skill Gifting Failure - Unable to get item for skillID %u.", skillID);
        }
        newPoints = skill->GetSPForLevel(level);
        skill->SetAttribute(AttrSkillLevel, level);
        skill->SetAttribute(AttrSkillPoints, newPoints);
        //check for and remove this skill from training queue
        character->RemoveFromQueue(skill);
    } else {
        // Character does not have this skill
        std::ostringstream str;
        str << "Skill Gift by ";
        str << who->GetCharName();
        ItemData idata(skillID, ownerID, pTarget->GetLocationID(), flagSkill, 1, str.str().c_str());
        skill = sItemFactory.SpawnSkill(idata);
        if (skill.get() == nullptr)
            throw CustomError ("Unable to create item for skillID %u.", skillID);

        newPoints = skill->GetSPForLevel(level);
        skill->SetAttribute(AttrSkillLevel, level);
        skill->SetAttribute(AttrSkillPoints, newPoints);
        skill->ChangeSingleton(true);
        skill->Move(pTarget->GetCharacterID(), flagSkill, true);
    }

    skill->SaveItem();
    //  save gm skill gift in history  -allan
    character->SaveSkillHistory(EvESkill::Event::GMGift, GetFileTimeNow(), ownerID, skillID, level, newPoints);

    OnAdminSkillChange oasc;
        oasc.skillItemID = skill->itemID();
        oasc.skillTypeID = skillID;
        oasc.newSP = newPoints;
    PyTuple* tmp = oasc.Encode();
    pTarget->QueueDestinyEvent(&tmp);

    _log(SKILL__MESSAGE, "GM::GiveSkill - %s(%u) set to level %u with %u SP.", skill->name(), skillID, level, newPoints);
    return PyStatic.NewNone();
}

PyResult Command_online(Client *who, CommandDB *db, EVEServiceManager& services, const Seperator &args) {
    // the client's "/Online modules" sends the shipID
    if (args.argCount() != 2)
        throw CustomError ("Correct Usage: /online [me|charID|shipID]");

    ShipItemRef sRef;
    if (args.arg(1) == "me") {
        if (who->InPod())
            throw CustomError ("Command failed: You can't activate modules while in a pod");
        sRef = who->GetShip();
    } else if (args.isNumber(1)) {
        uint32 id(atoi(args.arg(1).c_str()));
        if (IsCharacterID(id)) {
            Client* pTarget(sEntityList.FindClientByCharID(id));
            if (pTarget == nullptr)
                throw CustomError ("Unable to find character %u", id);
            if (pTarget->InPod())
                throw CustomError ("Command failed: You can't activate modules while in a pod");
            sRef = pTarget->GetShip();
        } else {
            sRef = sItemFactory.GetShipRef(id);
        }
    } else {
        throw CustomError ("Argument 1 should be me, a character ID or a ship ID");
    }
    if (sRef.get() == nullptr)
        throw CustomError ("Unable to find that ship");
    sRef->OnlineAll();
    return(new PyString("All modules have been put Online"));
}

PyResult Command_unload(Client *who, CommandDB *db, EVEServiceManager& services, const Seperator &args) {
// this means 'remove'

    if (args.argCount() >= 2 && args.argCount() <= 3)
    {
        uint32 itemID=0,entityID=0;

        if (strcmp("me", args.arg(1).c_str())!=0)
            if (!args.isNumber(1))
            {
                throw CustomError ("Argument 1 should be an entity ID or me (me=self)");
            }
            entityID = atoi(args.arg(1).c_str());

        if (args.argCount() ==3)
        {
            if (strcmp("all", args.arg(2).c_str())!=0)
                if (!args.isNumber(2))
                    throw CustomError ("Argument 2 should be an item ID or all");
                itemID = atoi(args.arg(2).c_str());
        }

        //select character
        Client* tgt(nullptr);
        if (strcmp("me", args.arg(1).c_str())==0)
            tgt = who;
        else
        {
            tgt = sEntityList.FindClientByCharID( entityID );

            if (!tgt)
                throw CustomError ("Unable to find character %u", entityID );
        }

        /// This doesn't seem like a valid requirement
        //if (tgt->IsInSpace())
        //    throw CustomError ("Character needs to be docked.");

        if (args.argCount() == 3 && strcmp("all", args.arg(2).c_str())!=0) {
            // the client's "GM: /unload me this type" sends a typeID rather than an itemID
            std::vector<InventoryItemRef> modules;
            tgt->GetShip()->GetModuleManager()->GetModuleListOfRefsAsc(modules);
            bool found(false);
            for (auto cur : modules)
                if (cur->itemID() == itemID)
                    found = true;
            if (found) {
                tgt->GetShip()->GetModuleManager()->UnfitModule( itemID );
            } else {
                for (auto cur : modules)
                    if (cur->typeID() == itemID)
                        tgt->GetShip()->GetModuleManager()->UnfitModule( cur->itemID() );
            }
        }

        if (args.argCount() == 3 && strcmp("all", args.arg(2).c_str())==0)
            tgt->GetShip()->UnloadAllModules();

        return(new PyString("All Modules have been unloaded"));
    }
    else
        throw CustomError ("Command failed: You got the arguments all wrong.");
}

PyResult Command_repairmodules(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount()==1)
        who->GetShip()->RepairModules();
    if (args.argCount()==2) {
        if (!args.isNumber(1))
            throw CustomError ("Argument 1 should be a character ID");
        uint32 charID = atoi(args.arg(1).c_str());

        Client *target = sEntityList.FindClientByCharID(charID);
        if (target == NULL)
            throw CustomError ("Cannot find CharacterID %u", charID);
        target->GetShip()->RepairModules();
    }

    return(new PyString("Modules repaired successful."));
}

PyResult Command_dogma(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    //"dogma" "140019878" "agility" "=" "0.2"
    // the client's "Dogma / Edit attribute" and gmtools' AttributeInspector send this shape

    if (!(args.argCount() == 5)) {
        throw CustomError ("Correct Usage: /dogma [itemID|me] [attributeName|attributeID] = [value]");
    }

    if (args.arg(3) != "=") {
        throw CustomError ("/dogma You didn't use an '=' in between your attribute name and value.");
    }
    EvilNumber value;
    if (!ParseAttrValue(args.arg(4), value)) {
        throw CustomError ("/dogma The last argument must be a number");
    }

    InventoryItemRef iRef(cmd_find_item(who, args.arg(1)));
    if (iRef.get() == nullptr)
        throw CustomError ("/dogma Unable to find item %s", args.arg(1).c_str());
    uint16 attrID(cmd_find_attribute(db, args.arg(2)));
    if (attrID == 0)
        throw CustomError ("/dogma Unknown attribute %s", args.arg(2).c_str());

    return new PyString(SetAttrReply(who, db, iRef, attrID, value));
}

PyResult Command_kick(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    Client *target;
    if (args.argCount() == 2) {
        if (args.isNumber(1)) {
            int id = atoi(args.arg(1).c_str());
            target = sEntityList.FindClientByCharID(id);
        } else {
            const char *name = args.arg(1).c_str();
            target = sEntityList.FindClientByName(name);
        }
    }
    //support for characters with first and last names
    else if (args.argCount() == 3) {
        if (args.isHexNumber(1))
            throw CustomError ("Unknown arguments");

        std::string name = args.arg(1) + " " + args.arg(2);
        target = sEntityList.FindClientByName(name.c_str()) ;
    } else {
        throw CustomError ("Correct Usage: /kick [Character Name]");
    }

    if (target == NULL) {
        throw CustomError ("Cannot find Character");
    } else {
        target->DisconnectClient();
    }

    return nullptr;
}

PyResult Command_ban(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    std::string name;
    Client* target(nullptr);
    if (args.argCount() == 2) {
        if (args.isNumber(1)) {
            target = sEntityList.FindClientByCharID(atoi(args.arg(1).c_str()));
            if (target == nullptr)
                throw CustomError ("Character %s is not online.  Use their name to ban them while offline", args.arg(1).c_str());
        } else {
            name = args.arg(1);
        }
    } else if (args.argCount() == 3) {
        //support for characters with first and last names
        name = args.arg(1) + " " + args.arg(2);
    } else {
        throw CustomError ("Correct Usage: /ban [charID|Character Name]");
    }

    if (target == nullptr)
        target = sEntityList.FindClientByName(name.c_str());
    if (target != nullptr) {
        target->BanClient();
        target->DisconnectClient();
        return new PyString("Banned.");
    }

    // offline
    uint32 accountID(db->GetAccountID(name));
    if (accountID == 0)
        throw CustomError ("Cannot find character %s", name.c_str());
    ServiceDB::SetAccountBanStatus(accountID, true);
    return new PyString("Banned.");
}

PyResult Command_unban(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    uint32 accountID(0);
    std::string name;
    if (args.argCount() == 2) {
        name = args.arg(1);
        if (args.isNumber(1)) {
            accountID = db->GetAccountIDByChar(atoi(args.arg(1).c_str()));
        } else {
            accountID = db->GetAccountID(name);
        }
    }
    //support for characters with first and last names
    else if (args.argCount() == 3) {
        if (args.isHexNumber(1))
            throw CustomError ("Unknown arguments");

        name = args.arg(1) + " " + args.arg(2);
        accountID = db->GetAccountID(name);
    } else {
        throw CustomError ("Correct Usage: /unban [Character Name / Character ID]");
    }

    if (accountID == 0)
        throw CustomError ("Cannot find character %s", name.c_str());
    ServiceDB::SetAccountBanStatus(accountID, false);
    return new PyString("Unbanned.");
}

PyResult Command_whois(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    // /whois (charID|"name")  online or not
    if (args.argCount() < 2)
        throw CustomError ("Correct Usage: /whois (charID|\"name\")");
    uint32 charID(cmd_find_char_id(db, args, 1, who));
    CommandDB::WhoisData data = CommandDB::WhoisData();
    if ((charID == 0) or !db->GetWhoisData(charID, data))
        throw CustomError ("Cannot find character %s", args.arg(1).c_str());

    Client* pTarget(sEntityList.FindClientByCharID(charID));
    std::ostringstream str;
    char buf[64];
    str << "<b>" << data.name << "</b> (" << data.charID << ")<br>";
    snprintf(buf, sizeof(buf), "%llX", data.role);
    str << "Account: " << data.accountName << " (" << data.accountID << "), role 0x" << buf
        << (data.banned ? ", <color=red>banned</color>" : "") << "<br>";
    if (pTarget != nullptr) {
        str << "Online: ";
        if (pTarget->IsDocked()) {
            str << "docked in " << stDataMgr.GetStationName(pTarget->GetLocationID());
        } else {
            str << "in space in " << sDataMgr.GetSystemName(pTarget->GetSystemID());
        }
        if (pTarget->GetShip().get() != nullptr) {
            const ItemType* pType(sItemFactory.GetType(pTarget->GetShip()->typeID()));
            str << ", flying " << pTarget->GetShip()->itemName() << " (" << (pType != nullptr ? pType->name() : "?") << ")";
        }
        str << "<br>";
    } else {
        str << (data.online ? "Online (by the db, not connected here)" : "Offline") << ", last in ";
        if (data.stationID != 0) {
            str << data.stationName;
        } else {
            str << data.systemName;
        }
        str << "<br>";
    }
    str << "Corporation: " << data.corpName << " (" << data.corpID << ")";
    if (data.allianceID != 0)
        str << ", alliance " << data.allianceName << " (" << data.allianceID << ")";
    str << "<br>";
    snprintf(buf, sizeof(buf), "%.2f", data.securityRating);
    str << "Security status: " << buf << "<br>";
    snprintf(buf, sizeof(buf), "%.2f", data.balance);
    str << "Wallet: " << buf << " ISK<br>";
    str << "Skill points: " << data.skillPoints << "<br>";
    str << "Last logon: " << (data.logonDateTime > 0 ? Win32TimeToString(data.logonDateTime) : std::string("never"));
    str << ", last logoff: " << (data.logoffDateTime > 0 ? Win32TimeToString(data.logoffDateTime) : std::string("never")) << "<br>";
    int64 until(0);
    std::string reason;
    if (LSCService::IsGagged(charID, until, reason)) {
        str << "Gagged until " << Win32TimeToString(until) << ": " << reason << "<br>";
    } else {
        str << "Not gagged<br>";
    }

    std::string reply(str.str());
    who->SendInfoModalMsg("%s", reply.c_str());
    return new PyString(reply);
}

PyResult Command_chatban(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    // /chatban (charID|"name") [days|perm] ["reason"]  a gag in every channel that works for offline characters too
    const char* usage("Correct Usage: /chatban (charID|\"name\") [days|perm] [\"reason\"]");
    if (args.argCount() < 2)
        throw CustomError (usage);
    uint32 charID(cmd_find_char_id(db, args, 1, who));
    if (charID == 0)
        throw CustomError ("Cannot find character %s", args.arg(1).c_str());

    int64 days(0);  // 0 = permanent
    std::string reason("Banned from chat by a GM");
    size_t next(2);
    if (args.argCount() > next) {
        if (args.isNumber(next)) {
            days = atoll(args.arg(next).c_str());
            if (days < 1)
                throw CustomError (usage);
            ++next;
        } else if (strcasecmp(args.arg(next).c_str(), "perm") == 0) {
            ++next;
        }
    }
    if (args.argCount() > next)
        reason = args.arg(next);

    int64 until(GetFileTimeNow() + (days > 0 ? days : 100 * 365) * Win32Time_Day);
    LSCService::Gag(charID, until, reason, who->GetCharacterID());

    std::string length(days > 0 ? std::to_string(days) + " days" : std::string("good"));
    Client* pTarget(sEntityList.FindClientByCharID(charID));
    if (pTarget != nullptr)
        pTarget->SendNotifyMsg("You have been banned from chat for %s: %s", length.c_str(), reason.c_str());
    sLog.Yellow("Command", "%s: chat-banned %u for %s: %s", who->GetName(), charID, length.c_str(), reason.c_str());
    return new PyString("Chat-banned " + std::to_string(charID) + " for " + length + ".  /ungag lifts it.");
}

PyResult Command_givelp(Client* who, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() == 4) {
        uint32 characterID;
        if (strcmp("me", args.arg(1).c_str())==0) {
            characterID = who->GetCharID();
        } else if (args.isNumber(1)) {
            characterID = stoi(args.arg(1));
            if (sEntityList.FindClientByCharID(characterID) == nullptr){ 
                throw CustomError ("Provided characterID is invalid or character is offline");
            }
        } else {
            throw CustomError ("Argument 1 should be a characterID or 'me'");
        }
        uint32 corporationID;
        std::string corpName;
        if (args.isNumber(2)) {
            corporationID = stoi(args.arg(2));
            corpName = CorporationDB::GetCorpName(corporationID);
            if (strcmp("Unknown", corpName.c_str())==0) {
                throw CustomError ("Invalid corporationID provided");
            }
        } else {
            throw CustomError ("Argument 2 should be a corporationID");
        }
        int amount;
        if (args.isNumber(3)) {
            amount = stoi(args.arg(3));
            if (amount <= 0) {
                throw CustomError ("Argument 3 should be a positive number");
            }
        } else {
            throw CustomError ("Argument 3 should be a valid number");
        }
        LPService::AddLP(characterID, corporationID, amount);
        std::string message("Added " + std::to_string(amount) + " LP to char " + std::to_string(characterID)
                            + " for corp " + corpName + ".");
        return(new PyString(message));
    } else {
        throw CustomError ("Correct Usage: /givelp ['me'|<characterID>] [corporationID] [amount]");
    }
    return nullptr;
}
