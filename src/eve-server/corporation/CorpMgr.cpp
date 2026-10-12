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

#include "EVE_Skills.h"

#include "Client.h"
#include "EntityList.h"
#include "account/AccountDB.h"
#include "account/AccountService.h"
#include "alliance/AllianceDB.h"
#include "character/Character.h"
#include "character/CharacterDB.h"
#include "corporation/CorporationDB.h"
#include "corporation/CorpMgr.h"
#include "faction/WarManager.h"
#include "alliance/AllianceManager.h"
#include "corporation/ItemLockdown.h"
#include "inventory/ItemFactory.h"
#include "mail/NotificationMgr.h"
#include "station/Station.h"
#include "station/StationDB.h"
#include "station/StationDataMgr.h"
#include "station/StationOffice.h"

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
    // office rent and alliance maintenance bills
    sBillingMgr.RegisterHandlers(Corp::BillType::RentalBill,
        [this](const BillData& bill) { OnRentPaid(bill); },
        [this](const BillData& bill) { OnRentOverdue(bill); });
    sBillingMgr.RegisterHandlers(Corp::BillType::AllianceMaintainanceBill,
        nullptr,
        [this](const BillData& bill) { OnAllianceBillOverdue(bill); });
    sBillingMgr.RegisterHandlers(Corp::BillType::SovereigntyMarker,
        nullptr,
        [this](const BillData& bill) { OnSovBillOverdue(bill); });

    sLog.Blue("          CorpMgr", "Corporation Manager Initialized.");
    return 1;
}

