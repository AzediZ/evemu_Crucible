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
    Rewrite:    Allan
*/

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

#include "eve-server.h"
#include "EVE_Station.h"



#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "chat/LSCService.h"
#include "corporation/CorpMgr.h"
#include "corporation/ItemLockdown.h"
#include "station/StationOffice.h"
#include "corporation/CorpStationMgr.h"
#include "station/Station.h"
#include "station/StationDataMgr.h"
#include "station/StationDB.h"
#include "services/ServiceManager.h"
#include "character/Character.h"
#include "inventory/AttributeEnum.h"

// the client's const.costCloneContract (AskAcceptCloneContractCost shows it), so not configurable  (F15)
static constexpr double CloneContractCost = 5600.0;

CorpStationMgr::CorpStationMgr(EVEServiceManager& mgr) :
    BindableService("corpStationMgr", mgr)
{
    this->Add("GetStationServiceStates", &CorpStationMgr::GetStationServiceStates);
    this->Add("GetImprovementStaticData", &CorpStationMgr::GetImprovementStaticData);
}

BoundDispatcher* CorpStationMgr::BindObject(Client *client, PyRep* bindParameters) {
    if (!bindParameters->IsInt()) {
        codelog(SERVICE__ERROR, "%s Service: invalid bind argument type %s", GetName().c_str(), bindParameters->TypeString());
        return nullptr;
    }

    uint32_t stationID = bindParameters->AsInt ()->value ();

    // ensure the player is in the given station
    if (client->GetStationID2() != stationID)
        return nullptr;

    auto it = this->m_instances.find (stationID);

    if (it != this->m_instances.end ())
        return it->second;

    CorpStationMgrIMBound* bound = new CorpStationMgrIMBound(this->GetServiceManager(), *this, m_db, stationID);

    this->m_instances.insert_or_assign (stationID, bound);

    return bound;
}

void CorpStationMgr::BoundReleased (CorpStationMgrIMBound* bound) {
    auto it = this->m_instances.find (bound->GetStationID());

    if (it == this->m_instances.end ())
        return;

    this->m_instances.erase (it);
}

