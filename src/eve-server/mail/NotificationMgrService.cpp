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
    Author:     caytchen
*/

#include "eve-server.h"

#include "EVE_Mail.h"
#include "Client.h"
#include "mail/MailDB.h"
#include "mail/NotificationMgr.h"
#include "mail/NotificationMgrService.h"

// this service is part of mail and used with the 'notifications' tab of mail window.
// notifications are written by NotificationMgr::Send*() and stored in chrNotifications.
//  rows are a dbutil.CRowset: notificationID I4, typeID I4, senderID I4, receiverID I4, processed BOOL,
//  created FILETIME, data WSTR (yaml), deleted BOOL

// client const.notificationsMaxUpdated
static const size_t notificationsMaxUpdated = 100;

NotificationMgrService::NotificationMgrService() :
    Service("notificationMgr", eAccessLevel_Character)
{
    this->Add("GetByGroupID", &NotificationMgrService::GetByGroupID);
    this->Add("GetUnprocessed", &NotificationMgrService::GetUnprocessed);
    this->Add("MarkGroupAsProcessed", &NotificationMgrService::MarkGroupAsProcessed);
    this->Add("MarkAllAsProcessed", &NotificationMgrService::MarkAllAsProcessed);
    this->Add("MarkAsProcessed", &NotificationMgrService::MarkAsProcessed);
    this->Add("DeleteGroupNotifications", &NotificationMgrService::DeleteGroupNotifications);
    this->Add("DeleteAllNotifications", &NotificationMgrService::DeleteAllNotifications);
    this->Add("DeleteNotifications", &NotificationMgrService::DeleteNotifications);
}

// reads a list of notificationIDs, max notificationsMaxUpdated (the client enforces the same limit)
static void GetNotificationIDs(PyList* list, std::vector<int32>& into)
{
    if (list->size() > notificationsMaxUpdated)
        throw CustomError("You can only update %u notifications at a time.", (uint32)notificationsMaxUpdated);
    for (PyList::const_iterator itr = list->begin(); itr != list->end(); ++itr)
        into.push_back(PyRep::IntegerValue(*itr));
}

PyResult NotificationMgrService::GetByGroupID(PyCallArgs &call, PyInt* groupID)
{
    std::vector<uint16> types;
    if (groupID->value() == Notify::Groups::Unread)
        return MailDB::GetNotifications(call.client->GetCharacterID(), types, true);

    NotificationMgr::GetGroupTypes(groupID->value(), types);
    if (types.empty())
        types.push_back(0);     // unknown group -> no rows
    return MailDB::GetNotifications(call.client->GetCharacterID(), types, false);
}

PyResult NotificationMgrService::GetUnprocessed(PyCallArgs &call)
{
    // called when mail window's notifications tab opened, and for the neocom blink/unread counts
    std::vector<uint16> types;
    return MailDB::GetNotifications(call.client->GetCharacterID(), types, true);
}

PyResult NotificationMgrService::MarkGroupAsProcessed(PyCallArgs &call, PyInt* groupID)
{
    if (groupID->value() == Notify::Groups::Unread) {
        MailDB::MarkNotificationsProcessed(call.client->GetCharacterID(), nullptr, nullptr);
        return PyStatic.NewNone();
    }

    std::vector<uint16> types;
    NotificationMgr::GetGroupTypes(groupID->value(), types);
    if (!types.empty())
        MailDB::MarkNotificationsProcessed(call.client->GetCharacterID(), nullptr, &types);
    return PyStatic.NewNone();
}

PyResult NotificationMgrService::MarkAllAsProcessed(PyCallArgs &call)
{
    MailDB::MarkNotificationsProcessed(call.client->GetCharacterID(), nullptr, nullptr);
    return PyStatic.NewNone();
}

PyResult NotificationMgrService::MarkAsProcessed(PyCallArgs &call, PyList* notificationIDsToMarkAsRead)
{
    std::vector<int32> ids;
    GetNotificationIDs(notificationIDsToMarkAsRead, ids);
    if (!ids.empty())
        MailDB::MarkNotificationsProcessed(call.client->GetCharacterID(), &ids, nullptr);
    return PyStatic.NewNone();
}

PyResult NotificationMgrService::DeleteGroupNotifications(PyCallArgs &call, PyInt* groupID)
{
    if (groupID->value() == Notify::Groups::Unread) {
        // the unread "group" is every unprocessed notification
        MailDB::DeleteNotifications(call.client->GetCharacterID(), nullptr, nullptr, true);
        return PyStatic.NewNone();
    }

    std::vector<uint16> types;
    NotificationMgr::GetGroupTypes(groupID->value(), types);
    if (!types.empty())
        MailDB::DeleteNotifications(call.client->GetCharacterID(), nullptr, &types);
    return PyStatic.NewNone();
}

PyResult NotificationMgrService::DeleteAllNotifications(PyCallArgs &call)
{
    MailDB::DeleteNotifications(call.client->GetCharacterID(), nullptr, nullptr);
    return PyStatic.NewNone();
}

PyResult NotificationMgrService::DeleteNotifications(PyCallArgs &call, PyList* notificatinIDs)
{
    std::vector<int32> ids;
    GetNotificationIDs(notificatinIDs, ids);
    if (!ids.empty())
        MailDB::DeleteNotifications(call.client->GetCharacterID(), &ids, nullptr);
    return PyStatic.NewNone();
}
