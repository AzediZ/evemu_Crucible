 /**
  * @name FacilitiesTest.cpp
  *   unit tests of the sim's facility capacity (market/sim/SimFacilities, SimLineCapacity, PLAN 6.6):  the policy, the
  *   player guarantees and the book on the harness' lines, and the world's view of it (Prod::ICapacity)
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "HarnessFacilities.h"
#include "TestCheck.h"
#include "market/sim/SimFacilities.h"
#include "market/sim/SimLineCapacity.h"

using namespace MktSim;

namespace {
    const int64_t T0 = 1323043200;      // 2011-12-05 00:00 UTC
    const uint32_t Jita = 60003760;

    JobRequest Request(uint32_t actorID, uint32_t stationID, double hours, uint32_t runs = 1)
    {
        JobRequest req;
        req.actorID = actorID;
        req.activity = LineActivity::Manufacturing;
        req.stationID = stationID;
        req.outputTypeID = 587;     // Rifter
        req.runs = runs;
        req.unitsPerRun = 1;
        req.secondsPerRun = hours * 3600.0 / runs;
        return req;
    }

    // every pending line write of the book, by line
    std::map<uint32_t, FacilityBook::LineWrite> Writes(const FacilityBook& book)
    {
        std::vector<FacilityBook::LineWrite> list;
        book.PendingLineWrites(list);
        std::map<uint32_t, FacilityBook::LineWrite> out;
        for (auto& w : list)
            out[w.lineID] = w;
        return out;
    }

    // what a source line holds after a write (FactoryDB::SetNextFreeTimes)
    int64_t Apply(int64_t current, const FacilityBook::LineWrite& w)
    {
        if (w.hasGuard and (current == w.guard))
            return w.value;
        return std::max(current, w.floor);
    }

    // the harness' lines with a line check:  lines in 'player' have a player's job, lines in 'gone' don't exist
    class CheckedLines : public HarnessFacilities {
    public:
        explicit CheckedLines(const FacilityParams& p) : HarnessFacilities(p) { }
        std::set<uint32_t> player, gone;
    protected:
        bool CheckLines(const std::vector<uint32_t>& lineIDs, std::map<uint32_t, std::pair<int64_t, bool>>& into) override {
            for (auto id : lineIDs)
                if (gone.count(id) == 0)
                    into[id] = std::make_pair((int64_t)(T0 + 777), player.count(id) > 0);
            return true;
        }
    };
}

int FacilitiesTest()
{
    /* the policy */
    T_CHECK_EQ(Capacity::ShareCap(50, 0.3), 15);
    T_CHECK_EQ(Capacity::ShareCap(10, 0.3), 3);
    T_CHECK_EQ(Capacity::ShareCap(3, 0.3), 0);
    T_CHECK_EQ(Capacity::ShareCap(20, 1.5), 20);
    T_CHECK_EQ(Capacity::ShareCap(20, 0.0), 0);
    // the share and the free lines both limit;  the free lines a player needs are never taken
    T_CHECK_EQ(Capacity::Allowed(50, 50, 0, 0.3, 2), 15);
    T_CHECK_EQ(Capacity::Allowed(50, 50, 15, 0.3, 2), 0);
    T_CHECK_EQ(Capacity::Allowed(50, 5, 0, 0.3, 2), 3);
    T_CHECK_EQ(Capacity::Allowed(50, 2, 0, 0.3, 2), 0);
    T_CHECK_EQ(Capacity::Allowed(50, 1, 0, 0.3, 2), 0);
    T_CHECK_EQ(Capacity::Allowed(10, 10, 2, 0.3, 2), 1);
    T_CHECK_EQ(Capacity::Allowed(0, 0, 0, 0.3, 0), 0);
    T_CHECK(Capacity::CanMove(3, 2));
    T_CHECK(!Capacity::CanMove(2, 2));
    // jobs are cut at the longest job, at least one run
    T_CHECK_EQ(Capacity::RunsPerJob(100, 3600.0, 24.0), 24);
    T_CHECK_EQ(Capacity::RunsPerJob(10, 3600.0, 24.0), 10);
    T_CHECK_EQ(Capacity::RunsPerJob(5, 40.0 * 3600.0, 24.0), 1);
    T_CHECK_EQ(Capacity::RunsPerJob(0, 3600.0, 24.0), 0);
    T_CHECK_EQ(Capacity::JobSeconds(3, 1000.5, 1.0), 3002);
    T_CHECK_EQ(Capacity::JobSeconds(1, 0.0, 1.0), 1);
    // install plus every started hour (RamMethods::Calculate)
    T_CHECK_EQ(Capacity::JobCost(1000.0, 333.0, 3600), 133300);
    T_CHECK_EQ(Capacity::JobCost(1000.0, 333.0, 3601), 166600);
    T_CHECK_EQ(Capacity::JobCost(0.0, 0.0, 7200), 0);
    T_CHECK(Capacity::Refill(0.0, 2e6, 1800) == 1e6);
    T_CHECK(Capacity::Refill(1.5e6, 2e6, 3600) == 2e6);

    FacilityParams p;   // the plan's defaults:  real lines, 0.3, 2 free, 24 hours, 2e6 m3/h

    /* a hub with 50 manufacturing lines:  the sim takes 15, on 15 different lines, then is refused */
    {
        HarnessFacilities fac(p);
        T_CHECK_EQ(fac.Available(Jita, LineActivity::Manufacturing, T0), 15);
        std::set<uint32_t> lines;
        uint32_t made(0);
        for (int i = 0; i < 40; ++i) {
            FacilityJob job(fac.Reserve(Request(7, Jita, 3.0), T0));
            if (job.jobID == 0)
                break;
            ++made;
            T_CHECK(job.lineID != 0);
            lines.insert(job.lineID);
            T_CHECK_EQ(job.end - job.start, 3 * 3600);
            T_CHECK_EQ(job.cost, 100000 + 3 * 33300);
            T_CHECK_EQ(job.payeeID, 1000002);
        }
        T_CHECK_EQ(made, 15);
        T_CHECK_EQ(lines.size(), 15);
        FacilityBook::StationUse use;
        T_CHECK(fac.Use(Jita, LineActivity::Manufacturing, T0, use));
        T_CHECK_EQ(use.lines, 50);
        T_CHECK_EQ(use.free, 35);
        T_CHECK_EQ(use.simBusy, 15);
        T_CHECK_EQ(use.simOnLines, 15);
        T_CHECK_EQ(use.allowed, 0);
        // other activities and stations are separate;  a station without lines takes nothing
        T_CHECK_EQ(fac.Available(Jita, LineActivity::Invention, T0), 6);
        fac.SetStationLines(60004588, LineActivity::Manufacturing, 0);  // Rens
        T_CHECK_EQ(fac.Available(60004588, LineActivity::Manufacturing, T0), 0);
        T_CHECK_EQ(fac.Reserve(Request(7, 60004588, 1.0), T0).jobID, 0);
        fac.SetStationLines(60000001, LineActivity::Manufacturing, 3);
        T_CHECK_EQ(fac.Reserve(Request(7, 60000001, 1.0), T0).jobID, 0);
    }

    /* players busy on most lines:  the sim stops where MinFreeLinesPerStation lines are still free */
    {
        HarnessFacilities fac(p);
        fac.SetPlayerBusyShare(0.76);   // 38 of 50 lines
        uint32_t made(0);
        while (fac.Reserve(Request(7, Jita, 2.0), T0).jobID != 0)
            ++made;
        T_CHECK_EQ(made, 10);
        FacilityBook::StationUse use;
        fac.Use(Jita, LineActivity::Manufacturing, T0, use);
        T_CHECK_EQ(use.free, 2);
        T_CHECK_EQ(fac.Available(Jita, LineActivity::Manufacturing, T0 + 3600 + 1), 0);   // the players are busy again the next hour
        // the players' lines free up:  the sim may take more, up to its share
        fac.SetPlayerBusyShare(0.0);
        fac.InvalidateLines();
        T_CHECK_EQ(fac.Available(Jita, LineActivity::Manufacturing, T0 + 3600 + 1), 5);
    }

    /* a player picks a line the sim holds:  quoted as before the sim, the sim's job moves on */
    {
        HarnessFacilities fac(p);
        fac.SetStationLines(Jita, LineActivity::Manufacturing, 10);     // the sim may hold 3
        FacilityJob a(fac.Reserve(Request(7, Jita, 5.0), T0));
        FacilityJob b(fac.Reserve(Request(7, Jita, 5.0), T0));
        T_CHECK((a.jobID != 0) and (b.jobID != 0));
        T_CHECK_EQ(fac.HeldBy(a.lineID), a.jobID);
        // the source shows the sim's time (after a flush):  the player's quote is the old one, so the job starts at once
        T_CHECK_EQ(fac.PlayerView(a.lineID, a.lineSet), a.linePrev);
        T_CHECK(a.linePrev <= T0);
        T_CHECK_EQ(fac.PlayerView(a.lineID + 100, 12345), 12345);     // a line the sim doesn't hold
        uint32_t lineA(a.lineID);
        T_CHECK(fac.PlayerTakesLine(lineA, T0 + 7200, T0 + 7200, T0 + 60));
        const FacilityJob* moved(fac.FindJob(a.jobID));
        T_CHECK(moved != nullptr);
        T_CHECK((moved->lineID != 0) and (moved->lineID != lineA) and (moved->lineID != b.lineID));
        T_CHECK_EQ(moved->end, a.end);      // the job's time is not changed
        T_CHECK_EQ(fac.HeldBy(lineA), 0);
        T_CHECK_EQ(fac.PlayerView(lineA, T0 + 7200), T0 + 7200);
        FacilityBook::StationUse use;
        fac.Use(Jita, LineActivity::Manufacturing, T0 + 60, use);
        T_CHECK_EQ(use.simBusy, 2);
        T_CHECK_EQ(use.free, 10 - 1 - 2);
        /* players take every free line but two:  then a job the player takes from the sim has nowhere to go and runs on
         *  without a line (the sim never leaves less than MinFreeLinesPerStation free for players) */
        uint32_t taken(0);
        for (uint32_t id = 1; id < 3000; ++id) {
            if ((fac.HeldBy(id) != 0) or (id == lineA))
                continue;
            fac.Use(Jita, LineActivity::Manufacturing, T0 + 60, use);
            if (use.free <= 2)
                break;
            fac.PlayerTakesLine(id, T0 + 7200, T0 + 7200, T0 + 60);
            ++taken;
        }
        fac.Use(Jita, LineActivity::Manufacturing, T0 + 60, use);
        T_CHECK_EQ(use.free, 2);
        T_CHECK(taken > 0);
        T_CHECK(fac.PlayerTakesLine(b.lineID, T0 + 7200, T0 + 7200, T0 + 60));
        T_CHECK_EQ(fac.FindJob(b.jobID)->lineID, 0);
        fac.Use(Jita, LineActivity::Manufacturing, T0 + 60, use);
        T_CHECK_EQ(use.free, 2);
        T_CHECK_EQ(use.simBusy, 2);
        T_CHECK_EQ(use.simOnLines, 1);
        T_CHECK_EQ(fac.Available(Jita, LineActivity::Manufacturing, T0 + 60), 0);
        // a job without a line still counts until it ends
        T_CHECK(fac.Release(b.jobID, b.end));
        T_CHECK_EQ(fac.Jobs(), 1);
    }

    /* long runs are split;  a job that ran its time needs no write;  a cancelled one puts its line back now */
    {
        HarnessFacilities fac(p);
        FacilityJob big(fac.Reserve(Request(9, Jita, 100.0, 100), T0));
        T_CHECK_EQ(big.runs, 24);
        T_CHECK_EQ(big.qtyOut, 24);
        T_CHECK_EQ(big.end, T0 + 24 * 3600);
        FacilityJob small(fac.Reserve(Request(9, Jita, 1.0, 2), T0));
        T_CHECK_EQ(small.runs, 2);
        FacilityJob test(fac.Reserve(Request(0, Jita, 0.5), T0));
        T_CHECK(test.jobID != 0);
        T_CHECK_EQ(fac.EndTestJobs(T0 + 1799), 0);
        T_CHECK_EQ(fac.EndTestJobs(T0 + 1800), 1);
        T_CHECK_EQ(fac.FindJobFor(9, Jita, LineActivity::Manufacturing, T0, T0 + 3600), small.jobID);
        T_CHECK(fac.Release(small.jobID, T0 + 3600));
        T_CHECK_EQ(fac.Jobs(), 1);
        T_CHECK_EQ(fac.HeldBy(small.lineID), 0);
        std::map<uint32_t, FacilityBook::LineWrite> w(Writes(fac));
        T_CHECK(w.find(small.lineID) == w.end());
        T_CHECK(w.find(test.lineID) == w.end());
        T_CHECK(w.find(big.lineID) != w.end());
        T_CHECK(fac.Release(big.jobID, T0 + 7200));
        T_CHECK(!fac.Release(big.jobID, T0 + 7200));
        T_CHECK_EQ(fac.Jobs(), 0);
        FacilityBook::StationUse use;
        fac.Use(Jita, LineActivity::Manufacturing, T0 + 7200, use);
        T_CHECK_EQ(use.free, 50);
    }

    /* line writes:  the sim's time goes on with GREATEST (never moves a player's back), and is undone only where it is
     *  still the sim's */
    {
        HarnessFacilities fac(p);
        FacilityJob a(fac.Reserve(Request(7, Jita, 4.0), T0));
        FacilityJob b(fac.Reserve(Request(7, Jita, 4.0), T0));
        std::map<uint32_t, FacilityBook::LineWrite> w(Writes(fac));
        T_CHECK_EQ(w.size(), 2);
        T_CHECK(!w[a.lineID].hasGuard);
        T_CHECK_EQ(w[a.lineID].floor, a.lineSet);
        T_CHECK_EQ(Apply(0, w[a.lineID]), a.lineSet);
        T_CHECK_EQ(Apply(T0 + 99999, w[a.lineID]), T0 + 99999);    // a later time stays
        fac.PendingWritten();
        T_CHECK(!fac.HasPending());
        // a cancelled job's line goes back to its time before the sim, unless a player's time is there
        fac.Release(a.jobID, T0 + 600);
        w = Writes(fac);
        T_CHECK_EQ(w.size(), 1);
        T_CHECK(w[a.lineID].hasGuard);
        T_CHECK_EQ(Apply(a.lineSet, w[a.lineID]), a.linePrev);
        T_CHECK_EQ(Apply(T0 + 5000, w[a.lineID]), T0 + 5000);
        // the sim takes it again before the flush:  the source still has the old time, which the new one replaces
        FacilityJob c(fac.Reserve(Request(7, Jita, 1.0), T0 + 600));
        T_CHECK_EQ(c.lineID, a.lineID);
        T_CHECK_EQ(c.linePrev, a.linePrev);
        w = Writes(fac);
        T_CHECK(w[a.lineID].hasGuard);
        T_CHECK_EQ(Apply(a.lineSet, w[a.lineID]), c.lineSet);
        // before the flush the player is quoted the time before the sim, whatever the source holds
        T_CHECK_EQ(fac.PlayerView(a.lineID, a.lineSet), a.linePrev);
        fac.PendingWritten();
        // released and flushed:  a player is quoted the old time while the source still has the sim's
        fac.Release(c.jobID, T0 + 700);
        T_CHECK_EQ(fac.PlayerView(c.lineID, c.lineSet), c.linePrev);
        // a player takes b's line after the flush:  the undo keeps the player's time
        fac.PlayerTakesLine(b.lineID, T0 + 50000, T0 + 50000, T0 + 700);
        w = Writes(fac);
        T_CHECK(w.find(b.lineID) != w.end());
        T_CHECK_EQ(Apply(T0 + 50000, w[b.lineID]), T0 + 50000);
        const FacilityJob* bNow(fac.FindJob(b.jobID));
        T_CHECK((bNow != nullptr) and (bNow->lineID != 0) and (bNow->lineID != b.lineID));
    }

    /* a restart:  jobs loaded into a new book hold their lines (checked against the lines' state) */
    {
        HarnessFacilities fac(p);
        std::vector<FacilityJob> saved;
        for (int i = 0; i < 5; ++i)
            saved.push_back(fac.Reserve(Request(11 + i, Jita, 1.0 + i), T0));
        CheckedLines again(p);
        again.player.insert(saved[1].lineID);       // a player has it now
        again.gone.insert(saved[2].lineID);
        for (auto& j : saved) {
            FacilityJob loaded(j);
            loaded.linePrev = 0;
            loaded.lineSet = 0;
            again.LoadJob(loaded, false);
        }
        T_CHECK_EQ(again.Jobs(), 5);
        // before the check the jobs hold their lines (a player is quoted them free)
        T_CHECK(again.HeldBy(saved[1].lineID) != 0);
        T_CHECK_EQ(again.PlayerView(saved[0].lineID, T0 + 777), 0);
        T_CHECK_EQ(again.VerifyRestored(), 2);
        T_CHECK_EQ(again.VerifyRestored(), 0);
        T_CHECK_EQ(again.HeldBy(saved[1].lineID), 0);
        T_CHECK_EQ(again.HeldBy(saved[2].lineID), 0);
        T_CHECK(again.HeldBy(saved[0].lineID) != 0);
        T_CHECK(!again.HasPending());
        FacilityBook::StationUse use;
        again.Use(Jita, LineActivity::Manufacturing, T0 + 60, use);
        T_CHECK_EQ(use.simBusy, 5);
        T_CHECK_EQ(use.simOnLines, 3);
        T_CHECK_EQ(use.free, 47);
        T_CHECK_EQ(again.Available(Jita, LineActivity::Manufacturing, T0 + 60), 10);
        // after the check the source's time is the sim's:  a cancelled job undoes it
        uint32_t id0(again.FindJobFor(11, Jita, LineActivity::Manufacturing, saved[0].start, saved[0].end));
        T_CHECK(id0 != 0);
        again.Release(id0, T0 + 60);
        std::map<uint32_t, FacilityBook::LineWrite> w(Writes(again));
        T_CHECK((w.size() == 1) and w[saved[0].lineID].hasGuard);
        T_CHECK_EQ(Apply(T0 + 777, w[saved[0].lineID]), 0);
    }

    /* counters only (UseRealAssemblyLines false):  the same limits, no lines, no line writes */
    {
        FacilityParams v(p);
        v.realLines = false;
        HarnessFacilities fac(v);
        fac.SetPlayerBusyShare(0.9);    // not seen:  counters only
        uint32_t made(0);
        for (;;) {
            FacilityJob job(fac.Reserve(Request(7, Jita, 2.0), T0));
            if (job.jobID == 0)
                break;
            T_CHECK_EQ(job.lineID, 0);
            ++made;
        }
        T_CHECK_EQ(made, 15);
        T_CHECK(Writes(fac).empty());
    }

    /* refinery soft cap:  2e6 m3 an hour per station, an hour's worth at most */
    {
        HarnessFacilities fac(p);
        T_CHECK(fac.RefineAllowance(Jita, 3e6, T0) == 2e6);
        T_CHECK(fac.RefineAllowance(Jita, 1e6, T0) == 0.0);
        T_CHECK(fac.RefineAllowance(60008494, 1e6, T0) == 1e6);
        T_CHECK(fac.RefineAllowance(Jita, 3e6, T0 + 1800) == 1e6);
        T_CHECK(fac.RefineAllowance(Jita, 5e6, T0 + 1800 + 7200) == 2e6);
        FacilityParams nolimit(p);
        nolimit.refineryM3PerHour = 0.0;
        HarnessFacilities unlimited(nolimit);
        T_CHECK(unlimited.RefineAllowance(Jita, 9e9, T0) == 9e9);
    }

    /* the world's view (Prod::ICapacity):  the actor's slots and the station's share both limit;  jobs are found again
     *  by actor, station, start and end;  the line's cost is charged */
    {
        HarnessFacilities fac(p);
        LineCapacity cap(fac, 1000.0, 333.0);
        cap.SetNodeStation(1, Jita);
        cap.SetSlots(21, 10);
        cap.SetSlots(22, 10);
        T_CHECK_EQ(cap.Slots(21), 10);
        T_CHECK_EQ(cap.Free(21, 1, LineActivity::Manufacturing, T0), 10);
        T_CHECK_EQ(cap.Free(21, 2, LineActivity::Manufacturing, T0), 0);   // a node without a station
        T_CHECK_EQ(cap.Free(23, 1, LineActivity::Manufacturing, T0), 0);   // no slots
        std::vector<SimJob> jobs;
        for (int i = 0; i < 20; ++i) {
            SimJob job;
            job.actorID = (i < 10) ? 21 : 22;
            job.nodeID = 1;
            job.activity = LineActivity::Manufacturing;
            job.typeID = 587;
            job.runs = 2;
            job.qtyOut = 2;
            job.start = T0;
            job.end = T0 + 3600 * (1 + i % 3);
            if (!cap.Reserve(job, T0))
                continue;
            T_CHECK(job.lineID != 0);
            jobs.push_back(job);
        }
        T_CHECK_EQ(jobs.size(), 15);                // the station's share, not the actors' 20 slots
        T_CHECK_EQ(cap.Free(21, 1, LineActivity::Manufacturing, T0), 0);
        // the planner's probe (not booked) and a booked job cost the same here:  install + rent of started hours
        SimJob probe;
        probe.start = T0;
        probe.end = T0 + 5400;
        T_CHECK(cap.JobCost(probe) == 1000.0 + 2 * 333.0);
        T_CHECK(cap.JobCost(jobs[0]) == 1000.0 + 333.0);
        // a player moves a job off its line:  the world finds it, and learns its line for mktSimJob
        uint32_t oldLine(jobs[0].lineID);
        fac.PlayerTakesLine(oldLine, T0 + 9000, T0 + 9000, T0 + 10);
        uint32_t newLine(cap.CurrentLine(jobs[0]));
        T_CHECK(newLine != oldLine);
        // the world ends a job:  its slot and its share are free again
        cap.Free(21, 1, LineActivity::Manufacturing, jobs[0].end);    // the world's time moves on
        cap.Release(jobs[0]);
        T_CHECK_EQ(fac.Jobs(), 14);
        T_CHECK_EQ(cap.Free(21, 1, LineActivity::Manufacturing, jobs[0].end), 1);
        // the world drops a job it couldn't pay for at once:  nothing of it reaches the lines
        SimJob extra(jobs[0]);
        extra.start = jobs[0].end;
        extra.end = extra.start + 3600;
        T_CHECK(cap.Reserve(extra, extra.start));
        cap.Release(extra);
        T_CHECK_EQ(fac.Jobs(), 14);
        // a restart:  the world's jobs come back on their lines
        HarnessFacilities fac2(p);
        LineCapacity cap2(fac2, 1000.0, 333.0);
        cap2.SetNodeStation(1, Jita);
        cap2.SetSlots(21, 10);
        cap2.SetSlots(22, 10);
        for (size_t i = 1; i < jobs.size(); ++i)
            cap2.Restore(jobs[i]);
        T_CHECK_EQ(fac2.Jobs(), 14);
        T_CHECK_EQ(cap2.Free(21, 1, LineActivity::Manufacturing, T0 + 60), 1);
        T_CHECK_EQ(cap2.CurrentLine(jobs[3]), jobs[3].lineID);
        cap2.Release(jobs[3]);
        T_CHECK_EQ(fac2.Jobs(), 13);
    }

    return MktTest::failures;
}
