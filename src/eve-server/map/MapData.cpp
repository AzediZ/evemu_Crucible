
/**
 * @name MapData.cpp
 *   a group of methods and functions to get map info.
 *     this is mostly used for getting random points in system, system jumps, and misc mission destination info
 *  - added static data for StationExtraInfo (from mapservice)
 * @Author:         Allan
 * @date:   13 November 2018
 */
#include "../StaticDataMgr.h"
#include "agents/Agent.h"
#include "map/MapData.h"
#include "map/MapDB.h"
#include "station/StationDataMgr.h"
#include "system/SystemManager.h"
#include "system/SystemEntity.h"

#include "../../eve-common/EVE_Map.h"


MapData::MapData()
: m_stationExtraInfo(nullptr),
m_pseudoSecurities(nullptr)
{
    m_regionJumps.clear();
    m_constJumps.clear();
    m_systemJumps.clear();
}

void MapData::Close()
{
    PySafeDecRef(m_stationExtraInfo);
    PySafeDecRef(m_pseudoSecurities);
}

int MapData::Initialize()
{
    Populate();
    sLog.Blue("          MapData", "Map Data Manager Initialized.");
    return 1;
}

void MapData::Clear()
{
    m_regionJumps.clear();
    m_constJumps.clear();
    m_systemJumps.clear();
    m_neighbors.clear();
    m_sysConst.clear();
    m_sysRegion.clear();
    m_constSystems.clear();
    m_constAdj.clear();
}

void MapData::GetInfo()
{
    // print out list of bad jumps
    // m_badJumps
}

void MapData::Populate()
{
    m_pseudoSecurities = MapDB::GetPseudoSecurities();

    double start = GetTimeMSeconds();

    m_stationExtraInfo = new PyTuple(3);
    m_stationExtraInfo->items[0] = MapDB::GetStationExtraInfo();
    m_stationExtraInfo->items[1] = MapDB::GetStationOpServices();
    m_stationExtraInfo->items[2] = MapDB::GetStationServiceInfo();
    sLog.Cyan("          MapData", "StationExtraInfo loaded in %.3fms.",(GetTimeMSeconds() - start));

    start = GetTimeMSeconds();
    DBQueryResult* res = new DBQueryResult();
    MapDB::GetSystemJumps(*res);
    DBResultRow row;
    while (res->GetRow(row)) {
        //SELECT ctype, fromsol, tosol FROM mapConnections
        if (row.GetInt(0) == Map::Jumptype::Region) {
            m_regionJumps.emplace(row.GetInt(1), row.GetInt(2));
        } else if (row.GetInt(0) == Map::Jumptype::Constellation) {
            m_constJumps.emplace(row.GetInt(1), row.GetInt(2));
        } else {
            m_systemJumps.emplace(row.GetInt(1), row.GetInt(2));
        }
        AddNeighbor(row.GetUInt(1), row.GetUInt(2));
        AddNeighbor(row.GetUInt(2), row.GetUInt(1));
    }

    sLog.Cyan("          MapData", "%lu Region jumps, %lu Constellation jumps and %lu System jumps loaded in %.3fms.", //
              m_regionJumps.size(), m_constJumps.size(), m_systemJumps.size(), (GetTimeMSeconds() - start));

    // cleanup
    SafeDelete(res);

    // system -> constellation/region, and constellation adjacency (for agent ranges)
    start = GetTimeMSeconds();
    DBQueryResult locRes;
    MapDB::GetSolarSystemLocations(locRes);
    while (locRes.GetRow(row)) {
        // SELECT solarSystemID, constellationID, regionID FROM mapSolarSystems
        uint32 systemID = row.GetUInt(0);
        m_sysConst[systemID] = row.GetUInt(1);
        m_sysRegion[systemID] = row.GetUInt(2);
        m_constSystems.emplace(row.GetUInt(1), systemID);
    }
    for (auto& cur : m_neighbors) {
        std::map<uint32, uint32>::iterator fromItr = m_sysConst.find(cur.first);
        if (fromItr == m_sysConst.end())
            continue;
        for (auto toSys : cur.second) {
            std::map<uint32, uint32>::iterator toItr = m_sysConst.find(toSys);
            if ((toItr == m_sysConst.end()) or (toItr->second == fromItr->second))
                continue;
            m_constAdj[fromItr->second].insert(toItr->second);
        }
    }
    sLog.Cyan("          MapData", "%lu systems in %lu constellations mapped in %.3fms.", //
              m_sysConst.size(), m_constAdj.size(), (GetTimeMSeconds() - start));
}

