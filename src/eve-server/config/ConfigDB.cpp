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
    Rewrite:    Allan
*/

#include "eve-server.h"

#include "config/ConfigDB.h"

namespace {
    // runs one owner query and adds its rows (ownerID, ownerName, typeID, gender, ownerNameID) to 'into'
    void AddOwnerRows(std::map<int32, OwnerRow>& into, const char* query, const std::string& ids)
    {
        DBQueryResult res;
        if (!sDatabase.RunQuery(res, query, ids.c_str())) {
            codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
            return;
        }
        DBResultRow row;
        while (res.GetRow(row)) {
            OwnerRow data = OwnerRow();
            data.ownerID = row.GetInt(0);
            data.ownerName = (row.IsNull(1) ? "" : std::string(row.GetText(1), row.ColumnLength(1)));
            data.typeID = (row.IsNull(2) ? 0 : row.GetUInt(2));
            data.gender = (row.IsNull(3) ? -1 : (int8)row.GetInt(3));
            data.ownerNameID = (row.IsNull(4) ? 0 : row.GetUInt(4));
            into[data.ownerID] = data;
        }
    }
}

void ConfigDB::GetOwnerRows(const std::vector<int32>& ids, std::map<int32, OwnerRow>& into)
{
    std::vector<int32> player, agent, corp, ally, faction, station, type, owner;
    for (auto cur : ids) {
        if (into.find(cur) != into.end())
            continue;
        if (cur == 1) {
            OwnerRow data = OwnerRow();
            data.ownerID = 1;
            data.ownerName = "EVE System";
            data.typeID = 0;
            data.gender = -1;
            data.ownerNameID = 0;
            into[1] = data;
        } else if (IsCharacterID(cur)) {
            player.push_back(cur);
        } else if (IsAgent(cur)) {
            agent.push_back(cur);
        } else if (IsFaction(cur)) {
            faction.push_back(cur);
        } else if (IsCorp(cur)) {
            corp.push_back(cur);
        } else if (IsAlliance(cur)) {
            ally.push_back(cur);
        } else if (IsStationID(cur)) {
            station.push_back(cur);
        } else if ((cur > 0) and (cur < 33000)) {
            type.push_back(cur);
        } else {
            owner.push_back(cur);
        }
    }

    std::string list;
    if (!player.empty()) {
        list.clear();
        ListToINString(player, list);
        AddOwnerRows(into, "SELECT characterID, characterName, typeID, gender, NULL"
                " FROM chrCharacters WHERE characterID IN (%s)", list);
    }
    if (!agent.empty()) {
        list.clear();
        ListToINString(agent, list);
        AddOwnerRows(into, "SELECT c.characterID, c.characterName, IF(c.typeID > 0, c.typeID, o.typeID), c.gender, NULL"
                " FROM chrNPCCharacters AS c LEFT JOIN eveStaticOwners AS o ON o.ownerID = c.characterID"
                " WHERE c.characterID IN (%s)", list);
    }
    if (!corp.empty()) {
        list.clear();
        ListToINString(corp, list);
        AddOwnerRows(into, "SELECT corporationID, corporationName, 2, NULL, NULL"
                " FROM crpCorporation WHERE corporationID IN (%s)", list);
    }
    if (!ally.empty()) {
        list.clear();
        ListToINString(ally, list);
        AddOwnerRows(into, "SELECT allianceID, allianceName, 16159, NULL, NULL"
                " FROM alnAlliance WHERE allianceID IN (%s)", list);
    }
    if (!faction.empty()) {
        list.clear();
        ListToINString(faction, list);
        AddOwnerRows(into, "SELECT factionID, factionName, 30, NULL, NULL"
                " FROM facFactions WHERE factionID IN (%s)", list);
    }
    if (!station.empty()) {
        list.clear();
        ListToINString(station, list);
        AddOwnerRows(into, "SELECT stationID, stationName, stationTypeID, NULL, NULL"
                " FROM staStations WHERE stationID IN (%s)", list);
    }
    if (!type.empty()) {
        list.clear();
        ListToINString(type, list);
        AddOwnerRows(into, "SELECT typeID, typeName, typeID, NULL, typeNameID"
                " FROM invTypes WHERE typeID IN (%s)", list);
    }

    // anything not found above may still be a static owner (npc characters, eve system entities)
    for (auto cur : ids)
        if ((cur != 1) and (into.find(cur) == into.end()))
            owner.push_back(cur);
    if (!owner.empty()) {
        list.clear();
        ListToINString(owner, list);
        AddOwnerRows(into, "SELECT ownerID, ownerName, typeID, NULL, NULL"
                " FROM eveStaticOwners WHERE ownerID IN (%s)", list);
    }
}

