
 /**
  * @name FleetService.cpp
  *     Fleet Service code for EVEmu
  *     This singleton object is used to access and manipulate all dynamic fleet data
  *
  * @Author:        Allan
  * @date:          05 August 2014 (original skeleton outline)
  * @update:        21 November 2017 (begin actual implementation)
  *
  */

//work in progress

#include "eve-server.h"
#include "chat/LSCService.h"
#include "fleet/FleetService.h"
#include "ship/modules/GangLinkModule.h"
#include "ship/modules/ModuleManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "services/ServiceManager.h"

// the id of the bubble a member's ship is in;  0 docked or between bubbles (no ship entity, no bubble)
static uint32 BubbleIDOf(Client* pClient)
{
    if ((pClient == nullptr) or (pClient->GetShipSE() == nullptr) or (pClient->GetShipSE()->SysBubble() == nullptr))
        return 0;
    return pClient->GetShipSE()->SysBubble()->GetID();
}

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

//  Manager class functions and methods...

FleetService::FleetService()
: m_lsc(nullptr),
m_initalized(false),
m_departing(nullptr)
{
}

void FleetService::Initialize(EVEServiceManager& svc)
{
    m_lsc = svc.Lookup <LSCService>("LSC");

    m_joinReq.clear();
    m_inviteData.clear();
    m_fleetWings.clear();
    m_wingSquads.clear();
    m_wingDataMap.clear();
    m_fleetMembers.clear();
    m_fleetDataMap.clear();
    m_squadDataMap.clear();
    m_fleetAdvertMap.clear();
    m_reconnect.clear();
    m_watchlists.clear();
    m_beacons.clear();
    m_bridges.clear();

    //  these will have to be incremented individually, then stored according to fleet
    m_fleetID = FLEET_ID;  //950000000
    m_wingID = WING_ID;   //960000000
    m_squadID = SQUAD_ID; //970000000

    m_initalized = true;
    sLog.Blue("     FleetService", "Fleet Service Initialized.");
}


uint32 FleetService::CreateFleet(Client *pClient)
{
    if (pClient == nullptr)
        return 0;

    Character* pChar = pClient->GetChar().get();
    if (pChar == nullptr)
        return 0;

    FleetData fData = FleetData();
        fData.isFreeMove = true;
        fData.isLootLogging = false;
        fData.isRegistered = false;
        fData.isVoiceEnabled = sConfig.chat.EnableVoiceChat;   //false
        fData.creator = pClient;
        fData.leader = pClient;
        fData.booster = pClient;
        fData.name = pChar->itemName();
        fData.name += "'s Fleet";
        fData.motd = "";
        fData.isMutedByLeader.clear();
        fData.isExcludedFromMuting.clear();
        fData.squads = 1;
        fData.dateCreated = GetFileTimeNow();
    m_fleetDataMap.emplace(m_fleetID, fData);

    WingData wData = WingData();
        wData.fleetID = m_fleetID;
        wData.name = "Wing 1";
        wData.booster = nullptr;
        wData.leader = nullptr;
    m_wingDataMap.emplace(m_wingID, wData);
    m_fleetWings.emplace(m_fleetID, m_wingID);

    BoostData bData = BoostData();
        bData.armored = pChar->GetSkillLevel(EvESkill::ArmoredWarfare);
        bData.info = pChar->GetSkillLevel(EvESkill::InformationWarfare);
        bData.mining = pChar->GetSkillLevel(EvESkill::MiningForeman);
        bData.siege = pChar->GetSkillLevel(EvESkill::SiegeWarfare);
        bData.skirmish = pChar->GetSkillLevel(EvESkill::SkirmishWarfare);
    SquadData sData = SquadData();
        sData.boost = bData;
        sData.name = "Squad 1";
        sData.booster = nullptr;
        sData.leader = nullptr;
        sData.wingID = m_wingID;
        sData.fleetID = m_fleetID;
        sData.members.clear();
    m_squadDataMap.emplace(m_squadID, sData);
    m_wingSquads.emplace(m_wingID, m_squadID);

    // set char fleet data and send to client
    CharFleetData fleet = CharFleetData();
        fleet.fleetID = m_fleetID;        //this is also lsc channel #
        fleet.wingID = -1;
        fleet.squadID = -1;
        fleet.job = Fleet::Job::Creator;
        fleet.role = Fleet::Role::FleetLeader;
        fleet.booster = Fleet::Booster::Fleet;
        fleet.joinTime = GetFileTimeNow();
    pChar->SetFleetData(fleet);

    // update the fleet member map with new member for this fleet.
    m_fleetMembers.emplace(m_fleetID, pClient);

    _log(FLEET__INFO, "FleetService::CreateFleet() - fleetID: %u, wingID: %u, squadID: %u, leaderID: %u", m_fleetID, m_wingID, m_squadID, pChar->itemID());

    sLog.Cyan("CreateFleet", "bData -- leader: %i, armored: %i, info: %i, siege: %i, skirmish: %i, mining: %i", \
            sData.boost.leader, sData.boost.armored, sData.boost.info, sData.boost.siege, sData.boost.skirmish, sData.boost.mining);

    if (sConfig.chat.EnableFleetChat)
        m_lsc->CreateSystemChannel(m_fleetID);
    if (sConfig.chat.EnableWingChat)
        m_lsc->CreateSystemChannel(m_wingID);
    if (sConfig.chat.EnableSquadChat)
        m_lsc->CreateSystemChannel(m_squadID);

    // increment counters after channels are created (to avoid wrong channel creation)
    ++m_fleetID;
    ++m_wingID;
    ++m_squadID;

    return fleet.fleetID;
}

PyRep* FleetService::CreateWing(uint32 fleetID)
{
    int8 count = m_fleetWings.count(fleetID);
    // callers check the limit and throw FleetTooManyWings
    if (count >= Fleet::Limits::MaxWings)
        return nullptr;
    WingData wData = WingData();
        wData.fleetID = fleetID;
        wData.name = "Wing ";
        wData.name += std::to_string(count + 1);
        wData.booster = nullptr;
        wData.leader = nullptr;
    m_wingDataMap.emplace(m_wingID, wData);
    m_fleetWings.emplace(fleetID, m_wingID);

    _log(FLEET__INFO, "FleetService::CreateWing() - fleetID: %u, wingID: %u", fleetID, m_wingID);

    PyTuple* tuple1 = new PyTuple(1);
        tuple1->SetItem(0, new PyInt(m_wingID));
    SendFleetUpdate(fleetID, "OnFleetWingAdded", tuple1);

    if (sConfig.chat.EnableWingChat)
        m_lsc->CreateSystemChannel(m_wingID);

    std::list<int32> wing, squad;
    wing.clear();
    squad.clear();
    wing.emplace(wing.end(), m_wingID);
    UpdateBoost(fleetID, true, wing, squad);

    PyInt* res = new PyInt(m_wingID);
    ++m_wingID;
    return res;
}

void FleetService::CreateSquad(uint32 fleetID, uint32 wingID)
{
    // callers check the limit and throw FleetTooManySquads
    if (m_wingSquads.count(wingID) >= Fleet::Limits::MaxSquadsPerWing)
        return;
    IncFleetSquads(fleetID, wingID);
    FleetData data = FleetData();
    GetFleetData(fleetID, data);
    BoostData bData = BoostData();
    SquadData sData = SquadData();
        sData.boost = bData;
        sData.name = "Squad ";
        sData.name += std::to_string(data.squads);
        sData.wingID = wingID;
        sData.fleetID = fleetID;
        sData.booster = nullptr;
        sData.leader = nullptr;
        sData.members.clear();
    m_squadDataMap.emplace(m_squadID, sData);
    m_wingSquads.emplace(wingID, m_squadID);

    _log(FLEET__INFO, "FleetService::CreateSquad() - fleetID: %u, wingID: %u, squadID: %u", fleetID, wingID, m_squadID);

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(wingID));
        tuple->SetItem(1, new PyInt(m_squadID));
    SendFleetUpdate(fleetID, "OnFleetSquadAdded", tuple);

    if (sConfig.chat.EnableSquadChat)
        m_lsc->CreateSystemChannel(m_squadID);

    std::list<int32> wing, squad;
    wing.clear();
    wing.emplace(wing.end(), wingID);
    squad.clear();
    squad.emplace(squad.end(), m_squadID);
    UpdateBoost(fleetID, true, wing, squad);

    ++m_squadID;
}

void FleetService::CreateFleetAdvert(uint32 fleetID, FleetAdvert data)
{
    /** @todo   update inviteScope to use names */
    std::map<uint32, FleetAdvert>::iterator itr = m_fleetAdvertMap.find(fleetID);
    if (itr != m_fleetAdvertMap.end()) {
        m_fleetAdvertMap.erase(fleetID);
        _log(FLEET__INFO, "CreateFleetAdvert()  Updating Advert for FleetID: %u, Scope: %u", fleetID, data.inviteScope);
    } else {
        _log(FLEET__INFO, "CreateFleetAdvert()  Creating Advert for FleetID: %u, Scope: %u", fleetID, data.inviteScope);
    }

    m_fleetAdvertMap.emplace(fleetID, data);
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr != m_fleetDataMap.end())
        UpdateOptions(fleetID, fItr->second.isFreeMove, /*isRegistered*/true, fItr->second.isVoiceEnabled);

    // update chat channel(s) with new description?
}

bool FleetService::AddMember(Client* pClient, uint32 fleetID, int32 wingID, int32 squadID, int8 job, int8 role, int8 booster)
{
    Character* pChar = pClient->GetChar().get();
    if (pChar == nullptr)
        return false;

    _log(FLEET__TRACE, "AddMember - CharID: %u, FleetID: %u, WingID: %i, Squad: %i, Job: %s, Role: %s, Booster: %s", \
    pChar->itemID(), fleetID, wingID, squadID, GetJobName(job).c_str(), GetRoleName(role).c_str(), GetBoosterName(booster).c_str());

    // add new member to fleet data
    m_fleetMembers.emplace(fleetID, pClient);

    // set char fleet data and send to client
    CharFleetData fData = CharFleetData();
        fData.fleetID = fleetID;
        fData.wingID = wingID;
        fData.squadID = squadID;
        fData.job = job; //Fleet::Job::None;
        fData.role = role;
        fData.booster = booster;
        fData.joinTime = GetFileTimeNow();
    pChar->SetFleetData(fData);

    PyTuple* obj = new PyTuple(1);
        obj->SetItem(0, MakeFleetState(fleetID));
    pClient->SendNotification("OnFleetStateChange", "charid", obj, true);

    bool fleet(false);
    if (role == Fleet::Role::FleetLeader) {
        std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
        if (fItr == m_fleetDataMap.end()) {
            _log(FLEET__ERROR, "Fleet Data for fleetID: %u not foune.", fleetID);
            return false;
        }
        if (fItr->second.booster == nullptr) {
            fItr->second.booster = pClient;
            fleet = true;
        }
        if (fItr->second.leader == nullptr) {
            fItr->second.leader = pClient;
            fleet = true;
        } else {
            _log(FLEET__ERROR, "FC is already filled.");
            return false;
        }
    }

    PyTuple* count = new PyTuple(1);
        count->SetItem(0, new PyInt((255 - m_fleetMembers.count(fleetID))));  // this is slots left from 255 (256 - leader)
    pClient->SendNotification("OnFleetActive", "clientID", count, true);

    std::list<int32> wing, squad;
    wing.clear();
    squad.clear();

    if (IsWingID(wingID)) {
        std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
        if (itr == m_wingDataMap.end()) {
            _log(FLEET__ERROR, "Wing Data for wingID: %u not foune.", wingID);
            return false;
        }
        if (role == Fleet::Role::WingLeader) {
            if (itr->second.booster == nullptr) {
                itr->second.booster = pClient;
                wing.emplace(wing.end(), wingID);
            }
            if (itr->second.leader == nullptr) {
                itr->second.leader = pClient;
            } else {
                _log(FLEET__ERROR, "WC is already filled.");
                return false;
            }
        }
        PyTuple* count = new PyTuple(2);
            count->SetItem(0, new PyInt(wingID));
            count->SetItem(1, new PyInt(IsWingActive(wingID) ? 0 : 1));
        pClient->SendNotification("OnWingActive", "clientID", count, true);

        if (!IsSquadID(squadID))
            wing.emplace(wing.end(), wingID);
    }

    if (IsSquadID(squadID)) {
        std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
        if (itr == m_squadDataMap.end()) {
            _log(FLEET__ERROR, "Squad Data for squadID: %u not foune.", squadID);
            return false;
        }
        if (role == Fleet::Role::SquadLeader) {
            if (itr->second.booster == nullptr) {
                itr->second.booster = pClient;
                squad.emplace(squad.end(), squadID);
            }
            if (itr->second.leader == nullptr) {
                itr->second.leader = pClient;
            } else {
                _log(FLEET__ERROR, "SC is already filled.");
                return false;
            }
        }
        itr->second.members.emplace(pChar->itemID(), pClient);
        PyTuple* count = new PyTuple(2);
            count->SetItem(0, new PyInt(squadID));
            count->SetItem(1, PyStatic.NewOne());
        pClient->SendNotification("OnSquadActive", "clientID", count, true);

        squad.emplace(squad.end(), squadID);
    }

    // update all members with new member data
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
    SendFleetUpdate(fleetID, "OnFleetJoin", res);

    UpdateBoost(fleetID, fleet, wing, squad);
    return true;
}

