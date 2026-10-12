 /**
  * @name SimFacilities.cpp
  *   market simulation:  facility capacity (PLAN 6.6).  the policy and the job book shared by the server and the harness
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>
#include <cmath>

#include "market/sim/SimFacilities.h"

namespace MktSim {

/* the policy */

uint32_t Capacity::ShareCap(uint32_t lines, double maxShare)
{
    if (maxShare <= 0.0)
        return 0;
    double cap(std::floor(maxShare * lines + 1e-9));
    return (cap >= lines) ? lines : (uint32_t)cap;
}

uint32_t Capacity::Allowed(uint32_t lines, uint32_t free, uint32_t simBusy, double maxShare, uint32_t minFree)
{
    uint32_t cap(ShareCap(lines, maxShare));
    if ((simBusy >= cap) or (free <= minFree))
        return 0;
    return std::min(cap - simBusy, free - minFree);
}

bool Capacity::CanMove(uint32_t free, uint32_t minFree)
{
    return free > minFree;
}

uint32_t Capacity::RunsPerJob(uint32_t runs, double secondsPerRun, double maxHours)
{
    if (runs == 0)
        return 0;
    if ((secondsPerRun <= 0.0) or (maxHours <= 0.0))
        return runs;
    double n(std::floor(maxHours * 3600.0 / secondsPerRun + 1e-9));
    if (n < 1.0)
        return 1;
    return (n >= runs) ? runs : (uint32_t)n;
}

int64_t Capacity::JobSeconds(uint32_t runs, double secondsPerRun, double timeMultiplier)
{
    double t(std::ceil(runs * secondsPerRun * timeMultiplier - 1e-6));
    return (t < 1.0) ? 1 : (int64_t)t;
}

Cents Capacity::JobCost(double costInstall, double costPerHour, int64_t seconds)
{
    int64_t hours((seconds + 3599) / 3600);
    double isk(std::max(0.0, costInstall) + std::max(0.0, costPerHour) * hours);
    return (Cents)std::llround(isk * 100.0);
}

double Capacity::Refill(double level, double m3PerHour, int64_t dt)
{
    if (dt > 0)
        level += m3PerHour * dt / 3600.0;
    return std::min(level, m3PerHour);
}

/* the book */

FacilityBook::FacilityBook()
: m_nextJobID(1)
{
}

FacilityBook::StationLines* FacilityBook::Lines(uint32_t stationID, uint8_t activity, int64_t now)
{
    StationKey key(stationID, activity);
    std::map<StationKey, StationLines>::iterator itr = m_stations.find(key);
    if ((itr != m_stations.end()) and (now >= itr->second.loaded) and ((now - itr->second.loaded) < m_p.lineRefreshSeconds))
        return &itr->second;

    StationLines& s(m_stations[key]);
    s.loaded = now;
    s.lines.clear();
    s.any = LoadLines(stationID, activity, now, s.lines);
    std::sort(s.lines.begin(), s.lines.end(), [](const FacilityLine& a, const FacilityLine& b) { return a.lineID < b.lineID; });
    // the source may not have the sim's latest writes yet (they go with the next flush):  memory knows better
    for (auto& line : s.lines) {
        m_lineStation[line.lineID] = key;
        uint32_t jobID(HeldBy(line.lineID));
        if (jobID != 0) {
            const FacilityJob& job(m_jobs[jobID]);
            line.freeAt = job.end;
            line.raw = job.lineSet;
            continue;
        }
        std::unordered_map<uint32_t, LineMark>::const_iterator mItr = m_marks.find(line.lineID);
        if ((mItr != m_marks.end()) and mItr->second.inSource and (line.raw == mItr->second.flushed)) {
            // released, the source still has the sim's time:  it is reverted with the next flush
            line.raw = mItr->second.prev;
            line.freeAt = now;
        }
    }
    return &s;
}

FacilityLine* FacilityBook::FindLine(uint32_t lineID)
{
    std::unordered_map<uint32_t, StationKey>::const_iterator kItr = m_lineStation.find(lineID);
    if (kItr == m_lineStation.end())
        return nullptr;
    std::map<StationKey, StationLines>::iterator sItr = m_stations.find(kItr->second);
    if (sItr == m_stations.end())
        return nullptr;
    for (auto& line : sItr->second.lines)
        if (line.lineID == lineID)
            return &line;
    return nullptr;
}

