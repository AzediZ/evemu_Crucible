#!/usr/bin/env python3
"""
gen_client_sync.py - writes the migrations that bring the server's static tables in line with the Crucible client's
own bulkdata (bulkdata/*.cache2 and bulkdata/mapbulk.db).

    python -I utils/bulkdata/gen_client_sync.py --client "<EVE client folder>" [--summary <file.md>]

Server data:  sql/base plus the Up sections of sql/migrations, leaving out the files this script wrote.  Tables, column
mapping and per-table rules:  specs.py.  The rules:
  * the client wins wherever it has a value and the server's differs (floats compared at the client column's precision);
  * where the client is empty (NULL, blank text, 'None', or a column's placeholder) the server's value stays, except in
    a spec's null_is_data columns;
  * rows only the client has are inserted, rows only the server has are deleted (unless the spec keeps them);
  * a NULL is only ever written to a column that already holds NULLs, so code that reads it with GetInt() cannot meet
    a new one.
Each migration loads the changed rows into a temporary table and applies them with UPDATE / DELETE / INSERT ... JOIN;
its Down section puts the old values back the same way.  The output is deterministic;  stdlib only.
"""

import argparse
import collections
import decimal
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import compare as C  # noqa: E402
import serverdata as S  # noqa: E402
import specs  # noqa: E402

HEADER = S.SKIP_HEADER
MIGRATIONS = S.MIGRATIONS
FIRST_TIMESTAMP = 20261012120000
MAX_STATEMENT = 400 * 1024          # bytes per INSERT, well under the smallest max_allowed_packet
INT_RANGE = {'tinyint': 8, 'smallint': 16, 'mediumint': 24, 'int': 32, 'bigint': 64}
MOON_RE = re.compile(r' Moon ([0-9]+)$', re.I)


def file_name(index, group):
    return '%d-client-sync-%s.sql' % (FIRST_TIMESTAMP + index * 100, group)


# ---------------------------------------------------------------------------------------------------------------------
# values

def is_text(ctype):
    return C.base_type(ctype) in C.TEXT_TYPES


def is_empty(spec, col, ctype, v):
    if v is None:
        return True
    if is_text(ctype) and isinstance(v, str) and (v.strip() == '' or v == 'None'):
        return True
    return v in spec.placeholders.get(col, ())


def zero(ctype):
    return '' if is_text(ctype) else 0


def fits(ctype, v):
    """None when v fits a column of type ctype, else why not"""
    if v is None:
        return None
    bt = C.base_type(ctype)
    if bt in INT_RANGE:
        if not isinstance(v, int):
            return 'not an integer'
        bits = INT_RANGE[bt]
        lo, hi = (0, 2 ** bits - 1) if 'unsigned' in ctype else (-2 ** (bits - 1), 2 ** (bits - 1) - 1)
        return None if lo <= v <= hi else 'out of range'
    if bt == 'bit':
        return None if v in (0, 1) else 'not a bit'
    if bt in ('float', 'double', 'real'):
        return None if math.isfinite(float(v)) else 'not finite'
    if bt in ('char', 'varchar'):
        n = int(re.search(r'\((\d+)\)', ctype).group(1))
        if len(v) > n:
            return 'longer than %d' % n
    if bt in C.TEXT_TYPES:
        if any(ord(ch) > 0xFFFF for ch in v):
            return 'needs utf8mb4'
        if bt == 'text' and len(v.encode('utf-8')) > 65535:
            return 'longer than a text'
    return None


def sql_value(v):
    if v is None:
        return 'NULL'
    if isinstance(v, bool):
        return '1' if v else '0'
    if isinstance(v, int):
        return str(v)
    if isinstance(v, decimal.Decimal):
        return format(v, 'f')
    if isinstance(v, float):
        if v == int(v) and abs(v) < 1e15:
            return str(int(v))
        return repr(v)
    if isinstance(v, bytes):
        return "X'%s'" % v.hex().upper() if v else "''"
    s = str(v)
    # sql-migrate ends a statement at a line whose last word before a '--' ends in ';', and the connection's character
    # set is unknown:  anything non-ascii, or with '--' in it, goes as a hex literal (bytes, stored as they are)
    if '--' in s or any(ord(ch) > 126 for ch in s):
        return "X'%s'" % s.encode('utf-8').hex().upper()
    s = (s.replace('\\', '\\\\').replace("'", "\\'").replace('\0', '\\0').replace('\n', '\\n')
         .replace('\r', '\\r').replace('\t', '\\t').replace('\x1a', '\\Z'))
    return "'%s'" % s


