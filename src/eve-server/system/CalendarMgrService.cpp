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
#include "Client.h"
#include "EntityList.h"
#include "account/AccountService.h"
#include "corporation/CorpMgr.h"
#include "mail/MailMgr.h"
#include "system/CalendarDB.h"
#include "system/CalendarMgrService.h"


CalendarMgrService::CalendarMgrService() :
    Service("calendarMgr", eAccessLevel_Character)
{
    this->Add("CreatePersonalEvent", &CalendarMgrService::CreatePersonalEvent);
    this->Add("CreateCorporationEvent", &CalendarMgrService::CreateCorporationEvent);
    this->Add("CreateAllianceEvent", &CalendarMgrService::CreateAllianceEvent);
    this->Add("UpdateEventParticipants", &CalendarMgrService::UpdateEventParticipants);
    this->Add("EditPersonalEvent", &CalendarMgrService::EditPersonalEvent);
    this->Add("EditCorporationEvent", &CalendarMgrService::EditCorporationEvent);
    this->Add("EditAllianceEvent", &CalendarMgrService::EditAllianceEvent);
    this->Add("GetResponsesForCharacter", &CalendarMgrService::GetResponsesForCharacter);
    this->Add("SendEventResponse", &CalendarMgrService::SendEventResponse);
    this->Add("DeleteEvent", &CalendarMgrService::DeleteEvent);
    this->Add("GetResponsesToEvent", &CalendarMgrService::GetResponsesToEvent);
}

/* helpers */
// code points in a utf-8 string
static size_t Utf8Length(const std::string& str)
{
    size_t len(0);
    for (auto c : str)
        if ((c & 0xC0) != 0x80)
            ++len;
    return len;
}

void CalendarMgrService::ValidateEvent(int64 dateTime, std::string& title, std::string& description)
{
    if (title.find_first_not_of(" \t\r\n") == std::string::npos)
        throw UserError ("CalendarEventMustSpecifyTitle");
    if (Utf8Length(title) > Calendar::MaxTitleSize)
        throw UserError ("CalendarTitleTooLong");
    MailMgr::TruncateUtf8(description, Calendar::MaxDescrSize);

    // the client checks to the minute; allow for clock differences
    int64 now((int64)GetFileTimeNow());
    if (dateTime < now - EvE::Time::Hour)
        throw UserError ("CalendarCannotPlanThePast");

    // client IsTooFarInFuture:  no more than calendarViewRangeInMonths calendar months ahead
    EvE::TimeParts nowParts = GetTimeParts();
    EvE::TimeParts then = GetTimeParts(dateTime);
    if ((then.year * 12 + then.month) - (nowParts.year * 12 + nowParts.month) > Calendar::ViewRangeInMonths)
        throw UserError ("CalendarTooFarIntoFuture")
                .AddFormatValue ("numMonths", new PyInt(Calendar::ViewRangeInMonths));
}

bool CalendarMgrService::CanModify(Client* pClient, const Calendar::EventInfo& info)
{
    if (IsCharacterID(info.ownerID))
        return (info.ownerID == pClient->GetCharacterID());
    // corp and alliance events: the owner is the editor's corp/alliance and the editor manages chat (eveCalendar.py)
    if (!sCorpMgr.HasRole(pClient->GetCorpRole(), Corp::Role::ChatManager))
        return false;
    if (IsCorp(info.ownerID) and (info.flag != Calendar::Flag::Automated))
        return (info.ownerID == (uint32)pClient->GetCorporationID());
    if (IsAlliance(info.ownerID))
        return (info.ownerID == (uint32)pClient->GetAllianceID());
    return false;
}

bool CalendarMgrService::CanSee(Client* pClient, const Calendar::EventInfo& info)
{
    if (info.ownerID == ownerSystem)
        return true;
    if (info.ownerID == pClient->GetCharacterID())
        return true;
    if (info.ownerID == (uint32)pClient->GetCorporationID())
        return true;
    if ((pClient->GetAllianceID() != 0) and (info.ownerID == (uint32)pClient->GetAllianceID()))
        return true;
    return CalendarDB::IsInvited(info.eventID, pClient->GetCharacterID());
}

