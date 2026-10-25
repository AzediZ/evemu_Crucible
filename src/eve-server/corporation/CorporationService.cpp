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
    Author:        Zhur
*/

#include "eve-server.h"


#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "EVE_Mail.h"
#include "EntityList.h"
#include "corporation/CorpMgr.h"
#include "corporation/CorporationService.h"
#include "station/StationDataMgr.h"

CorporationService::CorporationService() :
    Service("corporationSvc")
{
    this->Add("GetFactionInfo", &CorporationService::GetFactionInfo);
    this->Add("GetCorpInfo", &CorporationService::GetCorpInfo);
    this->Add("GetNPCDivisions", &CorporationService::GetNPCDivisions);
    this->Add("GetEmploymentRecord", &CorporationService::GetEmploymentRecord);
    this->Add("GetRecruitmentAdsByCriteria", &CorporationService::GetRecruitmentAdsByCriteria);
    this->Add("GetRecruitmentAdRegistryData", &CorporationService::GetRecruitmentAdRegistryData);
    this->Add("GetRecruitmentAdsForCorporation", &CorporationService::GetRecruitmentAdsForCorporation);

    this->Add("GetMedalsReceived", &CorporationService::GetMedalsReceived);
    this->Add("GetMedalDetails", &CorporationService::GetMedalDetails);
    this->Add("GetAllCorpMedals", &CorporationService::GetAllCorpMedals);
    this->Add("CreateMedal", &CorporationService::CreateMedal);
    this->Add("GiveMedalToCharacters", &CorporationService::GiveMedalToCharacters);
    this->Add("SetMedalStatus", &CorporationService::SetMedalStatus);
    this->Add("GetMedalStatuses", &CorporationService::GetMedalStatuses);
    this->Add("GetRecipientsOfMedal", &CorporationService::GetRecipientsOfMedal);
}

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

PyResult CorporationService::GetNPCDivisions(PyCallArgs &call)
{
    return sDataMgr.GetNPCDivisions();
}

PyResult CorporationService::GetEmploymentRecord(PyCallArgs &call, PyInt* characterID) {
    return m_db.GetEmploymentRecord(characterID->value());
}

PyResult CorporationService::GetFactionInfo(PyCallArgs &call) {
    /*self.factionIDbyNPCCorpID, self.factionRegions, self.factionConstellations, self.factionSolarSystems,
     * self.factionRaces, self.factionStationCount, self.factionSolarSystemCount, self.npcCorpInfo = sm.RemoteSvc('corporationSvc').GetFactionInfo()
     *        for corpID, factionID in self.factionIDbyNPCCorpID.iteritems():
     *            if factionID not in self.corpsByFactionID:
     *                self.corpsByFactionID[factionID] = []
     *            if corpID not in self.corpsByFactionID[factionID]:
     *                self.corpsByFactionID[factionID].append(corpID)
     *
     *        owners = {}
     *        for k, v in self.factionIDbyNPCCorpID.iteritems():
     *            owners[k] = 0
     *            owners[v] = 0
     */

    return sDataMgr.GetFactionInfo();
}

// this wants corp market info
PyResult CorporationService::GetCorpInfo(PyCallArgs &call, PyInt* corporationID) {
    return m_db.GetMktInfo(corporationID->value());
}

PyResult CorporationService::GetRecruitmentAdRegistryData(PyCallArgs& call)
{   // working
    _log(CORP__CALL, "CorporationService::Handle_GetRecruitmentAdRegistryData()");
    call.Dump(CORP__CALL_DUMP);

    PyDict* dict = new PyDict();
        dict->SetItemString("types", m_db.GetAdTypeData());
        dict->SetItemString("groups", m_db.GetAdGroupData());
    PyObject* args = new PyObject("util.KeyVal", dict);
    if (is_log_enabled(CORP__RSP_DUMP))
        args->Dump(CORP__RSP_DUMP, "");
    return args;
}

