/**
 * @name StationDB.cpp
 *      database methods for station data
 *
 * @author Allan
 * @date 14 December 2017
 *
 */


#include "eve-server.h"

#include "station/StationDB.h"
#include "station/StationDataMgr.h"


uint32 StationDB::CreateOffice(ItemData& idata, OfficeData& odata)
{
    uint32 uid = 0;
    DBerror err;
    if (!sDatabase.RunQueryLID(err, uid,
        "INSERT INTO staOffices (name, corporationID, stationID, officeFolderID, flag, solarSystemID, typeID, stationTypeID, lockDown, rentalFee, expiryDateTime)"
        " VALUES"
        " ('%s',%u,%u,%u,%u,%u, 27, %u,%u,%u,%lli)",
        odata.name.c_str(), odata.corporationID, odata.stationID, odata.folderID, idata.flag, stDataMgr.GetStationSystemID(odata.stationID), odata.typeID, odata.lockDown, odata.rentalFee, odata.expiryTime)
    )
        codelog(DATABASE__ERROR, "Error in CreateOffice query: %s", err.c_str());

    return (odata.officeID = uid);
}

void StationDB::UpdateOfficeData(OfficeData& data)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE staOffices SET lockDown = %u, rentalFee = %lli, expiryDateTime = %lli, impounded = %u, publiclyAvailable = %u, billID = %u"
        " WHERE itemID = %u", (data.lockDown ? 1 : 0), data.rentalFee, data.expiryTime, (data.impounded ? 1 : 0), (data.publiclyAvailable ? 1 : 0),
        data.billID, data.officeID);
}

void StationDB::SetOfficeImpounded(uint32 officeID, bool impounded)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE staOffices SET impounded = %u, billID = 0 WHERE itemID = %u", (impounded ? 1 : 0), officeID);
}

void StationDB::SetOfficeLease(uint32 officeID, int64 expiryDateTime, uint32 billID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE staOffices SET expiryDateTime = %lli, billID = %u WHERE itemID = %u", expiryDateTime, billID, officeID);
}

void StationDB::SetOfficePublic(uint32 officeID, bool publiclyAvailable)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE staOffices SET publiclyAvailable = %u WHERE itemID = %u", (publiclyAvailable ? 1 : 0), officeID);
}

uint32 StationDB::GetCorpOffice(uint32 corpID, uint32 stationID, bool impounded)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID FROM staOffices WHERE corporationID = %u AND stationID = %u AND impounded = %u",
                corpID, stationID, (impounded ? 1 : 0)))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetUInt(0);
    return 0;
}

void StationDB::GetOfficesWithoutBill(std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID FROM staOffices WHERE impounded = 0 AND billID = 0 AND publiclyAvailable = 1")) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

void StationDB::GetLapsedOffices(int64 now, std::vector<uint32>& into)
{
    // offices past their lease with no bill to extend it (no longer publicly available)
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID FROM staOffices WHERE impounded = 0 AND billID = 0 AND expiryDateTime > 0 AND expiryDateTime <= %lli", now)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

void StationDB::GetOfficesWithBillDue(int64 before, std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT o.itemID FROM staOffices o JOIN bills b ON b.billID = o.billID"
        " WHERE o.impounded = 0 AND b.paid = 0 AND b.dueDateTime <= %lli", before))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

uint32 StationDB::CountImpoundedItems(uint32 officeID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(itemID) FROM entity WHERE locationID = %u AND flag = %u", officeID, flagImpounded)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetUInt(0);
    return 0;
}

void StationDB::GetCorpOffices(uint32 corpID, std::map<uint32, uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID, stationID FROM staOffices WHERE corporationID = %u", corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into[row.GetUInt(0)] = row.GetUInt(1);
}

void StationDB::DeleteOffice(uint32 officeID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM staOffices WHERE itemID = %u", officeID);
}

int32 StationDB::GetOfficeCount(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(itemID) FROM staOffices WHERE corporationID = %u AND impounded = 0", corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Unable to find corporation's office data (%u)", corpID);
        return 0;
    }
    return row.GetInt(0);
}

