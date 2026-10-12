/*      CorpRegistryBound.cpp
 *      bound object to access functions for specific corp
 *
 *
 */

#include <string>

#include "EVE_Corp.h"
#include "EVE_Mail.h"
#include "CorpData.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "cache/ObjCacheService.h"
#include "chat/LSCService.h"
#include "corporation/CorpMgr.h"
#include "corporation/CorpRegistryService.h"
#include "corporation/CorpRegistryBound.h"
#include "corporation/OfficeSparseBound.h"
#include "corporation/ItemLockdown.h"
#include "station/StationDB.h"
#include "station/StationDataMgr.h"
#include "alliance/AllianceDB.h"
#include "alliance/AllianceBound.h"

/*
 * CORP__ERROR
 * CORP__WARNING
 * CORP__INFO
 * CORP__MESSAGE
 * CORP__TRACE
 * CORP__CALL
 * CORP__CALL_DUMP
 * CORP__RSP_DUMP
 * CORP__DB_ERROR
 * CORP__DB_WARNING
 * CORP__DB_INFO
 * CORP__DB_MESSAGE
 */

CorpRegistryBound::CorpRegistryBound(EVEServiceManager& mgr, CorpRegistryService& parent, CorporationDB& db, uint32 corpID) :
    EVEBoundObject(mgr, parent),
    m_db(db),
    m_offices (nullptr)
{
    this->Add("GetEveOwners", &CorpRegistryBound::GetEveOwners);
    this->Add("GetCorporation", &CorpRegistryBound::GetCorporation);
    this->Add("GetCorporations", &CorpRegistryBound::GetCorporations);
    this->Add("GetInfoWindowDataForChar", &CorpRegistryBound::GetInfoWindowDataForChar);
    this->Add("GetLockedItemLocations", &CorpRegistryBound::GetLockedItemLocations);
    this->Add("GetLockedItemsByLocation", &CorpRegistryBound::GetLockedItemsByLocation);
    this->Add("AddCorporation", &CorpRegistryBound::AddCorporation);
    this->Add("GetSuggestedTickerNames", &CorpRegistryBound::GetSuggestedTickerNames);
    this->Add("GetOffices", &CorpRegistryBound::GetOffices);
    this->Add("GetStations", &CorpRegistryBound::GetStations);

    this->Add("CreateRecruitmentAd", &CorpRegistryBound::CreateRecruitmentAd);
    this->Add("UpdateRecruitmentAd", &CorpRegistryBound::UpdateRecruitmentAd);
    this->Add("DeleteRecruitmentAd", &CorpRegistryBound::DeleteRecruitmentAd);
    this->Add("GetRecruiters", &CorpRegistryBound::GetRecruiters);
    this->Add("GetRecruitmentAdsForCorporation", &CorpRegistryBound::GetRecruitmentAdsForCorporation);
    this->Add("GetMyApplications", &CorpRegistryBound::GetMyApplications);
    this->Add("InsertApplication", &CorpRegistryBound::InsertApplication);
    this->Add("GetApplications", &CorpRegistryBound::GetApplications);
    this->Add("UpdateApplicationOffer", &CorpRegistryBound::UpdateApplicationOffer);
    this->Add("DeleteApplication", &CorpRegistryBound::DeleteApplication);
    this->Add("UpdateDivisionNames", &CorpRegistryBound::UpdateDivisionNames);
    this->Add("UpdateCorporation", &CorpRegistryBound::UpdateCorporation);
    this->Add("UpdateLogo", &CorpRegistryBound::UpdateLogo);
    this->Add("SetAccountKey", &CorpRegistryBound::SetAccountKey);
    this->Add("GetMember", &CorpRegistryBound::GetMember);
    this->Add("GetMembers", &CorpRegistryBound::GetMembers);
    this->Add("UpdateMember", &CorpRegistryBound::UpdateMember);
    this->Add("UpdateMembers", &CorpRegistryBound::UpdateMembers);

    this->Add("MoveCompanyShares", &CorpRegistryBound::MoveCompanyShares);
    this->Add("MovePrivateShares", &CorpRegistryBound::MovePrivateShares);
    this->Add("GetSharesByShareholder", &CorpRegistryBound::GetSharesByShareholder);
    this->Add("GetShareholders", &CorpRegistryBound::GetShareholders);
    this->Add("PayoutDividend", &CorpRegistryBound::PayoutDividend);

    this->Add("CanViewVotes", &CorpRegistryBound::CanViewVotes);
    this->Add("InsertVoteCase", &CorpRegistryBound::InsertVoteCase);
    this->Add("GetVotes", &CorpRegistryBound::GetVotes);
    this->Add("CanVote", &CorpRegistryBound::CanVote);
    this->Add("InsertVote", &CorpRegistryBound::InsertVote);
    this->Add("GetVoteCasesByCorporation", &CorpRegistryBound::GetVoteCasesByCorporation);
    this->Add("GetVoteCaseOptions", &CorpRegistryBound::GetVoteCaseOptions);
    this->Add("GetSanctionedActionsByCorporation", &CorpRegistryBound::GetSanctionedActionsByCorporation);
    this->Add("UpdateSanctionedAction", &CorpRegistryBound::UpdateSanctionedAction);

    this->Add("AddBulletin", &CorpRegistryBound::AddBulletin);
    this->Add("GetBulletins", &CorpRegistryBound::GetBulletins);
    this->Add("DeleteBulletin", &CorpRegistryBound::DeleteBulletin);

    this->Add("CreateLabel", &CorpRegistryBound::CreateLabel);
    this->Add("GetLabels", &CorpRegistryBound::GetLabels);
    this->Add("DeleteLabel", &CorpRegistryBound::DeleteLabel);
    this->Add("EditLabel", &CorpRegistryBound::EditLabel);
    this->Add("AssignLabels", &CorpRegistryBound::AssignLabels);
    this->Add("RemoveLabels", &CorpRegistryBound::RemoveLabels);

    this->Add("GetRecentKillsAndLosses", &CorpRegistryBound::GetRecentKillsAndLosses);
    this->Add("GetRoleGroups", &CorpRegistryBound::GetRoleGroups);
    this->Add("GetRoles", &CorpRegistryBound::GetRoles);
    this->Add("GetLocationalRoles", &CorpRegistryBound::GetLocationalRoles);

    this->Add("GetTitles", &CorpRegistryBound::GetTitles);
    this->Add("UpdateTitle", &CorpRegistryBound::UpdateTitle);
    this->Add("UpdateTitles", &CorpRegistryBound::UpdateTitles);
    this->Add("DeleteTitle", &CorpRegistryBound::DeleteTitle);
    this->Add("ExecuteActions", &CorpRegistryBound::ExecuteActions);

    this->Add("GetCorporateContacts", &CorpRegistryBound::GetCorporateContacts);
    this->Add("AddCorporateContact", &CorpRegistryBound::AddCorporateContact);
    this->Add("EditContactsRelationshipID", &CorpRegistryBound::EditContactsRelationshipID);
    this->Add("RemoveCorporateContacts", &CorpRegistryBound::RemoveCorporateContacts);
    this->Add("EditCorporateContact", &CorpRegistryBound::EditCorporateContact);

    this->Add("CreateAlliance", &CorpRegistryBound::CreateAlliance);
    this->Add("ApplyToJoinAlliance", &CorpRegistryBound::ApplyToJoinAlliance);
    this->Add("DeleteAllianceApplication", &CorpRegistryBound::DeleteAllianceApplication);
    this->Add("GetAllianceApplications", &CorpRegistryBound::GetAllianceApplications);
    this->Add("GetSuggestedAllianceShortNames", &CorpRegistryBound::GetSuggestedAllianceShortNames);

    this->Add("GetMembersPaged", &CorpRegistryBound::GetMembersPaged);
    this->Add("GetMembersByIds", &CorpRegistryBound::GetMembersByIds);
    this->Add("GetMemberIDsWithMoreThanAvgShares", &CorpRegistryBound::GetMemberIDsWithMoreThanAvgShares);
    this->Add("GetMemberIDsByQuery", &CorpRegistryBound::GetMemberIDsByQuery);
    this->Add("GetMemberTrackingInfo", &CorpRegistryBound::GetMemberTrackingInfo);
    this->Add("GetMemberTrackingInfoSimple", &CorpRegistryBound::GetMemberTrackingInfoSimple);
    this->Add("GetRentalDetailsPlayer", &CorpRegistryBound::GetRentalDetailsPlayer);
    this->Add("GetRentalDetailsCorp", &CorpRegistryBound::GetRentalDetailsCorp);

    this->Add("UpdateCorporationAbilities", &CorpRegistryBound::UpdateCorporationAbilities);
    this->Add("UpdateStationManagementSettings", &CorpRegistryBound::UpdateStationManagementSettings);
    this->Add("GetNumberOfPotentialCEOs", &CorpRegistryBound::GetNumberOfPotentialCEOs);

    this->Add("CanLeaveCurrentCorporation", &CorpRegistryBound::CanLeaveCurrentCorporation);
    this->Add("CanBeKickedOut", &CorpRegistryBound::CanBeKickedOut);
    this->Add("KickOutMember", &CorpRegistryBound::KickOutMember);
    this->Add("MemberCanRunForCEO", &CorpRegistryBound::MemberCanRunForCEO);
    this->Add("ResignFromCEO", &CorpRegistryBound::ResignFromCEO);

    m_corpID = corpID;

    this->m_cache = this->GetServiceManager().Lookup <ObjCacheService>("objectCaching");
    this->m_lsc = this->GetServiceManager().Lookup <LSCService>("LSC");
}

PyResult CorpRegistryBound::GetLocationalRoles(PyCallArgs &call)
{       // not sure if this is used...
    PyList* list = new PyList();
    list->AddItem(new PyInt(Corp::Role::HangarCanTake1));
    list->AddItem(new PyInt(Corp::Role::HangarCanTake2));
    list->AddItem(new PyInt(Corp::Role::HangarCanTake3));
    list->AddItem(new PyInt(Corp::Role::HangarCanTake4));
    list->AddItem(new PyInt(Corp::Role::HangarCanTake5));
    list->AddItem(new PyInt(Corp::Role::HangarCanTake6));
    list->AddItem(new PyInt(Corp::Role::HangarCanTake7));
    list->AddItem(new PyInt(Corp::Role::HangarCanQuery1));
    list->AddItem(new PyInt(Corp::Role::HangarCanQuery2));
    list->AddItem(new PyInt(Corp::Role::HangarCanQuery3));
    list->AddItem(new PyInt(Corp::Role::HangarCanQuery4));
    list->AddItem(new PyInt(Corp::Role::HangarCanQuery5));
    list->AddItem(new PyInt(Corp::Role::HangarCanQuery6));
    list->AddItem(new PyInt(Corp::Role::HangarCanQuery7));
    list->AddItem(new PyLong(Corp::Role::ContainerCanTake1));
    list->AddItem(new PyLong(Corp::Role::ContainerCanTake2));
    list->AddItem(new PyLong(Corp::Role::ContainerCanTake3));
    list->AddItem(new PyLong(Corp::Role::ContainerCanTake4));
    list->AddItem(new PyLong(Corp::Role::ContainerCanTake5));
    list->AddItem(new PyLong(Corp::Role::ContainerCanTake6));
    list->AddItem(new PyLong(Corp::Role::ContainerCanTake7));

    return list;
}

PyResult CorpRegistryBound::GetEveOwners(PyCallArgs &call) {
    /* this is a method-chaining call.
     * it comes with the bind request for a particular corp.
     *
     * the client wants a member list for given corp
     */
    return m_db.GetEveOwners(m_corpID);
}

PyResult CorpRegistryBound::GetInfoWindowDataForChar(PyCallArgs& call, std::optional <PyInt*> characterID)
{
    // if no characterID is specified, just return information for current character
    if (characterID.has_value() == false)
        return CharacterDB::GetInfoWindowDataForChar (call.client->GetCharacterID ());

    return CharacterDB::GetInfoWindowDataForChar (characterID.value()->value());
}

PyResult CorpRegistryBound::GetCorporation(PyCallArgs &call) {
    // called by member of this corp
    return m_db.GetCorporation(m_corpID);
}

PyResult CorpRegistryBound::GetRoles(PyCallArgs &call)
{   // working
    return m_db.GetCorpRoles();
}

PyResult CorpRegistryBound::GetRoleGroups(PyCallArgs &call)
{   // working
    return m_db.GetCorpRoleGroups();
}

PyResult CorpRegistryBound::GetTitles(PyCallArgs &call)
{   // working
    return m_db.GetTitles(m_corpID);
}

PyResult CorpRegistryBound::GetBulletins(PyCallArgs &call)
{   // working
    return m_db.GetBulletins(m_corpID);
}

PyResult CorpRegistryBound::GetCorporateContacts(PyCallArgs &call)
{   // working
    return m_db.GetContacts(m_corpID);
}

PyResult CorpRegistryBound::GetApplications(PyCallArgs &call)
{   // working
    return m_db.GetApplications(m_corpID);
}

PyResult CorpRegistryBound::GetRecruitmentAdsForCorporation(PyCallArgs &call)
{   // recruitments = self.GetCorpRegistry().GetRecruitmentAdsForCorporation()
    return m_db.GetAdRegistryData();
}

PyResult CorpRegistryBound::GetMyApplications(PyCallArgs &call)
{   // working
    return m_db.GetMyApplications(call.client->GetCharacterID());
}

PyResult CorpRegistryBound::GetMemberTrackingInfo(PyCallArgs &call)
{   // working
    return m_db.GetMemberTrackingInfo(m_corpID);
}

PyResult CorpRegistryBound::GetMemberTrackingInfoSimple(PyCallArgs &call)
{   // working
    return m_db.GetMemberTrackingInfoSimple(m_corpID);
}

PyResult CorpRegistryBound::GetShareholders(PyCallArgs &call)
{   // working
    return m_db.GetShares(m_corpID);
}

PyResult CorpRegistryBound::GetLabels(PyCallArgs &call)
{   // working
    return m_db.GetLabels(call.client->GetCorporationID());
}

PyResult CorpRegistryBound::CanViewVotes(PyCallArgs &call, PyInt* corporationID)
{   // working
    return m_db.PyHasShares(call.client->GetCharacterID(), corporationID->value());
}

PyResult CorpRegistryBound::GetMemberIDsWithMoreThanAvgShares(PyCallArgs &call)
{   // working
    return m_db.GetSharesForCorp(m_corpID);
}

PyResult CorpRegistryBound::GetRecruiters(PyCallArgs &call, PyInt* corpID, PyInt* adID)
{   // working
    return m_db.GetRecruiters(corpID->value(), adID->value());
}

PyResult CorpRegistryBound::DeleteTitle(PyCallArgs &call, PyInt* titleID)
{
    RequireDirector(call);

    // the title goes back to an empty 'Untitled n' and its holders lose it
    m_db.DeleteTitle(m_corpID, titleID->value());
    sCorpMgr.InvalidateTitles(m_corpID);

    std::vector<Client*> list;
    sEntityList.GetCorpClients(list, m_corpID);
    for (auto cur : list) {
        if ((cur == nullptr) or (cur->GetChar().get() == nullptr))
            continue;
        CorpData data = cur->GetChar()->GetCorpData();
        if ((data.titleMask & titleID->value()) == 0)
            continue;
        data.titleMask &= ~(int64)titleID->value();
        cur->GetChar()->UpdateCorpData(data);
    }
    CorporationDB::ClearTitle(m_corpID, titleID->value());
    return nullptr;
}

PyResult CorpRegistryBound::DeleteRecruitmentAd(PyCallArgs &call, PyInt* adID)
{   // working
    m_db.DeleteAdvert(adID->value());
    return nullptr;
}

PyResult CorpRegistryBound::GetSharesByShareholder(PyCallArgs &call, PyBool* corpShares)
{   // working
    return m_db.GetMyShares(corpShares->value() ? call.client->GetCorporationID() : call.client->GetCharacterID());
}

PyResult CorpRegistryBound::GetCorporations(PyCallArgs &call, PyInt* corporationID) {
    // working
    return m_db.GetCorporations(corporationID->value());
}

PyResult CorpRegistryBound::GetRecentKillsAndLosses(PyCallArgs &call, PyInt* number, PyInt* offset)
{   // working
    return m_db.GetKillsAndLosses(m_corpID, number->value(), offset->value());
}

