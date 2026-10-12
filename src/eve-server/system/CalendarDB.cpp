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
    Author:        Allan
*/


#include "eve-server.h"

#include "../../eve-common/EVE_Calendar.h"
#include "system/CalendarDB.h"


void CalendarDB::DeleteEvent(uint32 eventID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE sysCalendarEvents SET isDeleted = 1, dateModified = %lli WHERE eventID = %u",
                       (int64)GetFileTimeNow(), eventID);
}

uint32 CalendarDB::SaveNewEvent(uint32 ownerID, uint32 creatorID, int64 dateTime, uint16 duration, const std::string& title,
                                const std::string& description, bool important)
{
    uint8 flag(Calendar::Flag::Invalid);
    if (IsCharacterID(ownerID)) {
        flag = Calendar::Flag::Personal;
    } else if (IsCorp(ownerID)) {
        flag = Calendar::Flag::Corp;
    } else if (IsAlliance(ownerID)) {
        flag = Calendar::Flag::Alliance;
    } else if (ownerID == ownerSystem) {
        flag = Calendar::Flag::CCP;
    } else {
        flag = Calendar::Flag::Automated;
    }

    EvE::TimeParts data = GetTimeParts(dateTime);

    // title and text are player input
    std::string eTitle, eDesc;
    sDatabase.DoEscapeString(eTitle, title);
    sDatabase.DoEscapeString(eDesc, description);
    std::string dur(duration ? std::to_string(duration) : "NULL");

    uint32 eventID(0);
    DBerror err;
    if (!sDatabase.RunQueryLID(err, eventID,
        "INSERT INTO sysCalendarEvents(ownerID, creatorID, eventDateTime, eventDuration, importance,"
        " eventTitle, eventText, flag, month, year, dateModified)"
        " VALUES (%u, %u, %lli, %s, %u, '%s', '%s', %u, %u, %u, %lli)",
        ownerID, creatorID, dateTime, dur.c_str(), important ? 1 : 0, eTitle.c_str(), eDesc.c_str(), flag,
        data.month, data.year, (int64)GetFileTimeNow()))
    {
        codelog(DATABASE__ERROR, "Error in SaveNewEvent query: %s", err.c_str());
        return 0;
    }

    return eventID;
}

// for system/auto events
uint32 CalendarDB::SaveSystemEvent(uint32 ownerID, uint32 creatorID, int64 startDateTime, uint8 autoEventType,
                                   std::string title, std::string description, bool important/*false*/)
{
    EvE::TimeParts data = EvE::TimeParts();
    data = GetTimeParts(startDateTime);

    // the text holds player-chosen names (characters, structures)
    std::string eTitle, eDesc;
    sDatabase.DoEscapeString(eTitle, title);
    sDatabase.DoEscapeString(eDesc, description);

    uint32 eventID(0);
    DBerror err;
    sDatabase.RunQueryLID(err, eventID,
        "INSERT INTO sysCalendarEvents(ownerID, creatorID, eventDateTime, autoEventType,"
        " eventTitle, eventText, flag, month, year, importance)"
        " VALUES (%u, %u, %lli, %u, '%s', '%s', %u, %u, %u, %u)",
        ownerID, creatorID, startDateTime, autoEventType, eTitle.c_str(), eDesc.c_str(),
        Calendar::Flag::Automated, data.month, data.year, important?1:0);

    return eventID;
}

bool CalendarDB::GetEventInfo(uint32 eventID, Calendar::EventInfo& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT eventID, ownerID, creatorID, COALESCE(flag, 0), eventDateTime, COALESCE(eventDuration, 0), eventTitle,"
        " CAST(importance AS UNSIGNED), CAST(isDeleted AS UNSIGNED), COALESCE(autoEventType, 0)"
        " FROM sysCalendarEvents WHERE eventID = %u", eventID))
    {
        codelog(DATABASE__ERROR, "Error in GetEventInfo query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    into.eventID = row.GetUInt(0);
    into.ownerID = row.GetUInt(1);
    into.creatorID = row.GetUInt(2);
    into.flag = row.GetUInt(3);
    into.dateTime = row.GetInt64(4);
    into.duration = row.GetUInt(5);
    into.title = row.GetText(6);
    into.important = (row.GetUInt(7) != 0);
    into.isDeleted = (row.GetUInt(8) != 0);
    into.autoEventType = row.GetUInt(9);
    return true;
}

void CalendarDB::UpdateEvent(uint32 eventID, int64 dateTime, uint16 duration, const std::string& title, const std::string& description, bool important)
{
    EvE::TimeParts data = GetTimeParts(dateTime);
    std::string eTitle, eDesc;
    sDatabase.DoEscapeString(eTitle, title);
    sDatabase.DoEscapeString(eDesc, description);
    std::string dur(duration ? std::to_string(duration) : "NULL");

    DBerror err;
    sDatabase.RunQuery(err,
        "UPDATE sysCalendarEvents SET eventDateTime = %lli, eventDuration = %s, eventTitle = '%s', eventText = '%s',"
        " importance = %u, month = %u, year = %u, dateModified = %lli"
        " WHERE eventID = %u",
        dateTime, dur.c_str(), eTitle.c_str(), eDesc.c_str(), important ? 1 : 0, data.month, data.year,
        (int64)GetFileTimeNow(), eventID);
}

void CalendarDB::ResetResponsesOnReschedule(uint32 eventID)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "UPDATE sysCalendarResponses AS r JOIN sysCalendarEvents AS e ON e.eventID = r.eventID"
        " SET r.response = %u WHERE r.eventID = %u AND r.response IN (%u, %u) AND r.charID != e.ownerID",
        Calendar::Response::Undecided, eventID, Calendar::Response::Accepted, Calendar::Response::Maybe);
}

