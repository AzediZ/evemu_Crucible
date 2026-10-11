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
    Petition storage (petQueues, petCategories, petPetitions, petMessages, petLog)
*/

#include "eve-server.h"

#include "admin/PetitionDB.h"

namespace {
    std::string Escape(const std::string& str)
    {
        std::string esc;
        sDatabase.DoEscapeString(esc, str);
        return std::string(esc.c_str());    // DoEscapeString leaves a trailing NUL in the string
    }

    std::string Text(const DBResultRow& row, uint32 index)
    {
        if (row.IsNull(index))
            return "";
        return std::string(row.GetText(index), row.ColumnLength(index));
    }

    const char* s_petitionColumns =
        "petitionID, petitionerID, userID, categoryID, queueID, subject, petition, createDate, touchDate, claimedBy,"
        " closed, deleted, updated, closedDate, responseTimeRating, helpfulnessRating, attitudeRating, ratingComment, ratingDateTime";

    void ReadPetition(const DBResultRow& row, PetitionRow& into)
    {
        into.petitionID = row.GetUInt(0);
        into.petitionerID = row.GetUInt(1);
        into.userID = row.GetUInt(2);
        into.categoryID = row.GetUInt(3);
        into.queueID = (uint8)row.GetUInt(4);
        into.subject = Text(row, 5);
        into.petition = Text(row, 6);
        into.createDate = row.GetInt64(7);
        into.touchDate = row.GetInt64(8);
        into.claimedBy = (row.IsNull(9) ? 0 : row.GetUInt(9));
        into.closed = (row.GetInt(10) != 0);
        into.deleted = (row.GetInt(11) != 0);
        into.updated = (row.GetInt(12) != 0);
        into.closedDate = (row.IsNull(13) ? 0 : row.GetInt64(13));
        into.responseTimeRating = (row.IsNull(14) ? -1 : (int8)row.GetInt(14));
        into.helpfulnessRating = (row.IsNull(15) ? -1 : (int8)row.GetInt(15));
        into.attitudeRating = (row.IsNull(16) ? -1 : (int8)row.GetInt(16));
        into.ratingComment = Text(row, 17);
        into.ratingDateTime = (row.IsNull(18) ? 0 : row.GetInt64(18));
    }

    void ReadPetitions(DBQueryResult& res, std::vector<PetitionRow>& into)
    {
        DBResultRow row;
        while (res.GetRow(row)) {
            PetitionRow data = PetitionRow();
            ReadPetition(row, data);
            into.push_back(data);
        }
    }

    void ReadMessages(DBQueryResult& res, std::vector<PetitionMessage>& into)
    {
        DBResultRow row;
        while (res.GetRow(row)) {
            PetitionMessage data = PetitionMessage();
            data.messageID = row.GetUInt(0);
            data.petitionID = row.GetUInt(1);
            data.senderID = (row.IsNull(2) ? 0 : row.GetUInt(2));
            data.text = Text(row, 3);
            data.sentDate = row.GetInt64(4);
            data.comment = (row.GetInt(5) != 0);
            into.push_back(data);
        }
    }
}

void PetitionDB::GetCategories(std::vector<PetitionCategory>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT categoryID, parentID, displayName, description, queueID, languageID"
            " FROM petCategories ORDER BY parentID, categoryID"))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        PetitionCategory data = PetitionCategory();
        data.categoryID = row.GetUInt(0);
        data.parentID = row.GetUInt(1);
        data.displayName = Text(row, 2);
        data.description = Text(row, 3);
        data.queueID = (uint8)row.GetUInt(4);
        data.languageID = Text(row, 5);
        into.push_back(data);
    }
}

bool PetitionDB::GetCategory(uint32 categoryID, PetitionCategory& into)
{
    std::vector<PetitionCategory> list;
    GetCategories(list);
    for (auto& cur : list) {
        if (cur.categoryID == categoryID) {
            into = cur;
            return true;
        }
    }
    return false;
}

void PetitionDB::GetQueues(std::map<uint8, PetitionQueue>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT queueID, queueName, escalatesTo FROM petQueues ORDER BY queueID")) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        PetitionQueue data = PetitionQueue();
        data.queueID = (uint8)row.GetUInt(0);
        data.queueName = Text(row, 1);
        data.escalatesTo = (row.IsNull(2) ? -1 : (int16)row.GetInt(2));
        into[data.queueID] = data;
    }
}