PyResult CorporationService::GetRecruitmentAdsByCriteria(PyCallArgs& call, PyInt* typeMask, PyBool* inAlliance, std::optional<PyInt*> minMembers, std::optional<PyInt*> maxMembers)
{    //   return sm.RemoteSvc('corporationSvc').GetRecruitmentAdsByCriteria(typeMask, isInAlliance, minMembers, maxMembers)
    _log(CORP__CALL, "CorporationService::Handle_GetRecruitmentAdsByCriteria()");
    call.Dump(CORP__CALL_DUMP);

    return CorporationDB::GetAdRegistryData(
        typeMask->value(), inAlliance->value(),
        minMembers.has_value () ? minMembers.value ()->value() : 0,
        maxMembers.has_value () ? maxMembers.value ()->value() : 0);
}

PyResult CorporationService::GetRecruitmentAdsForCorporation(PyCallArgs& call)
{
    // recruitments = self.GetCorpRegistry().GetRecruitmentAdsForCorporation()
    _log(CORP__CALL, "CorporationService::Handle_GetRecruitmentAdsForCorporation()");
    call.Dump(CORP__CALL_DUMP);

    // the caller's corp's own
    return CorporationDB::GetAdRegistryData(0, false, 0, 0, call.client->GetCorporationID());
}


/**     ***********************************************************************
 * @note   these below are partially coded
 */

PyResult CorporationService::CreateMedal(PyCallArgs &call, PyWString* name, PyWString* description, PyList* medalData) {
    // destroy = sm.StartService('medals').CreateMedal(mName, mDesc, cMedalData)
    //  destroy = true will close window

    if (name->size() > 30)
        throw UserError ("MedalNameTooLong");
    if (description->size() < 3)
        throw UserError ("MedalDescriptionTooShort");
    if (description->size() > 150)
        throw UserError ("MedalDescriptionTooLong");

    //take the money, send wallet blink event record the transaction in corp journal.  paid to SCC
    std::string reason = "DESC: Medal Creation by ";
    reason += call.client->GetName();
    reason += " in ";   // just extra shit.  this will show in tooltip.  ;)
    reason += stDataMgr.GetStationName(call.client->GetStationID());
    AccountService::TransferFunds(
        call.client->GetCorporationID(),
        corpSCC,
        sConfig.rates.medalCreateCost,
        reason.c_str(),
        Journal::EntryType::MedalCreation,
        call.client->GetStationID(),
        Account::KeyType::Cash
    );

    // not sure of the requirements for these errors....
     // MedalNameInvalid, MedalNameInvalid2, MedalDescriptionInvalid2

    uint16 medalID = m_db.CreateMedal(call.client->GetCorporationID(), call.client->GetCharacterID(), name->content (), description->content ());
    if (medalID == 0) {
        // error on save.
        call.client->SendErrorMsg("Error when saving Medal Data.  Ref: ServerError xxxxx");
        return nullptr;
    }

    PyList* list(nullptr);
    std::vector<Corp::MedalData> dataList;
    //if (is_log_enabled(CORP__PKT_TRACE))
    //    args.data->Dump(CORP__PKT_TRACE, "");
    for (PyList::const_iterator itr = medalData->begin(); itr != medalData->end(); ++itr) {
        list = (*itr)->AsList();
        if (list == nullptr)
            continue;
        // type:  1 = ribbon, 2 = medal
        Corp::MedalData data = Corp::MedalData();
            data.part = PyRep::IntegerValue(list->GetItem(0));
            data.graphic = PyRep::StringContent(list->GetItem(1));
            data.color = PyRep::IntegerValue(list->GetItem(2));
        dataList.push_back( data );
    }

    // this will either save data or it wont...
    m_db.SaveMedalData(medalID, dataList);

    return PyStatic.NewFalse();
}

PyResult CorporationService::GetMedalsReceived(PyCallArgs &call, PyInt* characterID) {
    // this should be cached
    //  medalInfo, medalGraphics = sm.StartService('medals').GetMedalsReceived(charID)
    /*
     * 13:25:05 [CorpCallDump]   Call Arguments:
     * 13:25:05 [CorpCallDump]      Tuple: 1 elements
     * 13:25:05 [CorpCallDump]       [ 0]    Integer: 90000000
     */
    _log(CORP__CALL, "CorporationService::Handle_GetMedalsReceived()");
    call.Dump(CORP__CALL_DUMP);

    PyTuple* res = new PyTuple(2);
        res->SetItem(0, m_db.GetMedalsReceived(characterID->value()));
        res->SetItem(1, m_db.GetMedalsReceivedDetails(characterID->value()));
    if (is_log_enabled(CORP__RSP_DUMP))
        res->Dump(CORP__RSP_DUMP, "");
    return res;
}

