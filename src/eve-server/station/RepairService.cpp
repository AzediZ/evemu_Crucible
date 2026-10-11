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
    Author:        Reve
    Rewrite:    Allan
*/

#include "eve-server.h"
#include "packets/Repair.h"



#include "Client.h"
#include "account/AccountService.h"
#include "inventory/Inventory.h"
#include "station/StationDataMgr.h"
#include "ship/ShipDB.h"
#include "station/RepairService.h"
#include "station/Station.h"
#include "system/SystemManager.h"
#include "services/ServiceManager.h"


RepairService::RepairService(EVEServiceManager& mgr) :
    BindableService ("repairSvc", mgr)
{
    this->Add("UnasembleItems", &RepairService::UnasembleItems);
}

BoundDispatcher* RepairService::BindObject(Client* client, PyRep* bindParameters) {
    uint32 locationID = bindParameters->AsInt ()->value ();
    auto it = this->m_instances.find (locationID);

    if (it != this->m_instances.end ())
        return it->second;

    RepairServiceBound* bound = new RepairServiceBound (this->GetServiceManager(), *this, locationID);
    this->m_instances.insert_or_assign (locationID, bound);

    return bound;
}

void RepairService::BoundReleased (RepairServiceBound* bound) {
    auto it = this->m_instances.find (bound->GetLocationID());

    if (it == this->m_instances.end ())
        return;

    this->m_instances.erase (it);
}

void RepairService::GetDamageReports(uint32 itemID, Inventory* pInv, PyList* list) {
    std::vector<InventoryItemRef> itemRefVec;
    InventoryItemRef iRef = pInv->GetByID(itemID);
    if (iRef.get() == nullptr) {
        iRef = sItemFactory.GetItemRef(itemID);
        if (iRef.get() == nullptr)
            return;
    }
    itemRefVec.push_back(iRef);
    if (iRef->IsShipItem()) {
        if (iRef->GetShipItem()->IsActive()) {
            iRef->GetShipItem()->GetModuleRefVec(itemRefVec);
        }
        else {
            iRef->GetShipItem()->GetModuleItemVec(itemRefVec);
        }
    }

    for (auto cur : itemRefVec) {
        RepairItemData rid;
        rid.itemID = cur->itemID();
        rid.typeID = cur->typeID();
        rid.groupID = cur->groupID();
        // whole units, rounded up.  the client quotes ceil(damage) * costToRepairOneUnitOfDamage and RepairItems charges the same
        rid.damage = (int32)std::ceil(GetItemDamage(cur));
        rid.maxHealth = cur->GetAttribute(AttrHP).get_int();
        if (cur->IsShipItem() or (cur->categoryID() == EVEDB::invCategories::Drone))
            rid.maxHealth += cur->GetAttribute(AttrArmorHP).get_int();
        // not sure how to find this data
        rid.repairable = 1;
        rid.costToRepairOneUnitOfDamage = GetRepairCostPerHP(cur);

        list->AddItem(rid.Encode());
    }
}

float RepairService::GetItemDamage(InventoryItemRef iRef) {
    float damage = iRef->GetAttribute(AttrDamage).get_float();
    if (iRef->IsShipItem() or (iRef->categoryID() == EVEDB::invCategories::Drone))
        damage += iRef->GetAttribute(AttrArmorDamage).get_float();
    if (damage < 0)
        damage = 0;
    return damage;
}

double RepairService::GetRepairCostPerHP(InventoryItemRef iRef) {
    if (iRef->IsShipItem())
        // ship is (basePrice)*7.5e-10
        return (iRef->type().basePrice() * sConfig.rates.ShipRepairModifier);
    // modules are (basePrice)*1.25e-6
    return (iRef->type().basePrice() * sConfig.rates.ModuleRepairModifier);
}