void CalendarDB::GetInvitees(uint32 eventID, std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT characterID FROM sysCalendarEventInvitees WHERE eventID = %u", eventID))
        return;
    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

bool CalendarDB::IsInvited(uint32 eventID, uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT 1 FROM sysCalendarEventInvitees WHERE eventID = %u AND characterID = %u", eventID, charID))
        return false;
    return (res.GetRowCount() > 0);
}

void CalendarDB::AddInvitees(uint32 eventID, const std::vector<uint32>& charIDs)
{
    if (charIDs.empty())
        return;
    std::ostringstream inv, rsp;
    bool first(true);
    for (auto cur : charIDs) {
        if (!first) {
            inv << ",";
            rsp << ",";
        }
        first = false;
        inv << "(" << eventID << "," << cur << ")";
        rsp << "(" << eventID << "," << cur << "," << (uint32)Calendar::Response::Undecided << ")";
    }
    DBerror err;
    sDatabase.RunQuery(err, "INSERT IGNORE INTO sysCalendarEventInvitees (eventID, characterID) VALUES %s", inv.str().c_str());
    sDatabase.RunQuery(err,
        "INSERT INTO sysCalendarResponses (eventID, charID, response) VALUES %s"
        " ON DUPLICATE KEY UPDATE response = VALUES(response)", rsp.str().c_str());
}

void CalendarDB::RemoveInvitees(uint32 eventID, const std::vector<uint32>& charIDs)
{
    if (charIDs.empty())
        return;
    std::ostringstream ids, rsp;
    ids << "0";
    bool first(true);
    for (auto cur : charIDs) {
        ids << "," << cur;
        if (!first)
            rsp << ",";
        first = false;
        rsp << "(" << eventID << "," << cur << "," << (uint32)Calendar::Response::Uninvited << ")";
    }
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM sysCalendarEventInvitees WHERE eventID = %u AND characterID IN (%s)", eventID, ids.str().c_str());
    sDatabase.RunQuery(err,
        "INSERT INTO sysCalendarResponses (eventID, charID, response) VALUES %s"
        " ON DUPLICATE KEY UPDATE response = VALUES(response)", rsp.str().c_str());
}

PyRep* CalendarDB::EventListFromResult(DBQueryResult& res)
{
    if (res.GetRowCount() < 1)
        return nullptr;

    DBResultRow row;
    PyList* list = new PyList();
    while (res.GetRow(row)) {
        PyDict* dict = new PyDict();
            dict->SetItemString("eventID",              new PyInt(row.GetInt(0)));
            dict->SetItemString("ownerID",              new PyInt(row.GetInt(1)));
            dict->SetItemString("eventDateTime",        new PyLong(row.GetInt64(2)));
            dict->SetItemString("dateModified",         row.IsNull(3) ? PyStatic.NewNone() : new PyLong(row.GetInt64(3)));
            dict->SetItemString("eventDuration",        row.IsNull(4) ? PyStatic.NewNone() : new PyInt(row.GetInt(4)));
            dict->SetItemString("importance",           new PyBool(row.GetBool(5)));
            dict->SetItemString("eventTitle",           new PyWString(std::string(row.GetText(6))));
            dict->SetItemString("flag",                 new PyInt(row.GetInt(7)));
            // client patch to allow non-corp automated events for ram jobs
            if (row.GetInt(7) == Calendar::Flag::Automated)
                dict->SetItemString("autoEventType",    new PyInt(row.GetInt(8)));
            dict->SetItemString("isDeleted",            new PyBool(row.GetBool(9)));
        list->AddItem(new PyObject("util.KeyVal",       dict));
    }

    return list;
}

PyRep* CalendarDB::GetEventList(uint32 ownerID, uint32 month, uint32 year)
{
    if (ownerID == 0)
        return nullptr;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT eventID, ownerID, eventDateTime, dateModified, eventDuration, importance, eventTitle, flag,"
        " autoEventType, isDeleted"
        " FROM sysCalendarEvents WHERE ownerID = %u AND month = %u AND year = %u", ownerID, month, year))
    {
        codelog(DATABASE__ERROR, "Error in GetEventList query: %s", res.error.c_str());
        return nullptr;
    }

    return EventListFromResult(res);
}

