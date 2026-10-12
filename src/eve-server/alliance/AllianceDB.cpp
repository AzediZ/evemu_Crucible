/**
 * @name AllianceDB.cpp
 *      database methods for alliance data
 *
 * @author Allan
 * @updates James
 * @date 24 May 2019
 *
 */

#include "Client.h"
#include "StaticDataMgr.h"
#include "character/Character.h"
#include "alliance/AllianceDB.h"

namespace {
    // the alliance row as the client caches it (all_cso_alliance):  no executor is None ("in turmoil"), members are pilots
    const char* AllianceColumns =
        " a.allianceID, a.allianceName, a.description, a.typeID, a.shortName, NULLIF(a.executorCorpID, 0) AS executorCorpID,"
        " a.creatorCorpID, a.creatorCharID, a.startDate,"
        " (SELECT CAST(IFNULL(SUM(c.memberCount), 0) AS SIGNED) FROM crpCorporation AS c WHERE c.allianceID = a.allianceID AND c.deleted = 0) AS memberCount,"
        " a.url, a.deleted, 0 AS dictatorial ";     //Dictatorial is not used in Crucible but must be set
}

void AllianceDB::AddBulletin(uint32 allyID, uint32 ownerID, uint32 cCharID, const std::string &title, const std::string &body)
{
    std::string eTitle, eBody;
    sDatabase.DoEscapeString(eTitle, title);
    sDatabase.DoEscapeString(eBody, body);
    int64 now = GetFileTimeNow();
    DBerror err;
    sDatabase.RunQuery(err,
                       "INSERT INTO alnBulletins (allianceID, ownerID, createCharacterID, createDateTime, editCharacterID, editDateTime, title, body)"
                       " VALUES (%u, %u, %u, %lli, %u, %lli, '%s', '%s')",
                       allyID, ownerID, cCharID, now, cCharID, now, eTitle.c_str(), eBody.c_str());
}

void AllianceDB::EditBulletin(uint32 allyID, uint32 bulletinID, uint32 eCharID, const std::string &title, const std::string &body)
{
    std::string eTitle, eBody;
    sDatabase.DoEscapeString(eTitle, title);
    sDatabase.DoEscapeString(eBody, body);
    DBerror err;
    sDatabase.RunQuery(err,
                       "UPDATE alnBulletins SET editCharacterID = %u, editDateTime = %lli, title = '%s', body = '%s'"
                       " WHERE bulletinID = %u AND allianceID = %u",
                       eCharID, (int64)GetFileTimeNow(), eTitle.c_str(), eBody.c_str(), bulletinID, allyID);
}

void AllianceDB::DeleteBulletin(uint32 allyID, uint32 bulletinID)
{
    DBerror err;
    sDatabase.RunQuery(err,
                       "DELETE from alnBulletins "
                       " WHERE bulletinID = %u AND allianceID = %u",
                       bulletinID, allyID);
}

PyRep *AllianceDB::GetBulletins(uint32 allyID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT allianceID, bulletinID, ownerID, createCharacterID, createDateTime, editCharacterID, editDateTime, title, body"
                            " FROM alnBulletins"
                            " WHERE allianceID = %u ",
                            allyID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToCRowset(res);
}

PyRep *AllianceDB::GetAlliance(uint32 allyID)
{
    // called by alliance member
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            " SELECT %s FROM alnAlliance AS a WHERE a.allianceID = %u",
                            AllianceColumns, allyID))
    {
        codelog(ALLY__DB_ERROR, "Error in retrieving alliance's data (%u)", allyID);
        return nullptr;
    }

    DBResultRow row;
    if (!res.GetRow(row))
    {
        codelog(ALLY__DB_WARNING, "Unable to find alliance's data (%u)", allyID);
        return nullptr;
    }

    return DBRowToPackedRow(row);
}

