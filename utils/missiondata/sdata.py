"""
sdata.py - static data for the mission data tools:  table loader for the sql/base dumps and npc/item name resolution.
Standard library only.
"""

import difflib
import gzip
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BASE = os.path.join(ROOT, 'sql', 'base')

CAT_CELESTIAL, CAT_SHIP, CAT_ENTITY, CAT_ASTEROID, CAT_STRUCTURE = 2, 6, 11, 25, 23
NPC_CATS = (CAT_ENTITY, CAT_CELESTIAL, CAT_STRUCTURE, CAT_ASTEROID)

# reporters' names for the empire navies
WORD_SUBS = [('amarr navy ', 'imperial navy '), ('amarr ', 'imperial '), ('caldari ', 'state '),
             ('gallente navy ', 'federation navy '), ('gallente ', 'federation '), ('minmatar fleet ', 'republic fleet '),
             ('minmatar ', 'republic '), ('khanid navy ', 'khanid '), ('ammatar ', 'ammatar navy ')]

# name prefixes of the npcs of a faction (deadspace, asteroid and mission ships), tried for short names ('Fanatic')
FACTION_PREFIXES = {
    500011: ['Gist', 'Gistum', 'Gistii', 'Gistior', 'Gistatis', 'Arch Gistum', 'Arch Gistii', 'Angel', 'Domination'],
    500012: ['Corpus', 'Corpum', 'Corpii', 'Corpior', 'Corpatis', 'Dark Corpus', 'Blood', 'Dark Blood'],
    500010: ['Pith', 'Pithum', 'Pithi', 'Pithior', 'Pithatis', 'Dire Pithi', 'Guristas', 'Dread Guristas'],
    500019: ['Centus', 'Centum', 'Centii', 'Centior', 'Centatis', "Sansha's", "True Sansha's", 'Sansha'],
    500020: ['Core', 'Corelum', 'Coreli', 'Corelior', 'Corelatis', 'Serpentis', 'Shadow Serpentis'],
    500022: ['Infested', 'Strain'],
    500003: ['Imperial Navy', 'Imperial', 'Amarr', 'Imperial Templar'],
    500007: ['Ammatar Navy', 'Ammatar'],
    500008: ['Khanid', 'Khanid Navy'],
    500001: ['State', 'Caldari Navy', 'Caldari'],
    500004: ['Federation', 'Federation Navy', 'Federation Praktor', 'Gallente'],
    500002: ['Republic', 'Republic Fleet', 'Republic Tribal', 'Minmatar'],
    500018: ["Mordu's Legion", "Mordu's"],
    0: ['Mercenary'],
}


def parse_values(s, i):
    """the rows of one INSERT ... VALUES list starting at s[i]"""
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
            while s[i] == ' ':
                i += 1
            if s[i] == "'":
                i += 1
                buf = []
                while True:
                    c = s[i]
                    if c == '\\':
                        nx = s[i + 1]
                        buf.append({'0': '\0', 'n': '\n', 'r': '\r', 't': '\t'}.get(nx, nx))
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
                while s[j] not in ',)':
                    j += 1
                tok = s[i:j].strip()
                i = j
                if tok == 'NULL':
                    row.append(None)
                else:
                    try:
                        row.append(int(tok))
                    except ValueError:
                        try:
                            row.append(float(tok))
                        except ValueError:
                            row.append(tok)
            while s[i] == ' ':
                i += 1
            if s[i] == ',':
                i += 1
                continue
            if s[i] == ')':
                i += 1
                break
        rows.append(row)
    return rows


def load_table(name, path=None):
    """list of dicts (column -> value) from sql/base/<name>.sql.gz"""
    data = gzip.open(path or os.path.join(BASE, name + '.sql.gz'), 'rt', encoding='utf-8', errors='replace').read()
    m = re.search(r'CREATE TABLE `%s` \((.*?)\n\)' % name, data, re.S)
    cols = re.findall(r'^\s+`(\w+)`', m.group(1), re.M)
    out = []
    for im in re.finditer(r'INSERT INTO `%s`(?: \([^)]*\))? VALUES\s*' % name, data):
        for row in parse_values(data, im.end()):
            if len(row) == len(cols):
                out.append(dict(zip(cols, row)))
    return out


def norm(name):
    """comparison key of a name:  lower case, single spaces, no punctuation but apostrophes"""
    name = name.replace('’', "'").replace('`', "'")
    name = re.sub(r"[^a-z0-9' ]+", ' ', name.lower())
    return re.sub(r'\s+', ' ', name).strip()