bool FleetService::UpdateMember(uint32 charID, uint32 fleetID, int32 newWingID, int32 newSquadID, int8 newJob, int8 newRole, int8 newBooster)
{
    bool fleet(false);
    std::list<int32> wing, squad;
    wing.clear();
    squad.clear();
    int8 oldRole(0), oldJob(0), oldBooster(0);
    int32 oldWingID(0), oldSquadID(0);
    // verify member data
    Client* pClient = GetMember(fleetID, charID);
    if (pClient == nullptr)
        return false;
    Character* pChar = pClient->GetChar().get();
    if (pChar == nullptr)
        return false;

    oldJob = pChar->fleetJob();
    oldRole = pChar->fleetRole();
    oldWingID = pChar->wingID();
    oldSquadID = pChar->squadID();
    oldBooster = pChar->fleetBooster();

    // 0 means 'unchanged' (resolve it first, or the squad roster below would drop this member)
    if (newWingID == 0)
        newWingID = oldWingID;
    if (newSquadID == 0)
        newSquadID = oldSquadID;

    // update fleet data
    if (oldBooster != newBooster) {
        if (oldBooster == Fleet::Booster::Fleet) {
            std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
            if (fItr == m_fleetDataMap.end())
                return false;
            fItr->second.booster = nullptr;
            fleet = true;
        }

        if (newBooster == Fleet::Booster::Fleet) {
            std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
            if (fItr == m_fleetDataMap.end())
                return false;
            fItr->second.booster = pClient;
            fleet = true;
        }
    }

    if (oldRole != newRole) {
        if (oldRole == Fleet::Role::FleetLeader) {
            std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
            if (fItr == m_fleetDataMap.end())
                return false;
            fItr->second.leader = nullptr;
        }

        if (newRole == Fleet::Role::FleetLeader) {
            std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
            if (fItr == m_fleetDataMap.end())
                return false;
            fItr->second.leader = pClient;
            if (fItr->second.booster == nullptr) {
                fItr->second.booster = pClient;
                newBooster = Fleet::Booster::Fleet;
                fleet = true;
            }
        }
    }
    // update wing data
    if (IsWingID(oldWingID)) {
        std::map<uint32, WingData>::iterator wItr = m_wingDataMap.find(oldWingID);
        if (wItr == m_wingDataMap.end())
            return false;
        if ((oldRole != newRole) and (oldRole == Fleet::Role::WingLeader)) {
            wItr->second.leader = nullptr;
        }
        if ((oldBooster != newBooster) and (oldBooster == Fleet::Booster::Wing)) {
            wItr->second.booster = nullptr;
            wing.emplace(wing.end(), oldWingID);
        }
    }

    if (newWingID == 0) {
        newWingID = oldWingID;
    } else if (IsWingID(newWingID)) {
        std::map<uint32, WingData>::iterator wItr = m_wingDataMap.find(newWingID);
        if (wItr == m_wingDataMap.end())
            return false;
        if ((oldBooster != newBooster) and (newBooster == Fleet::Booster::Wing)) {
            wItr->second.booster = pClient;
            wing.emplace(wing.end(), newWingID);
        }
        if ((oldRole != newRole) and (newRole == Fleet::Role::WingLeader)) {
            wItr->second.leader = pClient;
            if (wItr->second.booster == nullptr) {
                wItr->second.booster = pClient;
                newBooster = Fleet::Booster::Wing;
                wing.emplace(wing.end(), newWingID);
            }
        }
    }

    // update squad data
    if (IsSquadID(oldSquadID)) {
        std::map<uint32, SquadData>::iterator sItr = m_squadDataMap.find(oldSquadID);
        if (sItr == m_squadDataMap.end())
            return false;
        if ((oldRole != newRole) and (oldRole == Fleet::Role::SquadLeader))
            sItr->second.leader = nullptr;
        if ((oldBooster != newBooster) and (oldBooster == Fleet::Booster::Squad)) {
            sItr->second.booster = nullptr;
            squad.emplace(squad.end(), oldSquadID);
        }
        if (oldSquadID != newSquadID) {
            sItr->second.members.erase(charID);
            // need a fast way to iterate thru wing data for active status...
            SendActiveStatus(fleetID, oldWingID, oldSquadID);
        }
    }

    if (newSquadID == 0) {
        newSquadID = oldSquadID;
    } else if (IsSquadID(newSquadID)) {
        std::map<uint32, SquadData>::iterator sItr = m_squadDataMap.find(newSquadID);
        if (sItr == m_squadDataMap.end())
            return false;
        if ((oldBooster != newBooster) and (newBooster == Fleet::Booster::Squad)) {
            sItr->second.booster = pClient;
            squad.emplace(squad.end(), newSquadID);
        }
        if ((oldRole != newRole) and (newRole == Fleet::Role::SquadLeader)) {
            sItr->second.leader = pClient;
            if (sItr->second.booster == nullptr) {
                sItr->second.booster = pClient;
                newBooster = Fleet::Booster::Squad;
                squad.emplace(squad.end(), newSquadID);
            }
        }
        if (newSquadID != oldSquadID) {
            sItr->second.members.emplace(charID, pClient);
            SendActiveStatus(fleetID, newWingID, newSquadID);
        }
    }

    _log(FLEET__TRACE, "UpdateMember - CharID: %u, FleetID: %u, oldWingID: %i, newWingID: %i, oldSquad: %i, newSquadID: %i, oldJob: %s, newJob: %s, oldRole: %s, newRole: %s, oldBooster: %s, newBooster: %s", \
            charID, fleetID, oldWingID, newWingID, oldSquadID, newSquadID, \
            GetJobName(oldJob).c_str(), GetJobName(newJob).c_str(), GetRoleName(oldRole).c_str(), GetRoleName(newRole).c_str(), \
            GetBoosterName(oldBooster).c_str(), GetBoosterName(newBooster).c_str());

    // update char data
    CharFleetData fData = CharFleetData();
        fData.fleetID = fleetID;
        fData.wingID = newWingID;
        fData.squadID = newSquadID;
        fData.job = newJob;
        fData.role = newRole;
        fData.booster = newBooster;
        fData.joinTime = pChar->fleetJoinTime();
    pChar->SetFleetData(fData);

    // update fleet members with new data
    MemberChangedRSP res;
        res.charID = charID;
        res.fleetID = fleetID;
        res.oldWingID = oldWingID;
        res.oldSquadID = oldSquadID;
        res.oldRole = oldRole;
        res.oldJob = oldJob;
        res.oldBooster = oldBooster;
        res.newWingID = newWingID;
        res.newSquadID = newSquadID;
        res.newRole = newRole;
        res.newJob = newJob;
        res.newBooster = newBooster;
        res.isOnlyMember = (m_fleetMembers.count(fleetID) > 1 ? false : true);
    SendFleetUpdate(fleetID, "OnFleetMemberChanged", res.Encode());

    UpdateBoost(fleetID, fleet, wing, squad);

    // send "OnFleetMove" (empty tuple) if/when voice is enabled for this fleet

    return true;
}

// a squad member gets the squad's boost when they have a ship in space in the booster's system.
//  docked members have no ship, and neither does a member who is undocking until their ship is made.
static bool CanTakeBoost(Client* pMember, Client* pBooster)
{
    if ((pMember == nullptr) or (pBooster == nullptr) or (pMember->GetShipSE() == nullptr))
        return false;
    return (pMember->GetSystemID() == pBooster->GetSystemID());
}

void FleetService::UpdateBoost(uint32 fleetID, bool fleet, std::list<int32>& wing, std::list<int32>& squad)
{
    double start = GetTimeUSeconds();
    /*  this needs to check all boosters to update squad boost levels
     *   we will also check current levels to see if members need to be updated
     */
    std::vector< uint32 > wingIDs, squadIDs;
    std::map<ShipSE*, BoostData> memberUpdateMap;

    bool fBoost(false);
    int8 armored(0), info(0), leader(0), mining(0), siege(0), skirmish(0);

    BoostData fData = BoostData();
    BoostData bData = BoostData();

    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr != m_fleetDataMap.end()) {
        // set base boost data from FB
        if ((fItr->second.leader != nullptr) and (fItr->second.leader->IsInSpace()))
            if (m_fleetWings.count(fleetID) <= fItr->second.leader->GetChar()->GetSkillLevel(EvESkill::FleetCommand)) {
                if ((fItr->second.booster != nullptr) and (fItr->second.booster->IsInSpace()))
                    if (fItr->second.leader->GetSystemID() == fItr->second.booster->GetSystemID()) {
                        Character* pChar = fItr->second.booster->GetChar().get();
                        if (pChar != nullptr) {
                            if (pChar->HasSkillTrainedToLevel(EvESkill::ArmoredWarfare, 1))
                                fData.armored   = pChar->GetSkillLevel(EvESkill::ArmoredWarfare);
                            if (pChar->HasSkillTrainedToLevel(EvESkill::InformationWarfare, 1))
                                fData.info      = pChar->GetSkillLevel(EvESkill::InformationWarfare);
                            if (pChar->HasSkillTrainedToLevel(EvESkill::SiegeWarfare, 1))
                                fData.siege     = pChar->GetSkillLevel(EvESkill::SiegeWarfare);
                            if (pChar->HasSkillTrainedToLevel(EvESkill::SkirmishWarfare, 1))
                                fData.skirmish  = pChar->GetSkillLevel(EvESkill::SkirmishWarfare);
                            if (pChar->HasSkillTrainedToLevel(EvESkill::MiningForeman, 1))
                                fData.mining    = pChar->GetSkillLevel(EvESkill::MiningForeman);
                            if (fData.armored or fData.info or fData.leader or fData.mining or fData.siege or fData.skirmish)
                                fBoost = true;
                        }
                    }
                // this is for FC only.  will always get own skill, and here they get their fleet boost, also
                fData.leader = fItr->second.leader->GetChar()->GetSkillLevel(EvESkill::Leadership);
                if (fItr->second.leader->IsInSpace() and (fItr->second.leader->GetShipSE() != nullptr))
                    fItr->second.leader->GetShipSE()->ApplyBoost(fData);
            }
    }

    _log( FLEET__TRACE, "UpdateBoost - FB: %s, leader: %i, armored: %i, info: %i, siege: %i, skirmish: %i, mining: %i", \
            (fBoost ? "true" : "false"), fData.leader, fData.armored, fData.info, fData.siege, fData.skirmish, fData.mining);

    // check squad boost updates....sb overwriting higher boosts
    if (fleet) {
        // update all fleet members due to fleet booster update
        wingIDs.clear();
        GetWingIDs(fleetID, wingIDs);
        for (auto wingID : wingIDs) {
            if (!IsWingID(wingID))        // if WingID is invalid, remove it from map!!
                continue;
            bData = BoostData();
            if (fBoost) {
                bData.armored   = fData.armored;
                bData.info      = fData.info;
                bData.mining    = fData.mining;
                bData.siege     = fData.siege;
                bData.skirmish  = fData.skirmish;
            }
            SetWingBoostData(wingID, bData);

            squadIDs.clear();
            GetSquadIDs(wingID, squadIDs);
            for (auto squadID : squadIDs) {
                if (!IsSquadID(squadID))
                    continue;
                bool sboost(false);
                SetSquadBoostData(squadID, bData, sboost);
                SquadData sData = SquadData();
                GetSquadData(squadID, sData);
                for (auto cur : sData.members)  // SC is a member
                    if (sboost and CanTakeBoost(cur.second, sData.booster))
                        memberUpdateMap.emplace(cur.second->GetShipSE(), sData.boost);
            }
        }
    } else if (!wing.empty()) {
        wing.sort();
        wing.unique();
        for (auto wingID : wing) {
            if (!IsWingID(wingID))
                continue;
            bData = BoostData();
            if (fBoost) {
                bData.armored   = fData.armored;
                bData.info      = fData.info;
                bData.mining    = fData.mining;
                bData.siege     = fData.siege;
                bData.skirmish  = fData.skirmish;
            }
            SetWingBoostData(wingID, bData);

            squadIDs.clear();
            GetSquadIDs(wingID, squadIDs);
            for (auto squadID : squadIDs) {
                if (!IsSquadID(squadID))
                    continue;
                bool sboost(false);
                SetSquadBoostData(squadID, bData, sboost);
                SquadData sData = SquadData();
                GetSquadData(squadID, sData);
                for (auto cur : sData.members)
                    if (sboost and CanTakeBoost(cur.second, sData.booster))
                        memberUpdateMap.emplace(cur.second->GetShipSE(), sData.boost);
            }
        }
    } else if (!squad.empty()) {
        squad.sort();
        squad.unique();
        for (auto squadID : squad) {
            if (!IsSquadID(squadID))       // if squadID is invalid, remove it from map!!
                continue;
            bData = BoostData();
            if (fBoost) {
                bData.armored   = fData.armored;
                bData.info      = fData.info;
                bData.mining    = fData.mining;
                bData.siege     = fData.siege;
                bData.skirmish  = fData.skirmish;
            }
            SquadData sData = SquadData();
            GetSquadData(squadID, sData);
            SetWingBoostData(sData.wingID, bData);
            bool sboost(false);
            SetSquadBoostData(squadID, bData, sboost);
            for (auto cur : sData.members)
                if (sboost and CanTakeBoost(cur.second, sData.booster))
                    memberUpdateMap.emplace(cur.second->GetShipSE(), sData.boost);
        }
    }

    // update boost effects on these members' ships using updated boost levels
    // this is for fleet boost only, as modules will apply/remove their effects using the FxSystem
    for (auto cur : memberUpdateMap)
        if (cur.first != nullptr)
            cur.first->ApplyBoost(cur.second);

    _log( FLEET__TRACE, "FleetService::UpdateBoost() - Updated %lu members of fleetID: %u in %.2fus.  fleet: %s, wing: %s, squad: %s", \
            memberUpdateMap.size(), fleetID, GetTimeUSeconds() - start, (fleet ? "true" : "false"), (wing.empty() ? "false" : "true"), (squad.empty() ? "false" : "true"));

    // whatever changed the skill boosts changes who gets which links
    UpdateLinks(fleetID);
}

bool FleetService::FleetBoostPasses(uint32 fleetID)
{
    std::map<uint32, FleetData>::iterator itr = m_fleetDataMap.find(fleetID);
    if ((itr == m_fleetDataMap.end()) or (itr->second.leader == nullptr) or !itr->second.leader->IsInSpace())
        return false;
    return (m_fleetWings.count(fleetID) <= itr->second.leader->GetChar()->GetSkillLevel(EvESkill::FleetCommand));
}

bool FleetService::WingBoostPasses(uint32 wingID)
{
    std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
    if ((itr == m_wingDataMap.end()) or (itr->second.leader == nullptr) or !itr->second.leader->IsInSpace())
        return false;
    return (m_wingSquads.count(wingID) <= itr->second.leader->GetChar()->GetSkillLevel(EvESkill::WingCommand));
}

bool FleetService::SquadBoostPasses(uint32 squadID)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if ((itr == m_squadDataMap.end()) or (itr->second.leader == nullptr) or !itr->second.leader->IsInSpace())
        return false;
    return (itr->second.members.size() <= (itr->second.leader->GetChar()->GetSkillLevel(EvESkill::Leadership) * 2));
}

