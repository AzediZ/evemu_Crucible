#!/usr/bin/env python3
"""check_shots.py - the npc ai fairness audit (doc/code_and_design_notes/planning/npc-ai/PLAN.md phase 5, section 5.1).

Every turret shot of an npc or sentry is logged on DAMAGE__TRACE_NPC (players', drones' and starbases' on DAMAGE__TRACE)
with every term that decided it (ship/modules/TurretFormulas.cpp, TurretFormulas::Roll):

    NPC::ToHit <shooter>(<id>) -> <target>(<id>):  surface 9500m (optimal 6000, falloff 3000), transversal 210.0m/s,
      angular 0.0221053 (tracking 0.0450000), sigRes 125 vs sig 400.0 -> track 0.1535, range 1.1667, chance 0.38393,
      u 0.21788 -> 0.708 (hit)

This script reads a sample of those lines (the last --sample, default 1000) and checks them against the shared formula
(npc/ai/NpcMath.cpp, ported below):

  1. terms     track = (angular / tracking) * (sigRes / sig), range = max(0, surface - optimal) / falloff, recomputed
               from the logged inputs (within their rounding);  chance = 0.5 ^ (track^2 + range^2) from the logged terms
  2. outcome   a hit exactly when u < chance;  a hit is a wrecking shot (3.0) when u < the wrecking chance
               (rates.WreckingChance, --wrecking), else quality u + 0.49
  3. totals    hits against the sum of the chances (a z-score over the sample), hit rates by chance band, the draws u
               uniform on [0, 1) (chi-square over 10 bins), wrecking shots against their expectation

Usage:
  python -I check_shots.py <server log> [--sample N] [--who NPC,Sentry] [--wrecking 0.01] [--verbose]
  python -I check_shots.py --selftest

Enable DAMAGE__TRACE_NPC=1 in log.ini first.  Exit status 0 = everything checks out, 1 = a check failed, 2 = no shots.
stdlib only, deterministic.
"""

import argparse
import math
import random
import re
import sys

LINE = re.compile(
    r"(?P<who>\w+)::ToHit (?P<src>.*?)\((?P<srcid>\d+)\) -> (?P<dst>.*?)\((?P<dstid>\d+)\):\s+"
    r"surface (?P<surface>[-\d.]+)m \(optimal (?P<optimal>[-\d.]+), falloff (?P<falloff>[-\d.]+)\), "
    r"transversal (?P<transversal>[-\d.]+)m/s, angular (?P<angular>[-\d.e+]+) \(tracking (?P<tracking>[-\d.e+]+)\), "
    r"sigRes (?P<sigres>[-\d.]+) vs sig (?P<sig>[-\d.]+) -> track (?P<track>[-\d.]+), range (?P<range>[-\d.]+), "
    r"chance (?P<chance>[-\d.]+), u (?P<u>[-\d.]+) -> (?P<shot>[-\d.]+) \((?P<result>\w+)\)")

FLOATS = ("surface", "optimal", "falloff", "transversal", "angular", "tracking", "sigres", "sig", "track", "range",
          "chance", "u", "shot")


# --- the formula (npc/ai/NpcMath.cpp TurretChanceToHit / ResolveTurretShot) -------------------------------------------
def terms(angular, surface, optimal, falloff, tracking, sig_res, sig, radius=0.0):
    """(track, range, chance) as NpcMath::TurretChanceToHit computes them"""
    angular = max(0.0, angular)
    surface = max(0.0, surface)
    optimal = max(0.0, optimal)
    track = 0.0
    if tracking <= 0.0:
        if angular > 0.0:
            return (0.0, 0.0, 0.0)
    else:
        if sig <= 0.0:
            sig = radius
        if sig < 1.0:
            sig = 1.0
        track = (angular / tracking) * (max(0.0, sig_res) / sig)
    rng = 0.0
    outside = surface - optimal
    if outside > 0.0:
        if falloff <= 0.0:
            return (track, 0.0, 0.0)
        rng = outside / falloff
    return (track, rng, math.pow(0.5, track * track + rng * rng))


