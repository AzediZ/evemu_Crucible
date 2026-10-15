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

#include "EVE_Mail.h"
#include "EVE_Station.h"

#include "EntityList.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "character/Character.h"
#include "character/CharacterDB.h"
#include "mail/NotificationMgr.h"
#include "pos/PosMgrDB.h"
#include "standing/StandingDB.h"
#include "station/JumpCloneService.h"
#include "station/StationDataMgr.h"
#include "services/ServiceManager.h"
#include "station/Outpost.h"

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

void JumpCloneService::InvalidateShipCache(uint32 shipID, uint32 charID)
{
    ShipItemRef sRef(sItemFactory.GetShipRef(shipID));
    if ((sRef.get() == nullptr) or !sRef->HasPilot() or (sRef->GetPilot()->GetShipID() != shipID))
        return;
    PyTuple* tmp = new PyTuple(2);
        tmp->SetItem(0, new PyInt(shipID));
        tmp->SetItem(1, new PyInt(charID));
    sRef->GetPilot()->SendNotification("OnShipJumpCloneCacheInvalidated", "charid", &tmp, false);
}

void JumpCloneService::DestroyClone(uint32 jumpCloneID, uint32 destroyerID)
{
    uint32 ownerID(0), locationID(0), locationOwnerID(0);
    if (!CharacterDB::GetJumpClone(jumpCloneID, ownerID, locationID, locationOwnerID))
        return;
    std::vector<uint16> implants;
    CharacterDB::GetJumpCloneImplantTypes(jumpCloneID, implants);
    CharacterDB::DeleteJumpClone(jumpCloneID);

    // "the jump clone ... was destroyed, along with these implants"  (notificationUtil ParamJumpCloneDeleted1/2Notification)
    PyList* typeIDs = new PyList();
    for (auto type : implants)
        typeIDs->AddItem(new PyInt(type));
    PyDict* data = new PyDict();
        data->SetItemString("locationID", new PyInt(locationID));
        data->SetItemString("locationOwnerID", new PyInt(locationOwnerID));
        data->SetItemString("typeIDs", typeIDs);
    if (destroyerID != 0)
        data->SetItemString("destroyerID", new PyInt(destroyerID));
    NotificationMgr::Send(ownerID, (destroyerID != 0) ? Notify::Types::JumpCloneDeleted2 : Notify::Types::JumpCloneDeleted1,
            (destroyerID != 0) ? destroyerID : locationOwnerID, data);
    InvalidateClientCaches(ownerID);
    _log(CHARACTER__INFO, "Jump clone %u of %u in %u destroyed by %u.", jumpCloneID, ownerID, locationID, destroyerID);
}

void JumpCloneService::DestroyClonesAt(uint32 locationID, uint32 destroyerID)
{
    std::vector<uint32> cloneIDs;
    CharacterDB::GetJumpCloneIDsAtLocation(locationID, cloneIDs);
    for (auto cur : cloneIDs)
        DestroyClone(cur, destroyerID);
}

JumpCloneService::ShipCloneOffer* JumpCloneService::FindOffer(uint32 charID)
{
    // offers of characters that went offline (or left the ship) are dropped
    std::map<uint32, ShipCloneOffer>::iterator itr = m_offers.begin();
    while (itr != m_offers.end()) {
        Client* pPilot(sEntityList.FindClientByCharID(itr->second.pilotID));
        if ((pPilot == nullptr) or (sEntityList.FindClientByCharID(itr->second.targetID) == nullptr)
        or (pPilot->GetShipID() != itr->second.shipID)) {
            itr = m_offers.erase(itr);
        } else {
            ++itr;
        }
    }
    for (auto& cur : m_offers)
        if ((cur.second.pilotID == charID) or (cur.second.targetID == charID))
            return &cur.second;
    return nullptr;
}

