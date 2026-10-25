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
    Author:     Zhur
    Rewrite:    Allan
*/

#include "system/CrimeWatch.h"
#include "eve-server.h"




#include "EVEServerConfig.h"
#include "EVE_Sov.h"
#include "npc/Drone.h"
#include "npc/LawEnforcement.h"
#include "planet/CustomsOffice.h"
#include "planet/Moon.h"
#include "pos/Structure.h"
#include "pos/Tower.h"
#include "ship/ShipService.h"
#include "station/StationDataMgr.h"
#include "StaticDataMgr.h"
#include "ship/modules/GenericModule.h"
#include "inventory/ContainerSecurity.h"
#include "system/Container.h"
#include "system/DestinyManager.h"
#include "system/Interdiction.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "services/ServiceManager.h"

ShipService::ShipService(EVEServiceManager& mgr) :
    BindableService("ship", mgr)
{
}

/** @todo do we need more data here?  */
BoundDispatcher *ShipService::BindObject(Client *client, PyRep* bindParameters) {
    /*
     * 23:08:44 [ClientMsg] ShipService bind request
     * 23:08:44 [ClientMsg]      Tuple: 2 elements
     * 23:08:44 [ClientMsg]       [ 0]    Integer: 60014137     <<-- locationID
     * 23:08:44 [ClientMsg]       [ 1]    Integer: 15           <<-- location's groupID
     */
    _log(CLIENT__MESSAGE, "ShipService bind request");

    // this service should be bound by locationID and ship, but we'll use shipID for now...
    uint32 shipID = client->GetShip()->itemID();
    auto it = this->m_instances.find (shipID);

    if (it != this->m_instances.end ())
        return it->second;

    ShipBound* bound = new ShipBound(this->GetServiceManager(), *this, client->GetShip().get());

    this->m_instances.insert_or_assign (shipID, bound);

    return bound;
}

void ShipService::BoundReleased (ShipBound* bound) {
    auto it = this->m_instances.find (bound->GetShipID());

    if (it == this->m_instances.end ())
        return;

    this->m_instances.erase (it);
}
ShipBound::ShipBound (EVEServiceManager& mgr, ShipService& parent, ShipItem* ship) :
    EVEBoundObject(mgr, parent),
    pShip(ship)
{
    this->Add("Board", &ShipBound::Board);
    this->Add("Eject", &ShipBound::Eject);
    this->Add("LeaveShip", &ShipBound::LeaveShip);
    this->Add("ActivateShip", &ShipBound::ActivateShip);
    this->Add("Undock", &ShipBound::Undock);
    this->Add("AssembleShip", static_cast <PyResult (ShipBound::*)(PyCallArgs&, PyInt*)> (&ShipBound::AssembleShip));
    this->Add("AssembleShip", static_cast <PyResult(ShipBound::*)(PyCallArgs&, PyList*)> (&ShipBound::AssembleShip));
    this->Add("Drop", &ShipBound::Drop);
    this->Add("Scoop", &ShipBound::Scoop);
    this->Add("ScoopDrone", &ShipBound::ScoopDrone);
    this->Add("ScoopToSMA", &ShipBound::ScoopToSMA);
    this->Add("LaunchFromContainer", &ShipBound::LaunchFromContainer);
    this->Add("Jettison", &ShipBound::Jettison);
    this->Add("ConfigureShip", &ShipBound::ConfigureShip);
    this->Add("GetShipConfiguration", &ShipBound::GetShipConfiguration);
    this->Add("SelfDestruct", &ShipBound::SelfDestruct);
    this->Add("BoardStoredShip", &ShipBound::BoardStoredShip);
    this->Add("StoreVessel", &ShipBound::StoreVessel);
}

/* names of the active modules that can't be stopped early (cyno, siege, triage, jump portal...).  "" if none
 *  these keep a pilot in the ship (ShipEjectFailedSystemsOccupied)
 */
static std::string ActiveBlockingModules(ShipItemRef shipRef)
{
    std::string names;
    if ((shipRef.get() == nullptr) or (shipRef->GetModuleManager() == nullptr))
        return names;
    std::vector<GenericModule*> modules;
    shipRef->GetModuleManager()->GetActiveModules(modules);
    for (auto pMod : modules) {
        if ((pMod == nullptr) or !pMod->GetAttribute(AttrDisallowEarlyDeactivation).get_bool())
            continue;
        if (!names.empty())
            names += ", ";
        names += pMod->GetSelf()->itemName();
    }
    return names;
}

