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
    Update:     Allan
*/

#include "ClientSession.h"
#include "EntityList.h"
#include "EVEServerConfig.h"

#include <set>

/**
 * Whitelist of session variables that are sent through the wire to the client.
 * Every other key is server-internal (corpRole, cloneStationID, fleetjob, editor_*, ...) and is
 * still readable by the server through GetCurrent*() but never leaves it.
 *
 * Taken from the Crucible client's session classes:
 *   base.CoreSession.__persistvars__  (carbon/common/script/sys/sessions.py)
 *   base.Session.__persistvars__ and the 'locationid'/'corprole' nonpersistvars (script/common/script/sys/eveSessions.py)
 *
 * @author Almamu (original blacklist)
 */
static const std::set<std::string> CLIENTVARS = {
    // CoreSession
    "userid", "languageID", "role", "charid", "address", "userType", "maxSessionTime", "sessionType",
    // Session
    "regionid", "constellationid", "allianceid", "warfactionid", "corpid", "fleetid", "fleetrole", "fleetbooster",
    "wingid", "squadid", "shipid", "stationid", "stationid2", "worldspaceid", "solarsystemid", "solarsystemid2",
    "hqID", "baseID", "rolesAtAll", "rolesAtHQ", "rolesAtBase", "rolesAtOther", "genderID", "bloodlineID", "raceID",
    "corpAccountKey", "inDetention", "battleID",
    // client nonpersistvars the client accepts from the server
    "locationid", "corprole"
};


ClientSession::ClientSession()
: mSession(new PyDict()),
mDirty(false),
m_sessionID(0)
{
    /* default session values */
    mSession->SetItemString("role", new_tuple(PyStatic.NewNone(), new PyLong(Acct::Role::PLAYER | Acct::Role::NEWBIE), PyStatic.NewFalse()));
    mSession->SetItemString("userid", new_tuple(PyStatic.NewNone(), PyStatic.NewZero(), PyStatic.NewFalse()));
    mSession->SetItemString("address", new_tuple(PyStatic.NewNone(), new PyString("0.0.0.0"), PyStatic.NewFalse()));

    /*  session id is unique to each session.
     * is not saved or shared between chars
     */
    // random.getrandbits(63), unique among live sessions (assigned by RegisterSID)
    sEntityList.RegisterSID(m_sessionID);
}

ClientSession::~ClientSession()
{
    // do we clear session vars here, or let ~PyDict() do it?
    PyDecRef(mSession);
    sEntityList.RemoveSID(m_sessionID);
}

// note:  cannot destroy these Py* objects here.
void ClientSession::Clear(const char* name)
{
    _Set(name, PyStatic.NewNone());
}

void ClientSession::SetInt(const char* name, int32 value)
{
    _Set(name, new PyInt(value));
}

void ClientSession::SetLong(const char* name, int64 value)
{
    _Set(name, new PyLong(value));
}

void ClientSession::SetString(const char* name, const char* value)
{
    _Set(name, new PyString(value));
}

void ClientSession::SetFloat(const char* name, double value)
{
    _Set(name, new PyFloat(value));
}

int32 ClientSession::GetLastInt(const char* name) const
{
    return PyRep::IntegerValue(_GetLast(name));
}

int32 ClientSession::GetCurrentInt(const char* name) const
{
    return PyRep::IntegerValue(_GetCurrent(name));
}

int64 ClientSession::GetLastLong(const char* name) const
{
    return PyRep::IntegerValue(_GetLast(name));
}

int64 ClientSession::GetCurrentLong(const char* name) const
{
    return PyRep::IntegerValue(_GetCurrent(name));
}

std::string ClientSession::GetLastString(const char* name) const
{
    return PyRep::StringContent(_GetLast(name));
}

std::string ClientSession::GetCurrentString(const char* name) const
{
    return PyRep::StringContent(_GetCurrent(name));
}

double ClientSession::GetLastFloat(const char* name) const
{
    return PyRep::FloatValue(_GetLast(name));
}

