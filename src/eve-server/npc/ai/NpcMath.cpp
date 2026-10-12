 /**
  * @name NpcMath.cpp
  *   pure formulas shared by players, npcs, drones, sentries and starbase batteries
  *   (doc/code_and_design_notes/npc-ai/PLAN.md section 5).  see NpcMath.h
  *
  *   this file is also compiled into eve-test, so it includes nothing from the server
  */

#include "NpcMath.h"

#include <algorithm>
#include <cmath>

namespace NpcMath {

namespace {
    inline bool Finite(double v)                        { return std::isfinite(v); }
}

TurretGeometry Geometry(const Vec3& shooterPos, const Vec3& shooterVel, double shooterRadius,
                        const Vec3& targetPos, const Vec3& targetVel, double targetRadius)
{
    TurretGeometry g = TurretGeometry();
    double dx(targetPos.x - shooterPos.x), dy(targetPos.y - shooterPos.y), dz(targetPos.z - shooterPos.z);
    g.centre = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!Finite(g.centre))
        g.centre = 0.0;
    g.surface = std::max(0.0, g.centre - std::max(0.0, shooterRadius) - std::max(0.0, targetRadius));
    if (g.centre <= 0.0)
        return g;   // same spot:  no line of sight, no angular velocity

    double ux(dx / g.centre), uy(dy / g.centre), uz(dz / g.centre);
    double vx(targetVel.x - shooterVel.x), vy(targetVel.y - shooterVel.y), vz(targetVel.z - shooterVel.z);
    double radial(vx * ux + vy * uy + vz * uz);
    double tx(vx - radial * ux), ty(vy - radial * uy), tz(vz - radial * uz);
    g.transversal = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (!Finite(g.transversal))
        g.transversal = 0.0;
    g.angular = g.transversal / g.centre;
    return g;
}

double TurretChanceToHit(const TurretInputs& in, TurretTerms* terms/*nullptr*/)
{
    TurretTerms t = TurretTerms();
    if (terms != nullptr)
        *terms = t;

    double angular(Finite(in.angular) ? std::max(0.0, in.angular) : 0.0);
    double surface(Finite(in.surface) ? std::max(0.0, in.surface) : 0.0);
    double optimal(Finite(in.optimal) ? std::max(0.0, in.optimal) : 0.0);

    // tracking
    if (!Finite(in.tracking) or (in.tracking <= 0.0)) {
        if (angular > 0.0)
            return 0.0;     // can't track anything that moves across our sights
        t.track = 0.0;
    } else {
        double sig(in.targetSig);
        if (!Finite(sig) or (sig <= 0.0))
            sig = in.targetRadius;
        if (!Finite(sig) or (sig < 1.0))
            sig = 1.0;
        double sigRes(Finite(in.sigRes) ? std::max(0.0, in.sigRes) : 0.0);
        t.track = (angular / in.tracking) * (sigRes / sig);
    }

    // range
    double outside(surface - optimal);
    if (outside > 0.0) {
        if (!Finite(in.falloff) or (in.falloff <= 0.0))
            return 0.0;     // no falloff:  nothing past optimal
        t.range = outside / in.falloff;
    }

    t.chance = std::pow(0.5, (t.track * t.track) + (t.range * t.range));
    if (!Finite(t.chance))
        t.chance = 0.0;
    if (terms != nullptr)
        *terms = t;
    return t.chance;
}

double ResolveTurretShot(double chance, double u, double wreckingChance)
{
    if (!Finite(chance) or !Finite(u) or (u >= chance))
        return 0.0;
    if (u < wreckingChance)
        return 3.0;
    return u + 0.49;
}

double MissileDamageFactor(double sig, double aoeCloud, double aoeVelocity, double targetSpeed, double drf, double drs)
{
    if (!Finite(aoeCloud) or (aoeCloud <= 0.0))
        return 1.0;
    if (!Finite(sig) or (sig <= 0.0))
        return 0.0;
    double v(targetSpeed);
    if (!Finite(v) or (v <= 0.0))
        v = 1.0;
    double sizeTerm(sig / aoeCloud);
    double factor(std::min(1.0, sizeTerm));
    // the velocity term needs a usable reduction factor and sensitivity
    if (Finite(drf) and Finite(drs) and (drf > 0.0) and (drs > 0.0) and (drs != 1.0) and Finite(aoeVelocity) and (aoeVelocity > 0.0)) {
        double exponent(std::log(drf) / std::log(drs));
        double speedTerm(std::pow(sizeTerm * (aoeVelocity / v), exponent));
        if (Finite(speedTerm))
            factor = std::min(factor, speedTerm);
    }
    if (!Finite(factor) or (factor < 0.0))
        return 0.0;
    return factor;
}

double MissileRange(double velocity, double flightTimeMs)
{
    if (!Finite(velocity) or !Finite(flightTimeMs) or (velocity <= 0.0) or (flightTimeMs <= 0.0))
        return 0.0;
    return velocity * flightTimeMs / 1000.0;
}

