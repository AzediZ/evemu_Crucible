#!/usr/bin/env python3
"""
parse_reports.py - turns the cached eve-survival.org mission reports (fetch_wayback.py) into out/reports.json

    python -I parse_reports.py [--cache cache/] [--aliases aliases.json] [--out out/reports.json]

Per page:  title and level, enemy faction, mission type, space type, pockets (rooms), their groups (waves) with the ship
lines resolved to invTypes (npc names, prefix inheritance 'Pith Usurper/Massacrer' -> 'Pith Massacrer', plurals,
aliases.json, close matches), structures, wave triggers (the TRIGGER ship of the previous wave, a cleared wave, a timer
or an attack), the inferred objective (explicit 'Objective:' line, 'triggers mission completion', 'destroy the <structure>',
'bring/retrieve <item>', else kill all) with a confidence, mining ore/volume, and the client title messageID
(agtMissions/qstMining name match).  Names that don't resolve go to out/unresolved.txt.
Reads sql/base/{invTypes,invGroups,agtMissions,qstMining,qstCourier}.sql.gz.  Standard library only.
"""

import argparse
import html
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdata   # noqa: E402

FACTIONS = [
    # (words in the page's faction text, factionID).  order matters ('Blood Raiders' before 'Amarr')
    (('angel',), 500011), (('blood',), 500012), (('gurista',), 500010), (('sansha',), 500019),
    (('serpentis',), 500020), (('rogue drone', 'drone'), 500022), (('mordu',), 500018),
    (('ammatar',), 500007), (('khanid',), 500008), (('amarr',), 500003), (('caldari',), 500001),
    (('gallente',), 500004), (('minmatar',), 500002), (('thukker',), 500015), (('concord', 'ded'), 500006),
    (('mercenar', 'merc'), 0), (('common',), 0),
]
TYPES = {'encounter': 2, 'kill': 2, 'combat': 2, 'deadspace': 2, 'courier': 3, 'trade': 4, 'mining': 5}

STOP = re.compile(r'^(blitz|tips?|hints?|notes?|tactics?|strategy|walk ?through|loot|salvage|rewards?|bounty|bounties|'
                  r'standings?|ships? detail|comments|video|example|summary|recommended|general|info|fitting|'
                  r'overview|warning|radar|magnetometric|cosmic|class \d)', re.I)
POCKET = re.compile(r'(pocket|room|stage|area|deadspace|single pocket|second|third|fourth|fifth|final pocket|'
                    r'gate|^part \d)', re.I)
GROUP = re.compile(r'(group|spawn|wave|reinforce|initial|warp.?in|structures?|sentr|stationary|asteroid|mining|'
                    r'variant|ships|enemies|npcs?|defen[dc]ers?|guards?|rats?|first|second|third|fourth|fifth|'
                    r'final|last|attackers?|ambush|trigger)', re.I)
INITIAL = re.compile(r'(initial|^group\s*\d*|^single group|warp.?in|^first group|^spawn\s*1\b|^wave\s*1\b|'
                     r'^structures?|^sentr|^stationary|at (the )?warp|^ships$|^defen|^guards?|^enemies|^npcs?)', re.I)
SHIP_LINE = re.compile(r'^\s*(\d+)\s*(?:x\s*)?(?:(?:-|–|/|to)\s*(\d+)\s*)?x\s*(.+)$', re.I)
SHIP_LINE2 = re.compile(r'^\s*(\d+)\s+(?!km|m\b|min|sec|minutes?|seconds?|jumps?|au\b)([A-Z].+)$')
CLASS_WORDS = re.compile(r'^(elite |pirate |faction |npc )?(frigates?|destroyers?|cruisers?|battle ?cruisers?|battleships?|'
                         r'bs|bc|hacs?|heavy assault( cruisers?| ships?)?|interceptors?|assault frigates?|'
                         r'industrials?|haulers?|towers?|sentr(y|ies)( guns?)?|structures?|drones?|commanders?|'
                         r'officers?|overseers?|transports?|capitals?|dreadnoughts?|carriers?|titans?|logistics|'
                         r'shuttles?|bunkers?|e?war( ship)?s?|ewar frigates?|ewar cruisers?|ships?)$', re.I)
