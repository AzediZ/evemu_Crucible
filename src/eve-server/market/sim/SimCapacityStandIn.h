 /**
  * @name SimCapacityStandIn.h
  *   market simulation:  a copy of the phase 3 capacity interface (MktSim::SimJob in SimTypes.h and Prod::ICapacity in
  *   SimProduction.h, as on wt/mkt3 44e37393) for building SimLineCapacity before phase 3 is merged.  SimLineCapacity.h
  *   only includes it while SimProduction.h does not exist;  once phase 3 is in, this file is not used and can be deleted
  *
  * @date:          15Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_CAPACITY_STAND_IN_H__
#define _EVE_SERVER_MARKET_SIM_CAPACITY_STAND_IN_H__

#include "market/sim/SimTypes.h"

namespace MktSim {

    // a sim industry job (mktSimJob).  the inputs were used up when it started;  the output appears when it ends
    struct SimJob {
        uint32_t jobID          = 0;
        uint32_t actorID        = 0;
        uint32_t nodeID         = 0;
        uint32_t stationID      = 0;    // where it runs (the node's station until phase 4 adds production sites)
        uint8_t  activity       = 1;    // Activity::Manufacturing
        uint16_t typeID         = 0;    // output
        uint32_t runs           = 0;
        Units    qtyOut         = 0;
        uint32_t lineID         = 0;    // the assembly line it holds (0:  virtual capacity, PLAN 6.6 mode false)
        int64_t  start          = 0;
        int64_t  end            = 0;
    };

namespace Prod {

    class ICapacity {
    public:
        virtual ~ICapacity()                            { }
        virtual void     SetSlots(uint32_t actorID, uint32_t slots) = 0;
        virtual uint32_t Slots(uint32_t actorID) const = 0;
        virtual uint32_t Free(uint32_t actorID, uint32_t nodeID, uint8_t activity, int64_t now) const = 0;
        virtual bool     Reserve(SimJob& job, int64_t now) = 0;
        virtual void     Restore(const SimJob& job) = 0;
        virtual void     Release(const SimJob& job) = 0;
        virtual double   JobCost(const SimJob& job) const = 0;
    };

}
}

#endif  // _EVE_SERVER_MARKET_SIM_CAPACITY_STAND_IN_H__