def q(name):
    return '`%s`' % name


# ---------------------------------------------------------------------------------------------------------------------
# plans

class Plan(object):
    """one server table:  its rows as they are (orig) and as they should be (work)"""

    def __init__(self, table, key, natural):
        self.table = table
        self.name = table.name
        self.key = key
        self.natural = natural
        self.types = dict(table.types)
        self.orig = collections.OrderedDict()
        for r in table.rows:
            self.orig[self.keyof(r)] = r
        self.work = collections.OrderedDict((k, dict(r)) for k, r in self.orig.items())
        self.changed = collections.defaultdict(set)     # key -> columns changed in place
        self.col_changes = collections.Counter()
        self.has_nulls = {c: any(r.get(c) is None for r in table.rows) for c in table.columns}
        self.derived = []                                # (up sql, down sql, keys, column)
        self.add_columns = {}                            # column -> ddl
        self.modify_columns = {}                         # column -> (ddl, old ddl or None)
        self.specs = []
        self.notes = []
        self.problems = []

    def keyof(self, row):
        return tuple(C.norm(self.types[c], row.get(c)) for c in self.key)

    def set(self, k, col, v):
        self.work[k][col] = v
        if k in self.orig:
            self.changed[k].add(col)

    def empty_value(self, col):
        """what an empty client value becomes in a new row"""
        ctype = self.types[col]
        if col not in self.table.notnull and self.has_nulls.get(col):
            return None
        return zero(ctype)

    def default_value(self, col):
        ctype = self.types[col]
        if col in self.table.defaults and self.table.defaults[col] is not None:
            return C.norm(ctype, self.table.defaults[col])
        return self.empty_value(col)

    def val(self, col, v):
        """v as written to col:  bit values the dumps give as characters go out as 0 / 1"""
        if isinstance(v, str) and C.base_type(self.types[col]) == 'bit':
            return ord(v[0]) if v else 0
        return v

    def insert_columns(self):
        return [c for c in self.table.columns if not (self.natural and c in self.table.autoinc)]


class Context(object):
    def __init__(self, client):
        self.client = client
        self.plans = collections.OrderedDict()
        self.attr_storage = None

    def plan(self, spec):
        if spec.table not in self.plans:
            table = C.server_table(spec)
            key = spec.natural_key or spec.key or table.pk
            p = Plan(table, key, bool(spec.natural_key))
            for col, (_, ddl) in spec.add_columns.items():
                if col in table.added_columns:
                    p.add_columns[col] = ddl
            self.plans[spec.table] = p
        p = self.plans[spec.table]
        for col, (ctype, ddl, old) in spec.modify_columns.items():
            if p.types[col] != ctype:
                p.types[col] = ctype
                p.modify_columns[col] = (ddl, old)
        return p


def apply_spec(ctx, spec):
    plan = ctx.plan(spec)
    plan.specs.append(spec)
    res = C.compare(spec, ctx.client)
    types = res.types
    cols = [c for c in res.columns if c not in plan.key and c not in spec.virtual]
    for k, crow in res.client.items():
        row = plan.work.get(k)
        if row is None:
            if k in plan.orig:
                continue                    # deleted by an earlier spec of this table
            plan.work[k] = new_row(ctx, plan, spec, res, k, crow)
            continue
        for c in cols:
            cv = crow.get(c)
            sv = C.norm(types[c], row.get(c))
            if C.same(types[c], cv, sv, res.sdbt.get(c)):
                continue
            if C.benign(types[c], cv, sv):
                continue                    # NULL on one side, 0 / '' on the other:  the same "nothing"
            if is_empty(spec, c, types[c], cv):
                if cv is None and c in spec.null_is_data:
                    # the server's own way of saying "none":  NULL where the column has NULLs, else 0 / ''
                    nv = None if plan.has_nulls.get(c) else zero(types[c])
                    if sv != nv:
                        plan.set(k, c, nv)
                        plan.col_changes[c] += 1
                continue
            plan.set(k, c, cv)
            plan.col_changes[c] += 1
        if 'value' in spec.virtual:
            set_attribute_value(ctx, plan, k, crow.get('value'), res.sdbt.get('value'))
    if spec.server_only == 'delete':
        for k in res.server_only:
            if k not in spec.keep_keys:
                plan.work.pop(k, None)