DIST = re.compile(r'(\d+)\s*(?:-|–|to)?\s*(\d+)?\s*km', re.I)


def faction_of(text):
    t = text.lower()
    for words, fid in FACTIONS:
        if any(w in t for w in words):
            return fid
    return None


def page_text(raw):
    """content lines with heading markers ('#3 Single pocket')"""
    i = raw.find('id="content"')
    c = raw[i:] if i >= 0 else raw
    for end in ('class="commentsheader"', 'id="comments"', 'There are no comments', 'There is one comment',
                'There are '):
        j = c.find(end)
        if j > 0:
            c = c[:j]
            break
    c = re.sub(r'(?is)<script.*?</script>', '', c)
    c = re.sub(r'(?is)<div class="lastedit">.*?</div>', '\n', c)
    c = re.sub(r'(?is)<h([1-6])[^>]*>(.*?)</h\1>', lambda m: '\n\x01%s %s\n' % (m.group(1), m.group(2)), c)
    c = re.sub(r'(?i)<br\s*/?>', '\n', c)
    c = re.sub(r'(?i)</(p|li|tr|div|table|ul|ol)>', '\n', c)
    c = re.sub(r'(?i)</t[dh]>', ' | ', c)
    c = re.sub(r'(?i)<(strong|b)>\s*trigger\s*</(strong|b)>', ' TRIGGER ', c)
    c = re.sub(r'<[^>]+>', ' ', c)
    c = html.unescape(c).replace('\xa0', ' ').replace('’', "'")
    lines = []
    for ln in c.split('\n'):
        ln = re.sub(r'\s+', ' ', ln).strip(' |')
        if ln:
            lines.append(ln)
    return lines