PyRep *AllianceDB::GetMyApplications(uint32 corpID)
{
    // sm.GetService('corp').GetAllianceApplications()[allianceID]
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            " SELECT applicationID, corporationID, allianceID, applicationText, "
                            " state, applicationDateTime, CAST(deleted AS UNSIGNED) AS deleted "
                            " FROM alnApplications AS a"
                            " WHERE corporationID = %u AND applicationID = "
                            "  (SELECT MAX(b.applicationID) FROM alnApplications AS b WHERE b.corporationID = a.corporationID AND b.allianceID = a.allianceID)",
                            corpID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    PyObjectEx *obj = DBResultToCIndexedRowset(res, "allianceID");
    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
}

PyRep *AllianceDB::GetApplications(uint32 allyID)
{
    // all_cso_applications filters the rejected ones out unless they're asked for.  effective applications are members now
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            " SELECT applicationID, corporationID, allianceID, applicationText, "
                            " state, applicationDateTime, CAST(deleted AS UNSIGNED) AS deleted "
                            " FROM alnApplications AS a"
                            " WHERE allianceID = %u AND state != %u AND deleted = 0 AND applicationID = "
                            "  (SELECT MAX(b.applicationID) FROM alnApplications AS b WHERE b.corporationID = a.corporationID AND b.allianceID = a.allianceID)",
                            allyID, EveAlliance::AppStatus::AppEffective))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    PyObjectEx *obj = DBResultToCIndexedRowset(res, "corporationID");
    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
}

bool AllianceDB::GetCurrentApplicationInfo(uint32 allyID, uint32 corpID, Alliance::ApplicationInfo &aInfo)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            " SELECT applicationID, corporationID, allianceID, applicationText, "
                            " state, applicationDateTime, CAST(deleted AS UNSIGNED) "
                            " FROM alnApplications"
                            " WHERE corporationID = %u AND allianceID = %u"
                            " ORDER BY applicationID DESC LIMIT 1",
                            corpID, allyID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        aInfo.valid = false;
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row))
    {
        codelog(ALLY__DB_WARNING, "There's no previous application.");
        aInfo.valid = false;
        return false;
    }

    aInfo.appID = row.GetInt(0);
    aInfo.allyID = allyID;
    aInfo.corpID = corpID;
    aInfo.appText = row.GetText(3);
    aInfo.state = row.GetInt(4);
    aInfo.appTime = row.GetInt64(5);
    aInfo.deleted = row.GetInt(6);
    aInfo.valid = true;
    return true;
}

bool AllianceDB::InsertApplication(Alliance::ApplicationInfo &aInfo)
{
    if (!aInfo.valid)
    {
        _log(ALLY__DB_WARNING, "InsertApplication(): aInfo contains invalid data");
        return false;
    }

    std::string escaped;
    sDatabase.DoEscapeString(escaped, aInfo.appText);
    aInfo.appTime = GetFileTimeNow();
    aInfo.deleted = 0;
    DBerror err;
    if (!sDatabase.RunQueryLID(err, aInfo.appID,
                               " INSERT INTO alnApplications"
                               " (corporationID, allianceID, applicationText, state, applicationDateTime)"
                               " VALUES (%u, %u, '%s', %u, %lli)",
                               aInfo.corpID, aInfo.allyID, escaped.c_str(), aInfo.state, aInfo.appTime))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());
        return false;
    }

    return true;
}

bool AllianceDB::UpdateApplication(const Alliance::ApplicationInfo &aInfo)
{
    if (!aInfo.valid)
    {
        _log(ALLY__DB_WARNING, "UpdateApplication(): info contains invalid data");
        return false;
    }

    DBerror err;
    std::string escaped;
    sDatabase.DoEscapeString(escaped, aInfo.appText);
    if (!sDatabase.RunQuery(err,
                            " UPDATE alnApplications"
                            " SET state = %u, applicationText = '%s'"
                            " WHERE applicationID = %u",
                            aInfo.state, escaped.c_str(), aInfo.appID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());
        return false;
    }
    return true;
}

bool AllianceDB::DeleteApplication(const Alliance::ApplicationInfo &aInfo)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            " DELETE FROM alnApplications"
                            " WHERE corporationID = %u and allianceID = %u ",
                            aInfo.corpID, aInfo.allyID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());
        return false;
    }
    return true;
}