uint32_t FacilityBook::HeldBy(uint32_t lineID) const
{
    std::unordered_map<uint32_t, uint32_t>::const_iterator itr = m_held.find(lineID);
    return (itr == m_held.end()) ? 0 : itr->second;
}

bool FacilityBook::IsFree(const FacilityLine& line, int64_t now) const
{
    return (line.freeAt <= now) and (HeldBy(line.lineID) == 0);
}

uint32_t FacilityBook::CountFree(const StationLines& s, int64_t now) const
{
    uint32_t n(0);
    for (auto& line : s.lines)
        if (IsFree(line, now))
            ++n;
    return n;
}

uint32_t FacilityBook::SimBusy(uint32_t stationID, uint8_t activity) const
{
    std::map<StationKey, uint32_t>::const_iterator itr = m_busy.find(StationKey(stationID, activity));
    return (itr == m_busy.end()) ? 0 : itr->second;
}

uint32_t FacilityBook::AllowedAt(const StationLines& s, uint32_t stationID, uint8_t activity, int64_t now) const
{
    uint32_t lines((uint32_t)s.lines.size()), busy(SimBusy(stationID, activity)), freeNow(0);
    if (m_p.realLines) {
        freeNow = CountFree(s, now);
    } else {
        // counters only:  the sim assumes the lines it doesn't use are free
        freeNow = (lines > busy) ? (lines - busy) : 0;
    }
    return Capacity::Allowed(lines, freeNow, busy, m_p.maxLineShare, m_p.minFreeLines);
}

FacilityLine* FacilityBook::PickLine(StationLines& s, int64_t now)
{
    for (auto& line : s.lines)
        if (IsFree(line, now))
            return &line;
    return nullptr;
}

void FacilityBook::Take(FacilityJob& job, FacilityLine& line, int64_t now)
{
    job.lineID = line.lineID;
    job.linePrev = line.raw;
    job.lineSet = RawFor(job.end, now);
    line.raw = job.lineSet;
    line.freeAt = job.end;
    m_held[line.lineID] = job.jobID;
    m_touched.insert(line.lineID);
    // a line the sim had before keeps what it was before the first time (and what the source still holds)
    if (m_marks.find(line.lineID) == m_marks.end()) {
        LineMark& mark(m_marks[line.lineID]);
        mark.prev = job.linePrev;
    }
}

void FacilityBook::Leave(FacilityJob& job)
{
    if (job.lineID == 0)
        return;
    uint32_t lineID(job.lineID);
    m_held.erase(lineID);
    FacilityLine* line(FindLine(lineID));
    if (line != nullptr) {
        line->raw = job.linePrev;
        line->freeAt = std::min(line->freeAt, job.start);
    }
    std::unordered_map<uint32_t, LineMark>::iterator mItr = m_marks.find(lineID);
    if (mItr != m_marks.end()) {
        if (mItr->second.inSource) {
            // the source has the sim's time:  put the line back (a player's time in the meantime stays)
            m_touched.insert(lineID);
        } else {
            // nothing of the sim's reached the source
            m_marks.erase(mItr);
        }
    }
    job.lineID = 0;
}

void FacilityBook::Remove(std::map<uint32_t, FacilityJob>::iterator itr)
{
    const FacilityJob& job(itr->second);
    std::pair<std::multimap<JobKey, uint32_t>::iterator, std::multimap<JobKey, uint32_t>::iterator> range(m_byKey.equal_range(KeyOf(job)));
    for (std::multimap<JobKey, uint32_t>::iterator kItr = range.first; kItr != range.second; ++kItr) {
        if (kItr->second == job.jobID) {
            m_byKey.erase(kItr);
            break;
        }
    }
    std::map<StationKey, uint32_t>::iterator bItr = m_busy.find(StationKey(job.stationID, job.activity));
    if (bItr != m_busy.end()) {
        if (bItr->second > 1) {
            --bItr->second;
        } else {
            m_busy.erase(bItr);
        }
    }
    m_jobs.erase(itr);
}

