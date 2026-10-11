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
    Author:     Zhur, Captnoord
    Rewrite:    Allan
*/

#include "EntityList.h"
#include "system/CrimeWatch.h"
#include "eve-server.h"
#include "../../eve-common/EVE_Station.h"


#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "inventory/InventoryBound.h"
#include "inventory/InvBrokerService.h"
#include "planet/CustomsOffice.h"
#include "planet/Planet.h"
#include "planet/Colony.h"
#include "pos/Structure.h"
#include "ship/modules/GenericModule.h"
#include "system/BookmarkDB.h"
#include "system/Container.h"
#include "system/SystemManager.h"
#include "system/SystemEntity.h"
#include "station/Station.h"
#include "station/Outpost.h"
#include "station/StationDataMgr.h"
#include "manufacturing/FactoryDB.h"
#include "contract/ContractUtils.h"
#include "inventory/ContainerSecurity.h"

InventoryBound::InventoryBound(EVEServiceManager &mgr, BoundServiceParent<InventoryBound>& parent, InventoryItemRef item, EVEItemFlags flag, uint32 ownerID, bool passive) :
    EVEBoundObject(mgr, parent),
    pInventory(item->GetMyInventory()),
    m_flag(flag),
    m_self(item),
    m_itemID(item->itemID()),
    m_ownerID(ownerID),
    m_passive(passive)
{
    EVEBoundObject::Add("List", &InventoryBound::List);
    EVEBoundObject::Add("Add", &InventoryBound::Add);
    EVEBoundObject::Add("MultiAdd", &InventoryBound::MultiAdd);
    EVEBoundObject::Add("GetItem", &InventoryBound::GetItem);
    EVEBoundObject::Add("RemoveChargeToCargo", &InventoryBound::RemoveChargeToCargo);
    EVEBoundObject::Add("RemoveChargeToHangar", &InventoryBound::RemoveChargeToHangar);
    EVEBoundObject::Add("MultiMerge", &InventoryBound::MultiMerge);
    EVEBoundObject::Add("StackAll", &InventoryBound::StackAll);
    EVEBoundObject::Add("StripFitting", &InventoryBound::StripFitting);
    EVEBoundObject::Add("DestroyFitting", &InventoryBound::DestroyFitting);
    EVEBoundObject::Add("ImportExportWithPlanet", &InventoryBound::ImportExportWithPlanet);
    EVEBoundObject::Add("CreateBookmarkVouchers", &InventoryBound::CreateBookmarkVouchers);
    EVEBoundObject::Add("ListDroneBay", &InventoryBound::ListDroneBay);
    EVEBoundObject::Add("SetPassword", &InventoryBound::SetPassword);
    EVEBoundObject::Add("EnterPassword", &InventoryBound::EnterPassword);
    EVEBoundObject::Add("HasExistingPasswordSet", &InventoryBound::HasExistingPasswordSet);
    EVEBoundObject::Add("RetrievePassword", &InventoryBound::RetrievePassword);
    EVEBoundObject::Add("LockItem", &InventoryBound::LockItem);
    EVEBoundObject::Add("UnlockItem", &InventoryBound::UnlockItem);
    EVEBoundObject::Add("ALSCConfigGet", &InventoryBound::ALSCConfigGet);
    EVEBoundObject::Add("ALSCConfigSet", &InventoryBound::ALSCConfigSet);
    EVEBoundObject::Add("ALSCLogGet", &InventoryBound::ALSCLogGet);
    EVEBoundObject::Add("RunRefiningProcess", &InventoryBound::RunRefiningProcess);
    EVEBoundObject::Add("TakeOutTrash", &InventoryBound::TakeOutTrash);
    EVEBoundObject::Add("BreakPlasticWrap", &InventoryBound::BreakPlasticWrap);
    EVEBoundObject::Add("Build", &InventoryBound::Build);

    _log(INV__BIND, "Created InventoryBound object %p for %s(%u) and ownerID %u with flag %s  (passive: %s)", \
            this, m_self->name(), m_itemID, ownerID, sDataMgr.GetFlagName(flag), (m_passive ? "true" : "false"));
}

PyResult InventoryBound::GetItem(PyCallArgs &call) {
    _log(INV__MESSAGE, "Calling InventoryBound::GetItem() for %s(%u)", m_self->name(), m_itemID);
    return m_self->GetItem();
}

PyResult InventoryBound::StripFitting(PyCallArgs &call)
{
    call.client->GetShip()->StripFitting();
    return nullptr;
}

PyResult InventoryBound::DestroyFitting(PyCallArgs &call, PyInt* itemID) {
    _log(INV__MESSAGE, "Calling InventoryBound::DestroyFitting() for %s(%u)", m_self->name(), m_itemID);
    call.client->GetShip()->RemoveRig(sItemFactory.GetItemRef(itemID->value()));

    return nullptr;
}

PyResult InventoryBound::StackAll(PyCallArgs &call, std::optional <PyInt*> flag) {
    // a password protected container (the client asks for the password and calls again)
    sContSec.CheckOpen(call.client, m_self);

    call.Dump(INV__DUMP);

    EVEItemFlags stackFlag = m_flag;

    if (flag.has_value()) {
        stackFlag = (EVEItemFlags)flag.value()->value();
    }

    _log(INV__MESSAGE, "Calling InventoryBound::StackAll() for %s(%u) in %s.  Bound flag is %s", \
            m_self->name(), m_itemID, sDataMgr.GetFlagName(stackFlag), sDataMgr.GetFlagName(m_flag));

    //Stack Items contained in this inventory
    pInventory->StackAll(stackFlag, m_ownerID);

    return nullptr;
}

PyResult InventoryBound::ImportExportWithPlanet(PyCallArgs &call, PyInt* spaceportPinID, PyDict* importData, PyDict* exportData, PyFloat* taxRate) {
    /*
            customsOfficeInventory = sm.GetService('invCache').GetInventoryFromId(self.customsOfficeID)
            customsOfficeInventory.ImportExportWithPlanet(self.spaceportPinID, importData, exportData, self.taxRate)
            */

    /*
     * 23:21:49 [PlanetPktTrace]  Call_PlanetCustomsXfer
     * 23:21:49 [PlanetPktTrace]  spaceportPinID=140000640
     * 23:21:49 [PlanetPktTrace]  importData:
     * 23:21:49 [PlanetPktTrace]       Dictionary: 3 entries
     * 23:21:49 [PlanetPktTrace]        [ 0]   Key:    Integer: 140000606
     * 23:21:49 [PlanetPktTrace]        [ 0] Value:    Integer: 1001
     * 23:21:49 [PlanetPktTrace]        [ 1]   Key:    Integer: 140000610
     * 23:21:49 [PlanetPktTrace]        [ 1] Value:    Integer: 1000
     * 23:21:49 [PlanetPktTrace]        [ 2]   Key:    Integer: 140000608
     * 23:21:49 [PlanetPktTrace]        [ 2] Value:    Integer: 1000
     * 23:21:49 [PlanetPktTrace]  exportData:
     * 23:21:49 [PlanetPktTrace]       Dictionary: Empty
     * 23:21:49 [PlanetPktTrace]  taxRate=0.0500000007451
     */
    //{'FullPath': u'UI/Messages', 'messageID': 256577, 'label': u'CannotImportNotEnoughWarehouseSpaceBody'}(u'You cannot import commodities to that spaceport, as it does not have sufficient storage space to handle the incoming goods.', None, None)
    //{'FullPath': u'UI/Messages', 'messageID': 256626, 'label': u'CannotExportNotEnoughSpaceBody'}(u'You cannot export commodities to the customs office, as it does not have sufficient storage space to handle the incoming goods.', None, None)

    //  this is (should be) customs office
    if (m_self->groupID() != EVEDB::invGroups::Orbital_Infrastructure) {
        _log(ITEM__ERROR, "%s: Called CustomsOffice xFer using non-co item %s(%u).", call.client->GetName(), m_self->name(), m_self->itemID());
        return nullptr;
    }

    std::map<uint32, uint16> importItems, exportItems;
    for (PyDict::const_iterator itr = importData->begin(); itr != importData->end(); ++itr)
        importItems[PyRep::IntegerValueU32(itr->first)] = PyRep::IntegerValue(itr->second);

    for (PyDict::const_iterator itr = exportData->begin(); itr != exportData->end(); ++itr)
        exportItems[PyRep::IntegerValueU32(itr->first)] = PyRep::IntegerValue(itr->second);

    // ok, so from here, we need to get officeRef->officeSE->planet->colony to make xfer....crazy shit
    StructureItemRef sRef = StructureItemRef::StaticCast(m_self);
    Colony* pColony = sRef->GetMySE()->GetCOSE()->GetPlanetSE()->GetColony(call.client);
    pColony->PlanetXfer(spaceportPinID->value(), importItems, exportItems, taxRate->value());

    return nullptr;
}

