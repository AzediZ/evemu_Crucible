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
    Rewrite:    Allan
*/

#include "system/CrimeWatch.h"
#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "StaticDataMgr.h"
#include "fleet/FleetService.h"
#include "ship/JumpDrive.h"
#include "cache/ObjCacheService.h"
#include "planet/PlanetDB.h"
#include "system/SystemDB.h"
#include "planet/Moon.h"
#include "planet/CustomsOffice.h"
#include "pos/PosMgrDB.h"
#include "pos/Structure.h"
#include "pos/JumpBridge.h"
#include "pos/Tower.h"
#include "ship/BeyonceService.h"
#include "ship/modules/CloakModule.h"
#include "ship/modules/JumpPortalModule.h"
#include "ship/modules/ModuleManager.h"
#include "station/StationDataMgr.h"
#include "system/BookmarkService.h"
#include "system/Container.h"
#include "system/DestinyManager.h"
#include "system/WarpInPoint.h"
#include "system/Interdiction.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/DungeonMgr.h"
#include "system/cosmicMgrs/ManagerDB.h"

BeyonceService::BeyonceService(EVEServiceManager& mgr)
: BindableService("beyonce", mgr, eAccessLevel_SolarSystem2)
{
    this->Add("GetFormations", &BeyonceService::GetFormations);
}

BoundDispatcher* BeyonceService::BindObject(Client* client, PyRep* bindParameters)
{
    return new BeyonceBound(this->GetServiceManager(), *this, client);
}

void BeyonceService::BoundReleased (BeyonceBound* bound) {

}

PyResult BeyonceService::GetFormations(PyCallArgs &call) {
    // this is only called when player enters new system and calls to bind new beyonce
    if (!call.client->IsSetStateSent())
        call.client->CheckBallparkTimer();

    PyTuple* res = new PyTuple( 2 );
        Beyonce_Formation f;
            //Diamond formation
            f.name = "Diamond";
            f.pos1.x = 100;
            f.pos2.y = 100;
            f.pos3.x = -100;
            f.pos4.y = -100;
        res->SetItem( 0, f.Encode() );
            //Arrow formation
            f.name = "Arrow";
            f.pos1.x = 100;
            f.pos1.z = -50;
            f.pos2.x = 50;
            f.pos2.y = 0;
            f.pos3.x = -100;
            f.pos3.z = -50;
            f.pos4.x = -50;
            f.pos4.y = 0;
        res->SetItem( 1, f.Encode() );
    return res;
}

BeyonceBound::BeyonceBound(EVEServiceManager& mgr, BeyonceService& parent, Client* client) :
    EVEBoundObject(mgr, parent)
{
    this->Add("CmdFollowBall", &BeyonceBound::CmdFollowBall);   //*
    this->Add("CmdOrbit", &BeyonceBound::CmdOrbit);   //*
    this->Add("CmdAlignTo", &BeyonceBound::CmdAlignTo);
    this->Add("CmdGotoDirection", &BeyonceBound::CmdGotoDirection);   //*
    this->Add("CmdGotoBookmark", &BeyonceBound::CmdGotoBookmark);
    this->Add("CmdSetSpeedFraction", &BeyonceBound::CmdSetSpeedFraction);
    this->Add("CmdStop", &BeyonceBound::CmdStop);
    this->Add("CmdWarpToStuff", &BeyonceBound::CmdWarpToStuff);
    this->Add("CmdDock", &BeyonceBound::CmdDock);
    this->Add("CmdTurboDock", &BeyonceBound::CmdTurboDock);
    this->Add("CmdStargateJump", &BeyonceBound::CmdStargateJump);
    this->Add("UpdateStateRequest", &BeyonceBound::UpdateStateRequest);
    this->Add("CmdWarpToStuffAutopilot", &BeyonceBound::CmdWarpToStuffAutopilot); //*
    this->Add("CmdAbandonLoot", &BeyonceBound::CmdAbandonLoot);
    this->Add("CmdChangeStructureOwner", &BeyonceBound::CmdChangeStructureOwner);

    this->Add("CmdFleetRegroup", &BeyonceBound::CmdFleetRegroup);
    this->Add("CmdFleetTagTarget", &BeyonceBound::CmdFleetTagTarget);
    this->Add("CmdBeaconJumpFleet", &BeyonceBound::CmdBeaconJumpFleet);
    this->Add("CmdBeaconJumpAlliance", &BeyonceBound::CmdBeaconJumpAlliance);
    this->Add("CmdJumpThroughFleet", &BeyonceBound::CmdJumpThroughFleet);
    this->Add("CmdJumpThroughAlliance", &BeyonceBound::CmdJumpThroughAlliance);
    this->Add("CmdBridgeToMember", &BeyonceBound::CmdBridgeToMember);
    this->Add("CmdBridgeToStructure", &BeyonceBound::CmdBridgeToStructure);
    this->Add("CmdJumpThroughCorporationStructure", &BeyonceBound::CmdJumpThroughCorporationStructure);

    client->SetBeyonce(true);
    if (client->IsLogin() and !client->IsSetStateSent())
        client->SetBallPark();

    this->m_bookmark = this->GetServiceManager().Lookup <BookmarkService>("bookmark");
}