void FacilityBook::EndJob(std::map<uint32_t, FacilityJob>::iterator itr)
{
    // a line the job ran its time on is free by itself, unless the source still has an older time of the sim's
    const FacilityJob& job(itr->second);
    if (job.lineID != 0) {
        std::unordered_map<uint32_t, LineMark>::iterator mItr = m_marks.find(job.lineID);
        if ((mItr != m_marks.end()) and (!mItr->second.inSource or (mItr->second.flushed == job.lineSet))) {
            m_marks.erase(mItr);
            m_touched.erase(job.lineID);
        }
    }
    Leave(itr->second);
    Remove(itr);
}

uint32_t FacilityBook::Available(uint32_t stationID, uint8_t activity, int64_t now)
{
    StationLines* s(Lines(stationID, activity, now));
    if ((s == nullptr) or s->lines.empty())
        return 0;
    return AllowedAt(*s, stationID, activity, now);
}

FacilityJob FacilityBook::Reserve(const JobRequest& req, int64_t now)
{
    FacilityJob job;
    if ((req.runs == 0) or (req.stationID == 0))
        return job;
    StationLines* s(Lines(req.stationID, req.activity, now));
    if ((s == nullptr) or s->lines.empty() or (AllowedAt(*s, req.stationID, req.activity, now) == 0))
        return job;
    // counters only:  the station's first line stands for its lines (time multiplier, cost, owner)
    FacilityLine* line(m_p.realLines ? PickLine(*s, now) : &s->lines.front());
    if (line == nullptr)
        return job;

    job.jobID = m_nextJobID++;
    job.actorID = req.actorID;
    job.activity = req.activity;
    job.stationID = req.stationID;
    job.outputTypeID = req.outputTypeID;
    int64_t seconds(0);
    if (req.seconds > 0) {
        job.runs = req.runs;
        seconds = Capacity::JobSeconds(1, (double)req.seconds, line->timeMultiplier);
    } else {
        job.runs = Capacity::RunsPerJob(req.runs, req.secondsPerRun * line->timeMultiplier, m_p.maxJobHours);
        seconds = Capacity::JobSeconds(job.runs, req.secondsPerRun, line->timeMultiplier);
    }
    job.qtyOut = (Units)job.runs * req.unitsPerRun;
    job.start = now;
    job.end = now + seconds;
    job.cost = Capacity::JobCost(line->costInstall, line->costPerHour, seconds);
    job.payeeID = line->ownerID;
    if (m_p.realLines)
        Take(job, *line, now);

    m_jobs[job.jobID] = job;
    m_byKey.insert(std::make_pair(KeyOf(job), job.jobID));
    ++m_busy[StationKey(job.stationID, job.activity)];
    return job;
}

bool FacilityBook::Release(uint32_t jobID, int64_t now)
{
    std::map<uint32_t, FacilityJob>::iterator itr = m_jobs.find(jobID);
    if (itr == m_jobs.end())
        return false;
    if (itr->second.end <= now) {
        EndJob(itr);
    } else {
        Leave(itr->second);
        Remove(itr);
    }
    return true;
}

uint32_t FacilityBook::EndTestJobs(int64_t now)
{
    std::vector<uint32_t> due;
    for (auto& cur : m_jobs)
        if ((cur.second.actorID == 0) and (cur.second.end <= now))
            due.push_back(cur.first);
    for (auto jobID : due)
        EndJob(m_jobs.find(jobID));
    return (uint32_t)due.size();
}

double FacilityBook::RefineAllowance(uint32_t stationID, double m3, int64_t now)
{
    if (m3 <= 0.0)
        return 0.0;
    if (m_p.refineryM3PerHour <= 0.0)
        return m3;
    std::map<uint32_t, std::pair<double, int64_t>>::iterator itr = m_refine.find(stationID);
    if (itr == m_refine.end())
        itr = m_refine.insert(std::make_pair(stationID, std::make_pair(m_p.refineryM3PerHour, now))).first;
    double level(Capacity::Refill(itr->second.first, m_p.refineryM3PerHour, now - itr->second.second));
    double take(std::min(m3, level));
    itr->second.first = level - take;
    itr->second.second = std::max(itr->second.second, now);
    return take;
}

