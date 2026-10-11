
/**
 * @name PosMgr.cpp
 *   Class for pos manager calls.
 *
 * @Author:         Allan
 * @date:   28 December 17
 */

/*
 * POS__ERROR
 * POS__WARNING
 * POS__MESSAGE
 * POS__DUMP
 * POS__DEBUG
 * POS__DESTINY
 * POS__SLIMITEM
 * POS__TRACE
 */


#include "eve-server.h"



#include "planet/Moon.h"
#include "pos/PosMgr.h"
#include "pos/Array.h"
#include "pos/Battery.h"
#include "pos/Structure.h"
#include "pos/Tower.h"
#include "pos/Weapon.h"
#include "pos/JumpBridge.h"
#include "EntityList.h"
#include "inventory/ItemFactory.h"
#include "ship/JumpDrive.h"
#include "system/SystemManager.h"
#include "services/ServiceManager.h"

PosMgr::PosMgr(EVEServiceManager &mgr) :
    BindableService("posMgr", mgr)
{
    this->Add("GetJumpArrays", &PosMgr::GetJumpArrays);
    this->Add("GetControlTowers", &PosMgr::GetControlTowers);
    this->Add("GetControlTowerFuelRequirements", &PosMgr::GetControlTowerFuelRequirements);
}

BoundDispatcher* PosMgr::BindObject(Client* client, PyRep* bindParameters) {
    _log( POS__DUMP, "PosMgr bind request for:" );
    bindParameters->Dump( POS__DUMP, "    " );

    if (!bindParameters->IsInt()){
        sLog.Error( "PosMgr::CreateBoundObject", "%s: bind_args is not int: '%s'. ", client->GetName(), bindParameters->TypeString() );
        return nullptr;
    }

    uint32 systemID = bindParameters->AsInt()->value();
    auto it = this->m_instances.find (systemID);

    if (it != this->m_instances.end ())
        return it->second;

    PosMgrBound* bound = new PosMgrBound(this->GetServiceManager(), *this, systemID);

    this->m_instances.insert_or_assign (systemID, bound);

    return bound;
}

void PosMgr::BoundReleased (PosMgrBound* bound) {
    auto it = this->m_instances.find (bound->GetSystemID());

    if (it == this->m_instances.end ())
        return;

    this->m_instances.erase (it);
}

PyResult PosMgr::GetControlTowerFuelRequirements(PyCallArgs &call) {
    /** @todo put this in static data */
    DBQueryResult res;
    m_db.GetControlTowerFuelRequirements(res);

    DBResultRow row;
    PyList* list = new PyList();
    while (res.GetRow(row)) {
        //SELECT controlTowerTypeID, resourceTypeID, purpose, quantity, minSecurityLevel, factionID, wormholeClassID
        PyDict* dict = new PyDict();
        dict->SetItemString("controlTowerTypeID",   new PyInt(row.GetInt(0)));
        dict->SetItemString("resourceTypeID",       new PyInt(row.GetInt(1)));
        dict->SetItemString("purpose",              new PyInt(row.GetInt(2)));
        dict->SetItemString("quantity",             new PyInt(row.GetInt(3)));
        dict->SetItemString("minSecurityLevel",     (row.IsNull(4) ? PyStatic.NewNone() : new PyFloat(row.GetDouble(4))));
        dict->SetItemString("factionID",            (row.IsNull(5) ? PyStatic.NewNone() : new PyInt(row.GetInt(5))));
        dict->SetItemString("wormholeClassID",      (row.IsNull(6) ? PyStatic.NewNone() : new PyInt(row.GetInt(6))));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }

    return list;
}

PyResult PosMgr::GetControlTowers(PyCallArgs &call) {
    /*  ct = sm.RemoteSvc('posMgr').GetControlTowers()
     *        for row in ct:
     *            typeID, structureID, solarSystemID = row[0:3]
     *
     *                        [CRowSet]
     *                            [DBRowDescriptor]
     *                                ["typeID" =>  [I4] ]
     *                                ["itemID" =>  [I8] ]
     *                                ["locationID" =>  [I8] ]
     *                            Rows:
     *                                [PyPackedRow 21 bytes]
     *                                    ["typeID" =>  [I4] <20062>]
     *                                    ["itemID" =>  [I8] <1013581844534>]
     *                                    ["locationID" =>  [I8] <30002206>]
     *                                [PyPackedRow 21 bytes]
     *                                    ["typeID" =>  [I4] <27606>]
     *                                    ["itemID" =>  [I8] <1010759458081>]
     *                                    ["locationID" =>  [I8] <30002225>]
     */

    _log(POS__TRACE,  "PosMgr::Handle_GetControlTowers()");
    call.Dump(POS__DUMP);

    return m_db.GetCorpControlTowers(call.client->GetCorporationID());
}