PyList* ConfigDB::OwnerRowToList(const OwnerRow& row)
{
    PyList* list = new PyList(5);
        list->SetItem(0, new PyInt(row.ownerID));
        list->SetItem(1, new PyWString(row.ownerName));
        list->SetItem(2, new PyInt(row.typeID));
        list->SetItem(3, (row.gender < 0 ? PyStatic.NewNone() : new PyInt(row.gender)));
        list->SetItem(4, (row.ownerNameID == 0 ? PyStatic.NewNone() : new PyInt(row.ownerNameID)));
    return list;
}

PyRep *ConfigDB::GetMultiOwnersEx(const std::vector<int32> &entityIDs) {
    /* cfg.eveowners:  (header, rows), always this shape (the client unpacks it), every row keyed by a requested id.
     *  stations keep their stationID as the key (the client removes each returned key from its request set)
     */
    std::map<int32, OwnerRow> rows;
    GetOwnerRows(entityIDs, rows);

    PyList* header = new PyList(5);
        header->SetItemString(0, "ownerID");
        header->SetItemString(1, "ownerName");
        header->SetItemString(2, "typeID");
        header->SetItemString(3, "gender");
        header->SetItemString(4, "ownerNameID");
    PyList* results = new PyList();
    for (auto& cur : rows)
        results->AddItem(OwnerRowToList(cur.second));

    PyTuple* response = new PyTuple(2);
        response->SetItem(0, header);
        response->SetItem(1, results);
    return response;
}

PyRep *ConfigDB::GetMultiAllianceShortNamesEx(const std::vector<int32> &entityIDs) {
    std::string ids;
    ListToINString(entityIDs, ids);
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT allianceID, shortName FROM alnAlliance WHERE allianceID IN (%s)", ids.c_str() )) {
        codelog(DATABASE__ERROR, "Error in GetMultiAllianceShortNamesEx query: %s", res.error.c_str());
        return new PyInt(0);
    }

    return DBResultToTupleSet(res);
}

PyRep *ConfigDB::GetMultiLocationsEx(const std::vector<int32> &entityIDs) {
    // this is locations only....region, const, system, station, ship
    // wtf are asteroids seen in this call???
    std::vector<int32> staticItems, dynamicItems, asteroidItems;
    staticItems.clear();
    dynamicItems.clear();
    asteroidItems.clear();

    for (auto cur : entityIDs) {
        if (IsStaticItem(cur)) {
            staticItems.push_back(cur);
        } else if (IsAsteroidID(cur)) {
            asteroidItems.push_back(cur);
        } else {
            dynamicItems.push_back(cur);
        }
    }

    DBQueryResult res;
    std::string ids = "";

    if (staticItems.size()) {
        ListToINString(staticItems, ids);
        if (!sDatabase.RunQuery(res,
            "SELECT "
            " itemID AS locationID,"
            " itemName AS locationName,"
            " x, y, z,"
            " itemNameID AS locationNameID"   //locationName = localization.GetByMessageID(self.locationNameID)
            " FROM mapDenormalize"
            " WHERE itemID in (%s)", ids.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in GetMultiLocationsEx query: %s", res.error.c_str());
        }
        ids = "";
    }

    if (dynamicItems.size()) {
        ListToINString(dynamicItems, ids);
        if (!sDatabase.RunQuery(res,
            "SELECT "
            " itemID AS locationID,"
            " itemName AS locationName,"
            " x, y, z,"
            " NULL AS locationNameID"
            " FROM entity"
            " WHERE itemID in (%s)", ids.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in GetMultiLocationsEx query: %s", res.error.c_str());
        }
        ids = "";
    }

    if (asteroidItems.size()) {
        ListToINString(asteroidItems, ids);
        sLog.Warning("GetMultiLocationsEx", "Asteroid Items (%s) requested.", ids.c_str());
        if (!sDatabase.RunQuery(res,
            "SELECT "
            " itemID AS locationID,"
            " itemName AS locationName,"
            " x, y, z,"
            " NULL AS locationNameID"
            " FROM sysAsteroids"
            " WHERE itemID in (%s)", ids.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in GetMultiLocationsEx query: %s", res.error.c_str());
        }
    }

    return DBResultToTupleSet(res);
}

PyRep* ConfigDB::GetMultiStationEx(const std::vector< int32 >& entityIDs)
{
    std::string ids;
    ListToINString(entityIDs, ids);
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT stationID, stationName, stationTypeID, solarSystemID, x, y, z FROM staStations WHERE stationID in (%s)", ids.c_str())) {
        codelog(DATABASE__ERROR, "Error in GetMultiStationEx query: %s", res.error.c_str());
    }

    return DBResultToTupleSet(res);
}


PyRep *ConfigDB::GetMultiCorpTickerNamesEx(const std::vector<int32> &entityIDs)
{
    std::string ids;
    ListToINString(entityIDs, ids);
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT "
        "   corporationID, tickerName,"
        "   shape1, shape2, shape3,"
        "   color1, color2, color3 "
        " FROM crpCorporation"
        " WHERE corporationID in (%s)", ids.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in GetMultiCorpTickerNamesEx query: %s", res.error.c_str());
        return new PyInt(0);
    }

    return DBResultToRowList(res);
}


PyRep *ConfigDB::GetMultiGraphicsEx(const std::vector<int32> &entityIDs) {
    std::string ids;
    ListToINString(entityIDs, ids);

    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        "   graphicID, url3D, urlWeb, icon, urlSound, explosionID"
        " FROM eveGraphics "
        " WHERE graphicID in (%s)", ids.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in GetMultiGraphicsEx query: %s", res.error.c_str());
        return new PyInt(0);
    }

    return DBResultToRowList(res);
}

