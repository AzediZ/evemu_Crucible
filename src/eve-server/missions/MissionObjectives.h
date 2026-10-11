/**
  * @name MissionObjectives.h
  *   mission objective checks and turn-in (what Complete needs, and what it takes from the pilot)
  *
  *   Check() has no side effects.  everything that removes items from the pilot is in Consume().
  *   new mission types add a case to both (Encounter, Trade, Research/Data, Storyline...)
  */


#ifndef _EVE_SERVER_MISSION_OBJECTIVES_H__
#define _EVE_SERVER_MISSION_OBJECTIVES_H__


#include "../eve-server.h"
#include "../../eve-common/EVE_Missions.h"

class Client;

class MissionObjectives
{
public:
    // station where the objective is turned in (courier: destinationID; mining: originID (agent's station)).  0 if not location-bound
    static uint32 GetDeliveryLocation(const MissionOffer& offer);
    // returns Mission::Objective::*.  no side effects
    static uint8 Check(Client* pClient, const MissionOffer& offer);
    // removes the delivered items of item-based objectives.  call only after Check() == Complete.
    //  returns false (and takes nothing) if it could not take everything
    static bool Consume(Client* pClient, const MissionOffer& offer);
    // qty of typeID owned by the char, unassembled, in the station's item hangar + the active ship's cargo (when docked there).
    //  stationID 0 counts the active ship's cargo only
    static uint32 CountDelivered(Client* pClient, uint32 stationID, uint16 typeID);
    // takes qty from the hangar first, then the cargo, and deletes the taken stacks.
    //  false (nothing taken) if there is not enough, unless allowPartial, which takes what there is
    static bool TakeDelivered(Client* pClient, uint32 stationID, uint16 typeID, uint32 qty, bool allowPartial = false);
    // does accepting this mission give the pilot cargo (courier package)
    static bool SpawnsCargoOnAccept(const MissionOffer& offer)  { return offer.typeID == Mission::Type::Courier; }
};

#endif  // _EVE_SERVER_MISSION_OBJECTIVES_H__
