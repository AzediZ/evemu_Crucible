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

#include "eve-server.h"

#include "EVE_Mail.h"

#include "Client.h"
#include "ConsoleCommands.h"
#include "contract/ContractUtils.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "ServiceDB.h"
#include "agents/Agent.h"
#include "exploration/Exploration.h"
#include "fleet/FleetService.h"
#include "exploration/Probes.h"
#include "station/InsuranceService.h"
#include "map/MapDB.h"
#include "market/MarketMgr.h"
#include "market/sim/MarketSimMgr.h"
#include "npc/ambient/AmbientMgr.h"
#include "market/sim/SimFacilityMgr.h"
#include "missions/MissionDataMgr.h"
#include "station/Station.h"
#include "system/DestinyManager.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/CivilianMgr.h"
#include "system/cosmicMgrs/WormholeMgr.h"
#include "system/cosmicMgrs/ManagerDB.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "corporation/CorporationDB.h"
#include "corporation/BillingMgr.h"
#include "faction/WarManager.h"
#include "alliance/AllianceManager.h"
#include "corporation/CorpMgr.h"
#include "agents/AgentServices.h"

#include <random>

EntityList::EntityList()
: m_services(nullptr),
m_targTimer(0, true),
m_stampTimer(0, true),
m_minuteTimer(0, true),
m_startTime(0),
m_npcs(0),
m_stamp(1000),   /* arbitrary.  start at 1k.  in seconds.  used for destiny and client counters */
m_minutes(0),
m_connections(0),
m_clientSeedID(0)
{
    m_agents.clear();
    m_probes.clear();
    m_clients.clear();
    m_players.clear();
    m_systems.clear();
    m_stations.clear();
    m_targMgrs.clear();
    m_corpMembers.clear();

    m_shipTracking = sConfig.debug.UseShipTracking;
}

EntityList::~EntityList() {
    sLog.Green("   ServerShutdown", " Complete.");
}

void EntityList::Initialize() {
    m_startTime = GetFileTimeNow();

    /* start the timers */
    m_targTimer.Start(250);     // testing targeting and scan probes at 4/sec
    m_stampTimer.Start(1000);   // 1hz tic timer
    m_minuteTimer.Start(60000); // does this need to be accurate?

    m_clientSeedID = ServiceDB::SetClientSeed();
    sLog.Green( "       ServerInit", "ClientSeed Initialized." );

    if (is_log_enabled(SERVER__STACKTRACE))
        sConfig.debug.StackTrace = true;

    sLog.Blue("       EntityList", "Entity Manager Initialized.");
}

void EntityList::Shutdown() {
    /* halt server (console 'x', /shutdown, a scheduled shutdown or a signal).
     * sConsole.IsShutdown() is true here, so clients take the shutdown logout path (no ship left in space).
     * server run loop will exit after control is returned from this function, which will clean up remaining items.
     */
    if (m_clients.empty())
        return;

    sLog.Warning("   ServerShutdown", "Logging out %lu clients.", m_clients.size());
    for (auto cur : m_clients) {
        if (cur->GetUserID() > 0)
            cur->SendNotifyMsg("The server is shutting down.");
        SafeDelete(cur);
    }

    m_clients.clear();
    m_players.clear();
    m_corpMembers.clear();
}

void EntityList::Close()
{
    if (m_clients.size() > 0) {
        sLog.Yellow("       EntityList", "Cleaning up %lu clients, %lu systems, %lu agents, and %lu stations", \
                    m_clients.size(), m_systems.size(), m_agents.size(), m_stations.size());
    } else {
        sLog.Green("       EntityList", "Cleaning up %lu clients, %lu systems, %lu agents, and %lu stations", \
                    m_clients.size(), m_systems.size(), m_agents.size(), m_stations.size());
    }

    for (auto cur : m_clients)
        SafeDelete(cur);

    for (auto cur : m_agents)
        SafeDelete(cur.second);

    for (auto cur : m_systems) {
        cur.second->UnloadSystem();
        SafeDelete(cur.second);
    }

    sLog.Warning("       EntityList", "Entity List has been closed." );
}