class Parser:
    def __init__(self, sd):
        self.sd = sd
        self.unresolved = {}    # name -> set(pages)

    def miss(self, name, page):
        self.unresolved.setdefault(name, set()).add(page)

    def resolve_names(self, text, page, log=True, prefixes=()):
        """'Battleship (Gist Malakim/Gist Nephilim)' -> [typeIDs], or [] (also [] for plain class words)"""
        text = re.sub(r'\s+', ' ', text).strip(' .,:;-')
        cands = []
        m = re.match(r'^(.*?)\((.*?)\)\s*(.*)$', text)
        if m:
            outer, inner = m.group(1).strip(), m.group(2).strip()
            cands = [inner, outer] if CLASS_WORDS.match(outer) or not outer else [outer, inner]
        else:
            cands = [text]
        for cand in cands:
            if not cand or CLASS_WORDS.match(cand):
                continue
            parts = [p.strip() for p in re.split(r'\s*/\s*|\s+or\s+|\s*,\s*', cand) if p.strip()]
            if not parts:
                continue
            first = parts[0].split()
            ids = []
            for p in parts:
                t = self.sd.resolve(p, sdata.NPC_CATS, prefixes=prefixes)
                if t is None and len(p.split()) < len(first):
                    # 'Pith Usurper/Massacrer' -> 'Pith Massacrer'
                    t = self.sd.resolve(' '.join(first[:len(first) - len(p.split())] + p.split()), sdata.NPC_CATS)
                if (t is None) and (len(p.split()) > 1) and prefixes:
                    # a wrong faction prefix ('Corpior Bishop' is a Corpatis Bishop)
                    t = self.sd.resolve(' '.join(p.split()[1:]), sdata.NPC_CATS, fuzzy=False, prefixes=prefixes)
                if t is None:
                    if log:
                        self.miss(p, page)
                    continue
                ids.append(t)
            if ids:
                return ids
        return []

    def ship_lines(self, line, page, prefixes):
        """ship/structure entries of a line (one, or several:  '2x State Katana; 2x Imperial Sixtus',
         'Battleships (1x Amarr Navy Apocalypse, 2x Amarr Navy Armageddon)')"""
        parts = re.split(r'(?<=[;,(+])\s*(?=\d+\s*x\s)|\s+and\s+(?=\d+\s*x\s)|\s+\+\s+(?=\d)', line)
        if len(parts) > 1 and sum(1 for p in parts if re.match(r'\s*\d+\s*x\s', p)) > 1:
            trig = bool(re.search(r'\btrigger', line, re.I))
            out = []
            for p in parts:
                p = p.strip(' ;,()+')
                e = self.ship_line(p, page, prefixes)
                if e is not None:
                    out.append(e)
            if out and trig and not any(e['isTrigger'] for e in out):
                out[-1]['isTrigger'] = True
            return out
        e = self.ship_line(line, page, prefixes)
        return [e] if e is not None else []

    def ship_line(self, line, page, prefixes=()):
        """a ship/structure line -> dict, or None"""
        trigger = bool(re.search(r'\btrigger', line, re.I)) and not re.search(r'not (the )?trigger|no trigger', line, re.I)
        clean = re.sub(r'\bTRIGGER\b.*$', '', line, flags=re.I).strip(' ,-')
        m = SHIP_LINE.match(clean)
        qmin = qmax = 1
        if m:
            qmin = int(m.group(1))
            qmax = int(m.group(2)) if m.group(2) else qmin
            rest = m.group(3)
        else:
            m2 = SHIP_LINE2.match(clean)
            if m2 and int(m2.group(1)) < 50:
                qmin = qmax = int(m2.group(1))
                rest = m2.group(2)
            else:
                rest = clean
        if qmin > 60 or qmax > 60:
            return None
        if qmax < qmin:
            qmin, qmax = qmax, qmin
        # trailing notes after the names:  'Gist Throne), Explosive torpedoes'
        notes = ''
        if ')' in rest:
            k = rest.rfind(')')
            notes = rest[k + 1:]
            rest = rest[:k + 1]
        elif ',' in rest:
            rest, notes = rest.split(',', 1)
        rest = re.sub(r'\s*\[.*?\]', '', rest)
        if len(rest) > 80:
            return None
        # lines without a quantity count only when the whole line is a known name (structures:  'Starbase Stasis Tower')
        ids = self.resolve_names(rest, page, log=bool(m), prefixes=prefixes) if (m or len(rest.split()) <= 6) else []
        if not ids:
            return None
        return {'qtyMin': qmin, 'qtyMax': qmax, 'typeIDs': ids, 'isTrigger': trigger,
                'names': [self.sd.name(i) for i in ids], 'notes': notes.strip(' ,'),
                'objective': bool(re.search(r'mission (objective|completion|complete)|objective|completes the mission',
                                            line, re.I))}

    def parse(self, key, raw, meta, snapshot):
        lines = page_text(raw)
        rep = {'pageKey': key, 'snapshot': snapshot, 'source': 'mirror2017' if snapshot == 'mirror2017' else 'wayback',
               'title': '', 'name': '', 'level': 0, 'enemyFaction': '', 'enemyFactionID': None, 'missionType': '',
               'typeID': 2, 'spaceType': 0, 'damageDealt': '', 'storyline': False, 'arc': False, 'pockets': [],
               'objective': {'type': 'KillAll', 'typeID': 0, 'qty': 0, 'confidence': 'default', 'text': ''},
               'mining': None, 'notes': []}
        # title
        for ln in lines:
            if ln.startswith('\x011 '):
                rep['title'] = ln[3:].strip()
                break
        t = rep['title']
        m = re.match(r'^(.*?)[,\s-]*(?:\(?\s*level|lvl|\bl)\s*(\d)\)?\s*(.*)$', t, re.I)
        name = m.group(1) if m else t
        if m:
            rep['level'] = int(m.group(2))
        name = re.sub(r'\s*\((angel|blood|gurista|sansha|serpentis|rogue|drone|merc|amarr|caldari|gallente|minmatar|'
                      r'ammatar|khanid|mordu|thukker)[^)]*\)', '', name, flags=re.I).strip(' ,-')
        if meta.get('title'):
            name = meta['title']
        rep['name'] = name
        if not rep['level']:
            rep['level'] = meta.get('level') or 0
        if not rep['level']:
            d = re.search(r'(\d)(?=[a-z]*$)', key)
            rep['level'] = int(d.group(1)) if d else 0
        if re.search(r'\(\s*\d+\s*of\s*\d+\s*\)|part \d|\d+ of \d+', name + ' ' + key, re.I):
            rep['arc'] = True
        if 'storyline' in key.lower():
            rep['storyline'] = True

        # the enemy faction first:  its npc name prefixes resolve short names
        early = None
        for ln in lines:
            fm = re.match(r'^factions?\s*:\s*(.*)', ln, re.I)
            if fm:
                early = faction_of(fm.group(1))
                break
        if early is None and meta.get('faction'):
            early = faction_of(meta['faction'])
        prefixes = list(sdata.FACTION_PREFIXES.get(early if early is not None else -1, []))
        if early not in (None, 0):
            prefixes += sdata.FACTION_PREFIXES[0]

        pocket = None
        group = None
        in_variant = False
        stopped = False
        header_done = False
        text_buf = []

        def new_pocket(title):
            p = {'name': title, 'groups': [], 'structures': [], 'asteroids': [], 'text': []}
            rep['pockets'].append(p)
            return p

        def new_group(p, title, variant=False):
            dm = DIST.search(title)
            g = {'heading': title, 'variant': variant, 'ships': [], 'text': [],
                 'distKm': [int(dm.group(1)), int(dm.group(2) or dm.group(1))] if dm else None}
            p['groups'].append(g)
            return g

        for ln in lines[1:]:
            hm = re.match(r'^\x01(\d) (.*)$', ln)
            if hm:
                lvl, ht = int(hm.group(1)), hm.group(2).strip(' :')
                if (len(ht) <= 2) or (lvl == 1):
                    # the page title
                    continue
                # a chain described on one page:  only its first part is a mission of its own
                cm = re.search(r'\(\s*(\d+)\s*of\s*\d+\s*\)|^part (\d+)|(\d+) of \d+', ht, re.I)
                if cm and lvl <= 2:
                    part = int([g for g in cm.groups() if g][0])
                    rep['arc'] = True
                    rep['part'] = rep.get('part') or part
                    if part > 1:
                        break
                    continue
                if STOP.match(ht) and not re.search(r'spawn|wave|group|pocket|room', ht, re.I):
                    stopped = True
                    continue
                if POCKET.search(ht) and not re.search(r'spawn|wave|group\b|reinforce', ht, re.I):
                    pocket = new_pocket(ht)
                    group = None
                    stopped = False
                    header_done = True
                    continue
                if GROUP.search(ht) or re.match(r'^[A-Z]?\d+[a-z]?$', ht):
                    if pocket is None:
                        pocket = new_pocket('Single pocket')
                    variant = bool(re.match(r'^variant|^example|^option', ht, re.I))
                    group = new_group(pocket, ht, variant)
                    stopped = False
                    header_done = True
                    continue
                # unknown heading:  stop collecting ships under it
                stopped = True
                continue
            if stopped:
                continue
            low = ln.lower()
            if not header_done:
                for fld, pat in (('enemyFaction', r'^factions?\s*:\s*(.*)'), ('missionType', r'^mission type\s*:\s*(.*)'),
                                 ('space', r'^space ?type\s*:\s*(.*)'), ('damageDealt', r'^damage dealt\s*:\s*(.*)'),
                                 ('objectiveText', r'^(?:objective|mission goal|goal)\s*:\s*(.*)')):
                    fm = re.match(pat, ln, re.I)
                    if fm:
                        rep[fld] = fm.group(1).strip()
            if re.match(r'^(objective|mission goal|goal)\s*:', ln, re.I):
                rep['objectiveText'] = ln.split(':', 1)[1].strip()
            text_buf.append(ln)
            entries = self.ship_lines(ln, key, prefixes)
            for sl in entries:
                if pocket is None:
                    pocket = new_pocket('Single pocket')
                    header_done = True
                cat = self.sd.category(sl['typeIDs'][0])
                if cat == sdata.CAT_ASTEROID:
                    pocket['asteroids'].append(sl)
                elif not self.sd.is_npc_ship(sl['typeIDs'][0]):
                    pocket['structures'].append(sl)
                else:
                    if group is None:
                        group = new_group(pocket, 'Initial')
                    group['ships'].append(sl)
            if not entries:
                if group is not None:
                    group['text'].append(ln)
                elif pocket is not None:
                    pocket['text'].append(ln)

        # faction, type, space
        fid = faction_of(rep['enemyFaction']) if rep['enemyFaction'] else None
        if fid is None and meta.get('faction'):
            fid = faction_of(meta['faction'])
        rep['enemyFactionID'] = fid if fid is not None else 0
        mt = rep['missionType'].lower()
        if 'storyline' in mt:
            rep['storyline'] = True
        for w, tid in TYPES.items():
            if w in mt:
                rep['typeID'] = tid
                break
        sp = (rep.get('space') or '') + ' ' + rep['missionType']
        if re.search(r'gate|gated', sp, re.I) and not re.search(r'no (initial )?gate|w/?o gate|without gate', sp, re.I):
            rep['spaceType'] = 2
        elif re.search(r'deadspace', sp, re.I):
            rep['spaceType'] = 1
        if len(rep['pockets']) > 1:
            rep['spaceType'] = 2

        self.objective(rep, text_buf)
        rep['_text'] = text_buf
        return rep

    def objective(self, rep, text):
        obj = rep['objective']
        alltext = '\n'.join(text)
        explicit = rep.get('objectiveText', '')
        # a ship that completes the mission
        for p in rep['pockets']:
            for g in p['groups']:
                gtext = ' '.join(g['text']).lower()
                completes = bool(re.search(r'(triggers?|trigger the|is the) mission (completion|objective)|completes? the mission|'
                                           r'mission (objective|complete)', gtext))
                for s in g['ships']:
                    if s['objective'] or (completes and s['isTrigger']):
                        obj.update({'type': 'KillTrigger', 'typeID': s['typeIDs'][0], 'confidence': 'pattern'})
            for s in p['structures']:
                if s['objective']:
                    obj.update({'type': 'DestroyStructure', 'typeID': s['typeIDs'][0], 'confidence': 'pattern'})
        src = explicit or alltext
        # destroy the <structure>
        if obj['type'] == 'KillAll':
            for m in re.finditer(r'(?:destroy|kill|blow up|take out) (?:the |a |an )?([A-Z][\w\' -]{3,60}?)(?:[.,!;:]|\s+(?:and|to|in|which|that|before|located|at)\b|$)',
                                 src, re.M):
                t = self.sd.resolve(m.group(1), fuzzy=False)
                if t is not None and not self.sd.is_npc_ship(t) and self.sd.category(t) != sdata.CAT_ASTEROID:
                    obj.update({'type': 'DestroyStructure', 'typeID': t,
                                'confidence': 'explicit' if explicit else 'pattern'})
                    break
        # bring / retrieve an item
        if obj['type'] in ('KillAll',):
            for m in re.finditer(r'(?:retrieve|bring(?: back)?|acquire|recover|loot|obtain|return with|collect)\s+(?:the |a |an |some )?'
                                 r'(?:(\d+)\s*(?:x\s*|units of\s*)?)?([A-Z][\w\' -]{2,60}?)(?:\s+(?:from|to|back|and|which|that|in)\b|[.,!;:(]|$)',
                                 src, re.M):
                t = self.sd.resolve(m.group(2), cats=tuple(range(1, 100)), fuzzy=False)
                if t is not None and self.sd.category(t) not in (sdata.CAT_ENTITY, sdata.CAT_CELESTIAL, sdata.CAT_ASTEROID):
                    obj.update({'type': 'Fetch', 'typeID': t, 'qty': int(m.group(1) or 1),
                                'confidence': 'explicit' if explicit else 'pattern'})
                    break
        if explicit and obj['confidence'] == 'default':
            obj['confidence'] = 'explicit-killall' if re.search(r'kill|destroy|eliminate|clear|wipe', explicit, re.I) else 'default'
        obj['text'] = explicit


