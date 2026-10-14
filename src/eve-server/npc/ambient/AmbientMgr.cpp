 /**
  * @name AmbientMgr.cpp
  *   the ambient npcs' server-wide part (doc/code_and_design_notes/planning/ambient-npc-plan.md 3, 4.1, 6.2, 6.3).  see AmbientMgr.h
  */

#include "eve-server.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "EVEServerConfig.h"
#include "EntityList.h"
#include "StaticDataMgr.h"
#include "map/MapData.h"
#include "station/StationDataMgr.h"
#include "npc/ai/AmbientRules.h"
#include "npc/ambient/AmbientMgr.h"
#include "npc/ambient/AmbientSystem.h"
#include "system/CrimeWatch.h"
#include "system/SystemManager.h"

/* ------------------------------------------------------------------------------------------------------------------
 *  Ambient:: names and races
 * ------------------------------------------------------------------------------------------------------------------ */
const char* Ambient::RoleName(uint8 role)
{
    switch (role) {
        case Role::Hauler:      return "hauler";
        case Role::Courier:     return "courier";
        case Role::Freighter:   return "freighter";
        case Role::Navy:        return "navy";
        case Role::Concord:     return "concord";
        case Role::Miner:       return "miner";
        case Role::Criminal:    return "criminal";
        default:                return "none";
    }
}

const char* Ambient::RaceName(uint8 race)
{
    switch (race) {
        case Race::Amarr:       return "Amarr";
        case Race::Caldari:     return "Caldari";
        case Race::Gallente:    return "Gallente";
        case Race::Minmatar:    return "Minmatar";
        default:                return "";
    }
}

uint32 Ambient::RaceFaction(uint8 race)
{
    switch (race) {
        case Race::Amarr:       return factionAmarr;
        case Race::Caldari:     return factionCaldari;
        case Race::Gallente:    return factionGallente;
        case Race::Minmatar:    return factionMinmatar;
        default:                return 0;
    }
}

uint32 Ambient::RaceNavyCorp(uint8 race)
{
    // Amarr Navy, Caldari Navy, Federation Navy, Republic Fleet (7.1)
    switch (race) {
        case Race::Amarr:       return 1000084;
        case Race::Caldari:     return 1000035;
        case Race::Gallente:    return 1000120;
        case Race::Minmatar:    return 1000051;
        default:                return 0;
    }
}

uint8 Ambient::FactionRace(uint32 factionID)
{
    switch (factionID) {
        case factionAmarr:
        case factionAmmatar:
        case factionKhanid:         return Race::Amarr;
        case factionCaldari:        return Race::Caldari;
        case factionGallente:
        case factionSistersOfEVE:   return Race::Gallente;
        case factionMinmatar:
        case factionThukker:        return Race::Minmatar;
        default:                    return Race::None;
    }
}

const char* Ambient::LegName(uint8 kind)
{
    switch (kind) {
        case Leg::Hold:         return "Hold";
        case Leg::WarpTo:       return "WarpTo";
        case Leg::ApproachGate: return "ApproachGate";
        case Leg::JumpOut:      return "JumpOut";
        case Leg::ApproachDock: return "ApproachDock";
        case Leg::Dock:         return "Dock";
        case Leg::Loiter:       return "Loiter";
        case Leg::Mine:         return "Mine";
        case Leg::Visit:        return "Visit";
        case Leg::Hunt:         return "Hunt";
        case Leg::Leave:        return "Leave";
        case Leg::Squad:        return "Squad";
        default:                return "None";
    }
}