PyRep* CalendarDB::GetInvitedEventList(uint32 charID, uint32 month, uint32 year)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT e.eventID, e.ownerID, e.eventDateTime, e.dateModified, e.eventDuration, e.importance, e.eventTitle, e.flag,"
        " e.autoEventType, e.isDeleted"
        " FROM sysCalendarEvents AS e"
        " JOIN sysCalendarEventInvitees AS i ON i.eventID = e.eventID"
        " WHERE i.characterID = %u AND e.ownerID != %u AND e.month = %u AND e.year = %u", charID, charID, month, year))
    {
        codelog(DATABASE__ERROR, "Error in GetInvitedEventList query: %s", res.error.c_str());
        return nullptr;
    }

    return EventListFromResult(res);
}

PyRep* CalendarDB::GetEventDetails(uint32 eventID)
{
    if (eventID == 0)
        return nullptr;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT ownerID, creatorID, eventText"
        " FROM sysCalendarEvents WHERE eventID = %u", eventID))
    {
        codelog(DATABASE__ERROR, "Error in GetEventList query: %s", res.error.c_str());
        return nullptr;
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return nullptr;

    PyDict* dict = new PyDict();
        dict->SetItemString("eventID",          new PyInt(eventID));
        dict->SetItemString("ownerID",          new PyInt(row.GetInt(0)));
        dict->SetItemString("creatorID",        new PyInt(row.GetInt(1)));
        dict->SetItemString("eventText",        new PyWString(std::string(row.GetText(2))));

    return new PyObject("util.KeyVal", dict);
}

void CalendarDB::SaveEventResponse(uint32 charID, uint32 eventID, uint32 response)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO `sysCalendarResponses`(`eventID`, `charID`, `response`)"
        " VALUES (%u, %u, %u)"
        " ON DUPLICATE KEY UPDATE response = VALUES(response)", eventID, charID, response);
}

PyRep* CalendarDB::GetResponsesForCharacter(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,"SELECT eventID, response FROM sysCalendarResponses WHERE charID = %u", charID)) {
        codelog(DATABASE__ERROR, "Error in GetEventList query: %s", res.error.c_str());
        return nullptr;
    }

    DBResultRow row;
    PyList *list = new PyList();
    while (res.GetRow(row)) {
        // list char response for each event
        PyDict* dict = new PyDict();
            dict->SetItemString("eventID", new PyInt(row.GetInt(0)));
            dict->SetItemString("status",  new PyInt(row.GetInt(1)));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }

    return list;
}

PyRep* CalendarDB::GetResponsesToEvent(uint32 eventID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT charID, response FROM sysCalendarResponses WHERE eventID = %u AND response != %u"
        " UNION ALL"
        " SELECT i.characterID, %u FROM sysCalendarEventInvitees AS i"
        " WHERE i.eventID = %u AND NOT EXISTS (SELECT 1 FROM sysCalendarResponses AS r WHERE r.eventID = i.eventID AND r.charID = i.characterID)",
        eventID, Calendar::Response::Uninvited, Calendar::Response::Undecided, eventID))
    {
        codelog(DATABASE__ERROR, "Error in GetEventList query: %s", res.error.c_str());
        return nullptr;
    }

    DBResultRow row;
    PyList *list = new PyList();
    while (res.GetRow(row)) {
        // list char response for each event
        PyDict* dict = new PyDict();
            dict->SetItemString("characterID", new PyInt(row.GetInt(0)));
            dict->SetItemString("status",  new PyInt(row.GetInt(1)));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }

    return list;
}

uint32 CalendarDB::CountUpcomingEvents(uint32 charID, uint32 corpID, uint32 allianceID)
{
    EvE::TimeParts now = GetTimeParts();
    uint16 nextYear(now.year);
    uint8 nextMonth(now.month + 1);
    if (nextMonth > Calendar::Month::December) {
        nextMonth = Calendar::Month::January;
        ++nextYear;
    }

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT COUNT(*) FROM sysCalendarEvents AS e"
        " WHERE e.isDeleted = 0 AND e.eventDateTime > %lli"
        " AND ((e.year = %u AND e.month = %u) OR (e.year = %u AND e.month = %u))"
        " AND (e.ownerID IN (%u, %u, %u, %u)"
        "  OR EXISTS (SELECT 1 FROM sysCalendarEventInvitees AS i WHERE i.eventID = e.eventID AND i.characterID = %u))"
        " AND NOT EXISTS (SELECT 1 FROM sysCalendarResponses AS r WHERE r.eventID = e.eventID AND r.charID = %u AND r.response = %u)",
        (int64)GetFileTimeNow(), now.year, now.month, nextYear, nextMonth, charID, corpID, allianceID, ownerSystem,
        charID, charID, Calendar::Response::Declined))
    {
        codelog(DATABASE__ERROR, "Error in CountUpcomingEvents query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}