/* m_clients is used to search for online players and numerous other things.
 *  the problem here is any searching is done thru iteration, which can get expensive.
 *  however, clients are added before their char is selected, so there is no charID for map placement.
 *    maybe use m_clients for basic Process() calls and use m_players for character/client searching
 *
 * update:  done and working very well.
 */

void EntityList::Add( Client* pClient ) {
    ++m_connections;
    if (pClient != nullptr)
        m_clients.push_back(pClient);
}

void EntityList::Remove(Client* pClient) {
    /* note:  will get expensive for many clients  */
    std::vector<Client*>::iterator itr = m_clients.begin();
    for (; itr != m_clients.end(); ++itr)
        if ((*itr) == pClient) {
            m_clients.erase(itr);
            return;
        }
}

void EntityList::AddPlayer(Client* pClient)
{
    if (pClient != nullptr)
        if (pClient->IsValidSession()) {
            m_players.emplace(pClient->GetCharacterID(), pClient);
            if (IsPlayerCorp(pClient->GetCorporationID()))
                m_corpMembers[pClient->GetCorporationID()][pClient] = pClient->GetCorpRole();
        } else {
            m_players.emplace(pClient->GetCharID(), pClient);
            // make note about invalid session and failure to add player to corp roles.
            //   this is nbd if player is in npc corp or in player corp with no roles
        }
}

void EntityList::UpdateCorpMember(Client* pClient, uint32 oldCorpID)
{
    if ((pClient == nullptr) or !pClient->IsValidSession())
        return;

    // corp change:  out of the old corp's members
    if (oldCorpID != (uint32)pClient->GetCorporationID()) {
        std::map<uint32, corpRole>::iterator itr = m_corpMembers.find(oldCorpID);
        if (itr != m_corpMembers.end())
            itr->second.erase(pClient);
    }

    if (IsPlayerCorp(pClient->GetCorporationID()))
        m_corpMembers[pClient->GetCorporationID()][pClient] = pClient->GetCorpRole();
}

void EntityList::RemovePlayer(Client* pClient)
{
    if (pClient != nullptr)
        if (pClient->IsValidSession()) {
            m_players.erase(pClient->GetCharacterID());
            // remove player from corp map, if applicable
            std::map<uint32, corpRole>::iterator itr = m_corpMembers.find(pClient->GetCorporationID());
            if (itr != m_corpMembers.end())
                itr->second.erase(pClient);
        } else {
            m_players.erase(pClient->GetCharID());
        }
}


namespace {
    /* an exception that escaped one system's, client's or manager's tick used to stop the whole server.
     *  each tick is guarded on its own, so the error is logged and everything else carries on.
     *  the log is limited, so an error that repeats every second can't flood it */
    uint32 s_tickErrors = 0;

    void LogTickError(const char* what, uint32 id, const char* error)
    {
        ++s_tickErrors;
        if ((s_tickErrors <= 20) or (s_tickErrors % 100 == 0))
            sLog.Error("    EntityList::Tick", "Caught %s in %s %u (error %u since start).  Carrying on.", error, what, id, s_tickErrors);
    }

    // runs fn;  false if it threw (and the error was logged)
    template <typename Fn>
    bool GuardTick(const char* what, uint32 id, Fn&& fn)
    {
        try {
            fn();
            return true;
        } catch (PyException&) {
            LogTickError(what, id, "a Python exception");
        } catch (std::exception& e) {
            LogTickError(what, id, e.what());
        } catch (...) {
            LogTickError(what, id, "an unknown exception");
        }
        return false;
    }
}