def new_row(ctx, plan, spec, res, k, crow):
    row = {}
    for c in plan.table.columns:
        if plan.natural and c in plan.table.autoinc:
            row[c] = None
        elif c in crow and c not in spec.virtual:
            row[c] = plan.empty_value(c) if crow[c] is None else crow[c]
        else:
            row[c] = plan.default_value(c)
    if 'value' in spec.virtual:
        v = crow.get('value')
        row['valueInt'], row['valueFloat'] = None, None
        storage = attribute_storage(ctx)
        if v is not None and float(v).is_integer() and storage.get(row['attributeID']) == 'int' \
                and -2 ** 31 <= v < 2 ** 31:
            row['valueInt'] = int(v)
        else:
            row['valueFloat'] = float(v) if v is not None else 0.0
    hook = INSERT_HOOKS.get(plan.name)
    if hook:
        hook(ctx, plan, row, res.raw.get(k, {}))
    return row


def attribute_storage(ctx):
    """attributeID -> 'int' / 'float':  how most of the server's rows of that attribute store their value"""
    if ctx.attr_storage is None:
        n = collections.defaultdict(collections.Counter)
        for r in S.load('dgmTypeAttributes').rows:
            n[r['attributeID']]['int' if r['valueInt'] is not None else 'float'] += 1
        ctx.attr_storage = {a: c.most_common(1)[0][0] for a, c in n.items()}
    return ctx.attr_storage


def set_attribute_value(ctx, plan, k, cv, cdbt):
    row = plan.work[k]
    sv = row['valueFloat'] if row.get('valueInt') is None else row['valueInt']
    if cv is None or C.same('double', float(cv), None if sv is None else float(sv), cdbt):
        return
    if row.get('valueInt') is not None and float(cv).is_integer() and -2 ** 31 <= cv < 2 ** 31:
        plan.set(k, 'valueInt', int(cv))
    else:
        if row.get('valueInt') is not None:
            plan.set(k, 'valueInt', None)
        plan.set(k, 'valueFloat', float(cv))
    plan.col_changes['value'] += 1


# ---------------------------------------------------------------------------------------------------------------------
# table specific

def _hook_npc_character(ctx, plan, row, raw):
    """chrNPCCharacters wants the bloodline's character type and the station's location"""
    blood = {r['bloodlineID']: r['typeID'] for r in S.load('bloodlineTypes').rows}
    row['typeID'] = blood.get(raw.get('bloodlineID'), 0)
    sta = {r['stationID']: r for r in S.load('staStations').rows}.get(row.get('stationID'))
    if sta:
        row['solarSystemID'] = sta['solarSystemID']
        row['constellationID'] = sta['constellationID']
        row['regionID'] = sta['regionID']
    if raw.get('startDate'):
        row['startDateTime'] = row['createDateTime'] = row['corporationDateTime'] = raw['startDate']


def _hook_agent(ctx, plan, row, raw):
    if 'legacyDivisionID' in row:
        row['legacyDivisionID'] = row['divisionID']


def _hook_corporation(ctx, plan, row, raw):
    """a new npc corporation's game state comes from an existing one (CBD Corporation)"""
    template = plan.orig.get((1000002,))
    for c in plan.table.columns:
        if c in specs.NPC_CORP_IDENTITY or c in plan.key or template is None:
            continue
        row[c] = template[c]
    row['memberCount'] = 0


INSERT_HOOKS = {
    'chrNPCCharacters': _hook_npc_character,
    'agtAgents': _hook_agent,
    'crpCorporation': _hook_corporation,
}