PyResult CorpRegistryBound::GetMember(PyCallArgs &call, PyInt* characterID)
{
    // corp.members.GetMember(charID):  the member row (util.KeyVal), None when not a member
    std::vector<uint32> ids;
    ids.push_back(characterID->value());
    PyList* list = CorporationDB::GetMemberRows(m_corpID, &ids, 0, 0);
    if (list->empty()) {
        PyDecRef(list);
        return PyStatic.NewNone();
    }

    PyRep* rep = list->GetItem(0);
    PyIncRef(rep);
    PyDecRef(list);
    return rep;
}

void CorpRegistryBound::RequireMember(PyCallArgs& call)
{
    if (call.client->GetCorporationID() != (int32)m_corpID)
        throw CustomError("You are not a member of this corporation.");
}

void CorpRegistryBound::RequireDirector(PyCallArgs& call)
{
    RequireMember(call);
    if ((call.client->GetRolesAtAll() & Corp::Role::Director) != Corp::Role::Director)
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Only the CEO and directors can do that."));
}

PyResult CorpRegistryBound::SetAccountKey(PyCallArgs &call, PyInt* accountKey)
{   // working
    call.client->GetChar()->SetAccountKey(accountKey->value());
    return this->GetOID();
}

PyResult CorpRegistryBound::GetSuggestedTickerNames(PyCallArgs &call, PyWString* name)
{
    PyList* result = new PyList();
    SuggestedTickerName sTN;
    sTN.tName.clear();
    uint32 cnLen = name->content().length();
    // Easiest ticker-generation method: get the capital letters.
    for (uint32 i=0; i < cnLen; ++i)
        if (name->content()[i] >= 'A' && name->content()[i] <= 'Z')
            sTN.tName += name->content()[i];

    result->AddItem( sTN.Encode() );
    return result;
}

PyResult CorpRegistryBound::GetSuggestedAllianceShortNames(PyCallArgs &call, PyWString* name)
{
    PyList* result = new PyList();
    SuggestedShortName sSN;
    sSN.sName.clear();
    uint32 cnLen = name->content().length();
    // Easiest ticker-generation method: get the capital letters.
    for (uint32 i=0; i < cnLen; ++i)
        if (name->content()[i] >= 'A' && name->content()[i] <= 'Z')
            sSN.sName += name->content()[i];

    result->AddItem( sSN.Encode() );
    return result;
}

PyResult CorpRegistryBound::GetMembers(PyCallArgs &call)
{   // working
    // this just wants a member count and time (and headers)
    uint16 rowCount = m_db.GetMemberCount(m_corpID);

    // TODO: properly implement this call, an example is provided on GetOffices of what's needed

    /*  update....do we need to send header also??
     *
        [PyObjectData Name: util.SparseRowset]
          [PyTuple 3 items]
            [PyList 19 items]
              [PyString "characterID"]
              [PyString "corporationID"]
              [PyString "divisionID"]
              [PyString "squadronID"]
              [PyString "title"]
              [PyString "roles"]
              [PyString "grantableRoles"]
              [PyString "startDateTime"]
              [PyString "baseID"]
              [PyString "rolesAtHQ"]
              [PyString "grantableRolesAtHQ"]
              [PyString "rolesAtBase"]
              [PyString "grantableRolesAtBase"]
              [PyString "rolesAtOther"]
              [PyString "grantableRolesAtOther"]
              [PyString "titleMask"]
              [PyString "accountKey"]
              [PyString "rowDate"]
              [PyString "blockRoles"]
            [PySubStruct]
              [PySubStream 51 bytes]
                [PyTuple 3 items]
                  [PyString "N=700149:17018"]
                  [PyDict 1 kvp]
                    [PyString "realRowCount"]
                    [PyInt 3]
                  [PyIntegerVar 129515350203058462]
            [PyInt 3]
    */


    PyList* headers = new PyList;

    headers->AddItemString ("characterID");
    headers->AddItemString ("corporationID");
    headers->AddItemString ("divisionID");
    headers->AddItemString ("squadronID");
    headers->AddItemString ("title");
    headers->AddItemString ("roles");
    headers->AddItemString ("grantableRoles");
    headers->AddItemString ("startDateTime");
    headers->AddItemString ("baseID");
    headers->AddItemString ("rolesAtHQ");
    headers->AddItemString ("grantableRolesAtHQ");
    headers->AddItemString ("rolesAtBase");
    headers->AddItemString ("grantableRolesAtBase");
    headers->AddItemString ("rolesAtOther");
    headers->AddItemString ("grantableRolesAtOther");
    headers->AddItemString ("titleMask");
    headers->AddItemString ("accountKey");
    headers->AddItemString ("rowDate");
    headers->AddItemString ("blockRoles");

    // TODO: rewrite this to properly use different services, right now any calls to the MembersSparseRowset will be directed here
    PyDict *dict = new PyDict();
        dict->SetItemString("realRowCount", new PyInt(rowCount));   // this is current member count
    PyTuple* boundObject = new PyTuple(3);
        boundObject->SetItem(0, new PyString (this->GetIDString()));    // node info here
        boundObject->SetItem(1, dict);
        boundObject->SetItem(2, new PyLong(GetFileTimeNow()));

    GetMembersSparseRowset ret;
        ret.boundObject = boundObject;
        ret.realRowCount = rowCount;
    return ret.Encode();
}

PyResult CorpRegistryBound::UpdateDivisionNames(PyCallArgs &call,
    PyRep* div1, PyRep* div2, PyRep* div3, PyRep* div4, PyRep* div5, PyRep* div6, PyRep* div8,
    PyRep* wal1, PyRep* wal2, PyRep* wal3, PyRep* wal4, PyRep* wal5, PyRep* wal6, PyRep* wal7)
{   // working
    // TODO: stop using this and make use of the parameters, changing this is way too out of the scope of the service manager changes
    Call_UpdateDivisionNames args;
    if (!args.Decode(&call.tuple)) {
        codelog(SERVICE__ERROR, "CorpRegistryBound: Failed to decode arguments.");
        return PyStatic.NewNone();
    }

    RequireDirector(call);

    Notify_IntRaw notif;
        notif.key = m_corpID;
        notif.data = new PyDict();

    if (!m_db.UpdateDivisionNames(notif.key, args, notif.data)) {
        codelog(SERVICE__ERROR, "%s: Failed to update division names for corp %u", call.client->GetName(), notif.key);
        PyDecRef( notif.data );
        return PyStatic.NewNone();
    }

    sCorpMgr.BroadcastCorpChange(m_corpID, notif.data);
    notif.data = nullptr;

    // returns nodeID and timestamp
    return this->GetOID();
}

PyResult CorpRegistryBound::GetMembersPaged(PyCallArgs &call, PyInt* page) {
    // bco_members.PagedCorpMembers.PopulatePage(page):  1-based page of 50.  util.PagedCollection.Add stores the set at
    //  perPage * resultSet.page and reads page n from perPage * (n - 1), so the set's page is page - 1
    static const uint32 perPage = 50;
    uint32 pageNo = (page->value() > 1 ? page->value() : 1);

    PyDict* dict = new PyDict();
        dict->SetItemString("collection", CorporationDB::GetMemberRows(m_corpID, nullptr, (pageNo - 1) * perPage, perPage));
        dict->SetItemString("totalCount", new PyInt(m_db.GetMemberCount(m_corpID)));
        dict->SetItemString("page", new PyInt(pageNo - 1));
        dict->SetItemString("perPage", new PyInt(perPage));
    PyObject* rsp = new PyObject("util.PagedResultSet", dict);

    if (is_log_enabled(CORP__RSP_DUMP))
        rsp->Dump(CORP__RSP_DUMP, "");

    return rsp;
}

PyResult CorpRegistryBound::GetMembersByIds(PyCallArgs &call, PyList* memberIDs) {
    //return self.GetCorpRegistry().GetMembersByIds(memberIDs)    unknown IDs are left out
    std::vector<uint32> ids;
    for (PyList::const_iterator itr = memberIDs->begin(); itr != memberIDs->end(); ++itr)
        if ((*itr)->IsInt() or (*itr)->IsLong())
            ids.push_back(PyRep::IntegerValueU32(*itr));

    PyList* list = CorporationDB::GetMemberRows(m_corpID, &ids, 0, 0);
    if (is_log_enabled(CORP__RSP_DUMP))
        list->Dump(CORP__RSP_DUMP, "");

    return list;
}

PyResult CorpRegistryBound::AddCorporation(PyCallArgs &call,
    PyRep* name, PyRep* ticker, PyRep* description, PyRep* url, PyFloat* taxRate,
    PyRep* shape1, PyRep* shape2, PyRep* shape3,
    PyRep* color1, PyRep* color2, PyRep* color3,
    PyRep* typeface, PyInt* applicationEnabled) {

    Client* pClient(call.client);
    // TODO: stop using this and make use of the parameters, changing this is way too out of the scope of the service manager changes
    Call_AddCorporation args;
    if (!args.Decode(&call.tuple)) {
        codelog(SERVICE__ERROR, "CorpRegistryBound: Failed to decode arguments.");
        return nullptr;
    }

    // verify they're not using bad words in their corp name
    for (const auto cur : badWords)
        if (EvE::icontains(args.corpName, cur))
            throw CustomError ("Corp Name contains banned words");

    // this hits db directly, so test for possible sql injection code
    for (const auto cur : badCharsSearch)
        if (EvE::icontains(args.corpName, cur))
            throw CustomError ("Corp Name contains invalid characters");


    // this hits db directly, so test for possible sql injection code
    for (const auto cur : badCharsSearch)
        if (EvE::icontains(args.corpTicker, cur))
            throw CustomError ("Corp Ticker contains invalid characters");

    // verify ticker is available
    if (m_db.IsTickerTaken(args.corpTicker))
        throw UserError ("CorpTickerNameInvalidTaken");

    double corp_cost = sConfig.rates.corpCost;
    if (pClient->GetBalance(Account::CreditType::ISK) < corp_cost) {
        _log(SERVICE__ERROR, "%s: Cannot afford corporation startup costs!", pClient->GetName());
        // send error to client here?
        return nullptr;
    }

    //adding a corporation will affect eveStaticOwners, so we gotta invalidate the cache...
    //  call to db.AddCorporation() will update eveStaticOwners with new corp
    PyString* cache_name = new PyString( "config.StaticOwners" );
    this->m_cache->InvalidateCache( cache_name );
    PySafeDecRef( cache_name );

    // Register new corp
    uint32 corpID(0);
    if (!m_db.AddCorporation(args, pClient, corpID)) {
        codelog(SERVICE__ERROR, "New corporation creation failed.");
        return nullptr;
    }

    // make sure AI is set correctly in db.  sometimes it's not (cause i forget)
    if (!IsPlayerCorp(corpID)) {
        // not sure what to do here yet....delete everything, make sure db autoincrement is set right then reapply
        _log(CORP__ERROR, "Auto Increment improperly set in DB.");
        pClient->SendErrorMsg("Server Error when creating corp.  CorpID invalid.");
        return nullptr;
    }

    // create default role title data
    m_db.CreateTitleData(corpID);

    //take the money, send wallet blink event record the transaction in their journal.
    std::string reason = "DESC: Creating new corporation: ";
    reason += args.corpName;
    reason += " (";
    reason += args.corpTicker;
    reason += ")";

    AccountService::TransferFunds(
        pClient->GetCharacterID(),
        m_db.GetStationOwner(pClient->GetStationID()),  // station owner files paperwork, this is fee for that
        corp_cost,
        reason.c_str(),
        Journal::EntryType::CorporationRegistrationFee,
        pClient->GetStationID(),
        Account::KeyType::Cash
    );

    // add corp event for creating new corp
    m_db.AddItemEvent(corpID, pClient->GetCharacterID(), Corp::EventType::CreatedCorporation);

    // create corp channel
    this->m_lsc->CreateSystemChannel(corpID);

    CorpData data = CorpData();
        data.name = args.corpName;
        data.ticker = args.corpTicker;
        data.taxRate = args.taxRate;
        data.baseID = pClient->GetLocationID();
        data.corpHQ = pClient->GetLocationID();
        data.corporationID = corpID;
        data.corpAccountKey = Account::KeyType::Cash;
        data.corpRole = Corp::Role::Admin;
        data.rolesAtAll = Corp::Role::Admin;
        data.rolesAtBase = Corp::Role::Admin;
        data.rolesAtHQ = Corp::Role::Admin;
        data.rolesAtOther = Corp::Role::Admin;
        data.grantableRoles = Corp::Role::Admin;
        data.grantableRolesAtBase = Corp::Role::Admin;
        data.grantableRolesAtHQ = Corp::Role::Admin;
        data.grantableRolesAtOther = Corp::Role::Admin;
    // update corp data and refresh session data.
    pClient->GetChar()->JoinCorporation(data);
    // member limit and allowed races from the founder's skills
    sCorpMgr.UpdateAbilities(corpID);

    // Here we send a notification about creating a new corporation...
    OnCorporationChanged cc;
    cc.corpID = corpID;
    if (!m_db.CreateCorporationCreatePacket(cc, m_corpID, corpID)) {
        codelog(SERVICE__ERROR, "Failed to create OnCorpChanged notification stream.");
        // This is a big problem, because this way we won't be able to see the difference...
        pClient->SendErrorMsg("Unable to notify about corp creation.");
        return nullptr;
    }
    PyTuple* a1 = cc.Encode();
    // send single to client
    pClient->SendNotification("OnCorporationChanged", "clientID", &a1);
    // send multi to station guests
    PyIncRef(a1);
    sEntityList.Multicast("OnCorporationChanged", "stationid", &a1, NOTIF_DEST__LOCATION, pClient->GetLocationID());

    return m_db.GetCorporations(corpID);
}

PyResult CorpRegistryBound::UpdateTitle(PyCallArgs &call,
    PyRep* titleID, PyRep* titleName,
    PyRep* roles, PyRep* grantableRoles,
    PyRep* rolesAtHQ, PyRep* grantableRolesAtHQ, 
    PyRep* rolesAtBase, PyRep* grantableRolesAtBase, 
    PyRep* rolesAtOther, PyRep* grantableRolesAtOther) {
    // self.GetCorpRegistry().UpdateTitle(titleID, titleName, roles, grantableRoles, rolesAtHQ, grantableRolesAtHQ, rolesAtBase, grantableRolesAtBase, rolesAtOther, grantableRolesAtOther)
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateTitle()");
    //call.Dump(CORP__CALL_DUMP);

    // TODO: stop using this and make use of the parameters, changing this is way too out of the scope of the service manager changes
    Call_UpdateTitleData args;
    if (!args.Decode(&call.tuple)) {
        codelog(SERVICE__ERROR, "CorpRegistryBound: Failed to decode arguments.");
        return nullptr;
    }

    RequireDirector(call);

    PyDict* updates = new PyDict();
    if (m_db.UpdateTitle(m_corpID, args, updates)) {
        OnTitleChanged change;
            change.corpID = m_corpID;
            change.titleID = args.titleID;
            change.changes = updates;
        sEntityList.CorpNotify(m_corpID, Notify::Types::CorpNews, "OnTitleChanged", "corpid", change.Encode());

        // title holders' session roles change with the title
        sCorpMgr.InvalidateTitles(m_corpID);
        sCorpMgr.RefreshTitleHolders(m_corpID, args.titleID);
    } else {
        PyDecRef(updates);
    }

    return nullptr;
}