void CalendarMgrService::GetAudience(const Calendar::EventInfo& info, std::vector<Client*>& into)
{
    if (IsCharacterID(info.ownerID)) {
        std::vector<uint32> chars(1, info.ownerID);
        CalendarDB::GetInvitees(info.eventID, chars);
        for (auto cur : chars) {
            Client* pClient(sEntityList.FindClientByCharID(cur));
            if (pClient != nullptr)
                into.push_back(pClient);
        }
    } else if (IsCorp(info.ownerID)) {
        sEntityList.GetCorpClients(into, info.ownerID);
    } else if (IsAlliance(info.ownerID)) {
        std::vector<Client*> all;
        sEntityList.GetClients(all);
        for (auto cur : all)
            if ((uint32)cur->GetAllianceID() == info.ownerID)
                into.push_back(cur);
    }
}

PyTuple* CalendarMgrService::NewEventTuple(const Calendar::EventInfo& info)
{
    // OnNewCalendarEvent(eventID, ownerID, eventDateTime, eventDuration, eventTitle, importance[, autoEventType])
    PyTuple* tuple = new PyTuple(6);
        tuple->SetItem(0, new PyInt(info.eventID));
        tuple->SetItem(1, new PyInt(info.ownerID));
        tuple->SetItem(2, new PyLong(info.dateTime));
        tuple->SetItem(3, info.duration ? (PyRep*)new PyInt(info.duration) : PyStatic.NewNone());
        tuple->SetItem(4, new PyWString(info.title));
        tuple->SetItem(5, new PyBool(info.important));
    return tuple;
}

void CalendarMgrService::SendNewEvent(const Calendar::EventInfo& info, const std::vector<Client*>& clients)
{
    for (auto cur : clients) {
        PyTuple* tuple = NewEventTuple(info);
        cur->SendNotification("OnNewCalendarEvent", "charid", &tuple, false);
    }
}

void CalendarMgrService::SendRemoveEvent(uint32 eventID, int64 dateTime, bool isDeleted, const std::vector<Client*>& clients)
{
    // OnRemoveCalendarEvent(eventID, eventDateTime, isDeleted):  deleted events stay listed as cancelled, uninvited ones go
    for (auto cur : clients) {
        PyTuple* tuple = new PyTuple(3);
            tuple->SetItem(0, new PyInt(eventID));
            tuple->SetItem(1, new PyLong(dateTime));
            tuple->SetItem(2, new PyBool(isDeleted));
        cur->SendNotification("OnRemoveCalendarEvent", "charid", &tuple, false);
    }
}

// player character ids from a client list, without duplicates
static void GetCharIDs(PyRep* rep, std::vector<uint32>& into)
{
    std::set<uint32> seen;
    if (rep == nullptr)
        return;
    std::vector<PyRep*> items;
    if (rep->IsList()) {
        for (PyList::const_iterator itr = rep->AsList()->begin(); itr != rep->AsList()->end(); ++itr)
            items.push_back(*itr);
    } else if (rep->IsTuple()) {
        for (PyTuple::const_iterator itr = rep->AsTuple()->begin(); itr != rep->AsTuple()->end(); ++itr)
            items.push_back(*itr);
    }
    for (auto cur : items) {
        uint32 id(PyRep::IntegerValueU32(cur));
        if (IsCharacterID(id) and seen.insert(id).second)
            into.push_back(id);
    }
}

static std::string Text(PyRep* rep)
{
    if ((rep == nullptr) or rep->IsNone())
        return "";
    return PyRep::StringContent(rep);
}

static uint16 Duration(PyRep* rep)
{
    // minutes, None = not specified
    if ((rep == nullptr) or rep->IsNone())
        return 0;
    int64 value(PyRep::IntegerValue(rep));
    if ((value < 0) or (value > 24 * 60))
        return 0;
    return (uint16)value;
}