PyResult InventoryBound::RemoveChargeToHangar(PyCallArgs &call, PyTuple* chargeInfo, std::optional<PyRep*> quantity) {
    int32 shipID = PyRep::IntegerValue(chargeInfo->GetItem(0));
    int32 flagID = PyRep::IntegerValue(chargeInfo->GetItem(1));
    int32 typeID = PyRep::IntegerValue(chargeInfo->GetItem(2));
    
    /** @todo determine if this is needed, and implement if so */
    //uint32 quantity = 0;
    //if (call.tuple->size() == 2)
    //    quantity = PyRep::IntegerValue(call.tuple->GetItem(1));

    // this call is used to remove sublocation (charge) items, which is virtual to real.
    //  since our code does this, we will return "None" here to avoid client subsequently calling MultiAdd() or MultiMerge()
    call.client->GetShip()->RemoveCharge((EVEItemFlags)flagID);
    return PyStatic.NewNone();
}

PyResult InventoryBound::RemoveChargeToCargo(PyCallArgs &call, PyTuple* chargeInfo, std::optional<PyRep*> quantity) {
    int32 shipID = PyRep::IntegerValue(chargeInfo->GetItem(0));
    int32 flagID = PyRep::IntegerValue(chargeInfo->GetItem(1));
    int32 typeID = PyRep::IntegerValue(chargeInfo->GetItem(2));

    /** @todo determine if this is needed, and implement if so */
    //uint32 quantity = 0;
    //if (call.tuple->size() == 2)
    //    quantity = PyRep::IntegerValue(call.tuple->GetItem(1));

    // this call is used to remove sublocation (charge) items, which is virtual to real.
    //  since our code does this, we will return "None" here to avoid client subsequently calling MultiAdd() or MultiMerge()
    call.client->GetShip()->RemoveCharge((EVEItemFlags)flagID);
    return PyStatic.NewNone();
}

/* audit log containers log what's put in (Add) and what's taken out (Move):  typeID and quantity */
static void LogContainerMove(Client* pClient, InventoryItemRef iRef, uint32 fromID, uint32 toID, int32 qty = 0)
{
    if ((fromID == toID) or (pClient == nullptr))
        return;
    if (qty < 1)
        qty = (iRef->isSingleton() ? 1 : iRef->quantity());
    InventoryItemRef toRef = sItemFactory.GetItemRefFromID(toID, false);
    if (ContainerSecurity::IsAuditLog(toRef))
        sContSec.Log(toRef, pClient->GetCharacterID(), Inv::ALSCAction::Add, true, iRef->typeID(), qty);
    InventoryItemRef fromRef = sItemFactory.GetItemRefFromID(fromID, false);
    if (ContainerSecurity::IsAuditLog(fromRef))
        sContSec.Log(fromRef, pClient->GetCharacterID(), Inv::ALSCAction::Move, true, iRef->typeID(), qty);
}

/* exploration site containers can't be looted until they're hacked/analyzed/salvaged */
static void CheckLockedContainer(Client* pClient, InventoryItemRef iRef)
{
    if ((pClient == nullptr) or !pClient->IsInSpace() or (pClient->SystemMgr() == nullptr))
        return;
    SystemEntity* pSE(pClient->SystemMgr()->GetSE(iRef->locationID()));
    if ((pSE == nullptr) or !pSE->IsContainerSE() or !pSE->GetContSE()->IsLocked())
        return;
    throw UserError("LockedContainerInaccessible").AddFormatValue("typeID", new PyInt(pSE->GetTypeID()));
}

/* Crucible can flagging: taking from another player's can or wreck in empire space, without the right to, flags you
 *  to the owner's corporation (or the owner, in an npc corp) for 15 minutes.  it's not a crime.  the client's rules
 *  (consider.ConfirmTakeFromContainer): secure containers, our own, our corp's, fleet loot and abandoned wrecks are free
 */
static void CheckTheft(Client* pClient, InventoryItemRef iRef)
{
    if (!sConfig.crime.Enabled or (pClient == nullptr) or !pClient->IsInSpace() or (pClient->SystemMgr() == nullptr))
        return;
    SystemEntity* pSE(pClient->SystemMgr()->GetSE(iRef->locationID()));
    if ((pSE == nullptr) or !(pSE->IsContainerSE() or pSE->IsWreckSE()))
        return;
    InventoryItemRef contRef(pSE->GetSelf());
    if ((contRef->groupID() == EVEDB::invGroups::Secure_Cargo_Container) or (contRef->groupID() == EVEDB::invGroups::Audit_Log_Secure_Container))
        return;
    uint32 ownerID(contRef->ownerID());
    if (!IsCharacterID(ownerID) or (ownerID == pClient->GetCharacterID()))
        return;
    if (pSE->IsWreckSE() and pSE->GetWreckSE()->IsAbandoned())
        return;
    uint32 ownerCorpID(0);
    Client* pOwner(sEntityList.FindClientByCharID(ownerID));
    if (pOwner != nullptr) {
        ownerCorpID = pOwner->GetCorporationID();
    } else {
        CharacterRef cRef(sItemFactory.GetCharacterRef(ownerID));
        if (cRef.get() != nullptr)
            ownerCorpID = cRef->corporationID();
    }
    if ((ownerCorpID == pClient->GetCorporationID()) and !IsNPCCorp(ownerCorpID))
        return;
    if (sCrimeWatch.CanTakeFrom(pClient->GetCharacterID(), ownerID))
        return;
    sCrimeWatch.Theft(pClient, ownerID, ownerCorpID);
}

PyResult InventoryBound::MultiMerge(PyCallArgs &call, PyList* items, std::optional<PyRep*> sourceContainerID) {
    // a password protected container (the client asks for the password and calls again)
    sContSec.CheckOpen(call.client, m_self);

    _log(INV__MESSAGE, "IB::MultiMerge() called by %s(%u)", m_self->name(), m_itemID);
    call.Dump(INV__DUMP);

    sItemFactory.SetUsingClient(call.client);
    Inventory* pInv(nullptr); // = sItemFactory.GetInventoryFromId(args.locationID);
    /* args.locationID is NOT the destination...it is the source, and NOT what we're looking for here.
    if (pInv == nullptr) {
        _log(INV__WARNING, "Failed to get container inventory for locationID %u.", args.locationID);
        return nullptr;
    }
    */

    std::vector<PyRep *>::const_iterator itr = items->begin(), end = items->end();
    for (; itr != end; ++itr) {
        if ((*itr)->IsTuple() == false)
            continue;

        PyTuple* tuple1 = (*itr)->AsTuple();

        int32 sourceID = PyRep::IntegerValue(tuple1->GetItem(0));
        int32 destID = PyRep::IntegerValue(tuple1->GetItem(1));
        int32 qty = PyRep::IntegerValue(tuple1->GetItem(2));

        InventoryItemRef srcItem = sItemFactory.GetItemRef( sourceID );
        if (srcItem.get() == nullptr) {
            _log(INV__WARNING, "Failed to load source item %u. Skipping.", sourceID);
            continue;
        }

        InventoryItemRef destItem = sItemFactory.GetItemRef( destID );
        if (destItem.get() == nullptr) {
            _log(INV__WARNING, "Failed to load destination item %u. Skipping.", destID);
            continue;
        }

        // get inventory of destination container for ValidateAddItem() check
        pInv = sItemFactory.GetInventoryFromId(destItem->locationID());
        if (pInv == nullptr) {
            _log(INV__WARNING, "Failed to get inventory for locationID %u.", destItem->locationID());
            continue;
        }

        // may we take it from where it is, and put it there?
        try {
            InventoryItemRef destCont = sItemFactory.GetItemRefFromID(destItem->locationID(), false);
            if ((destCont.get() != nullptr) and (destCont->typeID() == EVEDB::invTypes::PlasticWrap))
                throw CustomError("You can't put items into a courier package.");
            CheckSourceAccess(call.client, srcItem, destItem->locationID(), destItem->flag());
        } catch (...) {
            sItemFactory.UnsetUsingClient();
            throw;
        }

        if (pInv->ValidateAddItem(destItem->flag(), srcItem)) {
            CheckLockedContainer(call.client, srcItem);
            CheckTheft(call.client, srcItem);
            uint32 fromLocationID(srcItem->locationID());
            destItem->Merge( srcItem, qty, true );
            LogContainerMove(call.client, destItem, fromLocationID, destItem->locationID(), qty);
        }
        // if false, error is thrown in ValidateAddItem() call
    }
    sItemFactory.UnsetUsingClient();

    return nullptr;
}

