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
    Author:        Luck, caytchen
*/

#include "eve-server.h"

#include "EVE_Mail.h"
#include "mail/MailMgr.h"
#include "mail/MailMgrService.h"

/* eve mail (mail window).  all calls work on the caller's own mail only.
 *  headers:  messageID, senderID, toCharacterIDs ("1,2,3"), toListID|None, toCorpOrAllianceID|None, title, sentDate
 *  status:   messageID, statusMask (mailStatusMask*), labelMask (mailLabel* + custom label bits)
 *  bodies are zlib compressed utf-8
 */

std::map<uint32, int64> MailMgrService::s_lastSent;

MailMgrService::MailMgrService() :
    Service("mailMgr", eAccessLevel_Character)
{
    this->Add("SendMail", &MailMgrService::SendMail);
    this->Add("PrimeOwners", &MailMgrService::PrimeOwners);
    this->Add("SyncMail", &MailMgrService::SyncMail);
    this->Add("GetMailHeaders", &MailMgrService::GetMailHeaders);
    this->Add("GetBody", &MailMgrService::GetBody);
    this->Add("MoveToTrash", &MailMgrService::MoveToTrash);
    this->Add("MoveFromTrash", &MailMgrService::MoveFromTrash);
    this->Add("MarkAsUnread", &MailMgrService::MarkAsUnread);
    this->Add("MarkAsRead", &MailMgrService::MarkAsRead);
    this->Add("MoveAllToTrash", &MailMgrService::MoveAllToTrash);
    this->Add("MoveToTrashByLabel", &MailMgrService::MoveToTrashByLabel);
    this->Add("MoveToTrashByList", &MailMgrService::MoveToTrashByList);
    this->Add("MarkAllAsUnread", &MailMgrService::MarkAllAsUnread);
    this->Add("MarkAsUnreadByLabel", &MailMgrService::MarkAsUnreadByLabel);
    this->Add("MarkAsUnreadByList", &MailMgrService::MarkAsUnreadByList);
    this->Add("MarkAllAsRead", &MailMgrService::MarkAllAsRead);
    this->Add("MarkAsReadByLabel", &MailMgrService::MarkAsReadByLabel);
    this->Add("MarkAsReadByList", &MailMgrService::MarkAsReadByList);
    this->Add("MoveAllFromTrash", &MailMgrService::MoveAllFromTrash);
    this->Add("EmptyTrash", &MailMgrService::EmptyTrash);
    this->Add("DeleteMail", &MailMgrService::DeleteMail);
    this->Add("GetLabels", &MailMgrService::GetLabels);
    this->Add("EditLabel", &MailMgrService::EditLabel);
    this->Add("CreateLabel", &MailMgrService::CreateLabel);
    this->Add("DeleteLabel", &MailMgrService::DeleteLabel);
    this->Add("AssignLabels", &MailMgrService::AssignLabels);
    this->Add("RemoveLabels", &MailMgrService::RemoveLabels);
}

// integer elements of a list of ids (anything else is skipped)
static void GetIDs(PyList* list, std::vector<int32>& into)
{
    for (PyList::const_iterator itr = list->begin(); itr != list->end(); ++itr)
        if ((*itr)->IsInt() or (*itr)->IsLong())
            into.push_back(PyRep::IntegerValue(*itr));
}

// None or 0 = no value
static int64 OptionalInt(PyRep* rep)
{
    if ((rep == nullptr) or rep->IsNone())
        return 0;
    return PyRep::IntegerValue(rep);
}