PyResult PosMgr::GetJumpArrays(PyCallArgs &call) {
    /*        jb = sm.RemoteSvc('posMgr').GetJumpArrays()
     *
     *            for data in jb:
     *                solarSystemID, subData = data (fromSystem, solarSystemData)     <-- tuple (to solarSystemID, StructureData)
     *                    ssid = subData.keys()[0]                                    <-- jb itemID (from)
     *                    tsid = subData.values()[0][1]                               <-- jb itemID (to)
     *
     *                    fromStructure = solarSystemData.keys()[0]                   <-- indexed dict of tuple
     *                    toSystem = cfg.evelocations.Get(solarSystemData.values()[0][0])     <-- to solarSystemID
     *                    toStructure = solarSystemData.values()[0][1]                       <-- jb itemID (to)
     *                    toStructureType = solarSystemData.values()[0][2]                   <-- jb typeID
     */
    DBQueryResult res;
    m_db.GetCorpJumpArrays(call.client->GetCorporationID(), res);

    DBResultRow row;
    PyList* list = new PyList();
    while (res.GetRow(row)) {
        //SELECT itemID, systemID, toItemID, toTypeID, toSystemID
        PyTuple* rsp = new PyTuple(2);
            rsp->SetItem(0, new PyInt(row.GetInt(1)));
        if (row.GetInt(2) > 0) {
        PyDict* dict = new PyDict();
            PyTuple* tuple = new PyTuple(3);
                tuple->SetItem(0, new PyInt(row.GetInt(4)));
                tuple->SetItem(1, new PyInt(row.GetInt(2)));
                tuple->SetItem(2, new PyInt(row.GetInt(3)));
            dict->SetItem(new PyInt(row.GetInt(0)), tuple);
            rsp->SetItem(1, dict);
        } else {
            rsp->SetItem(1, PyStatic.NewNone());
        }

        list->AddItem(rsp);
    }

    list->Dump(POS__RSP_DUMP, "    ");
    return list;
}

PosMgrBound::PosMgrBound(EVEServiceManager& mgr, PosMgr& parent, uint32 systemID) :
    EVEBoundObject(mgr, parent)
{
    m_systemID = systemID;

    this->Add("GetMoonForTower", &PosMgrBound::GetMoonForTower);
    this->Add("SetTowerPassword", static_cast <PyResult (PosMgrBound::*)(PyCallArgs&, PyInt*, PyRep*, PyBool*, PyBool*)> (&PosMgrBound::SetTowerPassword));
    this->Add("SetTowerPassword", static_cast <PyResult(PosMgrBound::*)(PyCallArgs&, PyInt*, PyRep*)> (&PosMgrBound::SetTowerPassword));
    this->Add("SetShipPassword", &PosMgrBound::SetShipPassword);
    this->Add("GetSiloCapacityByItemID", &PosMgrBound::GetSiloCapacityByItemID);
    this->Add("AnchorOrbital", &PosMgrBound::AnchorOrbital);
    this->Add("UnanchorOrbital", &PosMgrBound::UnanchorOrbital);
    this->Add("OnlineOrbital", &PosMgrBound::OnlineOrbital);
    this->Add("GMUpgradeOrbital", &PosMgrBound::GMUpgradeOrbital);
    this->Add("AnchorStructure", &PosMgrBound::AnchorStructure);
    this->Add("UnanchorStructure", &PosMgrBound::UnanchorStructure);
    this->Add("AssumeStructureControl", &PosMgrBound::AssumeStructureControl);
    this->Add("RelinquishStructureControl", &PosMgrBound::RelinquishStructureControl);
    this->Add("ChangeStructureProvisionType", &PosMgrBound::ChangeStructureProvisionType);
    this->Add("CompleteOrbitalStateChange", &PosMgrBound::CompleteOrbitalStateChange);
    this->Add("GetMoonProcessInfoForTower", &PosMgrBound::GetMoonProcessInfoForTower);
    this->Add("LinkResourceForTower", &PosMgrBound::LinkResourceForTower);
    this->Add("RunMoonProcessCycleforTower", &PosMgrBound::RunMoonProcessCycleforTower);
    this->Add("GetStarbasePermissions", &PosMgrBound::GetStarbasePermissions);
    this->Add("SetStarbasePermissions", &PosMgrBound::SetStarbasePermissions);
    this->Add("GetTowerNotificationSettings", &PosMgrBound::GetTowerNotificationSettings);
    this->Add("SetTowerNotifications", &PosMgrBound::SetTowerNotifications);
    this->Add("GetTowerSentrySettings", &PosMgrBound::GetTowerSentrySettings);
    this->Add("SetTowerSentrySettings", &PosMgrBound::SetTowerSentrySettings);
    this->Add("InstallJumpBridgeLink", &PosMgrBound::InstallJumpBridgeLink);
    this->Add("UninstallJumpBridgeLink", &PosMgrBound::UninstallJumpBridgeLink);
}

namespace {
    // the starbase roles that open the tower management window's tabs
    const int64 StarbaseViewRoles = (Corp::Role::StarbaseConfig | Corp::Role::StarbaseCaretaker | Corp::Role::InfrastructureTacticalOfficer);

    // a coordinate of the client's (x, y, z):  float or int
    double PosValue(PyRep* rep)
    {
        if (rep == nullptr)
            return 0.0;
        if (rep->IsFloat())
            return rep->AsFloat()->value();
        return (double)PyRep::IntegerValue(rep);
    }
}

TowerSE* PosMgrBound::GetTower(Client* pClient, PyInt* itemID, int64 roles, bool setter)
{
    SystemManager* pSystem(pClient->SystemMgr());
    SystemEntity* pSE((pSystem == nullptr) ? nullptr : pSystem->GetSE(itemID->value()));
    if ((pSE == nullptr) or (pSE->GetTowerSE() == nullptr))
        throw CustomError ("That control tower can't be found.");
    TowerSE* pTower(pSE->GetTowerSE());
    if (pTower->GetCorporationID() != (uint32)pClient->GetCorporationID())
        throw UserError("CantConfigureNotInRightCorp");
    if (!pTower->HasRole(pClient, roles)) {
        if (roles == (Corp::Role::StarbaseConfig | Corp::Role::StarbaseCaretaker))
            throw UserError("CantConfigureCorpStarbaseOrCaretakerRoleLacking");
        throw UserError("CantConfigureCorpStarbaseRoleLacking");
    }
    // a reinforced tower is locked down
    if (setter and pTower->IsReinforced())
        throw UserError("CantPerformTowerReinforced").AddFormatValue("typeID", new PyInt(pTower->GetTypeID()));
    return pTower;
}

