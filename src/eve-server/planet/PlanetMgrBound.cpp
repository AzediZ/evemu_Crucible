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
    Author:        Reve, Comet0
    Rewrite:    Allan
*/

//work in progress

/*
 * PLANET__ERROR
 * PLANET__WARNING
 * PLANET__MESSAGE
 * PLANET__DEBUG
 * PLANET__INFO
 * PLANET__TRACE
 * PLANET__DUMP
 * PLANET__RES_DUMP
 * PLANET__GC_DUMP
 * PLANET__PKT_TRACE
 * PLANET__DB_ERROR
 * PLANET__DB_WARNING
 */


#include "eve-server.h"



#include "packets/Planet.h"
#include "planet/Colony.h"
#include "planet/Planet.h"
#include "planet/PlanetMgr.h"
#include "planet/PlanetMgrBound.h"
#include "planet/PlanetDataMgr.h"
#include "system/SystemManager.h"

PlanetMgrService::PlanetMgrService(EVEServiceManager &mgr) :
    BindableService("planetMgr", mgr)  /*planetBaseBroker is for dust*/
{
    this->Add("GetPlanet", &PlanetMgrService::GetPlanet);
    this->Add("DeleteLaunch", &PlanetMgrService::DeleteLaunch);
    this->Add("GetPlanetsForChar", &PlanetMgrService::GetPlanetsForChar);
    this->Add("GetMyLaunchesDetails", &PlanetMgrService::GetMyLaunchesDetails);
}

BoundDispatcher* PlanetMgrService::BindObject(Client *client, PyRep* bindParameters) {
    /* sends planetID */
    _log(PLANET__INFO, "PlanetMgrService bind request for:");
    bindParameters->Dump(PLANET__INFO, "    ");
    if (!bindParameters->IsInt()) {
        _log(SERVICE__ERROR, "%s Service: invalid bind argument type %s", GetName().c_str(), bindParameters->TypeString());
        return nullptr;
    }

    uint32 planetID = bindParameters->AsInt()->value();
    // the colony on a planet is per character, so is the bound object
    std::pair<uint32, uint32> key = std::make_pair(planetID, client->GetCharacterID());
    auto it = this->m_instances.find (key);

    if (it != this->m_instances.end ())
        return it->second;

    StaticData sData = StaticData();
    if (!sDataMgr.GetStaticInfo(planetID, sData) or (sData.systemID == 0)) {
        _log(SERVICE__ERROR, "%s Service: %u is not a planetID", GetName().c_str(), planetID);
        return nullptr;
    }
    SystemManager* pSysMgr = sEntityList.FindOrBootSystem(sData.systemID);
    if (pSysMgr == nullptr) {
        client->SendErrorMsg("system boot failure");
        return nullptr;
    }
    SystemEntity* pSE = pSysMgr->GetSE(sData.itemID);
    if ((pSE == nullptr) or !pSE->IsPlanetSE()) {
        _log(SERVICE__ERROR, "%s Service: itemID is not planetID or planet not found", GetName().c_str());
        return nullptr;
    }

    PlanetMgrBound* bound = new PlanetMgrBound(this->GetServiceManager (), *this, planetID, sData.systemID, client->GetCharacterID());

    this->m_instances.insert_or_assign (key, bound);

    return bound;
}

void PlanetMgrService::BoundReleased (PlanetMgrBound* bound) {
    auto it = this->m_instances.find (bound->GetKey());

    if (it == this->m_instances.end ())
        return;

    this->m_instances.erase (it);
}

PyResult PlanetMgrService::GetPlanetsForChar(PyCallArgs &call) {
    return PlanetDB::GetPlanetsForChar(call.client->GetCharacterID());
}

PyResult PlanetMgrService::GetMyLaunchesDetails(PyCallArgs &call) {
    return PlanetDB::GetMyLaunchesDetails(call.client->GetCharacterID());
}