PyRep* StationDB::GetOffices(uint32 stationID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        " corporationID, itemID, officeFolderID"
        " FROM staOffices"
        " WHERE stationID = %u AND impounded = 0", stationID)
    )
        codelog(DATABASE__ERROR, "Error in GetOffices query: %s", res.error.c_str());

    return DBResultToCRowset(res);
}

bool StationDB::GetOfficeData(uint32 officeID, OfficeData& odata)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        " corporationID, stationID, typeID, lockDown, rentalFee, expiryDateTime, officeFolderID, impounded, publiclyAvailable, billID"
        " FROM staOffices"
        " WHERE itemID = %u", officeID)
    ) {
        codelog(DATABASE__ERROR, "Error in GetOfficeData query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return false;

    odata.officeID      = officeID;
    odata.corporationID = row.GetInt(0);
    odata.stationID     = row.GetInt(1);
    odata.typeID        = row.GetInt(2);
    odata.lockDown      = row.GetBool(3);
    odata.rentalFee     = row.GetInt64(4);
    odata.expiryTime    = row.GetInt64(5);
    odata.folderID      = row.GetInt(6);
    odata.impounded     = (row.GetInt(7) != 0);
    odata.publiclyAvailable = (row.GetInt(8) != 0);
    odata.billID        = row.GetUInt(9);
    return true;
}


void StationDB::GetStationOfficeData(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT"
        " itemID, corporationID, stationID, typeID, lockDown, rentalFee, expiryDateTime, officeFolderID"
        " FROM staOffices WHERE impounded = 0")
    )
        codelog(DATABASE__ERROR, "Error in GetStationOfficeData query: %s", res.error.c_str());
}

PyRep* StationDB::GetStationOfficeIDs(uint32 locationID, uint32 corpID, const char* key)
{
    DBQueryResult res;
    if (sDataMgr.IsStation(locationID)) {
        sDatabase.RunQuery(res, "SELECT itemID AS officeID, stationID, officeFolderID"
        " FROM staOffices WHERE stationID = %u AND impounded = 0", locationID);
    } else if (IsOfficeFolder(locationID)) {
        sDatabase.RunQuery(res, "SELECT itemID AS officeID, stationID, officeFolderID"
        " FROM staOffices WHERE officeFolderID = %u AND impounded = 0",
            locationID - STATION_OFFICE_OFFSET);
    } else if (IsOfficeID(locationID)) {
        sDatabase.RunQuery(res, "SELECT itemID AS officeID, stationID, officeFolderID"
        " FROM staOffices WHERE itemID = %u", locationID);
    } else {
        _log(CORP__DB_ERROR, "StationDB::GetStationOfficeIDs got invalid locationID %u", locationID);
    }

    DBResultRow row;
    if (res.GetRow(row))
        return DBRowToRow(row);

    return nullptr;

    //return DBResultToCIndexedRowset(res, key);
    //return DBResultToIndexRowset(res, key);
    //return DBResultToCRowset(res);
    //return DBResultToRowList(res);
    //return DBResultToRowset(res);
    //return DBResultToPackedRowDict(res, key);
}

void StationDB::GetStationData(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  s.stationID, s.x, s.y, s.z, st.dockOrientationX, st.dockOrientationY, st.dockOrientationZ,"
        "  s.dockingCostPerVolume, s.maxShipVolumeDockable, s.officeSlots, s.officeRentalCost,"
        "  s.operationID, s.stationTypeID, s.corporationID, s.stationName,"
        "  s.reprocessingStationsTake, s.reprocessingEfficiency, s.reprocessingHangarFlag, st.conquerable, st.hangarGraphicID,"
        "  m.orbitID, m.radius, m.security, o.description, o.descriptionID, t.graphicID, s.solarSystemID, s.constellationID, s.regionID,"
        "  st.dockEntryX, st.dockEntryY, st.dockEntryZ, s.upgradeLevel"
        " FROM staStations AS s"
        "  LEFT JOIN staStationTypes AS st USING (stationTypeID)"
        "  LEFT JOIN invTypes AS t ON t.typeID = s.stationTypeID"
        "  LEFT JOIN staOperations AS o ON o.operationID = s.operationID"
        "  LEFT JOIN mapDenormalize AS m ON m.itemID = s.stationID")
        )
        codelog(DATABASE__ERROR, "Error in GetStationData query: %s", res.error.c_str());
}