void EntityList::Process() {
    Client* pClient(nullptr);
    std::vector<Client*>::iterator citr = m_clients.begin();
    while (citr != m_clients.end()) {
        bool keep(true);
        GuardTick("the network tick of client", (*citr)->GetCharID(), [&]{ keep = (*citr)->ProcessNet(); });
        if (keep) {
            ++citr;
        } else {
            pClient = *citr;
            citr = m_clients.erase(citr);
            SafeDelete(pClient);
        }
    }

    if (m_targTimer.Check()) {
        std::unordered_map<SystemEntity*, TargetManager*>::iterator titr = m_targMgrs.begin();
        while (titr != m_targMgrs.end()) {
            bool keep(true);
            GuardTick("the target manager of entity", titr->first->GetID(), [&]{ keep = titr->second->Process(); });
            if (keep) {
                ++titr;
            } else {
                titr = m_targMgrs.erase(titr);
            }
        }
        std::map<uint32, ProbeSE*>::iterator pitr = m_probes.begin();
        while (pitr != m_probes.end()) {
            bool keep(true);
            GuardTick("the tick of probe", pitr->first, [&]{ keep = pitr->second->ProcessTic(); });
            if (keep) {
                ++pitr;
            } else {
                pitr = m_probes.erase(pitr);
            }
        }
    }

    /* check for 1Hz timer tic */
    if (m_stampTimer.Check()) {
        double profileStartTime(GetTimeUSeconds());

        ++m_stamp;

        // move every entity to the new stamp first.  destiny updates from here on are stamped with it, and the
        //  client applies them to its own simulation at this stamp, so our state must already be there
        for (auto cur : m_systems)
            if (cur.second != nullptr)
                GuardTick("the destiny tick of system", cur.first, [&]{ cur.second->ProcessDestiny(); });

        for (auto cur : m_players)
            if (cur.second->IsValidSession())   // verify client is constructed before calling ProcessClient() on it
                GuardTick("the tick of character", cur.first, [&]{ cur.second->ProcessClient(); });

    /** @todo test for adding OpenMP here to enable MP per system. */
    // this wont work....possibility of removing systems, therefore invalidating the iterator.
    // bad things can happen if this is running parallel on MP
    //#pragma omp parallel  // starts a new team
        std::map<uint32, SystemManager*>::iterator itr = m_systems.begin();
        while (itr != m_systems.end()) {
            if (itr->second == nullptr) { /* this shouldnt happen.  log error to make note */
                sLog.Error(" EntityList::Proc", "Deleting System %u", itr->first);
                itr = m_systems.erase(itr);
                continue;
            }
            bool keep(true);    /* Process each loaded system.  one that throws stays loaded */
            GuardTick("the tick of system", itr->first, [&]{ keep = itr->second->ProcessTic(); });
            if (!keep) {
                itr->second->UnloadSystem();
                SafeDelete(itr->second);
                itr = m_systems.erase(itr);
                continue;
            }
            ++itr;
        }

        // these need 1Hz tics.  each is guarded on its own, so one manager's error doesn't stop the others
        GuardTick("the civilian manager", 0, []{ sCivMgr.Process(); });
        GuardTick("the ambient npc manager", 0, []{ sAmbientMgr.Process(); });  // ambient npcs:  transits between systems, the market score refresh
        GuardTick("the locator manager", 0, []{ sLocatorMgr.Process(); });
        GuardTick("the bubble manager", 0, []{ sBubbleMgr.Process(); });
        GuardTick("the exploration manager", 0, []{ sExplorationMgr.Process(); });
        GuardTick("the fleet service", 0, []{ sFltSvc.Process(); });  // fleet invite timeouts
        GuardTick("the market simulation", 0, []{ sMktSim.Process(); });  // market simulation (keeps to its own time budget)
        GuardTick("the market sim's station lines", 0, []{ sSimFacilities.Process(); });   // GM test jobs, and its writes when the sim doesn't make them

        // these minute tics do not need to be precise
        if (m_minuteTimer.Check()) {
            ++m_minutes;
            GuardTick("the heartbeat", 0, []{ ServiceDB::SaveHeartbeat(); });  // 1m  the last time the server ran (bills skip the time it was off)
            GuardTick("the mission manager", 0, []{ sMissionDataMgr.Process(); });  // 1m
            GuardTick("the war manager", 0, []{ sWarMgr.Process(); });  // 1m  war notifications and bills
            GuardTick("the alliance manager", 0, []{ sAllianceMgr.Process(); });  // 1m  alliance joins, daily executor count

            if (m_minutes % 5 == 0) { // ~5m
                GuardTick("the wormhole manager", 0, []{ sWHMgr.Process(); });
                GuardTick("the market manager", 0, []{ sMktMgr.Process(); });  // market order expiry, daily price history rollup
                GuardTick("contract expiry", 0, []{ ContractUtils::ProcessExpiry(); });  // optional contract cleanup and courier failure
                GuardTick("the corporation manager", 0, []{ sCorpMgr.Process(); });  // vote cases, sanctioned actions, office rent
                GuardTick("the billing manager", 0, []{ sBillingMgr.Process(); });  // auto-pay, overdue bills, alliance maintenance
                for (auto cur : m_systems)
                    GuardTick("the 5 minute update of system", cur.first, [&]{ cur.second->UpdateData(); });   // update active system timers and dynamic data every 5m
            }
            if (m_minutes % 15 == 0) { // ~15m
                GuardTick("the console status", 0, []{ sConsole.UpdateStatus(); });
                GuardTick("the development index save", 0, []{ svDataMgr.SaveDevIndices(); });
            }
            if (m_minutes % 60 == 0) { // ~1h
                GuardTick("the star map statistics", 0, []{ MapDB::ManipulateTimeData(); });
                GuardTick("the development index decay", 0, []{ svDataMgr.Process(); });
                GuardTick("insurance expiry", 0, []{ InsuranceService::ProcessExpiry(); });  // insurance expiry notifications
            }
        }

        if (sConfig.debug.UseProfiling)
            sProfiler.AddTime(Profile::entityS, GetTimeUSeconds() - profileStartTime);
    }
}

