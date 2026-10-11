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
    Author:        Allan, based on original design by Comet0
*/

/*
 * PLANET__ERROR
 * PLANET__WARNING
 * PLANET__MESSAGE
 * PLANET__DEBUG
 * PLANET__INFO
 * PLANET__TRACE
 * PLANET__DUMP
 * PLANET__RES_DUMP
 * PLANET__GC_DUMP
 * PLANET__PKT_TRACE
 * PLANET__DB_ERROR
 * PLANET__DB_WARNING
 */


#include <iomanip>

#include "planet/Colony.h"
#include "planet/PlanetDB.h"
#include "planet/PlanetDataMgr.h"


void PlanetDB::GetSchematicData(DBQueryResult& res)
{
    // load info into PIDataMgr
    if (!sDatabase.RunQuery(res, "SELECT schematicID, typeID, quantity, isInput FROM piTypeMap" ))
        _log(DATABASE__ERROR, "Error in GetSchematicData Query: %s", res.error.c_str());
}

void PlanetDB::GetSchematicTimes(DBQueryResult& res)
{
    // load info into PIDataMgr
    if (!sDatabase.RunQuery(res, "SELECT schematicID, cycleTime FROM piSchematics"))
        _log(DATABASE__ERROR, "Error in GetSchematicTimes Query: %s", res.error.c_str());
}

void PlanetDB::GetPlanetData(DBQueryResult& res)
{
    // load info into PlanetDataMgr
    if (!sDatabase.RunQuery(res,
        "SELECT planet.typeID AS planetTypeID,"
        " resource.typeID AS resourceID"
        " FROM invTypes planet, invTypes resource, dgmTypeAttributes dgm1, dgmTypeAttributes dgm2 "
        " WHERE dgm1.typeID = dgm2.typeID AND dgm1.attributeID = 1632 AND dgm1.valueFloat = planet.typeID AND dgm2.attributeID = 709"
        " AND dgm2.valueFloat = resource.typeID ORDER BY planet.typeID ")) {
        _log(DATABASE__ERROR, "Error in GetPlanetData Query: %s", res.error.c_str());
        }
}


PyRep* PlanetDB::GetPlanetsForChar(uint32 charID) {
  /** self.colonizationData = sm.RemoteSvc('planetMgr').GetPlanetsForChar()
        returns  (solarSystemID, planetID, typeID, numberOfPins)
    */
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT solarSystemID, planetID, typeID, numberOfPins"
        " FROM piPlanets WHERE charID = %u", charID)) {
        _log(DATABASE__ERROR, "Error in GetPlanetsForChar query: %s", res.error.c_str());
        return nullptr;
    }
    _log(DATABASE__RESULTS, "GetPlanetsForChar returned %lu items", res.GetRowCount());
    return DBResultToCRowset(res);
}

//  expired = not blue.os.GetWallclockTime() - launch.launchTime < const.piLaunchOrbitDecayTime (5d)
PyRep* PlanetDB::GetMyLaunchesDetails(uint32 charID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID AS launchID, status, itemID, solarSystemID, planetID, launchTime, x, y, z"
        " FROM piLaunches WHERE charID = %u", charID)) {
        _log(DATABASE__ERROR, "Error in GetMyLaunchesDetails Query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToRowset(res);
}

GPoint PlanetDB::GetLaunchPos(uint32 launchID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT x,y,z FROM piLaunches WHERE launchID = %u", launchID)) {
        _log(DATABASE__ERROR, "Error in GetLaunchPos query: %s", res.error.c_str());
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "Error in GetLaunchPos query: %s", res.error.c_str());
        return NULL_ORIGIN;
    }
    GPoint pos(row.GetDouble(0), row.GetDouble(1), row.GetDouble(2));
    return pos;
}

uint32 PlanetDB::GetLaunchPlanet(uint32 launchID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT planetID FROM piLaunches WHERE launchID = %u", launchID)) {
        _log(DATABASE__ERROR, "Error in GetLaunchPlanet query: %s", res.error.c_str());
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "Error in GetLaunchPlanet query: %s", res.error.c_str());
        return 0;
    }

    return row.GetInt(0);
}

