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
*/


#ifndef __BEYONCE_SERVICE_H_INCL__
#define __BEYONCE_SERVICE_H_INCL__

#include "ship/ShipDB.h"
#include "services/BoundService.h"
#include "system/BookmarkService.h"

class BeyonceBound;

class BeyonceService : public BindableService <BeyonceService, BeyonceBound> {
public:
    BeyonceService(EVEServiceManager& mgr);

    void BoundReleased (BeyonceBound* bound) override;

protected:
    ShipDB m_db;

    PyResult GetFormations(PyCallArgs& call);

    //overloaded in order to support bound objects:
    BoundDispatcher *BindObject(Client *client, PyRep* bindParameters) override;
};


class BeyonceBound : public EVEBoundObject <BeyonceBound>
{
public:
    BeyonceBound(EVEServiceManager& mgr, BeyonceService& parent, Client* client);

protected:
    // throws CantWarpWhileCloaked unless the active cloak is a covert ops cloak
    void CheckCloakedWarp(Client* pClient);
    // false when a cloaking device that isn't warp safe is running
    static bool CanWarpCloaked(Client* pClient);
    // true (after telling the pilot) during the doomsday's warp lockout
    bool CheckWarpLockout(Client* pClient);

    /* CmdWarpToStuff helpers */
    // finds what to warp to: a fixed point (bookmarks) or an entity.  false (after telling the client when needed) when there's nothing to warp to
    bool ResolveWarpTarget(Client* pClient, const std::string& type, PyRep* id, GPoint& point, SystemEntity*& pSE, uint32& toID);
    /* the warp-in point of pSE (or point when pSE is null) for a ship at shipPos.
     *  distance is the requested range on entry and the range to use on return (some objects add to it) */
    static GPoint GetWarpInPoint(SystemEntity* pSE, uint32 toID, const GPoint& point, const GPoint& shipPos, int32& distance);
    /* commander warp: the commander's subordinates in the system (or on grid, world.fleetWarpOnGridOnly) that can warp now
     *  (excludeCharID is never warped).  speed is the warp speed for the whole group (0 = each ship's own speed) */
    void GetFleetWarpMembers(Client* pCmdr, uint32 excludeCharID, std::vector<Client*>& members, int32& speed);
    void FleetWarp(Client* pCmdr, const std::vector<Client*>& members, const GPoint& point, SystemEntity* pSE, uint32 toID, int32 minRange, int32 speed);
    // capital jump drive to a fleet member's cyno or an alliance cynosural generator array (CmdBeaconJumpFleet/Alliance)
    PyResult DoBeaconJump(Client* pClient, bool alliance, uint32 charID, uint32 beaconID, uint32 systemID);
    // opens the fitted jump portal generator to a fleet member's cyno or an alliance generator array (CmdBridgeToMember/Structure)
    PyResult DoOpenBridge(Client* pClient, bool alliance, uint32 charID, uint32 beaconID, uint32 systemID);
    // jump through another ship's open portal (CmdJumpThroughFleet/Alliance).  the bridging ship pays the fuel
    PyResult DoBridgeJump(Client* pClient, bool alliance, uint32 otherCharID, uint32 otherShipID, uint32 beaconID, uint32 systemID);

    PyResult CmdFollowBall(PyCallArgs& call, PyInt* ballID, PyRep* distance);
    PyResult CmdSetSpeedFraction(PyCallArgs& call, PyFloat* speedFraction);
    PyResult CmdAlignTo(PyCallArgs& call, PyInt* entityID);
    PyResult CmdGotoDirection(PyCallArgs& call, PyFloat* x, PyFloat* y, PyFloat* z);
    PyResult CmdGotoBookmark(PyCallArgs& call, PyInt* bookmarkID);
    PyResult CmdOrbit(PyCallArgs& call, PyInt* entityID, PyRep* rangeValue);
    PyResult CmdWarpToStuff(PyCallArgs& call, PyString* type, PyRep* id);
    PyResult CmdWarpToStuffAutopilot(PyCallArgs& call, PyInt* destID);
    PyResult CmdStop(PyCallArgs& call);
    PyResult CmdDock(PyCallArgs& call, PyInt* celestialID, PyInt* shipID, std::optional<PyRep*> paymentRequired);
    PyResult CmdTurboDock(PyCallArgs& call, PyInt* stationID, std::optional<PyRep*> paymentRequired);
    PyResult CmdStargateJump(PyCallArgs& call, PyInt* fromStargateID, PyInt* toStargateID, PyInt* shipID);
    PyResult CmdAbandonLoot(PyCallArgs& call, PyList* wreckIDs);
    PyResult UpdateStateRequest(PyCallArgs& call);
    PyResult CmdJumpThroughFleet(PyCallArgs& call, PyInt* otherCharID, PyInt* otherShipID, PyInt* beaconID, PyInt* solarSystemID);
    PyResult CmdJumpThroughAlliance(PyCallArgs& call, PyInt* otherShipID, PyInt* beaconID, PyInt* solarSystemID);
    PyResult CmdJumpThroughCorporationStructure(PyCallArgs& call, PyInt* itemID, PyInt* remoteStructureID, PyInt* remoteSystemID);
    PyResult CmdBeaconJumpFleet(PyCallArgs& call, PyInt* characterID, PyInt* beaconID, PyInt* solarSystemID);
    PyResult CmdBeaconJumpAlliance(PyCallArgs& call, PyInt* beaconID, PyInt* solarSystemID);
    PyResult CmdBridgeToMember(PyCallArgs& call, PyInt* charID, PyInt* beaconID, PyInt* solarSystemID);
    PyResult CmdBridgeToStructure(PyCallArgs& call, PyInt* beaconID, PyInt* solarSystemID);
    PyResult CmdFleetRegroup(PyCallArgs& call);
    PyResult CmdFleetTagTarget(PyCallArgs& call, PyInt* itemID, PyString* tag);
    
private:
    BookmarkService* m_bookmark;
};

#endif