SystemManager* EntityList::FindOrBootSystem(uint32 systemID) {
    if (!sDataMgr.IsSolarSystem(systemID)) {
        _log(SERVER__INIT_ERR, "BootSystem() called with invalid systemID (%u)", systemID);
        return nullptr;
    }

    std::map<uint32, SystemManager*>::iterator itr = m_systems.find(systemID);
    if (itr != m_systems.end())
        return itr->second;

    SystemManager* pSM = new SystemManager(systemID, *m_services);
    if ((pSM == nullptr) or (!pSM->BootSystem())) {
        _log(SERVER__INIT_ERR, "BootSystem() - Booting system %u failed", systemID);
        SafeDelete(pSM);
        return nullptr;
    }

    _log(SERVER__INIT, "BootSystem() - Booted system %u", systemID);
    m_systems[systemID] = pSM;
    return pSM;
}

// cannot put add/remove station in header due to incomplete StationItemRef class
void EntityList::AddStation(uint32 stationID, StationItemRef itemRef) {
    m_stations[stationID] = itemRef;
}

void EntityList::RemoveStation(uint32 stationID) {
    m_stations.erase(stationID);
}

Agent* EntityList::GetAgent(uint32 agentID) {
    std::map<uint32, Agent*>::iterator res = m_agents.find(agentID);
    if (res != m_agents.end())
        return res->second;

    Agent* pAgent = new Agent(agentID);
    if (!pAgent->Load()) {
        delete pAgent;
        return nullptr;
    }
    m_agents[agentID] = pAgent;
    return pAgent;
}

void EntityList::GetClients(std::vector<Client*> &result) const {
    for (auto cur : m_players)
        result.push_back(cur.second);
}

void EntityList::GetSystems(std::vector<SystemManager*> &result) const {
    for (auto cur : m_systems)
        if ((cur.second != nullptr) and cur.second->IsLoaded())
            result.push_back(cur.second);
}

