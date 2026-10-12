 /**
  * @name RewardMgrService.h
  *     rewardMgr: delayed rewards (the LP an incursion pays when its mothership dies)
  *
  * @date:          12 October 2026
  *
  */

#ifndef EVEMU_MAP_REWARDMGRSERVICE_H_
#define EVEMU_MAP_REWARDMGRSERVICE_H_

#include "services/Service.h"

class RewardMgrService : public Service <RewardMgrService>
{
public:
    RewardMgrService();

protected:
    // the Journal's Incursions tab: {rewardGroupID: [util.KeyVal(rewardQuantity)]}
    PyResult GetDelayedRewardsByGroupIDs(PyCallArgs& call, PyList* groupIDs);
};

#endif  // EVEMU_MAP_REWARDMGRSERVICE_H_