def post_expressions(ctx):
    """keep the server-only expressions an effect (or another kept expression) still uses"""
    ep, xp = ctx.plans.get('dgmEffects'), ctx.plans.get('dgmExpressions')
    if not ep or not xp:
        return
    allx = {k[0]: r for k, r in xp.orig.items()}
    allx.update({k[0]: r for k, r in xp.work.items()})
    todo = [r.get(c) for r in ep.work.values() for c in ('preExpression', 'postExpression')]
    used = set()
    while todo:
        x = todo.pop()
        if x in used or x not in allx:
            continue
        used.add(x)
        todo.extend(allx[x].get(c) for c in ('arg1', 'arg2'))
    kept = 0
    for k, r in xp.orig.items():
        if k not in xp.work and k[0] in used:
            xp.work[k] = dict(r)
            kept += 1
    if kept:
        xp.notes.append('%d server-only expressions kept because effects still use them' % kept)


def post_map(ctx):
    """moons' orbitIndex is the number at the end of their name:  one statement instead of 342k rows"""
    p = ctx.plans.get('mapDenormalize')
    if not p:
        return
    cand = [k for k, r in p.orig.items() if r['groupID'] == 8 and r['orbitIndex'] is None
            and MOON_RE.search(r['itemName'] or '')]
    keys = []
    for k in cand:
        w = p.work.get(k)
        m = MOON_RE.search(p.orig[k]['itemName'])
        if w is None or w['orbitIndex'] != int(m.group(1)):
            raise SystemExit('mapDenormalize %s: the moon-number rule does not hold, update the generator' % (k,))
        keys.append(k)
    if not keys:
        return
    if any(r['groupID'] == 8 and r['orbitIndex'] is not None for r in p.orig.values()):
        raise SystemExit('mapDenormalize: some moons already have an orbitIndex, the Down statement would lose it')
    for k in keys:
        p.changed[k].discard('orbitIndex')
        if not p.changed[k]:
            del p.changed[k]
    up = ("UPDATE `mapDenormalize` SET `orbitIndex` = CAST(SUBSTRING_INDEX(`itemName`, ' ', -1) AS UNSIGNED)\n"
          "    WHERE `groupID` = 8 AND `orbitIndex` IS NULL AND `itemName` REGEXP ' Moon [0-9]+$';")
    down = "UPDATE `mapDenormalize` SET `orbitIndex` = NULL WHERE `groupID` = 8;"
    p.derived.append((up, down, keys, 'orbitIndex'))
    p.notes.append('orbitIndex of %d moons set from the number at the end of their name (one statement)' % len(keys))


# ---------------------------------------------------------------------------------------------------------------------
# sql

class Ops(object):
    def __init__(self, plan):
        p = plan
        derived = {}
        for _, _, keys, col in p.derived:
            for k in keys:
                derived.setdefault(k, set()).add(col)
        self.base = p.orig
        self.inserted = [k for k in p.work if k not in p.orig]
        self.deleted = [k for k in p.orig if k not in p.work]
        self.updated = [k for k in p.work if k in p.orig and p.changed.get(k)]
        self.upd_cols = sorted({c for k in self.updated for c in p.changed[k]}, key=p.table.columns.index)
        self.derived_count = sum(len(v) for v in derived.values())


def check_values(plan, ops):
    for k in ops.updated:
        for c in plan.changed[k]:
            why = fits(plan.types[c], plan.work[k][c])
            if why:
                plan.problems.append('%s %s.%s = %r: %s' % (plan.name, k, c, plan.work[k][c], why))
    for k in ops.inserted:
        for c in plan.insert_columns():
            why = fits(plan.types[c], plan.work[k][c])
            if why:
                plan.problems.append('%s %s (new).%s = %r: %s' % (plan.name, k, c, plan.work[k][c], why))