void EntityList::GetCorpClients(std::vector<Client*> &result, uint32 corpID) const {
    std::map<uint32, corpRole>::const_iterator cItr = m_corpMembers.find(corpID);
    if (cItr == m_corpMembers.end())
        return;

    corpRole::const_iterator itr = cItr->second.begin(), end = cItr->second.end();
    while (itr != end) {
        if (itr->first != nullptr)
            result.push_back(itr->first);
        ++itr;
    }
}

// this method is corrected, as stations have their own guestlist now.
void EntityList::GetStationGuestList(uint32 stationID, std::vector<Client*> &result) const {
    std::map<uint32, StationItemRef>::const_iterator itr = m_stations.find(stationID);
    if (itr != m_stations.end())
        itr->second->GetGuestList(result);
}

uint32 EntityList::GetLoggedInCount() const
{
    uint32 count(0);
    for (auto cur : m_clients)
        if (cur->GetUserID() > 0)
            ++count;
    return count;
}

void EntityList::GetProbesForChar(uint32 charID, uint32 systemID, std::vector<ProbeSE*>& into)
{
    for (auto cur : m_probes)
        if ((cur.second->GetOwnerCharID() == charID) and (cur.second->SystemMgr() != nullptr) and (cur.second->SystemMgr()->GetID() == systemID))
            into.push_back(cur.second);
}

bool EntityList::IsOnline(uint32 charID)
{
    if (m_players.find(charID) == m_players.end())
        return false;
    return true;
}

PyRep* EntityList::PyIsOnline(uint32 charID)
{
    if (m_players.find(charID) == m_players.end())
        return PyStatic.NewFalse();
    return PyStatic.NewTrue();
}

Client* EntityList::FindClientByCharID(uint32 charID) const
{
    std::map<uint32, Client*>::const_iterator itr = m_players.find(charID);
    if (itr != m_players.end())
        return itr->second;
    return nullptr;
}

StationItemRef EntityList::GetStationByID(uint32 stationID) {
    std::map<uint32, StationItemRef>::iterator res = m_stations.find(stationID);
    if (res != m_stations.end())
        return res->second;
    return StationItemRef(nullptr);
}

std::string EntityList::GetAnomalyID()
{
    // these should be totally unique.  design a way to enforce this
    std::string str1 = "", str2 = "";
    for (uint8 i = 0; i < 3; ++i) {
        str1 += alphaList[MakeRandomInt(0,25)];    //rand() % sizeof(alphaList) - 1
        str2 += std::to_string(MakeRandomInt(0,9));
    }

    std::string res = str1;
    res += "-";
    res += str2;
    // not sure if we need to keep track of these IDs...
    //m_anomIDs.push_back(res);
    return res;
}

void EntityList::GetUpTime( std::string& time )
{
    float seconds = m_stamp - 1000;
    float minutes = seconds/60;
    float hours = minutes/60;
    float days = hours/24;
    float weeks = days/7;
    float months = days/30;

    int s(fmod(seconds, 60));
    int m(fmod(minutes, 60));
    int h(fmod(hours, 24));
    int d(fmod(days, 7));
    int w(fmod(weeks, 4));
    int M(fmod(months, 12));

    std::ostringstream uptime;
    if (M) {
        uptime << M << "M" << w << "w" << d << "d" << h << "h" << m << "m" << s << "s";
    } else if (w) {
        uptime << w << "w" << d << "d" << h << "h" << m << "m" << s << "s";
    } else if (d) {
        uptime << d << "d" << h << "h" << m << "m" << s << "s";
    } else if (h) {
        uptime << h << "h" << m << "m" << s << "s";
    } else if (m) {
        uptime << m << "m" << s << "s";
    } else {
        uptime << s << "s";
    }

    //std::shared_ptr<const char*> ret = uptime.str().c_str();
    time = uptime.str();
}


