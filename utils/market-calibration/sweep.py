"""sweep.py:  parameter sweeps of the market sim harness (market sim plan, phase 7).

Draws N parameter sets from a Latin hypercube over the main tuning keys (elasticities, spreads, noise, slack), runs the
harness's acceptance tests (phases 2, 3, 4 and 6) for every set in parallel processes, reads their PASS/FAIL lines and
numbers, scores every set against the plan's targets (section 4.1) and the acceptance limits, and writes a ranked CSV
and a summary (with each parameter's rank correlation to the scores):

    python -I utils/market-calibration/sweep.py --harness <build>/src/eve-market-sim/eve-market-sim.exe \\
        --out <dir> [--static export.json] [--samples 24] [--jobs 8] [--phases 2,3,4,6] [--seeds 1] [--baseline]

--static runs on a calibration export (gen_market_calibration.py --export-json;  a real-data run takes 6-18 minutes
per phase and seed), without it the harness's synthetic world (about 1.5-2 minutes per phase).  --space FILE replaces
the default space (lines 'Key low high [log]'), --set Key=value fixes a key in every set, --baseline adds the
defaults as set 0, --sets FILE runs given sets (a CSV with a header of keys) instead of drawing them.  Runs whose log
is complete are not run again (an interrupted sweep resumes:  the same command with the same --out);  --rescore only
reads the logs of --out;  --only runs.csv runs just the runs a previous runs.csv lists as missing (when its logs are
gone;  its results then cover those runs only).  Every scoring writes runs.csv:  one row per run, done or missing,
with its failed checks and figures.  Keys are the
harness's scenario keys:  the <marketSim> names (doc/code_and_design_notes/market-sim/TUNING.md) plus
HalfSpreadScale, StockElasticityScale and ElasticityScale (the calibration's per type parameters, scaled).

Stdlib only.  The draw is deterministic (--draw-seed).
"""
import argparse
import concurrent.futures
import csv
import math
import os
import random
import re
import subprocess
import sys
import time

# the default space:  key -> (low, high, log scale).  the defaults sit inside every range
DEFAULT_SPACE = [
    ('MarketNoise', 0.7, 1.3, False),
    ('TransientNoise', 0.5, 1.5, False),
    ('MinHalfSpread', 0.01, 0.025, False),
    ('HalfSpreadScale', 0.75, 1.3, False),
    ('ElasticityScale', 0.7, 1.4, False),
    ('StockElasticityScale', 0.7, 1.4, False),
    ('FairValueHalfLifeDays', 2.0, 5.0, False),
    ('InternalSpreadShare', 0.2, 0.8, False),
    ('MiningSlack', 0.15, 0.4, False),
    ('ProductionSlack', 0.5, 1.5, False),
    ('HarvestSlack', 1.5, 3.0, False),
    ('HaulerMargin', 0.02, 0.05, False),
    ('RemoteSupplyElasticity', 1.0, 3.0, False),
]

# the shipped defaults of the keys above (EVEServerConfig / eve-server.xml;  the scales are 1)
DEFAULTS = {
    'MarketNoise': 1.0, 'TransientNoise': 1.0, 'MinHalfSpread': 0.015, 'HalfSpreadScale': 1.0, 'ElasticityScale': 1.0,
    'StockElasticityScale': 1.0, 'FairValueHalfLifeDays': 3.0, 'InternalSpreadShare': 0.5, 'MiningSlack': 0.25,
    'ProductionSlack': 1.0, 'HarvestSlack': 2.0, 'HaulerMargin': 0.03, 'RemoteSupplyElasticity': 2.0,
}

# phase -> (harness flag, CTest's days)
PHASES = {
    '2': ([], 365),
    '3': (['--phase3'], 120),
    '4': (['--phase4'], 120),
    '6': (['--phase6'], 100),
}

# the checks the owner decides on (PLAN 4.1's T1 targets against the calibration's own figures):  reported, and
#  scored apart ('fails_excl_t1')
T1_TARGET_CHECKS = ('3:T1 modules', '3:T1 ships')


def latin_hypercube(space, n, rng):
    """n points, each dimension split into n strata, one point per stratum, strata shuffled per dimension"""
    cols = []
    for key, lo, hi, logscale in space:
        strata = list(range(n))
        rng.shuffle(strata)
        col = []
        for s in strata:
            u = (s + rng.random()) / n
            if logscale:
                v = math.exp(math.log(lo) + u * (math.log(hi) - math.log(lo)))
            else:
                v = lo + u * (hi - lo)
            col.append(float('%.4g' % v))
        cols.append(col)
    return [{space[d][0]: cols[d][i] for d in range(len(space))} for i in range(n)]


