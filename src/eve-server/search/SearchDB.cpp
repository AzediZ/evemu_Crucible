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
    Author:      Allan, bb2k
*/

#include "eve-server.h"

#include "search/SearchDB.h"

/**   search runs thru these types...even inventory (type 10)
searchResultAgent = 1
searchResultCharacter = 2
searchResultCorporation = 3
searchResultAlliance = 4
searchResultFaction = 5
searchResultConstellation = 6
searchResultSolarSystem = 7
searchResultRegion = 8
searchResultStation = 9
searchResultInventoryType = 10
searchResultAllOwners = [1, 2, 3, 4, 5]
searchResultAllLocations = [6, 7, 8, 9]
searchMaxResults = 500
searchMinWildcardLength = 3
*/

void SearchDB::BuildIDs(const std::string& str, int type, uint32 charID, bool hideNPC, bool onlyAltName, std::vector<uint32>& out)
{
    const char* s = str.c_str();
    uint32 limit = searchMaxResults;
    DBQueryResult res;
    bool ok(false);
    switch (type) {
        case searchResultAgent:
            ok = sDatabase.RunQuery(res,
                "SELECT characterID FROM chrNPCCharacters"
                " WHERE characterName LIKE '%s' ORDER BY characterName LIMIT %u", s, limit);
            break;
        case searchResultCharacter:
            ok = sDatabase.RunQuery(res,
                "SELECT characterID FROM chrCharacters"
                " WHERE characterName LIKE '%s' ORDER BY characterName LIMIT %u", s, limit);
            break;
        case searchResultCorporation:
            if (onlyAltName) {
                ok = sDatabase.RunQuery(res,
                    "SELECT corporationID FROM crpCorporation"
                    " WHERE tickerName LIKE '%s' AND corporationID >= %u ORDER BY corporationName LIMIT %u",
                    s, (hideNPC ? minPCCorporation : 0), limit);
            } else {
                ok = sDatabase.RunQuery(res,
                    "SELECT corporationID FROM crpCorporation"
                    " WHERE (corporationName LIKE '%s' OR tickerName LIKE '%s') AND corporationID >= %u"
                    " ORDER BY corporationName LIMIT %u",
                    s, s, (hideNPC ? minPCCorporation : 0), limit);
            }
            break;
        case searchResultAlliance:
            if (onlyAltName) {
                ok = sDatabase.RunQuery(res,
                    "SELECT allianceID FROM alnAlliance"
                    " WHERE shortName LIKE '%s' ORDER BY allianceName LIMIT %u", s, limit);
            } else {
                ok = sDatabase.RunQuery(res,
                    "SELECT allianceID FROM alnAlliance"
                    " WHERE (allianceName LIKE '%s' OR shortName LIKE '%s') ORDER BY allianceName LIMIT %u",
                    s, s, limit);
            }
            break;
        case searchResultFaction:
            ok = sDatabase.RunQuery(res,
                "SELECT factionID FROM facFactions"
                " WHERE factionName LIKE '%s' ORDER BY factionName LIMIT %u", s, limit);
            break;
        case searchResultConstellation:
            ok = sDatabase.RunQuery(res,
                "SELECT constellationID FROM mapConstellations"
                " WHERE constellationName LIKE '%s' ORDER BY constellationName LIMIT %u", s, limit);
            break;
        case searchResultSolarSystem:
            ok = sDatabase.RunQuery(res,
                "SELECT solarSystemID FROM mapSolarSystems"
                " WHERE solarSystemName LIKE '%s' ORDER BY solarSystemName LIMIT %u", s, limit);
            break;
        case searchResultRegion:
            ok = sDatabase.RunQuery(res,
                "SELECT regionID FROM mapRegions"
                " WHERE regionName LIKE '%s' ORDER BY regionName LIMIT %u", s, limit);
            break;
        case searchResultStation:
            ok = sDatabase.RunQuery(res,
                "SELECT stationID FROM staStations"
                " WHERE stationName LIKE '%s' ORDER BY stationName LIMIT %u", s, limit);
            break;
        case searchResultInventoryType:
            // cfg.invtypes:  published types
            ok = sDatabase.RunQuery(res,
                "SELECT typeID FROM invTypes"
                " WHERE typeName LIKE '%s' AND published = 1 ORDER BY typeName LIMIT %u", s, limit);
            break;
        default:
            _log(SERVICE__WARNING, "SearchDB::BuildIDs() - unknown search type %i", type);
            return;
    }

    if (!ok) {
        codelog(DATABASE__ERROR, "Error in search query (type %i): %s", type, res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        out.push_back(row.GetUInt(0));
}

PyRep *SearchDB::Query(std::string string, std::vector<int> *searchID, uint32 charID) {
    // {resultType: [ids]}, empty types left out
    PyDict *dict = new PyDict();
    std::set<int> done;
    for (auto type : *searchID) {
        if (!done.insert(type).second)
            continue;
        std::vector<uint32> ids;
        BuildIDs(string, type, charID, false, false, ids);
        if (ids.empty())
            continue;
        PyList* list = new PyList();
        for (auto cur : ids)
            list->AddItem(new PyInt(cur));
        dict->SetItem(new PyInt(type), list);
    }

    return dict;
}

PyRep *SearchDB::QuickQuery(std::string string, std::vector<int> *searchID, uint32 charID, bool hideNPC, bool onlyAltName) {
    // flat list of ids over all requested types, without duplicates
    PyList *result = new PyList();
    std::set<uint32> seen;
    std::set<int> done;
    for (auto type : *searchID) {
        if (!done.insert(type).second)
            continue;
        std::vector<uint32> ids;
        BuildIDs(string, type, charID, hideNPC, onlyAltName, ids);
        for (auto cur : ids) {
            if (seen.size() >= searchMaxResults)
                return result;
            if (seen.insert(cur).second)
                result->AddItem(new PyInt(cur));
        }
    }

    return result;
}
