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


#ifndef __CONFIGDB_H_INCL__
#define __CONFIGDB_H_INCL__

#include "ServiceDB.h"


class PyRep;

// one cfg.eveowners row:  ownerID, ownerName, typeID, gender, ownerNameID
struct OwnerRow {
    int32 ownerID;
    std::string ownerName;
    uint32 typeID;
    int8 gender;            // -1 = None (not a character)
    uint32 ownerNameID;     // 0 = None
};

class ConfigDB
: public ServiceDB
{
public:
    /* owner rows for characters, agents, corporations, alliances, factions, stations, types (<33000) and
     *  other static owners.  ids not found are left out.  shared by config.GetMultiOwnersEx, mailMgr.PrimeOwners
     *  and StaticDataMgr::GetOwnerName
     */
    static void GetOwnerRows(const std::vector<int32>& ids, std::map<int32, OwnerRow>& into);
    // 5-element list in cfg.eveowners header order
    static PyList* OwnerRowToList(const OwnerRow& row);

    PyRep *GetMultiOwnersEx(const std::vector<int32> &entityIDs);
    PyRep *GetMultiLocationsEx(const std::vector<int32> &entityIDs);
    PyRep *GetMultiStationEx(const std::vector<int32> &entityIDs);
    PyRep *GetMultiAllianceShortNamesEx(const std::vector<int32> &entityIDs);
    PyRep *GetMultiCorpTickerNamesEx(const std::vector<int32> &entityIDs);
    PyRep *GetMultiGraphicsEx(const std::vector<int32> &entityIDs);
    PyRep *GetMultiInvTypesEx(const std::vector<int32> &typeIDs);
    PyObject *GetUnits();
    PyObjectEx *GetMapObjects(uint32 entityID, bool wantRegions, bool wantConstellations, bool wantSystems, bool wantStations);
    PyObject *GetMap(uint32 solarSystemID);
    PyObject *GetMapOffices(uint32);
    PyObject *GetMapConnections(uint32, bool, bool, bool, uint16, uint16);
    PyObject *ListLanguages();
    PyRep *GetStationSolarSystemsByOwner(uint32 ownerID);
    PyRep *GetCelestialStatistic(uint32 celestialID);
    PyRep *GetTextsForGroup(const std::string & langID, uint32 textgroup);
    PyObject *GetMapLandmarks();

    /**
    * @brief Retrieves dynamic, celestial objects for a given solar system
    *
    * @param[in] solarSystemID  ID of the solar system whose objects are being retrieved
    * @author allan
    */
    PyRep *GetDynamicCelestials(uint32 solarSystemID);

protected:
};

#endif



