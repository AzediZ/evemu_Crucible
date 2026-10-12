/*
 *
 *
 * note:  these are the same calls as in charFittingMgr
 */


#include "eve-server.h"

#include "character/FittingDB.h"
#include "corporation/CorpFittingMgr.h"

/* client fittingSvc: GetFittingMgr(ownerID) picks charFittingMgr for session.charid and corpFittingMgr for session.corpid.
 *  both services share FittingDB's implementation; corp writes need the Fitting Manager role
 */

CorpFittingMgr::CorpFittingMgr() :
    Service("corpFittingMgr", eAccessLevel_Corporation)
{
    this->Add("GetFittings", &CorpFittingMgr::GetFittings);
    this->Add("SaveFitting", &CorpFittingMgr::SaveFitting);
    this->Add("SaveManyFittings", &CorpFittingMgr::SaveManyFittings);
    this->Add("DeleteFitting", &CorpFittingMgr::DeleteFitting);
    this->Add("UpdateNameAndDescription", &CorpFittingMgr::UpdateNameAndDescription);
}

PyResult CorpFittingMgr::GetFittings(PyCallArgs &call, PyInt* ownerID)
{
    // dict fittingID -> KeyVal(fittingID, ownerID, name, description, shipTypeID, fitData)
    return FittingDB::HandleGetFittings(call.client, ownerID->value(), true);
}

PyResult CorpFittingMgr::SaveFitting(PyCallArgs &call, PyInt* ownerID, PyObject* fitting)
{
    // returns the new fittingID
    return FittingDB::HandleSaveFitting(call.client, ownerID->value(), true, fitting);
}

PyResult CorpFittingMgr::SaveManyFittings(PyCallArgs &call, PyInt* ownerID, PyDict* fittingsToSave)
{
    // fittingsToSave is {tempFittingID(-1, -2, ..): KeyVal}; returns rows with .tempFittingID and .realFittingID
    return FittingDB::HandleSaveManyFittings(call.client, ownerID->value(), true, fittingsToSave);
}

PyResult CorpFittingMgr::DeleteFitting(PyCallArgs &call, PyInt* ownerID, PyInt* fittingID)
{
    FittingDB::HandleDeleteFitting(call.client, ownerID->value(), true, fittingID->value());
    return nullptr;
}

PyResult CorpFittingMgr::UpdateNameAndDescription(PyCallArgs &call, PyInt* fittingID, PyInt* ownerID, PyRep* name, PyRep* description)
{
    FittingDB::HandleUpdateNameAndDescription(call.client, ownerID->value(), true, fittingID->value(), name, description);
    return nullptr;
}
