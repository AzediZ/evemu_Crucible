"""
serverdata.py - the repo's static tables as the database has them after a fresh install:  the sql/base dumps plus what
the Up sections of sql/migrations do to them.  Stdlib only.

    t = serverdata.load('invTypes')
    t.columns      ['typeID', 'groupID', ...]
    t.types        {'typeID': 'smallint(5)', ...}
    t.pk           ['typeID']        (empty when the table has none)
    t.rows         [ {col: value}, ... ]

Migration statements that are understood:  INSERT [IGNORE] INTO t [(cols)] VALUES ...,  REPLACE INTO ...,
UPDATE t SET c = v[, ...] WHERE c = v [AND c = v ...] / WHERE c IN (...),  DELETE FROM t WHERE <same>.
Anything else that names a loaded table is listed in t.unhandled so a caller can deal with it by hand.
"""

import gzip
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BASE = os.path.join(ROOT, 'sql', 'base')
MIGRATIONS = os.path.join(ROOT, 'sql', 'migrations')


def parse_values(s, i):
    """the rows of one VALUES list starting at s[i];  returns (rows, end index)"""
    rows = []
    n = len(s)
    while i < n:
        while i < n and s[i] in ' ,\n\r\t':
            i += 1
        if i >= n or s[i] != '(':
            break
        i += 1
        row = []
        while True:
            while s[i] in ' \n\r\t':
                i += 1
            if s[i] == "'":
                i += 1
                buf = []
                while True:
                    c = s[i]
                    if c == '\\':
                        nx = s[i + 1]
                        buf.append({'0': '\0', 'n': '\n', 'r': '\r', 't': '\t', 'Z': '\x1a', 'b': '\b'}.get(nx, nx))
                        i += 2
                    elif c == "'":
                        if i + 1 < n and s[i + 1] == "'":
                            buf.append("'")
                            i += 2
                        else:
                            i += 1
                            break
                    else:
                        buf.append(c)
                        i += 1
                row.append(''.join(buf))
            else:
                j = i
                depth = 0
                while depth or s[j] not in ',)':
                    if s[j] == '(':
                        depth += 1
                    elif s[j] == ')':
                        depth -= 1
                    j += 1
                tok = s[i:j].strip()
                i = j
                row.append(_literal(tok))
            while s[i] in ' \n\r\t':
                i += 1
            if s[i] == ',':
                i += 1
                continue
            if s[i] == ')':
                i += 1
                break
        rows.append(row)
    return rows, i


def _literal(tok):
    if tok.upper() == 'NULL':
        return None
    if re.match(r"^_binary\s*'", tok):
        return tok[tok.index("'") + 1:-1]
    if re.match(r"^b'[01]+'$", tok):
        return int(tok[2:-1], 2)
    try:
        return int(tok)
    except ValueError:
        pass
    try:
        return float(tok)
    except ValueError:
        return tok


class Table(object):
    def __init__(self, name, columns, types, pk):
        self.name = name
        self.columns = columns
        self.types = types
        self.pk = pk
        self.rows = []
        self.unhandled = []
        self.sources = []

    def lc(self, col):
        """the table's spelling of a column name, matched case-insensitively"""
        for c in self.columns:
            if c.lower() == col.lower():
                return c
        return None


def _statements(sql):
    """the statements of a script, comments removed (quotes respected)"""
    out = []
    buf = []
    i = 0
    n = len(sql)
    while i < n:
        c = sql[i]
        if c in "'`\"":
            j = i + 1
            while j < n:
                if sql[j] == '\\' and c != '`':
                    j += 2
                    continue
                if sql[j] == c:
                    if j + 1 < n and sql[j + 1] == c:
                        j += 2
                        continue
                    break
                j += 1
            buf.append(sql[i:j + 1])
            i = j + 1
            continue
        if c == '-' and sql.startswith('--', i) and (i + 2 >= n or sql[i + 2] in ' \t\r\n'):
            j = sql.find('\n', i)
            i = n if j < 0 else j
            continue
        if c == '#':
            j = sql.find('\n', i)
            i = n if j < 0 else j
            continue
        if c == '/' and sql.startswith('/*', i):
            j = sql.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == ';':
            st = ''.join(buf).strip()
            if st:
                out.append(st)
            buf = []
            i += 1
            continue
        buf.append(c)
        i += 1
    st = ''.join(buf).strip()
    if st:
        out.append(st)
    return out


