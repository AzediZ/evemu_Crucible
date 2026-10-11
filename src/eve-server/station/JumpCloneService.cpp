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

#include "eve-server.h"

#include "EVE_Station.h"

#include "EntityList.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "character/Character.h"
#include "character/CharacterDB.h"
#include "pos/PosMgrDB.h"
#include "standing/StandingDB.h"
#include "station/JumpCloneService.h"
#include "station/StationDataMgr.h"
#include "services/ServiceManager.h"

JumpCloneService::JumpCloneService(EVEServiceManager& mgr) :
    BindableService("jumpCloneSvc", mgr)
{
}

BoundDispatcher* JumpCloneService::BindObject (Client* client, PyRep* bindParameters)
{
    return new JumpCloneBound(this->GetServiceManager(), *this, client->GetLocationID());
}

void JumpCloneService::BoundReleased (JumpCloneBound* bound) {

}

void JumpCloneService::InvalidateClientCaches(uint32 charID, uint32 stationID/*0*/)
{
    Client* pClient(sEntityList.FindClientByCharID(charID));
    if (pClient == nullptr)
        return;
    PyTuple* tmp = new PyTuple(0);
    pClient->SendNotification("OnJumpCloneCacheInvalidated", "charid", &tmp, false);
    if (stationID == 0)
        return;
    tmp = new PyTuple(2);
        tmp->SetItem(0, new PyInt(stationID));
        tmp->SetItem(1, new PyInt(charID));
    pClient->SendNotification("OnStationJumpCloneCacheInvalidated", "charid", &tmp, false);
}

JumpCloneBound::JumpCloneBound (EVEServiceManager& mgr, JumpCloneService& parent, uint32 locationID) :
    EVEBoundObject(mgr, parent),
    m_svc(parent),
    m_locationID(locationID),                 // station or solar system
    m_locGroupID(EVEDB::invGroups::Station)   // solarsystem(in space) or station
{
    this->Add("GetCloneState", &JumpCloneBound::GetCloneState);
    this->Add("GetShipCloneState", &JumpCloneBound::GetShipCloneState);
    this->Add("GetPriceForClone", &JumpCloneBound::GetPriceForClone);
    this->Add("InstallCloneInStation", &JumpCloneBound::InstallCloneInStation);
    this->Add("GetStationCloneState", &JumpCloneBound::GetStationCloneState);
    this->Add("OfferShipCloneInstallation", &JumpCloneBound::OfferShipCloneInstallation);
    this->Add("DestroyInstalledClone", &JumpCloneBound::DestroyInstalledClone);
    this->Add("AcceptShipCloneInstallation", &JumpCloneBound::AcceptShipCloneInstallation);
    this->Add("CancelShipCloneInstallation", &JumpCloneBound::CancelShipCloneInstallation);
    this->Add("CloneJump", &JumpCloneBound::CloneJump);
    /*
     _ _notifyevents__ = ['OnShipJumpCloneInstallationOffered',
     'OnShipJumpCloneInstallationDone',
     'OnJumpCloneCacheInvalidated',
     'OnShipJumpCloneCacheInvalidated',
     'OnStationJumpCloneCacheInvalidated',
     'OnShipJumpCloneInstallationCanceled']
     */

    if (!sDataMgr.IsStation(m_locationID))
        m_locGroupID = EVEDB::invGroups::Solar_System;
}

PyResult JumpCloneBound::GetCloneState(PyCallArgs &call) {
    /* KeyVal(clones = rowset jumpCloneID, locationID,  implants = rowset jumpCloneID, typeID,  timeLastJump = filetime or None)
     *  the client indexes clones by jumpCloneID and filters implants by jumpCloneID
     */
    uint32 charID(call.client->GetCharacterID());
    PyRep* clones(CharacterDB::GetJumpClones(charID));
    PyRep* implants(CharacterDB::GetJumpCloneImplants(charID));
    if ((clones == nullptr) or (implants == nullptr)) {
        PySafeDecRef(clones);
        PySafeDecRef(implants);
        throw CustomError("Unable to read your jump clones.");
    }

    PyDict* dict = new PyDict();
    dict->SetItemString( "clones", clones );
    dict->SetItemString( "implants", implants );
    /* the character sheet always counts limitCloneJumpHours (24h) from this.  a configured cooldown shifts the time sent,
     *  so the countdown it shows is the one enforced */
    int64 last(CharacterDB::GetLastCloneJumpTime(charID));
    if (last > 0) {
        last += ((int64)sConfig.character.jumpCloneCooldownHours - 24) * EvE::Time::Hour;
        dict->SetItemString( "timeLastJump", new PyLong(last) );
    } else {
        dict->SetItemString( "timeLastJump", PyStatic.NewNone() );
    }

    return new PyObject( "util.KeyVal", dict );
}

PyResult JumpCloneBound::GetShipCloneState(PyCallArgs &call) {
    // rowset jumpCloneID, ownerID, locationID of the clones in the caller's ship (shipconfig shows ownerID's name)
    PyRep* rs(CharacterDB::GetJumpClonesAtLocation(call.client->GetShipID()));
    if (rs == nullptr)
        throw CustomError("Unable to read the jump clones in your ship.");
    return rs;
}