void JumpCloneService::SendOfferNotification(const char* notifyType, uint32 charID, const ShipCloneOffer& offer)
{
    Client* pClient(sEntityList.FindClientByCharID(charID));
    if (pClient == nullptr)
        return;
    // one positional arg:  args = (offeringCharID, targetCharID, shipID, b)
    PyTuple* args = new PyTuple(4);
        args->SetItem(0, new PyInt(offer.pilotID));
        args->SetItem(1, new PyInt(offer.targetID));
        args->SetItem(2, new PyInt(offer.shipID));
        args->SetItem(3, PyStatic.NewNone());
    PyTuple* tmp = new PyTuple(1);
        tmp->SetItem(0, args);
    pClient->SendNotification(notifyType, "charid", &tmp, false);
}

// the ship pClient pilots in space, if it can take ship clones and its Clone Vat Bay is active.  throws the client's errors
static ShipItemRef GetCloneShip(Client* pClient)
{
    if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr))
        throw UserError("JumpShipLacksCloneSupport");
    ShipItemRef sRef(pClient->GetShip());
    if (sRef->GetAttribute(AttrMaxJumpClones) < EvilOne)
        throw UserError("JumpShipLacksCloneSupport");
    if (sRef->GetModuleManager()->GetActiveModuleByGroup(EVEDB::invGroups::Clone_Vat_Bay) == nullptr)
        throw UserError("JumpShipLacksActiveCloneVatBay");
    return sRef;
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
    if (m_svc.FindOffer(charID) != nullptr)
        throw UserError("JumpNoStationCloneWhenDoingShipOne");
    StationData stData = StationData();
    stDataMgr.GetStationData(stationID, stData);
    /* no station has the jump clone facility bit in the Crucible data (staOperationServices), and the client offers the
     *  install in the medical window, which needs a clone bay (base.py:  medical's serviceIDs).  the station management
     *  window adds the bit to every station (base_stationmanagement.GetStationServiceMask).  so cloning is what counts */
    if ((stData.serviceMask & (Station::JumpCloneFacility | Station::Cloning)) == 0)
        throw CustomError("This station has no jump clone facility.");
    OutpostSE::CheckServiceOnline(stationID, Station::Cloning);

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

    if (ownerID != charID) {
        // the owner learns who did it (JumpCloneDeleted2)
        JumpCloneService::DestroyClone(cloneID->value(), charID);
        JumpCloneService::InvalidateShipCache(locationID, ownerID);
        return nullptr;
    }

    CharacterDB::DeleteJumpClone(cloneID->value());
    _log(CHARACTER__INFO, "%s(%u) destroyed jump clone %u in %u.", pClient->GetName(), charID, cloneID->value(), locationID);

    JumpCloneService::InvalidateClientCaches(ownerID, sDataMgr.IsStation(locationID) ? locationID : 0);
    if (!sDataMgr.IsStation(locationID))
        JumpCloneService::InvalidateShipCache(locationID, ownerID);
    return nullptr;
}