def title_key(name):
    """mission title key:  'The Blockade' == 'Blockade, The' == 'blockade'"""
    k = norm(name)
    m = re.match(r'^(.*?) (the|a|an)$', k)
    if m:
        k = m.group(1)
    k = re.sub(r'^(the|a|an) ', '', k)
    return k


class StaticData:
    def __init__(self, types_path=None, groups_path=None):
        groups = {g['groupID']: g for g in load_table('invGroups', groups_path)}
        self.types = {}
        self.by_name = {}           # norm(name) -> [typeID]
        for t in load_table('invTypes', types_path):
            g = groups.get(t['groupID'])
            t['categoryID'] = g['categoryID'] if g else 0
            t['groupName'] = g['groupName'] if g else ''
            self.types[t['typeID']] = t
            if t['typeName']:
                self.by_name.setdefault(norm(t['typeName']), []).append(t['typeID'])
        self.entity_names = sorted(k for k, ids in self.by_name.items()
                                   if any(self.types[i]['categoryID'] in (CAT_ENTITY, CAT_CELESTIAL, CAT_STRUCTURE) for i in ids))
        self.aliases = {}

    def load_aliases(self, path):
        if path and os.path.exists(path):
            with open(path, encoding='utf-8') as f:
                for k, v in json.load(f).items():
                    if not k.startswith('_'):
                        self.aliases[norm(k)] = v

    def pick(self, ids, cats):
        """the best type of a name:  wanted categories first, then the lowest typeID"""
        good = [i for i in ids if self.types[i]['categoryID'] in cats]
        return min(good) if good else None

    def resolve(self, name, cats=(CAT_ENTITY, CAT_CELESTIAL, CAT_STRUCTURE), fuzzy=True, prefixes=()):
        """typeID of an npc/structure/item name, or None.  aliases.json values are type names or typeIDs.
         prefixes:  name prefixes of the enemy faction tried when the name alone is unknown ('Fanatic' -> 'Corpatis Fanatic')"""
        t = self._resolve(name, cats, False)
        if t is not None:
            return t
        k = norm(name)
        for old, new in WORD_SUBS:
            if k.startswith(old):
                t = self._resolve(new + k[len(old):], cats, False)
                if t is not None:
                    return t
        bare = re.sub(r'^(starbase|deadspace|pirate|npc) ', '', k)
        for p in prefixes:
            t = self._resolve(p + ' ' + bare, cats, False)
            if t is not None:
                return t
        return self._resolve(name, cats, fuzzy) if fuzzy else None

    def _resolve(self, name, cats, fuzzy):
        k = norm(name)
        if not k:
            return None
        if k in self.aliases:
            v = self.aliases[k]
            if v is None:
                return None
            if isinstance(v, int):
                return v
            k = norm(v)
        if k in self.by_name:
            t = self.pick(self.by_name[k], cats)
            if t is not None:
                return t
        # plurals ('Gistior Shatterers', 'Gist Warlords')
        for cand in (re.sub(r'ies$', 'y', k), re.sub(r'es$', '', k), re.sub(r's$', '', k)):
            if cand != k and cand in self.by_name:
                t = self.pick(self.by_name[cand], cats)
                if t is not None:
                    return t
        if fuzzy and (CAT_ENTITY in cats):
            m = difflib.get_close_matches(k, self.entity_names, n=1, cutoff=0.9)
            if m:
                return self.pick(self.by_name[m[0]], cats)
        return None

    def name(self, type_id):
        t = self.types.get(type_id)
        return t['typeName'] if t else str(type_id)

    def category(self, type_id):
        t = self.types.get(type_id)
        return t['categoryID'] if t else 0

    def group_name(self, type_id):
        t = self.types.get(type_id)
        return t['groupName'] if t else ''

    def is_npc_ship(self, type_id):
        """an npc ship (not a sentry, tower, bunker or other structure entity)"""
        t = self.types.get(type_id)
        if t is None or t['categoryID'] != CAT_ENTITY:
            return False
        g = t['groupName'].lower()
        return not any(w in g for w in ('sentry', 'tower', 'structure', 'bunker', 'battery', 'wall', 'barricade',
                                         'outpost', 'stargate', 'station', 'habitat', 'container', 'beacon', 'spawn',
                                         'cloud', 'billboard', 'gate', 'array', 'deadspace overseer'))