namespace {
    // '{structure} in {system}' parameters of the bridge messages
    [[noreturn]] void ThrowBridgeError(const char* msgKey, const char* structure, uint32 systemID)
    {
        throw UserError(msgKey)
                .AddFormatValue("structure", new PyString(structure))
                .AddFormatValue("system", new PyString(sDataMgr.GetSystemName(systemID)));
    }

    // the local bridge the caller configures:  owned by the caller's corp, Config Starbase Equipment role
    JumpBridgeSE* GetConfigurableBridge(Client* pClient, uint32 itemID)
    {
        SystemManager* pSystem(pClient->SystemMgr());
        SystemEntity* pSE(pSystem == nullptr ? nullptr : pSystem->GetSE(itemID));
        if ((pSE == nullptr) or (pSE->GetJumpBridgeSE() == nullptr))
            ThrowBridgeError("CantBridgeInvalidJumpPortal", (pSE == nullptr ? "structure" : pSE->GetName()), pClient->GetSystemID());
        if (pSE->GetCorporationID() != (uint32)pClient->GetCorporationID())
            throw UserError("CantConfigureNotInRightCorp");
        if ((pClient->GetCorpRole() & Corp::Role::StarbaseConfig) != Corp::Role::StarbaseConfig)
            throw UserError("CantConfigureCorpSovereigntyRoleLacking");
        return pSE->GetJumpBridgeSE();
    }

    // a loaded remote bridge, nullptr when its system isn't loaded (never boots one)
    JumpBridgeSE* GetLoadedBridge(uint32 systemID, uint32 itemID)
    {
        SystemManager* pSystem(sEntityList.FindSystem(systemID));
        if (pSystem == nullptr)
            return nullptr;
        SystemEntity* pSE(pSystem->GetSE(itemID));
        return (pSE == nullptr ? nullptr : pSE->GetJumpBridgeSE());
    }
}

PyResult PosMgrBound::InstallJumpBridgeLink(PyCallArgs &call, PyInt* localItemID, PyInt* remoteSolarSystemID, PyInt* remoteItemID) {
    /**
     *    def BridgePortals(self, localItemID, remoteSolarSystemID, remoteItemID):
     *        posLocation = util.Moniker('posMgr', session.solarsystemid)
     *        posLocation.InstallJumpBridgeLink(localItemID, remoteSolarSystemID, remoteItemID)
     *  the remote end is picked from map.GetLinkableJumpArrays()
     */
    _log(POS__TRACE,  "PosMgrBound::Handle_InstallJumpBridgeLink()");
    call.Dump(POS__DUMP);

    Client* pClient(call.client);
    JumpBridgeSE* pLocal(GetConfigurableBridge(pClient, localItemID->value()));
    uint32 localSystemID(pClient->GetSystemID());
    uint32 remoteID(remoteItemID->value()), remoteSysID(remoteSolarSystemID->value());

    // remote bridge, from the db (its system may not be loaded)
    EVEPOS::JumpBridgeData remote = EVEPOS::JumpBridgeData();
        remote.itemID = remoteID;
    InventoryItemRef remoteRef(sItemFactory.GetItemRef(remoteID));
    std::string name(remoteRef.get() == nullptr ? "Jump Bridge" : remoteRef->itemName());
    const char* remoteName(name.c_str());
    if (!m_db.GetBridgeData(remote) or (remoteRef.get() == nullptr) or ((uint32)remote.systemID != remoteSysID))
        ThrowBridgeError("CantBridgeJumpPortalGone", remoteName, remoteSysID);
    if (remoteRef->groupID() != EVEDB::invGroups::Jump_Portal_Array)
        ThrowBridgeError("CantBridgeInvalidJumpPortal", remoteName, remoteSysID);
    // the network is the alliance's (or the corp's own without one)
    uint32 allianceID(pClient->GetAllianceID());
    if ((uint32)remote.corpID != (uint32)pClient->GetCorporationID())
        if ((allianceID == 0) or (PosMgrDB::GetCorpAllianceID(remote.corpID) != allianceID))
            ThrowBridgeError("CantBridgeInvalidJumpPortal", remoteName, remoteSysID);
    if (remoteSysID == localSystemID)
        ThrowBridgeError("CantBridgeInvalidJumpPortal", remoteName, remoteSysID);

    if (remoteRef->typeID() != pLocal->GetTypeID())
        throw UserError("CantBridgeDifferentTypes")
                .AddTypeName("sourceType", pLocal->GetTypeID())
                .AddTypeName("destType", remoteRef->typeID());

    // both anchored and offline
    if (pLocal->GetState() != EVEPOS::StructureState::Anchored)
        ThrowBridgeError("CantBridgeJumpPortalNotAnchored", pLocal->GetName(), localSystemID);
    JumpBridgeSE* pRemote(GetLoadedBridge(remoteSysID, remoteID));
    int8 remoteState(pRemote == nullptr ? PosMgrDB::GetStructureState(remoteID) : pRemote->GetState());
    if (remoteState != EVEPOS::StructureState::Anchored)
        ThrowBridgeError("CantBridgeJumpPortalNotAnchored", remoteName, remoteSysID);

    if (pLocal->IsLinked())
        ThrowBridgeError("CantBridgeJumpPortalLinked", pLocal->GetName(), localSystemID);
    if ((pRemote == nullptr ? remote.toItemID : pRemote->GetBridgeData().toItemID) != 0)
        ThrowBridgeError("CantBridgeJumpPortalLinked", remoteName, remoteSysID);

    double ly(JumpDrive::LightYears(localSystemID, remoteSysID));
    double range(pLocal->GetSelf()->GetAttribute(AttrJumpDriveRange).get_double());
    if (ly > range)
        throw UserError("CantBridgeTooFar")
                .AddFormatValue("dist", new PyFloat(ly))
                .AddFormatValue("structure", new PyString(pLocal->GetName()))
                .AddFormatValue("range", new PyFloat(range));

    // Install jump bridge link both ways
    m_db.InstallBridgeLink(pLocal->GetID(), remoteSysID, remoteID, remoteRef->typeID());
    m_db.InstallBridgeLink(remoteID, localSystemID, pLocal->GetID(), pLocal->GetTypeID());
    pLocal->SetLink(remoteSysID, remoteID, remoteRef->typeID());
    if (pRemote != nullptr)
        pRemote->SetLink(localSystemID, pLocal->GetID(), pLocal->GetTypeID());

    _log(POS__MESSAGE, "%s linked %s(%u) in %u to %u in %u (%.2f ly)", pClient->GetName(), pLocal->GetName(), pLocal->GetID(),
            localSystemID, remoteID, remoteSysID, ly);
    return PyStatic.NewNone();
}

