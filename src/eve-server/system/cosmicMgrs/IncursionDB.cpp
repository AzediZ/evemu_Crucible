 /**
  * @name IncursionDB.cpp
  *     Incursion database queries for EVEmu
  *
  * @date:          12 October 2026
  *
  */

#include "eve-server.h"
#include "system/cosmicMgrs/IncursionDB.h"

void IncursionDB::GetSiteTypes(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT siteTypeID, siteName, sceneType, designFleet, iskReward, lpReward, influence, secBand, objectiveTypeID"
        " FROM incSiteTypes"))
        _log(DATABASE__ERROR, "Error in GetSiteTypes query: %s", res.error.c_str());
}

void IncursionDB::GetSiteObjects(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT siteTypeID, wave, typeID, quantity, x, y, z"
        " FROM incSiteObjects"
        " ORDER BY siteTypeID, wave, objectID"))
        _log(DATABASE__ERROR, "Error in GetSiteObjects query: %s", res.error.c_str());
}

void IncursionDB::GetIncursions(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT incursionID, constellationID, stagingSystemID, state, influence, startTime, stateTime, lastProgress"
        " FROM incIncursions"))
        _log(DATABASE__ERROR, "Error in GetIncursions query: %s", res.error.c_str());
}

void IncursionDB::GetIncursionSystems(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT incursionID, systemID, sceneType FROM incSystems"))
        _log(DATABASE__ERROR, "Error in GetIncursionSystems query: %s", res.error.c_str());
}

uint32 IncursionDB::CreateIncursion(uint32 constellationID, uint32 stagingSystemID, uint32 now)
{
    DBerror err;
    uint32 incursionID(0);
    if (!sDatabase.RunQueryLID(err, incursionID,
        "INSERT INTO incIncursions (constellationID, stagingSystemID, state, influence, startTime, stateTime, lastProgress)"
        " VALUES (%u, %u, 0, 1, %u, %u, %u)",
        constellationID, stagingSystemID, now, now, now)) {
        _log(DATABASE__ERROR, "Error in CreateIncursion query: %s", err.c_str());
        return 0;
    }
    return incursionID;
}

void IncursionDB::AddIncursionSystem(uint32 incursionID, uint32 systemID, uint8 sceneType)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "INSERT INTO incSystems (incursionID, systemID, sceneType) VALUES (%u, %u, %u)",
        incursionID, systemID, sceneType))
        _log(DATABASE__ERROR, "Error in AddIncursionSystem query: %s", err.c_str());
}

void IncursionDB::SaveIncursion(uint32 incursionID, uint8 state, double influence, uint32 stateTime, uint32 lastProgress)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "UPDATE incIncursions SET state = %u, influence = %f, stateTime = %u, lastProgress = %u WHERE incursionID = %u",
        state, influence, stateTime, lastProgress, incursionID))
        _log(DATABASE__ERROR, "Error in SaveIncursion query: %s", err.c_str());
}

void IncursionDB::DeleteIncursion(uint32 incursionID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM incSystems WHERE incursionID = %u", incursionID);
    sDatabase.RunQuery(err, "DELETE FROM incIncursions WHERE incursionID = %u", incursionID);
}

void IncursionDB::GetCandidateConstellations(DBQueryResult& res, float minSec, uint8 minSystems)
{
    // skip wormhole space (regionID 11000000+) and the Jove regions
    if (!sDatabase.RunQuery(res,
        "SELECT s.constellationID FROM mapSolarSystems s"
        " WHERE s.regionID < 11000000 AND s.regionID NOT IN (10000004, 10000017, 10000019)"
        " GROUP BY s.constellationID"
        " HAVING MIN(s.security) >= %f AND COUNT(*) >= %u"
        "  AND s.constellationID IN (SELECT DISTINCT constellationID FROM staStations)",
        minSec, minSystems))
        _log(DATABASE__ERROR, "Error in GetCandidateConstellations query: %s", res.error.c_str());
}

void IncursionDB::GetConstellationSystems(DBQueryResult& res, uint32 constellationID)
{
    if (!sDatabase.RunQuery(res,
        "SELECT s.solarSystemID, s.security,"
        " (SELECT COUNT(*) FROM staStations st WHERE st.solarSystemID = s.solarSystemID),"
        " (SELECT COUNT(*) FROM mapSolarSystemJumps j WHERE j.fromSolarSystemID = s.solarSystemID AND j.toConstellationID != s.constellationID)"
        " FROM mapSolarSystems s WHERE s.constellationID = %u",
        constellationID))
        _log(DATABASE__ERROR, "Error in GetConstellationSystems query: %s", res.error.c_str());
}

void IncursionDB::GetConstellationJumps(DBQueryResult& res, uint32 constellationID)
{
    if (!sDatabase.RunQuery(res,
        "SELECT fromSolarSystemID, toSolarSystemID FROM mapSolarSystemJumps"
        " WHERE fromConstellationID = %u AND toConstellationID = %u",
        constellationID, constellationID))
        _log(DATABASE__ERROR, "Error in GetConstellationJumps query: %s", res.error.c_str());
}

std::string IncursionDB::GetConstellationName(uint32 constellationID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT constellationName FROM mapConstellations WHERE constellationID = %u", constellationID)) {
        _log(DATABASE__ERROR, "Error in GetConstellationName query: %s", res.error.c_str());
        return "Unknown";
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return "Unknown";
    return row.GetText(0);
}

void IncursionDB::DeleteSignature(const std::string& sigID)
{
    std::string sID;
    sDatabase.DoEscapeString(sID, sigID);
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM sysSignatures WHERE sigID = '%s'", sID.c_str());
}