uint32 PetitionDB::Create(uint32 petitionerID, uint32 userID, uint32 categoryID, uint8 queueID, const std::string& subject,
                          const std::string& petition, const std::string& chatLog, const std::string& combatLog)
{
    int64 now = (int64)GetFileTimeNow();
    std::string eSubject(Escape(subject)), ePetition(Escape(petition)), eChat(Escape(chatLog)), eCombat(Escape(combatLog));
    uint32 petitionID(0);
    DBerror err;
    if (!sDatabase.RunQueryLID(err, petitionID,
            "INSERT INTO petPetitions (petitionerID, userID, categoryID, queueID, subject, petition, chatLog, combatLog, createDate, touchDate)"
            " VALUES (%u, %u, %u, %u, '%s', '%s', %s%s%s, %s%s%s, %lli, %lli)",
            petitionerID, userID, categoryID, queueID, eSubject.c_str(), ePetition.c_str(),
            (chatLog.empty() ? "" : "'"), (chatLog.empty() ? "NULL" : eChat.c_str()), (chatLog.empty() ? "" : "'"),
            (combatLog.empty() ? "" : "'"), (combatLog.empty() ? "NULL" : eCombat.c_str()), (combatLog.empty() ? "" : "'"),
            now, now))
    {
        codelog(DATABASE__ERROR, "Error creating petition: %s", err.c_str());
        return 0;
    }
    return petitionID;
}

bool PetitionDB::GetPetition(uint32 petitionID, PetitionRow& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM petPetitions WHERE petitionID = %u", s_petitionColumns, petitionID)) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    ReadPetition(row, into);
    return true;
}

void PetitionDB::GetByPetitioner(uint32 charID, std::vector<PetitionRow>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM petPetitions WHERE petitionerID = %u AND deleted = 0 ORDER BY createDate DESC",
            s_petitionColumns, charID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    ReadPetitions(res, into);
}

void PetitionDB::GetByClaimer(uint32 charID, std::vector<PetitionRow>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM petPetitions WHERE claimedBy = %u AND closed = 0 ORDER BY createDate",
            s_petitionColumns, charID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    ReadPetitions(res, into);
}

void PetitionDB::GetQueue(uint8 queueID, std::vector<PetitionRow>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM petPetitions WHERE queueID = %u AND closed = 0 AND deleted = 0 ORDER BY createDate",
            s_petitionColumns, queueID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    ReadPetitions(res, into);
}

uint32 PetitionDB::CountOpen(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM petPetitions WHERE petitionerID = %u AND closed = 0 AND deleted = 0", charID)) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

bool PetitionDB::Claim(uint32 petitionID, uint32 gmID)
{
    DBerror err;
    uint32 affected(0);
    if (!sDatabase.RunQuery(err, affected,
            "UPDATE petPetitions SET claimedBy = %u, updated = 0, touchDate = %lli"
            " WHERE petitionID = %u AND claimedBy IS NULL AND closed = 0 AND deleted = 0",
            gmID, (int64)GetFileTimeNow(), petitionID))
    {
        codelog(DATABASE__ERROR, "Error claiming petition %u: %s", petitionID, err.c_str());
        return false;
    }
    return (affected > 0);
}

void PetitionDB::Unclaim(uint32 petitionID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE petPetitions SET claimedBy = NULL, touchDate = %lli WHERE petitionID = %u",
            (int64)GetFileTimeNow(), petitionID);
}

void PetitionDB::SetQueue(uint32 petitionID, uint8 queueID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE petPetitions SET queueID = %u, touchDate = %lli WHERE petitionID = %u",
            queueID, (int64)GetFileTimeNow(), petitionID);
}

void PetitionDB::Close(uint32 petitionID)
{
    int64 now = (int64)GetFileTimeNow();
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE petPetitions SET closed = 1, closedDate = %lli, touchDate = %lli WHERE petitionID = %u",
            now, now, petitionID);
}

void PetitionDB::Delete(uint32 petitionID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE petPetitions SET deleted = 1 WHERE petitionID = %u", petitionID);
}

void PetitionDB::SetUpdated(uint32 petitionID, bool updated)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE petPetitions SET updated = %u WHERE petitionID = %u", (updated ? 1 : 0), petitionID);
}