/* this call is used for moving an item to *THIS* inventory
 * Moving items to/from containers
 * Removing Module/Charges from ship (using 'remove' button on item slot)
 * Adding Modules to a specific slot on ship
 */
PyResult InventoryBound::Add(PyCallArgs &call, PyInt* itemID, PyInt* containerID) {
    // a password protected container (the client asks for the password and calls again)
    sContSec.CheckOpen(call.client, m_self);

    if (is_log_enabled(INV__DUMP)) {
        _log(INV__DUMP, "IB::Handle_Add() size= %lli", call.tuple->size());
        call.Dump(INV__DUMP);
    }

    uint16 toFlag = m_flag;
    if (call.byname.find("flag") != call.byname.end())
        toFlag = PyRep::IntegerValueU32(call.byname.find("flag")->second);
    toFlag = ResolveAddFlag(toFlag);

    InventoryItemRef iRef = sItemFactory.GetItemRef(itemID->value());
    if (iRef.get() == nullptr)
        return nullptr;

    bool moveStack = false;
    int32 quantity = 0;
    if (call.byname.find("qty") != call.byname.end())
        quantity = PyRep::IntegerValue(call.byname.find("qty")->second);

    if (call.byname.find("dividing") != call.byname.end()) {
        // split stack, move original ref, leave remainder here with new item
        InventoryItemRef newItem = iRef->Split(iRef->quantity() -quantity);
        if (newItem.get() == nullptr) {
            _log(INV__ERROR, "IB::Handle_Add() - Error splitting item %u. Skipping.", iRef->itemID());
            return nullptr;
        }
        iRef = newItem;
        itemID = new PyInt (iRef->itemID());
    // we're not dividing the stack, so check for removing loaded charges
    } else if ((iRef->categoryID() == EVEDB::invCategories::Charge) and (IsModuleSlot(iRef->flag()))) {
        moveStack = true;
        quantity = iRef->quantity();
    } else if (call.client->IsInSpace() and (toFlag == flagCargoHold) and (quantity == 0)) {
        moveStack = true;
        quantity = iRef->quantity();
    }

    float capacity = 0.0f;
    if (call.byname.find("capacity") != call.byname.end())
        capacity = PyRep::IntegerValueU32(call.byname.find("capacity")->second);

    if (quantity < 1)
        quantity = 1;

    _log(INV__MESSAGE, "IB::Handle_Add() - moving %i %s(%i) from (%i:%s) to me(%s:%u:%s).", \
            quantity, iRef->name(), itemID->value(), containerID->value(), sDataMgr.GetFlagName(iRef->flag()),\
            m_self->name(), m_itemID, sDataMgr.GetFlagName(toFlag));

    std::vector<int32> items;
    items.push_back(itemID->value());

    return MoveItems(call.client, items, (EVEItemFlags)toFlag, quantity, moveStack, capacity);
}

// this call is for moving items to *THIS* inventory
PyResult InventoryBound::MultiAdd(PyCallArgs &call, PyList* itemIDs, PyInt* containerID) {
    // a password protected container (the client asks for the password and calls again)
    sContSec.CheckOpen(call.client, m_self);

    if (is_log_enabled(INV__DUMP)) {
        _log(INV__DUMP, "IB::Handle_MultiAdd() size= %lli", call.tuple->size());
        call.Dump(INV__DUMP);
    }

    if (call.tuple->items.size() != 2) {
        _log(INV__ERROR, "IB::Handle_MultiAdd()  Unexpected number of elements in tuple: %lu (should be 2).", call.tuple->items.size() );
        return nullptr;
    }

    std::vector<int32> items;

    PyList::const_iterator list_2_cur = itemIDs->begin();
    for (size_t list_2_index(0); list_2_cur != itemIDs->end(); ++list_2_cur, ++list_2_index) {
        if (!(*list_2_cur)->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_MultiAdd_2 failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
            return nullptr;
        }

        const PyInt* t = (*list_2_cur)->AsInt();
        items.push_back(t->value());
    }


    uint16 toFlag = m_flag;
    if (call.byname.find("flag") != call.byname.end())
        toFlag = PyRep::IntegerValueU32(call.byname.find("flag")->second);
    toFlag = ResolveAddFlag(toFlag);

    int32 quantity = 1;
    if (call.byname.find("qty") != call.byname.end())
        quantity = PyRep::IntegerValue(call.byname.find("qty")->second);

    //bool byname(fromManyFlags):true == unload charges from module referenced
    bool moveStack = false;
    if (call.byname.find("fromManyFlags") != call.byname.end())
        if (!call.byname.find("fromManyFlags")->second->IsNone())
            moveStack = true;

    float capacity = 0.0f;
    if (call.byname.find("capacity") != call.byname.end())
        capacity = PyRep::IntegerValueU32(call.byname.find("capacity")->second);

    if (capacity > 1) {
        moveStack = true;
    } else if (quantity < 1) {
        moveStack = true;
    }

    // moving TO hangar...move all items in stack, if applicable...this includes ship corp hangars - is this what we want here?
    if (IsHangarFlag(toFlag))
        moveStack = true;

    if (m_self->IsShipItem() and !moveStack) {
        std::vector<InventoryItemRef> itemVec;
        for (auto cur : items)
            itemVec.push_back(sItemFactory.GetItemRef(cur));
        items = CatSortItems(itemVec);
    }

    _log(INV__MESSAGE, "IB::Handle_MultiAdd() - moving %lu items from (%i:%s) to me(%s:%u:%s).", \
                items.size(), containerID->value(), sDataMgr.GetFlagName(m_flag), m_self->name(), m_itemID, sDataMgr.GetFlagName(toFlag));

    return MoveItems( call.client, items, (EVEItemFlags)toFlag, quantity, moveStack, capacity);
}