const char* Ambient::RemovalName(uint8 reason)
{
    switch (reason) {
        case Removal::Jumped:   return "jumped";
        case Removal::Docked:   return "docked";
        case Removal::Fled:     return "fled";
        case Removal::OffGrid:  return "off grid";
        case Removal::Linger:   return "unwatched";
        case Removal::WindDown: return "wind down";
        case Removal::GM:       return "gm";
        default:                return "done";
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  AmbientMgr
 * ------------------------------------------------------------------------------------------------------------------ */
AmbientMgr::AmbientMgr()
: m_enabled(false),
  m_seed(0),
  m_travellerID(0),
  m_groupID(0),
  m_materialised(0),
  m_capHits(0),
  m_events(0),
  m_transitsDone(0),
  m_transitsDropped(0),
  m_nextMarketRefresh(0)
{
}

void AmbientMgr::Initialize()
{
    if (!sConfig.ambient.Enabled) {
        sLog.Warning("   Ambient NPCs", "Ambient NPCs Disabled.");
        return;
    }

    m_seed = sConfig.ambient.RngSeed;
    if (m_seed == 0)
        m_seed = static_cast<uint64_t>(GetSteadyTime()) ^ (static_cast<uint64_t>(std::time(nullptr)) << 20);
    m_rng.Seed(m_seed, 0x414D4249);   // "AMBI"
    m_lootRng.Seed(static_cast<uint32>(m_seed ^ (m_seed >> 32)));

    // the catalog's graphics check:  every entity and ship type's graphicID
    std::map<uint32, uint32> graphics;
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT t.typeID, t.graphicID FROM invTypes t JOIN invGroups g USING(groupID)"
                                " WHERE g.categoryID IN (6, 11)")) {
        while (res.GetRow(row))
            graphics[row.GetUInt(0)] = (row.IsNull(1) ? 0 : row.GetUInt(1));
    }
    m_catalog.Load(graphics);
    LoadMarket();
    LoadCorps();
    m_enabled = true;

    if (sConfig.ambient.MarketRefreshMinutes > 0)
        m_nextMarketRefresh = GetSteadyTime() + (static_cast<int64>(sConfig.ambient.MarketRefreshMinutes) * 60000);

    sLog.Green("   Ambient NPCs", "Ambient NPCs Enabled.  %s", m_catalog.Describe().c_str());
    uint32 corps(0);
    for (auto& cur : m_corps)
        corps += static_cast<uint32>(cur.second.size());
    sLog.Green("   Ambient NPCs", "%u market nodes, %u region shares, %u empire npc corps.  rng seed %llu", (uint32)m_nodes.size(),
               (uint32)m_regionShare.size(), corps, static_cast<unsigned long long>(m_seed));
    for (auto& cur : m_nodes)
        _log(AMBIENT__INFO, "AmbientMgr: market node station %u (system %u, kind %u):  h %.3f", cur.stationID, cur.systemID,
             cur.kind, cur.h);
}

void AmbientMgr::LoadMarket()
{
    m_nodes.clear();
    m_regionShare.clear();

    /* market hub turnover (6.2):  read-only SELECTs of the market sim's tables, never the sim itself.  missing tables or no
     *  turnover:  the configured hubs in their order get fixed scores and every region share is 0 */
    DBQueryResult res;
    DBResultRow row;
    bool haveNodes(sDatabase.RunQuery(res, "SHOW TABLES LIKE 'mktSimNode'") and (res.GetRowCount() > 0));
    bool haveCells(sDatabase.RunQuery(res, "SHOW TABLES LIKE 'mktSimCell'") and (res.GetRowCount() > 0));
    double Tmax(0.0);
    std::vector<std::pair<Node, double>> turnover;
    if (haveNodes and haveCells and sDatabase.RunQuery(res,
            "SELECT n.stationID, n.solarSystemID, n.kind, SUM(c.demandEwma * EXP(c.lnV))"
            " FROM mktSimNode n JOIN mktSimCell c ON c.nodeID = n.nodeID"
            " WHERE n.enabled = 1 GROUP BY n.nodeID")) {
        while (res.GetRow(row)) {
            Node n;
            n.stationID = row.GetUInt(0);
            n.systemID = row.GetUInt(1);
            n.kind = static_cast<uint8>(row.GetUInt(2));
            n.h = 0.0;
            double T(row.IsNull(3) ? 0.0 : row.GetDouble(3));
            if (!std::isfinite(T) or (T < 0.0))
                T = 0.0;
            if (T > Tmax)
                Tmax = T;
            turnover.push_back(std::make_pair(n, T));
        }
    }
    for (auto& cur : turnover) {
        if ((cur.first.kind != 1) and (cur.first.kind != 2))
            continue;
        cur.first.h = AmbientRules::NodeScore(cur.second, Tmax, cur.first.kind);
        if (cur.first.h > 0.0)
            m_nodes.push_back(cur.first);
    }
    if (m_nodes.empty() or !(Tmax > 0.0)) {
        m_nodes.clear();
        static const double fallback[] = {1.0, 0.8, 0.6, 0.55, 0.5};
        std::stringstream list(sConfig.marketSim.Hubs);
        std::string item;
        size_t i(0);
        while (std::getline(list, item, ',') and (i < 5)) {
            uint32 stationID(static_cast<uint32>(strtoul(item.c_str(), nullptr, 10)));
            StationData sData;
            if ((stationID == 0) or !stDataMgr.GetStationData(stationID, sData))
                continue;
            Node n;
            n.stationID = stationID;
            n.systemID = sData.systemID;
            n.kind = 1;
            n.h = fallback[i++];
            m_nodes.push_back(n);
        }
        _log(AMBIENT__WARN, "AmbientMgr: no market sim turnover.  %u configured hubs get fixed scores", (uint32)m_nodes.size());
        return;
    }

    bool haveShares(sDatabase.RunQuery(res, "SHOW TABLES LIKE 'mktCalRegionDefault'") and (res.GetRowCount() > 0));
    if (haveShares and sDatabase.RunQuery(res, "SELECT regionID, iskShare FROM mktCalRegionDefault")) {
        std::vector<std::pair<uint32, double>> shares;
        double maxShare(0.0);
        while (res.GetRow(row)) {
            double s(row.IsNull(1) ? 0.0 : row.GetDouble(1));
            shares.push_back(std::make_pair(row.GetUInt(0), s));
            if (s > maxShare)
                maxShare = s;
        }
        for (auto& cur : shares)
            m_regionShare[cur.first] = AmbientRules::RegionShare(cur.second, maxShare);
    }
}