PyResult CorpStationMgr::GetImprovementStaticData(PyCallArgs& call)
{
    _log(CORP__CALL, "CorpStationMgr::Handle_GetImprovementStaticData()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::StationDB::GetOutpostImprovementStaticData(res);

    DBResultRow row;

    DBRowDescriptor* header = new DBRowDescriptor();
    header->AddColumn("typeID", DBTYPE_I4);
    header->AddColumn("raceID", DBTYPE_I4);
    header->AddColumn("requiredAssemblyLineTypeID", DBTYPE_I4);
    header->AddColumn("requiredImprovementTypeID", DBTYPE_I4);

    CRowSet* rowset = new CRowSet(&header);

    while (res.GetRow(row)) {
        PyPackedRow* newRow = rowset->NewRow();
        newRow->SetField("typeID", new PyInt(row.GetInt(0)));
        newRow->SetField("raceID", new PyInt(row.GetInt(1)));
        uint32 asmLine = row.GetInt(2);
        if (asmLine == 0) {
            newRow->SetField("requiredAssemblyLineTypeID", new PyNone());
        }
        else {
            newRow->SetField("requiredAssemblyLineTypeID", new PyInt(asmLine));
        }
        uint32 improvement = row.GetInt(3);
        if (improvement == 0) {
            newRow->SetField("requiredImprovementTypeID", new PyNone());
        }
        else {
            newRow->SetField("requiredImprovementTypeID", new PyInt(improvement));
        }
    }

    PyDict* dict = new PyDict();
    dict->SetItemString("improvementTypes", rowset);

    return new PyObject("util.KeyVal", dict);
}

PyResult CorpStationMgr::GetStationServiceStates(PyCallArgs& call)
{
    _log(CORP__CALL, "CorpStationMgr::Handle_GetStationServiceStates()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::GetStationServiceStates(call.client->GetLocationID(), res);

    return DBResultToIntRowDict(res, 2);
}

CorpStationMgrIMBound::CorpStationMgrIMBound(EVEServiceManager& mgr, CorpStationMgr& parent, CorporationDB& db, uint32 station_id) :
    EVEBoundObject(mgr, parent),
    m_db(db),
    m_stationID(station_id)
{
    this->Add("GetCorporateStationInfo", &CorpStationMgrIMBound::GetCorporateStationInfo);
    this->Add("DoStandingCheckForStationService", &CorpStationMgrIMBound::DoStandingCheckForStationService);
    this->Add("GetPotentialHomeStations", &CorpStationMgrIMBound::GetPotentialHomeStations);
    this->Add("SetHomeStation", &CorpStationMgrIMBound::SetHomeStation);
    this->Add("SetCloneTypeID", &CorpStationMgrIMBound::SetCloneTypeID);
    this->Add("GetQuoteForRentingAnOffice", &CorpStationMgrIMBound::GetQuoteForRentingAnOffice);
    this->Add("GetNumberOfUnrentedOffices", &CorpStationMgrIMBound::GetNumberOfUnrentedOffices);
    this->Add("MoveCorpHQHere", &CorpStationMgrIMBound::MoveCorpHQHere);
    this->Add("RentOffice", &CorpStationMgrIMBound::RentOffice);
    this->Add("CancelRentOfOffice", &CorpStationMgrIMBound::CancelRentOfOffice);
    //testing
    this->Add("GetStationOffices", &CorpStationMgrIMBound::GetStationOffices);
    this->Add("GetCorporateStationOffice", &CorpStationMgrIMBound::GetCorporateStationOffice);
    this->Add("DoesPlayersCorpHaveJunkAtStation", &CorpStationMgrIMBound::DoesPlayersCorpHaveJunkAtStation);
    this->Add("GetQuoteForGettingCorpJunkBack", &CorpStationMgrIMBound::GetQuoteForGettingCorpJunkBack);
    this->Add("PayForReturnOfCorpJunk", &CorpStationMgrIMBound::PayForReturnOfCorpJunk);
    this->Add("GetStationServiceIdentifiers", &CorpStationMgrIMBound::GetStationServiceIdentifiers);
    this->Add("GetStationDetails", &CorpStationMgrIMBound::GetStationDetails);
    this->Add("GetStationServiceAccessRule", &CorpStationMgrIMBound::GetStationServiceAccessRule);
    this->Add("GetStationManagementServiceCostModifiers", &CorpStationMgrIMBound::GetStationManagementServiceCostModifiers);
    this->Add("GetRentableItems", &CorpStationMgrIMBound::GetRentableItems);
    this->Add("GetOwnerIDsOfClonesAtStation", &CorpStationMgrIMBound::GetOwnerIDsOfClonesAtStation);
    this->Add("GetStationImprovements", &CorpStationMgrIMBound::GetStationImprovements);

    pStationItem = sEntityList.GetStationByID(station_id).get();
}

PyResult CorpStationMgrIMBound::GetStationOffices(PyCallArgs& call)
{
    return pStationItem->GetOffices();
}

PyResult CorpStationMgrIMBound::GetNumberOfUnrentedOffices(PyCallArgs &call)
{
    return new PyInt(pStationItem->GetAvalibleOfficeCount());
}

PyResult CorpStationMgrIMBound::GetQuoteForRentingAnOffice(PyCallArgs &call)
{
    return new PyLong(pStationItem->GetOfficeRentalFee());
}

// cannot find this call in client code
PyResult CorpStationMgrIMBound::GetCorporateStationInfo(PyCallArgs &call) {
    // is this right?
    /* returns:
     *  list(
     *      eveowners:
     *          rowset: ownerID,ownerName,typeID
     *      corporations:
     *          rowset:corporationID,corporationName,description,shares,graphicID,memberCount,ceoID,stationID,raceID,corporationType,creatorID,hasPlayerPersonnelManager,tickerName,sendCharTerminationMessage,shape1,shape2,shape3,color1,color2,color3,typeface,memberLimit,allowedMemberRaceIDs,url,taxRate,minimumJoinStanding,division1,division2,division3,division4,division5,division6,division7,allianceID
     *      offices (may be None):
     *          rowset: corporationID,itemID,officeFolderID
     *      )
     */

    PyList *list = new PyList();
        list->AddItem(m_db.ListStationOwners(m_stationID));
        list->AddItem(m_db.ListStationCorps(m_stationID));
        list->AddItem(StationDB::GetOffices(m_stationID));/*m_db.ListStationOffices*/
    return list;
}

PyResult CorpStationMgrIMBound::SetCloneTypeID(PyCallArgs &call, PyInt* cloneTypeID) {
    // medical window:  corpStationMgr.SetCloneTypeID(typeID).  the client lists only better clones of groupClone  (F15)
    Client* pClient(call.client);
    if (!pClient->IsDocked())
        throw CustomError("You must be docked to buy a clone.");
    StationData stData = StationData();
    stDataMgr.GetStationData(pClient->GetStationID(), stData);
    if ((stData.serviceMask & Station::Cloning) == 0)
        throw CustomError("This station does not offer cloning services.");
    const ItemType* pType(sItemFactory.GetType(cloneTypeID->value()));
    if ((pType == nullptr) or (pType->groupID() != EVEDB::invGroups::Clone))
        throw CustomError("That is not a clone.");
    CharacterDB c_db;
    uint32 oldTypeID(0);
    c_db.GetActiveCloneType(pClient->GetCharacterID(), oldTypeID);
    const ItemType* pOld(sItemFactory.GetType(oldTypeID));
    if ((pOld != nullptr) and (pType->GetAttribute(AttrSkillPointsSaved) <= pOld->GetAttribute(AttrSkillPointsSaved)))
        throw UserError("MedicalThisCloneIsWorse");

    //Get cost of clone  (Crucible prices, see the F15 clones migration)
    double cost = m_db.GetCloneTypeCostByID(cloneTypeID->value());

    //take the money, send wallet blink event record the transaction in their journal.
    std::string reason = "DESC: Updating Clone in ";
    // make config option for station name or system name here?
    reason += pClient->GetSystemName();
    reason += " at ";
    reason += stDataMgr.GetStationName(pClient->GetStationID());

    // paid to the station's owner
    AccountService::TransferFunds(
        pClient->GetCharacterID(),
        stData.corporationID,
        cost,
        reason.c_str(),
        Journal::EntryType::CloneActivation,
        pClient->GetStationID(),
        Account::KeyType::Cash
    );

    //update type of clone
    c_db.ChangeCloneType(pClient->GetCharacterID(), cloneTypeID->value());

    // charactersheet OnCloneTypeUpdated(newCloneType)
    PyTuple* tmp = new PyTuple(1);
        tmp->SetItem(0, new PyInt(cloneTypeID->value()));
    pClient->SendNotification("OnCloneTypeUpdated", "charid", &tmp, false);
    return PyStatic.NewNone();
}

PyResult CorpStationMgrIMBound::RentOffice(PyCallArgs &call, PyInt* amount) {
    // lobby.RentOffice:  AskPayOfficeRentalFee {cost, duration} then RentOffice(cost);  returns the officeID
    Client* pClient = call.client;
    uint32 corpID = pClient->GetCorporationID();
    if (!IsPlayerCorp(corpID))
        throw CustomError("Only player corporations can rent offices.");
    if (!CorpMgr::HasRole(pClient->GetCorpRole(), Corp::Role::CanRentOffice))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You need the Rent Office role to rent an office."));

    // see if corp has office in station already.
    if (pStationItem->GetOfficeID(corpID))
        throw UserError ("RentingYouHaveAnOfficeHere");

    // this may not be needed, as rental fee is queried immediately prior to this call
    if (amount->value() != pStationItem->GetOfficeRentalFee())
        _log(CORP__WARNING, "RentOffice() - Was quoted %i but station reports %u for office rental at %s", \
            amount->value(), pStationItem->GetOfficeRentalFee(), pStationItem->name());

    // the first 30 days are paid now (throws NotEnoughMoneyCorp)
    std::string reason = "Office Rental at ";
    reason += pStationItem->itemName();
    reason += " by ";
    reason += pClient->GetCharName();
    AccountService::TransferFunds(corpID, pStationItem->GetOwnerID(), pStationItem->GetOfficeRentalFee(), reason.c_str(), Journal::EntryType::OfficeRentalFee, m_stationID);

    // renting where the corp's property is impounded gives that office (and its contents) back
    uint32 impoundedID = StationDB::GetCorpOffice(corpID, m_stationID, true);
    if (impoundedID != 0) {
        sCorpMgr.ReactivateOffice(impoundedID);
        return new PyInt(impoundedID);
    }

    OfficeData odata = OfficeData();
        odata.ticker = pClient->GetChar()->corpTicker();
        odata.corporationID = corpID;
        odata.expiryTime = Win32TimeNow() + 30 * EvE::Time::Day;
        odata.lockDown = false;
        odata.publiclyAvailable = true;
        odata.rentalFee = pStationItem->GetOfficeRentalFee();
        odata.typeID = 27;  // office typeID
    pStationItem->RentOffice(odata);

    if (!odata.officeID) {
        codelog(CORP__ERROR, "%s: Error at renting a new office", pClient->GetName());
        return nullptr;
    }

    // "the corporation will immediately receive a bill for the next 30 days"
    if (sConfig.corp.BillsEnabled)
        sCorpMgr.IssueRentBill(odata.officeID);

    // This has to be sent to everyone in the station
    OfficeAttributeUpdate change;
        change.oldOfficeFolderID = PyStatic.NewNone();
        change.oldOfficeID = PyStatic.NewNone();
        change.oldStationID = PyStatic.NewNone();
        change.oldTypeID = PyStatic.NewNone();
        change.newOfficeFolderID = odata.folderID;
        change.newOfficeID = odata.officeID;
        change.newStationID = odata.stationID;
        change.newTypeID = odata.typeID;
    OnObjectPublicAttributesUpdated update;
        update.notificationParams = new PyDict();
        update.realRowCount = 1; // what is this?
        update.bindID = this->GetIDString ();
        update.changePKIndexValue = odata.stationID; // primary key
        update.changes = change.Encode();
    PyTuple* payload = update.Encode();
    std::vector<Client*> cVec;
    cVec.clear();
    pStationItem->GetGuestList(cVec);
    for (auto cur : cVec) {
        PyIncRef(payload);
        cur->SendNotification("OnObjectPublicAttributesUpdated", "stationid", &payload, false);
    }
    PyDecRef( payload );

    // returns officeID
    return new PyInt(odata.officeID);
}

PyResult CorpStationMgrIMBound::GetCorporateStationOffice(PyCallArgs &call) {
  /*
   *        if not session.stationid2:
   *            return
   *        if self.itemIDOfficeFolderIDByCorporationID is None:
   *            self.itemIDOfficeFolderIDByCorporationID = util.IndexRowset(['corporationID', 'itemID', 'officeFolderID'], [], 'corporationID')
   *            corpStationMgr = self.GetCorpStationManager()
   *            corpStationMgr.Bind()
   *            offices = corpStationMgr.GetCorporateStationOffice()
   *            for office in offices:
   *                self.itemIDOfficeFolderIDByCorporationID[office.corporationID] = [office.corporationID, office.itemID, office.officeFolderID]
   */
  // this gets all corp offices in current station
    return pStationItem->GetOffices();
}

PyResult CorpStationMgrIMBound::MoveCorpHQHere(PyCallArgs &call)
{
    if (call.client->GetCorpHQ() == m_stationID)
        throw UserError ("CorpHQIsAtThisStation");

    uint32 corpID = call.client->GetCorporationID();
    if (!IsPlayerCorp(corpID) or ((call.client->GetRolesAtAll() & Corp::Role::Director) != Corp::Role::Director))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Only the CEO and directors can move the corporation's headquarters."));

    uint32 oldHQ = call.client->GetCorpHQ();
    CorporationDB::UpdateCorpHQ(corpID, m_stationID);

    PyDict* change = new PyDict();
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(oldHQ));
        tuple->SetItem(1, new PyInt(m_stationID));
    change->SetItemString("stationID", tuple);
    sCorpMgr.BroadcastCorpChange(corpID, change);

    // update all online members with new hq change
    Client* pClient(nullptr);
    std::vector<uint32> ids;
    CorporationDB::GetMemberIDs(corpID, ids, true);
    for (auto cur : ids) {
        pClient = sEntityList.FindClientByCharID(cur);
        if (pClient != nullptr)
            pClient->GetChar()->SetCorpHQ(m_stationID);
    }

    return nullptr;
}

/**     ***********************************************************************
 * @note   these below are partially coded
 */

PyResult CorpStationMgrIMBound::GetPotentialHomeStations(PyCallArgs &call) {
    // this is for station options for xfering clone
    //returns stationID,typeID,serviceMask

    Client* pClient(call.client);
    std::vector<uint32> stVec;
    stVec.push_back(m_stationID);   // current station.  is this right?
    // first char home station always an option
    stVec.push_back(CharacterDB::GetStartingStationByCareer(pClient->GetChar()->careerID()));
    // get corp stations and add to vector
    m_db.GetCorpStations(pClient->GetCorporationID(), stVec);

    // TODO:  not sure how to determine possible stations yet...
    /*  ideas:
     *      get all corp offices.
     *      determine standings of requesting character to specific stations/locations
     *
     *      pc corp members - get corp standings vs. potential stations/corps/locations
     *      npc corp members - get faction standings for potential locations
     *
     *  other possible stations?  test for min/max potential station count?
     */

    PyList* list = new PyList();
    StationData data = StationData();
    for (auto cur : stVec) {
        stDataMgr.GetStationData(cur, data);
        PyDict* dict = new PyDict();
        dict->SetItemString("stationID", new PyInt(cur));
        dict->SetItemString("typeID", new PyInt(data.typeID));
        dict->SetItemString("serviceMask", new PyLong(data.serviceMask));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }

    return list;
}

PyResult CorpStationMgrIMBound::GetQuoteForGettingCorpJunkBack(PyCallArgs &call)
{   //cost = corpStationMgr.GetQuoteForGettingCorpJunkBack()
    //  "an impound fee, which is equivalent to 50% of the cost of the rental price for an office"
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_GetQuoteForGettingCorpJunkBack()");
    call.Dump(CORP__CALL_DUMP);

    return new PyInt(GetJunkReleaseCost());
}

int32 CorpStationMgrIMBound::GetJunkReleaseCost()
{
    return (int32)std::round(pStationItem->GetOfficeRentalFee() * sConfig.corp.JunkReleaseFraction);
}

PyResult CorpStationMgrIMBound::DoesPlayersCorpHaveJunkAtStation(PyCallArgs &call)
{   //if corpStationMgr.DoesPlayersCorpHaveJunkAtStation():
    //  opening the corp hangar without an office:  the impounded property can be paid back
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_DoesPlayersCorpHaveJunkAtStation()");
    call.Dump(CORP__CALL_DUMP);

    uint32 officeID = StationDB::GetCorpOffice(call.client->GetCorporationID(), m_stationID, true);
    if (officeID == 0)
        return PyStatic.NewFalse();
    return new PyBool(StationDB::CountImpoundedItems(officeID) > 0);
}

PyResult CorpStationMgrIMBound::SetHomeStation(PyCallArgs &call, PyInt* newHomeStationID) {
    // sm.GetService('corp').GetCorpStationManager().SetHomeStation(newHomeStationID)
    /** @todo this is once a year on live, unless a new char changes corps.  (F24 owns that limit)
     *  we will need to make other checks when this is called, as i dont think client checks anything.
     */
    Client* pClient(call.client);
    uint32 homeStationID(0);
    CharacterDB::GetCharHomeStation(pClient->GetCharacterID(), homeStationID);
    if (homeStationID == (uint32)newHomeStationID->value())
        throw UserError("MedicalYouAlreadyHaveACloneContractAtThatStation");
    // only the stations GetPotentialHomeStations offers
    std::vector<uint32> stVec;
    stVec.push_back(m_stationID);
    stVec.push_back(CharacterDB::GetStartingStationByCareer(pClient->GetChar()->careerID()));
    m_db.GetCorpStations(pClient->GetCorporationID(), stVec);
    if (std::find(stVec.begin(), stVec.end(), (uint32)newHomeStationID->value()) == stVec.end())
        throw CustomError("You can't move your clone contract to that station.");
    // the client asked AskAcceptCloneContractCost {cost: const.costCloneContract}, paid to the new station's owner  (F15)
    std::string reason("DESC: Clone contract moved to ");
    reason += stDataMgr.GetStationName(newHomeStationID->value());
    AccountService::TransferFunds(pClient->GetCharacterID(), stDataMgr.GetOwnerID(newHomeStationID->value()), CloneContractCost,
            reason.c_str(), Journal::EntryType::CloneTransfer, newHomeStationID->value(), Account::KeyType::Cash);

    pClient->GetChar()->SetBaseID(newHomeStationID->value());
    CharacterDB::ChangeCloneLocation(call.client->GetCharID(), newHomeStationID->value());
    return nullptr;
}


/**     ***********************************************************************
 * @note   these do absolutely nothing at this time....
 */

PyResult CorpStationMgrIMBound::DoStandingCheckForStationService(PyCallArgs &call, PyInt* stationServiceID) {
    // not sure what this actually does...
    //   corpStationMgr.DoStandingCheckForStationService(stationServiceID)
    /*
     * 01:09:09 L CorpStationMgrIMBound::Handle_DoStandingCheckForStationService(): size= 1
     * 01:09:09 [SvcCallDump]   Call Arguments:
     * 01:09:09 [SvcCallDump]       Tuple: 1 elements
     * 01:09:09 [SvcCallDump]         [ 0] Integer field: 4096  (repair)
     * 23:09:41 L Server: DoStandingCheckForStationService call made to
     * 23:09:41 L CorpStationMgrIMBound::Handle_DoStandingCheckForStationService(): size= 1
     * 23:09:41 [SvcCall]   Call Arguments:
     * 23:09:41 [SvcCall]       Tuple: 1 elements
     * 23:09:41 [SvcCall]         [ 0] Integer field: 8192
     * 23:09:41 L Server: DoStandingCheckForStationService call made to
     * 23:09:41 L CorpStationMgrIMBound::Handle_DoStandingCheckForStationService(): size= 1
     * 23:09:41 [SvcCall]   Call Arguments:
     * 23:09:41 [SvcCall]       Tuple: 1 elements
     * 23:09:41 [SvcCall]         [ 0] Integer field: 16384
     * 18:49:22 L CorpStationMgrIMBound::Handle_DoStandingCheckForStationService(): size= 1
     * 18:49:22 [SvcCall]   Call Arguments:
     * 18:49:22 [SvcCall]       Tuple: 1 elements
     * 18:49:22 [SvcCall]         [ 0] Integer field: 65536  (fitting)
     * 01:11:32 L CorpStationMgrIMBound::Handle_DoStandingCheckForStationService(): size= 1
     * 01:11:32 [SvcCallDump]   Call Arguments:
     * 01:11:32 [SvcCallDump]       Tuple: 1 elements
     * 01:11:32 [SvcCallDump]         [ 0] Integer field: 1048576  (insurance)
     */

    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_DoStandingCheckForStationService()");
    call.Dump(CORP__CALL_DUMP);

    // returns None
    return PyStatic.NewNone();
}

PyResult CorpStationMgrIMBound::CancelRentOfOffice(PyCallArgs &call)
{   //  corpStationMgr.CancelRentOfOffice()     crpUnrentOfficeWithContent:  "Unrenting the office will cause the content to be impounded"
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_CancelRentOfOffice()");
    call.Dump(CORP__CALL_DUMP);

    uint32 corpID = call.client->GetCorporationID();
    if (!CorpMgr::HasRole(call.client->GetCorpRole(), Corp::Role::CanRentOffice))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You need the Rent Office role to unrent an office."));

    uint32 officeID = StationDB::GetCorpOffice(corpID, m_stationID, false);
    if (officeID == 0)
        throw CustomError("Your corporation does not rent an office here.");

    // the current period is not refunded;  the open rent bill is cancelled
    sCorpMgr.ExpireOffice(officeID, "");
    return nullptr;
}

PyResult CorpStationMgrIMBound::PayForReturnOfCorpJunk(PyCallArgs &call, PyRep* cost)
{   //    corpStationMgr.PayForReturnOfCorpJunk(cost)     after CrpJunkAcceptCost {cost}
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_PayForReturnOfCorpJunk()");
    call.Dump(CORP__CALL_DUMP);

    uint32 corpID = call.client->GetCorporationID();
    if ((call.client->GetCorpRole() & Corp::Role::Director) != Corp::Role::Director)
        throw UserError("CrpJunkOnlyAvailableToDirector");

    uint32 officeID = StationDB::GetCorpOffice(corpID, m_stationID, true);
    if ((officeID == 0) or (StationDB::CountImpoundedItems(officeID) == 0))
        throw CustomError("Your corporation has no impounded property here.");

    // locked items stay impounded until a vote unlocks them
    StationOfficeRef oRef = sItemFactory.GetOfficeRef(officeID);
    if ((oRef.get() != nullptr) and (oRef->GetMyInventory() != nullptr)) {
        oRef->GetMyInventory()->LoadContents();
        std::vector<InventoryItemRef> items;
        oRef->GetMyInventory()->GetItemsByFlag(flagImpounded, items);
        for (auto cur : items)
            if (ItemLockdown::IsLocked(cur->itemID()))
                throw UserError("CrpJunkContainsLockedItem");
    }

    int32 quote = GetJunkReleaseCost();
    double offered(0);
    if (cost->IsFloat()) {
        offered = PyRep::FloatValue(cost);
    } else if (cost->IsInt() or cost->IsLong()) {
        offered = (double)PyRep::IntegerValue(cost);
    }
    if (offered < quote)
        throw CustomError("The release fee is %i ISK.", quote);

    std::string reason = "Release of impounded property at ";
    reason += pStationItem->itemName();
    AccountService::TransferFunds(corpID, pStationItem->GetOwnerID(), quote, reason.c_str(), Journal::EntryType::ReleaseOfImpoundedProperty,
                                  m_stationID, call.client->GetCorpAccountKey());

    // the property comes back with the office, for a new lease (and its rent bill)
    sCorpMgr.ReactivateOffice(officeID);
    return nullptr;
}

PyResult CorpStationMgrIMBound::GetStationServiceIdentifiers(PyCallArgs &call)
{
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_GetStationServiceIdentifiers()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::GetStationServiceIdentifiers(res);
    // serviceNameID
    return DBResultToCRowset(res);
}

PyResult CorpStationMgrIMBound::GetStationDetails(PyCallArgs &call, PyInt* stationID)
{
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_GetStationDetails()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::GetStationDetails(stationID->value(), res);

    PyDict* dict = new PyDict();
    DBResultRow row;

    while (res.GetRow(row)) {    
        dict->SetItemString("stationName", new PyString(row.GetText(0), row.ColumnLength(0)));
        dict->SetItemString("stationID", new PyInt(row.GetInt(1)));
        dict->SetItemString("orbitID", new PyInt(row.GetInt(2)));
        dict->SetItemString("description", new PyString(row.GetText(3), row.ColumnLength(3)));
        dict->SetItemString("security", new PyFloat(row.GetFloat(4)));
        dict->SetItemString("dockingCostPerVolume", new PyFloat(row.GetFloat(5)));
        dict->SetItemString("officeRentalCost", new PyInt(row.GetInt(6)));
        dict->SetItemString("reprocessingStationsTake", new PyFloat(row.GetFloat(7)));
        dict->SetItemString("reprocessingHangarFlag", new PyInt(row.GetInt(8)));
        dict->SetItemString("corporationID", new PyInt(row.GetInt(9)));
        dict->SetItemString("maxShipVolumeDockable", new PyInt(row.GetInt(10)));
        dict->SetItemString("exitTime", PyStatic.NewNone());
        dict->SetItemString("standingOwnerID", new PyInt(row.GetInt(9)));
        dict->SetItemString("upgradeLevel", new PyInt(row.GetInt(11)));
    }
    return new PyObject("util.KeyVal", dict);
}

PyResult CorpStationMgrIMBound::GetStationServiceAccessRule(PyCallArgs &call, PyInt* stationID, PyInt* serviceID)
{
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_GetStationServiceAccessRule()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::GetStationServiceAccessRule(stationID->value(), serviceID->value(), res);

    PyDict* dict = new PyDict();
    DBResultRow row;

    // If no rule exists for this service, we should return the default
    if (res.GetRowCount() == 0) {
        dict->SetItemString("serviceID", new PyInt(serviceID->value()));
        dict->SetItemString("minimumStanding", new PyFloat(0));
        dict->SetItemString("minimumCharSecurity", new PyFloat(0));
        dict->SetItemString("maximumCharSecurity", new PyFloat(0));
        dict->SetItemString("minimumCorpSecurity", new PyFloat(0));
        dict->SetItemString("maximumCorpSecurity", new PyFloat(0));
    }
    else {
        while (res.GetRow(row)) {    
            dict->SetItemString("serviceID", new PyInt(row.GetInt(0)));
            dict->SetItemString("minimumStanding", new PyFloat(row.GetFloat(2)));
            dict->SetItemString("minimumCharSecurity", new PyFloat(row.GetFloat(3)));
            dict->SetItemString("maximumCharSecurity", new PyFloat(row.GetFloat(4)));
            dict->SetItemString("minimumCorpSecurity", new PyFloat(row.GetFloat(5)));
            dict->SetItemString("maximumCorpSecurity", new PyFloat(row.GetFloat(6)));
        }
    }

    return new PyObject("util.KeyVal", dict);
}

PyResult CorpStationMgrIMBound::GetStationManagementServiceCostModifiers(PyCallArgs &call, PyInt* stationID)
{
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_GetStationManagementServiceCostModifiers()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::GetStationManagementServiceCostModifiers(stationID->value(), res);

    PyDict* dict = new PyDict();
    DBResultRow row;

    DBRowDescriptor *header = new DBRowDescriptor();
    header->AddColumn("serviceID", DBTYPE_I4);
    header->AddColumn("discountPerGoodStandingPoint", DBTYPE_R4);
    header->AddColumn("surchargePerBadStandingPoint", DBTYPE_R4);

    CRowSet *rowset = new CRowSet(&header);

    // If no configuration exists for this service, we should return the default
    if (res.GetRowCount() == 0) {
        StationDB::GetStationServiceIdentifiers(res);
        while (res.GetRow(row)) {
            // Only show services which exist in this station
            uint32 serviceID = row.GetInt(0);
            uint32 serviceMask = stDataMgr.GetStationServiceMask(stationID->value());
            if((serviceMask & serviceID) == serviceID) 
            {
                PyPackedRow *newRow = rowset->NewRow();
                newRow->SetField("serviceID", new PyInt(serviceID));
                newRow->SetField("discountPerGoodStandingPoint", new PyFloat(0));
                newRow->SetField("surchargePerBadStandingPoint", new PyFloat(0));
            }
        }
    }
    else {
        while (res.GetRow(row)) {    
            PyPackedRow *newRow = rowset->NewRow();
            newRow->SetField("serviceID", new PyInt(row.GetInt(0)));
            newRow->SetField("discountPerGoodStandingPoint", new PyFloat(row.GetFloat(1)));
            newRow->SetField("surchargePerBadStandingPoint", new PyFloat(row.GetFloat(2)));
        }
    }

    return rowset;
}

PyResult CorpStationMgrIMBound::GetRentableItems(PyCallArgs &call)
{
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_GetRentableItems()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::GetRentableItems(call.client->GetLocationID() ,res);

    return DBResultToCRowset(res);    
}

PyResult CorpStationMgrIMBound::GetOwnerIDsOfClonesAtStation(PyCallArgs &call, PyInt* corporationID)
{
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_GetOwnerIDsOfClonesAtStation()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::GetOwnerIDsOfClonesAtStation(call.client->GetLocationID(), corporationID->value(), res);

    return DBResultToCRowset(res);    
}

PyResult CorpStationMgrIMBound::GetStationImprovements(PyCallArgs &call)
{
    _log(CORP__CALL, "CorpStationMgrIMBound::Handle_GetStationImprovements()");
    call.Dump(CORP__CALL_DUMP);

    DBQueryResult res;
    StationDB::GetOutpostImprovements(call.client->GetLocationID(), res);

    PyDict* dict = new PyDict();
    DBResultRow row;

    PyRep *imp[6];

    // If no data exists for this station, we should return the default
    if (res.GetRowCount() == 0)
        for (int i = 0; i < 6; i++)
            imp[i] = new PyNone();
    else
        while (res.GetRow(row))
            for (int i = 0; i < 6; i++)
                if (row.GetInt(i) == 0)
                    imp[i] = new PyNone();
                else 
                    imp[i] = new PyInt(row.GetInt(i));

    dict->SetItemString("improvementTier2aTypeID", imp[0]);
    dict->SetItemString("improvementTier3aTypeID", imp[1]);
    dict->SetItemString("improvementTier1bTypeID", imp[2]);
    dict->SetItemString("improvementTier1aTypeID", imp[3]);
    dict->SetItemString("improvementTier2bTypeID", imp[4]);
    dict->SetItemString("improvementTier1cTypeID", imp[5]);

    return new PyObject("util.KeyVal", dict);
}