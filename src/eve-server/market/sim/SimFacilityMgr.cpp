 /**
  * @name SimFacilityMgr.cpp
  *   market simulation:  the server's facility capacity (PLAN 6.6):  npc station lines and the player hooks
  *
  * @date:          15Oct26
  *
  */

#include <cstdarg>

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "manufacturing/FactoryDB.h"
#include "market/sim/MarketSimDB.h"
#include "market/sim/SimFacilityMgr.h"

using namespace MktSim;

namespace {
    std::string Fmt(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    bool Exec(const std::string& sql)
    {
        DBerror err;
        if (!sDatabase.RunQuery(err, "%s", sql.c_str())) {
            _log(MARKET__SIM, "SimFacilityMgr - query failed: %s", err.c_str());
            return false;
        }
        return true;
    }

    // a FILETIME as unix seconds, rounded up (a line busy until x.5 is not free at x)
    int64 UnixCeil(int64 fileTime)
    {
        return MarketSimDB::ToUnix(fileTime + 9999999LL);
    }

    const uint8 s_activities[] = { LineActivity::Manufacturing, LineActivity::ResearchTime, LineActivity::ResearchMaterial,
                                   LineActivity::Copying, LineActivity::Invention };
}

SimFacilityMgr::SimFacilityMgr()
: m_offset(0),
  m_lastFlush(0),
  m_loaded(false),
  m_attached(false)
{
}

void SimFacilityMgr::ReadParams()
{
    FacilityParams p;
    p.realLines = sConfig.marketSim.UseRealAssemblyLines;
    p.maxLineShare = std::min(1.0, std::max(0.0, sConfig.marketSim.MaxLineShare));
    p.minFreeLines = sConfig.marketSim.MinFreeLinesPerStation;
    p.refineryM3PerHour = sConfig.marketSim.RefineryM3PerHour;
    SetParams(p);
}

int64 SimFacilityMgr::WallNow() const
{
    return MarketSimDB::ToUnix((int64)GetFileTimeNow());
}

int64 SimFacilityMgr::SimNow() const
{
    return WallNow() + m_offset;
}

int SimFacilityMgr::Initialize()
{
    ReadParams();
    m_loaded = true;
    m_lastFlush = SimNow();
    sLog.Blue("  SimFacilityMgr", "Market sim facilities:  %s station lines, at most %.0f%% of a station's, %u kept free.",
              m_p.realLines ? "real" : "counted", m_p.maxLineShare * 100.0, m_p.minFreeLines);
    return 1;
}

MktSim::LineCapacity* SimFacilityMgr::Capacity(const std::vector<NodeData>& nodes, double probeInstall, double probePerHour)
{
    m_capacity.reset(new LineCapacity(*this, probeInstall, probePerHour));
    for (auto& node : nodes)
        m_capacity->SetNodeStation(node.nodeID, node.stationID);
    return m_capacity.get();
}

void SimFacilityMgr::Process()
{
    if (!m_loaded)
        return;
    int64 now(SimNow());
    uint32 lost(VerifyRestored());
    if (lost > 0)
        _log(MARKET__SIM, "Market sim facilities:  %u restored jobs lost their line to a player (they go on without one).", lost);
    EndTestJobs(now);
    if (!m_attached and HasPending() and ((now - m_lastFlush) >= (int64)std::max<uint32>(5, sConfig.marketSim.FlushSeconds)))
        Flush();
}

void SimFacilityMgr::Close()
{
    if (!m_loaded)
        return;
    // attached:  the market sim's flush wrote them with its stock (a write of our own could split the two)
    if (!m_attached)
        Flush();
}

bool SimFacilityMgr::LoadLines(uint32_t stationID, uint8_t activity, int64_t now, std::vector<FacilityLine>& into)
{
    m_offset = now - WallNow();
    std::vector<FactoryDB::SimLine> rows;
    if (!FactoryDB::SelectLinesForSim(std::vector<uint32>(1, stationID), activity, rows))
        return false;
    for (auto& r : rows) {
        FacilityLine line;
        line.lineID = r.lineID;
        line.ownerID = r.ownerID;
        line.raw = r.nextFreeTime;
        line.freeAt = UnixCeil(r.nextFreeTime) + m_offset;
        line.timeMultiplier = (r.timeMultiplier > 0.0) ? r.timeMultiplier : 1.0;
        line.costInstall = r.costInstall;
        line.costPerHour = r.costPerHour;
        into.push_back(line);
    }
    return !into.empty();
}

bool SimFacilityMgr::CheckLines(const std::vector<uint32_t>& lineIDs, std::map<uint32_t, std::pair<int64_t, bool>>& into)
{
    std::vector<uint32> ids(lineIDs.begin(), lineIDs.end());
    std::map<uint32, std::pair<int64, bool>> states;
    if (!FactoryDB::GetLineStatesForSim(ids, (int64)GetFileTimeNow(), states))
        return false;
    for (auto& cur : states)
        into[cur.first] = std::make_pair((int64_t)cur.second.first, cur.second.second);
    return true;
}

int64_t SimFacilityMgr::RawFor(int64_t end, int64_t now) const
{
    // on the wall clock players see:  as long from now as the job is from the sim's now
    return (int64)GetFileTimeNow() + (end - now) * EvE::Time::Second;
}

int64 SimFacilityMgr::PlayerNextFreeTime(uint32 lineID, int64 nextFreeTime) const
{
    if (!m_loaded)
        return nextFreeTime;
    return PlayerView(lineID, nextFreeTime);
}

void SimFacilityMgr::PlayerInstalls(uint32 lineID, int64 endTime)
{
    if (!m_loaded)
        return;
    uint32 jobID(HeldBy(lineID));
    if (PlayerTakesLine(lineID, UnixCeil(endTime) + m_offset, endTime, SimNow())) {
        const FacilityJob* job(FindJob(jobID));
        _log(MARKET__SIM, "Market sim facilities:  a player installs on line %u;  sim job %u %s.", lineID, jobID,
             ((job != nullptr) and (job->lineID != 0)) ? Fmt("moves to line %u", job->lineID).c_str() : "goes on without a line");
    }
}

bool SimFacilityMgr::WritePending()
{
    std::vector<LineWrite> lines;
    PendingLineWrites(lines);
    if (lines.empty())
        return true;
    std::vector<FactoryDB::LineTimeWrite> writes;
    for (auto& w : lines)
        writes.push_back(FactoryDB::LineTimeWrite{w.lineID, w.hasGuard, w.guard, w.value, w.floor});
    return FactoryDB::SetNextFreeTimes(writes);
}

bool SimFacilityMgr::Flush()
{
    m_lastFlush = SimNow();
    if (!HasPending())
        return true;
    DBTransaction tx("market sim facilities");
    if (!WritePending()) {
        tx.Rollback();
        sLog.Error("  SimFacilityMgr", "Market sim facilities:  writing the lines failed.  They are kept in memory and written again.");
        return false;
    }
    if (!tx.Commit())
        return false;
    PendingWritten();
    return true;
}

std::string SimFacilityMgr::Command(Client* who, const Seperator& args)
{
    std::string sub((args.argCount() > 1) ? args.arg(1) : "status");
    std::string usage("usage: /simfacility status | station <stationID> | reserve <stationID> <activity> <jobs> <hours> |"
                      " release <jobID|test>   (activity: 1 manufacturing, 3 PE, 4 ME, 5 copying, 8 invention)");
    if (!m_loaded)
        return "The market sim's facilities are not loaded.";
    int64 now(SimNow());
    if (sub == "status") {
        std::vector<FacilityJob> jobs;
        JobsOf(0, jobs);
        uint32 onLines(0), tests(0);
        for (auto& j : jobs) {
            if (j.lineID != 0)
                ++onLines;
            if (j.actorID == 0)
                ++tests;
        }
        return Fmt("%s station lines:  share %.2f, %u lines kept free, refinery %.0f m3/h.\n"
                   "%u jobs running (%u on lines, %u GM test jobs).  Line writes pending:  %s;  flushed by %s.",
                   m_p.realLines ? "Real" : "Counted (not real)", m_p.maxLineShare, m_p.minFreeLines, m_p.refineryM3PerHour,
                   (uint32)jobs.size(), onLines, tests, HasPending() ? "yes" : "no", m_attached ? "the market sim" : "itself");
    }
    if (sub == "station") {
        if ((args.argCount() < 3) or !args.isNumber(2))
            return usage;
        uint32 stationID((uint32)std::strtoul(args.arg(2).c_str(), nullptr, 10));
        InvalidateLines(stationID);
        std::string out;
        for (auto activity : s_activities) {
            StationUse use;
            if (!Use(stationID, activity, now, use))
                continue;
            out += Fmt("activity %u:  %u lines, %u free, the sim's %u (%u on lines), at most %u;  it may start %u more.\n",
                       activity, use.lines, use.free, use.simBusy, use.simOnLines, use.cap, use.allowed);
        }
        return out.empty() ? Fmt("Station %u has no lines the sim may use.", stationID) : out;
    }
    if ((sub == "reserve") or (sub == "release")) {
        if ((who->GetAccountRole() & Acct::Role::PROGRAMMER) == 0)
            return "/simfacility " + sub + " needs the PROGRAMMER role.";
    }
    if (sub == "reserve") {
        if ((args.argCount() < 6) or !args.isNumber(2) or !args.isNumber(3) or !args.isNumber(4))
            return usage;
        JobRequest req;
        req.actorID = 0;    // a GM test job:  no output
        req.stationID = (uint32)std::strtoul(args.arg(2).c_str(), nullptr, 10);
        req.activity = (uint8)std::atoi(args.arg(3).c_str());
        req.runs = 1;
        req.unitsPerRun = 0;
        req.secondsPerRun = std::max(60.0, std::atof(args.arg(5).c_str()) * 3600.0);
        int jobs(std::min(std::max(1, std::atoi(args.arg(4).c_str())), 1000));
        InvalidateLines(req.stationID);
        std::string ids;
        int made(0);
        for (; made < jobs; ++made) {
            FacilityJob job(Reserve(req, now));
            if (job.jobID == 0)
                break;
            ids += Fmt(" %u", job.jobID) + (job.lineID ? Fmt("@%u", job.lineID) : std::string("@-"));
        }
        if (!m_attached)
            Flush();
        _log(MARKET__SIM, "Market sim facilities:  %s reserved %i test jobs at station %u (activity %u).", who->GetName(), made,
             req.stationID, req.activity);
        return Fmt("%i of %i test jobs reserved (job@line):%s%s", made, jobs, ids.c_str(),
                   m_attached ? "  (written with the market sim's next flush)" : "");
    }
    if (sub == "release") {
        if (args.argCount() < 3)
            return usage;
        uint32 n(0);
        if (args.arg(2) == "test") {
            std::vector<FacilityJob> jobs;
            JobsOf(0, jobs);
            for (auto& j : jobs)
                if ((j.actorID == 0) and Release(j.jobID, now))
                    ++n;
        } else if (args.isNumber(2)) {
            // test jobs only:  the sim's own jobs are the world's (it releases them when they end)
            uint32 jobID((uint32)std::strtoul(args.arg(2).c_str(), nullptr, 10));
            const FacilityJob* job(FindJob(jobID));
            if ((job == nullptr) or (job->actorID != 0))
                return Fmt("%u is not a GM test job.", jobID);
            if (Release(jobID, now))
                ++n;
        } else {
            return usage;
        }
        if (!m_attached)
            Flush();
        _log(MARKET__SIM, "Market sim facilities:  %s released %u jobs.", who->GetName(), n);
        return Fmt("%u jobs released.", n);
    }
    return usage;
}
