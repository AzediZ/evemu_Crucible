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
#include "EntityList.h"
#include "mail/MailMgr.h"
#include "mail/MailingListMgrService.h"

/* eve mail mailing lists.
 *  roles (mailListUsers.role): mailingListMemberMuted, Default, Operator, Owner
 *  access (mailList.defaultAccess, mailListAccess per char/corp/alliance): mailingListBlocked, mailingListAllowed
 *  operators manage members, access and the welcome mail; only the owner deletes the list or grants/revokes operator.
 *  GMs act as owner on every list.
 */

MailingListMgrService::MailingListMgrService() :
    Service("mailingListsMgr", eAccessLevel_Character)
{
    this->Add("GetJoinedLists", &MailingListMgrService::GetJoinedLists);
    this->Add("Create", &MailingListMgrService::Create);
    this->Add("Join", &MailingListMgrService::Join);
    this->Add("Leave", &MailingListMgrService::Leave);
    this->Add("Delete", &MailingListMgrService::Delete);
    this->Add("KickMembers", &MailingListMgrService::KickMembers);
    this->Add("GetMembers", &MailingListMgrService::GetMembers);
    this->Add("SetEntityAccess", &MailingListMgrService::SetEntityAccess);
    this->Add("ClearEntityAccess", &MailingListMgrService::ClearEntityAccess);
    this->Add("SetMembersMuted", &MailingListMgrService::SetMembersMuted);
    this->Add("SetMembersOperator", &MailingListMgrService::SetMembersOperator);
    this->Add("SetMembersClear", &MailingListMgrService::SetMembersClear);
    this->Add("SetDefaultAccess", &MailingListMgrService::SetDefaultAccess);
    this->Add("GetInfo", &MailingListMgrService::GetInfo);
    this->Add("GetSettings", &MailingListMgrService::GetSettings);
    this->Add("GetWelcomeMail", &MailingListMgrService::GetWelcomeMail);
    this->Add("SaveWelcomeMail", &MailingListMgrService::SaveWelcomeMail);
    this->Add("SendWelcomeMail", &MailingListMgrService::SendWelcomeMail);
    this->Add("ClearWelcomeMail", &MailingListMgrService::ClearWelcomeMail);
}

void MailingListMgrService::GetKeyAndName(const std::string& input, std::string& key, std::string& displayName)
{
    // client util.GetKeyAndNormalize(): key = case folded, whitespace removed, last '\' part; name = whitespace runs collapsed
    key.clear();
    displayName.clear();
    char last = 0;
    for (char c : input) {
        if (std::isspace((unsigned char)c)) {
            if (c != last)
                displayName += c;
        } else {
            key += (char)std::tolower((unsigned char)c);
            displayName += c;
        }
        last = c;
    }
    size_t pos = key.rfind('\\');
    if (pos != std::string::npos)
        key = key.substr(pos + 1);
}

bool MailingListMgrService::IsGM(Client* pClient)
{
    return ((pClient->GetAccountRole() & Acct::Role::GMH) == Acct::Role::GMH);
}

int8 MailingListMgrService::GetRole(Client* pClient, uint32 listID)
{
    if (IsGM(pClient))
        return mailingListMemberOwner;
    return MailDB::GetMemberRole(listID, pClient->GetCharacterID());
}

void MailingListMgrService::RequireOperator(Client* pClient, uint32 listID)
{
    MailDB::MailListInfo info;
    if (!MailDB::GetList(listID, info))
        throw UserError("MailingListNoSuchList");
    if (GetRole(pClient, listID) < mailingListMemberOperator)
        throw CustomError("You must be an operator of this mailing list to do that.");
}

void MailingListMgrService::Notify(const std::vector<uint32>& charIDs, const char* notifyType, uint32 listID, int32 charID/*0*/)
{
    for (auto cur : charIDs) {
        Client* pClient = sEntityList.FindClientByCharID(cur);
        if (pClient == nullptr)
            continue;
        PyTuple* tuple = new PyTuple(charID ? 2 : 1);
            tuple->SetItem(0, new PyInt(listID));
        if (charID)
            tuple->SetItem(1, new PyInt(charID));
        pClient->SendNotification(notifyType, "charid", &tuple);
    }
}