void MapData::AddNeighbor(uint32 fromSys, uint32 toSys)
{
    std::vector<uint32>& list = m_neighbors[fromSys];
    if (std::find(list.begin(), list.end(), toSys) == list.end())
        list.push_back(toSys);
}

int32 MapData::GetJumpCount(uint32 fromSys, uint32 toSys, uint8 maxJumps/*50*/)
{
    if (fromSys == toSys)
        return 0;

    // breadth-first over stargates
    std::set<uint32> seen;
    std::vector<uint32> current(1, fromSys), next;
    seen.insert(fromSys);
    for (uint8 jumps = 1; jumps <= maxJumps; ++jumps) {
        next.clear();
        for (auto sys : current) {
            std::map<uint32, std::vector<uint32>>::iterator itr = m_neighbors.find(sys);
            if (itr == m_neighbors.end())
                continue;
            for (auto to : itr->second) {
                if (to == toSys)
                    return jumps;
                if (seen.insert(to).second)
                    next.push_back(to);
            }
        }
        if (next.empty())
            break;
        current.swap(next);
    }
    return -1;
}

void MapData::GetSystemsWithinJumps(uint32 from, uint8 maxJumps, std::vector<std::pair<uint32, uint8>>& out)
{
    std::set<uint32> seen;
    std::vector<uint32> current(1, from), next;
    seen.insert(from);
    out.push_back(std::make_pair(from, (uint8)0));
    for (uint8 jumps = 1; jumps <= maxJumps; ++jumps) {
        next.clear();
        for (auto sys : current) {
            std::map<uint32, std::vector<uint32>>::iterator itr = m_neighbors.find(sys);
            if (itr == m_neighbors.end())
                continue;
            for (auto to : itr->second) {
                if (seen.insert(to).second) {
                    next.push_back(to);
                    out.push_back(std::make_pair(to, jumps));
                }
            }
        }
        if (next.empty())
            break;
        current.swap(next);
    }
}

void MapData::AddConstellationSystems(const std::set<uint32>& consts, std::vector<uint32>& out)
{
    for (auto constID : consts) {
        auto range = m_constSystems.equal_range(constID);
        for (auto itr = range.first; itr != range.second; ++itr)
            out.push_back(itr->second);
    }
}

void MapData::GetSystemsInAgentRange(uint32 originSys, uint8 range, std::vector<uint32>& out)
{
    using namespace Agents::Range;

    // faction warfare is out of scope:  nearest combat zone -> same or neighbouring system.  career hub -> agent's system
    if (range == NearestEnemyCombatZone)
        range = SameOrNeighboringSystem;
    else if ((range == NearestCareerHub) or (range == 0))
        range = SameSystem;

    uint32 constID(0), regionID(0);
    std::map<uint32, uint32>::iterator cItr = m_sysConst.find(originSys);
    if (cItr != m_sysConst.end())
        constID = cItr->second;
    std::map<uint32, uint32>::iterator rItr = m_sysRegion.find(originSys);
    if (rItr != m_sysRegion.end())
        regionID = rItr->second;

    std::vector<uint32> neighbors;
    std::map<uint32, std::vector<uint32>>::iterator nItr = m_neighbors.find(originSys);
    if (nItr != m_neighbors.end())
        neighbors = nItr->second;

    switch (range) {
        case SameSystem: {
            out.push_back(originSys);
        } break;
        case SameOrNeighboringSystemSameConstellation:
        case NeighboringSystemSameConstellation: {
            if (range == SameOrNeighboringSystemSameConstellation)
                out.push_back(originSys);
            for (auto sys : neighbors)
                if (m_sysConst[sys] == constID)
                    out.push_back(sys);
        } break;
        case SameOrNeighboringSystem:
        case NeighboringSystem: {
            if (range == SameOrNeighboringSystem)
                out.push_back(originSys);
            for (auto sys : neighbors)
                out.push_back(sys);
        } break;
        case SameConstellation: {
            AddConstellationSystems(std::set<uint32>{constID}, out);
        } break;
        case SameOrNeighboringConstellationSameRegion:
        case SameOrNeighboringConstellation:
        case NeighboringConstellationSameRegion:
        case NeighboringConstellation: {
            bool sameRegion = ((range == SameOrNeighboringConstellationSameRegion) or (range == NeighboringConstellationSameRegion));
            std::set<uint32> consts;
            if ((range == SameOrNeighboringConstellationSameRegion) or (range == SameOrNeighboringConstellation))
                consts.insert(constID);
            std::map<uint32, std::set<uint32>>::iterator aItr = m_constAdj.find(constID);
            if (aItr != m_constAdj.end()) {
                for (auto adj : aItr->second) {
                    if (sameRegion) {
                        // a constellation's systems share its region
                        auto sItr = m_constSystems.find(adj);
                        if ((sItr == m_constSystems.end()) or (m_sysRegion[sItr->second] != regionID))
                            continue;
                    }
                    consts.insert(adj);
                }
            }
            AddConstellationSystems(consts, out);
        } break;
        default: {
            _log(AGENT__ERROR, "MapData::GetSystemsInAgentRange() - unknown range %u", range);
            out.push_back(originSys);
        } break;
    }
}



