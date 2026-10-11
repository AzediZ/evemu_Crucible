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
        "SELECT startDate, shipName, shipID, endDate, ownerID, fraction"
		" FROM shipInsurance"
        " WHERE shipID = %u", shipID );

    if (res.GetRow(row))
        return DBRowToRow(row);

    return PyStatic.NewZero();
}

PyRep *ShipDB::GetInsuranceByOwnerID(uint32 ownerID) {
    DBQueryResult res;
    sDatabase.RunQuery(res,
        "SELECT startDate, shipName, shipID, endDate, ownerID, fraction"
		" FROM shipInsurance"
        " WHERE ownerID = %u", ownerID );

    return DBResultToRowset(res);
}

bool ShipDB::InsertInsuranceByShipID(uint32 shipID, std::string name, uint32 ownerID, float fraction, double payOut, bool isCorpItem, uint8 numWeeks) {
    DBerror err;
    return sDatabase.RunQuery(err, "INSERT INTO shipInsurance"
            " (shipID, shipName, ownerID, isCorpItem, startDate, endDate, fraction, payOutAmount)"
            " VALUES (%u, '%s', %u, %u, %f, %f, %.2f, %f)",
                       shipID, name.c_str(), ownerID, isCorpItem, GetFileTimeNow(), \
                       (GetFileTimeNow() + (EvE::Time::Week * numWeeks)), fraction, payOut );
}

void ShipDB::DeleteInsuranceByShipID(uint32 shipID) {
    DBerror err;
	sDatabase.RunQuery(err, "DELETE FROM shipInsurance WHERE shipID=%u", shipID);
}

double ShipDB::GetShipInsurancePayout(uint32 shipID, uint16 typeID, bool& isDefault) {
    isDefault = false;
    DBQueryResult res;
    DBResultRow row;
    sDatabase.RunQuery(res, "SELECT payOutAmount FROM shipInsurance WHERE shipID = %u", shipID);
    if (res.GetRow(row))
        return row.GetDouble(0);

    // Crucible: an uninsured ship pays 40% of its insurance value (InsuranceService uses the base price as that value)
    isDefault = true;
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr)
        return 0;
    return pType->basePrice() * 0.4;
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
