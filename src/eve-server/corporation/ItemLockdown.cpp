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

#include "eve-server.h"

#include "../../eve-common/EVE_Mail.h"

#include "EntityList.h"
#include "corporation/CorporationDB.h"
#include "corporation/ItemLockdown.h"
#include "inventory/ItemFactory.h"
#include "manufacturing/Blueprint.h"
#include "station/StationDataMgr.h"

std::set<uint32> ItemLockdown::s_locked;
bool ItemLockdown::s_loaded = false;

void ItemLockdown::Load()
{
    if (s_loaded)
        return;
    s_locked.clear();
    CorporationDB::GetLockedItemIDs(s_locked);
    s_loaded = true;
}

bool ItemLockdown::IsLocked(uint32 itemID)
{
    Load();
    return (s_locked.find(itemID) != s_locked.end());
}

void ItemLockdown::ValidateLockable(uint32 corpID, uint32 itemID, uint32 stationID)
{
    // only corp-owned blueprint originals in that corp's office in the station
    InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
    if (iRef.get() == nullptr)
        throw UserError("CrpLockFailedItemNotFound");
    if (iRef->categoryID() != EVEDB::invCategories::Blueprint)
        throw UserError("CrpLockFailedIllegalItemType");
    BlueprintRef bpRef = sItemFactory.GetBlueprintRef(itemID);
    if ((bpRef.get() == nullptr) or bpRef->copy())
        throw UserError("CrpLockFailedOnlyBlueprintOriginals");
    if (iRef->ownerID() != corpID)
        throw UserError("CrpCantLockItemNotYours");
    uint32 officeID(stDataMgr.GetOfficeIDForCorp(stationID, corpID));
    if ((officeID == 0) or (iRef->locationID() != officeID))
        throw UserError("CrpLockFailedItemNotFound");
}

void ItemLockdown::Lock(uint32 corpID, uint32 itemID, uint32 stationID)
{
    ValidateLockable(corpID, itemID, stationID);
    if (IsLocked(itemID))
        return;

    CorporationDB::AddLockedItem(itemID, corpID, stationID);
    s_locked.insert(itemID);

    InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
    Notify(corpID, itemID, (iRef.get() == nullptr ? 0 : iRef->typeID()), stationID, true);
    _log(CORP__TRACE, "ItemLockdown::Lock() - corp %u locked item %u in %u", corpID, itemID, stationID);
}

void ItemLockdown::Unlock(uint32 corpID, uint32 itemID)
{
    if (!IsLocked(itemID))
        throw UserError("CrpLockedItemNotFound");

    // the station it was locked in
    uint32 stationID(0);
    DBQueryResult res;
    if (sDatabase.RunQuery(res, "SELECT locationID FROM crpLockedItems WHERE itemID = %u AND ownerID = %u", itemID, corpID)) {
        DBResultRow row;
        if (res.GetRow(row))
            stationID = row.GetUInt(0);
    }

    CorporationDB::RemoveLockedItem(itemID);
    s_locked.erase(itemID);

    InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
    Notify(corpID, itemID, (iRef.get() == nullptr ? 0 : iRef->typeID()), stationID, false);
    _log(CORP__TRACE, "ItemLockdown::Unlock() - corp %u unlocked item %u in %u", corpID, itemID, stationID);
}

PyRep* ItemLockdown::GetLockedItemLocations(uint32 corpID)
{
    return CorporationDB::GetLockedItemLocations(corpID);
}

PyRep* ItemLockdown::GetLockedItemsByLocation(uint32 corpID, uint32 stationID)
{
    return CorporationDB::GetLockedItemsByLocation(corpID, stationID);
}

void ItemLockdown::Notify(uint32 corpID, uint32 itemID, uint16 typeID, uint32 stationID, bool locked)
{
    /* OnLockedItemChange(itemID, ownerID, locationID, change)  change = {column: (old, new)}
     *  a new lock is (None, value) per column, a removed one (value, None) */
    PyDict* change = new PyDict();
    std::map<std::string, uint32> columns;
        columns["itemID"] = itemID;
        columns["typeID"] = typeID;
        columns["ownerID"] = corpID;
        columns["locationID"] = stationID;
    for (auto cur : columns) {
        PyTuple* values = new PyTuple(2);
        if (locked) {
            values->SetItem(0, PyStatic.NewNone());
            values->SetItem(1, new PyInt(cur.second));
        } else {
            values->SetItem(0, new PyInt(cur.second));
            values->SetItem(1, PyStatic.NewNone());
        }
        change->SetItemString(cur.first.c_str(), values);
    }

    PyTuple* tuple = new PyTuple(4);
        tuple->SetItem(0, new PyInt(itemID));
        tuple->SetItem(1, new PyInt(corpID));
        tuple->SetItem(2, new PyInt(stationID));
        tuple->SetItem(3, change);
    sEntityList.CorpNotify(corpID, Notify::Types::ItemUpdateStation, "OnLockedItemChange", "corpid", tuple);
}
