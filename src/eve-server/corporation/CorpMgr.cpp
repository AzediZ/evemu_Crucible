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

#include "EVE_Corp.h"
#include "EVE_Mail.h"

#include "Client.h"
#include "EntityList.h"
#include "character/Character.h"
#include "character/CharacterDB.h"
#include "corporation/CorporationDB.h"
#include "corporation/CorpMgr.h"
#include "mail/NotificationMgr.h"

/*
 * CORP__ERROR
 * CORP__WARNING
 * CORP__INFO
 * CORP__MESSAGE
 * CORP__TRACE
 */

std::optional<int64> Corp::MemberUpdate::Int(PyRep* rep)
{
    if ((rep == nullptr) or rep->IsNone())
        return std::nullopt;
    if (rep->IsInt() or rep->IsLong() or rep->IsBool())
        return PyRep::IntegerValue(rep);
    if (rep->IsFloat())
        return (int64)PyRep::FloatValue(rep);
    return std::nullopt;
}

CorpMgr::CorpMgr()
{
    m_titles.clear();
    m_ceoVote.clear();
}

int CorpMgr::Initialize()
{
    sLog.Blue("          CorpMgr", "Corporation Manager Initialized.");
    return 1;
}

void CorpMgr::Process()
{
}

/* roles */
void CorpMgr::LoadTitles(uint32 corpID)
{
    std::map<int64, Corp::TitleRoles>& titles = m_titles[corpID];
    titles.clear();
    CorporationDB::GetTitleRoles(corpID, titles);
}

Corp::TitleRoles CorpMgr::GetTitleRoles(uint32 corpID, int64 titleMask)
{
    Corp::TitleRoles roles = Corp::TitleRoles();
    if ((titleMask == 0) or !IsPlayerCorp(corpID))
        return roles;

    std::map<uint32, std::map<int64, Corp::TitleRoles>>::iterator itr = m_titles.find(corpID);
    if (itr == m_titles.end()) {
        LoadTitles(corpID);
        itr = m_titles.find(corpID);
    }

    for (auto cur : itr->second) {
        if ((titleMask & cur.first) != cur.first)
            continue;
        roles.roles                 |= cur.second.roles;
        roles.grantableRoles        |= cur.second.grantableRoles;
        roles.rolesAtHQ             |= cur.second.rolesAtHQ;
        roles.grantableRolesAtHQ    |= cur.second.grantableRolesAtHQ;
        roles.rolesAtBase           |= cur.second.rolesAtBase;
        roles.grantableRolesAtBase  |= cur.second.grantableRolesAtBase;
        roles.rolesAtOther          |= cur.second.rolesAtOther;
        roles.grantableRolesAtOther |= cur.second.grantableRolesAtOther;
    }
    return roles;
}

void CorpMgr::ComputeEffectiveRoles(uint32 charID, const CorpData& data, int64& all, int64& hq, int64& base, int64& other)
{
    Corp::TitleRoles titles = GetTitleRoles(data.corporationID, data.titleMask);
    all   = data.corpRole | data.rolesAtAll | titles.roles;
    hq    = data.rolesAtHQ | titles.rolesAtHQ;
    base  = data.rolesAtBase | titles.rolesAtBase;
    other = data.rolesAtOther | titles.rolesAtOther;

    if (!IsPlayerCorp(data.corporationID))
        return;
    if (CorporationDB::GetCorporationCEO(data.corporationID) != charID)
        return;

    if (IsCEORevoked(data.corporationID, charID)) {
        // "Your roles as CEO have been revoked for the duration of the voting period."
        all = hq = base = other = 0;
        return;
    }

    // directors and the active CEO hold every role (the client's UserIsActiveCEO looks for Director in corprole)
    all |= Corp::Role::Admin;
}

void CorpMgr::RefreshMemberSession(uint32 charID, bool reloadCorp/*false*/)
{
    Client* pClient = sEntityList.FindClientByCharID(charID);
    if ((pClient == nullptr) or (pClient->GetChar().get() == nullptr))
        return;

    CorpData data = pClient->GetChar()->GetCorpData();
    if (reloadCorp)
        CorporationDB::GetCorpData(data);
    pClient->GetChar()->UpdateCorpData(data);
}

