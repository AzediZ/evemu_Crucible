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
Author: Zhur, Allan
*/

#include "eve-server.h"


#include "ship/ShipDB.h"
#include "inventory/InventoryItem.h"
#include "inventory/ItemFactory.h"
#include "inventory/ItemType.h"


PyRep *ShipDB::GetInsuranceByShipID(uint32 shipID) {
    DBQueryResult res;
    DBResultRow row;
    sDatabase.RunQuery(res,
        "SELECT startDate, shipName, shipID, endDate, ownerID, fraction, typeID, isCorpItem"
        " FROM shipInsurance"
        " WHERE shipID = %u", shipID );

    if (res.GetRow(row))
        return DBRowToRow(row);

    return PyStatic.NewZero();
}

PyRep *ShipDB::GetInsuranceByOwnerID(uint32 ownerID) {
    // expired contracts stay in the table a while (InsuranceService::ProcessExpiry), the client only wants current ones
    DBQueryResult res;
    sDatabase.RunQuery(res,
        "SELECT startDate, shipName, shipID, endDate, ownerID, fraction, typeID, isCorpItem"
        " FROM shipInsurance"
        " WHERE ownerID = %u AND endDate > %lli", ownerID, GetFileTimeNow() );

    return DBResultToRowset(res);
}

bool ShipDB::InsertInsurance(uint32 shipID, uint16 typeID, const std::string& name, uint32 ownerID, bool isCorpItem, float fraction, double payOut, uint8 numWeeks) {
    std::string escaped;
    sDatabase.DoEscapeString(escaped, name);
    int64 now(GetFileTimeNow());
    DBerror err;
    if (!sDatabase.RunQuery(err, "INSERT INTO shipInsurance"
            " (shipID, shipName, ownerID, isCorpItem, corpID, typeID, startDate, endDate, fraction, payOutAmount)"
            " VALUES (%u, '%s', %u, %u, %u, %u, %lli, %lli, %.3f, %lli)",
            shipID, escaped.c_str(), ownerID, isCorpItem ? 1 : 0, isCorpItem ? ownerID : 0, typeID, now,
            now + (EvE::Time::Week * numWeeks), fraction, (int64)payOut))
    {
        codelog(DATABASE__ERROR, "Failed to insure ship %u: %s", shipID, err.c_str());
        return false;
    }
    return true;
}

void ShipDB::DeleteInsuranceByShipID(uint32 shipID) {
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM shipInsurance WHERE shipID=%u", shipID);
}

static void FillInsuranceRow(DBResultRow& row, ShipDB::InsuranceRow& into)
{
    into.shipID     = row.GetUInt(0);
    into.ownerID    = row.GetUInt(1);
    into.typeID     = row.GetUInt(2);
    into.isCorp     = (row.GetInt(3) != 0);
    into.fraction   = row.GetFloat(4);
    into.startDate  = row.GetInt64(5);
    into.endDate    = row.GetInt64(6);
    into.shipName   = row.GetText(7);
}

bool ShipDB::GetInsurance(uint32 shipID, InsuranceRow& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT shipID, ownerID, typeID, isCorpItem, fraction, startDate, endDate, shipName FROM shipInsurance WHERE shipID = %u", shipID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }
    if (!res.GetRow(row))
        return false;
    FillInsuranceRow(row, into);
    return true;
}

void ShipDB::GetExpiredInsurance(int64 now, std::vector<InsuranceRow>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT shipID, ownerID, typeID, isCorpItem, fraction, startDate, endDate, shipName FROM shipInsurance"
        " WHERE endDate < %lli AND expiryNotified = 0 LIMIT 500", now))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    while (res.GetRow(row)) {
        InsuranceRow data = InsuranceRow();
        FillInsuranceRow(row, data);
        into.push_back(data);
    }
}

void ShipDB::SetExpiryNotified(uint32 shipID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE shipInsurance SET expiryNotified = 1 WHERE shipID = %u", shipID);
}

void ShipDB::DeleteInsuranceEndedBefore(int64 time)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM shipInsurance WHERE endDate < %lli", time);
}

int8 ShipDB::GetInsuranceKind(uint32 shipID)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res, "SELECT isCorpItem FROM shipInsurance WHERE shipID = %u", shipID) or !res.GetRow(row))
        return -1;
    return (row.GetInt(0) != 0) ? 1 : 0;
}

void ShipDB::VoidInsurance(uint32 shipID)
{
    DeleteInsuranceByShipID(shipID);
}

void ShipDB::OnShipOwnerChanged(uint32 shipID, uint32 newOwnerID)
{
    // repackaging, trading, selling, contracting or moving a ship into a corp hangar voids personal insurance
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM shipInsurance WHERE shipID = %u AND isCorpItem = 0 AND ownerID <> %u", shipID, newOwnerID);
}

bool ShipDB::IsShipInsured(uint32 shipID)
{
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT ownerID FROM shipInsurance WHERE shipID = %u", shipID );
    DBResultRow row;
    if (res.GetRow(row))
        return true;

    return false;
}

void ShipDB::ClearWeaponGroups(uint32 shipID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM shipWeaponGroups WHERE shipID = %u", shipID );
}

void ShipDB::LoadWeaponGroups(uint32 shipID, DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT masterID, slaveID FROM shipWeaponGroups WHERE shipID = %u", shipID );
}

void ShipDB::SaveWeaponGroups(uint32 shipID, std::multimap< uint32, uint32 >& data)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM shipWeaponGroups WHERE shipID = %u", shipID );
    std::ostringstream Inserts;
    // start the insert data.
    Inserts << "INSERT INTO shipWeaponGroups";
    Inserts << " (shipID, masterID, slaveID)";
    bool first = true;
    for (auto cur : data) {
        if (first) {
            Inserts << " VALUES ";
            first = false;
        } else {
            Inserts << ", ";
        }
        Inserts << "(" << shipID << ", " << cur.first << ", " << cur.second << ")";
    }

    if (!first)
        if (!sDatabase.RunQuery(err, Inserts.str().c_str()))
            _log(DATABASE__ERROR, "SaveLinkedWeapons - unable to save data - %s", err.c_str());
}
