/**
 * @name WarDB.cpp
 *   the crpWars table (wars between player corporations and alliances)
 */

#include "eve-server.h"

#include "faction/WarDB.h"
#include "faction/WarManager.h"

namespace {
    // nullable columns:  0 is NULL
    std::string Nullable(int64 value)
    {
        return (value == 0 ? std::string("NULL") : std::to_string(value));
    }
}

void WarDB::CreateTable()
{
    DBerror err;
    sDatabase.RunQuery(err,
        "CREATE TABLE IF NOT EXISTS crpWars ("
        " warID INT UNSIGNED NOT NULL AUTO_INCREMENT,"
        " declaredByID INT UNSIGNED NOT NULL,"
        " againstID INT UNSIGNED NOT NULL,"
        " timeDeclared BIGINT NOT NULL,"
        " timeFinished BIGINT NULL DEFAULT NULL,"
        " retracted BIGINT NULL DEFAULT NULL,"
        " retractedBy INT UNSIGNED NULL DEFAULT NULL,"
        " billID INT UNSIGNED NULL DEFAULT NULL,"
        " mutual TINYINT(1) NOT NULL DEFAULT 0,"
        " endReason TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        " nextBillTime BIGINT NOT NULL DEFAULT 0,"
        " weeklyCost DOUBLE NOT NULL DEFAULT 0,"
        " legalNotified TINYINT(1) NOT NULL DEFAULT 0,"
        " PRIMARY KEY (warID), KEY declaredByID (declaredByID), KEY againstID (againstID))"
        " ENGINE=InnoDB DEFAULT CHARSET=utf8");
}

bool WarDB::LoadWars(int64 since, std::vector<War::Data>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT warID, declaredByID, againstID, timeDeclared, IFNULL(timeFinished, 0), IFNULL(retracted, 0),"
        " IFNULL(retractedBy, 0), IFNULL(billID, 0), mutual, endReason, nextBillTime, weeklyCost, legalNotified"
        " FROM crpWars WHERE timeFinished IS NULL OR timeFinished > %lli", since))
    {
        codelog(WAR__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        War::Data w = War::Data();
            w.warID = row.GetUInt(0);
            w.declaredByID = row.GetUInt(1);
            w.againstID = row.GetUInt(2);
            w.timeDeclared = row.GetInt64(3);
            w.timeFinished = row.GetInt64(4);
            w.retracted = row.GetInt64(5);
            w.retractedBy = row.GetUInt(6);
            w.billID = row.GetUInt(7);
            w.mutual = (row.GetInt(8) != 0);
            w.endReason = row.GetUInt(9);
            w.nextBillTime = row.GetInt64(10);
            w.weeklyCost = row.GetDouble(11);
            w.legalNotified = (row.GetInt(12) != 0);
        into.push_back(w);
    }
    return true;
}

uint32 WarDB::InsertWar(const War::Data& w)
{
    DBerror err;
    uint32 warID(0);
    if (!sDatabase.RunQueryLID(err, warID,
        "INSERT INTO crpWars (declaredByID, againstID, timeDeclared, timeFinished, retracted, retractedBy, billID, mutual,"
        " endReason, nextBillTime, weeklyCost, legalNotified)"
        " VALUES (%u, %u, %lli, %s, %s, %s, %s, %u, %u, %lli, %f, %u)",
        w.declaredByID, w.againstID, w.timeDeclared, Nullable(w.timeFinished).c_str(), Nullable(w.retracted).c_str(),
        Nullable(w.retractedBy).c_str(), Nullable(w.billID).c_str(), (w.mutual ? 1 : 0), w.endReason, w.nextBillTime,
        w.weeklyCost, (w.legalNotified ? 1 : 0)))
    {
        codelog(WAR__ERROR, "Error in query: %s", err.c_str());
        return 0;
    }
    return warID;
}

void WarDB::SaveWar(const War::Data& w)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "UPDATE crpWars SET declaredByID = %u, againstID = %u, timeDeclared = %lli, timeFinished = %s, retracted = %s,"
        " retractedBy = %s, billID = %s, mutual = %u, endReason = %u, nextBillTime = %lli, weeklyCost = %f, legalNotified = %u"
        " WHERE warID = %u",
        w.declaredByID, w.againstID, w.timeDeclared, Nullable(w.timeFinished).c_str(), Nullable(w.retracted).c_str(),
        Nullable(w.retractedBy).c_str(), Nullable(w.billID).c_str(), (w.mutual ? 1 : 0), w.endReason, w.nextBillTime,
        w.weeklyCost, (w.legalNotified ? 1 : 0), w.warID))
    {
        codelog(WAR__ERROR, "Error in query: %s", err.c_str());
    }
}

PyRep* WarDB::GetWarsRowset(uint32 ownerID, int64 since)
{
    // war_cso_wars builds blue.DBRow(indexRowSet.header, line) from OnWarChanged, so the columns are the client's, in its order
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT warID, declaredByID, againstID, timeDeclared, timeFinished, retracted, retractedBy, billID, mutual"
        " FROM crpWars"
        " WHERE (declaredByID = %u OR againstID = %u) AND (timeFinished IS NULL OR timeFinished > %lli)",
        ownerID, ownerID, since))
    {
        codelog(WAR__ERROR, "Error in query: %s", res.error.c_str());
    }
    return DBResultToCIndexedRowset(res, "warID");
}

void WarDB::LoadCorpAlliances(std::map<uint32, uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT corporationID, allianceID FROM crpCorporation WHERE allianceID != 0 AND deleted = 0")) {
        codelog(WAR__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into[row.GetUInt(0)] = row.GetUInt(1);
}

bool WarDB::IsOwnerAlive(uint32 ownerID)
{
    DBQueryResult res;
    if (IsAlliance(ownerID)) {
        sDatabase.RunQuery(res, "SELECT deleted FROM alnAlliance WHERE allianceID = %u", ownerID);
    } else if (IsPlayerCorp(ownerID)) {
        sDatabase.RunQuery(res, "SELECT deleted FROM crpCorporation WHERE corporationID = %u", ownerID);
    } else {
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    return (row.GetInt(0) == 0);
}