def _create_table(data, name):
    m = re.search(r'CREATE TABLE (?:IF NOT EXISTS )?`?%s`?\s*\((.*?)\n\)\s*(?:ENGINE|;)' % re.escape(name), data, re.S | re.I)
    if not m:
        return None
    body = m.group(1)
    columns, types, pk, notnull, defaults, autoinc = [], {}, [], set(), {}, set()
    for line in body.splitlines():
        line = line.strip().rstrip(',')
        cm = re.match(r'`(\w+)`\s+(\S+(?:\s+unsigned)?)', line)
        if cm:
            col = cm.group(1)
            columns.append(col)
            types[col] = cm.group(2).lower()
            if re.search(r'NOT NULL', line, re.I):
                notnull.add(col)
            dm = re.search(r"DEFAULT\s+('(?:[^'\\]|\\.|'')*'|b'[01]+'|[^\s,]+)", line, re.I)
            if dm:
                lit = dm.group(1)
                defaults[col] = parse_values('(' + lit + ')', 0)[0][0][0] if lit.startswith("'") else _literal(lit)
            if re.search(r'AUTO_INCREMENT', line, re.I):
                autoinc.add(col)
            continue
        pm = re.match(r'PRIMARY KEY\s*\((.*?)\)', line, re.I)
        if pm:
            pk = [c.strip().strip('`') for c in pm.group(1).split(',')]
    t = Table(name, columns, types, pk)
    t.notnull = notnull
    t.defaults = defaults
    t.autoinc = autoinc
    return t


def _apply(table, st):
    """apply one migration statement to table;  False when it names the table but is not understood"""
    name = re.escape(table.name)
    m = re.match(r'(INSERT(?:\s+IGNORE)?|REPLACE)\s+INTO\s+`?%s`?\s*(\([^)]*\))?\s*VALUES\s*' % name, st, re.I)
    if m:
        cols = table.columns
        if m.group(2):
            cols = [table.lc(c.strip().strip('`')) for c in m.group(2)[1:-1].split(',')]
        rows, end = parse_values(st, m.end())
        tail = st[end:].strip()
        upd = None
        if tail:
            um = re.match(r'ON DUPLICATE KEY UPDATE\s+(.*)$', tail, re.I | re.S)
            if not um:
                return False
            upd = [table.lc(c) for c in re.findall(r'`?(\w+)`?\s*=\s*VALUES\s*\(', um.group(1), re.I)]
        replace = m.group(1).upper() == 'REPLACE'
        index = _index(table)
        for r in rows:
            new = dict.fromkeys(table.columns)
            new.update(zip(cols, r))
            k = _key(table, new)
            if k is not None and k in index:
                old = index[k]
                if replace:
                    old.clear()
                    old.update(new)
                elif upd:
                    for c in upd:
                        old[c] = new[c]
                continue
            table.rows.append(new)
            if k is not None:
                index[k] = new
        return True
    m = re.match(r'UPDATE\s+`?%s`?\s+SET\s+(.*?)(?:\s+WHERE\s+(.*))?$' % name, st, re.I | re.S)
    if m and m.group(2) is None:
        sets = _assignments(table, m.group(1))
        if sets is None:
            return False
        for r in table.rows:
            for c, v in sets.items():
                r[c] = r[v.col] if isinstance(v, _ColumnRef) else v
        return True
    if m:
        sets = _assignments(table, m.group(1))
        where = _where(table, m.group(2))
        if sets is None or where is None:
            return False
        for r in table.rows:
            if where(r):
                for c, v in sets.items():
                    r[c] = r[v.col] if isinstance(v, _ColumnRef) else v
        return True
    # UPDATE t r JOIN invTypes x ON x.typeID = r.col SET r.c = v WHERE r.c2 = v2 AND x.groupID = g
    m = re.match(r'UPDATE\s+`?%s`?\s+(\w+)\s+JOIN\s+`?invTypes`?\s+(\w+)\s+ON\s+(\w+)\.typeID\s*=\s*(\w+)\.(\w+)\s+'
                 r'SET\s+(\w+)\.(\w+)\s*=\s*(\S+)\s+WHERE\s+(\w+)\.(\w+)\s*=\s*(\S+)\s+AND\s+(\w+)\.groupID\s*=\s*(\d+)$'
                 % name, st.strip(), re.I | re.S)
    if m and m.group(2) == m.group(3) == m.group(12) and m.group(1) == m.group(4) == m.group(6) == m.group(9):
        col, setcol, setval = table.lc(m.group(5)), table.lc(m.group(7)), _literal(m.group(8))
        wcol, wval, group = table.lc(m.group(10)), _literal(m.group(11)), int(m.group(13))
        groups = {r['typeID']: r['groupID'] for r in load('invTypes').rows}
        for r in table.rows:
            if r.get(wcol) == wval and groups.get(r.get(col)) == group:
                r[setcol] = setval
        return True
    m = re.match(r'DELETE\s+FROM\s+`?%s`?\s+WHERE\s+(.*)$' % name, st, re.I | re.S)
    if m:
        where = _where(table, m.group(1))
        if where is None:
            return False
        table.rows = [r for r in table.rows if not where(r)]
        return True
    if re.match(r'(ALTER|CREATE|DROP)\s', st, re.I):
        return _alter(table, st)
    return False