PyRep* InventoryBound::MoveItems(Client* pClient, std::vector< int32 >& items, EVEItemFlags toFlag, int32 quantity, bool moveStack, float capacity)
{   // complete method rewrite -allan 21Dec17
    ShipItem* pShip = pClient->GetShip().get();
    bool donating = false, ship = false, customs = false;
    int32 origQty = quantity;

    // courier packages are sealed until broken
    if (m_self->typeID() == EVEDB::invTypes::PlasticWrap)
        throw CustomError("You can't put items into a courier package.");

    // we will need to check *this for specific item-moving rules
    switch (m_self->categoryID()) {
        // specific container-type categories that may move items in/out
        case EVEDB::invCategories::Trading: {
            // this shouldnt hit.  trading is handled in separate system
            codelog(INV__ERROR, "IB::MoveItems() - Trading Category called.");
            EvE::traceStack();
        } break;
        case EVEDB::invCategories::Structure: {
            // this is all POS groups.  use corp donating checks
            donating = true;
            // may have to reset flags based on type
            switch (m_self->groupID()) {
                case EVEDB::invGroups::Control_Tower: {
                    // flag 0 = fuel bay, flag = 122 strontium bay (2nd storage)
                } break;
                case EVEDB::invGroups::Mobile_Missile_Sentry:
                case EVEDB::invGroups::Mobile_Projectile_Sentry:
                case EVEDB::invGroups::Mobile_Laser_Sentry:
                case EVEDB::invGroups::Mobile_Hybrid_Sentry: {
                    // flag 27 = hislot0 using ammo capy
                } break;
                case EVEDB::invGroups::Electronic_Warfare_Battery:
                case EVEDB::invGroups::Sensor_Dampening_Battery:
                case EVEDB::invGroups::Stasis_Webification_Battery:
                case EVEDB::invGroups::Warp_Scrambling_Battery:
                case EVEDB::invGroups::Energy_Neutralizing_Battery:
                case EVEDB::invGroups::Target_Painting_Battery: {
                } break;
                case EVEDB::invGroups::Refining_Array:
                case EVEDB::invGroups::Ship_Maintenance_Array:
                case EVEDB::invGroups::Assembly_Array:
                case EVEDB::invGroups::Shield_Hardening_Array:
                case EVEDB::invGroups::Corporate_Hangar_Array:
                case EVEDB::invGroups::Stealth_Emitter_Array:
                case EVEDB::invGroups::Scanner_Array:
                case EVEDB::invGroups::Logistics_Array:
                case EVEDB::invGroups::Cynosural_Generator_Array:
                case EVEDB::invGroups::Structure_Repair_Array: {
                } break;
                default: {
                } break;
            }
        } break;
        case EVEDB::invCategories::Station: {
            switch (m_self->groupID()) {
                case EVEDB::invGroups::Station: {
                    // standard station hangar
                    if (toFlag == flagCorpMarket)
                        donating = true;
                    if (toFlag == flagImpounded)
                        donating = true;
                    //if (toFlag == flagDelivery)
                    //    donating = true;
                    if (toFlag == flagLocked) {
                        ; // do tests here for moving locked items (corp role config and ??)
                    }
                } break;
                case EVEDB::invGroups::Station_Services: {
                    // station offices, office and factory folders.
                    if (m_self->typeID() == EVEDB::invTypes::Office) //office.  use corp donating checks
                        donating = true;
                } break;
            }
        } break;
        case EVEDB::invCategories::Orbitals: {
            switch (m_self->groupID()) {
                case EVEDB::invGroups::Orbital_Construction_Platform: {
                        ; // not sure what to do here yet
                } break;
                case EVEDB::invGroups::Orbital_Infrastructure: {
                    // orbital command centers and customs offices
                    // we dont change owners of xfered items
                    customs = true;
                } break;
            }
        } break;
        case EVEDB::invCategories::Celestial: {
            // containers, wrecks, construction platform, station improve/upgrade platform,
            if (IsPlayerCorp(m_ownerID))
                donating = true;
        } break;
        case EVEDB::invCategories::Ship: {
            // do module checks and specific removeItem() for these items
            ship = true;
            // will need to add checks on from container to verify donating flag here.
            //donating = true;
        } break;
    }

    EVEItemFlags fromFlag(flagNone);
    EVEItemFlags origFlag(toFlag);
    InventoryItemRef iRef(nullptr);
    sItemFactory.SetUsingClient(pClient);

    std::vector<int32>::const_iterator itr = items.begin();
    for (; itr != items.end(); ++itr) {
        // reset vars for adding multiple items
        toFlag = origFlag;
        quantity = origQty;

        iRef = sItemFactory.GetItemRef(*itr);
        if (iRef.get() == nullptr) {
            _log(INV__ERROR, "IB::MoveItems() - item %i not found.  continuing.", (*itr));
            continue;
        }

        // may we take it from where it is?
        try {
            CheckSourceAccess(pClient, iRef, m_itemID, toFlag);
        } catch (...) {
            sItemFactory.UnsetUsingClient();
            throw;
        }

        if (iRef->typeID() == EVEDB::invTypes::Bookmark) {
            // update this to keep owner/creator and other data
            iRef->Donate(m_ownerID, m_itemID, toFlag);
            continue;
        }

        // taking it out of someone else's can or wreck
        if (iRef->locationID() != m_itemID) {
            CheckLockedContainer(pClient, iRef);
            CheckTheft(pClient, iRef);
        }

        fromFlag = iRef->flag();

        if (moveStack)
            quantity = iRef->quantity();

        // if client send capy, it is TOTAL space in this container, NOT current space.
        if (capacity > 0) {
            if (quantity < 1)
                quantity = iRef->quantity();    // assume all.
        } else if (quantity < 1) {
            _log(INV__ERROR, "IB::MoveItems() - Quantity < 1.  Setting quantity = 1.");
            quantity = 1;
        }

        // check for items that need specific handling
        if (IsRigSlot(fromFlag)) //  cant remove rigs like this.  send error.
            throw UserError ("CannotRemoveUpgradeManually");
        // a fitted subsystem can only be replaced, by dropping another one on its slot
        else if (IsSubSystem(fromFlag))
            throw UserError ("CantUnfitSubSystems");
        else if (IsModuleSlot(fromFlag)) {
            // can we remove modules from an inactive ship?  not yet...
            if (pShip == nullptr)
                throw CustomError ("Ship not found. The %s wasnt moved.  Ref: ServerError 63290", iRef->name());

            //if (sDataMgr.IsSolarSystem(pShip->locationID()))
            //    throw CustomError ("You cannot remove modules in space.");

            // verify module isnt active here (before we get too far in processing)
            GenericModule* pMod = pShip->GetModule(fromFlag);
            if (pMod == nullptr)
                throw CustomError ("That module was not found.  Chances are this is a server error, and either docking or reloging will correct it.");
            if (pMod->IsActive())
                throw CustomError ("Your %s is currently active.  You must wait for the cycle to complete before it can be removed.", pMod->GetSelf()->name());

            if (IsModuleSlot(toFlag)) {
                if (sDataMgr.IsSolarSystem(pShip->locationID()))
                    throw CustomError ("You cannot exchange module slots in space.");
                //ModulesNotLoadableInSpace  <-- this needs {device} but i dont know what module it is

                // we are wanting to change slots on a fitted module.
                pShip->MoveModuleSlot(fromFlag, toFlag);
                Call_SingleIntegerArg result;
                result.arg = iRef->itemID();
                return result.Encode();
            } else {
                // are we just unloading charges?
                if (iRef->categoryID() == EVEDB::invCategories::Charge) {
                    pShip->UnloadModule(pMod);
                    return nullptr;
                }
                pShip->RemoveItem(iRef);
            }
        }

        if (!moveStack and (quantity < iRef->quantity())) {
            InventoryItemRef newItem = iRef->Split(quantity);
            if (newItem.get() == nullptr) {
                _log(INV__ERROR, "IB::MoveItems() - Error splitting item %u. Skipping.", iRef->itemID());
                continue;
            }
            iRef = newItem;
            if (iRef.get() == nullptr) {
                _log(INV__ERROR, "IB::MoveItems() - Error getting split item. Skipping.");
                continue;
            }
            if (iRef->quantity() > quantity) {
                _log(INV__ERROR, "IB::MoveItems() - Split item %u qty(%u) > requested qty of %u.  Continuing.", \
                            iRef->itemID(), iRef->quantity(), quantity);
            }
        }

        uint32 fromLocationID(iRef->locationID());
        if (donating) {
            iRef->Donate(m_ownerID, m_itemID, toFlag);
            LogContainerMove(pClient, iRef, fromLocationID, m_itemID);
            continue;
        }

        // move item to new location
        if (ship) {
            // are we adding module to ship using autoFit?
            if (toFlag == flagNone) {
               // assert(iRef->categoryID() != EVEDB::invCategories::Charge); // crash here...this should NOT happen.
                if ((iRef->categoryID() == EVEDB::invCategories::Module)
                or  (iRef->categoryID() == EVEDB::invCategories::Subsystem)) {
                    toFlag = pShip->FindAvailableModuleSlot(iRef);
                    if (toFlag == flagIllegal) {
                        pClient->SendNotifyMsg("Your ship has no available slots to fit this module.  Putting the %s in your CargoHold.", iRef->name());
                        toFlag = flagCargoHold;
                    }
                } else {
                    // this needs work to verify mFlag is correct for application and that it is init'd correctly
                    toFlag = m_flag;
                }
            }

            //{'FullPath': u'UI/Messages', 'messageID': 259155, 'label': u'DronesNotLoadableInSpaceBody'}(u'You cannot load to or unload from the drone bay while in space.', None, None)
            if (pClient->IsInSpace() and ((toFlag == flagDroneBay) != (iRef->flag() == flagDroneBay)))
                throw UserError ("DronesNotLoadableInSpace");

            // subsystems are only changed docked.  one dropped on a fitted subsystem replaces it
            InventoryItemRef oldSubRef(nullptr);
            if (IsSubSystem(toFlag)) {
                if (pClient->IsInSpace())
                    throw UserError ("CantFitSubSystemInSpace");
                oldSubRef = m_self->GetShipItem()->GetModuleRef(toFlag);
            }

            // verify item is allowed in container first
            m_self->GetShipItem()->VerifyHoldType(toFlag, iRef, pClient); // this will throw if it fails

            // then check for module limits
            if (IsModuleSlot(toFlag)) {
                m_self->GetShipItem()->TryModuleLimitChecks(toFlag, iRef, (oldSubRef.get() != nullptr)); // this will throw if it fails
            } else if (IsCargoHoldFlag(toFlag) or IsHangarFlag(toFlag) or (toFlag == flagDroneBay)) {
                pInventory->ValidateAddItem(toFlag, iRef);  // this will throw if it fails
            }

            if (oldSubRef.get() != nullptr) {
                // the replaced subsystem goes where the new one came from
                uint32 oldLocationID(iRef->locationID());
                EVEItemFlags oldFlag(iRef->flag());
                m_self->GetShipItem()->RemoveItem(oldSubRef);
                oldSubRef->Move(oldLocationID, oldFlag, true);
            }

            // check adding item to ship...if it fails, return to previous container
            if (m_self->GetShipItem()->AddItemByFlag(toFlag, iRef, pClient) < 1) {
                //ALL items *should* have a loaded container item.
                InventoryItemRef contRef = sItemFactory.GetItemContainerRef(*itr);
                if (contRef.get() != nullptr) {
                    contRef->AddItem(iRef);
                } else {
                    _log(INV__ERROR, "IB::MoveItems() - previous container for item %i not found.  continuing.", (*itr));
                }
                continue;
            }
            // a T3 with fewer slots or hardpoints now
            if (IsSubSystem(toFlag))
                m_self->GetShipItem()->EvictExcessModules();
        } else if (customs) {
            pInventory->ValidateAddItem(toFlag, iRef);  // this will throw if it fails
            StructureItemRef sRef = StructureItemRef::StaticCast(m_self);
            sRef->AddItem(iRef);// this will throw if it fails
        } else {
            pInventory->ValidateAddItem(toFlag, iRef);  // this will throw if it fails
            iRef->Donate(m_ownerID, m_itemID, toFlag);
        }
        LogContainerMove(pClient, iRef, fromLocationID, m_itemID);
    }

    sItemFactory.UnsetUsingClient();

    if (iRef.get() == nullptr)
        return nullptr;

    if (items.size() == 1) {
        //call returns itemID for single-item adds...not sure about others
        Call_SingleIntegerArg result;
        result.arg = iRef->itemID();
        return result.Encode();
    }

    return nullptr;
}

