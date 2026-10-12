"""
compare.py - compares the repo's static tables (sql/base + migrations) with the Crucible client's bulkdata.

    python -I utils/bulkdata/compare.py --client "<EVE client>" [--table invTypes ...] [--samples 5]

Prints, per server table:  rows only the client has, rows only the server has, rows whose values differ (per column),
and differences that are only NULL vs 0 / NULL vs '' (counted, not treated as mismatches).
"""

import argparse
import collections
import decimal
import math
import os
import sqlite3
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import evemarshal  # noqa: E402
import serverdata  # noqa: E402
from specs import SPECS  # noqa: E402

INT_TYPES = ('int', 'tinyint', 'smallint', 'mediumint', 'bigint', 'bit')
FLOAT_TYPES = ('float',)
DOUBLE_TYPES = ('double', 'real', 'decimal')
TEXT_TYPES = ('char', 'varchar', 'text', 'mediumtext', 'longtext', 'tinytext')


def base_type(t):
    return t.split('(')[0].split()[0]


_client_cache = {}


def client_rows(client_dir, source):
    """(columns, rows, dbtypes) of a spec source;  dbtypes maps column -> marshal DBTYPE (None for sqlite)"""
    key = (client_dir, source)
    if key in _client_cache:
        return _client_cache[key]
    if isinstance(source, tuple) and source[0] == 'mapbulk':
        db = sqlite3.connect(os.path.join(client_dir, 'bulkdata', 'mapbulk.db'))
        cur = db.execute(source[1])
        cols = [d[0] for d in cur.description]
        rows = [list(r) for r in cur.fetchall()]
        db.close()
        out = (cols, rows, {c: (evemarshal.DBTYPE_R8 if c in ('x', 'y', 'z', 'radius') else None) for c in cols})
    else:
        obj = evemarshal.load_file(os.path.join(client_dir, 'bulkdata', '%d.cache2' % source))
        cols, rows = evemarshal.table(obj)
        dbt = {}
        hdr = obj.header[1].get('header') if isinstance(obj, evemarshal.ObjectEx) else None
        if hdr is not None:
            dbt = dict(evemarshal.descriptor_columns(hdr))
        out = (cols, rows, dbt)
    _client_cache[key] = out
    return out


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def norm(stype, v):
    """a server-comparable form of v for a column of server type stype"""
    if v is None:
        return None
    bt = base_type(stype)
    if bt in INT_TYPES:
        if isinstance(v, str):
            if bt == 'bit':
                return ord(v[0]) if v else 0
            try:
                return int(v)
            except ValueError:
                return v
        if isinstance(v, bool):
            return int(v)
        if isinstance(v, float):
            return int(v) if v == int(v) else v
        if isinstance(v, decimal.Decimal):
            return int(v) if v == int(v) else float(v)
        return v
    if bt in FLOAT_TYPES or bt in DOUBLE_TYPES:
        if isinstance(v, bool):
            return float(v)
        if isinstance(v, (int, float, decimal.Decimal)):
            return float(v)
        try:
            return float(v)
        except ValueError:
            return v
    if bt in TEXT_TYPES:
        if isinstance(v, bytes):
            v = v.decode('utf-8', 'replace')
        if isinstance(v, bool):
            return str(int(v))
        if not isinstance(v, str):
            return str(v) if not isinstance(v, float) or v != int(v) else str(int(v))
        return v
    return v


def same(stype, cv, sv, cdbt):
    """cv (client) and sv (server) normalized;  cdbt the client column's DBTYPE"""
    if cv == sv:
        return True
    if cv is None or sv is None:
        return False
    bt = base_type(stype)
    if isinstance(cv, float) and isinstance(sv, float):
        if bt == 'float' or cdbt == evemarshal.DBTYPE_R4:
            return f32(cv) == f32(sv)
        if math.isclose(cv, sv, rel_tol=1e-12, abs_tol=0.0):
            return True
        return False
    if isinstance(cv, str) and isinstance(sv, str) and bt in TEXT_TYPES:
        return False
    if isinstance(cv, (int, float)) and isinstance(sv, (int, float)):
        return math.isclose(float(cv), float(sv), rel_tol=1e-12)
    return False


def benign(stype, cv, sv):
    """a NULL on one side against the column's empty value on the other"""
    if cv is None and sv is None:
        return True
    other = sv if cv is None else cv if sv is None else 'x'
    bt = base_type(stype)
    if bt in TEXT_TYPES:
        return other == ''
    return other == 0 or other == 0.0


class Result(object):
    def __init__(self, spec, table):
        self.spec = spec
        self.table = table
        self.key = None
        self.columns = []              # server columns compared
        self.client = {}               # key -> {server col: normalized client value}
        self.server = {}               # key -> server row
        self.client_only = []
        self.server_only = []
        self.diffs = {}                # key -> {col: (client, server)}
        self.benign = collections.Counter()
        self.col_diffs = collections.Counter()
        self.unmapped_client = []
        self.raw = {}                  # key -> the client row as decoded (client column names)
        self.dup_client = 0
        self.dup_server = 0