PyResult PosMgrBound::UninstallJumpBridgeLink(PyCallArgs &call, PyInt* itemID) {
    /**
     *    def UnbridgePortal(self, itemID):
     *        posLocation = util.Moniker('posMgr', session.solarsystemid)
     *        posLocation.UninstallJumpBridgeLink(itemID)
     *
     */
    _log(POS__TRACE,  "PosMgrBound::Handle_UninstallJumpBridgeLink()");
    call.Dump(POS__DUMP);

    JumpBridgeSE* pLocal(GetConfigurableBridge(call.client, itemID->value()));
    if (pLocal->GetState() != EVEPOS::StructureState::Anchored)
        ThrowBridgeError("CantUnbridgeJumpPortalNotAnchored", pLocal->GetName(), call.client->GetSystemID());

    EVEPOS::JumpBridgeData data(pLocal->GetBridgeData());
    m_db.UninstallRemoteBridgeLink(itemID->value());
    m_db.UninstallBridgeLink(itemID->value());
    pLocal->ClearLink();
    if (data.toItemID != 0)
        if (JumpBridgeSE* pRemote = GetLoadedBridge(data.toSystemID, data.toItemID)) {
            pRemote->ClearLink();
            // can't stay online without a link
            if (pRemote->IsOnline() or (pRemote->GetProcState() == EVEPOS::ProcState::Onlining))
                pRemote->Deactivate(0);
        }

    return PyStatic.NewNone();
}

PyResult PosMgrBound::GetSiloCapacityByItemID(PyCallArgs &call, PyInt* itemID) {
    // invCache:  the silo's actual capacity (m3), with its tower's silo bonus
    _log(POS__TRACE,  "PosMgrBound::Handle_GetSiloCapacityByItemID()");
    call.Dump(POS__DUMP);

    SystemManager* pSystem(call.client->SystemMgr());
    SystemEntity* pSE((pSystem == nullptr) ? nullptr : pSystem->GetSE(itemID->value()));
    if ((pSE == nullptr) or (pSE->GetPOSSE() == nullptr))
        throw CustomError ("That structure can't be found.");
    return new PyFloat(TowerSE::GetSiloCapacity(pSE->GetPOSSE()));
}

PyResult PosMgrBound::GetTowerNotificationSettings(PyCallArgs &call, PyInt* itemID) {
    /*
     *        notifySettings = self.posMgr.GetTowerNotificationSettings(self.slimItem.itemID)
     *        self.fuelNotifyCheckbox.SetChecked(notifySettings.sendFuelNotifications, 0)
     *        self.calendarCheckbox.SetChecked(notifySettings.showInCalendar, 0)
     */
    _log(POS__TRACE,  "PosMgrBound::Handle_GetTowerNotificationSettings()");
    call.Dump(POS__DUMP);

    TowerSE* pTSE(GetTower(call.client, itemID, StarbaseViewRoles, false));

    PyList* header = new PyList(2);
        header->SetItemString(0, "sendFuelNotifications");
        header->SetItemString(1, "showInCalendar");
    PyDict* dict = new PyDict();
        dict->SetItemString("header", header);

    PyList* line = new PyList(2);
        line->SetItem(0, new PyBool(pTSE->SendFuelNotifications()));
        line->SetItem(1, new PyBool(pTSE->ShowInCalendar()));
    dict->SetItemString("line", line);

    return new PyObject("util.Row", dict);
}

PyResult PosMgrBound::SetTowerNotifications(PyCallArgs &call, PyInt* itemID, PyBool* showInCalendar, PyBool* sendFuelNotifications) {
    //self.posMgr.SetTowerNotifications(self.slimItem.itemID, showInCalendar, sendFuelNotifications)

    TowerSE* pTSE(GetTower(call.client, itemID, Corp::Role::StarbaseConfig, true));

    pTSE->SetSendFuelNotifications(sendFuelNotifications->value());
    pTSE->SetShowInCalendar(showInCalendar->value());
    pTSE->UpdateNotify();
    return PyStatic.NewNone();
}

