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
    Author:        caytchen, groove
*/

#include "eve-server.h"

#include "mail/MailDB.h"
#include "EVE_Mail.h"


namespace {
// ",1,2,3" -> used inside "IN (0%s)".  integers only
std::string IDList(const std::vector<int32>& ids, size_t first = 0, size_t count = std::string::npos)
{
    std::ostringstream str;
    for (size_t i = first; (i < ids.size()) and (i - first < count); ++i)
        str << "," << ids[i];
    return str.str();
}

// DBcore::RunQuery(DBQueryResult&) formats into a 4k buffer; keep id lists well below it
const size_t maxIDsPerQuery = 300;

const char* SetBits(bool set)
{
    return (set ? "|" : "& ~");
}
}

PyRep* MailDB::GetMailStatus(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT messageID, statusMask, labelMask FROM mailStatus WHERE characterID = %u", charID))
        codelog(DATABASE__ERROR, "Failed to get mail status for %u: %s", charID, res.error.c_str());
    return DBResultToCRowset(res);
}

PyRep* MailDB::GetMailHeaders(uint32 charID, uint32 minID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT m.messageID, m.senderID, m.toCharacterIDs, m.toListID, m.toCorpOrAllianceID, m.title, m.sentDate"
            " FROM mailStatus AS s"
            " JOIN mailMessage AS m USING (messageID)"
            " WHERE s.characterID = %u AND m.messageID > %u", charID, minID))
        codelog(DATABASE__ERROR, "Failed to get mail headers for %u: %s", charID, res.error.c_str());
    return DBResultToCRowset(res);
}

PyRep* MailDB::GetMailHeadersByIDs(uint32 charID, const std::vector<int32>& ids)
{
    PyObjectEx* rowset(nullptr);
    size_t i(0);
    do {
        DBQueryResult res;
        if (!sDatabase.RunQuery(res,
                "SELECT m.messageID, m.senderID, m.toCharacterIDs, m.toListID, m.toCorpOrAllianceID, m.title, m.sentDate"
                " FROM mailStatus AS s"
                " JOIN mailMessage AS m USING (messageID)"
                " WHERE s.characterID = %u AND m.messageID IN (0%s)",
                charID, IDList(ids, i, maxIDsPerQuery).c_str()))
            codelog(DATABASE__ERROR, "Failed to get mail headers for %u: %s", charID, res.error.c_str());

        if (rowset == nullptr) {
            rowset = DBResultToCRowset(res);
        } else {
            DBResultRow row;
            while (res.GetRow(row))
                FillPackedRow(row, ((CRowSet*)rowset)->NewRow());
        }
        i += maxIDsPerQuery;
    } while (i < ids.size());

    return rowset;
}

PyString* MailDB::GetMailBody(uint32 charID, uint32 messageID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT m.body FROM mailMessage AS m"
            " JOIN mailStatus AS s USING (messageID)"
            " WHERE s.characterID = %u AND m.messageID = %u", charID, messageID))
    {
        codelog(DATABASE__ERROR, "Failed to get mail body %u for %u: %s", messageID, charID, res.error.c_str());
        return nullptr;
    }

    DBResultRow row;
    if (!res.GetRow(row) or row.IsNull(0))
        return nullptr;

    return new PyString(row.GetText(0), row.ColumnLength(0));
}

void MailDB::SetStatusBits(uint32 charID, const std::vector<int32>& ids, uint32 mask, bool set)
{
    DBerror err;
    for (size_t i = 0; i < ids.size(); i += maxIDsPerQuery)
        if (!sDatabase.RunQuery(err,
                "UPDATE mailStatus SET statusMask = statusMask %s %u"
                " WHERE characterID = %u AND messageID IN (0%s)",
                SetBits(set), mask, charID, IDList(ids, i, maxIDsPerQuery).c_str()))
            codelog(DATABASE__ERROR, "Failed to update mail status for %u: %s", charID, err.c_str());
}

void MailDB::SetStatusBitsAll(uint32 charID, uint32 mask, bool set, bool skipTrashed)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "UPDATE mailStatus SET statusMask = statusMask %s %u"
            " WHERE characterID = %u%s",
            SetBits(set), mask, charID, (skipTrashed ? " AND (statusMask & 8) = 0" : "")))
        codelog(DATABASE__ERROR, "Failed to update mail status for %u: %s", charID, err.c_str());
}