/* only called in space */
PyResult ShipBound::Board(PyCallArgs &call, PyInt* newShipID, std::optional<PyInt*> oldShipID) {
    if (call.client->IsSessionChange()) {
        call.client->SendNotifyMsg("Session Change currently active.");
        return nullptr;
    }

    Client* pClient = call.client;

    ShipSE* pShipSE =  pClient->GetShipSE();
    if (pShipSE == nullptr)
        throw CustomError ("Invalid Ship.  Ref: ServerError xxxxx");

    SystemManager* pSystem = pClient->SystemMgr();
    if (pSystem == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no system manager!", call.client->GetName());
        return nullptr;
    }

    SystemEntity* pNewSE(pSystem->GetSE(newShipID->value()));
    if ((pNewSE == nullptr) or !pNewSE->IsShipSE() or pNewSE->IsDead())
        throw UserError ("ScoopObjectGone").AddFormatValue("target", new PyString("The ship"));
    ShipSE* pNewShipSE(pNewSE->GetShipSE());

    // an empty pod in space, or a ship somebody is flying (a logged off pilot's ship left in space is still theirs)
    if ((pNewShipSE->GetTypeID() == itemTypeCapsule) or pNewShipSE->HasPilot()
    or  (pSystem->GetLaw()->GetLoggedOffOwner(pNewShipSE->GetID()) != 0))
        throw UserError ("ShipAlreadyHasPilot");

    // a pod climbing into a ship isn't switching ships (as far as CONCORD's evidence rule goes)
    if (!pClient->InPod()) {
        sCrimeWatch.CanSwitchShip(pClient);
        std::string modules(ActiveBlockingModules(pClient->GetShip()));
        if (!modules.empty())
            throw UserError ("ShipEjectFailedSystemsOccupied").AddFormatValue("types", new PyString(modules));
    }

    uint32 ownerID(pNewShipSE->GetOwnerID());
    if (ownerID != pClient->GetCharacterID()) {
        // nobody may take a ship that someone has locked
        if ((pNewShipSE->TargetMgr() != nullptr) and pNewShipSE->TargetMgr()->IsTargeted())
            throw UserError ("CantBoardTargeted");
        // nor one whose owner is being chased by CONCORD
        if (IsCharacterID(ownerID) and sCrimeWatch.IsCriminal(ownerID))
            throw UserError ("ShipInvolvedInPoliceCrime");
    }

    // should we eject player here and deny boarding new ship, or just leave char in current ship and return?
    if (!pNewShipSE->GetShipItemRef()->ValidateBoardShip(pClient->GetChar()))
        throw CustomError ("You do not have the skills to fly a %s.", pNewShipSE->GetName());

    // the client's maxBoardingDistance is a surface distance
    double distance(pShipSE->GetPosition().distance(pNewShipSE->GetPosition()) - pShipSE->GetRadius() - pNewShipSE->GetRadius());
    if (distance > sConfig.world.shipBoardDistance)
        throw UserError ("NotCloseEnoughToBoard").AddFormatValue("maxdist", new PyInt(sConfig.world.shipBoardDistance));

    pClient->Board(pNewShipSE);
    // a criminal in a new ship is still a criminal
    sCrimeWatch.OnEnterSpace(pClient);

    /* return error msg from this call, if applicable (not sure how yet), else nodeid and timestamp */
    // returns nodeID and timestamp

    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

/* only called in space.  the client refuses it in a station itself (NoEjectingToSpaceInStation) */
PyResult ShipBound::Eject(PyCallArgs &call) {
    if (call.client->IsSessionChange()) {
        call.client->SendNotifyMsg("Session Change currently active.");
        return nullptr;
    }

    //no arguments.
    Client* pClient = call.client;
    SystemEntity* pShipSE = pClient->GetShipSE();
    if (pShipSE == nullptr)
        throw CustomError ("Invalid Ship.  Ref: ServerError xxxxx");
    if (pClient->InPod())
        throw UserError ("CannotEjectFromCapsule");
    if (pShipSE->DestinyMgr()->IsWarping())
        throw UserError ("ShipInWarp");

    // Crucible: criminals may not switch ships.  (the weapons timer lock is optional, crime.SwitchShipWeaponsTimer)
    sCrimeWatch.CanSwitchShip(pClient);

    // modules that can't be stopped early keep the pilot in (cyno field, siege, triage, jump portal...)
    std::string modules(ActiveBlockingModules(pClient->GetShip()));
    if (!modules.empty())
        throw UserError ("ShipEjectFailedSystemsOccupied").AddFormatValue("types", new PyString(modules));

    pClient->Eject();

    /* return error msg from this call, if applicable (not sure how yet), else nodeid and timestamp */
    // returns nodeID and timestamp
    //
    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

// NOTE  LeaveShip and ActivateShip are working.  dont fuck with them
/* only called when docked. */
PyResult ShipBound::LeaveShip(PyCallArgs &call, PyInt* shipID)
{
    if (call.client->IsSessionChange()) {
        call.client->SendNotifyMsg("Session Change currently active.");
        return nullptr;
    }

    //  sends itemID of ship to leave
    Client* pClient = call.client;
    uint32 podID = pClient->GetPodID();
    ShipItemRef podRef = pClient->SystemMgr()->GetShipFromInventory(podID);
    if (podRef.get() == nullptr)
        podRef = sItemFactory.GetShipRef(podID);

    //verify owner (not sure why pod doesnt have correct owner...)
    podRef->ChangeOwner(pClient->GetCharacterID(), false);
    //move capsule into the players hangar
    podRef->Move(pClient->GetStationID(), flagHangar, true);

    // capsuleID = shipsvc.LeaveShip(shipid)
    return new PyInt(podID);
}

/* only called when docked. */
PyResult ShipBound::ActivateShip(PyCallArgs &call, PyInt* newShipID, std::optional<PyInt*> oldShipID) {
    //self.instanceCache, self.instanceFlagQuantityCache, self.wbData = self.remoteShipMgr.ActivateShip(shipID, oldShipID)

    if (call.client->IsSessionChange()) {
        call.client->SendNotifyMsg("Session Change currently active.");
        return nullptr;
    }
    Client* pClient = call.client;
    ShipItemRef newShipRef = sItemFactory.GetShipRef(newShipID->value());
    if (newShipRef.get() == nullptr) {
        sLog.Error("ShipBound::Handle_ActivateShip()", "%s: Failed to get new ship %u.", pClient->GetName(), newShipID->value());
        throw CustomError ("Something bad happened as you prepared to board the ship.  Ref: ServerError 15173+1");
    }
    //ShipMustBeInPersonalHangar

    pClient->BoardShip(newShipRef);

    // response should return ship modules, loaded charges, and linked weapons
    PyTuple* rsp = new PyTuple(3);
        rsp->SetItem(0, newShipRef->GetShipState());    //dict of ship modules
        rsp->SetItem(1, newShipRef->GetChargeState());    //dict of flagID/subLocation{loc, flag, typeID}
        rsp->SetItem(2, newShipRef->GetLinkedWeapons()); // dict of linked modules
    if (is_log_enabled(CLIENT__INFO))
        rsp->Dump(CLIENT__INFO, "    ");
    return rsp;
}

PyResult ShipBound::Undock(PyCallArgs &call, PyInt* shipID, PyBool* ignoreContraband) {
    //ShipIllegalTypeUndock

    /*  we could have some fun with these....
     * (258696, `Undock Delayed`)
     * (258702, `Undock Prohibited`)
     * (258700, `Undock Delayed`)
     * (258703, `Officials have closed the undocking ramps in {system} due to heavy congestion. Please try again later.`)
     * (258697, `{system} Traffic Control is currently offline and unable to process your undocking request. Please try again in a moment.`)
     */

    Client* pClient = call.client;
    ShipItemRef pShip = pClient->GetShip();
    if (pShip.get() == nullptr) {
        sLog.Error("ShipBound::Handle_ActivateShip()", "%s: Failed to get ship item.", pClient->GetName());
        throw CustomError ("Something bad happened as you prepared to board the ship.  Ref: ServerError 15173");
    }

    // nowhere near implementing ignoreContraband yet....

    // a strategic cruiser needs all of its subsystems.  (the client offers the assembly window for incomplete hulls)
    if ((pShip->groupID() == EVEDB::invGroups::StrategicCruiser)
    and (pShip->GetModuleManager()->GetSubSystemCount() < pShip->GetAttribute(AttrMaxSubSystems).get_uint32()))
        throw UserError ("TooFewSubSystemsToUndock");

    //  get vector of online modules as (k,v) pair,
    //    where key is slotID, value is moduleID
    if (call.byname.find("onlineModules") != call.byname.end()) {
        PyDict* onlineModules = call.byname["onlineModules"]->AsDict();
        if (is_log_enabled(MODULE__INFO)) {
            _log(MODULE__INFO, "Dumping 'onlineModules' List");
            onlineModules->Dump(MODULE__INFO, "   ");
        }
        PyDict::const_iterator cur = onlineModules->begin(), end = onlineModules->end();
        for (; cur != end; ++cur)
            pShip->AddModuleToOnlineVec(cur->second->AsInt()->value());
    }

    pClient->UndockFromStation();

    // returns nodeID and timestamp
    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

/* the generic launch rules, before an item goes into space.  "" if it may be launched, else the error is added to list
 *  Crucible: POS, sov and orbital structures need a player corp, anchoringSecurityLevelMax keeps some structures out of
 *  high-sec, disallowInEmpireSpace (mobile warp disruptors...) keeps items out of empire space
 */
static const char* DropCheck(Client* pClient, InventoryItemRef iRef, bool npcCorpLaunch, PyDict*& args)
{
    ShipSE* pShipSE(pClient->GetShipSE());
    SystemManager* pSystem(pClient->SystemMgr());
    args = new PyDict();
    if ((iRef->locationID() != pShipSE->GetID()) or (iRef->flag() != flagCargoHold)) {
        args->SetItemString("itemTypeID", new PyInt(iRef->typeID()));
        args->SetItemString("containerTypeID", new PyInt(pShipSE->GetTypeID()));
        return "CantLaunchItemNotPresent";
    }
    uint8 categoryID(iRef->categoryID());
    bool structure((categoryID == EVEDB::invCategories::Structure)
                or (categoryID == EVEDB::invCategories::SovereigntyStructure)
                or (categoryID == EVEDB::invCategories::StructureUpgrade)
                or (categoryID == EVEDB::invCategories::Orbitals)
                or (iRef->groupID() == EVEDB::invGroups::Construction_Platform)
                or (iRef->groupID() == EVEDB::invGroups::Station_Upgrade_Platform)
                or (iRef->groupID() == EVEDB::invGroups::Station_Improvement_Platform));
    // launching for an NPC corp gives the corp nothing; structures always need a player corp
    if (npcCorpLaunch or (structure and IsNPCCorp(pClient->GetCorporationID()))) {
        args->SetItemString("item", new PyInt(iRef->typeID()));
        return "DropNeedsPlayerCorp";
    }
    if (iRef->type().HasAttribute(AttrAnchoringSecurityLevelMax)
    and (CrimeWatch::SecurityLevel(pSystem->GetSystemSecurityRating()) > iRef->type().GetAttribute(AttrAnchoringSecurityLevelMax).get_float()))
        return "CantInHighSecSpace";
    if (iRef->type().HasAttribute(AttrDisallowInEmpireSpace)
    and (iRef->type().GetAttribute(AttrDisallowInEmpireSpace) > EvilZero)
    and InterdictionMgr::IsRestricted(pSystem))
        return "CantInEmpireSpace";
    // a control tower can't be launched from inside another tower's force field
    if ((iRef->groupID() == EVEDB::invGroups::Control_Tower) and (pShipSE->SysBubble() != nullptr) and pShipSE->SysBubble()->HasTower()) {
        TowerSE* pTower(pShipSE->SysBubble()->GetTowerSE());
        if (pTower->InForceField(pShipSE->GetPosition()))
            return "CantLaunchInsideForcefield";
    }
    return "";
}

/* sovereignty structure placement rules.  "" if it may be launched here */
static std::string SovLaunchError(Client* pClient, SystemManager* pSystem, InventoryItemRef iRef)
{
    // empire, npc null-sec and w-space can't be claimed
    SystemData sysData = SystemData();
    sDataMgr.GetSystemData(pClient->GetSystemID(), sysData);
    std::string refusal(SovRules::LaunchRefusal(SovRules::Classify(pClient->GetSystemID(), sysData.factionID, sysData.securityRating)));
    if (!refusal.empty())
        return refusal;
    if (pClient->GetAllianceID() == 0)
        return "You must be part of an alliance to launch a sovereignty structure.";
    ShipSE* pShipSE(pClient->GetShipSE());
    uint32 sovAllianceID(svDataMgr.GetSystemAllianceID(pClient->GetSystemID()));
    switch (iRef->groupID()) {
        case EVEDB::invGroups::Sovereignty_Blockade_Units: {
            /* near a stargate, in a claimed system, one per gate.  whoever owns it, an online SBU counts:  "an alliance can
             *  even disrupt their own sovereignty claim by onlining SBUs in their own system" (EVElopedia Sovereignty guide)
             */
            bool atGate(false);
            for (auto cur : pSystem->GetOperationalStatics())
                if (cur.second->IsGateSE() and (cur.second->SysBubble() == pShipSE->SysBubble()))
                    atGate = true;
            if (!atGate)
                return "Sovereignty blockade units must be deployed near a stargate.";
            if (sovAllianceID == 0)
                return "You cannot launch a Sovereignty Blockade Unit in an unclaimed system.";
            SystemEntity* pGate(pSystem->GetClosestGateSE(pShipSE->GetPosition()));
            if ((pGate != nullptr) and pGate->GetGateSE()->HasSBU())
                return "There is already a Sovereignty Blockade Unit on this stargate.";
            for (auto cur : pSystem->GetOperationalStatics())
                if (cur.second->IsSBUSE() and (cur.second->SysBubble() == pShipSE->SysBubble()))
                    return "There is already a Sovereignty Blockade Unit on this stargate.";
        } break;
        case EVEDB::invGroups::Territorial_Claim_Units: {
            // todo:  check Structure.h  - these can be dropped and anchored, but not onlined if >1 in system
            for (auto cur : pSystem->GetOperationalStatics())
                if (cur.second->IsTCUSE())
                    return "There is already a Territorial Claim Unit in this system.";
            if (sovAllianceID != 0)
                return "This system has already been claimed.";
        } break;
        case EVEDB::invGroups::Infrastructure_Hubs: {
            for (auto cur : pSystem->GetOperationalStatics())
                if (cur.second->IsIHubSE())
                    return "There is already an Infrastructure Hub this system.";
            if (sovAllianceID != (uint32)pClient->GetAllianceID())
                return "You cannot launch an Infrastructure Hub in a system not claimed by your alliance.";
        } break;
    }
    return "";
}

/* moves one item from the pilot's cargo into space as a new entity of ownerID.  adds its new itemID to result.
 *  the entity is built by DynamicEntityFactory (structures, sov structures, orbitals, construction platforms, containers)
 */
bool ShipBound::LaunchToSpace(Client* pClient, InventoryItemRef iRef, uint32 ownerID, const GPoint& location, PyList* result, uint32 planetID/*0*/)
{
    SystemManager* pSystem(pClient->SystemMgr());
    ShipSE* pShipSE(pClient->GetShipSE());

    // one per launch
    InventoryItemRef lRef(iRef);
    if (!iRef->isSingleton() and (iRef->quantity() > 1)) {
        lRef = iRef->Split(1);
        if (lRef.get() == nullptr)
            return false;
    }

    DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.ownerID = ownerID;
        entity.factionID = pClient->GetWarFactionID();
        entity.allianceID = pClient->GetAllianceID();
        entity.corporationID = pClient->GetCorporationID();
        entity.itemID = lRef->itemID();
        entity.itemName = lRef->itemName();
        entity.typeID = lRef->typeID();
        entity.groupID = lRef->groupID();
        entity.categoryID = lRef->categoryID();
        entity.planetID = planetID;
        entity.position = location;

    // Move item from cargo bay to space: (and send OnItemChange packet)
    lRef->Move(pClient->GetLocationID(), flagNone, true);
    lRef->SetPosition(location);
    lRef->ChangeOwner(ownerID);

    SystemEntity* pSE = DynamicEntityFactory::BuildEntity(*pSystem, entity);
    if (pSE == nullptr) {
        //couldnt create entity.  move item back to orig location
        lRef->Donate(pClient->GetCharacterID(), pShipSE->GetID(), flagCargoHold);
        return false;
    }

    // item was successfully created.  set singleton
    lRef->ChangeSingleton(true);
    if (pSE->IsPOSSE())
        pSE->GetPOSSE()->Drop(pShipSE->SysBubble());
    pSystem->AddEntity(pSE);
    result->AddItem(new PyInt(entity.itemID));
    return true;
}

PyResult ShipBound::Drop(PyCallArgs &call, PyList* PyToDropList, std::optional <PyInt*> oOwnerID, PyBool* ignoreWarning)
{
    /* util.LaunchFromShip():  Drop([(itemID, qty)], ownerID, ignoreWarning).  returns {itemID: [newItemID..., (msgKey, {args})...]}
     *  each tuple is raised on the client as UserError(msgKey, args).  LaunchCPWarning is thrown, and the launch is retried
     *  with ignoreWarning once the pilot agrees.  "Launch for Self" sends our charID, "Launch for Corp" our corpID, drones nothing
     */
    _log(SHIP__INFO, "ShipBound::Handle_Drop()");
    call.Dump(SHIP__INFO);

    Client* pClient = call.client;
    SystemManager* pSystem = pClient->SystemMgr();
    ShipSE* pShipSE(pClient->GetShipSE());
    ShipItemRef shipRef(pClient->GetShip());
    if (!pClient->IsInSpace() or (pSystem == nullptr) or (pShipSE == nullptr) or (shipRef.get() == nullptr)) {
        _log(SERVICE__ERROR, "%s: Trying to drop items when not in space!", pClient->GetName());
        return new PyDict();
    }
    ShipItem* pShip(shipRef.get());     // the drone rules below use the active ship

    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());
    uint32 ownerID(oOwnerID.has_value() ? oOwnerID.value()->value() : charID);
    if ((ownerID != charID) and (ownerID != corpID)) {
        if (IsCorp(ownerID))
            throw UserError ("CantLaunchNotInCorp").AddOwnerName("corp", ownerID);
        throw UserError ("CantLaunchNotOwner");
    }
    bool npcCorpLaunch((ownerID == corpID) and IsNPCCorp(corpID));
    bool warn((ignoreWarning == nullptr) or !ignoreWarning->value());

    // the warnings are thrown before anything is launched, and the whole group comes back when the pilot agrees
    if (warn)
        for (PyList::const_iterator itr = PyToDropList->begin(); itr != PyToDropList->end(); ++itr) {
            if (!(*itr)->IsTuple() or ((*itr)->AsTuple()->size() < 2))
                continue;
            InventoryItemRef wRef(sItemFactory.GetItemRef((*itr)->AsTuple()->GetItem(0)->AsInt()->value()));
            if ((wRef.get() != nullptr) and (wRef->groupID() == EVEDB::invGroups::Construction_Platform))
                throw UserError ("LaunchCPWarning");
            // eveMisc.LaunchFromShip asks the same for outpost upgrade and improvement platforms
            if ((wRef.get() != nullptr) and ((wRef->groupID() == EVEDB::invGroups::Station_Upgrade_Platform)
                                          or (wRef->groupID() == EVEDB::invGroups::Station_Improvement_Platform)))
                throw UserError ("LaunchUpgradePlatformWarning");
        }

    PyDict* dict = new PyDict();
    bool shipDrop(false);
    double radius(pShipSE->GetRadius());
    for (PyList::const_iterator itr = PyToDropList->begin(); itr != PyToDropList->end(); ++itr) {
        if (!(*itr)->IsTuple() or ((*itr)->AsTuple()->size() < 2))
            continue;
        uint32 itemID((*itr)->AsTuple()->GetItem(0)->AsInt()->value());
        int32 qty((*itr)->AsTuple()->GetItem(1)->AsInt()->value());

        PyList* list = new PyList();
        // errors go in the item's result list as (msgName, args), which the client shows as UserErrors
        auto addError = [&](const char* msgName, PyDict* args) {
            PyTuple* err = new PyTuple(2);
                err->SetItem(0, new PyString(msgName));
                err->SetItem(1, args);
            list->AddItem(err);
        };
        auto addCustomError = [&](const std::string& text) {
            PyDict* args = new PyDict();
                args->SetItemString("error", new PyString(text));
            addError("CustomError", args);
        };

        InventoryItemRef iRef(sItemFactory.GetItemRef(itemID));
        if (iRef.get() == nullptr) {
            sLog.Error("ShipBound::Handle_Drop()", "%s: Unable to find item %u to drop.", pClient->GetName(), itemID);
            addCustomError("That item is no longer in your cargo hold.");
            dict->SetItem(new PyInt(itemID), list);
            continue;
        }

        GPoint location(pShipSE->GetPosition());
        location.MakeRandomPointOnSphereLayer(500 + radius + iRef->radius(), 1500 + radius + iRef->radius());

        if (iRef->categoryID() != EVEDB::invCategories::Drone) {
            PyDict* args(nullptr);
            const char* error(DropCheck(pClient, iRef, npcCorpLaunch, args));
            if (error[0] != '\0') {
                addError(error, args);
                dict->SetItem(new PyInt(itemID), list);
                continue;
            }
            PyDecRef(args);
        }

        switch (iRef->categoryID()) {
            case EVEDB::invCategories::Drone: {
                if (!sConfig.testing.EnableDrones) {
                    throw CustomError ("Drones are disabled.");
                }

                CharacterRef cRef(pClient->GetChar());
                ShipSE* pShipSE(pClient->GetShipSE());
                uint32 maxDrones(cRef->GetAttribute(AttrMaxActiveDrones).get_uint32());
                if (maxDrones < 1) {
                    throw UserError ("NoDroneManagementAbilities")
                            .AddFormatValue ("typeID", new PyInt (iRef->typeID ()));
                    //{'FullPath': u'UI/Messages', 'messageID': 259203, 'label': u'NoDroneManagementAbilitiesBody'}(u'You cannot launch {[item]typeID.nameWithArticle} because you do not have the ability to control any drones.', None, {u'{[item]typeID.nameWithArticle}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'nameWithArticle', 'args': 0, 'kwargs': {}, 'variableName': 'typeID'}})
                }
                if ((pShipSE == nullptr) or pClient->InPod())
                    throw CustomError ("You need a ship to launch drones.");

                if ((iRef->flag() != flagDroneBay) or (iRef->locationID() != pShip->itemID())) {
                    // {'FullPath': u'UI/Messages', 'messageID': 259680, 'label': u'DropItemNotInDroneBayBody'}(u'{[item]item.name} cannot be dropped because it is not in your drone bay.', None, {u'{[item]item.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'item'}})
                    PyDict* args = new PyDict();
                        args->SetItemString("item", new PyInt(iRef->typeID()));
                    addError("DropItemNotInDroneBay", args);
                    break;
                }

                int32 count(qty);
                int32 available(iRef->isSingleton() ? 1 : iRef->quantity());
                if (count > available)
                    count = available;
                double bandwidth(pShip->GetAttribute(AttrDroneBandwidth).get_double());
                double needed(iRef->type().HasAttribute(AttrDroneBandwidthUsed) ? iRef->type().GetAttribute(AttrDroneBandwidthUsed).get_double() : 0.0);
                for (int32 i = 0; i < count; ++i) {
                    if (DroneSE::CountControlledBy(pSystem, pClient->GetCharacterID()) >= maxDrones) {
                        //{'FullPath': u'UI/Messages', 'messageID': 259140, 'label': u'NoDroneManagementAbilitiesLeftBody'}(u'You cannot launch {[item]item.name} because you are already controlling {[numeric]limit} drones, as much as you have skill to.', None, {u'{[numeric]limit}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'limit'}, u'{[item]item.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'item'}})
                        PyDict* args = new PyDict();
                            args->SetItemString("item", new PyInt(iRef->typeID()));
                            args->SetItemString("limit", new PyInt(maxDrones));
                        addError("NoDroneManagementAbilitiesLeft", args);
                        break;
                    }
                    double used(DroneSE::BandwidthUsedBy(pSystem, pShipSE->GetID()));
                    if (used + needed > bandwidth) {
                        //{'FullPath': u'UI/Messages', 'messageID': 258041, 'label': u'MaxBandwidthExceeded2Body'}(u"You don't have enough bandwidth to launch {droneName}. You need {droneBandwidthUsed} Mbit/s but only have {bandwidthLeft} Mbit/s available.")
                        PyDict* args = new PyDict();
                            args->SetItemString("droneName", new PyString(iRef->itemName()));
                            args->SetItemString("droneBandwidthUsed", new PyInt(static_cast<int32>(needed)));
                            args->SetItemString("bandwidthLeft", new PyInt(static_cast<int32>(bandwidth - used)));
                        addError("MaxBandwidthExceeded2", args);
                        break;
                    }

                    // launch one drone off the stack.  the last one is the stack itself
                    InventoryItemRef dRef(iRef);
                    if (!iRef->isSingleton() and (iRef->quantity() > 1)) {
                        dRef = iRef->Split(1);
                        if (dRef.get() == nullptr) {
                            _log(INV__ERROR, "ShipBound::Handle_Drop() - Error splitting item %u. Skipping.", iRef->itemID());
                            break;
                        }
                    }
                    pShipSE->LaunchDrone(dRef);
                    list->AddItem(new PyInt(dRef->itemID()));
                    shipDrop = true;
                    if (dRef == iRef)
                        break;
                }
            } break;
            case EVEDB::invCategories::Structure: {
                // POS anchoring rules (charters, standings, moon distance, per-system limits) are F18's
                SystemEntity* pMoon(pSystem->GetClosestMoonSE(location));
                if ((pMoon == nullptr) or (pMoon->GetMoonSE() == nullptr)) {
                    addCustomError("Structures can only be launched near a moon.");
                    break;
                }
                if (iRef->groupID() == EVEDB::invGroups::Control_Tower) {
                    if (pMoon->GetMoonSE()->HasTower()) {
                        addCustomError("This Moon already has a Control Tower in orbit.");
                        break;
                    }
                } else if (!pMoon->GetMoonSE()->HasTower()) {
                    addCustomError("You need an anchored Control Tower before launching modules.");
                    break;
                }
                if (LaunchToSpace(pClient, iRef, ownerID, location, list))
                    shipDrop = true;
            } break;
            case EVEDB::invCategories::Deployable: {
                // "Launch for Self/Corp".  each one is launched unanchored, the owner anchors it from its menu
                int32 count(iRef->isSingleton() ? 1 : qty);
                if (count < 1)
                    count = 1;
                for (int32 i = 0; i < count; ++i) {
                    uint32 launchedID(LaunchDeployable(pClient, iRef, ownerID));
                    list->AddItem(new PyInt(launchedID));
                    if (launchedID == iRef->itemID())
                        break;      // that was the whole stack
                }
            } break;
            case EVEDB::invCategories::SovereigntyStructure: {
                std::string error(SovLaunchError(pClient, pSystem, iRef));
                if (!error.empty()) {
                    addCustomError(error);
                    break;
                }
                if (LaunchToSpace(pClient, iRef, ownerID, location, list))
                    shipDrop = true;
            } break;
            case EVEDB::invCategories::Orbitals: {
                // the gantry floats until it is anchored at a planet (CustomsSE::AnchorOrbital)
                if (iRef->groupID() != EVEDB::invGroups::Orbital_Construction_Platform)
                    break;
                if (LaunchToSpace(pClient, iRef, ownerID, location, list))
                    shipDrop = true;
            } break;
            case EVEDB::invCategories::Celestial: {
                switch (iRef->groupID()) {
                    case EVEDB::invGroups::Construction_Platform: {   // outpost construction platforms
                        /* our alliance must hold sovereignty here, and "there can only be a single outpost or station per
                         *  conquerable solar system" (Ten Ton Hammer outpost guide, 2010).  InventoryBound::Build checks again */
                        uint32 planetID(pSystem->GetClosestPlanetID(pShipSE->GetPosition()));
                        if (planetID == 0) {
                            addCustomError("There is no planet in this system to build an outpost at.");
                            break;
                        }
                        if (svDataMgr.GetSystemAllianceID(pClient->GetSystemID()) != (uint32)pClient->GetAllianceID()) {
                            addCustomError("You cannot launch Construction Platforms in a system which your alliance does not control.");
                            break;
                        }
                        if (sDataMgr.GetStationCount(pClient->GetSystemID()) > 0) {
                            addCustomError("There is already a station in this system.  Only one outpost or station can be in a solar system.");
                            break;
                        }
                        if (LaunchToSpace(pClient, iRef, ownerID, location, list))
                            shipDrop = true;
                    } break;
                    case EVEDB::invGroups::Station_Upgrade_Platform:        // Foundation, Pedestal and Monument
                    case EVEDB::invGroups::Station_Improvement_Platform: {  // outpost improvements
                        // they are anchored and built at the system's outpost, which must be the launching corp's
                        uint32 outpostID(stDataMgr.GetOutpostInSystem(pClient->GetSystemID()));
                        if (outpostID == 0) {
                            addCustomError("There is no outpost in this system to upgrade.");
                            break;
                        }
                        if (stDataMgr.GetOwnerID(outpostID) != (uint32)pClient->GetCorporationID()) {
                            addCustomError("Only the corporation that owns the outpost can upgrade it.");
                            break;
                        }
                        if (LaunchToSpace(pClient, iRef, ownerID, location, list))
                            shipDrop = true;
                    } break;
                    case EVEDB::invGroups::Cargo_Container:
                    case EVEDB::invGroups::Secure_Cargo_Container:
                    case EVEDB::invGroups::Audit_Log_Secure_Container:
                    case EVEDB::invGroups::Freight_Container: {
                        if (LaunchToSpace(pClient, iRef, ownerID, location, list))
                            shipDrop = true;
                    } break;
                }
            } break;
            default: {
                _log(INV__ERROR, "ShipBound::Handle_Drop() - Item %s (cat %u) is not launchable.", iRef->name(), iRef->categoryID());
            }
        }

        // nothing launched and no reason given: the item can't be launched from a ship
        if (list->empty()) {
            PyDict* args = new PyDict();
                args->SetItemString("item", Client::TypeNameArg(iRef->typeID()));
                args->SetItemString("containerTypeID", new PyInt(shipRef->typeID()));
            addError("CantLaunchItemUnlaunchable", args);
        }
        dict->SetItem(new PyInt(itemID), list);
    }

    if (shipDrop)
        pShipSE->DestinyMgr()->SendJettisonPacket();

    return dict;
}

PyResult ShipBound::Scoop(PyCallArgs &call, PyInt* itemID, std::optional<PyRep*> password) {
    // ship.Scoop(objectID[, password]).  the password is sent on a retry after ShpScoopSecureCC
    Client* pClient(call.client);
    SystemManager* pSysMgr(pClient->SystemMgr());
    if (pSysMgr == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no system manager.", pClient->GetName());
        return PyStatic.mtDict();
    }
    SystemEntity* pSE = pSysMgr->GetSE(itemID->value());
    if (pSE == nullptr) {
        _log(SERVICE__ERROR, "%s: Unable to find object %i to scoop.", pClient->GetName(), itemID->value());
        return PyStatic.mtDict();
        //{'FullPath': u'UI/Messages', 'messageID': 258825, 'label': u'ScoopObjectGoneBody'}(u'{target} is no longer there.', None, {u'{target}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'target'}})
    }

    //CannotScoopTargetedShip
    //CantScoopAnchoring
    //CantScoopOwnerNpc
    //CantScoopThat
    //ScoopObjectGone
    //ShpScoopSecureCC
    //CanOnlyScoopIfUnanchored

    // check to see if this object is anchored and if so, refuse to scoop it
    if (pSE->IsContainerSE())
        if (pSE->GetContSE()->IsAnchored())
            throw CustomError ("%s is anchored.  Cannot scoop.", pSE->GetName());
    if (pSE->IsDeployableSE()) {
        if (pSE->GetDeployableSE()->IsAnchored())
            throw CustomError ("%s is anchored.  Cannot scoop.", pSE->GetName());
        uint32 ownerID(pSE->GetOwnerID());
        if ((ownerID != pClient->GetCharacterID()) and (ownerID != (uint32)pClient->GetCorporationID()))
            throw CustomError ("You do not own the %s.  Cannot scoop.", pSE->GetName());
    }

    // orbitals:  only an unanchored gantry of ours comes back aboard (customs offices never)
    if (pSE->IsCOSE()) {
        CustomsSE* pCO(pSE->GetCOSE());
        if (!pCO->IsGantry() or (pCO->GetState() != EVEPOS::EntityState::Unanchored) or (pCO->GetPlanetID() != 0))
            throw CustomError ("%s is anchored.  Cannot scoop.", pSE->GetName());
        uint32 ownerID(pSE->GetOwnerID());
        if ((ownerID != pClient->GetCharacterID()) and (ownerID != (uint32)pClient->GetCorporationID()))
            throw CustomError ("You do not own the %s.  Cannot scoop.", pSE->GetName());
    }

    // drones: ours, or abandoned
    if (pSE->IsDroneSE()) {
        DroneSE* pDrone(pSE->GetDroneSE());
        uint32 charID(pClient->GetCharacterID());
        if ((pDrone->IsControlled() and (pDrone->GetControllerOwnerID() != charID))
        or  (IsCharacterID(pDrone->GetOwnerID()) and (pDrone->GetOwnerID() != charID)))
            throw CustomError ("%s is under another pilot's control.  Cannot scoop.", pSE->GetName());
        pDrone->ScoopTo(pClient->GetShipSE(), flagCargoHold);   // throws if it won't fit
        SafeDelete(pDrone);
        return PyStatic.mtDict();
    }

    InventoryItemRef iRef = pSE->GetSelf();
    if (iRef.get() == nullptr) {
        codelog(CLIENT__ERROR, "ItemRef for %i not found.", itemID->value());
        return PyStatic.mtDict();
    }

    // someone else's password protected secure container is disarmed with its password, or it won't come aboard
    if (pSE->IsContainerSE() and ContainerSecurity::IsSecure(iRef)
    and (iRef->ownerID() != (uint32)pClient->GetCharacterID())
    and sContSec.HasPassword(iRef->itemID(), Inv::SecPassword::General)) {
        if (!password.has_value() or !sContSec.MatchPassword(iRef->itemID(), Inv::SecPassword::General, password.value()))
            throw UserError("ShpScoopSecureCC");
    }

    // Check cargo bay capacity:
    if (pClient->GetShip()->GetMyInventory()->ValidateAddItem(flagCargoHold, iRef)) {  // this will throw if it fails
        // We have enough Cargo bay capacity to hold the item being scooped,
        // so take ownership of it
        iRef->ChangeOwner(pClient->GetCharacterID(), true);
        // perform data cleanup for structures
        if (pSE->IsPOSSE())
            pSE->GetPOSSE()->Scoop();
        if (pSE->IsCOSE())
            PosMgrDB::DeleteCustomsData(iRef->itemID());
        // move it into the cargo bay:
        pClient->MoveItem(iRef->itemID(), pClient->GetShipID(), flagCargoHold);
        pSysMgr->RemoveEntity(pSE);
        // delete SE since item is no longer in space
        SafeDelete(pSE);
    }

    return PyStatic.mtDict();
}

/* returns {droneID: (msgName, args)} for drones that could not be scooped, as the entity commands do.
 *  a drone we control that is too far away is told to come back to the bay instead
 */
PyResult ShipBound::ScoopDrone(PyCallArgs &call, PyList* itemIDs) {
    Client* pClient(call.client);
    SystemManager* pSysMgr(pClient->SystemMgr());
    ShipSE* pShipSE(pClient->GetShipSE());
    PyDict* errors = new PyDict();
    if ((pSysMgr == nullptr) or (pShipSE == nullptr))
        return errors;
    uint32 charID(pClient->GetCharacterID());

    for (PyList::const_iterator itr = itemIDs->begin(); itr != itemIDs->end(); ++itr) {
        if (!(*itr)->IsInt())
            continue;
        uint32 droneID((*itr)->AsInt()->value());
        SystemEntity* pSE(pSysMgr->GetSE(droneID));
        if ((pSE == nullptr) or !pSE->IsDroneSE()) {
            //{'FullPath': u'UI/Messages', 'messageID': 258825, 'label': u'ScoopObjectGoneBody'}(u'{target} is no longer there.')
            PyDict* args = new PyDict();
                args->SetItemString("target", new PyString("The drone"));
            PyTuple* err = new PyTuple(2);
                err->SetItem(0, new PyString("ScoopObjectGone"));
                err->SetItem(1, args);
            errors->SetItem(new PyInt(droneID), err);
            continue;
        }
        DroneSE* pDrone(pSE->GetDroneSE());

        // ours, or abandoned
        if ((pDrone->IsControlled() and (pDrone->GetControllerOwnerID() != charID))
        or  (IsCharacterID(pDrone->GetOwnerID()) and (pDrone->GetOwnerID() != charID)))
            throw CustomError ("The %s is under another pilot's control.  Cannot scoop.", pDrone->GetName());

        double distance(pDrone->GetPosition().distance(pShipSE->GetPosition()) - pDrone->GetRadius() - pShipSE->GetRadius());
        if (distance > 2500.0) {
            if (pDrone->GetControllerID() == pShipSE->GetID()) {
                pDrone->GetAI()->ReturnToBay();
                continue;
            }
            throw CustomError ("The %s is too far away to scoop.", pDrone->GetName());
        }

        // these throw if the drone won't fit
        pDrone->ScoopTo(pShipSE, flagDroneBay);
        SafeDelete(pDrone);
    }

    return errors;
}

PyResult ShipBound::Jettison(PyCallArgs &call, PyList* itemIDs) {
    /* menusvc.Jettison():  any cargo hold item.  everything goes into one new jetcan (structures and deployables too, as
     *  packaged cargo; launching is Drop's job).  freighters had no jettison tube before Retribution
     */
    Client* pClient(call.client);
    ShipSE* pShipSE(pClient->GetShipSE());
    SystemManager* pSysMgr(pClient->SystemMgr());
    if (!pClient->IsInSpace() or (pShipSE == nullptr) or (pSysMgr == nullptr)) {
        _log(SERVICE__ERROR, "%s: Trying to jettison items when not in space!", pClient->GetName());
        return nullptr;
    }
    ShipItemRef shipRef(pClient->GetShip());
    if ((shipRef->groupID() == EVEDB::invGroups::Freighter) or (shipRef->groupID() == EVEDB::invGroups::JumpFreighter))
        throw UserError ("ShpCantUseCargoInSpaceJettison");

    // only items in our own cargo hold (not fitted modules, drones or anyone else's things)
    std::vector<InventoryItemRef> items;
    std::set<uint32> seen;
    for (PyList::const_iterator itr = itemIDs->begin(); itr != itemIDs->end(); ++itr) {
        if (!(*itr)->IsInt())
            continue;
        uint32 itemID((*itr)->AsInt()->value());
        if (!seen.insert(itemID).second)
            continue;
        InventoryItemRef iRef(sItemFactory.GetItemRef(itemID));
        if (iRef.get() == nullptr)
            continue;
        if ((iRef->locationID() != shipRef->itemID()) or (iRef->flag() != flagCargoHold))
            throw UserError ("CannotJettisonItem").AddFormatValue("typeID", new PyInt(iRef->typeID()));
        items.push_back(iRef);
    }
    if (items.empty())
        throw UserError ("NothingToJettison");

    if (!pClient->IsJetcanAvalible())
        throw UserError ("ShpJettisonPending")
                .AddTimeShort ("eta", pClient->JetcanTime() * EvE::Time::Second);

    // Spawn jetcan
    GPoint location(pShipSE->GetPosition());
    location.MakeRandomPointOnSphere(500.0f);
    ItemData p_idata(
                    23,                         // 23 = cargo container
                    pClient->GetCharacterID(),
                    pClient->GetLocationID(),
                    flagNone,
                    "Jettisoned Cargo Container",
                    location);
    CargoContainerRef jcRef(sItemFactory.SpawnCargoContainer(p_idata));
    if (jcRef.get() == nullptr)
        throw CustomError ("Unable to spawn jetcan.");

    FactionData data = FactionData();
        data.allianceID = pClient->GetAllianceID();
        data.corporationID = pClient->GetCorporationID();
        data.factionID = pClient->GetWarFactionID();
        data.ownerID = pClient->GetCharacterID();
    ContainerSE* cSE = new ContainerSE(jcRef, this->GetServiceManager(), pSysMgr, data);
    cSE->Init();
    jcRef->SetMySE(cSE);
    // set anchored to avoid deletion while it is empty
    jcRef->SetAnchor(true);
    pSysMgr->AddEntity(cSE);
    pShipSE->DestinyMgr()->SendJettisonPacket();
    pClient->StartJetcanTimer();

    bool notAll(false);
    for (auto iRef : items) {
        if (jcRef->GetMyInventory()->HasAvailableSpace(flagNone, iRef)) {
            pClient->MoveItem(iRef->itemID(), jcRef->itemID(), flagNone);
            continue;
        }
        // as many of the stack as will fit, then stop
        float unitVolume(iRef->GetAttribute(AttrVolume).get_float());
        if (!iRef->isSingleton() and (unitVolume > 0)) {
            int32 count((int32)floor(jcRef->GetMyInventory()->GetRemainingCapacity(flagNone) / unitVolume));
            if ((count > 0) and (count < iRef->quantity())) {
                InventoryItemRef part(iRef->Split(count));
                if (part.get() != nullptr)
                    part->Move(jcRef->itemID(), flagNone, true);
            }
        }
        _log(ITEM__WARNING, "%s: Jetcan %u is full.", pClient->GetName(), jcRef->itemID());
        notAll = true;
        break;
    }

    //jetcan should spawn unanchored
    jcRef->SetAnchor(false);
    // the can is out there already, so this is only a message
    if (notAll)
        pClient->SendRemoteMessage("NotAllItemsWereMoved");

    return nullptr;
}

uint32 ShipBound::LaunchDeployable(Client* pClient, InventoryItemRef iRef, uint32 ownerID)
{
    SystemManager* pSystem(pClient->SystemMgr());
    ShipSE* pShipSE(pClient->GetShipSE());
    if ((pSystem == nullptr) or (pShipSE == nullptr) or !pClient->IsInSpace())
        throw CustomError ("You must be in space to launch the %s.", iRef->name());
    if (iRef->locationID() != pShipSE->GetID())
        throw CustomError ("The %s is not in your ship.", iRef->name());

    // mobile warp disruptors only work in 0.0 and w-space
    if (iRef->type().HasAttribute(AttrDisallowInEmpireSpace)
    and (iRef->type().GetAttribute(AttrDisallowInEmpireSpace) > EvilZero)
    and InterdictionMgr::IsRestricted(pSystem))
        throw UserError ("CantInEmpireSpace");

    // required skills (anchoring, propulsion jamming...)
    CharacterRef cRef(pClient->GetChar());
    static const uint16 skills[2][2] = { { AttrRequiredSkill1, AttrRequiredSkill1Level }, { AttrRequiredSkill2, AttrRequiredSkill2Level } };
    for (uint8 i = 0; i < 2; ++i) {
        if (!iRef->type().HasAttribute(skills[i][0]))
            continue;
        uint16 skillID(iRef->type().GetAttribute(skills[i][0]).get_uint32());
        uint8 level(iRef->type().HasAttribute(skills[i][1]) ? iRef->type().GetAttribute(skills[i][1]).get_uint32() : 1);
        if ((skillID > 0) and !cRef->HasSkillTrainedToLevel(skillID, level))
            throw CustomError ("You don't have the skills required to launch the %s.", iRef->name());
    }

    // one per launch.  the last one is the stack itself
    InventoryItemRef dRef(iRef);
    if (!iRef->isSingleton() and (iRef->quantity() > 1)) {
        dRef = iRef->Split(1);
        if (dRef.get() == nullptr)
            throw CustomError ("Unable to launch the %s.", iRef->name());
    }

    // launched for the corp only when the client asks for our corp
    if (ownerID != (uint32)pClient->GetCorporationID())
        ownerID = pClient->GetCharacterID();

    DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.ownerID = ownerID;
        entity.factionID = pClient->GetWarFactionID();
        entity.allianceID = pClient->GetAllianceID();
        entity.corporationID = pClient->GetCorporationID();
        entity.itemID = dRef->itemID();
        entity.itemName = dRef->itemName();
        entity.typeID = dRef->typeID();
        entity.groupID = dRef->groupID();
        entity.categoryID = dRef->categoryID();

    GPoint location(pShipSE->GetPosition());
    location.MakeRandomPointOnSphereLayer(500 + pShipSE->GetRadius(), 1500 + pShipSE->GetRadius());

    // Move item from cargo bay to space: (and send OnItemChange packet)
    dRef->Move(pClient->GetLocationID(), flagNone, true);
    dRef->SetPosition(location);
    dRef->ChangeOwner(ownerID);
    dRef->ChangeSingleton(true);
    // launched unanchored, at full shields
    dRef->SetAttribute(AttrOnline, EvilZero, false);
    dRef->SetAttribute(AttrShieldCharge, dRef->GetAttribute(AttrShieldCapacity), false);
    dRef->SaveItem();
    entity.position = location;

    SystemEntity* pSE = DynamicEntityFactory::BuildEntity(*pSystem, entity);
    if (pSE == nullptr) {
        //couldnt create entity.  move item back to the cargo bay
        dRef->Donate(pClient->GetCharacterID(), pShipSE->GetID(), flagCargoHold);
        throw CustomError ("Unable to launch the %s.", iRef->name());
    }

    pSystem->AddEntity(pSE);
    pShipSE->DestinyMgr()->SendJettisonPacket();
    _log(ITEM__TRACE, "ShipBound::LaunchDeployable() - %s launched %s(%u) for %u", pClient->GetName(), dRef->name(), dRef->itemID(), ownerID);
    return dRef->itemID();
}

PyResult ShipBound::AssembleShip(PyCallArgs& args, PyInt* shipID) {
    // this one has subSystems as a byname argument, handled by the AssembleShip call with a PyList for now
    PyList* list = new PyList();

    PyIncRef(shipID);   // shipID belongs to the call arguments
    list->AddItem(shipID);

    PyResult result(this->AssembleShip(args, list));
    PyDecRef(list);
    return result;
}

PyResult ShipBound::AssembleShip(PyCallArgs &call, PyList* itemIDs) {
    /* 13:05:41 [BindDump] NodeID: 888444 BindID: 129 calling AssembleShip in service manager 'ShipBound'
     * 13:05:41 [BindDump]   Call Arguments:
     * 13:05:41 [BindDump]       Tuple: 1 elements
     * 13:05:41 [BindDump]         [ 0] List: 5 elements
     * 13:05:41 [BindDump]         [ 0]   [ 0] Integer field: 140000073
     * 13:05:41 [BindDump]         [ 0]   [ 1] Integer field: 140000074
     * 13:05:41 [BindDump]         [ 0]   [ 2] Integer field: 140000075
     * 13:05:41 [BindDump]         [ 0]   [ 3] Integer field: 140000076
     * 13:05:41 [BindDump]         [ 0]   [ 4] Integer field: 140000077
     *
     *              [PyTuple 2 items]     << response to AssembleShip call
     *                [PyList 1 items]
     *                  [PyPackedRow 33 bytes]
     *                    ["itemID" => <1002333477860> [I8]]
     *                    ["typeID" => <24700> [I4]]
     *                    ["ownerID" => <1661059544> [I4]]
     *                    ["locationID" => <61000012> [I8]]
     *                    ["flagID" => <4> [I2]]
     *                    ["quantity" => <-1> [I4]]
     *                    ["groupID" => <419> [I2]]
     *                    ["categoryID" => <6> [I2]]
     *                    ["customInfo" => <empty string> [Str]]
     *                [PyDict 1 kvp]
     *                  [PyInt 10]        << flagdisconnect??
     *                  [PyInt 0]
     */

    call.Dump(COLLECT__CALL_DUMP);
    if (call.tuple->empty())
        return nullptr;

    bool t3Ship = false;
    std::vector<int32> itemIDList;
    PyList* subSystemList(nullptr);
    if (call.byname.find("subSystems") != call.byname.end()) {
        /*
         * 21:29:15 [Bound] ShipBound::AssembleShip()
         * 21:29:15 [CollectCallDump]   Call Arguments:
         * 21:29:15 [CollectCallDump]      Tuple: 1 elements
         * 21:29:15 [CollectCallDump]       [ 0]    Integer: 140023628
         * 21:29:15 [CollectCallDump]  Named Arguments:
         * 21:29:15 [CollectCallDump]   subSystems
         * 21:29:15 [CollectCallDump]       List: 5 elements
         * 21:29:15 [CollectCallDump]       [ 0]    Integer: 140023637
         * 21:29:15 [CollectCallDump]       [ 1]    Integer: 140023638
         * 21:29:15 [CollectCallDump]       [ 2]    Integer: 140023635
         * 21:29:15 [CollectCallDump]       [ 3]    Integer: 140023634
         * 21:29:15 [CollectCallDump]       [ 4]    Integer: 140023636
         *
         * 16:31:52 W AssembleShip: byname call is Invalid Type
         *
         */
        // this is a list of subSystems for t3 assembly
        t3Ship = true;
        itemIDList.push_back(PyRep::IntegerValueU32(call.tuple->GetItem(0)));
        subSystemList = call.byname.find("subSystems")->second->AsList();
    } else if (call.tuple->GetItem(0)->IsList()) {
        PyList::const_iterator list_2_cur = itemIDs->begin();
        for (size_t list_2_index(0); list_2_cur != itemIDs->end(); ++list_2_cur, ++list_2_index) {
            if (!(*list_2_cur)->IsInt()) {
                _log(XMLP__DECODE_ERROR, "Decode Call_AssembleShip failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
                return nullptr;
            }

            const PyInt* t = (*list_2_cur)->AsInt();
            itemIDList.push_back(t->value());
        }
    } else if (call.tuple->size() == 2) { // this one doesn't seem to be anywhere in the normal client code
        if (call.tuple->GetItem(0)->IsInt()
        and call.tuple->GetItem(1)->IsString()) {
            // This block is for how DNA calls AssembleShip
            // @TODO Ignoring name
            // Can't get xmlpktgen to pickup the change so.. lol
            //if (!argsNamed.Decode(&call.tuple)) {
            //    codelog(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
            //    return nullptr;
            //}
            itemIDList.push_back(PyRep::IntegerValueU32(call.tuple->GetItem(0)));
        } else {
            sLog.Error("AssembleShip", "tuple size == 2 and ([0] != int and [1] != string) or some shit like that.");
        }
    } else {
        sLog.Error( "Handle_AssembleShip", "end of conditional" );
        return nullptr;
    }

    ShipItemRef ship(nullptr);
    for (auto cur : itemIDList) {
        ship = sItemFactory.GetShipRef(cur);

        if (ship.get() == nullptr) {
            _log(ITEM__ERROR, "Failed to load ship %i to assemble.", cur);
            continue;
        }

        // mine, or my corp's (an office or corporate hangar array division i may take from)
        if (ship->ownerID() != call.client->GetCharacterID()) {
            uint32 locationID(call.client->IsDocked() ? call.client->GetStationID() : call.client->GetLocationID());
            if ((ship->ownerID() != call.client->GetCorporationID())
            or  !Inventory::CanTakeFromCorpHangar(call.client, locationID, ship->flag()))
                throw CustomError ("You are not allowed to assemble the %s.", ship->name());
        }

        //check if the ship is a stack
        if (ship->quantity() > 1) {
            // Split the stack into a new inventory item with quantity of one, cast to ShipItemRef then assembled:
            // original item stack will be left with qty-1 at original location
            ship = ShipItemRef::StaticCast(ship->Split(1, true));
            if (ship.get() == nullptr) {
                _log(ITEM__ERROR, "Failed to split stack to assemble ship %i.", cur);
                continue;
            }
        }

        if ( t3Ship ) {
            if (subSystemList == nullptr) {
                // send error here, then exit
                sLog.Error("AssembleShip", "subSystemList == nullptr for %s", call.client->GetName());
                return nullptr;
            }
            /* the client sends the subsystems for the empty slots (assembleModularShip.Submit).  each must be ours, in this
             *  station, a subsystem for this hull, for a free slot and not the same slot as another.  all are checked
             *  before any is moved.  (the client refuses this in space with CantAssembleModularShipInSpace)
             */
            if (!call.client->IsDocked() or (ship->locationID() != call.client->GetStationID()))
                throw UserError ("CantAssembleModularShipInSpace");
            std::vector<InventoryItemRef> fitted;
            ship->GetMyInventory()->GetItemsByFlagRange(flagSubSystem0, flagSubSystem4, fitted);
            std::set<uint32> slots;
            for (auto cur : fitted)
                slots.insert(cur->flag());
            std::vector<InventoryItemRef> subSystems;
            PyList::const_iterator itr = subSystemList->begin(), end = subSystemList->end();
            for (; itr != end; ++itr) {
                InventoryItemRef subRef(sItemFactory.GetItemRef(PyRep::IntegerValueU32(*itr)));
                if (subRef.get() == nullptr)
                    throw CustomError ("A subsystem for your %s was not found.", ship->name());
                uint32 slot(subRef->GetAttribute(AttrSubSystemSlot).get_uint32());
                if ((subRef->ownerID() != call.client->GetCharacterID())
                or  (subRef->locationID() != ship->locationID())
                or  (subRef->categoryID() != EVEDB::invCategories::Subsystem)
                or  (subRef->GetAttribute(AttrFitsToShipType).get_uint32() != ship->typeID())
                or  !IsSubSystem(slot)
                or  (slots.find(slot) != slots.end()))
                    throw UserError ("SubSystemFitFailed")
                            .AddTypeName ("subSystemName", subRef->typeID ());
                slots.insert(slot);
                subSystems.push_back(subRef);
            }
            if ((fitted.size() + subSystems.size()) != ship->GetAttribute(AttrMaxSubSystems).get_uint32())
                throw UserError ("NotEnoughSubSystems")
                        .AddFormatValue ("ship", new PyInt (ship->typeID ()));

            // Move the specified subsystems to the newly assembled Tech 3 ship
            for (auto subRef : subSystems) {
                if (subRef->quantity() > 1)
                    subRef = subRef->Split(1);
                if (subRef.get() == nullptr)
                    continue;
                EVEItemFlags flag((EVEItemFlags)subRef->GetAttribute(AttrSubSystemSlot).get_uint32());
                if (ship->HasPilot()) {
                    // our active ship: its module manager fits it
                    ship->AddItemByFlag(flag, subRef, call.client);
                } else {
                    subRef->ChangeSingleton(true, false);
                    subRef->Move(ship->itemID(), flag, true);
                }
            }
        }

        ship->ChangeSingleton(true, true);
    }
    return nullptr;
}

PyResult ShipBound::GetShipConfiguration(PyCallArgs &call)
{
    PyDict* dict = new PyDict();
    dict->SetItemString("allowFleetSMBUsage", new PyBool(call.client->GetShipSE()->GetFleetSMBUsage()));
    return dict;
}

PyResult ShipBound::ConfigureShip(PyCallArgs &call, PyDict* configuration)
{
    PyRep* allow = configuration->GetItemString("allowFleetSMBUsage");
    if ((allow != nullptr) and allow->IsBool())
        call.client->GetShipSE()->SetFleetSMBUsage(allow->AsBool()->value());

    return nullptr;
}


/* ship maintenance array (POS) and ship maintenance bay (carriers, supercarriers, titans, Orca, Rorqual)
 *  Crucible rules: stored ships are assembled and carry nothing but charges in their holds (world.StoreShipWithCargo for the
 *  later rule), the Rorqual only takes industrial classes, criminals and pilots with live aggression can't store their ship.
 *  an SMA can be used by anyone in the owner's corp or alliance (there were no SMA access settings yet).  someone else's
 *  maintenance bay needs the same corp, or the same fleet when its pilot allows fleet access (ConfigureShip)
 */
static const float maxShipMaintenanceDistance = 5000.0f;    // client maxConfigureDistance (surface distance)
static const float maxShipScoopDistance = 2500.0f;          // client maxCargoContainerTransferDistance

static double SurfaceDistance(SystemEntity* pA, SystemEntity* pB)
{
    double distance(pA->GetPosition().distance(pB->GetPosition()) - pA->GetRadius() - pB->GetRadius());
    return (distance < 0 ? 0 : distance);
}

SystemEntity* ShipBound::GetShipContainer(Client* pClient, uint32 containerID, EVEItemFlags& flag, bool forLaunch)
{
    SystemManager* pSystem(pClient->SystemMgr());
    ShipSE* pMyShipSE(pClient->GetShipSE());
    if ((pSystem == nullptr) or (pMyShipSE == nullptr))
        throw UserError ("CantLaunchContainerNotPresent");
    SystemEntity* pSE(pSystem->GetSE(containerID));
    if ((pSE == nullptr) or pSE->IsDead())
        throw UserError ("CantLaunchContainerNotPresent");

    uint16 groupID(pSE->GetSelf()->groupID());
    if (pSE->IsPOSSE() and ((groupID == EVEDB::invGroups::Ship_Maintenance_Array)
                        or (forLaunch and (groupID == EVEDB::invGroups::Assembly_Array)))) {
        if (pSE->GetPOSSE()->GetState() < EVEPOS::StructureState::Online)
            throw CustomError ("The %s is not online.", pSE->GetName());
        // the client's checkIsMineOrCorpsOrAlliances
        if ((pSE->GetCorporationID() != pClient->GetCorporationID())
        and ((pClient->GetAllianceID() == 0) or (pSE->GetAllianceID() != pClient->GetAllianceID())))
            throw CustomError ("You do not have access to the %s.", pSE->GetName());
        flag = flagNone;
    } else if (pSE->IsShipSE() and pSE->GetSelf()->HasAttribute(AttrHasShipMaintenanceBay)) {
        bool access(pSE == pMyShipSE);
        if (!access)
            access = (pSE->GetOwnerID() == pClient->GetCharacterID());
        if (!access and pSE->HasPilot()) {
            Client* pPilot(pSE->GetPilot());
            if (pPilot->GetCorporationID() == pClient->GetCorporationID())
                access = true;
            else if ((pClient->GetFleetID() != 0) and (pPilot->GetFleetID() == pClient->GetFleetID()) and pSE->GetShipSE()->GetFleetSMBUsage())
                access = true;
        }
        if (!access)
            throw CustomError ("You cannot do that because you are not a fellow corporation member of the pilot, or in their fleet (if they have allowed fleet members access).");
        flag = flagShipHangar;
    } else {
        throw UserError ("CantLaunchContainerIncompatible")
                .AddFormatValue("structure", new PyString(sDataMgr.GetGroupName(groupID)));
    }

    if ((pSE != pMyShipSE) and (SurfaceDistance(pMyShipSE, pSE) > maxShipMaintenanceDistance))
        throw UserError ("NotCloseEnoughToAdd").AddFormatValue("maxdist", new PyInt(maxShipMaintenanceDistance));

    return pSE;
}

void ShipBound::CheckStorable(Client* pClient, ShipItemRef shipRef, SystemEntity* pDest, EVEItemFlags flag)
{
    InventoryItemRef destRef(pDest->GetSelf());
    if (shipRef->groupID() == EVEDB::invGroups::Capsule)
        throw UserError ("CannotStoreCapsule");
    if (!shipRef->isSingleton())
        throw UserError ("CannotPlaceUnassembledShipInSMA")
                .AddFormatValue("container", new PyInt(destRef->typeID()))
                .AddFormatValue("type", new PyInt(shipRef->typeID()));
    if (shipRef->HasPilot() and (shipRef->GetPilot() != pClient))
        throw UserError ("CannotPlacePilotedShipInStructure")
                .AddFormatValue("container", new PyInt(destRef->typeID()))
                .AddFormatValue("type", new PyInt(shipRef->typeID()));

    // cargo.  fitted modules, their charges, rigs and drones are fine
    std::vector<InventoryItemRef> contents;
    shipRef->GetMyInventory()->GetInventoryVec(contents);
    for (auto cur : contents) {
        if (!IsCargoHoldFlag(cur->flag()))
            continue;
        if (sConfig.world.StoreShipWithCargo) {
            // the later rule: only assembled containers are refused
            uint16 groupID(cur->groupID());
            if (cur->isSingleton() and ((groupID == EVEDB::invGroups::Cargo_Container)
                                     or (groupID == EVEDB::invGroups::Secure_Cargo_Container)
                                     or (groupID == EVEDB::invGroups::Audit_Log_Secure_Container)
                                     or (groupID == EVEDB::invGroups::Freight_Container)))
                throw UserError ("CannotStoreCargoNotEmpty").AddFormatValue("ship", new PyString(shipRef->itemName()));
        } else if (cur->categoryID() != EVEDB::invCategories::Charge) {
            throw UserError ("CannotStoreContainsCargo");
        }
    }

    // the Rorqual's bay only takes industrial classes
    if ((destRef->typeID() == EVEDB::invTypes::Rorqual) and (flag == flagShipHangar)
    and (shipRef->groupID() != EVEDB::invGroups::MiningBarge)
    and (shipRef->groupID() != EVEDB::invGroups::Exhumer)
    and (shipRef->groupID() != EVEDB::invGroups::Industrial)
    and (shipRef->groupID() != EVEDB::invGroups::TransportShip))
        throw UserError ("CannotAddShipTypeToMaintainanceBay");

    if (!destRef->GetMyInventory()->HasAvailableSpace(flag, shipRef))
        throw UserError ("CannotStoreDestinationUnsuitable")
                .AddFormatValue("ship", new PyString(shipRef->itemName()))
                .AddFormatValue("dest", new PyString(destRef->itemName()));
}

ShipItemRef ShipBound::GetStoredShip(Client* pClient, SystemEntity* pContainer, uint32 shipID)
{
    InventoryItemRef iRef(sItemFactory.GetItemRef(shipID));
    uint32 containerTypeID(pContainer->GetSelf()->typeID());
    if ((iRef.get() == nullptr) or (iRef->locationID() != pContainer->GetID()))
        throw UserError ("CantLaunchItemNotPresent")
                .AddFormatValue("itemTypeID", new PyInt((iRef.get() == nullptr) ? 0 : iRef->typeID()))
                .AddFormatValue("containerTypeID", new PyInt(containerTypeID));
    if (iRef->categoryID() != EVEDB::invCategories::Ship)
        throw UserError ("CantLaunchItemUnlaunchable")
                .AddFormatValue("item", new PyString(iRef->itemName()))
                .AddFormatValue("containerTypeID", new PyInt(containerTypeID));
    if (!iRef->isSingleton())
        throw UserError ("CantLaunchItemNonSingleton")
                .AddFormatValue("item", new PyString(iRef->itemName()))
                .AddFormatValue("containerTypeID", new PyInt(containerTypeID));
    // the pilot of a ship with a maintenance bay may launch anything in it.  from someone else's bay, only your own ships
    if (pContainer->IsShipSE() and (pContainer != pClient->GetShipSE()) and (iRef->ownerID() != pClient->GetCharacterID()))
        throw CustomError ("The %s in the %s does not belong to you.", iRef->name(), pContainer->GetName());

    ShipItemRef shipRef(sItemFactory.GetShipRef(shipID));
    if (shipRef.get() == nullptr)
        throw UserError ("CantLaunchItemNotPresent")
                .AddFormatValue("itemTypeID", new PyInt(iRef->typeID()))
                .AddFormatValue("containerTypeID", new PyInt(containerTypeID));
    return shipRef;
}

ShipSE* ShipBound::LaunchStoredShip(Client* pClient, SystemEntity* pContainer, ShipItemRef shipRef)
{
    SystemManager* pSystem(pClient->SystemMgr());
    uint32 containerID(shipRef->locationID());
    EVEItemFlags oldFlag(shipRef->flag());

    GPoint pos(pContainer->GetPosition());
    pos.MakeRandomPointOnSphere(pContainer->GetRadius() + shipRef->radius() + MakeRandomFloat(100, 300));

    shipRef->SetCustomInfo(nullptr);
    shipRef->Move(pSystem->GetID(), flagNone, true);
    shipRef->SetPosition(pos);

    DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.itemID = shipRef->itemID();
        entity.itemName = shipRef->itemName();
        entity.typeID = shipRef->typeID();
        entity.groupID = shipRef->groupID();
        entity.categoryID = shipRef->categoryID();
        entity.ownerID = shipRef->ownerID();
        entity.corporationID = pClient->GetCorporationID();
        entity.allianceID = pClient->GetAllianceID();
        entity.factionID = pClient->GetWarFactionID();
        entity.position = pos;
    SystemEntity* pSE(DynamicEntityFactory::BuildEntity(*pSystem, entity));
    if ((pSE == nullptr) or !pSE->IsShipSE()) {
        SafeDelete(pSE);
        shipRef->Move(containerID, oldFlag, true);
        throw CustomError ("The %s could not be launched.  Ref: ServerError 25108.", shipRef->name());
    }
    pSystem->AddEntity(pSE);
    pClient->GetShipSE()->DestinyMgr()->SendJettisonPacket();
    return pSE->GetShipSE();
}

PyResult ShipBound::LaunchFromContainer(PyCallArgs &call, PyInt* structureID, PyList* ids) {
    /* menusvc.LaunchSMAContents():  LaunchFromContainer(structureID, ids), ids holding each itemID 'stacksize' times
     *  (ships are singletons, so a repeat means nothing).  the client checks for warp itself
     */
    Client* pClient(call.client);
    if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr))
        return nullptr;
    if (pClient->GetShipSE()->DestinyMgr()->IsWarping())
        throw UserError ("ShipInWarp");

    EVEItemFlags flag(flagNone);
    SystemEntity* pContainer(GetShipContainer(pClient, structureID->value(), flag, true));

    std::set<uint32> shipIDs;
    for (PyList::const_iterator itr = ids->begin(); itr != ids->end(); ++itr)
        if ((*itr)->IsInt())
            shipIDs.insert((*itr)->AsInt()->value());

    for (auto shipID : shipIDs)
        LaunchStoredShip(pClient, pContainer, GetStoredShip(pClient, pContainer, shipID));

    return nullptr;
}

PyResult ShipBound::ScoopToSMA(PyCallArgs &call, PyInt* objectID) {
    /* menusvc.RealScoopSMA(), after approaching.  offered for any ship that isn't a capsule or ours when our ship has a maintenance bay */
    Client* pClient(call.client);
    SystemManager* pSystem(pClient->SystemMgr());
    ShipSE* pMyShipSE(pClient->GetShipSE());
    if (!pClient->IsInSpace() or (pSystem == nullptr) or (pMyShipSE == nullptr))
        return nullptr;

    SystemEntity* pSE(pSystem->GetSE(objectID->value()));
    if ((pSE == nullptr) or pSE->IsDead())
        throw UserError ("ScoopObjectGone").AddFormatValue("target", new PyString("The ship"));
    if (!pMyShipSE->GetSelf()->HasAttribute(AttrHasShipMaintenanceBay) or !pSE->IsShipSE()
    or  (pSE->GetSelf()->groupID() == EVEDB::invGroups::Capsule) or (pSE == pMyShipSE))
        throw UserError ("CantScoopThat")
                .AddFormatValue("targetGroup", new PyString(sDataMgr.GetGroupName(pSE->GetSelf()->groupID())));
    if (pSE->HasPilot() or (pSystem->GetLaw()->GetLoggedOffOwner(pSE->GetID()) != 0))
        throw UserError ("ShipAlreadyHasPilot");

    // our own ships, or abandoned ones nobody has locked
    uint32 charID(pClient->GetCharacterID());
    bool abandoned(pSE->GetOwnerID() == 1);
    if (!abandoned and (pSE->GetOwnerID() != charID))
        throw CustomError ("The %s does not belong to you.", pSE->GetName());
    if (abandoned and (pSE->TargetMgr() != nullptr) and pSE->TargetMgr()->IsTargeted())
        throw UserError ("CannotScoopTargetedShip");

    if (SurfaceDistance(pMyShipSE, pSE) > maxShipScoopDistance)
        throw UserError ("NotCloseEnoughToAdd").AddFormatValue("maxdist", new PyInt(maxShipScoopDistance));

    ShipItemRef shipRef(pSE->GetShipSE()->GetShipItemRef());
    CheckStorable(pClient, shipRef, pMyShipSE, flagShipHangar);

    if (abandoned)
        shipRef->ChangeOwner(charID);
    // out of space and into our bay
    pSystem->RemoveEntity(pSE);
    SafeDelete(pSE);
    shipRef->SetCustomInfo(nullptr);
    shipRef->Move(pMyShipSE->GetID(), flagShipHangar, true);
    shipRef->SaveShip();

    return nullptr;
}

PyResult ShipBound::BoardStoredShip(PyCallArgs &call, PyInt* structureID, PyInt* shipID) {
    // menusvc.BoardSMAShip():  PerformSessionChange('board', ship.BoardStoredShip, structureID, shipID)
    Client* pClient(call.client);
    if (pClient->IsSessionChange()) {
        pClient->SendNotifyMsg("Session Change currently active.");
        return nullptr;
    }
    if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr))
        return nullptr;
    if (pClient->GetShipSE()->DestinyMgr()->IsWarping())
        throw UserError ("ShipInWarp");

    EVEItemFlags flag(flagNone);
    SystemEntity* pContainer(GetShipContainer(pClient, structureID->value(), flag, true));
    ShipItemRef shipRef(GetStoredShip(pClient, pContainer, shipID->value()));

    if (!shipRef->ValidateBoardShip(pClient->GetChar()))
        throw CustomError ("You do not have the skills to fly a %s.", shipRef->name());
    if (!pClient->InPod()) {
        sCrimeWatch.CanSwitchShip(pClient);
        std::string modules(ActiveBlockingModules(pClient->GetShip()));
        if (!modules.empty())
            throw UserError ("ShipEjectFailedSystemsOccupied").AddFormatValue("types", new PyString(modules));
    }

    // the ship comes out next to its container and the pilot boards it there.  the old ship stays in space
    ShipSE* pNewShipSE(LaunchStoredShip(pClient, pContainer, shipRef));
    pClient->Board(pNewShipSE);
    sCrimeWatch.OnEnterSpace(pClient);

    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

