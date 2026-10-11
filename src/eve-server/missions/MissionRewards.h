
 /**
  * @name MissionRewards.h
  *   agent mission rewards (Crucible):  ISK with Negotiation and the time bonus, LP from the agent's system security and
  *   the division's Connections skill, and the fleet share of completed missions
  *
  *   LP = base x (1.6288 - agent system security) x (1 + 10% x division Connections level)
  *   ISK = base (x (1.6288 - security), missions.IskSecurityScaling) x (1 + 5% x Negotiation level)
  */


#ifndef _EVE_SERVER_MISSION_REWARDS_H__
#define _EVE_SERVER_MISSION_REWARDS_H__


#include "../eve-server.h"
#include "../../eve-common/EVE_Missions.h"

class Agent;
class Client;
class Character;

namespace MissionRewards {
    // scales the offer's base rewards (MissionDataMgr::CreateMissionOffer) for this pilot, at offer time.  pChar may be null (no skills)
    void Compute(Agent* pAgent, Character* pChar, MissionOffer& offer);
    // the pilots sharing a completion:  the completer first, then fleet members (missions.FleetShareSameSystem), up to FleetShareMax
    void GetSharers(Client* pClient, std::vector<Client*>& out);
    // cargo volume of the item rewards (they go to the completer)
    float GetItemVolume(const MissionOffer& offer);
    /* pays a completed mission:  ISK, the time bonus and LP split equally between the sharers (the completer alone when
     *  there is one), the item reward to the completer (station hangar when docked, else cargo hold) */
    void Pay(Agent* pAgent, Client* pClient, MissionOffer& offer, const std::vector<Client*>& sharers);
}


#endif  // _EVE_SERVER_MISSION_REWARDS_H__
