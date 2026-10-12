 /**
  * @name SimLineCapacity.h
  *   market simulation:  the world's capacity (Prod::ICapacity, phase 3) on station lines (FacilityBook, PLAN 6.6).
  *   it replaces VirtualCapacity with World::SetCapacity before Build():  a manufacturer may start a job while it has a
  *   slot of its own left and one of its node's stations takes one more (MaxLineShare, MinFreeLinesPerStation);  the
  *   job holds a real line (or a station counter in mode UseRealAssemblyLines false) and pays the line's install and
  *   rent.  a node's stations are its industrial site until phase 4 has production sites:  the job runs at the one with
  *   the most room (job.stationID), its output still comes to the node
  *   the world keeps the jobs (mktSimJob) and hands their output over;  the book finds a job again by its actor,
  *   station, activity, start and end (the world gives it its jobID after Reserve, and a player may move it to
  *   another line meanwhile:  CurrentLine)
  *
  * @date:          15Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_LINE_CAPACITY_H__
#define _EVE_SERVER_MARKET_SIM_LINE_CAPACITY_H__

#include <map>
#include <vector>

// phase 3's interface;  before phase 3 is merged, a copy of it
#if __has_include("market/sim/SimProduction.h")
#include "market/sim/SimProduction.h"
#else
#include "market/sim/SimCapacityStandIn.h"
#endif
#include "market/sim/SimFacilities.h"

namespace MktSim {

    class LineCapacity : public Prod::ICapacity {
    public:
        /* book:  the lines (the server's sSimFacilities, the harness' HarnessFacilities).  a job not booked yet (the
         *  planner's probe) is costed at probeInstall + probePerHour per started hour */
        LineCapacity(FacilityBook& book, double probeInstall, double probePerHour);

        // where a node's jobs may run (the node's own station first)
        void SetNodeStations(uint32_t nodeID, const std::vector<uint32_t>& stationIDs)  { m_nodeStations[nodeID] = stationIDs; }

        /* Prod::ICapacity */
        void     SetSlots(uint32_t actorID, uint32_t slots) override  { m_slots[actorID] = slots; }
        uint32_t Slots(uint32_t actorID) const override;
        // the actor's slots left, and what the node's stations take now
        uint32_t Free(uint32_t actorID, uint32_t nodeID, uint8_t activity, int64_t now) const override;
        /* books a line at the node's station with the most room (sets job.stationID and job.lineID;  job.end stretches with
         *  the line's time multiplier).  a job.stationID that isn't one of the node's (a production site) is kept.  false:
         *  no capacity */
        bool     Reserve(SimJob& job, int64_t now) override;
        // a job loaded after a restart holds its line again (checked with the book's VerifyRestored)
        void     Restore(const SimJob& job) override;
        // the job ended or was dropped:  its line is free
        void     Release(const SimJob& job) override;
        // isk:  the line's install and rent for the job's started hours (Isk::LineCost, to the station owner)
        double   JobCost(const SimJob& job) const override;

        // the line the job holds now (a player may have moved it), for mktSimJob.assemblyLineID.  0:  none
        uint32_t CurrentLine(const SimJob& job) const;

    private:
        const std::vector<uint32_t>* StationsOf(uint32_t nodeID) const;
        uint32_t BookJob(const SimJob& job) const;

        FacilityBook& m_book;
        double m_probeInstall;
        double m_probePerHour;
        std::map<uint32_t, uint32_t> m_slots;
        std::map<uint32_t, uint32_t> m_used;
        std::map<uint32_t, std::vector<uint32_t>> m_nodeStations;
        mutable int64_t m_now;      // the latest time the world gave (Release has none)
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_LINE_CAPACITY_H__
