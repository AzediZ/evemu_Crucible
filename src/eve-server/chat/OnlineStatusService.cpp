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
    Author:        Zhur, Allan
*/

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "EntityList.h"
#include "character/CharacterDB.h"
#include "chat/OnlineStatusService.h"

/* watchlist online state (address book).  a character's state is shown to the owners that have it on their
 *  watchlist (and, with chat.ContactStatusMutual, are on its watchlist too).
 *  GetInitialState rows: contactID I4, online I4.  pushes OnContactLoggedOn/OnContactLoggedOff(charID)
 */

OnlineStatusService::OnlineStatusService() :
    Service("onlineStatus", eAccessLevel_Character)
{
    this->Add("GetInitialState", &OnlineStatusService::GetInitialState);
    this->Add("GetOnlineStatus", &OnlineStatusService::GetOnlineStatus);
}

bool OnlineStatusService::CanSeeStatus(uint32 viewerID, uint32 targetID)
{
    if (!CharacterDB::IsInWatchlist(viewerID, targetID))
        return false;
    if (sConfig.chat.ContactStatusMutual and !CharacterDB::IsInWatchlist(targetID, viewerID))
        return false;
    return true;
}

void OnlineStatusService::NotifyStatusChange(uint32 charID, bool online)
{
    std::vector<uint32> watchers;
    CharacterDB::GetWatchers(charID, watchers);
    if (watchers.empty())
        return;

    std::vector<uint32> mutual;
    if (sConfig.chat.ContactStatusMutual)
        CharacterDB::GetWatchlist(charID, mutual);

    for (auto cur : watchers) {
        if (sConfig.chat.ContactStatusMutual and (std::find(mutual.begin(), mutual.end(), cur) == mutual.end()))
            continue;
        Client* pClient = sEntityList.FindClientByCharID(cur);
        if (pClient == nullptr)
            continue;
        PyTuple* tuple = new PyTuple(1);
            tuple->SetItem(0, new PyInt(charID));
        pClient->SendNotification(online ? "OnContactLoggedOn" : "OnContactLoggedOff", "charid", &tuple);
    }
}

PyResult OnlineStatusService::GetInitialState(PyCallArgs &call) {
    // online state of every character on the caller's watchlist.  the client indexes it by contactID
    DBRowDescriptor* header = new DBRowDescriptor();
        header->AddColumn("contactID", DBTYPE_I4);
        header->AddColumn("online", DBTYPE_I4);
    CRowSet* rowset = new CRowSet(&header);

    uint32 charID = call.client->GetCharacterID();
    std::vector<uint32> watchlist;
    CharacterDB::GetWatchlist(charID, watchlist);
    for (auto cur : watchlist) {
        if (!IsCharacterID(cur))
            continue;
        if (sConfig.chat.ContactStatusMutual and !CharacterDB::IsInWatchlist(cur, charID))
            continue;
        PyPackedRow* row = rowset->NewRow();
            row->SetField((uint32)0, new PyInt(cur));
            row->SetField(1, new PyInt(sEntityList.IsOnline(cur) ? 1 : 0));
    }

    return rowset;
}

PyResult OnlineStatusService::GetOnlineStatus(PyCallArgs &call, PyInt* characterID) {
    // the address book asks this for a contact it just put on the watchlist
    if (!IsCharacterID(characterID->value()) or !CanSeeStatus(call.client->GetCharacterID(), characterID->value()))
        return PyStatic.NewFalse();
    return sEntityList.PyIsOnline(characterID->value());
}
