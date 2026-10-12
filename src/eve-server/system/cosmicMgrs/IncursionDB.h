 /**
  * @name IncursionDB.h
  *     Incursion database queries for EVEmu
  *
  * @date:          12 October 2026
  *
  */

#ifndef EVEMU_SYSTEM_INCURSIONDB_H_
#define EVEMU_SYSTEM_INCURSIONDB_H_

#include "eve-server.h"

class IncursionDB
{
public:
    // incSiteTypes (siteTypeID, siteName, sceneType, designFleet, iskReward, lpReward, influence)
    static void GetSiteTypes(DBQueryResult& res);
    // incSiteObjects (siteTypeID, wave, typeID, quantity, x, y, z)
    static void GetSiteObjects(DBQueryResult& res);

    static void GetIncursions(DBQueryResult& res);
    static void GetIncursionSystems(DBQueryResult& res);
    // returns new incursionID, 0 on failure
    static uint32 CreateIncursion(uint32 constellationID, uint32 stagingSystemID, uint32 now);
    static void AddIncursionSystem(uint32 incursionID, uint32 systemID, uint8 sceneType);
    static void SaveIncursion(uint32 incursionID, uint8 state, double influence, uint32 stateTime, uint32 lastProgress);
    static void DeleteIncursion(uint32 incursionID);

    // constellations that can host an incursion (empire space, all systems at or above minSec, at least minSystems systems, a station)
    static void GetCandidateConstellations(DBQueryResult& res, float minSec, uint8 minSystems);
    // solarSystemID, security, hasStation, isBorder
    static void GetConstellationSystems(DBQueryResult& res, uint32 constellationID);
    // fromSolarSystemID, toSolarSystemID inside the constellation
    static void GetConstellationJumps(DBQueryResult& res, uint32 constellationID);
    static std::string GetConstellationName(uint32 constellationID);

    static void DeleteSignature(const std::string& sigID);
};

#endif  // EVEMU_SYSTEM_INCURSIONDB_H_