const FacilityJob* FacilityBook::FindJob(uint32_t jobID) const
{
    std::map<uint32_t, FacilityJob>::const_iterator itr = m_jobs.find(jobID);
    return (itr == m_jobs.end()) ? nullptr : &itr->second;
}

uint32_t FacilityBook::FindJobFor(uint32_t actorID, uint32_t stationID, uint8_t activity, int64_t start, int64_t end,
                                  uint32_t preferLine/*0*/) const
{
    std::pair<std::multimap<JobKey, uint32_t>::const_iterator, std::multimap<JobKey, uint32_t>::const_iterator>
        range(m_byKey.equal_range(JobKey(actorID, stationID, activity, start, end)));
    if (range.first == range.second)
        return 0;
    if (preferLine != 0)
        for (std::multimap<JobKey, uint32_t>::const_iterator itr = range.first; itr != range.second; ++itr)
            if (HeldBy(preferLine) == itr->second)
                return itr->second;
    return range.first->second;
}

void FacilityBook::JobsOf(uint32_t actorID, std::vector<FacilityJob>& into) const
{
    for (auto& cur : m_jobs)
        if ((actorID == 0) or (cur.second.actorID == actorID))
            into.push_back(cur.second);
}

bool FacilityBook::PlayerTakesLine(uint32_t lineID, int64_t playerFreeAt, int64_t playerRaw, int64_t now)
{
    uint32_t jobID(HeldBy(lineID));
    FacilityJob* job(nullptr);
    if (jobID != 0) {
        job = &m_jobs[jobID];
        Leave(*job);
    }

    // the player's line is busy now, whatever the source says until the player's job is saved
    StationLines* s(nullptr);
    if (job != nullptr)
        s = Lines(job->stationID, job->activity, now);
    FacilityLine* line(FindLine(lineID));
    if (line != nullptr) {
        line->freeAt = std::max(line->freeAt, playerFreeAt);
        line->raw = playerRaw;
    }
    if (job == nullptr)
        return false;

    // the sim's job goes on:  on another free line if the player guarantee allows it, else without a line
    if (m_p.realLines and (job->end > now) and (s != nullptr) and Capacity::CanMove(CountFree(*s, now), m_p.minFreeLines)) {
        FacilityLine* other(PickLine(*s, now));
        if (other != nullptr)
            Take(*job, *other, now);
    }
    return true;
}

int64_t FacilityBook::PlayerView(uint32_t lineID, int64_t raw) const
{
    uint32_t jobID(HeldBy(lineID));
    if (jobID != 0) {
        std::map<uint32_t, FacilityJob>::const_iterator itr = m_jobs.find(jobID);
        if (itr != m_jobs.end())
            return itr->second.linePrev;
    }
    std::unordered_map<uint32_t, LineMark>::const_iterator mItr = m_marks.find(lineID);
    if ((mItr != m_marks.end()) and mItr->second.inSource and (raw == mItr->second.flushed))
        return mItr->second.prev;
    return raw;
}

bool FacilityBook::Use(uint32_t stationID, uint8_t activity, int64_t now, StationUse& into)
{
    into = StationUse();
    StationLines* s(Lines(stationID, activity, now));
    if ((s == nullptr) or s->lines.empty())
        return false;
    into.lines = (uint32_t)s->lines.size();
    into.free = CountFree(*s, now);
    into.simBusy = SimBusy(stationID, activity);
    for (auto& line : s->lines)
        if (HeldBy(line.lineID) != 0)
            ++into.simOnLines;
    into.cap = Capacity::ShareCap(into.lines, m_p.maxLineShare);
    into.allowed = AllowedAt(*s, stationID, activity, now);
    return true;
}

void FacilityBook::InvalidateLines(uint32_t stationID/*0*/)
{
    if (stationID == 0) {
        m_stations.clear();
        return;
    }
    for (std::map<StationKey, StationLines>::iterator itr = m_stations.begin(); itr != m_stations.end(); ) {
        if (itr->first.first == stationID) {
            itr = m_stations.erase(itr);
        } else {
            ++itr;
        }
    }
}

