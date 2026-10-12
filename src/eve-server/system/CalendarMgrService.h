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


#ifndef EVEMU_SYSTEM_CALENDARMGR_H__
#define EVEMU_SYSTEM_CALENDARMGR_H__

#include "services/Service.h"
#include "Client.h"
#include "system/CalendarDB.h"

class CalendarMgrService : public Service <CalendarMgrService>
{
public:
    CalendarMgrService();

protected:
    PyResult GetResponsesForCharacter(PyCallArgs& call);
    PyResult GetResponsesToEvent(PyCallArgs& call, PyInt* eventID, PyInt* ownerID);
    PyResult DeleteEvent(PyCallArgs& call, PyInt* eventID, PyInt* ownerID);
    PyResult SendEventResponse(PyCallArgs& call, PyInt* eventID, PyInt* ownerID, PyInt* response);
    // the client sends None for "no duration" and unicode or str text, so these take PyRep
    PyResult CreatePersonalEvent(PyCallArgs& call, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important, PyRep* invitees);
    PyResult CreateCorporationEvent(PyCallArgs& call, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important);
    PyResult CreateAllianceEvent(PyCallArgs& call, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important);
    PyResult EditPersonalEvent(PyCallArgs& call, PyRep* eventID, PyRep* oldDateTime, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important);
    PyResult EditCorporationEvent(PyCallArgs& call, PyRep* eventID, PyRep* oldDateTime, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important);
    PyResult EditAllianceEvent(PyCallArgs& call, PyRep* eventID, PyRep* oldDateTime, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important);
    PyResult UpdateEventParticipants(PyCallArgs& call, PyRep* eventID, PyRep* charsToAdd, PyRep* charsToRemove);

private:
    PyResult CreateGroupEvent(PyCallArgs& call, uint32 ownerID, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important);
    PyResult EditEvent(PyCallArgs& call, PyRep* eventID, PyRep* dateTime, PyRep* duration, PyRep* title, PyRep* description, PyRep* important);

public:
    // title 1-40 characters, description cut to 500, not in the past, at most 12 calendar months ahead (throws the client's errors)
    static void ValidateEvent(int64 dateTime, std::string& title, std::string& description);
    // personal events: the owner.  corp/alliance events: their members with the chat manager role
    static bool CanModify(Client* pClient, const Calendar::EventInfo& info);
    // owner, invitee, member of the owning corp/alliance, or a system event
    static bool CanSee(Client* pClient, const Calendar::EventInfo& info);
    // the online characters that have the event in their calendar
    static void GetAudience(const Calendar::EventInfo& info, std::vector<Client*>& into);
    static PyTuple* NewEventTuple(const Calendar::EventInfo& info);
    static void SendNewEvent(const Calendar::EventInfo& info, const std::vector<Client*>& clients);
    static void SendRemoveEvent(uint32 eventID, int64 dateTime, bool isDeleted, const std::vector<Client*>& clients);
};

#endif // EVEMU_SYSTEM_CALENDARMGR_H__