def _alter(table, st):
    m = re.match(r'ALTER\s+TABLE\s+`?%s`?\s+(.*)$' % re.escape(table.name), st, re.I | re.S)
    if not m:
        return re.match(r'CREATE\s+TABLE\s+IF\s+NOT\s+EXISTS', st, re.I) is not None
    ok = True
    for part in re.split(r',\s*(?=ADD|DROP|MODIFY|CHANGE)', m.group(1), flags=re.I):
        am = re.match(r'ADD\s+(?:COLUMN\s+)?`?(\w+)`?\s+(\S+(?:\s+unsigned)?)(.*)$', part.strip(), re.I | re.S)
        if am and am.group(1).upper() not in ('INDEX', 'KEY', 'UNIQUE', 'PRIMARY', 'CONSTRAINT'):
            col = am.group(1)
            dm = re.search(r"DEFAULT\s+('(?:[^']*)'|\S+)", am.group(3), re.I)
            default = _literal(dm.group(1).strip("'")) if dm else None
            after = re.search(r'\bAFTER\s+`?(\w+)`?', am.group(3), re.I)
            if after and table.lc(after.group(1)):
                table.columns.insert(table.columns.index(table.lc(after.group(1))) + 1, col)
            elif re.search(r'\bFIRST\b', am.group(3), re.I):
                table.columns.insert(0, col)
            else:
                table.columns.append(col)
            table.types[col] = am.group(2).lower()
            if re.search(r'NOT NULL', am.group(3), re.I):
                table.notnull.add(col)
            for r in table.rows:
                r[col] = default
            continue
        if re.match(r'(ADD|DROP)\s+(INDEX|KEY|UNIQUE)', part.strip(), re.I):
            continue
        mm = re.match(r'MODIFY\s+(?:COLUMN\s+)?`?(\w+)`?\s+(\S+(?:\s+unsigned)?)(.*)$', part.strip(), re.I | re.S)
        if mm and table.lc(mm.group(1)):
            col = table.lc(mm.group(1))
            table.types[col] = mm.group(2).lower()
            if re.search(r'NOT NULL', mm.group(3), re.I):
                table.notnull.add(col)
            else:
                table.notnull.discard(col)
            continue
        dm = re.match(r'DROP\s+(?:COLUMN\s+)?`?(\w+)`?\s*$', part.strip(), re.I)
        if dm and table.lc(dm.group(1)):
            col = table.lc(dm.group(1))
            table.columns.remove(col)
            table.types.pop(col, None)
            for r in table.rows:
                r.pop(col, None)
            continue
        ok = False
    return ok