bool RepairService::IsRepairableHere(Client* pClient, InventoryItemRef iRef, uint32 stationID) {
    uint32 officeID(0);
    if (iRef->ownerID() == pClient->GetCharacterID()) {
        // personal item
    } else if ((iRef->ownerID() == pClient->GetCorporationID()) and IsPlayerCorp(iRef->ownerID())) {
        officeID = stDataMgr.GetOfficeIDForCorp(stationID, iRef->ownerID());
        if (officeID == 0)
            return false;
    } else {
        return false;
    }

    // in the station (hangar) or the corp's office there
    uint32 locationID(iRef->locationID());
    if ((locationID == stationID) or ((officeID != 0) and (locationID == officeID)))
        return true;

    // fitted to, or carried in, a ship or container that is in the station (this includes the active ship while docked)
    if (!IsPlayerItem(locationID))
        return false;
    InventoryItemRef cRef = sItemFactory.GetItemRef(locationID);
    if (cRef.get() == nullptr)
        return false;
    if (cRef->locationID() == stationID)
        return true;
    if ((officeID != 0) and (cRef->locationID() == officeID))
        return true;
    return false;
}

PyResult RepairService::UnasembleItems(PyCallArgs& call, PyDict* validIDsByStationID, PyList* skipChecks) {
    // sm.RemoteSvc('repairSvc').UnasembleItems(dict(validIDsByStationID), skipChecks)

    /*
     * 15:17:44 [PhysicsInfo]  Call_UnasembleItems
     * 15:17:44 [PhysicsInfo]  dict:
     * 15:17:44 [PhysicsInfo]      Dictionary: 2 entries
     * 15:17:44 [PhysicsInfo]        [ 0] Key: Integer field: 60004591     <-- stationID
     * 15:17:44 [PhysicsInfo]        [ 0] Value: List: 3 elements          <-- list of 2 element tuples
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 0] Tuple: 2 elements  <-- tuple of itemID/stationID
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 0]   [ 0] Integer field: 140006472
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 0]   [ 1] Integer field: 60004591
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 1] Tuple: 2 elements  <-- tuple of itemID/stationID
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 1]   [ 0] Integer field: 140006477
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 1]   [ 1] Integer field: 60004591
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 2] Tuple: 2 elements  <-- tuple of itemID/stationID
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 2]   [ 0] Integer field: 140006476
     * 15:17:44 [PhysicsInfo]        [ 0] Value:   [ 2]   [ 1] Integer field: 60004591
     * 15:17:44 [PhysicsInfo]        [ 1] Key: Integer field: 60014137     <-- stationID
     * 15:17:44 [PhysicsInfo]        [ 1] Value: List: 1 elements          <-- list of 2 element tuples
     * 15:17:44 [PhysicsInfo]        [ 1] Value:   [ 0] Tuple: 2 elements  <-- tuple of itemID/stationID
     * 15:17:44 [PhysicsInfo]        [ 1] Value:   [ 0]   [ 0] Integer field: 140000028
     * 15:17:44 [PhysicsInfo]        [ 1] Value:   [ 0]   [ 1] Integer field: 60014137
     * 15:17:44 [PhysicsInfo]  list:
     * 15:17:44 [PhysicsInfo]      List: Empty                             <-- skipChecks, not sure what this is for
     */

    PyList* pList(nullptr);
    PyTuple* tuple(nullptr);
    InventoryItemRef iRef(nullptr);
    uint32 itemID(0); //,locationID = 0,  itemLoc = 0;

    if (skipChecks->size() > 0)
        ;  // skipChecks is populated....do something constructive here

    /* first pass: validate every item before anything changes.
     *  the item must be the caller's (or their corp's, in its office there), in the station given as key (or inside something there),
     *  not the active ship, and undamaged (repackaging resets damage, so it would be a free repair) */
    for (PyDict::const_iterator dictItr = validIDsByStationID->begin(); dictItr != validIDsByStationID->end(); ++dictItr) {
        if (!dictItr->first->IsInt() or !dictItr->second->IsList())
            throw CustomError("Invalid repackage request.");
        uint32 stationID = dictItr->first->AsInt()->value();
        if (!sDataMgr.IsStation(stationID))
            throw CustomError("Items can only be repackaged in a station.");
        pList = dictItr->second->AsList();
        for (PyList::const_iterator listItr = pList->begin(); listItr != pList->end(); ++listItr) {
            if (!(*listItr)->IsTuple() or ((*listItr)->AsTuple()->size() < 1))
                throw CustomError("Invalid repackage request.");
            itemID = PyRep::IntegerValue((*listItr)->AsTuple()->GetItem(0));
            iRef = sItemFactory.GetItemRef(itemID);
            if (iRef.get() == nullptr)
                continue;
            if (itemID == call.client->GetShipID())
                throw CustomError("You cannot repackage your active ship.");
            if (!IsRepairableHere(call.client, iRef, stationID)) {
                _log(SERVICE__WARNING, "UnasembleItems - %s(%u) tried to repackage %s(%u) owned by %u in %u (station %u).", \
                        call.client->GetName(), call.client->GetCharacterID(), iRef->name(), itemID, iRef->ownerID(), iRef->locationID(), stationID);
                throw CustomError("You cannot repackage %s.", iRef->name());
            }
            // fitted modules are repackaged with their ship (StripFitting), not on their own
            if (IsModuleSlot(iRef->flag()))
                throw CustomError("Unfit %s before repackaging it.", iRef->name());
            if (GetItemDamage(iRef) > 0)
                throw UserError("CantRepackageDamagedItem");
            if (iRef->IsShipItem()) {
                // fitted modules lose their damage when the ship is repackaged
                std::vector<InventoryItemRef> modules;
                iRef->GetShipItem()->GetModuleItemVec(modules);
                for (auto cur : modules)
                    if (GetItemDamage(cur) > 0)
                        throw UserError("CantRepackageDamagedItem");
            }
        }
    }

    /** @todo  may have to switch to station inventory to get item to check if this is container, and remove items BEFORE repacking!!  */
    for (PyDict::const_iterator dictItr = validIDsByStationID->begin(); dictItr != validIDsByStationID->end(); ++dictItr) {
        // Dictionary key is LocationID, value is tuple of itemID/item locationID
        //locationID = dictItr->first->AsInt()->value();
        pList = dictItr->second->AsList();
        if (pList != nullptr) {
            // Iterate through list.
            for (PyList::const_iterator listItr = pList->begin(); listItr != pList->end(); ++listItr) {
                // List is tuples of itemID, LocationID pairs.
                tuple = (*listItr)->AsTuple();
                if (tuple != nullptr) {
                    // Get the itemID.
                    itemID = PyRep::IntegerValue(tuple->GetItem(0));
                    //itemLoc = PyRep::IntegerValue(tuple->GetItem(1));
                    iRef = sItemFactory.GetItemRef(itemID);
                    if (iRef.get() != nullptr) {
                        // Add type exceptions here.
                        if (iRef->categoryID() == EVEDB::invCategories::Blueprint
                            or iRef->categoryID() == EVEDB::invCategories::Skill
                            or iRef->categoryID() == EVEDB::invCategories::Material) {
                            // Item cannot be repackaged once used!
                            continue;
                        }
                        // if this is a ship, remove all modules and empty cargo holds before repacking
                        //  maybe we should instruct player that ship is fitted/has cargo, and let them do this
                        if (iRef->IsShipItem()) {
                            iRef->GetShipItem()->EmptyCargo();
                            iRef->GetShipItem()->StripFitting();
                            // check insurance and delete if applicable
                            ShipDB::DeleteInsuranceByShipID(itemID);
                        }

                        iRef->ChangeSingleton(false, true);
                    }
                }
            }
        }
    }

    // MultipleItemsFailedToRepackage

    return PyStatic.NewNone();
}