void PetitionDB::Touch(uint32 petitionID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE petPetitions SET touchDate = %lli WHERE petitionID = %u", (int64)GetFileTimeNow(), petitionID);
}

void PetitionDB::SetRating(uint32 petitionID, uint8 responseTime, uint8 helpfulness, uint8 attitude, const std::string& comment, int64 when)
{
    DBerror err;
    sDatabase.RunQuery(err,
            "UPDATE petPetitions SET responseTimeRating = %u, helpfulnessRating = %u, attitudeRating = %u, ratingComment = '%s',"
            " ratingDateTime = %lli WHERE petitionID = %u",
            responseTime, helpfulness, attitude, Escape(comment).c_str(), when, petitionID);
}

uint32 PetitionDB::AddMessage(uint32 petitionID, uint32 senderID, const std::string& text, bool comment, bool isRead)
{
    uint32 messageID(0);
    char sender[16];
    if (senderID > 0)
        snprintf(sender, sizeof(sender), "%u", senderID);
    else
        snprintf(sender, sizeof(sender), "NULL");
    DBerror err;
    if (!sDatabase.RunQueryLID(err, messageID,
            "INSERT INTO petMessages (petitionID, senderID, text, sentDate, comment, isRead) VALUES (%u, %s, '%s', %lli, %u, %u)",
            petitionID, sender, Escape(text).c_str(), (int64)GetFileTimeNow(), (comment ? 1 : 0), (isRead ? 1 : 0)))
    {
        codelog(DATABASE__ERROR, "Error adding petition message: %s", err.c_str());
        return 0;
    }
    return messageID;
}

void PetitionDB::GetMessages(uint32 petitionID, bool includeComments, std::vector<PetitionMessage>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT messageID, petitionID, senderID, text, sentDate, comment FROM petMessages"
            " WHERE petitionID = %u%s ORDER BY messageID", petitionID, (includeComments ? "" : " AND comment = 0")))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    ReadMessages(res, into);
}

void PetitionDB::GetUnread(uint32 charID, std::vector<PetitionMessage>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT m.messageID, m.petitionID, m.senderID, m.text, m.sentDate, m.comment"
            " FROM petMessages AS m JOIN petPetitions AS p USING (petitionID)"
            " WHERE p.petitionerID = %u AND p.deleted = 0 AND m.comment = 0 AND m.isRead = 0"
            " AND (m.senderID IS NULL OR m.senderID != %u) ORDER BY m.messageID", charID, charID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    ReadMessages(res, into);
}

void PetitionDB::MarkRead(uint32 messageID, uint32 charID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE petMessages AS m JOIN petPetitions AS p USING (petitionID) SET m.isRead = 1"
            " WHERE m.messageID = %u AND p.petitionerID = %u", messageID, charID);
}

void PetitionDB::AddLog(uint32 petitionID, uint32 charID, const std::string& userName, uint8 eventID)
{
    char character[16];
    if (charID > 0)
        snprintf(character, sizeof(character), "%u", charID);
    else
        snprintf(character, sizeof(character), "NULL");
    DBerror err;
    if (!sDatabase.RunQuery(err, "INSERT INTO petLog (petitionID, logDateTime, characterID, userName, eventID) VALUES (%u, %lli, %s, '%s', %u)",
            petitionID, (int64)GetFileTimeNow(), character, Escape(userName).c_str(), eventID))
        codelog(DATABASE__ERROR, "Error adding petition log: %s", err.c_str());
}

void PetitionDB::GetLog(uint32 petitionID, std::vector<PetitionLogRow>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT logDateTime, characterID, userName, eventID FROM petLog WHERE petitionID = %u ORDER BY logID",
            petitionID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        PetitionLogRow data = PetitionLogRow();
        data.logDateTime = row.GetInt64(0);
        data.characterID = (row.IsNull(1) ? 0 : row.GetUInt(1));
        data.userName = Text(row, 2);
        data.eventID = (uint8)row.GetUInt(3);
        into.push_back(data);
    }
}

std::string PetitionDB::GetAccountName(uint32 accountID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT accountName FROM account WHERE accountID = %u", accountID)) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return "";
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return "";
    return Text(row, 0);
}

uint32 PetitionDB::GetCharacterAccount(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT accountID FROM chrCharacters WHERE characterID = %u", charID)) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row) or row.IsNull(0))
        return 0;
    return row.GetUInt(0);
}