def server_table(spec):
    """the server table, with the columns the spec adds (empty in every row)"""
    table = serverdata.load(spec.table)
    if not hasattr(table, 'added_columns'):
        table.added_columns = set()
    for col, (ctype, _) in spec.add_columns.items():
        if table.lc(col) is None:
            table.added_columns.add(col)
            table.columns.append(col)
            table.types[col] = ctype.lower()
            for r in table.rows:
                r[col] = None
    return table


def compare(spec, client_dir):
    table = server_table(spec)
    res = Result(spec, table)
    ccols, crows, cdbt = client_rows(client_dir, spec.source)
    key = spec.natural_key or spec.key or table.pk
    if not key:
        raise ValueError('%s: no key' % spec.table)
    colmap = {}
    vtypes = {}
    for c in ccols:
        if c in spec.virtual:
            colmap[c] = c
            vtypes[c] = spec.virtual[c][0]
            continue
        sc = table.lc(spec.rename.get(c, c))
        if sc is None or sc in spec.ignore or (spec.only and sc not in spec.only and sc not in key):
            res.unmapped_client.append(c)
            continue
        colmap[c] = sc
    res.key = key
    res.columns = [colmap[c] for c in ccols if c in colmap]
    sdbt = {colmap[c]: cdbt.get(c) for c in colmap}
    res.sdbt = sdbt
    types = dict(table.types)
    types.update(vtypes)
    res.types = types
    getters = {c: g for c, (_, g) in spec.virtual.items()}
    for r in crows:
        d = {colmap[c]: norm(types[colmap[c]], v) for c, v in zip(ccols, r) if c in colmap}
        if spec.transform:
            d = spec.transform(d)
            if d is None:
                continue
        k = tuple(d.get(c) for c in key)
        if k in res.client:
            res.dup_client += 1
        res.client[k] = d
        res.raw[k] = dict(zip(ccols, r))
    for r in table.rows:
        k = tuple(norm(table.types[c], r.get(c)) for c in key)
        if k in res.server:
            res.dup_server += 1
        res.server[k] = r
    for k, d in res.client.items():
        s = res.server.get(k)
        if s is None:
            res.client_only.append(k)
            continue
        dd = {}
        for c in res.columns:
            if c in key:
                continue
            cv = d.get(c)
            sv = norm(types[c], getters[c](s) if c in getters else s.get(c))
            if same(types[c], cv, sv, sdbt.get(c)):
                continue
            if benign(types[c], cv, sv):
                res.benign[c] += 1
                continue
            dd[c] = (cv, sv)
            res.col_diffs[c] += 1
        if dd:
            res.diffs[k] = dd
    res.server_only = [k for k in res.server if k not in res.client]
    return res


def short(v, n=60):
    s = repr(v)
    return s if len(s) <= n else s[:n] + '...'


def report(res, samples=3, out=sys.stdout):
    sp = res.spec
    src = sp.source if isinstance(sp.source, int) else sp.source[0]
    w = out.write
    w('== %s  (client %s)  key %s\n' % (sp.table, src, ','.join(res.key)))
    w('   client %d rows, server %d rows;  client-only %d, server-only %d, differing %d\n'
      % (len(res.client), len(res.server), len(res.client_only), len(res.server_only), len(res.diffs)))
    if res.dup_client or res.dup_server:
        w('   duplicate keys: client %d, server %d\n' % (res.dup_client, res.dup_server))
    if res.unmapped_client:
        w('   client columns with no server column: %s\n' % ', '.join(res.unmapped_client))
    missing = [c for c in res.table.columns if c not in res.columns and c not in res.key
               and not any(c in ('valueInt', 'valueFloat') for _ in sp.virtual)]
    if missing:
        w('   server columns the client does not have: %s\n' % ', '.join(missing))
    if res.table.unhandled:
        for fn, st in res.table.unhandled:
            w('   UNHANDLED migration statement %s: %s\n' % (fn, short(st, 120)))
    for c, n in res.col_diffs.most_common():
        ex = [(k, v[c]) for k, v in res.diffs.items() if c in v][:samples]
        w('   %-28s %6d  e.g. %s\n' % (c, n, '; '.join('%s: client %s server %s' % (
            ','.join(map(str, k)), short(cv, 40), short(sv, 40)) for k, (cv, sv) in ex)))
    if res.benign:
        w('   null-vs-empty only: %s\n' % ', '.join('%s %d' % kv for kv in res.benign.most_common()))
    if res.client_only:
        w('   client-only e.g. %s\n' % ', '.join(','.join(map(str, k)) for k in sorted(res.client_only, key=str)[:samples * 3]))
    if res.server_only:
        w('   server-only e.g. %s\n' % ', '.join(','.join(map(str, k)) for k in sorted(res.server_only, key=str)[:samples * 3]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--client', required=True, help='the EVE client folder (the one with bulkdata/)')
    ap.add_argument('--table', action='append')
    ap.add_argument('--samples', type=int, default=3)
    a = ap.parse_args()
    for sp in SPECS:
        if a.table and sp.table not in a.table:
            continue
        report(compare(sp, a.client), a.samples)
        sys.stdout.flush()


if __name__ == '__main__':
    main()