RepairServiceBound::RepairServiceBound(EVEServiceManager& mgr, RepairService& parent, uint32 locationID) :
    EVEBoundObject(mgr, parent),
    m_locationID(locationID)
{
    this->Add("DamageModules", &RepairServiceBound::DamageModules);
    this->Add("RepairItems", &RepairServiceBound::RepairItems);
    this->Add("GetDamageReports", &RepairServiceBound::GetDamageReports);
}

PyResult RepairServiceBound::DamageModules(PyCallArgs &call, PyList* itemIDAndAmountOfDamage) {
    /*    itemIDAndAmountOfDamageList.append((item.itemID, amount)) <-- amount is % of damage
     *    self.repairSvc.DamageModules(itemIDAndAmountOfDamageList)
     */

    _log(PHYSICS__INFO, "RepairSvcBound::Handle_DamageModules() size=%lli", call.tuple->size());
    call.Dump(PHYSICS__INFO);

    return PyStatic.NewNone();
}

PyResult RepairServiceBound::RepairItems(PyCallArgs &call, PyList* itemIDs, PyFloat* iskAmount) {
    //  self.repairSvc.RepairItems(itemIDs, amount['qty'])
    /*
     * 00:18:28 W RepairSvcBound::Handle_RepairItems(): size= 2
     * 00:18:28 [PhysicsInfo]   Call Arguments:
     * 00:18:28 [PhysicsInfo]       Tuple: 2 elements
     * 00:18:28 [PhysicsInfo]         [ 0] List: 1 elements                 <-- list of itemIDs to repair
     * 00:18:28 [PhysicsInfo]         [ 0]   [ 0] Integer field: 140005905  <-- itemID
     * 00:18:28 [PhysicsInfo]         [ 1] Real field: 112500.000000        <-- isk amount to spend on repairs.
     */
    // also false for NaN
    if (!(iskAmount->value() >= 0.01))
        return nullptr;

    if ((uint32)call.client->GetStationID() != m_locationID)
        throw CustomError("You must be docked in this station to use its repair facilities.");

    /* itemIDs is list of itemIDs to repair (damage report lines: ships, their fitted modules, other items).
     * iskAmount is how much to spend on repairs.
     *  the client asks for a full repair (and always sends the full amount) when any module is selected.
     *  ships (and other non-module items) can be partially repaired for part of the cost.
     */
    struct RepairEntry {
        InventoryItemRef iRef;
        float damage;
    };
    std::vector<RepairEntry> entries;
    std::set<uint32> seen;
    double total(0);
    bool hasModule(false);
    for (PyList::const_iterator itr = itemIDs->begin(); itr != itemIDs->end(); ++itr) {
        uint32 itemID(PyRep::IntegerValueU32(*itr));
        if (!seen.insert(itemID).second)
            continue;
        InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
        if (iRef.get() == nullptr)
            continue;
        if (!RepairService::IsRepairableHere(call.client, iRef, m_locationID)) {
            _log(SERVICE__WARNING, "RepairItems - %s(%u) tried to repair %s(%u) owned by %u in %u (station %u).", \
                    call.client->GetName(), call.client->GetCharacterID(), iRef->name(), itemID, iRef->ownerID(), iRef->locationID(), m_locationID);
            continue;
        }
        float damage(RepairService::GetItemDamage(iRef));
        if (damage <= 0)
            continue;
        // same formula as the client's quote: ceil(damage) * costToRepairOneUnitOfDamage
        total += std::ceil(damage) * RepairService::GetRepairCostPerHP(iRef);
        if (iRef->categoryID() == EVEDB::invCategories::Module)
            hasModule = true;
        entries.push_back({iRef, damage});
    }

    if (entries.empty())
        return nullptr;

    /** @todo  entire repair be based on items damage vs mats list at market value
     * get bp required mats for building.
     * from there, determine % of that type/amt that *may* be required for repairing.
     * check player hangar for amounts, and if present, subtract market value from cost estimate.
     * 5-10% of total cost as min charge for use of facilities
     */

    /** more shit to check for and add as required...
     * NotEnoughRepairMaterialToFinishAllRepairs
     * NotEnoughRepairMaterialToFinishAllRepairsBody'}(u'There is not enough unallocated {typeName} left to initiate repairs on this module.', None, {u'{typeName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'typeName'}})
     *
     *{'messageKey': 'RepairModuleNotDamaged', 'dataID': 17882542, 'suppressable': False, 'bodyID': 259175, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1492}
     * {'messageKey': 'RepairNoMoneyForRepair', 'dataID': 17882601, 'suppressable': False, 'bodyID': 259197, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259196, 'messageID': 1493}
     */

    double amount(std::min((double)iskAmount->value(), total));
    double fraction(1.0);
    if (amount < (total - 0.01)) {
        // modules can't be partially repaired.  the client sends the full amount when any is selected
        if (hasModule)
            throw CustomError("Modules can only be repaired completely.  The repair costs %.2f ISK.", total);
        fraction = amount / total;
    } else {
        amount = total;
    }

    // charge first.  this throws if the wallet is short, and nothing is repaired
    AccountService::TransferFunds(call.client->GetCharacterID(), stDataMgr.GetOwnerID(m_locationID), amount, "Repair", Journal::EntryType::RepairBill, m_locationID);

    for (auto& cur : entries) {
        bool hasArmor(cur.iRef->IsShipItem() or (cur.iRef->categoryID() == EVEDB::invCategories::Drone));
        if (fraction >= 1.0) {
            cur.iRef->SetAttribute(AttrDamage, EvilZero);
            if (hasArmor)
                cur.iRef->SetAttribute(AttrArmorDamage, EvilZero);
            continue;
        }

        // this will repair hull first, then armor
        float toRepair(cur.damage * fraction);
        float hull(cur.iRef->GetAttribute(AttrDamage).get_float());
        float newHull(std::max(0.0f, hull - toRepair));
        cur.iRef->SetAttribute(AttrDamage, newHull);
        if (hasArmor) {
            float rest(toRepair - (hull - newHull));
            float armor(cur.iRef->GetAttribute(AttrArmorDamage).get_float());
            cur.iRef->SetAttribute(AttrArmorDamage, std::max(0.0f, armor - rest));
        }
    }

    return PyStatic.NewNone();
}

