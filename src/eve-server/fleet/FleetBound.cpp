
 /**
  * @name FleetBound.cpp
  *     Fleet Bound code for EVEmu
  *     This code handles specific fleet functions
  *
  * @Author:        Allan
  * @date:          05 August 2014 (original skeleton outline)
  * @update:        21 November 2017 (begin actual implementation)
  *
  */


//work in progress

#include "eve-server.h"



#include "fleet/FleetBound.h"
#include "fleet/FleetObject.h"
#include "services/ServiceManager.h"

/*
FLEET__ERROR
FLEET__WARNING
FLEET__MESSAGE
FLEET__DEBUG
FLEET__INFO
FLEET__TRACE
FLEET__DUMP
FLEET__BIND_DUMP
*/

FleetBound::FleetBound(EVEServiceManager& mgr, FleetObject& parent, uint32 fleetID) :
    EVEBoundObject(mgr, parent),
    m_fleetID (fleetID)
{
    this->Add("Init", &FleetBound::Init);
    this->Add("GetInitState", &FleetBound::GetInitState);
    this->Add("GetFleetID", &FleetBound::GetFleetID);
    this->Add("Invite", &FleetBound::Invite);
    this->Add("AcceptInvite", &FleetBound::AcceptInvite);
    this->Add("RejectInvite", &FleetBound::RejectInvite);
    this->Add("ChangeWingName", &FleetBound::ChangeWingName);
    this->Add("ChangeSquadName", &FleetBound::ChangeSquadName);
    this->Add("SetOptions", &FleetBound::SetOptions);
    this->Add("GetWings", &FleetBound::GetWings);
    this->Add("MakeLeader", &FleetBound::MakeLeader);
    this->Add("SetBooster", &FleetBound::SetBooster);
    this->Add("MoveMember", &FleetBound::MoveMember);
    this->Add("KickMember", &FleetBound::KickMember);
    this->Add("CreateWing", &FleetBound::CreateWing);
    this->Add("CreateSquad", &FleetBound::CreateSquad);
    this->Add("DeleteWing", &FleetBound::DeleteWing);
    this->Add("DeleteSquad", &FleetBound::DeleteSquad);
    this->Add("LeaveFleet", &FleetBound::LeaveFleet);
    this->Add("GetFleetComposition", &FleetBound::GetFleetComposition);
    this->Add("GetJoinRequests", &FleetBound::GetJoinRequests);
    this->Add("RejectJoinRequest", &FleetBound::RejectJoinRequest);
    this->Add("SendBroadcast", &FleetBound::SendBroadcast);
    this->Add("SetMotdEx", &FleetBound::SetMotdEx);
    this->Add("GetMotd", &FleetBound::GetMotd);
    this->Add("UpdateMemberInfo", &FleetBound::UpdateMemberInfo);

    // stubs
    this->Add("Reconnect", &FleetBound::Reconnect);
    this->Add("AddToVoiceChat", &FleetBound::AddToVoiceChat);
    this->Add("SetVoiceMuteStatus", &FleetBound::SetVoiceMuteStatus);
    this->Add("ExcludeFromVoiceMute", &FleetBound::ExcludeFromVoiceMute);
}

Character* FleetBound::CheckMember(PyCallArgs& call)
{
    if (call.client == nullptr)
        throw UserError("FleetNotInFleet");
    Character* pChar = call.client->GetChar().get();
    if ((pChar == nullptr) or (pChar->fleetID() != (int32)m_fleetID))
        throw UserError("FleetNotInFleet");
    return pChar;
}

Character* FleetBound::GetTargetChar(uint32 charID, Client*& pClient)
{
    pClient = sFltSvc.GetMember(m_fleetID, charID);
    if (pClient == nullptr)
        throw UserError("FleetMemberNotFound");
    Character* pChar = pClient->GetChar().get();
    if (pChar == nullptr)
        throw UserError("FleetMemberNotFound");
    return pChar;
}

bool FleetBound::IsBossOrFC(Character* pChar)
{
    return (sFltSvc.IsBoss(pChar) or (pChar->fleetRole() == Fleet::Role::FleetLeader));
}

PyResult FleetBound::GetFleetID(PyCallArgs &call) {
    return new PyInt(m_fleetID);
}