void CorpMgr::Process()
{
    ProcessVotes();
    ProcessOffices();
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

bool CorpMgr::LoadMemberData(uint32 charID, CorpData& data)
{
    Client* pClient = sEntityList.FindClientByCharID(charID);
    if ((pClient != nullptr) and (pClient->GetChar().get() != nullptr)) {
        data = pClient->GetChar()->GetCorpData();
        return true;
    }
    data = CorpData();
    return CharacterDB::GetCharCorpData(charID, data);
}

void CorpMgr::SaveMemberData(uint32 charID, CorpData& data)
{
    Client* pClient = sEntityList.FindClientByCharID(charID);
    if ((pClient != nullptr) and (pClient->GetChar().get() != nullptr)) {
        pClient->GetChar()->UpdateCorpData(data);
    } else {
        CharacterDB::SaveCorpData(charID, data);
    }
}

/* membership */
bool CorpMgr::CanLeave(uint32 charID, std::string& errorKey, PyDict*& args)
{
    // corp_ui_home:  canLeave, error, errorDetails = CanLeaveCurrentCorporation();  the Quit button raises UserError(error, errorDetails)
    args = nullptr;
    CorpData data = CorpData();
    if (!LoadMemberData(charID, data))
        return false;

    if (!IsPlayerCorp(data.corporationID)) {
        errorKey = "CrpCantQuitDefaultCorporation";
        args = new PyDict();
        return false;
    }
    if (CorporationDB::GetCorporationCEO(data.corporationID) == charID) {
        errorKey = "CrpCantQuitCEO";
        args = new PyDict();
        return false;
    }

    // a member with roles must 'Prepare to Quit' first (removes the roles and starts the stasis)
    int64 roles = data.corpRole | data.rolesAtAll | data.rolesAtHQ | data.rolesAtBase | data.rolesAtOther;
    int64 grantable = data.grantableRoles | data.grantableRolesAtHQ | data.grantableRolesAtBase | data.grantableRolesAtOther;
    if ((roles | grantable | data.titleMask) != 0) {
        std::string rolelist = CorporationDB::GetRoleNames(roles | grantable);
        if (rolelist.empty())
            rolelist = "Titles";
        errorKey = "CrpCantQuitNotInStasis";
        args = new PyDict();
            args->SetItemString("char", new PyInt(charID));
            args->SetItemString("rolelist", new PyString(rolelist));
        return false;
    }

    // 24 hours of corporate stasis after the last roles went
    if ((sConfig.corp.StasisHours > 0) and (data.rolesClearedDateTime > 0)) {
        int64 elapsed = (GetFileTimeNow() - data.rolesClearedDateTime) / EvE::Time::Hour;
        if (elapsed < 0)
            elapsed = 0;
        if (elapsed < (int64)sConfig.corp.StasisHours) {
            errorKey = "CrpCantQuitNotCompletedStasisPeriod";
            args = new PyDict();
                args->SetItemString("char", new PyInt(charID));
                args->SetItemString("hour", new PyInt((int32)elapsed));
                args->SetItemString("hoursleft", new PyInt((int32)(sConfig.corp.StasisHours - elapsed)));
            return false;
        }
    }

    return true;
}

bool CorpMgr::CanBeKicked(Client* pCaller, uint32 corpID, uint32 charID, std::string& reason)
{
    if (pCaller->GetCorporationID() != (int32)corpID) {
        reason = "You are not a member of this corporation.";
        return false;
    }
    if ((pCaller->GetRolesAtAll() & Corp::Role::Director) != Corp::Role::Director) {
        reason = "Only the CEO and directors can kick members out of the corporation.";
        return false;
    }

    CorpData data = CorpData();
    if (!LoadMemberData(charID, data) or (data.corporationID != corpID)) {
        reason = "That character is not a member of this corporation.";
        return false;
    }
    if (CorporationDB::GetCorporationCEO(corpID) == charID) {
        reason = "The CEO cannot be kicked out of the corporation.";
        return false;
    }

    // members with roles have their roles removed first, then go through the same stasis as when quitting
    int64 roles = data.corpRole | data.rolesAtAll | data.rolesAtHQ | data.rolesAtBase | data.rolesAtOther
                | data.grantableRoles | data.grantableRolesAtHQ | data.grantableRolesAtBase | data.grantableRolesAtOther | data.titleMask;
    if (roles != 0) {
        reason = "This member still holds roles or titles.  Remove them first; the member can be kicked out once their corporate stasis period is over.";
        return false;
    }
    if ((sConfig.corp.StasisHours > 0) and (data.rolesClearedDateTime > 0)
    and ((GetFileTimeNow() - data.rolesClearedDateTime) < ((int64)sConfig.corp.StasisHours * EvE::Time::Hour))) {
        reason = "This member recently held roles and is still in corporate stasis.";
        return false;
    }

    return true;
}

uint32 CorpMgr::GetDefaultCorp(uint32 charID)
{
    // "join one of the starter NPC corporations" (ConfirmQuitCorporation):  the character's school corp
    uint32 corpID(0);
    if (CorporationDB::GetCorporationBySchool(CharacterDB::GetSchoolID(charID), corpID) and IsNPCCorp(corpID))
        return corpID;
    if (IsNPCCorp(sConfig.character.startCorporation))
        return sConfig.character.startCorporation;
    return 1000044;     // School of Applied Knowledge
}

void CorpMgr::MoveMemberToCorp(uint32 charID, uint32 newCorpID)
{
    CorpData old = CorpData();
    if (!LoadMemberData(charID, old))
        return;
    uint32 oldCorpID = old.corporationID;
    if (oldCorpID == newCorpID)
        return;

    // a new member starts without roles, titles or base
    CorpData data = CorpData();
        data.corporationID = newCorpID;
        data.corpAccountKey = Account::KeyType::Cash;
        data.startDateTime = GetFileTimeNow();
    CorporationDB::GetCorpData(data);

    Client* pClient = sEntityList.FindClientByCharID(charID);
    if ((pClient != nullptr) and (pClient->GetChar().get() != nullptr)) {
        pClient->GetChar()->SetCorpTitle("");
        pClient->GetChar()->JoinCorporation(data);   // employment history, session, save
    } else {
        CharacterDB::AddEmployment(charID, newCorpID, oldCorpID);
        CharacterDB::SaveCorpData(charID, data);
        CharacterDB::SetTitle(charID, "");
    }

    CorporationDB cdb;
    if (IsPlayerCorp(oldCorpID))
        cdb.AddItemEvent(oldCorpID, charID, Corp::EventType::LeftCorporation);
    if (IsPlayerCorp(newCorpID))
        cdb.AddItemEvent(newCorpID, charID, Corp::EventType::JoinedCorporation);

    // both corps' member lists:  OnCorporationMemberChanged(charID, {corporationID: (old, new), startDateTime: (old, new)})
    PyDict* change = new PyDict();
        AddChange(change, "corporationID", new PyInt(oldCorpID), new PyInt(newCorpID));
        AddChange(change, "startDateTime", new PyLong(old.startDateTime), new PyLong(data.startDateTime));
    if (IsPlayerCorp(oldCorpID)) {
        PyIncRef(change);
        SendMemberChanged(oldCorpID, charID, change);
    }
    SendMemberChanged(newCorpID, charID, change);

    _log(CORP__INFO, "CorpMgr::MoveMemberToCorp() - %u moved from corp %u to %u", charID, oldCorpID, newCorpID);
}

void CorpMgr::LeaveCorp(uint32 charID, bool kicked, uint32 kickerID)
{
    CorpData old = CorpData();
    if (!LoadMemberData(charID, old))
        return;
    uint32 oldCorpID = old.corporationID;

    MoveMemberToCorp(charID, GetDefaultCorp(charID));

    PyDict* data = new PyDict();
        data->SetItemString("charID", new PyInt(charID));
        data->SetItemString("corpID", new PyInt(oldCorpID));
    NotifyCorp(oldCorpID, Notify::Types::CharLeftCorp, charID, data);

    if (kicked) {
        data = new PyDict();
            data->SetItemString("corpID", new PyInt(oldCorpID));
        NotifyChar(charID, Notify::Types::CorpKicked, kickerID, data);
    }
}

uint8 CorpMgr::GetSkillLevel(uint32 charID, uint16 skillTypeID)
{
    Client* pClient = sEntityList.FindClientByCharID(charID);
    if ((pClient != nullptr) and (pClient->GetChar().get() != nullptr)) {
        int8 level = pClient->GetChar()->GetSkillLevel(skillTypeID);
        return (level > 0 ? level : 0);
    }
    return CharacterDB::GetSkillLevel(charID, skillTypeID);
}

uint32 CorpMgr::GetSkillMemberLimit(uint32 charID)
{
    // corporationMemberBonus (attribute 191) of the skills, per level
    uint32 limit = GetSkillLevel(charID, EvESkill::CorporationManagement) * 10;
    limit += GetSkillLevel(charID, EvESkill::MegacorpManagement) * 50;
    limit += GetSkillLevel(charID, EvESkill::EmpireControl) * 200;
    limit += GetSkillLevel(charID, EvESkill::Sovereignty) * 1000;
    return limit;
}

void CorpMgr::BroadcastCorpChange(uint32 corpID, PyDict* change)
{
    if (change == nullptr)
        return;
    if (change->empty() or !IsPlayerCorp(corpID)) {
        PyDecRef(change);
        return;
    }

    // bco_corporations.OnCorporationChanged(corpID, change)
    PyTuple* payload = new PyTuple(2);
        payload->SetItem(0, new PyInt(corpID));
        payload->SetItem(1, change);
    sEntityList.CorpNotify(corpID, Notify::Types::CorpNews, "OnCorporationChanged", "corpid", payload);
}

void CorpMgr::UpdateAbilities(uint32 corpID)
{
    uint32 ceoID = CorporationDB::GetCorporationCEO(corpID);
    if (!IsCharacterID(ceoID))
        return;

    uint16 oldLimit(0);
    uint8 oldRaces(0);
    CorporationDB::GetAbilities(corpID, oldLimit, oldRaces);

    uint32 limit = GetSkillMemberLimit(ceoID);
    if (limit > 0xffff)
        limit = 0xffff;
    // the client tests race.raceID & allowedMemberRaceIDs (Caldari 1, Minmatar 2, Amarr 4, Gallente 8);  Ethnic Relations opens all of them
    uint8 races = CharacterDB::GetRaceID(ceoID);
    if (GetSkillLevel(ceoID, EvESkill::EthnicRelations) > 0)
        races |= (1 | 2 | 4 | 8);

    if ((limit == oldLimit) and (races == oldRaces))
        return;

    CorporationDB::SetAbilities(corpID, limit, races);
    PyDict* change = new PyDict();
    if (limit != oldLimit)
        AddChange(change, "memberLimit", new PyInt(oldLimit), new PyInt(limit));
    if (races != oldRaces)
        AddChange(change, "allowedMemberRaceIDs", new PyInt(oldRaces), new PyInt(races));
    BroadcastCorpChange(corpID, change);
}

/* CEO */
bool CorpMgr::CanRunForCEO(uint32 corpID, uint32 charID)
{
    if (!IsPlayerCorp(corpID))
        return false;
    if (CorporationDB::GetCorporationCEO(corpID) == charID)
        return false;

    // skills:  Corporation Management, and enough to manage the corp's current members
    if (GetSkillLevel(charID, EvESkill::CorporationManagement) < 1)
        return false;
    CorporationDB cdb;
    if (GetSkillMemberLimit(charID) < cdb.GetMemberCount(corpID))
        return false;

    // shares
    if (sConfig.corp.CEOVoteMinSharePercent > 0) {
        int64 needed = ((int64)CorporationDB::GetCorpShares(corpID) * sConfig.corp.CEOVoteMinSharePercent + 99) / 100;
        if (CorporationDB::GetSharesHeld(charID, corpID) < needed)
            return false;
    }

    // "You cannot run for CEO as there has been a CEO vote issued within the last two weeks."
    int64 last = CorporationDB::GetLastCEOVote(corpID);
    if ((last > 0) and ((GetFileTimeNow() - last) < ((int64)sConfig.corp.CEOVoteCooldownDays * EvE::Time::Day)))
        return false;

    return true;
}

void CorpMgr::GetPotentialCEOs(uint32 corpID, std::vector<uint32>& out)
{
    out.clear();
    uint32 ceoID = CorporationDB::GetCorporationCEO(corpID);
    CorporationDB cdb;
    uint16 members = cdb.GetMemberCount(corpID);

    std::vector<uint32> ids;
    CorporationDB::GetMemberIDs(corpID, ids, true);
    std::vector<uint32> offline;
    CorporationDB::GetMemberIDs(corpID, offline, false);
    ids.insert(ids.end(), offline.begin(), offline.end());

    for (auto cur : ids) {
        if (cur == ceoID)
            continue;
        if (GetSkillLevel(cur, EvESkill::CorporationManagement) < 1)
            continue;
        if (GetSkillMemberLimit(cur) < members)
            continue;
        out.push_back(cur);
    }
}

void CorpMgr::ChangeCEO(uint32 corpID, uint32 newCeoID, bool byVote)
{
    uint32 oldCeoID = CorporationDB::GetCorporationCEO(corpID);
    if (oldCeoID == newCeoID)
        return;

    CorporationDB::SetCEO(corpID, newCeoID);
    InvalidateCEOVote(corpID);

    // the new CEO holds every role
    CorpData data = CorpData();
    if (LoadMemberData(newCeoID, data)) {
        data.corpRole = data.rolesAtAll = data.rolesAtHQ = data.rolesAtBase = data.rolesAtOther = Corp::Role::Admin;
        data.grantableRoles = data.grantableRolesAtHQ = data.grantableRolesAtBase = data.grantableRolesAtOther = Corp::Role::Admin;
        data.blockRoles = false;
        SaveMemberData(newCeoID, data);
    }

    // a CEO voted out loses every role;  one who resigns keeps theirs
    if (IsCharacterID(oldCeoID) and LoadMemberData(oldCeoID, data) and (data.corporationID == corpID)) {
        if (byVote) {
            data.corpRole = data.rolesAtAll = data.rolesAtHQ = data.rolesAtBase = data.rolesAtOther = 0;
            data.grantableRoles = data.grantableRolesAtHQ = data.grantableRolesAtBase = data.grantableRolesAtOther = 0;
            data.titleMask = 0;
            data.rolesClearedDateTime = GetFileTimeNow();
        }
        SaveMemberData(oldCeoID, data);
    }

    CorporationDB cdb;
    cdb.AddItemEvent(corpID, newCeoID, Corp::EventType::BecameCEOOfCorporation);
    if (IsCharacterID(oldCeoID))
        cdb.AddItemEvent(corpID, oldCeoID, Corp::EventType::LeftCEOPositionOfCorporation);

    PyDict* change = new PyDict();
        AddChange(change, "ceoID", new PyInt(oldCeoID), new PyInt(newCeoID));
    BroadcastCorpChange(corpID, change);

    PyDict* notify = new PyDict();
        notify->SetItemString("corpID", new PyInt(corpID));
        notify->SetItemString("oldCeoID", new PyInt(oldCeoID));
        notify->SetItemString("newCeoID", new PyInt(newCeoID));
    NotifyCorp(corpID, Notify::Types::CorpNewCEO, newCeoID, notify);

    // member limit and races follow the CEO's skills
    UpdateAbilities(corpID);

    _log(CORP__INFO, "CorpMgr::ChangeCEO() - corp %u CEO %u -> %u (%s)", corpID, oldCeoID, newCeoID, (byVote ? "vote" : "resigned"));
}

void CorpMgr::LiquidateCorp(uint32 corpID, uint32 byCharID)
{
    if (!IsPlayerCorp(corpID))
        return;

    // "all your corporation's assets will be acquired by the local authorities":  the wallet goes to the character shareholders
    std::map<uint32, uint32> holders;
    CorporationDB::GetShareholders(corpID, holders);
    int64 held(0);
    for (std::map<uint32, uint32>::iterator itr = holders.begin(); itr != holders.end(); ) {
        if (!IsCharacterID(itr->first)) {
            itr = holders.erase(itr);
            continue;
        }
        held += itr->second;
        ++itr;
    }

    std::map<uint32, double> payouts;
    std::string reason = "Liquidation of ";
    reason += CorporationDB::GetCorpName(corpID);
    if (held > 0) {
        for (uint16 key = Account::KeyType::Cash; key <= Account::KeyType::Cash7; ++key) {
            double balance = AccountDB::GetCorpBalance(corpID, key);
            if (balance < 0.01)
                continue;
            for (auto cur : holders) {
                double amount = std::floor(balance * cur.second / held * 100) / 100;
                if (amount < 0.01)
                    continue;
                try {
                    AccountService::TransferFunds(corpID, cur.first, amount, reason, Journal::EntryType::CorporationDividendPayment, corpID, key);
                    payouts[cur.first] += amount;
                } catch (...) {
                    _log(CORP__ERROR, "CorpMgr::LiquidateCorp() - payout of %.2f to %u from corp %u failed", amount, cur.first, corpID);
                }
            }
        }
    }

    // members to their starter corps, the one closing the corp last
    std::vector<uint32> members, offline;
    CorporationDB::GetMemberIDs(corpID, members, true);
    CorporationDB::GetMemberIDs(corpID, offline, false);
    members.insert(members.end(), offline.begin(), offline.end());

    std::set<uint32> told;
    for (auto cur : payouts) {
        PyDict* data = new PyDict();
            data->SetItemString("corpID", new PyInt(corpID));
            data->SetItemString("payout", new PyFloat(cur.second));
        NotifyChar(cur.first, Notify::Types::CorpLiquidation, byCharID, data);
        told.insert(cur.first);
    }
    for (auto cur : members) {
        if (told.find(cur) != told.end())
            continue;
        PyDict* data = new PyDict();
            data->SetItemString("corpID", new PyInt(corpID));
            data->SetItemString("payout", new PyFloat(0));
        NotifyChar(cur, Notify::Types::CorpLiquidation, byCharID, data);
    }

    // offices and their contents go with the corp, and so do its bills and wars
    ReleaseOffices(corpID);
    sBillingMgr.CancelBillsOf(corpID);
    sWarMgr.OnOwnerClosed(corpID);

    // the members still online see the corp close before they move
    PyDict* change = new PyDict();
        AddChange(change, "deleted", new PyInt(0), new PyInt(1));
    BroadcastCorpChange(corpID, change);

    for (auto cur : members)
        if (cur != byCharID)
            MoveMemberToCorp(cur, GetDefaultCorp(cur));
    if (IsCharacterID(byCharID))
        MoveMemberToCorp(byCharID, GetDefaultCorp(byCharID));

    CorporationDB cdb;
    cdb.AddItemEvent(corpID, byCharID, Corp::EventType::DeletedCorporation);
    CorporationDB::CloseCorporation(corpID);
    InvalidateCEOVote(corpID);
    InvalidateTitles(corpID);

    _log(CORP__INFO, "CorpMgr::LiquidateCorp() - corp %u closed by %u, %u members moved", corpID, byCharID, members.size());
}

void CorpMgr::DeleteTree(InventoryItemRef iRef)
{
    // containers (and their contents) go first, the item itself last
    if (iRef->GetMyInventory() != nullptr) {
        iRef->GetMyInventory()->LoadContents();
        std::vector<InventoryItemRef> items;
        iRef->GetMyInventory()->GetInventoryVec(items);
        for (auto cur : items)
            DeleteTree(cur);
    }
    if (ItemLockdown::IsLocked(iRef->itemID())) {
        try {
            ItemLockdown::Unlock(iRef->ownerID(), iRef->itemID());
        } catch (...) { }
    }
    iRef->Delete();
}

void CorpMgr::ReleaseOffices(uint32 corpID)
{
    // every office of the corp and its contents are gone
    std::map<uint32, uint32> offices;
    StationDB::GetCorpOffices(corpID, offices);
    for (auto cur : offices) {
        StationOfficeRef oRef = sItemFactory.GetOfficeRef(cur.first);
        if (oRef.get() != nullptr) {
            if (oRef->GetMyInventory() != nullptr) {
                oRef->GetMyInventory()->LoadContents();
                std::vector<InventoryItemRef> items;
                oRef->GetMyInventory()->GetInventoryVec(items);
                for (auto item : items)
                    DeleteTree(item);
            }
        }

        DeleteOffice(cur.first);

        _log(CORP__INFO, "CorpMgr::ReleaseOffices() - office %u of corp %u in station %u removed", cur.first, corpID, cur.second);
    }
}

namespace {
// const.rentalPeriodOffice (days)
const int64 OfficeRentalPeriod = 30 * EvE::Time::Day;
// the rent bill warning before an office expires
const int64 OfficeExpiryWarning = 3 * EvE::Time::Day;

void SendOfficeChanged(uint32 corpID, uint32 officeID, uint32 folderID)
{
    // bco_locations.OnOfficeRentalChanged(corporationID, officeID, officeFolderID):  None, None = office gone
    PyTuple* payload = new PyTuple(3);
        payload->SetItem(0, new PyInt(corpID));
        payload->SetItem(1, (officeID ? (PyRep*)new PyInt(officeID) : PyStatic.NewNone()));
        payload->SetItem(2, (folderID ? (PyRep*)new PyInt(folderID) : PyStatic.NewNone()));
    sEntityList.CorpNotify(corpID, Notify::Types::CorpNews, "OnOfficeRentalChanged", "corpid", payload);
}

void NotifyOfficeExpiration(const OfficeData& odata, const std::string& errorText, int32 daysLeft)
{
    // CorpOfficeExpiration {typeID, stationID, dueDate, rentPeriod, errorText} (typeOfficeFolder = the office)
    //  rentPeriod is shown as "You have {rentPeriod} days to vacate the location"
    PyDict* data = new PyDict();
        data->SetItemString("typeID", new PyInt(26));
        data->SetItemString("stationID", new PyInt(odata.stationID));
        data->SetItemString("dueDate", new PyLong(odata.expiryTime));
        data->SetItemString("rentPeriod", new PyInt(daysLeft));
        data->SetItemString("errorText", new PyString(errorText));
    sCorpMgr.NotifyCorp(odata.corporationID, Notify::Types::CorpOfficeExpiration, stDataMgr.GetOwnerID(odata.stationID), data,
                        Corp::Role::Accountant | Corp::Role::JuniorAccountant);
}
}

void CorpMgr::IssueRentBill(uint32 officeID)
{
    OfficeData odata = OfficeData();
    if (!StationDB::GetOfficeData(officeID, odata) or odata.impounded or !odata.publiclyAvailable)
        return;

    // "the corporation will immediately receive a bill for the next 30 days", due when the current period ends
    double fee = stDataMgr.GetOfficeRentalFee(odata.stationID);
    if (fee < 10000)
        fee = 10000;
    uint32 billID = sBillingMgr.CreateBill(Corp::BillType::RentalBill, odata.corporationID, stDataMgr.GetOwnerID(odata.stationID), fee,
                                           odata.expiryTime, EVEDB::invTypes::Office, odata.stationID);
    StationDB::SetOfficeLease(officeID, odata.expiryTime, billID);
}

void CorpMgr::OnRentPaid(const BillData& bill)
{
    // another 30 days from the end of the current lease, and the bill for the period after that
    uint32 officeID = StationDB::GetCorpOffice(bill.debtorID, bill.externalID2, false);
    OfficeData odata = OfficeData();
    if ((officeID == 0) or !StationDB::GetOfficeData(officeID, odata))
        return;

    int64 expiry = EvE::max(odata.expiryTime, (int64)GetFileTimeNow()) + OfficeRentalPeriod;
    StationDB::SetOfficeLease(officeID, expiry, 0);
    m_warnedOffices.erase(officeID);
    IssueRentBill(officeID);
}

void CorpMgr::OnRentOverdue(const BillData& bill)
{
    uint32 officeID = StationDB::GetCorpOffice(bill.debtorID, bill.externalID2, false);
    if (officeID != 0)
        ExpireOffice(officeID, "");
}

void CorpMgr::OnAllianceBillOverdue(const BillData& bill)
{
    // "If this is ever unpaid, the alliance will be closed at the following downtime."
    uint32 executorID = AllianceDB::GetExecutorID(bill.debtorID);
    // AllMaintenanceBill {allianceID, dueDate}
    PyDict* data = new PyDict();
        data->SetItemString("allianceID", new PyInt(bill.debtorID));
        data->SetItemString("dueDate", new PyLong(bill.dueDateTime));
    NotifyCorp(executorID, Notify::Types::AllMaintenanceBill, corpCONCORD, data);

    if (sConfig.corp.CloseAllianceOnUnpaidBill) {
        _log(CORP__WARNING, "CorpMgr::OnAllianceBillOverdue() - alliance %u did not pay its maintenance bill %u and is closed", bill.debtorID, bill.billID);
        sAllianceMgr.DisbandAlliance(bill.debtorID);
    }
}

void CorpMgr::OnSovBillOverdue(const BillData& bill)
{
    // the alliance was warned with SovAllBillLate ("almost due") before the due date (BillingMgr::ProcessSovBills)
    /** @todo F14:  an unpaid sovereignty bill should cost the alliance its claim (svDataMgr has no claim-drop hook yet) */
    _log(CORP__WARNING, "CorpMgr::OnSovBillOverdue() - alliance %u did not pay sovereignty bill %u for system %i", bill.debtorID, bill.billID, bill.externalID2);
}

void CorpMgr::ExpireOffice(uint32 officeID, const std::string& errorText)
{
    OfficeData odata = OfficeData();
    if (!StationDB::GetOfficeData(officeID, odata) or odata.impounded)
        return;

    // "the corporation loses office access":  the hangar contents are impounded where they are
    bool empty(true);
    StationOfficeRef oRef = sItemFactory.GetOfficeRef(officeID);
    if ((oRef.get() != nullptr) and (oRef->GetMyInventory() != nullptr)) {
        oRef->GetMyInventory()->LoadContents();
        std::vector<InventoryItemRef> items;
        oRef->GetMyInventory()->GetInventoryVec(items);
        for (auto cur : items) {
            empty = false;
            if (IsHangarFlag(cur->flag()))
                cur->SetFlag(flagImpounded, true);
        }
    }

    StationItemRef sRef = sEntityList.GetStationByID(odata.stationID);
    if (sRef.get() != nullptr)
        sRef->RemoveOffice(officeID);
    stDataMgr.RemoveOffice(odata.stationID, officeID);
    sBillingMgr.CancelBill(odata.billID);
    m_warnedOffices.erase(officeID);

    if (empty) {
        StationDB::DeleteOffice(officeID);
        if (oRef.get() != nullptr)
            oRef->Delete();
    } else {
        StationDB::SetOfficeImpounded(officeID, true);
    }

    SendOfficeChanged(odata.corporationID, 0, 0);
    NotifyOfficeExpiration(odata, errorText, 0);

    _log(CORP__INFO, "CorpMgr::ExpireOffice() - office %u of corp %u in station %u %s", officeID, odata.corporationID, odata.stationID,
         (empty ? "removed" : "impounded"));
}

void CorpMgr::ReactivateOffice(uint32 officeID)
{
    OfficeData odata = OfficeData();
    if (!StationDB::GetOfficeData(officeID, odata) or !odata.impounded)
        return;

    StationDB::SetOfficeImpounded(officeID, false);
    odata.impounded = false;
    odata.billID = 0;
    odata.expiryTime = GetFileTimeNow() + OfficeRentalPeriod;
    StationDB::SetOfficeLease(officeID, odata.expiryTime, 0);

    // impounded items return to the first hangar division
    StationOfficeRef oRef = sItemFactory.GetOfficeRef(officeID);
    if ((oRef.get() != nullptr) and (oRef->GetMyInventory() != nullptr)) {
        oRef->GetMyInventory()->LoadContents();
        std::vector<InventoryItemRef> items;
        oRef->GetMyInventory()->GetItemsByFlag(flagImpounded, items);
        for (auto cur : items)
            cur->SetFlag(flagHangar, true);
    }

    StationItemRef sRef = sEntityList.GetStationByID(odata.stationID);
    if (sRef.get() != nullptr)
        sRef->RestoreOffice(odata);
    stDataMgr.AddOffice(odata.stationID, odata);
    SendOfficeChanged(odata.corporationID, officeID, odata.folderID);

    IssueRentBill(officeID);
    _log(CORP__INFO, "CorpMgr::ReactivateOffice() - office %u of corp %u in station %u in use again", officeID, odata.corporationID, odata.stationID);
}

void CorpMgr::DeleteOffice(uint32 officeID)
{
    OfficeData odata = OfficeData();
    if (!StationDB::GetOfficeData(officeID, odata))
        return;

    StationOfficeRef oRef = sItemFactory.GetOfficeRef(officeID);
    StationItemRef sRef = sEntityList.GetStationByID(odata.stationID);
    if (sRef.get() != nullptr)
        sRef->RemoveOffice(officeID);
    stDataMgr.RemoveOffice(odata.stationID, officeID);
    sBillingMgr.CancelBill(odata.billID);
    StationDB::DeleteOffice(officeID);
    if (oRef.get() != nullptr)
        oRef->Delete();
    SendOfficeChanged(odata.corporationID, 0, 0);
}

void CorpMgr::ProcessOffices()
{
    if (!sConfig.corp.BillsEnabled)
        return;

    int64 now = GetFileTimeNow();

    // offices without a rent bill (rented before bills existed):  billed for their next period, with a week to pay when the lease already ran out
    std::vector<uint32> ids;
    StationDB::GetOfficesWithoutBill(ids);
    for (auto cur : ids) {
        OfficeData odata = OfficeData();
        if (!StationDB::GetOfficeData(cur, odata))
            continue;
        if (odata.expiryTime < now + EvE::Time::Day)
            StationDB::SetOfficeLease(cur, now + 7 * EvE::Time::Day, 0);
        IssueRentBill(cur);
    }

    // offices the outpost owner no longer rents out end with their lease
    ids.clear();
    StationDB::GetLapsedOffices(now, ids);
    for (auto cur : ids)
        ExpireOffice(cur, "The station owner no longer makes this office available.");

    // the rent is due soon and still unpaid
    ids.clear();
    StationDB::GetOfficesWithBillDue(now + OfficeExpiryWarning, ids);
    for (auto cur : ids) {
        if (!m_warnedOffices.insert(cur).second)
            continue;
        OfficeData odata = OfficeData();
        if (StationDB::GetOfficeData(cur, odata))
            NotifyOfficeExpiration(odata, "", (int32)EvE::max((int64)0, (odata.expiryTime - now + EvE::Time::Day - 1) / EvE::Time::Day));
    }
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

/* shares */
bool CorpMgr::IsActiveCEO(uint32 corpID, uint32 charID)
{
    if (!IsPlayerCorp(corpID))
        return false;
    if (CorporationDB::GetCorporationCEO(corpID) != charID)
        return false;
    return !IsCEORevoked(corpID, charID);
}

void CorpMgr::SendShareChange(uint32 holderID, uint32 corpID, uint32 oldShares, uint32 newShares)
{
    if (oldShares == newShares)
        return;

    // bco_shares.OnShareChange(shareholderID, corporationID, change):  an add has every old value None, a removal every new one
    uint32 holderCorpID = (IsCorp(holderID) ? holderID : CharacterDB::GetCorpID(holderID));
    PyDict* change = new PyDict();
    if (oldShares == 0) {
        AddChange(change, "corporationID", PyStatic.NewNone(), new PyInt(corpID));
        AddChange(change, "shareholderID", PyStatic.NewNone(), new PyInt(holderID));
        AddChange(change, "shares", PyStatic.NewNone(), new PyInt(newShares));
        AddChange(change, "shareholderCorporationID", PyStatic.NewNone(), new PyInt(holderCorpID));
    } else if (newShares == 0) {
        AddChange(change, "corporationID", new PyInt(corpID), PyStatic.NewNone());
        AddChange(change, "shareholderID", new PyInt(holderID), PyStatic.NewNone());
        AddChange(change, "shares", new PyInt(oldShares), PyStatic.NewNone());
        AddChange(change, "shareholderCorporationID", new PyInt(holderCorpID), PyStatic.NewNone());
    } else {
        AddChange(change, "shares", new PyInt(oldShares), new PyInt(newShares));
    }

    PyTuple* payload = new PyTuple(3);
        payload->SetItem(0, new PyInt(holderID));
        payload->SetItem(1, new PyInt(corpID));
        payload->SetItem(2, change);

    if (IsCorp(holderID)) {
        sEntityList.CorpNotify(holderID, Notify::Types::CorpNews, "OnShareChange", "corpid", payload);
        return;
    }

    Client* pClient = sEntityList.FindClientByCharID(holderID);
    if (pClient != nullptr) {
        pClient->SendNotification("OnShareChange", "charid", &payload);
    } else {
        PyDecRef(payload);
    }
}

void CorpMgr::TransferShares(uint32 corpID, uint32 fromID, uint32 toID, uint32 count)
{
    if (count == 0)
        return;
    if (fromID == toID)
        return;
    if (!IsPlayerCorp(corpID))
        throw CustomError("Only player corporations have shares.");
    if (!IsCharacterID(toID) and !IsPlayerCorp(toID))
        throw UserError("GiveSharesSelectOwner");

    uint32 fromOld(0), toOld(0);
    if (!CorporationDB::MoveShares(corpID, fromID, toID, count, fromOld, toOld))
        throw UserError("NotEnoughShares").AddFormatValue("amount", new PyInt(count)).AddFormatValue("balance", new PyInt(fromOld));

    SendShareChange(fromID, corpID, fromOld, fromOld - count);
    SendShareChange(toID, corpID, toOld, toOld + count);

    _log(CORP__INFO, "CorpMgr::TransferShares() - %u shares of corp %u from %u to %u", count, corpID, fromID, toID);
}

uint32 CorpMgr::GetVoteWeight(uint32 charID, uint32 corpID)
{
    uint32 weight = CorporationDB::GetSharesHeld(charID, corpID);

    // the CEO votes with the shares their corp holds (bco_voting.CanVote)
    CorpData data = CorpData();
    if (LoadMemberData(charID, data) and IsActiveCEO(data.corporationID, charID))
        weight += CorporationDB::GetSharesHeld(data.corporationID, corpID);

    return weight;
}

/* votes and sanctioned actions */
void CorpMgr::SendVoteCaseChanged(uint32 corpID, uint32 voteCaseID, PyDict* change)
{
    if (change == nullptr)
        return;
    PyTuple* payload = new PyTuple(3);
        payload->SetItem(0, new PyInt(corpID));
        payload->SetItem(1, new PyInt(voteCaseID));
        payload->SetItem(2, change);
    sEntityList.CorpNotify(corpID, Notify::Types::CorpNews, "OnCorporationVoteCaseChanged", "corpid", payload);
}

void CorpMgr::SendSanctionChanged(uint32 corpID, uint32 voteCaseID, PyDict* change)
{
    if (change == nullptr)
        return;
    PyTuple* payload = new PyTuple(3);
        payload->SetItem(0, new PyInt(corpID));
        payload->SetItem(1, new PyInt(voteCaseID));
        payload->SetItem(2, change);
    sEntityList.CorpNotify(corpID, Notify::Types::CorpNews, "OnSanctionedActionChanged", "corpid", payload);
}

void CorpMgr::OnVoteCaseOpened(uint32 corpID, uint32 voteCaseID, uint32 proposerID)
{
    Corp::VoteCase vc = Corp::VoteCase();
    if (!CorporationDB::GetVoteCase(voteCaseID, vc))
        return;

    SendVoteCaseChanged(corpID, voteCaseID, CorporationDB::GetVoteItemChange(voteCaseID));

    // CorpVote {subject, body} to the shareholders and the CEO
    std::map<uint32, uint32> holders;
    CorporationDB::GetShareholders(corpID, holders);
    std::vector<uint32> ids;
    for (auto cur : holders)
        if (IsCharacterID(cur.first))
            ids.push_back(cur.first);
    uint32 ceoID = CorporationDB::GetCorporationCEO(corpID);
    if (IsCharacterID(ceoID) and (holders.find(ceoID) == holders.end()))
        ids.push_back(ceoID);
    PyDict* data = new PyDict();
        data->SetItemString("subject", new PyWString(vc.voteCaseText));
        data->SetItemString("body", new PyWString(vc.description));
    NotificationMgr::Send(ids, Notify::Types::CorpVote, proposerID, data);

    if (vc.voteType != Corp::VoteType::CEO)
        return;

    // "{charID} is running for CEO in {corp}. Your roles as CEO have been revoked for the duration of the voting period."
    CorporationDB::SetLastCEOVote(corpID, GetFileTimeNow());
    InvalidateCEOVote(corpID);
    RefreshMemberSession(ceoID);
    data = new PyDict();
        data->SetItemString("corpID", new PyInt(corpID));
        data->SetItemString("charID", new PyInt(proposerID));
    NotifyChar(ceoID, Notify::Types::CorpVoteCEORevoked, proposerID, data);
}

void CorpMgr::ProcessVotes()
{
    std::vector<uint32> ids;
    CorporationDB::GetExpiredVoteCases(GetFileTimeNow(), ids);
    for (auto cur : ids)
        CloseVote(cur);
}

void CorpMgr::CloseVote(uint32 voteCaseID)
{
    Corp::VoteCase vc = Corp::VoteCase();
    if (!CorporationDB::GetVoteCase(voteCaseID, vc) or (vc.status != 2))
        return;

    std::vector<Corp::VoteCaseOptions> options;
    CorporationDB::GetVoteCaseOptions(voteCaseID, options);

    // the option with the most share-weighted votes wins;  a tie or no votes is a "don't" (the last option)
    double total(0), best(-1);
    int8 winner(-1);
    bool tie(false);
    for (auto cur : options) {
        total += cur.votesFor;
        if (cur.votesFor > best) {
            best = cur.votesFor;
            winner = cur.optionID;
            tie = false;
        } else if (cur.votesFor == best) {
            tie = true;
        }
    }
    if ((total <= 0) or tie)
        winner = ((vc.voteType == Corp::VoteType::General) or options.empty() ? -1 : options.back().optionID);
    // a CEO candidate needs more than half of the votes cast
    if ((vc.voteType == Corp::VoteType::CEO) and (winner == 0) and !options.empty() and (options.front().votesFor * 2 <= total))
        winner = options.back().optionID;

    int32 parameter(0);
    for (auto cur : options)
        if (cur.optionID == winner)
            parameter = cur.parameter;

    int64 now = GetFileTimeNow();
    bool inEffect(false), actedUpon(false), newCEO(false);
    int64 timeActedUpon(0), expires(0);
    switch (vc.voteType) {
        case Corp::VoteType::CEO: {
            CorpData data = CorpData();
            if ((winner >= 0) and (parameter != 0) and LoadMemberData(parameter, data) and (data.corporationID == vc.corporationID)) {
                newCEO = true;
                inEffect = actedUpon = true;
                timeActedUpon = now;
            }
        } break;
        case Corp::VoteType::General: {
            // a decision, nothing to implement
            if (winner >= 0) {
                inEffect = actedUpon = true;
                timeActedUpon = now;
            }
        } break;
        default: {
            // shares, kick, war, lockdown, unlock:  the CEO may implement a passed action until it expires
            if ((winner >= 0) and (parameter != 0))
                expires = now + (int64)sConfig.corp.SanctionDays * EvE::Time::Day;
        } break;
    }

    CorporationDB::CloseVoteCase(voteCaseID, winner, inEffect, actedUpon, timeActedUpon, expires);
    if (vc.voteType == Corp::VoteType::CEO)
        InvalidateCEOVote(vc.corporationID);

    PyDict* change = new PyDict();
        AddChange(change, "status", new PyInt(2), new PyInt(1));
        if (inEffect)
            AddChange(change, "inEffect", new PyInt(0), new PyInt(1));
        if (actedUpon) {
            AddChange(change, "actedUpon", new PyInt(0), new PyInt(1));
            AddChange(change, "timeActedUpon", new PyLong(0), new PyLong(timeActedUpon));
        }
    SendVoteCaseChanged(vc.corporationID, voteCaseID, change);
    if (winner >= 0)
        SendSanctionChanged(vc.corporationID, voteCaseID, CorporationDB::GetSanctionChange(voteCaseID));

    if (vc.voteType == Corp::VoteType::CEO) {
        if (newCEO) {
            ChangeCEO(vc.corporationID, parameter, true);
        } else {
            // the CEO's roles come back
            RefreshMemberSession(CorporationDB::GetCorporationCEO(vc.corporationID));
        }
    }

    _log(CORP__INFO, "CorpMgr::CloseVote() - vote case %u of corp %u (type %u) closed, winning option %i", voteCaseID, vc.corporationID, vc.voteType, winner);
}

void CorpMgr::ImplementSanction(Client* pCaller, uint32 voteCaseID)
{
    uint32 corpID = pCaller->GetCorporationID();
    uint32 charID = pCaller->GetCharacterID();
    Corp::VoteCase vc = Corp::VoteCase();
    if (!CorporationDB::GetVoteCase(voteCaseID, vc) or (vc.corporationID != corpID))
        throw CustomError("That sanctioned action could not be found.");
    if (!IsActiveCEO(corpID, charID))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You can not implement this sanctioned action unless you are the active CEO."));
    if ((vc.status != 1) or (vc.winningOptionID < 0) or vc.actedUpon or vc.inEffect)
        throw CustomError("This sanctioned action can not be implemented.");
    if (vc.expires <= GetFileTimeNow())
        throw CustomError("This sanctioned action has expired.");

    std::vector<Corp::VoteCaseOptions> options;
    CorporationDB::GetVoteCaseOptions(voteCaseID, options);
    Corp::VoteCaseOptions option = Corp::VoteCaseOptions();
    for (auto cur : options)
        if (cur.optionID == vc.winningOptionID)
            option = cur;
    if (option.parameter == 0)
        throw CustomError("This sanctioned action can not be implemented.");

    int64 now = GetFileTimeNow();
    bool news(false);
    switch (vc.voteType) {
        case Corp::VoteType::Shares: {
            uint32 oldTotal = CorporationDB::GetCorpShares(corpID);
            uint32 oldHeld = CorporationDB::GetSharesHeld(corpID, corpID);
            CorporationDB::IssueShares(corpID, option.parameter);
            SendShareChange(corpID, corpID, oldHeld, oldHeld + option.parameter);
            PyDict* change = new PyDict();
                AddChange(change, "shares", new PyInt(oldTotal), new PyInt(oldTotal + option.parameter));
            BroadcastCorpChange(corpID, change);
            news = true;
        } break;
        case Corp::VoteType::KickMember: {
            // the vote replaces the stasis rule, but not someone in space
            CorpData data = CorpData();
            if (!LoadMemberData(option.parameter, data) or (data.corporationID != corpID))
                throw CustomError("That character is no longer a member of the corporation.");
            if (CorporationDB::GetCorporationCEO(corpID) == (uint32)option.parameter)
                throw CustomError("The CEO cannot be expelled from the corporation.");
            Client* pTarget = sEntityList.FindClientByCharID(option.parameter);
            if ((pTarget != nullptr) and !pTarget->IsDocked())
                throw UserError("CrpCanNotKickMemberInSpace");
            LeaveCorp(option.parameter, true, charID);
            news = true;
        } break;
        case Corp::VoteType::ItemLock: {
            ItemLockdown::Lock(corpID, option.parameter, option.parameter2);
        } break;
        case Corp::VoteType::ItemUnlock: {
            ItemLockdown::Unlock(corpID, option.parameter);
            // the lockdown it ends is rescinded
            uint32 lockCaseID = CorporationDB::GetInEffectLockdown(corpID, option.parameter);
            Corp::VoteCase lock = Corp::VoteCase();
            if ((lockCaseID != 0) and CorporationDB::GetVoteCase(lockCaseID, lock)) {
                lock.inEffect = false;
                lock.rescended = true;
                lock.timeRescended = now;
                CorporationDB::UpdateSanction(lock);
                PyDict* change = new PyDict();
                    AddChange(change, "inEffect", new PyInt(1), new PyInt(0));
                    AddChange(change, "timeRescended", new PyLong(0), new PyLong(now));
                SendSanctionChanged(corpID, lockCaseID, change);
            }
        } break;
        case Corp::VoteType::War: {
            // a corporation in an alliance goes to war through the alliance
            if (pCaller->GetAllianceID() != 0)
                throw UserError("CrpCanNotSanctionWarOwner");
            // the declaration is charged now (throws NotEnoughMoneyCorp)
            sWarMgr.DeclareWar(corpID, option.parameter, pCaller->GetCharacterID());
        } break;
        default:
            throw CustomError("This sanctioned action can not be implemented.");
    }

    vc.inEffect = true;
    vc.actedUpon = true;
    vc.timeActedUpon = now;
    CorporationDB::UpdateSanction(vc);

    PyDict* change = new PyDict();
        AddChange(change, "inEffect", new PyInt(0), new PyInt(1));
        AddChange(change, "actedUpon", new PyInt(0), new PyInt(1));
        AddChange(change, "timeActedUpon", new PyLong(0), new PyLong(now));
    SendSanctionChanged(corpID, voteCaseID, change);

    if (news) {
        // CorpNews {corpID, voteType, parameter, inEffect, body}
        PyDict* data = new PyDict();
            data->SetItemString("corpID", new PyInt(corpID));
            data->SetItemString("voteType", new PyInt(vc.voteType));
            data->SetItemString("parameter", new PyInt(option.parameter));
            data->SetItemString("inEffect", new PyInt(1));
            data->SetItemString("body", new PyString(""));
        NotifyCorp(corpID, Notify::Types::CorpNews, charID, data);
    }

    _log(CORP__INFO, "CorpMgr::ImplementSanction() - %u implemented vote case %u (type %u) of corp %u", charID, voteCaseID, vc.voteType, corpID);
}

/* dividends */
void CorpMgr::PayDividend(Client* pCaller, uint32 corpID, bool shareholders, double amount)
{
    uint32 charID = pCaller->GetCharacterID();
    if (!IsActiveCEO(corpID, charID))
        throw UserError("OnlyCEOCanPayoutDividends");
    if (amount < 0.01)
        return;

    uint16 key = pCaller->GetCorpAccountKey();
    if ((key < Account::KeyType::Cash) or (key > Account::KeyType::Cash7))
        key = Account::KeyType::Cash;
    if (AccountDB::GetCorpBalance(corpID, key) < amount)
        throw UserError("CorpHasNoMoneyToPayoutDividends");

    // payee -> share of the payout
    std::map<uint32, double> payouts;
    if (shareholders) {
        // "based upon the number of shares you hold, in relation to the total number of shares issued" (the corp's own treasury holds no claim)
        std::map<uint32, uint32> holders;
        CorporationDB::GetShareholders(corpID, holders);
        holders.erase(corpID);
        int64 held(0);
        for (auto cur : holders)
            held += cur.second;
        if (held == 0)
            throw CustomError("There are no shareholders to pay.");
        for (auto cur : holders)
            payouts[cur.first] = std::floor(amount * cur.second / held * 100) / 100;
    } else {
        // "split evenly between all members"
        std::vector<uint32> members, offline;
        CorporationDB::GetMemberIDs(corpID, members, true);
        CorporationDB::GetMemberIDs(corpID, offline, false);
        members.insert(members.end(), offline.begin(), offline.end());
        if (members.empty())
            return;
        double each = std::floor(amount / members.size() * 100) / 100;
        for (auto cur : members)
            payouts[cur] = each;
    }

    std::string reason = "Dividend payment from ";
    reason += CorporationDB::GetCorpName(corpID);
    for (auto cur : payouts) {
        if (cur.second < 0.01)
            continue;
        AccountService::TransferFunds(corpID, cur.first, cur.second, reason, Journal::EntryType::CorporationDividendPayment, charID, key);

        PyDict* data = new PyDict();
            data->SetItemString("corpID", new PyInt(corpID));
            data->SetItemString("payout", new PyFloat(cur.second));
            data->SetItemString("isMembers", new PyBool(!shareholders));
        if (IsCharacterID(cur.first)) {
            NotifyChar(cur.first, Notify::Types::CorpDividend, charID, data);
        } else {
            NotifyCorp(cur.first, Notify::Types::CorpDividend, charID, data, Corp::Role::Accountant);
        }
    }

    _log(CORP__INFO, "CorpMgr::PayDividend() - corp %u paid %.2f to %u %s", corpID, amount, payouts.size(), (shareholders ? "shareholders" : "members"));
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