void MapData::GetMissionDestination(Agent* pAgent, uint8 misionType, MissionOffer& offer)
{
    using namespace Mission::Type;
    using namespace Agents::Range;

    uint8 destRange = offer.range;
    bool station = true, ship = false;    // will have to tweak this later for particular mission events

    // determine distance based on preset range from db or in some cases, mission type and agent level
    switch(misionType) {
        case Tutorial: {
            // always same system?
            destRange = SameSystem;
        } break;
        case Data:
        case Trade:
        case Courier:
        case Research: {
            //destRange += m_data.level *2;
        } break;
        case EpicArc:
        case Anomic:
        case Burner:
        case Cosmos: {
            station = false;
            destRange += pAgent->GetLevel();
        } break;
        case Mining:
        case Encounter:
        case Storyline: {
            station = false;
            //destRange = offer.range;
        } break;
    }

    // missions that add the agent level can go past the constellation ranges
    if ((destRange > NeighboringConstellation) and (destRange != NearestEnemyCombatZone) and (destRange != NearestCareerHub))
        destRange = NeighboringConstellation;

    /* candidate systems for this range.  station missions need a station other than the agent's own:
     *  when the range has none, widen it (same or neighbouring system, then neighbouring constellations)
     */
    uint32 originSys = pAgent->GetSystemID();
    std::vector<uint32> candidates;
    const uint8 ranges[3] = { destRange, SameOrNeighboringSystem, SameOrNeighboringConstellation };
    for (uint8 i = 0; (i < 3) and candidates.empty(); ++i) {
        if ((i > 0) and (ranges[i] == ranges[i - 1]))
            continue;
        std::vector<uint32> systems;
        GetSystemsInAgentRange(originSys, ranges[i], systems);
        if (!station) {
            candidates = systems;
            break;
        }
        for (auto sys : systems) {
            std::vector<uint32> list;
            sDataMgr.GetStationList(sys, list);
            for (auto stationID : list)
                if (stationID != pAgent->GetStationID())
                    candidates.push_back(stationID);
        }
        if (!candidates.empty() and (i > 0))
            _log(AGENT__WARNING, "Agent::GetMissionDestination() - no station in range %u of %u; used range %u.", destRange, originSys, ranges[i]);
    }

    if (candidates.empty()) {
        // nothing in range:  the agent's own station (or system)
        _log(AGENT__ERROR, "Agent::GetMissionDestination() - no destination found in range %u of %u.", destRange, originSys);
        offer.destinationID = (station ? pAgent->GetStationID() : originSys);
    } else {
        offer.destinationID = candidates.at(MakeRandomInt(0, (candidates.size() -1)));
    }

    if (ship) {
        ;  // code here for agent in ship
    }

    if (sDataMgr.IsStation(offer.destinationID)) {
        StationData data = StationData();
        stDataMgr.GetStationData(offer.destinationID, data);
        offer.destinationOwnerID    = data.corporationID;
        offer.destinationSystemID   = data.systemID;
        offer.destinationTypeID     = data.typeID;
    } else if (ship) {
        offer.destinationSystemID   = offer.destinationID;
        offer.destinationTypeID     = sDataMgr.GetStaticType(offer.destinationID);
        offer.dungeonLocationID     = offer.destinationID;
        offer.dungeonSolarSystemID  = offer.destinationID;
    } else {
        offer.destinationSystemID   = offer.destinationID;
        offer.destinationTypeID     = sDataMgr.GetStaticType(offer.destinationID);
        offer.dungeonLocationID     = offer.destinationID;
        offer.dungeonSolarSystemID  = offer.destinationID;
    }
}

void MapData::GetPlanets(uint32 systemID, std::vector<DBGPointEntity>& into) {
    uint8 total = 0;
    MapDB::GetPlanets(systemID, into, total);
}

void MapData::GetMoons(uint32 systemID, std::vector<DBGPointEntity>& into) {
    uint8 total = 0;
    MapDB::GetMoons(systemID, into, total);
}

