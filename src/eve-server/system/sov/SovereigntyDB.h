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
    Author:       James
*/

#ifndef __SOVEREIGNTYDB_H_INCL__
#define __SOVEREIGNTYDB_H_INCL__

#include "ServiceDB.h"

class PyRep;
class Client;

class SovereigntyDB
: public ServiceDB
{
public:
    static void GetSovereigntyData(DBQueryResult& res);
    static void GetSovereigntyDataForSystem(DBQueryResult& res, uint32 systemID);
    static void AddSovereigntyData(SovereigntyData data, uint32& claimID);
    static void RemoveSovereigntyData(uint32 systemID);
    // the number of stargates with an online sovereignty blockade unit (EVE_Sov.h)
    static void SetContested(uint32 systemID, uint8 sbuGates);
    static void SetClaimTime(uint32 systemID, int64 claimTime);
    static void SetHubID(uint32 systemID, uint32 hubID);
    static void SetJammerID(uint32 systemID, uint32 jammerID);
    static void SetBeaconID(uint32 systemID, uint32 beaconID);

    // development indices  (sovDevIndices)
    // solarSystemID, militaryPoints, industrialPoints, militaryIncreasing, industrialIncreasing, lastDecayTime
    static void LoadDevIndices(DBQueryResult& res);
    static void SaveDevIndex(const DevIndexData& data);
    // locationID (hubID), typeID of online upgrades in the given hubs
    static void GetActiveUpgrades(DBQueryResult& res, const std::string& hubIDs);

    /* recent sovereignty activity (map.GetRecentSovActivity).  stationID 0 = system sov; 0 owners are stored as NULL
     *  gain:  oldOwnerID 0.  loss:  ownerID 0.  outpost capture:  both set
     */
    static void LogSovChange(uint32 systemID, uint32 stationID, uint32 ownerID, uint32 oldOwnerID);
    // solarSystemID, stationID, ownerID, oldOwnerID  of changes at or after 'since' (filetime)
    static void GetRecentSovChanges(DBQueryResult& res, int64 since);
};

#endif