void StationDB::GetStationBaseData(DBQueryResult& res, uint32 typeID)
{
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  st.dockOrientationX, st.dockOrientationY, st.dockOrientationZ,"
        "  st.conquerable, st.hangarGraphicID,"
        "  o.description, o.descriptionID, t.graphicID,"
        "  st.dockEntryX, st.dockEntryY, st.dockEntryZ, o.operationID, o.operationName"
        " FROM staStationTypes AS st"
        "  LEFT JOIN invTypes AS t ON t.typeID = st.stationTypeID"
        "  LEFT JOIN staOperations AS o ON o.operationName = t.typeName"
        " COLLATE utf8_general_ci"
        " WHERE st.stationTypeID=%u", typeID)
        )
        codelog(DATABASE__ERROR, "Error in GetStationData query: %s", res.error.c_str());
}

void StationDB::GetStationSystem(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT stationID, solarSystemID FROM staStations"))
        codelog(DATABASE__ERROR, "Error in GetStationSystem query: %s", res.error.c_str());
}

void StationDB::GetStationRegion(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT stationID, regionID FROM staStations"))
        codelog(DATABASE__ERROR, "Error in GetStationRegion query: %s", res.error.c_str());
}

void StationDB::GetStationConstellation(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT stationID, constellationID FROM staStations"))
        codelog(DATABASE__ERROR, "Error in GetStationConstellation query: %s", res.error.c_str());
}

void StationDB::GetOperationServiceIDs(DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT operationID, serviceID FROM staOperationServices");
}

void StationDB::LoadOffices(OwnerData& od, std::vector< uint32 >& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID FROM staOffices WHERE stationID = %u", od.locID)) {
        codelog(DATABASE__ERROR, "Error in LoadOffices query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetInt(0));
}

void StationDB::CreateOutpost(StationData &data)
{
    // Add data to mapDenormalize table
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "INSERT INTO mapDenormalize (itemID,typeID,groupID,solarSystemID,constellationID,regionID,orbitID,x,y,z,radius,itemName,itemNameID,security)"
        " VALUES"
        " (%u,%u,%u,%u,%u,%u,%u,%f,%f,%f,%f,'%s',0,%f)",
        data.stationID,            //itemID
        data.typeID,               //typeID
        EVEDB::invGroups::Station, //groupID
        data.systemID,             //solarSystemID
        data.constellationID,      //constellationID
        data.regionID,             //regionID
        data.orbitID,              //orbitID
        data.position.x,           //x
        data.position.y,           //y
        data.position.z,           //z
        data.radius,               //radius
        data.name.c_str(),         //itemName
        data.security              //security
    )) {
        codelog(DATABASE__ERROR, "Error in CreateStation query: %s", err.c_str());
    }

    if (!sDatabase.RunQuery(err,
        "INSERT INTO staStations (stationID,security,dockingCostPerVolume,maxShipVolumeDockable,officeSlots,officeRentalCost,operationID,stationTypeID,corporationID,solarSystemID,constellationID,regionID,stationName,x,y,z,reprocessingEfficiency,reprocessingStationsTake,reprocessingHangarFlag)"
        " VALUES"
        " (%u,%f,%f,%u,%u,%u,%u,%u,%u,%u,%u,%u,'%s',%f,%f,%f,%f,%f,%u)",
        data.stationID,                //itemID
        data.security,                 //security
        data.dockingCostPerVolume,     //dockingCostPerVolume
        data.maxShipVolumeDockable,    //maxShipVolumeDockable
        data.officeSlots,              //officeSlots
        data.officeRentalFee,          //officeRentalCost
        data.operationID,              //operationID
        data.typeID,                   //stationTypeID
        data.corporationID,            //corporationID
        data.systemID,                 //solarSystemID
        data.constellationID,          //constellationID
        data.regionID,                 //regionID
        data.name.c_str(),             //stationName
        data.position.x,               //x
        data.position.y,               //y
        data.position.z,               //z
        data.reprocessingEfficiency,   //reprocessingEfficiency
        data.reprocessingStationsTake, //reprocessingStationsTake
        flagHangar                     //reprocessingHangarFlag
    )) {
        codelog(DATABASE__ERROR, "Error in CreateStation query: %s", err.c_str());
    }
}