std::vector< int32 > InventoryBound::CatSortItems(std::vector< InventoryItemRef >& itemVec)
{
    /* sorts a vector of items by category, with loaded modules first (in slot order), then loaded charges (in slot order), then cargo
     * if there is only one item, no sorting required...
     *  this is called on fitting a group of modules from MultiAdd
     *   -allan
     */
    std::vector<int32> items;
    if (itemVec.size() < 2) {
        items.push_back(itemVec.at(0)->itemID());
        return items;
    }

    uint16 count(0);
    double start(0.0);
    if (sConfig.debug.IsTestServer and sConfig.debug.UseProfiling)
        start = GetTimeUSeconds();

    //begin basic sort
    bool done(false);
    InventoryItemRef tmp(nullptr);
    while (!done) {
        done = true;  //assume sorted
        //iterate though list
        for (int i = 0, i2 = 1; (i < itemVec.size()) and (i2 < itemVec.size()); ++i, ++i2) {
            //check if each pair is sorted by category.   modules -> charges -> subsystems
            if (itemVec[i]->categoryID() < itemVec[i2]->categoryID()) {
                //it's not, so flip the values
                tmp = itemVec[i];
                itemVec[i] = itemVec[i2];
                itemVec[i2] = tmp;
                done = false;  //we weren't sorted, so now go back and check if we are
            }
            ++count;
        }
    }

    for (auto cur : itemVec)
        items.push_back(cur->itemID());

    if (sConfig.debug.IsTestServer and sConfig.debug.UseProfiling)
        sLog.Warning("IB::CatSortItems", "%lu items sorted in %.3fus with %u loops.", items.size(), (GetTimeUSeconds() - start), count);

    return items;  //returns sorted list
}

/**     ***********************************************************************
 * @note   these below are partially coded
 */

PyResult InventoryBound::List(PyCallArgs &call, std::optional <PyInt*> listFlag) {
    if (pInventory == nullptr)
        return PyStatic.NewNone();

    // a password protected container (the client asks for the password and calls again)
    sContSec.CheckOpen(call.client, m_self);

    _log(INV__DUMP, "IB::List() dump.");
    call.Dump(INV__DUMP);

    /* ownerID will need to be 'modified' here for corp access because of how we load items */
    //uint32 ownerID = m_ownerID;

    /* this item was originally bound to this flag, but can send specific flag on occasion
     * usually flag is set for subLocation, shipHangar, POS items, and CCHold
        def GetSubSystemInFlag(self, shipID, flagID):
            items = shipInv.List(flagID)
     */

    EVEItemFlags flag = m_flag, oldFlag = m_flag;
    if (call.byname.find("flag") != call.byname.end())
        flag = (EVEItemFlags)PyRep::IntegerValueU32(call.byname.find("flag")->second);

    if (listFlag.has_value()) {
        flag = (EVEItemFlags)listFlag.value()->value();
    }

    _log(INV__MESSAGE, "IB::List() called by %s with ownerID %u for %s(%u:%s%s) - origFlag: %s", \
    call.client->GetName(), m_ownerID, m_self->name(), m_itemID, sDataMgr.GetFlagName(flag), \
            (m_passive ? ":passive" : ""), sDataMgr.GetFlagName(oldFlag));

    // check for owner type of this inventory for reference checks
    if (IsOfficeID(m_itemID)) {
        // office owned by corp in station
        // check for owner or corp
        if (call.client->GetCorporationID() != m_ownerID)
            ; // calling char is not member of corp.  send error?
    } else if (IsPlayerItem(m_itemID)) {
        // this is probably a ship calling for all items
        //  is also used for t3 ships, POS, subLocations, and possibly others that are 'player items'
        flag = flagNone;
        /*
    } else if (IsPlayerCorp(m_itemID)) {
        // this one probably will not be used.
        //  what items in a corp would be listed?  corpItem dont have inventory
    } else if (IsCharacterID(m_itemID)) { // this is checked in Inventory::List()
        // this is asking for skill list...char is a container for skills
        flag = flagNone;
    } else if (sDataMgr.IsSolarSystem(m_itemID)) {
        //  not sure how to do this one...will have to check on WHEN system listing would be called
        */
    } else if (sDataMgr.IsStation(m_itemID)) {
        // this will get owners items only, including corps
    } else if (IsControlBunker(m_itemID)) {
        // not sure what this is yet
    }
    /*
     *    if (IsCargoHoldFlag(flag)) {
     *        // check for owner or corpID
     *
} else if (IsHangarFlag(flag)) {
    // check for owner or corpID

    flag = flagNone;
    if (call.client->GetCorporationID() != m_ownerID)
        ; // calling char is not member of corp.  send error?
} else if (IsOfficeFlag(flag)) {
    // flags for npc station containers, owned by station, but items owned by corp
    //  this is market deliveries, impounded, etc.
    // check for corpID

    flag = flagNone;
    if (call.client->GetCorporationID() != m_ownerID)
        ; // calling char is not member of corp.  send error?
}
*/

    return pInventory->List(flag, m_ownerID);
}

