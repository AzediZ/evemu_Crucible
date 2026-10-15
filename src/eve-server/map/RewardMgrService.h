 /**
  * @name RewardMgrService.h
  *     rewardMgr: delayed rewards (the LP an incursion pays when its mothership dies) and the site payout tables
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
    // the Journal's Incursions tab: {rewardGroupID: [util.KeyVal(rewardQuantity)]}.  groupIDs is a tuple from the
    //  Crucible client (seen in a server log), so take any sequence
    PyResult GetDelayedRewardsByGroupIDs(PyCallArgs& call, PyRep* groupIDs);   // the client sends a tuple
    // the Journal's encounter list and its payout chart: what a site of one kind pays by fleet size
    PyResult GetRewardData(PyCallArgs& call, PyInt* rewardID);
};

#endif  // EVEMU_MAP_REWARDMGRSERVICE_H_