const GPoint MapData::GetRandPointOnPlanet(uint32 systemID) {
    std::vector<DBGPointEntity> planetIDs;
    GetPlanets(systemID, planetIDs);
    if (planetIDs.empty())
        return NULL_ORIGIN;

    const DBGPointEntity& planet = planetIDs[MakeRandomInt(0, (planetIDs.size() -1))];
    // just outside the planet
    GPoint pos(planet.position);
    pos.MakeRandomPointOnSphereLayer(planet.radius + 25000, planet.radius + 50000);
    return pos;
}

const GPoint MapData::GetRandPointOnMoon(uint32 systemID) {
    std::vector<DBGPointEntity> moonIDs;
    GetMoons(systemID, moonIDs);
    if (moonIDs.empty())
        return NULL_ORIGIN;

    const DBGPointEntity& moon = moonIDs[MakeRandomInt(0, (moonIDs.size() -1))];
    // just outside the moon
    GPoint pos(moon.position);
    pos.MakeRandomPointOnSphereLayer(moon.radius + 5000, moon.radius + 10000);
    return pos;
}

uint32 MapData::GetRandPlanet(uint32 systemID) {
    std::vector<DBGPointEntity> planetIDs;
    GetPlanets(systemID, planetIDs);
    if (planetIDs.empty())
        return 0;

    return planetIDs[MakeRandomInt(0, (planetIDs.size() -1))].itemID;
}

const GPoint MapData::Get2RandPlanets(uint32 systemID) {
    std::vector<DBGPointEntity> planetIDs;
    GetPlanets(systemID, planetIDs);
    if (planetIDs.size() < 2)
        return GetRandPointOnPlanet(systemID);

    uint32 first = MakeRandomInt(0, (planetIDs.size() -1));
    uint32 second = MakeRandomInt(0, (planetIDs.size() -2));
    if (second >= first)
        ++second;
    const GPoint& a = planetIDs[first].position;
    const GPoint& b = planetIDs[second].position;
    return GPoint((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2);
}

const GPoint MapData::Get3RandPlanets(uint32 systemID) {
    std::vector<DBGPointEntity> planetIDs;
    GetPlanets(systemID, planetIDs);
    if (planetIDs.size() < 3)
        return GetRandPointOnPlanet(systemID);

    // three different planets:  shuffle the first three places
    for (uint8 i = 0; i < 3; ++i)
        std::swap(planetIDs[i], planetIDs[MakeRandomInt(i, (planetIDs.size() -1))]);
    const GPoint& a = planetIDs[0].position;
    const GPoint& b = planetIDs[1].position;
    const GPoint& c = planetIDs[2].position;
    return GPoint((a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3);
}

uint32 MapData::GetRandMoon(uint32 systemID) {
    std::vector<DBGPointEntity> moonIDs;
    GetMoons(systemID, moonIDs);
    if (moonIDs.empty())
        return 0;

    return moonIDs[MakeRandomInt(0, (moonIDs.size() -1))].itemID;
}

const GPoint MapData::GetRandPointInSystem(uint32 systemID, int64 distance) {
    // the sun is at the system origin
    SystemData data = SystemData();
    sDataMgr.GetSystemData(systemID, data);
    if ((data.radius > 0) and (distance > data.radius))
        distance = data.radius;
    if (distance < 1)
        return NULL_ORIGIN;

    GPoint pos(NULL_ORIGIN);
    pos.MakeRandomPointOnSphere((double)distance);
    return pos;
}

const GPoint MapData::GetAnomalyPoint(SystemManager* pSys)
{
    std::vector<DBGPointEntity> planetIDs;
    GetPlanets(pSys->GetID(), planetIDs);
    if (planetIDs.empty())
        return NULL_ORIGIN;

    const DBGPointEntity& planet = planetIDs[MakeRandomInt(0, (planetIDs.size() -1))];
    SystemEntity* pSE(pSys->GetSE(planet.itemID));
    GPoint pos(pSE != nullptr ? pSE->GetPosition() : planet.position);
    pos.MakeRandomPointOnSphereLayer(ONE_AU_IN_METERS / 3, ONE_AU_IN_METERS * 4);
    return pos;
}

const GPoint MapData::GetAnomalyPoint(uint32 systemID)
{
    std::vector<DBGPointEntity> planetIDs;
    GetPlanets(systemID, planetIDs);
    if (planetIDs.empty())
        return NULL_ORIGIN;

    GPoint pos(planetIDs[MakeRandomInt(0, (planetIDs.size() -1))].position);
    pos.MakeRandomPointOnSphereLayer(ONE_AU_IN_METERS / 3, ONE_AU_IN_METERS * 4);
    return pos;
}