// This function gets the next available outpostID from the staStation table for use during outpost construction
uint32 StationDB::GetNewOutpostID()
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT stationID FROM staStations WHERE stationID>61000000 ORDER BY stationID DESC LIMIT 1")) {
        codelog(DATABASE__ERROR, "Error in GetNewOutpostID query: %s", res.error.c_str());
        return 0;
    }

    uint32 newID(0);
    DBResultRow row;
    while (res.GetRow(row)) {
        newID = row.GetUInt(0) + 1;
    }

    // If there is no existing outpost in the db, we will use the base value
    if (newID == 0) {
        return 61000000;
    }

    return newID;
}

void StationDB::GetStationServiceStates(uint32 stationID, DBQueryResult& res)
{
    // Send a rowset of the following values:
    /*
    [PyString "solarSystemID"] = [PyInt 30001984]
    [PyString "stationID"] = [PyInt 61000012]
    [PyString "serviceID"] = [PyInt 65536]
    [PyString "stationServiceItemID"] = [PyIntegerVar 318021026]
    [PyString "isEnabled"]
    */

    sDatabase.RunQuery(res, "SELECT solarSystemID, stationID, ( "
    "    CASE  "
    "        WHEN typeID=28155 THEN 65536 "
    "        WHEN typeID=28156 THEN 16 "
    "        WHEN typeID=28157 THEN 8192 "
    "        WHEN typeID=28158 THEN 512 "
    "        WHEN typeID=28159 THEN 4096 "
    "        WHEN typeID=28166 THEN 16384 "
    "        ELSE 0 "
    "    END) AS serviceID, "
    "IFNULL(itemID, 0) AS stationServiceItemID, IF(flag=144 ,1, 0) AS isEnabled  "
    "FROM entity e "
    "  INNER JOIN staStations s ON s.x = e.x AND s.y = e.y AND s.z = e.z "
    "  WHERE s.stationID=%u AND typeID IN (28155,28156,28157,28158,28159,28166) ", stationID);
    }

void StationDB::GetStationServiceIdentifiers(DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT serviceID, serviceName, serviceNameID "
    "FROM staServices ");
}

void StationDB::GetStationManagementServiceCostModifiers(uint32 stationID, DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT serviceID, discountPerGoodStandingPoint, "
    "surchargePerBadStandingPoint "
    "FROM staOutpostServiceConfig WHERE stationID=%u", stationID);
}

void StationDB::GetStationServiceAccessRule(uint32 stationID, uint32 serviceID, DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT serviceID, serviceName, minimumStanding, minimumCharSecurity, "
    "maximumCharSecurity, minimumCorpSecurity, maximumCorpSecurity "
    "FROM staOutpostServiceConfig "
    "INNER JOIN staServices USING (serviceID) "
    "WHERE stationID=%u AND serviceID=%u", stationID, serviceID);
}

void StationDB::GetStationDetails(uint32 stationID, DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT stationName, stationID, orbitID, description, "
    "s.security as security, dockingCostPerVolume, officeRentalCost, reprocessingStationsTake, "
    "reprocessingHangarFlag, corporationID as ownerID, maxShipVolumeDockable, upgradeLevel, exitTime, standingOwnerID "
    "FROM staStations s "
    "INNER JOIN mapDenormalize m ON s.stationID = m.itemID "
    "INNER JOIN staOperations USING (operationID) "
    "WHERE stationID=%u", stationID);
}

void StationDB::GetRentableItems(uint32 stationID, DBQueryResult& res)
{
    // base_stationmanagement offices tab:  typeID (typeOfficeFolder), number, rentedToID, publiclyAvailable, startDate,
    //  rentPeriodInDays, periodCost, balanceDueDate.  number is the officeID (UpdateStationManagementSettings sends it back)
    sDatabase.RunQuery(res, "SELECT 26 AS typeID, itemID AS number, corporationID AS rentedToID, publiclyAvailable,"
    " (expiryDateTime - %lli) AS startDate, 30 AS rentPeriodInDays, rentalFee AS periodCost, expiryDateTime AS balanceDueDate"
    " FROM staOffices WHERE stationID = %u AND impounded = 0", (int64)(30 * EvE::Time::Day), stationID);
}

uint32 StationDB::GetStandingOwner(uint32 stationID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT IFNULL(standingOwnerID, 0) FROM staStations WHERE stationID = %u", stationID)) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetUInt(0);
    return 0;
}