PyResult PosMgrBound::GetTowerSentrySettings(PyCallArgs &call, PyInt* itemID) {
    //  standing, status, statusDrop, war, standingOwnerID = self.sentrySettings = self.posMgr.GetTowerSentrySettings(self.slimItem.itemID)
    PyDict* data = new PyDict();
    PyList* header = new PyList(5);
        header->SetItemString(0, "standing");
        header->SetItemString(1, "status");
        header->SetItemString(2, "statusDrop");
        header->SetItemString(3, "corpWar");
        header->SetItemString(4, "standingOwnerID");
    data->SetItemString("header", header);

    TowerSE* pTSE(GetTower(call.client, itemID, StarbaseViewRoles, false));

    // limits that are switched off go back as None (unchecked boxes)
    PyList* line = new PyList(5);
        line->SetItem(0, TowerSE::IsSentrySettingOff(pTSE->GetStanding()) ? PyStatic.NewNone() : new PyFloat(pTSE->GetStanding()));
        line->SetItem(1, TowerSE::IsSentrySettingOff(pTSE->GetSentryStatus()) ? PyStatic.NewNone() : new PyFloat(pTSE->GetSentryStatus()));
        line->SetItem(2, new PyBool(pTSE->GetStatusDrop()));
        line->SetItem(3, new PyBool(pTSE->GetCorpWar()));
        line->SetItem(4, new PyInt(pTSE->GetStandingOwnerID()));
    data->SetItemString("line", line);

    return new PyObject("util.Row", data);
}

namespace {
    // the client sends floats or ints, and None for unchecked limits
    float SentryValue(PyRep* rep)
    {
        if (rep->IsFloat())
            return rep->AsFloat()->value();
        if (rep->IsInt())
            return rep->AsInt()->value();
        return TowerSE::SentrySettingOff;
    }
    bool SentryFlag(PyRep* rep)
    {
        if (rep->IsBool())
            return rep->AsBool()->value();
        if (rep->IsInt())
            return (rep->AsInt()->value() != 0);
        return false;
    }
}

PyResult PosMgrBound::SetTowerSentrySettings(PyCallArgs &call, PyInt* itemID, PyRep* standing, PyRep* status, PyRep* statusDrop, PyRep* corpWar, PyRep* useAllianceStandings) {
    //  self.posMgr.SetTowerSentrySettings(self.slimItem.itemID, standing, status, statusDrop, war, useAllianceStandings)
    TowerSE* pTSE(GetTower(call.client, itemID, Corp::Role::StarbaseConfig, true));

    pTSE->SetStanding(SentryValue(standing));
    pTSE->SetStatus(SentryValue(status));
    pTSE->SetStatusDrop(SentryFlag(statusDrop));
    pTSE->SetCorpWar(SentryFlag(corpWar));
    pTSE->SetUseAllyStandings(SentryFlag(useAllianceStandings));
    // the client ticks 'use alliance standings' when standingOwnerID is the viewer's alliance
    if (SentryFlag(useAllianceStandings) and IsAlliance(call.client->GetAllianceID())) {
        pTSE->SetStandingOwnerID(call.client->GetAllianceID());
    } else {
        pTSE->SetStandingOwnerID(call.client->GetCorporationID());
    }
    pTSE->UpdateSentry();

    return PyStatic.NewNone();
}

PyResult PosMgrBound::GetStarbasePermissions(PyCallArgs &call, PyInt* itemID) {
    //  deployFlags, usageFlagsList = self.posMgr.GetStarbasePermissions(self.slimItem.itemID)
    TowerSE* pTSE(GetTower(call.client, itemID, StarbaseViewRoles, false));

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, pTSE->GetDeployFlags());  // deployFlags
        tuple->SetItem(1, pTSE->GetUsageFlagList());  // usageFlagsList
    return tuple;
}