/* calls */
PyResult CalendarMgrService::GetResponsesForCharacter(PyCallArgs& call) {
    return CalendarDB::GetResponsesForCharacter(call.client->GetCharacterID());
}

PyResult CalendarMgrService::GetResponsesToEvent(PyCallArgs& call, PyInt* eventID, PyInt* ownerID)
{
    Calendar::EventInfo info = Calendar::EventInfo();
    if (!CalendarDB::GetEventInfo(eventID->value(), info) or !CanSee(call.client, info))
        return new PyList();
    return CalendarDB::GetResponsesToEvent(eventID->value());
}

PyResult CalendarMgrService::DeleteEvent(PyCallArgs& call, PyInt* eventID, PyInt* ownerID)
{
    Calendar::EventInfo info = Calendar::EventInfo();
    if (!CalendarDB::GetEventInfo(eventID->value(), info))
        throw UserError ("CalendarNoSuchEvent");
    if (!CanModify(call.client, info))
        throw UserError ("CalendarEventCannotModify");
    if (info.isDeleted)
        return nullptr;

    CalendarDB::DeleteEvent(info.eventID);

    // everyone who sees it (the deleting client doesn't update itself)
    std::vector<Client*> clients;
    GetAudience(info, clients);
    SendRemoveEvent(info.eventID, info.dateTime, true, clients);
    return nullptr;
}

PyResult CalendarMgrService::SendEventResponse(PyCallArgs& call, PyInt* eventID, PyInt* ownerID, PyInt* response)
{
    int32 answer(response->value());
    if ((answer != Calendar::Response::Declined) and (answer != Calendar::Response::Accepted) and (answer != Calendar::Response::Maybe))
        throw CustomError ("That is not a valid response.");

    Calendar::EventInfo info = Calendar::EventInfo();
    if (!CalendarDB::GetEventInfo(eventID->value(), info))
        throw UserError ("CalendarNoSuchEvent");
    if (!CanSee(call.client, info))
        throw CustomError ("You are not invited to this event.");

    CalendarDB::SaveEventResponse(call.client->GetCharacterID(), info.eventID, answer);
    return nullptr;
}

PyResult CalendarMgrService::CreatePersonalEvent(PyCallArgs& call, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important, PyRep* invitees)
{
    // newEventID = self.calendarMgr.CreatePersonalEvent(dateTime, duration, title, description, important, invitees)
    //  (wrapped in CSPAChargedAction when there are invitees).  the client adds the event to its own calendar
    uint32 charID(call.client->GetCharacterID());
    int64 when(PyRep::IntegerValue(dateTime));
    std::string sTitle(Text(title)), sDescr(Text(description));
    ValidateEvent(when, sTitle, sDescr);

    std::vector<uint32> guests, ids;
    GetCharIDs(invitees, ids);
    for (auto cur : ids)
        if (cur != charID)
            guests.push_back(cur);
    if (guests.size() > Calendar::MaxInvitees)
        throw CustomError ("You can invite at most %u characters to an event.", (uint32)Calendar::MaxInvitees);

    // CSPA:  guests that refuse the sender are dropped
    if (!guests.empty())
        AccountService::ChargeCSPA(call.client, guests, call);

    Calendar::EventInfo info = Calendar::EventInfo();
    info.eventID = CalendarDB::SaveNewEvent(charID, charID, when, Duration(duration), sTitle, sDescr, PyRep::IntegerValue(important) != 0);
    if (info.eventID == 0)
        throw CustomError ("The event could not be saved.");
    info.ownerID = charID;
    info.creatorID = charID;
    info.flag = Calendar::Flag::Personal;
    info.dateTime = when;
    info.duration = Duration(duration);
    info.title = sTitle;
    info.important = (PyRep::IntegerValue(important) != 0);

    CalendarDB::SaveEventResponse(charID, info.eventID, Calendar::Response::Accepted);
    CalendarDB::AddInvitees(info.eventID, guests);

    std::vector<Client*> clients;
    for (auto cur : guests) {
        Client* pClient(sEntityList.FindClientByCharID(cur));
        if (pClient != nullptr)
            clients.push_back(pClient);
    }
    SendNewEvent(info, clients);

    return new PyInt(info.eventID);
}

