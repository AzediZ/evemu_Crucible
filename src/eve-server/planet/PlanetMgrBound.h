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
    Updates:    Allan
*/

#ifndef EVEMU_PLANET_PLANETMGR_BOUND_H_
#define EVEMU_PLANET_PLANETMGR_BOUND_H_

#include "services/BoundService.h"
#include "planet/PlanetDB.h"
#include "planet/PlanetMgr.h"
#include "Client.h"

class PlanetMgrBound;

class PlanetMgrService : public BindableService <PlanetMgrService, PlanetMgrBound>
{
public:
    PlanetMgrService(EVEServiceManager& mgr);

    void BoundReleased (PlanetMgrBound* bound) override;
protected:
    BoundDispatcher* BindObject(Client *client, PyRep* bindParameters);

    PyResult GetPlanetsForChar(PyCallArgs& call);
    PyResult GetMyLaunchesDetails(PyCallArgs& call);
    PyResult GetPlanet(PyCallArgs& call, PyInt* planetID);
    PyResult DeleteLaunch(PyCallArgs& call, PyInt* launchID);

private:
    // (planetID, charID) - every character gets its own bound object (and colony) for a planet
    std::map<std::pair<uint32, uint32>, PlanetMgrBound*> m_instances;
};


class PlanetMgrBound : public EVEBoundObject <PlanetMgrBound>
{
public:
    PlanetMgrBound(EVEServiceManager& mgr, PlanetMgrService& parent, uint32 planetID, uint32 systemID, uint32 charID);

    uint32 GetPlanetID ()                               { return m_planetID; }
    std::pair<uint32, uint32> GetKey()                  { return std::make_pair(m_planetID, m_charID); }

protected:
    PyResult GetPlanetResourceInfo(PyCallArgs& call);
    PyResult GetPlanetInfo(PyCallArgs& call);
    PyResult GetExtractorsForPlanet(PyCallArgs& call, PyInt* planetID);
    PyResult UserUpdateNetwork(PyCallArgs& call, PyList* commandList);
    PyResult GetProgramResultInfo(PyCallArgs& call, PyRep* ecuID, PyInt* typeID, PyList* heads, PyRep* headRadius);
    PyResult GetResourceData(PyCallArgs& call, PyObject* info);
    PyResult UserAbandonPlanet(PyCallArgs& call);
    PyResult UserLaunchCommodities(PyCallArgs& call, PyInt* commandPinID, PyDict* commoditiesToLaunch);
    PyResult UserTransferCommodities(PyCallArgs& call, PyList* path, PyDict* commodities);
    PyResult GetCommandPinsForPlanet(PyCallArgs& call, PyInt* planetID);
    PyResult GetFullNetworkForOwner(PyCallArgs& call, PyInt* planetID, PyInt* characterID);
    PyResult GMAddCommodity(PyCallArgs& call, PyRep* pinID, PyInt* typeID, PyRep* quantity);
    PyResult GMConvertCommandCenter(PyCallArgs& call, PyRep* pinID);
    PyResult GMForceInstallProgram(PyCallArgs& call, PyRep* pinID, PyInt* typeID, PyRep* cycleTime, PyRep* lifetimeHours, PyRep* qtyPerCycle, PyRep* radius);
    PyResult GMGetLocalDistributionReport(PyCallArgs& call, PyInt* planetID, PyTuple* surfacePoint);
    PyResult GMGetSynchedServerState(PyCallArgs& call, PyInt* characterID);
    PyResult GMRunDepletionSim(PyCallArgs& call, PyRep* typeID, PyObject* info);
    PyResult GMGetCompleteResource(PyCallArgs& call, PyInt* typeID, PyRep* layer);
    PyResult GMCreateNuggetLayer(PyCallArgs& call, PyInt* planetID, PyInt* typeID);

    // the GM planet tools:  GML, GMH, WORLDMOD or PROGRAMMER
    static void CheckGM(Client* pClient);
    // tells the colony's owner to re-read it (planetSvc.OnMajorPlanetStateUpdate)
    void NotifyOwner(bool numCommandCentersChanged = false);

    /*

    data = planet.remoteHandler.GMGetCompleteResource(resourceTypeID, layer)
        sh = builder.CreateSHFromBuffer(data.data, data.numBands)

    self.planet.remoteHandler.GMCreateNuggetLayer(self.planetID, typeID)
        self.GMShowResource(typeID, 'nuggets')      {{ 'nuggets' = layer here }}

        */

protected:
    // the planet and colony are looked up on every call; the system may have been unloaded since the bind
    PlanetSE* GetPlanetSE();
    Colony* GetColony();
    // only the character that bound this object may use it
    void CheckCaller(Client* pClient);

    uint32 m_planetID;
    uint32 m_systemID;
    uint32 m_charID;
};
#endif  // EVEMU_PLANET_PLANETMGR_BOUND_H_