PyResult PosMgrBound::SetStarbasePermissions(PyCallArgs &call, PyInt* itemID, PyObject* deployFlags, PyObject* usageFlagsList) {
    //  self.posMgr.SetStarbasePermissions(self.slimItem.itemID, self.sr.deployFlags, self.sr.usageFlagsList)
    /* values....
     * 0 - corp role config equip.
     * 1 - corp members
     * 2 - ally members
     * 3 - corp role fuel tech
     */

    _log(POS__TRACE,  "PosMgrBound::Handle_SetStarbasePermissions()");
    call.Dump(POS__DUMP);

    TowerSE* pTSE(GetTower(call.client, itemID, Corp::Role::StarbaseConfig, true));

    // an access level (EVEPOS::Access 0-3) from a row
    auto level = [](PyList* row, size_t idx) -> int8 {
        PyRep* rep(row->size() > idx ? row->GetItem(idx) : nullptr);
        if ((rep == nullptr) or !rep->IsInt() or (rep->AsInt()->value() < EVEPOS::Access::ConfigRole)
        or (rep->AsInt()->value() > EVEPOS::Access::FuelTechRole))
            throw CustomError ("Invalid starbase permission.");
        return rep->AsInt()->value();
    };

    // decode deployFlags object (util.Row)
    PyRep* args((deployFlags == nullptr) ? nullptr : deployFlags->arguments());
    PyRep* rep((args == nullptr) or !args->IsDict() ? nullptr : args->AsDict()->GetItemString("line"));
    if ((rep == nullptr) or !rep->IsList()) {
        codelog(POS__ERROR, "deployFlags 'line' item is not PyList");
        throw CustomError ("Invalid starbase permissions.");
    }
    PyList* list = rep->AsList();
    int8 anchor(level(list, 0)), unanchor(level(list, 1)), online(level(list, 2)), offline(level(list, 3));

    // decode usageFlagsList object (util.Rowset)
    PyRep* args2((usageFlagsList == nullptr) ? nullptr : usageFlagsList->arguments());
    PyRep* rep2((args2 == nullptr) or !args2->IsDict() ? nullptr : args2->AsDict()->GetItemString("lines"));
    if ((rep2 == nullptr) or !rep2->IsList()) {
        codelog(POS__ERROR, "usageFlagsList 'lines' item is not PyList");
        throw CustomError ("Invalid starbase permissions.");
    }
    // validate everything before changing anything
    std::vector<std::tuple<uint32, int8, int8, int8>> usage;
    PyList* list2 = rep2->AsList();
    for (PyList::const_iterator itr = list2->begin(); itr != list2->end(); ++itr) {
        if (!(*itr)->IsList() or ((*itr)->AsList()->size() < 4) or !(*itr)->AsList()->GetItem(0)->IsInt()) {
            codelog(POS__ERROR, "usageFlagsList - row is not a list of 4");
            continue;
        }
        PyList* row((*itr)->AsList());
        usage.emplace_back(row->GetItem(0)->AsInt()->value(), level(row, 1), level(row, 2), level(row, 3));
    }

    pTSE->SetDeployFlags(anchor, unanchor, online, offline);
    // only this tower's structures (SetUseFlags ignores others)
    for (auto cur : usage)
        pTSE->SetUseFlags(std::get<0>(cur), std::get<1>(cur), std::get<2>(cur), std::get<3>(cur));

    return PyStatic.NewNone();
}

PyResult PosMgrBound::GetMoonForTower(PyCallArgs &call, PyInt* itemID) {
  /*
13:13:06 L PosMgrBound::Handle_GetMoonForTower(): size= 1
13:13:06 [SvcCall]   Call Arguments:
13:13:06 [SvcCall]       Tuple: 1 elements
13:13:06 [SvcCall]         [ 0] Integer field: 140001260

self.moonID = self.moon[0]
if self.moon[1] is not None:
    for typeID, quantity in self.moon[1]:

    returns
        tuple
            moonID
            tuple
                resource typeID
                resource quantity

  _log(POS__TRACE,  "PosMgrBound::Handle_GetMoonForTower()");
  call.Dump(POS__DUMP);
  */

    TowerSE* pTSE(GetTower(call.client, itemID, StarbaseViewRoles, false));
    MoonSE* pMSE = pTSE->GetMoonSE();
    if (pMSE == nullptr)
        return PyStatic.NewNone();

    std::map<uint16, uint8>::iterator itr = pMSE->GooBegin(), end = pMSE->GooEnd();
    PyList* list = new PyList();
    while (itr != end) {
        PyTuple* resource = new PyTuple(2);
            resource->SetItem(0, new PyInt(itr->first));
            resource->SetItem(1, new PyInt(itr->second));
        list->AddItem(resource);
        ++itr;
    }

    PyTuple* item = new PyTuple(2);
        item->SetItem(0, new PyInt(pMSE->GetID()));
        item->SetItem(1, list);
    return item;
}

PyResult PosMgrBound::SetShipPassword(PyCallArgs &call, PyWString* password) {
    /*
     * 13:16:17 L PosMgrBound::Handle_SetShipPassword(): size= 1
     * 13:16:17 [SvcCall]   Call Arguments:
     * 13:16:17 [SvcCall]       Tuple: 1 elements
     * 13:16:17 [SvcCall]         [ 0] WString: 'test'             << password
     */

    // havent been able to call this while docked, but there is an error msg for it.
    if (call.client->IsDocked() or (call.client->GetShipSE() == nullptr))
        throw UserError("CannotSetShieldHarmonicPassword");

    // not from inside a force field
    ShipSE* pShipSE(call.client->GetShipSE());
    if ((pShipSE->SysBubble() != nullptr) and pShipSE->SysBubble()->HasTower()
    and pShipSE->SysBubble()->GetTowerSE()->InForceField(pShipSE->GetPosition()))
        throw UserError("CantChangeShipHarmonicWithinForceField");

    pShipSE->SetPassword(PyRep::StringContent(password));

    return PyStatic.NewNone();
}

PyResult PosMgrBound::SetTowerPassword(PyCallArgs& call, PyInt* itemID, PyRep* password) {
    // 'Set new password for force field' (pwn.EnterTowerPassword)
    TowerSE* pTSE(GetTower(call.client, itemID, Corp::Role::StarbaseConfig, true));
    // the force field can't be retuned while the shield is weak
    if (pTSE->GetShieldFraction() < EVEPOS::ShieldFluctuationLevel)
        throw UserError("CantPerformTowerShieldFluctuations").AddFormatValue("tower", new PyInt(pTSE->GetTypeID()));

    if (password->IsString() or password->IsWString())
        pTSE->SetPassword(PyRep::StringContent(password));
    pTSE->UpdatePassword();

    return PyStatic.NewNone();
}