def read_space(path):
    space = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.split('#', 1)[0].split()
            if not line:
                continue
            space.append((line[0], float(line[1]), float(line[2]), len(line) > 3 and line[3] == 'log'))
    return space


# ---------------------------------------------------------------------------------------------------------------------
# reading a harness log

RE_CHECK = re.compile(r'^\s*\[(PASS|FAIL)\] (.*)$')
RE_DONE = re.compile(r'(acceptance tests passed|ACCEPTANCE TESTS FAILED) \((\d+) failure')
NUM = r'([-+]?\d+(?:\.\d+)?)'


def check_name(phase, text):
    """a check's name:  the words before its first number ('mineral shock:  the minerals peak at')"""
    m = re.search(r'(?<=[\s(\[])x?[-+]?\d', text)
    name = text[:m.start()] if m else text
    name = re.sub(r'\s+', ' ', name).strip(' :;,([')
    # the T1 / T2 / phase 2 checks keep their subject
    m = re.match(r'(T[12] \w+|\(\w\) [\w-]+)', text)
    if m:
        name = m.group(1)
    m = re.match(r'(.*?):\s+region (\d+)', text)
    if m:
        name = '%s region %s' % (m.group(1), m.group(2))
    return '%s:%s' % (phase, name)


def parse_log(phase, path):
    """the checks (name -> passed) and the metrics (name -> value) of one run;  None if the log is incomplete"""
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        return None
    if not RE_DONE.search(text):
        return None
    checks, metrics = {}, {}
    for line in text.splitlines():
        if phase == '2' and '[info] (d) without the cells calibrated at the' in line:
            # (d) without the cells at the calibration's volatility floor (not a check;  not scored)
            for cat, ratio in re.findall(r' (\w+) x([\d.]+) \(\d+\)', line):
                metrics['p2.noise_off_floor.' + cat] = float(ratio)
            continue
        m = RE_CHECK.match(line)
        if not m:
            continue
        ok, body = m.group(1) == 'PASS', m.group(2)
        name = check_name(phase, body)
        # a check that repeats (per seed, per region) fails if any instance fails
        checks[name] = checks.get(name, True) and ok
        if phase == '2':
            m = re.search(r'price index in \[%s, %s\].*longest clamp (\d+) days' % (NUM, NUM), body)
            if m:
                metrics['p2.index_min'] = float(m.group(1))
                metrics['p2.index_max'] = float(m.group(2))
                metrics['p2.clamp_days'] = float(m.group(3))
            m = re.search(r'M/M\* in \[%s, %s\]' % (NUM, NUM), body)
            if m:
                metrics['p2.money_min'] = float(m.group(1))
                metrics['p2.money_max'] = float(m.group(2))
            if body.startswith('(d) noise'):
                for cat, ratio in re.findall(r' (\w+) [\d.]+/[\d.]+=x([\d.]+) \(\d+\)', body):
                    metrics['p2.noise.' + cat] = float(ratio)
            m = re.search(r'worst day at %s of its intake cap' % NUM, body)
            if m:
                metrics['p2.dumper_cap'] = float(m.group(1))
        elif phase == '3':
            m = re.search(r'the minerals peak at x%s' % NUM, body)
            if m:
                metrics['p3.mineral_peak'] = float(m.group(1))
            m = re.search(r'the Drake peaks at x%s' % NUM, body)
            if m:
                metrics['p3.drake_peak'] = float(m.group(1))
            m = re.match(r"T1 (\w+):  median price / bill %s at steady state;  PLAN 4.1 %s \(x%s.*the calibration's %s" % (NUM, NUM, NUM, NUM), body)
            if m:
                metrics['p3.t1.' + m.group(1)] = float(m.group(4))
                cal = float(m.group(5))
                if cal > 0:
                    metrics['p3.t1cal.' + m.group(1)] = float(m.group(2)) / cal
        elif phase == '4':
            m = re.search(r'region (\d+) / the Forge over \d+ types:  median %s \(allowed' % NUM, body)
            if m:
                metrics['p4.median.%s' % m.group(1)] = float(m.group(2))
            m = re.search(r'high-sec hubs x%s \(median' % NUM, body)
            if m:
                metrics['p4.route_risk'] = float(m.group(1))
        elif phase == '6':
            m = re.match(r'T2 (\w+):  median price / bill %s .*PLAN 4.1 %s \(x%s' % (NUM, NUM, NUM), body)
            if m:
                metrics['p6.t2.' + m.group(1)] = float(m.group(4))
            m = re.search(r'%s sigma \(allowed 2\)' % NUM, body)
            if m:
                metrics['p6.invention_z'] = float(m.group(1))
            m = re.search(r'the components x%s.*the T2 x%s' % (NUM, NUM), body)
            if m:
                metrics['p6.moon_components'] = float(m.group(1))
                metrics['p6.moon_t2'] = float(m.group(2))
    return checks, metrics