// this is my answer to the crazy looping of Multicast shit...
void EntityList::CorpNotify(uint32 corpID, uint8 bCastType, const char* notifyType, const char* idType, PyTuple* payload) const
{
    // make sure this is player corp (which it really should be, but just in case....)
    if (IsNPCCorp(corpID))
        return;
    std::map<uint32, Client*> cMap;
    std::map<uint32, corpRole>::const_iterator cItr = m_corpMembers.find(corpID);
    if (cItr == m_corpMembers.end()) {
        PySafeDecRef(payload);
        return; // no corp members online now.  nothing to do here.
    }

    // determine who in corp needs to be notified
    using namespace Notify::Types;
    //using namespace Corp::Role;
    // auto doesnt work here...dunno why yet.
    corpRole::const_iterator itr = cItr->second.begin(), end = cItr->second.end();
    switch (bCastType) {
        case CorpNews:
        case CorpNewCEO:
        case CharLeftCorp:
        case CorpTaxChange:
        case CorpDividend:
        case CorpLiquidation:
        case CloneRevoked1: {
            // all members?
            while (itr != end) {
                cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;
        case CorpAppNew:
        case CorpAppReject:
        case CorpAppAccept: {
            // who else wants/needs this?
            // PersonnelManager is only role that can view corp applications
            while (itr != end) {
                if (CorpMgr::HasRole(itr->second, Corp::Role::PersonnelManager))
                    cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;
        case AllMaintenanceBill:
        case CorpAllBill:
        case BillOutOfMoney:
        case BillPaidCorpAll:
        case CorpOfficeExpiration: {
            // bills go to the accountants (and directors)
            while (itr != end) {
                if (CorpMgr::HasRole(itr->second, Corp::Role::Accountant) or CorpMgr::HasRole(itr->second, Corp::Role::JuniorAccountant))
                    cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;
        case CorpVote: {
            // any member that can vote (has shares)
            //  damn...dunno if i wanna do this one like this....hit db every loop here??  fukin nuts!
            // this is another vote for putting "corp shares" in character.corpData
            //    well, then we'd have to hit db for offine chars.....omg
            CorporationDB mdb;
            while (itr != end) {
                // if (itr->first->GetChar()->HasShares())  // not written, no underlying code yet
                if (mdb.HasShares(itr->first->GetCharacterID(), corpID))
                    cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;

        // character notifications (CorpMgr::NotifyChar), wars (WarManager via NotificationMgr), sovereignty, starbases and FW:  not routed to corps here
        case CharMedal:
        case AllWarDeclared:
        case AllWarSurrender:
        case AllWarRetracted:
        case AllWarInvalidated:
        case CharBill:
        case BillPaidChar:
        case CorpVoteCEORevoked:
        case CorpWarDeclared:
        case CorpWarFightingLegal:
        case CorpWarSurrender:
        case CorpWarRetracted:
        case CorpWarInvalidated:
        case ContainerPassword:
        case SovAllClaimFail:
        case SovCorpClaimFail:
        case SovAllBillLate:
        case SovCorpBillLate:
        case SovAllClaimLost:
        case SovCorpClaimLost:
        case SovAllClaimAquired:
        case SovCorpClaimAquired:
        case AllAnchoring:
        case AllStructVulnerable:
        case AllStrucInvulnerable:
        case SovDisruptor:
        case CorpStructLost:
        case FWCorpJoin:
        case FWCorpLeave:
        case FWCorpKick:
        case FWCharKick:
        case FWCorpWarning:
        case FWCharWarning:
        case FWCharRankLoss:
        case FWCharRankGain:
        case FWAllianceWarning:
        case FWAllianceKick:
        case TransactionReversal:
        case Reimbursement:
        case TowerAlert:
        case TowerResourceAlert:
        case StationAggression1:
        case StationStateChange:
        case StationConquer:
        case StationAggression2:
        case FacWarCorpJoinRequest:
        case FacWarCorpLeaveRequest:
        case FacWarCorpJoinWithdraw:
        case FacWarCorpLeaveWithdraw:
        case SovereigntyTCUDamage:
        case SovereigntySBUDamage:
        case SovereigntyIHDamage:
        case ContactAdd:
        case ContactEdit:
        case CorpKicked:
        case OrbitalAttacked:
        case OrbitalReinforced:
        case OwnershipTransferred:
            break;

        // internal corp notifications
        case FactoryJob: {      // factory job completion added to calendar
            // who else wants/needs this?
            //  lets start with factory manager, and may have to add later
            while (itr != end) {
                if (CorpMgr::HasRole(itr->second, Corp::Role::FactoryManager))
                    cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;
        case MarketOrder: {
            // corp orders are handled by traders and accountants (quote.py useCorp checkbox)
            while (itr != end) {
                if (CorpMgr::HasRole(itr->second, Corp::Role::Trader) or CorpMgr::HasRole(itr->second, Corp::Role::Accountant))
                    cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;
        case WalletChange: {
            while (itr != end) {
                // this may need to check if player has access to division changed - will require a LOT more code
                if (CorpMgr::HasRole(itr->second, Corp::Role::Accountant) or CorpMgr::HasRole(itr->second, Corp::Role::Auditor)
                or CorpMgr::HasRole(itr->second, Corp::Role::JuniorAccountant))
                    cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;
        case ItemUpdateStation: {
            // all members?
            while (itr != end) {
                cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;
        case ItemUpdateSystem: {
            // all members?
            while (itr != end) {
                cMap.emplace(itr->first->GetCharacterID(), itr->first);
                ++itr;
            }
        } break;
    }

    for (auto cur : cMap) {
        PyIncRef(payload);
        cur.second->SendNotification( notifyType, idType, payload, false );   // are any of these sequenced?
    }

    PyDecRef(payload);
}

void EntityList::Broadcast(const char* notifyType, const char* idType, PyTuple** payload) const {
    //build a little notification out of it.
    EVENotificationStream notify;
        notify.remoteObject = 1;
        notify.args = *payload;
    payload = nullptr;    //consumed

    //now sent it to the client
    PyAddress dest;
        dest.type = PyAddress::Broadcast;
        dest.service = notifyType;
        dest.bcast_idtype = idType;
    Broadcast(dest, notify);
}

void EntityList::Broadcast(const PyAddress &dest, EVENotificationStream &noti) const {
    for (auto cur : m_players)
        cur.second->SendNotification(dest, noti);
}

void EntityList::Multicast(const character_set &cset, const PyAddress &dest, EVENotificationStream &noti) const {
    std::map<uint32, Client*>::const_iterator itr = m_players.begin();
    for (auto cur : cset) {
        itr = m_players.find(cur);
        if (itr != m_players.end())
            itr->second->SendNotification(dest, noti);
    }
}

// updated to remove looping thru entire client list for each call....still needs work
void EntityList::Multicast( const char* notifyType, const char* idType, PyTuple** in_payload, NotificationDestination target, uint32 targID, bool seq )
{
    PyTuple* payload = *in_payload;
    in_payload = nullptr;

    std::vector<Client*> cVec;
    cVec.clear();
    switch( target ) {
        case NOTIF_DEST__LOCATION: {
            if (sDataMgr.IsStation(targID)) {
                GetStationGuestList(targID, cVec);
            } else if (sDataMgr.IsSolarSystem(targID)) {
                SystemManager* pSysMgr = FindOrBootSystem(targID);
                if (pSysMgr == nullptr)
                    break;
                pSysMgr->GetClientList(cVec);
            } else {
                sLog.Error("EntityList::Multicast 1", "DEST__LOCATION - location %u is neither station nor system", targID);
                EvE::traceStack();
            }
        } break;
        case NOTIF_DEST__CORPORATION: {
            std::map<uint32, corpRole>::const_iterator cItr = m_corpMembers.find(targID);
            if (cItr == m_corpMembers.end())
                break;
            corpRole::const_iterator itr = cItr->second.begin();
            while (itr != cItr->second.end()) {
                cVec.push_back(itr->first);
                ++itr;
            }
        } break;
    };

    for (auto cur : cVec) {
        PyIncRef(payload);
        cur->SendNotification( notifyType, idType, &payload, seq );
    }

    PyDecRef( payload );
}

// updated.  so much better this way.
void EntityList::Multicast(const char* notifyType, const char* idType, PyTuple** in_payload, const MulticastTarget &mcset, bool seq)
{
    // consume payload
    PyTuple* payload = *in_payload;
    in_payload = nullptr;

    if (!mcset.characters.empty())
        for (auto cur : mcset.characters) {
            std::map<uint32, Client*>::iterator itr = m_players.find(cur);
            if ( itr != m_players.end()) {
                PyIncRef(payload);
                itr->second->SendNotification( notifyType, idType, &payload, seq );
            }
        }

    if (!mcset.locations.empty()) {
        SystemManager* pSysMgr(nullptr);
        std::vector<Client*> cVec;
        cVec.clear();
        for (auto cur : mcset.locations) {
            if (sDataMgr.IsStation(cur)) {
                GetStationGuestList(cur, cVec);
            } else if (sDataMgr.IsSolarSystem(cur)) {
                pSysMgr = FindOrBootSystem(cur);
                if (pSysMgr == nullptr)
                    continue;
                pSysMgr->GetClientList(cVec);
            } else {
                sLog.Error("EntityList::Multicast 2", "location %u is neither station nor system", cur);
                EvE::traceStack();
            }
        }
        for (auto cur : cVec) {
            PyIncRef(payload);
            cur->SendNotification( notifyType, idType, &payload, seq );
        }
    }

    // this will need list of interested parties from corp.  update this call to use CorpNotify() where possible.
    if (!mcset.corporations.empty()) {
        sLog.Error("EntityList::Multicast 2", "Corporation MulticastTarget called.");
        EvE::traceStack();
        for (auto cur : mcset.corporations) {
            std::map<uint32, corpRole>::const_iterator cItr = m_corpMembers.find(cur);
            if (cItr == m_corpMembers.end())
                continue;
            corpRole::const_iterator itr = cItr->second.begin();
            while (itr != cItr->second.end()) {
                PyIncRef(payload);
                itr->first->SendNotification( notifyType, idType, &payload, seq );
                ++itr;
            }
        }
    }

    PyDecRef( payload );
}

void EntityList::Multicast(const character_set &cset, const char* notifyType, const char* idType, PyTuple** in_payload, bool seq) const
{
    // consume payload
    PyTuple* payload = *in_payload;
    in_payload = nullptr;

    std::map<uint32, Client*>::const_iterator itr = m_players.begin();
    for (auto cur : cset) {
        itr = m_players.find(cur);
        if (itr != m_players.end()) {
            PyIncRef(payload);
            itr->second->SendNotification(notifyType, idType, &payload, seq);
        }
    }
    PyDecRef( payload );
}

void EntityList::Unicast(uint32 charID, const char* notifyType, const char* idType, PyTuple** payload, bool seq) {
    Client* pClient = FindClientByCharID(charID);
    if (pClient != nullptr)
        pClient->SendNotification( notifyType, idType, payload, seq );
}

/** @todo @note NOTE: TODO: HACK: the Find* methods below can get very expensive for many players */

//used by gmCommands....i dont like this one either....but at least it's use will be seldom
Client* EntityList::FindClientByName(const char* name) const {
    for (auto cur : m_players) {
        CharacterRef cRef = cur.second->GetChar();
        if (cRef.get() != nullptr)
            if (strcmp(cRef->name(), name) == 0)
                return cur.second;
    }
    return nullptr;
}

/** @todo  this needs more work.  hacked for now...  */
void EntityList::RegisterSID(int64 &sessionID) {
    /* the client uses random.getrandbits(63); draw random 63-bit non-zero ids until one is unused */
    static std::mt19937_64 sidGen(std::random_device{}());
    int64 sid(0);
    do {
        sid = static_cast<int64>(sidGen() & 0x7FFFFFFFFFFFFFFFULL);
    } while ((sid == 0) or !m_sessions.insert(sid).second);
    sessionID = sid;
}

void EntityList::RemoveSID ( int64 sessionID ) {
    m_sessions.erase(sessionID);
}