void CorpMgr::RefreshTitleHolders(uint32 corpID, int64 titleID)
{
    std::vector<Client*> list;
    sEntityList.GetCorpClients(list, corpID);
    for (auto cur : list) {
        if ((cur == nullptr) or (cur->GetChar().get() == nullptr))
            continue;
        if ((cur->GetChar()->GetCorpData().titleMask & titleID) == 0)
            continue;
        RefreshMemberSession(cur->GetCharacterID());
    }
}

void CorpMgr::SendMemberChanged(uint32 corpID, uint32 charID, PyDict* change)
{
    if (change == nullptr)
        return;
    if (change->empty() or !IsPlayerCorp(corpID)) {
        PyDecRef(change);
        return;
    }

    // corp.members:  OnCorporationMemberChanged(memberID, change)  change = {column: (old, new)}
    PyTuple* payload = new PyTuple(2);
        payload->SetItem(0, new PyInt(charID));
        payload->SetItem(1, change);
    sEntityList.CorpNotify(corpID, Notify::Types::CorpNews, "OnCorporationMemberChanged", "corpid", payload);
}

std::string CorpMgr::GetMemberTitle(uint32 charID)
{
    Client* pClient = sEntityList.FindClientByCharID(charID);
    if ((pClient != nullptr) and (pClient->GetChar().get() != nullptr))
        return pClient->GetChar()->title();
    return CorporationDB::GetMemberTitle(charID);
}

namespace {
// one role mask of a member edit:  name, the member's value, the caller's grantable mask of that kind
struct RoleField {
    const char* name;
    int64* value;
    const std::optional<int64>* update;
    int64 grantable;
};

void AddChange(PyDict* change, const char* key, PyRep* oldValue, PyRep* newValue)
{
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, oldValue);
        tuple->SetItem(1, newValue);
    change->SetItemString(key, tuple);
}
}