def utilisation(name, v):
    """how much of an acceptance test's room a metric uses:  0 on target, 1 at the limit (beyond:  fails).  None:  not
    scored"""
    if name == 'p2.index_min':
        return max(0.0, (1.0 - v) / 0.2)
    if name == 'p2.index_max':
        return max(0.0, (v - 1.0) / 0.25)
    if name == 'p2.clamp_days':
        return v / 3.5
    if name == 'p2.money_min':
        return max(0.0, (1.0 - v) / 0.3)
    if name == 'p2.money_max':
        return max(0.0, (v - 1.0) / 0.4)
    if name.startswith('p2.noise.'):
        return abs(math.log(max(v, 1e-6))) / math.log(2.0)
    # (the dumper always sells up to its cap:  a pass/fail check, not scored)
    if name == 'p3.mineral_peak':
        return abs(v - 1.4) / 0.25
    if name.startswith('p3.t1.') or name.startswith('p3.t1cal.'):
        return abs(v - 1.0) / 0.10
    if name.startswith('p4.median.'):
        return abs(v - 1.025) / 0.055
    if name == 'p4.route_risk':
        return 0.005 / (v - 1.0) if v > 1.0 else 2.0
    if name.startswith('p6.t2.'):
        return abs(v - 1.0) / 0.20
    if name == 'p6.invention_z':
        return abs(v) / 2.0
    return None


# ---------------------------------------------------------------------------------------------------------------------
# running