void FleetService::ApplyLinksTo(Client* pMember, const std::vector<Client*>& boosters)
{
    if ((pMember == nullptr) or (pMember->GetShipSE() == nullptr))
        return;

    LinkMap wanted;
    if (pMember->IsInSpace()) {
        // strongest module of each link effect among the boosters (no stacking of the same link)
        std::map<uint16, GenericModule*> best;
        std::map<uint16, float> bestScore;
        std::set<Client*> seen;
        for (auto pBooster : boosters) {
            if ((pBooster == nullptr) or (seen.find(pBooster) != seen.end()))
                continue;
            seen.insert(pBooster);
            if (!pBooster->IsInSpace() or (pBooster->GetShipSE() == nullptr) or (pBooster->GetSystemID() != pMember->GetSystemID()))
                continue;
            std::vector<GenericModule*> modules;
            pBooster->GetShip()->GetModuleManager()->GetActiveModules(modules);
            for (auto pMod : modules) {
                if ((pMod->groupID() != EVEDB::invGroups::Gang_Coordinator) or !pMod->IsActiveModule())
                    continue;
                uint16 effectID(pMod->GetActiveModule()->GetEffectID());
                if (effectID == 0)
                    continue;
                float score(0.0f);
                for (auto& lMod : GangLinkModule::GetModifiers(effectID))
                    score += std::fabs(pMod->GetAttribute(lMod.srcAttr).get_float());
                std::map<uint16, float>::iterator sItr = bestScore.find(effectID);
                if ((sItr == bestScore.end()) or (score > sItr->second)) {
                    best[effectID] = pMod;
                    bestScore[effectID] = score;
                }
            }
        }

        ShipItemRef shipRef(pMember->GetShip());
        for (auto& cur : best) {
            for (auto& lMod : GangLinkModule::GetModifiers(cur.first)) {
                float mult(1.0f + (cur.second->GetAttribute(lMod.srcAttr).get_float() / 100.0f));
                if (lMod.skillID == 0) {
                    std::pair<uint32, uint16> key(shipRef->itemID(), lMod.targetAttr);
                    LinkMap::iterator itr = wanted.find(key);
                    wanted[key] = (itr == wanted.end() ? mult : itr->second * mult);
                    continue;
                }
                std::vector<InventoryItemRef> modules;
                shipRef->GetModuleManager()->GetModuleListByReqSkill(lMod.skillID, modules);
                for (auto mRef : modules) {
                    if (!mRef->HasAttribute(lMod.targetAttr))
                        continue;
                    std::pair<uint32, uint16> key(mRef->itemID(), lMod.targetAttr);
                    LinkMap::iterator itr = wanted.find(key);
                    wanted[key] = (itr == wanted.end() ? mult : itr->second * mult);
                }
            }
        }
    }

    pMember->GetShipSE()->ApplyLinkBonuses(wanted);
}

void FleetService::UpdateLinks(uint32 fleetID)
{
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr == m_fleetDataMap.end())
        return;

    bool fleetPasses(FleetBoostPasses(fleetID));
    std::set<Client*> done;
    std::vector<uint32> wingIDs, squadIDs;
    GetWingIDs(fleetID, wingIDs);
    for (auto wingID : wingIDs) {
        std::map<uint32, WingData>::iterator wItr = m_wingDataMap.find(wingID);
        if (wItr == m_wingDataMap.end())
            continue;
        bool wingPasses(WingBoostPasses(wingID));
        squadIDs.clear();
        GetSquadIDs(wingID, squadIDs);
        for (auto squadID : squadIDs) {
            std::map<uint32, SquadData>::iterator sItr = m_squadDataMap.find(squadID);
            if (sItr == m_squadDataMap.end())
                continue;
            std::vector<Client*> boosters;
            if (SquadBoostPasses(squadID)) {
                boosters.push_back(sItr->second.booster);
                if (wingPasses) {
                    boosters.push_back(wItr->second.booster);
                    if (fleetPasses)
                        boosters.push_back(fItr->second.booster);
                }
            }
            for (auto cur : sItr->second.members) {
                ApplyLinksTo(cur.second, boosters);
                done.insert(cur.second);
            }
        }
        // the wing commander sits above the squads
        if ((wItr->second.leader != nullptr) and (done.find(wItr->second.leader) == done.end())) {
            std::vector<Client*> boosters;
            if (wingPasses) {
                boosters.push_back(wItr->second.booster);
                if (fleetPasses)
                    boosters.push_back(fItr->second.booster);
            }
            ApplyLinksTo(wItr->second.leader, boosters);
            done.insert(wItr->second.leader);
        }
    }
    if ((fItr->second.leader != nullptr) and (done.find(fItr->second.leader) == done.end())) {
        std::vector<Client*> boosters;
        if (fleetPasses)
            boosters.push_back(fItr->second.booster);
        ApplyLinksTo(fItr->second.leader, boosters);
        done.insert(fItr->second.leader);
    }
    // anyone else (no position yet) gets nothing
    auto range = m_fleetMembers.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        if (done.find(itr->second) == done.end())
            ApplyLinksTo(itr->second, std::vector<Client*>());
}

void FleetService::SetWingBoostData(uint32 wingID, BoostData& bData)
{
    bool boost(false);
    int8 leader(0), armored(0), info(0), mining(0), siege(0), skirmish(0);
    std::map<uint32, WingData>::iterator wItr = m_wingDataMap.find(wingID);
    if (wItr == m_wingDataMap.end())
        return;
    if ((wItr->second.leader != nullptr) and (wItr->second.leader->IsInSpace()))
        if (m_wingSquads.count(wingID) <= wItr->second.leader->GetChar()->GetSkillLevel(EvESkill::WingCommand)) {
            if ((wItr->second.booster != nullptr) and (wItr->second.booster->IsInSpace()))
                if (wItr->second.leader->GetSystemID() == wItr->second.booster->GetSystemID()) {
                    Character* pChar = wItr->second.booster->GetChar().get();
                    if (pChar != nullptr) {
                        leader = wItr->second.leader->GetChar()->GetSkillLevel(EvESkill::Leadership);    // this applies ONLY to self
                        if (pChar->HasSkillTrainedToLevel(EvESkill::ArmoredWarfare, 1))
                            armored     = pChar->GetSkillLevel(EvESkill::ArmoredWarfare);
                        if (pChar->HasSkillTrainedToLevel(EvESkill::InformationWarfare, 1))
                            info        = pChar->GetSkillLevel(EvESkill::InformationWarfare);
                        if (pChar->HasSkillTrainedToLevel(EvESkill::SiegeWarfare, 1))
                            siege       = pChar->GetSkillLevel(EvESkill::SiegeWarfare);
                        if (pChar->HasSkillTrainedToLevel(EvESkill::SkirmishWarfare, 1))
                            skirmish    = pChar->GetSkillLevel(EvESkill::SkirmishWarfare);
                        if (pChar->HasSkillTrainedToLevel(EvESkill::MiningForeman, 1))
                            mining      = pChar->GetSkillLevel(EvESkill::MiningForeman);
                    }
                    boost = true;
                    wItr->second.boost.armored  = ((armored < bData.armored)   ? bData.armored   : armored);
                    wItr->second.boost.info     = ((info < bData.info)         ? bData.info      : info);
                    wItr->second.boost.mining   = ((mining < bData.mining)     ? bData.mining    : mining);
                    wItr->second.boost.siege    = ((siege < bData.siege)       ? bData.siege     : siege);
                    wItr->second.boost.skirmish = ((skirmish < bData.skirmish) ? bData.skirmish  : skirmish);
                } else {
                    wItr->second.boost = BoostData();
                }
            // this is for WC only.  will always get own skill, and here they get their wing boost, also
            wItr->second.boost.leader   = leader;
            if (wItr->second.leader->GetShipSE() != nullptr)
                wItr->second.leader->GetShipSE()->ApplyBoost(wItr->second.boost);
        }

    if (!boost)
        wItr->second.boost = BoostData();

    _log( FLEET__TRACE, "BoostData - WB: %s, wingID: %u - leader: %i, armored: %i, info: %i, siege: %i, skirmish: %i, mining: %i", \
            (boost ? "true" : "false"), wingID, wItr->second.boost.leader, wItr->second.boost.armored, wItr->second.boost.info, \
            wItr->second.boost.siege, wItr->second.boost.skirmish, wItr->second.boost.mining);
}

void FleetService::SetSquadBoostData(uint32 squadID, BoostData bData, bool& sboost)
{
    int8 leader(0), armored(0), info(0), mining(0), siege(0), skirmish(0);
    std::map<uint32, SquadData>::iterator sItr = m_squadDataMap.find(squadID);
    if (sItr == m_squadDataMap.end())
        return;
    if ((sItr->second.leader != nullptr) and (sItr->second.leader->IsInSpace()))
        if (sItr->second.members.size() <= (sItr->second.leader->GetChar()->GetSkillLevel(EvESkill::Leadership) * 2)) {
            if ((sItr->second.booster != nullptr) and (sItr->second.booster->IsInSpace()))
                if (sItr->second.leader->GetSystemID() == sItr->second.booster->GetSystemID()) {
                    Character* pChar = sItr->second.booster->GetChar().get();
                    if (pChar != nullptr) {
                        leader = sItr->second.leader->GetChar()->GetSkillLevel(EvESkill::Leadership);    // this applies ONLY to self
                        if (pChar->HasSkillTrainedToLevel(EvESkill::ArmoredWarfare, 1))
                            armored     = pChar->GetSkillLevel(EvESkill::ArmoredWarfare);
                        if (pChar->HasSkillTrainedToLevel(EvESkill::InformationWarfare, 1))
                            info        = pChar->GetSkillLevel(EvESkill::InformationWarfare);
                        if (pChar->HasSkillTrainedToLevel(EvESkill::SiegeWarfare, 1))
                            siege       = pChar->GetSkillLevel(EvESkill::SiegeWarfare);
                        if (pChar->HasSkillTrainedToLevel(EvESkill::SkirmishWarfare, 1))
                            skirmish    = pChar->GetSkillLevel(EvESkill::SkirmishWarfare);
                        if (pChar->HasSkillTrainedToLevel(EvESkill::MiningForeman, 1))
                            mining      = pChar->GetSkillLevel(EvESkill::MiningForeman);
                    }
                    sItr->second.boost.armored  = ((armored < bData.armored)   ? bData.armored   : armored);
                    sItr->second.boost.info     = ((info < bData.info)         ? bData.info      : info);
                    sItr->second.boost.mining   = ((mining < bData.mining)     ? bData.mining    : mining);
                    sItr->second.boost.siege    = ((siege < bData.siege)       ? bData.siege     : siege);
                    sItr->second.boost.skirmish = ((skirmish < bData.skirmish) ? bData.skirmish  : skirmish);
                } else {
                    sItr->second.boost = BoostData();
                }
            // squad will always get this if SC is skilled
            sboost = true;
            sItr->second.boost.leader = leader;
        }

    if (!sboost)
        sItr->second.boost = BoostData();

    _log( FLEET__TRACE, "BoostData - SB: %s, squadID: %u - leader: %i, armored: %i, info: %i, siege: %i, skirmish: %i, mining: %i", \
            (sboost ? "true" : "false"), squadID, sItr->second.boost.leader, sItr->second.boost.armored, sItr->second.boost.info, \
            sItr->second.boost.siege, sItr->second.boost.skirmish, sItr->second.boost.mining);
}

void FleetService::UpdateOptions(uint32 fleetID, bool isFreeMove, bool isRegistered, bool isVoiceEnabled)
{
    std::map<uint32, FleetData>::iterator itr = m_fleetDataMap.find(fleetID);
    if (itr == m_fleetDataMap.end())
        return;
    // if nothing changed, do nothing
    if ((itr->second.isFreeMove == isFreeMove) and (itr->second.isRegistered == isRegistered) and (itr->second.isVoiceEnabled == isVoiceEnabled))
        return;

    PyDict* was = new PyDict();
        was->SetItemString("isFreeMove",        new PyBool(itr->second.isFreeMove));
        was->SetItemString("isRegistered",      new PyBool(itr->second.isRegistered));
        was->SetItemString("isVoiceEnabled",    new PyBool(itr->second.isVoiceEnabled));
    PyDict* is = new PyDict();
        is->SetItemString("isFreeMove",         new PyBool(isFreeMove));
        is->SetItemString("isRegistered",       new PyBool(isRegistered));
        is->SetItemString("isVoiceEnabled",     new PyBool(isVoiceEnabled));
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyObject("util.KeyVal", was));
        tuple->SetItem(1, new PyObject("util.KeyVal", is));
    SendFleetUpdate(fleetID, "OnFleetOptionsChanged", tuple);

    _log(FLEET__TRACE, "FleetService::UpdateOptions() - fleetID: %u FreeMove: %s, Registered: %s, Voice: %s", \
            fleetID, isFreeMove ? "true" : "false", isRegistered ? "true" : "false", isVoiceEnabled ? "true" : "false");
    itr->second.isFreeMove = isFreeMove;
    itr->second.isRegistered = isRegistered;
    itr->second.isVoiceEnabled = isVoiceEnabled;
}

PyRep* FleetService::GetMOTD(uint32 fleetID)
{
    // the client uses this as the motd string (fleetSvc.GetMotd, lscchannel)
    std::map<uint32, FleetData>::iterator itr = m_fleetDataMap.find(fleetID);
    if (itr == m_fleetDataMap.end())
        return new PyString("");
    return new PyString(itr->second.motd);
}

void FleetService::SetMOTD(uint32 fleetID, std::string motd)
{
    std::map<uint32, FleetData>::iterator itr = m_fleetDataMap.find(fleetID);
    if (itr != m_fleetDataMap.end())
        itr->second.motd = motd;

    PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, new PyString(motd));
    SendFleetUpdate(fleetID, "OnFleetMotdChanged", tuple);

    // the client draws the fleet channel's motd from fleet.GetMotd(); keep the channel's own copy in step
    if (m_lsc != nullptr)
        m_lsc->SetSystemChannelMOTD(fleetID, motd);
}

void FleetService::RenameWing(uint32 wingID, std::string name)
{
    std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
    if (itr == m_wingDataMap.end())
        return;

    itr->second.name = name;

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(wingID));
        tuple->SetItem(1, new PyString(name));
    SendFleetUpdate(itr->second.fleetID, "OnFleetWingNameChanged", tuple);

    _log(FLEET__TRACE, "FleetService::RenameWing() %u to %s", wingID, name.c_str());
}

void FleetService::RenameSquad(uint32 squadID, std::string name)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if (itr == m_squadDataMap.end())
        return;

    itr->second.name = name;

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(squadID));
        tuple->SetItem(1, new PyString(name));
    SendFleetUpdate(itr->second.fleetID, "OnFleetSquadNameChanged", tuple);

    _log(FLEET__TRACE, "FleetService::RenameSquad() %u to %s", squadID, name.c_str());
}

bool FleetService::IsWingActive(int32 wingID)
{
    auto range = m_wingSquads.equal_range(wingID);
    for (auto itr = range.first; itr != range.second; ++itr) {
        std::map<uint32, SquadData>::iterator sItr = m_squadDataMap.find(itr->second);
        if (sItr != m_squadDataMap.end())
            if (sItr->second.members.size() > 0)
                return true;
    }
    return false;
}

void FleetService::GetSquadIDs(uint32 wingID, std::vector< uint32 >& squadIDs)
{
    auto range = m_wingSquads.equal_range(wingID);
    for (auto itr = range.first; itr != range.second; ++itr)
        squadIDs.push_back(itr->second);
}

void FleetService::GetWingIDs(uint32 fleetID, std::vector< uint32 >& wingIDs)
{
    auto range = m_fleetWings.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        wingIDs.push_back(itr->second);
}

void FleetService::GetFleetData(uint32 fleetID, FleetData& data)
{
    std::map<uint32, FleetData>::iterator itr = m_fleetDataMap.find(fleetID);
    if (itr != m_fleetDataMap.end())
        data = itr->second;
}

void FleetService::GetWingData(uint32 wingID, WingData& data)
{
    std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
    if (itr != m_wingDataMap.end())
        data = itr->second;
}