PyResult PosMgrBound::SetTowerPassword(PyCallArgs &call, PyInt* itemID, PyRep* password, PyBool* allowCorp, PyBool* allowAlliance) {
    // the tower window's force field tab.  password False keeps the current one
    TowerSE* pTSE(GetTower(call.client, itemID, Corp::Role::StarbaseConfig, true));
    // the force field can't be retuned while the shield is weak
    if (pTSE->GetShieldFraction() < EVEPOS::ShieldFluctuationLevel)
        throw UserError("CantPerformTowerShieldFluctuations").AddFormatValue("tower", new PyInt(pTSE->GetTypeID()));

    if (password->IsString() or password->IsWString()) {
        pTSE->SetPassword(PyRep::StringContent(password));
        pTSE->UpdatePassword();
    }

    pTSE->SetCorpAccess(allowCorp->value());
    pTSE->SetAllyAccess(allowAlliance->value());
    pTSE->UpdateAccess();

    return PyStatic.NewNone();
}

PyResult PosMgrBound::UnanchorStructure(PyCallArgs &call, PyInt* itemID) {
    // the client unanchors through dogmaLM.Activate(itemID, effectAnchorLiftForStructures); this is the same thing
    _log(POS__TRACE,  "PosMgrBound::Handle_UnanchorStructure()");
    call.Dump(POS__DUMP);

    SystemManager* pSystem(call.client->SystemMgr());
    SystemEntity* pSE((pSystem == nullptr) ? nullptr : pSystem->GetSE(itemID->value()));
    if ((pSE == nullptr) or (pSE->GetPOSSE() == nullptr))
        throw CustomError ("That structure can't be found.");
    pSE->GetPOSSE()->PullAnchor(call.client);

    return PyStatic.NewNone();
}

PyResult PosMgrBound::AnchorStructure(PyCallArgs &call, PyInt* structureID, PyTuple* position) {
    _log(POS__TRACE, "POS Mgr::Anchor()");
    call.Dump(POS__DUMP);
    if (!call.client->IsInSpace() or (call.client->GetShipSE() == nullptr))
        throw CustomError ("You must be in space to anchor a structure.");
    SystemManager* pSystem(call.client->SystemMgr());
    SystemEntity* pSE((pSystem == nullptr) ? nullptr : pSystem->GetSE(structureID->value()));
    if ((pSE == nullptr) or (pSE->GetPOSSE() == nullptr))
        throw CustomError ("That structure can't be found.");
    StructureSE* pTSE(pSE->GetPOSSE());

    // modules go where the pilot placed them.  towers (0,0,0) anchor where they float
    GPoint pos(pTSE->GetPosition());
    if ((position != nullptr) and (position->size() > 2) and !pTSE->IsTowerSE())
        pos = GPoint(PosValue(position->GetItem(0)), PosValue(position->GetItem(1)), PosValue(position->GetItem(2)));
    pTSE->SetAnchor(call.client, pos);

    // legacy:  the tower jumped to its moon point, take the pilot there
    if (pTSE->IsTowerSE() and sConfig.pos.TowerSnapToMoon) {
        uint32 dist = pTSE->GetSelf()->radius() + call.client->GetShip()->radius();
        call.client->GetShipSE()->DestinyMgr()->WarpTo(pos, dist);
    }
    // returns nodeID and timestamp
    return this->GetOID();
}

PyResult PosMgrBound::GetMoonProcessInfoForTower(PyCallArgs &call, PyInt* itemID) {
    TowerSE* pTSE(GetTower(call.client, itemID, StarbaseViewRoles, false));
    return pTSE->GetProcessInfo();
}

PyResult PosMgrBound::AssumeStructureControl(PyCallArgs &call, PyInt* itemID) {
    // NOTE:  this is for controlling pos defences
    /*
        posMgr = moniker.GetPOSMgr()
        posMgr.AssumeStructureControl(item.itemID)

        02:02:07 W PosMgrBound::Handle_AssumeStructureControl(): size=1
        02:02:07 [POS:Dump]   Call Arguments:
        02:02:07 [POS:Dump]      Tuple: 1 elements
        02:02:07 [POS:Dump]       [ 0]    Integer: 140000311

        'OnAssumeStructureControl',
        'OnRelinquishStructureControl',

    */
    _log(POS__TRACE,  "PosMgrBound::Handle_AssumeStructureControl()");
    call.Dump(POS__DUMP);

    return PyStatic.NewNone();
}

PyResult PosMgrBound::RelinquishStructureControl(PyCallArgs &call, PyInt* itemID) {
    /*
        posMgr = moniker.GetPOSMgr()
        posMgr.RelinquishStructureControl(item.itemID)
    */
    _log(POS__TRACE,  "PosMgrBound::Handle_RelinquishStructureControl()");
    call.Dump(POS__DUMP);

    return PyStatic.NewNone();
}

PyResult PosMgrBound::AnchorOrbital(PyCallArgs &call, PyInt* itemID) {
    /*
     *  def AnchorOrbital(self, itemID):
     *      posMgr = util.Moniker('posMgr', session.solarsystemid)
     *      posMgr.AnchorOrbital(itemID)
     */

    _log(POS__TRACE,  "PosMgrBound::Handle_()");
    call.Dump(POS__DUMP);

    return PyStatic.NewNone();
}