PyResult JumpCloneBound::OfferShipCloneInstallation(PyCallArgs &call, PyInt* characterID) {
    /* OfferShipCloneInstallation(charID): the pilot of a ship with an active Clone Vat Bay (titans, Rorqual) offers a clone
     *  installation to a pilot nearby, who gets OnShipJumpCloneInstallationOffered and accepts (paying the fee) or cancels
     */
    Client* pClient(call.client);
    uint32 pilotID(pClient->GetCharacterID()), targetID(characterID->value());
    ShipItemRef sRef(GetCloneShip(pClient));
    uint32 maxClones(sRef->GetAttribute(AttrMaxJumpClones).get_uint32());
    uint32 count(CharacterDB::CountJumpClonesAtLocation(sRef->itemID()));
    if (count >= maxClones)
        throw UserError("JumpShipStoringMaxClones")
                .AddAmount("have", count)
                .AddAmount("max", maxClones);

    Client* pTarget(sEntityList.FindClientByCharID(targetID));
    if ((pTarget == nullptr) or (targetID == pilotID) or !pTarget->IsInSpace() or (pTarget->GetShipSE() == nullptr)
    or (pTarget->GetSystemID() != pClient->GetSystemID()))
        throw UserError("JumpInstalleeNotPresent");
    // surface distance
    double maxDistance(sRef->GetAttribute(AttrMaxOperationalDistance).get_double());
    double distance(pClient->GetShipSE()->GetPosition().distance(pTarget->GetShipSE()->GetPosition())
            - pClient->GetShipSE()->GetRadius() - pTarget->GetShipSE()->GetRadius());
    if (distance > maxDistance)
        throw UserError("JumpInstalleeTooDistant")
                .AddFormatValue("distance", new PyInt((int32)maxDistance));
    uint32 jumpCloneID(0);
    if (CharacterDB::GetJumpCloneAt(targetID, sRef->itemID(), jumpCloneID))
        throw UserError("JumpOtherAlreadyHasShipClone")
                .AddFormatValue("other", new PyInt(targetID));
    JumpCloneService::ShipCloneOffer* pOffer(m_svc.FindOffer(pilotID));
    if (pOffer != nullptr)
        throw UserError("JumpNoShipCloneWhenDoingShipOne")
                .AddFormatValue("other", new PyInt((pOffer->pilotID == pilotID) ? pOffer->targetID : pOffer->pilotID));
    if (m_svc.FindOffer(targetID) != nullptr)
        throw UserError("JumpNoShipCloneForBusyChar")
                .AddFormatValue("other", new PyInt(targetID));

    JumpCloneService::ShipCloneOffer offer = JumpCloneService::ShipCloneOffer();
        offer.pilotID = pilotID;
        offer.targetID = targetID;
        offer.shipID = sRef->itemID();
        offer.accepted = false;
    m_svc.AddOffer(offer);
    JumpCloneService::SendOfferNotification("OnShipJumpCloneInstallationOffered", targetID, offer);
    return nullptr;
}

PyResult JumpCloneBound::AcceptShipCloneInstallation(PyCallArgs &call) {
    // lm.AcceptShipCloneInstallation(), after JumpCloneInstallationOffered {offerer, shipname, costs}
    Client* pClient(call.client);
    uint32 charID(pClient->GetCharacterID());
    JumpCloneService::ShipCloneOffer* pOffer(m_svc.FindOffer(charID));
    if ((pOffer == nullptr) or (pOffer->targetID != charID))
        throw UserError("JumpShipCloneInstallAcceptWTF");
    if (pOffer->accepted)
        throw UserError("JumpShipCloneInstallAcceptTooLate");
    JumpCloneService::ShipCloneOffer offer(*pOffer);

    // anything failing from here ends the offer for both sides
    try {
        uint32 maxClones(pClient->GetChar()->GetSkillLevel(EvESkill::InfomorphPsychology));
        if (maxClones == 0)
            throw UserError("JumpCharStoringMaxClonesNone");
        uint32 count(CharacterDB::CountJumpClones(charID));
        if (count >= maxClones)
            throw UserError("JumpCharStoringMaxClones")
                    .AddAmount("have", count)
                    .AddAmount("max", maxClones);
        Client* pPilot(sEntityList.FindClientByCharID(offer.pilotID));
        if (pPilot == nullptr)
            throw UserError("JumpShipCloneInstallAcceptWTF");
        ShipItemRef sRef(GetCloneShip(pPilot));
        uint32 shipMax(sRef->GetAttribute(AttrMaxJumpClones).get_uint32());
        uint32 shipCount(CharacterDB::CountJumpClonesAtLocation(offer.shipID));
        if (shipCount >= shipMax)
            throw UserError("JumpShipStoringMaxClones")
                    .AddAmount("have", shipCount)
                    .AddAmount("max", shipMax);

        pOffer->accepted = true;
        // the fee goes to the pilot of the ship
        std::string reason("DESC: Jump clone installation in ");
        reason += sRef->itemName();
        AccountService::TransferFunds(charID, offer.pilotID, sConfig.character.jumpCloneCost, reason.c_str(),
                Journal::EntryType::JumpCloneInstallationFee, offer.shipID, Account::KeyType::Cash);
    } catch (...) {
        m_svc.RemoveOffer(offer.pilotID);
        JumpCloneService::SendOfferNotification("OnShipJumpCloneInstallationCanceled", offer.pilotID, offer);
        throw;
    }

    std::vector<uint16> none;
    CharacterDB::AddJumpClone(charID, offer.shipID, offer.pilotID, none);
    m_svc.RemoveOffer(offer.pilotID);
    _log(CHARACTER__INFO, "%s(%u) installed a jump clone in ship %u of %u.", pClient->GetName(), charID, offer.shipID, offer.pilotID);

    JumpCloneService::SendOfferNotification("OnShipJumpCloneInstallationDone", offer.pilotID, offer);
    JumpCloneService::SendOfferNotification("OnShipJumpCloneInstallationDone", charID, offer);
    JumpCloneService::InvalidateShipCache(offer.shipID, charID);
    JumpCloneService::InvalidateClientCaches(charID);
    return nullptr;
}