void FleetService::GetSquadData(uint32 squadID, SquadData& data)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if (itr != m_squadDataMap.end())
        data = itr->second;
}

void FleetService::GetMemeberVec(uint32 fleetID, std::vector< Client* >& data)
{
    auto range = m_fleetMembers.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        data.push_back(itr->second);
}

PyRep* FleetService::GetFleetAdvert(uint32 fleetID)
{
    Client* pClient(nullptr);
    std::map<uint32, FleetAdvert>::iterator itr = m_fleetAdvertMap.find(fleetID);
    if (itr == m_fleetAdvertMap.end())
        return nullptr;

    pClient = itr->second.leader;
    if (pClient == nullptr)
        return nullptr;
    AvalibleFleetsRSP fleetRSP;
        fleetRSP.fleetID = itr->first;
        fleetRSP.local_minSecurity = itr->second.local_minSecurity;
        fleetRSP.description = itr->second.description;
        fleetRSP.public_minStanding = itr->second.public_minStanding;
        fleetRSP.fleetName = itr->second.fleetName;
        fleetRSP.advertTime = itr->second.advertTime;
        fleetRSP.dateCreated = itr->second.dateCreated;
        fleetRSP.joinNeedsApproval = itr->second.joinNeedsApproval;

    PyTuple* localTuple = new PyTuple(1);
    PyList* localList = new PyList();
    for (auto cur1 : itr->second.local_allowedEntities) // corpID, allianceID, militiaID - if applicable
        localList->AddItemInt(cur1);
    localTuple->SetItem(0, localList);
    PyToken* token = new PyToken("__builtin__.set");
    PyTuple* localTuple2 = new PyTuple(2);
        localTuple2->SetItem(0, token);
        localTuple2->SetItem(1, localTuple);
        fleetRSP.local_allowedEntities = new PyObjectEx(false, localTuple2);

    PyTuple* publicTuple = new PyTuple(1);
    PyList* publicList = new PyList();
    for (auto cur2 : itr->second.public_allowedEntities)    // searches creator's addy book and addes charIDs based on standings
        publicList->AddItemInt(cur2);
        publicTuple->SetItem(0, publicList);
    PyTuple* publicTuple2 = new PyTuple(2);
        PyIncRef(token);
        publicTuple2->SetItem(0, token);
        publicTuple2->SetItem(1, publicTuple);
        fleetRSP.public_allowedEntities = new PyObjectEx(false, publicTuple2);

        fleetRSP.local_minStanding = itr->second.local_minStanding;
        fleetRSP.numMembers = m_fleetMembers.count(itr->first);
        fleetRSP.hideInfo = itr->second.hideInfo;
        fleetRSP.public_minSecurity = itr->second.public_minSecurity;
        fleetRSP.inviteScope = itr->second.inviteScope;
        fleetRSP.solarSystemID = itr->second.solarSystemID;
        fleetRSP.charID = pClient->GetCharacterID();
        fleetRSP.corpID = pClient->GetCorporationID();
        fleetRSP.warFactionID = pClient->GetWarFactionID();
        fleetRSP.securityStatus = pClient->GetSecurityRating();
        fleetRSP.allianceID = pClient->GetAllianceID();

    fleetRSP.Dump(FLEET__DEBUG);
    return fleetRSP.Encode();
}

bool FleetService::SetInviteScope(uint32 fleetID, uint8 scope, Client* pBoss)
{
    if (pBoss == nullptr)
        return false;
    if (scope == Fleet::Invite::Closed) {
        RemoveFleetAdvert(fleetID);
        return true;
    }

    FleetAdvert data = FleetAdvert();
    std::map<uint32, FleetAdvert>::iterator itr = m_fleetAdvertMap.find(fleetID);
    if (itr != m_fleetAdvertMap.end()) {
        data = itr->second;
    } else {
        FleetData fData = FleetData();
        GetFleetData(fleetID, fData);
        data.fleetID = fleetID;
        data.hideInfo = false;
        data.joinNeedsApproval = false;
        data.fleetName = std::string(pBoss->GetName()) + "'s fleet";
        data.dateCreated = (fData.dateCreated > 0 ? fData.dateCreated : GetFileTimeNow());
        data.local_minSecurity = -10;
        data.local_minStanding = -10;
        data.public_minSecurity = -10;
        data.public_minStanding = -10;
    }
    data.inviteScope = scope;
    data.leader = pBoss;
    data.solarSystemID = pBoss->GetSystemID();
    data.advertTime = GetFileTimeNow();

    /* the client's register window lists the boss's corp, alliance and militia for the local scopes (fleetregister.py).
     *  keep any other entities (standings-based lists) and set ours by the scope bits
     */
    std::vector<uint32>& local = data.local_allowedEntities;
    uint32 ours[3] = { static_cast<uint32>(pBoss->GetCorporationID()), static_cast<uint32>(pBoss->GetAllianceID()),
                       static_cast<uint32>(pBoss->GetWarFactionID()) };
    uint8 bits[3] = { Fleet::Invite::Corp, Fleet::Invite::Alliance, Fleet::Invite::Militia };
    for (int i = 0; i < 3; ++i) {
        if (ours[i] == 0)
            continue;
        local.erase(std::remove(local.begin(), local.end(), ours[i]), local.end());
        if (scope & bits[i])
            local.push_back(ours[i]);
    }

    CreateFleetAdvert(fleetID, data);
    return true;
}

void FleetService::RemoveFleetAdvert(uint32 fleetID)
{
    _log(FLEET__TRACE, "RemoveFleetAdvert for FleetID: %u", fleetID);
    m_fleetAdvertMap.erase(fleetID);
    // set options to remove registration
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr != m_fleetDataMap.end())
        UpdateOptions(fleetID, fItr->second.isFreeMove, /*isRegistered*/false, fItr->second.isVoiceEnabled);
}

void FleetService::DeleteFleet(uint32 fleetID)
{
    _log(FLEET__INFO, "FleetService::DeleteFleet() - fleetID: %u", fleetID);

    RemoveFleetAdvert(fleetID);

    std::vector<uint32> wings, squads;
    GetWingIDs(fleetID, wings);
    for (auto wing : wings) {
        squads.clear();
        GetSquadIDs(wing, squads);
        for (auto squad : squads) {
            m_squadDataMap.erase(squad);
            if (m_lsc != nullptr)
                m_lsc->DeleteSystemChannel(squad);
        }
        m_wingSquads.erase(wing);
        m_wingDataMap.erase(wing);
        if (m_lsc != nullptr)
            m_lsc->DeleteSystemChannel(wing);
    }
    m_fleetWings.erase(fleetID);
    m_fleetDataMap.erase(fleetID);
    m_fleetMembers.erase(fleetID);
    m_joinReq.erase(fleetID);

    for (auto itr = m_beacons.begin(); itr != m_beacons.end(); )
        if (itr->second.fleetID == fleetID) {
            itr = m_beacons.erase(itr);
        } else {
            ++itr;
        }
    for (auto itr = m_bridges.begin(); itr != m_bridges.end(); )
        if (!itr->second.alliance and (itr->second.fleetID == fleetID)) {
            itr = m_bridges.erase(itr);
        } else {
            ++itr;
        }

    for (auto itr = m_inviteData.begin(); itr != m_inviteData.end(); )
        if (itr->second.fleetID == fleetID) {
            itr = m_inviteData.erase(itr);
        } else {
            ++itr;
        }

    if (m_lsc != nullptr)
        m_lsc->DeleteSystemChannel(fleetID);
}

void FleetService::DeleteWing(uint32 wingID)
{
    std::map<uint32, WingData>::iterator wItr = m_wingDataMap.find(wingID);
    if (wItr == m_wingDataMap.end())
        return;

    uint32 fleetID = wItr->second.fleetID;

    std::vector<uint32> squads;
    GetSquadIDs(wingID, squads);
    std::map<uint32, SquadData>::iterator sItr;
    for (auto cur : squads) {
        sItr = m_squadDataMap.find(cur);
        if (sItr == m_squadDataMap.end())
            continue;
        DecFleetSquads(fleetID, wingID);
        m_squadDataMap.erase(sItr);
        if (m_lsc != nullptr)
            m_lsc->DeleteSystemChannel(cur);
    }
    m_wingSquads.erase(wingID);

    // remove this wing from the fleet's wing list
    auto range = m_fleetWings.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        if (itr->second == wingID) {
            m_fleetWings.erase(itr);
            break;
        }

    PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, new PyInt(wingID));
    SendFleetUpdate(fleetID, "OnFleetWingDeleted", tuple);

    m_wingDataMap.erase(wItr);
    if (m_lsc != nullptr)
        m_lsc->DeleteSystemChannel(wingID);
}

void FleetService::DeleteSquad(uint32 squadID)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if (itr == m_squadDataMap.end())
        return;

    DecFleetSquads(itr->second.fleetID, itr->second.wingID);

    // remove this squad from the wing's squad list
    auto range = m_wingSquads.equal_range(itr->second.wingID);
    for (auto sItr = range.first; sItr != range.second; ++sItr)
        if (sItr->second == squadID) {
            m_wingSquads.erase(sItr);
            break;
        }

    PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, new PyInt(squadID));
    SendFleetUpdate(itr->second.fleetID, "OnFleetSquadDeleted", tuple);

    m_squadDataMap.erase(itr);
    if (m_lsc != nullptr)
        m_lsc->DeleteSystemChannel(squadID);
}

void FleetService::IncFleetSquads(uint32 fleetID, uint32 wingID)
{
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr == m_fleetDataMap.end())
        return;
    ++(fItr->second.squads);
}

void FleetService::DecFleetSquads(uint32 fleetID, uint32 wingID)
{
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr == m_fleetDataMap.end())
        return;
    --(fItr->second.squads);
}

Client* FleetService::GetFleetLeader(uint32 fleetID)
{
    std::map<uint32, FleetData>::iterator itr = m_fleetDataMap.find(fleetID);
    if (itr == m_fleetDataMap.end())
        return nullptr;
    return itr->second.leader;
}

uint32 FleetService::GetFleetLeaderID(uint32 fleetID)
{
    std::map<uint32, FleetData>::iterator itr = m_fleetDataMap.find(fleetID);
    if ((itr == m_fleetDataMap.end()) or (itr->second.leader == nullptr))
        return 0;
    return itr->second.leader->GetCharacterID();
}

Client* FleetService::GetWingLeader(uint32 wingID)
{
    std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
    if (itr == m_wingDataMap.end())
        return nullptr;
    return itr->second.leader;
}

uint32 FleetService::GetWingLeaderID(uint32 wingID)
{
    std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
    if ((itr == m_wingDataMap.end()) or (itr->second.leader == nullptr))
        return 0;
    return itr->second.leader->GetCharacterID();
}

Client* FleetService::GetSquadLeader(uint32 squadID)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if (itr == m_squadDataMap.end())
        return nullptr;
    return itr->second.leader;
}

uint32 FleetService::GetSquadLeaderID(uint32 squadID)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if ((itr == m_squadDataMap.end()) or (itr->second.leader == nullptr))
        return 0;
    return itr->second.leader->GetCharacterID();
}

std::string FleetService::GetFleetName(uint32 fleetID)
{
    std::map<uint32, FleetData>::iterator itr = m_fleetDataMap.find(fleetID);
    if (itr == m_fleetDataMap.end())
        return "Fleet";
    return itr->second.name;
}

std::string FleetService::GetWingName(uint32 wingID)
{
    std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
    if (itr == m_wingDataMap.end())
        return "Wing";
    return itr->second.name;
}

std::string FleetService::GetSquadName(uint32 squadID)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if (itr == m_squadDataMap.end())
        return "Squad";
    return itr->second.name;
}

std::string FleetService::GetFleetDescription(uint32 fleetID)
{
    std::map<uint32, FleetAdvert>::iterator itr = m_fleetAdvertMap.find(fleetID);
    if (itr == m_fleetAdvertMap.end())
        return "No Info";
    return itr->second.description;
}

bool FleetService::GetRandUnitIDs(uint32 fleetID, int32& wingID, int32& squadID, int32 restrictWingID/*0*/, int32 restrictSquadID/*0*/)
{
    // wings and squads are kept in creation order.  use the first squad with room for another member
    auto wRange = m_fleetWings.equal_range(fleetID);
    for (auto wItr = wRange.first; wItr != wRange.second; ++wItr) {
        if ((restrictWingID > 0) and (wItr->second != (uint32)restrictWingID))
            continue;
        auto sRange = m_wingSquads.equal_range(wItr->second);
        for (auto sItr = sRange.first; sItr != sRange.second; ++sItr) {
            if ((restrictSquadID > 0) and (sItr->second != (uint32)restrictSquadID))
                continue;
            if (GetSquadMemberCount(sItr->second) < Fleet::Limits::MaxSquadMembers) {
                wingID = wItr->second;
                squadID = sItr->second;
                return true;
            }
        }
    }

    return false;
}

void FleetService::LeaveFleet(Client* pClient)
{
    if (pClient == nullptr)
        return;
    Character* pChar = pClient->GetChar().get();
    if (pChar == nullptr)
        return;
    uint32 fleetID = pChar->fleetID();
    if (fleetID == 0)
        return;

    // the boss role passes to the next in command before this member is removed
    if (IsBoss(pChar) and (m_fleetMembers.count(fleetID) > 1))
        PassBoss(fleetID, pClient);

    PyTuple* tuple = new PyTuple(1);
    tuple->SetItem(0, new PyInt(pChar->itemID()));
    SendFleetUpdate(fleetID, "OnFleetLeave", tuple);

    RemoveMember(pClient);

    // the leaver's cyno can no longer be used by the fleet (the field stays lit until its cycle ends)
    uint32 charID(pChar->itemID());
    RemoveBeacon(charID);
    // and its fleet bridge closes for the fleet (the portal generator still runs its cycle)
    for (auto itr = m_bridges.begin(); itr != m_bridges.end(); ++itr)
        if (!itr->second.alliance and (itr->second.charID == charID) and (itr->second.fleetID == fleetID)) {
            RemoveBridge(itr->first);
            break;
        }

    // the leaver's watch list goes, and nobody watches the leaver any more
    m_watchlists.erase(charID);
    for (auto itr = m_watchlists.begin(); itr != m_watchlists.end(); ) {
        itr->second.erase(charID);
        if (itr->second.empty()) {
            itr = m_watchlists.erase(itr);
        } else {
            ++itr;
        }
    }

    // links stop for the leaver, and for anyone the leaver was boosting
    if (pClient->GetShipSE() != nullptr)
        pClient->GetShipSE()->RemoveLinkBonuses();
    UpdateLinks(fleetID);

    CharFleetData fleet = CharFleetData();
    //call updates on fleet session data
    pChar->SetFleetData(fleet);

    // no pointer to this client may stay in the fleet data
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if ((fItr != m_fleetDataMap.end()) and (fItr->second.creator == pClient))
        fItr->second.creator = nullptr;
    std::map<uint32, FleetAdvert>::iterator aItr = m_fleetAdvertMap.find(fleetID);
    if ((aItr != m_fleetAdvertMap.end()) and (aItr->second.leader == pClient)) {
        if ((fItr != m_fleetDataMap.end()) and (fItr->second.creator != nullptr)) {
            aItr->second.leader = fItr->second.creator;
        } else {
            m_fleetAdvertMap.erase(aItr);
        }
    }

    // an empty fleet is deleted
    if (m_fleetMembers.count(fleetID) == 0)
        DeleteFleet(fleetID);
}