double LockTimeMs(double scanResolution, double sig, double minMs/*1000*/, double maxMs/*180000*/)
{
    if (!Finite(scanResolution) or (scanResolution < 1.0))
        scanResolution = 1.0;
    if (!Finite(sig) or (sig < 1.0))
        sig = 1.0;
    // asinh(sig), written out:  eve-compat.h maps asinh to boost on some platforms
    double a(std::log(sig + std::sqrt((sig * sig) + 1.0)));
    double time(40000000.0 / (scanResolution * a * a));
    if (!Finite(time))
        time = (maxMs > 0.0 ? maxMs : 180000.0);
    if ((minMs > 0.0) and (time < minMs))
        time = minMs;
    if ((maxMs > 0.0) and (time > maxMs))
        time = maxMs;
    return time;
}

double ReactionDelayMs(double minMs, double maxMs, double reactionFactor, double scale, double u)
{
    if (!Finite(minMs) or (minMs < 0.0))
        minMs = 0.0;
    if (!Finite(maxMs) or (maxMs < minMs))
        maxMs = minMs;
    if (maxMs <= 0.0)
        return 0.0;
    if (!Finite(u))
        u = 0.0;
    u = std::min(1.0, std::max(0.0, u));
    double delay((minMs + (u * (maxMs - minMs))) * reactionFactor * scale);
    if (!Finite(delay) or (delay < 0.0))
        return 0.0;
    return delay;
}

double ChasePhaseMs(double maxMs, double chanceOfMax, double u1, double u2)
{
    if (!Finite(maxMs) or (maxMs <= 0.0))
        return 0.0;
    if (Finite(u1) and (u1 < chanceOfMax))
        return maxMs;
    if (!Finite(u2))
        u2 = 0.0;
    return std::min(1.0, std::max(0.0, u2)) * maxMs;
}

double L1Score(const L1ScoreInputs& in)
{
    if (!in.inAcquireRange)
        return 0.0;
    double threat(Finite(in.threat) ? std::max(0.0, in.threat) : 0.0);
    double score(1.0 + threat);
    // targets smaller than we like are worth less (larger ones are never penalised in Crucible)
    if (in.useSigRadius and Finite(in.preferredSig) and (in.preferredSig > 0.0) and Finite(in.targetSig)
    and (in.targetSig < in.preferredSig))
        score *= std::max(0.25, std::sqrt(std::max(0.0, in.targetSig) / in.preferredSig));
    if (in.droneRecentDamager)
        score *= 1.25;
    if (!in.inAttackRange)
        score *= 0.5;
    if (in.locked)
        score *= 1.1;
    return score;
}

bool L1ShouldSwitch(double bestScore, double primaryScore, double margin, int64_t nowMs, int64_t lastSwitchMs,
                    int64_t switchTimerMs, double u, double chanceToNotSwitch)
{
    if (!Finite(bestScore) or !Finite(primaryScore))
        return false;
    if (bestScore <= (margin * primaryScore))
        return false;
    if ((nowMs - lastSwitchMs) < switchTimerMs)
        return false;
    return (u >= chanceToNotSwitch);
}

double EwarRangeChance(double surface, double optimal, double falloff)
{
    if (!Finite(surface) or (surface <= optimal))
        return 1.0;
    if (!Finite(falloff) or (falloff <= 0.0))
        return 0.0;
    double x((surface - optimal) / falloff);
    return std::pow(0.5, x * x);
}

double StackingFactor(size_t index)
{
    double x(static_cast<double>(index) / 2.22292081);
    return std::pow(0.5, x * x);
}

double StackedProduct(const double* multipliers, size_t count)
{
    if ((multipliers == nullptr) or (count == 0))
        return 1.0;
    const size_t maxCount(32);
    double sorted[maxCount];
    size_t n(0);
    for (size_t i = 0; (i < count) and (n < maxCount); ++i)
        if (Finite(multipliers[i]))
            sorted[n++] = multipliers[i];
    // strongest first:  furthest from no change
    std::sort(sorted, sorted + n, [](double a, double b) { return std::fabs(a - 1.0) > std::fabs(b - 1.0); });
    double product(1.0);
    for (size_t i = 0; i < n; ++i)
        product *= (1.0 + ((sorted[i] - 1.0) * StackingFactor(i)));
    return std::max(0.0, product);
}

double RemoteEcmDurationMs(double maxDurationMs, double durationMultiplier, double players, double playerThreshold,
                           double playerScale, double minDurationMs)
{
    double exponent(0.0);
    if (Finite(playerScale) and (playerScale > 0.0) and Finite(players))
        exponent = std::max(0.0, (players - playerThreshold) / playerScale);
    double duration(maxDurationMs * std::pow(durationMultiplier, exponent));
    if (!Finite(duration))
        duration = maxDurationMs;
    double lo(std::min(minDurationMs, maxDurationMs)), hi(std::max(minDurationMs, maxDurationMs));
    return std::min(hi, std::max(lo, duration));
}

double TurretDps(double totalDamage, double damageMultiplier, double cycleMs)
{
    if (!Finite(cycleMs) or (cycleMs <= 0.0))
        return 0.0;
    return totalDamage * damageMultiplier / (cycleMs / 1000.0);
}

}   // namespace NpcMath