def resolve(chance, u, wrecking):
    """the shot for draw u:  0 miss, 3.0 wrecking, else u + 0.49"""
    if u >= chance:
        return 0.0
    if u < wrecking:
        return 3.0
    return u + 0.49


# --- reading ----------------------------------------------------------------------------------------------------------
def parse(lines, who):
    shots = []
    for line in lines:
        m = LINE.search(line)
        if m is None:
            continue
        if who and (m.group("who") not in who):
            continue
        rec = {k: float(m.group(k)) for k in FLOATS}
        rec["who"] = m.group("who")
        rec["result"] = m.group("result")
        rec["text"] = line.strip()
        shots.append(rec)
    return shots


# --- the checks -------------------------------------------------------------------------------------------------------
def check_shot(s, wrecking):
    """the problems of one shot (an empty list when it checks out)"""
    problems = []
    # 1. terms from the logged inputs.  inputs are rounded:  surface/optimal/falloff to 1m, angular/tracking to 7 decimals,
    #    sigRes to 1m, sig to 0.1m;  the terms to 4 decimals, chance and u to 5
    track, rng, _ = terms(s["angular"], s["surface"], s["optimal"], s["falloff"], s["tracking"], s["sigres"], s["sig"])
    if (s["sig"] > 0.0) and (s["tracking"] > 0.0):
        rel = 0.5e-7 / max(s["angular"], 1e-12) + 0.5e-7 / s["tracking"] + 0.5 / max(s["sigres"], 1.0) + 0.05 / max(s["sig"], 1.0)
        tol = 5e-5 + track * rel + 1e-6
        if abs(track - s["track"]) > tol:
            problems.append("track %.4f, the inputs give %.6f" % (s["track"], track))
    if s["falloff"] > 0.0:
        tol = 5e-5 + 1.5 / s["falloff"] + rng * (0.5 / s["falloff"])
        if abs(rng - s["range"]) > tol:
            problems.append("range %.4f, the inputs give %.6f" % (s["range"], rng))
    if s["chance"] > 0.0:
        chance = math.pow(0.5, s["track"] ** 2 + s["range"] ** 2)
        # the terms' own rounding (5e-5 each) moves the chance by at most this
        tol = 5e-6 + chance * math.log(2) * 2 * (abs(s["track"]) + abs(s["range"]) + 1e-4) * 5e-5 + 1e-6
        if abs(chance - s["chance"]) > tol:
            problems.append("chance %.5f, the terms give %.6f" % (s["chance"], chance))
    # 2. the outcome from chance and u (both to 5 decimals:  too close to call is skipped)
    if abs(s["u"] - s["chance"]) > 1e-5:
        hit = s["u"] < s["chance"]
        if hit != (s["shot"] > 0.0):
            problems.append("u %.5f vs chance %.5f should be a %s" % (s["u"], s["chance"], "hit" if hit else "miss"))
        elif hit and (abs(s["u"] - wrecking) > 1e-5):
            want = 3.0 if s["u"] < wrecking else s["u"] + 0.49
            if abs(want - s["shot"]) > 1.5e-3:
                problems.append("quality %.3f, the formula gives %.3f" % (s["shot"], want))
    label = "miss" if s["shot"] <= 0.0 else ("wrecking" if s["shot"] >= 3.0 else "hit")
    if label != s["result"]:
        problems.append("labelled %s, the value is a %s" % (s["result"], label))
    return problems