class _ColumnRef(object):
    def __init__(self, col):
        self.col = col


def _index(table):
    if not table.pk:
        return {}
    return {_key(table, r): r for r in table.rows}


def _key(table, row):
    if not table.pk:
        return None
    return tuple(row.get(c) for c in table.pk)


def _assignments(table, text):
    out = {}
    for part in re.split(r',(?=(?:[^\']*\'[^\']*\')*[^\']*$)', text):
        am = re.match(r'\s*`?(\w+)`?\s*=\s*(.+?)\s*$', part, re.S)
        if not am:
            return None
        col = table.lc(am.group(1))
        val = am.group(2)
        if col is None:
            return None
        if val.startswith("'"):
            val = parse_values('(' + val + ')', 0)[0][0][0]
        elif re.match(r'^-?[\d.eE+-]+$|^NULL$', val, re.I):
            val = _literal(val)
        elif table.lc(val.strip('`')):
            val = _ColumnRef(table.lc(val.strip('`')))
        else:
            return None
        out[col] = val
    return out


def _where(table, text):
    conds = []
    for part in re.split(r'\s+AND\s+', text.strip(), flags=re.I):
        part = part.strip()
        m = re.match(r'`?(\w+)`?\s*=\s*(.+)$', part)
        if m:
            col = table.lc(m.group(1))
            if col is None:
                return None
            val = m.group(2).strip()
            val = parse_values('(' + val + ')', 0)[0][0][0] if val.startswith("'") else _literal(val)
            conds.append((col, {val}))
            continue
        m = re.match(r'`?(\w+)`?\s+IN\s*\((.*)\)$', part, re.I | re.S)
        if m:
            col = table.lc(m.group(1))
            if col is None:
                return None
            conds.append((col, set(parse_values('(' + m.group(2) + ')', 0)[0][0])))
            continue
        return None
    return lambda r: all(r.get(c) in vals for c, vals in conds)


def migration_files():
    for fn in sorted(os.listdir(MIGRATIONS)):
        p = os.path.join(MIGRATIONS, fn)
        if fn.endswith('.sql') and os.path.isfile(p):
            yield fn, p


def up_section(text):
    return re.split(r'^\s*--\s*\+migrate\s+Down', text, flags=re.M | re.I)[0]


_cache = {}


SKIP_HEADER = '-- generated by utils/bulkdata/gen_client_sync.py'


def load(name, skip_migrations=()):
    """the table as a fresh install has it, without the migrations gen_client_sync.py wrote;  skip_migrations: more
    file names to leave out"""
    key = (name, tuple(skip_migrations))
    if key in _cache:
        return _cache[key]
    path = os.path.join(BASE, name + '.sql.gz')
    data = gzip.open(path, 'rt', encoding='utf-8', errors='replace', newline='').read()
    table = _create_table(data, name)
    table.sources.append(name + '.sql.gz')
    pat = re.compile(r'INSERT\s+(?:IGNORE\s+)?INTO\s+`?%s`?\s*(\([^)]*\))?\s*VALUES\s*' % re.escape(name), re.I)
    for im in pat.finditer(data):
        cols = table.columns
        if im.group(1):
            cols = [c.strip().strip('`') for c in im.group(1)[1:-1].split(',')]
        rows, _ = parse_values(data, im.end())
        for r in rows:
            row = dict.fromkeys(table.columns)
            row.update(zip(cols, r))
            table.rows.append(row)
    word = re.compile(r'`?\b%s\b`?' % re.escape(name), re.I)
    for fn, p in migration_files():
        if fn in skip_migrations:
            continue
        text = open(p, encoding='utf-8', errors='replace', newline='').read()
        if text.startswith(SKIP_HEADER):
            continue
        up = up_section(text)
        if not word.search(up):
            continue
        for st in _statements(up):
            head = st[:300]
            if not re.search(r'(INTO|UPDATE|FROM|TABLE)\s+`?%s`?\b' % re.escape(name), head, re.I):
                continue
            if _apply(table, st):
                if fn not in table.sources:
                    table.sources.append(fn)
            else:
                table.unhandled.append((fn, st[:200]))
    _cache[key] = table
    return table