PyResult BeyonceBound::CmdFollowBall(PyCallArgs &call, PyInt* ballID, PyRep* distance) {
    _log(AUTOPILOT__MESSAGE, "%s called Follow. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));

    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    }
    SystemManager* pSystem = call.client->SystemMgr();
    if (pSystem == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no system manager!", call.client->GetName());
        return PyStatic.NewNone();
    }
    SystemEntity* pSE = pSystem->GetSE(ballID->value());
    if (pSE == nullptr) {
        _log(CLIENT__ERROR, "%s: Unable to find entity %u to Follow/Approach.", call.client->GetName(), ballID->value());
        return PyStatic.NewNone();
    }

    call.client->SetInvul(false);
    call.client->SetUndock(false);

    pDestiny->Follow(pSE, PyRep::IntegerValue(distance));

    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdSetSpeedFraction(PyCallArgs &call, PyFloat* speedFraction) {
    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    }

    // this sets speed ONLY.  it doesn't start a stopped ship moving (see DestinyManager::SetSpeedFraction)
    // client should not legally send anything < 0.1 (except on rare occasion a 0.0 instead of Stop.)
    if ((speedFraction->value() != 0) && (speedFraction->value() < 0.1))
        return PyStatic.NewNone();

    //sLog.Warning( "BeyonceBound", "Handle_CmdSetSpeedFraction %.2f", arg.arg );
    if (!call.client->IsUndock())
        pDestiny->SetSpeedFraction(speedFraction->value());

    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdAlignTo(PyCallArgs &call, PyInt* entityID) {
    _log(AUTOPILOT__MESSAGE, "%s called Align. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));
    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }
    SystemManager* pSystem = call.client->SystemMgr();
    if (pSystem == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no system manager!", call.client->GetName());
        return PyStatic.NewNone();
    }

    SystemEntity* pEntity = pSystem->GetSE(entityID->value());
    if (pEntity == nullptr) {
        _log(CLIENT__ERROR, "%s: Unable to find entity %u to AlignTo.", call.client->GetName(), entityID->value());
        return PyStatic.NewNone();
    }

    call.client->SetInvul(false);
    call.client->SetUndock(false);

    pDestiny->AlignTo( pEntity );

    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdGotoDirection(PyCallArgs &call, PyFloat* x, PyFloat* y, PyFloat* z) {
    _log(AUTOPILOT__MESSAGE, "%s called GotoDirection. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));
    //call.client->SetAutoPilot(false);

    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    } else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }

    call.client->SetInvul(false);
    call.client->SetUndock(false);

    const GPoint dir = GPoint(x->value(), y->value(), z->value());
    pDestiny->GotoDirection(dir);

    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdGotoBookmark(PyCallArgs &call, PyInt* bookmarkID) {
    _log(AUTOPILOT__MESSAGE, "%s called GotoBookmark. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));
    //call.client->SetAutoPilot(false);

    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }

    double x(0.0), y(0.0), z(0.0);
    uint16 typeID(0);
    uint32 itemID(0), locationID(0);

    if (this->m_bookmark == nullptr) {
        sLog.Error( "BeyonceService::Handle_GotoBookmark()", "Attempt to access BookmarkService via (BookmarkService*)(call.client->services().LookupService(\"bookmark\")) returned NULL." );
        return PyStatic.NewNone();
    } else {
        this->m_bookmark->LookupBookmark(bookmarkID->value(), itemID, typeID, locationID, x, y, z);

        if (typeID == 5) {
            if (call.client->GetSystemID() != locationID) {
                //  this bm is for different system.  make and send error here.
                return PyStatic.NewNone();
            }

            GPoint point(x, y, z);
            pDestiny->GotoPoint(point);
        } else {
            // Bookmark type is of a static system entity, so search for it and obtain its coordinates:
            SystemEntity* pSE = call.client->SystemMgr()->GetSE(itemID);
            if (pSE == nullptr) {
                sLog.Error( "BeyonceService::Handle_GotoBookmark()", "%s: unable to find location %d", call.client->GetName(), itemID );
                return PyStatic.NewNone();
            }

            pDestiny->GotoPoint( pSE->GetPosition() );
        }
    }

    call.client->SetInvul(false);
    call.client->SetUndock(false);

    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdOrbit(PyCallArgs &call, PyInt* entityID, PyRep* rangeValue) {
    _log(AUTOPILOT__MESSAGE, "%s called Orbit. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));
    call.client->SetAutoPilot(false);

    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }

    SystemManager* pSystem = call.client->SystemMgr();
    if (pSystem == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no system manager!", call.client->GetName());
        return PyStatic.NewNone();
    }

    double range =
        rangeValue->IsInt()
        ? rangeValue->AsInt()->value()
        : rangeValue->AsFloat()->value();

    SystemEntity* pEntity = pSystem->GetSE(entityID->value());
    if (pEntity == nullptr) {
        _log(CLIENT__ERROR, "%s: Unable to find entity %u to Orbit.", call.client->GetName(), entityID->value());
        return PyStatic.NewNone();
    }

    call.client->SetInvul(false);
    call.client->SetUndock(false);

    pDestiny->Orbit(pEntity, range);

    return PyStatic.NewNone();
}

// only the covert ops cloak can warp
bool BeyonceBound::CanWarpCloaked(Client* pClient)
{
    ShipSE* pShip(pClient->GetShipSE());
    if ((pShip == nullptr) or (pShip->DestinyMgr() == nullptr) or !pShip->DestinyMgr()->IsModuleCloaked())
        return true;
    GenericModule* pMod(pClient->GetShip()->GetModuleManager()->GetActiveModuleByGroup(EVEDB::invGroups::Cloaking_Device));
    return ((pMod != nullptr) and pMod->IsCloakModule() and pMod->GetCloakModule()->IsWarpSafeCloak());
}

void BeyonceBound::CheckCloakedWarp(Client* pClient)
{
    if (!CanWarpCloaked(pClient))
        throw UserError ("CantWarpWhileCloaked");
}

bool BeyonceBound::CheckWarpLockout(Client* pClient)
{
    // a titan can't warp for 30s after firing its doomsday
    int64 remaining(pClient->GetShipSE()->WarpLockoutRemaining());
    if (remaining < 1)
        return false;
    pClient->SendNotifyMsg("Your ship cannot warp for another %u seconds after firing its super weapon.", (uint32)(remaining / EvE::Time::Second) + 1);
    return true;
}

bool BeyonceBound::ResolveWarpTarget(Client* pClient, const std::string& type, PyRep* id, GPoint& point, SystemEntity*& pSE, uint32& toID)
{
    SystemManager* pSystem = pClient->SystemMgr();
    std::string stringArg = "";
    if ((id->IsString())
    or  (id->IsWString())) {
        stringArg = PyRep::StringContent(id);
    } else {
        toID = PyRep::IntegerValueU32(id);
    }

    if (type == "item" ) {
        pSE = pSystem->GetSE(toID);
        if (pSE == nullptr) {
            codelog(CLIENT__ERROR, "%s: unable to find item location %u in %s(%u)", pClient->GetName(), toID, pSystem->GetName(), pSystem->GetID());
            return false;
        }
    } else if (type == "bookmark" ) {
        double x(0.0), y(0.0), z(0.0);
        uint16 typeID(0);
        uint32 locationID(0);
        uint32 bookmarkID(toID);

        if (this->m_bookmark == nullptr) {
            sLog.Error( "BeyonceService::Handle_WarpToStuff()", "Attempt to access BookmarkService returned NULL." );
            return false;
        }
        toID = 0;
        this->m_bookmark->LookupBookmark(bookmarkID, toID, typeID, locationID, x, y, z);

        if ( typeID == 5 ) {
            if (pClient->GetSystemID() != locationID) {
                //  this bm is for different system.  make error here.
                return false;
            }
            point.x = x;
            point.y = y;
            point.z = z;
        } else {
            // Bookmark type is of a static system entity, so search for it and obtain its coordinates:
            pSE = pSystem->GetSE( toID );
            if (pSE == nullptr) {
                codelog(CLIENT__ERROR, "%s: unable to find bookmark location %u in %s(%u)", pClient->GetName(), toID, pSystem->GetName(), pSystem->GetID());
                return false;
            }
        }
    } else if (type == "scan") {
        uint32 anomID = pSystem->GetAnomMgr()->GetAnomalyID(stringArg);
        pSE = pSystem->GetSE(anomID);
        toID = anomID;
    } else if (type == "launch") {
        //warpToPoint = PlanetDB::GetLaunchPos(toID);
        pSE = pSystem->GetSE(toID);
        // launchpickup - launch, launchid
    } else if (type == "char") {
        /* warp to a fleet member (menusvc WarpToMember / WarpFleetToMember):  the member must be in our fleet, in space in this system.
         *  the warp ends at the member's ship (cloaked members too)
         */
        Client* pTarget(sEntityList.FindClientByCharID(toID));
        if ((pTarget == nullptr) or !pClient->InFleet() or (pTarget->GetFleetID() != pClient->GetFleetID())
        or !pTarget->IsInSpace() or (pTarget->GetSystemID() != pClient->GetSystemID()) or (pTarget->GetShipSE() == nullptr))
            throw UserError("FleetNotAMember");
        if (pTarget == pClient)
            return false;
        pSE = pTarget->GetShipSE();
        toID = pSE->GetID();
    }
    else if (type == "epinstance") {
        // escalation sites (journal, locationInfoSvc, transmissionMgr): the instance's warp-in point
        if (!pSystem->GetDungMgr()->GetInstanceWarpIn(toID, pClient->GetCharacterID(), point))
            throw CustomError ("That site is not in this system.");
    } else if (type == "tutorial") {
        // tutorialsvc.WarpToBallpark:  the tutorial's site (a dust cloud, which is what the tutorial looks for) in a rookie system
        if (!WarpIn::IsNewbieSystem(pSystem->GetID()))
            throw UserError ("TutYouAreNotInANewbieSystem");
        pSE = pSystem->GetTutorialSite();
        if (pSE == nullptr)
            throw CustomError ("The tutorial site could not be found.");
        toID = pSE->GetID();
    } else {
        sLog.Error( "BeyonceService::Handle_WarpToStuff()", "Unexpected type value: '%s'.", type.c_str() );
        return false;
    }

    if ((pSE == nullptr) and point.isZero()) {
        // point is zero ....make error and return
        codelog(CLIENT__ERROR, "%s: warpToPoint.isZero() = true.  Cannot find location %u for '%s'", pClient->GetName(), toID, type.c_str());
        pClient->SendErrorMsg("WarpTo: Item location not found.");
        return false;
    }

    // the rooms of a deadspace complex past its first are only reached through its acceleration gates:
    //  a warp to anything in there ends at the complex's entrance
    GPoint entrance;
    if (pSystem->GetDungMgr()->RedirectWarp((pSE != nullptr) ? pSE->GetPosition() : point, entrance)) {
        pSE = nullptr;
        toID = 0;
        point = entrance;
    }
    return true;
}

GPoint BeyonceBound::GetWarpInPoint(SystemEntity* pSE, uint32 toID, const GPoint& point, const GPoint& shipPos, int32& distance)
{
    // a bookmark (or other fixed point)
    if (pSE == nullptr)
        return point;

    double radius(pSE->GetRadius());
    // a moon with a tower: 20km inside the edge of the tower's bubble (server convenience; the tower is the destination)
    if (pSE->IsMoonSE() and pSE->GetMoonSE()->HasTower()) {
        GPoint warpToPoint(pSE->GetMoonSE()->GetMyTower()->GetPosition());
        GVector vectorFromOrigin(shipPos, warpToPoint);
        vectorFromOrigin.normalize();   //we now have a direction
        GPoint stopPoint = (vectorFromOrigin * (BUBBLE_RADIUS_METERS - 20000));  // 20km inside bubble.
        warpToPoint -= stopPoint;
        distance = 0;
        return warpToPoint;
    }

    if (pSE->IsCOSE()) {
        distance += (radius / 2);
    } else if (pSE->IsGateSE()) {
        distance += (radius / 3);  // fudge the distance a bit for gates... its' a lil close by default
    } else if (pSE->IsWormholeSE()) {
        distance += 20000;  // add 20k for wh
    }
    // planets, large celestials and the edge of everything else (WarpInPoint.h)
    return WarpIn::ForEntity(pSE, shipPos);
}

void BeyonceBound::GetFleetWarpMembers(Client* pCmdr, uint32 excludeCharID, std::vector<Client*>& members, int32& speed)
{
    speed = 0;
    std::vector<Client*> candidates;
    sFltSvc.GetSubordinates(pCmdr, candidates, true, sConfig.world.fleetWarpOnGridOnly);

    // members that can't warp right now are left behind (Crucible didn't tell them)
    InterdictionMgr* pInterdict(pCmdr->SystemMgr() != nullptr ? pCmdr->SystemMgr()->GetInterdictionMgr() : nullptr);
    for (auto pClient : candidates) {
        if (pClient->GetCharacterID() == excludeCharID)
            continue;
        if (pClient->IsSessionChange() or pClient->IsJump() or pClient->IsDock() or pClient->IsLoginWarping())
            continue;
        ShipSE* pShip(pClient->GetShipSE());
        DestinyManager* pDestiny(pShip->DestinyMgr());
        if ((pDestiny == nullptr) or pDestiny->IsWarping() or pDestiny->IsFrozen())
            continue;
        if (pShip->IsWarpScrambled() or !CanWarpCloaked(pClient) or (pShip->WarpLockoutRemaining() > 0))
            continue;
        if ((pInterdict != nullptr) and pInterdict->IsWarpDisrupted(pShip))
            continue;
        members.push_back(pClient);
    }

    if (!sConfig.world.fleetWarpSlowestSpeed or members.empty())
        return;
    // the whole group warps at the speed of its slowest ship
    if (pCmdr->GetShipSE()->DestinyMgr() != nullptr)
        speed = pCmdr->GetShipSE()->DestinyMgr()->GetWarpSpeed();
    for (auto pClient : members) {
        int32 mySpeed(pClient->GetShipSE()->DestinyMgr()->GetWarpSpeed());
        if ((speed == 0) or (mySpeed < speed))
            speed = mySpeed;
    }
}

void BeyonceBound::FleetWarp(Client* pCmdr, const std::vector<Client*>& members, const GPoint& point, SystemEntity* pSE, uint32 toID, int32 minRange, int32 speed)
{
    for (auto pClient : members) {
        ShipSE* pShip(pClient->GetShipSE());
        // the target may have changed since GetFleetWarpMembers() (a member's own warp to it can't go anywhere)
        if ((pShip == nullptr) or (pShip == pSE) or (pShip->DestinyMgr() == nullptr))
            continue;
        int32 distance(minRange);
        GPoint warpToPoint(GetWarpInPoint(pSE, toID, point, pShip->GetPosition(), distance));
        distance += (pShip->GetRadius() * 2); // add ship diameter to distance

        pClient->SetInvul(false);
        pClient->SetUndock(false);
        pClient->SetAutoPilot(false);
        pShip->DestinyMgr()->WarpTo(warpToPoint, distance, false, nullptr, speed);
        if (!pShip->DestinyMgr()->IsAligningForWarp() and !pShip->DestinyMgr()->IsWarping())
            continue;   // too close, no capacitor, bubbled...  WarpTo told them

        PyDict* args = new PyDict();
            args->SetItemString("leader", new PyString(pCmdr->GetName()));
        pClient->SendRemoteMessage("FleetWarp", args);
    }
    _log(FLEET__TRACE, "%s fleet warped %u members to %u (speed %i)", pCmdr->GetName(), (uint32)members.size(), toID, speed);
}

PyResult BeyonceBound::CmdWarpToStuff(PyCallArgs &call, PyString* type, PyRep* id) {
    _log(AUTOPILOT__MESSAGE, "%s called WarpToStuff. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));
    //call.client->SetAutoPilot(false);

  _log(SERVICE__CALL_DUMP, "BeyonceBound::Handle_CmdWarpToStuff() - size=%lli", call.tuple->size());
   call.Dump(SERVICE__CALL_DUMP);

   // >0 means ship cannot warp (warp stabs are neg values, warp scrams are pos values)
   if (call.client->GetShipSE()->IsWarpScrambled())
        throw UserError ("WarpScrambled");
    // warp disruption fields (bubbles)
    if (call.client->SystemMgr() != nullptr)
        call.client->SystemMgr()->GetInterdictionMgr()->CheckCanWarp(call.client->GetShipSE());
    CheckCloakedWarp(call.client);
    if (CheckWarpLockout(call.client))
        return PyStatic.NewNone();

    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    }
    if (pDestiny->IsWarping()){
        call.client->SendNotifyMsg( "You are already warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }

    SystemManager* pSystem = call.client->SystemMgr();
    if (pSystem == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no system manager!", call.client->GetName());
        return PyStatic.NewNone();
    }

    // warp fleet/wing/squad (menusvc sends fleet=True for commanders only).  anyone else just warps alone
    bool fleet(false);
    std::map<std::string, PyRep*>::const_iterator kwItr = call.byname.find("fleet");
    if ((kwItr != call.byname.end()) and (kwItr->second != nullptr) and !kwItr->second->IsNone())
        fleet = (kwItr->second->IsBool() ? kwItr->second->AsBool()->value() : (PyRep::IntegerValue(kwItr->second) != 0));

    // get the warp-to distance specified by the client
    int32 distance(0); //Was set to 5000; setting to 0 makes warp in to station and gate more realistic.
    if (call.byname.find("minRange") != call.byname.end())
        distance = PyRep::IntegerValueU32(call.byname.find("minRange")->second);

    GPoint point(NULL_ORIGIN);
    SystemEntity* pSE(nullptr);
    uint32 toID(0);
    if (!ResolveWarpTarget(call.client, type->content(), id, point, pSE, toID))
        return PyStatic.NewNone();

    // the subordinates (and the group's warp speed) are taken before anyone moves
    std::vector<Client*> members;
    int32 speed(0);
    if (fleet and sFltSvc.IsCommander(call.client))
        GetFleetWarpMembers(call.client, (type->content() == "char" ? PyRep::IntegerValueU32(id) : 0), members, speed);

    int32 myDistance(distance);
    GPoint warpToPoint(GetWarpInPoint(pSE, toID, point, call.client->GetShipSE()->GetPosition(), myDistance));

    call.client->SetInvul(false);
    call.client->SetUndock(false);

    myDistance += (call.client->GetShipSE()->GetRadius() * 2); // add ship diameter to distance
    pDestiny->WarpTo(warpToPoint, myDistance, false, nullptr, speed);

    if (!members.empty())
        FleetWarp(call.client, members, point, pSE, toID, distance, speed);

    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdWarpToStuffAutopilot(PyCallArgs &call, PyInt* destID) {
    _log(AUTOPILOT__MESSAGE, "%s called WarpToStuffAutopilot. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));
    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }
    SystemManager* pSystem = call.client->SystemMgr();
    if (pSystem == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no system manager!", call.client->GetName());
        return PyStatic.NewNone();
    }

    SystemEntity* pSE = pSystem->GetSE(destID->value());
    if (pSE == nullptr) {
	  codelog(CLIENT__ERROR, "%s: unable to find destination Entity for ID %u", call.client->GetName(), destID->value());
        return PyStatic.NewNone();
    }

    // >0 means ship cannot warp (warp stabs are neg values, warp scrams are pos values)
    if (call.client->GetShipSE()->IsWarpScrambled())
        throw UserError ("WarpScrambled");
    // warp disruption fields (bubbles).  the autopilot turns itself off on 'WarpDisrupted' (autopilot.py)
    pSystem->GetInterdictionMgr()->CheckCanWarp(call.client->GetShipSE(), true);
    CheckCloakedWarp(call.client);
    if (CheckWarpLockout(call.client))
        return PyStatic.NewNone();

    call.client->SetInvul(false);
    call.client->SetUndock(false);
    // AP shit here.....
    call.client->SetAutoPilot(true);
    call.client->UpdateSessionInt("solarsystemid", pSystem->GetID());
    //call.client->UpdateSession();
    //call.client->SendSessionChange();

    uint16 distance = sConfig.world.apWarptoDistance;    // 15km default, as in Crucible (it became 10km in 2019)
    //Adding in ship and target object radius'
    //distance += call.client->GetShipSE()->GetRadius() + pSE->GetRadius();
    pDestiny->WarpTo(pSE->GetPosition(), distance, true, pSE);

    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdStop(PyCallArgs &call) {
    _log(AUTOPILOT__MESSAGE, "%s called Stop. AP: %s, Invul: %s", call.client->GetName(), \
            (call.client->IsAutoPilot() ? "true" : "false"), call.client->IsInvul()?"true":"false");

    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    }
    if (!pDestiny->IsMoving())
        return PyStatic.NewNone();
    if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }

    call.client->SetUndock(false);
    call.client->SetAutoPilot(false);

    pDestiny->Stop();

    return PyStatic.NewNone();
}

// CmdTurboDock (in client code)
PyResult BeyonceBound::CmdDock(PyCallArgs &call, PyInt* celestialID, PyInt* shipID, std::optional<PyRep*> paymentRequired) {
    // weapons timer and criminal lockdown
    sCrimeWatch.CheckDockOrJump(call.client, false);
    _log(AUTOPILOT__MESSAGE, "%s called Dock. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));
    if (call.client->IsSessionChange()) {
        call.client->SendNotifyMsg("Session Change currently active.");
        return PyStatic.NewNone();
    }
    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    } else if (pDestiny->IsModuleCloaked()) {
        throw UserError ("CantDockWhileCloaked");
    }
    // siege, triage, cyno...  modules that can't be stopped early keep the ship in space
    GenericModule* pBlocking(call.client->GetShip()->GetModuleManager()->GetActiveBlockingModule());
    if (pBlocking != nullptr)
        throw UserError ("CantDockWhileModuleActive")
                .AddFormatValue ("moduleType", new PyInt (pBlocking->typeID ()));
    SystemManager* pSystem = call.client->SystemMgr();
    if (pSystem == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no system manager.", call.client->GetName());
        return PyStatic.NewNone();
    }

    //  this sets m_dockStationID for radius checks and other things
    call.client->SetDockStationID( celestialID->value() );

    /* return error msg from this call, if applicable, else nodeid and timestamp */
    return pDestiny->AttemptDockOperation();
}

// GM (ctrl+shift+alt dock): skips the approach by moving the ship to the station first
PyResult BeyonceBound::CmdTurboDock(PyCallArgs &call, PyInt* stationID, std::optional<PyRep*> paymentRequired) {
    if ((call.client->GetAccountRole() & Acct::Role::GML) != Acct::Role::GML)
        throw CustomError ("Not allowed");
    if (call.client->IsSessionChange()) {
        call.client->SendNotifyMsg("Session Change currently active.");
        return PyStatic.NewNone();
    }
    ShipSE* pShip(call.client->GetShipSE());
    SystemManager* pSystem(call.client->SystemMgr());
    if ((pShip == nullptr) or (pShip->DestinyMgr() == nullptr) or (pSystem == nullptr))
        return PyStatic.NewNone();
    SystemEntity* pStation(pSystem->GetSE(stationID->value()));
    if ((pStation == nullptr) or !pStation->IsStationSE())
        throw CustomError ("Station %u not found", stationID->value());

    DestinyManager* pDestiny(pShip->DestinyMgr());
    if (pDestiny->IsWarping())
        pDestiny->Stop();
    // just outside the station, on the side the ship is on
    GVector dir(pStation->GetPosition(), pShip->GetPosition());
    if (dir.isZero())
        dir = GVector(1, 0, 0);
    dir.normalize();
    GPoint pos(pStation->GetPosition() + (dir * (pStation->GetRadius() + pShip->GetRadius() + 500)));
    pDestiny->Halt();
    pDestiny->SetPosition(pos, true);

    call.client->SetDockStationID(stationID->value());
    return pDestiny->AttemptDockOperation();
}

PyResult BeyonceBound::CmdStargateJump(PyCallArgs &call, PyInt* fromStargateID, PyInt* toStargateID, PyInt* shipID) {
    /* validate the jump before anything else:  the active ship, a gate in this system, that gate's destination,
     *  and the client's own range test (menusvc: ball.surfaceDist <= const.maxStargateJumpingDistance)
     */
    if ((uint32)shipID->value() != call.client->GetShipID())
        throw CustomError("Invalid ship.");
    ShipSE* pShipSE = call.client->GetShipSE();
    SystemManager* pSystem = call.client->SystemMgr();
    if ((pShipSE == nullptr) or (pSystem == nullptr))
        throw CustomError("You are not in space.");
    SystemEntity* pGate = pSystem->GetSE(fromStargateID->value());
    if ((pGate == nullptr) or !pGate->IsGateSE())
        throw CustomError("That stargate is not in this system.");
    if ((pShipSE->DestinyMgr() != nullptr) and pShipSE->DestinyMgr()->IsModuleCloaked())
        throw UserError("CantJumpWhileCloaked");
    // cyno, siege, triage, jump portal, doomsday...  modules that can't be stopped early keep the ship in this system
    GenericModule* pBlocking(call.client->GetShip()->GetModuleManager()->GetActiveBlockingModule());
    if (pBlocking != nullptr)
        throw UserError("CantJumpWhileModuleActive")
                .AddFormatValue("moduleType", new PyInt(pBlocking->typeID()));
    if (!pGate->GetGateSE()->HasDestination(toStargateID->value()))
        throw CustomError("That stargate does not lead there.");
    double dist = pShipSE->GetPosition().distance(pGate->GetPosition()) - pShipSE->GetRadius() - pGate->GetRadius();
    // allow one second of ship travel for client/server position skew
    double allowance(0.0);
    if (pShipSE->DestinyMgr() != nullptr)
        allowance = pShipSE->DestinyMgr()->GetSpeed();
    if (dist > maxStargateJumpingDistance + allowance)
        throw UserError("TargetNotWithinRange")
                .AddGroupName("targetGroupName", pGate->GetSelf()->groupID())
                .AddFormatValue("desiredRange", new PyFloat(maxStargateJumpingDistance))
                .AddFormatValue("actualDistance", new PyFloat(dist));

    // weapons timer and criminal lockdown
    sCrimeWatch.CheckDockOrJump(call.client, true);
    /*  jump system messages....
(67187, `{[location]system.name} Traffic Control: your jump-in clearance has expired.`)
(67191, `{[location]system.name} Traffic Control: you have been cleared for jump-in within {[timeinterval]expiration.writtenForm, from=second, to=second}.`)
(67210, `{[location]system.name} Traffic Control: you are at position {[numeric]pos} in queue for jump-in.`)
(258673, `{system} is currently loading. Bizzarre, seeing that you're there already... Please try again in a minute or two.`)
(258674, `The stargates in {system} are currently experiencing minor technical difficulties. Please try again in a moment.`)
(258675, `Jump Prohibited`)
(258676, `Officials have closed the stargates in {system} due to heavy congestion. Travelers are advised to enjoy the local scenery.`)
(258677, `{system} Traffic Control is currently experiencing heavy load and is unable to process your request. Please try again in a moment.`)
(258678, `{system} Traffic Control is currently offline and unable to process your jump request. Please try again in a moment.`)
(258679, `{system} Traffic Control is currently experiencing heavy load and is unable to process your request. You are #{[numeric]position} in queue for jump-in.`)
(258680, `Jump Prohibited`)
(258681, `Officials have closed the stargates in {system} due to heavy congestion. Travelers are advised to seek alternate routes.`)
(258683, `Your character is located within {system}, which is currently loading. Please try again in a moment.`)
(258685, `Your character is located within {system}, which is currently experiencing heavy load. Please try again in a moment.`)
(258687, `Your character is located within {system}, which has reached maximum capacity. You are #{[numeric]position} in queue for entrance. Please try again in a moment.`)
(258689, `Your character is located within {system}, which is currently stuck. Please select a different character or try later.`)
(258691, `The character is located within {system}, which is currently loading. Please try again in a moment.`)
(258693, `The character is located within {system}, which is currently experiencing heavy load. Please try again in a moment.`)
(258695, `The character is located within {system}, which is currently stuck. Please try again later.`)
(258699, `The item <b>{example}</b> was not found in the station <b>{[location]station.name}</b>. Please make sure all items are in the correct hangar before finalizing this contract.`)
(258701, `{system} Traffic Control is currently experiencing heavy load and was unable to process your request. Please try again in a moment.`)
(258704, `You cannot leave {system} yet because of instability in the space-time continuum. Please try again in a moment.`)
*/

    _log(AUTOPILOT__MESSAGE, "%s called Jump. AP: %s", call.client->GetName(), (call.client->IsAutoPilot() ? "true" : "false"));
    if (call.client->IsSessionChange()) {
        call.client->SendNotifyMsg("Session Change currently active.");
        return PyStatic.NewNone();
    }
    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        call.client->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }

    call.client->StargateJump(fromStargateID->value(), toStargateID->value());

    /* return error msg from this call, if applicable, else nodeid and timestamp */
    // returns nodeID and timestamp
    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

PyResult BeyonceBound::CmdChangeStructureOwner(PyCallArgs &call, PyInt* itemID, PyInt* oldOwnerID, PyInt* newOwnerID) {
    /* the client offers "Transfer Ownership" for anchored player customs offices to the owner corp's Directors.
     *  the new owner is any player corporation.  the office keeps its settings
     */
    Client* pClient(call.client);
    SystemManager* pSystem(pClient->SystemMgr());
    SystemEntity* pSE(pSystem == nullptr ? nullptr : pSystem->GetSE(itemID->value()));
    if ((pSE == nullptr) or (pSE->GetCOSE() == nullptr))
        throw CustomError ("Only customs offices can be transferred this way.");
    CustomsSE* pCO(pSE->GetCOSE());
    uint32 ownerID(pCO->GetOwnerID());
    if (!IsPlayerCorp(ownerID) or pCO->IsGantry())
        throw CustomError ("This %s can't be transferred.", pCO->GetName());
    if ((ownerID != (uint32)oldOwnerID->value()) or (ownerID != pClient->GetCorporationID()))
        throw CustomError ("Your corporation does not own this customs office.");
    if ((pClient->GetCorpRole() & Corp::Role::Director) == 0)
        throw CustomError ("Only Directors can transfer the ownership of a customs office.");
    uint32 newOwner(newOwnerID->value());
    if (!IsPlayerCorp(newOwner) or (newOwner == ownerID))
        throw CustomError ("Customs offices can only be transferred to another player corporation.");
    if (pCO->IsInTimedState())
        throw CustomError ("The customs office can't change hands while it is reinforced.");

    pCO->ChangeOwner(newOwner, PosMgrDB::GetCorpAllianceID(newOwner));
    _log(POS__MESSAGE, "%s transferred customs office %u from %u to %u", pClient->GetName(), pCO->GetID(), ownerID, newOwner);
    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdAbandonLoot(PyCallArgs &call, PyList* wreckIDs) {
	/*  remotePark.CmdAbandonLoot(wrecks)  <- this is pylist from 'abandonAllWrecks'
	 *  remotePark.CmdAbandonLoot([wreckID]) <- single itemID in list
	 */
  sLog.White( "BeyonceBound::Handle_CmdAbandonLoot()", "size=%lu", call.tuple->size());
    call.Dump(SERVICE__CALL_DUMP);

    std::vector<int32> ints;

    PyList::const_iterator list_2_cur = wreckIDs->begin();
    for (size_t list_2_index(0); list_2_cur != wreckIDs->end(); ++list_2_cur, ++list_2_index) {
        if (!(*list_2_cur)->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_SingleIntList failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
            return nullptr;
        }

        const PyInt* t = (*list_2_cur)->AsInt();
        ints.push_back(t->value());
    }
	/** @todo  change ownerID to _system for all loot also!!  */
	SystemEntity* pSE(nullptr);
	SystemManager* pSysMgr = call.client->SystemMgr();
	for (auto cur : ints) {
        pSE = pSysMgr->GetSE(cur);
        if (pSE == nullptr)
            continue;
        pSE->Abandon();
        PyTuple* slimData = new PyTuple(2);
            slimData->SetItem(0, new PyLong(pSE->GetID()));
            slimData->SetItem(1, new PyObject( "foo.SlimItem", pSE->MakeSlimItem()));
        PyTuple* itemData = new PyTuple(2);
            itemData->SetItem(0, new PyString("OnSlimItemChange"));
            itemData->SetItem(1, slimData);
        pSE->SysBubble()->BubblecastDestinyUpdate(&itemData, "OnSlimItemChange" );
    }

    return PyStatic.NewNone();
}

PyResult BeyonceBound::UpdateStateRequest(PyCallArgs &call) {
    codelog(CLIENT__ERROR, "%s: Client sent UpdateStateRequest. Previous call generated a bad return.  Check Logs.", call.client->GetName());

    DestinyManager* pDestiny = call.client->GetShipSE()->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", call.client->GetName());
        return PyStatic.NewNone();
    }
    if (pDestiny->IsWarping()) {
        call.client->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    }

    call.client->SetStateSent(false);
    pDestiny->SendSetState();

    return PyStatic.NewNone();
}



/** client side of the bridge and beacon jump calls (menusvc.py):
 *    def BridgeToMember(self, charID):
 *        beaconStuff = sm.GetService('fleet').GetActiveBeaconForChar(charID)
 *        if beaconStuff is None:
 *            return
 *        self.BridgeToBeacon(charID, beaconStuff)
 *
 *    def BridgeToBeaconAlliance(self, solarSystemID, beaconID):
 *        bp = sm.StartService('michelle').GetRemotePark()
 *        if bp is None:
 *            return
 *        bp.CmdBridgeToStructure(beaconID, solarSystemID)
 *
 *    def BridgeToBeacon(self, charID, beacon):
 *        solarsystemID, beaconID = beacon
 *        bp = sm.StartService('michelle').GetRemotePark()
 *        if bp is None:
 *            return
 *        bp.CmdBridgeToMember(charID, beaconID, solarsystemID)
 *
 *    def JumpThroughFleet(self, otherCharID, otherShipID):
 *        bp = sm.StartService('michelle').GetRemotePark()
 *        if bp is None:
 *            return
 *        bridge = sm.GetService('fleet').GetActiveBridgeForShip(otherShipID)
 *        if bridge is None:
 *            return
 *        solarsystemID, beaconID = bridge
 *        self.LogNotice('Jump Through Fleet', otherCharID, otherShipID, beaconID, solarsystemID)
 *        sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdJumpThroughFleet, otherCharID, otherShipID, beaconID, solarsystemID)
 *
 *    def JumpThroughAlliance(self, otherShipID):
 *        bp = sm.StartService('michelle').GetRemotePark()
 *        if bp is None:
 *            return
 *        bridge = sm.StartService('pwn').GetActiveBridgeForShip(otherShipID)
 *        if bridge is None:
 *            return
 *        solarsystemID, beaconID = bridge
 *        self.LogNotice('Jump Through Alliance', otherShipID, beaconID, solarsystemID)
 *        sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdJumpThroughAlliance, otherShipID, beaconID, solarsystemID)
 *
 *    def JumpToMember(self, charid):
 *        beaconStuff = sm.GetService('fleet').GetActiveBeaconForChar(charid)
 *        if beaconStuff is None:
 *            return
 *        self.JumpToBeaconFleet(charid, beaconStuff)
 *
 *    def JumpToBeaconFleet(self, charid, beacon):
 *        solarsystemID, beaconID = beacon
 *        bp = sm.StartService('michelle').GetRemotePark()
 *        if bp is None:
 *            return
 *        self.LogNotice('Jump To Beacon Fleet', charid, beaconID, solarsystemID)
 *        for wnd in uicore.registry.GetWindows()[:]:
 *            if getattr(wnd, '__guid__', None) == 'form.CorpHangarArray':
 *                wnd.CloseByUser()
 *
 *        sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdBeaconJumpFleet, charid, beaconID, solarsystemID)
 *
 *    def JumpToBeaconAlliance(self, solarSystemID, beaconID):
 *        bp = sm.StartService('michelle').GetRemotePark()
 *        if bp is None:
 *            return
 *        self.LogNotice('Jump To Beacon Alliance', beaconID, solarSystemID)
 *        sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdBeaconJumpAlliance, beaconID, solarSystemID)
 *
 *    def ActivateGridSmartBomb(self, charid, effect):
 *        beaconStuff = sm.GetService('fleet').GetActiveBeaconForChar(charid)
 *        if beaconStuff is None:
 *            return
 *        solarsystemID, beaconID = beaconStuff
 *        bp = sm.StartService('michelle').GetRemotePark()
 *        if bp is None:
 *            return
 *        effect.Activate(beaconID, False)
 */
PyResult BeyonceBound::DoOpenBridge(Client* pClient, bool alliance, uint32 charID, uint32 beaconID, uint32 systemID)
{
    _log(AUTOPILOT__MESSAGE, "%s called %s bridge to %u in %u.", pClient->GetName(), (alliance ? "alliance" : "fleet"), beaconID, systemID);
    if (pClient->IsSessionChange()) {
        pClient->SendNotifyMsg("Session Change currently active.");
        return PyStatic.NewNone();
    }
    ShipSE* pShipSE(pClient->GetShipSE());
    if ((pShipSE == nullptr) or !pClient->IsInSpace())
        throw CustomError("You are not in space.");
    DestinyManager* pDestiny(pShipSE->DestinyMgr());
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", pClient->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        pClient->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    } else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsCloaked()) {
        throw UserError("DeniedActivateCloaked");
    }

    ShipItemRef ship(pClient->GetShip());
    GenericModule* pMod(ship->GetModuleManager()->GetModuleByGroup(EVEDB::invGroups::Jump_Portal_Generator));
    if ((pMod == nullptr) or !pMod->IsJumpPortalModule())
        throw UserError("JumpNoPortal");
    if (!pMod->isOnline())
        throw CustomError("Your %s is offline.", pMod->GetSelf()->name());
    if (pMod->IsActive())
        throw CustomError("Your jump portal is already open.");
    JumpPortalModule* pPortal(pMod->GetJumpPortalModule());

    // other blocking modules, the doomsday's jump lockout, warp scramble, bubbles
    JumpDrive::CheckDriveFree(pClient, pMod);
    if (alliance and (pClient->GetAllianceID() == 0))
        throw CustomError("You must be in an alliance to open an alliance jump portal.");
    if (!alliance and !pClient->InFleet())
        throw CustomError("You must be in a fleet to open a jump portal to a fleet member's cynosural field.");

    BeaconData beacon = BeaconData();
    JumpDrive::ResolveBeacon(pClient, alliance, charID, beaconID, systemID, beacon);
    JumpDrive::CheckDestination(pClient->GetSystemID(), beacon.systemID);
    // titan portals (0) can't reach covert fields (5).  black ops portals (5) reach both
    if (pPortal->GetHarmonics() < beacon.harmonics)
        throw UserError("JumpHarmonicsDoNotMatch");

    double ly(JumpDrive::LightYears(pClient->GetSystemID(), beacon.systemID));
    double range(ship->GetAttribute(AttrJumpDriveRange).get_double());
    if (ly > range)
        throw UserError("JumpTooFar")
                .AddFormatValue("dist", new PyFloat(ly))
                .AddFormatValue("range", new PyFloat(range));

    BridgeData data = BridgeData();
        data.shipID = ship->itemID();
        data.charID = pClient->GetCharacterID();
        data.fleetID = (alliance ? 0 : pClient->GetFleetID());
        data.allianceID = (alliance ? pClient->GetAllianceID() : 0);
        data.fromSystemID = pClient->GetSystemID();
        data.toSystemID = beacon.systemID;
        data.beaconID = beacon.beaconID;
        data.beaconCharID = (alliance ? 0 : charID);
        data.moduleID = pMod->itemID();
        data.harmonics = pPortal->GetHarmonics();
        data.alliance = alliance;
        data.position = beacon.position;
    // the generator's capacitor and strontium are taken by the module cycle
    pPortal->OpenBridge(data);

    return PyStatic.NewNone();
}

PyResult BeyonceBound::DoBridgeJump(Client* pClient, bool alliance, uint32 otherCharID, uint32 otherShipID, uint32 beaconID, uint32 systemID)
{
    _log(AUTOPILOT__MESSAGE, "%s called %s jump through %u to %u in %u.", pClient->GetName(), (alliance ? "alliance" : "fleet"), otherShipID, beaconID, systemID);
    if (pClient->IsSessionChange()) {
        pClient->SendNotifyMsg("Session Change currently active.");
        return PyStatic.NewNone();
    }
    ShipSE* pShipSE(pClient->GetShipSE());
    SystemManager* pSystem(pClient->SystemMgr());
    if ((pShipSE == nullptr) or (pSystem == nullptr) or !pClient->IsInSpace())
        throw CustomError("You are not in space.");
    DestinyManager* pDestiny(pShipSE->DestinyMgr());
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", pClient->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        pClient->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    } else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        pClient->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    } else if (pDestiny->IsModuleCloaked()) {
        throw UserError("CantJumpWhileCloaked");
    }
    // modules that can't be stopped early keep the ship here (siege, cyno, ...)
    GenericModule* pBlocking(pClient->GetShip()->GetModuleManager()->GetActiveBlockingModule());
    if (pBlocking != nullptr)
        throw UserError("CantJumpWhileModuleActive")
                .AddFormatValue("moduleType", new PyInt(pBlocking->typeID()));
    // weapons timer and criminal lockdown, as for stargates
    sCrimeWatch.CheckDockOrJump(pClient, true);

    // the bridge must be open, lead there, and be open to us
    BridgeData bridge = BridgeData();
    if (!sFltSvc.GetBridge(otherShipID, bridge) or (bridge.alliance != alliance)
    or (bridge.beaconID != beaconID) or (bridge.toSystemID != systemID) or (bridge.fromSystemID != pClient->GetSystemID())
    or (alliance and ((bridge.allianceID == 0) or (bridge.allianceID != (uint32)pClient->GetAllianceID())))
    or (!alliance and (!pClient->InFleet() or (bridge.fleetID != pClient->GetFleetID()) or (bridge.charID != otherCharID))))
        throw UserError("JumpNoBridge");
    if (otherShipID == pClient->GetShipID())
        throw CustomError("You cannot jump through your own jump portal.");
    SystemEntity* pBridger(pSystem->GetSE(otherShipID));
    if ((pBridger == nullptr) or !pBridger->IsShipSE())
        throw UserError("JumpNoBridge");

    // the client's own range test (menusvc: surface distance within const.maxStargateJumpingDistance)
    double dist(pShipSE->GetPosition().distance(pBridger->GetPosition()) - pShipSE->GetRadius() - pBridger->GetRadius());
    double allowance(pDestiny->GetSpeed());     // one second of travel for client/server position skew
    if (dist > maxStargateJumpingDistance + allowance)
        throw UserError("TargetNotWithinRange")
                .AddGroupName("targetGroupName", pBridger->GetSelf()->groupID())
                .AddFormatValue("desiredRange", new PyFloat(maxStargateJumpingDistance))
                .AddFormatValue("actualDistance", new PyFloat(dist));

    // capitals (-5) can't use a titan portal (0).  only covert ops capable ships (5) can use a black ops portal (5)
    ShipItemRef ship(pClient->GetShip());
    if (JumpDrive::Harmonics(ship.get()) < bridge.harmonics)
        throw UserError("JumpHarmonicsDoNotMatch");
    JumpDrive::CheckDestination(pClient->GetSystemID(), bridge.toSystemID);
    // the portal leads nowhere once its cyno (or the alliance's generator array) is gone
    BeaconData beacon = BeaconData();
    JumpDrive::ResolveBeacon(pClient, alliance, bridge.beaconCharID, bridge.beaconID, bridge.toSystemID, beacon);

    /* the bridging ship pays:  ly x its jumpDriveConsumptionAmount x our mass x its jumpPortalConsumptionMassFactor
     *  of its jumpDriveConsumptionType (menusvc / posConst.GetJumpFuelConsumption) */
    ShipItemRef bridger(pBridger->GetShipSE()->GetShipItemRef());
    double ly(JumpDrive::LightYears(pClient->GetSystemID(), bridge.toSystemID));
    uint16 fuelType(bridger->GetAttribute(AttrJumpDriveConsumptionType).get_uint32());
    double massFactor(ship->GetAttribute(AttrMass).get_double() * bridger->GetAttribute(AttrJumpPortalConsumptionMassFactor).get_double());
    uint32 qty(JumpDrive::FuelNeeded(ly, bridger->GetAttribute(AttrJumpDriveConsumptionAmount).get_double(), massFactor));
    if (JumpDrive::CountFuel(bridger, fuelType) < qty) {
        const ItemType* pType(sItemFactory.GetType(fuelType));
        throw CustomError("The bridging ship does not have enough %s to bridge your ship (%u units required).", (pType != nullptr ? pType->name().c_str() : "fuel"), qty);
    }

    // throws when a move is already pending, before anything is taken
    bool covert(bridge.harmonics > 0);
    pClient->CynoJump(bridge.toSystemID, bridge.position, (covert ? "effects.JumpDriveOutBO" : "effects.JumpDriveOut"),
            (covert ? "effects.JumpDriveInBO" : "effects.JumpDriveIn"));
    JumpDrive::ConsumeFuel(bridger, fuelType, qty);

    _log(SHIP__MESSAGE, "%s bridged %.2f ly through %s(%u) to %u in %u for %u units of %u", pClient->GetName(), ly, pBridger->GetName(),
            otherShipID, bridge.beaconID, bridge.toSystemID, qty, fuelType);

    /* return error msg from this call, if applicable, else nodeid and timestamp */
    // returns nodeID and timestamp
    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

PyResult BeyonceBound::CmdBridgeToMember(PyCallArgs &call, PyInt* charID, PyInt* beaconID, PyInt* solarSystemID) {
    // bp.CmdBridgeToMember(charID, beaconID, solarsystemID)
    return DoOpenBridge(call.client, false, charID->value(), beaconID->value(), solarSystemID->value());
}

PyResult BeyonceBound::CmdBridgeToStructure(PyCallArgs &call, PyInt* beaconID, PyInt* solarSystemID) {
    // bp.CmdBridgeToStructure(beaconID, solarSystemID)
    return DoOpenBridge(call.client, true, 0, beaconID->value(), solarSystemID->value());
}

PyResult BeyonceBound::CmdJumpThroughFleet(PyCallArgs &call, PyInt* otherCharID, PyInt* otherShipID, PyInt* beaconID, PyInt* solarSystemID) {
    // sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdJumpThroughFleet, otherCharID, otherShipID, beaconID, solarsystemID)
    return DoBridgeJump(call.client, false, otherCharID->value(), otherShipID->value(), beaconID->value(), solarSystemID->value());
}

PyResult BeyonceBound::CmdJumpThroughAlliance(PyCallArgs &call, PyInt* otherShipID, PyInt* beaconID, PyInt* solarSystemID) {
    //sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdJumpThroughAlliance, otherShipID, beaconID, solarsystemID)
    return DoBridgeJump(call.client, true, 0, otherShipID->value(), beaconID->value(), solarSystemID->value());
}

PyResult BeyonceBound::CmdJumpThroughCorporationStructure(PyCallArgs &call, PyInt* itemID, PyInt* remoteStructureID, PyInt* remoteSystemID) {
    //sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdJumpThroughCorporationStructure, itemID, remoteStructureID, remoteSystemID)
    Client* pClient(call.client);
    _log(AUTOPILOT__MESSAGE, "%s called bridge jump.", pClient->GetName());
    if (pClient->IsSessionChange()) {
        pClient->SendNotifyMsg("Session Change currently active.");
        return PyStatic.NewNone();
    }

    ShipSE* pShipSE(pClient->GetShipSE());
    if ((pShipSE == nullptr) or !pClient->IsInSpace() or (pClient->SystemMgr() == nullptr))
        throw CustomError("You are not in space.");
    DestinyManager* pDestiny = pShipSE->DestinyMgr();
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", pClient->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        pClient->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    }  else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        pClient->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }

    // weapons timer and criminal lockdown, as for stargates
    sCrimeWatch.CheckDockOrJump(pClient, true);

    ShipItemRef ship(pClient->GetShip());
    // the client offers the jump only outside capsules
    if (ship->groupID() == EVEDB::invGroups::Capsule)
        throw CustomError("You cannot use a jump bridge in a capsule.");

    SystemEntity* pSE(pClient->SystemMgr()->GetSE(itemID->value()));
    JumpBridgeSE* pBridge(pSE == nullptr ? nullptr : pSE->GetJumpBridgeSE());
    if (pBridge == nullptr)
        throw UserError("JumpDestinationInvalid");

    double distance(pShipSE->DistanceTo2(pBridge) - pShipSE->GetRadius() - pBridge->GetRadius());
    if (distance > maxStargateJumpingDistance)
        throw CustomError("You must be within %u m of the %s to jump.", (uint32)maxStargateJumpingDistance, pBridge->GetName());

    // both ends online, and the link the client asks for is this bridge's
    TowerSE* pTower(pBridge->GetControlTower());
    if (!pBridge->IsOnline() or (pTower == nullptr) or !pTower->IsOnline())
        throw UserError("CantJumpJumpPortalOffline")
                .AddFormatValue("structure", new PyString(pBridge->GetName()))
                .AddFormatValue("system", new PyString(sDataMgr.GetSystemName(pClient->GetSystemID())));
    const EVEPOS::JumpBridgeData& link(pBridge->GetBridgeData());
    if ((link.toItemID == 0) or ((uint32)link.toItemID != (uint32)remoteStructureID->value())
    or  ((uint32)link.toSystemID != (uint32)remoteSystemID->value()))
        throw UserError("JumpDestinationInvalid");

    // access:  the owner corp, the alliance if the tower allows it, or the force field password
    bool access((uint32)pClient->GetCorporationID() == pBridge->GetCorporationID());
    if (!access and pTower->GetAllyAccess() and IsAlliance(pBridge->GetAllianceID()))
        access = (pClient->GetAllianceID() == pBridge->GetAllianceID());
    if (!access and !pTower->GetPassword().empty())
        access = (pShipSE->GetPassword() == pTower->GetPassword());
    if (!access)
        throw UserError("CantJumpJumpPortalAccessRestricted");
    if (pTower->ConsidersHostile(pClient))
        throw UserError("CantJumpJumpPortalObservesTowerSettings");

    // remote end (boots its system, like a gate jump would)
    SystemManager* pRemoteSys(sEntityList.FindOrBootSystem(link.toSystemID));
    SystemEntity* pRemoteSE(pRemoteSys == nullptr ? nullptr : pRemoteSys->GetSE(link.toItemID));
    JumpBridgeSE* pRemote(pRemoteSE == nullptr ? nullptr : pRemoteSE->GetJumpBridgeSE());
    if (pRemote == nullptr)
        throw UserError("JumpDestinationInvalid");
    TowerSE* pRemoteTower(pRemote->GetControlTower());
    if (!pRemote->IsOnline() or (pRemoteTower == nullptr) or !pRemoteTower->IsOnline())
        throw UserError("CantJumpJumpPortalOffline")
                .AddFormatValue("structure", new PyString(pRemote->GetName()))
                .AddFormatValue("system", new PyString(sDataMgr.GetSystemName(link.toSystemID)));
    if ((uint32)pRemote->GetBridgeData().toItemID != pBridge->GetID())
        throw UserError("JumpDestinationInvalid");

    // fuel from the departure bridge:  ly x jumpDriveConsumptionAmount x ship mass x jumpPortalConsumptionMassFactor
    double ly(JumpDrive::LightYears(pClient->GetSystemID(), link.toSystemID));
    uint32 need(pBridge->FuelNeeded(ly, ship->GetAttribute(AttrMass).get_double()));
    uint32 have(pBridge->CountFuel());
    if (have < need)
        throw UserError("JumpNotEnoughCharge3")
                .AddFormatValue("required", new PyInt(need))
                .AddTypeName("typeName", pBridge->GetFuelType())
                .AddFormatValue("available", new PyInt(have));

    // throws when a move is already pending, before any fuel is taken
    pClient->CynoJump(link.toSystemID, pRemote->GetPosition());
    pBridge->ConsumeFuel(need);

    _log(SHIP__MESSAGE, "%s bridges %.2f ly from %u to %u in %u for %u fuel", pClient->GetName(), ly, pBridge->GetID(),
            pRemote->GetID(), link.toSystemID, need);

    /* return error msg from this call, if applicable, else nodeid and timestamp */
    // returns nodeID and timestamp
    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

PyResult BeyonceBound::DoBeaconJump(Client* pClient, bool alliance, uint32 charID, uint32 beaconID, uint32 systemID)
{
    _log(AUTOPILOT__MESSAGE, "%s called %s beacon jump to %u in %u.", pClient->GetName(), (alliance ? "alliance" : "fleet"), beaconID, systemID);
    if (pClient->IsSessionChange()) {
        pClient->SendNotifyMsg("Session Change currently active.");
        return PyStatic.NewNone();
    }
    ShipSE* pShipSE(pClient->GetShipSE());
    if ((pShipSE == nullptr) or !pClient->IsInSpace())
        throw CustomError("You are not in space.");
    DestinyManager* pDestiny(pShipSE->DestinyMgr());
    if (pDestiny == nullptr) {
        codelog(CLIENT__ERROR, "%s: Client has no destiny manager!", pClient->GetName());
        return PyStatic.NewNone();
    } else if (pDestiny->IsWarping()) {
        pClient->SendNotifyMsg( "You can't do this while warping");
        return PyStatic.NewNone();
    } else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    }

    ShipItemRef ship(pClient->GetShip());
    if (!ship->GetAttribute(AttrCanJump).get_bool())
        throw UserError("JumpNoDrive");
    // active cyno/portal/siege, doomsday lockout, warp scramble, bubbles
    JumpDrive::CheckDriveFree(pClient);
    if (pDestiny->IsFrozen()) {
        pClient->SendNotifyMsg( "Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }
    // weapons timer and criminal lockdown, as for stargates
    sCrimeWatch.CheckDockOrJump(pClient, true);

    BeaconData beacon = BeaconData();
    JumpDrive::ResolveBeacon(pClient, alliance, charID, beaconID, systemID, beacon);
    JumpDrive::CheckDestination(pClient->GetSystemID(), beacon.systemID);
    // capitals (-5) can't use covert fields (5).  black ops (5) can use both
    if (JumpDrive::Harmonics(ship.get()) < beacon.harmonics)
        throw UserError("JumpHarmonicsDoNotMatch");

    double ly(JumpDrive::LightYears(pClient->GetSystemID(), beacon.systemID));
    double range(ship->GetAttribute(AttrJumpDriveRange).get_double());
    if (ly > range)
        throw UserError("JumpTooFar")
                .AddFormatValue("dist", new PyFloat(ly))
                .AddFormatValue("range", new PyFloat(range));

    // jumpDriveCapacitorNeed is a fraction of the capacitor's capacity
    double cap(ship->GetAttribute(AttrCapacitorCharge).get_double());
    double capacity(ship->GetAttribute(AttrCapacitorCapacity).get_double());
    double need(capacity * ship->GetAttribute(AttrJumpDriveCapacitorNeed).get_double());
    if (cap < need)
        throw UserError("JumpNotEnoughCap")
                .AddFormatValue("need", new PyFloat(need))
                .AddFormatValue("got", new PyFloat(cap));

    // fuel per light year (Jump Fuel Conservation and the jump freighter bonus are in the attribute through dogma)
    uint16 fuelType(ship->GetAttribute(AttrJumpDriveConsumptionType).get_uint32());
    uint32 qty(JumpDrive::FuelNeeded(ly, ship->GetAttribute(AttrJumpDriveConsumptionAmount).get_double()));
    if (JumpDrive::CountFuel(ship, fuelType) < qty) {
        const ItemType* pType(sItemFactory.GetType(fuelType));
        throw CustomError("This jump requires %u units of %s in your fuel bay or cargo hold.", qty, (pType != nullptr ? pType->name().c_str() : "fuel"));
    }

    // throws when a move is already pending, before anything is taken
    bool blackOps(ship->groupID() == EVEDB::invGroups::BlackOps);
    pClient->CynoJump(beacon.systemID, beacon.position, (blackOps ? "effects.JumpDriveOutBO" : "effects.JumpDriveOut"),
            (blackOps ? "effects.JumpDriveInBO" : "effects.JumpDriveIn"));

    JumpDrive::ConsumeFuel(ship, fuelType, qty);
    if (capacity > 0)
        ship->SetShipCapacitorLevel((cap - need) / capacity);

    _log(SHIP__MESSAGE, "%s jumps %.2f ly to %u in %u for %u units of %u", pClient->GetName(), ly, beacon.beaconID, beacon.systemID, qty, fuelType);

    /* return error msg from this call, if applicable, else nodeid and timestamp */
    // returns nodeID and timestamp
    // HACK: WE'RE RETURNING BACK THE SAME BOUND SERVICE, IN REALITY A NEW BOUND INSTANCE SHOULD BE CREATED FOR THIS SHIP IN SPECIFIC
    //       INSTEAD OF REUSING THIS ONE, THIS WOULD HELP KEEP INFORMATION IN/OUT OF MEMORY BASED ON THE BOUND SERVICES
    return this->GetOID();
}

PyResult BeyonceBound::CmdBeaconJumpFleet(PyCallArgs &call, PyInt* characterID, PyInt* beaconID, PyInt* solarSystemID) {
    // sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdBeaconJumpFleet, charid, beaconID, solarsystemID)
    return DoBeaconJump(call.client, false, characterID->value(), beaconID->value(), solarSystemID->value());
}

PyResult BeyonceBound::CmdBeaconJumpAlliance(PyCallArgs &call, PyInt* beaconID, PyInt* solarSystemID) {
    // sm.StartService('sessionMgr').PerformSessionChange('jump', bp.CmdBeaconJumpAlliance, beaconID, solarSystemID)
    return DoBeaconJump(call.client, true, 0, beaconID->value(), solarSystemID->value());
}

PyResult BeyonceBound::CmdFleetRegroup(PyCallArgs &call) {
    // fleetSvc.Regroup() -> bp.CmdFleetRegroup().  the commander's subordinates on grid approach the commander at full speed
    if (!call.client->InFleet() or !(sFltSvc.IsCommander(call.client) or sFltSvc.IsBoss(call.client->GetChar().get())))
        throw UserError("FleetNotAllowed");
    ShipSE* pCmdrSE(call.client->GetShipSE());
    if ((pCmdrSE == nullptr) or !call.client->IsInSpace())
        return PyStatic.NewNone();

    std::vector<Client*> members;
    sFltSvc.GetSubordinates(call.client, members, true, true);
    for (auto pClient : members) {
        if (pClient->IsSessionChange() or pClient->IsJump() or pClient->IsDock() or pClient->IsLoginWarping())
            continue;
        DestinyManager* pDestiny(pClient->GetShipSE()->DestinyMgr());
        if ((pDestiny == nullptr) or pDestiny->IsWarping() or pDestiny->IsFrozen())
            continue;
        pClient->SetUndock(false);
        pClient->SetAutoPilot(false);
        pDestiny->Follow(pCmdrSE, 50);
        pDestiny->SetSpeedFraction(1.0f);

        PyDict* args = new PyDict();
            args->SetItemString("leader", new PyString(call.client->GetName()));
        pClient->SendRemoteMessage("FleetRegroup", args);
    }

    return PyStatic.NewNone();
}

PyResult BeyonceBound::CmdFleetTagTarget(PyCallArgs &call, PyInt* itemID, PyString* tag) {
    // bp.CmdFleetTagTarget(itemID, tag).  the client offers this to commanders (menusvc ImFleetCommander)
    if (!call.client->InFleet() or !(sFltSvc.IsCommander(call.client) or sFltSvc.IsBoss(call.client->GetChar().get())))
        throw UserError("FleetNotAllowed");
    // menusvc tags: '0'-'9', 'A'-'J', 'X', 'Y', 'Z'
    const std::string& str(tag->content());
    if (str.size() != 1)
        return PyStatic.NewNone();
    char c(str[0]);
    if (!(((c >= '0') and (c <= '9')) or ((c >= 'A') and (c <= 'J')) or (c == 'X') or (c == 'Y') or (c == 'Z')))
        return PyStatic.NewNone();

    sFltSvc.SetTargetTag(call.client->GetFleetID(), itemID->value(), str);
    return PyStatic.NewNone();
}