PyResult PlanetMgrService::GetPlanet(PyCallArgs& call, PyInt* planetID) {
    _log(PLANET__DEBUG, "PlanetMgrService::Handle_GetPlanet() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);

    return nullptr;
}

PyResult PlanetMgrService::DeleteLaunch(PyCallArgs& call, PyInt* launchID) {
    //sm.RemoteSvc('planetMgr').DeleteLaunch(launchID)  -  'Remove' in the journal's launch list.
    // launchID is the container's itemID (GetMyLaunchesDetails).  this only removes the journal entry; a container still in space stays.
    if (PlanetDB::GetLaunchOwner(launchID->value()) != call.client->GetCharacterID())
        return nullptr;

    PlanetDB::DeleteLaunch(launchID->value());
    return nullptr;
}

PlanetMgrBound::PlanetMgrBound (EVEServiceManager& mgr, PlanetMgrService& parent, uint32 planetID, uint32 systemID, uint32 charID) :
    EVEBoundObject(mgr, parent),
    m_planetID(planetID),
    m_systemID(systemID),
    m_charID(charID)
{
    this->Add("GetPlanetInfo", &PlanetMgrBound::GetPlanetInfo);
    this->Add("GetPlanetResourceInfo", &PlanetMgrBound::GetPlanetResourceInfo);
    this->Add("GetCommandPinsForPlanet", &PlanetMgrBound::GetCommandPinsForPlanet);
    this->Add("GetFullNetworkForOwner", &PlanetMgrBound::GetFullNetworkForOwner);
    this->Add("GetExtractorsForPlanet", &PlanetMgrBound::GetExtractorsForPlanet);
    this->Add("GetProgramResultInfo", &PlanetMgrBound::GetProgramResultInfo);
    this->Add("GetResourceData", &PlanetMgrBound::GetResourceData);
    this->Add("UserAbandonPlanet", &PlanetMgrBound::UserAbandonPlanet);
    this->Add("UserLaunchCommodities", &PlanetMgrBound::UserLaunchCommodities);
    this->Add("UserTransferCommodities", &PlanetMgrBound::UserTransferCommodities);
    this->Add("UserUpdateNetwork", &PlanetMgrBound::UserUpdateNetwork);
    /* GM tools */
    this->Add("GMAddCommodity", &PlanetMgrBound::GMAddCommodity);
    this->Add("GMConvertCommandCenter", &PlanetMgrBound::GMConvertCommandCenter);
    this->Add("GMForceInstallProgram", &PlanetMgrBound::GMForceInstallProgram);
    this->Add("GMGetLocalDistributionReport", &PlanetMgrBound::GMGetLocalDistributionReport);
    this->Add("GMGetSynchedServerState", &PlanetMgrBound::GMGetSynchedServerState);
    this->Add("GMRunDepletionSim", &PlanetMgrBound::GMRunDepletionSim);
    this->Add("GMGetCompleteResource", &PlanetMgrBound::GMGetCompleteResource);
    this->Add("GMCreateNuggetLayer", &PlanetMgrBound::GMCreateNuggetLayer);
}

PlanetSE* PlanetMgrBound::GetPlanetSE()
{
    SystemManager* pSysMgr(sEntityList.FindOrBootSystem(m_systemID));
    SystemEntity* pSE(pSysMgr == nullptr ? nullptr : pSysMgr->GetSE(m_planetID));
    if ((pSE == nullptr) or (pSE->GetPlanetSE() == nullptr))
        throw CustomError ("Planet not available");
    return pSE->GetPlanetSE();
}

Colony* PlanetMgrBound::GetColony()
{
    return GetPlanetSE()->GetColony(m_charID);
}

void PlanetMgrBound::CheckCaller(Client* pClient)
{
    if (pClient->GetCharacterID() != m_charID)
        throw CustomError ("This planet view belongs to another character.");
}

PyResult PlanetMgrBound::GetPlanetResourceInfo(PyCallArgs &call) {
    if (!sConfig.cosmic.PIEnabled) {
        call.client->SendErrorMsg("The PI system is currently disabled.");
        return nullptr;
    }

    return GetPlanetSE()->GetPlanetResourceInfo();
}

PyResult PlanetMgrBound::GetPlanetInfo(PyCallArgs &call) {
    if (!sConfig.cosmic.PIEnabled) {
        call.client->SendErrorMsg("The PI system is currently disabled.");
        return nullptr;
    }
    _log(PLANET__DEBUG, "PlanetMgrBound::Handle_GetPlanetInfo() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);
    CheckCaller(call.client);

    PyRep* res = GetPlanetSE()->GetPlanetInfo(GetColony());
        res->Dump(PLANET__RES_DUMP, "    ");

    return res;
}

PyResult PlanetMgrBound::GetExtractorsForPlanet(PyCallArgs &call, PyInt* planetID) {
    return GetPlanetSE()->GetExtractorsForPlanet(m_planetID);
}

PyResult PlanetMgrBound::UserUpdateNetwork(PyCallArgs &call, PyList* commandList) {
    if (!sConfig.cosmic.PIEnabled) {
        call.client->SendErrorMsg("The PI system is currently disabled.");
        return nullptr;
    }
    _log(PLANET__DEBUG, "PlanetMgrBound::Handle_UserUpdateNetwork() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);
    CheckCaller(call.client);

    PlanetSE* pPlanet(GetPlanetSE());
    PlanetMgr mgr(call.client, pPlanet, pPlanet->GetColony(m_charID));
    return mgr.UpdateNetwork(commandList);
}

PyResult PlanetMgrBound::GetProgramResultInfo(PyCallArgs &call, PyRep* ecuID, PyInt* typeID, PyList* heads, PyRep* headRadius) {
    /*
        qtyToDistribute, cycleTime, numCycles = self.remoteHandler.GetProgramResultInfo(pinID, typeID, pin.heads, headRadius)

     * 09:08:39 [PlanetDebug] PlanetMgrBound::Handle_GetProgramResultInfo() size=4
     * 09:08:39 [PlanetCallDump]   Call Arguments:
     * 09:08:39 [PlanetCallDump]       Tuple: 4 elements
     * 09:08:39 [PlanetCallDump]         [ 0] Integer field: 140006400              << ECU itemID
     * 09:08:39 [PlanetCallDump]         [ 1] Integer field: 2268                   << resource typeID
     * 09:08:39 [PlanetCallDump]         [ 2] List: 3 elements                      << heads
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 0] Tuple: 3 elements
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 0]   [ 0] Integer field: 0
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 0]   [ 1] Real field: 0.323109
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 0]   [ 2] Real field: 2.773487
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 1] Tuple: 3 elements
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 1]   [ 0] Integer field: 1
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 1]   [ 1] Real field: 0.344639
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 1]   [ 2] Real field: 2.882588
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 2] Tuple: 3 elements
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 2]   [ 0] Integer field: 2
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 2]   [ 1] Real field: 0.378424
     * 09:08:39 [PlanetCallDump]         [ 2]   [ 2]   [ 2] Real field: 2.820603
     * 09:11:30 [PlanetCallDump]         [ 3] Real field: 0.010000                  << smallest size.  0d1hr
     * 09:12:30 [PlanetCallDump]         [ 3] Real field: 0.018581                  << medium size.    3d
     * 09:13:36 [PlanetCallDump]         [ 3] Real field: 0.050000                  << largest size.  14d

    _log(PLANET__DEBUG, "PlanetMgrBound::Handle_GetProgramResultInfo() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);
     */
    CheckCaller(call.client);
    double radius = (headRadius->IsFloat() ? headRadius->AsFloat()->value() : (double)PyRep::IntegerValue(headRadius));
    return GetColony()->GetProgramResultInfo(ecuID, typeID->value(), heads, radius);
}

PyResult PlanetMgrBound::GetResourceData(PyCallArgs &call, PyObject* info) {
    //_log(PLANET__DEBUG, "PlanetMgrBound::Handle_GetResourceData() size=%lli", call.tuple->size());
    //call.Dump(PLANET__DUMP);
    /*
        inRange, sh = planet.GetResourceData(resourceTypeID)        << check packets for this call --- none.  :(
*/
            /*  this is called by planet view page, by "resource filter" for given typeID
             *     this is dump before dict decoding
             * 12:23:45 [PlanetCallDump]    Dictionary: 8 entries
             * 12:23:45 [PlanetCallDump]      [ 0] Key: String: 'proximity'
             * 12:23:45 [PlanetCallDump]      [ 0] Value: Integer field: 4
             * 12:23:45 [PlanetCallDump]      [ 1] Key: String: 'updateTime'
             * 12:23:45 [PlanetCallDump]      [ 1] Value: Integer field: 0
             * 12:23:45 [PlanetCallDump]      [ 2] Key: String: 'advancedPlanetology'
             * 12:23:45 [PlanetCallDump]      [ 2] Value: Integer field: 0
             * 12:23:45 [PlanetCallDump]      [ 3] Key: String: 'remoteSensing'
             * 12:23:45 [PlanetCallDump]      [ 3] Value: Integer field: 3
             * 12:23:45 [PlanetCallDump]      [ 4] Key: String: 'newBand'
             * 12:23:45 [PlanetCallDump]      [ 4] Value: Integer field: 15
             * 12:23:45 [PlanetCallDump]      [ 5] Key: String: 'planetology'
             * 12:23:45 [PlanetCallDump]      [ 5] Value: Integer field: 0
             * 12:23:45 [PlanetCallDump]      [ 6] Key: String: 'oldBand'
             * 12:23:45 [PlanetCallDump]      [ 6] Value: Integer field: 0
             * 12:23:45 [PlanetCallDump]      [ 7] Key: String: 'resourceTypeID'
             * 12:23:45 [PlanetCallDump]      [ 7] Value: Integer field: 2268
             */


    Call_ResourceDataDict dict;
    PyDict* input = info->arguments()->AsDict();
    //input->Dump(PLANET__DUMP, "   ");
    // not the consuming Decode(&input):  the dict belongs to the call's KeyVal, which frees it
    if (!dict.Decode(input)) {
        codelog(SERVICE__ERROR, "PlanetMgrBound: Failed to decode arguments.");
        return nullptr;
    }

    return GetPlanetSE()->GetResourceData(call.client, dict);
}

//01:52:23 [PlanetDebug] PlanetMgrBound::Handle_UserAbandonPlanet() size=0
PyResult PlanetMgrBound::UserAbandonPlanet(PyCallArgs &call) {
    _log(PLANET__DEBUG, "PlanetMgrBound::Handle_UserAbandonPlanet() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);
    CheckCaller(call.client);

    PlanetSE* pPlanet(GetPlanetSE());
    pPlanet->AbandonColony(call.client, pPlanet->GetColony(m_charID));

    return nullptr;
}

PyResult PlanetMgrBound::UserLaunchCommodities(PyCallArgs &call, PyInt* commandPinID, PyDict* commoditiesToLaunch) {
    /*
            lastLaunchTime = self.remoteHandler.UserLaunchCommodities(commandPinID, commoditiesToLaunch)
            for typeID, qty in commoditiesToLaunch.iteritems():
            */
    _log(PLANET__DEBUG, "PlanetMgrBound::Handle_UserLaunchCommodities() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);
    /* 20:00:35 L PlanetMgrBound: Handle_UserLaunchCommodities() size=2
     * 20:00:35 [PlanetCallDump]   Call Arguments:
     * 20:00:35 [PlanetCallDump]       Tuple: 2 elements
     * 20:00:35 [PlanetCallDump]         [ 0] Integer field: 140000083
     * 20:00:35 [PlanetCallDump]         [ 1] Dictionary: 1 entries
     * 20:00:35 [PlanetCallDump]         [ 1]   [ 0] Key: Integer field: 2268
     * 20:00:35 [PlanetCallDump]         [ 1]   [ 0] Value: Integer field: 1
     * 20:00:35 [PlanetCallDump]   Call Named Arguments:
     * 20:00:35 [PlanetCallDump]     Argument 'machoVersion':
     * 20:00:35 [PlanetCallDump]         Integer field: 1
     *
     *
21:44:05 [PlanetCallDump]    Dictionary: 1 entries
21:44:05 [PlanetCallDump]      [ 0] Key: Integer field: 3645
21:44:05 [PlanetCallDump]      [ 0] Value: Real field: 120.000000   << no clue why this changed to float from int.
eve-server: /usr/local/src/eve/EVEmu/src/eve-common/python/PyRep.h:141: PyInt* PyRep::AsInt(): Assertion `IsInt()' failed.
     */
    //dict->Dump(PLANET__DUMP, "   ");
    CheckCaller(call.client);
    std::map<uint16, uint32> items;
    PyDict::const_iterator itr = commoditiesToLaunch->begin();
    for (; itr != commoditiesToLaunch->end(); ++itr) {
        // quantities may arrive as floats
        int64 qty = itr->second->IsFloat() ? (int64)itr->second->AsFloat()->value() : PyRep::IntegerValue(itr->second);
        if (qty > 0)
            items[PyRep::IntegerValue(itr->first)] = (uint32)qty;
    }

    return GetColony()->LaunchCommodities(call.client, commandPinID->value(), items);
}

PyResult PlanetMgrBound::UserTransferCommodities(PyCallArgs &call, PyList* path, PyDict* commodities) {
    _log(PLANET__DEBUG, "PlanetMgrBound::Handle_UserTransferCommodities() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);
/*
        simTime, sourceRunTime = self.remoteHandler.UserTransferCommodities(path, commodities)
        {{ simTime = time to stop (complete time), sourceRunTime = previous runtime}}
        15:19:15 [PlanetDebug] PlanetMgrBound::Handle_UserTransferCommodities() size=2
        15:19:15 [PlanetCallDump]   Call Arguments:
        15:19:15 [PlanetCallDump]       Tuple: 2 elements
        15:19:15 [PlanetCallDump]         [ 0] List: 5 elements
        15:19:15 [PlanetCallDump]         [ 0]   [ 0] Integer field: 140007050
        15:19:15 [PlanetCallDump]         [ 0]   [ 1] Integer field: 140007049
        15:19:15 [PlanetCallDump]         [ 0]   [ 2] Integer field: 140006983
        15:19:15 [PlanetCallDump]         [ 0]   [ 3] Integer field: 140007002
        15:19:15 [PlanetCallDump]         [ 0]   [ 4] Integer field: 140006971
        15:19:15 [PlanetCallDump]         [ 1] Dictionary: 2 entries
        15:19:15 [PlanetCallDump]         [ 1]   [ 0] Key: Integer field: 2390
        15:19:15 [PlanetCallDump]         [ 1]   [ 0] Value: Integer field: 100
        15:19:15 [PlanetCallDump]         [ 1]   [ 1] Key: Integer field: 2309
        15:19:15 [PlanetCallDump]         [ 1]   [ 1] Value: Integer field: 44400
        */

    CheckCaller(call.client);
    if (path->size() < 2)
        throw UserError("CreateRouteTooShort");
    std::vector<uint32> pinPath;
    for (size_t i = 0; i < path->size(); ++i)
        pinPath.push_back(PyRep::IntegerValueU32(path->GetItem(i)));
    std::map<uint16, uint32> items;
    PyDict::const_iterator itr = commodities->begin();
    for (; itr != commodities->end(); ++itr) {
        int64 qty = itr->second->IsFloat() ? (int64)itr->second->AsFloat()->value() : PyRep::IntegerValue(itr->second);
        if (qty > 0)
            items[PyRep::IntegerValue(itr->first)] = (uint32)qty;
    }

    return GetColony()->TransferCommodities(call.client, pinPath, items);
}


/**     ***********************************************************************
 * @note   these do absolutely nothing at this time....
 */

PyResult PlanetMgrBound::GetCommandPinsForPlanet(PyCallArgs &call, PyInt* planetID) {
    /*  called by "get other character's networks" in planet menu
     * 16:42:42 [PlanetDebug] PlanetMgrBound::Handle_GetCommandPinsForPlanet() size=1
     * 16:42:42 [PlanetCallDump]   Call Arguments:
     * 16:42:42 [PlanetCallDump]       Tuple: 1 elements
     * 16:42:42 [PlanetCallDump]         [ 0] Integer field: 40159411
     */
    _log(PLANET__DEBUG, "PlanetMgrBound::Handle_GetCommandPinsForPlanet() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);

    // {charID: KeyVal(pinID, typeID, ownerID, latitude, longitude)}  (otherPinManager.RenderCommandCentersOfOtherCharacters)
    DBQueryResult res;
    PlanetDB::GetCommandPinsForPlanet(m_planetID, res);
    PyDict* dict = new PyDict();
    DBResultRow row;
    while (res.GetRow(row)) {
        PyDict* args = new PyDict();
            args->SetItemString("pinID", new PyInt(row.GetInt(0)));
            args->SetItemString("typeID", new PyInt(row.GetInt(1)));
            args->SetItemString("ownerID", new PyInt(row.GetInt(2)));
            args->SetItemString("latitude", new PyFloat(row.GetDouble(3)));
            args->SetItemString("longitude", new PyFloat(row.GetDouble(4)));
        dict->SetItem(new PyInt(row.GetInt(2)), new PyObject("util.KeyVal", args));
    }
    return dict;
}

PyResult PlanetMgrBound::GetFullNetworkForOwner(PyCallArgs &call, PyInt* planetID, PyInt* characterID) {
    /*
     */
    _log(PLANET__DEBUG, "PlanetMgrBound::Handle_GetFullNetworkForOwner() size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);

    // pins, links = GetFullNetworkForOwner(planetID, charID)   (otherPinManager.RenderOtherCharactersNetwork)
    //   pins are KeyVal(pinID, typeID, ownerID, latitude, longitude), links are (endpoint1, endpoint2)
    //   other characters' pins are public in the planet view, but only on the bound planet
    DBQueryResult pins, links;
    PlanetDB::GetNetworkForOwner(m_planetID, characterID->value(), pins, links);
    PyList* pinList = new PyList();
    DBResultRow row;
    while (pins.GetRow(row)) {
        PyDict* args = new PyDict();
            args->SetItemString("pinID", new PyInt(row.GetInt(0)));
            args->SetItemString("typeID", new PyInt(row.GetInt(1)));
            args->SetItemString("ownerID", new PyInt(row.GetInt(2)));
            args->SetItemString("latitude", new PyFloat(row.GetDouble(3)));
            args->SetItemString("longitude", new PyFloat(row.GetDouble(4)));
        pinList->AddItem(new PyObject("util.KeyVal", args));
    }
    PyList* linkList = new PyList();
    while (links.GetRow(row)) {
        PyTuple* link = new PyTuple(2);
            link->SetItem(0, new PyInt(row.GetInt(0)));
            link->SetItem(1, new PyInt(row.GetInt(1)));
        linkList->AddItem(link);
    }
    PyTuple* res = new PyTuple(2);
        res->SetItem(0, pinList);
        res->SetItem(1, linkList);
    return res;
}

void PlanetMgrBound::CheckGM(Client* pClient)
{
    if ((pClient->GetAccountRole() & (Acct::Role::GML | Acct::Role::GMH | Acct::Role::WORLDMOD | Acct::Role::PROGRAMMER)) == 0)
        throw CustomError ("This planet command is for GMs only.");
}

void PlanetMgrBound::NotifyOwner(bool numCommandCentersChanged/*false*/)
{
    // planetSvc.OnMajorPlanetStateUpdate(planetID, numCommandCentersChanged):  the client re-reads the planet
    Client* pClient(sEntityList.FindClientByCharID(m_charID));
    if (pClient == nullptr)
        return;
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(m_planetID));
        tuple->SetItem(1, new PyBool(numCommandCentersChanged));
    pClient->SendNotification("OnMajorPlanetStateUpdate", "clientID", &tuple);
}

PyResult PlanetMgrBound::GMAddCommodity(PyCallArgs &call, PyRep* pinID, PyInt* typeID, PyRep* quantity) {
    // GMAddCommodity(pinID, typeID, quantity):  puts commodities into a pin (as much as it takes) and lets them flow on
    CheckGM(call.client);
    if (!pinID->IsInt() and !pinID->IsLong())
        throw CustomError ("Submit the network first.");
    int64 qty = (quantity->IsFloat() ? (int64)quantity->AsFloat()->value() : PyRep::IntegerValue(quantity));
    if (qty < 1)
        return PyStatic.NewNone();
    uint32 added = GetColony()->GMAddCommodity(PyRep::IntegerValueU32(pinID), typeID->value(), (uint32)std::min(qty, (int64)UINT32_MAX));
    _log(PLANET__INFO, "PlanetMgrBound::GMAddCommodity() - %s added %u of %u %s to pin %u", \
            call.client->GetName(), added, (uint32)qty, sPIDataMgr.GetProductName(typeID->value()), PyRep::IntegerValueU32(pinID));
    NotifyOwner();
    return PyStatic.NewNone();
}

PyResult PlanetMgrBound::GMConvertCommandCenter(PyCallArgs &call, PyRep* pinID) {
    // a CCP tool without a known purpose (the GM planet menu offers it for command centers)
    CheckGM(call.client);
    throw CustomError ("Not supported.");
}

PyResult PlanetMgrBound::GMForceInstallProgram(PyCallArgs &call, PyRep* pinID, PyInt* typeID, PyRep* cycleTime, PyRep* lifetimeHours, PyRep* qtyPerCycle, PyRep* radius) {
    /*  ClientPlanet.GMInstallProgram (deposit designer):
     *        if typeID not in resourceInfo or qtyPerCycle < 0 or cycleTime < 10 * SEC or lifetimeHours < 1 or headRadius <= 0.0:
     *            return
     *        self.remoteHandler.GMForceInstallProgram(pinID, typeID, cycleTime, lifetimeHours, qtyPerCycle, headRadius)
     */
    CheckGM(call.client);
    if (!pinID->IsInt() and !pinID->IsLong())
        throw CustomError ("Submit the network first.");
    int64 cycle = PyRep::IntegerValue(cycleTime);
    int64 hours = PyRep::IntegerValue(lifetimeHours);
    int64 qty = PyRep::IntegerValue(qtyPerCycle);
    double headRadius = (radius->IsFloat() ? radius->AsFloat()->value() : (double)PyRep::IntegerValue(radius));
    if (!GetPlanetSE()->IsResourceType(typeID->value()))
        throw CustomError ("That resource is not found on this planet.");
    if ((qty < 0) or (cycle < 10 * EvE::Time::Second) or (hours < 1) or (headRadius <= 0))
        throw CustomError ("Invalid program.");
    GetColony()->GMForceInstallProgram(PyRep::IntegerValueU32(pinID), typeID->value(), cycle, hours * EvE::Time::Hour, (uint32)std::min(qty, (int64)UINT32_MAX), headRadius);
    NotifyOwner();
    return PyStatic.NewNone();
}

PyResult PlanetMgrBound::GMGetLocalDistributionReport(PyCallArgs &call, PyInt* planetID, PyTuple* surfacePoint) {
    // GMGetLocalDistributionReport(planetID, (surfacePoint.theta, surfacePoint.phi)):  {'base':{}, 'quality':{}, 'deplete':{}, 'final':{}, 'raw':{}}
    CheckGM(call.client);
    if (surfacePoint->size() < 2)
        throw CustomError ("Invalid surface point.");
    double theta = PyRep::FloatValue(surfacePoint->GetItem(0));    // longitude
    double phi = PyRep::FloatValue(surfacePoint->GetItem(1));      // latitude
    return GetPlanetSE()->GetLocalDistributionReport(2.0 * M_PI - theta, phi);
}

PyResult PlanetMgrBound::GMGetSynchedServerState(PyCallArgs &call, PyInt* characterID) {
    /*  ClientPlanet.GMVerifySimulation:
     *        simulationDuration, remoteColonyData = self.remoteHandler.GMGetSynchedServerState(session.charid)
     *        simEndTime = remoteColonyData.currentSimTime
     *        colony.RunSimulation(runSimUntil=simEndTime)   ... then compares every pin attribute
     */
    CheckGM(call.client);
    if (characterID->value() != m_charID)
        throw CustomError ("Only the bound character's colony can be verified.");
    Colony* pColony(GetColony());
    double start(GetTimeUSeconds());
    pColony->SimulateToNow();
    int64 duration = (int64)((GetTimeUSeconds() - start) * 10);     // FILETIME units
    PyTuple* res = new PyTuple(2);
        res->SetItem(0, new PyLong(duration));
        res->SetItem(1, pColony->GetColony());
    return res;
}

PyResult PlanetMgrBound::GMRunDepletionSim(PyCallArgs &call, PyRep* typeID, PyObject* info) {
    /*  myPinManager.GMRunDepletionSim:  KeyVal(totalDuration, points=[KeyVal(longitude, latitude, amount, duration, headRadius)])
     *   totalDuration in FILETIME (days), a point's duration in minutes.  the points are added as depletion that started
     *   totalDuration ago, so the GM sees the map as it would be now
     */
    CheckGM(call.client);
    if (typeID->IsNone())
        throw CustomError ("Select a resource first.");
    uint16 resourceTypeID = (uint16)PyRep::IntegerValue(typeID);
    if (!GetPlanetSE()->IsResourceType(resourceTypeID))
        throw CustomError ("That resource is not found on this planet.");
    if ((info->arguments() == nullptr) or !info->arguments()->IsDict())
        throw CustomError ("Invalid depletion info.");
    PyDict* dict = info->arguments()->AsDict();
    PyRep* durationRep = dict->GetItemString("totalDuration");
    PyRep* pointsRep = dict->GetItemString("points");
    if ((durationRep == nullptr) or (pointsRep == nullptr) or !pointsRep->IsList())
        throw CustomError ("Invalid depletion info.");
    int64 totalDuration = PyRep::IntegerValue(durationRep);
    int64 start = GetFileTimeNow() - totalDuration;
    std::vector<PIResource::Depletion> rows;
    PyList* points = pointsRep->AsList();
    for (size_t i = 0; i < points->size(); ++i) {
        PyRep* rep = points->GetItem(i);
        if (!rep->IsObject() or (rep->AsObject()->arguments() == nullptr) or !rep->AsObject()->arguments()->IsDict())
            continue;
        PyDict* point = rep->AsObject()->arguments()->AsDict();
        PyRep* lat = point->GetItemString("latitude");
        PyRep* lon = point->GetItemString("longitude");
        PyRep* amount = point->GetItemString("amount");
        PyRep* duration = point->GetItemString("duration");
        PyRep* headRadius = point->GetItemString("headRadius");
        if ((lat == nullptr) or (lon == nullptr) or (amount == nullptr) or (duration == nullptr) or (headRadius == nullptr))
            continue;
        PIResource::Depletion row = PIResource::Depletion();
            row.latitude = PyRep::FloatValue(lat);
            row.longitude = PyRep::FloatValue(lon);
            row.amount = (amount->IsFloat() ? amount->AsFloat()->value() : (double)PyRep::IntegerValue(amount));
            row.duration = PyRep::IntegerValue(duration) * EvE::Time::Minute;
            row.headRadius = PyRep::FloatValue(headRadius);
            row.startTime = start;
        rows.push_back(row);
    }
    GetPlanetSE()->AddDepletion(resourceTypeID, rows);
    _log(PLANET__INFO, "PlanetMgrBound::GMRunDepletionSim() - %s added %u depletion points for %s on %u", \
            call.client->GetName(), rows.size(), sPIDataMgr.GetProductName(resourceTypeID), m_planetID);
    return PyStatic.NewNone();
}

PyResult PlanetMgrBound::GMGetCompleteResource(PyCallArgs &call, PyInt* typeID, PyRep* layer) {
    /*  planetUISvc.GMShowResource(resourceTypeID, layer):
     *        data = planet.remoteHandler.GMGetCompleteResource(resourceTypeID, layer)
     *        sh = builder.CreateSHFromBuffer(data.data, data.numBands)
     *   layers:  current (server), player, base, depletion, nuggets
     */
    CheckGM(call.client);
    return GetPlanetSE()->GetLayerData(call.client, typeID->value(), PyRep::StringContent(layer));
}

PyResult PlanetMgrBound::GMCreateNuggetLayer(PyCallArgs &call, PyInt* planetID, PyInt* typeID) {
    // CCP's internal resource seeding.  not known, and the maps here are generated
    CheckGM(call.client);
    throw CustomError ("Not supported.");
}