void MailDB::SetStatusBitsByLabel(uint32 charID, int64 labelMask, uint32 mask, bool set, bool skipTrashed)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "UPDATE mailStatus SET statusMask = statusMask %s %u"
            " WHERE characterID = %u AND (labelMask & %lli) = %lli%s",
            SetBits(set), mask, charID, labelMask, labelMask, (skipTrashed ? " AND (statusMask & 8) = 0" : "")))
        codelog(DATABASE__ERROR, "Failed to update mail status by label for %u: %s", charID, err.c_str());
}

void MailDB::SetStatusBitsByList(uint32 charID, uint32 listID, uint32 mask, bool set, bool skipTrashed)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "UPDATE mailStatus AS s"
            " JOIN mailMessage AS m USING (messageID)"
            " SET s.statusMask = s.statusMask %s %u"
            " WHERE s.characterID = %u AND m.toListID = %u%s",
            SetBits(set), mask, charID, listID, (skipTrashed ? " AND (s.statusMask & 8) = 0" : "")))
        codelog(DATABASE__ERROR, "Failed to update mail status by list for %u: %s", charID, err.c_str());
}

void MailDB::SetLabelBits(uint32 charID, const std::vector<int32>& ids, int64 labelMask, bool set)
{
    DBerror err;
    for (size_t i = 0; i < ids.size(); i += maxIDsPerQuery)
        if (!sDatabase.RunQuery(err,
                "UPDATE mailStatus SET labelMask = labelMask %s %lli"
                " WHERE characterID = %u AND messageID IN (0%s)",
                SetBits(set), labelMask, charID, IDList(ids, i, maxIDsPerQuery).c_str()))
            codelog(DATABASE__ERROR, "Failed to update mail labels for %u: %s", charID, err.c_str());
}

void MailDB::DeleteMail(uint32 charID, const std::vector<int32>& ids)
{
    DBerror err;
    for (size_t i = 0; i < ids.size(); i += maxIDsPerQuery)
        if (!sDatabase.RunQuery(err,
                "DELETE FROM mailStatus WHERE characterID = %u AND messageID IN (0%s)",
                charID, IDList(ids, i, maxIDsPerQuery).c_str()))
            codelog(DATABASE__ERROR, "Failed to delete mail for %u: %s", charID, err.c_str());
    PurgeOrphanMessages();
}

void MailDB::EmptyTrash(uint32 charID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "DELETE FROM mailStatus WHERE characterID = %u AND (statusMask & %u) <> 0",
            charID, mailStatusMaskTrashed))
        codelog(DATABASE__ERROR, "Failed to empty trash for %u: %s", charID, err.c_str());
    PurgeOrphanMessages();
}

void MailDB::PurgeOrphanMessages()
{
    // a message is kept as long as anybody still has it
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "DELETE m FROM mailMessage AS m"
            " LEFT JOIN mailStatus AS s USING (messageID)"
            " WHERE s.messageID IS NULL"))
        codelog(DATABASE__ERROR, "Failed to purge mail messages: %s", err.c_str());
}

uint32 MailDB::InsertMessage(uint32 senderID, const std::string& toCharacterIDs, uint32 toListID, uint32 toCorpOrAllianceID,
                             const std::string& title, const std::string& compressedBody, int64 sentDate)
{
    std::string titleEsc, bodyEsc;
    sDatabase.DoEscapeString(titleEsc, title);
    sDatabase.DoEscapeString(bodyEsc, compressedBody);

    std::string listStr("NULL"), corpStr("NULL");
    if (toListID > 0)
        listStr = std::to_string(toListID);
    if (toCorpOrAllianceID > 0)
        corpStr = std::to_string(toCorpOrAllianceID);

    DBerror err;
    uint32 messageID(0);
    if (!sDatabase.RunQueryLID(err, messageID,
            "INSERT INTO mailMessage (senderID, toCharacterIDs, toListID, toCorpOrAllianceID, title, body, sentDate)"
            " VALUES (%u, '%s', %s, %s, '%s', '%s', %lli)",
            senderID, toCharacterIDs.c_str(), listStr.c_str(), corpStr.c_str(), titleEsc.c_str(), bodyEsc.c_str(), sentDate))
    {
        codelog(DATABASE__ERROR, "Failed to insert mail message from %u: %s", senderID, err.c_str());
        return 0;
    }
    return messageID;
}