_CS_TABLE = re.compile(r'CREATE TEMPORARY TABLE `(csn?)_(\w+)` \((.*?)\n\);', re.S)


def synced_files():
    """the migrations gen_client_sync.py wrote, in order"""
    for fn, p in migration_files():
        with open(p, encoding='utf-8', errors='replace', newline='') as f:
            if f.read(len(SKIP_HEADER)) == SKIP_HEADER:
                yield fn, p


def load_synced(name):
    """the table as a fresh install has it with the client-sync migrations too:  load() plus the rows those files stage
    in their temporary tables:  csn_<table> holds whole new rows;  in cs_<table> op 'i' inserts a row, 'd' deletes it,
    'u' sets the columns whose bit is set in mask (bit i = the i-th non-key column of cs_<table>).  The sync files are
    replayed after every other migration, which is right as long as no later hand-written migration changes the same
    rows.  Returns a new Table (load()'s cached one is left alone);  t.sources lists the sync files that had rows for
    it."""
    key = ('synced', name)
    if key in _cache:
        return _cache[key]
    base = load(name)
    t = Table(base.name, list(base.columns), dict(base.types), list(base.pk))
    t.notnull, t.defaults, t.autoinc = set(base.notnull), dict(base.defaults), set(base.autoinc)
    t.rows = [dict(r) for r in base.rows]
    t.unhandled = list(base.unhandled)
    t.sources = list(base.sources)
    for fn, p in synced_files():
        up = up_section(open(p, encoding='utf-8', errors='replace', newline='').read())
        for m in _CS_TABLE.finditer(up):
            if m.group(2).lower() != name.lower():
                continue
            tmp = m.group(1) + '_' + m.group(2)
            body = m.group(3)
            cols = re.findall(r'^\s*`(\w+)`', body, re.M)
            pkm = re.search(r'PRIMARY KEY \(([^)]*)\)', body)
            pk = [c.strip().strip('`') for c in pkm.group(1).split(',')] if pkm else list(t.pk)
            data_cols = [c for c in cols if c not in ('op', 'mask') and c not in pk]
            index = {tuple(str(r.get(t.lc(k))) for k in pk): r for r in t.rows}
            pat = re.compile(r'INSERT INTO `%s` \(([^)]*)\) VALUES\s*' % re.escape(tmp))
            # the staging rows sit between this CREATE and the temporary table's DROP
            end = up.find('DROP TEMPORARY TABLE `%s`' % tmp, m.end())
            section = up[m.end():] if end < 0 else up[m.end():end]
            for im in pat.finditer(section):
                icols = [c.strip().strip('`') for c in im.group(1).split(',')]
                rows, _ = parse_values(section, im.end())
                for rv in rows:
                    r = dict(zip(icols, rv))
                    k = tuple(str(r[c]) for c in pk)
                    op = r.get('op', 'i')
                    if op == 'd':
                        old = index.pop(k, None)
                        if old is not None:
                            t.rows.remove(old)
                    elif op == 'i':
                        row = dict.fromkeys(t.columns)
                        row.update(t.defaults)
                        for c in icols:
                            if c not in ('op', 'mask') and t.lc(c):
                                row[t.lc(c)] = r[c]
                        old = index.pop(k, None)
                        if old is not None:
                            t.rows.remove(old)
                        t.rows.append(row)
                        index[k] = row
                    else:
                        row = index.get(k)
                        if row is None:
                            continue
                        for bit, c in enumerate(data_cols):
                            if int(r['mask']) & (1 << bit) and t.lc(c):
                                row[t.lc(c)] = r.get(c)
                if fn not in t.sources:
                    t.sources.append(fn)
    _cache[key] = t
    return t