PyResult CorporationService::GetMedalDetails(PyCallArgs &call, PyInt* medalID)
{   // working
    _log(CORP__CALL, "CorporationService::Handle_GetMedalDetails()");
    call.Dump(CORP__CALL_DUMP);

    return m_db.GetMedalDetails(medalID->value());
}

PyResult CorporationService::GetAllCorpMedals(PyCallArgs& call, PyInt* corporationID)
{   //working
    // medals, medalDetails = sm.RemoteSvc('corporationSvc').GetAllCorpMedals(corpID)
    _log(CORP__CALL, "CorporationService::Handle_GetAllCorpMedals()");
    call.Dump(CORP__CALL_DUMP);

    PyTuple* res = new PyTuple(2);
        res->SetItem(0, m_db.GetAllCorpMedals(corporationID->value()));
        res->SetItem(1, m_db.GetCorpMedalData(corporationID->value()));
    if (is_log_enabled(CORP__RSP_DUMP))
        res->Dump(CORP__RSP_DUMP, "");
    return res;
}

PyResult CorporationService::GetRecipientsOfMedal(PyCallArgs &call, PyInt* medalID)
{
    //   recipients = sm.RemoteSvc('corporationSvc').GetRecipientsOfMedal(medalID)
    //          called from GetMedalSubContent in corp_ui_member_deco
    _log(CORP__CALL, "CorporationService::Handle_GetRecipientsOfMedal()");
    call.Dump(CORP__CALL_DUMP);

    /*
     *     issuerID = recipients.issuerID
     *     date = recipients.date
     *     statusID, statusName = self.GetStatus(recipients.status)
     *     reason = recipients.reason
     */
    return m_db.GetRecipientsOfMedal(medalID->value());
}

PyResult CorporationService::GiveMedalToCharacters(PyCallArgs &call, PyInt* medalID, PyList* recipientIDs, PyWString* reason) {
    //  sm.RemoteSvc('corporationSvc').GiveMedalToCharacters(medalID, recipientID, reason)
    /*
     * 13:24:32 [CorpCallDump]   Call Arguments:
     * 13:24:32 [CorpCallDump]      Tuple: 3 elements
     * 13:24:32 [CorpCallDump]       [ 0]    Integer: 8
     * 13:24:32 [CorpCallDump]       [ 1]   List: 1 elements
     * 13:24:32 [CorpCallDump]       [ 1]   [ 0]    Integer: 90000000
     * 13:24:32 [CorpCallDump]       [ 2]    WString: 'testing this shit'
     */
    _log(CORP__CALL, "CorporationService::Handle_GiveMedalToCharacters()");
    call.Dump(CORP__CALL_DUMP);

    // can award one medal to multiple chars at once, at 5m isk per award
    uint32 corpID = call.client->GetCorporationID();
    std::vector< uint32 > charVec;
    for (PyList::const_iterator itr = recipientIDs->begin(); itr != recipientIDs->end(); ++itr)
        if (IsCharacterID(PyRep::IntegerValue(*itr)))
            charVec.push_back(PyRep::IntegerValue(*itr));
    if (charVec.empty())
        return nullptr;

    double cost = sConfig.rates.medalAwardCost;
    cost *= charVec.size();

    //take the money, send wallet blink event record the transaction in corp journal.  paid to SCC
    std::string finalReason = "DESC: Awarding Medal by ";
    finalReason += call.client->GetName();
    AccountService::TransferFunds(
        corpID,
        corpSCC,
        cost,
        finalReason,
        Journal::EntryType::MedalIssuing
    );

    m_db.GiveMedalToCharacters(call.client->GetCharacterID(), corpID, medalID->value(), charVec, reason->content());

    // CharMedal {medalID, corpID, reason} and OnMedalIssued() to the recipients, OnCorporationMedalAdded(medalID) to the corp
    for (auto cur : charVec) {
        PyDict* data = new PyDict();
            data->SetItemString("medalID", new PyInt(medalID->value()));
            data->SetItemString("corpID", new PyInt(corpID));
            data->SetItemString("reason", new PyWString(reason->content()));
        sCorpMgr.NotifyChar(cur, Notify::Types::CharMedal, call.client->GetCharacterID(), data);
        Client* pClient = sEntityList.FindClientByCharID(cur);
        if (pClient != nullptr) {
            PyTuple* payload = new PyTuple(0);
            pClient->SendNotification("OnMedalIssued", "charid", &payload);
        }
    }
    PyTuple* payload = new PyTuple(1);
        payload->SetItem(0, new PyInt(medalID->value()));
    sEntityList.CorpNotify(corpID, Notify::Types::CorpNews, "OnCorporationMedalAdded", "corpid", payload);

    return nullptr;
}

