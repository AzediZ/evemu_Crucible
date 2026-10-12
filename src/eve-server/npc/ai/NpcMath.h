 /**
  * @name NpcMath.h
  *   pure formulas shared by players, npcs, drones, sentries and starbase batteries
  *   (doc/code_and_design_notes/npc-ai/PLAN.md section 5)
  *
  *   no server dependencies:  every input is a parameter, and random draws are passed in (u in [0,1)),
  *   so the results are deterministic.  src/eve-test/npcai/NpcMathTest.cpp checks the plan's worked values.
  *   nothing here returns NaN;  bad inputs give a miss (0), no damage reduction (1) or the clamp bounds.
  */

#ifndef _EVE_NPC_AI_NPCMATH_H_
#define _EVE_NPC_AI_NPCMATH_H_

#include <cstddef>
#include <cstdint>

namespace NpcMath {

struct Vec3 {
    double x, y, z;
};

/* turret geometry between a shooter and its target (positions in m, velocities in m/s, radii in m)
 *  centre:     |p_t - p_s|
 *  surface:    max(0, centre - r_s - r_t)  (the range term uses this)
 *  transversal: the part of the relative velocity across the line of sight (the radial part is ignored)
 *  angular:    transversal / centre (rad/s), 0 when the centres coincide
 */
struct TurretGeometry {
    double centre;
    double surface;
    double transversal;
    double angular;
};
TurretGeometry Geometry(const Vec3& shooterPos, const Vec3& shooterVel, double shooterRadius,
                        const Vec3& targetPos, const Vec3& targetVel, double targetRadius);

struct TurretInputs {
    double angular;         // rad/s
    double surface;         // m, surface distance
    double optimal;         // m (already scaled by tracking disruptors on the shooter)
    double falloff;         // m (npc tracking disruptors don't touch it in Crucible)
    double tracking;        // rad/s (already scaled by tracking disruptors)
    double sigRes;          // m, the turret's optimalSigRadius
    double targetSig;       // m, including painters and the npc mwd bloom.  <= 0 uses targetRadius
    double targetRadius;    // m
};
struct TurretTerms {
    double track;           // (angular / tracking) * (sigRes / sig)
    double range;           // max(0, surface - optimal) / falloff
    double chance;          // 0.5 ^ (track^2 + range^2)
};
/* the standard (player) turret formula.  signature resolution is inside the roll:  there is no separate damage reduction
 *  for big guns on small ships.  guards:  tracking <= 0 hits only a target with no angular velocity, falloff <= 0 hits only
 *  inside optimal, a missing signature uses the radius.  terms (optional) receives the parts for trace logs */
double TurretChanceToHit(const TurretInputs& in, TurretTerms* terms = nullptr);
/* the shot for draw u in [0,1):  0 = miss (u >= chance), 3.0 = wrecking (u < wreckingChance, only inside a hit),
 *  else the quality multiplier u + 0.49 (0.49 .. 1.49) */
double ResolveTurretShot(double chance, double u, double wreckingChance);

/* missiles (5.2)
 *  explosion factor = min(1, S/E, ((S/E) * (Ve/V)) ^ (ln DRF / ln DRS))
 *   S target signature, E explosion radius, Ve explosion velocity, V target speed (<= 0 counts as 1).
 *   no explosion radius = full damage.  unusable DRF/DRS drop the velocity term */
double MissileDamageFactor(double sig, double aoeCloud, double aoeVelocity, double targetSpeed, double drf, double drs);
// flight range in m:  velocity (m/s) x flight time (ms) / 1000
double MissileRange(double velocity, double flightTimeMs);

/* lock time in ms:  40,000,000 / (scanResolution * asinh(sig)^2), clamped to [minMs, maxMs].  scan resolution and
 *  signature below 1 count as 1.  players use the same formula without the clamp (pass 0 / 0 to disable it) */
double LockTimeMs(double scanResolution, double sig, double minMs = 1000.0, double maxMs = 180000.0);

/* reaction delay before the first weapon or ewar activation (5.6), ms:
 *   U(min, max) x reactionFactor x scale, with u the draw.  min and max both 0 = no delay */
double ReactionDelayMs(double minMs, double maxMs, double reactionFactor, double scale, double u);

/* one phase of the npc chase cycle (5.5):  the full maxMs with chanceOfMax (draw u1), else U(0, maxMs) (draw u2) */
double ChasePhaseMs(double maxMs, double chanceOfMax, double u1, double u2);

/* level-one target score (5.7):  (1 + threat) x S_sig x S_drone x S_range x S_lock */
struct L1ScoreInputs {
    double threat;
    double targetSig;
    double preferredSig;        // 1655 if > 0, else the npc's own signature
    bool useSigRadius;          // 1650 > 0 (and 1648 > 0)
    bool droneRecentDamager;    // a drone that damaged us in the last 30s
    bool inAttackRange;         // within R_atk
    bool inAcquireRange;        // within R_acq
    bool locked;                // already locked
};
double L1Score(const L1ScoreInputs& in);
/* the level-one switch rule:  best beats primary by margin, the switch timer has passed, and the draw u >= 1651 */
bool L1ShouldSwitch(double bestScore, double primaryScore, double margin, int64_t nowMs, int64_t lastSwitchMs,
                    int64_t switchTimerMs, double u, double chanceToNotSwitch);

/* ewar activation chance past optimal (surface distance):  1 inside optimal, 0.5 ^ (((d - optimal) / falloff)^2) past it,
 *  0 past optimal without falloff */
double EwarRangeChance(double surface, double optimal, double falloff);

/* stacking penalty of the i-th strongest effect (0 based):  0.5 ^ ((i / 2.22292081)^2) */
double StackingFactor(size_t index);
/* the stacking-penalised product of speed/tracking/range multipliers (e.g. 0.4 for a 60% web).  the strongest
 *  (furthest from 1) counts in full, the next ones less.  works on a copy, at most 32 multipliers are used */
double StackedProduct(const double* multipliers, size_t count);

/* remote ecm burst interval (5.8), ms:  clamp(1661 x 1660 ^ max(0, (players - 1663) / 1662), 1659, 1661) */
double RemoteEcmDurationMs(double maxDurationMs, double durationMultiplier, double players, double playerThreshold,
                           double playerScale, double minDurationMs);

/* damage per second of a turret:  total base damage x damage multiplier / cycle seconds */
double TurretDps(double totalDamage, double damageMultiplier, double cycleMs);

/* npc movement mode by surface distance to the primary (5.5):
 *  d <= 1.5 x orbit range:  orbit at the orbit range (entityFlyRange) at cruise speed
 *  d <= boost range:        follow at the orbit range at cruise speed
 *  further:                 follow plus the chase cycle (stall at cruise speed, boost at maxVelocity) */
namespace Move {
    enum : uint8_t { None = 0, Orbit = 1, Follow = 2, Chase = 3 };
}
uint8_t MoveMode(double distance, double orbitRange, double boostRange);

/* the data leash (5.4):  Hard past wanderRange x hardScale (or hardOverride when > 0), Soft past wanderRange x softScale */
namespace Leash {
    enum : uint8_t { None = 0, Soft = 1, Hard = 2 };
}
uint8_t LeashState(double distanceFromHome, double wanderRange, double softScale, double hardScale, double hardOverride = 0.0);

/* self-repair (5.9):  the chance a cycle is skipped, the effect's delay chance x its multiplier (1015-1020), 0..1 */
double RepairSkipChance(double delayChance, double multiplier);

}   // namespace NpcMath

#endif  // _EVE_NPC_AI_NPCMATH_H_