def parse_index(path, meta):
    """MissionReports* index tables:  page -> {title, faction, level}"""
    raw = open(path, encoding='latin1').read()
    for tr in re.findall(r'(?is)<tr>(.*?)</tr>', raw):
        cells = re.findall(r'(?is)<td[^>]*>(.*?)</td>', tr)
        if len(cells) < 3:
            continue
        title = html.unescape(re.sub(r'<[^>]+>', '', cells[0])).strip()
        faction = html.unescape(re.sub(r'<[^>]+>', '', cells[1])).strip()
        for i, cell in enumerate(cells[1:], 1):
            for page in re.findall(r'wakka=([A-Za-z0-9]+)"', cell):
                lv = re.search(r'level\s*(\d)', re.sub(r'<[^>]+>', ' ', cell), re.I)
                m = meta.setdefault(page, {})
                if title and 'title' not in m:
                    # 'Blockade, The' -> 'The Blockade'
                    tm = re.match(r'^(.*), (The|A|An)$', title, re.I)
                    m['title'] = (tm.group(2).capitalize() + ' ' + tm.group(1)) if tm else title
                if faction and not re.search(r'level', faction, re.I):
                    m.setdefault('faction', faction)
                if lv:
                    m.setdefault('level', int(lv.group(1)))
    return meta


