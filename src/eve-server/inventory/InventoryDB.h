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
*/

#ifndef __INVENTORYDB_H_INCL__
#define __INVENTORYDB_H_INCL__

#include "ServiceDB.h"
//#include "inventory/ItemRef.h"


class InventoryDB
: public ServiceDB
{
public:
    bool GetItemContents(OwnerData &od, std::vector<uint32> &into);
    bool GetItemContents(uint32 itemID, EVEItemFlags flag, std::vector<uint32> &into);
    bool GetItemContents(uint32 itemID, EVEItemFlags flag, uint32 ownerID, std::vector<uint32> &into);

    static void DeleteTrackingCans();

    /* trash (junkyard) log:  trashed items wait at locJunkyard for a GM restore until they're purged */
    static void AddTrashLog(uint32 itemID, uint32 ownerID, uint32 locationID, uint16 flag, uint32 byID);
    static bool GetTrashLog(uint32 itemID, uint32& ownerID, uint32& locationID, uint16& flag);
    static void DeleteTrashLog(uint32 itemID);
    // newest 50 trashed items of an owner:  itemID, typeID, quantity, trashTime, locationID
    static void ListTrash(uint32 ownerID, DBQueryResult& res);
    // deletes items trashed before cutoff (Win32 time), their contents and their per-item rows
    static void PurgeTrash(int64 cutoff);
    // deletes items and all they contain, with their per-item rows
    static void DeleteItemsRecursive(std::vector<uint32>& itemIDs);

    /* secure container passwords and audit log container settings.  an empty password is no password */
    static bool GetContainerSecurity(uint32 itemID, std::string& general, std::string& config, uint8& alscConfig);
    static void SaveContainerSecurity(uint32 itemID, const std::string& general, const std::string& config, uint8 alscConfig);
    static void DeleteContainerSecurity(uint32 itemID);
    /* audit log container action log.  arg1/arg2 of 0 are stored as NULL */
    static void AddAuditLog(uint32 containerID, uint32 actorID, uint8 actionID, bool ok, uint32 invMgrLocationID,
                            uint32 locationID, uint16 flagID, int32 arg1, int32 arg2);
    // up to 50 rows:  logID, logDate, inventoryMgrLocationID, locationID, flagID, actorID, actionID, status, arg1, arg2
    static PyRep* GetAuditLog(uint32 containerID, int64 fromDate, int32 fromLogID, bool forward);
    static bool HasAuditLogSince(uint32 containerID, int64 since);
};

#endif
