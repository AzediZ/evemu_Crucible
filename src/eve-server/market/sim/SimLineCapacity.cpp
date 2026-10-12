 /**
  * @name SimLineCapacity.cpp
  *   market simulation:  the world's capacity on station lines (see SimLineCapacity.h)
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>

#include "market/sim/SimLineCapacity.h"

namespace MktSim {

LineCapacity::LineCapacity(FacilityBook& book, double probeInstall, double probePerHour)
: m_book(book),
  m_probeInstall(probeInstall),
  m_probePerHour(probePerHour),
  m_now(0)
{
}

uint32_t LineCapacity::Slots(uint32_t actorID) const
{
    std::map<uint32_t, uint32_t>::const_iterator itr = m_slots.find(actorID);
    return (itr == m_slots.end()) ? 0 : itr->second;
}

uint32_t LineCapacity::StationOf(const SimJob& job) const
{
    if (job.stationID != 0)
        return job.stationID;
    std::map<uint32_t, uint32_t>::const_iterator itr = m_nodeStation.find(job.nodeID);
    return (itr == m_nodeStation.end()) ? 0 : itr->second;
}

uint32_t LineCapacity::BookJob(const SimJob& job) const
{
    return m_book.FindJobFor(job.actorID, StationOf(job), job.activity, job.start, job.end, job.lineID);
}

uint32_t LineCapacity::Free(uint32_t actorID, uint32_t nodeID, uint8_t activity, int64_t now) const
{
    m_now = std::max(m_now, now);
    std::map<uint32_t, uint32_t>::const_iterator u = m_used.find(actorID);
    uint32_t used((u == m_used.end()) ? 0 : u->second), slots(Slots(actorID));
    if (slots <= used)
        return 0;
    std::map<uint32_t, uint32_t>::const_iterator s = m_nodeStation.find(nodeID);
    if (s == m_nodeStation.end())
        return 0;
    return std::min(slots - used, m_book.Available(s->second, activity, now));
}

bool LineCapacity::Reserve(SimJob& job, int64_t now)
{
    m_now = std::max(m_now, now);
    std::map<uint32_t, uint32_t>::const_iterator u = m_used.find(job.actorID);
    if (((u == m_used.end()) ? 0 : u->second) >= Slots(job.actorID))
        return false;
    JobRequest req;
    req.actorID = job.actorID;
    req.activity = job.activity;
    req.stationID = StationOf(job);
    req.outputTypeID = job.typeID;
    req.runs = std::max<uint32_t>(1, job.runs);
    req.unitsPerRun = job.qtyOut / (Units)req.runs;
    // the book's job starts when the world's does (the world starts its jobs now)
    if (job.start <= 0) {
        job.end += now - job.start;
        job.start = now;
    }
    req.seconds = std::max<int64_t>(1, job.end - job.start);
    FacilityJob booked(m_book.Reserve(req, job.start));
    if (booked.jobID == 0)
        return false;
    job.lineID = booked.lineID;
    job.end = booked.end;
    ++m_used[job.actorID];
    return true;
}

void LineCapacity::Restore(const SimJob& job)
{
    FacilityJob j;
    j.actorID = job.actorID;
    j.activity = job.activity;
    j.stationID = StationOf(job);
    j.lineID = m_book.Params().realLines ? job.lineID : 0;
    j.outputTypeID = job.typeID;
    j.runs = job.runs;
    j.qtyOut = job.qtyOut;
    j.start = job.start;
    j.end = job.end;
    // what the line had before the sim isn't kept:  0 (free) is all a player's quote needs
    m_book.LoadJob(j, false);
    ++m_used[job.actorID];
}

void LineCapacity::Release(const SimJob& job)
{
    uint32_t id(BookJob(job));
    if (id != 0)
        m_book.Release(id, m_now);
    std::map<uint32_t, uint32_t>::iterator u = m_used.find(job.actorID);
    if ((u != m_used.end()) and (u->second > 0))
        --u->second;
}

double LineCapacity::JobCost(const SimJob& job) const
{
    uint32_t id(BookJob(job));
    const FacilityJob* booked((id != 0) ? m_book.FindJob(id) : nullptr);
    if (booked != nullptr)
        return (double)booked->cost / 100.0;
    return (double)Capacity::JobCost(m_probeInstall, m_probePerHour, std::max<int64_t>(0, job.end - job.start)) / 100.0;
}

uint32_t LineCapacity::CurrentLine(const SimJob& job) const
{
    uint32_t id(BookJob(job));
    const FacilityJob* booked((id != 0) ? m_book.FindJob(id) : nullptr);
    return (booked != nullptr) ? booked->lineID : job.lineID;
}

}
