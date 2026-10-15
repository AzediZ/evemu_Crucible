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

void IncursionDB::GetTrueSanshaModules(std::vector<uint16>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT t.typeID FROM invTypes AS t JOIN invGroups AS g ON g.groupID = t.groupID"
        " WHERE t.typeName LIKE 'True Sansha %%' AND g.categoryID = 7 AND t.published = 1")) {
        _log(DATABASE__ERROR, "Error in GetTrueSanshaModules query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

void IncursionDB::GetSiteObjects(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT siteTypeID, room, wave, role, typeID, quantity, param, itemName, x, y, z"
        " FROM incSiteObjects"
        " ORDER BY siteTypeID, room, wave, objectID"))
        _log(DATABASE__ERROR, "Error in GetSiteObjects query: %s", res.error.c_str());
}

void IncursionDB::GetIncursions(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        "SELECT incursionID, constellationID, stagingSystemID, state, influence, startTime, stateTime, lastProgress, hasBoss, hqRun"
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

void IncursionDB::SaveIncursion(uint32 incursionID, uint8 state, double influence, uint32 stateTime, uint32 lastProgress, bool hasBoss, bool hqRun)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "UPDATE incIncursions SET state = %u, influence = %f, stateTime = %u, lastProgress = %u, hasBoss = %u, hqRun = %u WHERE incursionID = %u",
        state, influence, stateTime, lastProgress, hasBoss ? 1 : 0, hqRun ? 1 : 0, incursionID))
        _log(DATABASE__ERROR, "Error in SaveIncursion query: %s", err.c_str());
}

void IncursionDB::DeleteIncursion(uint32 incursionID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM incSystems WHERE incursionID = %u", incursionID);
    sDatabase.RunQuery(err, "DELETE FROM incLPPool WHERE incursionID = %u", incursionID);
    sDatabase.RunQuery(err, "DELETE FROM incIncursions WHERE incursionID = %u", incursionID);
}

void IncursionDB::AddPoolLP(uint32 incursionID, uint32 characterID, uint32 lp)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "INSERT INTO incLPPool (incursionID, characterID, lp) VALUES (%u, %u, %u)"
        " ON DUPLICATE KEY UPDATE lp = lp + %u",
        incursionID, characterID, lp, lp))
        _log(DATABASE__ERROR, "Error in AddPoolLP query: %s", err.c_str());
}

void IncursionDB::GetPool(DBQueryResult& res, uint32 incursionID)
{
    if (!sDatabase.RunQuery(res, "SELECT characterID, lp FROM incLPPool WHERE incursionID = %u", incursionID))
        _log(DATABASE__ERROR, "Error in GetPool query: %s", res.error.c_str());
}

uint32 IncursionDB::GetPoolLP(uint32 incursionID, uint32 characterID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT lp FROM incLPPool WHERE incursionID = %u AND characterID = %u", incursionID, characterID))
        return 0;
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
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