def run_one(harness, args, log, timeout):
    if parse_log_any(log):
        return log, 'cached', 0.0
    t0 = time.time()
    with open(log + '.tmp', 'w', encoding='utf-8') as out:
        try:
            subprocess.run([harness] + args, stdout=out, stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            out.write('\nTIMEOUT after %ds\n' % timeout)
    os.replace(log + '.tmp', log)
    return log, 'ran', time.time() - t0


def parse_log_any(log):
    try:
        with open(log, encoding='utf-8', errors='replace') as f:
            return RE_DONE.search(f.read()) is not None
    except OSError:
        return False


def spearman(xs, ys):
    pairs = [(x, y) for x, y in zip(xs, ys) if x is not None and y is not None]
    if len(pairs) < 4:
        return None

    def ranks(v):
        order = sorted(range(len(v)), key=lambda i: v[i])
        r = [0.0] * len(v)
        i = 0
        while i < len(order):
            j = i
            while j + 1 < len(order) and v[order[j + 1]] == v[order[i]]:
                j += 1
            for k in range(i, j + 1):
                r[order[k]] = (i + j) / 2.0
            i = j + 1
        return r
    rx, ry = ranks([p[0] for p in pairs]), ranks([p[1] for p in pairs])
    n = len(pairs)
    mx, my = sum(rx) / n, sum(ry) / n
    sxy = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    sxx = sum((a - mx) ** 2 for a in rx)
    syy = sum((b - my) ** 2 for b in ry)
    return sxy / math.sqrt(sxx * syy) if sxx > 0 and syy > 0 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--harness', help='the eve-market-sim executable (copy it out of the build folder for long sweeps)')
    ap.add_argument('--out', required=True, help='folder for the scenarios, logs, sweep.csv and summary.txt')
    ap.add_argument('--static', help='a calibration export (gen_market_calibration.py --export-json);  default:  synthetic')
    ap.add_argument('--samples', type=int, default=24)
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument('--phases', default='2,3,4,6')
    ap.add_argument('--seeds', type=int, default=1, help='harness seeds 1..N per phase and set')
    ap.add_argument('--days', action='append', default=[], metavar='PHASE=DAYS', help='e.g. 2=120 (default as CTest)')
    ap.add_argument('--space', help="a file of 'Key low high [log]' lines (default:  DEFAULT_SPACE)")
    ap.add_argument('--set', action='append', default=[], metavar='KEY=VALUE', help='fixed in every set')
    ap.add_argument('--sets', help='a CSV of sets to run (header:  keys) instead of a draw')
    ap.add_argument('--baseline', action='store_true', help='add the defaults as set 0')
    ap.add_argument('--draw-seed', type=int, default=20111129)
    ap.add_argument('--timeout', type=int, default=4 * 3600, help='seconds per harness run')
    ap.add_argument('--rescore', action='store_true', help='only read the logs already in --out')
    ap.add_argument('--only', metavar='RUNS_CSV',
                    help='run only the runs a runs.csv lists as missing (set, phase, seed):  resumes a sweep whose logs are gone')
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    phases = [p.strip() for p in a.phases.split(',') if p.strip()]
    for p in phases:
        if p not in PHASES:
            sys.exit('unknown phase %s (2, 3, 4, 6)' % p)
    days = {p: PHASES[p][1] for p in PHASES}
    for d in a.days:
        k, v = d.split('=')
        days[k] = int(v)
    fixed = {}
    for s in a.set:
        k, v = s.split('=')
        fixed[k.strip()] = float(v)

    # the sets
    space = read_space(a.space) if a.space else DEFAULT_SPACE
    keys = [s[0] for s in space]
    if a.sets:
        with open(a.sets, newline='', encoding='utf-8') as f:
            sets = [{k: float(v) for k, v in row.items() if v not in (None, '')} for row in csv.DictReader(f)]
        keys = sorted(set(k for s in sets for k in s))
    else:
        sets = latin_hypercube(space, a.samples, random.Random(a.draw_seed))
    if a.baseline:
        sets.insert(0, {k: DEFAULTS.get(k, 1.0) for k in keys})
    sets = [dict(s, **fixed) for s in sets]
    keys = keys + [k for k in sorted(fixed) if k not in keys]

    # the runs
    jobs = []
    for i, s in enumerate(sets):
        scn = os.path.join(a.out, 'set%03d.scn' % i)
        body = '# sweep set %d\n' % i + ''.join('%s = %.6g\n' % (k, s[k]) for k in sorted(s))
        if not os.path.exists(scn) or open(scn, encoding='utf-8').read() != body:
            with open(scn, 'w', encoding='utf-8', newline='\n') as f:
                f.write(body)
        for p in phases:
            for seed in range(1, a.seeds + 1):
                log = os.path.join(a.out, 'set%03d_p%s_s%d.log' % (i, p, seed))
                args = (['--static', a.static] if a.static else ['--synthetic']) + PHASES[p][0]
                args += ['--days', str(days[p]), '--seed', str(seed), '--scenario', scn]
                if p == '2':
                    args.append('--no-repeat')
                jobs.append((i, p, seed, log, args))
    if a.only:
        with open(a.only, newline='', encoding='utf-8') as f:
            todo_keys = set((int(r['set']), r['phase'], int(r['seed'])) for r in csv.DictReader(f) if r['status'] != 'done')
        jobs = [j for j in jobs if (j[0], j[1], j[2]) in todo_keys]
    if not a.rescore:
        if not a.harness:
            sys.exit('--harness is needed to run (or --rescore)')
        todo = [j for j in jobs if not parse_log_any(j[3])]
        # the long ones first
        todo.sort(key=lambda j: -days[j[1]] * (2 if j[1] == '2' else 3))
        print('%d sets, %d runs (%d to do), %d at a time' % (len(sets), len(jobs), len(todo), a.jobs), flush=True)
        t0 = time.time()
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
            futs = [pool.submit(run_one, a.harness, j[4], j[3], a.timeout) for j in todo]
            for n, fut in enumerate(concurrent.futures.as_completed(futs), 1):
                log, how, secs = fut.result()
                print('  [%d/%d] %s %s in %.0fs (%.0f min so far)' % (n, len(futs), os.path.basename(log), how, secs,
                                                                     (time.time() - t0) / 60), flush=True)

    # scoring
    rows = []
    all_checks, all_metrics = set(), set()
    runs = []           # runs.csv:  one row per harness run, finished or not (what a resume still has to do)
    for i, s in enumerate(sets):
        checks, metrics, util, missing = {}, {}, {}, 0
        for (si, p, seed, log, _) in jobs:
            if si != i:
                continue
            got = parse_log(p, log)
            run = {'set': i, 'phase': p, 'seed': seed, 'log': os.path.basename(log)}
            runs.append(run)
            if got is None:
                run['status'] = 'missing'
                missing += 1
                continue
            c, m = got
            run['status'] = 'done'
            run['fails'] = sum(1 for ok in c.values() if not ok)
            run['failed_checks'] = ' | '.join(sorted(k for k, ok in c.items() if not ok))
            run.update(m)
            for k, ok in c.items():
                checks.setdefault(k, []).append(ok)
            for k, v in m.items():
                metrics.setdefault(k, []).append(v)
                u = utilisation(k, v)
                if u is not None:
                    util[k] = max(util.get(k, 0.0), u)
        fails = sorted(k for k, oks in checks.items() if not all(oks))
        fails_excl = [k for k in fails if k not in T1_TARGET_CHECKS]
        # the T1 checks are scored against the calibration's own figures (p3.t1cal.*), not PLAN 4.1's (p3.t1.*)
        scored = {k: v for k, v in util.items() if not k.startswith('p3.t1.')}
        worst = max(scored.items(), key=lambda kv: kv[1]) if scored else ('', 0.0)
        row = {'set': i, 'missing_runs': missing, 'fails': len(fails), 'fails_excl_t1': len(fails_excl),
               'max_u': round(worst[1], 4), 'mean_u': round(sum(scored.values()) / len(scored), 4) if scored else 0.0,
               'worst': worst[0], 'failed_checks': ' | '.join(fails)}
        row.update({k: s.get(k, '') for k in keys})
        for k, v in metrics.items():
            row[k] = round(sum(v) / len(v), 4)
        all_checks.update(checks)
        all_metrics.update(metrics)
        rows.append(row)
    rows.sort(key=lambda r: (r['missing_runs'] > 0, r['fails_excl_t1'], r['fails'], r['max_u'], r['mean_u'], r['set']))
    for rank, r in enumerate(rows, 1):
        r['rank'] = rank
    metric_cols = sorted(all_metrics)
    cols = ['rank', 'set'] + keys + ['missing_runs', 'fails_excl_t1', 'fails', 'max_u', 'mean_u', 'worst'] + metric_cols + ['failed_checks']
    with open(os.path.join(a.out, 'sweep.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction='ignore')
        w.writeheader()
        for r in rows:
            w.writerow(r)

    run_metrics = sorted(set(k for r in runs for k in r) - {'set', 'phase', 'seed', 'log', 'status', 'fails', 'failed_checks'})
    with open(os.path.join(a.out, 'runs.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=['set', 'phase', 'seed', 'status', 'fails'] + run_metrics + ['failed_checks', 'log'],
                           extrasaction='ignore')
        w.writeheader()
        for r in runs:
            w.writerow(r)

    # the summary
    out = []
    out.append('market sim sweep:  %d sets, phases %s, %d seed(s), %s' % (len(sets), ','.join(phases), a.seeds,
                                                                         a.static or 'synthetic world'))
    complete = [r for r in rows if r['missing_runs'] == 0]
    passing = [r for r in complete if r['fails_excl_t1'] == 0]
    out.append('runs finished %d of %d (runs.csv;  run the same command again to do the rest)' % (
        sum(1 for r in runs if r['status'] == 'done'), len(runs)))
    out.append('complete sets %d;  passing every phase %s acceptance test (PLAN 4.1 T1 targets aside) %d;  all of them %d'
               % (len(complete), '/'.join(phases), len(passing), sum(1 for r in complete if r['fails'] == 0)))
    out.append('')
    out.append('ranked (fails without / with the T1 PLAN 4.1 targets, max and mean use of the tests\' room, worst metric):')
    for r in rows[:15]:
        out.append('  #%-3d set %-3d  fails %d/%d  max_u %.2f  mean_u %.2f  worst %-22s %s' % (
            r['rank'], r['set'], r['fails_excl_t1'], r['fails'], r['max_u'], r['mean_u'], r['worst'],
            '  '.join('%s=%s' % (k, r[k]) for k in keys)))
    out.append('')
    out.append('checks that failed (sets):')
    for name in sorted(all_checks):
        bad = [r['set'] for r in complete if name in r['failed_checks'].split(' | ')]
        if bad:
            out.append('  %-40s %3d of %d:  %s' % (name, len(bad), len(complete), ' '.join(map(str, bad[:30]))))
    out.append('')
    out.append('rank correlation (Spearman) of each key with the scores and the borderline metrics:')
    focus = ['max_u', 'mean_u', 'fails_excl_t1'] + [m for m in metric_cols if m.startswith(('p2.noise.', 'p2.noise_off', 'p6.t2.', 'p6.invention', 'p3.t1', 'p3.mineral', 'p4.route', 'p2.index_max', 'p2.money_max'))]
    out.append('  %-24s' % 'metric' + ''.join('%9s' % k[:8] for k in keys))
    for m in focus:
        line = '  %-24s' % m[:24]
        for k in keys:
            c = spearman([r.get(k) if r.get(k) != '' else None for r in complete], [r.get(m) for r in complete])
            line += '%9s' % ('' if c is None else '%+.2f' % c)
        out.append(line)
    text = '\n'.join(out) + '\n'
    with open(os.path.join(a.out, 'summary.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(text)


if __name__ == '__main__':
    main()