PyResult InventoryBound::CreateBookmarkVouchers(PyCallArgs &call, PyList* bookmarkIDs, PyInt* flag, PyBool* isMove) {
    /*
     *    bookmarksDeleted, newVouchers = self.CreateBookmarkVouchers(bookmarkIDs, flag, isMove)
     */
    call.Dump(BOOKMARK__CALL_DUMP);
    /*
        if (tuple1->GetItem(0)->IsList()) {
        bmIDs = tuple1->GetItem(0)->AsList();
        //PyIncRef(bmIDs);
    } else {
        _log(XMLP__DECODE_ERROR, "Decode Call_CreateVouchers failed: bmIDs is not a list: %s", tuple1->GetItem(0)->TypeString());
        return false;
    }

    flag = PyRep::IntegerValue(tuple1->GetItem(1));
    isMove = PyRep::IntegerValue(tuple1->GetItem(2));
    */
    if (m_self->ownerID() != call.client->GetCharID())
        throw UserError ("CanOnlyCreateVoucherInPersonalHangar");

    PyList* vouchers = new PyList();
    PyList* deleted = new PyList();

    uint32 locationID = call.client->GetLocationID();
    if (flag->value() == flagCargoHold)
        locationID = call.client->GetShipID();

    // when trying to copy vouchers to jetcan, location is solar system....grrrr
    if (sDataMgr.IsSolarSystem(locationID)) {
        flag = new PyInt (flagCargoHold);
        locationID = call.client->GetShipID();
    }

    if ( bookmarkIDs->size() < 1 ) {
        sLog.Error( "IB::Handle_CreateBookmarkVouchers()", "%s: args.bmIDs->size() == 0.  Expected size > 0.", call.client->GetName() );
    } else {
        BookmarkDB m_db;
        PyList::const_iterator itr = bookmarkIDs->begin();
        for (; itr != bookmarkIDs->end(); ++itr) {
            //ItemData ( typeID, ownerID, locationID, flag, quantity, customInfo, contraband)
            ItemData iData( 51, call.client->GetCharacterID(), locTemp, flagNone, 1, itoa(PyRep::IntegerValueU32(*itr)));
            InventoryItemRef iRef = sItemFactory.SpawnItem( iData );
            if (iRef.get() == nullptr) {
                codelog(ITEM__ERROR, "%s: Failed to spawn bookmark voucher for bmID %u", call.client->GetName(), PyRep::IntegerValueU32(*itr));
                continue;
            }
            //iRef->Rename(std::to_string(BookmarkDB::GetBookmarkName(PyRep::IntegerValueU32(*itr))));
            iRef->Move(locationID, (EVEItemFlags)flag->value(), true);
            /*
            PyDict* dict = new PyDict();
            dict->SetItemString("description", new PyString(BookmarkDB::GetBookmarkName(atoi(iRef->customInfo().c_str()))));
            dict->SetItemString( "itemID",       new PyInt(iRef->itemID()));
            dict->SetItemString( "typeID",       new PyInt(iRef->type().id()));
            dict->SetItemString( "ownerID",      new PyInt(iRef->ownerID()));
            dict->SetItemString( "locationID",   new PyInt(iRef->locationID()));
            dict->SetItemString( "flagID",       new PyInt(iRef->flag()));
            dict->SetItemString( "quantity",     new PyInt(iRef->quantity()));
            dict->SetItemString( "groupID",      new PyInt(iRef->type().groupID()));
            dict->SetItemString( "categoryID",   new PyInt(iRef->type().categoryID()));
            dict->SetItemString( "customInfo",   new PyString(iRef->customInfo()));
            vouchers->AddItem(new PyObject("util.KeyVal", dict));
            */
            if (isMove->value()) {
                PyDict* dict = new PyDict();
                // may need more here.  not sure yet
                //dict->SetItemString("description", new PyString(BookmarkDB::GetBookmarkName(atoi(iRef->customInfo().c_str()))));
                dict->SetItemString("bookmarkID", new PyInt(PyRep::IntegerValueU32(*itr)));
                deleted->AddItem(new PyObject("util.KeyVal", dict));
                // change owner in db to remove bm from current owner's pnp window
                m_db.ChangeOwner(PyRep::IntegerValueU32(*itr));
            }
        }
    }

    //  when bm is copied to another players' places tab, copy data from db using bookmarkID stored in ItemData.customInfo

    PyTuple* tuple = new PyTuple(2);
    tuple->SetItem(0, deleted );
    tuple->SetItem(1, vouchers);
    tuple->Dump(BOOKMARK__RSP_DUMP, "    ");
    return tuple;
}

/**     ***********************************************************************
 * @note   these do absolutely nothing at this time....
 */

PyResult InventoryBound::TakeOutTrash(PyCallArgs &call, PyList* itemIDs) {
    // GM menu:  invCache.GetInventory(const.containerHangar).TakeOutTrash([ invItem.itemID for invItem in invItems ])
    _log(INV__MESSAGE, "%s Calling InventoryBound::TakeOutTrash() for %s(%u)", call.client->GetName(), m_self->name(), m_itemID);
    GMTakeOutTrash(call.client, m_itemID, itemIDs);
    return nullptr;
}

void InventoryBound::GMTakeOutTrash(Client* pClient, uint32 hangarID, PyList* itemIDs)
{
    if ((pClient->GetAccountRole() & (Acct::Role::GML | Acct::Role::GMH | Acct::Role::WORLDMOD)) == 0)
        throw UserError("PermissionDenied");

    for (PyList::const_iterator itr = itemIDs->begin(); itr != itemIDs->end(); ++itr) {
        if (!(*itr)->IsInt())
            continue;
        InventoryItemRef iRef = sItemFactory.GetItemRef(PyRep::IntegerValueU32(*itr));
        if (iRef.get() == nullptr)
            continue;
        // only what lies on the hangar floor here.  never the active ship
        if ((iRef->locationID() != hangarID) or (iRef->flag() != flagHangar) or (iRef->itemID() == pClient->GetShipID()))
            continue;
        if (iRef->IsShipItem() and ShipItemRef::StaticCast(iRef)->HasPilot())
            continue;
        _log(INV__MESSAGE, "IB::GMTakeOutTrash() - %s deleted %s(%u) owned by %u in %u", \
                pClient->GetName(), iRef->name(), iRef->itemID(), iRef->ownerID(), hangarID);
        iRef->Delete();
    }
}

void InventoryBound::CheckSourceAccess(Client* pClient, InventoryItemRef iRef, uint32 toLocationID, EVEItemFlags toFlag)
{
    // rearranging in place
    if ((iRef->locationID() == toLocationID) and (iRef->flag() == toFlag))
        return;

    // what the item is in now (not loaded for stations and systems)
    InventoryItemRef srcRef = sItemFactory.GetItemRefFromID(iRef->locationID(), false);
    if ((srcRef.get() == nullptr) and (IsPlayerItem(iRef->locationID()) or IsOfficeID(iRef->locationID())))
        srcRef = sItemFactory.GetItemRef(iRef->locationID());

    if (srcRef.get() != nullptr) {
        // an unbroken courier package keeps its contents
        if (srcRef->typeID() == EVEDB::invTypes::PlasticWrap)
            throw CustomError("You must break the courier package before you can take anything out of it.");
        // locked audit log container contents stay until they are unlocked
        if ((iRef->flag() == flagLocked) and (srcRef->groupID() == EVEDB::invGroups::Audit_Log_Secure_Container))
            throw UserError("CrpItemIsLocked");
        // a password protected container
        sContSec.CheckOpen(pClient, srcRef);
    }

    // corp hangar division take roles
    uint32 corpID(pClient->GetCorporationID());
    if (!IsPlayerCorp(corpID))
        return;

    if (iRef->ownerID() == corpID) {
        // an office division floor.  the office is in the station
        if (IsOfficeID(iRef->locationID()) and (srcRef.get() != nullptr)) {
            if (!Inventory::CanTakeFromCorpHangar(pClient, srcRef->locationID(), iRef->flag()))
                throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You do not have the role to take items from that hangar division."));
            return;
        }
        // the deliveries hangar
        if (sDataMgr.IsStation(iRef->locationID()) and (iRef->flag() == flagCorpMarket)) {
            if (!Inventory::CanTakeFromCorpHangar(pClient, iRef->locationID(), flagCorpMarket))
                throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You do not have the role Accountant or Trader."));
            return;
        }
    }

    // a corp container sitting in an office division
    if ((srcRef.get() != nullptr) and (srcRef->ownerID() == corpID)
    and (srcRef->categoryID() == EVEDB::invCategories::Celestial) and IsOfficeID(srcRef->locationID())) {
        InventoryItemRef officeRef = sItemFactory.GetItemRef(srcRef->locationID());
        if ((officeRef.get() != nullptr)
        and !Inventory::CanTakeFromCorpHangar(pClient, officeRef->locationID(), srcRef->flag(), true))
            throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You do not have the role to take items from containers in that hangar division."));
    }
}

