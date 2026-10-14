 /**
  * @name SimFacilities.h
  *   market simulation:  facility capacity (doc/code_and_design_notes/planning/market-sim/PLAN.md 6.6, phase 5).  the sim's
  *   manufacturing, research, copying and invention jobs take npc station assembly lines, and its refining is metered per
  *   station, while players can always still use the stations:
  *     - per station and activity the sim holds at most floor(MaxLineShare x lines) jobs, and leaves at least
  *       MinFreeLinesPerStation lines free.  it only takes lines that are free now (it never queues behind a line)
  *     - a player who picks a line the sim holds gets it at once:  the sim's job moves to another free line of the
  *       station, or keeps running without one (the player never waits for, or is refused because of, the sim)
  *
  *   FacilityBook keeps which lines the sim's jobs hold, on lines from a source:  the server's (SimFacilityMgr:
  *   ramAssemblyLines) or the harness' (HarnessFacilities:  synthetic lines in memory).  the jobs themselves (their
  *   outputs, mktSimJob) are the world's (SimWorld);  LineCapacity (SimLineCapacity.h) is the world's view of the book.
  *   Capacity holds the policy as pure functions.  nothing here depends on the server
  *
  *   times are the sim's (unix seconds);  a line's 'raw' next free time is in its source's unit (the server's is FILETIME)
  *
  * @date:          15Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_FACILITIES_H__
#define _EVE_SERVER_MARKET_SIM_FACILITIES_H__

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "market/sim/SimTypes.h"

namespace MktSim {

    // the R.A.M. activities (ramActivities) of station lines
    namespace LineActivity {
        enum : uint8_t {
            Manufacturing       = 1,
            ResearchTime        = 3,
            ResearchMaterial    = 4,
            Copying             = 5,
            Invention           = 8
        };
    }

    // <marketSim> UseRealAssemblyLines, MaxLineShare, MinFreeLinesPerStation, MaxSimJobHours, RefineryM3PerHour (PLAN 9.6)
    struct FacilityParams {
        bool     realLines          = true;     // jobs take real lines (players see them busy);  false:  counters only
        double   maxLineShare       = 0.3;      // of a station's lines of an activity, at most this share is the sim's
        uint32_t minFreeLines       = 2;        // the sim leaves at least this many lines of an activity free
        double   maxJobHours        = 24.0;     // longest job of a JobRequest by runs (a single run may be longer)
        double   refineryM3PerHour  = 2e6;      // refining per station (soft:  what is over waits).  <= 0:  no limit
        int64_t  lineRefreshSeconds = 300;      // a station's lines are re-read this often (players' jobs come by the hook)
    };

    // a line the sim may use (npc owned, no restriction that keeps an npc corp out)
    struct FacilityLine {
        uint32_t lineID         = 0;
        uint32_t ownerID        = 0;    // who the rent goes to (the station's owner corp)
        int64_t  freeAt         = 0;    // sim time the line is free from (<= now:  free now)
        int64_t  raw            = 0;    // the line's next free time as its source keeps it
        double   timeMultiplier = 1.0;  // the line type's baseTimeMultiplier
        double   costInstall    = 0.0;  // isk
        double   costPerHour    = 0.0;  // isk, at least the line type's minimum
    };

    // a job to book
    struct JobRequest {
        uint32_t actorID        = 0;    // 0:  a GM test job
        uint8_t  activity       = LineActivity::Manufacturing;
        uint32_t stationID      = 0;
        uint16_t outputTypeID   = 0;
        uint32_t runs           = 1;
        Units    unitsPerRun    = 1;    // output per run (portion size)
        double   secondsPerRun  = 0.0;  // on a line with time multiplier 1;  runs are cut to MaxSimJobHours
        int64_t  seconds        = 0;    // > 0:  the whole job's length on a line with time multiplier 1 (no cut)
    };

    // a job holding capacity
    struct FacilityJob {
        uint32_t jobID          = 0;    // the book's (0:  none, a refused Reserve)
        uint32_t actorID        = 0;    // 0:  a GM test job
        uint8_t  activity       = 0;
        uint32_t stationID      = 0;
        uint32_t lineID         = 0;    // 0:  no line (counters only, or the line went to a player)
        uint16_t outputTypeID   = 0;
        uint32_t runs           = 0;
        Units    qtyOut         = 0;
        int64_t  start          = 0;
        int64_t  end            = 0;
        Cents    cost           = 0;    // install and rent (per started hour), due to payeeID:  a sink
        uint32_t payeeID        = 0;
        int64_t  linePrev       = 0;    // the line's raw next free time before the sim took it (0 after a restart:  free)
        int64_t  lineSet        = 0;    // the raw next free time the sim gave the line
    };

    // the policy (pure, unit tested)
    namespace Capacity {
        // the most jobs the sim keeps at a station:  floor(maxShare x lines)
        uint32_t ShareCap(uint32_t lines, double maxShare);
        /* how many more jobs the sim may start at a station now.  lines:  the station's lines of the activity the sim may
         *  use;  free:  of those, free now and not the sim's;  simBusy:  the sim's jobs there (on lines or not) */
        uint32_t Allowed(uint32_t lines, uint32_t free, uint32_t simBusy, double maxShare, uint32_t minFree);
        // a player took a line the sim held:  the sim's job may move to another free line (free:  the others free now)
        bool     CanMove(uint32_t free, uint32_t minFree);
        // runs of one job:  at most maxHours long, at least one run, at most runs
        uint32_t RunsPerJob(uint32_t runs, double secondsPerRun, double maxHours);
        // seconds of a job (whole seconds, rounded up)
        int64_t  JobSeconds(uint32_t runs, double secondsPerRun, double timeMultiplier);
        // install cost plus the rent of every started hour (as RamMethods::Calculate charges a player), in cents
        Cents    JobCost(double costInstall, double costPerHour, int64_t seconds);
        // the refinery allowance (m3) after dt seconds:  refills at m3PerHour, up to an hour's worth
        double   Refill(double level, double m3PerHour, int64_t dt);
    }

    class FacilityBook {
    public:
        FacilityBook();
        virtual ~FacilityBook()                         { }

        const FacilityParams& Params() const            { return m_p; }
        void SetParams(const FacilityParams& p)         { m_p = p; }

        // how many more jobs of the activity the station takes now (0:  none, or the station has no such lines)
        uint32_t Available(uint32_t stationID, uint8_t activity, int64_t now);
        /* books a job starting now (a line when lines are real).  comes back with its book jobID (0:  refused, no
         *  capacity), runs, qtyOut, start, end (the line's time multiplier applied), cost, payee and line */
        FacilityJob Reserve(const JobRequest& req, int64_t now);
        // the job ends:  its line is free.  before its end (cancelled) the line's time is put back.  false:  no such job
        bool Release(uint32_t jobID, int64_t now);
        // refinery soft cap:  how much of m3 the station refines now.  it is taken from the station's allowance;  the
        //  caller keeps the rest for later
        double RefineAllowance(uint32_t stationID, double m3, int64_t now);
        const FacilityJob* FindJob(uint32_t jobID) const;
        /* the job of an actor at a station that started at start and ends at end (0:  none).  of several such, the one on
         *  preferLine if there is one */
        uint32_t FindJobFor(uint32_t actorID, uint32_t stationID, uint8_t activity, int64_t start, int64_t end,
                            uint32_t preferLine = 0) const;
        // the jobs of an actor (0:  all)
        void JobsOf(uint32_t actorID, std::vector<FacilityJob>& into) const;
        size_t Jobs() const                             { return m_jobs.size(); }
        // ends the GM test jobs (actor 0) that ran their time
        uint32_t EndTestJobs(int64_t now);

        /* players.  a player installs a job on lineID (busy until playerFreeAt, raw playerRaw):  a sim job on it moves to
         *  another free line of the station, or keeps running without a line.  true:  the sim had it */
        bool PlayerTakesLine(uint32_t lineID, int64_t playerFreeAt, int64_t playerRaw, int64_t now);
        /* the next free time a player is quoted for a line (raw:  what the line's source holds):  as it was before the sim
         *  took it, so the sim never delays a player's job or makes a quote fail (RamCannotGuaranteeStartTime) */
        int64_t PlayerView(uint32_t lineID, int64_t raw) const;
        // the job holding a line (0:  none)
        uint32_t HeldBy(uint32_t lineID) const;

        // a station's numbers (GM, tests).  false:  the station has no lines of the activity the sim may use
        struct StationUse {
            uint32_t lines      = 0;
            uint32_t free       = 0;    // free now and not the sim's
            uint32_t simBusy    = 0;    // the sim's jobs (on lines or not)
            uint32_t simOnLines = 0;
            uint32_t cap        = 0;    // ShareCap
            uint32_t allowed    = 0;    // Available
        };
        bool Use(uint32_t stationID, uint8_t activity, int64_t now, StationUse& into);
        // drops cached lines (0:  every station), so they are read again
        void InvalidateLines(uint32_t stationID = 0);

        /* persistence:  the lines' next free times the source must get (the jobs are the world's).  a write is
         *      nextFreeTime = IF(nextFreeTime = guard, value, GREATEST(nextFreeTime, floor))   (no guard:  GREATEST only)
         *  so the sim never moves a player's time back, and only undoes its own */
        struct LineWrite {
            uint32_t lineID;
            bool     hasGuard;
            int64_t  guard;
            int64_t  value;
            int64_t  floor;
        };
        void PendingLineWrites(std::vector<LineWrite>& into) const;
        bool HasPending() const                         { return !m_touched.empty(); }
        // the writes above are saved
        void PendingWritten();

        /* restarts.  a job the world loaded holds its line again.  checked:  the line holds job.lineSet;  else the line is
         *  checked with the next VerifyRestored() (a player may have it now:  the job goes on without it) */
        uint32_t LoadJob(const FacilityJob& job, bool checked);
        // checks the lines of the jobs loaded unchecked (CheckLines).  returns how many lost their line
        uint32_t VerifyRestored();

    protected:
        // the lines of the activity at the station the sim may use, with their state now.  false:  none
        virtual bool LoadLines(uint32_t stationID, uint8_t activity, int64_t now, std::vector<FacilityLine>& into) = 0;
        // the raw next free time a line gets for a sim job that ends at end
        virtual int64_t RawFor(int64_t end, int64_t now) const   { (void)now; return end; }
        /* lines as they are:  raw next free time, and whether a player has a job on it.  lines gone are left out.  false:
         *  can't tell now (VerifyRestored tries again) */
        virtual bool CheckLines(const std::vector<uint32_t>& lineIDs, std::map<uint32_t, std::pair<int64_t, bool>>& into)
        { (void)lineIDs; (void)into; return false; }

        FacilityParams m_p;

    private:
        typedef std::pair<uint32_t, uint8_t> StationKey;    // stationID, activity
        typedef std::tuple<uint32_t, uint32_t, uint8_t, int64_t, int64_t> JobKey;  // actor, station, activity, start, end
        struct StationLines {
            int64_t loaded = 0;
            bool    any = false;
            std::vector<FacilityLine> lines;
        };
        // what the sim wrote to a line, while it is the sim's value in the source
        struct LineMark {
            bool    inSource = false;   // 'flushed' is in the source
            int64_t flushed = 0;
            int64_t prev = 0;           // the line's raw value before the sim took it
        };

        static JobKey KeyOf(const FacilityJob& j)       { return JobKey(j.actorID, j.stationID, j.activity, j.start, j.end); }
        StationLines* Lines(uint32_t stationID, uint8_t activity, int64_t now);
        FacilityLine* FindLine(uint32_t lineID);
        bool     IsFree(const FacilityLine& line, int64_t now) const;
        uint32_t CountFree(const StationLines& s, int64_t now) const;
        uint32_t SimBusy(uint32_t stationID, uint8_t activity) const;
        uint32_t AllowedAt(const StationLines& s, uint32_t stationID, uint8_t activity, int64_t now) const;
        FacilityLine* PickLine(StationLines& s, int64_t now);
        void     Take(FacilityJob& job, FacilityLine& line, int64_t now);
        void     Leave(FacilityJob& job);
        void     Remove(std::map<uint32_t, FacilityJob>::iterator itr);
        // a job that ran its time:  off its line (no write unless needed) and out of the book
        void     EndJob(std::map<uint32_t, FacilityJob>::iterator itr);

        std::map<uint32_t, FacilityJob> m_jobs;
        std::multimap<JobKey, uint32_t> m_byKey;
        std::map<StationKey, StationLines> m_stations;
        std::unordered_map<uint32_t, StationKey> m_lineStation; // lineID -> its station (lines read so far)
        std::unordered_map<uint32_t, uint32_t> m_held;          // lineID -> jobID
        std::map<StationKey, uint32_t> m_busy;                  // the sim's jobs per station and activity
        std::unordered_map<uint32_t, LineMark> m_marks;         // lineID ->
        std::map<uint32_t, std::pair<double, int64_t>> m_refine; // stationID -> (allowance m3, time)
        std::set<uint32_t> m_touched;                           // lines to write
        std::vector<uint32_t> m_unchecked;                      // jobs loaded with lines not checked yet
        uint32_t m_nextJobID;
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_FACILITIES_H__
