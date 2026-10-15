 /**
  * @name RewardMgrService.cpp
  *     rewardMgr: delayed rewards (the LP an incursion pays when its mothership dies)
  *
  * @date:          12 October 2026
  *
  *  the client asks for these by the rewardGroupID that map.GetIncursionGlobalReport() gives each incursion
  *   rewards = delayedRewards.get(data.rewardGroupID, None)
  *   data.loyaltyPoints = rewards[0].rewardQuantity if rewards else 0
  */

#include "eve-server.h"

#include "Client.h"
#include "map/RewardMgrService.h"
#include "system/cosmicMgrs/IncursionMgr.h"

RewardMgrService::RewardMgrService() :
    Service("rewardMgr", eAccessLevel_Character)
{
    this->Add("GetDelayedRewardsByGroupIDs", &RewardMgrService::GetDelayedRewardsByGroupIDs);
}

PyResult RewardMgrService::GetDelayedRewardsByGroupIDs(PyCallArgs& call, PyRep* groupIDs)
{
    return sIncursionMgr.GetDelayedRewards(call.client->GetCharacterID(), groupIDs);
}
