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

/* Corp item lockdown:  blueprint originals in a corp office, locked by a shareholder vote (voteItemLockdown) and
 *  unlocked by another (voteItemUnlock).  a locked item can't be moved (not even between divisions), trashed,
 *  delivered, split, repackaged, contracted or sold.  it can still be used in R.A.M. jobs.
 *  the vote side (vote cases, sanctioned actions) calls ValidateLockable when a lockdown vote is proposed and
 *  Lock/Unlock when the vote is acted upon.
 *  crpLockedItems keeps itemID, ownerID (the corp), locationID (the station) and locked.
 */

#ifndef EVEMU_CORPORATION_ITEMLOCKDOWN_H_
#define EVEMU_CORPORATION_ITEMLOCKDOWN_H_

#include "eve-compat.h"

class PyRep;

class ItemLockdown
{
public:
    static bool IsLocked(uint32 itemID);
    // throws the client's Crp* lock errors when the item can't be locked by corpID in stationID
    static void ValidateLockable(uint32 corpID, uint32 itemID, uint32 stationID);
    // validates, stores and tells the corp (OnLockedItemChange)
    static void Lock(uint32 corpID, uint32 itemID, uint32 stationID);
    // CrpLockedItemNotFound when it isn't locked
    static void Unlock(uint32 corpID, uint32 itemID);

    // corpRegistry calls:  stationIDs with locked items, and the locked items in a station (CIndexedRowset by itemID)
    static PyRep* GetLockedItemLocations(uint32 corpID);
    static PyRep* GetLockedItemsByLocation(uint32 corpID, uint32 stationID);

private:
    static void Load();
    static void Notify(uint32 corpID, uint32 itemID, uint16 typeID, uint32 stationID, bool locked);

    static std::set<uint32> s_locked;
    static bool s_loaded;
};

#endif  // EVEMU_CORPORATION_ITEMLOCKDOWN_H_