PyResult CorpRegistryBound::UpdateTitles(PyCallArgs &call, PyObject* titles) {
    //    self.GetCorpRegistry().UpdateTitles(titles)
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateTitles()");
    //call.Dump(CORP__CALL_DUMP);
    RequireDirector(call);
    if (!call.tuple->GetItem(0)->IsObject()) {
        codelog(CORP__ERROR, "Tuple Item is wrong type: %s.  Expected PyObject.", call.tuple->GetItem(0)->TypeString());
        return nullptr;
    }

    if (!titles->arguments()->IsDict()) {
        codelog(CORP__ERROR, "Object Argument is wrong type: %s.  Expected PyDict.", titles->arguments()->TypeString());
        return nullptr;
    }

    PyDict* dict = titles->arguments()->AsDict();
    //dict->Dump(CORP__TRACE, "    ");
    PyRep* rep = dict->GetItemString("lines");
    if (!rep->IsList()) {
        codelog(CORP__ERROR, "'lines' item is not PyList: %s", rep->TypeString());
        return nullptr;
    }

    Call_UpdateTitleData args;
    PyList* list = rep->AsList();
    for (PyList::const_iterator itr = list->begin(); itr != list->end(); ++itr) {
        if (!(*itr)->IsList()) {
            codelog(CORP__ERROR, "itr item is not PyList: %s", (*itr)->TypeString());
            continue;
        }
        PyDict* updates = new PyDict();
        PyList* list2 = (*itr)->AsList();
        args.titleID = list2->GetItem(0)->AsInt()->value();
        args.titleName = PyRep::StringContent(list2->GetItem(1));
        args.roles = PyRep::IntegerValue(list2->GetItem(2));
        args.grantableRoles = PyRep::IntegerValue(list2->GetItem(3));
        args.rolesAtHQ = PyRep::IntegerValue(list2->GetItem(4));
        args.grantableRolesAtHQ = PyRep::IntegerValue(list2->GetItem(5));
        args.rolesAtBase = PyRep::IntegerValue(list2->GetItem(6));
        args.grantableRolesAtBase = PyRep::IntegerValue(list2->GetItem(7));
        args.rolesAtOther = PyRep::IntegerValue(list2->GetItem(8));
        args.grantableRolesAtOther = PyRep::IntegerValue(list2->GetItem(9));
        if (m_db.UpdateTitle(m_corpID, args, updates)) {
            OnTitleChanged change;
                change.corpID = m_corpID;
                change.titleID = args.titleID;
                change.changes = updates;
            sEntityList.CorpNotify(m_corpID, Notify::Types::CorpNews, "OnTitleChanged", "corpid", change.Encode());

            sCorpMgr.InvalidateTitles(m_corpID);
            sCorpMgr.RefreshTitleHolders(m_corpID, args.titleID);
        } else {
            PyDecRef(updates);
        }
    }

    return nullptr;
}

PyResult CorpRegistryBound::UpdateCorporation(PyCallArgs &call, PyRep* description, PyRep* url, PyRep* tax, std::optional<PyRep*> acceptApplications) {
    // bco_corporations.UpdateCorporation(description, url, taxRate, acceptApplications)
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateCorporation() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    RequireDirector(call);

    double taxRate(0);
    if (tax->IsFloat()) {
        taxRate = PyRep::FloatValue(tax);
    } else if (tax->IsInt() or tax->IsLong()) {
        taxRate = (double)PyRep::IntegerValue(tax);
    }
    taxRate = EvE::max(0.0, EvE::min(1.0, taxRate));

    int8 recruiting(-1);
    if (acceptApplications.has_value()) {
        std::optional<int64> value = Corp::MemberUpdate::Int(acceptApplications.value());
        if (value.has_value())
            recruiting = (value.value() ? 1 : 0);
    }

    PyDict* change = new PyDict();
    if (!m_db.UpdateCorporation(m_corpID, PyRep::StringContent(description), PyRep::StringContent(url), taxRate, recruiting, change)) {
        codelog(SERVICE__ERROR, "%s: Failed to update corporation data for corp %u", call.client->GetName(), m_corpID);
        PyDecRef(change);
        return nullptr;
    }

    // tax rate change:  notification, and the members' cached tax rate (bounty and reward taxes)
    PyRep* taxChange = change->GetItemString("taxRate");
    if ((taxChange != nullptr) and taxChange->IsTuple()) {
        double oldRate = PyRep::FloatValue(taxChange->AsTuple()->GetItem(0));
        PyDict* data = new PyDict();
            data->SetItemString("corpID", new PyInt(m_corpID));
            data->SetItemString("oldTaxRate", new PyFloat(oldRate * 100));
            data->SetItemString("newTaxRate", new PyFloat(taxRate * 100));
        sCorpMgr.NotifyCorp(m_corpID, Notify::Types::CorpTaxChange, call.client->GetCharacterID(), data);

        std::vector<Client*> list;
        sEntityList.GetCorpClients(list, m_corpID);
        for (auto cur : list)
            sCorpMgr.RefreshMemberSession(cur->GetCharacterID(), true);
    }

    sCorpMgr.BroadcastCorpChange(m_corpID, change);
    return nullptr;
}

PyResult CorpRegistryBound::UpdateLogo(PyCallArgs &call,
    PyRep* shape1, PyRep* shape2, PyRep* shape3,
    PyRep* color1, PyRep* color2, PyRep* color3,
    PyRep* typeface)
{
    // TODO: stop using this and make use of the parameters, changing this is way too out of the scope of the service manager changes
    Call_UpdateLogo args;
    if (!args.Decode(&call.tuple)) {
        codelog(SERVICE__ERROR, "CorpRegistryBound: Failed to decode arguments.");
        return nullptr;
    }

    RequireDirector(call);

    Notify_IntRaw notif;
    notif.key = m_corpID;

    // Check if we have enough money
    double logo_change = 100;
    if (AccountDB::GetCorpBalance(notif.key, Account::KeyType::Cash) < logo_change) {
        _log( SERVICE__ERROR, "%s: Cannot afford corporation logo change costs", call.client->GetName());
        call.client->SendErrorMsg("Your corporation doesn't have enough money (%u ISK) to change it's logo.", logo_change);
        return nullptr;
    }

    notif.data = new PyDict();
    if (!m_db.UpdateLogo(notif.key, args, notif.data))    {
        codelog( SERVICE__ERROR, "Corporation logo change failed..." );
        PyDecRef( notif.data );
        return nullptr;
    }

    std::string reason = "Change Corporation Logo.";

    // move cash and record the transaction.
    AccountService::TransferFunds(
        call.client->GetCharacterID(),
        m_db.GetStationOwner(call.client->GetStationID()),
        logo_change,
        reason,
        Journal::EntryType::CorporationLogoChangeCost,
        Account::KeyType::Cash
    ); // main wallet

    sCorpMgr.BroadcastCorpChange(m_corpID, notif.data);
    notif.data = nullptr;

    return m_db.GetCorporation(notif.key);
}

PyResult CorpRegistryBound::AddBulletin(PyCallArgs &call, PyRep* title, PyRep* body) {
    // self.GetCorpRegistry().AddBulletin(title, body)
    // self.GetCorpRegistry().AddBulletin(title, body, bulletinID=bulletinID, editDateTime=editDateTime)    <-- this is to update bulletin
    _log(CORP__CALL, "CorpRegistryBound::Handle_AddBulletin()");
    call.Dump(CORP__CALL_DUMP);

    std::string titleStr = PyRep::StringContent(title);
    std::string bodyStr = PyRep::StringContent(body);

    bool edit = false;
    int64 editDateTime = 0;
    if (call.byname.find("editDateTime") != call.byname.end()) {
        if (call.byname.find("editDateTime")->second->IsLong()) {
            edit = true;
            editDateTime = PyInt::IntegerValue(call.byname.find("editDateTime")->second);
        } else {
            _log(CORP__ERROR, "Handle_AddBulletin - editDateTime is of the wrong type: '%s'.  Expected PyString or PyWString.", \
                            call.byname.find("editDateTime")->second->TypeString());
        }
    }

    int64 bulletinID = 0;
    if (call.byname.find("bulletinID") != call.byname.end()) {
        if (call.byname.find("bulletinID")->second->IsInt()) {
            edit = true;
            bulletinID = PyInt::IntegerValue(call.byname.find("bulletinID")->second);
        } else {
            _log(CORP__ERROR, "Handle_AddBulletin - bulletinID is of the wrong type: '%s'.  Expected PyInt.", \
                            call.byname.find("bulletinID")->second->TypeString());
        }
    }

    if (edit) {
        if (bulletinID >= 100000) {
            AllianceDB::EditBulletin(bulletinID, call.client->GetCharacterID(), editDateTime, titleStr, bodyStr);
        } else {
            m_db.EditBulletin(bulletinID, call.client->GetCharacterID(), editDateTime, titleStr, bodyStr);
        }

    } else {
        m_db.AddBulletin(m_corpID, m_corpID, call.client->GetCharacterID(), titleStr, bodyStr);
    }

    return nullptr;
}

PyResult CorpRegistryBound::DeleteBulletin(PyCallArgs &call, PyInt* bulletinID) {
    //self.GetCorpRegistry().DeleteBulletin(id)
    _log(CORP__CALL, "CorpRegistryBound::Handle_DeleteBulletin() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    if (bulletinID->value() >= 100000) {
        AllianceDB::DeleteBulletin(bulletinID->value());
    } else {
        m_db.DeleteBulletin(bulletinID->value());
    }
    return nullptr;
}

PyResult CorpRegistryBound::CreateRecruitmentAd(PyCallArgs &call, PyInt* days, PyInt* typeMask, std::optional <PyInt*> allianceID, PyWString* description, PyInt* channelID, PyList* recruiterIDs, PyWString* title) {
    // return self.GetCorpRegistry().CreateRecruitmentAd(days, typeMask, allianceID, description, channelID, recruiters, title)
    _log(CORP__CALL, "CorpRegistryBound::Handle_CreateRecruitmentAd()");
    call.Dump(CORP__CALL_DUMP);
    /*
     * 00:41:50 [SvcCall] Service CorpRegistryBound::CreateRecruitmentAd()
     * 00:41:50 W CorpRegistryBound::Handle_CreateRecruitmentAd(): size= 7
     * 00:41:50 [CorpCallDump]   Call Arguments:
     * 00:41:50 [CorpCallDump]      Tuple: 7 elements
     * 00:41:50 [CorpCallDump]       [ 0]    Integer: 14
     * 00:41:50 [CorpCallDump]       [ 1]    Integer: 34672663
     * 00:41:50 [CorpCallDump]       [ 2]       None
     * 00:41:50 [CorpCallDump]       [ 3]    WString: 'test'
     * 00:41:50 [CorpCallDump]       [ 4]    Integer: 0
     * 00:41:50 [CorpCallDump]       [ 5]   List: 1 elements
     * 00:41:50 [CorpCallDump]       [ 5]   [ 0]    Integer: 90000000
     * 00:41:50 [CorpCallDump]       [ 6]    WString: 'test'
     */

    uint32 amount = 1250000;
    switch (days->value()) {
        case 7:    amount = 2250000;  break;
        case 14:   amount = 4000000;  break;
        case 28:   amount = 7500000;  break;
    }

    AccountService::TransferFunds(m_corpID, call.client->GetCorpHQ(), amount, "Initial Advert Time for Corp Recruit Advert", Journal::EntryType::CorporationAdvertisementFee, call.client->GetCharacterID());

    int32 adID = m_db.CreateAdvert(call.client, m_corpID, typeMask->value(), days->value(), m_db.GetCorpMemberCount(m_corpID), description->content(), channelID->value(), title->content());

    std::vector<int32> recruiters;
    recruiters.clear();
    for (PyList::const_iterator itr = recruiterIDs->begin(); itr != recruiterIDs->end(); ++itr)
        recruiters.push_back(PyRep::IntegerValue(*itr));
    //recruiters.push_back((*itr)->AsInt()->value());

    // if no recruiters defined, default to creating character
    if (recruiters.empty())
        recruiters.push_back(call.client->GetCharacterID());

    m_db.AddRecruiters(adID, (int32)m_corpID, recruiters);

    return nullptr;
}

PyResult CorpRegistryBound::UpdateRecruitmentAd(PyCallArgs &call, PyInt* adID, PyInt* typeMask, PyWString* description, PyInt* channelID, PyList* recruiterIDs, PyWString* title, PyInt* addedDays) {
    // return self.GetCorpRegistry().UpdateRecruitmentAd(adID, typeMask, description, channelID, recruiters, title, addedDays)
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateRecruitmentAd() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    /*
     * 04:47:40 [CorpCall] CorpRegistryBound::Handle_UpdateRecruitmentAd() size=7
     * 04:47:40 [CorpCallDump]   Call Arguments:
     * 04:47:40 [CorpCallDump]      Tuple: 7 elements
     * 04:47:40 [CorpCallDump]       [ 0]    Integer: 3
     * 04:47:40 [CorpCallDump]       [ 1]    Integer: 34869363
     * 04:47:40 [CorpCallDump]       [ 2]    WString: 'test advert msg'
     * 04:47:40 [CorpCallDump]       [ 3]    Integer: 0
     * 04:47:40 [CorpCallDump]       [ 4]   List: 3 elements
     * 04:47:40 [CorpCallDump]       [ 4]   [ 0]    Integer: 90000000
     * 04:47:40 [CorpCallDump]       [ 4]   [ 1]    Integer: 91000001
     * 04:47:40 [CorpCallDump]       [ 4]   [ 2]    Integer: 91000003
     * 04:47:40 [CorpCallDump]       [ 5]    WString: 'mining corp'
     * 04:47:40 [CorpCallDump]       [ 6]    Integer: 0
     */

    uint8 days = 1;
    // check that corp can afford added time, if requested
    if (addedDays->value()) {
        // 3d = .75misk, 7d = 1.75misk, 14d = 3.5misk
        uint32 amount = 750000;
        switch (addedDays->value()) {
            case 7:    amount = 1750000;  break;
            case 14:   amount = 3500000;  break;
        }

        AccountService::TransferFunds(m_corpID, call.client->GetCorpHQ(), amount, "Added Advert Time to Corp Recruit Advert", Journal::EntryType::CorporationAdvertisementFee);

        // do some funky shit to determine days left for advert then add additional time if requested
        int64 time = m_db.GetAdvertTime(adID->value(), m_corpID);
        if (time > 0) {
            time -= GetFileTimeNow();
            days = time / EvE::Time::Day;
            days += addedDays->value();
        }
    }

    m_db.UpdateAdvert(adID->value(), m_corpID, typeMask->value(), days, m_db.GetCorpMemberCount(m_corpID), description->content(), channelID->value(), title->content());

    std::vector<int32> recruiters;
    recruiters.clear();
    for (PyList::const_iterator itr = recruiterIDs->begin(); itr != recruiterIDs->end(); ++itr)
        recruiters.push_back(PyRep::IntegerValue(*itr));

    // if no recruiters defined, default to creating character
    if (recruiters.empty())
        recruiters.push_back(call.client->GetCharacterID());

    m_db.AddRecruiters(adID->value(), (int32)m_corpID, recruiters);

    return m_db.GetAdvert(adID->value());
}

PyResult CorpRegistryBound::MoveCompanyShares(PyCallArgs &call, PyInt* corporationID, PyInt* toShareholderID, PyInt* numberOfShares) {
    // return self.GetCorpRegistry().MoveCompanyShares(corporationID, toShareholderID, numberOfShares)
    //  the corporation's own (treasury) shares, by its CEO or directors
    _log(CORP__CALL, "CorpRegistryBound::Handle_MoveCompanyShares()");
    call.Dump(CORP__CALL_DUMP);

    RequireDirector(call);
    if ((uint32)corporationID->value() != m_corpID)
        throw CustomError("You can only give away your own corporation's shares.");
    if (IsCorp(toShareholderID->value()))
        throw CustomError("You cannot give shares to a corporation.");
    if (numberOfShares->value() <= 0)
        return nullptr;

    sCorpMgr.TransferShares(m_corpID, m_corpID, toShareholderID->value(), numberOfShares->value());
    return nullptr;
}

PyResult CorpRegistryBound::MovePrivateShares(PyCallArgs &call, PyInt* corporationID, PyInt* toShareholderID, PyInt* numberOfShares) {
    // return self.GetCorpRegistry().MovePrivateShares(corporationID, toShareholderID, numberOfShares)
    //  the caller's own shares of any corporation
    _log(CORP__CALL, "CorpRegistryBound::Handle_MovePrivateShares()");
    call.Dump(CORP__CALL_DUMP);

    if (numberOfShares->value() <= 0)
        return nullptr;

    sCorpMgr.TransferShares(corporationID->value(), call.client->GetCharacterID(), toShareholderID->value(), numberOfShares->value());
    return nullptr;
}

