/**
 * @name Outpost.h
 *   Class for Outposts.
 *
 * @Author:           James
 * @date:   17 October 2021
 */

#ifndef EVEMU_POS_OUTPOST_H_
#define EVEMU_POS_OUTPOST_H_

#include "DataClasses.h"
#include "pos/Structure.h"
#include "station/Station.h"

// Class for Construction Platform (egg)
class PlatformSE
: public StructureSE
{
public:
    PlatformSE(StructureItemRef structure, EVEServiceManager &services, SystemManager *system, const FactionData &fData)
    : StructureSE(structure, services, system, fData)   { /* do nothing here */ }

    virtual ~PlatformSE()                               { /* do nothing here */ }

    /* class type pointer querys. */
    virtual PlatformSE*         GetPlatformSE()         { return this; }

    /* class type tests. */
    virtual bool                IsPlatformSE()          { return true; }
    virtual bool                isGlobal()              { return true; }
    virtual bool                IsOperSE()              { return true; }

};

// Class for Outpost (StationSE derivative)
class OutpostSE
: public StationSE
{
public:
    OutpostSE(StationItemRef station, EVEServiceManager &services, SystemManager* system);
    virtual ~OutpostSE()                                { /* Do nothing here */ }

    /* class type pointer querys. */
    virtual OutpostSE* GetOutpostSE()                   { return this; }
    /* Static */
    virtual bool IsOutpostSE()                          { return true; }
    // outposts can be locked, but take no damage
    /** @todo F14: sovereignty sieges (shield/armor reinforcement, services, capture) make outposts vulnerable */
    virtual bool IsInvul()                              { return true; }

    virtual void SpawnStationService(Client* pClient, StationData stData, uint32 serviceType);
    // the destructible service entities (Station_Services types) of the services in newMask that oldMask lacked
    void SpawnStationServices(Client* pClient, const StationData& stData, uint32 oldMask, uint32 newMask);

    /* works out what the outpost's upgrade level and improvements give (OutpostRules:  services, office slots, refinery,
     *  assembly lines) from its type's static data, saves it and updates the station data the clients get.
     *  false when the station is not a player outpost type */
    static bool Refresh(uint32 stationID);
};

#endif  // EVEMU_POS_OUTPOST_H_