def totals(shots, wrecking, out):
    """the sample against its expectation.  returns the number of failed totals"""
    failed = 0
    n = len(shots)
    hits = sum(1 for s in shots if s["shot"] > 0.0)
    expected = sum(s["chance"] for s in shots)
    var = sum(s["chance"] * (1.0 - s["chance"]) for s in shots)
    z = (hits - expected) / math.sqrt(var) if var > 0.0 else 0.0
    out("shots %d:  hits %d, expected %.1f (z %.2f)%s" % (n, hits, expected, z, "  <- FAIL" if abs(z) > 3.0 else ""))
    if abs(z) > 3.0:
        failed += 1
    # hit rate by chance band
    bands = [(0.0, 0.1), (0.1, 0.3), (0.3, 0.5), (0.5, 0.7), (0.7, 0.9), (0.9, 1.0001)]
    for lo, hi in bands:
        band = [s for s in shots if lo <= s["chance"] < hi]
        if not band:
            continue
        bh = sum(1 for s in band if s["shot"] > 0.0)
        be = sum(s["chance"] for s in band)
        bv = sum(s["chance"] * (1.0 - s["chance"]) for s in band)
        bz = (bh - be) / math.sqrt(bv) if bv > 0.0 else 0.0
        out("  chance %.1f-%.1f:  %4d shots, hit %.3f, expected %.3f (z %.2f)" % (lo, min(hi, 1.0), len(band), bh / len(band),
                                                                              be / len(band), bz))
    # the draws:  uniform on [0, 1)
    if n >= 50:
        bins = [0] * 10
        for s in shots:
            bins[min(int(s["u"] * 10), 9)] += 1
        chi2 = sum((b - n / 10.0) ** 2 / (n / 10.0) for b in bins)
        # 9 degrees of freedom:  p = 0.001 at 27.88
        out("draws u:  chi-square %.2f over 10 bins (9 df, fails above 27.88)%s" % (chi2, "  <- FAIL" if chi2 > 27.88 else ""))
        if chi2 > 27.88:
            failed += 1
    wrecks = sum(1 for s in shots if s["shot"] >= 3.0)
    wexp = sum(min(s["chance"], wrecking) for s in shots)
    out("wrecking shots %d, expected %.1f (rates.WreckingChance %.4f)" % (wrecks, wexp, wrecking))
    if (wexp >= 5.0) and (abs(wrecks - wexp) > 4.0 * math.sqrt(wexp)):
        out("  <- FAIL:  wrecking shots far off their expectation")
        failed += 1
    return failed


def audit(shots, wrecking, verbose, out=print):
    bad = 0
    for s in shots:
        problems = check_shot(s, wrecking)
        if problems:
            bad += 1
            if verbose or bad <= 20:
                out("FAIL  %s\n      %s" % (s["text"], ";  ".join(problems)))
    out("%d of %d shots check out" % (len(shots) - bad, len(shots)))
    failed = totals(shots, wrecking, out)
    return (bad == 0) and (failed == 0)


# --- self test --------------------------------------------------------------------------------------------------------
def line_for(who, angular, surface, optimal, falloff, tracking, sig_res, sig, u, wrecking, transversal=0.0, corrupt=False):
    track, rng, chance = terms(angular, surface, optimal, falloff, tracking, sig_res, sig)
    shot = resolve(chance, u, wrecking)
    if corrupt:
        shot = 0.0 if shot > 0.0 else 0.8
    label = "miss" if shot <= 0.0 else ("wrecking" if shot >= 3.0 else "hit")
    return ("%s::ToHit Gistii Hijacker(9000001) -> Rifter(140000001):  surface %.0fm (optimal %.0f, falloff %.0f), "
            "transversal %.1fm/s, angular %.7f (tracking %.7f), sigRes %.0f vs sig %.1f -> track %.4f, range %.4f, "
            "chance %.5f, u %.5f -> %.3f (%s)" % (who, surface, optimal, falloff, transversal, angular, tracking, sig_res, sig,
                                                  track, rng, chance, u, shot, label))