void PlanetDB::GetLaunchDetails(Launch::Data& data)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID, status, solarSystemID, planetID, launchTime, x, y, z FROM piLaunches WHERE launchID = %u", data.launchID)) {
        _log(DATABASE__ERROR, "Error in GetLaunchDetails query: %s", res.error.c_str());
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "Error in GetLaunchDetails query: %s", res.error.c_str());
        return;
    }

    data.itemID = row.GetInt(0);
    data.status = row.GetInt(1);
    data.solarSystemID = row.GetInt(2);
    data.planetID = row.GetInt(3);
    data.launchTime = row.GetInt64(4);
    data.x = row.GetDouble(5);
    data.y = row.GetDouble(6);
    data.z = row.GetDouble(7);
}
uint32 PlanetDB::GetLaunchItemID(uint32 launchID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID FROM piLaunches WHERE launchID = %u", launchID)) {
        _log(DATABASE__ERROR, "Error in GetLaunchItemID query: %s", res.error.c_str());
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "Error in GetLaunchItemID query: %s", res.error.c_str());
        return 0;
    }

    return row.GetInt(0);
}

void PlanetDB::UpdateLaunchStatus(uint32 itemID, uint8 status)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE piLaunches SET status=%u WHERE itemID = %u", status, itemID);
}


void PlanetDB::GetExtractorsForPlanet(uint32 planetID, DBQueryResult& res)
{
    // heads of the planet's ecus with a running program
    if (!sDatabase.RunQuery(res,
        "SELECT h.ecuID, h.headID, p.typeID, p.programType, h.ownerID, h.latitude, h.longitude"
        " FROM piECUHeads AS h"
        " JOIN piPins AS p ON p.pinID = h.ecuID"
        " JOIN piCCPin AS cc ON cc.pinID = h.ccPinID"
        " WHERE cc.planetID = %u AND p.programType <> 0 AND p.expiryTime > %lli",
        planetID, GetFileTimeNow()))
    {
        _log(DATABASE__ERROR, "Error in GetExtractorsForPlanet Query: %s", res.error.c_str());
        return;
    }

    _log(DATABASE__RESULTS, "GetExtractorsForPlanet returned %lu items", res.GetRowCount());
}

void PlanetDB::GetCommandPinsForPlanet(uint32 planetID, DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT pinID, typeID, charID, latitude, longitude"
        " FROM piCCPin WHERE planetID = %u", planetID))
        _log(DATABASE__ERROR, "Error in GetCommandPinsForPlanet Query: %s", res.error.c_str());
}

void PlanetDB::GetNetworkForOwner(uint32 planetID, uint32 charID, DBQueryResult& pins, DBQueryResult& links)
{
    if (!sDatabase.RunQuery(pins,
        "SELECT p.pinID, p.typeID, p.ownerID, p.latitude, p.longitude"
        " FROM piPins AS p"
        " JOIN piCCPin AS cc ON cc.pinID = p.ccPinID"
        " WHERE cc.planetID = %u AND cc.charID = %u", planetID, charID))
        _log(DATABASE__ERROR, "Error in GetNetworkForOwner Query: %s", pins.error.c_str());
    if (!sDatabase.RunQuery(links,
        "SELECT LEAST(l.endpoint1, l.endpoint2), GREATEST(l.endpoint1, l.endpoint2)"
        " FROM piLinks AS l"
        " JOIN piCCPin AS cc ON cc.pinID = l.ccPinID"
        " WHERE cc.planetID = %u AND cc.charID = %u", planetID, charID))
        _log(DATABASE__ERROR, "Error in GetNetworkForOwner Query: %s", links.error.c_str());
}