void FleetService::ClientLogout(Client* pClient)
{
    if (pClient == nullptr)
        return;

    // drop invites issued by or to this client
    for (auto itr = m_inviteData.begin(); itr != m_inviteData.end(); )
        if ((itr->second.invited == pClient) or (itr->second.inviteBy == pClient)) {
            itr = m_inviteData.erase(itr);
        } else {
            ++itr;
        }

    // drop join requests from this client
    for (auto itr = m_joinReq.begin(); itr != m_joinReq.end(); )
        if (itr->second == pClient) {
            itr = m_joinReq.erase(itr);
        } else {
            ++itr;
        }

    Character* pChar = pClient->GetChar().get();
    if ((pChar != nullptr) and (pChar->fleetID() != 0)) {
        // the client asks to rejoin (fleet.Reconnect()) when it logs back in within RECONNECT_TIMEOUT
        SaveReconnectData(pClient);
        m_departing = pClient;
        LeaveFleet(pClient);
        m_departing = nullptr;
    }

    // drop fleet adverts still pointing to this client (LeaveFleet hands its own fleet's advert to the boss)
    for (auto itr = m_fleetAdvertMap.begin(); itr != m_fleetAdvertMap.end(); )
        if (itr->second.leader == pClient) {
            itr = m_fleetAdvertMap.erase(itr);
        } else {
            ++itr;
        }
}

void FleetService::PassBoss(uint32 fleetID, Client* pLeaving)
{
    Client* pFC(nullptr);
    Client* pWC(nullptr);
    Client* pSC(nullptr);
    Client* pOldest(nullptr);
    int64 oldest(0);

    auto range = m_fleetMembers.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr) {
        Client* pClient = itr->second;
        if ((pClient == nullptr) or (pClient == pLeaving))
            continue;
        Character* pChar = pClient->GetChar().get();
        if (pChar == nullptr)
            continue;
        switch (pChar->fleetRole()) {
            case Fleet::Role::FleetLeader: {
                if (pFC == nullptr)
                    pFC = pClient;
            } break;
            case Fleet::Role::WingLeader: {
                if (pWC == nullptr)
                    pWC = pClient;
            } break;
            case Fleet::Role::SquadLeader: {
                if (pSC == nullptr)
                    pSC = pClient;
            } break;
        }
        if ((pOldest == nullptr) or (pChar->fleetJoinTime() < oldest)) {
            pOldest = pClient;
            oldest = pChar->fleetJoinTime();
        }
    }

    Client* pNewBoss = pFC;
    if (pNewBoss == nullptr)
        pNewBoss = pWC;
    if (pNewBoss == nullptr)
        pNewBoss = pSC;
    if (pNewBoss == nullptr)
        pNewBoss = pOldest;
    if (pNewBoss == nullptr)
        return;

    _log(FLEET__INFO, "FleetService::PassBoss() - fleetID: %u boss passes from %s to %s", \
            fleetID, (pLeaving != nullptr ? pLeaving->GetName() : "(none)"), pNewBoss->GetName());
    SetBoss(fleetID, pNewBoss);
}

void FleetService::SetBoss(uint32 fleetID, Client* pNewBoss)
{
    if (pNewBoss == nullptr)
        return;
    Character* pNewChar = pNewBoss->GetChar().get();
    if (pNewChar == nullptr)
        return;

    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr == m_fleetDataMap.end())
        return;

    // clear the boss job on every other member (there should only be one)
    std::vector<Client*> members;
    GetMemeberVec(fleetID, members);
    for (auto pClient : members) {
        if ((pClient == nullptr) or (pClient == pNewBoss))
            continue;
        Character* pChar = pClient->GetChar().get();
        if ((pChar == nullptr) or !IsBoss(pChar))
            continue;
        UpdateMember(pChar->itemID(), fleetID, 0, 0, pChar->fleetJob() & ~Fleet::Job::Creator, pChar->fleetRole(), pChar->fleetBooster());
    }

    if (!IsBoss(pNewChar))
        UpdateMember(pNewChar->itemID(), fleetID, 0, 0, pNewChar->fleetJob() | Fleet::Job::Creator, pNewChar->fleetRole(), pNewChar->fleetBooster());

    Client* pOldBoss = fItr->second.creator;
    fItr->second.creator = pNewBoss;

    std::map<uint32, FleetAdvert>::iterator aItr = m_fleetAdvertMap.find(fleetID);
    if ((aItr != m_fleetAdvertMap.end()) and ((aItr->second.leader == pOldBoss) or (aItr->second.leader == nullptr)))
        aItr->second.leader = pNewBoss;
}

bool FleetService::IsBoss(Character* pChar)
{
    if (pChar == nullptr)
        return false;
    return ((pChar->fleetJob() & Fleet::Job::Creator) == Fleet::Job::Creator);
}

bool FleetService::IsSuperior(Character* me, Character* target)
{
    if ((me == nullptr) or (target == nullptr))
        return false;
    if (me->fleetID() != target->fleetID())
        return false;
    if (me == target)
        return true;
    if (IsBoss(me) or (me->fleetRole() == Fleet::Role::FleetLeader))
        return true;
    if ((me->fleetRole() == Fleet::Role::WingLeader) and (me->wingID() > 0) and (target->wingID() == me->wingID()))
        return true;
    if ((me->fleetRole() == Fleet::Role::SquadLeader) and (me->squadID() > 0) and (target->squadID() == me->squadID()))
        return true;
    return false;
}

Client* FleetService::GetMember(uint32 fleetID, uint32 charID)
{
    auto range = m_fleetMembers.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        if ((itr->second != nullptr) and (itr->second->GetCharacterID() == charID))
            return itr->second;
    return nullptr;
}

bool FleetService::IsWingInFleet(uint32 wingID, uint32 fleetID)
{
    std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
    if (itr == m_wingDataMap.end())
        return false;
    return (itr->second.fleetID == fleetID);
}

bool FleetService::IsSquadInFleet(uint32 squadID, uint32 fleetID)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if (itr == m_squadDataMap.end())
        return false;
    return (itr->second.fleetID == fleetID);
}

uint32 FleetService::GetSquadMemberCount(uint32 squadID)
{
    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
    if (itr == m_squadDataMap.end())
        return 0;
    return itr->second.members.size();
}

uint32 FleetService::GetWingMemberCount(uint32 wingID)
{
    std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(wingID);
    if (itr == m_wingDataMap.end())
        return 0;

    // squad members plus the wing commander
    uint32 count(0);
    auto range = m_fleetMembers.equal_range(itr->second.fleetID);
    for (auto mItr = range.first; mItr != range.second; ++mItr) {
        if (mItr->second == nullptr)
            continue;
        Character* pChar = mItr->second->GetChar().get();
        if ((pChar != nullptr) and (pChar->wingID() == (int32)wingID))
            ++count;
    }
    return count;
}

Client* FleetService::GetPositionHolder(uint32 fleetID, int32 wingID, int32 squadID, int8 role)
{
    switch (role) {
        case Fleet::Role::FleetLeader:  return GetFleetLeader(fleetID);
        case Fleet::Role::WingLeader:   return (wingID > 0 ? GetWingLeader(wingID) : nullptr);
        case Fleet::Role::SquadLeader:  return (squadID > 0 ? GetSquadLeader(squadID) : nullptr);
    }
    return nullptr;
}

bool FleetService::IsCommander(Client* pClient)
{
    if ((pClient == nullptr) or !pClient->InFleet())
        return false;
    Character* pChar = pClient->GetChar().get();
    if (pChar == nullptr)
        return false;
    switch (pChar->fleetRole()) {
        case Fleet::Role::FleetLeader:  return true;
        case Fleet::Role::WingLeader:   return (pChar->wingID() > 0);
        case Fleet::Role::SquadLeader:  return (pChar->squadID() > 0);
    }
    return false;
}

void FleetService::GetSubordinates(Client* pCmdr, std::vector<Client*>& into, bool sameSystem/*true*/, bool sameBubble/*false*/)
{
    if (!IsCommander(pCmdr))
        return;
    Character* pCmdrChar = pCmdr->GetChar().get();
    int8 role(pCmdrChar->fleetRole());
    SystemBubble* pBubble(nullptr);
    if (sameBubble) {
        if ((pCmdr->GetShipSE() == nullptr) or !pCmdr->IsInSpace())
            return;
        pBubble = pCmdr->GetShipSE()->SysBubble();
        if (pBubble == nullptr)
            return;
    }

    auto range = m_fleetMembers.equal_range(pCmdr->GetFleetID());
    for (auto itr = range.first; itr != range.second; ++itr) {
        Client* pClient(itr->second);
        if ((pClient == nullptr) or (pClient == pCmdr) or (pClient == m_departing))
            continue;
        if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr))
            continue;
        Character* pChar = pClient->GetChar().get();
        if (pChar == nullptr)
            continue;
        if ((role == Fleet::Role::WingLeader) and (pChar->wingID() != pCmdrChar->wingID()))
            continue;
        if ((role == Fleet::Role::SquadLeader) and (pChar->squadID() != pCmdrChar->squadID()))
            continue;
        if (sameSystem and (pClient->GetSystemID() != pCmdr->GetSystemID()))
            continue;
        if (sameBubble and (pClient->GetShipSE()->SysBubble() != pBubble))
            continue;
        into.push_back(pClient);
    }
}

void FleetService::SetTargetTag(uint32 fleetID, uint32 itemID, const std::string& tag)
{
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr == m_fleetDataMap.end())
        return;

    std::map<uint32, std::string>& tags = fItr->second.targetTags;
    // a tag marks one item only
    for (auto itr = tags.begin(); itr != tags.end(); )
        if ((itr->second == tag) and (itr->first != itemID)) {
            itr = tags.erase(itr);
        } else {
            ++itr;
        }
    tags[itemID] = tag;

    _log(FLEET__TRACE, "FleetService::SetTargetTag() - fleetID: %u item %u tagged '%s'", fleetID, itemID, tag.c_str());

    PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, MakeFleetState(fleetID));
    SendFleetUpdate(fleetID, "OnFleetStateChange", tuple);
}

PyRep* FleetService::MakeFleetState(uint32 fleetID)
{
    PyDict* tags = new PyDict();
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr != m_fleetDataMap.end())
        for (auto& cur : fItr->second.targetTags)
            tags->SetItem(new PyInt(cur.first), new PyString(cur.second));
    PyDict* dict = new PyDict();
        dict->SetItemString("targetTags", tags);
    return new PyObject("util.KeyVal", dict);
}

// ("OnFleetDamageStateChange", (shipID, damageState)) ballpark event to pWatcher about pWatched's ship (same system, in space)
static void SendFleetDamageState(Client* pWatcher, Client* pWatched)
{
    if ((pWatcher == nullptr) or (pWatched == nullptr) or (pWatcher == pWatched))
        return;
    if (!pWatcher->IsInSpace() or !pWatched->IsInSpace() or (pWatcher->GetSystemID() != pWatched->GetSystemID()))
        return;
    ShipSE* pShip(pWatched->GetShipSE());
    if (pShip == nullptr)
        return;
    PyTuple* args = new PyTuple(2);
        args->SetItem(0, new PyInt(pShip->GetID()));
        args->SetItem(1, pShip->MakeDamageDetails());
    PyTuple* up = new PyTuple(2);
        up->SetItem(0, new PyString("OnFleetDamageStateChange"));
        up->SetItem(1, args);
    pWatcher->QueueDestinyUpdate(&up);   // consumed
}

void FleetService::SetWatchlist(Client* pClient, PyRep* fav)
{
    if (pClient == nullptr)
        return;
    uint32 charID(pClient->GetCharacterID());
    m_watchlists.erase(charID);
    // None means damage updates are off
    if (!pClient->InFleet() or (fav == nullptr) or fav->IsNone())
        return;

    std::vector<uint32> ids;
    if (fav->IsList()) {
        for (PyList::const_iterator itr = fav->AsList()->begin(); itr != fav->AsList()->end(); ++itr)
            ids.push_back(PyRep::IntegerValueU32(*itr));
    } else if (fav->IsTuple()) {
        for (PyTuple::const_iterator itr = fav->AsTuple()->begin(); itr != fav->AsTuple()->end(); ++itr)
            ids.push_back(PyRep::IntegerValueU32(*itr));
    } else {
        _log(FLEET__WARNING, "SetWatchlist() - %s sent a %s watch list", pClient->GetName(), fav->TypeString());
        return;
    }
    if (ids.size() > Fleet::Limits::MaxDamageSenders)
        throw UserError("FleetTooManyFavorites").AddFormatValue("num", new PyInt(Fleet::Limits::MaxDamageSenders));

    std::set<uint32> watched;
    for (auto id : ids)
        if ((id != charID) and (GetMember(pClient->GetFleetID(), id) != nullptr))
            watched.insert(id);
    if (watched.empty())
        return;
    m_watchlists[charID] = watched;

    // the current state of the watched ships nearby
    for (auto id : watched)
        SendFleetDamageState(pClient, GetMember(pClient->GetFleetID(), id));
}

void FleetService::SendWatchlistDamage(Client* pWatched)
{
    if ((pWatched == nullptr) or !pWatched->InFleet() or m_watchlists.empty())
        return;
    uint32 charID(pWatched->GetCharacterID());
    for (auto& cur : m_watchlists) {
        if (cur.second.find(charID) == cur.second.end())
            continue;
        SendFleetDamageState(GetMember(pWatched->GetFleetID(), cur.first), pWatched);
    }
}

void FleetService::SaveReconnectData(Client* pClient)
{
    if (pClient == nullptr)
        return;
    Character* pChar = pClient->GetChar().get();
    if ((pChar == nullptr) or (pChar->fleetID() == 0))
        return;

    // forget stale records
    int64 now(GetFileTimeNow());
    int64 limit((Fleet::Limits::ReconnectTimeout * EvE::Time::Minute) + (30 * EvE::Time::Second));
    for (auto itr = m_reconnect.begin(); itr != m_reconnect.end(); )
        if ((now - itr->second.time) > limit) {
            itr = m_reconnect.erase(itr);
        } else {
            ++itr;
        }

    ReconnectData data = ReconnectData();
        data.fleetID = pChar->fleetID();
        data.wingID = pChar->wingID();
        data.squadID = pChar->squadID();
        data.job = pChar->fleetJob();
        data.role = pChar->fleetRole();
        data.booster = pChar->fleetBooster();
        data.time = now;
    m_reconnect[pChar->itemID()] = data;
}

