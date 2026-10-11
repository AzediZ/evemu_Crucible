
/**
 * @name MapData.h
 *   a group of methods and functions to get map info.
 *     this is mostly used for getting random points in system, system jumps, and misc mission destination info
 * @Author:         Allan
 * @date:   13 November 2018
 */


#ifndef _EVE_MAP_MAPDATA_H_
#define _EVE_MAP_MAPDATA_H_

#include "../eve-server.h"

#include "../../eve-common/EVE_Missions.h"


class Agent;
class SystemManager;

class MapData
: public Singleton< MapData >
{
public:
    MapData();
    ~MapData()                                          { /* do nothing here */ }

    int                 Initialize();

    void                Clear();
    void                Close();

    void                GetInfo();

    PyTuple*            GetStationExtraInfo()           { PyIncRef(m_stationExtraInfo); return m_stationExtraInfo; }
    PyObject*           GetPseudoSecurities()           { PyIncRef(m_pseudoSecurities); return m_pseudoSecurities; }

    void                GetMissionDestination(Agent* pAgent, uint8 misionType, MissionOffer& offer);


    void                GetMoons(uint32 systemID, std::vector<DBGPointEntity>& into);
    void                GetPlanets(uint32 systemID, std::vector<DBGPointEntity>& into);
    uint32              GetRandMoon(uint32 systemID);
    uint32              GetRandPlanet(uint32 systemID);
    const GPoint        GetAnomalyPoint(SystemManager* pSys);
    const GPoint        GetAnomalyPoint(uint32 systemID);
    // midpoint of two different random planets (a random planet point when there are fewer)
    const GPoint        Get2RandPlanets(uint32 systemID);
    // centroid of three different random planets (a random planet point when there are fewer)
    const GPoint        Get3RandPlanets(uint32 systemID);
    // random points just outside a random moon/planet
    const GPoint        GetRandPointOnMoon(uint32 systemID);
    const GPoint        GetRandPointOnPlanet(uint32 systemID);
    // random point 'distance' from the sun, kept inside the system radius
    const GPoint        GetRandPointInSystem(uint32 systemID, int64 distance);

    /* stargate routing (mapConnections, both directions).  shortest jump count, -1 when farther than maxJumps */
    int32               GetJumpCount(uint32 fromSys, uint32 toSys, uint8 maxJumps = 50);
    // every system within maxJumps of 'from' (including 'from' at 0), with its jump count
    void                GetSystemsWithinJumps(uint32 from, uint8 maxJumps, std::vector<std::pair<uint32, uint8>>& out);
    // systems matching an agent destination range (Agents::Range, EVE_Agent.h).  neighbouring = one jump away
    void                GetSystemsInAgentRange(uint32 originSys, uint8 range, std::vector<uint32>& out);

protected:
    void                Populate();

private:
    PyTuple*            m_stationExtraInfo;
    PyObject*           m_pseudoSecurities;

    std::multimap<uint32, uint32>        m_regionJumps;  //fromSys/toSys
    std::multimap<uint32, uint32>        m_constJumps;   //fromSys/toSys
    std::multimap<uint32, uint32>        m_systemJumps;  //fromSys/toSys

    std::map<uint32, std::vector<uint32>>   m_neighbors;    //system/systems one jump away (all jump types, both directions)
    std::map<uint32, uint32>                m_sysConst;     //system/constellation
    std::map<uint32, uint32>                m_sysRegion;    //system/region
    std::multimap<uint32, uint32>           m_constSystems; //constellation/system
    std::map<uint32, std::set<uint32>>      m_constAdj;     //constellation/adjacent constellations

    void                AddNeighbor(uint32 fromSys, uint32 toSys);
    // systems of these constellations
    void                AddConstellationSystems(const std::set<uint32>& consts, std::vector<uint32>& out);
};


//Singleton
#define sMapData \
( MapData::get() )


#endif  // _EVE_MAP_MAPDATA_H_

/**
 *   the purpose of this class is to have a common location with methods used to define
 *      random points in solar systems based on planet and moon positions.
 *   GetRandPointOnPlanet() will query solar systems for planets, pick a random planet,
 *      and define a coordnate within that planet's bubble.
 *   GetRandPointOnMoon() does same as above, but using moons.
 *   Get2RandPlanets() will pick a random point between 2 planets, for warp-out/warp-in
 *      and other things as we see fit.
 *   Get3RandPlanets() does same as above, but using 3 planets.
 *   GetRandPointInSystem() will define a random point within a given system.
 *
 *   this class of methods should be used for positioning mission space, cosmic signatures,
 *      anomolies, complexes, and other things needing a random position in a given system.
 *
 *   these methods will also be used for random npc spawns and their warping/movement.
 *
 *
 *  NOTE i seem to remember that ALL COSMIC SPAWNS are within 6au from planets.
 *       ....cant find that info now.  -allan 31Jul14
 */