void AllianceDB::SetApplicationEffectiveAt(uint32 appID, int64 effectiveAt)
{
    DBerror err;
    if (!sDatabase.RunQuery(err, "UPDATE alnApplications SET effectiveAt = %lli WHERE applicationID = %u", effectiveAt, appID))
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());
}

void AllianceDB::GetDueApplications(int64 now, std::map<uint32, std::pair<uint32, uint32>>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT applicationID, corporationID, allianceID FROM alnApplications"
                            " WHERE state = %u AND deleted = 0 AND effectiveAt > 0 AND effectiveAt <= %lli",
                            EveAlliance::AppStatus::AppAccepted, now))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into[row.GetUInt(0)] = std::make_pair(row.GetUInt(1), row.GetUInt(2));
}

bool AllianceDB::HasPendingApplication(uint32 allyID, uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT applicationID FROM alnApplications"
                            " WHERE corporationID = %u AND allianceID = %u AND deleted = 0 AND state IN (%u, %u)",
                            corpID, allyID, EveAlliance::AppStatus::AppNew, EveAlliance::AppStatus::AppAccepted))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }
    return (res.GetRowCount() > 0);
}

PyRep *AllianceDB::GetContacts(uint32 allyID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT contactID, CAST(inWatchlist AS UNSIGNED) AS inWatchlist, relationshipID, labelMask"
                            " FROM alnContacts WHERE ownerID = %u",
                            allyID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    PyObjectEx *obj = DBResultToCIndexedRowset(res, "contactID");
    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
}

void AllianceDB::AddContact(uint32 ownerID, int32 contactID, float relationshipID)
{
    DBerror err;
    sDatabase.RunQuery(err,
                       "INSERT INTO alnContacts (ownerID, contactID, relationshipID, inWatchlist, labelMask)"
                       " VALUES (%u, %i, %f, 0, 0)"
                       " ON DUPLICATE KEY UPDATE relationshipID = VALUES(relationshipID)",
                       ownerID, contactID, relationshipID);
}

void AllianceDB::UpdateContact(float relationshipID, uint32 contactID, uint32 ownerID)
{
    DBerror err;
    sDatabase.RunQuery(err,
                       "UPDATE alnContacts SET relationshipID=%f "
                       " WHERE contactID=%u AND ownerID=%u ",
                       relationshipID, contactID, ownerID);
}

void AllianceDB::RemoveContact(uint32 contactID, uint32 ownerID)
{
    DBerror err;
    sDatabase.RunQuery(err,
                       "DELETE from alnContacts "
                       " WHERE contactID=%u AND ownerID=%u ",
                       contactID, ownerID);
}

void AllianceDB::GetContactLabelMasks(uint32 allyID, const std::vector<int32>& contactIDs, std::map<int32, int64>& into)
{
    if (contactIDs.empty())
        return;
    std::string ids;
    for (auto cur : contactIDs) {
        if (!ids.empty())
            ids += ",";
        ids += std::to_string(cur);
    }
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT contactID, labelMask FROM alnContacts WHERE ownerID = %u AND contactID IN (%s)", allyID, ids.c_str())) {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into[row.GetInt(0)] = row.GetInt64(1);
}

PyRep* AllianceDB::GetRelationships(uint32 allyID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT toID, relationship FROM alnRelationships WHERE allianceID = %u", allyID))
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
    return DBResultToCIndexedRowset(res, "toID");
}

bool AllianceDB::GetRelationship(uint32 allyID, uint32 toID, uint8& relationship)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT relationship FROM alnRelationships WHERE allianceID = %u AND toID = %u", allyID, toID)) {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    relationship = row.GetUInt(0);
    return true;
}

void AllianceDB::SetRelationship(uint32 allyID, uint32 toID, uint8 relationship)
{
    DBerror err;
    if (!sDatabase.RunQuery(err, "REPLACE INTO alnRelationships (allianceID, toID, relationship) VALUES (%u, %u, %u)", allyID, toID, relationship))
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());
}