PyResult MailMgrService::SendMail(PyCallArgs &call, PyList* toCharacterIDs, PyRep* toListID, PyRep* toCorpOrAllianceID, PyRep* title, PyRep* body, PyRep* isReplyTo, PyRep* isForwardedFrom)
{
    // the client sends this thru util.CSPAChargedAction; CSPA charges (ContactCostNotApproved) are not implemented
    uint32 charID = call.client->GetCharacterID();

    int64 now = (int64)GetFileTimeNow();
    if (sConfig.world.mailDelay > 0) {
        std::map<uint32, int64>::iterator itr = s_lastSent.find(charID);
        if ((itr != s_lastSent.end()) and (now - itr->second < sConfig.world.mailDelay * Win32Time_Second))
            throw CustomError("You can only send one EVE mail every %u seconds.", (uint32)sConfig.world.mailDelay);
    }

    std::vector<int32> ids, toChars;
    GetIDs(toCharacterIDs, ids);
    if (ids.size() > mailMaxRecipients)
        throw UserError("EvemailMaxRecipients")
                .AddFormatValue("max", new PyInt(mailMaxRecipients))
                .AddFormatValue("name", new PyString(""));
    // agents, npcs and other non-characters can't get mail
    for (auto cur : ids)
        if (IsCharacterID(cur))
            toChars.push_back(cur);

    int32 listID = (int32)OptionalInt(toListID);
    int32 corpOrAllianceID = (int32)OptionalInt(toCorpOrAllianceID);
    if ((listID > 0) and (corpOrAllianceID > 0))
        throw UserError("EvemailMaxListOrCorp")
                .AddFormatValue("name", new PyString(""))
                .AddFormatValue("groupName", new PyString(""));

    if (corpOrAllianceID > 0) {
        if (IsCorp(corpOrAllianceID)) {
            if (corpOrAllianceID != call.client->GetCorporationID())
                throw UserError("EvemailCantSendToOtherCorp");
            if (IsNPCCorp(corpOrAllianceID))
                throw UserError("EvemailCantSendToNPCCorp");
        } else if (IsAlliance(corpOrAllianceID)) {
            if (corpOrAllianceID != call.client->GetAllianceID())
                throw UserError("EvemailCantSendToOtherAlliance");
            if ((call.client->GetCorpRole() & Corp::Role::ChatManager) != Corp::Role::ChatManager)
                throw UserError("EvemailCantSendToAllianceNoRole");
        } else {
            throw UserError("EvemailSendingFailed");
        }
    }

    if (listID > 0) {
        int8 role = MailDB::GetMemberRole(listID, charID);
        if (role < 0)
            throw UserError("EveMailCanOnlySendToOwnMailinglists");
        if (role == mailingListMemberMuted)
            throw CustomError("You are muted on this mailing list.");
    }

    if (toChars.empty() and (listID == 0) and (corpOrAllianceID == 0))
        throw UserError("EveMailNoCharFound").AddFormatValue("name", new PyString(""));

    std::string subject = PyRep::StringContent(title);
    if (subject.find_first_not_of(" \t\r\n") == std::string::npos)
        throw UserError("NoSubject");
    MailMgr::TruncateUtf8(subject, mailMaxSubjectSize);
    std::string text = PyRep::StringContent(body);
    MailMgr::TruncateUtf8(text, mailMaxTaggedBodySize);

    uint32 messageID = MailMgr::SendMail(charID, toChars, listID, corpOrAllianceID, subject, text, 0, charID);
    if (messageID == 0)
        throw UserError("EvemailSendingFailed");

    // the client marks the original mail locally (mailSvc.SendMail)
    int32 replyTo = (int32)OptionalInt(isReplyTo);
    if (replyTo > 0) {
        std::vector<int32> orig(1, replyTo);
        MailDB::SetStatusBits(charID, orig, mailStatusMaskReplied, true);
    }
    int32 forwardedFrom = (int32)OptionalInt(isForwardedFrom);
    if (forwardedFrom > 0) {
        std::vector<int32> orig(1, forwardedFrom);
        MailDB::SetStatusBits(charID, orig, mailStatusMaskForwarded, true);
    }

    s_lastSent[charID] = now;
    return new PyInt(messageID);
}

PyResult MailMgrService::PrimeOwners(PyCallArgs &call, PyList* ownerIDs)
{
    std::vector<int32> owners;
    GetIDs(ownerIDs, owners);
    return ServiceDB::PrimeOwners(owners);
}

PyResult MailMgrService::SyncMail(PyCallArgs &call, PyRep* first, PyRep* last)
{
    // first/last are the lowest/highest messageIDs the client has cached (None, 0 when it has none).
    //  newMail = every header above `last`; anything older the client is missing is fetched with GetMailHeaders
    uint32 lastID = (uint32)OptionalInt(last);
    uint32 charID = call.client->GetCharacterID();

    PyDict* dict = new PyDict();
        dict->SetItemString("newMail", MailDB::GetMailHeaders(charID, lastID));
        dict->SetItemString("oldMail", PyStatic.NewNone());
        dict->SetItemString("mailStatus", MailDB::GetMailStatus(charID));
    return new PyObject("util.KeyVal", dict);
}