PyObject *ConfigDB::GetUnits()
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT unitID, unitName, displayName FROM eveUnits")) {
        codelog(DATABASE__ERROR, "Error in GetUnits query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToIndexRowset(res, "unitID");
}

PyObjectEx *ConfigDB::GetMapObjects(uint32 entityID, bool wantRegions,
    bool wantConstellations, bool wantSystems, bool wantStations)
{       //  corrected query  -allan 8Dec14
    const char *key = "solarSystemID";
    if (wantRegions) {
        entityID = 3;   /* a little hackish... */
        key = "typeID";
    } else if (wantConstellations) {
        key = "regionID";
    } else if (wantSystems) {
        key = "constellationID";
    } else if (wantStations) {
        key = "solarSystemID";
    }

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT "
        "   groupID, typeID, itemID, itemName,"
        "   solarSystemID AS locationID, IFNULL(orbitID, 0) AS orbitID,"
        "   x, y, z"
        " FROM mapDenormalize"
        " WHERE %s=%u", key, entityID )) {
        codelog(DATABASE__ERROR, "Error in GetMapObjects query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

/** @todo update this to use static data mgr */
PyObject *ConfigDB::GetMap(uint32 solarSystemID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT "
        "   s.solarSystemID AS locationID,"
        "   s.xMin, s.xMax, s.yMin,"
        "   s.yMax, s.zMin, s.zMax,"
        "   s.luminosity,"
        "   d.x, d.y, d.z,"
        "   d.itemID,"
        "   d.itemName,"
        "   d.typeID,"
        "   d.orbitID,"
        "   j.celestialID AS destinations"
        " FROM mapSolarSystems AS s"
        "  LEFT JOIN mapDenormalize AS d USING (solarSystemID)"
        "  LEFT JOIN mapJumps AS j ON j.stargateID = d.itemID"
        " WHERE solarSystemID=%u", solarSystemID
    )) {
        codelog(DATABASE__ERROR, "Error in GetMap query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyObject *ConfigDB::ListLanguages() {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT languageID,languageName,translatedLanguageName FROM languages")) {
        codelog(DATABASE__ERROR, "Error in ListLanguages query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}


PyRep *ConfigDB::GetMultiInvTypesEx(const std::vector<int32> &entityIDs) {
    std::string ids;
    ListToINString(entityIDs, ids);
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "   typeID,groupID,typeName,description,graphicID,radius,"
        "   mass,volume,capacity,portionSize,raceID,basePrice,"
        "   published,marketGroupID,chanceOfDuplicating "
        " FROM invTypes "
        " WHERE typeID in (%s)", ids.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in GetMultiInvTypesEx query: %s", res.error.c_str());
        return new PyInt(0);
    }

    return DBResultToRowList(res);
}

PyRep *ConfigDB::GetStationSolarSystemsByOwner(uint32 ownerID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT "
        " stationID, solarSystemID "
        " FROM staStations "
        " WHERE corporationID = %u ", ownerID))
    {
        codelog(DATABASE__ERROR, "Error in GetStationSolarSystemsByOwner query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *ConfigDB::GetCelestialStatistic(uint32 celestialID) {
    //  corrected db query  -allan 8Dec14
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "   celestialID,"
        "   temperature,"
        "   spectralClass,"
        "   luminosity,"
        "   age,"
        "   life,"
        "   orbitRadius,"
        "   eccentricity,"
        "   massDust,"
        "   massGas,"
        "   fragmented,"
        "   density,"
        "   surfaceGravity,"
        "   escapeVelocity,"
        "   orbitPeriod,"
        "   rotationRate,"
        "   locked,"
        "   pressure,"
        "   radius,"
        "   mass"
        " FROM mapCelestialStatistics"
        " WHERE celestialID = %u", celestialID))
        {
            codelog(DATABASE__ERROR, "Error in GetCelestialStatistic query: %s", res.error.c_str());
            return new PyInt(0);
        }

    return DBResultToCRowset(res);
}