PyResult ShipBound::StoreVessel(PyCallArgs &call, PyInt* destID) {
    // menusvc.StoreVessel():  PerformSessionChange('storeVessel', ship.StoreVessel, destID).  always the current ship
    Client* pClient(call.client);
    if (pClient->IsSessionChange()) {
        pClient->SendNotifyMsg("Session Change currently active.");
        return nullptr;
    }
    ShipSE* pShipSE(pClient->GetShipSE());
    if (!pClient->IsInSpace() or (pShipSE == nullptr))
        return nullptr;
    if (pClient->InPod())
        throw UserError ("CannotStoreCapsule");

    sCrimeWatch.CanSwitchShip(pClient);
    // a pilot can't hide their ship from anyone they are fighting
    if (sConfig.crime.Enabled and sCrimeWatch.HasLiveFlags(pClient->GetCharacterID()))
        throw UserError ("CannotStoreVesselBecauseOfCombat");
    std::string modules(ActiveBlockingModules(pClient->GetShip()));
    if (!modules.empty())
        throw UserError ("ShipEjectFailedSystemsOccupied").AddFormatValue("types", new PyString(modules));
    if (pShipSE->DestinyMgr()->IsWarping())
        throw UserError ("ShipInWarp");

    EVEItemFlags flag(flagNone);
    SystemEntity* pDest(GetShipContainer(pClient, destID->value(), flag, false));
    if (pDest == pShipSE)
        throw UserError ("CannotStoreDestinationUnsuitable")
                .AddFormatValue("ship", new PyString(pShipSE->GetName()))
                .AddFormatValue("dest", new PyString(pDest->GetName()));
    CheckStorable(pClient, pClient->GetShip(), pDest, flag);

    pClient->StoreShip(pDest->GetSelf(), flag);

    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

PyResult ShipBound::SelfDestruct(PyCallArgs &call, PyInt* shipID) {
    /* menusvc.SelfDestructShip -> PerformSessionChange('selfdestruct', ship.SelfDestruct, shipID).  only offered for the current ship.
     *  selecting it again aborts the countdown.  the messages (SelfDestructInitiated/Timer/Immediate, Aborted2, Cancelled*, the
     *  *Other versions for the pilots around) are sent by SystemManager, which runs the timer.  the live server sent
     *  SelfDestructTimer {what: (4, typeID), time: "1 Minute 49 Seconds"} every 10 seconds
     */
    Client* pClient(call.client);
    if (pClient->IsSessionChange() or !pClient->IsInSpace())
        return nullptr;
    SystemManager* pSystem(pClient->SystemMgr());
    ShipSE* pShipSE(pClient->GetShipSE());
    if ((pSystem == nullptr) or (pShipSE == nullptr))
        return nullptr;
    if (shipID->value() != pClient->GetShipID())
        throw UserError("CannotSelfDestruct");
    if (pShipSE->DestinyMgr()->IsWarping())
        throw UserError("ShipInWarp");

    if (pSystem->IsSelfDestructing(pShipSE->GetID())) {
        pSystem->AbortSelfDestruct(pClient);
    } else {
        pSystem->StartSelfDestruct(pClient);
    }

    // returns nodeID and timestamp
    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}