PyResult MailMgrService::GetMailHeaders(PyCallArgs &call, PyList* messageIDs)
{
    std::vector<int32> ids;
    GetIDs(messageIDs, ids);
    return MailDB::GetMailHeadersByIDs(call.client->GetCharacterID(), ids);
}

PyResult MailMgrService::GetBody(PyCallArgs &call, PyInt* messageID, PyRep* isUnread)
{
    uint32 charID = call.client->GetCharacterID();
    PyString* body = MailDB::GetMailBody(charID, messageID->value());
    if (body == nullptr)
        return PyStatic.NewNone();

    if (isUnread->IsBool() ? isUnread->AsBool()->value() : (OptionalInt(isUnread) != 0)) {
        std::vector<int32> ids(1, messageID->value());
        MailDB::SetStatusBits(charID, ids, mailStatusMaskRead, true);
    }
    return body;
}

PyResult MailMgrService::MoveToTrash(PyCallArgs &call, PyList* messageIDs)
{
    std::vector<int32> ids;
    GetIDs(messageIDs, ids);
    MailDB::SetStatusBits(call.client->GetCharacterID(), ids, mailStatusMaskTrashed, true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MoveFromTrash(PyCallArgs &call, PyList* messageIDs)
{
    std::vector<int32> ids;
    GetIDs(messageIDs, ids);
    MailDB::SetStatusBits(call.client->GetCharacterID(), ids, mailStatusMaskTrashed, false);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MarkAsUnread(PyCallArgs &call, PyList* messageIDs)
{
    std::vector<int32> ids;
    GetIDs(messageIDs, ids);
    MailDB::SetStatusBits(call.client->GetCharacterID(), ids, mailStatusMaskRead, false);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MarkAsRead(PyCallArgs &call, PyList* messageIDs)
{
    std::vector<int32> ids;
    GetIDs(messageIDs, ids);
    MailDB::SetStatusBits(call.client->GetCharacterID(), ids, mailStatusMaskRead, true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MoveAllToTrash(PyCallArgs &call)
{
    MailDB::SetStatusBitsAll(call.client->GetCharacterID(), mailStatusMaskTrashed, true, false);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MoveAllFromTrash(PyCallArgs &call)
{
    MailDB::SetStatusBitsAll(call.client->GetCharacterID(), mailStatusMaskTrashed, false, false);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MoveToTrashByLabel(PyCallArgs &call, PyRep* labelID)
{
    MailDB::SetStatusBitsByLabel(call.client->GetCharacterID(), OptionalInt(labelID), mailStatusMaskTrashed, true, false);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MoveToTrashByList(PyCallArgs &call, PyInt* listID)
{
    MailDB::SetStatusBitsByList(call.client->GetCharacterID(), listID->value(), mailStatusMaskTrashed, true, false);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MarkAllAsRead(PyCallArgs &call)
{
    // the client only marks mail outside the trash
    MailDB::SetStatusBitsAll(call.client->GetCharacterID(), mailStatusMaskRead, true, true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MarkAllAsUnread(PyCallArgs &call)
{
    MailDB::SetStatusBitsAll(call.client->GetCharacterID(), mailStatusMaskRead, false, true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MarkAsReadByLabel(PyCallArgs &call, PyRep* labelID)
{
    MailDB::SetStatusBitsByLabel(call.client->GetCharacterID(), OptionalInt(labelID), mailStatusMaskRead, true, true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MarkAsUnreadByLabel(PyCallArgs &call, PyRep* labelID)
{
    MailDB::SetStatusBitsByLabel(call.client->GetCharacterID(), OptionalInt(labelID), mailStatusMaskRead, false, true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MarkAsReadByList(PyCallArgs &call, PyInt* listID)
{
    MailDB::SetStatusBitsByList(call.client->GetCharacterID(), listID->value(), mailStatusMaskRead, true, true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::MarkAsUnreadByList(PyCallArgs &call, PyInt* listID)
{
    MailDB::SetStatusBitsByList(call.client->GetCharacterID(), listID->value(), mailStatusMaskRead, false, true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::EmptyTrash(PyCallArgs &call)
{
    MailDB::EmptyTrash(call.client->GetCharacterID());
    return PyStatic.NewNone();
}

PyResult MailMgrService::DeleteMail(PyCallArgs &call, PyList* messageIDs)
{
    std::vector<int32> ids;
    GetIDs(messageIDs, ids);
    MailDB::DeleteMail(call.client->GetCharacterID(), ids);
    return PyStatic.NewNone();
}

PyResult MailMgrService::GetLabels(PyCallArgs &call)
{
    return MailDB::GetLabels(call.client->GetCharacterID());
}

PyResult MailMgrService::CreateLabel(PyCallArgs &call, PyRep* name, std::optional<PyRep*> color)
{
    uint32 charID = call.client->GetCharacterID();
    std::string labelName = PyRep::StringContent(name);
    if (labelName.empty())
        throw UserError("MailLabelMustProvideName");
    MailMgr::TruncateUtf8(labelName, mailMaxLabelSize);
    if (MailDB::LabelNameExists(charID, labelName))
        throw UserError("MailLabelNameExists");
    if (MailDB::GetLabelCount(charID) >= mailMaxNumLabels)
        throw CustomError("You cannot have more than %u labels.", (uint32)mailMaxNumLabels);

    int32 colorVal(0);
    bool hasColor = (color.has_value() and (color.value() != nullptr) and !color.value()->IsNone());
    if (hasColor)
        colorVal = (int32)PyRep::IntegerValue(color.value());

    uint32 labelID = MailDB::CreateLabel(charID, labelName, hasColor ? &colorVal : nullptr);
    if (labelID == 0)
        throw CustomError("You cannot have more than %u labels.", (uint32)mailMaxNumLabels);
    return new PyInt(labelID);
}

PyResult MailMgrService::EditLabel(PyCallArgs &call, PyRep* labelID, PyRep* name, PyRep* color)
{
    // None = unchanged.  the system labels (inbox, sent, corp, alliance) can only be renamed client side
    uint32 id = (uint32)OptionalInt(labelID);
    if ((id == 0) or ((id & mailLabelsSystem) != 0))
        return PyStatic.NewNone();

    uint32 charID = call.client->GetCharacterID();
    std::string labelName;
    bool hasName = !name->IsNone();
    if (hasName) {
        labelName = PyRep::StringContent(name);
        if (labelName.empty())
            throw UserError("MailLabelMustProvideName");
        MailMgr::TruncateUtf8(labelName, mailMaxLabelSize);
        if (MailDB::LabelNameExists(charID, labelName, id))
            throw UserError("MailLabelNameExists");
    }
    int32 colorVal(0);
    bool hasColor = !color->IsNone();
    if (hasColor)
        colorVal = (int32)PyRep::IntegerValue(color);

    MailDB::EditLabel(charID, id, hasName ? &labelName : nullptr, hasColor ? &colorVal : nullptr);
    return PyStatic.NewNone();
}

PyResult MailMgrService::DeleteLabel(PyCallArgs &call, PyRep* labelID)
{
    uint32 id = (uint32)OptionalInt(labelID);
    if ((id == 0) or ((id & mailLabelsSystem) != 0))
        return PyStatic.NewNone();
    MailDB::DeleteLabel(call.client->GetCharacterID(), id);
    return PyStatic.NewNone();
}

PyResult MailMgrService::AssignLabels(PyCallArgs &call, PyList* messageIDs, PyRep* labelID)
{
    // labelID may be a sum of label bits (new mail with several labels)
    std::vector<int32> ids;
    GetIDs(messageIDs, ids);
    MailDB::SetLabelBits(call.client->GetCharacterID(), ids, OptionalInt(labelID), true);
    return PyStatic.NewNone();
}

PyResult MailMgrService::RemoveLabels(PyCallArgs &call, PyList* messageIDs, PyRep* labelID)
{
    std::vector<int32> ids;
    GetIDs(messageIDs, ids);
    MailDB::SetLabelBits(call.client->GetCharacterID(), ids, OptionalInt(labelID), false);
    return PyStatic.NewNone();
}