void MailingListMgrService::SendWelcome(uint32 listID, const std::string& title, const std::string& body, const std::vector<uint32>& toIDs)
{
    // automated list mail: sender is the list, so the client names it with the list's display name.
    //  the welcome editor is plain text; the mail window shows tagged text
    std::string text;
    for (char c : body) {
        if (c == '\r')
            continue;
        if (c == '\n') {
            text += "<br>";
        } else {
            text += c;
        }
    }
    std::vector<int32> none;
    MailMgr::SendMail(listID, none, listID, 0, title, text, mailStatusMaskAutomated, 0, &toIDs);
}

// characters in a list of ids
static void GetCharIDs(PyList* list, std::vector<uint32>& into)
{
    for (PyList::const_iterator itr = list->begin(); itr != list->end(); ++itr)
        if ((*itr)->IsInt() or (*itr)->IsLong())
            into.push_back(PyRep::IntegerValueU32(*itr));
}

PyResult MailingListMgrService::GetJoinedLists(PyCallArgs& call)
{
    return MailDB::GetJoinedLists(call.client->GetCharacterID());
}

PyResult MailingListMgrService::Create(PyCallArgs& call, PyRep* name, PyInt* defaultAccess, PyInt* defaultMemberAccess, std::optional<PyInt*> mailCost)
{
    std::string key, displayName;
    GetKeyAndName(PyRep::StringContent(name), key, displayName);
    if (key.empty())
        throw CustomError("You must give the mailing list a name.");
    if (displayName.size() > mailingListMaxNameSize)
        throw CustomError("Mailing list names can have at most %u characters.", (uint32)mailingListMaxNameSize);
    if (MailDB::GetListByName(key) != 0)
        throw CustomError("A mailing list with that name already exists.");

    int8 access = (defaultAccess->value() == mailingListBlocked ? mailingListBlocked : mailingListAllowed);
    int8 role = defaultMemberAccess->value();
    if ((role != mailingListMemberMuted) and (role != mailingListMemberDefault))
        role = mailingListMemberDefault;
    uint32 cost = (mailCost.has_value() ? (uint32)std::max(0, mailCost.value()->value()) : 0);

    uint32 listID = MailDB::CreateList(call.client->GetCharacterID(), key, displayName, access, role, cost);
    if (listID == 0)
        throw CustomError("The mailing list could not be created.");
    return new PyInt(listID);
}

PyResult MailingListMgrService::Join(PyCallArgs& call, PyRep* listName)
{
    std::string key, displayName;
    GetKeyAndName(PyRep::StringContent(listName), key, displayName);
    uint32 listID = MailDB::GetListByName(key);
    MailDB::MailListInfo info;
    if ((listID == 0) or !MailDB::GetList(listID, info))
        throw UserError("MailingListNoSuchList");

    uint32 charID = call.client->GetCharacterID();
    if (MailDB::GetMemberRole(listID, charID) >= 0)
        return MailDB::GetJoinedListInfo(listID, charID);

    // most specific access entry wins: character, then corp, then alliance, then the list default
    int8 access = info.defaultAccess;
    std::map<uint32, int8> entities;
    MailDB::GetEntityAccess(listID, entities);
    std::map<uint32, int8>::iterator itr = entities.find(charID);
    if (itr == entities.end())
        itr = entities.find(call.client->GetCorporationID());
    if ((itr == entities.end()) and (call.client->GetAllianceID() > 0))
        itr = entities.find(call.client->GetAllianceID());
    if (itr != entities.end())
        access = itr->second;
    if ((access == mailingListBlocked) and !IsGM(call.client))
        throw CustomError("You are not allowed to join this mailing list.");

    if (MailDB::GetMemberCount(listID) >= mailinglistMaxMembers)
        throw CustomError("This mailing list is full (%u members).", (uint32)mailinglistMaxMembers);

    MailDB::AddMember(listID, charID, info.defaultMemberAccess);

    if (info.hasWelcomeMail) {
        std::vector<uint32> toIDs(1, charID);
        SendWelcome(listID, info.welcomeTitle, info.welcomeBody, toIDs);
    }

    return MailDB::GetJoinedListInfo(listID, charID);
}