PyResult InventoryBound::BreakPlasticWrap(PyCallArgs &call) {
    /* invCache.GetInventoryFromId(wrapID).BreakPlasticWrap(), after ConfirmBreakCourierPackage
     *  the contents go where the package is, to its owner, and the package goes - which fails the courier contract
     *  (ContractUtils::OnCrateDeleted from InventoryItem::Delete) */
    if (m_self->typeID() != EVEDB::invTypes::PlasticWrap)
        throw CustomError("That isn't a courier package.");

    Client* pClient(call.client);
    if (!pClient->IsDocked())
        throw UserError("CanOnlyDoInStations");
    uint32 stationID(pClient->GetStationID()), corpID(pClient->GetCorporationID());
    uint32 officeID(IsPlayerCorp(corpID) ? stDataMgr.GetOfficeIDForCorp(stationID, corpID) : 0);

    if (m_self->ownerID() == pClient->GetCharacterID()) {
        // our item hangar here
        if ((m_self->locationID() != stationID) or (m_self->flag() != flagHangar))
            throw UserError("CanOnlyDoInStations");
    } else if (IsPlayerCorp(corpID) and (m_self->ownerID() == corpID)) {
        // our corp's office or deliveries here
        if (!(((officeID != 0) and (m_self->locationID() == officeID) and IsHangarFlag(m_self->flag()))
            or ((m_self->locationID() == stationID) and (m_self->flag() == flagCorpMarket))))
            throw UserError("CanOnlyDoInStations");
        if (!Inventory::CanTakeFromCorpHangar(pClient, stationID, m_self->flag()))
            throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You do not have the role to take items from that hangar division."));
    } else {
        throw UserError("CantDoThatWithSomeoneElsesStuff");
    }

    uint32 wrapID(m_itemID), locationID(m_self->locationID()), ownerID(m_self->ownerID());
    EVEItemFlags flag(m_self->flag());

    sItemFactory.SetUsingClient(pClient);
    std::vector<InventoryItemRef> items;
    if (pInventory != nullptr) {
        pInventory->LoadContents();
        pInventory->GetInventoryVec(items);
    }
    for (auto cur : items) {
        // the goods were held by the system while in transit
        if (cur->ownerID() != ownerID)
            ContractUtils::ChangeOwnerRecursive(cur, ownerID);
        // the package deletes itself when the last item comes out
        cur->Move(locationID, flag, true);
    }
    // an empty package
    if (sItemFactory.GetItemRefFromID(wrapID, false).get() != nullptr)
        m_self->Delete();
    sItemFactory.UnsetUsingClient();

    _log(INV__MESSAGE, "IB::BreakPlasticWrap() - %s broke courier package %u (%lu items)", pClient->GetName(), wrapID, items.size());
    return nullptr;
}

EVEItemFlags InventoryBound::ResolveAddFlag(uint16 toFlag) const
{
    // audit log containers hold their contents locked or unlocked.  the client sends its lock default (flagLocked when unset)
    if (m_self->groupID() == EVEDB::invGroups::Audit_Log_Secure_Container) {
        if ((toFlag == flagLocked) or (toFlag == flagUnlocked))
            return (EVEItemFlags)toFlag;
        return flagLocked;
    }
    // locked/unlocked only exist in audit log containers
    if ((toFlag == flagLocked) or (toFlag == flagUnlocked))
        return m_flag;
    return (EVEItemFlags)toFlag;
}

/* secure containers and audit log containers.  the client wraps these in its PasswordHandlerCallWrapper */

PyResult InventoryBound::SetPassword(PyCallArgs &call, PyInt* which, PyRep* oldPassword, PyRep* newPassword) {
    // container.SetPassword(which, old|None, new)
    sContSec.SetPassword(call.client, m_self, which->value(), oldPassword, newPassword);
    return nullptr;
}

PyResult InventoryBound::EnterPassword(PyCallArgs &call, PyInt* which, PyRep* password) {
    sContSec.EnterPassword(call.client, m_self, which->value(), password);
    return nullptr;
}

PyResult InventoryBound::HasExistingPasswordSet(PyCallArgs &call, PyInt* which) {
    // decides whether the client asks for the old password
    if (!ContainerSecurity::IsSecure(m_self))
        return new PyBool(false);
    return new PyBool(sContSec.HasPassword(m_itemID, which->value()));
}

PyResult InventoryBound::RetrievePassword(PyCallArgs &call, PyInt* which) {
    sContSec.RetrievePassword(call.client, m_self, which->value());
    return nullptr;
}

PyResult InventoryBound::LockItem(PyCallArgs &call, PyInt* itemID) {
    // menu ALSCLock:  invCache.GetInventoryFromId(item.locationID).LockItem(item.itemID)
    if (!ContainerSecurity::IsAuditLog(m_self))
        throw UserError("CantConfigureThat");
    sContSec.CheckManage(call.client, m_self);
    sContSec.CheckConfigPassword(call.client, m_self, Inv::ALSCConfig::NeedToLock);

    if (pInventory != nullptr)
        pInventory->LoadContents();
    InventoryItemRef iRef = ((pInventory == nullptr) ? InventoryItemRef(nullptr) : pInventory->GetByID(itemID->value()));
    if (iRef.get() == nullptr)
        throw UserError("CrpLockedItemNotFound");
    if (iRef->flag() == flagLocked)
        throw UserError("CrpItemIsLocked");

    iRef->SetFlag(flagLocked, true);
    sContSec.Log(m_self, call.client->GetCharacterID(), Inv::ALSCAction::Lock, true, iRef->typeID(), (iRef->isSingleton() ? 1 : iRef->quantity()));
    return nullptr;
}

PyResult InventoryBound::UnlockItem(PyCallArgs &call, PyInt* itemID) {
    // menu ALSCUnlock:  invCache.GetInventoryFromId(item.locationID).UnlockItem(item.itemID)
    if (!ContainerSecurity::IsAuditLog(m_self))
        throw UserError("CantConfigureThat");
    sContSec.CheckManage(call.client, m_self);
    sContSec.CheckConfigPassword(call.client, m_self, Inv::ALSCConfig::NeedToUnlock);

    if (pInventory != nullptr)
        pInventory->LoadContents();
    InventoryItemRef iRef = ((pInventory == nullptr) ? InventoryItemRef(nullptr) : pInventory->GetByID(itemID->value()));
    if (iRef.get() == nullptr)
        throw UserError("CrpLockedItemNotFound");
    if (iRef->flag() != flagLocked)
        throw UserError("CrpItemIsNotLocked");

    iRef->SetFlag(flagUnlocked, true);
    sContSec.Log(m_self, call.client->GetCharacterID(), Inv::ALSCAction::Unlock, true, iRef->typeID(), (iRef->isSingleton() ? 1 : iRef->quantity()));
    return nullptr;
}

PyResult InventoryBound::ALSCConfigGet(PyCallArgs &call) {
    // the client hides the password settings when this throws
    if (!ContainerSecurity::IsAuditLog(m_self))
        throw UserError("CantConfigureThat");
    sContSec.CheckManage(call.client, m_self);
    sContSec.CheckConfigPassword(call.client, m_self, 0);
    return new PyInt(sContSec.GetConfig(m_itemID));
}

PyResult InventoryBound::ALSCConfigSet(PyCallArgs &call, PyInt* config) {
    sContSec.SetConfig(call.client, m_self, config->value());
    return nullptr;
}