bool MailDB::InsertStatusRows(uint32 messageID, const std::vector<StatusRow>& rows)
{
    DBerror err;
    for (size_t i = 0; i < rows.size(); i += 500) {
        std::ostringstream query;
        query << "INSERT IGNORE INTO mailStatus (messageID, characterID, statusMask, labelMask) VALUES ";
        for (size_t j = i; (j < rows.size()) and (j < i + 500); ++j) {
            if (j > i)
                query << ",";
            query << "(" << messageID << "," << rows[j].charID << "," << rows[j].statusMask << "," << rows[j].labelMask << ")";
        }
        if (!sDatabase.RunQuery(err, "%s", query.str().c_str())) {
            codelog(DATABASE__ERROR, "Failed to insert mail status rows for message %u: %s", messageID, err.c_str());
            return false;
        }
    }
    return true;
}

void MailDB::GetExistingCharacters(const std::vector<int32>& ids, std::vector<int32>& into)
{
    for (size_t i = 0; i < ids.size(); i += maxIDsPerQuery) {
        DBQueryResult res;
        if (!sDatabase.RunQuery(res,
                "SELECT characterID FROM chrCharacters WHERE characterID IN (0%s)",
                IDList(ids, i, maxIDsPerQuery).c_str()))
        {
            codelog(DATABASE__ERROR, "Failed to look up characters: %s", res.error.c_str());
            continue;
        }
        DBResultRow row;
        while (res.GetRow(row))
            into.push_back(row.GetInt(0));
    }
}

void MailDB::GetListMemberIDs(uint32 listID, std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT characterID FROM mailListUsers WHERE listID = %u", listID)) {
        codelog(DATABASE__ERROR, "Failed to get members of mailing list %u: %s", listID, res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

uint32 MailDB::GetUnreadCount(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT COUNT(*) FROM mailStatus WHERE characterID = %u AND (statusMask & %u) = 0",
            charID, mailStatusMaskRead | mailStatusMaskTrashed))
    {
        codelog(DATABASE__ERROR, "Failed to count unread mail for %u: %s", charID, res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

PyRep* MailDB::GetLabels(uint32 charID)
{
    // dict labelID -> KeyVal(labelID, name, color).  the client adds the system labels itself
    PyDict* ret = new PyDict();

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT bit, name, color FROM mailLabel WHERE ownerID = %u", charID)) {
        codelog(DATABASE__ERROR, "Failed to get mail labels for %u: %s", charID, res.error.c_str());
        return ret;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        uint32 bit = row.GetUInt(0);
        if (bit > 30)
            continue;
        int32 labelID = (int32)(1u << bit);
        PyDict* dict = new PyDict();
            dict->SetItemString("labelID", new PyInt(labelID));
            dict->SetItemString("name", new PyWString(row.GetText(1), row.ColumnLength(1)));
            dict->SetItemString("color", row.IsNull(2) ? PyStatic.NewNone() : new PyInt(row.GetInt(2)));
        ret->SetItem(new PyInt(labelID), new PyObject("util.KeyVal", dict));
    }

    return ret;
}

uint32 MailDB::GetLabelCount(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM mailLabel WHERE ownerID = %u", charID))
        return 0;
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

bool MailDB::LabelNameExists(uint32 charID, const std::string& name, uint32 skipLabelID/*0*/)
{
    std::string nameEsc;
    sDatabase.DoEscapeString(nameEsc, name);

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT bit FROM mailLabel WHERE ownerID = %u AND name = '%s'", charID, nameEsc.c_str()))
        return false;

    DBResultRow row;
    while (res.GetRow(row))
        if ((skipLabelID == 0) or (row.GetInt(0) != BitFromLabelID(skipLabelID)))
            return true;
    return false;
}

uint32 MailDB::CreateLabel(uint32 charID, const std::string& name, const int32* color)
{
    // lowest free bit above the system labels (inbox, sent, corp, alliance = bits 0..3)
    std::set<uint32> used;
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT bit FROM mailLabel WHERE ownerID = %u", charID)) {
        codelog(DATABASE__ERROR, "Failed to get mail labels for %u: %s", charID, res.error.c_str());
        return 0;
    }
    DBResultRow row;
    while (res.GetRow(row))
        used.insert(row.GetUInt(0));

    uint32 bit(4);
    while ((bit <= 30) and (used.find(bit) != used.end()))
        ++bit;
    if (bit > 30)
        return 0;

    std::string nameEsc;
    sDatabase.DoEscapeString(nameEsc, name);
    std::string colorStr("NULL");
    if (color != nullptr)
        colorStr = std::to_string(*color);

    DBerror err;
    if (!sDatabase.RunQuery(err,
            "INSERT INTO mailLabel (bit, name, color, ownerID) VALUES (%u, '%s', %s, %u)",
            bit, nameEsc.c_str(), colorStr.c_str(), charID))
    {
        codelog(DATABASE__ERROR, "Failed to create mail label for %u: %s", charID, err.c_str());
        return 0;
    }

    return (1u << bit);
}

void MailDB::EditLabel(uint32 charID, uint32 labelID, const std::string* name, const int32* color)
{
    if ((name == nullptr) and (color == nullptr))
        return;

    std::ostringstream set;
    if (name != nullptr) {
        std::string nameEsc;
        sDatabase.DoEscapeString(nameEsc, *name);
        set << "name = '" << nameEsc << "'";
    }
    if (color != nullptr) {
        if (name != nullptr)
            set << ", ";
        set << "color = " << *color;
    }

    DBerror err;
    if (!sDatabase.RunQuery(err,
            "UPDATE mailLabel SET %s WHERE ownerID = %u AND bit = %u",
            set.str().c_str(), charID, BitFromLabelID(labelID)))
        codelog(DATABASE__ERROR, "Failed to edit mail label for %u: %s", charID, err.c_str());
}

void MailDB::DeleteLabel(uint32 charID, uint32 labelID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "UPDATE mailStatus SET labelMask = labelMask & ~%u WHERE characterID = %u",
            labelID, charID))
        codelog(DATABASE__ERROR, "Failed to clear mail label for %u: %s", charID, err.c_str());

    if (!sDatabase.RunQuery(err,
            "DELETE FROM mailLabel WHERE ownerID = %u AND bit = %u", charID, BitFromLabelID(labelID)))
        codelog(DATABASE__ERROR, "Failed to delete mail label for %u: %s", charID, err.c_str());
}