PyResult CalendarMgrService::CreateCorporationEvent(PyCallArgs& call, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important)
{
    return CreateGroupEvent(call, call.client->GetCorporationID(), dateTime, duration, title, description, important);
}

PyResult CalendarMgrService::CreateAllianceEvent(PyCallArgs& call, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important)
{
    if (!IsAlliance(call.client->GetAllianceID()))
        throw UserError ("CalendarEventCannotModify");
    return CreateGroupEvent(call, call.client->GetAllianceID(), dateTime, duration, title, description, important);
}

PyResult CalendarMgrService::CreateGroupEvent(PyCallArgs& call, uint32 ownerID, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important)
{
    // corp and alliance events need the chat manager role.  the creating client doesn't add them itself
    if (!sCorpMgr.HasRole(call.client->GetCorpRole(), Corp::Role::ChatManager))
        throw UserError ("CalendarEventCannotModify");
    if (IsNPCCorp(ownerID))
        throw UserError ("CalendarEventCannotModify");

    int64 when(PyRep::IntegerValue(dateTime));
    std::string sTitle(Text(title)), sDescr(Text(description));
    ValidateEvent(when, sTitle, sDescr);

    Calendar::EventInfo info = Calendar::EventInfo();
    info.eventID = CalendarDB::SaveNewEvent(ownerID, call.client->GetCharacterID(), when, Duration(duration), sTitle, sDescr, PyRep::IntegerValue(important) != 0);
    if (info.eventID == 0)
        throw CustomError ("The event could not be saved.");
    info.ownerID = ownerID;
    info.creatorID = call.client->GetCharacterID();
    info.flag = IsAlliance(ownerID) ? Calendar::Flag::Alliance : Calendar::Flag::Corp;
    info.dateTime = when;
    info.duration = Duration(duration);
    info.title = sTitle;
    info.important = (PyRep::IntegerValue(important) != 0);

    std::vector<Client*> clients;
    GetAudience(info, clients);
    SendNewEvent(info, clients);

    return new PyInt(info.eventID);
}

PyResult CalendarMgrService::EditPersonalEvent(PyCallArgs& call, PyRep* eventID, PyRep* oldDateTime, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important)
{
    //self.calendarMgr.EditPersonalEvent(eventID, oldDateTime, dateTime, duration, title, description, important)
    return EditEvent(call, eventID, dateTime, duration, title, description, important);
}

PyResult CalendarMgrService::EditCorporationEvent(PyCallArgs& call, PyRep* eventID, PyRep* oldDateTime, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important)
{
    // self.calendarMgr.EditCorporationEvent(eventID, oldDateTime, dateTime, duration, title, description, important)
    return EditEvent(call, eventID, dateTime, duration, title, description, important);
}

PyResult CalendarMgrService::EditAllianceEvent(PyCallArgs& call, PyRep* eventID, PyRep* oldDateTime, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important)
{
    //self.calendarMgr.EditAllianceEvent(eventID, oldDateTime, dateTime, duration, title, description, important)
    return EditEvent(call, eventID, dateTime, duration, title, description, important);
}

