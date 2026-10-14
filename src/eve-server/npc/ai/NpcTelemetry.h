 /**
  * @name NpcTelemetry.h
  *   what the npc ai of one system did lately, for tuning against expectations (doc/code_and_design_notes/planning/npc-ai/PLAN.md
  *   phase 5:  "a periodic NPC__AI_STATS line per system (engaged count, mean lock time, hit ratio, switches per minute)").
  *
  *   SystemManager owns one window per system.  the npcs and sentries count into it as they go, the system closes each
  *   tick (BeginTick/EndTick) and writes the line every npc.StatsInterval (Format), then starts a new window.
  *   expected bands (plan phase 5 acceptance):  standard-ai switches per minute about 0 (drone rolls only), level-one
  *   switches per engaged npc-minute at most 1.
  *   also the last turret shots of one npc (ShotLog), for /npcai verbose:  every term that decided each shot.
  *   header only and dependency free (also used by eve-test, npcai/NpcPolishTest)
  */

#ifndef _EVE_NPC_AI_NPCTELEMETRY_H_
#define _EVE_NPC_AI_NPCTELEMETRY_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

struct NpcStats {
    int64_t windowStart = 0;    // ms
    uint64_t ticks = 0;         // ticks closed in this window
    // per tick, summed over the window
    uint64_t npcTicks = 0;      // npcs that existed (ai ran or not), summed over ticks
    uint64_t engagedTicks = 0;  // npcs not idle after their ai ran, summed over ticks
    uint64_t engagedL1Ticks = 0;// level-one npcs of those
    uint32_t engagedMax = 0;    // most npcs engaged in one tick
    // events
    uint64_t locks = 0;         // locks an npc or sentry started
    double lockMs = 0.0;        //  their lock times, summed
    uint64_t shots = 0;         // npc and sentry turret shots
    uint64_t hits = 0;          //  of them hits (wrecking ones too)
    uint64_t wrecking = 0;      //  of them wrecking shots
    uint64_t missiles = 0;      // npc and sentry missiles launched
    uint64_t switchesStandard = 0;  // deliberate switches of standard (and passive) npcs:  drone rolls
    uint64_t switchesL1 = 0;    // deliberate switches of level-one npcs
    uint64_t reinforcements = 0;// sleeper capital reinforcements spawned
    // cost
    uint64_t aiRuns = 0;        // npc ai Process() calls that ran
    double aiUs = 0.0;          //  their time, summed
    double aiUsMax = 0.0;       //  the longest one
    double tickUsMax = 0.0;     // the most npc ai time in one tick
    uint64_t lodSkipped = 0;    // npc ticks the level of detail skipped
    uint64_t budgetDeferred = 0;// npc ticks the budget pushed to the next tick

    // this tick (EndTick folds them in)
    uint32_t curNpcs = 0;
    uint32_t curEngaged = 0;
    uint32_t curEngagedL1 = 0;
    double curUs = 0.0;

    void Reset(int64_t nowMs)                           { *this = NpcStats();  windowStart = nowMs; }
    void BeginTick()                                    { curNpcs = 0;  curEngaged = 0;  curEngagedL1 = 0;  curUs = 0.0; }
    void EndTick() {
        ++ticks;
        npcTicks += curNpcs;
        engagedTicks += curEngaged;
        engagedL1Ticks += curEngagedL1;
        if (curEngaged > engagedMax)
            engagedMax = curEngaged;
        if (curUs > tickUsMax)
            tickUsMax = curUs;
    }
    // one npc's ai run, us
    void AddRun(double us) {
        ++aiRuns;
        aiUs += us;
        curUs += us;
        if (us > aiUsMax)
            aiUsMax = us;
    }
    void AddLock(double ms)                             { ++locks;  lockMs += (ms > 0.0 ? ms : 0.0); }
    // a turret shot's quality (0 miss, 3 wrecking, else a hit)
    void AddShot(double quality) {
        ++shots;
        if (quality > 0.0)
            ++hits;
        if (quality >= 3.0)
            ++wrecking;
    }
    bool Empty() const                                  { return (npcTicks == 0) and (shots == 0) and (locks == 0); }
    // seconds this window covers (at least 1)
    double Seconds(int64_t nowMs) const {
        double s((nowMs - windowStart) / 1000.0);
        return (s >= 1.0 ? s : 1.0);
    }
    // per minute of the window
    double PerMinute(uint64_t count, int64_t nowMs) const   { return (count * 60.0) / Seconds(nowMs); }
    // per minute of one engaged npc (engagedTicks are npc-seconds)
    static double PerNpcMinute(uint64_t count, uint64_t npcSeconds) {
        return (npcSeconds == 0 ? 0.0 : (count * 60.0) / static_cast<double>(npcSeconds));
    }
    double MeanEngaged() const                          { return (ticks == 0 ? 0.0 : static_cast<double>(engagedTicks) / ticks); }
    double MeanNpcs() const                             { return (ticks == 0 ? 0.0 : static_cast<double>(npcTicks) / ticks); }
    double MeanLockMs() const                           { return (locks == 0 ? 0.0 : lockMs / locks); }
    double HitRatio() const                             { return (shots == 0 ? 0.0 : static_cast<double>(hits) / shots); }
    double WreckingRatio() const                        { return (shots == 0 ? 0.0 : static_cast<double>(wrecking) / shots); }
    double MeanAiUs() const                             { return (aiRuns == 0 ? 0.0 : aiUs / aiRuns); }