void FacilityBook::PendingLineWrites(std::vector<LineWrite>& into) const
{
    for (auto lineID : m_touched) {
        std::unordered_map<uint32_t, LineMark>::const_iterator mItr = m_marks.find(lineID);
        bool inSource((mItr != m_marks.end()) and mItr->second.inSource);
        uint32_t jobID(HeldBy(lineID));
        if (jobID != 0) {
            const FacilityJob& job(m_jobs.find(jobID)->second);
            into.push_back(LineWrite{lineID, inSource, inSource ? mItr->second.flushed : 0, job.lineSet, job.lineSet});
        } else if (inSource) {
            // undo the sim's time.  floor 0:  any other time (a player's) stays
            into.push_back(LineWrite{lineID, true, mItr->second.flushed, mItr->second.prev, 0});
        }
    }
}

void FacilityBook::PendingWritten()
{
    for (auto lineID : m_touched) {
        uint32_t jobID(HeldBy(lineID));
        if (jobID != 0) {
            const FacilityJob& job(m_jobs.find(jobID)->second);
            LineMark& mark(m_marks[lineID]);
            mark.inSource = true;
            mark.flushed = job.lineSet;
            mark.prev = job.linePrev;
        } else {
            m_marks.erase(lineID);
        }
    }
    m_touched.clear();
}

uint32_t FacilityBook::LoadJob(const FacilityJob& job, bool checked)
{
    FacilityJob j(job);
    j.jobID = m_nextJobID++;
    // two jobs on one line (the line moved to another job before a crash):  the second runs without it
    if ((j.lineID != 0) and (HeldBy(j.lineID) != 0))
        j.lineID = 0;
    m_jobs[j.jobID] = j;
    m_byKey.insert(std::make_pair(KeyOf(j), j.jobID));
    ++m_busy[StationKey(j.stationID, j.activity)];
    if (j.lineID != 0) {
        m_held[j.lineID] = j.jobID;
        LineMark& mark(m_marks[j.lineID]);
        mark.inSource = true;
        mark.flushed = j.lineSet;
        mark.prev = j.linePrev;
        if (!checked)
            m_unchecked.push_back(j.jobID);
    }
    return j.jobID;
}

uint32_t FacilityBook::VerifyRestored()
{
    if (m_unchecked.empty())
        return 0;
    std::vector<uint32_t> lineIDs;
    for (auto jobID : m_unchecked) {
        std::map<uint32_t, FacilityJob>::const_iterator itr = m_jobs.find(jobID);
        if ((itr != m_jobs.end()) and (itr->second.lineID != 0))
            lineIDs.push_back(itr->second.lineID);
    }
    std::map<uint32_t, std::pair<int64_t, bool>> states;
    if (!lineIDs.empty() and !CheckLines(lineIDs, states))
        return 0;   // again later
    uint32_t lost(0);
    for (auto jobID : m_unchecked) {
        std::map<uint32_t, FacilityJob>::iterator itr = m_jobs.find(jobID);
        if ((itr == m_jobs.end()) or (itr->second.lineID == 0))
            continue;
        FacilityJob& j(itr->second);
        std::map<uint32_t, std::pair<int64_t, bool>>::const_iterator sItr = states.find(j.lineID);
        if ((sItr == states.end()) or sItr->second.second) {
            // the line is gone, or a player has it:  the job goes on without it (the player's time stays)
            m_held.erase(j.lineID);
            m_marks.erase(j.lineID);
            m_touched.erase(j.lineID);
            j.lineID = 0;
            ++lost;
            continue;
        }
        // what the line holds is the sim's time (written with the job, in one transaction)
        j.lineSet = sItr->second.first;
        LineMark& mark(m_marks[j.lineID]);
        mark.inSource = true;
        mark.flushed = j.lineSet;
        FacilityLine* line(FindLine(j.lineID));
        if (line != nullptr) {
            line->raw = j.lineSet;
            line->freeAt = j.end;
        }
    }
    m_unchecked.clear();
    return lost;
}

}
