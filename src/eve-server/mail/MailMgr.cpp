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
*/

#include "eve-server.h"

#include "EVE_Mail.h"
#include "Client.h"
#include "EntityList.h"
#include "mail/MailDB.h"
#include "mail/MailMgr.h"

void MailMgr::ComputeMasks(uint32 charID, uint32 senderID, const std::vector<int32>& toChars, int32 toListID,
                           int32 toCorpOrAllianceID, uint32& statusMask, uint32& labelMask)
{
    // mirrors mailSvc.OnMailSent, so a later SyncMail agrees with what the client made of the push
    labelMask = 0;
    if (std::find(toChars.begin(), toChars.end(), (int32)charID) != toChars.end())
        labelMask = mailLabelInbox;
    if (charID == senderID) {
        if ((labelMask == 0) and (toCorpOrAllianceID == 0) and (toListID == 0))
            statusMask |= mailStatusMaskRead;
        labelMask |= mailLabelSent;
    }
    if (toCorpOrAllianceID != 0) {
        // client util.IsCorporation() is true for any corp id
        if (IsCorp(toCorpOrAllianceID)) {
            labelMask |= mailLabelCorporation;
        } else {
            labelMask |= mailLabelAlliance;
        }
    }
}

uint32 MailMgr::SendMail(uint32 senderID, uint32 toCharID, const std::string& title, const std::string& body)
{
    std::vector<int32> toChars;
    toChars.push_back(toCharID);
    return SendMail(senderID, toChars, 0, 0, title, body);
}

uint32 MailMgr::SendMail(uint32 senderID, const std::vector<int32>& toCharacterIDs, int32 toListID, int32 toCorpOrAllianceID,
                         const std::string& title, const std::string& body, uint32 extraStatus/*0*/, uint32 noNotifyID/*0*/,
                         const std::vector<uint32>* overrideRecipients/*nullptr*/)
{
    if (toListID < 0)
        toListID = 0;
    if (toCorpOrAllianceID < 0)
        toCorpOrAllianceID = 0;

    // direct recipients: existing characters only, in the order given, no repeats
    std::vector<int32> wanted, existing, toChars;
    for (auto cur : toCharacterIDs)
        if (IsCharacterID(cur) and (std::find(wanted.begin(), wanted.end(), cur) == wanted.end()))
            wanted.push_back(cur);
    MailDB::GetExistingCharacters(wanted, existing);
    for (auto cur : wanted)
        if (std::find(existing.begin(), existing.end(), cur) != existing.end())
            toChars.push_back(cur);

    std::set<uint32> recipients;
    for (auto cur : toChars)
        recipients.insert(cur);

    std::vector<uint32> groupIDs;
    if (overrideRecipients != nullptr) {
        groupIDs = *overrideRecipients;
    } else {
        if (toCorpOrAllianceID > 0) {
            if (IsCorp(toCorpOrAllianceID)) {
                MailDB::GetCorpMembersWithRoles(toCorpOrAllianceID, 0, groupIDs);
            } else if (IsAlliance(toCorpOrAllianceID)) {
                MailDB::GetAllianceMembersWithRoles(toCorpOrAllianceID, 0, groupIDs);
            }
        }
        if (toListID > 0)
            MailDB::GetListMemberIDs(toListID, groupIDs);
    }
    for (auto cur : groupIDs)
        if (IsCharacterID(cur))
            recipients.insert(cur);

    // the sender keeps a copy in "sent".  npc/agent/list senders don't
    if (IsCharacterID(senderID))
        recipients.insert(senderID);

    if (recipients.empty()) {
        _log(MAIL__WARNING, "MailMgr::SendMail() - mail '%s' from %u has no recipients", title.c_str(), senderID);
        return 0;
    }

    // body is stored as zlib compressed utf-8, the way GetBody hands it to the client
    Buffer bodyInput(body.begin(), body.end());
    Buffer bodyCompressed;
    DeflateData(bodyInput, bodyCompressed);
    std::string bodyStr(bodyCompressed.begin<char>(), bodyCompressed.end<char>());

    std::string toStr;
    for (auto cur : toChars) {
        if (!toStr.empty())
            toStr += ",";
        toStr += std::to_string(cur);
    }

    // client rounds its local copy to the minute (mailSvc.SendMail)
    int64 sentDate = ((int64)GetFileTimeNow() / Win32Time_Minute) * Win32Time_Minute;

    uint32 messageID = MailDB::InsertMessage(senderID, toStr, toListID, toCorpOrAllianceID, title, bodyStr, sentDate);
    if (messageID == 0)
        return 0;

    std::vector<MailDB::StatusRow> rows;
    rows.reserve(recipients.size());
    for (auto cur : recipients) {
        MailDB::StatusRow row;
            row.charID = cur;
            row.statusMask = extraStatus;
        ComputeMasks(cur, senderID, toChars, toListID, toCorpOrAllianceID, row.statusMask, row.labelMask);
        rows.push_back(row);
    }
    if (!MailDB::InsertStatusRows(messageID, rows))
        return messageID;

    // OnMailSent(messageID, senderID, sentDate, toCharacterIDs, toListID, toCorpOrAllianceID, title, statusMask)
    //  the client computes the labels itself from these values
    PyList* toList = new PyList();
    for (auto cur : toChars)
        toList->AddItem(new PyInt(cur));
    PyTuple* tuple = new PyTuple(8);
        tuple->SetItem(0, new PyInt(messageID));
        tuple->SetItem(1, new PyInt(senderID));
        tuple->SetItem(2, new PyLong(sentDate));
        tuple->SetItem(3, toList);
        tuple->SetItem(4, toListID > 0 ? (PyRep*)new PyInt(toListID) : PyStatic.NewNone());
        tuple->SetItem(5, toCorpOrAllianceID > 0 ? (PyRep*)new PyInt(toCorpOrAllianceID) : PyStatic.NewNone());
        tuple->SetItem(6, new PyWString(title));
        tuple->SetItem(7, new PyInt(extraStatus));

    for (auto cur : recipients) {
        if (cur == noNotifyID)
            continue;
        Client* pClient = sEntityList.FindClientByCharID(cur);
        if (pClient == nullptr)
            continue;
        PyTuple* payload = tuple->Clone()->AsTuple();
        pClient->SendNotification("OnMailSent", "charid", &payload);
    }
    PyDecRef(tuple);

    return messageID;
}

uint32 MailMgr::GetUnreadCount(uint32 charID)
{
    return MailDB::GetUnreadCount(charID);
}

void MailMgr::TruncateUtf8(std::string& str, size_t maxChars)
{
    size_t chars(0), i(0);
    while (i < str.size()) {
        if (chars == maxChars) {
            str.resize(i);
            return;
        }
        unsigned char c = str[i];
        if (c < 0x80) {
            i += 1;
        } else if ((c >> 5) == 0x6) {
            i += 2;
        } else if ((c >> 4) == 0xe) {
            i += 3;
        } else if ((c >> 3) == 0x1e) {
            i += 4;
        } else {
            i += 1;     // stray continuation byte
        }
        ++chars;
    }
}