PyResult JumpCloneBound::CancelShipCloneInstallation(PyCallArgs &call) {
    /* lm.CancelShipCloneInstallation(), from either side.  the other side's client calls it again on the
     *  OnShipJumpCloneInstallationCanceled it gets, and ignores the WTF error
     */
    uint32 charID(call.client->GetCharacterID());
    JumpCloneService::ShipCloneOffer* pOffer(m_svc.FindOffer(charID));
    if (pOffer == nullptr)
        throw UserError("JumpShipCloneInstallCancelWTF");
    if (pOffer->accepted)
        throw UserError("JumpShipCloneInstallCancelTooLate");
    JumpCloneService::ShipCloneOffer offer(*pOffer);
    m_svc.RemoveOffer(offer.pilotID);
    JumpCloneService::SendOfferNotification("OnShipJumpCloneInstallationCanceled",
            (offer.pilotID == charID) ? offer.targetID : offer.pilotID, offer);
    return nullptr;
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
    bool ok(okWithLosingClone.has_value() and okWithLosingClone.value()->value());
    ShipSE* pDestShip(nullptr);
    if (!sDataMgr.IsStation(destID)) {
        if (!IsPlayerItem(destID))
            throw UserError("JumpDestinationInvalid");
        // a ship clone: the ship must be in space with its pilot and an active Clone Vat Bay
        ShipItemRef sRef(sItemFactory.GetShipRef(destID));
        if ((sRef.get() == nullptr) or !sRef->HasPilot())
            throw UserError("JumpDestinationNotWilling");
        Client* pPilot(sRef->GetPilot());
        if ((pPilot->GetShipID() != destID) or !pPilot->IsInSpace() or (pPilot->GetShipSE() == nullptr)
        or (sRef->GetModuleManager()->GetActiveModuleByGroup(EVEDB::invGroups::Clone_Vat_Bay) == nullptr))
            throw UserError("JumpDestinationNotWilling");
        if (IsWSpaceID(pPilot->GetSystemID()))
            throw UserError("JumpDestinationIsWormholeSpace");
        pDestShip = pPilot->GetShipSE();
    }

    // our clone already here has to go, as this body takes its place
    uint32 originCloneID(0);
    if (CharacterDB::GetJumpCloneAt(charID, stationID, originCloneID)) {
        if (!ok) {
            std::vector<uint16> lost;
            CharacterDB::GetJumpCloneImplantTypes(originCloneID, lost);
            throw UserError(lost.empty() ? "JumpCheckWillLoseExistingClone" : "JumpCheckWillLoseExistingCloneAndImplants");
        }
        CharacterDB::DeleteJumpClone(originCloneID);
    }
    // into a ship we wake up in a pod in space
    if ((pDestShip != nullptr) and !ok)
        throw UserError("JumpCheckIntoShip");

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

    if (pDestShip != nullptr) {
        pClient->CloneJumpToShip(pDestShip);
        JumpCloneService::InvalidateShipCache(destID, charID);
    } else {
        pClient->CloneJumpToStation(destID);
    }

    JumpCloneService::InvalidateClientCaches(charID, stationID);
    if (pDestShip == nullptr)
        JumpCloneService::InvalidateClientCaches(charID, destID);
    return nullptr;
}
