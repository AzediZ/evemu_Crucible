 /**
  * @name AmbientRules.h
  *   the pure rules of the ambient npcs (doc/code_and_design_notes/planning/ambient-npc-plan.md section 6):  security band,
  *   busyness, event rates, the Poisson sampler, grid and itinerary weights, hull and race picks, caps, miner, patrol and
  *   criminal numbers, landing distances and the warp factor of a reference hull.
  *   header only and dependency free (also used by eve-test, npcai/AmbientRulesTest).  every draw takes the caller's NpcRng
  */

#ifndef _EVE_NPC_AI_AMBIENTRULES_H_
#define _EVE_NPC_AI_AMBIENTRULES_H_

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "EVE_Warp.h"
#include "NpcRng.h"

namespace AmbientRules {

/* 6.1 security band.  level is the client's rounded level (CrimeWatch::SecurityLevel).  wormhole regions start at 11000000 */
namespace Band {
    enum : uint8_t {
        None    = 0,    // null sec, wormholes:  nothing ambient
        Low     = 1,    // 0.1 - 0.4:  light traffic, miners, criminals.  no patrols
        High    = 2     // 0.5 - 1.0:  everything
    };
}
inline uint8_t GetBand(float level, uint32_t regionID)
{
    if (regionID >= 11000000u)
        return Band::None;
    if (!(level > 0.0f))
        return Band::None;
    if (level >= 0.45f)
        return Band::High;
    return Band::Low;
}

/* 6.2 a market node's score from its turnover T (isk/day) against the biggest node's Tmax.  kind:  1 hub (floored at 0.6),
 *  2 regional (capped at 0.5), 3 site (ignored) */
inline double NodeScore(double T, double Tmax, uint8_t kind)
{
    if ((kind != 1) and (kind != 2))
        return 0.0;
    double h(0.0);
    if ((T > 0.0) and (Tmax > 0.0))
        h = std::log1p(T) / std::log1p(Tmax);
    if (!std::isfinite(h) or (h < 0.0))
        h = 0.0;
    if (h > 1.0)
        h = 1.0;
    if (kind == 1)
        return (h < 0.6 ? 0.6 : h);
    if (T <= 0.0)
        return 0.0;
    return (h > 0.5 ? 0.5 : h);
}

// 6.2 a region's share term:  sqrt(iskShare / max iskShare)
inline double RegionShare(double iskShare, double maxShare)
{
    if (!(iskShare > 0.0) or !(maxShare > 0.0))
        return 0.0;
    double r(std::sqrt(iskShare / maxShare));
    return (r > 1.0 ? 1.0 : r);
}

// 6.3 P:  a hub's pull at 'jumps' jumps (-1 = no route:  0)
inline double HubProximity(double h, int32_t jumps, double falloff)
{
    if ((jumps < 0) or !(h > 0.0))
        return 0.0;
    if (!(falloff > 0.0))
        return (jumps == 0 ? h : 0.0);
    return h * std::exp(-static_cast<double>(jumps) / falloff);
}

// 6.3 S:  min(1, stations / 6)
inline double StationTerm(uint32_t stations)
{
    double s(static_cast<double>(stations) / 6.0);
    return (s > 1.0 ? 1.0 : s);
}

// 6.3 the system's busyness A from its market (H), hub proximity (P), region share (R) and station (S) terms
inline double Busyness(double H, double P, double R, double S)
{
    double a(0.04 + (0.46 * H) + (0.22 * P) + (0.12 * R) + (0.16 * S));
    if (!std::isfinite(a) or (a < 0.0))
        return 0.0;
    return (a > 1.0 ? 1.0 : a);
}

// 6.4 the day/night factor at hourUTC (0 - 24)
inline double Diurnal(double hourUTC, double amplitude, double peakHour)
{
    if (amplitude == 0.0)
        return 1.0;
    static constexpr double pi = 3.14159265358979323846;
    return 1.0 + (amplitude * std::cos(2.0 * pi * (hourUTC - peakHour) / 24.0));
}

// 6.4 a system's traffic events per minute
inline double TrafficRate(double A, double gamma, double rateAtHub, double floor, double secMult, double density, double diurnal)
{
    double base(rateAtHub * std::pow((A > 0.0 ? A : 0.0), gamma));
    if (base < floor)
        base = floor;
    double rate(base * secMult * density * diurnal);
    return ((std::isfinite(rate) and (rate > 0.0)) ? rate : 0.0);
}

// "never"
static constexpr int64_t Never = std::numeric_limits<int64_t>::max();

// 6.4 the time to the next event of a Poisson process of lambda per minute (ms, never under 250)
inline int64_t NextArrivalMs(NpcRng& rng, double lambdaPerMin)
{
    if (!(lambdaPerMin > 0.0) or !std::isfinite(lambdaPerMin))
        return Never;
    double u(rng.Uniform());
    double minutes(-std::log(1.0 - u) / lambdaPerMin);
    double ms(minutes * 60000.0);
    if (!(ms >= 250.0))
        ms = 250.0;
    if (ms > 1.0e15)
        ms = 1.0e15;
    return static_cast<int64_t>(ms);
}

// a Poisson count with this mean (Knuth below 30, normal above)
inline uint32_t PoissonCount(NpcRng& rng, double mean)
{
    if (!(mean > 0.0) or !std::isfinite(mean))
        return 0;
    if (mean < 30.0) {
        double limit(std::exp(-mean)), p(1.0);
        uint32_t k(0);
        do {
            ++k;
            p *= rng.Uniform();
        } while ((p > limit) and (k < 1000));
        return k - 1;
    }
    // Box-Muller
    static constexpr double twoPi = 6.28318530717958647692;
    double u1(rng.Uniform()), u2(rng.Uniform());
    if (u1 < 1e-300)
        u1 = 1e-300;
    double z(std::sqrt(-2.0 * std::log(u1)) * std::cos(twoPi * u2));
    double v(std::floor(mean + (std::sqrt(mean) * z) + 0.5));
    return (v > 0.0 ? static_cast<uint32_t>(v) : 0);
}

// uniform whole ms in [loMs, hiMs]
inline int64_t UniformMs(NpcRng& rng, double loMs, double hiMs)
{
    if (hiMs < loMs) {
        double t(loMs);
        loMs = hiMs;
        hiMs = t;
    }
    return static_cast<int64_t>(rng.Range(loMs, hiMs));
}

/* a pick weighted by weights (at or under 0 never picked).  npos when nothing can be picked */
static constexpr size_t npos = static_cast<size_t>(-1);
inline size_t PickWeighted(NpcRng& rng, const double* weights, size_t count)
{
    double total(0.0);
    for (size_t i = 0; i < count; ++i)
        if (weights[i] > 0.0)
            total += weights[i];
    if (!(total > 0.0))
        return npos;
    double point(rng.Uniform() * total);
    size_t last(npos);
    for (size_t i = 0; i < count; ++i) {
        if (!(weights[i] > 0.0))
            continue;
        last = i;
        if (point < weights[i])
            return i;
        point -= weights[i];
    }
    return last;
}
inline size_t PickWeighted(NpcRng& rng, const std::vector<double>& weights)
{
    return (weights.empty() ? npos : PickWeighted(rng, weights.data(), weights.size()));
}

/* 6.5 itinerary kinds */
namespace Kind {
    enum : uint8_t {
        Through     = 0,    // gate -> gate
        ArriveDock  = 1,    // gate -> station
        UndockLeave = 2,    // station -> gate
        StationHop  = 3,    // station -> station
        LocalRun    = 4,    // station -> planet or moon -> station
        Count       = 5
    };
}
typedef std::array<double, Kind::Count> KindArray;

// 6.5 the itinerary kinds' weights for a system with these gates and stations (normalised;  all 0 when none fits)
inline KindArray KindWeights(uint32_t gates, uint32_t stations, double H)
{
    KindArray w;
    w.fill(0.0);
    if (gates >= 2)
        w[Kind::Through] = 0.20 + (0.45 * (1.0 - H));
    if (stations >= 2)
        w[Kind::StationHop] = 0.05;
    if (stations > 0)
        w[Kind::LocalRun] = 0.02;
    if ((stations > 0) and (gates > 0)) {
        double rest((1.0 - w[Kind::Through] - w[Kind::StationHop] - w[Kind::LocalRun]) / 2.0);
        if (rest < 0.0)
            rest = 0.0;
        w[Kind::ArriveDock] = rest;
        w[Kind::UndockLeave] = rest;
    }
    double total(0.0);
    for (double v : w)
        total += v;
    if (total > 0.0)
        for (double& v : w)
            v /= total;
    return w;
}

// 6.5 a gate's traffic weight:  its destination's busyness, more toward a hub, less from null sec and wormholes
inline double GateWeight(double destA, bool towardHub, bool destNone)
{
    double w(0.3 + (destA > 0.0 ? destA : 0.0) + (towardHub ? 0.6 : 0.0));
    return (destNone ? w * 0.3 : w);
}

// 6.5 a station's traffic weight (h = its market node score, 0 for other stations)
inline double StationWeight(double h)
{
    return 1.0 + (6.0 * (h > 0.0 ? h : 0.0));
}

/* 6.5 one traffic event's itinerary:  indexes into the system's gate and station lists (npos = not used) */
struct Itinerary {
    uint8_t kind = Kind::Through;
    size_t entryGate = npos;
    size_t exitGate = npos;
    size_t stationA = npos;
    size_t stationB = npos;
};
// a weighted pick without the index 'exclude'
inline size_t PickExcept(NpcRng& rng, const std::vector<double>& weights, size_t exclude)
{
    if (exclude >= weights.size())
        return PickWeighted(rng, weights);
    std::vector<double> w(weights);
    w[exclude] = 0.0;
    return PickWeighted(rng, w);
}
// false when no itinerary fits (no kind with weight, or not enough gates/stations with weight)
inline bool PickItinerary(NpcRng& rng, const KindArray& kinds, const std::vector<double>& gateW,
                          const std::vector<double>& stationW, Itinerary& out)
{
    out = Itinerary();
    size_t k(PickWeighted(rng, kinds.data(), kinds.size()));
    if (k == npos)
        return false;
    out.kind = static_cast<uint8_t>(k);
    switch (out.kind) {
        case Kind::Through:
            out.entryGate = PickWeighted(rng, gateW);
            out.exitGate = PickExcept(rng, gateW, out.entryGate);
            return ((out.entryGate != npos) and (out.exitGate != npos));
        case Kind::ArriveDock:
            out.entryGate = PickWeighted(rng, gateW);
            out.stationA = PickWeighted(rng, stationW);
            return ((out.entryGate != npos) and (out.stationA != npos));
        case Kind::UndockLeave:
            out.stationA = PickWeighted(rng, stationW);
            out.exitGate = PickWeighted(rng, gateW);
            return ((out.stationA != npos) and (out.exitGate != npos));
        case Kind::StationHop:
            out.stationA = PickWeighted(rng, stationW);
            out.stationB = PickExcept(rng, stationW, out.stationA);
            return ((out.stationA != npos) and (out.stationB != npos));
        case Kind::LocalRun:
            out.stationA = PickWeighted(rng, stationW);
            return (out.stationA != npos);
    }
    return false;
}

/* 6.5 hull class of a traffic ship */
namespace Hull {
    enum : uint8_t {
        Hauler      = 0,
        Courier     = 1,
        Freighter   = 2,
        Navy        = 3,
        Count       = 4
    };
}
typedef std::array<double, Hull::Count> HullArray;
inline HullArray ClassWeights(uint8_t band, double H)
{
    HullArray w;
    if (band == Band::High) {
        w = {{0.55, 0.22, 0.08 * (H > 0.0 ? H : 0.0), 0.15}};
    } else if (band == Band::Low) {
        w = {{0.65, 0.30, 0.0, 0.05}};
    } else {
        w.fill(0.0);
        return w;
    }
    double total(0.0);
    for (double v : w)
        total += v;
    for (double& v : w)
        v /= total;
    return w;
}

/* 6.5 race (0 Amarr, 1 Caldari, 2 Gallente, 3 Minmatar):  the system's own empire (home, 255 = none) unless a foreigner
 *  (0.3 + 0.2 H), then any of the four */
inline uint8_t PickRace(NpcRng& rng, uint8_t home, double H)
{
    double foreign(0.3 + (0.2 * H));
    if ((home < 4) and !rng.Chance(foreign))
        return home;
    uint8_t r(static_cast<uint8_t>(rng.Uniform() * 4.0));
    return (r > 3 ? 3 : r);
}

/* 6.5 caps */
inline uint32_t GridCapFor(bool isHubGrid, uint32_t gridCap, uint32_t hubGridCap)
{
    return (isHubGrid ? hubGridCap : gridCap);
}
inline bool CapAllows(uint32_t gridCount, uint32_t gridCap, uint32_t sysCount, uint32_t sysCap, uint32_t globalCount,
                      uint32_t globalCap)
{
    return ((gridCount < gridCap) and (sysCount < sysCap) and (globalCount < globalCap));
}

/* 6.6 miners per observed belt */
inline double MinerTarget(double minersPerBelt, double A, uint8_t band, double lowMult, bool ice, double iceFactor,
                          double density, double diurnal)
{
    if (band == Band::None)
        return 0.0;
    double m(minersPerBelt * (0.4 + (0.6 * A)) * density * diurnal);
    if (band == Band::Low)
        m *= lowMult;
    if (ice)
        m *= iceFactor;
    return ((std::isfinite(m) and (m > 0.0)) ? m : 0.0);
}
// cycles until a miner is "full", uniform in [min, max]
inline uint32_t MinerCycles(NpcRng& rng, uint32_t lo, uint32_t hi)
{
    if (hi < lo) {
        uint32_t t(lo);
        lo = hi;
        hi = t;
    }
    uint32_t v(lo + static_cast<uint32_t>(rng.Uniform() * static_cast<double>(hi - lo + 1)));
    return (v > hi ? hi : v);
}
// 7.2 hull miner (Procurer, Retriever, Covetor, Hulk) weights
inline std::array<double, 4> MinerHullWeights(uint8_t band)
{
    if (band == Band::Low)
        return {{0.10, 0.45, 0.30, 0.15}};
    return {{0.30, 0.45, 0.20, 0.05}};
}

/* 6.7 patrols and criminals */
inline uint32_t ConcordSquads(uint8_t band, double A, float level, double base, uint32_t maxSquads, double density)
{
    if ((band != Band::High) or (maxSquads == 0))
        return 0;
    double n(base * (0.5 + (1.5 * A)) * (level >= 0.65f ? 1.0 : 0.6) * density);
    int64_t r(static_cast<int64_t>(std::floor(n + 0.5)));
    if (r < 1)
        r = 1;
    if (r > static_cast<int64_t>(maxSquads))
        r = maxSquads;
    return static_cast<uint32_t>(r);
}
inline uint32_t NavySquads(uint8_t band, bool hasNavy, double A, double base, double density)
{
    if ((band != Band::High) or !hasNavy)
        return 0;
    double n(base * (0.5 + A) * density);
    return (n > 0.0 ? static_cast<uint32_t>(std::floor(n + 0.5)) : 0);
}
// criminal events per hour (1.0 -> 0.1, 0.5 -> 1.1 at the default base)
inline double CriminalRatePerHour(uint8_t band, float level, double A, double highBase, double lowBase, double density)
{
    double r(0.0);
    if (band == Band::High) {
        r = highBase * 2.0 * (1.05 - static_cast<double>(level)) * density;
    } else if (band == Band::Low) {
        r = lowBase * (0.5 + A) * density;
    }
    return ((std::isfinite(r) and (r > 0.0)) ? r : 0.0);
}
/* do criminals go after players unprovoked (8.5)?  only while the crime rules are on (crime.Enabled, /crime on|off:  with them
 *  off pilots test in peace), and then as the toggle of the system's band says (ambient.HighSecCriminalsAttackPlayers,
 *  LowSecCriminalsAttackPlayers).  either way they hunt the ambient civilians and shoot back at whoever hurts them */
inline bool CriminalsAttackPlayers(bool crimeOn, bool highSec, bool highToggle, bool lowToggle)
{
    if (!crimeOn)
        return false;
    return (highSec ? highToggle : lowToggle);
}
// a criminal group's size:  1 (70%), 2 (25%), 3 (5%)
inline uint32_t CriminalGroupSize(NpcRng& rng)
{
    double u(rng.Uniform());
    return (u < 0.70 ? 1 : (u < 0.95 ? 2 : 3));
}
// a criminal's hull class:  0 frigate, 1 destroyer, 2 cruiser.  high:  80/20/0, low:  65/20/15
inline uint8_t CriminalClass(NpcRng& rng, uint8_t band)
{
    double u(rng.Uniform());
    if (band == Band::High)
        return (u < 0.80 ? 0 : 1);
    return (u < 0.65 ? 0 : (u < 0.85 ? 1 : 2));
}

/* 8 landing distances (m) by node kind */
namespace Node {
    enum : uint8_t {
        Gate    = 0,
        Station = 1,
        Belt    = 2,
        Planet  = 3,    // planet or moon
        Entity  = 4     // a ship or other entity (a patrol answering a crime)
    };
}
/* gate:  radius + 500 - 2500.  station:  2 - 12 km from the dock point.  belt:  5 - 25 km.  planet or moon:  10 - 40 km off
 *  its surface.  an entity:  5 - 15 km */
inline double LandingDistance(NpcRng& rng, uint8_t nodeKind, double radius)
{
    switch (nodeKind) {
        case Node::Gate:    return radius + rng.Range(500.0, 2500.0);
        case Node::Station: return rng.Range(2000.0, 12000.0);
        case Node::Belt:    return rng.Range(5000.0, 25000.0);
        case Node::Entity:  return rng.Range(5000.0, 15000.0);
        default:            return radius + rng.Range(10000.0, 40000.0);
    }
}

// 7.2 the warp factor (AU/s x 10) of a reference hull with warpSpeedMultiplier ref600 and baseWarpSpeed ref1281
inline int32_t WarpFactorFor(double ref600, double ref1281)
{
    return WarpSpeed::Factor(WarpSpeed::AUPerSecond(ref600, ref1281));
}

// 6.8 rule 3:  no invented arrival through a gate whose paired gate is watched in an active system
inline bool SuppressArrival(bool pairedObserved, bool pairedActive)
{
    return (pairedObserved and pairedActive);
}

// 6.8 rule 1:  gate jump to appearance in the next system, ms in [minS, maxS] s
inline int64_t TransitDelayMs(NpcRng& rng, uint32_t minS, uint32_t maxS)
{
    return UniformMs(rng, static_cast<double>(minS) * 1000.0, static_cast<double>(maxS) * 1000.0);
}

}   // namespace AmbientRules

#endif  // _EVE_NPC_AI_AMBIENTRULES_H_