void AmbientMgr::LoadCorps()
{
    m_corps.clear();
    // the four empires' npc corps (7.1, A3:  crpNPCCorporations has factionID)
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res, "SELECT corporationID, factionID, stationCount FROM crpNPCCorporations"
                                 " WHERE factionID IN (%u, %u, %u, %u)", factionAmarr, factionCaldari, factionGallente, factionMinmatar))
        return;
    while (res.GetRow(row))
        m_corps[row.GetUInt(1)].push_back(std::make_pair(row.GetUInt(0), !row.IsNull(2) and (row.GetUInt(2) > 0)));
}

void AmbientMgr::Process()
{
    if (!m_enabled)
        return;
    int64 now(GetSteadyTime());

    // the hourly market refresh.  profiles are recomputed as systems need them
    if ((m_nextMarketRefresh > 0) and (now >= m_nextMarketRefresh)) {
        m_nextMarketRefresh = now + (static_cast<int64>(sConfig.ambient.MarketRefreshMinutes) * 60000);
        LoadMarket();
        m_profiles.clear();
        _log(AMBIENT__INFO, "AmbientMgr: market scores refreshed (%u nodes)", (uint32)m_nodes.size());
    }

    // due transits (6.8 rule 2):  the destination system takes the ship if it is loaded and active, else nobody saw it
    if (!m_transits.empty()) {
        std::vector<Transit> due;
        for (auto itr = m_transits.begin(); itr != m_transits.end();) {
            if (itr->due <= now) {
                due.push_back(*itr);
                itr = m_transits.erase(itr);
            } else {
                ++itr;
            }
        }
        for (auto& cur : due) {
            SystemManager* pSys(sEntityList.FindSystem(cur.destSystem));
            if ((pSys != nullptr) and pSys->IsLoaded() and (pSys->GetAmbient() != nullptr) and pSys->GetAmbient()->IsActive()
            and pSys->GetAmbient()->AcceptTransit(cur.traveller, cur.destGate)) {
                ++m_transitsDone;
            } else {
                ++m_transitsDropped;
            }
        }
    }
}

const Ambient::Profile& AmbientMgr::GetProfile(uint32 systemID)
{
    std::map<uint32, Ambient::Profile>::iterator itr(m_profiles.find(systemID));
    if (itr != m_profiles.end())
        return itr->second;

    Ambient::Profile& p(m_profiles[systemID]);
    p.systemID = systemID;
    SystemData sData;
    if (!sDataMgr.GetSystemData(systemID, sData))
        return p;
    p.regionID = sData.regionID;
    p.trueSec = sData.securityRating;
    p.level = CrimeWatch::SecurityLevel(sData.securityRating);
    p.band = AmbientRules::GetBand(p.level, p.regionID);
    p.factionID = (sData.factionID != 0 ? sData.factionID : sDataMgr.GetRegionFaction(sData.regionID));
    p.homeRace = Ambient::FactionRace(p.factionID);
    std::vector<uint32> stations;
    sDataMgr.GetStationList(systemID, stations);
    p.stations = static_cast<uint32>(stations.size());
    p.S = AmbientRules::StationTerm(p.stations);

    // H:  the best market node here.  P:  the best hub's pull at its jump distance (8 at most)
    double bestPull(0.0);
    for (auto& cur : m_nodes) {
        if (cur.systemID == systemID)
            p.H = std::max(p.H, cur.h);
        if (cur.kind != 1)
            continue;
        int32 jumps(sMapData.GetJumpCount(systemID, cur.systemID, 8));
        double pull(AmbientRules::HubProximity(cur.h, jumps, sConfig.ambient.JumpFalloff));
        if ((jumps >= 0) and ((p.nearestHubSystem == 0) or (pull > bestPull))) {
            bestPull = pull;
            p.nearestHubSystem = cur.systemID;
            p.nearestHubJumps = jumps;
        }
    }
    p.P = bestPull;
    std::map<uint32, double>::iterator rItr(m_regionShare.find(p.regionID));
    p.R = (rItr == m_regionShare.end() ? 0.0 : rItr->second);
    p.A = AmbientRules::Busyness(p.H, p.P, p.R, p.S);
    return p;
}

