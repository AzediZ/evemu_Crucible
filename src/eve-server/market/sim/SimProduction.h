
 /**
  * @name SimProduction.h
  *   market simulation:  recipes and capacity of the supply chain (PLAN 6.4, 6.6, phase 3).
  *     refining          whole batches at the station's yield, less the station's take (Crucible ReprocessingService)
  *     manufacturing     the bill of materials per unit (SimStaticData), the job's hours, its line cost
  *     capacity          ICapacity:  how many jobs an actor may run and what a line costs.  phase 3 runs on
  *                       VirtualCapacity (a slot count per actor, nothing written);  phase 5 plugs in the real
  *                       assembly lines (ramAssemblyLines.nextFreeTime) behind the same interface
  *     mining mix        the mix of ores a consortium's fleet mines so its minerals meet the node's needs (a
  *                       non-negative least squares fit, solved once when the world is built)
  *   pure functions and small classes, no server dependencies
  *
  * @date:          15Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_PRODUCTION_H__
#define _EVE_SERVER_MARKET_SIM_PRODUCTION_H__

#include <map>
#include <utility>
#include <vector>

#include "market/sim/SimStaticData.h"

namespace MktSim {
namespace Prod {

    /* refining (PLAN 6.6):  station efficiency + 0.375 x 1.1 x 1.2 x 1.25 (the actors have every skill at V), at most 1.
     *  Crucible ReprocessingService::CalcReprocessingEfficiency */
    double RefineYield(double stationEfficiency);

    struct RefineResult {
        Units used = 0;                                         // input units refined (whole batches)
        std::vector<std::pair<uint16_t, Units>> gross;          // output before the station's take
        std::vector<std::pair<uint16_t, Units>> tax;            // the station's take (a goods sink)
    };
    // refines up to qty units of t (whole portionSize batches).  nothing when qty is less than a batch
    void Refine(const TypeData& t, Units qty, double yield, double take, RefineResult& into);
    // what one unit of t refines into at these prices (isk per unit, after yield and take).  price(typeID) -> isk
    template <class PriceFn>
    double RefineValue(const TypeData& t, double yield, double take, PriceFn price)
    {
        double v(0.0);
        for (auto& out : t.refine)
            v += out.second * price(out.first);
        return v * yield * (1.0 - take);
    }

    /* manufacturing.  hours one unit takes:  productionTime per run (portionSize units) x timeMultiplier (Industry V
     *  0.8 and the line's multiplier, RamMethods::Calculate).  a type without a blueprint time gets defaultSeconds */
    double BuildHours(const TypeData& t, double timeMultiplier, double defaultSeconds);

    /* how many jobs an actor may run and what a job costs.  the world holds every job (World::Jobs, persisted in
     *  mktSimJob) and hands a finished job's output to its actor itself (World::CompleteJob, at job.end);  the capacity
     *  only says yes or no and what a line costs, so a real-line implementation (phase 5:  FactoryDB's free lines of
     *  job.stationID for job.activity, nextFreeTime written back in batches) replaces VirtualCapacity without touching
     *  the actors (World::SetCapacity, before Build).  the calls:
     *    Free      before planning:  how many more jobs the actor may start at the node now
     *    Reserve   a job about to start (actorID, nodeID, stationID, activity, typeID, runs, start, end set):  pick a
     *              line, set job.lineID, and job.end when the line's time multiplier changes it;  false:  none free
     *    JobCost   then:  what the line charges (the actor pays it as Isk::LineCost;  if it can't, Release follows)
     *    Release   the job ended (its output is the actor's now) or was dropped:  the line is free (nextFreeTime may stay)
     *    Restore   a job loaded from mktSimJob holds its line again */
    class ICapacity {
    public:
        virtual ~ICapacity()                            { }
        // the actor's limit:  lines it may hold at once (manufacturers' capacity, set when the world is built)
        virtual void     SetSlots(uint32_t actorID, uint32_t slots) = 0;
        virtual uint32_t Slots(uint32_t actorID) const = 0;
        // jobs the actor may still start now at the node
        virtual uint32_t Free(uint32_t actorID, uint32_t nodeID, uint8_t activity, int64_t now) const = 0;
        // takes a line for the job (sets job.lineID, may stretch job.end).  false:  nothing free
        virtual bool     Reserve(SimJob& job, int64_t now) = 0;
        // a job loaded after a restart holds its line again (whatever Free says)
        virtual void     Restore(const SimJob& job) = 0;
        // the job ended (or was dropped):  its line is free
        virtual void     Release(const SimJob& job) = 0;
        // isk the job pays the line's owner:  installation plus rent for its hours (a sink)
        virtual double   JobCost(const SimJob& job) const = 0;
    };

    // PLAN 6.6 mode UseRealAssemblyLines = false:  per actor slot counters, nothing written
    class VirtualCapacity : public ICapacity {
    public:
        VirtualCapacity(double installCost, double costPerHour)
        : m_install(installCost), m_perHour(costPerHour) { }

        void     SetSlots(uint32_t actorID, uint32_t slots) override       { m_slots[actorID] = slots; }
        uint32_t Slots(uint32_t actorID) const override;
        uint32_t Free(uint32_t actorID, uint32_t nodeID, uint8_t activity, int64_t now) const override;
        bool     Reserve(SimJob& job, int64_t now) override;
        void     Restore(const SimJob& job) override                        { ++m_used[job.actorID]; }
        void     Release(const SimJob& job) override;
        double   JobCost(const SimJob& job) const override;

    private:
        double m_install, m_perHour;
        std::map<uint32_t, uint32_t> m_slots, m_used;
    };

    /* the ore mix:  x[o] >= 0 (m3 a day of ore o) so that sum_o y[o][m] x[o] meets need[m] as closely as it can, in
     *  relative terms (least squares of (supply - need) / need over the minerals with a need).  multiplicative updates
     *  from x0 (> 0 where an ore may be mined), deterministic.  y[o][m]:  units of mineral m per m3 of ore o */
    std::vector<double> FitMix(const std::vector<std::vector<double>>& y, const std::vector<double>& need,
                               std::vector<double> x0, int iterations = 400);

}
}

#endif  // _EVE_SERVER_MARKET_SIM_PRODUCTION_H__