PyResult MailingListMgrService::Leave(PyCallArgs& call, PyInt* listID)
{
    uint32 charID = call.client->GetCharacterID();
    int8 role = MailDB::GetMemberRole(listID->value(), charID);
    if (role < 0)
        return PyStatic.NewNone();
    if (role == mailingListMemberOwner)
        throw CustomError("The owner can't leave a mailing list; delete it instead.");

    std::vector<uint32> ids(1, charID);
    MailDB::RemoveMembers(listID->value(), ids);
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::Delete(PyCallArgs& call, PyInt* listID)
{
    MailDB::MailListInfo info;
    if (!MailDB::GetList(listID->value(), info))
        throw UserError("MailingListNoSuchList");
    if (GetRole(call.client, listID->value()) != mailingListMemberOwner)
        throw CustomError("Only the owner can delete a mailing list.");

    std::vector<uint32> members;
    MailDB::GetListMemberIDs(listID->value(), members);
    MailDB::DeleteList(listID->value());

    members.erase(std::remove(members.begin(), members.end(), (uint32)call.client->GetCharacterID()), members.end());
    Notify(members, "OnMailingListDeleted", listID->value());
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::KickMembers(PyCallArgs& call, PyInt* listID, PyList* memberIDs)
{
    RequireOperator(call.client, listID->value());
    bool isOwner = (GetRole(call.client, listID->value()) == mailingListMemberOwner);

    std::map<uint32, int8> roles;
    MailDB::GetMemberRoles(listID->value(), roles);
    std::vector<uint32> ids, kicked;
    GetCharIDs(memberIDs, ids);
    for (auto cur : ids) {
        std::map<uint32, int8>::iterator itr = roles.find(cur);
        if (itr == roles.end())
            continue;
        // the owner stays; operators can only be removed by the owner
        if (itr->second == mailingListMemberOwner)
            continue;
        if ((itr->second == mailingListMemberOperator) and !isOwner)
            continue;
        kicked.push_back(cur);
    }

    MailDB::RemoveMembers(listID->value(), kicked);
    for (auto cur : kicked) {
        std::vector<uint32> one(1, cur);
        Notify(one, "OnMailingListLeave", listID->value(), cur);
    }
    return PyStatic.NewNone();
}

void MailingListMgrService::SetRoles(Client* pClient, uint32 listID, PyList* memberIDs, int8 role, const char* notifyType)
{
    RequireOperator(pClient, listID);
    bool isOwner = (GetRole(pClient, listID) == mailingListMemberOwner);
    // only the owner may grant operator
    if ((role == mailingListMemberOperator) and !isOwner)
        throw CustomError("Only the owner can make members operators.");

    std::map<uint32, int8> roles;
    MailDB::GetMemberRoles(listID, roles);
    std::vector<uint32> ids, changed;
    GetCharIDs(memberIDs, ids);
    for (auto cur : ids) {
        std::map<uint32, int8>::iterator itr = roles.find(cur);
        if (itr == roles.end())
            continue;
        if (itr->second == mailingListMemberOwner)
            continue;
        // and only the owner may take it away
        if ((itr->second == mailingListMemberOperator) and !isOwner)
            continue;
        if (itr->second == role)
            continue;
        changed.push_back(cur);
    }

    MailDB::SetMemberRoles(listID, changed, role);
    Notify(changed, notifyType, listID);
}

PyResult MailingListMgrService::SetMembersMuted(PyCallArgs& call, PyInt* listID, PyList* memberIDs)
{
    SetRoles(call.client, listID->value(), memberIDs, mailingListMemberMuted, "OnMailingListSetMuted");
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::SetMembersOperator(PyCallArgs& call, PyInt* listID, PyList* memberIDs)
{
    SetRoles(call.client, listID->value(), memberIDs, mailingListMemberOperator, "OnMailingListSetOperator");
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::SetMembersClear(PyCallArgs& call, PyInt* listID, PyList* memberIDs)
{
    SetRoles(call.client, listID->value(), memberIDs, mailingListMemberDefault, "OnMailingListSetClear");
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::GetMembers(PyCallArgs& call, PyInt* listID)
{
    // dict memberID -> role
    RequireOperator(call.client, listID->value());

    std::map<uint32, int8> roles;
    MailDB::GetMemberRoles(listID->value(), roles);
    PyDict* dict = new PyDict();
    for (auto cur : roles)
        dict->SetItem(new PyInt(cur.first), new PyInt(cur.second));
    return dict;
}

PyResult MailingListMgrService::SetEntityAccess(PyCallArgs& call, PyInt* listID, PyInt* entityID, PyInt* access)
{
    RequireOperator(call.client, listID->value());
    MailDB::SetEntityAccess(listID->value(), entityID->value(),
                            access->value() == mailingListBlocked ? mailingListBlocked : mailingListAllowed);
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::ClearEntityAccess(PyCallArgs& call, PyInt* listID, PyInt* entityID)
{
    RequireOperator(call.client, listID->value());
    MailDB::ClearEntityAccess(listID->value(), entityID->value());
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::SetDefaultAccess(PyCallArgs& call, PyInt* listID, PyInt* defaultAccess, PyInt* defaultMemberAccess, std::optional<PyInt*> mailCost)
{
    RequireOperator(call.client, listID->value());

    int8 access = (defaultAccess->value() == mailingListBlocked ? mailingListBlocked : mailingListAllowed);
    int8 role = defaultMemberAccess->value();
    if ((role != mailingListMemberMuted) and (role != mailingListMemberDefault))
        role = mailingListMemberDefault;
    uint32 cost = (mailCost.has_value() ? (uint32)std::max(0, mailCost.value()->value()) : 0);
    MailDB::SetListDefaults(listID->value(), access, role, cost);
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::GetInfo(PyCallArgs& call, PyInt* listID)
{
    // anybody may ask (sender names of list mail).  None = no such list
    MailDB::MailListInfo info;
    if (!MailDB::GetList(listID->value(), info))
        return PyStatic.NewNone();

    PyDict* dict = new PyDict();
        dict->SetItemString("id", new PyInt(info.id));
        dict->SetItemString("name", new PyWString(info.name));
        dict->SetItemString("displayName", new PyWString(info.displayName));
    return new PyObject("util.KeyVal", dict);
}

PyResult MailingListMgrService::GetSettings(PyCallArgs& call, PyInt* listID)
{
    // KeyVal(access = {ownerID: access}, defaultAccess, defaultMemberAccess, cost)
    RequireOperator(call.client, listID->value());

    MailDB::MailListInfo info;
    MailDB::GetList(listID->value(), info);

    std::map<uint32, int8> entities;
    MailDB::GetEntityAccess(listID->value(), entities);
    PyDict* access = new PyDict();
    for (auto cur : entities)
        access->SetItem(new PyInt(cur.first), new PyInt(cur.second));

    PyDict* dict = new PyDict();
        dict->SetItemString("access", access);
        dict->SetItemString("defaultAccess", new PyInt(info.defaultAccess));
        dict->SetItemString("defaultMemberAccess", new PyInt(info.defaultMemberAccess));
        dict->SetItemString("cost", new PyInt(info.cost));
    return new PyObject("util.KeyVal", dict);
}

PyResult MailingListMgrService::GetWelcomeMail(PyCallArgs& call, PyInt* listID)
{
    // falsy when there is none, else a sequence whose [0] has .title and .body
    RequireOperator(call.client, listID->value());

    PyList* list = new PyList();
    MailDB::MailListInfo info;
    if (MailDB::GetList(listID->value(), info) and info.hasWelcomeMail) {
        PyDict* dict = new PyDict();
            dict->SetItemString("title", new PyWString(info.welcomeTitle));
            dict->SetItemString("body", new PyWString(info.welcomeBody));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
    return list;
}

PyResult MailingListMgrService::SaveWelcomeMail(PyCallArgs& call, PyInt* listID, PyRep* title, PyRep* body)
{
    RequireOperator(call.client, listID->value());

    std::string subject = PyRep::StringContent(title);
    if (subject.empty())
        throw UserError("NoSubject");
    MailMgr::TruncateUtf8(subject, mailMaxSubjectSize);
    std::string text = PyRep::StringContent(body);
    MailMgr::TruncateUtf8(text, mailMaxBodySize);
    MailDB::SetWelcomeMail(listID->value(), &subject, &text);
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::SendWelcomeMail(PyCallArgs& call, PyInt* listID, PyRep* title, PyRep* body)
{
    // "save and send to all": save, then send it to every current member
    RequireOperator(call.client, listID->value());

    std::string subject = PyRep::StringContent(title);
    if (subject.empty())
        throw UserError("NoSubject");
    MailMgr::TruncateUtf8(subject, mailMaxSubjectSize);
    std::string text = PyRep::StringContent(body);
    MailMgr::TruncateUtf8(text, mailMaxBodySize);
    MailDB::SetWelcomeMail(listID->value(), &subject, &text);

    std::vector<uint32> members;
    MailDB::GetListMemberIDs(listID->value(), members);
    if (!members.empty())
        SendWelcome(listID->value(), subject, text, members);
    return PyStatic.NewNone();
}

PyResult MailingListMgrService::ClearWelcomeMail(PyCallArgs& call, PyInt* listID)
{
    RequireOperator(call.client, listID->value());
    MailDB::SetWelcomeMail(listID->value(), nullptr, nullptr);
    return PyStatic.NewNone();
}
