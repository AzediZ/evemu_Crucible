"""report.py:  how a running market sim compares with its calibration (market sim plan, phase 7).

Reads the sim's price history and its cells, and compares them per category with the calibration (mktCal*):  the
price index (average price against the calibrated price at the node), the fair values against their anchors,
realised volatility (the calibration's estimator:  1.4826 x MAD of ln(p[d+5]/p[d]) / sqrt(5), derive.py
horizon_volatility), traded volume against the calibrated volume at EconomyScale, and the hubs' prices against the
Forge's.  Prints a summary and flags what is outside the acceptance tests' bands, then the worst outliers.

From a server's database (the mariadb or mysql client on PATH, or --mariadb):
    python -I utils/market-calibration/report.py --db evemu [--host 127.0.0.1] [--port 3306] [--user root]
        [--password ...] [--days 30] [--scale 0.01 | --config eve-server.xml] [--history-source sim|market]
From the files /mktsim export <name> [days] writes into the server's log folder (no database needed):
    python -I utils/market-calibration/report.py --export <logdir>/<name>
From the harness (eve-market-sim ... --export cells.csv --history-csv history.csv [--audit-csv audit.csv]):
    python -I utils/market-calibration/report.py --cells cells.csv --history history.csv [--audit audit.csv]
        [--names export.json]

--history-source sim (default) reads the sim's own trades (mktSimHistory, today included) plus the players' sales
(mktTransactions);  market reads mktHistory, what the client's graphs show, from the sim's first day on (the back-filled
days before it are synthetic).  --days limits the window, --min-trades the trades a day a cell needs for the
volatility (5, as the harness's AT (d)).  Stdlib only.
"""
import argparse
import csv
import datetime
import math
import os
import re
import shutil
import subprocess
import sys

FILETIME_EPOCH = 116444736000000000     # 1970-01-01 as a FILETIME
FILETIME_DAY = 864000000000
FORGE = 10000002

# invCategories -> the report's categories (the harness's AT (d) names, plus the rest)
CATEGORIES = {
    4: 'Materials', 25: 'Materials', 6: 'Ships', 7: 'Modules', 8: 'Charges', 17: 'Commodities', 18: 'Drones',
    20: 'Implants', 42: 'PI', 43: 'PI', 32: 'Subsystems', 22: 'Deployables', 23: 'Structures', 9: 'Blueprints',
    16: 'Skills',
}
# PLAN 4.1:  the observed same-day price ratio IQR against the Forge (hub regions)
OBSERVED_IQR = {10000043: (0.85, 1.20), 10000032: (0.79, 1.23), 10000030: (0.83, 1.25), 10000042: (0.79, 1.37)}
REGION_NAMES = {10000002: 'The Forge', 10000043: 'Domain', 10000032: 'Sinq Laison', 10000030: 'Heimatar',
                10000042: 'Metropolis', 10000016: 'Lonetrek', 10000020: 'Tash-Murkon', 10000033: 'The Citadel',
                10000064: 'Essence'}