void AllianceDB::DeleteRelationship(uint32 allyID, uint32 toID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err, "DELETE FROM alnRelationships WHERE allianceID = %u AND toID = %u", allyID, toID))
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());
}

bool AllianceDB::AddEmployment(uint32 allyID, uint32 corpID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "INSERT INTO crpEmployment"
                            "  (allianceID, corporationID, startDate)"
                            " VALUES (%u, %u, %lli)",
                            allyID, corpID, (int64)GetFileTimeNow()))
    {
        codelog(DATABASE__ERROR, "Error in employment insert query: %s", err.c_str());
    }

    if (!sDatabase.RunQuery(err, "UPDATE alnAlliance SET memberCount = memberCount+1 WHERE allianceID = %u", allyID))
        codelog(ALLY__DB_ERROR, "Error in new corp member increase query: %s", err.c_str());
    return true;
}

PyRep *AllianceDB::GetEmploymentRecord(uint32 corpID)
{
    // infosvc.GetAllianceHistorySubContent:  rows newest first, allianceID None = left the alliance before it,
    //  and the last row is "Corporation founded" (the oldest start in the corp's member history)
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT startDate, allianceID, deleted FROM ("
                            "  SELECT e.startDate AS startDate, NULLIF(e.allianceID, 0) AS allianceID, CAST(IFNULL(a.deleted, 0) AS SIGNED) AS deleted, 0 AS ord"
                            "   FROM crpEmployment AS e LEFT JOIN alnAlliance AS a ON a.allianceID = e.allianceID"
                            "   WHERE e.corporationID = %u"
                            "  UNION ALL"
                            "  SELECT IFNULL((SELECT MIN(c.startDate) FROM chrEmployment AS c WHERE c.corporationID = %u),"
                            "         IFNULL((SELECT MIN(e2.startDate) FROM crpEmployment AS e2 WHERE e2.corporationID = %u), 0)), NULL, 0, 1"
                            " ) AS h ORDER BY ord, startDate DESC",
                            corpID, corpID, corpID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

uint32 AllianceDB::GetExecutorID(uint32 allyID)
{
    uint32 executorID = 0;
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT executorCorpID FROM alnAlliance "
                            " WHERE allianceID = %u",
                            allyID))
    {
        codelog(DATABASE__ERROR, "Error in checking current alliance executorCorpID: %s", res.error.c_str());
    }

    DBResultRow row;
    while (res.GetRow(row))
    {
        executorID = row.GetUInt(0);
    }

    return executorID;
}

void AllianceDB::SetExecutor(uint32 allyID, uint32 corpID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err, "UPDATE alnAlliance SET executorCorpID = %u WHERE allianceID = %u", corpID, allyID))
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());
}

void AllianceDB::CloseAlliance(uint32 allyID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err, "UPDATE alnAlliance SET deleted = 1, executorCorpID = 0, memberCount = 0 WHERE allianceID = %u", allyID))
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());
    // pending applications to it go nowhere
    sDatabase.RunQuery(err, "UPDATE alnApplications SET state = %u WHERE allianceID = %u AND state IN (%u, %u)",
                       EveAlliance::AppStatus::AppRejected, allyID, EveAlliance::AppStatus::AppNew, EveAlliance::AppStatus::AppAccepted);
}

bool AllianceDB::IsMember(uint32 allyID, uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT corporationID FROM crpCorporation WHERE corporationID = %u AND allianceID = %u AND deleted = 0", corpID, allyID)) {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }
    return (res.GetRowCount() > 0);
}