PyResult RepairServiceBound::GetDamageReports(PyCallArgs &call, PyList* itemIDs) {
    /*
     * 20:39:30 W RepairSvcBound::Handle_GetDamageReports(): size= 1
     * 20:39:30 [SvcCallDump]   Call Arguments:
     * 20:39:30 [SvcCallDump]       Tuple: 1 elements
     * 20:39:30 [SvcCallDump]         [ 0] List: 1 elements
     * 20:39:30 [SvcCallDump]         [ 0]   [ 0] Integer field: 140012041
     */
    std::vector <int32> ints;

    // TODO: update this when the type changes are in place so these things are easier to work with
    PyList::const_iterator list_2_cur = itemIDs->begin();
    for (size_t list_2_index(0); list_2_cur != itemIDs->end(); ++list_2_cur, ++list_2_index) {
        if (!(*list_2_cur)->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_SingleIntList failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
            return nullptr;
        }

        const PyInt* t = (*list_2_cur)->AsInt();
        ints.push_back(t->value());
    }

    PyDict* dict = new PyDict();
    Client* pClient = call.client;
    StationItemRef sRef = pClient->SystemMgr()->GetStationFromInventory(m_locationID);
    Inventory* pInv = sRef->GetMyInventory();
    float standing = 0.0f;
    // standing system isnt complete, but this is the correct data methods for station standing checks
    if (IsNPCCorp(sRef->ownerID())) {
        standing = pClient->GetChar()->GetNPCCorpStanding(sRef->ownerID(), pClient->GetCharacterID());
    } else {
        standing = pClient->GetChar()->GetStandingModified(sRef->ownerID(), pClient->GetCharacterID());
    }

    for (auto cur : ints) {
        RepairListRsp rlr;
            rlr.discount       = "0%";  // not sure....seen 0% and 100% in packets
            rlr.serviceCharge  = "0%";  // not sure....seen 0% in packets
            rlr.playerStanding = standing;
            rlr.lines = new PyList();
            RepairService::GetDamageReports(cur, pInv, rlr.lines);
        dict->SetItem(new PyInt(cur), rlr.Encode());
    }

    return dict;
}