PyResult PosMgrBound::UnanchorOrbital(PyCallArgs &call, PyInt* itemID) {
    /*
     *  def UnanchorOrbital(self, itemID):
     *      posMgr = util.Moniker('posMgr', session.solarsystemid)
     *      posMgr.UnanchorOrbital(itemID)
     */
    _log(POS__TRACE,  "PosMgrBound::Handle_UnanchorOrbital()");
    call.Dump(POS__DUMP);

    return PyStatic.NewNone();
}

PyResult PosMgrBound::OnlineOrbital(PyCallArgs &call, PyInt* itemID) {
    _log(POS__TRACE,  "PosMgrBound::Handle_OnlineOrbital()");
    call.Dump(POS__DUMP);

    return PyStatic.NewNone();
}

PyResult PosMgrBound::ChangeStructureProvisionType(PyCallArgs &call, PyInt* towerID, PyInt* itemID, PyRep* typeID) {
    // this changes silo content or mining product
    /*
     * 03:11:32 W PosMgrBound::Handle_ChangeStructureProvisionType(): size=3
     * 03:11:32 [POS:Dump]   Call Arguments:
     * 03:11:32 [POS:Dump]      Tuple: 3 elements
     * 03:11:32 [POS:Dump]       [ 0]    Integer: 140000061     <-- towerID
     * 03:11:32 [POS:Dump]       [ 1]    Integer: 140000224     <-- itemID
     * 03:11:32 [POS:Dump]       [ 2]    Integer: 16634         <-- typeID
     */
    _log(POS__TRACE,  "PosMgrBound::Handle_ChangeStructureProvisionType()");
    call.Dump(POS__DUMP);

    // the tower window's production tab (typeID None clears it)
    TowerSE* pTSE(GetTower(call.client, towerID, Corp::Role::StarbaseConfig | Corp::Role::StarbaseCaretaker, true));
    pTSE->ChangeProvisionType(itemID->value(), (typeID->IsInt() ? typeID->AsInt()->value() : 0));

    return PyStatic.NewNone();
}

PyResult PosMgrBound::LinkResourceForTower(PyCallArgs &call, PyInt* itemID, PyList* connections) {
    // [(sourceID, destID, typeID), ...].  the client reloads the links when this isn't true
    _log(POS__TRACE,  "PosMgrBound::Handle_LinkResourceForTower()");
    call.Dump(POS__DUMP);

    TowerSE* pTSE(GetTower(call.client, itemID, Corp::Role::StarbaseConfig | Corp::Role::StarbaseCaretaker, true));
    if (pTSE->SetProcessLinks(connections))
        return PyStatic.NewTrue();
    return PyStatic.NewFalse();
}

PyResult PosMgrBound::RunMoonProcessCycleforTower(PyCallArgs &call, PyInt* itemID) {
    // this sets cycle 'on' for given towerID
    /*
     * 03:14:25 W PosMgrBound::Handle_RunMoonProcessCycleforTower(): size=1
     * 03:14:25 [POS:Dump]   Call Arguments:
     * 03:14:25 [POS:Dump]      Tuple: 1 elements
     * 03:14:25 [POS:Dump]       [ 0]    Integer: 140000061     <-- towerID
     */
    _log(POS__TRACE,  "PosMgrBound::Handle_RunMoonProcessCycleforTower()");
    call.Dump(POS__DUMP);

    // GM button:  one hour of moon processes now
    if ((call.client->GetAccountRole() & (Acct::Role::GML | Acct::Role::GMH | Acct::Role::WORLDMOD)) == 0)
        throw CustomError ("Not allowed");
    SystemManager* pSystem(call.client->SystemMgr());
    SystemEntity* pSE((pSystem == nullptr) ? nullptr : pSystem->GetSE(itemID->value()));
    if ((pSE == nullptr) or (pSE->GetTowerSE() == nullptr))
        throw CustomError ("That control tower can't be found.");
    pSE->GetTowerSE()->RunMoonProcesses();
    pSE->GetTowerSE()->NotifyProcessChange();

    return PyStatic.NewNone();
}

PyResult PosMgrBound::GMUpgradeOrbital(PyCallArgs &call, PyInt* itemID) {
    /*
     *  def GMUpgradeOrbital(self, itemID):
     *      posMgr = util.Moniker('posMgr', session.solarsystemid)
     *      posMgr.GMUpgradeOrbital(itemID)
     */
    _log(POS__TRACE,  "PosMgrBound::Handle_GMUpgradeOrbital()");
    call.Dump(POS__DUMP);

    return PyStatic.NewNone();
}

PyResult PosMgrBound::CompleteOrbitalStateChange(PyCallArgs &call, PyInt* itemID) {
    /*
     *  def CompleteOrbitalStateChange(self, itemID):
     *      posMgr = util.Moniker('posMgr', session.solarsystemid)
     *      posMgr.CompleteOrbitalStateChange(itemID)
     */
    _log(POS__TRACE,  "PosMgrBound::Handle_CompleteOrbitalStateChange()");
    call.Dump(POS__DUMP);

    if ((call.client->GetAccountRole() & (Acct::Role::GML | Acct::Role::GMH | Acct::Role::WORLDMOD)) == 0)
        throw CustomError ("Not allowed");
    SystemManager* pSystem(sEntityList.FindOrBootSystem(m_systemID));
    SystemEntity* pSE(pSystem == nullptr ? nullptr : pSystem->GetSE(itemID->value()));
    if ((pSE == nullptr) or (pSE->GetPOSSE() == nullptr))
        throw CustomError ("%u has no state change to complete", itemID->value());
    pSE->GetPOSSE()->CompleteStateChange();

    return PyStatic.NewNone();
}

