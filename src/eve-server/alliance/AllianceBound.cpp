
/**
  * @name AllianceBound.cpp
  *     Alliance Bound code
  *
  * @Author:        Allan
  * Updated:        James
  * @date:          16 January 2018
  */

#include <string>

#include "EVE_Corp.h"
#include "EVE_Mail.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "alliance/AllianceBound.h"
#include "alliance/AllianceRegistry.h"
#include "alliance/AllianceDB.h"
#include "alliance/AllianceData.h"
#include "cache/ObjCacheService.h"
#include "corporation/BillingMgr.h"
#include "chat/LSCService.h"
#include "packets/CorporationPkts.h"
#include "character/CharacterDB.h"
#include "corporation/CorporationDB.h"
#include "faction/WarManager.h"
#include "alliance/AllianceManager.h"
#include "corporation/CorpMgr.h"
#include "mail/MailMgr.h"
#include "EVEServerConfig.h"
#include "station/StationDB.h"
#include "station/StationDataMgr.h"

/*
 * ALLY__ERROR
 * ALLY__WARNING
 * ALLY__INFO
 * ALLY__MESSAGE
 * ALLY__TRACE
 * ALLY__CALL
 * ALLY__CALL_DUMP
 * ALLY__RSP_DUMP
 */

AllianceBound::AllianceBound(EVEServiceManager& mgr, AllianceRegistry& parent, AllianceDB& db, uint32 allyID)
    : EVEBoundObject(mgr, parent),
      m_db(db)
{
    this->Add("GetAlliance", &AllianceBound::GetAlliance);
    this->Add("AddBulletin", &AllianceBound::AddBulletin);
    this->Add("GetBulletins", &AllianceBound::GetBulletins);
    this->Add("DeleteBulletin", &AllianceBound::DeleteBulletin);
    this->Add("CreateLabel", &AllianceBound::CreateLabel);
    this->Add("GetLabels", &AllianceBound::GetLabels);
    this->Add("DeleteLabel", &AllianceBound::DeleteLabel);
    this->Add("EditLabel", &AllianceBound::EditLabel);
    this->Add("AssignLabels", &AllianceBound::AssignLabels);
    this->Add("RemoveLabels", &AllianceBound::RemoveLabels);
    this->Add("GetMembers", &AllianceBound::GetMembers);
    this->Add("DeclareExecutorSupport", &AllianceBound::DeclareExecutorSupport);
    this->Add("DeleteMember", &AllianceBound::DeleteMember);
    this->Add("GetApplications", &AllianceBound::GetApplications);
    this->Add("UpdateApplication", &AllianceBound::UpdateApplication);
    this->Add("AddToVoiceChat", &AllianceBound::AddToVoiceChat);
    this->Add("PayBill", &AllianceBound::PayBill);
    this->Add("GetBills", &AllianceBound::GetBills);
    this->Add("GetBillsReceivable", &AllianceBound::GetBillsReceivable);
    this->Add("GetBillBalance", &AllianceBound::GetBillBalance);
    this->Add("GetAllianceContacts", &AllianceBound::GetAllianceContacts);
    this->Add("AddAllianceContact", &AllianceBound::AddAllianceContact);
    this->Add("EditAllianceContact", &AllianceBound::EditAllianceContact);
    this->Add("RemoveAllianceContacts", &AllianceBound::RemoveAllianceContacts);
    this->Add("EditContactsRelationshipID", &AllianceBound::EditContactsRelationshipID);
    this->Add("UpdateAlliance", &AllianceBound::UpdateAlliance);
    this->Add("GetRelationships", &AllianceBound::GetRelationships);
    this->Add("SetRelationship", &AllianceBound::SetRelationship);
    this->Add("DeleteRelationship", &AllianceBound::DeleteRelationship);
    this->Add("DeclareWarAgainst", &AllianceBound::DeclareWarAgainst);
    this->Add("RetractWar", &AllianceBound::RetractWar);
    this->Add("ChangeMutualWarFlag", &AllianceBound::ChangeMutualWarFlag);

    m_allyID = allyID;
    this->m_cache = this->GetServiceManager().Lookup <ObjCacheService>("objectCaching");
    this->m_lsc = this->GetServiceManager().Lookup <LSCService>("LSC");
}