int MailDB::BitFromLabelID(uint32 id)
{
    for (int i = 0; i < 32; ++i)
        if ((id & (1u << i)) != 0)
            return i;
    return 0;
}

PyDict *MailDB::GetJoinedMailingLists(uint32 characterID)
{
    // @NOTE: May have to util.keyval this
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
                " SELECT l.id, l.displayName, l.defaultAccess, l.defaultMemberAccess, l.cost,"
                "   u.listID, u.characterID, u.role, u.access"
                "  FROM mailListUsers AS u "
                "   LEFT JOIN mailList AS l ON l.id = u.listID "
                "   WHERE u.characterID = %u" , characterID))
    {
        codelog(DATABASE__ERROR, " Failed to get joined mailing lists" );
        return nullptr;
    }

    DBResultRow row;
    PyDict *ret = new PyDict();
    while (res.GetRow(row))
    {
        PyDict *dict = new PyDict();

        dict->SetItemString(" displayName" , new PyString(row.GetText(1)));

        int32 role = row.GetInt(7);
        switch (role)
        {
        case mailingListMemberMuted: {
            dict->SetItemString(" isMuted" , new PyBool(true));
            dict->SetItemString(" isOwner" , new PyBool(false));
            dict->SetItemString(" isOperator" , new PyBool(false));
        } break;
        case mailingListMemberOperator: {
            dict->SetItemString(" isMuted" , new PyBool(false));
            dict->SetItemString(" isOwner" , new PyBool(false));
            dict->SetItemString(" isOperator" , new PyBool(true));
        } break;
        case mailingListMemberOwner: {
            dict->SetItemString(" isMuted" , new PyBool(false));
            dict->SetItemString(" isOwner" , new PyBool(true));
            dict->SetItemString(" isOperator" , new PyBool(false));
        } break;
        }

        ret->SetItem(new PyInt(row.GetInt(0)), new PyObject(" util.KeyVal" , dict));
    }

    return ret;
}


uint32 MailDB::CreateMailingList(uint32 creator, std::string name, int32 defaultAccess, int32 defaultMemberAccess, int32 cost)
{
    DBerror err;
    uint32 id = 0;
    uint32& idr = id;

    if (!sDatabase.RunQueryLID(err, idr,
                            " INSERT INTO mailList "
                            " (displayName, defaultAccess, defaultMemberAccess, cost) "
                            " VALUES( '%s', %u, %u, %u) " , name.c_str(), defaultAccess,
                               defaultMemberAccess, cost))
    {
        codelog(DATABASE__ERROR, " Failed to create mailing list" );
        return -1;
    }

    if (!sDatabase.RunQuery(err,
                            " INSERT INTO mailListUsers "
                            " (listID, characterID, role, access) "
                            " VALUES(%u, %u, %u, %u)" , id, creator, mailingListMemberOwner,
                            mailingListAllowed))
    {
        codelog(DATABASE__ERROR, " Failed to insert owner of mailing list" );
        return -1;
    }

    return id;
}