def parse_mining_index(path):
    """MissionReportsMining:  title key -> m3"""
    out = {}
    raw = open(path, encoding='latin1').read()
    for tr in re.findall(r'(?is)<tr>(.*?)</tr>', raw):
        cells = [html.unescape(re.sub(r'<[^>]+>', ' ', c)).strip() for c in re.findall(r'(?is)<td[^>]*>(.*?)</td>', tr)]
        if len(cells) < 2:
            continue
        m = re.match(r'([\d,.]+)\s*m', cells[1])
        if m:
            out[sdata.title_key(re.sub(r'\(.*?\)', '', cells[0]))] = float(m.group(1).replace(',', ''))
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cache', default=os.path.join(here, 'cache'))
    ap.add_argument('--aliases', default=os.path.join(here, 'aliases.json'))
    ap.add_argument('--out', default=os.path.join(here, 'out', 'reports.json'))
    args = ap.parse_args()
    os.makedirs(os.path.dirname(args.out), exist_ok=True)

    sd = sdata.StaticData()
    sd.load_aliases(args.aliases)
    # client title messageIDs:  agtMissions (encounters) and qstMining/qstCourier (their ids are title messageIDs)
    titles = {}
    for tbl in ('agtMissions', 'qstMining', 'qstCourier'):
        for r in sdata.load_table(tbl):
            if r.get('name'):
                titles.setdefault(sdata.title_key(r['name']), r['id'])
    # asteroid (ore and ice) names, longest first
    ore_names = [(n, sd.pick(ids, (sdata.CAT_ASTEROID,))) for n, ids in sd.by_name.items()
                 if sd.pick(ids, (sdata.CAT_ASTEROID,)) is not None and len(n) > 3]
    # reporters' spellings ('Bandine', 'Augmene') from aliases.json
    ore_names += [(k, sd.resolve(k, (sdata.CAT_ASTEROID,), fuzzy=False)) for k in sd.aliases]
    ore_names = sorted([o for o in ore_names if o[1] is not None], key=lambda x: -len(x[0]))
    mining_rows = {}
    for r in sdata.load_table('qstMining'):
        mining_rows.setdefault(sdata.title_key(r['name'] or ''), r)

    meta = {}
    for idx in ('MissionReports', 'MissionReportsLevel1', 'MissionReportsLevel2', 'MissionReportsLevel3',
                'MissionReportsLevel4', 'MissionReportsLevel5', 'MissionReportsMining', 'CourierMissionsList'):
        p = os.path.join(args.cache, idx + '.html')
        if os.path.exists(p):
            parse_index(p, meta)
    mining_m3 = {}
    p = os.path.join(args.cache, 'MissionReportsMining.html')
    if os.path.exists(p):
        mining_m3 = parse_mining_index(p)

    parser = Parser(sd)
    reports = []
    pages = [l.strip() for l in open(os.path.join(args.cache, 'pages.txt')) if l.strip()]
    for key in pages:
        path = os.path.join(args.cache, key + '.html')
        if not os.path.exists(path):
            continue
        ts_path = os.path.join(args.cache, key + '.ts')
        snapshot = open(ts_path).read().strip() if os.path.exists(ts_path) else ''
        raw = open(path, encoding='cp1252', errors='replace').read()
        rep = parser.parse(key, raw, meta.get(key, {}), snapshot)
        nships = sum(len(g['ships']) for p in rep['pockets'] for g in p['groups'])
        nroids = sum(len(p['asteroids']) for p in rep['pockets'])
        tkey = sdata.title_key(re.sub(r'\(.*?\)', '', rep['name']))
        if rep['typeID'] == 5 or (nroids and tkey in mining_m3):
            rep['typeID'] = 5
            ore = None
            # the ore the title names ('Understanding Augmene') wins
            for name, tid in ore_names:
                if re.search(r'\b' + re.escape(name) + r's?\b', rep['name'].lower()):
                    ore = tid
                    break
            for p in rep['pockets']:
                for a in p['asteroids']:
                    ore = ore or a['typeIDs'][0]
            text = ' '.join(rep['_text']).lower()
            if ore is None:
                # 'Acquire 9000 Green Arisite (45000 m3)', 'mine 2000m3 of Banidine ore':  the first ore named
                best = None
                # the first ore the text names
                for name, tid in ([] if best else ore_names):
                    m = re.search(r'\b' + re.escape(name) + r's?\b', text)
                    if m and (best is None or m.start() < best[0]):
                        best = (m.start(), tid)
                ore = best[1] if best else None
            m3 = mining_m3.get(tkey)
            if m3 is None:
                mm = re.search(r'([\d,.]+)\s*m3', text)
                m3 = float(mm.group(1).replace(',', '')) if mm else None
            row = mining_rows.get(tkey)
            units = re.search(r'([1-9][\d,]*)\s*units', text)
            if ore and (m3 or units):
                vol = sd.types[ore]['volume'] or 0.1
                # the units the page names, else the index's cargo volume
                qty = int(units.group(1).replace(',', '')) if units else int(math.ceil(m3 / vol))
                rep['mining'] = {'oreTypeID': ore, 'm3': qty * vol, 'qty': qty}
                rep['objective'] = {'type': 'Mine', 'typeID': ore, 'qty': rep['mining']['qty'], 'confidence': 'index',
                                    'text': ''}
            if row:
                rep['nameID'] = row['id']
        if not rep['level'] or (nships == 0 and nroids == 0 and rep['typeID'] != 3 and not rep['mining']):
            continue
        del rep['_text']
        # a chain part ('In The Midst of Deadspace (3 of 5)') is its own title, so the full name is tried first
        rep['nameID'] = rep.get('nameID') or titles.get(sdata.title_key(rep['name']), 0) or titles.get(tkey, 0)
        reports.append(rep)

    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(reports, f, indent=1, sort_keys=True)
    with open(os.path.join(os.path.dirname(args.out), 'unresolved.txt'), 'w', encoding='utf-8') as f:
        for name, pages in sorted(parser.unresolved.items(), key=lambda x: (-len(x[1]), x[0])):
            f.write('%d\t%s\t%s\n' % (len(pages), name, ' '.join(sorted(pages)[:5])))
    print('%d reports (%d with a title id), %d unresolved names' % (
        len(reports), sum(1 for r in reports if r['nameID']), len(parser.unresolved)))


if __name__ == '__main__':
    main()