def selftest():
    ok = True

    def expect(what, got, want, tol):
        nonlocal ok
        good = abs(got - want) <= tol
        ok = ok and good
        print("  %-52s %s  got %.6f want %.6f" % (what, "ok  " if good else "FAIL", got, want))

    print(" the plan's test vectors (PLAN.md appendix)")
    expect("Turret A", terms(0.06, 1000, 5000, 1000, 0.1, 125, 40)[2], 0.087436, 5e-6)
    expect("Turret B (optimal + falloff, no angular)", terms(0.0, 6000, 5000, 1000, 0.1, 125, 40)[2], 0.5, 1e-12)
    expect("Turret C sig 400", terms(0.0075, 19000, 24000, 12000, 0.033, 400, 400)[2], 0.96483, 5e-6)
    expect("Turret C sig 40", terms(0.0075, 19000, 24000, 12000, 0.033, 400, 40)[2], 0.027867, 5e-6)
    expect("wrecking inside a hit", resolve(0.005, 0.004, 0.01), 3.0, 0.0)
    expect("no wrecking outside a hit", resolve(0.005, 0.006, 0.01), 0.0, 0.0)
    expect("hit quality u + 0.49", resolve(0.5, 0.3, 0.01), 0.79, 1e-12)

    print(" 1000 synthetic shots (fixed seed) check out")
    rnd = random.Random(20111129)
    lines = []
    for _ in range(1000):
        lines.append(line_for("NPC", rnd.uniform(0.0, 0.08), rnd.uniform(0, 30000), rnd.uniform(2000, 20000),
                              rnd.uniform(1000, 12000), rnd.uniform(0.01, 0.4), rnd.choice([40, 125, 400]),
                              rnd.choice([35, 40, 125, 150, 400]), rnd.random(), 0.01, rnd.uniform(0, 400)))
    shots = parse(lines, ["NPC"])
    quiet = []
    good = audit(shots, 0.01, False, quiet.append)
    print("  %-52s %s" % ("parsed 1000", "ok  " if len(shots) == 1000 else "FAIL"))
    print("  %-52s %s" % ("all good", "ok  " if good else "FAIL"))
    if not good:
        print("\n".join(quiet))
    ok = ok and good and (len(shots) == 1000)

    print(" a wrong outcome is caught")
    bad = parse([line_for("NPC", 0.0, 1000, 5000, 1000, 0.1, 125, 40, 0.2, 0.01, corrupt=True)], ["NPC"])
    caught = bool(check_shot(bad[0], 0.01))
    print("  %-52s %s" % ("miss logged for u 0.2 < chance 1", "ok  " if caught else "FAIL"))
    ok = ok and caught

    print(" a biased draw is caught")
    biased = []
    for _ in range(1000):
        biased.append(line_for("NPC", 0.0, 1000, 5000, 1000, 0.1, 125, 40, rnd.random() * 0.5, 0.01))
    quiet = []
    caught = not audit(parse(biased, ["NPC"]), 0.01, False, quiet.append)
    print("  %-52s %s" % ("u only in [0, 0.5)", "ok  " if caught else "FAIL"))
    ok = ok and caught

    print("selftest %s" % ("passed" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description="npc ai fairness audit:  turret shot log lines against the shared formula")
    ap.add_argument("log", nargs="?", help="server log with DAMAGE__TRACE_NPC (or DAMAGE__TRACE) lines")
    ap.add_argument("--sample", type=int, default=1000, help="the last N shots (0 = all).  1000 default")
    ap.add_argument("--who", default="NPC,Sentry", help="shooters:  NPC, Sentry, Turret, Drone, Structure (comma separated)")
    ap.add_argument("--wrecking", type=float, default=0.01, help="rates.WreckingChance.  0.01 default")
    ap.add_argument("--verbose", action="store_true", help="every failing shot (else the first 20)")
    ap.add_argument("--selftest", action="store_true", help="check the checker:  the plan's vectors and synthetic shots")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.log:
        ap.error("a log file (or --selftest)")
    with open(args.log, "r", encoding="utf-8", errors="replace") as f:
        shots = parse(f, [w.strip() for w in args.who.split(",") if w.strip()])
    if args.sample > 0:
        shots = shots[-args.sample:]
    if not shots:
        print("no shots of %s in %s (is DAMAGE__TRACE_NPC on?)" % (args.who, args.log))
        return 2
    return 0 if audit(shots, args.wrecking, args.verbose) else 1


if __name__ == "__main__":
    sys.exit(main())