bool FleetService::Reconnect(Client* pClient, uint32 fleetID)
{
    if (pClient == nullptr)
        return false;
    Character* pChar = pClient->GetChar().get();
    if ((pChar == nullptr) or (pChar->fleetID() != 0))
        return false;

    std::map<uint32, ReconnectData>::iterator itr = m_reconnect.find(pChar->itemID());
    if (itr == m_reconnect.end())
        return false;
    ReconnectData data(itr->second);
    m_reconnect.erase(itr);

    // 2 minutes (RECONNECT_TIMEOUT), and 30s of grace for the client's 10s reconnect delay and login time
    int64 limit((Fleet::Limits::ReconnectTimeout * EvE::Time::Minute) + (30 * EvE::Time::Second));
    if ((data.fleetID != fleetID) or ((GetFileTimeNow() - data.time) > limit))
        return false;
    std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
    if (fItr == m_fleetDataMap.end())
        return false;
    if (GetFleetMemberCount(fleetID) >= Fleet::Limits::MaxMembers)
        return false;

    // the old command position, if nobody took it meanwhile
    int8 role(data.role);
    int32 wingID(data.wingID), squadID(data.squadID);
    switch (role) {
        case Fleet::Role::FleetLeader: {
            if (fItr->second.leader != nullptr)
                role = Fleet::Role::Member;
        } break;
        case Fleet::Role::WingLeader: {
            if (!IsWingInFleet(wingID, fleetID) or (GetWingLeader(wingID) != nullptr))
                role = Fleet::Role::Member;
        } break;
        case Fleet::Role::SquadLeader: {
            if (!IsSquadInFleet(squadID, fleetID) or (GetSquadLeader(squadID) != nullptr)
            or (GetSquadMemberCount(squadID) >= Fleet::Limits::MaxSquadMembers))
                role = Fleet::Role::Member;
        } break;
        default:
            role = Fleet::Role::Member;
    }

    int8 booster(Fleet::Booster::No);
    switch (role) {
        case Fleet::Role::FleetLeader: {
            wingID = -1;
            squadID = -1;
            if (fItr->second.booster == nullptr)
                booster = Fleet::Booster::Fleet;
        } break;
        case Fleet::Role::WingLeader: {
            squadID = -1;
            std::map<uint32, WingData>::iterator wItr = m_wingDataMap.find(wingID);
            if ((wItr != m_wingDataMap.end()) and (wItr->second.booster == nullptr))
                booster = Fleet::Booster::Wing;
        } break;
        case Fleet::Role::SquadLeader: {
            std::map<uint32, SquadData>::iterator sItr = m_squadDataMap.find(squadID);
            if ((sItr != m_squadDataMap.end()) and (sItr->second.booster == nullptr))
                booster = Fleet::Booster::Squad;
        } break;
        case Fleet::Role::Member: {
            // the old squad, else the first squad with room
            if ((squadID <= 0) or !IsSquadInFleet(squadID, fleetID) or (GetSquadMemberCount(squadID) >= Fleet::Limits::MaxSquadMembers))
                if (!GetRandUnitIDs(fleetID, wingID, squadID))
                    return false;
            // a squad booster keeps the job when it's still free
            if ((data.booster == Fleet::Booster::Squad) and (squadID == data.squadID)) {
                std::map<uint32, SquadData>::iterator sItr = m_squadDataMap.find(squadID);
                if ((sItr != m_squadDataMap.end()) and (sItr->second.booster == nullptr))
                    booster = Fleet::Booster::Squad;
            }
        } break;
    }

    _log(FLEET__INFO, "FleetService::Reconnect() - %s rejoins fleet %u (wing %i, squad %i, role %s)",
            pClient->GetName(), fleetID, wingID, squadID, GetRoleName(role).c_str());

    if (!AddMember(pClient, fleetID, wingID, squadID, Fleet::Job::None, role, booster))
        return false;
    // the boss job comes back only when nobody holds it
    if ((data.job & Fleet::Job::Creator) and (fItr->second.creator == nullptr))
        SetBoss(fleetID, pClient);
    return true;
}

// OnJumpBeaconChange(charID, solarsystemID, beaconID, active)  (fleetSvc.activeBeacon)
static void SendBeaconChange(Client* pClient, const BeaconData& data, bool active)
{
    if (pClient == nullptr)
        return;
    PyTuple* tuple = new PyTuple(4);
        tuple->SetItem(0, new PyInt(data.charID));
        tuple->SetItem(1, new PyInt(data.systemID));
        tuple->SetItem(2, new PyInt(data.beaconID));
        tuple->SetItem(3, new PyBool(active));
    pClient->SendNotification("OnJumpBeaconChange", "clientID", &tuple);
}

void FleetService::AddBeacon(const BeaconData& data)
{
    RemoveBeacon(data.charID);
    if (m_fleetDataMap.find(data.fleetID) == m_fleetDataMap.end())
        return;
    m_beacons[data.charID] = data;

    _log(FLEET__TRACE, "FleetService::AddBeacon() - char %u lit beacon %u in %u for fleet %u", data.charID, data.beaconID, data.systemID, data.fleetID);
    for (auto pClient : GetFleetClients(data.fleetID))
        if (pClient != m_departing)
            SendBeaconChange(pClient, data, true);
}

void FleetService::RemoveBeacon(uint32 charID, uint32 beaconID/*0*/)
{
    std::map<uint32, BeaconData>::iterator itr = m_beacons.find(charID);
    if (itr == m_beacons.end())
        return;
    if ((beaconID != 0) and (itr->second.beaconID != beaconID))
        return;
    BeaconData data(itr->second);
    m_beacons.erase(itr);

    _log(FLEET__TRACE, "FleetService::RemoveBeacon() - beacon %u of char %u removed from fleet %u", data.beaconID, charID, data.fleetID);
    for (auto pClient : GetFleetClients(data.fleetID))
        if (pClient != m_departing)
            SendBeaconChange(pClient, data, false);
}

bool FleetService::GetBeacon(uint32 charID, BeaconData& data)
{
    std::map<uint32, BeaconData>::iterator itr = m_beacons.find(charID);
    if (itr == m_beacons.end())
        return false;
    data = itr->second;
    return true;
}

// OnBridgeModeChange (fleetSvc.activeBridge) or OnAllianceBridgeModeChange (pwn.allianceBridgesByShip):
//  (shipID, toSolarsystemID, toBeaconID, active)
static void SendBridgeChange(Client* pClient, const BridgeData& data, bool active)
{
    if (pClient == nullptr)
        return;
    PyTuple* tuple = new PyTuple(4);
        tuple->SetItem(0, new PyInt(data.shipID));
        tuple->SetItem(1, new PyInt(data.toSystemID));
        tuple->SetItem(2, new PyInt(data.beaconID));
        tuple->SetItem(3, new PyBool(active));
    pClient->SendNotification((data.alliance ? "OnAllianceBridgeModeChange" : "OnBridgeModeChange"), "clientID", &tuple);
}

void FleetService::SendJumpState(Client* pClient)
{
    if ((pClient == nullptr) or !pClient->InFleet())
        return;
    for (auto& cur : m_beacons)
        if (cur.second.fleetID == pClient->GetFleetID())
            SendBeaconChange(pClient, cur.second, true);
    for (auto& cur : m_bridges)
        if (!cur.second.alliance and (cur.second.fleetID == pClient->GetFleetID()))
            SendBridgeChange(pClient, cur.second, true);
}

// everyone who may use the bridge:  the fleet, or the alliance's pilots in the bridging ship's system
static void GetBridgeAudience(const BridgeData& data, std::vector<Client*>& into)
{
    if (!data.alliance) {
        into = sFltSvc.GetFleetClients(data.fleetID);
        return;
    }
    SystemManager* pSystem(sEntityList.FindSystem(data.fromSystemID));
    if (pSystem == nullptr)
        return;
    std::vector<Client*> clients;
    pSystem->GetClientList(clients);
    for (auto pClient : clients)
        if ((pClient != nullptr) and ((uint32)pClient->GetAllianceID() == data.allianceID))
            into.push_back(pClient);
}

void FleetService::AddBridge(const BridgeData& data)
{
    RemoveBridge(data.shipID);
    m_bridges[data.shipID] = data;

    _log(FLEET__TRACE, "FleetService::AddBridge() - %s bridge from ship %u in %u to %u in %u", (data.alliance ? "alliance" : "fleet"),
            data.shipID, data.fromSystemID, data.beaconID, data.toSystemID);
    std::vector<Client*> clients;
    GetBridgeAudience(data, clients);
    for (auto pClient : clients)
        if (pClient != m_departing)
            SendBridgeChange(pClient, data, true);
}

void FleetService::RemoveBridge(uint32 shipID)
{
    std::map<uint32, BridgeData>::iterator itr = m_bridges.find(shipID);
    if (itr == m_bridges.end())
        return;
    BridgeData data(itr->second);
    m_bridges.erase(itr);

    _log(FLEET__TRACE, "FleetService::RemoveBridge() - bridge from ship %u closed", shipID);
    std::vector<Client*> clients;
    GetBridgeAudience(data, clients);
    for (auto pClient : clients)
        if (pClient != m_departing)
            SendBridgeChange(pClient, data, false);
}

bool FleetService::GetBridge(uint32 shipID, BridgeData& data)
{
    std::map<uint32, BridgeData>::iterator itr = m_bridges.find(shipID);
    if (itr == m_bridges.end())
        return false;
    data = itr->second;
    return true;
}

void FleetService::GetAllianceBridges(uint32 systemID, uint32 allianceID, PyList* into)
{
    if ((into == nullptr) or (allianceID == 0))
        return;
    for (auto& cur : m_bridges) {
        if (!cur.second.alliance or (cur.second.allianceID != allianceID) or (cur.second.fromSystemID != systemID))
            continue;
        PyTuple* tuple = new PyTuple(3);
            tuple->SetItem(0, new PyInt(cur.second.shipID));
            tuple->SetItem(1, new PyInt(cur.second.toSystemID));
            tuple->SetItem(2, new PyInt(cur.second.beaconID));
        into->AddItem(tuple);
    }
}

bool FleetService::CanSeeAdvert(uint32 fleetID, Client* pViewer)
{
    if (pViewer == nullptr)
        return false;
    std::map<uint32, FleetAdvert>::iterator itr = m_fleetAdvertMap.find(fleetID);
    if (itr == m_fleetAdvertMap.end())
        return false;
    // members (and the boss) always see their own fleet
    if (pViewer->GetFleetID() == fleetID)
        return true;

    const FleetAdvert& ad(itr->second);
    uint32 charID(pViewer->GetCharacterID());
    uint32 corpID(pViewer->GetCorporationID());
    uint32 allianceID(pViewer->GetAllianceID());
    uint32 factionID(pViewer->GetWarFactionID());
    float sec(pViewer->GetSecurityRating());
    auto listed = [](const std::vector<uint32>& list, uint32 id) {
        return ((id != 0) and (std::find(list.begin(), list.end(), id) != list.end()));
    };

    /* local access (corp, alliance, militia scopes):  the viewer belongs to a chosen group of the boss and is listed in
     *  local_allowedEntities (the client puts the corp/alliance/militia IDs there, or the boss's contacts at the chosen standing).
     *  a min security of 0 means none (the packet's none marker)
     */
    if (ad.inviteScope & (Fleet::Invite::Corp | Fleet::Invite::Alliance | Fleet::Invite::Militia)) {
        bool member(true);
        Client* pBoss(ad.leader);
        if (pBoss != nullptr) {
            member = (((ad.inviteScope & Fleet::Invite::Corp) and (corpID == (uint32)pBoss->GetCorporationID()))
                   or ((ad.inviteScope & Fleet::Invite::Alliance) and (allianceID != 0) and (allianceID == (uint32)pBoss->GetAllianceID()))
                   or ((ad.inviteScope & Fleet::Invite::Militia) and (factionID != 0) and (factionID == (uint32)pBoss->GetWarFactionID())));
        }
        if (member
        and (listed(ad.local_allowedEntities, charID) or listed(ad.local_allowedEntities, corpID)
            or listed(ad.local_allowedEntities, allianceID) or listed(ad.local_allowedEntities, factionID))
        and ((ad.local_minSecurity == 0) or (sec >= ad.local_minSecurity)))
            return true;
    }

    // public access:  listed in public_allowedEntities (contacts at the chosen standing).  no standing given = everyone
    if (ad.inviteScope & Fleet::Invite::Public) {
        if (((ad.public_minStanding == 0)
            or listed(ad.public_allowedEntities, charID) or listed(ad.public_allowedEntities, corpID)
            or listed(ad.public_allowedEntities, allianceID))
        and ((ad.public_minSecurity == 0) or (sec >= ad.public_minSecurity)))
            return true;
    }

    return false;
}

void FleetService::RemoveMember(Client* pClient)
{
    Character* pChar = pClient->GetChar().get();
    if (pChar == nullptr)
        return;

    uint32 fleetID = pChar->fleetID();

    // update fleet data
    if (fleetID) {
        std::map<uint32, FleetData>::iterator fItr = m_fleetDataMap.find(fleetID);
        if (fItr != m_fleetDataMap.end()) {
            if (pChar->fleetBooster() == Fleet::Booster::Fleet)
                fItr->second.booster = nullptr;
            if ((pChar->fleetRole() == Fleet::Role::FleetLeader) or (fItr->second.leader == pClient))
                fItr->second.leader = nullptr;
            if (fItr->second.booster == pClient)
                fItr->second.booster = nullptr;
        }
    }

    // update wing data
    if (pChar->wingID() > 0) {
        std::map<uint32, WingData>::iterator itr = m_wingDataMap.find(pChar->wingID());
        if (itr != m_wingDataMap.end()) {
            if (pChar->fleetBooster() == Fleet::Booster::Wing)
                itr->second.booster = nullptr;
            if ((pChar->fleetRole() == Fleet::Role::WingLeader) or (itr->second.leader == pClient))
                itr->second.leader = nullptr;
            if (itr->second.booster == pClient)
                itr->second.booster = nullptr;
        }
    }

    // update squad data
    if (pChar->squadID() > 0) {
        std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(pChar->squadID());
        if (itr != m_squadDataMap.end()) {
            itr->second.members.erase(pChar->itemID());
            if (pChar->fleetBooster() == Fleet::Booster::Squad)
                itr->second.booster = nullptr;
            if ((pChar->fleetRole() == Fleet::Role::SquadLeader) or (itr->second.leader == pClient))
                itr->second.leader = nullptr;
            if (itr->second.booster == pClient)
                itr->second.booster = nullptr;
        }
    }

    auto range = m_fleetMembers.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        if (itr->second == pClient) {
            m_fleetMembers.erase(itr);
            break;
        }
}