PyRep *ConfigDB::GetDynamicCelestials(uint32 solarSystemID) {
    //  corrected query and return type per packet info
    //      this returns ONLY OUTPOSTS.  -allan 8Dec14
    /* this packet is from a crowded (73 items) system (including pos'), yet is only return from this call.
              [PyObjectEx Type2]
                [PyTuple 2 items]
                  [PyTuple 1 items]
                    [PyToken dbutil.CRowset]
                  [PyDict 1 kvp]
                    [PyString "header"]
                [PyPackedRow 50 bytes]
                  ["groupID" => <15> [I2]]
                  ["typeID" => <21645> [I4]]    <-- Gallente Administrative Outpost
                  ["itemID" => <61000562> [I4]]
                  ["itemName" => <5E-EZC VI - X333 GEORGIOU'S PLACE> [WStr]]
                  ["locationID" => <30004168> [I4]]
                  ["orbitID" => <40264214> [I4]]
                  ["connector" => <0> [I4]]
                  ["x" => <-435188244480> [R8]]
                  ["y" => <27742740480> [R8]]
                  ["z" => <430524948480> [R8]]
                  ["celestialIndex" => <6> [UI1]]
                  ["orbitIndex" => <0> [UI1]]
                  */

    DBQueryResult result;
    if (!sDatabase.RunQuery(result,
        "SELECT"
        "   groupID,"
        "   typeID,"
        "   itemID,"
        "   itemName,"
        "   solarSystemID AS locationID,"
        "   IFNULL(orbitID, False) AS orbitID,"
        "   0 AS connector,"     //this is connector field....have only seen 0 in server packets.
        "   x, y, z"
        //"   celestialIndex,"    //  this index denotes which planet this item orbits (PLANET #....NOT moon #)
        //"   orbitIndex"         //  this index denotes which asteroid belt this item belongs to
        " FROM mapDenormalize"
        " WHERE solarSystemID = %u"
        " AND groupID = %d"
        " AND itemID > %d",   //this is min itemid for outposts   (pos' do NOT go here)
        solarSystemID, EVEDB::invGroups::Station, maxNPCStation )) {
        codelog(DATABASE__ERROR, "Error in GetDynamicCelestials query: %s", result.error.c_str());
            return new PyInt(0);
    }

    return DBResultToCRowset(result);
}

PyRep *ConfigDB::GetTextsForGroup(const std::string & langID, uint32 textgroup) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT textLabel, `text` FROM intro WHERE langID = '%s' AND textgroup = %u", langID.c_str(), textgroup)) {
        codelog(DATABASE__ERROR, "Error in GetTextsForGroup query: %s", res.error.c_str());
        return new PyInt(0);
    }

    return DBResultToRowset(res);
}

PyObject *ConfigDB::GetMapOffices(uint32 solarSystemID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT corporationID, stationID FROM staStations WHERE solarSystemID=%u", solarSystemID)) {
        codelog(DATABASE__ERROR, "Error in GetMapOffices query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

/** @todo more map data in /eve/client/script/ui/shared/maps/mapcommon.py */
PyObject *ConfigDB::GetMapConnections(uint32 id, bool sol, bool reg, bool con, uint16 cel, uint16 _c) {
  sLog.Warning ("ConfigDB::GetMapConnections", "DB query - System:%u, Sol:%u, Reg:%u, Con:%u, Cel:%u, cached:%u", id, sol, reg, con, cel, _c);
    const char *key = "fromsol";
    if (sol) {
        key = "fromsol";
    } else if (reg) {
        key = "fromreg";
    } else if (con) {
        key = "fromcon";
    } else {
        sLog.Error ("ConfigDB::GetMapConnections()", "Bad argument (id: %u, sol: %u, reg: %u, con: %u) passed to key.", id, sol, reg, con );
    }

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT ctype, fromreg, fromcon, fromsol, stargateID, celestialID, tosol, tocon, toreg"
        " FROM mapConnections"
        " WHERE %s = %u", key, id )) {
        codelog(DATABASE__ERROR, "Error in GetMapConnections query: %s", res.error.c_str());
            sLog.Error ("ConfigDB::GetMapConnections()", "No Data for key: %s, id: %u.", key, id);
            return nullptr;
    }
    return DBResultToRowset(res);
}

PyObject *ConfigDB::GetMapLandmarks() {    // working 29June14
    DBQueryResult res;

      if (!sDatabase.RunQuery(res,
          "SELECT"
          "   landmarkID,"
          "   landmarkName,"
          "   0 AS landmarkNameID,"
          "   x, y, z,"
          "   radius"
          " FROM mapLandmarks" ))
      {
          codelog(DATABASE__ERROR, "Error in GetMapLandmarks query: %s", res.error.c_str());
          return nullptr;
      }
    return DBResultToRowset(res);
}