bool CorpMgr::UpdateMember(Client* pCaller, uint32 corpID, const Corp::MemberUpdate& upd, bool throwOnDeny)
{
    auto deny = [&](const char* reason) -> bool {
        _log(CORP__WARNING, "CorpMgr::UpdateMember() - %s(%u) editing %u in corp %u denied: %s",
             pCaller->GetName(), pCaller->GetCharacterID(), upd.charID, corpID, reason);
        if (throwOnDeny)
            throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString(reason));
        return false;
    };

    if (!IsPlayerCorp(corpID))
        return deny("You cannot change the roles of members of an NPC corporation.");
    if (pCaller->GetCorporationID() != (int32)corpID)
        return deny("You are not a member of this corporation.");

    // the target's current data
    Client* pTarget = sEntityList.FindClientByCharID(upd.charID);
    bool online = ((pTarget != nullptr) and (pTarget->GetChar().get() != nullptr));
    CorpData data = CorpData();
    if (online) {
        data = pTarget->GetChar()->GetCorpData();
    } else if (!CharacterDB::GetCharCorpData(upd.charID, data)) {
        return deny("That character could not be found.");
    }
    if (data.corporationID != corpID)
        return deny("That character is not a member of this corporation.");

    const CorpData old = data;
    std::string oldTitle = GetMemberTitle(upd.charID);
    std::string newTitle = oldTitle;
    if (upd.title.has_value())
        newTitle = upd.title.value();

    // caller's standing in the corp
    uint32 callerID = pCaller->GetCharacterID();
    uint32 ceoID = CorporationDB::GetCorporationCEO(corpID);
    bool callerIsDirector = ((pCaller->GetRolesAtAll() & Corp::Role::Director) == Corp::Role::Director);
    bool callerIsCEO = ((callerID == ceoID) and callerIsDirector);
    bool self = (callerID == upd.charID);

    // caller's grantable roles (own and titles); directors and the CEO may grant everything but Director
    CorpData callerData = pCaller->GetChar()->GetCorpData();
    Corp::TitleRoles callerTitles = GetTitleRoles(corpID, callerData.titleMask);
    int64 all = (callerIsDirector ? Corp::Role::Admin : 0);
    // only the CEO grants or removes Director (bco_members.GetMyGrantableRoles)
    int64 allGeneral = (callerIsCEO ? Corp::Role::Admin : (all & ~((int64)Corp::Role::Director)));
    std::vector<RoleField> fields = {
        {"roles",                 &data.rolesAtAll,            &upd.roles,                 allGeneral | callerData.grantableRoles | callerTitles.grantableRoles},
        {"grantableRoles",        &data.grantableRoles,        &upd.grantableRoles,        all | callerData.grantableRoles | callerTitles.grantableRoles},
        {"rolesAtHQ",             &data.rolesAtHQ,             &upd.rolesAtHQ,             all | callerData.grantableRolesAtHQ | callerTitles.grantableRolesAtHQ},
        {"grantableRolesAtHQ",    &data.grantableRolesAtHQ,    &upd.grantableRolesAtHQ,    all | callerData.grantableRolesAtHQ | callerTitles.grantableRolesAtHQ},
        {"rolesAtBase",           &data.rolesAtBase,           &upd.rolesAtBase,           all | callerData.grantableRolesAtBase | callerTitles.grantableRolesAtBase},
        {"grantableRolesAtBase",  &data.grantableRolesAtBase,  &upd.grantableRolesAtBase,  all | callerData.grantableRolesAtBase | callerTitles.grantableRolesAtBase},
        {"rolesAtOther",          &data.rolesAtOther,          &upd.rolesAtOther,          all | callerData.grantableRolesAtOther | callerTitles.grantableRolesAtOther},
        {"grantableRolesAtOther", &data.grantableRolesAtOther, &upd.grantableRolesAtOther, all | callerData.grantableRolesAtOther | callerTitles.grantableRolesAtOther},
    };
    // own general roles are kept in both corpRole and rolesAtAll; older rows may only have one of them
    data.rolesAtAll |= data.corpRole;
    int64 oldRoles = data.rolesAtAll;

    bool newBlock = (upd.blockRoles.has_value() ? upd.blockRoles.value() : old.blockRoles);
    int64 newTitleMask = (upd.titleMask.has_value() ? upd.titleMask.value() : old.titleMask);
    uint32 newBaseID = (upd.baseID.has_value() ? (uint32)upd.baseID.value() : old.baseID);

    // "prepare to quit":  blocking new roles also drops every role and title
    bool startBlock = (newBlock and !old.blockRoles);
    if (startBlock)
        newTitleMask = 0;

    bool changed = false;
    if (upd.charID == ceoID) {
        if (!callerIsCEO and !self)
            return deny("Only the CEO can change the CEO's roles.");
    }

    // titles, title text and base:  CEO and directors
    if (newTitle.compare(oldTitle) != 0) {
        if (!callerIsDirector)
            return deny("Only the CEO and directors can change member titles.");
        changed = true;
    }
    if (newBaseID != old.baseID) {
        if (!callerIsDirector and !self)
            return deny("Only the CEO and directors can change a member's base.");
        changed = true;
    }
    if (newTitleMask != old.titleMask) {
        // a member may drop their own titles
        bool onlyDropping = (self and ((newTitleMask & ~old.titleMask) == 0));
        if (!callerIsDirector and !onlyDropping)
            return deny("Only the CEO and directors can change member titles.");
        if (newBlock and ((newTitleMask & ~old.titleMask) != 0))
            return deny("This member is blocking roles.");
        changed = true;
    }
    if (newBlock != old.blockRoles) {
        if (!self and !callerIsDirector)
            return deny("Only the member themselves can block or allow new roles.");
        changed = true;
    }

    for (auto& cur : fields) {
        int64 oldValue = *cur.value;
        int64 newValue = oldValue;
        if (startBlock) {
            newValue = 0;
        } else if (cur.update->has_value()) {
            newValue = cur.update->value();
        }
        if (newValue == oldValue)
            continue;

        int64 added = newValue & ~oldValue;
        int64 removed = oldValue & ~newValue;
        if (added != 0) {
            if (newBlock)
                return deny("This member is blocking roles.");
            if ((cur.value == &data.rolesAtAll) and ((added & Corp::Role::Director) == Corp::Role::Director) and !callerIsCEO)
                return deny("Only the CEO can grant the Director role.");
            if ((added & ~cur.grantable) != 0)
                return deny("You cannot grant roles you are not allowed to grant.");
        }
        // anyone may drop their own roles
        if ((removed != 0) and !self and ((removed & ~cur.grantable) != 0))
            return deny("You cannot remove roles you are not allowed to grant.");

        *cur.value = newValue;
        changed = true;
    }

    if (!changed)
        return true;

    data.corpRole = data.rolesAtAll;
    data.titleMask = newTitleMask;
    data.baseID = newBaseID;
    data.blockRoles = newBlock;

    // corporate stasis starts when the last role goes
    bool hadRoles = ((oldRoles | old.grantableRoles | old.rolesAtHQ | old.grantableRolesAtHQ | old.rolesAtBase | old.grantableRolesAtBase
                    | old.rolesAtOther | old.grantableRolesAtOther | old.titleMask) != 0);
    bool hasRoles = ((data.rolesAtAll | data.grantableRoles | data.rolesAtHQ | data.grantableRolesAtHQ | data.rolesAtBase | data.grantableRolesAtBase
                    | data.rolesAtOther | data.grantableRolesAtOther | data.titleMask) != 0);
    if (hadRoles and !hasRoles)
        data.rolesClearedDateTime = GetFileTimeNow();

    if (online) {
        if (newTitle.compare(oldTitle) != 0)
            pTarget->GetChar()->SetCorpTitle(newTitle);
        pTarget->GetChar()->UpdateCorpData(data);
    } else {
        if (newTitle.compare(oldTitle) != 0)
            CharacterDB::SetTitle(upd.charID, newTitle);
        CharacterDB::SaveCorpData(upd.charID, data);
    }

    bool grantable = ((data.grantableRoles != old.grantableRoles) or (data.grantableRolesAtHQ != old.grantableRolesAtHQ)
                   or (data.grantableRolesAtBase != old.grantableRolesAtBase) or (data.grantableRolesAtOther != old.grantableRolesAtOther));
    CorporationDB cdb;
    if (oldRoles != data.rolesAtAll)
        cdb.AddRoleHistory(corpID, upd.charID, callerID, oldRoles, data.rolesAtAll, false);
    if (grantable)
        cdb.AddRoleHistory(corpID, upd.charID, callerID, old.grantableRoles, data.grantableRoles, true);

    // the member row's columns that changed
    PyDict* change = new PyDict();
    if (newTitle.compare(oldTitle) != 0)
        AddChange(change, "title", new PyWString(oldTitle), new PyWString(newTitle));
    if (oldRoles != data.rolesAtAll)
        AddChange(change, "roles", new PyLong(oldRoles), new PyLong(data.rolesAtAll));
    for (size_t i = 1; i < fields.size(); ++i) {
        int64 oldValue(0);
        switch (i) {
            case 1: oldValue = old.grantableRoles; break;
            case 2: oldValue = old.rolesAtHQ; break;
            case 3: oldValue = old.grantableRolesAtHQ; break;
            case 4: oldValue = old.rolesAtBase; break;
            case 5: oldValue = old.grantableRolesAtBase; break;
            case 6: oldValue = old.rolesAtOther; break;
            case 7: oldValue = old.grantableRolesAtOther; break;
        }
        if (oldValue != *fields[i].value)
            AddChange(change, fields[i].name, new PyLong(oldValue), new PyLong(*fields[i].value));
    }
    if (old.baseID != data.baseID)
        AddChange(change, "baseID", (old.baseID ? (PyRep*)new PyInt(old.baseID) : PyStatic.NewNone()),
                  (data.baseID ? (PyRep*)new PyInt(data.baseID) : PyStatic.NewNone()));
    if (old.titleMask != data.titleMask)
        AddChange(change, "titleMask", new PyLong(old.titleMask), new PyLong(data.titleMask));
    if (old.blockRoles != data.blockRoles)
        AddChange(change, "blockRoles", new PyInt(old.blockRoles ? 1 : 0), new PyInt(data.blockRoles ? 1 : 0));

    SendMemberChanged(corpID, upd.charID, change);

    _log(CORP__INFO, "CorpMgr::UpdateMember() - %s(%u) updated member %u of corp %u", pCaller->GetName(), callerID, upd.charID, corpID);
    return true;
}

/* CEO */
bool CorpMgr::IsCEORevoked(uint32 corpID, uint32 charID)
{
    if (!IsPlayerCorp(corpID))
        return false;
    if (CorporationDB::GetCorporationCEO(corpID) != charID)
        return false;

    std::map<uint32, bool>::iterator itr = m_ceoVote.find(corpID);
    if (itr == m_ceoVote.end())
        itr = m_ceoVote.emplace(corpID, CorporationDB::HasOpenCEOVote(corpID)).first;
    return itr->second;
}

/* notifications */
void CorpMgr::NotifyCorp(uint32 corpID, uint16 notifyType, uint32 senderID, PyDict* data, int64 roleMask/*0*/)
{
    NotificationMgr::SendToCorp(corpID, roleMask, notifyType, senderID, data);
}

void CorpMgr::NotifyChar(uint32 charID, uint16 notifyType, uint32 senderID, PyDict* data)
{
    NotificationMgr::Send(charID, notifyType, senderID, data);
}
