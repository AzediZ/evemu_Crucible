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
    Author:     Zhur
    Updates:    Allan
*/

/** @todo  most of this needs to go into static data */

#include "eve-server.h"

#include "Client.h"


/* this is only called by Inventory::LoadContents()
 * it is optimized for specific calling objects, to avoid multiple db hits while loading,
 * and to load only things needed for this object at the time of the call.
 */
bool InventoryDB::GetItemContents(OwnerData &od, std::vector<uint32> &into) {
    std::stringstream query;
    query << "SELECT itemID FROM entity WHERE locationID = ";
    query << od.locID;

    if (sDataMgr.IsSolarSystem(od.locID)) {
        query << " AND ownerID = " << od.ownerID;
    } else if (sDataMgr.IsStation(od.locID)) {
        if (od.ownerID == 1) {
            /* this will get agents in station */
            query << " AND ownerID < " << maxNPCItem;
            query << " AND itemID < " << maxNPCItem;
        } else {
            if (IsPlayerCorp(od.corpID)) {
                // check for items owned by corp in players cargo/hangar
                query << " AND ((ownerID = " << od.ownerID << ") OR (ownerID = " << od.corpID << "))";
            } else {
                query << " AND ownerID = " << od.ownerID;
            }
        }
    } else if (IsCharacterID(od.locID)) {
        if (od.ownerID == 1) {
            // not sure what to do here....
        } else if (IsPlayerCorp(od.corpID)) {
            // check for items owned by corp in players cargo/hangar...this *should* work for cap ships' cargo, also
           query << " AND ((ownerID = " << od.ownerID << ") OR (ownerID = " << od.corpID << "))";
        } else {
            query << " AND ownerID = " << od.ownerID;
        }
    } else if (IsOfficeID(od.locID)) {
        // may not need this, as location is officeID, but items MAY be owned by players in corp hangar.
        //query << " AND ownerID = " << od.ownerID;
    }

    query << " ORDER BY itemID";

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,query.str().c_str() )) {
        codelog(DATABASE__ERROR, "Error in GetItemContents query for locationID %u: %s", od.locID, res.error.c_str());
        return false;
    }

    _log(DATABASE__RESULTS, "GetItemContents: '%s' returned %lu items", query.str().c_str(), res.GetRowCount());
    DBResultRow row;
    while( res.GetRow( row ) )
        into.push_back( row.GetUInt( 0 ) );

    return true;
}

/*  not used? */
bool InventoryDB::GetItemContents(uint32 itemID, EVEItemFlags flag, std::vector<uint32> &into)
{
    DBQueryResult res;

    if ( !sDatabase.RunQuery( res,
        "SELECT "
        "  itemID"
        " FROM entity "
        " WHERE locationID=%u"
        "  AND flag=%d",
        itemID, (int)flag ) )
    {
        codelog(DATABASE__ERROR, "Error in GetItemContents query for item %u: %s", itemID, res.error.c_str());
        return false;
    }

    _log(DATABASE__RESULTS, "GetItemContents for item %u returned %lu items", itemID, res.GetRowCount());
    DBResultRow row;
    while( res.GetRow( row ) )
        into.push_back( row.GetUInt( 0 ) );

    return true;
}

/*  not used? */
bool InventoryDB::GetItemContents(uint32 itemID, EVEItemFlags flag, uint32 ownerID, std::vector<uint32> &into)
{
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT "
        "  itemID"
        " FROM entity "
        " WHERE locationID=%u"
        "  AND flag=%d"
        "  AND ownerID=%u",
        itemID, (int)flag, ownerID))
    {
        codelog(DATABASE__ERROR, "Error in GetItemContents query for item %u with flag %i: %s", itemID, (int)flag, res.error.c_str());
        return false;
    }

    _log(DATABASE__RESULTS, "GetItemContents for item %u with flag %i returned %lu items", itemID, (int)flag, res.GetRowCount());
    DBResultRow row;
    while(res.GetRow(row))
        into.push_back(row.GetUInt(0));

    return true;
}

void InventoryDB::DeleteTrackingCans()
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM entity WHERE customInfo LIKE '%Position Test%'");  // 90.63s on main, 0.037s on dev
    //sDatabase.RunQuery(err, "DELETE FROM entity WHERE itemName LIKE '%Bubble%'");         // 66.75s on main, 0.036s on dev
}

