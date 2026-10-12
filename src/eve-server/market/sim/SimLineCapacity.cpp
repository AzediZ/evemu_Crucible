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

const std::vector<uint32_t>* LineCapacity::StationsOf(uint32_t nodeID) const
{
    std::map<uint32_t, std::vector<uint32_t>>::const_iterator itr = m_nodeStations.find(nodeID);
    return ((itr == m_nodeStations.end()) or itr->second.empty()) ? nullptr : &itr->second;
}

uint32_t LineCapacity::BookJob(const SimJob& job) const
{
    uint32_t stationID(job.stationID);
    if (stationID == 0) {
        const std::vector<uint32_t>* stations(StationsOf(job.nodeID));
        stationID = (stations != nullptr) ? stations->front() : 0;
    }
    return m_book.FindJobFor(job.actorID, stationID, job.activity, job.start, job.end, job.lineID);
}

uint32_t LineCapacity::Free(uint32_t actorID, uint32_t nodeID, uint8_t activity, int64_t now) const
{
    m_now = std::max(m_now, now);
    std::map<uint32_t, uint32_t>::const_iterator u = m_used.find(actorID);
    uint32_t used((u == m_used.end()) ? 0 : u->second), slots(Slots(actorID));
    if (slots <= used)
        return 0;
    const std::vector<uint32_t>* stations(StationsOf(nodeID));
    if (stations == nullptr)
        return 0;
    uint32_t room(0);
    for (auto stationID : *stations)
        room += m_book.Available(stationID, activity, now);
    return std::min(slots - used, room);
}

bool LineCapacity::Reserve(SimJob& job, int64_t now)
{
    m_now = std::max(m_now, now);
    std::map<uint32_t, uint32_t>::const_iterator u = m_used.find(job.actorID);
    if (((u == m_used.end()) ? 0 : u->second) >= Slots(job.actorID))
        return false;
    // the node's station with the most room (the first of equals), or the job's own production site
    uint32_t stationID(0);
    const std::vector<uint32_t>* stations(StationsOf(job.nodeID));
    bool ofNode((job.stationID == 0) or ((stations != nullptr)
                and (std::find(stations->begin(), stations->end(), job.stationID) != stations->end())));
    if (ofNode and (stations != nullptr)) {
        uint32_t best(0);
        for (auto id : *stations) {
            uint32_t room(m_book.Available(id, job.activity, now));
            if (room > best) {
                best = room;
                stationID = id;
            }
        }
    } else if (!ofNode) {
        stationID = job.stationID;
    }
    if (stationID == 0)
        return false;
    JobRequest req;
    req.actorID = job.actorID;
    req.activity = job.activity;
    req.stationID = stationID;
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
    job.stationID = stationID;
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
    const std::vector<uint32_t>* stations(StationsOf(job.nodeID));
    j.stationID = (job.stationID != 0) ? job.stationID : ((stations != nullptr) ? stations->front() : 0);
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