PyRep* FleetService::GetWings(uint32 fleetID)
{
    std::vector< uint32 > wingIDs, squadIDs;
    wingIDs.clear();
    GetWingIDs(fleetID, wingIDs);
    PyDict* dict = new PyDict();
    for (auto wingID : wingIDs) {
        squadIDs.clear();
        WingData wData = WingData();
        GetWingData(wingID, wData);
        WingRSP wing;
            wing.name = wData.name;
            wing.wingID = wingID;
        GetSquadIDs(wingID, squadIDs);
        PyDict* dict2 = new PyDict();
        for (auto squadID : squadIDs) {
            SquadData sData = SquadData();
            GetSquadData(squadID, sData);
            SquadRSP squad;
                squad.name = sData.name;
                squad.squadID = squadID;
            dict2->SetItem(new PyInt(squadID), squad.Encode());
        }
        wing.squads = dict2;
        dict->SetItem(new PyInt(wingID), wing.Encode());
    }

    dict->Dump(FLEET__DEBUG, "    ");
    return dict;
}

PyRep* FleetService::GetAvailableFleets(Client* pViewer) {
    Client* pClient(nullptr);

    PyDict* fleetDict = new PyDict();
    for (auto cur : m_fleetAdvertMap) {
        pClient = cur.second.leader;
        if (pClient == nullptr)
            continue;
        if (!CanSeeAdvert(cur.first, pViewer))
            continue;
        AvalibleFleetsRSP fleetRSP;
            fleetRSP.fleetID = cur.first;
            fleetRSP.local_minSecurity = cur.second.local_minSecurity;
            fleetRSP.description = cur.second.description;
            fleetRSP.public_minStanding = cur.second.public_minStanding;
            fleetRSP.fleetName = cur.second.fleetName;
            fleetRSP.advertTime = cur.second.advertTime;
            fleetRSP.dateCreated = cur.second.dateCreated;
            fleetRSP.joinNeedsApproval = cur.second.joinNeedsApproval;

        PyTuple* localTuple = new PyTuple(1);
        PyList* localList = new PyList();
        for (auto cur1 : cur.second.local_allowedEntities)
            localList->AddItemInt(cur1);
        localTuple->SetItem(0, localList);
        PyToken* token = new PyToken("__builtin__.set");
        PyTuple* localTuple2 = new PyTuple(2);
        localTuple2->SetItem(0, token);
        localTuple2->SetItem(1, localTuple);
            fleetRSP.local_allowedEntities = new PyObjectEx(false, localTuple2);

        PyTuple* publicTuple = new PyTuple(1);
        PyList* publicList = new PyList();
        for (auto cur2 : cur.second.public_allowedEntities)
            publicList->AddItemInt(cur2);
        publicTuple->SetItem(0, publicList);
        PyTuple* publicTuple2 = new PyTuple(2);
        PyIncRef(token);
        publicTuple2->SetItem(0, token);
        publicTuple2->SetItem(1, publicTuple);
            fleetRSP.public_allowedEntities = new PyObjectEx(false, publicTuple2);

            fleetRSP.local_minStanding = cur.second.local_minStanding;
            fleetRSP.numMembers = m_fleetMembers.count(cur.first);
            fleetRSP.hideInfo = cur.second.hideInfo;
            fleetRSP.public_minSecurity = cur.second.public_minSecurity;
            fleetRSP.inviteScope = cur.second.inviteScope;
            fleetRSP.solarSystemID = cur.second.solarSystemID;
            fleetRSP.charID = pClient->GetCharacterID();
            fleetRSP.corpID = pClient->GetCorporationID();
            fleetRSP.warFactionID = pClient->GetWarFactionID();
            fleetRSP.securityStatus = pClient->GetSecurityRating();
            fleetRSP.allianceID = pClient->GetAllianceID();
        fleetDict->SetItem(new PyLong(cur.first), fleetRSP.Encode() );
    }

    fleetDict->Dump(FLEET__DEBUG, "    ");
    return fleetDict;
}

void FleetService::FleetBroadcast(Client* pFrom, uint32 itemID, int8 scope, int8 group, std::string msg)
{
    uint32 fleetID = pFrom->GetChar()->fleetID();
    if (!IsFleetID(fleetID)) {
        _log(FLEET__WARNING, "%s called FleetBroadcast with invalid fleetID %u.", pFrom->GetName(), fleetID);
        return;
    }
    if (group == Fleet::BCast::Group::None) {
        _log(FLEET__WARNING, "%s called FleetBroadcast with group = None for fleet %u.", pFrom->GetName(), fleetID);
        return;
    }

    int32 wingID(pFrom->GetChar()->wingID());
    if ((wingID > 0) and !IsWingID(wingID)) {
        _log(FLEET__WARNING, "%s called FleetBroadcast with invalid wing %i for fleet %u.", pFrom->GetName(), wingID, fleetID);
        return;
    }
    int32 squadID(pFrom->GetChar()->squadID());
    if ((squadID > 0) and !IsSquadID(squadID)) {
        _log(FLEET__WARNING, "%s called FleetBroadcast with invalid squad %i for fleet %u.", pFrom->GetName(), squadID, fleetID);
        return;
    }

    uint32 scopeID(0);
    std::vector<Client*> members;
    switch (scope) {
        case Fleet::BCast::Scope::System: {
            scopeID = pFrom->GetSystemID();
        } break;
        case Fleet::BCast::Scope::Bubble: {
            scopeID = BubbleIDOf(pFrom);
            if (scopeID == 0)
                return;
        } break;
    }
    switch (group) {
        case Fleet::BCast::Group::All: {
            auto range = m_fleetMembers.equal_range(fleetID);
            for (auto fItr = range.first; fItr != range.second; ++fItr) {
                if (scope == Fleet::BCast::Scope::Universe) {
                    members.push_back(fItr->second);
                } else if (scope == Fleet::BCast::Scope::System) {
                    if (fItr->second->GetSystemID() == scopeID)
                        members.push_back(fItr->second);
                } else if (scope == Fleet::BCast::Scope::Bubble) {
                    if (BubbleIDOf(fItr->second) == scopeID)
                        members.push_back(fItr->second);
                }
            }
        } break;
        // these 2 need to check fleet hierarchy for proper member list
        case Fleet::BCast::Group::Down: {
            if (wingID == -1) {
                auto range = m_fleetMembers.equal_range(fleetID);
                for (auto fItr = range.first; fItr != range.second; ++fItr) {
                    if (scope == Fleet::BCast::Scope::Universe) {
                        members.push_back(fItr->second);
                    } else if (scope == Fleet::BCast::Scope::System) {
                        if (fItr->second->GetSystemID() == scopeID)
                            members.push_back(fItr->second);
                    } else if (scope == Fleet::BCast::Scope::Bubble) {
                        if (BubbleIDOf(fItr->second) == scopeID)
                            members.push_back(fItr->second);
                    }
                }
            } else {
                if (squadID == -1) {
                    std::vector<uint32> squads;
                    GetSquadIDs(wingID, squads);
                    std::map<uint32, SquadData>::iterator itr;
                    for (auto cur : squads) {
                        itr = m_squadDataMap.find(cur);
                        if (itr == m_squadDataMap.end())
                            continue;
                        for (auto member : itr->second.members)
                            if (scope == Fleet::BCast::Scope::Universe) {
                                members.push_back(member.second);
                            } else if (scope == Fleet::BCast::Scope::System) {
                                if (member.second->GetSystemID() == scopeID)
                                    members.push_back(member.second);
                            } else if (scope == Fleet::BCast::Scope::Bubble) {
                                if (BubbleIDOf(member.second) == scopeID)
                                    members.push_back(member.second);
                            }
                    }
                } else {
                    std::map<uint32, SquadData>::iterator itr = m_squadDataMap.find(squadID);
                    if (itr == m_squadDataMap.end())
                        break;
                    for (auto member : itr->second.members)
                        if (scope == Fleet::BCast::Scope::Universe) {
                            members.push_back(member.second);
                        } else if (scope == Fleet::BCast::Scope::System) {
                            if (member.second->GetSystemID() == scopeID)
                                members.push_back(member.second);
                        } else if (scope == Fleet::BCast::Scope::Bubble) {
                            if (BubbleIDOf(member.second) == scopeID)
                                members.push_back(member.second);
                        }
                }
            }
        } break;
        case Fleet::BCast::Group::Up: {
            if (wingID == -1) {
                // pFrom is FC.  nobody above to msg.  make error here
                _log(FLEET__WARNING, "FC %s called FleetBroadcast with group == Up for fleet %u.", pFrom->GetName(), fleetID);
                pFrom->SendErrorMsg("You cannot broadcast to Superiors as FC.");
            } else {
                if (squadID == -1) {
                    members.push_back(GetFleetLeader(fleetID));
                    members.push_back(GetWingLeader(wingID));
                } else {
                    members.push_back(GetFleetLeader(fleetID));
                    members.push_back(GetWingLeader(wingID));
                    members.push_back(GetSquadLeader(squadID));
                }
            }
        } break;
    }

    // Broadcast(name, group, charID, solarSystemID = None, itemID = None, broadcastName(BroadcastEvent/label) = None):
    //OnFleetBroadcast(name, group, charID, solarSystemID, itemID):
    //   ('HealCapacitor', 3, 95895066, 30003500, 1019274373727L, None)))

    PyTuple* payload = new PyTuple(5);
        payload->SetItem(0, new PyString(msg));
        payload->SetItem(1, new PyInt(group));
        payload->SetItem(2, new PyInt(pFrom->GetCharacterID()));
        payload->SetItem(3, new PyInt(pFrom->GetSystemID()));
        payload->SetItem(4, new PyInt(itemID));
        // if BCastName(label) then add next item properly
        payload->SetItem(5, PyStatic.NewNone());

    uint8 count(0);
    for (auto cur : members) {
        if (cur == nullptr)
            continue;
        PySafeIncRef(payload);
        cur->SendNotification("OnFleetBroadcast", "clientID", payload, true);
        ++count;
    }

    if (is_log_enabled(FLEET__BCAST_DUMP)) {
        std::ostringstream grp;
        switch (group) {
            case Fleet::BCast::Group::All: {
                grp << "All " << std::to_string(count);
            } break;
            case Fleet::BCast::Group::Down:
            case Fleet::BCast::Group::Up: {
                grp << std::to_string(count);
                grp << " " << GetBCastGroupName(group).c_str();
            } break;
        }

        _log(FLEET__BCAST_DUMP, "%s FleetBroadcast '%s' to %s members of fleet %u.", GetBCastScopeName(scope).c_str(), msg.c_str(), grp.str().c_str() , fleetID);
        payload->Dump(FLEET__BCAST_DUMP, "   ");
    }

    PySafeDecRef(payload);
}

void FleetService::SendFleetUpdate(uint32 fleetID, const char* notifyType, PyTuple* payload)
{
    if (is_log_enabled(FLEET__UPDATE_DUMP)) {
        _log(FLEET__UPDATE_DUMP, "SendFleetUpdate '%s' to members of fleet %u.", notifyType, fleetID);
        payload->Dump(FLEET__UPDATE_DUMP, "   ");
    }

    std::vector<Client*> members;
    auto range = m_fleetMembers.equal_range(fleetID);
    for (auto fItr = range.first; fItr != range.second; ++fItr)
        members.push_back(fItr->second);

    for (auto cur : members) {
        if ((cur == nullptr) or (cur == m_departing))
            continue;
        PySafeIncRef(payload);
        cur->SendNotification(notifyType, "*fleetid", payload, true);    // this sends "*fleetid" update to all fleet members and is sequenced
    }
    PySafeDecRef(payload);
}

void FleetService::GetFleetMembersOnGrid(Client* pClient, std::vector< uint32 >& data)
{
    std::vector<Client*> members;
    uint32 scopeID = BubbleIDOf(pClient);
    if (scopeID == 0)
        return;
    auto range = m_fleetMembers.equal_range(pClient->GetFleetID());
    for (auto fItr = range.first; fItr != range.second; ++fItr)
        members.push_back(fItr->second);

    for (auto cur : members) {
        if (cur == nullptr)
            continue;
        if (BubbleIDOf(cur) == scopeID)
            data.push_back(cur->GetCharacterID());
    }
}

void FleetService::GetFleetMembersInSystem(Client* pClient, std::vector< uint32 >& data)
{
    std::vector<Client*> members;
    uint32 scopeID = pClient->GetShipSE()->SystemMgr()->GetID();
    auto range = m_fleetMembers.equal_range(pClient->GetFleetID());
    for (auto fItr = range.first; fItr != range.second; ++fItr)
        members.push_back(fItr->second);

    for (auto cur : members) {
        if (cur == nullptr)
            continue;
        if (cur->GetShipSE()->SystemMgr()->GetID() == scopeID)
            data.push_back(cur->GetCharacterID());
    }
}

void FleetService::GetFleetClientsInSystem(Client* pClient, std::vector< Client* >& data)
{
    std::vector<Client*> members;
    uint32 scopeID = pClient->GetShipSE()->SystemMgr()->GetID();
    auto range = m_fleetMembers.equal_range(pClient->GetFleetID());
    for (auto fItr = range.first; fItr != range.second; ++fItr)
        members.push_back(fItr->second);

    for (auto cur : members) {
        if (cur == nullptr)
            continue;
        if (cur->GetShipSE()->SystemMgr()->GetID() == scopeID)
            data.push_back(cur);
    }
}

std::vector<Client *> FleetService::GetFleetClients(uint32 fleetID) {
    std::vector<Client*> members;
    auto range = m_fleetMembers.equal_range(fleetID);
    for (auto fItr = range.first; fItr != range.second; ++fItr)
        members.push_back(fItr->second);

    return members;
}

void FleetService::SendActiveStatus(uint32 fleetID, int32 wingID, int32 squadID)
{
    PyTuple* count = new PyTuple(1);
    count->SetItem(0, new PyInt((255 - m_fleetMembers.count(fleetID))));  // this is slots left from 255 (256 - leader)
    SendFleetUpdate(fleetID, "OnFleetActive", count);

    if (wingID > 0) {
        WingData wData = WingData();
        GetWingData(wingID, wData);
        PyTuple* count = new PyTuple(2);
            count->SetItem(0, new PyInt(wingID));
            count->SetItem(1, new PyInt(IsWingActive(wingID) ? 1 : 0));
        SendFleetUpdate(fleetID, "OnWingActive", count);
    }

    if (squadID > 0) {
        SquadData sData = SquadData();
        GetSquadData(squadID, sData);
        PyTuple* count = new PyTuple(2);
            count->SetItem(0, new PyInt(squadID));
            count->SetItem(1, new PyInt(sData.members.size()? 1 : 0));
        SendFleetUpdate(fleetID, "OnSquadActive", count);
    }
}

bool FleetService::GetInviteData(uint32 charID, InviteData& data)
{
    std::map<uint32, InviteData>::iterator itr = m_inviteData.find(charID);
    if (itr != m_inviteData.end()) {
        data = itr->second;
        return true;
    }
    return false;
}

bool FleetService::SaveInviteData(uint32 charID, InviteData& data)
{
    std::map<uint32, InviteData>::iterator itr = m_inviteData.find(charID);
    if (itr != m_inviteData.end())
        return false;

    m_inviteData.emplace(charID, data);    // map of invites by invitedCharID
    return true;
}

