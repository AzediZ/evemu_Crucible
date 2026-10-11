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
    Author:        Allan
*/


#ifndef EVEMU_PLANET_PLANETDB_H_
#define EVEMU_PLANET_PLANETDB_H_

#include "../eve-server.h"
#include "../POD_containers.h"
//#include "../DataClasses.h"

class PlanetDB
{
public:
    static PyRep* GetPlanetsForChar(uint32 charID);
    static PyRep* GetMyLaunchesDetails(uint32 charID);
    // planets the character has a colony on (Interplanetary Consolidation)
    static uint32 CountPlanetsForChar(uint32 charID);

    // must send data.launchID
    static void GetLaunchDetails(Launch::Data &data);
    static GPoint GetLaunchPos(uint32 launchID);
    static uint32 GetLaunchItemID(uint32 launchID);
    static uint32 GetLaunchPlanet(uint32 launchID);
    static void UpdateLaunchStatus(uint32 itemID, uint8 status);

    void GetPlanetData(DBQueryResult& res);
    void GetSchematicData(DBQueryResult& res);
    void GetSchematicTimes(DBQueryResult& res);
    void GetSchematicPins(DBQueryResult& res);
    void GetAttributeDefaults(DBQueryResult& res);
    void GetPITypeAttributes(DBQueryResult& res);

    /* colonies.  the colony is loaded and saved whole; ColonySim works on it in memory */
    // fills ccPinID, level, currentSimTime and the raw pins (with contents and heads), links and routes
    bool LoadColony(uint32 charID, uint32 planetID, PI_CCPin& ccPin);
    void SaveColony(PI_CCPin& ccPin, uint32 systemID, uint16 planetTypeID);
    void DeleteColony(uint32 ccPinID, uint32 planetID, uint32 charID);
    // a new routeID from piRoutes' auto increment
    uint32 InsertRoute(uint32 ccPinID, const PI_Route& route);

    static void GetExtractorsForPlanet(uint32 planetID, DBQueryResult& res);
    // pinID, typeID, ownerID, latitude, longitude of every command center on the planet
    static void GetCommandPinsForPlanet(uint32 planetID, DBQueryResult& res);
    // pinID, typeID, ownerID, latitude, longitude  and  endpoint1, endpoint2 of a character's colony on the planet
    static void GetNetworkForOwner(uint32 planetID, uint32 charID, DBQueryResult& pins, DBQueryResult& links);
    static uint32 GetLaunchOwner(uint32 itemID);

    void SaveLaunch(uint32 contID, uint32 charID, uint32 systemID, uint32 planetID, GPoint& pos);
    static void DeleteLaunch(uint32 contID);
};

#endif  // EVEMU_PLANET_PLANETDB_H_