double ClientSession::GetCurrentFloat(const char* name) const
{
    return PyRep::FloatValue(_GetCurrent(name));
}

void ClientSession::EncodeChanges(PyDict* into)
{
    if (!mDirty)
        return;

    for (auto cur : *mSession)
    {
        PyTuple* valueTuple = cur.second->AsTuple ();

        if (valueTuple->GetItem (2)->AsBool ()->value () == false)
            continue;

        // mark the value as not new
        valueTuple->SetItem (2, PyStatic.NewFalse());

        // add the value to the list if the client knows it
        if (CLIENTVARS.find (cur.first->AsString ()->content ()) != CLIENTVARS.end ()) {
            // the key and the values stay in mSession too
            PyRep* last(valueTuple->GetItem (0));
            PyRep* current(valueTuple->GetItem (1));
            PyIncRef(cur.first);
            PyIncRef(last);
            PyIncRef(current);
            into->SetItem (cur.first, new_tuple (last, current));
        }
    }

    mDirty = false;
}

void ClientSession::EncodeInitialState (PyDict* into)
{
    for (auto cur : *mSession)
    {
        PyTuple* valueTuple = cur.second->AsTuple ();

        valueTuple->SetItem(2, PyStatic.NewFalse());

        // add the value to the initial state only if the client knows it
        if (CLIENTVARS.find (cur.first->AsString ()->content ()) != CLIENTVARS.end ()) {
            // the key and the value stay in mSession too
            PyRep* current(valueTuple->GetItem (1));
            PyIncRef(cur.first);
            PyIncRef(current);
            into->SetItem (cur.first, current);
        }
    }

    // mark the session as not dirty
    mDirty = false;
}

bool ClientSession::HasValue (const char* name) const
{
    PyTuple* valueTuple = _GetValueTuple (name);

    if (valueTuple == nullptr)
        return false;

    PyRep* value = valueTuple->GetItem(1);

    return value != nullptr && value->IsNone() == false;
}

PyTuple* ClientSession::_GetValueTuple(const char* name) const
{
    PyRep* value(mSession->GetItemString(name));
    if (value == nullptr)
        return nullptr;
    return value->AsTuple();
}

PyRep* ClientSession::_GetLast(const char* name) const
{
    PyTuple* tuple(_GetValueTuple(name)); // copy c'tor
    if (tuple == nullptr) {
        _log(CLIENT__SESSION_NOTFOUND, "ClientSession::_GetLast - value not found with name '%s'", name);
        return nullptr;
    }
    return tuple->GetItem(0);
}

PyRep* ClientSession::_GetCurrent(const char* name) const
{
    PyTuple* tuple(_GetValueTuple(name)); // copy c'tor
    if (tuple == nullptr) {
        if (is_log_enabled(CLIENT__SESSION_NOTFOUND)) {
            _log(CLIENT__SESSION_NOTFOUND, "ClientSession::_GetCurrent - value not found with name '%s'", name);
            EvE::traceStack();
        }
        return nullptr;
    }
    return tuple->GetItem(1);
}

void ClientSession::_Set(const char* name, PyRep* value)
{
    PyTuple* tuple(_GetValueTuple(name)); // copy c'tor
    if (tuple == nullptr) {
        tuple = new_tuple(PyStatic.NewNone(), PyStatic.NewNone(), PyStatic.NewFalse());
        mSession->SetItemString(name, tuple);
    }

    PyRep* current(tuple->GetItem(1)); // copy c'tor
    if (value->hash() != current->hash()) {
        // current moves to slot 0; SetItem(1) below releases slot 1's reference to it
        PyIncRef(current);
        tuple->SetItem(0, current); /* didn't the session need to store the old value too? */
        tuple->SetItem(1, value);
        tuple->SetItem(2, PyStatic.NewTrue());
        mDirty = true;
    } else {
        PyDecRef(value);
    }
}
