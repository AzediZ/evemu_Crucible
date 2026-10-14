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
*/

#ifndef _INVENTORY_BOUND_H
#define _INVENTORY_BOUND_H


#include "services/BoundService.h"
#include "Client.h"

class InventoryBound : public EVEBoundObject <InventoryBound>
{
public:
    InventoryBound(EVEServiceManager &mgr, BoundServiceParent<InventoryBound>& parent, InventoryItemRef item, EVEItemFlags flag, uint32 ownerID,  bool passive);

    // GM menu 'take out trash':  deletes the listed items lying in hangarID's item hangar
    static void GMTakeOutTrash(Client* pClient, uint32 hangarID, PyList* itemIDs);

protected:
    PyResult GetItem(PyCallArgs& call);
    PyResult StripFitting(PyCallArgs& call);
    PyResult DestroyFitting(PyCallArgs& call, PyInt* itemID);
    PyResult StackAll(PyCallArgs& call, std::optional <PyInt*> flag);
    PyResult ImportExportWithPlanet(PyCallArgs& call, PyInt* spaceportPinID, PyDict* importData, PyDict* exportData, PyFloat* taxRate);
    PyResult RemoveChargeToHangar(PyCallArgs& call, PyTuple* chargeInfo, std::optional<PyRep*> quantity);
    PyResult RemoveChargeToCargo(PyCallArgs& call, PyTuple* chargeInfo, std::optional<PyRep*> quantity);
    PyResult MultiMerge(PyCallArgs& call, PyList* items, std::optional<PyRep*> sourceContainerID);
    PyResult Add(PyCallArgs& call, PyInt* itemID, PyInt* containerID);
    PyResult MultiAdd(PyCallArgs& call, PyList* itemIDs, PyInt* containerID);
    PyResult List(PyCallArgs& call, std::optional <PyInt*> listFlag);
    PyResult CreateBookmarkVouchers(PyCallArgs& call, PyList* bookmarkIDs, PyInt* flag, PyBool* isMove);
    PyResult TakeOutTrash(PyCallArgs& call, PyList* itemIDs);
    PyResult BreakPlasticWrap(PyCallArgs& call);
    // secure containers and audit log containers (ContainerSecurity)
    PyResult SetPassword(PyCallArgs& call, PyInt* which, PyRep* oldPassword, PyRep* newPassword);
    PyResult EnterPassword(PyCallArgs& call, PyInt* which, PyRep* password);
    PyResult HasExistingPasswordSet(PyCallArgs& call, PyInt* which);
    PyResult RetrievePassword(PyCallArgs& call, PyInt* which);
    PyResult LockItem(PyCallArgs& call, PyInt* itemID);
    PyResult UnlockItem(PyCallArgs& call, PyInt* itemID);
    PyResult ALSCConfigGet(PyCallArgs& call);
    PyResult ALSCConfigSet(PyCallArgs& call, PyInt* config);
    PyResult ALSCLogGet(PyCallArgs& call, PyRep* fromDate, PyRep* fromLogID, PyInt* forward);
    PyResult ListDroneBay(PyCallArgs& call);
    PyResult RunRefiningProcess(PyCallArgs& call);
    PyResult Build(PyCallArgs& call);
    // the three platforms Build() handles:  outpost construction, outpost upgrade (Foundation...), outpost improvement
    void BuildOutpost(Client* pClient, StructureSE* pPlatform, uint16 stationTypeID);
    void BuildOutpostUpgrade(Client* pClient, StructureSE* pPlatform, uint16 upgradeTypeID);
    // are the materials of typeID (invTypeMaterials) in the platform?  throws the first one missing
    void CheckPlatformMaterials(uint16 typeID);
    // uses the materials up, moves what is left to the pilot's hangar at stationID and removes the platform
    void UsePlatform(Client* pClient, StructureSE* pPlatform, uint16 typeID, uint32 stationID);

    bool m_passive;     // still not sure what this is for
    EVEItemFlags m_flag;

    uint32 m_itemID;
    uint32 m_ownerID;

    Inventory* pInventory;

    InventoryItemRef m_self;

    std::vector< int32 > CatSortItems(std::vector< InventoryItemRef >& itemVec);

    // the flag an item added to this inventory gets (audit log containers keep their contents locked or unlocked)
    EVEItemFlags ResolveAddFlag(uint16 toFlag) const;
    /* may pClient take iRef out of where it is now (to toLocationID/toFlag)?  courier packages, locked items, secure
     *  containers and corp hangar division roles.  throws the client's errors */
    void CheckSourceAccess(Client* pClient, InventoryItemRef iRef, uint32 toLocationID, EVEItemFlags toFlag);

    PyRep* MoveItems(Client* pClient, std::vector< int32 >& items, EVEItemFlags toFlag, int32 quantity, bool manyFlags, float capacity);
};

#endif//_INVENTORY_BOUND_H