PyResult AllianceBound::GetAlliance(PyCallArgs &call)
{
    // Works
    //   self.members = self.GetMoniker().GetAlliance()
    _log(ALLY__CALL, "AllianceBound::Handle_GetAlliance() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    // called by member of this alliance
    return m_db.GetAlliance(m_allyID);
}

PyResult AllianceBound::GetMembers(PyCallArgs &call)
{
    // Works
    //   self.members = self.GetMoniker().GetMembers()
    _log(ALLY__CALL, "AllianceBound::Handle_GetMembers() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    return m_db.GetMembers(m_allyID);
}

PyResult AllianceBound::DeclareExecutorSupport(PyCallArgs &call, PyInt* chosenExecutor)
{
    //   self.GetMoniker().DeclareExecutorSupport(corpID)    the member's CEO or a director, for a member corp
    _log(ALLY__CALL, "AllianceBound::Handle_DeclareExecutorSupport() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    AllianceManager::RequireMember(call.client, m_allyID);
    if (!AllianceManager::IsCEOOrDirector(call.client))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Only the CEO or a director can declare executor support."));
    uint32 corpID(call.client->GetCorporationID()), chosen(chosenExecutor->value());
    if (!AllianceDB::IsMember(m_allyID, chosen))
        throw UserError("AllianceExecutorInOtherAlliance");
    int64 since = AllianceDB::GetMemberStartDate(corpID);
    if ((int64)GetFileTimeNow() - since < (int64)sConfig.alliance.ExecutorMinMemberDays * EvE::Time::Day)
        throw UserError("CanNotDeclareExecutorInFirstWeek");

    uint32 oldChosen(AllianceDB::GetChosenExecutor(corpID));
    if (oldChosen == chosen)
        return nullptr;
    m_db.DeclareExecutorSupport(corpID, chosen);
    AllianceManager::SendMemberChanged(m_allyID, corpID, AllianceManager::MemberUpdated, oldChosen, chosen);
    return nullptr;
}

PyResult AllianceBound::DeleteMember(PyCallArgs &call, PyInt* corporationID)
{
    //  self.GetMoniker().DeleteMember(corpID)    quit (own corp:  its CEO or a director) or kick (the executor's directors)
    _log(ALLY__CALL, "AllianceBound::Handle_DeleteMember() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    AllianceManager::RequireMember(call.client, m_allyID);
    uint32 corpID(corporationID->value());
    if (corpID == (uint32)call.client->GetCorporationID()) {
        if (!AllianceManager::IsCEOOrDirector(call.client))
            throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Only the CEO or a director can take the corporation out of the alliance."));
    } else {
        AllianceManager::RequireExecutorDirector(call.client, m_allyID);
        if (!AllianceDB::IsMember(m_allyID, corpID))
            throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("That corporation is not a member of the alliance."));
    }

    sAllianceMgr.LeaveAlliance(corpID, m_allyID);
    return nullptr;
}

PyResult AllianceBound::GetApplications(PyCallArgs &call)
{
    //   self.applications = self.GetMoniker().GetApplications()
    // Get all applications to our alliance
    _log(ALLY__CALL, "AllianceBound::Handle_GetApplications() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    return m_db.GetApplications(m_allyID);
}

PyResult AllianceBound::UpdateApplication(PyCallArgs &call, PyInt* corporationID, PyWString* applicationText, PyInt* state)
{
    //    return self.GetMoniker().UpdateApplication(corpID, applicationText, state)    the executor's directors accept or reject
    _log(ALLY__CALL, "AllianceBound::Handle_UpdateApplication() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    AllianceManager::RequireExecutorDirector(call.client, m_allyID);

    uint32 corpID(corporationID->value());
    Alliance::ApplicationInfo oldInfo = Alliance::ApplicationInfo();
    if (!m_db.GetCurrentApplicationInfo(m_allyID, corpID, oldInfo))
        throw CustomError("That application could not be found.");
    if (oldInfo.state != EveAlliance::AppStatus::AppNew)
        throw CustomError("That application has already been decided.");

    Alliance::ApplicationInfo newInfo = oldInfo;
    newInfo.valid = true;
    newInfo.appText = applicationText->content();
    switch (state->value()) {
        case EveAlliance::AppStatus::AppAccepted: {
            // the corp's wars are adopted, but wars it declared itself can't be
            if (sWarMgr.HasDeclaredWars(corpID))
                throw UserError("CanNotAdoptWarsAsCorpHasDesclaredWarsInProgress");
            if (sWarMgr.WarOwnerOf(corpID) != corpID)
                throw CustomError("That corporation is already a member of an alliance.");
            newInfo.state = EveAlliance::AppStatus::AppAccepted;
        } break;
        case EveAlliance::AppStatus::AppRejected: {
            newInfo.state = EveAlliance::AppStatus::AppRejected;
        } break;
        default:
            throw CustomError("An application can only be accepted or rejected.");
    }
    if (!m_db.UpdateApplication(newInfo))
        throw CustomError("The application could not be updated.");
    AllianceManager::SendApplicationChanged(oldInfo, newInfo);

    uint32 ceoID(CorporationDB::GetCorporationCEO(corpID));
    if (newInfo.state == EveAlliance::AppStatus::AppAccepted) {
        // "Accepted applications take 24 hours to become effective"
        if (sConfig.alliance.JoinDelayHours == 0) {
            sAllianceMgr.JoinAlliance(corpID, m_allyID, false);
        } else {
            AllianceDB::SetApplicationEffectiveAt(newInfo.appID, (int64)GetFileTimeNow() + (int64)sConfig.alliance.JoinDelayHours * EvE::Time::Hour);
            if (IsCharacterID(ceoID))
                MailMgr::SendMail(call.client->GetCharacterID(), ceoID, "Application to alliance accepted",
                    "Your application to join " + sDataMgr.GetOwnerName(m_allyID) + " has been accepted.  Your corporation becomes a member in "
                    + std::to_string(sConfig.alliance.JoinDelayHours) + " hours.");
        }
    } else if (IsCharacterID(ceoID)) {
        MailMgr::SendMail(call.client->GetCharacterID(), ceoID, "Application to alliance rejected",
            "Your application to join " + sDataMgr.GetOwnerName(m_allyID) + " has been rejected.");
    }
    _log(ALLY__INFO, "AllianceBound::UpdateApplication() - application of corp %u to alliance %u %s by %u", corpID, m_allyID,
         (newInfo.state == EveAlliance::AppStatus::AppAccepted ? "accepted" : "rejected"), call.client->GetCharacterID());
    return nullptr;
}

void AllianceBound::FillOAApplicationChange(OnAllianceApplicationChanged &OAAC, const Alliance::ApplicationInfo &Old, const Alliance::ApplicationInfo &New)
{
    if (Old.valid)
    {
        OAAC.applicationIDOld = new PyInt(Old.appID);
        OAAC.applicationTextOld = new PyString(Old.appText);
        OAAC.corporationIDOld = new PyInt(Old.corpID);
        OAAC.allianceIDOld = new PyInt(Old.allyID);
        OAAC.stateOld = new PyInt(Old.state);
        OAAC.applicationDateTimeOld = new PyLong(Old.appTime);
        if (Old.deleted == true)
        {
            OAAC.deletedOld = new PyBool(true);
        }
    }
    if (New.valid)
    {
        OAAC.applicationIDNew = new PyInt(New.appID);
        OAAC.applicationTextNew = new PyString(New.appText);
        OAAC.corporationIDNew = new PyInt(New.corpID);
        OAAC.allianceIDNew = new PyInt(New.allyID);
        OAAC.stateNew = new PyInt(New.state);
        OAAC.applicationDateTimeNew = new PyLong(New.appTime);
        if (New.deleted == true)
        {
            OAAC.deletedNew = new PyBool(true);
        }
    }
}

PyResult AllianceBound::AddToVoiceChat(PyCallArgs &call, PyString* channelName)
{
    //    success = moniker.GetAlliance().AddToVoiceChat(vivoxChannelName)
    _log(ALLY__CALL, "AllianceBound::Handle_AddToVoiceChat() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);
    return nullptr;
}

PyResult AllianceBound::PayBill(PyCallArgs &call, PyInt* billID, PyInt* fromAccountKey)
{
    //   return self.GetMoniker().PayBill(billID, fromAccountKey)    an accountant of the executor corp
    _log(ALLY__CALL, "AllianceBound::Handle_PayBill() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    BillData bill = BillData();
    if (!BillingMgr::GetBill(billID->value(), bill) or (bill.debtorID != m_allyID))
        throw CustomError("That bill could not be found.");

    sBillingMgr.PayBill(call.client, billID->value(), fromAccountKey->value());
    return nullptr;
}

PyResult AllianceBound::GetBillBalance(PyCallArgs &call, PyInt* billID)
{
    //   return self.GetMoniker().GetBillBalance(billID)     what is still owed on the bill
    _log(ALLY__CALL, "AllianceBound::Handle_GetBillBalance() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    BillData bill = BillData();
    if (!BillingMgr::GetBill(billID->value(), bill) or (bill.debtorID != m_allyID))
        return new PyFloat(0);
    return new PyFloat(bill.paid == 0 ? bill.amount + bill.interest : 0);
}

PyResult AllianceBound::GetBills(PyCallArgs &call)
{
    //   return self.GetMoniker().GetBills()
    _log(ALLY__CALL, "AllianceBound::Handle_GetBills() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    return BillingMgr::GetBillsPayable(m_allyID);
}

PyResult AllianceBound::GetBillsReceivable(PyCallArgs &call)
{
    //   return self.GetMoniker().GetBillsReceivable()
    _log(ALLY__CALL, "AllianceBound::Handle_GetBillsReceivable() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    return BillingMgr::GetBillsReceivable(m_allyID);
}

PyResult AllianceBound::AddBulletin(PyCallArgs &call, PyRep* title, PyRep* body)
{
    //   sm.GetService('alliance').GetMoniker().AddBulletin(title, body)
    //  sm.GetService('alliance').GetMoniker().AddBulletin(title, body, bulletinID=bulletinID, editDateTime=editDateTime)  <-- this is to update bulletin
    _log(ALLY__CALL, "AllianceBound::Handle_AddBulletin() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireBulletinManager(call);
    std::string titleStr = PyRep::StringContent(title);
    std::string bodyStr = PyRep::StringContent(body);
    std::map<std::string, PyRep*>::iterator itr = call.byname.find("bulletinID");
    if ((itr != call.byname.end()) and (itr->second->IsInt() or itr->second->IsLong())) {
        AllianceDB::EditBulletin(m_allyID, (uint32)PyRep::IntegerValue(itr->second), call.client->GetCharacterID(), titleStr, bodyStr);
    } else {
        m_db.AddBulletin(m_allyID, call.client->GetCorporationID(), call.client->GetCharacterID(), titleStr, bodyStr);
    }
    return nullptr;
}

PyResult AllianceBound::DeleteBulletin(PyCallArgs &call, PyInt* id)
{
    //   sm.GetService('alliance').GetMoniker().DeleteBulletin(id)
    _log(ALLY__CALL, "AllianceBound::Handle_DeleteBulletin() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireBulletinManager(call);
    AllianceDB::DeleteBulletin(m_allyID, id->value());
    return nullptr;
}

void AllianceBound::RequireBulletinManager(PyCallArgs& call)
{
    // base_corporation:  communications officers (corpRoleChatManager) of the member corps edit the alliance bulletins
    AllianceManager::RequireMember(call.client, m_allyID);
    if (!CorpMgr::HasRole(call.client->GetCorpRole(), Corp::Role::ChatManager))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You must be a communications officer to manage alliance bulletins."));
}

PyResult AllianceBound::GetBulletins(PyCallArgs &call)
{
    //   self.bulletins = self.GetMoniker().GetBulletins()
    _log(ALLY__CALL, "AllianceBound::Handle_GetBulletins() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);
    return m_db.GetBulletins(m_allyID);
}

PyResult AllianceBound::GetAllianceContacts(PyCallArgs &call)
{
    //    return self.GetMoniker().GetAllianceContacts()
    _log(ALLY__CALL, "AllianceBound::Handle_GetAllianceContacts() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    return m_db.GetContacts(m_allyID);
}

namespace {
    // contactIDs from the client:  integers only
    void ReadContactIDs(PyList* list, std::vector<int32>& into)
    {
        for (PyList::const_iterator itr = list->begin(); itr != list->end(); ++itr)
            if ((*itr)->IsInt() or (*itr)->IsLong())
                into.push_back((int32)PyRep::IntegerValue(*itr));
    }

    // standings come as int or float
    float ReadLevel(PyRep* rep)
    {
        if (rep->IsInt() or rep->IsLong())
            return (float)PyRep::IntegerValue(rep);
        return (float)PyRep::FloatValue(rep);
    }

    PyTuple* ContactUpdate(int32 contactID, PyRep* relationship, PyRep* labelMask)
    {
        PyTuple* update = new PyTuple(3);
            update->SetItem(0, new PyInt(contactID));
            update->SetItem(1, relationship);
            update->SetItem(2, labelMask);
        return update;
    }

    void AddChange(PyDict* change, const char* key, PyRep* oldValue, PyRep* newValue)
    {
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, oldValue);
            tuple->SetItem(1, newValue);
        change->SetItemString(key, tuple);
    }
}

void AllianceBound::RequireDiplomat(PyCallArgs& call)
{
    // addressbook.ShowLabelMenuAndManageBtn('alliancecontact'):  directors and diplomats of the executor corporation
    AllianceManager::RequireMember(call.client, m_allyID);
    if ((AllianceDB::GetExecutorID(m_allyID) != (uint32)call.client->GetCorporationID())
    or !CorpMgr::HasRole(call.client->GetCorpRole(), Corp::Role::Diplomat))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You must be a diplomat or director of the executor corporation to manage alliance contacts."));
}

void AllianceBound::SendContactsUpdated(PyList* updates)
{
    // addressbook.OnOrganizationContactsUpdated(ownerID, [(contactID, relationshipID|None, labelMask|None)]);  (id, None, None) = removed
    if (updates->empty()) {
        PyDecRef(updates);
        return;
    }
    PyTuple* payload = new PyTuple(2);
        payload->SetItem(0, new PyInt(m_allyID));
        payload->SetItem(1, updates);
    AllianceManager::SendToAlliance(m_allyID, "OnOrganizationContactsUpdated", payload);
}

void AllianceBound::SendLabelMasks(const std::vector<int32>& contactIDs)
{
    std::map<int32, int64> masks;
    AllianceDB::GetContactLabelMasks(m_allyID, contactIDs, masks);
    PyList* updates = new PyList();
    for (auto cur : masks)
        updates->AddItem(ContactUpdate(cur.first, PyStatic.NewNone(), new PyLong(cur.second)));
    SendContactsUpdated(updates);
}

PyResult AllianceBound::AddAllianceContact(PyCallArgs &call, PyInt* contactID, PyRep* relationshipID)
{
    //   self.GetMoniker().AddAllianceContact(contactID, relationshipID)
    _log(ALLY__CALL, "AllianceBound::Handle_AddAllianceContact() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    float level = ReadLevel(relationshipID);
    m_db.AddContact(m_allyID, contactID->value(), level);

    PyList* updates = new PyList();
        updates->AddItem(ContactUpdate(contactID->value(), new PyFloat(level), PyStatic.NewNone()));
    SendContactsUpdated(updates);
    return nullptr;
}

PyResult AllianceBound::EditAllianceContact(PyCallArgs &call, PyInt* contactID, PyRep* relationshipID)
{
    //   self.GetMoniker().EditAllianceContact(contactID, relationshipID)
    _log(ALLY__CALL, "AllianceBound::Handle_EditAllianceContact() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    float level = ReadLevel(relationshipID);
    m_db.UpdateContact(level, contactID->value(), m_allyID);

    PyList* updates = new PyList();
        updates->AddItem(ContactUpdate(contactID->value(), new PyFloat(level), PyStatic.NewNone()));
    SendContactsUpdated(updates);
    return nullptr;
}

PyResult AllianceBound::RemoveAllianceContacts(PyCallArgs &call, PyList* contactIDs)
{
    //   self.GetMoniker().RemoveAllianceContacts(contactIDs)
    _log(ALLY__CALL, "AllianceBound::Handle_RemoveAllianceContacts() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    std::vector<int32> ids;
    ReadContactIDs(contactIDs, ids);
    PyList* updates = new PyList();
    for (auto cur : ids) {
        m_db.RemoveContact(cur, m_allyID);
        updates->AddItem(ContactUpdate(cur, PyStatic.NewNone(), PyStatic.NewNone()));
    }
    SendContactsUpdated(updates);
    return nullptr;
}

PyResult AllianceBound::EditContactsRelationshipID(PyCallArgs &call, PyList* contactIDs, PyRep* relationshipID)
{
    //    self.GetMoniker().EditContactsRelationshipID(contactIDs, relationshipID)
    _log(ALLY__CALL, "AllianceBound::Handle_EditContactsRelationshipID() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    float level = ReadLevel(relationshipID);
    std::vector<int32> ids;
    ReadContactIDs(contactIDs, ids);
    PyList* updates = new PyList();
    for (auto cur : ids) {
        m_db.UpdateContact(level, cur, m_allyID);
        updates->AddItem(ContactUpdate(cur, new PyFloat(level), PyStatic.NewNone()));
    }
    SendContactsUpdated(updates);
    return nullptr;
}

PyResult AllianceBound::GetLabels(PyCallArgs &call)
{
    //   return self.GetMoniker().GetLabels()    dict labelID -> KeyVal(labelID, name, color), labelIDs are bit values
    _log(ALLY__CALL, "AllianceBound::Handle_GetLabels() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    return CharacterDB::GetContactLabels("alnLabels", m_allyID);
}

PyResult AllianceBound::CreateLabel(PyCallArgs &call, PyRep* name, std::optional<PyRep*> color)
{
    //   return self.GetMoniker().CreateLabel(name, color)    -> the new labelID (a bit value)
    _log(ALLY__CALL, "AllianceBound::Handle_CreateLabel() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    std::string labelName = PyRep::StringContent(name);
    if (labelName.empty())
        throw UserError("MailLabelMustProvideName");
    MailMgr::TruncateUtf8(labelName, mailMaxLabelSize);
    if (CharacterDB::ContactLabelNameExists("alnLabels", m_allyID, labelName))
        throw UserError("MailLabelNameExists");

    int32 colorVal(0);
    bool hasColor = (color.has_value() and (color.value() != nullptr) and !color.value()->IsNone());
    if (hasColor)
        colorVal = (int32)PyRep::IntegerValue(color.value());

    int64 labelID = CharacterDB::CreateContactLabel("alnLabels", m_allyID, labelName, hasColor ? &colorVal : nullptr);
    if (labelID == 0)
        throw CustomError("Your alliance has too many contact labels.");
    return new PyLong(labelID);
}

PyResult AllianceBound::DeleteLabel(PyCallArgs &call, PyRep* labelID)
{
    //   self.GetMoniker().DeleteLabel(labelID)    the label's bit is cleared from every contact
    _log(ALLY__CALL, "AllianceBound::Handle_DeleteLabel() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    CharacterDB::DeleteContactLabel("alnLabels", "alnContacts", m_allyID, PyRep::IntegerValue(labelID));
    return nullptr;
}

PyResult AllianceBound::EditLabel(PyCallArgs &call, PyRep* labelID, PyRep* name, std::optional<PyRep*> color)
{
    //   self.GetMoniker().EditLabel(labelID, name, color)    None = unchanged
    _log(ALLY__CALL, "AllianceBound::Handle_EditLabel() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    int64 id = PyRep::IntegerValue(labelID);
    std::string labelName;
    bool hasName = !name->IsNone();
    if (hasName) {
        labelName = PyRep::StringContent(name);
        if (labelName.empty())
            throw UserError("MailLabelMustProvideName");
        MailMgr::TruncateUtf8(labelName, mailMaxLabelSize);
        if (CharacterDB::ContactLabelNameExists("alnLabels", m_allyID, labelName, id))
            throw UserError("MailLabelNameExists");
    }
    int32 colorVal(0);
    bool hasColor = (color.has_value() and (color.value() != nullptr) and !color.value()->IsNone());
    if (hasColor)
        colorVal = (int32)PyRep::IntegerValue(color.value());

    CharacterDB::EditContactLabel("alnLabels", m_allyID, id, hasName ? &labelName : nullptr, hasColor ? &colorVal : nullptr);
    return nullptr;
}

PyResult AllianceBound::AssignLabels(PyCallArgs &call, PyList* contactIDs, PyRep* labelMask)
{
    //   self.GetMoniker().AssignLabels(contactIDs, labelMask)
    _log(ALLY__CALL, "AllianceBound::Handle_AssignLabels() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    std::vector<int32> ids;
    ReadContactIDs(contactIDs, ids);
    CharacterDB::SetContactLabelBits("alnContacts", m_allyID, ids, PyRep::IntegerValue(labelMask), true);
    SendLabelMasks(ids);
    return nullptr;
}

PyResult AllianceBound::RemoveLabels(PyCallArgs &call, PyList* contactIDs, PyRep* labelMask)
{
    //   self.GetMoniker().RemoveLabels(contactIDs, labelMask)
    _log(ALLY__CALL, "AllianceBound::Handle_RemoveLabels() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireDiplomat(call);
    std::vector<int32> ids;
    ReadContactIDs(contactIDs, ids);
    CharacterDB::SetContactLabelBits("alnContacts", m_allyID, ids, PyRep::IntegerValue(labelMask), false);
    SendLabelMasks(ids);
    return nullptr;
}

PyResult AllianceBound::GetRelationships(PyCallArgs &call)
{
    //   self.relationships = self.GetMoniker().GetRelationships()    (the legacy relationships; contacts replaced them in the UI)
    _log(ALLY__CALL, "AllianceBound::Handle_GetRelationships() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    return AllianceDB::GetRelationships(m_allyID);
}

void AllianceBound::SendRelationshipChanged(uint32 toID, uint8 oldRel, uint8 newRel)
{
    // OnAllianceRelationshipChanged(allianceID, toID, {toID, relationship})
    PyDict* change = new PyDict();
    if (oldRel == 0) {
        AddChange(change, "toID", PyStatic.NewNone(), new PyInt(toID));
        AddChange(change, "relationship", PyStatic.NewNone(), new PyInt(newRel));
    } else if (newRel == 0) {
        AddChange(change, "toID", new PyInt(toID), PyStatic.NewNone());
        AddChange(change, "relationship", new PyInt(oldRel), PyStatic.NewNone());
    } else {
        AddChange(change, "toID", new PyInt(toID), new PyInt(toID));
        AddChange(change, "relationship", new PyInt(oldRel), new PyInt(newRel));
    }
    PyTuple* payload = new PyTuple(3);
        payload->SetItem(0, new PyInt(m_allyID));
        payload->SetItem(1, new PyInt(toID));
        payload->SetItem(2, change);
    AllianceManager::SendToAlliance(m_allyID, "OnAllianceRelationshipChanged", payload);
}

PyResult AllianceBound::SetRelationship(PyCallArgs &call, PyInt* relationship, PyInt* toID)
{
    //   self.GetMoniker().SetRelationship(relationship, toID)
    _log(ALLY__CALL, "AllianceBound::Handle_SetRelationship() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    AllianceManager::RequireExecutorDirector(call.client, m_allyID);
    uint32 to(toID->value());
    if ((to == m_allyID) or AllianceDB::IsMember(m_allyID, to))
        throw CustomError("There is no point in setting a relationship to your own alliance or its members.");
    if (!IsPlayerCorp(to) and !IsAlliance(to))
        throw CustomError("Relationships can only be set to player corporations and alliances.");
    int32 rel(relationship->value());
    if ((rel < EveAlliance::Relation::NAP) or (rel > EveAlliance::Relation::Enemy))
        throw CustomError("That is not a valid relationship.");

    uint8 oldRel(0);
    AllianceDB::GetRelationship(m_allyID, to, oldRel);
    if (oldRel == rel)
        return nullptr;
    AllianceDB::SetRelationship(m_allyID, to, rel);
    SendRelationshipChanged(to, oldRel, rel);
    return nullptr;
}

PyResult AllianceBound::DeleteRelationship(PyCallArgs &call, PyInt* toID)
{
    //   self.GetMoniker().DeleteRelationship(toID)
    _log(ALLY__CALL, "AllianceBound::Handle_DeleteRelationship() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    AllianceManager::RequireExecutorDirector(call.client, m_allyID);
    uint8 oldRel(0);
    if (!AllianceDB::GetRelationship(m_allyID, toID->value(), oldRel))
        return nullptr;
    AllianceDB::DeleteRelationship(m_allyID, toID->value());
    SendRelationshipChanged(toID->value(), oldRel, 0);
    return nullptr;
}

PyResult AllianceBound::UpdateAlliance(PyCallArgs &call, PyWString* description, PyWString* url)
{
    //    return self.GetMoniker().UpdateAlliance(description, url)
    _log(ALLY__CALL, "AllianceBound::Handle_UpdateAlliance() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    AllianceManager::RequireExecutorDirector(call.client, m_allyID);
    m_db.UpdateAlliance(m_allyID, description->content(), url->content());
    AllianceManager::SendAllianceChanged(m_allyID);
    return nullptr;
}

void AllianceBound::RequireWarDirector(PyCallArgs& call)
{
    // corp_ui_wars.IsInCharge / all_ui_home:  a director of the executor corporation
    if (!sWarMgr.IsInCharge(m_allyID, call.client))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Only directors of the alliance's executor corporation can do that."));
}

PyResult AllianceBound::DeclareWarAgainst(PyCallArgs &call, PyInt* againstID)
{
    //   sm.GetService('alliance').DeclareWarAgainst(againstID)   after WarDeclareConfirm with GetCostOfWarAgainst
    _log(ALLY__CALL, "AllianceBound::Handle_DeclareWarAgainst() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireWarDirector(call);
    sWarMgr.DeclareWar(m_allyID, againstID->value(), call.client->GetCharacterID());
    return nullptr;
}

PyResult AllianceBound::RetractWar(PyCallArgs &call, PyInt* warID)
{
    //   sm.GetService('alliance').RetractWar(warID)
    _log(ALLY__CALL, "AllianceBound::Handle_RetractWar() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireWarDirector(call);
    sWarMgr.RetractWar(warID->value(), m_allyID);
    return nullptr;
}

PyResult AllianceBound::ChangeMutualWarFlag(PyCallArgs &call, PyInt* warID, PyRep* mutual)
{
    //   sm.GetService('alliance').ChangeMutualWarFlag(warID, mutual)
    _log(ALLY__CALL, "AllianceBound::Handle_ChangeMutualWarFlag() size=%lli", call.tuple->size());
    call.Dump(ALLY__CALL_DUMP);

    RequireWarDirector(call);
    sWarMgr.SetMutual(warID->value(), m_allyID, PyRep::IntegerValue(mutual) != 0);
    return nullptr;
}
