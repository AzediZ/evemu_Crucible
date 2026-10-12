
 /**
  * @name SimLogistics.cpp
  *   market simulation:  routes and route risk
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>
#include <cmath>
#include <queue>

#include "market/sim/SimLogistics.h"

namespace MktSim {

uint8_t Logi::Band(float security)
{
    if (security >= 0.45f)
        return SecBand::High;
    if (security > 0.0f)
        return SecBand::Low;
    return SecBand::Null;
}

double Logi::JumpLoss(const RiskParams& risk, uint8_t band, double value, double hullFactor)
{
    double p(0.0);
    switch (band) {
        case SecBand::High:
            // suicide ganks follow the cargo's value (PLAN 6.5)
            p = (value > 0.0) ? risk.highSec * std::pow(value / RouteRefValue, risk.highSecBeta) : 0.0;
            break;
        case SecBand::Low:
            p = risk.lowSec;
            break;
        default:
            p = risk.nullSec;
            break;
    }
    p *= std::max(0.0, hullFactor);
    return std::min(1.0, std::max(0.0, p));
}

double Logi::TripLoss(const RiskParams& risk, const uint16_t bandJumps[SecBand::Count], double value, double hullFactor)
{
    double survive(1.0);
    for (uint8_t b = 0; b < SecBand::Count; ++b)
        if (bandJumps[b] > 0)
            survive *= std::pow(1.0 - JumpLoss(risk, b, value, hullFactor), (double)bandJumps[b]);
    return std::min(1.0, std::max(0.0, 1.0 - survive));
}

double Logi::PackagedVolume(const TypeData& t)
{
    if (t.categoryID != Category::Ship)
        return t.volume;
    // the volumes the client hard-codes per ship group (InventoryItem::GetPackagedVolume)
    switch (t.groupID) {
        case 29: case 31: case 1022:                            return 500.0;
        case 25: case 324: case 830: case 893: case 831: case 834:  return 2500.0;
        case 543: case 463:                                     return 3750.0;
        case 541: case 420: case 963:                           return 5000.0;
        case 906: case 26: case 833: case 358: case 894: case 832:  return 10000.0;
        case 419: case 540:                                     return 15000.0;
        case 28: case 380:                                      return 20000.0;
        case 27: case 900: case 898: case 381:                  return 50000.0;
        case 941:                                               return 500000.0;
        case 883: case 547: case 485: case 513: case 902: case 659: return 1000000.0;
        case 30:                                                return 10000000.0;
    }
    return t.volume;
}

void RouteMap::Build(const StaticData& data, const RiskParams& risk, double riskScale)
{
    m_ids.clear();
    m_index.clear();
    m_adj.clear();
    m_band.clear();
    m_trees.clear();
    m_cache.clear();
    for (auto& cur : data.systems) {
        m_index[cur.first] = (uint32_t)m_ids.size();
        m_ids.push_back(cur.first);
        m_band.push_back(Logi::Band(cur.second.security));
    }
    m_adj.assign(m_ids.size(), std::vector<uint32_t>());
    for (auto& j : data.jumps) {
        std::unordered_map<uint32_t, uint32_t>::const_iterator a = m_index.find(j.first), b = m_index.find(j.second);
        if ((a == m_index.end()) or (b == m_index.end()) or (a->second == b->second))
            continue;
        m_adj[a->second].push_back(b->second);
        m_adj[b->second].push_back(a->second);
    }
    for (auto& adj : m_adj) {
        std::sort(adj.begin(), adj.end());
        adj.erase(std::unique(adj.begin(), adj.end()), adj.end());
    }
    for (int p = 0; p < Logi::Profiles; ++p) {
        m_enter[p].assign(m_ids.size(), 1.0);
        for (size_t i = 0; i < m_ids.size(); ++i) {
            double pj(Logi::JumpLoss(risk, m_band[i], Logi::RouteRefValue));
            double r((pj < 1.0) ? -std::log(1.0 - pj) : 50.0);
            m_enter[p][i] = 1.0 + Logi::ProfileLambda[p] * std::max(0.0, riskScale) * r;
        }
    }
}

size_t RouteMap::Edges() const
{
    size_t n(0);
    for (auto& adj : m_adj)
        n += adj.size();
    return n / 2;
}

uint8_t RouteMap::BandOf(uint32_t systemID) const
{
    std::unordered_map<uint32_t, uint32_t>::const_iterator itr = m_index.find(systemID);
    return (itr == m_index.end()) ? (uint8_t)SecBand::Null : m_band[itr->second];
}

const std::vector<int32_t>& RouteMap::Tree(uint32_t fromIdx, uint8_t profile)
{
    std::pair<uint32_t, uint8_t> key(fromIdx, profile);
    std::map<std::pair<uint32_t, uint8_t>, std::vector<int32_t>>::iterator itr = m_trees.find(key);
    if (itr != m_trees.end())
        return itr->second;
    size_t n(m_ids.size());
    std::vector<double> dist(n, 1e300);
    std::vector<int32_t> prev(n, -1);
    std::vector<uint8_t> done(n, 0);
    const std::vector<double>& enter(m_enter[profile % Logi::Profiles]);
    typedef std::pair<double, uint32_t> QE;
    // ties go to the lower index, the neighbours are sorted:  deterministic
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    dist[fromIdx] = 0.0;
    q.push(QE(0.0, fromIdx));
    while (!q.empty()) {
        QE top(q.top());
        q.pop();
        uint32_t u(top.second);
        if (done[u])
            continue;
        done[u] = 1;
        for (auto v : m_adj[u]) {
            double d(dist[u] + enter[v]);
            if (d < dist[v]) {
                dist[v] = d;
                prev[v] = (int32_t)u;
                q.push(QE(d, v));
            }
        }
    }
    prev[fromIdx] = (int32_t)fromIdx;
    return m_trees.emplace(key, prev).first->second;
}

const Route& RouteMap::Get(uint32_t fromSystem, uint32_t toSystem, uint8_t profile)
{
    profile = (uint8_t)(profile % Logi::Profiles);
    std::tuple<uint32_t, uint32_t, uint8_t> key(fromSystem, toSystem, profile);
    std::map<std::tuple<uint32_t, uint32_t, uint8_t>, Route>::iterator itr = m_cache.find(key);
    if (itr != m_cache.end())
        return itr->second;
    Route r;
    r.profile = profile;
    std::unordered_map<uint32_t, uint32_t>::const_iterator a = m_index.find(fromSystem), b = m_index.find(toSystem);
    if ((a != m_index.end()) and (b != m_index.end())) {
        if (a->second == b->second) {
            r.ok = true;
        } else {
            const std::vector<int32_t>& prev(Tree(a->second, profile));
            if (prev[b->second] >= 0) {
                r.ok = true;
                uint32_t v(b->second);
                while ((v != a->second) and (r.jumps < 60000)) {
                    ++r.jumps;
                    ++r.bandJumps[std::min<uint8_t>(m_band[v], SecBand::Count - 1)];
                    v = (uint32_t)prev[v];
                }
                // and the system it leaves from:  a cargo undocks and takes the gate there too (a low-sec site next to
                //  high-sec is still a low-sec system)
                ++r.bandJumps[std::min<uint8_t>(m_band[a->second], SecBand::Count - 1)];
            }
        }
    }
    return m_cache.emplace(key, r).first->second;
}

double RouteMap::Cost(const Route& r, double m3, double value, double freightPerM3Jump, const RiskParams& risk)
{
    if (!r.ok)
        return 1e300;
    return freightPerM3Jump * m3 * (double)r.jumps + Logi::TripLoss(risk, r.bandJumps, value) * value;
}

const Route& RouteMap::Best(uint32_t fromSystem, uint32_t toSystem, double m3, double value, double freightPerM3Jump, const RiskParams& risk)
{
    const Route* best(&m_none);
    double cost(1e300);
    for (uint8_t p = 0; p < Logi::Profiles; ++p) {
        const Route& r(Get(fromSystem, toSystem, p));
        double c(Cost(r, m3, value, freightPerM3Jump, risk));
        if (r.ok and (c < cost)) {
            cost = c;
            best = &r;
        }
    }
    return *best;
}

uint32_t RouteMap::Nearest(uint32_t fromSystem, uint8_t band, const std::vector<std::pair<uint32_t, uint32_t>>& candidates, uint16_t* jumps)
{
    uint32_t best(0);
    uint16_t bestJumps(0xFFFF);
    for (auto& cand : candidates) {
        if (BandOf(cand.second) != band)
            continue;
        const Route& r(Get(fromSystem, cand.second, 0));
        if (!r.ok)
            continue;
        if ((r.jumps < bestJumps) or ((r.jumps == bestJumps) and (cand.first < best))) {
            best = cand.first;
            bestJumps = r.jumps;
        }
    }
    if (jumps != nullptr)
        *jumps = (best != 0) ? bestJumps : 0;
    return best;
}

}