void StationDB::SetServiceAccessRule(uint32 stationID, uint32 serviceID, double minimumStanding, double minimumCharSecurity,
                                     double maximumCharSecurity, double minimumCorpSecurity, double maximumCorpSecurity)
{
    // staOutpostServiceConfig has no key:  one row per station and service, cost modifiers kept
    DBQueryResult res;
    double discount(0), surcharge(0);
    if (sDatabase.RunQuery(res, "SELECT discountPerGoodStandingPoint, surchargePerBadStandingPoint FROM staOutpostServiceConfig"
                " WHERE stationID = %u AND serviceID = %u", stationID, serviceID))
    {
        DBResultRow row;
        if (res.GetRow(row)) {
            discount = row.GetDouble(0);
            surcharge = row.GetDouble(1);
        }
    }

    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM staOutpostServiceConfig WHERE stationID = %u AND serviceID = %u", stationID, serviceID);
    if (!sDatabase.RunQuery(err,
        "INSERT INTO staOutpostServiceConfig (stationID, serviceID, minimumStanding, minimumCharSecurity, maximumCharSecurity,"
        " minimumCorpSecurity, maximumCorpSecurity, discountPerGoodStandingPoint, surchargePerBadStandingPoint)"
        " VALUES (%u, %u, %f, %f, %f, %f, %f, %f, %f)",
        stationID, serviceID, minimumStanding, minimumCharSecurity, maximumCharSecurity, minimumCorpSecurity, maximumCorpSecurity, discount, surcharge))
        codelog(DATABASE__ERROR, "Error in query: %s", err.c_str());
}

void StationDB::SetServiceCostModifier(uint32 stationID, uint32 serviceID, double discountPerGoodStandingPoint, double surchargePerBadStandingPoint)
{
    DBerror err;
    uint32 affected(0);
    if (!sDatabase.RunQuery(err, affected,
        "UPDATE staOutpostServiceConfig SET discountPerGoodStandingPoint = %f, surchargePerBadStandingPoint = %f"
        " WHERE stationID = %u AND serviceID = %u", discountPerGoodStandingPoint, surchargePerBadStandingPoint, stationID, serviceID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", err.c_str());
        return;
    }
    if (affected == 0)
        sDatabase.RunQuery(err,
            "INSERT INTO staOutpostServiceConfig (stationID, serviceID, minimumStanding, minimumCharSecurity, maximumCharSecurity,"
            " minimumCorpSecurity, maximumCorpSecurity, discountPerGoodStandingPoint, surchargePerBadStandingPoint)"
            " VALUES (%u, %u, -10, 0, 0, 0, 0, %f, %f)", stationID, serviceID, discountPerGoodStandingPoint, surchargePerBadStandingPoint);
}

void StationDB::UpdateStationSettings(uint32 stationID, const std::string& name, double dockingCostPerVolume, int32 officeRentalCost,
                                      double reprocessingStationsTake, int32 reprocessingHangarFlag, int32 exitTime, int32 standingOwnerID)
{
    std::string eName;
    sDatabase.DoEscapeString(eName, name);
    std::string exit = (exitTime < 0 ? "NULL" : std::to_string(exitTime));
    std::string owner = (standingOwnerID == 0 ? "NULL" : std::to_string(standingOwnerID));

    DBerror err;
    if (!sDatabase.RunQuery(err,
        "UPDATE staStations SET stationName = '%s', dockingCostPerVolume = %f, officeRentalCost = %i, reprocessingStationsTake = %f,"
        " reprocessingHangarFlag = %i, exitTime = %s, standingOwnerID = %s WHERE stationID = %u",
        eName.c_str(), dockingCostPerVolume, officeRentalCost, reprocessingStationsTake, reprocessingHangarFlag, exit.c_str(), owner.c_str(), stationID))
        codelog(DATABASE__ERROR, "Error in query: %s", err.c_str());
}

void StationDB::SetStationOwner(uint32 stationID, uint32 corpID)
{
    // the outpost and its assembly lines
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE staStations SET corporationID = %u, standingOwnerID = NULL WHERE stationID = %u", corpID, stationID);
    sDatabase.RunQuery(err, "UPDATE ramAssemblyLineStations SET ownerID = %u WHERE stationID = %u", corpID, stationID);
    sDatabase.RunQuery(err, "UPDATE ramAssemblyLines SET ownerID = %u WHERE containerID = %u", corpID, stationID);
}
void StationDB::GetOwnerIDsOfClonesAtStation(uint32 stationID, uint32 corpID, DBQueryResult& res)
{
    // medical clones (flagClone) at the station:  characterID, corporationID
    if (corpID == -1) {
        sDatabase.RunQuery(res, "SELECT entity.ownerID AS characterID, chrCharacters.corporationID FROM entity "
        "INNER JOIN chrCharacters ON entity.ownerID = chrCharacters.characterID "
        "WHERE entity.flag = %u AND entity.locationID=%u", flagClone, stationID);
    } else {
        sDatabase.RunQuery(res, "SELECT entity.ownerID AS characterID, chrCharacters.corporationID FROM entity "
        "INNER JOIN chrCharacters ON entity.ownerID = chrCharacters.characterID "
        "WHERE entity.flag = %u AND entity.locationID=%u AND chrCharacters.corporationID=%u", flagClone, stationID, corpID);
    }

}

void StationDB::GetOutpostImprovementStaticData(DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT typeID, raceID, requiredAssemblyLineTypeID, requiredImprovementTypeID "
    "FROM staImprovements");
}