PyResult CorporationService::GetMedalStatuses(PyCallArgs &call)
{
    //  return sm.RemoteSvc('corporationSvc').GetMedalStatuses()
    //  statusID, statusName = self.GetStatus(theyareallthesame.status)
    _log(CORP__CALL, "CorporationService::Handle_GetMedalStatuses()");
    call.Dump(CORP__CALL_DUMP);

    //  cant find any other references to this, so it's hacked in the db call
    // statusID, statusName
    PyRep* res = m_db.GetMedalStatuses();
    if (is_log_enabled(CORP__RSP_DUMP))
        res->Dump(CORP__RSP_DUMP, "");
    return res;
}


/**     ***********************************************************************
 * @note   these do absolutely nothing at this time....
 */

PyResult CorporationService::SetMedalStatus(PyCallArgs &call, PyDict* newStatus)
{
    //  sm.RemoteSvc('corporationSvc').SetMedalStatus(statusdict)
    //    this is called from char PD for setting view permissions on received medals
    /*
     * 02:24:28 [CorpCall] CorporationService::Handle_SetMedalStatus()
     * 02:24:28 [CorpCallDump]   Call Arguments:
     * 02:24:28 [CorpCallDump]      Tuple: 1 elements
     * 02:24:28 [CorpCallDump]       [ 0]  Dictionary: 1 entries
     * 02:24:28 [CorpCallDump]       [ 0]   [ 0]   Key:    Integer: 1   << medalID
     * 02:24:28 [CorpCallDump]       [ 0]   [ 0] Value:    Integer: 3   << public
     *
     * 02:36:18 [CorpCall] CorporationService::Handle_SetMedalStatus()
     * 02:36:18 [CorpCallDump]   Call Arguments:
     * 02:36:18 [CorpCallDump]      Tuple: 1 elements
     * 02:36:18 [CorpCallDump]       [ 0]  Dictionary: 1 entries
     * 02:36:18 [CorpCallDump]       [ 0]   [ 0]   Key:    Integer: 1   << medalID
     * 02:36:18 [CorpCallDump]       [ 0]   [ 0] Value:    Integer: 1   << remove
     *
     */
    _log(CORP__CALL, "CorporationService::Handle_SetMedalStatus()");
    call.Dump(CORP__CALL_DUMP);

    // {medalID: status}  crpMedalStatus:  1 = remove the award, 2 = private, 3 = public
    uint32 charID = call.client->GetCharacterID();
    for (PyDict::const_iterator itr = newStatus->begin(); itr != newStatus->end(); ++itr) {
        int32 medalID = PyRep::IntegerValue(itr->first);
        uint8 status = PyRep::IntegerValue(itr->second);
        if (status == 1) {
            m_db.RemoveMedal(charID, medalID);
        } else if ((status == 2) or (status == 3)) {
            m_db.SetMedalStatus(charID, medalID, status);
        }
    }

    PyTuple* payload = new PyTuple(0);
    call.client->SendNotification("OnMedalStatusChanged", "charid", &payload);
    return nullptr;
}

