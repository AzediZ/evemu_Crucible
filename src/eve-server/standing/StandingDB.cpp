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
    Rewrite:        Allan
*/

#include "Client.h"
#include "StaticDataMgr.h"
#include "standing/StandingDB.h"

/*  re-write of standing system  -allan 10Apr15
 *  DB tables have identical field names for ease of implementation
 * Get Fields
 *    toID = me|myCorp|myAlliance
 *    fromID = char|agent|corp|faction|alliance
 *    standing =  fraction representing current value
 * Set Fields
 *    toID = char|agent|corp|faction|alliance
 *    fromID = me|myCorp|myAlliance
 *    standing =  fraction representing current value

 DB tables [fields] (notes)
 repFactions    [fromID, toID, standing] (NPC Faction<-->NPC Faction) {populated, hard-coded  -- cant change}
 repStandings   [fromID, toID, standing] (agent-->char, agent-->PC corp  -- changed by missions status')
 repStandings   [fromID, toID, standing] (corporation<-->alliance, alliance<-->alliance  -- changed thru Corp window)
 repStandings   [fromID, toID, standing] (corporation-->character, corporation<-->corporation  -- changed thru Corp window)
 repStandings   [fromID, toID, standing] (character<-->character, character-->corporation  -- changed thru PnP window)
 repStandings   [fromID, toID, standing] (NPC corp-->char, NPC corp-->PC corp  -- changed by missions and faction kills)

 all change logs will go here.  not sure of all fields yet
 repStandingChanges [fromID, toID, eventID, eventTypeID, eventDateTime, modification, originalFromID, originalToID, int_1, int_2, int_3, msg] ()

 http://www.eveinfo.net/wiki/ind~4067.htm
 */

PyObjectEx *StandingDB::GetFactionStandings()
{
    // repFactions table is ONLY faction standings
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT fromID,toID,standing FROM repFactions");
    return DBResultToCRowset(res);
}

PyRep *StandingDB::GetMyStandings(uint32 charID)
{
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT fromID, standing AS rank FROM repStandings WHERE toID = %u", charID);
    return DBResultToCRowset(res);
}

PyRep *StandingDB::GetCharStandings(Client *pClient)
{
    DBQueryResult res;
    // the client shows its standing from CONCORD as the security status, so that row is always the security status
    sDatabase.RunQuery(res, "SELECT fromID, toID, standing FROM repStandings WHERE (toID = %u OR fromID = %u) AND NOT (fromID = %u AND toID = %u)"
                       " UNION ALL SELECT %u, %u, %f",
                       pClient->GetCharacterID(), pClient->GetCharacterID(), corpCONCORD, pClient->GetCharacterID(),
                       corpCONCORD, pClient->GetCharacterID(), pClient->GetSecurityRating());
    return DBResultToCRowset(res);
}

PyRep *StandingDB::GetCorpStandings(Client *pClient)
{
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT fromID, toID, standing FROM repStandings WHERE toID = %u OR fromID = %u", pClient->GetCorporationID(), pClient->GetCorporationID());
    return DBResultToCRowset(res);
}

PyRep *StandingDB::GetCharNPCStandings(uint32 charID)
{
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT fromID, toID, standing FROM chrNPCStandings WHERE toID = %u", charID);
    return DBResultToCRowset(res);
}