    // the NPC__AI_STATS line
    std::string Format(const char* systemName, uint32_t systemID, int64_t nowMs) const {
        char buf[768];
        std::snprintf(buf, sizeof(buf),
                "%s(%u) last %.0fs:  npcs %.1f (engaged %.1f, max %u), locks %llu (mean %.0fms), turret shots %llu (hits %.1f%%, "
                "wrecking %.2f%%), missiles %llu, switches/min standard %.2f level-one %.2f (%.2f per engaged level-one npc), "
                "reinforcements %llu, ai %.1fus/run (max %.0fus, tick max %.0fus, %llu runs), lod skipped %llu, budget deferred %llu",
                systemName, systemID, Seconds(nowMs), MeanNpcs(), MeanEngaged(), engagedMax,
                static_cast<unsigned long long>(locks), MeanLockMs(), static_cast<unsigned long long>(shots), HitRatio() * 100.0,
                WreckingRatio() * 100.0, static_cast<unsigned long long>(missiles), PerMinute(switchesStandard, nowMs),
                PerMinute(switchesL1, nowMs), PerNpcMinute(switchesL1, engagedL1Ticks),
                static_cast<unsigned long long>(reinforcements), MeanAiUs(), aiUsMax, tickUsMax,
                static_cast<unsigned long long>(aiRuns), static_cast<unsigned long long>(lodSkipped),
                static_cast<unsigned long long>(budgetDeferred));
        return std::string(buf);
    }
};

/* one turret shot and the terms that decided it (PLAN.md 5.1:  chance = 0.5 ^ (track^2 + range^2), a hit when u < chance,
 *  a wrecking shot when u < rates.WreckingChance inside a hit, else quality u + 0.49) */
struct NpcShotRecord {
    int64_t atMs = 0;
    uint32_t targetID = 0;
    double surface = 0.0, optimal = 0.0, falloff = 0.0;
    double transversal = 0.0, angular = 0.0, tracking = 0.0;
    double sigRes = 0.0, sig = 0.0;
    double track = 0.0, range = 0.0, chance = 0.0, u = 0.0, quality = 0.0;
};

/* the last few shots of one shooter */
class NpcShotLog {
public:
    static constexpr size_t Capacity = 6;
    void Add(const NpcShotRecord& rec)                  { m_recs[m_next] = rec;  m_next = (m_next + 1) % Capacity;  if (m_count < Capacity) ++m_count; }
    size_t Size() const                                 { return m_count; }
    // 0 = the newest
    const NpcShotRecord& Newest(size_t i) const         { return m_recs[(m_next + Capacity - 1 - (i % Capacity)) % Capacity]; }
    // one line for /npcai
    static std::string Format(const NpcShotRecord& r, int64_t nowMs) {
        char buf[384];
        std::snprintf(buf, sizeof(buf),
                "%.1fs ago -> %u:  surface %.0fm (opt %.0f fo %.0f) range %.4f, transversal %.1fm/s angular %.6f (trk %.6f) sigRes %.0f "
                "sig %.1f track %.4f, chance %.5f, u %.5f -> %s %.3f",
                (nowMs - r.atMs) / 1000.0, r.targetID, r.surface, r.optimal, r.falloff, r.range, r.transversal, r.angular,
                r.tracking, r.sigRes, r.sig, r.track, r.chance, r.u,
                (r.quality <= 0.0 ? "miss" : (r.quality >= 3.0 ? "wrecking" : "hit")), r.quality);
        return std::string(buf);
    }

private:
    NpcShotRecord m_recs[Capacity];
    size_t m_next = 0;
    size_t m_count = 0;
};

#endif  // _EVE_NPC_AI_NPCTELEMETRY_H_