void FleetService::RemoveInviteData(uint32 charID)
{
    m_inviteData.erase(charID); // remove invite data
}

void FleetService::Process()
{
    if (m_inviteData.empty())
        return;
    int64 now(GetFileTimeNow());
    for (auto itr = m_inviteData.begin(); itr != m_inviteData.end(); ) {
        if ((itr->second.expires == 0) or (itr->second.expires > now)) {
            ++itr;
            continue;
        }
        if (itr->second.inviteBy != nullptr) {
            // fleetSvc.OnFleetJoinReject(memberID, reasonCode)  const.rejectFleetInviteTimeout = 1
            PyTuple* tuple = new PyTuple(2);
                tuple->SetItem(0, new PyInt(itr->first));
                tuple->SetItem(1, new PyInt(1));
            itr->second.inviteBy->SendNotification("OnFleetJoinReject", "clientID", &tuple, false);
        }
        itr = m_inviteData.erase(itr);
    }
}

uint32 FleetService::GetPendingInvite(uint32 fleetID, int32 wingID, int32 squadID, int8 role)
{
    if ((role != Fleet::Role::FleetLeader) and (role != Fleet::Role::WingLeader) and (role != Fleet::Role::SquadLeader))
        return 0;
    for (auto cur : m_inviteData) {
        if ((cur.second.fleetID != fleetID) or (cur.second.role != role))
            continue;
        if ((role == Fleet::Role::FleetLeader)
        or ((role == Fleet::Role::WingLeader) and (cur.second.wingID == wingID))
        or ((role == Fleet::Role::SquadLeader) and (cur.second.squadID == squadID)))
            return cur.first;
    }
    return 0;
}

bool FleetService::AddJoinRequest(uint32 fleetID, Client* pClient)
{
    // ensure only one request per client
    auto range = m_joinReq.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        if (itr->second == pClient)
            return false;
    m_joinReq.emplace(fleetID, pClient);
    return true;
}

void FleetService::GetJoinRequests(uint32 fleetID, std::vector< Client* >& data)
{
    auto range = m_joinReq.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        data.push_back(itr->second);
}

void FleetService::RemoveJoinRequest(uint32 fleetID, Client* pClient)
{
    auto range = m_joinReq.equal_range(fleetID);
    for (auto itr = range.first; itr != range.second; ++itr)
        if (itr->second == pClient) {
            m_joinReq.erase(itr);
            return;
        }
}

std::string FleetService::GetJobName(int8 job)
{
    switch (job) {
        case 0:     return "None";
        case 1:     return "Scout";
        case 2:     return "Creator";
        default:    return "Invalid Job";
    }
}

std::string FleetService::GetRoleName(int8 role)
{
    switch (role) {
        case 1:     return "FleetLeader";
        case 2:     return "WingLeader";
        case 3:     return "SquadLeader";
        case 4:     return "Member";
        default:    return "Invalid Role";
    }
}

std::string FleetService::GetBoosterName(int8 booster)
{
    switch (booster) {
        case 0:     return "No";
        case 1:     return "Fleet";
        case 2:     return "Wing";
        case 3:     return "Squad";
        default:    return "Invalid Booster";
    }
}

std::string FleetService::GetBCastScopeName(int8 scope)
{
    switch (scope) {
        case 0:     return "Universe";
        case 1:     return "System";
        case 2:     return "Bubble";
        default:    return "Invalid Scope";
    }
}

std::string FleetService::GetBCastGroupName(int8 group)
{
    switch (group) {
        case 0:     return "No";
        case 1:     return "Subordinate";
        case 2:     return "Superior";
        case 3:     return "All";
        default:    return "Invalid Group";
    }
}

std::string FleetService::GetBoosterData(uint32 fleetID, uint16& length)
{
    if (!IsFleetID(fleetID) or (fleetID > m_fleetID))
        return "Invalid Data";

    /** @todo  add system checks in here */
    Character* pChar(nullptr);
    std::ostringstream str;
    str.clear();

    bool fboost(false);
    FleetData fData = FleetData();
    GetFleetData(fleetID, fData);
    std::string bossName = "(none)";
    if ((fData.creator != nullptr) and (fData.creator->GetChar().get() != nullptr))
        bossName = fData.creator->GetChar()->itemName();
    str << "<color=teal>" << fData.name << "  Created By: " << bossName;
    str << "  Members: " << std::to_string(m_fleetMembers.count(fleetID)) << "</color><br>"; //54
    length += 54;
    length += fData.name.size();
    length += bossName.size();

    if ((fData.leader != nullptr) and (fData.leader->IsInSpace()) and (pChar = fData.leader->GetChar().get()) != nullptr) {
        if (m_fleetWings.count(fleetID) > pChar->GetSkillLevel(EvESkill::FleetCommand)) {
            str << "<color=red>";
            fboost = false;
        } else {
            str << "<color=green>";
            fboost = true;
        }
        length += 13;
        str << "Fleet Cmdr: " << pChar->itemName(); //12
        length += pChar->itemName().size();

        str << "    " << std::to_string(pChar->GetSkillLevel(EvESkill::FleetCommand)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::WingCommand)) << "/";
        str << std::to_string(pChar->GetSkillLevel(EvESkill::Leadership)) << "</color><br>";//15
        length += 30;
    } else {
        str << "<color=red>No Fleet Cmdr</color><br>";
        length += 37;
    }

    if ((fData.booster != nullptr) and (fData.booster->IsInSpace()) and (pChar = fData.booster->GetChar().get()) != nullptr) {
        if ((fData.leader != nullptr) and (fData.leader->GetSystemID() == fData.booster->GetSystemID())) {
            if (pChar->HasSkillTrainedToLevel(EvESkill::ArmoredWarfare, 1) or pChar->HasSkillTrainedToLevel(EvESkill::InformationWarfare, 1)
                or pChar->HasSkillTrainedToLevel(EvESkill::SiegeWarfare, 1) or pChar->HasSkillTrainedToLevel(EvESkill::SkirmishWarfare, 1)
                or pChar->HasSkillTrainedToLevel(EvESkill::MiningForeman, 1)) {
                if (fboost) {
                    str << "<color=green>";
                } else {
                    str << "<color=yellow>";
                }
                } else {
                    if (fboost) {
                        str << "<color=yellow>";
                    } else {
                        str << "<color=red>";
                    }
                    fboost = false;
                }
                length += 14;
        } else {
            str << "<color=red> (Not In System)";
            fboost = false;
            length += 30;
        }
        str << "Fleet Booster: " << pChar->itemName();//15
        length += pChar->itemName().size();
        str << "    " << std::to_string(pChar->GetSkillLevel(EvESkill::ArmoredWarfare)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::InformationWarfare)) << "/";
        str << std::to_string(pChar->GetSkillLevel(EvESkill::SiegeWarfare)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::SkirmishWarfare)) << "/";
        str << std::to_string(pChar->GetSkillLevel(EvESkill::MiningForeman)) << "</color><br>";//30
        length += 50;
    } else {
        str << "<color=red>No Fleet Booster</color><br>";
        length += 40;
        fboost = false;
    }

    std::vector< uint32 > wingIDs, squadIDs;
    wingIDs.clear();
    GetWingIDs(fleetID, wingIDs);
    for (auto wingID : wingIDs) {
        if (!IsWingID(wingID))
            continue;
        bool wboost(false);
        WingData wData = WingData();
        GetWingData(wingID, wData);
        if ((wData.leader != nullptr) and (wData.leader->IsInSpace())
        and (pChar = wData.leader->GetChar().get()) != nullptr) {
            if (m_wingSquads.count(wingID) > pChar->GetSkillLevel(EvESkill::WingCommand)) {
                str << "<color=red>";
            } else {
                if (fboost) {
                    str << "<color=green>";
                } else {
                    str << "<color=yellow>";
                }
                wboost = true;
            }
            length += 13;
            str << "  " << wData.name.c_str() << " Cmdr: " << pChar->itemName(); //10
            length += wData.name.size();
            length += pChar->itemName().size();
            str << "    " << std::to_string(pChar->GetSkillLevel(EvESkill::FleetCommand)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::WingCommand)) << "/";
            str << std::to_string(pChar->GetSkillLevel(EvESkill::Leadership)) << "</color><br>";//15
            length += 30;
        } else {
            str << "  <color=red>" << wData.name.c_str() << " No Cmdr</color><br>";//33
            length += wData.name.size();
            length += 35;
        }
        if ((wData.booster != nullptr) and (wData.booster->IsInSpace())
        and ((pChar = wData.booster->GetChar().get()) != nullptr)) {
            if ((wData.leader != nullptr) and (wData.leader->GetSystemID() == wData.booster->GetSystemID())) {
                if (pChar->HasSkillTrainedToLevel(EvESkill::ArmoredWarfare, 1) or pChar->HasSkillTrainedToLevel(EvESkill::InformationWarfare, 1)
                or pChar->HasSkillTrainedToLevel(EvESkill::SiegeWarfare, 1) or pChar->HasSkillTrainedToLevel(EvESkill::SkirmishWarfare, 1)
                or pChar->HasSkillTrainedToLevel(EvESkill::MiningForeman, 1)) {
                    if (wboost) {
                        str << "<color=green>";
                    } else {
                        str << "<color=yellow>";
                    }
                } else {
                    if (wboost) {
                        str << "<color=yellow>";
                    } else {
                        str << "<color=red>";
                    }
                    wboost = false;
                }
                length += 14;
            } else {
                str << "<color=red> (Not In System)";
                wboost = false;
                length += 30;
            }
            str << "  " << "Booster: " << pChar->itemName();//11
            length += pChar->itemName().size();
            str << "    " << std::to_string(pChar->GetSkillLevel(EvESkill::ArmoredWarfare)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::InformationWarfare));
            str << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::SiegeWarfare)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::SkirmishWarfare)) << "/";
            str << std::to_string(pChar->GetSkillLevel(EvESkill::MiningForeman)) << "</color><br>";//32
            length += 38;
        } else {
            str << "  <color=red>" << wData.name.c_str() << " No Booster</color><br>";//43
            length += wData.name.size();
            length += 45;
            wboost = false;
        }

        squadIDs.clear();
        GetSquadIDs(wingID, squadIDs);
        for (auto squadID : squadIDs) {
            if (!IsSquadID(squadID))
                continue;
            bool sboost(false);
            SquadData sData = SquadData();
            GetSquadData(squadID, sData);
            if ((sData.leader != nullptr) and (sData.leader->IsInSpace()) and (pChar = sData.leader->GetChar().get()) != nullptr) {
                if (sData.members.size() > (pChar->GetSkillLevel(EvESkill::Leadership) * 2)) {
                    str << "<color=red>";
                } else {
                    if (wboost) {
                        str << "<color=green>";
                    } else {
                        str << "<color=yellow>";
                    }
                    sboost = true;
                }
                length += 13;
                str << "    " << sData.name.c_str() <<  " Cmdr: " << pChar->itemName(); //11
                length += sData.name.size();
                length += pChar->itemName().size();
                str << "    " << std::to_string(pChar->GetSkillLevel(EvESkill::FleetCommand)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::WingCommand)) << "/";
                str << std::to_string(pChar->GetSkillLevel(EvESkill::Leadership)) << "</color><br>";//21
                length += 22;
            } else {
                str << "    <color=red>" << sData.name.c_str() << " No Cmdr</color><br>";
                length += sData.name.size();
                length += 37;
            }
            if ((sData.booster != nullptr) and (sData.booster->IsInSpace()) and (pChar = sData.booster->GetChar().get()) != nullptr) {
                if ((sData.leader != nullptr) and (sData.leader->GetSystemID() == sData.booster->GetSystemID())) {
                    if (pChar->HasSkillTrainedToLevel(EvESkill::ArmoredWarfare, 1) or pChar->HasSkillTrainedToLevel(EvESkill::InformationWarfare, 1)
                        or pChar->HasSkillTrainedToLevel(EvESkill::SiegeWarfare, 1) or pChar->HasSkillTrainedToLevel(EvESkill::SkirmishWarfare, 1)
                        or pChar->HasSkillTrainedToLevel(EvESkill::MiningForeman, 1)) {
                        if (sboost) {
                            str << "<color=green>";
                        } else {
                            str << "<color=yellow>";
                        }
                        } else {
                            if (sboost) {
                                str << "<color=yellow>";
                            } else {
                                str << "<color=red>";
                            }
                            sboost = false;
                        }
                        length += 14;
                } else {
                    str << "<color=red> (Not In System)";
                    sboost = false;
                    length += 30;
                }
                str << "    Booster: " << pChar->itemName();//13
                length += pChar->itemName().size();
                str << "    " << std::to_string(pChar->GetSkillLevel(EvESkill::ArmoredWarfare)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::InformationWarfare));
                str << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::SiegeWarfare)) << "/" << std::to_string(pChar->GetSkillLevel(EvESkill::SkirmishWarfare)) << "/";
                str << std::to_string(pChar->GetSkillLevel(EvESkill::MiningForeman)) << "</color><br>";//25
                length += 40;
            } else {
                str << "    <color=red>" << sData.name.c_str() << " No Booster</color><br>";//45
                length += sData.name.size();
                length += 43;
                sboost = false;
            }
            if (sboost) {
                str << "    <color=green>";
            } else {
                str << "    <color=red>";
            }
            str << "Members: " << std::to_string(sData.members.size()) << "  Effective: ";
            length += 40;
            std::string bdata;
            if (sboost) {   // this is the only way i could get these working right...dunno why
                bdata = std::to_string(sData.boost.leader);
                bdata += "/";
                bdata += std::to_string(sData.boost.armored);
                bdata += "/";
                bdata += std::to_string(sData.boost.info);
                bdata += "/";
                bdata += std::to_string(sData.boost.siege);
                bdata += "/";
                bdata += std::to_string(sData.boost.skirmish);
                bdata += "/";
                bdata += std::to_string(sData.boost.mining);
                length += 11;
            } else {
                bdata = "0/0/0/0/0/0  (";
                bdata += std::to_string(sData.boost.leader);
                bdata += "/";
                bdata += std::to_string(sData.boost.armored);
                bdata += "/";
                bdata += std::to_string(sData.boost.info);
                bdata += "/";
                bdata += std::to_string(sData.boost.siege);
                bdata += "/";
                bdata += std::to_string(sData.boost.skirmish);
                bdata += "/";
                bdata += std::to_string(sData.boost.mining);
                bdata += ")";
                length += 26;
            }
            str << bdata.c_str();
            str << "</color><br>";//12
            length += 12;
        }
        if (squadIDs.empty()) {
            str << "    <color=fuchsia>No Squads</color><br><br>";
            length += 45;
        }
    }
    if (wingIDs.empty()) {
        str << "  <color=fuchsia>No Wings</color><br><br>";
        length += 45;
    }

    return str.str();
}