def temp_table(plan, name, lead, cols, rows):
    """CREATE TEMPORARY TABLE name (lead defs, key, cols) and the INSERTs that fill it with rows (lists of values)"""
    p = plan
    defs = list(lead)
    for c in p.key:
        defs.append('%s %s NOT NULL' % (q(c), p.types[c]))
    for c in cols:
        ctype = p.types[c]
        if is_text(ctype):
            # text, whatever the column's length:  a copy of a wide varchar would break the row size limit, and
            # check_values() already held every value to the real column
            ctype = ('MEDIUMTEXT' if C.base_type(ctype) in ('mediumtext', 'longtext') else 'TEXT') + \
                ' CHARACTER SET utf8mb4'
        defs.append('%s %s NULL' % (q(c), ctype))
    defs.append('PRIMARY KEY (%s)' % ', '.join(q(c) for c in p.key))
    out = ['CREATE TEMPORARY TABLE %s (\n    %s\n);' % (q(name), ',\n    '.join(defs))]
    names = [d.split()[0].strip('`') for d in lead] + p.key + cols
    head = 'INSERT INTO %s (%s) VALUES\n' % (q(name), ', '.join(q(c) for c in names))
    chunk, size = [], 0
    for vals in rows:
        line = '(' + ', '.join(vals) + ')'
        if chunk and size + len(line) > MAX_STATEMENT:
            out.append(head + ',\n'.join(chunk) + ';')
            chunk, size = [], 0
        chunk.append(line)
        size += len(line) + 2
    if chunk:
        out.append(head + ',\n'.join(chunk) + ';')
    return out


def value_tables(plan, ops, inverse):
    """the statements that apply one table's changes:  cs_<table> holds the changed columns of updated rows ('u',
    only the columns its mask names are set) and the keys of rows to delete ('d');  csn_<table> holds whole new rows.
    Up writes the new values, Down the old ones, deletes the rows Up added and puts back the rows Up removed"""
    p = plan
    out = []
    on = ' AND '.join('t.%s = s.%s' % (q(c), q(c)) for c in p.key)
    to_insert = ops.deleted if inverse else ops.inserted
    to_delete = ops.inserted if inverse else ops.deleted

    def keyvals(k):
        src = p.work.get(k) or p.orig.get(k)
        return [sql_value(p.val(c, src[c])) for c in p.key]

    if ops.updated or to_delete:
        t = 'cs_' + p.name
        ucols = ops.upd_cols
        masks = {}
        rows = []
        for k in ops.updated:
            cols = sorted(p.changed[k], key=ucols.index)
            bit = 0
            for c in cols:
                bit |= 1 << ucols.index(c)
            masks.setdefault(bit, cols)
            src = p.orig[k] if inverse else p.work[k]
            rows.append([sql_value('u'), str(bit)] + keyvals(k) +
                        [sql_value(p.val(c, src[c])) if c in p.changed[k] else 'NULL' for c in ucols])
        for k in to_delete:
            rows.append([sql_value('d'), '0'] + keyvals(k) + ['NULL'] * len(ucols))
        out += temp_table(p, t, ['`op` CHAR(1) NOT NULL', '`mask` BIGINT UNSIGNED NOT NULL DEFAULT 0'], ucols, rows)
        if to_delete:
            out.append("DELETE t FROM %s t JOIN %s s ON %s WHERE s.`op` = 'd';" % (q(p.name), q(t), on))
        for bit, cols in sorted(masks.items()):
            out.append("UPDATE %s t JOIN %s s ON %s\n    SET %s\n    WHERE s.`op` = 'u' AND s.`mask` = %d;"
                       % (q(p.name), q(t), on, ', '.join('t.%s = s.%s' % (q(c), q(c)) for c in cols), bit))
        out.append('DROP TEMPORARY TABLE %s;' % q(t))
    if to_insert:
        t = 'csn_' + p.name
        icols = p.insert_columns()
        vcols = [c for c in icols if c not in p.key]
        rows = []
        for k in to_insert:
            src = p.orig[k] if inverse else p.work[k]
            rows.append(keyvals(k) + [sql_value(p.val(c, src.get(c))) for c in vcols])
        out += temp_table(p, t, [], vcols, rows)
        out.append("INSERT INTO %s (%s)\n    SELECT %s FROM %s;"
                   % (q(p.name), ', '.join(q(c) for c in p.key + vcols), ', '.join(q(c) for c in p.key + vcols), q(t)))
        out.append('DROP TEMPORARY TABLE %s;' % q(t))
    return out