PyResult CalendarMgrService::EditEvent(PyCallArgs& call, PyRep* eventID, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important)
{
    Calendar::EventInfo info = Calendar::EventInfo();
    if (!CalendarDB::GetEventInfo(PyRep::IntegerValueU32(eventID), info))
        throw UserError ("CalendarNoSuchEvent");
    if (!CanModify(call.client, info))
        throw UserError ("CalendarEventCannotModify");
    if (info.isDeleted)
        throw UserError ("CalendarCannotEditDeleted");
    if (info.dateTime < (int64)GetFileTimeNow())
        throw UserError ("CalendarCannotEditPast");

    int64 when(PyRep::IntegerValue(dateTime));
    std::string sTitle(Text(title)), sDescr(Text(description));
    ValidateEvent(when, sTitle, sDescr);

    int64 oldWhen(info.dateTime);
    info.dateTime = when;
    info.duration = Duration(duration);
    info.title = sTitle;
    info.important = (PyRep::IntegerValue(important) != 0);
    CalendarDB::UpdateEvent(info.eventID, info.dateTime, info.duration, info.title, sDescr, info.important);
    // a new start time asks everyone who accepted (or maybe) again
    if (oldWhen != when)
        CalendarDB::ResetResponsesOnReschedule(info.eventID);

    // OnEditCalendarEvent(eventID, ownerID, oldEventDateTime, eventDateTime, eventDuration, eventTitle, importance) to everyone who sees it
    std::vector<Client*> clients;
    GetAudience(info, clients);
    for (auto cur : clients) {
        PyTuple* tuple = new PyTuple(7);
            tuple->SetItem(0, new PyInt(info.eventID));
            tuple->SetItem(1, new PyInt(info.ownerID));
            tuple->SetItem(2, new PyLong(oldWhen));
            tuple->SetItem(3, new PyLong(info.dateTime));
            tuple->SetItem(4, info.duration ? (PyRep*)new PyInt(info.duration) : PyStatic.NewNone());
            tuple->SetItem(5, new PyWString(info.title));
            tuple->SetItem(6, new PyBool(info.important));
        cur->SendNotification("OnEditCalendarEvent", "charid", &tuple, false);
    }
    return nullptr;
}

PyResult CalendarMgrService::UpdateEventParticipants(PyCallArgs& call, PyRep* eventID, PyRep* charsToAdd, PyRep* charsToRemove)
{
    // self.calendarMgr.UpdateEventParticipants(eventID, charsToAdd, charsToRemove)  (wrapped in CSPAChargedAction when adding)
    Calendar::EventInfo info = Calendar::EventInfo();
    if (!CalendarDB::GetEventInfo(PyRep::IntegerValueU32(eventID), info))
        throw UserError ("CalendarNoSuchEvent");
    if (!IsCharacterID(info.ownerID) or !CanModify(call.client, info))
        throw UserError ("CalendarEventCannotModify");
    if (info.isDeleted)
        throw UserError ("CalendarCannotEditDeleted");

    uint32 charID(call.client->GetCharacterID());
    std::vector<uint32> current, add, remove, ids;
    CalendarDB::GetInvitees(info.eventID, current);
    std::set<uint32> invited(current.begin(), current.end());

    GetCharIDs(charsToRemove, ids);
    for (auto cur : ids)
        if (invited.erase(cur) > 0)
            remove.push_back(cur);
    ids.clear();
    GetCharIDs(charsToAdd, ids);
    for (auto cur : ids)
        if ((cur != charID) and (invited.find(cur) == invited.end()))
            add.push_back(cur);
    if (invited.size() + add.size() > Calendar::MaxInvitees)
        throw CustomError ("You can invite at most %u characters to an event.", (uint32)Calendar::MaxInvitees);

    // CSPA:  new guests that refuse the sender are dropped
    if (!add.empty())
        AccountService::ChargeCSPA(call.client, add, call);

    CalendarDB::RemoveInvitees(info.eventID, remove);
    CalendarDB::AddInvitees(info.eventID, add);

    std::vector<Client*> clients;
    for (auto cur : add) {
        Client* pClient(sEntityList.FindClientByCharID(cur));
        if (pClient != nullptr)
            clients.push_back(pClient);
    }
    SendNewEvent(info, clients);
    clients.clear();
    for (auto cur : remove) {
        Client* pClient(sEntityList.FindClientByCharID(cur));
        if (pClient != nullptr)
            clients.push_back(pClient);
    }
    SendRemoveEvent(info.eventID, info.dateTime, false, clients);
    return nullptr;
}