/** @todo not sure about this yet.... wip   ....not used? */
PyRep *StandingDB::PrimeCharStandings(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT "
                            " itemID AS ownerID,"
                            " itemName AS ownerName,"
                            " typeID"
                            " FROM entity"
                            " WHERE itemID < 0"))
    {
        _log(DATABASE__ERROR, "Error in PrimeCharStandings query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *StandingDB::GetStandingTransactions(uint32 fromID, uint32 toID, uint8 direction, uint32 eventID, uint32 eventType, int64 eventDateTime)
{
    /* client StandingTransactionsWnd: rows are shown newest first, pages of 25 ("view more" hidden when fewer come back).
     *  "view more" sends the last (oldest) eventID with direction 0, "previous" the first (newest) with direction 1.
     *  a date sends eventDateTime (end of that day for direction 1) and no eventID
     */
    std::ostringstream where;
    where << " WHERE 1";
    if (fromID != 0)
        where << " AND fromID = " << fromID;
    if (toID != 0)
        where << " AND toID = " << toID;
    if (eventType != 0)
        where << " AND eventTypeID = " << eventType;
    if (eventDateTime != 0)
        where << " AND eventDateTime <= " << eventDateTime;
    bool newer((direction == 1) and (eventID != 0));
    if (eventID != 0)
        where << " AND eventID " << (newer ? ">" : "<") << " " << eventID;

    // the newer page is the 25 rows right after eventID, returned newest first like every page
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT * FROM ("
                            " SELECT"
                            "  eventID,"
                            "  eventTypeID,"
                            "  eventDateTime,"
                            "  fromID,"
                            "  toID,"
                            "  modification,"
                            "  originalFromID,"
                            "  originalToID,"
                            "  int_1,"
                            "  int_2,"
                            "  int_3,"
                            "  msg"
                            " FROM repStandingChanges"
                            " %s"
                            " ORDER BY eventID %s LIMIT 25) AS t"
                            " ORDER BY eventID DESC",
                            where.str().c_str(), (newer ? "ASC" : "DESC")))
    {
        codelog(SERVICE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToRowset(res);
}

float StandingDB::GetStanding(uint32 fromID, uint32 toID)
{
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT standing FROM repStandings WHERE fromID=%u AND toID=%u", fromID, toID);
    DBResultRow row;
    if (res.GetRow(row))
    {
        return row.GetFloat(0);
    }
    else
    {
        return 0.0f;
    }
}

void StandingDB::SetStanding(uint32 fromID, uint32 toID, float standing)
{
    DBerror err;
    sDatabase.RunQuery(err,
                       "INSERT INTO repStandings (fromID, toID, standing) VALUES (%u,%u,%f)"
                       " ON DUPLICATE KEY UPDATE standing = VALUES(standing)", fromID, toID, standing);
}

void StandingDB::UpdateStanding(uint32 fromID, uint32 toID, float standing)
{
    DBerror err;
    sDatabase.RunQuery(err,
                       "INSERT INTO repStandings (fromID, toID, standing)"
                       " VALUES (%u,%u,%f)"
                       " ON DUPLICATE KEY UPDATE standing = standing + %f",
                       fromID, toID, standing, standing);
}

/** @todo  implement repStandingChanges after standing system is working  */
void StandingDB::SaveStandingChanges(uint32 fromID, uint32 toID, uint16 eventType, float amount, std::string msg)
{
    /* eventTypeID,eventDateTime,fromID,toID,modification,originalFromID,originalToID,int_1,int_2,int_3,msg */
    std::string eMsg;
    sDatabase.DoEscapeString(eMsg, msg);
    DBerror err;
    sDatabase.RunQuery(err,
                       "INSERT INTO repStandingChanges (eventTypeID, eventDateTime, fromID, toID, modification, msg)"
                       " VALUES (%u, %f, %u, %u, %f, '%s' )",
                       eventType, GetFileTimeNow(), fromID, toID, amount, eMsg.c_str());
}

PyRep *StandingDB::GetStandingCompositions(uint32 fromID, uint32 toID)
{
    // client StandingCompositionsWnd: the row with ownerID == fromID is "Current Value", the others are listed as contributors
    DBQueryResult res;
    if (IsPlayerCorp(toID)) {
        // towards a player corp: fromID's standing to the corp, then to each of its members
        if (!sDatabase.RunQuery(res,
            "SELECT %u AS ownerID, COALESCE((SELECT standing FROM repStandings WHERE fromID = %u AND toID = %u), 0) AS standing"
            " UNION ALL"
            " SELECT r.toID, r.standing FROM repStandings AS r"
            " JOIN chrCharacters AS c ON c.characterID = r.toID"
            " WHERE r.fromID = %u AND c.corporationID = %u",
            fromID, fromID, toID, fromID, toID)) {
            codelog(DATABASE__ERROR, "Error in GetStandingCompositions query: %s", res.error.c_str());
            return PyStatic.NewNone();
        }
    } else if (!sDatabase.RunQuery(res,
            "SELECT %u AS ownerID, COALESCE((SELECT standing FROM repStandings WHERE fromID = %u AND toID = %u), 0) AS standing",
            fromID, fromID, toID)) {
        codelog(DATABASE__ERROR, "Error in GetStandingCompositions query: %s", res.error.c_str());
        return PyStatic.NewNone();
    }
    return DBResultToRowset(res);
}