def table_sql(plan, ops, inverse):
    p = plan
    out = []
    if not inverse:
        for col, ddl in p.add_columns.items():
            out.append('ALTER TABLE %s ADD COLUMN %s %s;' % (q(p.name), q(col), ddl))
        for col, (ddl, _) in p.modify_columns.items():
            out.append('ALTER TABLE %s MODIFY COLUMN %s %s;' % (q(p.name), q(col), ddl))
        for up, _, _, _ in p.derived:
            out.append(up)
    if ops.updated or ops.inserted or ops.deleted:
        out.extend(value_tables(p, ops, inverse))
    if inverse:
        for _, down, _, _ in p.derived:
            out.append(down)
        for col, (_, old) in p.modify_columns.items():
            if old:
                out.append('ALTER TABLE %s MODIFY COLUMN %s %s;' % (q(p.name), q(col), old))
            else:
                out.append('-- %s.%s stays wide:  rows written since may need the room' % (p.name, col))
        for col in p.add_columns:
            out.append('ALTER TABLE %s DROP COLUMN %s;' % (q(p.name), q(col)))
    return out


def describe(plan, ops):
    sp = plan.specs[0]
    srcs = ', '.join(('%d.cache2' % s.source) if isinstance(s.source, int) else 'mapbulk.db' for s in plan.specs)
    parts = []
    if ops.updated or ops.derived_count:
        parts.append('%d rows changed' % (len(set(ops.updated)) + ops.derived_count))
    if ops.inserted:
        parts.append('%d added' % len(ops.inserted))
    if ops.deleted:
        parts.append('%d removed' % len(ops.deleted))
    cols = ', '.join('%s %d' % kv for kv in plan.col_changes.most_common())
    return '%s (from %s): %s%s' % (plan.name, srcs, ', '.join(parts) or 'no change',
                                   ('; columns: ' + cols) if cols else '')


def write_group(index, group, plans, out_dir):
    path = os.path.join(out_dir, file_name(index, group))
    body = [HEADER + ' -- do not edit, change the generator and run it again',
            '-- the %s tables, brought in line with the Crucible client\'s bulkdata' % group]
    up, down, summary = [], [], []
    for p in plans:
        ops = Ops(p)
        if not (ops.updated or ops.inserted or ops.deleted or p.derived or p.add_columns or p.modify_columns):
            summary.append(describe(p, ops))
            continue
        summary.append(describe(p, ops))
        up.append('-- ' + describe(p, ops))
        up.extend(table_sql(p, ops, False))
        up.append('')
        down.append('-- %s' % p.name)
        down.extend(table_sql(p, ops, True))
        down.append('')
    if not up:
        if os.path.exists(path):
            os.remove(path)
        return None, summary
    for s in summary:
        body.append('--   ' + s)
    body += ['', '-- +migrate Up', ''] + up + ['-- +migrate Down', ''] + list(reversed_blocks(down))
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(body).rstrip('\n') + '\n')
    return path, summary


def reversed_blocks(lines):
    """the Down blocks in the reverse order of the Up blocks"""
    blocks, cur = [], []
    for line in lines:
        cur.append(line)
        if line == '':
            blocks.append(cur)
            cur = []
    if cur:
        blocks.append(cur)
    for b in reversed(blocks):
        for line in b:
            yield line


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--client', required=True, help='the EVE client folder (the one with bulkdata/)')
    ap.add_argument('--out', default=MIGRATIONS)
    ap.add_argument('--summary', help='also write the summary to this file')
    a = ap.parse_args()

    ctx = Context(a.client)
    for spec in specs.SPECS:
        apply_spec(ctx, spec)
        sys.stderr.write('  %s\n' % spec.table)
    post_expressions(ctx)
    post_map(ctx)

    problems = []
    for p in ctx.plans.values():
        check_values(p, Ops(p))
        problems.extend(p.problems)
    if problems:
        sys.stderr.write('values that do not fit their columns:\n  ' + '\n  '.join(problems[:200]) + '\n')
        raise SystemExit(1)

    lines = []
    for i, group in enumerate(specs.GROUPS):
        plans = [p for p in ctx.plans.values() if p.specs[0].group == group]
        path, summary = write_group(i, group, plans, a.out)
        lines.append('%s%s' % (group, (' -> ' + os.path.basename(path)) if path else ' (no change)'))
        for p, s in zip(plans, summary):
            lines.append('    ' + s)
            for n in p.notes:
                lines.append('        ' + n)
    text = '\n'.join(lines) + '\n'
    sys.stdout.write(text)
    if a.summary:
        with open(a.summary, 'w', encoding='utf-8') as f:
            f.write(text)


if __name__ == '__main__':
    main()