void CorpRegistryBound::GiveShares(PyCallArgs& call, uint32 toID, int64 count)
{
    // task management CTV_GIVE CTPG_SHARES:  treasury shares to a member
    RequireDirector(call);
    if (count <= 0)
        return;
    sCorpMgr.TransferShares(m_corpID, m_corpID, toID, (uint32)count);
}

PyResult CorpRegistryBound::GetMemberIDsByQuery(PyCallArgs &call, PyList* queryList, std::optional <PyInt*> includeImplied, PyInt* searchTitles) {
    /*this is performed thru corp window using a multitude of options, to get specific members based on quite variable criteria
     * not as complicated as i had originally thought.
     */

    //return self.GetCorpRegistry().GetMemberIDsByQuery(query, includeImplied, searchTitles)
    call.Dump(CORP__CALL_DUMP);

    if (queryList->empty()) {
        call.client->SendErrorMsg("You must choose a role to search for.");
        return nullptr;
    }

    /*
     *                query.append([property, operator, value])
     *                query.append([joinOperator, property, operator, value])
     *
     * 04:28:06 [CorpCallDump]   Call Arguments:
     * 04:28:06 [CorpCallDump]      Tuple: 3 elements
     * 04:28:06 [CorpCallDump]       [ 0]   List: 3 elements
     * 04:28:06 [CorpCallDump]       [ 0]   [ 0]   List: 3 elements
     * 04:28:06 [CorpCallDump]       [ 0]   [ 0]   [ 0]     String: 'roles'
     * 04:28:06 [CorpCallDump]       [ 0]   [ 0]   [ 1]    Integer: 7
     * 04:28:06 [CorpCallDump]       [ 0]   [ 0]   [ 2]    Integer: 134217728
     * 04:28:06 [CorpCallDump]       [ 0]   [ 1]   List: 4 elements
     * 04:28:06 [CorpCallDump]       [ 0]   [ 1]   [ 0]    Integer: 2           <-- joinOp  (AND/OR)
     * 04:28:06 [CorpCallDump]       [ 0]   [ 1]   [ 1]     String: 'roles'     <-- queryType   (Corp::QueryType {roles/charID/baseID/joinDate})
     * 04:28:06 [CorpCallDump]       [ 0]   [ 1]   [ 2]    Integer: 8           <-- searchOp (Corp::SearchOp)
     * 04:28:06 [CorpCallDump]       [ 0]   [ 1]   [ 3]    Integer: 4096        <-- value   (depends on searchOp, int, int64)
     * 04:28:06 [CorpCallDump]       [ 0]   [ 2]   List: 4 elements
     * 04:28:06 [CorpCallDump]       [ 0]   [ 2]   [ 0]    Integer: 2
     * 04:28:06 [CorpCallDump]       [ 0]   [ 2]   [ 1]     String: 'roles'
     * 04:28:06 [CorpCallDump]       [ 0]   [ 2]   [ 2]    Integer: 8
     * 04:28:06 [CorpCallDump]       [ 0]   [ 2]   [ 3]    Integer: 1024
     * 04:28:06 [CorpCallDump]       [ 1]    Integer: 1
     * 04:28:06 [CorpCallDump]       [ 2]    Integer: 1
     *
     */
    /*
     *    int8 joinOp = Corp::JoinOp::None;
     *    uint8 queryType = Corp::QueryType::Roles;
     *    uint8 searchOp = Corp::SearchOp::EQUAL;
     *    int64 value = 0;
     *    std::string searchString = "";
     */
    // query holder
    std::ostringstream query;
    query << "SELECT characterID FROM chrCharacters WHERE corporationID = ";
    query << m_corpID << " AND ";

    bool set = false;
    // decode query format
    PyList* list(nullptr);
    for (PyList::const_iterator itr = queryList->begin(); itr != queryList->end(); ++itr) {
        list = (*itr)->AsList();
        if (list == nullptr)
            continue;
        if (list->size() == 3) {
            Call_GetMemberIDsByQuery_List3 args3;
            if (!args3.Decode(&list)) {
                codelog(SERVICE__ERROR, "CorpRegistryBound: Failed to decode arguments.");
                return nullptr;
            }

            if (args3.queryType.compare("roles") == 0) {
                query << "corpRole";
            } else {
                query << args3.queryType;
            }

            if (GetSearchValues(args3.searchOp, args3.valueRaw, query)) {
                set = true;
            } else {
                call.client->SendErrorMsg("Search data invalid. Ref: ServerError xxxxx");
                return nullptr;
            }
        } else if (list->size() == 4) {
            // db will not do bitwise for multiple operators.
            //   will have to hit db for first call then query result with remaining conditionals to get result
            if (!set)
                return nullptr;
            Call_GetMemberIDsByQuery_List4 args4;
            if (!args4.Decode(&list)) {
                codelog(SERVICE__ERROR, "CorpRegistryBound: Failed to decode arguments.");
                return nullptr;
            }

            switch (args4.joinOp) {
                case Corp::JoinOp::AND: {
                    query << " AND ";
                } break;
                case Corp::JoinOp::OR: {
                    query << " OR ";
                } break;
                case Corp::JoinOp::None:
                default: {
                    // error
                    _log(CORP__ERROR, "CorpRegistryBound::Handle_GetMemberIDsByQuery() sent invalid JoinOp %i", args4.joinOp);
                    return nullptr;
                } break;
            }

            if (args4.queryType.compare("roles") == 0) {
                query << "corpRole";
            } else {
                query << args4.queryType;
            }

            if (GetSearchValues(args4.searchOp, args4.valueRaw, query)) {
                set = true;
            } else {
                call.client->SendErrorMsg("Search data invalid. Ref: ServerError xxxxx");
                return nullptr;
            }
        } else {
            _log(CORP__ERROR, "CorpRegistryBound::Handle_GetMemberIDsByQuery() - Invalid data size: %u.  Expected 3 or 4.", list->size());
            return nullptr;
        }
    }

    // get corp memberlist based on query
    std::vector<uint32> result;
    // make sure we have a valid query before sending to db method
    if (set)
        m_db.GetMembersForQuery(query, result);

    // create/clear list as needed
    if (list == nullptr) {
        list = new PyList();
    } else {
        list->clear();
    }

    // populate results
    for (auto cur : result)
        list->AddItem(new PyInt(cur));

    if (is_log_enabled(CORP__RSP_DUMP))
        list->Dump(CORP__RSP_DUMP, "");

    // return results
    return list;
}

//SELECT `characterID`\
`corpRole`, `rolesAtAll`, `rolesAtHQ`, `rolesAtBase`, `rolesAtOther`, \
`grantableRoles`, `grantableRolesAtHQ`, `grantableRolesAtBase`, `grantableRolesAtOther`,\
`titleMask`, `blockRoles`, `baseID`, `startDateTime` FROM `chrCharacters` WHERE 1
bool CorpRegistryBound::GetSearchValues(int8 op, PyRep* rep, std::ostringstream& query)
{
    using namespace Corp;
    switch (op) {
        case SearchOp::EQUAL: {
            query << " = ";
            query << PyRep::IntegerValue(rep);
        } break;
        case SearchOp::GREATER: {
            query << " > ";
            query << PyRep::IntegerValue(rep);
        } break;
        case SearchOp::GREATER_OR_EQUAL: {
            query << " >= ";
            query << PyRep::IntegerValue(rep);
        } break;
        case SearchOp::LESS: {
            query << " < ";
            query << PyRep::IntegerValue(rep);
        } break;
        case SearchOp::LESS_OR_EQUAL: {
            query << " <= ";
            query << PyRep::IntegerValue(rep);
        } break;
        case SearchOp::NOT_EQUAL: {
            query << " != ";
            query << PyRep::IntegerValue(rep);
        } break;
        case SearchOp::HAS_BIT: {
            query << " &";
            query << PyRep::IntegerValue(rep);
            query << " = ";
            query << PyRep::IntegerValue(rep);
        } break;
        case SearchOp::NOT_HAS_BIT: {
            query << " ~";
            query << PyRep::IntegerValue(rep);
            query << " = ";
            query << PyRep::IntegerValue(rep);
        } break;
        case SearchOp::STR_CONTAINS:
        case SearchOp::STR_LIKE: {
            query << "%";
            query << PyRep::StringContent(rep);
            query << "% ";
        } break;
        case SearchOp::STR_STARTS_WITH: {
            query << PyRep::StringContent(rep);
            query << "%";
        } break;
        case SearchOp::STR_ENDS_WITH: {
            query << "%";
            query << PyRep::StringContent(rep);
        } break;
        case SearchOp::STR_IS: {
            query << " = ";
            query << PyRep::StringContent(rep);
        } break;
        default: {
            _log(CORP__ERROR, "CorpRegistryBound::GetSearchValues() sent invalid searchOp %i", op);
            return false;
        };
    }
    return true;
}

// no longer used with new query decoding
uint8 CorpRegistryBound::GetQueryType(std::string queryType)
{
    if (queryType.compare("roles") == 0) {
        return Corp::QueryType::Roles;
    } else if (queryType.compare("rolesAtHQ") == 0) {
        return Corp::QueryType::Roles;
    } else if (queryType.compare("rolesAtBase") == 0) {
        return Corp::QueryType::Roles;
    } else if (queryType.compare("rolesAtOther") == 0) {
        return Corp::QueryType::Roles;
    } else if (queryType.compare("grantableRoles") == 0) {
        return Corp::QueryType::Roles;
    } else if (queryType.compare("grantableRolesAtHQ") == 0) {
        return Corp::QueryType::Roles;
    } else if (queryType.compare("grantableRolesAtBase") == 0) {
        return Corp::QueryType::Roles;
    } else if (queryType.compare("grantableRolesAtOther") == 0) {
        return Corp::QueryType::Roles;
    } else if (queryType.compare("baseID") == 0) {
        return Corp::QueryType::BaseID;
    } else if (queryType.compare("startDateTime") == 0) {
        return Corp::QueryType::StartDateTime;
    } else if (queryType.compare("characterID") == 0) {     //this is actually string.  check searchOp for details
        return Corp::QueryType::CharID;
    } else if (queryType.compare("titleMask") == 0) {
        return Corp::QueryType::TitleMask;
    }

    _log(CORP__ERROR, "CorpRegistryBound::GetQueryType() - Invalid QueryType: %s", queryType.c_str());
    return 0;
}


/**     ***********************************************************************
 * @note   these below are partially coded
 */

PyResult CorpRegistryBound::PayoutDividend(PyCallArgs &call, PyRep* payShareholders, PyRep* payoutAmount) {
    //self.GetCorpRegistry().PayoutDividend(payShareholders, payoutAmount)    payShareholders is an int (AskPayShareholders yes/no)
    _log(CORP__CALL, "CorpRegistryBound::Handle_PayoutDividend()");
    call.Dump(CORP__CALL_DUMP);

    RequireMember(call);

    double amount(0);
    if (payoutAmount->IsFloat()) {
        amount = PyRep::FloatValue(payoutAmount);
    } else if (payoutAmount->IsInt() or payoutAmount->IsLong()) {
        amount = (double)PyRep::IntegerValue(payoutAmount);
    }
    bool shareholders(false);
    std::optional<int64> pay = Corp::MemberUpdate::Int(payShareholders);
    if (pay.has_value())
        shareholders = (pay.value() != 0);

    sCorpMgr.PayDividend(call.client, m_corpID, shareholders, amount);
    return nullptr;
}

PyResult CorpRegistryBound::UpdateMember(PyCallArgs &call,
    PyInt* characterID, PyRep* title, PyRep* divisionID, PyRep* squadronID,
    PyRep* roles, PyRep* grantableRoles, PyRep* rolesAtHQ, PyRep* grantableRolesAtHQ,
    PyRep* rolesAtBase, PyRep* grantableRolesAtBase, PyRep* rolesAtOther, PyRep* grantableRolesAtOther,
    PyRep* baseID, std::optional<PyRep*> titleMask, std::optional<PyRep*> blockRoles) {
    //return self.GetCorpRegistry().UpdateMember(charIDToUpdate, title, divisionID, squadronID, roles, grantableRoles, rolesAtHQ, grantableRolesAtHQ, rolesAtBase, grantableRolesAtBase, rolesAtOther, grantableRolesAtOther, baseID, titleMask, blockRoles)
    //  any argument may be None (unchanged).  roles arrive as Int or Long.
    //  "remove all roles" (prepare to quit):  (charID, None, None, None, 0,0,0,0,0,0,0,0, None, 0, 1);  "allow new roles":  blockRoles 0 only
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateMember()");
    call.Dump(CORP__CALL_DUMP);

    RequireMember(call);

    Corp::MemberUpdate upd = Corp::MemberUpdate();
        upd.charID = characterID->value();
        if (!title->IsNone())
            upd.title = PyRep::StringContent(title);
        upd.roles = Corp::MemberUpdate::Int(roles);
        upd.grantableRoles = Corp::MemberUpdate::Int(grantableRoles);
        upd.rolesAtHQ = Corp::MemberUpdate::Int(rolesAtHQ);
        upd.grantableRolesAtHQ = Corp::MemberUpdate::Int(grantableRolesAtHQ);
        upd.rolesAtBase = Corp::MemberUpdate::Int(rolesAtBase);
        upd.grantableRolesAtBase = Corp::MemberUpdate::Int(grantableRolesAtBase);
        upd.rolesAtOther = Corp::MemberUpdate::Int(rolesAtOther);
        upd.grantableRolesAtOther = Corp::MemberUpdate::Int(grantableRolesAtOther);
        upd.baseID = Corp::MemberUpdate::Int(baseID);
        if (titleMask.has_value())
            upd.titleMask = Corp::MemberUpdate::Int(titleMask.value());
        if (blockRoles.has_value()) {
            std::optional<int64> block = Corp::MemberUpdate::Int(blockRoles.value());
            if (block.has_value())
                upd.blockRoles = (block.value() != 0);
        }

    sCorpMgr.UpdateMember(call.client, m_corpID, upd, true);
    return nullptr;
}

PyResult CorpRegistryBound::UpdateMembers(PyCallArgs &call, PyObject* rows) {
    // corp_ui_members / role management:  self.GetCorpRegistry().UpdateMembers(rows)
    //  util.Rowset header characterID, title, divisionID, squadronID, roles, grantableRoles, rolesAtHQ, grantableRolesAtHQ,
    //  rolesAtBase, grantableRolesAtBase, rolesAtOther, grantableRolesAtOther, baseID, titleMask.  None = unchanged
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateMembers()");
    call.Dump(CORP__CALL_DUMP);

    RequireMember(call);

    if (!rows->arguments()->IsDict()) {
        codelog(CORP__ERROR, "UpdateMembers rows argument is %s.  Expected PyDict.", rows->arguments()->TypeString());
        return nullptr;
    }

    PyDict* dict = rows->arguments()->AsDict();
    PyRep* header = dict->GetItemString("header");
    PyRep* lines = dict->GetItemString("lines");
    if ((header == nullptr) or (lines == nullptr) or !header->IsList() or !lines->IsList()) {
        codelog(CORP__ERROR, "UpdateMembers rowset has no header or lines");
        return nullptr;
    }

    std::map<std::string, size_t> columns;
    for (size_t i = 0; i < header->AsList()->size(); ++i)
        columns[PyRep::StringContent(header->AsList()->GetItem(i))] = i;

    for (PyList::const_iterator itr = lines->AsList()->begin(); itr != lines->AsList()->end(); ++itr) {
        if (!(*itr)->IsList())
            continue;
        PyList* line = (*itr)->AsList();
        auto get = [&](const char* name) -> PyRep* {
            std::map<std::string, size_t>::iterator col = columns.find(name);
            if ((col == columns.end()) or (col->second >= line->size()))
                return nullptr;
            return line->GetItem(col->second);
        };

        PyRep* charID = get("characterID");
        if ((charID == nullptr) or !(charID->IsInt() or charID->IsLong()))
            continue;

        Corp::MemberUpdate upd = Corp::MemberUpdate();
            upd.charID = PyRep::IntegerValueU32(charID);
            PyRep* title = get("title");
            if ((title != nullptr) and !title->IsNone())
                upd.title = PyRep::StringContent(title);
            upd.roles = Corp::MemberUpdate::Int(get("roles"));
            upd.grantableRoles = Corp::MemberUpdate::Int(get("grantableRoles"));
            upd.rolesAtHQ = Corp::MemberUpdate::Int(get("rolesAtHQ"));
            upd.grantableRolesAtHQ = Corp::MemberUpdate::Int(get("grantableRolesAtHQ"));
            upd.rolesAtBase = Corp::MemberUpdate::Int(get("rolesAtBase"));
            upd.grantableRolesAtBase = Corp::MemberUpdate::Int(get("grantableRolesAtBase"));
            upd.rolesAtOther = Corp::MemberUpdate::Int(get("rolesAtOther"));
            upd.grantableRolesAtOther = Corp::MemberUpdate::Int(get("grantableRolesAtOther"));
            upd.baseID = Corp::MemberUpdate::Int(get("baseID"));
            upd.titleMask = Corp::MemberUpdate::Int(get("titleMask"));

        // rows the caller may not change are skipped (logged)
        sCorpMgr.UpdateMember(call.client, m_corpID, upd, false);
    }

    return nullptr;
}