void AllianceDB::GetMemberCorps(uint32 allyID, std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT corporationID FROM crpCorporation WHERE allianceID = %u AND deleted = 0", allyID)) {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

void AllianceDB::GetExecutorSupport(uint32 allyID, std::map<uint32, uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT corporationID, IFNULL(chosenExecutorID, 0) FROM crpCorporation WHERE allianceID = %u AND deleted = 0", allyID)) {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into[row.GetUInt(0)] = row.GetUInt(1);
}

uint32 AllianceDB::GetChosenExecutor(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT IFNULL(chosenExecutorID, 0) FROM crpCorporation WHERE corporationID = %u", corpID)) {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

int64 AllianceDB::GetMemberStartDate(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT allianceMemberStartDate FROM crpCorporation WHERE corporationID = %u", corpID)) {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetInt64(0);
}

void AllianceDB::GetLiveAlliances(std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT allianceID FROM alnAlliance WHERE deleted = 0")) {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

bool AllianceDB::UpdateCorpAlliance(uint32 allyID, uint32 corpID)
{
    // a new member supports the current executor until it chooses otherwise
    uint32 executorID = GetExecutorID(allyID);

    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "UPDATE crpCorporation SET "
                            "  allianceID = %u, "
                            "  allianceMemberStartDate = %lli, "
                            "  chosenExecutorID = %u "
                            " WHERE corporationID = %u",
                            allyID, (int64)GetFileTimeNow(), executorID, corpID))
    {
        codelog(DATABASE__ERROR, "Error in corp alliance update query: %s", err.c_str());
        return false;
    }
    return true;
}

void AllianceDB::DeleteMember(uint32 allyID, uint32 corpID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "UPDATE crpCorporation SET "
                            "  allianceID = 0, "
                            "  allianceMemberStartDate = 0, "
                            "  chosenExecutorID = 0 "
                            " WHERE corporationID = %u",
                            corpID))
    {
        codelog(DATABASE__ERROR, "Error in deleting corp from alliance: %s", err.c_str());
    }

    // the members that supported it support nobody now
    if (!sDatabase.RunQuery(err, "UPDATE crpCorporation SET chosenExecutorID = 0 WHERE allianceID = %u AND chosenExecutorID = %u", allyID, corpID))
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());

    // the leave record (allianceID 0, shown as None) for the alliance history
    if (!sDatabase.RunQuery(err, "INSERT INTO crpEmployment (allianceID, corporationID, startDate) VALUES (0, %u, %lli)", corpID, (int64)GetFileTimeNow()))
        codelog(ALLY__DB_ERROR, "Error in query: %s", err.c_str());

    if (!sDatabase.RunQuery(err, "UPDATE alnAlliance SET memberCount = GREATEST(memberCount-1, 0) WHERE allianceID = %u", allyID))
        codelog(ALLY__DB_ERROR, "Error in alliance member decrease query: %s", err.c_str());
}

void AllianceDB::DeclareExecutorSupport(uint32 corpID, uint32 chosenExecutor)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "UPDATE crpCorporation SET "
                            "  chosenExecutorID = %u "
                            " WHERE corporationID = %u",
                            chosenExecutor, corpID))
    {
        codelog(DATABASE__ERROR, "Error in setting chosenExecutor: %s", err.c_str());
    }
}

bool AllianceDB::IsShortNameTaken(std::string shortName)
{
    std::string escaped;
    sDatabase.DoEscapeString(escaped, shortName);
    DBQueryResult res;
    sDatabase.RunQuery(res, " SELECT allianceID FROM alnAlliance WHERE shortName = '%s'", escaped.c_str());
    return (res.GetRowCount() != 0);
}

void AllianceDB::UpdateAlliance(uint32 allyID, std::string description, std::string url)
{
    std::string aDesc, aURL;
    sDatabase.DoEscapeString(aDesc, description);
    sDatabase.DoEscapeString(aURL, url);

    DBerror err;

    if (!sDatabase.RunQuery(err,
                            " UPDATE alnAlliance "
                            "  SET description='%s', url='%s' "
                            " WHERE allianceID=%u ",
                            aDesc.c_str(), aURL.c_str(), allyID))
    {
        codelog(ALLY__DB_ERROR, "Error in UpdateAlliance query: %s", err.c_str());
    }
}

