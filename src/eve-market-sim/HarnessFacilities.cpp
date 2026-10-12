 /**
  * @name HarnessFacilities.cpp
  *   the harness' station lines (see HarnessFacilities.h)
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>
#include <cmath>

#include "HarnessFacilities.h"

using namespace MktSim;

HarnessFacilities::HarnessFacilities(const FacilityParams& params)
: m_nextLineID(1),
  m_costInstall(1000.0),
  m_costPerHour(333.0),
  m_ownerID(1000002),
  m_playerBusy(0.0)
{
    SetParams(params);
    m_default[LineActivity::Manufacturing] = 50;
    m_default[LineActivity::ResearchTime] = 20;
    m_default[LineActivity::ResearchMaterial] = 20;
    m_default[LineActivity::Copying] = 10;
    m_default[LineActivity::Invention] = 20;
}

void HarnessFacilities::SetStationLines(uint32_t stationID, uint8_t activity, uint32_t lines)
{
    m_lines[Key(stationID, activity)] = lines;
    InvalidateLines(stationID);
}

void HarnessFacilities::SetDefaultLines(uint8_t activity, uint32_t lines)
{
    m_default[activity] = lines;
    InvalidateLines();
}

void HarnessFacilities::SetLineCost(double costInstall, double costPerHour, uint32_t ownerID)
{
    m_costInstall = costInstall;
    m_costPerHour = costPerHour;
    m_ownerID = ownerID;
    InvalidateLines();
}

bool HarnessFacilities::LoadLines(uint32_t stationID, uint8_t activity, int64_t now, std::vector<FacilityLine>& into)
{
    Key key(stationID, activity);
    std::map<Key, uint32_t>::const_iterator itr = m_lines.find(key);
    uint32_t count(0);
    if (itr != m_lines.end()) {
        count = itr->second;
    } else {
        std::map<uint8_t, uint32_t>::const_iterator dItr = m_default.find(activity);
        count = (dItr == m_default.end()) ? 0 : dItr->second;
    }
    if (count == 0)
        return false;

    // a station's lines keep their numbers (room for 1000 per station and activity)
    std::map<Key, uint32_t>::iterator fItr = m_firstLineID.find(key);
    if (fItr == m_firstLineID.end()) {
        fItr = m_firstLineID.insert(std::make_pair(key, m_nextLineID)).first;
        m_nextLineID += 1000;
    }
    // the players' lines are the first ones, busy until the next full hour
    uint32_t busy((uint32_t)std::lround(std::min(1.0, std::max(0.0, m_playerBusy)) * count));
    int64_t hourEnd((now - (now % 3600)) + 3600);
    for (uint32_t k = 0; (k < count) and (k < 1000); ++k) {
        FacilityLine line;
        line.lineID = fItr->second + k;
        line.ownerID = m_ownerID;
        line.freeAt = (k < busy) ? hourEnd : 0;
        line.raw = line.freeAt;
        line.timeMultiplier = 1.0;
        line.costInstall = m_costInstall;
        line.costPerHour = m_costPerHour;
        into.push_back(line);
    }
    return true;
}