PyResult CorpRegistryBound::InsertApplication(PyCallArgs &call, PyInt* corporationID, PyRep* message)
{
    // should "corp jumpers" be time-limited?   config option?   parameters?
    //  if so, put check here and return msg to player on hit

    // can we send app to non-player corp?
    if (!IsPlayerCorp(corporationID->value()))
        return nullptr;
    // check member limit
    if (m_db.GetCorpMemberCount(corporationID->value()) + 1 > m_db.GetCorpMemberLimit(corporationID->value())) {
        call.client->SendNotifyMsg("%s cannot accept any new members at this time.", m_db.GetCorpName(corporationID->value()).c_str());
        return nullptr;
    }

    // check member race
    //  ceo's ethnic relation skill allows +20%/lvl members of other races, based on member limit
    //  not sure how im gonna do this yet....db column in corp table of off-race count maybe?

    uint32 charID = call.client->GetCharacterID();
    /// Insert query into the db
    Corp::ApplicationInfo aInfo = Corp::ApplicationInfo();
        aInfo.valid = true;
        aInfo.charID = charID;
        aInfo.corpID = corporationID->value();
        aInfo.appText = PyRep::StringContent (message);
        aInfo.role = Corp::Role::Member;
        aInfo.grantRole = Corp::Role::Member;  // this is "None"
        aInfo.status = Corp::AppStatus::AppliedByCharacter;
        aInfo.appTime = GetFileTimeNow();
        aInfo.deleted = false;
        aInfo.lastCID = 0;

    if (!m_db.InsertApplication(aInfo)) {
        codelog(SERVICE__ERROR, "%s: Failed to insert application request", call.client->GetName());
        return nullptr;
    }

    m_db.AddItemEvent(corporationID->value(), charID, Corp::EventType::AppliedForMembershipOfCorporation);

    // this is sent for any change, including new
    Corp::ApplicationInfo oldInfo = Corp::ApplicationInfo();
        oldInfo.valid = false;
    OnCorporationApplicationChanged OCAC;
    FillOCApplicationChange(OCAC, oldInfo, aInfo);
    // would this be for another corp??
    //  could you bind/req this call from a diff corp?
    OCAC.corpID = corporationID->value();
    OCAC.charID = charID;
    sEntityList.CorpNotify(corporationID->value(), Notify::Types::CorpAppNew, "OnCorporationApplicationChanged", "*corpid&corprole", OCAC.Encode());
    // this is also sent to applicant
    call.client->SendNotification("OnCorporationApplicationChanged", "*corpid&corprole", OCAC.Encode(), false);

    /// need to find out what happens on the other side, if there's anything at all on the other side

    /// Send an evemail to those who can decide
    /// Well, for the moment, send it to the ceo
    std::string subject = "New application from ";
    subject += call.client->GetName();
    std::vector<int32> recipients;
    recipients.push_back(m_db.GetCorporationCEO(corporationID->value()));
    this->m_lsc->SendMail(charID, recipients, subject, aInfo.appText);

    // should this be sent from mail system?  maybe not...cannot determine type from mail.
    // for now, this notification will need to be created/sent from same method sending mail.
    PyDict* dict = new PyDict();
        dict->SetItemString("applicationText", new PyString(aInfo.appText));
        dict->SetItemString("corpID", new PyInt(corporationID->value()));
        dict->SetItemString("charID", new PyInt(charID));
    OnNotify onn;
        onn.created = GetFileTimeNow();
        onn.data    = dict;
        onn.notifyID = 0;
        onn.senderID = charID;
        onn.typeID  = Notify::Types::CorpAppNew;
    sEntityList.CorpNotify(corporationID->value(), Notify::Types::CorpAppNew, "OnNotificationReceived", "clientID", onn.Encode());

    /// Reply: ~\x00\x00\x00\x00\x01
    //returns none
    return nullptr;
}

PyResult CorpRegistryBound::UpdateApplicationOffer(PyCallArgs &call, PyInt* characterID, PyRep* applicationText, PyInt* status, PyNone* applicationDateTime) {
    //     return self.GetCorpRegistry().UpdateApplicationOffer(characterID, applicationText, status, applicationDateTime = None) NOTE: time not used.
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateApplicationOffer() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    // can we send app to non-player corp?
    if (!IsPlayerCorp(m_corpID))
        return nullptr;

    // applications are handled by personnel managers (and directors)
    RequireMember(call);
    if (!CorpMgr::HasRole(call.client->GetCorpRole(), Corp::Role::PersonnelManager))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Only personnel managers and directors can handle applications."));

    if (status->value() == Corp::AppStatus::AcceptedByCorporation)
        CheckCanAccept(characterID->value());

    Corp::ApplicationInfo oldInfo = Corp::ApplicationInfo();
        oldInfo.valid = true;
        oldInfo.appText = PyRep::StringContent(applicationText);
        oldInfo.charID = characterID->value();
        oldInfo.corpID = m_corpID;  // is this applicants current corpID or applied-to corpID??
    if (!m_db.GetCurrentApplicationInfo(oldInfo)) {
        codelog(SERVICE__ERROR, "%s: Failed to query application for char %u corp %u", call.client->GetName(), characterID->value(), m_corpID);
        return nullptr;
    }

    Corp::ApplicationInfo newInfo = oldInfo;
        newInfo.valid = true;  // allow saving of this update. we'll null it later.
        // these are populated as we're still saving new status in db.  subject to change as system progresses
        newInfo.status = status->value();
        newInfo.lastCID = call.client->GetCharacterID();
        //newInfo.appTime = GetFileTimeNow();  // not used here.
    if (!m_db.UpdateApplication(newInfo)) {
        codelog(SERVICE__ERROR, "%s: Failed to update application for char %u corp %u", call.client->GetName(), characterID->value(), m_corpID);
        return nullptr;
    }

    // sets 'new' info to PyNone in OnCorporationApplicationChanged packet.  this removes app from char/corp page
    //newInfo.valid = false;

    OnCorporationApplicationChanged ocac;
        ocac.charID = characterID->value();
        ocac.corpID = m_corpID;
    FillOCApplicationChange(ocac, oldInfo, newInfo);

    if (status->value() == Corp::AppStatus::AcceptedByCorporation) {
        sEntityList.CorpNotify(m_corpID, Notify::Types::CorpAppAccept, "OnCorporationApplicationChanged", "*corpid&corprole", ocac.Encode());

        // the applicant joins:  employment history, member lists of both corps, corp events
        sCorpMgr.MoveMemberToCorp(characterID->value(), m_corpID);

        PyDict* data = new PyDict();
            data->SetItemString("corpID", new PyInt(m_corpID));
            data->SetItemString("charID", new PyInt(characterID->value()));
        sCorpMgr.NotifyChar(characterID->value(), Notify::Types::CorpAppAccept, call.client->GetCharacterID(), data);
    } else if (status->value() == Corp::AppStatus::RejectedByCorporation) {
        sEntityList.CorpNotify(m_corpID, Notify::Types::CorpAppReject, "OnCorporationApplicationChanged", "*corpid&corprole", ocac.Encode());

        PyDict* data = new PyDict();
            data->SetItemString("corpID", new PyInt(m_corpID));
            data->SetItemString("charID", new PyInt(characterID->value()));
        sCorpMgr.NotifyChar(characterID->value(), Notify::Types::CorpAppReject, call.client->GetCharacterID(), data);
    } else if (status->value() == Corp::AppStatus::RenegotiatedByCorporation) {
        // i dont see this option in client code...may not be able to reneg app.
        _log(CORP__MESSAGE, "CorpRegistryBound::Handle_UpdateApplicationOffer() hit AppStatus::RenegotiatedByCorporation by %s ", call.client->GetName() );
    } else {
        codelog(SERVICE__ERROR, "%s: Sent unhandled status %u ", call.client->GetName(), status->value());
        // error...what to do here?
    }

    // update applicant, if online
    Client* pClient = sEntityList.FindClientByCharID(characterID->value());
    if (pClient != nullptr)
        pClient->SendNotification("OnCorporationApplicationChanged", "clientID", ocac.Encode(), false);

    return nullptr;
}

