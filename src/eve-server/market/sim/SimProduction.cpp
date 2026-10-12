
 /**
  * @name SimProduction.cpp
  *   market simulation:  refining, manufacturing time, virtual capacity, the ore mix
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>
#include <cmath>

#include "market/sim/SimProduction.h"

namespace MktSim {
namespace Prod {

double RefineYield(double stationEfficiency)
{
    double eff(stationEfficiency + 0.375 * 1.1 * 1.2 * 1.25);
    return (eff > 1.0) ? 1.0 : ((eff < 0.0) ? 0.0 : eff);
}

void Refine(const TypeData& t, Units qty, double yield, double take, RefineResult& into)
{
    into.used = 0;
    into.gross.clear();
    into.tax.clear();
    Units portion((Units)std::max<uint32_t>(1, t.portionSize));
    Units batches(qty / portion);
    if ((batches <= 0) or t.refine.empty())
        return;
    into.used = batches * portion;
    for (auto& out : t.refine) {
        // t.refine is per unit:  a batch gives out.second x portionSize
        Units gross((Units)std::floor(out.second * (double)into.used * yield + 1e-9));
        if (gross <= 0)
            continue;
        Units tax((Units)std::llround((double)gross * take));
        into.gross.push_back(std::make_pair(out.first, gross));
        if (tax > 0)
            into.tax.push_back(std::make_pair(out.first, std::min(tax, gross)));
    }
}

double BuildHours(const TypeData& t, double timeMultiplier, double defaultSeconds)
{
    double perRun((t.buildSeconds > 0.0) ? t.buildSeconds : defaultSeconds);
    return perRun * timeMultiplier / 3600.0 / (double)std::max<uint32_t>(1, t.portionSize);
}

uint32_t VirtualCapacity::Slots(uint32_t actorID) const
{
    std::map<uint32_t, uint32_t>::const_iterator itr = m_slots.find(actorID);
    return (itr == m_slots.end()) ? 0 : itr->second;
}

uint32_t VirtualCapacity::Free(uint32_t actorID, uint32_t /*nodeID*/, uint8_t /*activity*/, int64_t /*now*/) const
{
    std::map<uint32_t, uint32_t>::const_iterator u = m_used.find(actorID);
    uint32_t used((u == m_used.end()) ? 0 : u->second), slots(Slots(actorID));
    return (slots > used) ? (slots - used) : 0;
}

bool VirtualCapacity::Reserve(SimJob& job, int64_t now)
{
    if (Free(job.actorID, job.nodeID, job.activity, now) == 0)
        return false;
    ++m_used[job.actorID];
    job.lineID = 0;
    return true;
}

void VirtualCapacity::Release(const SimJob& job)
{
    std::map<uint32_t, uint32_t>::iterator u = m_used.find(job.actorID);
    if ((u != m_used.end()) and (u->second > 0))
        --u->second;
}

double VirtualCapacity::JobCost(const SimJob& job) const
{
    double hours((double)(job.end - job.start) / 3600.0);
    return m_install + m_perHour * std::max(0.0, hours);
}

std::vector<double> FitMix(const std::vector<std::vector<double>>& y, const std::vector<double>& need,
                           std::vector<double> x, int iterations)
{
    size_t ores(y.size()), mins(need.size());
    // A[m][o] = y[o][m] / need[m], b = 1:  minimise |A x - 1|^2 over x >= 0 (Lee and Seung's multiplicative update,
    //  x <- x (A'b) / (A'A x), which keeps x >= 0 and never increases the error)
    std::vector<double> atb(ores, 0.0);
    for (size_t o = 0; o < ores; ++o)
        for (size_t m = 0; m < mins; ++m)
            if (need[m] > 0.0)
                atb[o] += y[o][m] / need[m];
    std::vector<double> ax(mins, 0.0);
    for (int it = 0; it < iterations; ++it) {
        for (size_t m = 0; m < mins; ++m) {
            ax[m] = 0.0;
            if (!(need[m] > 0.0))
                continue;
            for (size_t o = 0; o < ores; ++o)
                ax[m] += y[o][m] / need[m] * x[o];
        }
        for (size_t o = 0; o < ores; ++o) {
            if (!(x[o] > 0.0))
                continue;
            double atax(0.0);
            for (size_t m = 0; m < mins; ++m)
                if (need[m] > 0.0)
                    atax += y[o][m] / need[m] * ax[m];
            x[o] = (atax > 0.0) ? x[o] * atb[o] / atax : 0.0;
        }
    }
    return x;
}

}
}