def median(v):
    v = sorted(v)
    n = len(v)
    if n == 0:
        return float('nan')
    return v[n // 2] if n % 2 else 0.5 * (v[n // 2 - 1] + v[n // 2])


def quantile(v, q):
    v = sorted(v)
    if not v:
        return float('nan')
    pos = q * (len(v) - 1)
    lo = int(math.floor(pos))
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (pos - lo)


def horizon_volatility(series, lag=5, min_pairs=20):
    """derive.horizon_volatility:  series is {day: price};  None with fewer than min_pairs pairs"""
    r = [math.log(series[d + lag] / p) for d, p in series.items() if d + lag in series and p > 0 and series[d + lag] > 0]
    if len(r) < min_pairs:
        return None
    m = median(r)
    return 1.4826 * median([abs(x - m) for x in r]) / math.sqrt(lag)


def day_of(filetime):
    return (int(filetime) - FILETIME_EPOCH) // FILETIME_DAY


def date_of(day):
    return (datetime.date(1970, 1, 1) + datetime.timedelta(days=int(day))).isoformat()


# ---------------------------------------------------------------------------------------------------------------------
# sources.  every loader returns cells {(regionID, typeID): {...}}, history {(regionID, typeID): {day: [isk, units,
#  trades]}}, the audit [(day, category, amount)], names {typeID: name}, and notes

class Db:
    def __init__(self, a):
        exe = a.mariadb or shutil.which('mariadb') or shutil.which('mysql')
        if not exe:
            sys.exit('no mariadb/mysql client found:  pass --mariadb <path to mariadb.exe>')
        self.cmd = [exe, '-h', a.host, '-P', str(a.port), '-u', a.user, '--batch', '--raw', '--skip-column-names', a.db]
        if a.password:
            self.cmd.insert(1, '-p' + a.password)

    def query(self, sql):
        p = subprocess.run(self.cmd + ['-e', sql], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if p.returncode != 0:
            raise RuntimeError(p.stderr.decode('utf-8', 'replace').strip())
        return [line.split('\t') for line in p.stdout.decode('utf-8', 'replace').splitlines() if line]


def config_scale(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        m = re.search(r'<marketSim>.*?<EconomyScale>\s*([\d.eE+-]+)\s*</EconomyScale>', f.read(), re.S)
    return float(m.group(1)) if m else None


def load_db(a):
    db = Db(a)
    notes = []
    state = dict((r[0], r[1]) for r in db.query('SELECT name, value FROM mktSimState'))
    start_day = int(state.get('startDay', '0') or 0)
    nodes = {int(r[0]): (int(r[1]), int(r[2])) for r in db.query('SELECT nodeID, stationID, regionID FROM mktSimNode WHERE enabled = 1')}
    if not nodes:
        sys.exit('mktSimNode is empty:  the sim has not been built in %s' % a.db)
    regions = sorted(set(n[1] for n in nodes.values()))
    rlist = ','.join(map(str, regions))
    types, names = {}, {}
    for r in db.query('SELECT c.typeID, g.categoryID, c.simClass, c.refPrice, c.volatility, c.unitsPerDay, i.typeName'
                      ' FROM mktCalType c JOIN invTypes i ON i.typeID = c.typeID JOIN invGroups g ON g.groupID = i.groupID'):
        types[int(r[0])] = (int(r[1]), int(r[2]), float(r[3]), float(r[4]), float(r[5]))
        names[int(r[0])] = r[6]
    rcal = {(int(r[0]), int(r[1])): (float(r[2]), float(r[3]))
            for r in db.query('SELECT regionID, typeID, priceRatio, unitsShare FROM mktCalRegion WHERE regionID IN (%s)' % rlist)}
    rdef = {int(r[0]): (float(r[1]), float(r[2]))
            for r in db.query('SELECT regionID, priceRatio, unitsShare FROM mktCalRegionDefault WHERE regionID IN (%s)' % rlist)}
    scale = a.scale
    if scale is None and a.config:
        scale = config_scale(a.config)
    if scale is None:
        scale = 0.01
        notes.append('EconomyScale taken as 0.01 (pass --scale or --config if the server runs another)')
    cells = {}
    for r in db.query('SELECT nodeID, typeID, lnV, lnAnchor, clampDays FROM mktSimCell'):
        node, typeID = int(r[0]), int(r[1])
        if node not in nodes or typeID not in types:
            continue
        region = nodes[node][1]
        cat, cls, ref, vol, upd = types[typeID]
        ratio, share = rcal.get((region, typeID), (None, None))
        if ratio is None:
            ratio, share = rdef.get(region, (1.0, 0.0))
        cells[(region, typeID)] = {'categoryID': cat, 'simClass': cls, 'refLocal': ref * ratio, 'refPrice': ref,
                                   'volatility': vol, 'd0': upd * share * scale, 'V': math.exp(float(r[2])),
                                   'anchor': math.exp(float(r[3])), 'clampDays': int(r[4])}
    # the window
    last = None
    for r in db.query('SELECT MAX(historyDate) FROM mktSimHistory'):
        if r[0] != 'NULL':
            last = day_of(r[0])
    if last is None:
        sys.exit('mktSimHistory is empty:  the sim has not traded yet')
    first = max(start_day, last - a.days + 1)
    history = {}

    def add(region, typeID, day, isk, units, trades):
        if units <= 0:
            return
        h = history.setdefault((region, typeID), {}).setdefault(day, [0.0, 0, 0])
        h[0] += isk
        h[1] += units
        h[2] += trades
    if a.history_source == 'market':
        for r in db.query('SELECT regionID, typeID, historyDate, avgPrice, volume, orders FROM mktHistory'
                          ' WHERE regionID IN (%s) AND historyDate >= %d' % (rlist, first * FILETIME_DAY + FILETIME_EPOCH)):
            add(int(r[0]), int(r[1]), day_of(r[2]), float(r[3]) * int(r[4]), int(r[4]), int(r[5]))
        notes.append('history:  mktHistory (rolled-up days only:  today is missing until the rollup) from %s' % date_of(first))
    else:
        for r in db.query('SELECT regionID, typeID, historyDate, sumValue, volume, trades FROM mktSimHistory'
                          ' WHERE regionID IN (%s) AND historyDate >= %d' % (rlist, first * FILETIME_DAY + FILETIME_EPOCH)):
            add(int(r[0]), int(r[1]), day_of(r[2]), int(r[3]) / 100.0, int(r[4]), int(r[5]))
        players = 0
        for r in db.query('SELECT regionID, typeID, FLOOR((transactionDate - %d) / %d), SUM(price * quantity), SUM(quantity), COUNT(*)'
                          ' FROM mktTransactions WHERE transactionType = 0 AND regionID IN (%s) AND transactionDate >= %d'
                          ' GROUP BY 1, 2, 3' % (FILETIME_EPOCH, FILETIME_DAY, rlist, first * FILETIME_DAY + FILETIME_EPOCH)):
            add(int(r[0]), int(r[1]), int(float(r[2])), float(r[3]), int(float(r[4])), int(r[5]))
            players += 1
        notes.append('history:  mktSimHistory + players\' sales (mktTransactions, %d region/type/days) from %s' % (players, date_of(first)))
    audit = [(int(r[0]), r[1], int(r[2])) for r in db.query('SELECT day, category, amount FROM mktSimAudit WHERE day >= %d ORDER BY day, category' % first)]
    return cells, history, audit, names, notes, last


def read_cells(path):
    """World::ExportCsv:  the cells, a blank line, the last day's audit"""
    cells, audit = {}, []
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    parts = text.split('\n\n', 1)
    rows = list(csv.DictReader(parts[0].splitlines()))
    if rows and 'regionID' not in rows[0]:
        sys.exit('%s is an export without the calibration columns (before market sim phase 7):  export it again' % path)
    for r in rows:
        cells[(int(r['regionID']), int(r['typeID']))] = {
            'categoryID': int(r['categoryID']), 'simClass': int(r['simClass']), 'refLocal': float(r['refLocal']),
            'refPrice': float(r['refPrice']), 'volatility': float(r['volatility']), 'd0': float(r['d0']),
            'V': float(r['V']), 'anchor': float(r['anchor']), 'clampDays': int(r['clampDays'])}
    if len(parts) > 1:
        for r in csv.DictReader(parts[1].strip().splitlines()):
            audit.append((int(r['day']), r['category'], int(r['amount'])))
    return cells, audit


def read_history(path, days):
    """mktSimHistory's columns (regionID,typeID,historyDate,low,high,sumValue,volume,trades;  money in cents), or
    mktHistory's (regionID,typeID,historyDate,lowPrice,highPrice,avgPrice,volume,orders;  ISK)"""
    history, last = {}, None
    with open(path, encoding='utf-8', errors='replace', newline='') as f:
        for r in csv.DictReader(f):
            day = day_of(r['historyDate'])
            units = int(r['volume'])
            if units <= 0:
                continue
            isk = int(r['sumValue']) / 100.0 if 'sumValue' in r else float(r['avgPrice']) * units
            trades = int(r.get('trades') or r.get('orders') or 0)
            h = history.setdefault((int(r['regionID']), int(r['typeID'])), {}).setdefault(day, [0.0, 0, 0])
            h[0] += isk
            h[1] += units
            h[2] += trades
            last = day if last is None else max(last, day)
    if last is not None:
        for key in list(history):
            history[key] = {d: v for d, v in history[key].items() if d > last - days}
    return history, last


def read_audit(path):
    out = []
    with open(path, encoding='utf-8', errors='replace', newline='') as f:
        for r in csv.DictReader(f):
            out.append((int(r['day']), r['category'], int(r['amount'])))
    return out


def load_files(a):
    notes = []
    if a.export:
        cells_path, hist_path, audit_path = a.export, a.export + '.history.csv', a.export + '.audit.csv'
    else:
        cells_path, hist_path, audit_path = a.cells, a.history, a.audit
    if not cells_path or not hist_path:
        sys.exit('pass --db, --export, or --cells and --history')
    cells, audit = read_cells(cells_path)
    history, last = read_history(hist_path, a.days)
    if last is None:
        sys.exit('%s has no history rows' % hist_path)
    if audit_path and os.path.exists(audit_path):
        audit = read_audit(audit_path)
    names = {}
    if a.names:
        import json
        with open(a.names, encoding='utf-8') as f:
            names = {int(k): v.get('name', '') for k, v in json.load(f)['types'].items()}
    notes.append('history:  %s;  cells:  %s' % (hist_path, cells_path))
    return cells, history, audit, names, notes, last


# ---------------------------------------------------------------------------------------------------------------------
# the report

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--db')
    ap.add_argument('--mariadb')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=3306)
    ap.add_argument('--user', default='root')
    ap.add_argument('--password', default='')
    ap.add_argument('--scale', type=float, help='EconomyScale (default:  --config\'s, else 0.01)')
    ap.add_argument('--config', help='the server\'s eve-server.xml (for EconomyScale)')
    ap.add_argument('--history-source', choices=('sim', 'market'), default='sim')
    ap.add_argument('--export', help='the path /mktsim export wrote (with .history.csv and .audit.csv next to it)')
    ap.add_argument('--cells', help='a cells csv (/mktsim export, eve-market-sim --export)')
    ap.add_argument('--history', help='a history csv (mktSimHistory or mktHistory columns;  eve-market-sim --history-csv)')
    ap.add_argument('--audit', help='an audit csv (day,category,amount;  eve-market-sim --audit-csv)')
    ap.add_argument('--names', help='a calibration export json (gen_market_calibration.py --export-json) for type names')
    ap.add_argument('--days', type=int, default=30, help='the window:  the last N days of history')
    ap.add_argument('--min-trades', type=float, default=5.0, help='trades a day a cell needs for the volatility')
    ap.add_argument('--min-pairs', type=int, default=20, help='5-day pairs a cell needs for the volatility')
    ap.add_argument('--top', type=int, default=10, help='outliers listed per kind')
    a = ap.parse_args()

    if a.db:
        cells, history, audit, names, notes, last = load_db(a)
    else:
        cells, history, audit, names, notes, last = load_files(a)
    days_seen = sorted(set(d for h in history.values() for d in h))
    if not days_seen:
        sys.exit('no history in the window (%s)' % '; '.join(notes))
    first = days_seen[0]
    ndays = last - first + 1
    regions = sorted(set(k[0] for k in cells))
    out = []
    flags = []
    out.append('market sim report:  %d cells in %d regions, history %s .. %s (%d days, %d with trades)' % (
        len(cells), len(regions), date_of(first), date_of(last), ndays, len(days_seen)))
    for n in notes:
        out.append('  ' + n)
    # a partial last day:  volumes per day count the full days only, unless there are none
    # a server's last day is still running (the harness's history has finished days only)
    running = bool(a.db or a.export)
    full_days = ([d for d in days_seen if d < last] or days_seen) if running else days_seen
    partial = len(full_days) < len(days_seen)
    # without a full day (a server that started today) volumes per day mean nothing
    volumes = (not running) or any(d < last for d in days_seen)

    # per cell statistics
    stats = {}
    for key, c in cells.items():
        h = history.get(key, {})
        isk = sum(v[0] for d, v in h.items() if d in full_days or not partial)
        units = sum(v[1] for d, v in h.items() if d in full_days or not partial)
        trades = sum(v[2] for v in h.values())
        s = {'tradingDays': len(h), 'unitsPerDay': units / max(1, len(full_days)), 'vwap': (isk / units) if units else None,
             'tradesPerDay': trades / max(1, len(h))}
        series = {d: v[0] / v[1] for d, v in h.items() if v[1] > 0}
        if s['tradesPerDay'] >= a.min_trades:
            s['vol'] = horizon_volatility(series, 5, a.min_pairs)
        s['series'] = series
        stats[key] = s

    # per category
    out.append('')
    out.append('per category (cells with trades;  price = window VWAP / calibrated price at the node;  V/anchor now;'
               '  volume = traded / calibrated at EconomyScale;  volatility = realised / calibrated, 5-day horizon):')
    out.append('  %-12s %6s %8s %8s %8s %9s %9s %8s %16s' % ('category', 'cells', 'traded', 'price', 'isk-wt', 'V/anchor',
                                                            'volume', 'units', 'volatility'))
    bycat = {}
    for key, c in cells.items():
        bycat.setdefault(CATEGORIES.get(c['categoryID'], 'Other'), []).append(key)
    for cat in sorted(bycat, key=lambda k: -sum(cells[x]['d0'] * cells[x]['refLocal'] for x in bycat[k])):
        keys = bycat[cat]
        traded = [k for k in keys if stats[k]['vwap'] is not None and cells[k]['refLocal'] > 0]
        pr = [stats[k]['vwap'] / cells[k]['refLocal'] for k in traded]
        w = [cells[k]['d0'] * cells[k]['refLocal'] for k in traded]
        iskw = math.exp(sum(wi * math.log(p) for wi, p in zip(w, pr)) / sum(w)) if traded and sum(w) > 0 else float('nan')
        va = [cells[k]['V'] / cells[k]['anchor'] for k in keys if cells[k]['anchor'] > 0]
        # isk-weighted:  the units of a category's types are worth very different amounts
        #  (not ore:  the sim's miners refine most of it where they mine it)
        d0 = sum(cells[k]['d0'] * cells[k]['refLocal'] for k in keys if cells[k]['simClass'] != 1)
        got = sum(stats[k]['unitsPerDay'] * cells[k]['refLocal'] for k in keys if cells[k]['simClass'] != 1)
        vr = [stats[k]['unitsPerDay'] / cells[k]['d0'] for k in keys if cells[k]['d0'] >= 1.0]
        vols = [(stats[k]['vol'], cells[k]['volatility']) for k in keys if stats[k].get('vol') is not None]
        volr = median([v[0] for v in vols]) / median([v[1] for v in vols]) if len(vols) >= 3 else None
        out.append('  %-12s %6d %8d %8.3f %8.3f %9.3f %9s %8.3f %16s' % (
            cat, len(keys), len(traded), median(pr) if pr else float('nan'), iskw, median(va) if va else float('nan'),
            ('x%.2f' % (got / d0)) if (d0 > 0 and volumes) else '-', median(vr) if (vr and volumes) else float('nan'),
            ('x%.2f (%d)' % (volr, len(vols))) if volr is not None else '- (%d)' % len(vols)))
        if traded and sum(w) > 0 and not 0.8 <= iskw <= 1.25:
            flags.append('%s:  price index %.3f outside [0.8, 1.25] (AT (b))' % (cat, iskw))
        if volr is not None and not 0.5 <= volr <= 2.0:
            flags.append('%s:  volatility x%.2f of the calibration, outside x0.5..x2 (AT (d))' % (cat, volr))
        if volumes and d0 > 0 and not 1.0 / 3.0 <= got / d0 <= 3.0:
            flags.append('%s:  traded isk x%.2f of the calibration at this EconomyScale (no acceptance test;  check'
                         ' EconomyScale)' % (cat, got / d0))
    out.append('  (price:  median over cells;  isk-wt:  geometric mean weighted by calibrated isk turnover;  volume:  isk traded'
               ' at calibrated prices against the calibration\'s;  units:  median over cells with at least 1 unit a day'
               ' calibrated;  ore is left out of the volume:  the sim\'s miners refine most of it where they mine it)')
    if not volumes:
        out.append('  (no full day of history yet:  volumes are not compared)')
    elif partial:
        out.append('  (volumes over %d full days;  the last day, %s, is still running)' % (len(full_days), date_of(last)))
    if ndays < a.min_pairs + 5:
        out.append('  (volatility needs %d+ days of history:  this window has %d)' % (a.min_pairs + 5, ndays))

    # the hubs against the Forge
    if FORGE in regions and len(regions) > 1:
        out.append('')
        out.append('hub prices against the Forge (per type, median of same-day average ratios;  then across types;  sim/cal:'
                   '  the median of each type\'s ratio against its calibrated ratio):')
        min_common = 10 if ndays >= 20 else max(1, ndays // 2)
        if ndays < 10:
            out.append('  (%d days:  the types that traded at both hubs so far are a small, biased set;  no flags before 10 days)' % ndays)
        for region in regions:
            if region == FORGE:
                continue
            ratios, cal, rel = [], [], []
            for (r, typeID), s in stats.items():
                if r != region:
                    continue
                fs = stats.get((FORGE, typeID))
                if not fs:
                    continue
                common = [d for d in s['series'] if d in fs['series']]
                if len(common) < min_common:
                    continue
                ratios.append(median([s['series'][d] / fs['series'][d] for d in common]))
                fc = cells.get((FORGE, typeID))
                if fc and fc['refLocal'] > 0 and cells[(region, typeID)]['refLocal'] > 0:
                    cal.append(cells[(region, typeID)]['refLocal'] / fc['refLocal'])
                    rel.append(ratios[-1] / cal[-1])
            if len(ratios) < 5:
                out.append('  %-12s too few types traded at both' % REGION_NAMES.get(region, region))
                continue
            p25, p50, p75 = quantile(ratios, 0.25), quantile(ratios, 0.5), quantile(ratios, 0.75)
            obs = OBSERVED_IQR.get(region)
            out.append('  %-12s %5d types  median %.3f  IQR %.3f..%.3f   calibrated %.3f, %.3f..%.3f   sim/cal %.3f%s' % (
                REGION_NAMES.get(region, region), len(ratios), p50, p25, p75, quantile(cal, 0.5), quantile(cal, 0.25),
                quantile(cal, 0.75), median(rel), ('   observed IQR %.2f..%.2f' % obs) if obs else ''))
            if ndays < 10:
                continue
            if region in OBSERVED_IQR and not 0.97 <= p50 <= 1.08:
                flags.append('%s:  hub median %.3f outside 0.97..1.08 (phase 4 AT)' % (REGION_NAMES.get(region, region), p50))
            if not 0.95 <= median(rel) <= 1.05:
                flags.append('%s:  prices against the Forge x%.3f of the calibrated ratios' % (REGION_NAMES.get(region, region), median(rel)))

    # the audit
    if not audit:
        out.append('')
        out.append('audit:  no day closed yet (the first audit is written after midnight UTC)')
    else:
        lastday = max(d for d, _, _ in audit)
        day = {c: v for d, c, v in audit if d == lastday}
        out.append('')
        out.append('audit, day %s (%d days in the window):' % (date_of(lastday), len(set(d for d, _, _ in audit))))
        money, target = day.get('money.end'), day.get('money.target')
        if money is not None and target:
            out.append('  money %.4g ISK, target %.4g ISK:  M/M* %.3f' % (money / 100.0, target / 100.0, money / float(target)))
            if not 0.7 <= money / float(target) <= 1.4:
                flags.append('M/M* %.3f outside [0.7, 1.4] (AT (c))' % (money / float(target)))
        if 'priceIndex.ppm' in day:
            out.append('  the sim\'s price index (top 500 isk basket against the calibrated prices):  %.4f' % (day['priceIndex.ppm'] / 1e6))
        res = [(d, c, v) for d, c, v in audit if c.startswith('residual.') and v != 0]
        out.append('  residuals:  %s' % ('none' if not res else '%d nonzero, first day %s %s %d' % (len(res), date_of(res[0][0]), res[0][1], res[0][2])))
        if res:
            flags.append('the audit has %d nonzero residuals (AT (a);  the sim pauses with PauseOnAuditResidual)' % len(res))
        flows = sorted(((c, v) for c, v in day.items() if c.startswith('isk.') and v), key=lambda kv: -abs(kv[1]))
        out.append('  the day\'s flows (ISK):  ' + ';  '.join('%s %s' % (c[4:], '{:,.0f}'.format(v / 100.0)) for c, v in flows[:10]))
        flows = sorted(((c, v) for c, v in day.items() if c.startswith('goods.') and v), key=lambda kv: -abs(kv[1]))
        out.append('  the day\'s flows (units of all types):  ' + ';  '.join('%s %s' % (c[6:], '{:,}'.format(v)) for c, v in flows[:10]))

    # outliers
    def name(typeID):
        return names.get(typeID, '')
    out.append('')
    out.append('outliers (cells with at least 3 trading days and 0.5 units a day calibrated):')
    cand = [(k, s) for k, s in stats.items() if s['tradingDays'] >= 3 and cells[k]['d0'] >= 0.5 and s['vwap'] and cells[k]['refLocal'] > 0]
    by_price = sorted(cand, key=lambda ks: -abs(math.log(ks[1]['vwap'] / cells[ks[0]]['refLocal'])))
    out.append('  price against the calibration:')
    for k, s in by_price[:a.top]:
        c = cells[k]
        out.append('    %8d at %-10d x%7.3f  (%.4g against %.4g ISK;  V/anchor %.3f, clamp %d days)  %s' % (
            k[1], k[0], s['vwap'] / c['refLocal'], s['vwap'], c['refLocal'], c['V'] / c['anchor'] if c['anchor'] else 0,
            c['clampDays'], name(k[1])))
    # (not ore:  the miners refine it where they mine it)
    by_vol = sorted([ks for ks in cand if cells[ks[0]]['simClass'] != 1],
                    key=lambda ks: -abs(math.log(max(1e-6, ks[1]['unitsPerDay']) / cells[ks[0]]['d0'])))
    out.append('  volume against the calibration (ore aside):')
    for k, s in by_vol[:a.top]:
        out.append('    %8d at %-10d x%7.3f  (%.4g a day against %.4g)  %s' % (
            k[1], k[0], s['unitsPerDay'] / cells[k]['d0'], s['unitsPerDay'], cells[k]['d0'], name(k[1])))
    vcand = [(k, s) for k, s in stats.items() if s.get('vol') is not None and cells[k]['volatility'] > 0]
    if vcand:
        out.append('  volatility against the calibration:')
        for k, s in sorted(vcand, key=lambda ks: -abs(math.log(ks[1]['vol'] / cells[ks[0]]['volatility'])))[:a.top]:
            out.append('    %8d at %-10d x%7.3f  (%.4f against %.4f)  %s' % (
                k[1], k[0], s['vol'] / cells[k]['volatility'], s['vol'], cells[k]['volatility'], name(k[1])))
    clamped = sorted(((k, c) for k, c in cells.items() if c['clampDays'] > 3), key=lambda kc: -kc[1]['clampDays'])
    if clamped:
        out.append('  fair value held at a Kmax bound for more than 3 days (a stock-out or a glut;  AT (b)):  %d cells' % len(clamped))
        for k, c in clamped[:a.top]:
            out.append('    %8d at %-10d %d days  %s' % (k[1], k[0], c['clampDays'], name(k[1])))
        flags.append('%d cells at a Kmax bound for more than 3 days' % len(clamped))

    out.append('')
    if flags:
        out.append('FLAGS (%d):' % len(flags))
        for f in flags:
            out.append('  ' + f)
    else:
        out.append('no flags:  every figure is inside the acceptance tests\' bands')
    print('\n'.join(out))
    return 1 if flags else 0


if __name__ == '__main__':
    sys.exit(main())