PyResult CorpRegistryBound::DeleteApplication(PyCallArgs & call, PyInt* corporationID, PyInt* characterID)
{
    //  self.GetCorpRegistry().DeleteApplication(corporationID, characterID)
    _log(CORP__CALL, "CorpRegistryBound::Handle_DeleteApplication() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    OnCorporationApplicationChanged ocac;
        ocac.corpID = corporationID->value();
        ocac.charID = characterID->value();
    Corp::ApplicationInfo newInfo = Corp::ApplicationInfo();
        newInfo.valid = false;
    Corp::ApplicationInfo oldInfo = Corp::ApplicationInfo();
        oldInfo.valid = true;
        oldInfo.corpID = corporationID->value();
        oldInfo.charID = characterID->value();
    if (!m_db.GetCurrentApplicationInfo(oldInfo)) {
        codelog(SERVICE__ERROR, "%s: Failed to query application info for char %u corp %u", call.client->GetName(), ocac.charID, ocac.corpID);
        return PyStatic.NewFalse();
    }

    FillOCApplicationChange(ocac, oldInfo, newInfo);
    if (!m_db.DeleteApplication(oldInfo)) {
        codelog(SERVICE__ERROR, "%s: Failed to delete application info for char %u corp %u", call.client->GetName(), ocac.charID, ocac.corpID);
        return PyStatic.NewFalse();
    }

    sEntityList.CorpNotify(m_corpID, Notify::Types::CorpAppNew, "OnCorporationApplicationChanged", "*corpid&corprole", ocac.Encode());

    return PyStatic.NewTrue();
}

void CorpRegistryBound::CheckCanAccept(uint32 charID)
{
    std::string corpName = CorporationDB::GetCorpName(m_corpID);

    // the applicant must be free to leave their current corp (not a CEO, no roles, stasis over)
    CorpData data = CorpData();
    if (!CorpMgr::LoadMemberData(charID, data))
        throw CustomError("That character could not be found.");
    if (IsPlayerCorp(data.corporationID)) {
        std::string errorKey;
        PyDict* args(nullptr);
        bool canLeave = sCorpMgr.CanLeave(charID, errorKey, args);
        PySafeDecRef(args);
        if (!canLeave)
            throw CustomError("The applicant cannot leave their current corporation at this time.");
    }

    // member limit (the CEO's corporation management skills)
    uint16 memberLimit(0);
    uint8 races(0);
    CorporationDB::GetAbilities(m_corpID, memberLimit, races);
    if (m_db.GetMemberCount(m_corpID) + 1 > memberLimit)
        throw CustomError("%s cannot accept any new members at this time.", corpName.c_str());

    if (!sConfig.corp.EnforceRaceLimit)
        return;

    // allowedMemberRaceIDs is a race bit mask (Caldari 1, Minmatar 2, Amarr 4, Gallente 8)
    uint8 raceID = CharacterDB::GetRaceID(charID);
    if ((races & raceID) == 0)
        throw CustomError("%s does not accept members of that race.", corpName.c_str());

    // Ethnic Relations:  members of other races than the CEO's up to 20% of the member limit per level
    uint32 ceoID = CorporationDB::GetCorporationCEO(m_corpID);
    uint8 ceoRace = CharacterDB::GetRaceID(ceoID);
    if (raceID != ceoRace) {
        uint32 quota = memberLimit * CorpMgr::GetSkillLevel(ceoID, EvESkill::EthnicRelations) / 5;
        if (CorporationDB::GetOtherRaceMemberCount(m_corpID, ceoRace) + 1 > quota)
            throw CustomError("%s cannot accept any more members of other races than its CEO's at this time.", corpName.c_str());
    }
}

void CorpRegistryBound::FillOCApplicationChange(OnCorporationApplicationChanged& OCAC, const Corp::ApplicationInfo& Old, const Corp::ApplicationInfo& New)
{
    //  ? (PyRep*)new PyInt(x) : PyStatic.NewNone();
    if (Old.valid) {
        OCAC.applicationIDOld = new PyInt(Old.appID);
        //OCAC.applicationDateTimeOld = new PyLong(Old.appTime);
        OCAC.applicationTextOld = new PyString(Old.appText);
        OCAC.characterIDOld = new PyInt(Old.charID);
        OCAC.corporationIDOld = new PyInt(Old.corpID);
        //OCAC.deletedOld = new PyInt(Old.deleted);
        //OCAC.grantableRolesOld = new PyLong(Old.grantRole);
        if (Old.lastCID) {
            OCAC.lastCorpUpdaterIDOld = new PyInt(Old.lastCID);
        } else {
            OCAC.lastCorpUpdaterIDOld = PyStatic.NewNone();
        }
        //OCAC.rolesOld = new PyLong(Old.role);
        OCAC.statusOld = new PyInt(Old.status);
    } else {
        OCAC.applicationIDOld = PyStatic.NewNone();
        //OCAC.applicationDateTimeOld = PyStatic.NewNone();
        OCAC.applicationTextOld = PyStatic.NewNone();
        OCAC.characterIDOld = PyStatic.NewNone();
        OCAC.corporationIDOld = PyStatic.NewNone();
        //OCAC.deletedOld = PyStatic.NewNone();
        //OCAC.grantableRolesOld = PyStatic.NewNone();
        OCAC.lastCorpUpdaterIDOld = PyStatic.NewNone();
        //OCAC.rolesOld = PyStatic.NewNone();
        OCAC.statusOld = PyStatic.NewNone();
    }

    if (New.valid) {
        OCAC.applicationIDNew = new PyInt(New.appID);
        //OCAC.applicationDateTimeNew = new PyLong(New.appTime);
        OCAC.applicationTextNew = new PyString(New.appText);
        OCAC.characterIDNew = new PyInt(New.charID);
        OCAC.corporationIDNew = new PyInt(New.corpID);
        //OCAC.deletedNew = new PyInt(New.deleted);
        //OCAC.grantableRolesNew = new PyLong(New.grantRole);
        if (New.lastCID) {
            OCAC.lastCorpUpdaterIDNew = new PyInt(New.lastCID);
        } else {
            OCAC.lastCorpUpdaterIDNew = PyStatic.NewNone();
        }
        //OCAC.rolesNew = new PyLong(New.role);
        OCAC.statusNew = new PyInt(New.status);
    } else {
        OCAC.applicationIDNew = PyStatic.NewNone();
        //OCAC.applicationDateTimeNew = PyStatic.NewNone();
        OCAC.applicationTextNew = PyStatic.NewNone();
        OCAC.characterIDNew = PyStatic.NewNone();
        OCAC.corporationIDNew = PyStatic.NewNone();
        //OCAC.deletedNew = PyStatic.NewNone();
        //OCAC.grantableRolesNew = PyStatic.NewNone();
        OCAC.lastCorpUpdaterIDNew = PyStatic.NewNone();
        //OCAC.rolesNew = PyStatic.NewNone();
        OCAC.statusNew = PyStatic.NewNone();
    }
}

PyResult CorpRegistryBound::GetStations(PyCallArgs &call)
{   // not working
    //  logs show this should be SparseRowset, but handled by bound corp registry object
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetStations() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    /*
     *    [PyTuple 1 items]
     *      [PySubStream 88 bytes]
     *        [PyObjectData Name: util.SparseRowset]
     *          [PyTuple 3 items]
     *            [PyList 2 items]
     *              [PyString "stationID"]
     *              [PyString "typeID"]
     *            [PySubStruct]
     *              [PySubStream 51 bytes]
     *                [PyTuple 3 items]
     *                  [PyString "N=698477:223512"]
     *                  [PyDict 1 kvp]
     *                    [PyString "realRowCount"]
     *                    [PyInt 0]
     *                  [PyIntegerVar 129492959106785392]
     *            [PyInt 0]
     *    [PyDict 1 kvp]
     *      [PyString "OID+"]
     *      [PyDict 1 kvp]
     *        [PyString "N=698477:223512"]
     *        [PyIntegerVar 129492959106785392]
     */

    /** @todo  need more data/info on this return to make it work right..
    PyBoundObject* bObj = new SparseCorpOfficeListBound(m_manager, m_db);
    if (bObj == NULL) {
        _log(SERVICE__ERROR, "%s: Unable to create bound object for:", call.client->GetName()); //errors here
        return nullptr;
    }

    // this sends header info and # offices in station
    // Data will be fetched from the SparseRowset
    CorpOfficeSparseRowset rsp;

    rsp.officeNumber = StationDB::GetOfficeCount(m_corpID);

    PyDict *dict = new PyDict();
    dict->SetItemString("realRowCount", new PyInt(rsp.officeNumber));

    rsp.boundObject = m_manager->BindObject(call.client, bObj, dict);

    PyObject* obj = rsp.Encode();
    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
    */
    PyObject* obj = m_db.GetStations(m_corpID);

    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
}

void CorpRegistryBound::BoundReleased (OfficeSparseBound* bound) {
    this->m_offices = nullptr;
}

PyResult CorpRegistryBound::GetOffices(PyCallArgs &call) {
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetOffices() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    PyList* headers = new PyList;

    headers->AddItemString ("itemID");
    headers->AddItemString ("stationID");
    headers->AddItemString ("stationTypeID");
    headers->AddItemString ("officeFolderID");
    headers->AddItemString ("typeID");

    if (this->m_offices == nullptr) {
        this->m_offices = new OfficeSparseBound (this->GetServiceManager (), *this, m_db, m_corpID, headers);
    }

    if (this->m_offices == nullptr) {
        _log(SERVICE__ERROR, "%s: Unable to create bound object for:", call.client->GetName()); //errors here
        return nullptr;
    }

    this->m_offices->NewReference (call.client);

    return this->m_offices->GetHeader ();
    /*
    [PyTuple 1 items]
      [PySubStream 114 bytes]
        [PyObjectData Name: util.SparseRowset]
          [PyTuple 3 items]
            [PyList 4 items]
              [PyString "stationID"]
              [PyString "typeID"]
              [PyString "officeID"]
              [PyString "officeFolderID"]
            [PySubStruct]
              [PySubStream 50 bytes]
                [PyTuple 3 items]
                  [PyString "N=789442:2172"]
                  [PyDict 1 kvp]
                    [PyString "realRowCount"]
                    [PyInt 8]
                  [PyIntegerVar 129753802088805346]
            [PyInt 8]
    [PyDict 1 kvp]
      [PyString "OID+"]
      [PyDict 1 kvp]
        [PyString "N=789442:2172"]
        [PyIntegerVar 129753802088805346]
    */
    /*  newish  (dv packet)
    [PyTuple 1 items]
      [PySubStream 139 bytes]
        [PyObjectData Name: eve.common.script.sys.rowset.SparseRowset]
          [PyTuple 3 items]
            [PyList 5 items]
              [PyString "locationID"]
              [PyString "solarSystemID"]
              [PyString "typeID"]
              [PyString "officeID"]
              [PyString "officeFolderID"]
            [PySubStruct]
              [PySubStream 50 bytes]
                [PyTuple 3 items]
                  [PyString "N=1222739:1759"]
                  [PyDict 1 kvp]
                    [PyString "realRowCount"]
                    [PyInt 0]
                  [PyIntegerVar 131101118262531055]
            [PyInt 0]
    [PyDict 1 kvp]
      [PyString "OID+"]
      [PyDict 1 kvp]
        [PyString "N=1222739:1759"]
        [PyIntegerVar 131101118262531055]
    */
}

PyResult CorpRegistryBound::InsertVoteCase(PyCallArgs &call, PyRep* voteCaseText, PyRep* description, PyInt* corporationID, PyInt* voteType,
                                           PyObject* voteCaseOptions, std::optional<PyRep*> startDateTime, std::optional<PyRep*> endDateTime) {
    //  return self.GetCorpRegistry().InsertVoteCase(voteCaseText, description, corporationID, voteType, voteCaseOptions, startDateTime, endDateTime)
    //  voteCaseOptions:  util.Rowset ['optionText', 'parameter', 'parameter1', 'parameter2'];  the row index is the optionID, option 0 is "do it"
    _log(CORP__CALL, "CorpRegistryBound::Handle_InsertVoteCase()");
    call.Dump(CORP__CALL_DUMP);

    RequireMember(call);
    uint32 charID = call.client->GetCharacterID();
    if ((uint32)corporationID->value() != m_corpID)
        throw CustomError("You can only propose votes in your own corporation.");
    // "You cannot propose a vote during a CEO voting period."
    if (CorporationDB::HasOpenCEOVote(m_corpID))
        throw UserError("CEOCantAddVoteInCEOVotePeriod");

    uint8 type = voteType->value();
    if (type == Corp::VoteType::CEO) {
        if (!sCorpMgr.CanRunForCEO(m_corpID, charID))
            throw UserError("CantRunForCEOAtTheMoment");
    } else if ((call.client->GetRolesAtAll() & Corp::Role::Director) != Corp::Role::Director) {
        throw UserError("CrpOnlyDirectorsCanProposeVotes");
    }

    // options
    if (!voteCaseOptions->arguments()->IsDict())
        throw CustomError("The vote options could not be read.");
    PyRep* lines = voteCaseOptions->arguments()->AsDict()->GetItemString("lines");
    if ((lines == nullptr) or !lines->IsList())
        throw CustomError("The vote options could not be read.");

    std::vector<Corp::VoteCaseOptions> options;
    int8 optionID(0);
    for (PyList::const_iterator itr = lines->AsList()->begin(); itr != lines->AsList()->end(); ++itr) {
        if (!(*itr)->IsList() or ((*itr)->AsList()->size() < 4))
            continue;
        PyList* line = (*itr)->AsList();
        Corp::VoteCaseOptions option = Corp::VoteCaseOptions();
            option.optionID = optionID++;
            option.optionText = PyRep::StringContent(line->GetItem(0));
            option.parameter = (int32)Corp::MemberUpdate::Int(line->GetItem(1)).value_or(0);
            option.parameter1 = (int32)Corp::MemberUpdate::Int(line->GetItem(2)).value_or(0);
            option.parameter2 = (int32)Corp::MemberUpdate::Int(line->GetItem(3)).value_or(0);
        options.push_back(option);
    }
    if (options.size() < 2)
        throw CustomError("A vote needs at least two options.");

    // what the vote is about
    Corp::VoteCaseOptions& subject = options.front();
    switch (type) {
        case Corp::VoteType::CEO: {
            subject.parameter = charID;
        } break;
        case Corp::VoteType::War: {
            if (call.client->GetAllianceID() != 0)
                throw UserError("CrpCanNotSanctionWarOwner");
            if (IsPlayerCorp(subject.parameter)) {
                CorpData data = CorpData();
                    data.corporationID = subject.parameter;
                CorporationDB::GetCorpData(data);
                if (data.allianceID != 0)
                    throw UserError("CrpCanNotSanctionWarAgainstOwner");
            }
        } break;
        case Corp::VoteType::Shares: {
            if (subject.parameter <= 0)
                throw CustomError("The number of new shares must be more than zero.");
        } break;
        case Corp::VoteType::KickMember: {
            CorpData data = CorpData();
            if (!CorpMgr::LoadMemberData(subject.parameter, data) or (data.corporationID != m_corpID))
                throw CustomError("That character is not a member of this corporation.");
            if (CorporationDB::GetCorporationCEO(m_corpID) == (uint32)subject.parameter)
                throw CustomError("The CEO cannot be expelled by a vote.  Propose a CEO vote instead.");
        } break;
        case Corp::VoteType::ItemLock: {
            ItemLockdown::ValidateLockable(m_corpID, subject.parameter, subject.parameter2);
        } break;
        case Corp::VoteType::ItemUnlock: {
            if (!ItemLockdown::IsLocked(subject.parameter))
                throw UserError("CrpItemIsNotLocked");
        } break;
        case Corp::VoteType::General:
            break;
        default:
            throw CustomError("Unknown vote type.");
    }

    // the voting period starts now and runs 1 to 30 days
    int64 now = GetFileTimeNow();
    int64 end = now + EvE::Time::Day;
    if (endDateTime.has_value() and (endDateTime.value()->IsInt() or endDateTime.value()->IsLong()))
        end = PyRep::IntegerValue(endDateTime.value());
    end = std::max<int64>(now + EvE::Time::Day, std::min<int64>(now + 30 * EvE::Time::Day, end));

    uint32 voteCaseID = CorporationDB::AddVoteCase(m_corpID, charID, type, PyRep::StringContent(voteCaseText), PyRep::StringContent(description),
                                                  now, end, options);
    if (voteCaseID == 0)
        throw CustomError("The vote could not be created.");

    sCorpMgr.OnVoteCaseOpened(m_corpID, voteCaseID, charID);
    return nullptr;
}

PyResult CorpRegistryBound::GetVoteCasesByCorporation(PyCallArgs &call, PyInt* corporationID, std::optional<PyInt*> status, std::optional <PyInt*> maxLen)
{
    //  xxx  = self.GetCorpRegistry().GetVoteCasesByCorporation(corpid)
    //return self.GetCorpRegistry().GetVoteCasesByCorporation(corpid, status, maxLen)

    /*** from closed
     * 04:24:15 W CorpRegistryBound::Handle_GetVoteCasesByCorporation(): size= 3
     * 04:24:15 [CorpCallDump]   Call Arguments:
     * 04:24:15 [CorpCallDump]       Tuple: 3 elements
     * 04:24:15 [CorpCallDump]         [ 0] Integer field: 98000000
     * 04:24:15 [CorpCallDump]         [ 1] Integer field: 1
     * 04:24:15 [CorpCallDump]         [ 2] Integer field: 20
     *** from open
     * 04:24:20 W CorpRegistryBound::Handle_GetVoteCasesByCorporation(): size= 3
     * 04:24:20 [CorpCallDump]   Call Arguments:
     * 04:24:20 [CorpCallDump]       Tuple: 3 elements
     * 04:24:20 [CorpCallDump]         [ 0] Integer field: 98000000
     * 04:24:20 [CorpCallDump]         [ 1] Integer field: 2
     * 04:24:20 [CorpCallDump]         [ 2] Integer field: 0
     *** from sanctionable actions
     * 12:48:19 [CorpCall] CorpRegistryBound::Handle_GetVoteCasesByCorporation()
     * 12:48:19 [CorpCallDump]   Call Arguments:
     * 12:48:19 [CorpCallDump]      Tuple: 1 elements
     * 12:48:19 [CorpCallDump]       [ 0]    Integer: 98000000
     *
     */
    /*
    [PyTuple 1 items]
      [PySubStream 193 bytes]
        [PyObjectData Name: util.IndexRowset]
          [PyDict 4 kvp]
            [PyString "items"]
            [PyDict 1 kvp]
              [PyInt 2167602]
              [PyList 8 items]
                [PyInt 2167602]
                [PyString "proposed vote"]
                [PyString "please vote for me"]
                [PyInt 98038978]
                [PyInt 1661059544]
                [PyInt 4]
                [PyIntegerVar 129492998400000000]
                [PyIntegerVar 129493862400000000]
            [PyString "RowClass"]
            [PyToken util.Row]
            [PyString "idName"]
            [PyString "voteCaseID"]
            [PyString "header"]
            [PyList 8 items]
              [PyString "voteCaseID"]
              [PyString "voteCaseText"]
              [PyString "description"]
              [PyString "corporationID"]
              [PyString "characterID"]
              [PyString "voteType"]
              [PyString "startDateTime"]
              [PyString "endDateTime"]
              */
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetVoteCasesByCorporation()");
    call.Dump(CORP__CALL_DUMP);

    uint8 statusValue = (status.has_value() ? status.value()->value() : 0);
    uint16 limit = (maxLen.has_value() ? maxLen.value()->value() : 0);
    return CorporationDB::GetVoteItems(corporationID->value(), statusValue, limit);
}

PyResult CorpRegistryBound::GetVoteCaseOptions(PyCallArgs &call, PyRep* corporationID, PyInt* voteCaseID) {
    // options = self.GetCorpRegistry().GetVoteCaseOptions(corpID, voteCaseID)     corpID may be None (sanctioned actions)
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetVoteCaseOptions()");
    call.Dump(CORP__CALL_DUMP);

    PyRep* rsp = CorporationDB::GetVoteOptions(voteCaseID->value());
    if (is_log_enabled(CORP__RSP_DUMP))
        rsp->Dump(CORP__RSP_DUMP, "");

    return rsp;
}

PyResult CorpRegistryBound::GetVotes(PyCallArgs &call, PyInt* corporationId, PyInt* voteCaseID) {
    // charVotes = sm.GetService('corp').GetVotes(self.corpID, vote.voteCaseID)
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetVotes() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    // this will need a bit more thought....
    /*
                charVotes = sm.GetService('corp').GetVotes(self.corpID, vote.voteCaseID)
                hasVoted = 0
                votedFor = -1
                if charVotes and charVotes.has_key(session.charid):
                    hasVoted = 1
                    votedFor = charVotes[session.charid].optionID
            */

    PyRep* rsp = CorporationDB::GetVotes(voteCaseID->value());
    if (is_log_enabled(CORP__RSP_DUMP))
        rsp->Dump(CORP__RSP_DUMP, "");

    return rsp;
}

/*  GetVoteCaseOptions
    [PyTuple 1 items]
      [PySubStream 243 bytes]
        [PyObjectData Name: util.IndexRowset]
          [PyDict 4 kvp]
            [PyString "items"]
            [PyDict 3 kvp]
              [PyInt 0]
              [PyList 9 items]
                [PyInt 2167602]
                [PyInt 0]
                [PyString "vote choice 1"]
                [PyIntegerVar 0]
                [PyFloat 0]
                [PyInt 0]
                [PyInt 0]
                [PyNone]
                [PyNone]
              [PyInt 1]
              [PyList 9 items]
                [PyInt 2167602]
                [PyInt 1]
                [PyString "vote choice 2"]
                [PyIntegerVar 1]
                [PyFloat 0]
                [PyInt 0]
                [PyInt 0]
                [PyNone]
                [PyNone]
              [PyInt 2]
              [PyList 9 items]
                [PyInt 2167602]
                [PyInt 2]
                [PyString "vote choice 3"]
                [PyIntegerVar 2]
                [PyFloat 0]
                [PyInt 0]
                [PyInt 0]
                [PyNone]
                [PyNone]
            [PyString "RowClass"]
            [PyToken util.Row]
            [PyString "idName"]
            [PyString "optionID"]
            [PyString "header"]
            [PyList 9 items]
              [PyString "voteCaseID"]
              [PyString "optionID"]
              [PyString "optionText"]
              [PyString "parameter"]
              [PyString "votesFor"]
              [PyString "votesMade"]
              [PyString "votesProxied"]
              [PyString "parameter1"]
              [PyString "parameter2"]

              */


PyResult CorpRegistryBound::GetSanctionedActionsByCorporation(PyCallArgs &call, PyInt* corporationID, PyInt* state) {
    //  rows = sm.GetService('corp').GetSanctionedActionsByCorporation(eve.session.corpid, state)
    /*
05:09:00 [CorpCallDump]   Call Arguments:
05:09:00 [CorpCallDump]      Tuple: 2 elements
05:09:00 [CorpCallDump]       [ 0]    Integer: 98000000
05:09:00 [CorpCallDump]       [ 1]    Integer: 1
*/
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetSanctionedActionsByCorporation()");
    call.Dump(CORP__CALL_DUMP);

    /*  this still needs work...used to show voting items in corp that 'mean something'
     *
     * if row.voteType in [const.voteItemUnlock,
     *  const.voteCEO,
     *  const.voteWar,
     *  const.voteShares,
     *  const.voteKickMember,
     *  const.voteItemLockdown] and row.parameter == 0:
     *     continue
     * voteType = self.voteTypes[row.voteType]
     * title = voteCases[row.voteCaseID].voteCaseText
     * description = voteCases[row.voteCaseID].description
     * expires = util.FmtDate(row.expires)
     * actedUpon = [localization.GetByLabel('UI/Generic/No'), localization.GetByLabel('UI/Generic/Yes')][row.actedUpon]
     * inEffect = [localization.GetByLabel('UI/Generic/No'), localization.GetByLabel('UI/Generic/Yes')][row.inEffect]
     * rescended = localization.GetByLabel('UI/Generic/No')
     * if row.timeRescended:
     *     rescended = localization.GetByLabel('UI/Corporations/CorpSanctionableActions/AnswerWithTimestamp', answer=rescended, timestamp=util.FmtDate(row.timeRescended))
     * if row.timeActedUpon:
     *     actedUpon = localization.GetByLabel('UI/Corporations/CorpSanctionableActions/AnswerWithTimestamp', answer=actedUpon, timestamp=util.FmtDate(row.timeActedUpon))
     */


    return CorporationDB::GetSanctionedItems(corporationID->value(), state->value());
}

PyResult CorpRegistryBound::UpdateSanctionedAction(PyCallArgs &call, PyInt* voteCaseID, PyRep* inEffect, std::optional<PyRep*> reason) {
    // corp_ui_sanctionableactions 'Implement':  UpdateSanctionedAction(voteCaseID, 1) by the active CEO
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateSanctionedAction()");
    call.Dump(CORP__CALL_DUMP);

    RequireMember(call);
    std::optional<int64> effect = Corp::MemberUpdate::Int(inEffect);
    if (!effect.has_value() or (effect.value() == 0))
        throw CustomError("A sanctioned action can only be undone by another vote.");

    sCorpMgr.ImplementSanction(call.client, voteCaseID->value());
    return nullptr;
}

PyResult CorpRegistryBound::CanVote(PyCallArgs &call, PyInt* corporationID) {
    // canVote = sm.GetService('corp').CanVote(self.corpID)
    _log(CORP__CALL, "CorpRegistryBound::Handle_CanVote() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    return new PyBool(sCorpMgr.GetVoteWeight(call.client->GetCharacterID(), corporationID->value()) > 0);
}


PyResult CorpRegistryBound::InsertVote(PyCallArgs &call, PyInt* corporationID, PyInt* voteCaseID, PyInt* voteValue) {
    // return self.GetCorpRegistry().InsertVote(corporationID, voteCaseID, voteValue)
    _log(CORP__CALL, "CorpRegistryBound::Handle_InsertVote() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    uint32 corpID = corporationID->value();
    uint32 charID = call.client->GetCharacterID();
    uint32 caseID = voteCaseID->value();
    Corp::VoteCase vc = Corp::VoteCase();
    if (!CorporationDB::GetVoteCase(caseID, vc) or (vc.corporationID != corpID))
        throw CustomError("That vote could not be found.");
    if ((vc.status != 2) or (vc.endDateTime <= GetFileTimeNow()))
        throw UserError("CorpVoteCaseClosed");
    if (CorporationDB::HasVoted(caseID, charID))
        throw UserError("CrpAlreadyVoted");

    std::vector<Corp::VoteCaseOptions> options;
    CorporationDB::GetVoteCaseOptions(caseID, options);
    double oldVotes(-1);
    for (auto cur : options)
        if (cur.optionID == voteValue->value())
            oldVotes = cur.votesFor;
    if (oldVotes < 0)
        throw CustomError("That is not an option of this vote.");

    // votes are weighted by shares
    uint32 weight = sCorpMgr.GetVoteWeight(charID, corpID);
    if (weight == 0)
        throw CustomError("You have no shares in this corporation and cannot vote.");

    if (!CorporationDB::CastVote(corpID, charID, caseID, voteValue->value(), weight))
        throw UserError("CrpAlreadyVoted");

    // OnCorporationVoteChanged(corpID, voteCaseID, characterID, add) and the option's new tally
    PyDict* change = new PyDict();
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, PyStatic.NewNone());
        tuple->SetItem(1, new PyInt(charID));
    change->SetItemString("characterID", tuple);
    tuple = new PyTuple(2);
        tuple->SetItem(0, PyStatic.NewNone());
        tuple->SetItem(1, new PyInt(caseID));
    change->SetItemString("voteCaseID", tuple);
    tuple = new PyTuple(2);
        tuple->SetItem(0, PyStatic.NewNone());
        tuple->SetItem(1, new PyInt(voteValue->value()));
    change->SetItemString("optionID", tuple);
    PyTuple* payload = new PyTuple(4);
        payload->SetItem(0, new PyInt(corpID));
        payload->SetItem(1, new PyInt(caseID));
        payload->SetItem(2, new PyInt(charID));
        payload->SetItem(3, change);
    if (call.client->GetCorporationID() != (int32)corpID) {
        PyIncRef(payload);
        call.client->SendNotification("OnCorporationVoteChanged", "clientID", &payload);
    }
    sEntityList.CorpNotify(corpID, Notify::Types::CorpNews, "OnCorporationVoteChanged", "corpid", payload);

    change = new PyDict();
    tuple = new PyTuple(2);
        tuple->SetItem(0, new PyFloat(oldVotes));
        tuple->SetItem(1, new PyFloat(oldVotes + weight));
    change->SetItemString("votesFor", tuple);
    payload = new PyTuple(4);
        payload->SetItem(0, new PyInt(corpID));
        payload->SetItem(1, new PyInt(caseID));
        payload->SetItem(2, new PyInt(voteValue->value()));
        payload->SetItem(3, change);
    if (call.client->GetCorporationID() != (int32)corpID) {
        PyIncRef(payload);
        call.client->SendNotification("OnCorporationVoteCaseOptionChanged", "clientID", &payload);
    }
    sEntityList.CorpNotify(corpID, Notify::Types::CorpNews, "OnCorporationVoteCaseOptionChanged", "corpid", payload);

    return nullptr;
}

PyResult CorpRegistryBound::GetLockedItemLocations(PyCallArgs& call)
{
    // corp.itemLocking:  locationIDs = self.GetCorpRegistry().GetLockedItemLocations()   (stations with locked items)
    return ItemLockdown::GetLockedItemLocations(m_corpID);
}

PyResult CorpRegistryBound::GetLockedItemsByLocation(PyCallArgs& call, PyInt* locationID)
{
    // corp.itemLocking:  rows = self.GetCorpRegistry().GetLockedItemsByLocation(locationID), kept and updated by OnLockedItemChange
    return ItemLockdown::GetLockedItemsByLocation(m_corpID, locationID->value());
}

/**     ***********************************************************************
 * @note   these do absolutely nothing at this time....
 */

PyResult CorpRegistryBound::AddCorporateContact(PyCallArgs &call, PyInt* contactID, PyInt* relationshipID) {
    //self.GetCorpRegistry().AddCorporateContact(contactID, relationshipID)
    _log(CORP__CALL, "CorpRegistryBound::Handle_AddCorporateContact()");
    call.Dump(CORP__CALL_DUMP);

    m_db.AddContact(m_corpID, contactID->value(), relationshipID->value());

    return nullptr;
}

PyResult CorpRegistryBound::EditCorporateContact(PyCallArgs &call, PyInt* contactID, PyInt* relationshipID) {
    //self.GetCorpRegistry().EditCorporateContact(contactID, relationshipID)
    _log(CORP__CALL, "CorpRegistryBound::Handle_EditCorporateContact()");
    call.Dump(CORP__CALL_DUMP);

    m_db.UpdateContact(relationshipID->value(), contactID->value(), m_corpID);

    return nullptr;
}

PyResult CorpRegistryBound::EditContactsRelationshipID(PyCallArgs &call, PyList* contactIDs, PyInt* relationshipID) {
    //self.GetCorpRegistry().EditContactsRelationshipID(contactIDs, relationshipID)
    _log(CORP__CALL, "CorpRegistryBound::Handle_EditContactsRelationshipID()");
    call.Dump(CORP__CALL_DUMP);

    for (PyList::const_iterator itr = contactIDs->begin(); itr != contactIDs->end(); ++itr) {
        m_db.UpdateContact(relationshipID->value(), PyRep::IntegerValueU32(*itr), m_corpID);
    }

    return nullptr;
}

PyResult CorpRegistryBound::RemoveCorporateContacts(PyCallArgs &call, PyList* contactIDs) {
    // self.GetCorpRegistry().RemoveCorporateContacts(contactIDs)
    _log(CORP__CALL, "CorpRegistryBound::Handle_RemoveCorporateContacts()");
    call.Dump(CORP__CALL_DUMP);

    for (PyList::const_iterator itr = contactIDs->begin(); itr != contactIDs->end(); ++itr) {
        m_db.RemoveContact(PyRep::IntegerValueU32(*itr), m_corpID);
    }

    return nullptr;
}

// this is a member role/title update by memberIDs called from corp->members->find in role->task mgmt
PyResult CorpRegistryBound::ExecuteActions(PyCallArgs &call, PyList* targetIDs, PyList* remoteActions) {
    //  corp_ui_member_view_task_mgt:  actions = [[verb, property, value], ...]
    //   CTV_ADD (1) / CTV_REMOVE (2):  property = a role mask column or 'titleMask', value = the mask
    //   CTV_SET (3):  'baseID'
    //   CTV_GIVE (4):  CTPG_CASH (6) isk from the corp wallet, CTPG_SHARES (7) corp shares
    _log(CORP__CALL, "CorpRegistryBound::Handle_ExecuteActions() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    RequireMember(call);

    for (PyList::const_iterator tItr = targetIDs->begin(); tItr != targetIDs->end(); ++tItr) {
        if (!((*tItr)->IsInt() or (*tItr)->IsLong()))
            continue;
        uint32 targetID = PyRep::IntegerValueU32(*tItr);

        CorpData data = CorpData();
        Client* pTarget = sEntityList.FindClientByCharID(targetID);
        if ((pTarget != nullptr) and (pTarget->GetChar().get() != nullptr)) {
            data = pTarget->GetChar()->GetCorpData();
        } else if (!CharacterDB::GetCharCorpData(targetID, data)) {
            continue;
        }
        if (data.corporationID != m_corpID)
            continue;

        Corp::MemberUpdate upd = Corp::MemberUpdate();
            upd.charID = targetID;
            upd.roles = data.corpRole | data.rolesAtAll;
            upd.grantableRoles = data.grantableRoles;
            upd.rolesAtHQ = data.rolesAtHQ;
            upd.grantableRolesAtHQ = data.grantableRolesAtHQ;
            upd.rolesAtBase = data.rolesAtBase;
            upd.grantableRolesAtBase = data.grantableRolesAtBase;
            upd.rolesAtOther = data.rolesAtOther;
            upd.grantableRolesAtOther = data.grantableRolesAtOther;
            upd.titleMask = data.titleMask;
            upd.baseID = data.baseID;

        bool edit = false;
        for (PyList::const_iterator aItr = remoteActions->begin(); aItr != remoteActions->end(); ++aItr) {
            if (!(*aItr)->IsList() or ((*aItr)->AsList()->size() < 3))
                continue;
            PyList* action = (*aItr)->AsList();
            int64 verb = PyRep::IntegerValue(action->GetItem(0));
            PyRep* property = action->GetItem(1);
            PyRep* value = action->GetItem(2);

            if (verb == Corp::Actions::Give) {
                int64 what = PyRep::IntegerValue(property);
                if (what == Corp::Actions::Cash) {
                    double amount = 0;
                    if (value->IsInt() or value->IsLong()) {
                        amount = (double)PyRep::IntegerValue(value);
                    } else if (value->IsFloat()) {
                        amount = PyRep::FloatValue(value);
                    }
                    if (amount < 0.01)
                        continue;
                    if (!CorpMgr::HasRole(call.client->GetCorpRole(), Corp::Role::Accountant))
                        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You must be an accountant to pay members from the corporation wallet."));
                    std::string reason = "Payment from ";
                    reason += CorporationDB::GetCorpName(m_corpID);
                    AccountService::TransferFunds(m_corpID, targetID, amount, reason, Journal::EntryType::CorporationAccountWithdrawal,
                                                  call.client->GetCharacterID(), call.client->GetCorpAccountKey());
                } else if (what == Corp::Actions::Shares) {
                    GiveShares(call, targetID, PyRep::IntegerValue(value));
                }
                continue;
            }

            if (!property->IsString() and !property->IsWString())
                continue;
            std::string name = PyRep::StringContent(property);

            if (verb == Corp::Actions::Set) {
                if (name.compare("baseID") == 0) {
                    upd.baseID = Corp::MemberUpdate::Int(value);
                    if (!upd.baseID.has_value())
                        upd.baseID = 0;
                    edit = true;
                }
                continue;
            }

            if ((verb != Corp::Actions::Add) and (verb != Corp::Actions::Remove))
                continue;

            std::optional<int64>* field(nullptr);
            if (name.compare("roles") == 0)                       field = &upd.roles;
            else if (name.compare("grantableRoles") == 0)         field = &upd.grantableRoles;
            else if (name.compare("rolesAtHQ") == 0)              field = &upd.rolesAtHQ;
            else if (name.compare("grantableRolesAtHQ") == 0)     field = &upd.grantableRolesAtHQ;
            else if (name.compare("rolesAtBase") == 0)            field = &upd.rolesAtBase;
            else if (name.compare("grantableRolesAtBase") == 0)   field = &upd.grantableRolesAtBase;
            else if (name.compare("rolesAtOther") == 0)           field = &upd.rolesAtOther;
            else if (name.compare("grantableRolesAtOther") == 0)  field = &upd.grantableRolesAtOther;
            else if (name.compare("titleMask") == 0)              field = &upd.titleMask;
            if (field == nullptr)
                continue;

            int64 mask = PyRep::IntegerValue(value);
            if (verb == Corp::Actions::Add) {
                *field = field->value() | mask;
            } else {
                *field = field->value() & ~mask;
            }
            edit = true;
        }

        if (edit)
            sCorpMgr.UpdateMember(call.client, m_corpID, upd, false);
    }

    return nullptr;
}

PyResult CorpRegistryBound::CreateLabel(PyCallArgs &call, PyRep* name, PyRep* color) {
    // return self.GetCorpRegistry().CreateLabel(name, color)
    _log(CORP__CALL, "CorpRegistryBound::Handle_CreateLabel() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    return nullptr;
}

PyResult CorpRegistryBound::DeleteLabel(PyCallArgs &call, PyInt* labelID) {
    // self.GetCorpRegistry().DeleteLabel(labelID)
    _log(CORP__CALL, "CorpRegistryBound::Handle_DeleteLabel() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    return nullptr;
}

PyResult CorpRegistryBound::EditLabel(PyCallArgs &call, PyInt* labelID, PyRep* name, PyRep* color) {
    // self.GetCorpRegistry().EditLabel(labelID, name, color)
    _log(CORP__CALL, "CorpRegistryBound::Handle_EditLabel() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    return nullptr;
}

PyResult CorpRegistryBound::AssignLabels(PyCallArgs &call, PyList* contactIDs, PyInt* labelMask) {
    // self.GetCorpRegistry().AssignLabels(contactIDs, labelMask)
    _log(CORP__CALL, "CorpRegistryBound::Handle_AssignLabels() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    return nullptr;
}

PyResult CorpRegistryBound::RemoveLabels(PyCallArgs &call, PyList* contactIDs, PyInt* labelMask) {
    // self.GetCorpRegistry().RemoveLabels(contactIDs, labelMask)
    _log(CORP__CALL, "CorpRegistryBound::Handle_RemoveLabels() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    return nullptr;
}

PyResult CorpRegistryBound::CreateAlliance(PyCallArgs &call, PyRep* allianceName, PyRep* shortName, PyRep* description, PyRep* url) {
    //self.GetCorpRegistry().CreateAlliance(allianceName, shortName, description, url)
    _log(CORP__CALL, "CorpRegistryBound::Handle_CreateAlliance() size=%lli", call.tuple->size());
    call.Dump(CORP__CALL_DUMP);

    AllianceDB a_db;
    Client* pClient(call.client);

    std::string allianceNameStr = PyRep::StringContent(allianceName);
    std::string shortNameStr = PyRep::StringContent(shortName);
    std::string descriptionStr = PyRep::StringContent(description);
    std::string urlStr = PyRep::StringContent(url);

    // verify they're not using bad words in their alliance name
    for (const auto cur : badWords)
        if (EvE::icontains(allianceNameStr, cur))
            throw CustomError ("Alliance Name contains banned words");

    // this hits db directly, so test for possible sql injection code
    for (const auto cur : badCharsSearch)
        if (EvE::icontains(allianceNameStr, cur))
            throw CustomError ("Alliance Name contains invalid characters");

    // this hits db directly, so test for possible sql injection code
    for (const auto cur : badCharsSearch)
        if (EvE::icontains(shortNameStr, cur))
            throw CustomError ("Alliance short name contains invalid characters");

    // verify short name is available
    if (a_db.IsShortNameTaken(shortNameStr))
        throw CustomError ("Alliance short name is taken.");

    double ally_cost = sConfig.rates.allyCost;
    if (pClient->GetBalance(Account::CreditType::ISK) < ally_cost) {
        _log(SERVICE__ERROR, "%s: Cannot afford alliance startup costs!", pClient->GetName());

        // send error to client here
        throw CustomError ("You must have %f ISK to create an alliance.", ally_cost);
        return nullptr;
    }

    //take the money, send wallet blink event record the transaction in their journal.
    std::string reason = "DESC: Creating new alliance: ";
    reason += allianceNameStr;
    reason += " (";
    reason += shortNameStr;
    reason += ")";

    AccountService::TransferFunds(
        pClient->GetCharacterID(),
        m_db.GetStationOwner(pClient->GetStationID()),  // station owner files paperwork, this is fee for that
        ally_cost,
        reason.c_str(),
        Journal::EntryType::AllianceRegistrationFee,
        pClient->GetStationID(),
        Account::KeyType::Cash
    );

    //creating an alliance will affect eveStaticOwners, so we gotta invalidate the cache...
    //  call to db.AddCorporation() will update eveStaticOwners with new corp
    PyString* cache_name = new PyString( "config.StaticOwners" );
    this->m_cache->InvalidateCache( cache_name );
    PySafeDecRef( cache_name );

    // Register new alliance (also gather current corpID and new allyID at same time)
    uint32 allyID(0);
    uint32 corpID(0);
    if (!a_db.CreateAlliance(allianceNameStr, shortNameStr, descriptionStr, urlStr, pClient, allyID, corpID)) {
        codelog(SERVICE__ERROR, "New alliance creation failed.");
        return nullptr;
    }

    // create alliance channel
    this->m_lsc->CreateSystemChannel(allyID);

    // join corporation to alliance
    if (!a_db.UpdateCorpAlliance(allyID, corpID)) {
        codelog(SERVICE__ERROR, "Alliance join failed.");
        return nullptr;
    }

    // Add alliance membership record to corporation
    if (!a_db.AddEmployment(allyID, corpID)) {
        codelog(SERVICE__ERROR, "Add corp employment failed.");
        return nullptr;
    }

    // Update alliance ID on client session
    pClient->UpdateSessionInt("allianceid", allyID);

    //Return alliance we just created
    return a_db.GetAlliance(allyID);
}

PyResult CorpRegistryBound::ApplyToJoinAlliance(PyCallArgs &call, PyInt* allianceID, PyWString* applicationText) {
    _log(CORP__CALL, "CorpRegistryBound::Handle_ApplyToJoinAlliance()");
    call.Dump(CORP__CALL_DUMP);

    AllianceDB a_db;

    Alliance::ApplicationInfo app;
        app.appText = applicationText->content();
        app.allyID = allianceID->value();
        app.corpID = m_corpID;
        app.state = EveAlliance::AppStatus::AppNew;
        app.valid = true;

    if (!a_db.InsertApplication(app)) {
        codelog(SERVICE__ERROR, "New alliance application failed.");
        return nullptr;
    }

    Alliance::ApplicationInfo oldInfo = Alliance::ApplicationInfo();
        oldInfo.valid = false;

    OnAllianceApplicationChanged oaac;
        oaac.corpID = m_corpID;
        oaac.allianceID = app.allyID;

    AllianceBound::FillOAApplicationChange(oaac, oldInfo, app);

    //Send to everyone who needs to see it in the applying corp and in the alliance executor corp
    uint32 executorID = AllianceDB::GetExecutorID(app.allyID);

    std::vector<Client*> list;
    sEntityList.GetCorpClients(list, oaac.corpID);
    for (auto cur : list)
    {
        if (cur != nullptr)
        {
            cur->SendNotification("OnAllianceApplicationChanged", "clientID", oaac.Encode(), false);
            _log(ALLY__TRACE, "OnAllianceApplicationChanged sent to %s (%u)", cur->GetName(), cur->GetCharID());
        }
    }

    list.clear();
    sEntityList.GetCorpClients(list, executorID);
    for (auto cur : list)
    {
        if (cur != nullptr)
        {
            cur->SendNotification("OnAllianceApplicationChanged", "clientID", oaac.Encode(), false);
            _log(ALLY__TRACE, "OnAllianceApplicationChanged sent to %s (%u)", cur->GetName(), cur->GetCharID());
        }
    }

    /// Send an evemail to those who can decide
    /// Well, for the moment, send it to the ceo
    std::string subject = "New application from ";
    subject += call.client->GetName();
    std::vector<int32> recipients;
    recipients.push_back(m_db.GetCorporationCEO(AllianceDB::GetExecutorID(app.allyID)));
    this->m_lsc->SendMail(m_db.GetCorporationCEO(m_corpID), recipients, subject, applicationText->content());

    return nullptr;
}

PyResult CorpRegistryBound::GetAllianceApplications(PyCallArgs &call) {
    //application = sm.GetService('corp').GetAllianceApplications()[allianceID]
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetAllianceApplications()");
    call.Dump(CORP__CALL_DUMP);

    return AllianceDB::GetMyApplications(m_corpID);
}

PyResult CorpRegistryBound::DeleteAllianceApplication(PyCallArgs &call, PyInt* allianceID) {
    _log(CORP__CALL, "CorpRegistryBound::Handle_DeleteAllianceApplication()");
    call.Dump(CORP__CALL_DUMP);

    //This is not implemented by client, no context menu option

    AllianceDB a_db;

    //Old application info
    Alliance::ApplicationInfo oldInfo = Alliance::ApplicationInfo();

    if (!a_db.GetCurrentApplicationInfo(allianceID->value(), call.client->GetCorporationID(), oldInfo))
    {
        _log(SERVICE__ERROR, "%s: Failed to query application for corp %u alliance %u", call.client->GetName(), call.client->GetCorporationID(), allianceID->value());
        return nullptr;
    }

    //New application info (set deleted flag to true here)
    Alliance::ApplicationInfo newInfo = oldInfo;
    newInfo.valid = true;
    newInfo.deleted = true;

    OnAllianceApplicationChanged oaac;
    oaac.allianceID = newInfo.allyID;
    oaac.corpID = newInfo.corpID;
    AllianceBound::FillOAApplicationChange(oaac, oldInfo, newInfo);

    if (!a_db.DeleteApplication(newInfo)) {
        codelog(SERVICE__ERROR, "Deletion failed.");
        return nullptr;
    }

    //Send to everyone who needs to see it in the applying corp and in the alliance executor corp
    uint32 executorID = AllianceDB::GetExecutorID(oaac.allianceID);

    std::vector<Client *> list;
    sEntityList.GetCorpClients(list, oaac.corpID);
    for (auto cur : list)
    {
        if (cur->GetChar().get() != nullptr)
        {
            cur->SendNotification("OnAllianceApplicationChanged", "clientID", oaac.Encode(), false);
            _log(ALLY__TRACE, "OnAllianceApplicationChanged sent to %s(%u)", cur->GetName(), cur->GetCharID());
        }
    }

    list.clear();
    sEntityList.GetCorpClients(list, executorID);
    for (auto cur : list)
    {
        if (cur->GetChar().get() != nullptr)
        {
            cur->SendNotification("OnAllianceApplicationChanged", "clientID", oaac.Encode(), false);
            _log(ALLY__TRACE, "OnAllianceApplicationChanged sent to %s(%u)", cur->GetName(), cur->GetCharID());
        }
    }

    return nullptr;
}

PyResult CorpRegistryBound::GetRentalDetailsPlayer(PyCallArgs &call) {
    //return self.GetCorpRegistry().GetRentalDetailsPlayer()
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetRentalDetailsPlayer()");
    call.Dump(CORP__CALL_DUMP);

    // factory and research slot rentals are not offered here;  the wallet's rentals tab lists nothing
    return new PyList();
}

PyResult CorpRegistryBound::GetRentalDetailsCorp(PyCallArgs &call) {
    // return self.GetCorpRegistry().GetRentalDetailsCorp()
    _log(CORP__CALL, "CorpRegistryBound::Handle_GetRentalDetailsCorp()");
    call.Dump(CORP__CALL_DUMP);

    // factory and research slot rentals are not offered here;  the wallet's rentals tab lists nothing
    return new PyList();
}

PyResult CorpRegistryBound::UpdateCorporationAbilities(PyCallArgs &call) {
    // corp_dlg_editcorpdetails 'Update with my skills':  None = nothing done;  the client re-reads memberLimit and allowedMemberRaceIDs
    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateCorporationAbilities()");
    call.Dump(CORP__CALL_DUMP);

    RequireMember(call);
    if (CorporationDB::GetCorporationCEO(m_corpID) != call.client->GetCharacterID())
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Only the CEO can update the corporation with their skills."));

    sCorpMgr.UpdateAbilities(m_corpID);
    return m_db.GetCorporation(m_corpID);
}

PyResult CorpRegistryBound::UpdateStationManagementSettings(PyCallArgs &call,
    PyRep* modifiedServiceAccessRulesByServiceID, PyRep* modifiedServiceCostModifiers,
    PyRep* modifiedRentableItems, PyRep* stationName, PyRep* description,
    PyRep* dockingCostPerVolume, PyRep* officeRentalCost, PyRep* reprocessingStationsTake,
    PyRep* reprocessingHangarFlag, PyRep* exitTime, PyRep* standingOwnerID) {
    //  self.corpStationMgr.UpdateStationManagementSettings(self.modifiedServiceAccessRulesByServiceID, self.modifiedServiceCostModifiers, self.modifiedRentableItems, self.station.stationName, self.station.description, self.station.dockingCostPerVolume, self.station.officeRentalCost, self.station.reprocessingStationsTake, self.station.reprocessingHangarFlag, self.station.exitTime, self.station.standingOwnerID)

    _log(CORP__CALL, "CorpRegistryBound::Handle_UpdateStationManagementSettings()");
    call.Dump(CORP__CALL_DUMP);

    // not real sure what this does yet....outpost shit maybe?

    return nullptr;
}

PyResult CorpRegistryBound::GetNumberOfPotentialCEOs(PyCallArgs &call) {
    //  return self.GetCorpRegistry().GetNumberOfPotentialCEOs()     compared > 0 by the resign dialog
    std::vector<uint32> candidates;
    sCorpMgr.GetPotentialCEOs(m_corpID, candidates);
    return new PyInt(candidates.size());
}


PyResult CorpRegistryBound::CanLeaveCurrentCorporation(PyCallArgs &call) {
    //  canLeave, error, errorDetails = corpSvc.CanLeaveCurrentCorporation()
    //  the Quit button raises UserError(error, errorDetails);  CrpCantQuitNotInStasis shows 'Prepare to Quit Corp' instead
    std::string errorKey;
    PyDict* args(nullptr);
    bool canLeave = sCorpMgr.CanLeave(call.client->GetCharacterID(), errorKey, args);

    PyTuple* tuple = new PyTuple(3);
    if (canLeave) {
        tuple->SetItem(0, PyStatic.NewTrue());
        tuple->SetItem(1, PyStatic.NewNone());
        tuple->SetItem(2, PyStatic.NewNone());
    } else {
        tuple->SetItem(0, PyStatic.NewFalse());
        tuple->SetItem(1, (errorKey.empty() ? (PyRep*)PyStatic.NewNone() : (PyRep*)new PyString(errorKey)));
        tuple->SetItem(2, (args == nullptr ? (PyRep*)new PyDict() : (PyRep*)args));
    }

    return tuple;
}

PyResult CorpRegistryBound::CanBeKickedOut(PyCallArgs &call, PyInt* characterID) {
    // base_corporation.CanKickOut(charID):  self (quitting) or by a director
    if (characterID->value() == call.client->GetCharacterID()) {
        std::string errorKey;
        PyDict* args(nullptr);
        bool canLeave = sCorpMgr.CanLeave(characterID->value(), errorKey, args);
        PySafeDecRef(args);
        return new PyBool(canLeave);
    }

    std::string reason;
    return new PyBool(sCorpMgr.CanBeKicked(call.client, m_corpID, characterID->value(), reason));
}

PyResult CorpRegistryBound::KickOutMember(PyCallArgs &call, PyInt* characterID) {
    // base_corporation.KickOut(charID):  self in a session change ('corp.kickself'), others plain
    RequireMember(call);
    uint32 charID = characterID->value();

    if (charID == call.client->GetCharacterID()) {
        if (!call.client->IsDocked())
            throw UserError("CrpCanNotChangeCorpInSpace");
        std::string errorKey;
        PyDict* args(nullptr);
        if (!sCorpMgr.CanLeave(charID, errorKey, args)) {
            UserError err(errorKey.c_str());
            if (args != nullptr) {
                for (PyDict::const_iterator itr = args->begin(); itr != args->end(); ++itr) {
                    PyIncRef(itr->second);
                    err.AddFormatValue(PyRep::StringContent(itr->first).c_str(), itr->second);
                }
                PyDecRef(args);
            }
            throw err;
        }
        sCorpMgr.LeaveCorp(charID, false, charID);
        return nullptr;
    }

    std::string reason;
    if (!sCorpMgr.CanBeKicked(call.client, m_corpID, charID, reason))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString(reason));

    // "You can not kick out this member of your corporation as they are currently in space."
    Client* pTarget = sEntityList.FindClientByCharID(charID);
    if ((pTarget != nullptr) and !pTarget->IsDocked())
        throw UserError("CrpCanNotKickMemberInSpace");

    sCorpMgr.LeaveCorp(charID, true, call.client->GetCharacterID());
    return nullptr;
}

PyResult CorpRegistryBound::MemberCanRunForCEO(PyCallArgs &call) {
    // bco_members.MemberCanRunForCEO:  cached by the client until skills, member limit, shares or CEO votes change
    return new PyBool(sCorpMgr.CanRunForCEO(m_corpID, call.client->GetCharacterID()));
}

PyResult CorpRegistryBound::ResignFromCEO(PyCallArgs &call, std::optional<PyInt*> newCeoID) {
    // base_corporation.ResignFromCEO, in a session change:  None on success, or the list of members who could take over
    //  (the client lets the CEO pick one and calls again).  no candidates:  the corporation closes
    RequireMember(call);
    uint32 charID = call.client->GetCharacterID();
    if (CorporationDB::GetCorporationCEO(m_corpID) != charID)
        throw UserError("OnlyCEOCanResign");
    if (!call.client->IsDocked())
        throw UserError("CrpCanNotChangeCorpInSpace");

    std::vector<uint32> candidates;
    sCorpMgr.GetPotentialCEOs(m_corpID, candidates);

    if (newCeoID.has_value()) {
        uint32 newID = newCeoID.value()->value();
        if (std::find(candidates.begin(), candidates.end(), newID) == candidates.end())
            throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("That member cannot take over as CEO."));
        sCorpMgr.ChangeCEO(m_corpID, newID, false);
        return nullptr;
    }

    if (!candidates.empty()) {
        PyList* list = new PyList();
        for (auto cur : candidates)
            list->AddItem(new PyInt(cur));
        return list;
    }

    // "all your corporation's assets will be acquired by the local authorities"
    if (call.client->GetAllianceID() != 0)
        throw UserError("CanNotDestroyCorpInAlliance");
    sCorpMgr.LiquidateCorp(m_corpID, charID);
    return nullptr;
}