bool AllianceDB::CreateAlliance(std::string name, std::string shortName, std::string description, std::string url, Client *pClient, uint32 &allyID, uint32 &corpID)
{
    std::string aName, aShort, aDesc, aURL;
    sDatabase.DoEscapeString(aName, name);
    sDatabase.DoEscapeString(aShort, shortName);
    sDatabase.DoEscapeString(aDesc, description);
    sDatabase.DoEscapeString(aURL, url);

    uint32 charID = pClient->GetCharacterID();
    corpID = pClient->GetCorporationID();

    DBerror err;

    if (!sDatabase.RunQueryLID(err, allyID,
                               " INSERT INTO alnAlliance ( "
                               "   allianceName, shortName, description, executorCorpID, creatorCorpID, creatorCharID, "
                               "   startDate, memberCount, url )"
                               " VALUES "
                               "   ('%s', '%s', '%s', %u, %u, %u, %lli, 0, '%s') ",
                               aName.c_str(), aShort.c_str(), aDesc.c_str(), corpID, corpID, charID, (int64)GetFileTimeNow(), aURL.c_str()))
    {
        codelog(ALLY__DB_ERROR, "Error in CreateAlliance query: %s", err.c_str());
        return false;
    }
    // It has to go into the eveStaticOwners too
    sDatabase.RunQuery(err, " INSERT INTO eveStaticOwners (ownerID,ownerName,typeID) VALUES (%u, '%s', 16159)", allyID, aName.c_str());

    return true;
}

PyRep *AllianceDB::GetMembers(uint32 allyID) //to be called by member of alliance
{
    // all_cso_members:  dbutil rowset keyed by corporationID (OnAllianceMemberChanged builds blue.DBRow(header, line) on it),
    //  chosenExecutorID None when the corp supports nobody
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT corporationID, corporationName, NULLIF(chosenExecutorID, 0) AS chosenExecutorID "
                            " FROM crpCorporation WHERE allianceID = %u AND deleted = 0 ",
                            allyID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    PyObjectEx *obj = DBResultToCIndexedRowset(res, "corporationID");
    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
}