double AmbientMgr::StationScore(uint32 stationID) const
{
    for (auto& cur : m_nodes)
        if (cur.stationID == stationID)
            return cur.h;
    return 0.0;
}

uint32 AmbientMgr::RandomCorp(uint32 factionID, NpcRng& rng) const
{
    std::map<uint32, std::vector<std::pair<uint32, bool>>>::const_iterator itr(m_corps.find(factionID));
    if ((itr == m_corps.end()) or itr->second.empty())
        return sDataMgr.GetFactionCorp(factionID);
    // three in four from corps that own stations:  they are the ones with ships to move
    std::vector<uint32> pool;
    bool stationsOnly(rng.Chance(0.75));
    for (auto& cur : itr->second)
        if (!stationsOnly or cur.second)
            pool.push_back(cur.first);
    if (pool.empty())
        for (auto& cur : itr->second)
            pool.push_back(cur.first);
    size_t idx(static_cast<size_t>(rng.Uniform() * static_cast<double>(pool.size())));
    return pool[std::min(idx, pool.size() - 1)];
}

void AmbientMgr::QueueTransit(const Ambient::Traveller& t, uint32 fromSystem, uint32 destSystem, uint32 destGate, int64 dueMs)
{
    if (!m_enabled or (destSystem == 0) or (destGate == 0))
        return;
    Transit tr;
    tr.traveller = t;
    tr.fromSystem = fromSystem;
    tr.destSystem = destSystem;
    tr.destGate = destGate;
    tr.due = dueMs;
    m_transits.push_back(tr);
}

void AmbientMgr::Register(AmbientSystem* pSystem, bool active)
{
    if (active) {
        m_active.insert(pSystem);
    } else {
        m_active.erase(pSystem);
    }
}

void AmbientMgr::AddMaterialised(int32 delta)
{
    int64 value(static_cast<int64>(m_materialised) + delta);
    m_materialised = static_cast<uint32>(value < 0 ? 0 : value);
}

std::string AmbientMgr::DescribeProfile(uint32 systemID)
{
    const Ambient::Profile& p(GetProfile(systemID));
    static const char* bands[] = {"none", "low", "high"};
    std::ostringstream str;
    str << std::fixed << std::setprecision(3);
    str << "<b>ambient profile of system " << systemID << "</b><br>";
    str << "security " << p.trueSec << " (level " << std::setprecision(1) << p.level << std::setprecision(3) << "), band "
        << bands[p.band < 3 ? p.band : 0] << ", region " << p.regionID << ", faction " << p.factionID << " ("
        << (p.homeRace < Ambient::Race::Count ? Ambient::RaceName(p.homeRace) : "no empire") << ")<br>";
    str << "H (market) " << p.H << ", P (hub proximity) " << p.P << ", R (region share) " << p.R << ", S (stations: "
        << p.stations << ") " << p.S << "<br>";
    str << "A (busyness) " << p.A << ".  nearest hub system " << p.nearestHubSystem << " at " << p.nearestHubJumps << " jumps<br>";
    double rate(AmbientRules::TrafficRate(p.A, sConfig.ambient.BusynessExponent, sConfig.ambient.TrafficRateAtHub,
                sConfig.ambient.TrafficRateFloor, (p.band == AmbientRules::Band::Low ? sConfig.ambient.LowSecMultiplier
                : sConfig.ambient.HighSecMultiplier), sConfig.ambient.DensityMultiplier, 1.0));
    str << "traffic " << std::setprecision(2) << (p.band == AmbientRules::Band::None ? 0.0 : rate)
        << " events/min (before the day/night factor)<br>";
    return str.str();
}

std::string AmbientMgr::DescribeGlobal()
{
    std::ostringstream str;
    str << "<b>ambient npcs</b> " << (m_enabled ? "enabled" : "disabled") << "<br>";
    str << m_catalog.Describe() << "<br>";
    str << "materialised " << m_materialised << " / " << sConfig.ambient.GlobalCap << " (global cap), cap hits " << m_capHits
        << ", events " << m_events << "<br>";
    str << "transits queued " << m_transits.size() << ", delivered " << m_transitsDone << ", dropped " << m_transitsDropped << "<br>";
    str << m_active.size() << " active systems<br>";
    for (auto pAmb : m_active)
        str << pAmb->Summary() << "<br>";
    return str.str();
}