void InventoryDB::AddTrashLog(uint32 itemID, uint32 ownerID, uint32 locationID, uint16 flag, uint32 byID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "REPLACE INTO invTrashLog (itemID, ownerID, locationID, flag, trashedBy, trashTime)"
        " VALUES (%u, %u, %u, %u, %u, %lli)", itemID, ownerID, locationID, flag, byID, GetFileTimeNow()))
        codelog(DATABASE__ERROR, "Error in AddTrashLog query for item %u: %s", itemID, err.c_str());
}

bool InventoryDB::GetTrashLog(uint32 itemID, uint32& ownerID, uint32& locationID, uint16& flag)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT ownerID, locationID, flag FROM invTrashLog WHERE itemID = %u", itemID)) {
        codelog(DATABASE__ERROR, "Error in GetTrashLog query for item %u: %s", itemID, res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    ownerID = row.GetUInt(0);
    locationID = row.GetUInt(1);
    flag = row.GetUInt(2);
    return true;
}

void InventoryDB::DeleteTrashLog(uint32 itemID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM invTrashLog WHERE itemID = %u", itemID);
}

void InventoryDB::ListTrash(uint32 ownerID, DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT t.itemID, e.typeID, e.quantity, t.trashTime, t.locationID"
        " FROM invTrashLog AS t"
        "  JOIN entity AS e ON e.itemID = t.itemID"
        " WHERE t.ownerID = %u"
        " ORDER BY t.trashTime DESC LIMIT 50", ownerID))
        codelog(DATABASE__ERROR, "Error in ListTrash query for owner %u: %s", ownerID, res.error.c_str());
}

void InventoryDB::PurgeTrash(int64 cutoff)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID FROM invTrashLog WHERE trashTime < %lli", cutoff)) {
        codelog(DATABASE__ERROR, "Error in PurgeTrash query: %s", res.error.c_str());
        return;
    }

    std::vector<uint32> roots;
    DBResultRow row;
    while (res.GetRow(row))
        roots.push_back(row.GetUInt(0));

    if (!roots.empty()) {
        DeleteItemsRecursive(roots);
        sLog.Cyan("        DBCleaner", "%lu trashed items purged.", roots.size());
    }

    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM invTrashLog WHERE trashTime < %lli", cutoff);
}

void InventoryDB::DeleteItemsRecursive(std::vector<uint32>& itemIDs)
{
    // the items, and breadth first everything inside them
    std::set<uint32> all;
    std::vector<uint32> level(itemIDs);
    DBQueryResult res;
    DBResultRow row;
    while (!level.empty()) {
        std::vector<uint32> next;
        for (size_t i = 0; i < level.size(); i += 500) {
            std::string ids;
            for (size_t j = i; (j < level.size()) and (j < i + 500); ++j) {
                if (!ids.empty())
                    ids += ",";
                ids += std::to_string(level[j]);
                all.insert(level[j]);
            }
            if (!sDatabase.RunQuery(res, "SELECT itemID FROM entity WHERE locationID IN (%s)", ids.c_str())) {
                codelog(DATABASE__ERROR, "Error in DeleteItemsRecursive query: %s", res.error.c_str());
                continue;
            }
            while (res.GetRow(row))
                if (all.find(row.GetUInt(0)) == all.end())
                    next.push_back(row.GetUInt(0));
        }
        level.swap(next);
    }

    std::vector<uint32> vec(all.begin(), all.end());
    DBerror err;
    for (size_t i = 0; i < vec.size(); i += 500) {
        std::string ids;
        for (size_t j = i; (j < vec.size()) and (j < i + 500); ++j) {
            if (!ids.empty())
                ids += ",";
            ids += std::to_string(vec[j]);
        }
        sDatabase.RunQuery(err, "DELETE FROM entity_attributes WHERE itemID IN (%s)", ids.c_str());
        sDatabase.RunQuery(err, "DELETE FROM invBlueprints WHERE itemID IN (%s)", ids.c_str());
        sDatabase.RunQuery(err, "DELETE FROM invContainerSecurity WHERE itemID IN (%s)", ids.c_str());
        sDatabase.RunQuery(err, "DELETE FROM invAuditLog WHERE containerID IN (%s)", ids.c_str());
        sDatabase.RunQuery(err, "DELETE FROM crpLockedItems WHERE itemID IN (%s)", ids.c_str());
        sDatabase.RunQuery(err, "DELETE FROM shipInsurance WHERE shipID IN (%s)", ids.c_str());
        sDatabase.RunQuery(err, "DELETE FROM entity WHERE itemID IN (%s)", ids.c_str());
    }
}
