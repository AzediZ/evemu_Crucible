 /**
  * @name LawRules.h
  *   the pure rules of Crucible law enforcement (doc/code_and_design_notes/planning/npc-ai/PLAN.md 4.8 - 4.10, phase 4):
  *   CONCORD's response delay, the faction police and navy thresholds, and the Crimewatch 1 sentry timers.
  *   header only and dependency free (also used by eve-test, npcai/LawRulesTest)
  */

#ifndef _EVE_NPC_AI_LAWRULES_H_
#define _EVE_NPC_AI_LAWRULES_H_

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace LawRules {

/* CONCORD's response in seconds for a system of security level 'level' (0.5 - 1.0, rounded to 0.1 as the client shows it).
 *  table is crime.ConcordResponse:  seconds for 1.0, 0.9, 0.8, 0.7, 0.6, 0.5.  a table with fewer than 6 numbers gives the
 *  default "2,5,10,15,20,25" (the closest pre-Crucible measurements, owner decision 3).  below 0.5 counts as 0.5 */
inline uint32_t ConcordDelaySeconds(const std::string& table, float level)
{
    std::vector<uint32_t> values;
    size_t start(0);
    while (start <= table.size()) {
        size_t end(table.find(',', start));
        if (end == std::string::npos)
            end = table.size();
        std::string item(table.substr(start, end - start));
        if (!item.empty())
            values.push_back(static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10)));
        start = end + 1;
    }
    if (values.size() < 6)
        values = {2, 5, 10, 15, 20, 25};
    int idx(static_cast<int>(std::lround((1.0f - level) * 10.0f)));
    if (idx < 0)
        idx = 0;
    if (idx > 5)
        idx = 5;
    return values[idx];
}

/* faction police chase pilots whose security status (rounded to 0.1 as the client shows it) is at or below
 *  -2.0 in a 1.0 system, down to -4.5 in a 0.5 system (EVElopedia 2011) */
inline float PoliceThreshold(float level)
{
    return -2.0f - ((1.0f - level) * 5.0f);
}
inline bool PoliceWanted(float securityStatus, float level)
{
    return ((std::round(securityStatus * 10.0f) / 10.0f) <= (PoliceThreshold(level) + 1e-4f));
}

/* faction navies attack pilots whose standing with their faction is at or below the threshold (crime.FactionNavyStanding) */
inline bool NavyWanted(float standing, float threshold)
{
    return (standing <= threshold);
}

/* a timer stamped at 'last' (0 = never) still runs at 'now' for 'duration' (same units) */
inline bool Live(int64_t last, int64_t duration, int64_t now)
{
    return ((last > 0) and (duration > 0) and ((last + duration) > now));
}

/* one of count candidates for a draw u in [0,1) (the sentry's random pick) */
inline size_t PickIndex(size_t count, double u)
{
    if (count == 0)
        return 0;
    if (!(u >= 0.0))
        u = 0.0;
    size_t idx(static_cast<size_t>(u * static_cast<double>(count)));
    return (idx < count ? idx : count - 1);
}

/* who a Crimewatch 1 gate or station gun may shoot (4.10):  a pilot with the global criminal countdown, a pilot with
 *  aggression against what the gun guards (its gate or station, or the guns themselves), or (crime.NpcFactionAggression)
 *  a pilot with the NPC aggression timer against the gun's faction.  their ships and drones alike, within crime.SentryRange.
 *  never pods, cloaked or warping ships.  security status alone (outlaws) isn't enough, in high or low sec */
struct SentryCandidate {
    bool criminal;          // global criminal countdown
    bool objectAggression;  // aggression against the protected object
    bool factionAggression; // npc aggression timer against the gun's faction (only when the knob is on)
    bool pod;
    bool hidden;            // cloaked or warping
    bool inRange;           // within crime.SentryRange
};
inline bool SentryMayShoot(const SentryCandidate& c)
{
    if (c.pod or c.hidden or !c.inRange)
        return false;
    return (c.criminal or c.objectAggression or c.factionAggression);
}

}   // namespace LawRules

#endif  // _EVE_NPC_AI_LAWRULES_H_