PyResult FleetBound::Init(PyCallArgs &call, std::optional <PyInt*> shipTypeID) {
    //self.fleet.Init(self.GetMyShipTypeID())
    // this only sends ship typeID when inspace.
    sLog.Warning("FleetBound", "Handle_Init() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

/*
    FleetInitCall args;
    if (!args.Decode(&call.tuple)) {
        codelog(SERVICE__ERROR, "Failed to decode bind args from '%s'", call.client->GetName());
        // this should throw an error here...
        return nullptr;
    }
    args.myShipTypeID
    args.unknownDict
    */

    Client* pClient = call.client;
    Character* pChar = CheckMember(call);

    // member is already added here, so cannot use sFltSvc.AddMember()
    JoinFleetRSP join;
        join.charID = pClient->GetCharacterID();
        join.clientID = pClient->GetClientID();
        join.job = pChar->fleetJob();
        join.role = pChar->fleetRole();
        join.shipTypeID = pClient->GetShip()->typeID();
        join.skillFleetCommand = pChar->GetSkillLevel(EvESkill::FleetCommand);
        join.skillLeadership = pChar->GetSkillLevel(EvESkill::Leadership);
        join.skillWingCommand = pChar->GetSkillLevel(EvESkill::WingCommand);
        join.squadID = pChar->squadID();
        join.roleBooster = pChar->fleetBooster();
        join.solarSystemID = pClient->GetSystemID();
        join.wingID = pChar->wingID();
        join.timestamp = pChar->fleetJoinTime();
    PyTuple* res = new PyTuple(1);
        res->SetItem(0, join.Encode());
    if (is_log_enabled(FLEET__DEBUG)) {
        _log(FLEET__UPDATE_DUMP, "OnFleetJoin");
        res->Dump(FLEET__UPDATE_DUMP, "   ");
    }
    pClient->SendNotification("OnFleetJoin", "clientid", &res, true);

    PyTuple* obj = new PyTuple(1);
        obj->SetItem(0, sFltSvc.MakeFleetState(m_fleetID));
    _log(FLEET__UPDATE_DUMP, "OnFleetStateChange");
    obj->Dump(FLEET__UPDATE_DUMP, "   ");
    pClient->SendNotification("OnFleetStateChange", "clientid", obj, true);

    PyTuple* count = new PyTuple(1);
        count->SetItem(0, new PyInt(254));
    _log(FLEET__UPDATE_DUMP, "OnFleetActive");
    count->Dump(FLEET__UPDATE_DUMP, "   ");
    pClient->SendNotification("OnFleetActive", "clientid", count, true);

    //response should be OID
    /*
    [PyDict 1 kvp]
      [PyString "OID+"]
      [PyDict 1 kvp]
        [PyString "N=790423:223898"]  "N=%u:%u", &nodeID, &bindID
        [PyIntegerVar 129756560170416597]
        */
    return new PyLong(GetFileTimeNow());
}

PyResult FleetBound::GetInitState(PyCallArgs &call) {
    CheckMember(call);

    FleetData fData = FleetData();
    sFltSvc.GetFleetData(m_fleetID, fData);

    GetInitStateRSP rsp;
        rsp.fleetID = m_fleetID;
        rsp.isFreeMove = fData.isFreeMove;
        rsp.isLootLogging = fData.isLootLogging;
        rsp.isRegistered = fData.isRegistered;
        rsp.isVoiceEnabled = fData.isVoiceEnabled;
        rsp.motd = fData.motd;
        rsp.name = fData.name;

    PyDict* muteDict = new PyDict();
    for (auto cur : fData.isExcludedFromMuting)
        muteDict->SetItem(new PyInt(cur.first), new PyInt(cur.second));
    PySafeDecRef(rsp.isExcludedFromMuting);
    rsp.isExcludedFromMuting = muteDict;

    PyDict* lMuteDict = new PyDict();
    for (auto cur : fData.isMutedByLeader)
        lMuteDict->SetItem(new PyInt(cur.first), new PyInt(cur.second));
    PySafeDecRef(rsp.isMutedByLeader);
    rsp.isMutedByLeader = lMuteDict;

    std::vector< uint32 > wingIDs, squadIDs;
    wingIDs.clear();
    sFltSvc.GetWingIDs(m_fleetID, wingIDs);
    PyDict* dict1 = new PyDict();
    for (auto wingID : wingIDs) {
        WingData wData = WingData();
        sFltSvc.GetWingData(wingID, wData);
        WingRSP wing;
        wing.name = wData.name;
        wing.wingID = wingID;

        squadIDs.clear();
        sFltSvc.GetSquadIDs(wingID, squadIDs);

        PyDict* dict = new PyDict();
        for (auto squadID : squadIDs) {
            SquadData sData = SquadData();
            sFltSvc.GetSquadData(squadID, sData);
            SquadRSP squad;
            squad.name = sData.name;
            squad.squadID = squadID;
            dict->SetItem(new PyInt(squadID), squad.Encode());
        }
        PySafeDecRef(wing.squads);
        wing.squads = dict;
        dict1->SetItem(new PyInt(wingID), wing.Encode());
    }
    PySafeDecRef(rsp.wings);
    rsp.wings = dict1;

    std::vector<Client*> members;
    sFltSvc.GetMemeberVec(m_fleetID, members);
    PyDict* dict2 = new PyDict();
    for (auto cur : members) {
        if ((cur == nullptr) or (cur->GetChar().get() == nullptr))
            continue;
        MemberRSP member;
            member.charID = cur->GetCharacterID();
            member.clientID = cur->GetClientID();
            member.job = cur->GetChar()->fleetJob();
            member.role = cur->GetChar()->fleetRole();
            member.shipTypeID = cur->GetShip()->typeID();
            member.skillFleetCommand = cur->GetChar()->GetSkillLevel(EvESkill::FleetCommand);
            member.skillLeadership = cur->GetChar()->GetSkillLevel(EvESkill::Leadership);
            member.skillWingCommand = cur->GetChar()->GetSkillLevel(EvESkill::WingCommand);
            member.squadID = cur->GetChar()->squadID();
            member.roleBooster = cur->GetChar()->fleetBooster();
            member.solarSystemID = cur->GetSystemID();
            member.wingID = cur->GetChar()->wingID();
            member.timestamp = cur->GetChar()->fleetJoinTime();
        dict2->SetItem(new PyInt(member.charID), member.Encode());
    }

    rsp.members = dict2;
    if (is_log_enabled(FLEET__DEBUG))
        rsp.Dump(FLEET__DEBUG);

    //response should be sequenced
    return rsp.Encode();
}

PyResult FleetBound::Invite(PyCallArgs &call, PyInt* characterID, std::optional<PyInt*> wingID, std::optional <PyInt*> squadID, std::optional <PyInt*> role) {
    sLog.Warning("FleetBound", "Handle_Invite() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    // boss and commanders may invite (menusvc ImFleetLeaderOrCommander)
    Character* pMe = CheckMember(call);
    bool all = IsBossOrFC(pMe);
    bool isWC = (pMe->fleetRole() == Fleet::Role::WingLeader);
    bool isSC = (pMe->fleetRole() == Fleet::Role::SquadLeader);
    if (!all and !isWC and !isSC)
        throw UserError("FleetNotAllowed");

    uint32 charID = characterID->value();
    if (charID == call.client->GetCharacterID())
        throw UserError("FleetMemberAlreadyInGroup");
    Client* pClient = sEntityList.FindClientByCharID(charID);
    if ((pClient == nullptr) or (pClient->GetChar().get() == nullptr))
        throw UserError("FleetCandidateOffline").AddOwnerName("invitee", charID);
    if (pClient->GetChar()->fleetID() == (int32)m_fleetID)
        throw UserError("FleetMemberAlreadyInGroup");
    if (pClient->GetChar()->fleetID() != 0)
        throw CustomError("%s is already in another fleet.", pClient->GetName());
    InviteData pending = InviteData();
    if (sFltSvc.GetInviteData(charID, pending))
        throw UserError("FleetMemberAlreadyInvited");
    if (sFltSvc.GetFleetMemberCount(m_fleetID) >= Fleet::Limits::MaxMembers)
        throw UserError("FleetTooManyMembers").AddAmount("num", (int)Fleet::Limits::MaxMembers);

    InviteData data = InviteData();
        data.fleetID = m_fleetID;
        data.inviteBy = call.client;
        data.invited = pClient;
        data.wingID = wingID.has_value() ? wingID.value()->value() : 0;  // client sends None for 'any'
        data.squadID = squadID.has_value() ? squadID.value()->value() : 0;
        data.role = role.has_value () ? role.value ()->value () : Fleet::Role::Member; // member as default
    if (data.wingID < 0)
        data.wingID = 0;
    if (data.squadID < 0)
        data.squadID = 0;

    switch (data.role) {
        case Fleet::Role::FleetLeader: {
            if (!all)
                throw UserError("FleetNotAllowed");
            data.wingID = -1;
            data.squadID = -1;
        } break;
        case Fleet::Role::WingLeader: {
            // only the boss/FC place wing commanders
            if (!all)
                throw UserError("FleetNotAllowed");
            if ((data.wingID <= 0) or !sFltSvc.IsWingInFleet(data.wingID, m_fleetID))
                throw UserError("FleetNotAllowed");
            data.squadID = -1;
        } break;
        case Fleet::Role::SquadLeader: {
            if ((data.squadID <= 0) or !sFltSvc.IsSquadInFleet(data.squadID, m_fleetID))
                throw UserError("FleetNotAllowed");
            SquadData sData = SquadData();
            sFltSvc.GetSquadData(data.squadID, sData);
            data.wingID = sData.wingID;
            // a wing commander places squad commanders in his own wing.  squad commanders invite members only
            if (!all and !(isWC and (data.wingID == pMe->wingID())))
                throw UserError("FleetNotAllowed");
            if (sFltSvc.GetSquadMemberCount(data.squadID) >= Fleet::Limits::MaxSquadMembers)
                throw UserError("FleetTooManyMembersInSquad").AddAmount("num", (int)Fleet::Limits::MaxSquadMembers);
        } break;
        case Fleet::Role::Member: {
            // wing/squad commanders place members inside their own unit
            int32 restrictWing = (isWC and !all) ? pMe->wingID() : 0;
            int32 restrictSquad = (isSC and !all) ? pMe->squadID() : 0;
            if (data.squadID > 0) {
                if (!sFltSvc.IsSquadInFleet(data.squadID, m_fleetID))
                    throw UserError("FleetNotAllowed");
                SquadData sData = SquadData();
                sFltSvc.GetSquadData(data.squadID, sData);
                data.wingID = sData.wingID;
                if (((restrictWing > 0) and (data.wingID != restrictWing))
                or ((restrictSquad > 0) and (data.squadID != restrictSquad)))
                    throw UserError("FleetNotAllowed");
                if (sFltSvc.GetSquadMemberCount(data.squadID) >= Fleet::Limits::MaxSquadMembers)
                    throw UserError("FleetTooManyMembersInSquad").AddAmount("num", (int)Fleet::Limits::MaxSquadMembers);
            } else {
                if (data.wingID > 0) {
                    if (!sFltSvc.IsWingInFleet(data.wingID, m_fleetID))
                        throw UserError("FleetNotAllowed");
                    if ((restrictWing > 0) and (data.wingID != restrictWing))
                        throw UserError("FleetNotAllowed");
                    restrictWing = data.wingID;
                }
                // char invited as generic squad member, no specific position
                if (!sFltSvc.GetRandUnitIDs(m_fleetID, data.wingID, data.squadID, restrictWing, restrictSquad))
                    throw UserError("FleetNoPositionFound");
            }
        } break;
        default: {
            _log(FLEET__ERROR, "Fleet Invite Role/UnitID error.  Role: %s, WingID: %i, SquadID: %i", sFltSvc.GetRoleName(data.role).c_str(), data.wingID, data.squadID);
            throw UserError("FleetNotAllowed");
        }
    }

    // commander slots must be free, and not promised to someone else
    if (data.role != Fleet::Role::Member) {
        Client* pHolder = sFltSvc.GetPositionHolder(m_fleetID, data.wingID, data.squadID, data.role);
        if (pHolder != nullptr)
            throw UserError("FleetPositionFilled").AddOwnerName("owner", pHolder->GetCharacterID());
        uint32 pendingID = sFltSvc.GetPendingInvite(m_fleetID, data.wingID, data.squadID, data.role);
        if (pendingID != 0)
            throw UserError("FleetPositionFilledInvite").AddOwnerName("owner", pendingID);
    }

    _log(FLEET__MESSAGE, "Fleet Invite:  Role: %s, WingID: %i, SquadID: %i", sFltSvc.GetRoleName(data.role).c_str(), data.wingID, data.squadID);

    if (!sFltSvc.SaveInviteData(charID, data))
        throw UserError("FleetMemberAlreadyInvited");

    // join requests are accepted via invite.  delete request on invite
    sFltSvc.RemoveJoinRequest(m_fleetID, pClient);

    PyTuple* tuple = new PyTuple(4);
        tuple->SetItem(0, new PyInt(m_fleetID));
        tuple->SetItem(1, new PyInt(data.inviteBy->GetCharacterID()));
        tuple->SetItem(2, new PyString("AskJoinFleet"));
    PyTuple* tuple2 = new PyTuple(2);
        tuple2->SetItem(0, new PyInt(call.client->GetChar()->fleetJob()));
        tuple2->SetItem(1, new PyInt(data.inviteBy->GetCharacterID()));
    PyDict* dict = new PyDict();
        dict->SetItemString("name", tuple2);
        tuple->SetItem(3, dict);
    if (is_log_enabled(FLEET__UPDATE_DUMP)) {
        _log(FLEET__UPDATE_DUMP, "OnFleetInvite");
        tuple->Dump(FLEET__UPDATE_DUMP, "   ");
    }
    pClient->SendNotification("OnFleetInvite", "clientID", &tuple, true);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::AcceptInvite(PyCallArgs &call, std::optional <PyInt*> shipTypeID) {
    sLog.Warning("FleetBound", "Handle_AcceptInvite() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pChar = call.client->GetChar().get();
    if (pChar == nullptr)
        return new PyLong(GetFileTimeNow());

    InviteData data = InviteData();
    if (!sFltSvc.GetInviteData(pChar->itemID(), data))
        throw UserError("FleetNotInvited");
    // the invite must be for this (bound) fleet
    if (data.fleetID != m_fleetID)
        throw UserError("FleetNotInvited");
    if (pChar->fleetID() != 0) {
        sFltSvc.RemoveInviteData(pChar->itemID());
        throw UserError("FleetYouAreAlreadyInFleet");
    }

    // the fleet may have changed since the invite was issued
    if (sFltSvc.GetFleetMemberCount(m_fleetID) >= Fleet::Limits::MaxMembers) {
        sFltSvc.RemoveInviteData(pChar->itemID());
        throw UserError("FleetTooManyMembers").AddAmount("num", (int)Fleet::Limits::MaxMembers);
    }
    if (data.role != Fleet::Role::Member) {
        Client* pHolder = sFltSvc.GetPositionHolder(m_fleetID, data.wingID, data.squadID, data.role);
        if ((pHolder != nullptr)
        or ((data.role == Fleet::Role::WingLeader) and !sFltSvc.IsWingInFleet(data.wingID, m_fleetID))
        or ((data.role == Fleet::Role::SquadLeader) and !sFltSvc.IsSquadInFleet(data.squadID, m_fleetID))) {
            // position gone; join as a member instead
            data.role = Fleet::Role::Member;
            data.wingID = 0;
            data.squadID = 0;
        }
    }
    if (data.role == Fleet::Role::Member) {
        if ((data.squadID <= 0) or !sFltSvc.IsSquadInFleet(data.squadID, m_fleetID)
        or (sFltSvc.GetSquadMemberCount(data.squadID) >= Fleet::Limits::MaxSquadMembers))
            if (!sFltSvc.GetRandUnitIDs(m_fleetID, data.wingID, data.squadID)) {
                sFltSvc.RemoveInviteData(pChar->itemID());
                throw UserError("FleetNoPositionFound");
            }
    } else if ((data.role == Fleet::Role::SquadLeader) and (sFltSvc.GetSquadMemberCount(data.squadID) >= Fleet::Limits::MaxSquadMembers)) {
        sFltSvc.RemoveInviteData(pChar->itemID());
        throw UserError("FleetTooManyMembersInSquad").AddAmount("num", (int)Fleet::Limits::MaxSquadMembers);
    }

    int8 booster = Fleet::Booster::No;
    switch (data.role) {
        case Fleet::Role::FleetLeader: {
            FleetData fData = FleetData();
            sFltSvc.GetFleetData(m_fleetID, fData);
            if (fData.booster == nullptr)
                booster = Fleet::Booster::Fleet;
        } break;
        case Fleet::Role::WingLeader: {
            WingData wData = WingData();
            sFltSvc.GetWingData(data.wingID, wData);
            if (wData.booster == nullptr)
                booster = Fleet::Booster::Wing;
        } break;
        case Fleet::Role::SquadLeader: {
            SquadData sData = SquadData();
            sFltSvc.GetSquadData(data.squadID, sData);
            if (sData.booster == nullptr)
                booster = Fleet::Booster::Squad;
        } break;
    }

    sFltSvc.RemoveInviteData(pChar->itemID());
    sFltSvc.AddMember(call.client, m_fleetID, data.wingID, data.squadID, Fleet::Job::None, data.role, booster);

    // returns nodeID and timestamp
    return this->GetOID();
}

PyResult FleetBound::RejectInvite(PyCallArgs &call, std::optional <PyBool*> inFleet) {
    sLog.Warning("FleetBound", "Handle_RejectInvite() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pChar = call.client->GetChar().get();
    if (pChar == nullptr)
        return new PyLong(GetFileTimeNow());

    // fleetSvc.OnFleetInvite: RejectInvite(True) when already in a fleet, (False) on auto-reject, () on a manual 'no'
    bool alreadyInFleet = inFleet.has_value() ? inFleet.value()->value() : false;

    // the client calls this unprompted (auto-reject), so a missing invite is not an error
    InviteData data = InviteData();
    if (!sFltSvc.GetInviteData(pChar->itemID(), data) or (data.fleetID != m_fleetID))
        return nullptr;

    if (data.inviteBy != nullptr) {
        // OnFleetJoinReject(memberID, reasonCode)  reason: const.rejectFleetInviteAlreadyInFleet = 2
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, new PyInt(pChar->itemID()));
            tuple->SetItem(1, new PyInt(alreadyInFleet ? 2 : 0));
        data.inviteBy->SendNotification("OnFleetJoinReject", "clientID", &tuple, false);
    }
    sFltSvc.RemoveInviteData(pChar->itemID());

    // returns nodeID and timestamp
    return this->GetOID();
}


PyResult FleetBound::ChangeWingName(PyCallArgs &call, PyInt* wingID, PyRep* name) {
          //   self.fleet.ChangeWingName(wingID, ret[:MAX_NAME_LENGTH])
    sLog.Warning("FleetBound", "Handle_ChangeWingName() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!sFltSvc.IsWingInFleet(wingID->value(), m_fleetID))
        throw UserError("FleetNotAllowed");
    if (!IsBossOrFC(pMe) and !((pMe->fleetRole() == Fleet::Role::WingLeader) and (pMe->wingID() == wingID->value())))
        throw UserError("FleetNotAllowed");

    // fleetcommon.MAX_NAME_LENGTH
    std::string newName = PyRep::StringContent(name);
    if (newName.size() > 10)
        newName.resize(10);
    sFltSvc.RenameWing(wingID->value(), newName);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::ChangeSquadName(PyCallArgs &call, PyInt* squadID, PyRep* name) {
          //   self.fleet.ChangeSquadName(squadID, ret[:MAX_NAME_LENGTH])
    sLog.Warning("FleetBound", "Handle_ChangeSquadName() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!sFltSvc.IsSquadInFleet(squadID->value(), m_fleetID))
        throw UserError("FleetNotAllowed");
    SquadData sData = SquadData();
    sFltSvc.GetSquadData(squadID->value(), sData);
    if (!IsBossOrFC(pMe)
    and !((pMe->fleetRole() == Fleet::Role::WingLeader) and (pMe->wingID() == (int32)sData.wingID))
    and !((pMe->fleetRole() == Fleet::Role::SquadLeader) and (pMe->squadID() == squadID->value())))
        throw UserError("FleetNotAllowed");

    std::string newName = PyRep::StringContent(name);
    if (newName.size() > 10)
        newName.resize(10);
    sFltSvc.RenameSquad(squadID->value(), newName);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::SetOptions(PyCallArgs &call, PyObject* options) {
          //   self.fleet.SetOptions(options)
    sLog.Warning("FleetBound", "Handle_SetOptions() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!sFltSvc.IsBoss(pMe))
        throw UserError("FleetNotCreator");

    if ((options->arguments() == nullptr) or !options->arguments()->IsDict())
        return nullptr;
    FleetData fData = FleetData();
    sFltSvc.GetFleetData(m_fleetID, fData);
    PyDict* dict = options->arguments()->AsDict();
    PyRep* rep(dict->GetItemString("isFreeMove"));
    bool isFreeMove = ((rep != nullptr) and rep->IsBool()) ? rep->AsBool()->value() : fData.isFreeMove;
    rep = dict->GetItemString("isRegistered");
    bool isRegistered = ((rep != nullptr) and rep->IsBool()) ? rep->AsBool()->value() : fData.isRegistered;
    rep = dict->GetItemString("isVoiceEnabled");
    bool isVoiceEnabled = ((rep != nullptr) and rep->IsBool()) ? (rep->AsBool()->value() ? sConfig.chat.EnableVoiceChat : false) : fData.isVoiceEnabled;

    sFltSvc.UpdateOptions(m_fleetID, isFreeMove, isRegistered, isVoiceEnabled);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::GetJoinRequests(PyCallArgs &call) {
          //   self.fleet.GetJoinRequests()
    /* 20:12:59 W FleetBound: Handle_GetJoinRequests() size=0
     * 20:12:59 [FleetDump]   Call Arguments:
     * 20:12:59 [FleetDump]       Tuple: Empty
     */
    sLog.Warning("FleetBound", "Handle_GetJoinRequests() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!IsBossOrFC(pMe))
        throw UserError("FleetNotAllowed");

    std::vector<Client*> cVec;
    sFltSvc.GetJoinRequests(m_fleetID, cVec);

    PyDict* rsp = new PyDict();
    for (auto cur : cVec) {
        if (cur == nullptr)
            continue;
        PyDict* dict = new PyDict();
        dict->SetItemString("charID", new PyInt(cur->GetCharacterID()));
        rsp->SetItem(new PyInt(cur->GetCharacterID()), new PyObject("util.KeyVal", dict));
    }

    if (is_log_enabled(FLEET__DEBUG))
        rsp->Dump(FLEET__DEBUG, "    ");
    return rsp;
}

PyResult FleetBound::RejectJoinRequest(PyCallArgs &call, PyInt* characterID) {
    //    self.fleet.RejectJoinRequest(charID)
    sLog.Warning("FleetBound", "Handle_RejectJoinRequest() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!IsBossOrFC(pMe))
        throw UserError("FleetNotAllowed");

    // the applicant is not a member; look it up in this fleet's join requests
    Client* pClient(nullptr);
    std::vector<Client*> cVec;
    sFltSvc.GetJoinRequests(m_fleetID, cVec);
    for (auto cur : cVec)
        if ((cur != nullptr) and (cur->GetCharacterID() == characterID->value())) {
            pClient = cur;
            break;
        }
    if (pClient == nullptr)
        return nullptr;

    sFltSvc.RemoveJoinRequest(m_fleetID, pClient);

    // fleetSvc.OnFleetJoinRejected(charID) shows FleetJoinRequestRejected with the rejecter's name
    PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, new PyInt(call.client->GetCharacterID()));
    pClient->SendNotification("OnFleetJoinRejected", "clientID", &tuple, false);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::GetFleetComposition(PyCallArgs &call) {
    sLog.Warning("FleetBound", "Handle_GetFleetComposition() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!IsBossOrFC(pMe) and (pMe->fleetRole() != Fleet::Role::WingLeader) and (pMe->fleetRole() != Fleet::Role::SquadLeader))
        throw UserError("FleetNotCommanderOrBoss");

    Character* pChar(nullptr);
    PyList* list = new PyList();
    std::vector<Client*> members;
    sFltSvc.GetMemeberVec(m_fleetID, members);
    for (auto cur : members) {
        if (cur == nullptr)
            continue;
        pChar = cur->GetChar().get();
        if (pChar == nullptr)
            continue;
        PyDict* dict = new PyDict();
        dict->SetItemString("characterID", new PyInt(cur->GetCharacterID()));
        dict->SetItemString("shipTypeID", new PyInt(cur->GetShip()->typeID()));
        dict->SetItemString("solarSystemID", new PyInt(cur->GetSystemID()));
        if (pChar->HasSkill(EvESkill::Leadership) or pChar->HasSkill(EvESkill::FleetCommand) or pChar->HasSkill(EvESkill::WingCommand)) {
            PyTuple* skills = new PyTuple(3);
                skills->SetItem(0, new PyInt(pChar->GetSkillLevel(EvESkill::Leadership)));
                skills->SetItem(1, new PyInt(pChar->GetSkillLevel(EvESkill::FleetCommand)));
                skills->SetItem(2, new PyInt(pChar->GetSkillLevel(EvESkill::WingCommand)));
            dict->SetItemString("skills", skills);
            PyTuple* skillIDs = new PyTuple(3);
                skillIDs->SetItem(0, new PyInt(EvESkill::Leadership));
                skillIDs->SetItem(1, new PyInt(EvESkill::FleetCommand));
                skillIDs->SetItem(2, new PyInt(EvESkill::WingCommand));
            dict->SetItemString("skillIDs", skillIDs);
        }
        list->AddItem(new PyObject("util.KeyVal", dict));
        pChar = nullptr;
    }

    if (is_log_enabled(FLEET__DEBUG))
        list->Dump(FLEET__DEBUG, "    ");
    return list;
}

PyResult FleetBound::GetWings(PyCallArgs &call) {
         //    self.fleet.GetWings()
    sLog.Warning("FleetBound", "Handle_GetWings() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    CheckMember(call);

    return sFltSvc.GetWings(m_fleetID);
}

// this is fleet-wide
PyResult FleetBound::SendBroadcast(PyCallArgs &call, PyRep* message, PyInt* group, PyInt* itemID) {
         //    self.fleet.SendBroadcast(name, self.broadcastScope, itemID)
    sLog.Warning("FleetBound", "Handle_SendBroadcast() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    CheckMember(call);

    sFltSvc.FleetBroadcast(call.client, itemID->value(), Fleet::BCast::Scope::Universe, group->value(), PyRep::StringContent (message));

    // returns nothing
    return nullptr;
}

PyResult FleetBound::UpdateMemberInfo(PyCallArgs &call, std::optional <PyInt*> shipTypeID) {
         //    self.fleet.UpdateMemberInfo(self.GetMyShipTypeID())
    /*
     * 13:26:54 W FleetBound: Handle_UpdateMemberInfo() size=1
     * 13:26:54 [FleetDump]   Call Arguments:
     * 13:26:54 [FleetDump]       Tuple: 1 elements
     * 13:26:54 [FleetDump]         [ 0] (None)
     */
    // ship type and system are read live from the client (GetInitState, GetFleetComposition), so there is nothing to store
    _log(FLEET__TRACE, "FleetBound::UpdateMemberInfo() - %s", call.client->GetName());

    CheckMember(call);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::SetMotdEx(PyCallArgs &call, PyRep* motd) {
         //    self.fleet.SetMotdEx(motd)
    sLog.Warning("FleetBound", "Handle_SetMotdEx() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!IsBossOrFC(pMe))
        throw UserError("FleetNotAllowed");

    sFltSvc.SetMOTD(m_fleetID, PyRep::StringContent(motd));

    // returns nothing
    return nullptr;
}

PyResult FleetBound::GetMotd(PyCallArgs &call) {
    //   self.motd = self.fleet.GetMotd()
    sLog.Warning("FleetBound", "Handle_GetMotd() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    CheckMember(call);

    return sFltSvc.GetMOTD(m_fleetID);
}

PyResult FleetBound::LeaveFleet(PyCallArgs &call) {
    //    self.fleet.LeaveFleet()
    sLog.Warning("FleetBound", "Handle_LeaveFleet() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    CheckMember(call);

    sFltSvc.LeaveFleet(call.client);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::MakeLeader(PyCallArgs &call, PyInt* characterID) {
    /* self.fleet.MakeLeader(charID)  */
    sLog.Warning("FleetBound", "Handle_MakeLeader() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    /* the client only offers this to the boss, for other members ('Make Fleet Boss').
     * it hands the boss job to the target.  roles and positions do not change.
     */
    Character* pMe = CheckMember(call);
    if (!sFltSvc.IsBoss(pMe))
        throw UserError("FleetNotCreator");

    Client* pTarget(nullptr);
    Character* pTargetChar = GetTargetChar(characterID->value(), pTarget);
    if (pTargetChar == pMe)
        return nullptr;

    sFltSvc.SetBoss(m_fleetID, pTarget);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::SetBooster(PyCallArgs &call, PyInt* characterID, std::optional <PyInt*> roleBooster) {
    /*self.fleet.SetBooster(charID, roleBooster):  */
    sLog.Warning("FleetBound", "Handle_SetBooster() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    Client* pTarget(nullptr);
    Character* pChar = GetTargetChar(characterID->value(), pTarget);

    bool all = IsBossOrFC(pMe);
    // commanding self only counts for commanders (menusvc checkSubordinate)
    bool commands = sFltSvc.IsSuperior(pMe, pChar);
    if ((pMe == pChar) and !all and (pMe->fleetRole() != Fleet::Role::WingLeader) and (pMe->fleetRole() != Fleet::Role::SquadLeader))
        commands = false;

    int8 newBooster = roleBooster.has_value() ? roleBooster.value()->value() : Fleet::Booster::No;
    if (newBooster == pChar->fleetBooster())
        return PyStatic.NewFalse();

    // there can be only one booster per each fleet/wing/squad.  an occupied slot is refused
    switch (newBooster) {
        case Fleet::Booster::Fleet: {
            if (!all or !commands)
                throw UserError("FleetNotAllowed");
            if (pChar->fleetBooster() != Fleet::Booster::No)
                throw UserError("FleetAlreadyBooster");
            FleetData fData = FleetData();
            sFltSvc.GetFleetData(m_fleetID, fData);
            if (fData.booster != nullptr)
                throw UserError("FleetBoosterRoleFull");
        } break;
        case Fleet::Booster::Wing: {
            if (!commands or (!all and (pMe->fleetRole() != Fleet::Role::WingLeader)))
                throw UserError("FleetNotAllowed");
            if ((pChar->fleetRole() == Fleet::Role::FleetLeader) or (pChar->wingID() <= 0))
                throw UserError("FleetBoosterIllegal");
            if (pChar->fleetBooster() != Fleet::Booster::No)
                throw UserError("FleetAlreadyBooster");
            WingData wData = WingData();
            sFltSvc.GetWingData(pChar->wingID(), wData);
            if (wData.booster != nullptr)
                throw UserError("FleetBoosterRoleFull");
        } break;
        case Fleet::Booster::Squad: {
            if (!commands)
                throw UserError("FleetNotAllowed");
            if ((pChar->fleetRole() == Fleet::Role::FleetLeader) or (pChar->fleetRole() == Fleet::Role::WingLeader) or (pChar->squadID() <= 0))
                throw UserError("FleetBoosterIllegal");
            if (pChar->fleetBooster() != Fleet::Booster::No)
                throw UserError("FleetAlreadyBooster");
            SquadData sData = SquadData();
            sFltSvc.GetSquadData(pChar->squadID(), sData);
            if (sData.booster != nullptr)
                throw UserError("FleetBoosterRoleFull");
        } break;
        case Fleet::Booster::No: {
            // revoke: a superior of the booster, or the booster himself
            if ((pMe != pChar) and !commands)
                throw UserError("FleetNotAllowed");
        } break;
        default:
            throw UserError("FleetBoosterIllegal");
    }

    if (sFltSvc.UpdateMember(characterID->value(), m_fleetID, pChar->wingID(), pChar->squadID(), pChar->fleetJob(), pChar->fleetRole(), newBooster))
        return PyStatic.NewTrue();

    // returns boolean
    return PyStatic.NewFalse();
}

PyResult FleetBound::MoveMember(PyCallArgs &call, PyInt* characterID, std::optional <PyInt*> wingID, std::optional <PyInt*> squadID, std::optional <PyInt*> role, std::optional <PyInt*> booster) {
    sLog.Warning("FleetBound", "Handle_MoveMember() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    Client* pTarget(nullptr);
    Character* pChar = GetTargetChar(characterID->value(), pTarget);

    // the client sends -1 (FLEET_NONEID) for 'no wing/squad'.  0 means unchanged
    int32 newWingID = wingID.has_value() ? wingID.value()->value() : -1;
    int32 newSquadID = squadID.has_value() ? squadID.value()->value() : -1;
    if (newWingID == 0)
        newWingID = pChar->wingID();
    if (newSquadID == 0)
        newSquadID = pChar->squadID();
    int8 newRole = role.has_value() ? role.value()->value() : pChar->fleetRole();
    int8 newBooster = booster.has_value() ? booster.value()->value() : pChar->fleetBooster();

    // validate the destination position
    switch (newRole) {
        case Fleet::Role::FleetLeader: {
            if ((newWingID > 0) or (newSquadID > 0))
                throw UserError("FleetNotAllowed");
            newWingID = -1;
            newSquadID = -1;
        } break;
        case Fleet::Role::WingLeader: {
            if ((newWingID <= 0) or !sFltSvc.IsWingInFleet(newWingID, m_fleetID) or (newSquadID > 0))
                throw UserError("FleetNotAllowed");
            newSquadID = -1;
        } break;
        case Fleet::Role::SquadLeader:
        case Fleet::Role::Member: {
            if ((newSquadID <= 0) or !sFltSvc.IsSquadInFleet(newSquadID, m_fleetID))
                throw UserError("FleetNotAllowed");
            SquadData sData = SquadData();
            sFltSvc.GetSquadData(newSquadID, sData);
            if ((newWingID > 0) and (newWingID != (int32)sData.wingID))
                throw UserError("FleetNotAllowed");
            newWingID = sData.wingID;
        } break;
        default:
            throw UserError("FleetNotAllowed");
    }

    // permissions (menusvc GetFleetMemberMenu2): boss/FC move anyone anywhere.  free-move lets a member place himself in any squad.
    //  a WC moves his subordinates (or himself) within his wing, a SC within his squad
    bool allowed(IsBossOrFC(pMe));
    if (!allowed) {
        bool squadRole = ((newRole == Fleet::Role::SquadLeader) or (newRole == Fleet::Role::Member));
        FleetData fData = FleetData();
        sFltSvc.GetFleetData(m_fleetID, fData);
        if (fData.isFreeMove and (pMe == pChar) and squadRole) {
            allowed = true;
        } else if (pMe->fleetRole() == Fleet::Role::WingLeader) {
            allowed = (squadRole and (pChar->wingID() == pMe->wingID()) and (newWingID == pMe->wingID()));
        } else if (pMe->fleetRole() == Fleet::Role::SquadLeader) {
            allowed = (squadRole and (pChar->squadID() == pMe->squadID()) and (newSquadID == pMe->squadID()));
        }
    }
    if (!allowed)
        throw UserError("FleetNotAllowed");

    // slot occupancy and capacity
    Client* pHolder = sFltSvc.GetPositionHolder(m_fleetID, newWingID, newSquadID, newRole);
    if ((pHolder != nullptr) and (pHolder != pTarget)) {
        if (newRole == Fleet::Role::FleetLeader)
            throw UserError("FleetAlreadyLeader");
        throw UserError("FleetPositionFilled").AddOwnerName("owner", pHolder->GetCharacterID());
    }
    if ((newSquadID > 0) and (newSquadID != pChar->squadID()) and (sFltSvc.GetSquadMemberCount(newSquadID) >= Fleet::Limits::MaxSquadMembers))
        throw UserError("FleetTooManyMembersInSquad").AddAmount("num", (int)Fleet::Limits::MaxSquadMembers);

    // a wing/squad booster role does not follow the member into another unit or a higher role
    if ((newBooster == Fleet::Booster::Wing) and ((newWingID != pChar->wingID()) or (newRole == Fleet::Role::FleetLeader)))
        newBooster = Fleet::Booster::No;
    if ((newBooster == Fleet::Booster::Squad) and ((newSquadID != pChar->squadID()) or (newRole == Fleet::Role::FleetLeader) or (newRole == Fleet::Role::WingLeader)))
        newBooster = Fleet::Booster::No;
    if (newBooster != pChar->fleetBooster()) {
        bool full(false);
        switch (newBooster) {
            case Fleet::Booster::Fleet: {
                FleetData fData = FleetData();
                sFltSvc.GetFleetData(m_fleetID, fData);
                full = ((fData.booster != nullptr) and (fData.booster != pTarget));
            } break;
            case Fleet::Booster::Wing: {
                WingData wData = WingData();
                sFltSvc.GetWingData(newWingID, wData);
                full = ((wData.booster != nullptr) and (wData.booster != pTarget));
            } break;
            case Fleet::Booster::Squad: {
                SquadData sData = SquadData();
                sFltSvc.GetSquadData(newSquadID, sData);
                full = ((sData.booster != nullptr) and (sData.booster != pTarget));
            } break;
        }
        if (full)
            throw UserError("FleetBoosterRoleFull");
    }

    // if voice is enabled for this fleet, SendNotification OnFleetMove to enable changing channels (on client side)

    if (sFltSvc.UpdateMember(characterID->value(), m_fleetID, newWingID, newSquadID, pChar->fleetJob(), newRole, newBooster))
        return PyStatic.NewTrue();

    // returns boolean
    return PyStatic.NewFalse();
}

PyResult FleetBound::KickMember(PyCallArgs &call, PyInt* characterID) {
    /*
     *        if charID == eve.session.charid:
     *            self.LeaveFleet()
     *        else:
     *            self.fleet.KickMember(charID)
     */
    sLog.Warning("FleetBound", "Handle_KickMember() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    Client* pTarget(nullptr);
    Character* pChar = GetTargetChar(characterID->value(), pTarget);

    if (pChar != pMe) {
        if (sFltSvc.IsBoss(pChar))
            throw UserError("FleetCantKickBoss");
        if (!sFltSvc.IsSuperior(pMe, pChar))
            throw UserError("FleetNotAllowed");
    }

    sFltSvc.LeaveFleet(pTarget);

    // returns boolean
    return PyStatic.NewTrue();
}

PyResult FleetBound::CreateWing(PyCallArgs &call) {
    /*  wingID = self.fleet.CreateWing()  */
    sLog.Warning("FleetBound", "Handle_CreateWing() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!IsBossOrFC(pMe))
        throw UserError("FleetNotAllowed");
    if (sFltSvc.GetWingCount(m_fleetID) >= Fleet::Limits::MaxWings)
        throw UserError("FleetTooManyWings").AddAmount("num", (int)Fleet::Limits::MaxWings);

    // the client tests 'if wingID:' and then creates the first squad itself
    PyRep* res = sFltSvc.CreateWing(m_fleetID);
    if (res == nullptr)
        return PyStatic.NewNone();
    return res;
}

PyResult FleetBound::CreateSquad(PyCallArgs &call, PyInt* wingID) {
    /* self.fleet.CreateSquad(wingID)  */
    sLog.Warning("FleetBound", "Handle_CreateSquad() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!sFltSvc.IsWingInFleet(wingID->value(), m_fleetID))
        throw UserError("FleetNotAllowed");
    if (!IsBossOrFC(pMe) and !((pMe->fleetRole() == Fleet::Role::WingLeader) and (pMe->wingID() == wingID->value())))
        throw UserError("FleetNotAllowed");
    if (sFltSvc.GetSquadCount(wingID->value()) >= Fleet::Limits::MaxSquadsPerWing)
        throw UserError("FleetTooManySquads").AddAmount("num", (int)Fleet::Limits::MaxSquadsPerWing);

    sFltSvc.CreateSquad(m_fleetID, wingID->value());

    // returns nothing
    return nullptr;
}

PyResult FleetBound::DeleteWing(PyCallArgs &call, PyInt* wingID) {
    /*    self.fleet.DeleteWing(wingID)  */
    sLog.Warning("FleetBound", "Handle_DeleteWing() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!sFltSvc.IsWingInFleet(wingID->value(), m_fleetID))
        throw UserError("FleetNotAllowed");
    if (!IsBossOrFC(pMe) and !((pMe->fleetRole() == Fleet::Role::WingLeader) and (pMe->wingID() == wingID->value())))
        throw UserError("FleetNotAllowed");
    if (sFltSvc.GetWingMemberCount(wingID->value()) > 0)
        throw UserError("FleetCannotDeleteNonEmptyWing");

    sFltSvc.DeleteWing(wingID->value());

    // returns nothing
    return nullptr;
}

PyResult FleetBound::DeleteSquad(PyCallArgs &call, PyInt* squadID) {
    /* self.fleet.DeleteSquad(squadID)  */
    sLog.Warning("FleetBound", "Handle_DeleteSquad() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    Character* pMe = CheckMember(call);
    if (!sFltSvc.IsSquadInFleet(squadID->value(), m_fleetID))
        throw UserError("FleetNotAllowed");
    SquadData sData = SquadData();
    sFltSvc.GetSquadData(squadID->value(), sData);
    if (!IsBossOrFC(pMe)
    and !((pMe->fleetRole() == Fleet::Role::WingLeader) and (pMe->wingID() == (int32)sData.wingID))
    and !((pMe->fleetRole() == Fleet::Role::SquadLeader) and (pMe->squadID() == squadID->value())))
        throw UserError("FleetNotAllowed");
    if (sFltSvc.GetSquadMemberCount(squadID->value()) > 0)
        throw UserError("FleetCannotDeleteNonEmptySquad");

    sFltSvc.DeleteSquad(squadID->value());

    // returns nothing
    return nullptr;
}

PyResult FleetBound::AddToVoiceChat(PyCallArgs &call, PyRep* channelName) {
    //    self.fleet.AddToVoiceChat(channelName)
    sLog.Warning("FleetBound", "Handle_AddToVoiceChat() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    CheckMember(call);

    //sConfig.chat.EnableVoiceChat;

    // returns nothing
    return nullptr;
}

PyResult FleetBound::SetVoiceMuteStatus(PyCallArgs &call, PyRep* status, PyRep* channel) {
    //    self.fleet.SetVoiceMuteStatus(status, channel)
    sLog.Warning("FleetBound", "Handle_SetVoiceMuteStatus() size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    CheckMember(call);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::ExcludeFromVoiceMute(PyCallArgs &call, PyInt* characterID, PyRep* channel) {
    //    self.fleet.ExcludeFromVoiceMute(charid, channel)
    sLog.Warning("FleetBound", "Handle_ExcludeFromVoiceMute()) size=%lli", call.tuple->size());
    call.Dump(FLEET__DUMP);

    CheckMember(call);

    // returns nothing
    return nullptr;
}

PyResult FleetBound::Reconnect(PyCallArgs &call) {
    /* no args.  the client calls this (on its own fleet's moniker) when it logs back in within RECONNECT_TIMEOUT of losing its
     *  connection (fleetSvc.AttemptReconnectLazy).  the caller is not a member yet.  the client ignores errors
     */
    if (!sFltSvc.Reconnect(call.client, m_fleetID))
        _log(FLEET__TRACE, "FleetBound::Reconnect() - %s could not rejoin fleet %u", call.client->GetName(), m_fleetID);

    // returns nothing
    return PyStatic.NewNone();
}
