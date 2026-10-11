/*
 *    ------------------------------------------------------------------------------------
 *    LICENSE:
 *    ------------------------------------------------------------------------------------
 *    This file is part of EVEmu: EVE Online Server Emulator
 *    Copyright 2006 - 2021 The EVEmu Team
 *    For the latest information visit https://evemu.dev
 *    ------------------------------------------------------------------------------------
 *    This program is free software; you can redistribute it and/or modify it under
 *    the terms of the GNU Lesser General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option) any later
 *    version.
 *
 *    This program is distributed in the hope that it will be useful, but WITHOUT
 *    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License along with
 *    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 *    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
 *    http://www.gnu.org/copyleft/lesser.txt.
 *    ------------------------------------------------------------------------------------
 *    Author:        Cometo
 *    Updates:  Allan
 *    Rewrite:  F19 - the colony runs the client's own simulation (planet/ColonySim)
 */

#ifndef __COLONY_H_INCL__
#define __COLONY_H_INCL__


#include "planet/PlanetDB.h"
#include "planet/ColonySim.h"
#include "services/ServiceManager.h"

class Client;
class PlanetSE;
class CustomsSE;
class SystemManager;

/* one character's colony on one planet.
 *  the colony is simulated lazily:  every access runs the simulation from the last simulated time to now (SimulateToNow).
 *  nothing runs on a timer, so a colony is always current when it is looked at, whether its owner is online or not.
 */
class Colony {
public:
    // a colony belongs to a character, not to a session.  calls that need the acting client take it as a parameter.
    Colony(EVEServiceManager& mgr, uint32 ownerID, PlanetSE* pSE);
    ~Colony();

    void Init();        // loads the colony
    void Save();
    // runs the client's simulation from the colony's current sim time to now, then saves
    void SimulateToNow();

    bool HasColony()                                    { return (m_colony.ccPinID != 0); }
    uint32 GetOwner()                                   { return m_ownerID; }
    uint8 GetLevel()                                    { return m_colony.level; }
    int64 GetSimTime()                                  { return m_colony.currentSimTime; }
    PlanetSE* GetPlanetSE()                             { return m_pSE; }
    const PI_CCPin& GetData()                           { return m_colony; }

    double GetPlanetRadius();
    uint16 GetPlanetTypeID();
    // the resource a program extracts, as the client's harmonic would show it
    ColonySim::ResourceField GetResourceField(uint16 typeID);

    // BaseColony.Serialize:  KeyVal(level, pins, links, routes, currentSimTime)
    PyRep* GetColony();
    // adds the colony keys (level, pins, links, routes, currentSimTime) to the planet info dict
    void SerializeInto(PyDict* args);

    // replaces this colony with an edited copy (UserUpdateNetwork) and saves it
    void Commit(const PI_CCPin& work);

    void AbandonColony(Client* pClient);
    PyRep* LaunchCommodities(Client* pClient, uint32 pinID, std::map< uint16, uint32 >& items);
    PyRep* TransferCommodities(Client* pClient, const std::vector<uint32>& path, std::map< uint16, uint32 >& items);
    void PlanetXfer(Client* pClient, CustomsSE* pCO, uint32 spaceportID, std::map< uint32, uint32 > importItems, std::map< uint16, uint32 > exportItems, double taxRate);
    // GetProgramResultInfo(pinID, typeID, heads, headRadius) -> (qtyToDistribute, cycleTime, numCycles).  does not change the colony
    PyRep* GetProgramResultInfo(PyRep* pinID, uint16 typeID, PyList* heads, double headRadius);

    /* GM tools */
    // puts commodities into a pin (what it can take) and routes them on.  returns the quantity added
    uint32 GMAddCommodity(uint32 pinID, uint16 typeID, uint32 quantity);
    // a program with the given output, cycle and lifetime, whatever the resource map says
    void GMForceInstallProgram(uint32 pinID, uint16 typeID, int64 cycleTime, int64 lifetime, uint32 qtyPerCycle, double headRadius);

    // where planetary construction, upgrade and launch taxes go:  CONCORD in high security, Interbus elsewhere
    uint32 GetNPCOwner();

private:
    // fills the derived pin data (kind, schematic, capacity) after a load
    void PrepareLoadedData();
    void SerializePins(PyList* list);
    void SerializeLinks(PyList* list);
    void SerializeRoutes(PyList* list);

    EVEServiceManager& m_svcMgr;
    PlanetSE* m_pSE;
    uint32 m_ownerID;

    PI_CCPin m_colony;

    PlanetDB m_db;
};

#endif