PyResult JumpCloneBound::GetStationCloneState(PyCallArgs &call) {
    // exported by the client service but unused by the UI.  the caller's clones here, the same shape as the ship rowset
    uint32 stationID(call.client->IsDocked() ? call.client->GetStationID() : 0);
    PyRep* rs(CharacterDB::GetJumpClonesAtLocation(stationID, call.client->GetCharacterID()));
    if (rs == nullptr)
        throw CustomError("Unable to read the jump clones in this station.");
    return rs;
}

PyResult JumpCloneBound::GetPriceForClone(PyCallArgs &call) {
    /*        kwargs = {'amount': None, 'player': 140000038}
     * TypeError: Numeric Formatter expects floating point or signed integer types.
     *  AskAcceptJumpCloneCost {[numeric]cost.isk}, JumpCloneInstallationOffered {costs} (util.FmtISK)
     */
    return new PyFloat(sConfig.character.jumpCloneCost);
}

PyResult JumpCloneBound::InstallCloneInStation(PyCallArgs &call) {
    // after AskAcceptJumpCloneCost
    Client* pClient(call.client);
    if (!pClient->IsDocked())
        throw UserError("NotAtStation");
    CharacterRef cRef(pClient->GetChar());
    uint32 charID(pClient->GetCharacterID()), stationID(pClient->GetStationID());
    StationData stData = StationData();
    stDataMgr.GetStationData(stationID, stData);
    if ((stData.serviceMask & Station::JumpCloneFacility) == 0)
        throw CustomError("This station has no jump clone facility.");

    // Infomorph Psychology: one clone per level
    uint32 maxClones(cRef->GetSkillLevel(EvESkill::InfomorphPsychology));
    if (maxClones == 0)
        throw UserError("JumpCharStoringMaxClonesNone");
    uint32 count(CharacterDB::CountJumpClones(charID));
    if (count >= maxClones)
        throw UserError("JumpCharStoringMaxClones")
                .AddAmount("have", count)
                .AddAmount("max", maxClones);
    uint32 jumpCloneID(0);
    if (CharacterDB::GetJumpCloneAt(charID, stationID, jumpCloneID))
        throw UserError("JumpAlreadyHaveStationClone");

    /* Crucible: npc stations want standing 8.0 with the station's owner, personal (with Connections/Diplomacy) or corp
     *  (removed in Aegis, 2015).  outposts: the owner's corp and alliance, or the owner's standing towards the pilot
     */
    float minStanding(sConfig.character.jumpCloneMinStanding);
    uint32 ownerID(stData.corporationID);
    if (minStanding > -10.0f) {
        bool allowed(false);
        if (stData.conquerable) {
            uint32 ownerAllyID(PosMgrDB::GetCorpAllianceID(ownerID));
            if ((pClient->GetCorporationID() == (int32)ownerID)
            or ((ownerAllyID != 0) and (pClient->GetAllianceID() == (int32)ownerAllyID))) {
                allowed = true;
            } else {
                std::vector<uint32> fromIDs = {ownerID};
                if (ownerAllyID != 0)
                    fromIDs.push_back(ownerAllyID);
                std::vector<uint32> toIDs = {charID, (uint32)pClient->GetCorporationID()};
                if (pClient->GetAllianceID() != 0)
                    toIDs.push_back(pClient->GetAllianceID());
                for (auto from : fromIDs)
                    for (auto to : toIDs)
                        if (StandingDB::GetStanding(from, to) >= minStanding)
                            allowed = true;
            }
        } else {
            allowed = ((cRef->GetStandingModified(ownerID) >= minStanding)
                    or (StandingDB::GetStanding(ownerID, pClient->GetCorporationID()) >= minStanding));
        }
        if (!allowed)
            throw UserError("PortStandingCheckFail")
                    .AddFormatValue("corpName", new PyString(sDataMgr.GetCorpName(ownerID)));
    }

    std::string reason("DESC: Jump clone installation at ");
    reason += stDataMgr.GetStationName(stationID);
    AccountService::TransferFunds(charID, ownerID, sConfig.character.jumpCloneCost, reason.c_str(),
            Journal::EntryType::JumpCloneInstallationFee, stationID, Account::KeyType::Cash);

    std::vector<uint16> none;
    CharacterDB::AddJumpClone(charID, stationID, 0, none);
    _log(CHARACTER__INFO, "%s(%u) installed a jump clone in station %u.", cRef->name(), charID, stationID);

    JumpCloneService::InvalidateClientCaches(charID, stationID);
    return nullptr;
}

