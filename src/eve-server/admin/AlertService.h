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
    Author:        Zhur, captnoord
*/

#ifndef _ALERT_SERVICE_H
#define _ALERT_SERVICE_H

#include "services/Service.h"
#include "python/PyTraceLog.h"


class AlertService : public Service<AlertService> {
public:
    AlertService();
    virtual ~AlertService();

protected:
    PyResult BeanCount(PyCallArgs &call, PyRep* stackIDHash);
    PyResult BeanDelivery(PyCallArgs &call, PyDict* beans);
    PyResult BeanDelivery(PyCallArgs &call, PyList* beans);
    PyResult GroupBeanDelivery(PyCallArgs &call, PyRep* compressedBeans);
    PyResult SendClientStackTraceAlert(PyCallArgs &call, PyTuple* stackId, PyRep* stackTrace, PyRep* mode, std::optional<PyRep*> nextErrorKeyHash);

    PyTraceLog* traceLogger;

private:
    // (errorID, 'B') for a known stack hash, (None, None) to ask the client for the trace
    PyTuple* LogMode(uint32 errorID);

    std::map<uint32, uint32> m_errorIDs;            // stack hash -> errorID
    std::map<uint32, std::string> m_errorFirstLine; // errorID -> first line of the trace
    uint32 m_nextErrorID;
};

#endif//_ALERT_SERVICE_H
