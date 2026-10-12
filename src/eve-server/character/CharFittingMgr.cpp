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
    Author:        Reve (outline only)
    Updates:    Allan
*/

// note:  these are the same calls as in corpFittingMgr


#include "eve-server.h"

#include "character/FittingDB.h"
#include "character/CharFittingMgr.h"

/* client fittingSvc: GetFittingMgr(ownerID) picks charFittingMgr for session.charid and corpFittingMgr for session.corpid.
 *  both services share FittingDB's implementation
 */

CharFittingMgr::CharFittingMgr() :
    Service("charFittingMgr")
{
    this->Add("GetFittings", &CharFittingMgr::GetFittings);
    this->Add("SaveFitting", &CharFittingMgr::SaveFitting);
    this->Add("SaveManyFittings", &CharFittingMgr::SaveManyFittings);
    this->Add("DeleteFitting", &CharFittingMgr::DeleteFitting);
    this->Add("UpdateNameAndDescription", &CharFittingMgr::UpdateNameAndDescription);
}

PyResult CharFittingMgr::GetFittings(PyCallArgs &call, PyInt* ownerID)
{
    // dict fittingID -> KeyVal(fittingID, ownerID, name, description, shipTypeID, fitData)
    return FittingDB::HandleGetFittings(call.client, ownerID->value(), false);
}

PyResult CharFittingMgr::SaveFitting(PyCallArgs &call, PyInt* ownerID, PyObject* fitting)
{
    // returns the new fittingID
    return FittingDB::HandleSaveFitting(call.client, ownerID->value(), false, fitting);
}

PyResult CharFittingMgr::SaveManyFittings(PyCallArgs &call, PyInt* ownerID, PyDict* fittingsToSave)
{
    // fittingsToSave is {tempFittingID(-1, -2, ..): KeyVal}; returns rows with .tempFittingID and .realFittingID
    return FittingDB::HandleSaveManyFittings(call.client, ownerID->value(), false, fittingsToSave);
}

PyResult CharFittingMgr::DeleteFitting(PyCallArgs &call, PyInt* ownerID, PyInt* fittingID)
{
    FittingDB::HandleDeleteFitting(call.client, ownerID->value(), false, fittingID->value());
    return nullptr;
}

PyResult CharFittingMgr::UpdateNameAndDescription(PyCallArgs &call, PyInt* fittingID, PyInt* ownerID, PyRep* name, PyRep* description)
{
    FittingDB::HandleUpdateNameAndDescription(call.client, ownerID->value(), false, fittingID->value(), name, description);
    return nullptr;
}