PyDict *MailDB::GetMailingListMembers(int32 listID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT listID, characterID, role, access FROM mailListUsers "
                            " WHERE listID = %u" , listID))
    {
        codelog(DATABASE__ERROR, " Failed to get mailing list members" );
        return nullptr;
    }

    DBResultRow row;
    PyDict *dict = new PyDict();

    while (res.GetRow(row))
    {
        dict->SetItem(new PyInt(row.GetInt(1)), new PyInt(row.GetInt(2)));
    }
    return dict;
}


PyObject *MailDB::MailingListGetSettings(int32 listID)
{
    PyDict *ret = new PyDict();

    // TODO(groove): Make this a join if possible

    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        " SELECT id, displayName, defaultAccess, defaultMemberAccess, cost FROM mailList "
                            "  WHERE id = %u" , listID))
    {
        codelog(DATABASE__ERROR, " Failed to get mailing list settings" );
        return nullptr;
    }
    DBResultRow row;

    if (!res.GetRow(row)) {
        codelog(DATABASE__ERROR, " mailList didn't give us a row" );
        return nullptr;
    }


    ret->SetItemString(" defaultAccess" , new PyInt(row.GetInt(2)));
    ret->SetItemString(" defaultMemberAccess" , new PyInt(row.GetInt(3)));

    DBQueryResult res2;

    if (!sDatabase.RunQuery(res,
        " SELECT listID, characterID, role, access FROM mailListUsers "
                            " WHERE listID = %u" , listID))
    {
        codelog(DATABASE__ERROR, " Failed to get mailing list settings" );
        return nullptr;
    }

    PyDict *dict = new PyDict();

    ret->SetItemString(" access" , dict);


    while (res.GetRow(row)) {
        dict->SetItem(new PyInt(row.GetInt(1)), new PyInt(row.GetInt(3)));
    }

    return new PyObject(" util.KeyVal" , ret);
}


void MailDB::SetMailingListDefaultAccess(int32 listID, int32 defaultAccess, int32 defaultMemberAccess, int32 cost)
{
    DBerror err;

    if (!sDatabase.RunQuery(err,
                   " UPDATE mailList "
                   " SET defaultAccess = %u, defaultMemberAccess = %u, cost = %u "
                   " WHERE id = %u" , defaultAccess, defaultMemberAccess, cost, listID))
    {
        codelog(DATABASE__ERROR, " Failed to update mailing list defaults" );
        return;
    }
}

void MailDB::DeleteMailingList(uint32 characterID, int32 listID)
{
    // not used yet.
}

void MailDB::JoinMailingList(uint32 characterID, std::string name)
{
    // not used yet.
}

void MailDB::LeaveMailingList(uint32 characterID, int32 listID)
{
    // not used yet.
}

void MailDB::MailingListClearEntityAccess(int32 entity, int32 listID)
{
    // not used yet.
}

void MailDB::MailingListSetEntityAccess(int32 entity, int32 access, int32 listID)
{
    // not used yet.
}

/*
 * Notifications
 */
namespace {
// "AND notificationID IN (..)" / "AND typeID IN (..)" for the optional id and type filters.  integers only
std::string NotificationFilter(const std::vector<int32>* ids, const std::vector<uint16>* types)
{
    std::ostringstream str;
    if (ids != nullptr) {
        str << " AND notificationID IN (0";
        for (auto cur : *ids)
            str << "," << cur;
        str << ")";
    }
    if (types != nullptr) {
        str << " AND typeID IN (0";
        for (auto cur : *types)
            str << "," << cur;
        str << ")";
    }
    return str.str();
}
}

uint32 MailDB::StoreNotification(uint32 receiverID, uint16 typeID, uint32 senderID, int64 created, const std::string& yaml)
{
    std::string yamlEsc;
    sDatabase.DoEscapeString(yamlEsc, yaml);

    DBerror err;
    uint32 notificationID(0);
    if (!sDatabase.RunQueryLID(err, notificationID,
            "INSERT INTO chrNotifications (typeID, senderID, receiverID, processed, created, data)"
            " VALUES (%u, %u, %u, 0, %lli, '%s')",
            typeID, senderID, receiverID, created, yamlEsc.c_str()))
    {
        codelog(DATABASE__ERROR, "Failed to store notification type %u for %u: %s", typeID, receiverID, err.c_str());
        return 0;
    }
    return notificationID;
}