PyResult InventoryBound::ALSCLogGet(PyCallArgs &call, PyRep* fromDate, PyRep* fromLogID, PyInt* forward) {
    // AuditLogSecureContainerLogViewer:  container.ALSCLogGet(fromDate, fromLogID, forward), 50 at a time
    if (!ContainerSecurity::IsAuditLog(m_self))
        throw UserError("CantConfigureThat");
    sContSec.CheckViewLog(call.client, m_self);
    sContSec.CheckConfigPassword(call.client, m_self, Inv::ALSCConfig::NeedToViewLog);
    return sContSec.GetLog(m_itemID, fromDate, fromLogID, forward->value());
}

PyResult InventoryBound::ListDroneBay(PyCallArgs &call) {
    // i dont think this one is used....
    sLog.Error("IB::ListDroneBay", "Character '%s', self: '%s'(%u)", call.client->GetName(), m_self->name(), m_itemID);
    _log(INV__MESSAGE, "%s Calling InventoryBound::ListDroneBay() for %s(%u)", call.client->GetName(), m_self->name(), m_itemID);
    call.Dump(INV__DUMP);
    return nullptr;
}

PyResult InventoryBound::RunRefiningProcess(PyCallArgs &call) {
    _log(POS__MESSAGE, "%s Calling InventoryBound::RunRefiningProcess() for %s(%u)", call.client->GetName(), m_self->name(), m_itemID);
    call.Dump(POS__DUMP);
    return nullptr;
}

// This function is called when an outpost construction platform is instructed to build
PyResult InventoryBound::Build(PyCallArgs &call) {
    _log(POS__MESSAGE, "%s Calling InventoryBound::Build() for %s(%u)", call.client->GetName(), m_self->name(), m_itemID);
    call.Dump(POS__DUMP);

    /*
    Outpost construction process:
    1. Ensure all required items are in the egg
    2. Initiate the destruction of the platform
    3. Initiate the building and registration of the new station
    */

    // Step 1
    _log(POS__MESSAGE, "Checking construction requirements for %s(%u).", m_self->name(), m_itemID);

    // Retrieve the SE object for the egg itself
    SystemEntity* egg = call.client->SystemMgr()->GetEntityByID(m_itemID);

    // Check if anchored, and if not return nullptr and error
    if (egg->GetPOSSE()->GetState() != EVEPOS::StructureState::Anchored) {
        call.client->SendNotifyMsg("This operation requires the construction platform to be anchored.");
        return nullptr;
    }

    // Get required materials to construct the outpost
    DBQueryResult res;
    DBResultRow row;
    uint32 stationType = m_self->GetAttribute(AttrStationTypeID).get_uint32();
    FactoryDB::GetOutpostMaterialCompositionOfItemType(stationType, res);
    std::vector<InventoryItemRef> platformItems;

    m_self->GetMyInventory()->GetItemsByFlag(flagNone, platformItems);

    while (res.GetRow(row)) {
        uint32 requiredType = row.GetUInt(0);
        uint32 requiredQuantity = row.GetUInt(1);
        uint32 quantity = 0;
        for (auto cur : platformItems) {
            if (cur->type().id() == requiredType) {
                quantity += cur->quantity();
                if (quantity >= requiredQuantity) {
                    break;
                }
            }
        }
        if (quantity < requiredQuantity) {
            call.client->SendNotifyMsg("This operation requires %u units of %s in the construction platform.", requiredQuantity, sItemFactory.GetType(requiredType)->name().c_str());
            return nullptr;
        }
    }

    // Step 2
    _log(POS__MESSAGE, "Removing entity %s(%u) from space safely.", m_self->name(), m_itemID);

    // Save the anchor position and planet radius since we'll need it when setting up the new station
    GPoint anchorPosition = egg->GetPosition();

    // Clear egg's data and remove it from space
    call.client->SystemMgr()->RemoveEntity(egg);
    m_self->ChangeOwner(0, true);
    SafeDelete(egg);

    // Step 3
    _log(POS__MESSAGE, "Creating new OutpostSE entity...");

    // Create the item data which we use to create the StationItemRef
    StationData stData = StationData();

    // Calculate stationID
    stData.stationID = StationDB::GetNewOutpostID();

    // Get base station data
    StationDB::GetStationBaseData(res, stationType);
    std::string stationBaseName;
    while (res.GetRow(row)) {
        stData.dockOrientation = GVector(row.GetDouble(0),row.GetDouble(1),row.GetDouble(2));
        stData.conquerable = row.GetBool(3);
        stData.hangarGraphicID = row.GetUInt(4);
        stData.description = row.GetText(5);
        stData.descriptionID = row.GetInt(6);
        stData.graphicID = row.GetInt(7);
        stData.dockEntry = GPoint(row.GetDouble(8),row.GetDouble(9),row.GetDouble(10));
        stData.operationID = row.GetUInt(11);
        stData.dockPosition = GPoint (row.GetDouble(8) + anchorPosition.x,
                                      row.GetDouble(9) + anchorPosition.y,
                                      row.GetDouble(10) + anchorPosition.z);
        stationBaseName = row.GetText(12);
    }

    // Get radius from StationType object
    StationType* stType = StationType::Load(stationType);
    stData.radius = stType->radius();

    // Location data
    stData.systemID = call.client->GetSystemID();
    stData.constellationID = call.client->GetConstellationID();
    stData.regionID = call.client->GetRegionID();
    stData.position = anchorPosition;
    stData.security = call.client->SystemMgr()->GetSecValue();

    // Other station data
    stData.typeID = stationType;
    stData.reprocessingHangarFlag = flagHangar;
    stData.corporationID = call.client->GetCorporationID();

    // Build station name
    stData.name = call.client->SystemMgr()->GetClosestPlanetSE(anchorPosition)->GetName()
        + std::string(" - ") + stationBaseName;

    // Set default configurable values
    stData.officeRentalFee = 10000;
    stData.maxShipVolumeDockable = 50000000;
    stData.dockingCostPerVolume = 0;

    // Set default service values
    stData.officeSlots = 8;
    stData.reprocessingEfficiency = 0.5;
    stData.reprocessingStationsTake = 0.05;

    // Set space values
    stData.orbitID = call.client->SystemMgr()->GetClosestPlanetID(anchorPosition);

    // Calculate service mask (temporarily, allow everything)
    stData.serviceMask = Station::ReprocessingPlant                        
                       | Station::Refinery
                       | Station::Market
                       | Station::BlackMarket
                       | Station::StockExchange
                       | Station::Cloning
                       | Station::Surgery
                       | Station::DNATherapy
                       | Station::RepairFacilities
                       | Station::Factory
                       | Station::Laboratory
                       | Station::Gambling
                       | Station::Fitting
                       | Station::Paintshop
                       | Station::News
                       | Station::Storage
                       | Station::Insurance
                       | Station::Docking
                       | Station::OfficeRental
                       | Station::JumpCloneFacility
                       | Station::LoyaltyPointStore
                       | Station::NavyOffices;

    // Add the new outpost to the stationDataMgr and the DB
    stDataMgr.AddOutpost(stData);

    // Update staticDataMgr
    sDataMgr.AddOutpost(stData);

    // Create the StationItem and spawn the OutpostSE entity
    StationItemRef itemRef = sItemFactory.GetStationRef(stData.stationID);
    OutpostSE* oSE = new OutpostSE(itemRef, call.client->services(), call.client->SystemMgr());
    sEntityList.AddStation(stData.stationID, itemRef);
    call.client->SystemMgr()->AddEntity(oSE);

    // Create and spawn all of the station service entities
    _log(POS__MESSAGE, "Creating new station service entities...");

    oSE->SpawnStationService(call.client, stData, EVEDB::invTypes::FittingService);
    oSE->SpawnStationService(call.client, stData, EVEDB::invTypes::ReprocessingService);
    oSE->SpawnStationService(call.client, stData, EVEDB::invTypes::FactoryService);
    oSE->SpawnStationService(call.client, stData, EVEDB::invTypes::CloningService);
    oSE->SpawnStationService(call.client, stData, EVEDB::invTypes::RepairService);
    oSE->SpawnStationService(call.client, stData, EVEDB::invTypes::LaboratoryService);

    return nullptr;
}