PyResult JumpCloneBound::DestroyInstalledClone(PyCallArgs &call, PyInt* cloneID) {
    //    lm.DestroyInstalledClone(cloneID)  after AskAreYouSure
    Client* pClient(call.client);
    uint32 charID(pClient->GetCharacterID()), ownerID(0), locationID(0), locationOwnerID(0);
    if (!CharacterDB::GetJumpClone(cloneID->value(), ownerID, locationID, locationOwnerID))
        throw UserError("JumpCloneNotFound");

    if (ownerID != charID) {
        // someone else's clone.  only the pilot of the ship it is in may destroy it  (ship clones)
        if (sDataMgr.IsStation(locationID))
            throw UserError("JumpCantDestroyCloneNoRight");
        if ((locationID != pClient->GetShipID()) or !pClient->IsInSpace())
            throw UserError("JumpCantDestroyCloneInOthersShip");
    }

    CharacterDB::DeleteJumpClone(cloneID->value());
    _log(CHARACTER__INFO, "%s(%u) destroyed jump clone %u of %u in %u.", pClient->GetName(), charID, cloneID->value(), ownerID, locationID);

    JumpCloneService::InvalidateClientCaches(ownerID, sDataMgr.IsStation(locationID) ? locationID : 0);
    return nullptr;
}

PyResult JumpCloneBound::OfferShipCloneInstallation(PyCallArgs &call, PyInt* characterID) {
    //    OfferShipCloneInstallation(charID)  //offeringCharID, targetCharID, shipID, b (b=unknown)
    throw UserError("JumpShipLacksCloneSupport");
}

PyResult JumpCloneBound::AcceptShipCloneInstallation(PyCallArgs &call) {
    //lm.AcceptShipCloneInstallation()
    throw UserError("JumpShipCloneInstallAcceptWTF");
}

PyResult JumpCloneBound::CancelShipCloneInstallation(PyCallArgs &call) {
    //lm.CancelShipCloneInstallation()
    throw UserError("JumpShipCloneInstallCancelWTF");
}

PyResult JumpCloneBound::CloneJump(PyCallArgs &call, PyInt* destLocationID, std::optional<PyBool*> okWithLosingClone) {
    /* sessionMgr.PerformSessionChange('clonejump', lm.CloneJump, destLocationID, okWithLosingClone)
     *  the body we leave becomes a jump clone here (with its implants), we wake up in the destination clone with its implants.
     *  boosters end.  a clone already installed here is destroyed (the client asks first).  our ships stay where they are
     */
    Client* pClient(call.client);
    if (!pClient->IsDocked())
        throw UserError("NotAtStation");
    CharacterRef cRef(pClient->GetChar());
    if (sConfig.character.requirePausedTraining and cRef->IsTraining())
        throw UserError("JumpSkillInTraining");

    uint32 charID(pClient->GetCharacterID()), stationID(pClient->GetStationID()), destID(destLocationID->value());
    int64 now(GetFileTimeNow());
    int64 remain(CharacterDB::GetLastCloneJumpTime(charID) + (int64)sConfig.character.jumpCloneCooldownHours * EvE::Time::Hour - now);
    if (remain > 0)
        throw UserError("JumpAttemptTooSoon")
                .AddFormatValue("hours", new PyInt(remain / EvE::Time::Hour))
                .AddFormatValue("minutes", new PyInt((remain % EvE::Time::Hour) / EvE::Time::Minute));

    uint32 destCloneID(0);
    if (!CharacterDB::GetJumpCloneAt(charID, destID, destCloneID))
        throw UserError("JumpNoCloneAtDestination");
    if (destID == stationID)
        throw UserError("JumpDestinationInvalid");
    if (!sDataMgr.IsStation(destID)) {
        if (IsPlayerItem(destID))
            throw UserError("JumpDestinationNotWilling");
        throw UserError("JumpDestinationInvalid");
    }

    // our clone already here has to go, as this body takes its place
    uint32 originCloneID(0);
    if (CharacterDB::GetJumpCloneAt(charID, stationID, originCloneID)) {
        if (!okWithLosingClone.has_value() or !okWithLosingClone.value()->value()) {
            std::vector<uint16> lost;
            CharacterDB::GetJumpCloneImplantTypes(originCloneID, lost);
            throw UserError(lost.empty() ? "JumpCheckWillLoseExistingClone" : "JumpCheckWillLoseExistingCloneAndImplants");
        }
        CharacterDB::DeleteJumpClone(originCloneID);
    }

    // swap bodies
    std::vector<uint16> mine, theirs;
    cRef->StoreImplants(mine);
    cRef->RemoveAllBoosters();
    CharacterDB::AddJumpClone(charID, stationID, 0, mine);
    CharacterDB::GetJumpCloneImplantTypes(destCloneID, theirs);
    CharacterDB::DeleteJumpClone(destCloneID);
    cRef->InstallImplants(theirs);
    CharacterDB::SetLastCloneJumpTime(charID, now);
    _log(CHARACTER__INFO, "%s(%u) clone jumped from %u to %u (%u implants left behind, %u implants found).", \
            pClient->GetName(), charID, stationID, destID, (uint32)mine.size(), (uint32)theirs.size());

    pClient->CloneJumpToStation(destID);

    JumpCloneService::InvalidateClientCaches(charID, stationID);
    JumpCloneService::InvalidateClientCaches(charID, destID);
    return nullptr;
}