CRowSet* MailDB::GetNotifications(uint32 charID, const std::vector<uint16>& types, bool unprocessedOnly)
{
    // header matches the live server's dbutil.CRowset for notificationMgr.GetByGroupID/GetUnprocessed
    DBRowDescriptor* header = new DBRowDescriptor();
        header->AddColumn("notificationID", DBTYPE_I4);
        header->AddColumn("typeID",         DBTYPE_I4);
        header->AddColumn("senderID",       DBTYPE_I4);
        header->AddColumn("receiverID",     DBTYPE_I4);
        header->AddColumn("processed",      DBTYPE_BOOL);
        header->AddColumn("created",        DBTYPE_FILETIME);
        header->AddColumn("data",           DBTYPE_WSTR);
        header->AddColumn("deleted",        DBTYPE_BOOL);
    CRowSet* rowset = new CRowSet(&header);

    std::string filter;
    if (!types.empty())
        filter = NotificationFilter(nullptr, &types);

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT notificationID, typeID, senderID, receiverID, processed, created, data"
            " FROM chrNotifications"
            " WHERE receiverID = %u%s%s"
            " ORDER BY notificationID DESC LIMIT 1000",
            charID, filter.c_str(), (unprocessedOnly ? " AND processed = 0" : "")))
    {
        codelog(DATABASE__ERROR, "Failed to get notifications for %u: %s", charID, res.error.c_str());
        return rowset;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        PyPackedRow* into = rowset->NewRow();
        into->SetField((uint32)0, new PyInt(row.GetInt(0)));
        into->SetField(1, new PyInt(row.GetInt(1)));
        into->SetField(2, new PyInt(row.GetInt(2)));
        into->SetField(3, new PyInt(row.GetInt(3)));
        into->SetField(4, new PyBool(row.GetInt(4) != 0));
        into->SetField(5, new PyLong(row.GetInt64(5)));
        into->SetField(6, new PyWString(row.IsNull(6) ? std::string() : std::string(row.GetText(6), row.ColumnLength(6))));
        into->SetField(7, new PyBool(false));
    }

    return rowset;
}

void MailDB::MarkNotificationsProcessed(uint32 charID, const std::vector<int32>* ids, const std::vector<uint16>* types)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "UPDATE chrNotifications SET processed = 1 WHERE receiverID = %u AND processed = 0%s",
            charID, NotificationFilter(ids, types).c_str()))
        codelog(DATABASE__ERROR, "Failed to mark notifications processed for %u: %s", charID, err.c_str());
}

void MailDB::DeleteNotifications(uint32 charID, const std::vector<int32>* ids, const std::vector<uint16>* types, bool unprocessedOnly/*false*/)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "DELETE FROM chrNotifications WHERE receiverID = %u%s%s",
            charID, NotificationFilter(ids, types).c_str(), (unprocessedOnly ? " AND processed = 0" : "")))
        codelog(DATABASE__ERROR, "Failed to delete notifications for %u: %s", charID, err.c_str());
}

uint32 MailDB::GetUnprocessedNotificationCount(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT COUNT(*) FROM chrNotifications WHERE receiverID = %u AND processed = 0", charID))
    {
        codelog(DATABASE__ERROR, "Failed to count notifications for %u: %s", charID, res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

void MailDB::GetCorpMembersWithRoles(uint32 corpID, int64 roleMask, std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT characterID FROM chrCharacters"
            " WHERE corporationID = %u"
            " AND (%lli = 0 OR ((corpRole | rolesAtAll) & %lli) <> 0 OR (corpRole & %lli) <> 0)",
            corpID, roleMask, roleMask, (int64)Corp::Role::Director))
    {
        codelog(DATABASE__ERROR, "Failed to get members of corp %u: %s", corpID, res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

void MailDB::GetAllianceMembersWithRoles(uint32 allianceID, int64 roleMask, std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT c.characterID FROM chrCharacters AS c"
            " JOIN crpCorporation AS p ON p.corporationID = c.corporationID"
            " WHERE p.allianceID = %u"
            " AND (%lli = 0 OR ((c.corpRole | c.rolesAtAll) & %lli) <> 0 OR (c.corpRole & %lli) <> 0)",
            allianceID, roleMask, roleMask, (int64)Corp::Role::Director))
    {
        codelog(DATABASE__ERROR, "Failed to get members of alliance %u: %s", allianceID, res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}