uint32 PlanetDB::GetLaunchOwner(uint32 itemID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT charID FROM piLaunches WHERE itemID = %u", itemID)) {
        _log(DATABASE__ERROR, "Error in GetLaunchOwner query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

uint32 PlanetDB::CountPlanetsForChar(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM piPlanets WHERE charID = %u", charID)) {
        _log(DATABASE__ERROR, "Error in CountPlanetsForChar query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

void PlanetDB::GetSchematicPins(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT schematicID, pinTypeID FROM piPinMap"))
        _log(DATABASE__ERROR, "Error in GetSchematicPins Query: %s", res.error.c_str());
}

void PlanetDB::GetAttributeDefaults(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT attributeID, defaultValue FROM dgmAttributeTypes"))
        _log(DATABASE__ERROR, "Error in GetAttributeDefaults Query: %s", res.error.c_str());
}

void PlanetDB::GetPITypeAttributes(DBQueryResult& res)
{
    // pins, links, extractor heads, customs offices and gantries, and the planetary resources and commodities.
    //  the import tax multiplier is on other commodities too
    if (!sDatabase.RunQuery(res,
        "SELECT dta.typeID, dta.attributeID, COALESCE(dta.valueFloat, dta.valueInt)"
        " FROM dgmTypeAttributes AS dta"
        " JOIN invTypes AS t ON t.typeID = dta.typeID"
        " JOIN invGroups AS g ON g.groupID = t.groupID"
        " WHERE g.categoryID IN (%u, %u, %u)"
        "  OR t.groupID IN (%u, %u)"
        "  OR dta.attributeID IN (%u, %u)",
        EVEDB::invCategories::PlanetaryInteraction, EVEDB::invCategories::PlanetaryResources, EVEDB::invCategories::PlanetaryCommodities,
        EVEDB::invGroups::Orbital_Infrastructure, EVEDB::invGroups::Orbital_Construction_Platform,
        AttrImportTaxMultiplier, AttrExportTaxMultiplier))
        _log(DATABASE__ERROR, "Error in GetPITypeAttributes Query: %s", res.error.c_str());
}

bool PlanetDB::LoadColony(uint32 charID, uint32 planetID, PI_CCPin& ccPin)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT pinID, level, lastSimTime FROM piCCPin WHERE charID = %u AND planetID = %u", charID, planetID)) {
        _log(DATABASE__ERROR, "Error in LoadColony Query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;

    ccPin.ccPinID = row.GetUInt(0);
    ccPin.level = row.GetInt(1);
    ccPin.currentSimTime = row.GetInt64(2);
    ccPin.ownerID = charID;
    ccPin.planetID = planetID;

    if (!sDatabase.RunQuery(res,
        "SELECT pinID, typeID, ownerID, state, latitude, longitude, schematicID, programType, headRadius,"
        " launchTime, cycleTime, expiryTime, installTime, lastRunTime, hasReceivedInputs, receivedInputsLastCycle, qtyPerCycle"
        " FROM piPins WHERE ccPinID = %u", ccPin.ccPinID)) {
        _log(DATABASE__ERROR, "Error in LoadColony Query: %s", res.error.c_str());
        return false;
    }
    while (res.GetRow(row)) {
        PI_Pin pin = PI_Pin();
            pin.typeID                  = row.GetUInt(1);
            pin.ownerID                 = row.GetUInt(2);
            pin.state                   = row.GetInt(3);
            pin.latitude                = row.GetDouble(4);
            pin.longitude               = row.GetDouble(5);
            pin.schematicID             = row.GetUInt(6);
            pin.programType             = row.GetUInt(7);
            pin.headRadius              = row.GetDouble(8);
            pin.lastLaunchTime          = row.GetInt64(9);
            pin.cycleTime               = row.GetInt64(10);
            pin.expiryTime              = row.GetInt64(11);
            pin.installTime             = row.GetInt64(12);
            pin.lastRunTime             = row.GetInt64(13);
            pin.hasReceivedInputs       = row.GetBool(14);
            pin.receivedInputsLastCycle = row.GetBool(15);
            pin.qtyPerCycle             = row.GetUInt(16);
        ccPin.pins[row.GetUInt(0)] = pin;
    }

    if (!sDatabase.RunQuery(res, "SELECT pinID, typeID, itemQty FROM piPinContents WHERE ccPinID = %u", ccPin.ccPinID))
        _log(DATABASE__ERROR, "Error in LoadColony Query: %s", res.error.c_str());
    while (res.GetRow(row)) {
        std::map<uint32, PI_Pin>::iterator itr = ccPin.pins.find(row.GetUInt(0));
        if ((itr != ccPin.pins.end()) and (row.GetInt(2) > 0))
            itr->second.contents[row.GetUInt(1)] = row.GetUInt(2);
    }

    if (!sDatabase.RunQuery(res, "SELECT ecuID, headID, typeID, latitude, longitude FROM piECUHeads WHERE ccPinID = %u", ccPin.ccPinID))
        _log(DATABASE__ERROR, "Error in LoadColony Query: %s", res.error.c_str());
    while (res.GetRow(row)) {
        std::map<uint32, PI_Pin>::iterator itr = ccPin.pins.find(row.GetUInt(0));
        if (itr == ccPin.pins.end())
            continue;
        PI_Heads head = PI_Heads();
            head.ecuPinID = row.GetUInt(0);
            head.typeID = row.GetUInt(2);
            head.latitude = row.GetDouble(3);
            head.longitude = row.GetDouble(4);
        itr->second.heads[row.GetUInt(1)] = head;
    }

    if (!sDatabase.RunQuery(res, "SELECT linkID, level, typeID, endpoint1, endpoint2 FROM piLinks WHERE ccPinID = %u", ccPin.ccPinID))
        _log(DATABASE__ERROR, "Error in LoadColony Query: %s", res.error.c_str());
    while (res.GetRow(row)) {
        PI_Link link = PI_Link();
            link.linkID = row.GetUInt(0);
            link.level = row.GetUInt(1);
            link.typeID = row.GetUInt(2);
            link.endpoint1 = row.GetUInt(3);
            link.endpoint2 = row.GetUInt(4);
        if (link.typeID == 0)
            link.typeID = 2280;     // the only link type
        if (link.endpoint1 > link.endpoint2)
            std::swap(link.endpoint1, link.endpoint2);
        ccPin.links[std::make_pair(link.endpoint1, link.endpoint2)] = link;
    }

    if (!sDatabase.RunQuery(res, "SELECT routeID, path, typeID, itemQty FROM piRoutes WHERE ccPinID = %u ORDER BY routeID", ccPin.ccPinID))
        _log(DATABASE__ERROR, "Error in LoadColony Query: %s", res.error.c_str());
    while (res.GetRow(row)) {
        PI_Route route = PI_Route();
            route.routeID = row.GetUInt(0);
            route.commodityTypeID = row.GetUInt(2);
            route.commodityQuantity = row.GetUInt(3);
        // path is pinIDs separated by ':'
        std::string path = row.GetText(1);
        size_t start = 0;
        while (start < path.size()) {
            size_t end = path.find(':', start);
            if (end == std::string::npos)
                end = path.size();
            if (end > start)
                route.path.push_back(strtoul(path.substr(start, end - start).c_str(), nullptr, 10));
            start = end + 1;
        }
        ccPin.routes[route.routeID] = route;
    }

    return true;
}

void PlanetDB::SaveColony(PI_CCPin& ccPin, uint32 systemID, uint16 planetTypeID)
{
    if (ccPin.ccPinID == 0)
        return;

    DBerror err;
    uint32 ccPinID = ccPin.ccPinID;
    std::map<uint32, PI_Pin>::const_iterator ccItr = ccPin.pins.find(ccPinID);
    if (ccItr != ccPin.pins.end()) {
        if (!sDatabase.RunQuery(err,
            "INSERT INTO piCCPin (pinID, charID, planetID, typeID, latitude, longitude, level, lastSimTime)"
            " VALUES (%u, %u, %u, %u, %.17g, %.17g, %u, %lli)"
            " ON DUPLICATE KEY UPDATE level = VALUES(level), lastSimTime = VALUES(lastSimTime)",
            ccPinID, ccPin.ownerID, ccPin.planetID, ccItr->second.typeID, ccItr->second.latitude, ccItr->second.longitude,
            ccPin.level, ccPin.currentSimTime))
            _log(DATABASE__ERROR, "SaveColony - unable to save command center %u: %s", ccPinID, err.c_str());
    } else {
        sDatabase.RunQuery(err, "UPDATE piCCPin SET level = %u, lastSimTime = %lli WHERE pinID = %u", ccPin.level, ccPin.currentSimTime, ccPinID);
    }
    if (!sDatabase.RunQuery(err,
        "INSERT INTO piPlanets (charID, solarSystemID, planetID, typeID, numberOfPins, ccPinID)"
        " VALUES (%u, %u, %u, %u, %u, %u)"
        " ON DUPLICATE KEY UPDATE numberOfPins = VALUES(numberOfPins), ccPinID = VALUES(ccPinID)",
        ccPin.ownerID, systemID, ccPin.planetID, planetTypeID, (uint32)ccPin.pins.size(), ccPinID))
        _log(DATABASE__ERROR, "SaveColony - unable to save planet for %u: %s", ccPin.ownerID, err.c_str());

    // pins:  removed pins go, the rest is upserted
    std::ostringstream pinIDs, Inserts;
    Inserts << std::setprecision(17);
    Inserts << "INSERT INTO piPins";
    Inserts << " (ccPinID, pinID, typeID, ownerID, state, level, latitude, longitude,";
    Inserts << " isCommandCenter, isLaunchable, isProcess, isStorage, isECU, hasReceivedInputs, receivedInputsLastCycle,";
    Inserts << " schematicID, programType, cycleTime, launchTime, expiryTime, installTime, lastRunTime, headRadius, qtyPerCycle)";
    bool first = true;
    for (auto& cur : ccPin.pins) {
        const PI_Pin& pin = cur.second;
        Inserts << (first ? " VALUES " : ", ");
        pinIDs << (first ? "" : ", ") << cur.first;
        first = false;
        Inserts << "(" << ccPinID << ", " << cur.first << ", " << pin.typeID << ", " << pin.ownerID << ", " << (int)pin.state << ", ";
        Inserts << (pin.kind == PI::Pin::Kind::CommandCenter ? (int)ccPin.level : 0) << ", " << pin.latitude << ", " << pin.longitude << ", ";
        Inserts << (pin.kind == PI::Pin::Kind::CommandCenter ? 1 : 0) << ", ";
        Inserts << ((pin.kind == PI::Pin::Kind::CommandCenter) or (pin.kind == PI::Pin::Kind::Spaceport) ? 1 : 0) << ", ";
        Inserts << (pin.kind == PI::Pin::Kind::Process ? 1 : 0) << ", ";
        Inserts << ((pin.kind == PI::Pin::Kind::CommandCenter) or (pin.kind == PI::Pin::Kind::Spaceport) or (pin.kind == PI::Pin::Kind::Storage) ? 1 : 0) << ", ";
        Inserts << (pin.kind == PI::Pin::Kind::ECU ? 1 : 0) << ", " << (pin.hasReceivedInputs ? 1 : 0) << ", " << (pin.receivedInputsLastCycle ? 1 : 0) << ", ";
        Inserts << pin.schematicID << ", " << pin.programType << ", " << pin.cycleTime << ", " << pin.lastLaunchTime << ", ";
        Inserts << pin.expiryTime << ", " << pin.installTime << ", " << pin.lastRunTime << ", " << pin.headRadius << ", " << pin.qtyPerCycle << ")";
    }
    if (first) {
        sDatabase.RunQuery(err, "DELETE FROM piPins WHERE ccPinID = %u", ccPinID);
    } else {
        sDatabase.RunQuery(err, "DELETE FROM piPins WHERE ccPinID = %u AND pinID NOT IN (%s)", ccPinID, pinIDs.str().c_str());
        Inserts << " ON DUPLICATE KEY UPDATE typeID = VALUES(typeID), ownerID = VALUES(ownerID), state = VALUES(state), level = VALUES(level),";
        Inserts << " latitude = VALUES(latitude), longitude = VALUES(longitude), isCommandCenter = VALUES(isCommandCenter), isLaunchable = VALUES(isLaunchable),";
        Inserts << " isProcess = VALUES(isProcess), isStorage = VALUES(isStorage), isECU = VALUES(isECU), hasReceivedInputs = VALUES(hasReceivedInputs),";
        Inserts << " receivedInputsLastCycle = VALUES(receivedInputsLastCycle), schematicID = VALUES(schematicID), programType = VALUES(programType),";
        Inserts << " cycleTime = VALUES(cycleTime), launchTime = VALUES(launchTime), expiryTime = VALUES(expiryTime), installTime = VALUES(installTime),";
        Inserts << " lastRunTime = VALUES(lastRunTime), headRadius = VALUES(headRadius), qtyPerCycle = VALUES(qtyPerCycle)";
        if (!sDatabase.RunQuery(err, "%s", Inserts.str().c_str()))
            _log(DATABASE__ERROR, "SaveColony - unable to save pins: %s", err.c_str());
    }

    // contents
    sDatabase.RunQuery(err, "DELETE FROM piPinContents WHERE ccPinID = %u", ccPinID);
    std::ostringstream contents;
    contents << "INSERT INTO piPinContents (ccPinID, pinID, typeID, itemQty)";
    first = true;
    for (auto& cur : ccPin.pins)
        for (auto cont : cur.second.contents) {
            if (cont.second < 1)
                continue;
            contents << (first ? " VALUES " : ", ");
            first = false;
            contents << "(" << ccPinID << ", " << cur.first << ", " << cont.first << ", " << cont.second << ")";
        }
    if (!first)
        if (!sDatabase.RunQuery(err, "%s", contents.str().c_str()))
            _log(DATABASE__ERROR, "SaveColony - unable to save contents: %s", err.c_str());

    // heads
    sDatabase.RunQuery(err, "DELETE FROM piECUHeads WHERE ccPinID = %u", ccPinID);
    std::ostringstream heads;
    heads << std::setprecision(17);
    heads << "INSERT INTO piECUHeads (ccPinID, ownerID, ecuID, headID, typeID, latitude, longitude)";
    first = true;
    for (auto& cur : ccPin.pins)
        for (auto head : cur.second.heads) {
            heads << (first ? " VALUES " : ", ");
            first = false;
            heads << "(" << ccPinID << ", " << cur.second.ownerID << ", " << cur.first << ", " << head.first << ", " << head.second.typeID;
            heads << ", " << head.second.latitude << ", " << head.second.longitude << ")";
        }
    if (!first)
        if (!sDatabase.RunQuery(err, "%s", heads.str().c_str()))
            _log(DATABASE__ERROR, "SaveColony - unable to save heads: %s", err.c_str());

    // links
    sDatabase.RunQuery(err, "DELETE FROM piLinks WHERE ccPinID = %u", ccPinID);
    std::ostringstream links;
    links << "INSERT INTO piLinks (ccPinID, linkID, level, typeID, state, endpoint1, endpoint2)";
    first = true;
    for (auto& cur : ccPin.links) {
        links << (first ? " VALUES " : ", ");
        first = false;
        links << "(" << ccPinID << ", " << cur.second.linkID << ", " << (int)cur.second.level << ", " << cur.second.typeID << ", 0, ";
        links << cur.second.endpoint1 << ", " << cur.second.endpoint2 << ")";
    }
    if (!first)
        if (!sDatabase.RunQuery(err, "%s", links.str().c_str()))
            _log(DATABASE__ERROR, "SaveColony - unable to save links: %s", err.c_str());

    // routes
    sDatabase.RunQuery(err, "DELETE FROM piRoutes WHERE ccPinID = %u", ccPinID);
    std::ostringstream routes;
    routes << "INSERT INTO piRoutes (ccPinID, routeID, srcPinID, destPinID, state, priority, path, typeID, itemQty)";
    first = true;
    for (auto& cur : ccPin.routes) {
        if (cur.second.path.empty())
            continue;
        std::string path;
        for (auto id : cur.second.path) {
            if (!path.empty())
                path += ":";
            path += std::to_string(id);
        }
        routes << (first ? " VALUES " : ", ");
        first = false;
        routes << "(" << ccPinID << ", " << cur.first << ", " << cur.second.path.front() << ", " << cur.second.path.back() << ", 0, 0, '";
        routes << path << "', " << cur.second.commodityTypeID << ", " << cur.second.commodityQuantity << ")";
    }
    if (!first)
        if (!sDatabase.RunQuery(err, "%s", routes.str().c_str()))
            _log(DATABASE__ERROR, "SaveColony - unable to save routes: %s", err.c_str());
}

uint32 PlanetDB::InsertRoute(uint32 ccPinID, const PI_Route& route)
{
    std::string path;
    for (auto id : route.path) {
        if (!path.empty())
            path += ":";
        path += std::to_string(id);
    }
    DBerror err;
    uint32 routeID = 0;
    if (!sDatabase.RunQueryLID(err, routeID,
        "INSERT INTO piRoutes (ccPinID, srcPinID, destPinID, state, priority, path, typeID, itemQty)"
        " VALUES (%u, %u, %u, 0, 0, '%s', %u, %u)",
        ccPinID, (route.path.empty() ? 0 : route.path.front()), (route.path.empty() ? 0 : route.path.back()),
        path.c_str(), route.commodityTypeID, route.commodityQuantity))
        _log(DATABASE__ERROR, "InsertRoute - Unable to save route: %s", err.GetError());
    return routeID;
}

void PlanetDB::SaveLaunch(uint32 contID, uint32 charID, uint32 systemID, uint32 planetID, GPoint& pos)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "INSERT INTO piLaunches (itemID, status, charID, solarSystemID, planetID, launchTime, x, y, z) "
        " VALUES (%u, 0, %u, %u, %u, %lli, %f, %f, %f)",
                           contID, charID, systemID, planetID, GetFileTimeNow(), pos.x, pos.y, pos.z))
        {
            _log(DATABASE__ERROR, "SaveLaunch - Unable to save Launch: %s", err.GetError());
        }
}

void PlanetDB::DeleteLaunch(uint32 contID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM piLaunches WHERE itemID = %u", contID);
}

int64 PlanetDB::GetLaunchTime(uint32 itemID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT launchTime FROM piLaunches WHERE itemID = %u", itemID)) {
        _log(DATABASE__ERROR, "Error in GetLaunchTime query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetInt64(0);
}

uint32 PlanetDB::OnLaunchContainerGone(uint32 itemID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT charID, status FROM piLaunches WHERE itemID = %u", itemID)) {
        _log(DATABASE__ERROR, "Error in OnLaunchContainerGone query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    uint32 charID = row.GetUInt(0);
    // a container that burned up stays in the journal (the client shows it burned up) until its owner removes it
    if (row.GetInt(1) != PI::Cargo::Deleted) {
        DBerror err;
        sDatabase.RunQuery(err, "DELETE FROM piLaunches WHERE itemID = %u", itemID);
    }
    return charID;
}

void PlanetDB::MarkCustomsOfficeSpawned(uint32 planetID)
{
    DBerror err;
    sDatabase.RunQuery(err, "INSERT IGNORE INTO piCustomsOfficeSpawn (planetID, spawnTime) VALUES (%u, %lli)", planetID, GetFileTimeNow());
}

void PlanetDB::GetSpawnedCustomsOffices(const std::vector<uint32>& planetIDs, std::set<uint32>& into)
{
    if (planetIDs.empty())
        return;
    std::string list;
    for (auto cur : planetIDs) {
        if (!list.empty())
            list += ",";
        list += std::to_string(cur);
    }
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT planetID FROM piCustomsOfficeSpawn WHERE planetID IN (%s)", list.c_str())) {
        _log(DATABASE__ERROR, "Error in GetSpawnedCustomsOffices query: %s", res.error.c_str());
        // don't spawn anything on a db error
        for (auto cur : planetIDs)
            into.insert(cur);
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into.insert(row.GetUInt(0));
}

void PlanetDB::DeleteColony(uint32 ccPinID, uint32 planetID, uint32 charID)
{
    DBerror err;
    // the pin and link items of the colony
    sDatabase.RunQuery(err, "DELETE FROM entity WHERE locationID = %u AND ownerID = %u AND flag = %u", planetID, charID, flagPlanetSurface);
    sDatabase.RunQuery(err, "DELETE FROM piCCPin WHERE pinID = %u", ccPinID);
    sDatabase.RunQuery(err, "DELETE FROM piPins WHERE ccPinID = %u", ccPinID);
    sDatabase.RunQuery(err, "DELETE FROM piLinks WHERE ccPinID = %u", ccPinID);
    sDatabase.RunQuery(err, "DELETE FROM piRoutes WHERE ccPinID = %u", ccPinID);
    sDatabase.RunQuery(err, "DELETE FROM piPlanets WHERE ccPinID = %u", ccPinID);
    sDatabase.RunQuery(err, "DELETE FROM piECUHeads WHERE ccPinID = %u", ccPinID);
    sDatabase.RunQuery(err, "DELETE FROM piPinContents WHERE ccPinID = %u", ccPinID);
}