PyRep *AllianceDB::GetAllianceMembers(uint32 allyID) //to be called from show details pane
{
    //This function is called to gather all of the corporationIDs associated to a particular alliance
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT corporationID "
                            " FROM crpCorporation WHERE allianceID = %u AND deleted = 0",
                            allyID))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *AllianceDB::GetRankedAlliances(uint32 maxLen)
{
    // all_ui_rankings:  the biggest alliances by pilots (GetRankedAlliances(maxLen), 0 = all)
    std::string limit;
    if (maxLen > 0)
        limit = " LIMIT " + std::to_string(maxLen);
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
                            "SELECT allianceID, allianceName, executorCorpID, description, typeID, shortName, creatorCorpID,"
                            " creatorCharID, startDate, memberCount, url, deleted FROM ("
                            "  SELECT %s FROM alnAlliance AS a WHERE a.deleted = 0"
                            " ) AS r ORDER BY memberCount DESC, allianceName%s",
                            AllianceColumns, limit.c_str()))
    {
        codelog(ALLY__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

bool AllianceDB::CreateAllianceChangePacket(OnAllianceChanged &ac, uint32 oldAllyID, uint32 newAllyID)
{
    // New Alliance \/
    if (newAllyID == 0)
    {
        ac.allianceIDNew = PyStatic.NewNone();
        ac.allianceNameNew = PyStatic.NewNone();
        ac.descriptionNew = PyStatic.NewNone();
        ac.typeIDNew = PyStatic.NewNone();
        ac.shortNameNew = PyStatic.NewNone();
        ac.executorCorpIDNew = PyStatic.NewNone();
        ac.creatorCorpIDNew = PyStatic.NewNone();
        ac.creatorCharIDNew = PyStatic.NewNone();
        ac.startDateNew = PyStatic.NewNone();
        ac.memberCountNew = PyStatic.NewNone();
        ac.urlNew = PyStatic.NewNone();
        ac.deletedNew = PyStatic.NewNone();
        ac.dictatorialNew = PyStatic.NewNone();
    }
    else
    {
        DBQueryResult res;
        if (!sDatabase.RunQuery(res, " SELECT %s FROM alnAlliance AS a WHERE a.allianceID = %u", AllianceColumns, newAllyID))
        {
            codelog(ALLY__DB_ERROR, "Error in retrieving new alliance's data (%u)", newAllyID);
            return false;
        }

        DBResultRow row;
        if (!res.GetRow(row))
        {
            codelog(ALLY__DB_WARNING, "Unable to find new alliance's data (%u)", newAllyID);
            return false;
        }

        ac.allianceIDNew = new PyInt(row.GetUInt(0));
        ac.allianceNameNew = new PyString(row.GetText(1));
        ac.descriptionNew = new PyString(row.GetText(2));
        ac.typeIDNew = new PyInt(row.GetUInt(3));
        ac.shortNameNew = new PyString(row.GetText(4));
        ac.executorCorpIDNew = (row.IsNull(5) ? PyStatic.NewNone() : new PyInt(row.GetUInt(5)));
        ac.creatorCorpIDNew = new PyInt(row.GetUInt(6));
        ac.creatorCharIDNew = new PyInt(row.GetUInt(7));
        ac.startDateNew = new PyLong(row.GetInt64(8));
        ac.memberCountNew = new PyInt(row.GetUInt(9));
        ac.urlNew = new PyString(row.GetText(10));
        ac.deletedNew = new PyInt(row.GetInt(11));
        ac.dictatorialNew = new PyInt(row.GetInt(12));
    }

    // Old Alliance \/
    if (oldAllyID == 0)
    {
        ac.allianceIDOld = PyStatic.NewNone();
        ac.allianceNameOld = PyStatic.NewNone();
        ac.descriptionOld = PyStatic.NewNone();
        ac.typeIDOld = PyStatic.NewNone();
        ac.shortNameOld = PyStatic.NewNone();
        ac.executorCorpIDOld = PyStatic.NewNone();
        ac.creatorCorpIDOld = PyStatic.NewNone();
        ac.creatorCharIDOld = PyStatic.NewNone();
        ac.startDateOld = PyStatic.NewNone();
        ac.memberCountOld = PyStatic.NewNone();
        ac.urlOld = PyStatic.NewNone();
        ac.deletedOld = PyStatic.NewNone();
        ac.dictatorialOld = PyStatic.NewNone();
    }
    else
    {
        DBQueryResult res;
        if (!sDatabase.RunQuery(res, " SELECT %s FROM alnAlliance AS a WHERE a.allianceID = %u", AllianceColumns, oldAllyID))
        {
            codelog(ALLY__DB_ERROR, "Error in retrieving old alliance's data (%u)", oldAllyID);
            return false;
        }

        DBResultRow row;
        if (!res.GetRow(row))
        {
            codelog(ALLY__DB_WARNING, "Unable to find old alliance's data (%u)", oldAllyID);
            return false;
        }
        ac.allianceIDOld = new PyInt(row.GetUInt(0));
        ac.allianceNameOld = new PyString(row.GetText(1));
        ac.descriptionOld = new PyString(row.GetText(2));
        ac.typeIDOld = new PyInt(row.GetUInt(3));
        ac.shortNameOld = new PyString(row.GetText(4));
        ac.executorCorpIDOld = (row.IsNull(5) ? PyStatic.NewNone() : new PyInt(row.GetUInt(5)));
        ac.creatorCorpIDOld = new PyInt(row.GetUInt(6));
        ac.creatorCharIDOld = new PyInt(row.GetUInt(7));
        ac.startDateOld = new PyLong(row.GetInt64(8));
        ac.memberCountOld = new PyInt(row.GetUInt(9));
        ac.urlOld = new PyString(row.GetText(10));
        ac.deletedOld = new PyInt(row.GetInt(11));
        ac.dictatorialOld = new PyInt(row.GetInt(12));
    }

    return true;
}