void StationDB::GetOutpostImprovements(uint32 stationID, DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT "
    "improvementTier2aTypeID, improvementTier3aTypeID, improvementTier1bTypeID, "
    "improvementTier1aTypeID, improvementTier2bTypeID, improvementTier1cTypeID "
    "FROM staImprovementsInstalled "
    "WHERE stationID=%u", stationID);
}

void StationDB::GetAllOutpostImprovements(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT stationID, improvementTier1aTypeID, improvementTier2aTypeID, improvementTier3aTypeID,"
        " improvementTier1bTypeID, improvementTier2bTypeID, improvementTier1cTypeID"
        " FROM staImprovementsInstalled"))
        codelog(DATABASE__ERROR, "Error in GetAllOutpostImprovements query: %s", res.error.c_str());
}

void StationDB::SaveOutpostImprovements(uint32 stationID, const uint16 (&slots)[6])
{
    // the table has no key:  one row per outpost
    DBerror err;
    if (!sDatabase.RunQuery(err, "DELETE FROM staImprovementsInstalled WHERE stationID = %u", stationID))
        codelog(DATABASE__ERROR, "Error deleting improvements of %u: %s", stationID, err.c_str());
    if (!sDatabase.RunQuery(err,
        "INSERT INTO staImprovementsInstalled (stationID, improvementTier1aTypeID, improvementTier2aTypeID, improvementTier3aTypeID,"
        " improvementTier1bTypeID, improvementTier2bTypeID, improvementTier1cTypeID) VALUES (%u, %u, %u, %u, %u, %u, %u)",
        stationID, slots[0], slots[1], slots[2], slots[3], slots[4], slots[5]))
        codelog(DATABASE__ERROR, "Error saving improvements of %u: %s", stationID, err.c_str());
}

float StationDB::GetStationTypeReprocessingEfficiency(uint16 stationTypeID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT reprocessingEfficiency FROM staStationTypes WHERE stationTypeID = %u", stationTypeID)) {
        codelog(DATABASE__ERROR, "Error in GetStationTypeReprocessingEfficiency query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row) or row.IsNull(0))
        return 0;
    return row.GetFloat(0);
}

void StationDB::SaveOutpostStats(uint32 stationID, uint8 upgradeLevel, uint8 officeSlots, float reprocessingEfficiency)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "UPDATE staStations SET upgradeLevel = %u, officeSlots = %u, reprocessingEfficiency = %f WHERE stationID = %u",
        upgradeLevel, officeSlots, reprocessingEfficiency, stationID))
        codelog(DATABASE__ERROR, "Error saving outpost stats of %u: %s", stationID, err.c_str());
}

PyRep* StationDB::GetStationsByRegion(uint32 regionID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT stationID, stationName, x, y, z, solarSystemID, stationTypeID, corporationID AS ownerID"
        " FROM staStations WHERE regionID = %u", regionID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToRowset(res);
}
