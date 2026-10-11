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
    Author:        Zhur, Captnoord
*/

#include "eve-server.h"

#include "EVEServerConfig.h"
#include "admin/AlertService.h"
#include "Client.h"

AlertService::AlertService() :
    Service("alert"),
    traceLogger(nullptr),
    m_nextErrorID(1)
{
    this->Add("BeanCount", &AlertService::BeanCount);
    this->Add("BeanDelivery", static_cast<PyResult (AlertService::*)(PyCallArgs &, PyDict*)>(&AlertService::BeanDelivery));
    this->Add("BeanDelivery", static_cast<PyResult (AlertService::*)(PyCallArgs &, PyList*)>(&AlertService::BeanDelivery));
    this->Add("GroupBeanDelivery", &AlertService::GroupBeanDelivery);
    this->Add("SendClientStackTraceAlert", &AlertService::SendClientStackTraceAlert);

    if (sConfig.debug.StackTrace or is_log_enabled(CLIENT__STACK_TRACE))
        traceLogger = new PyTraceLog("evemu_client_stack_trace.txt", true, true);
}

AlertService::~AlertService()
{
    SafeDelete(traceLogger);
}

PyTuple* AlertService::LogMode(uint32 errorID)
{
    PyTuple* result = new PyTuple(2);
    if (errorID == 0) {
        result->SetItem(0, PyStatic.NewNone());
        result->SetItem(1, PyStatic.NewNone());
    } else {
        result->SetItem(0, new PyInt(errorID));
        result->SetItem(1, new PyString("B"));      // LOGGING_BATCHED:  later repeats only come back as counts
    }
    return result;
}

/** A python error occurred in the client, and it asks how to handle it (alert.py __SendStackTraceAlert).
  *  returns (errorID, logMode).  errorID None asks the client to send the trace (SendClientStackTraceAlert);
  *  a known error gets its errorID and 'B' (batched), so repeats are counted and sent with BeanDelivery.
  *  each distinct error (stack hash) is therefore sent once per server run.
  */
PyResult AlertService::BeanCount(PyCallArgs &call, PyRep* stackIDHash) {
    uint32 hash = (uint32)PyRep::IntegerValue(stackIDHash);
    std::map<uint32, uint32>::iterator itr = m_errorIDs.find(hash);
    return LogMode(itr == m_errorIDs.end() ? 0 : itr->second);
}

/** The client counts repeats of known errors and sends them every BEAN_DELEVERY_TIME minutes:
  *  {errorID: (count, lastFileTime)}
  */
PyResult AlertService::BeanDelivery(PyCallArgs& call, PyDict* beans)
{
    for (PyDict::const_iterator itr = beans->begin(); itr != beans->end(); ++itr) {
        uint32 errorID = (uint32)PyRep::IntegerValue(itr->first);
        uint32 count(0);
        int64 lastTime(0);
        if (itr->second->IsTuple()) {
            PyTuple* tuple = itr->second->AsTuple();
            if (tuple->size() > 0)
                count = (uint32)PyRep::IntegerValue(tuple->GetItem(0));
            if (tuple->size() > 1)
                lastTime = PyRep::IntegerValue(tuple->GetItem(1));
        } else {
            count = (uint32)PyRep::IntegerValue(itr->second);
        }
        std::map<uint32, std::string>::iterator lItr = m_errorFirstLine.find(errorID);
        _log(CLIENT__WARNING, "%s: error #%u repeated %u times (last %s): %s", call.client->GetName(), errorID, count,
                (lastTime > 0 ? Win32TimeToString(lastTime).c_str() : "?"),
                (lItr == m_errorFirstLine.end() ? "<unknown error>" : lItr->second.c_str()));
    }
    return PyStatic.NewNone();
}

PyResult AlertService::BeanDelivery(PyCallArgs& call, PyList* beans)
{
    // not sent by the Crucible client.  kept in case an older form arrives
    _log(CLIENT__WARNING, "%s: BeanDelivery with %lu entries (list form)", call.client->GetName(), beans->size());
    return PyStatic.NewNone();
}

PyResult AlertService::GroupBeanDelivery(PyCallArgs& call, PyRep* compressedBeans)
{
    // zlib'd yaml of per-user details for 'D' (details) errors.  we only hand out 'B', so this is normally empty
    size_t size(0);
    if (compressedBeans->IsString())
        size = compressedBeans->AsString()->content().size();
    else if (compressedBeans->IsBuffer())
        size = compressedBeans->AsBuffer()->size();
    else if (compressedBeans->IsDict())
        size = compressedBeans->AsDict()->size();
    _log(CLIENT__TRACE, "%s: GroupBeanDelivery %lu bytes", call.client->GetName(), size);
    return PyStatic.NewNone();
}

/**
 * @brief The client sends us a python stack trace, after BeanCount returned no errorID for it.
 *
 *  stackId = (adler32 hash, text).  the return is stored as stacktraceLogMode[hash] and unpacked as (errorID, logMode)
 */
PyResult AlertService::SendClientStackTraceAlert(PyCallArgs &call, PyTuple* stackId, PyRep* stackTrace, PyRep* mode, std::optional<PyRep*> nextErrorKeyHash) {
    uint32 hash(0);
    std::string text;
    if (stackId->size() > 0)
        hash = (uint32)PyRep::IntegerValue(stackId->GetItem(0));
    if (stackId->size() > 1)
        text = PyRep::StringContent(stackId->GetItem(1));
    if (text.empty())
        text = PyRep::StringContent(stackTrace);

    uint32 errorID(0);
    std::map<uint32, uint32>::iterator itr = m_errorIDs.find(hash);
    if (itr == m_errorIDs.end()) {
        errorID = m_nextErrorID++;
        m_errorIDs[hash] = errorID;

        // first non-empty line identifies the error in the repeat logs
        std::string line;
        size_t pos(0);
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos)
                end = text.size();
            line = text.substr(pos, end - pos);
            while (!line.empty() and ((line.back() == '\r') or (line.back() == ' ')))
                line.pop_back();
            if (!line.empty())
                break;
            pos = end + 1;
        }
        m_errorFirstLine[errorID] = line;
    } else {
        errorID = itr->second;
    }

    _log(CLIENT__ERROR, "Client %s(%u) python error #%u: %s", call.client->GetName(), call.client->GetCharacterID(),
            errorID, m_errorFirstLine[errorID].c_str());

    if (traceLogger != nullptr)
        traceLogger->logTrace(*call.tuple);

    return LogMode(errorID);
}
