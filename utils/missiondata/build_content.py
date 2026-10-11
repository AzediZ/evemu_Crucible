#!/usr/bin/env python3
"""
build_content.py - builds the mission content migration from out/reports.json (parse_reports.py) and overrides.json

    python -I build_content.py [--reports out/reports.json] [--overrides overrides.json]
                               [--out ../../sql/migrations/20261011136100-F12-mission-content.sql] [--json out/missions.json]

For every report:  a msnMissions row (contentID = position of the page key in sorted order, 1-based) and its mission
pocket as an F13 dungeon (dungeonID = 101000000 + contentID, archetype 1 Mission, spawnWeight 0):  one dunRooms row per
pocket, the npcs of each wave as dunSpawnGroups/dunSpawnGroupTypes (wave 0 there on arrival, later waves spawned by the
TRIGGER ship of the wave before, a cleared wave, or a timer), structures, asteroids (mining), acceleration gates with the
mission level's ship restrictions, and the objective's trigger/event:  MissionCompletion when the objective npc or
structure dies, DungeonCompletion when the last pocket is cleared (kill all), DropLoot of the fetch item.
Geometry is synthesized (no coordinates on the reports), seeded with the contentID so a build is reproducible.
The briefing is a generated English paragraph (the client shows strings).  out/review.txt lists every mission for
curation in overrides.json.  Standard library only.

ID ranges (F13:  mission dungeons >= 101000000, their groups/triggers/events >= 2000000):
    contentID   1..99999            dungeonID   101000000 + contentID
    roomID      2000000 + n         objectID    6000000 + n
    spawnGroupID / triggerID / eventID          2000000 + n        lootGroupID 2000000 + contentID
"""

import argparse
import json
import math
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdata   # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(sdata.ROOT, 'sql', 'migrations', '20261011136100-F12-mission-content.sql')

TYPE_ACCEL_GATE = 17831
# Mission::Type / Mission::ObjType (EVE_Missions.h)
MT_ENCOUNTER, MT_COURIER, MT_TRADE, MT_MINING = 2, 3, 4, 5
OBJ = {'KillAll': 1, 'KillTrigger': 2, 'DestroyStructure': 3, 'Fetch': 4, 'Mine': 6, 'Trade': 7}
# client trigger/event values (EVE_Dungeon.h)
TRIG_EXPLODING, TRIG_ROOM_ENTERED, TRIG_ATTACKED, TRIG_ROOM_WIPED = 3, 8, 1, 31
EV_SPAWN_GUARDS, EV_MISSION_COMPLETION, EV_DUNGEON_COMPLETION, EV_DROP_LOOT = 2, 9, 11, 24
# distance (km) of the first groups by level
LEVEL_RANGE = {1: (15, 25), 2: (20, 35), 3: (25, 50), 4: (30, 70), 5: (40, 90)}
FACTION_NAMES = {500011: 'Angel Cartel', 500012: 'Blood Raider', 500010: 'Guristas', 500019: "Sansha's Nation",
                 500020: 'Serpentis', 500022: 'Rogue Drone', 500018: "Mordu's Legion", 500007: 'Ammatar',
                 500008: 'Khanid', 500003: 'Amarr', 500001: 'Caldari', 500004: 'Gallente', 500002: 'Minmatar',
                 500015: 'Thukker', 0: 'Mercenary'}
# agents allowed to offer missions against an empire (Char::Race bits of the opposing side)
RACE_MASK = {500003: 2, 500007: 2, 500008: 2, 500002: 4, 500001: 8, 500004: 1}
CAPITALS = [30, 485, 513, 547, 659, 883, 902]
SMALL = [25, 237, 324, 830, 831, 834, 893, 1022, 31, 420, 541, 26, 358, 832, 906, 833, 894, 963, 28, 380, 463, 543]
BIG = [27, 381, 898, 900, 941]
BC = [419, 540]


def ship_restrictions(level):
    """(allowed, denied) invGroups of the mission level's acceleration gates (MissionDataMgr::GetShipRestrictions)"""
    denied = list(CAPITALS)
    if level > 2:
        return [], denied
    allowed = list(SMALL)
    denied += BIG
    if level == 2:
        allowed += BC
    else:
        denied += BC
    return allowed, denied


def sql_str(s):
    return "'" + s.replace('\\', '\\\\').replace("'", "''") + "'"


def bearing_point(rnd, dist_m, flat=0.25):
    """a point dist_m from the origin in a random direction, mostly in the plane"""
    a = rnd.uniform(0, 2 * math.pi)
    e = rnd.uniform(-flat, flat)
    return (dist_m * math.cos(a) * math.cos(e), dist_m * math.sin(e), dist_m * math.sin(a) * math.cos(e))


class Build:
    def __init__(self, sd):
        self.sd = sd
        self.missions, self.dungeons, self.rooms, self.objects, self.gates = [], [], [], [], []
        self.groups, self.group_types, self.triggers, self.events, self.loot = [], [], [], [], []
        self.room_n = self.object_n = self.group_n = self.trigger_n = self.event_n = 0
        self.review = []
        self.contract = []

    def room(self, dungeon_id, name):
        self.room_n += 1
        rid = 2000000 + self.room_n
        self.rooms.append((rid, name[:100], dungeon_id))
        return rid

    def obj(self, room_id, type_id, pos, radius=0):
        self.object_n += 1
        oid = 6000000 + self.object_n
        self.objects.append((oid, room_id, type_id, round(pos[0]), round(pos[1]), round(pos[2]), int(radius)))
        return oid

    def group(self, room_id, wave, mode, pos, spread, warp_in, types):
        self.group_n += 1
        gid = 2000000 + self.group_n
        self.groups.append((gid, room_id, wave, mode, round(pos[0]), round(pos[1]), round(pos[2]), spread, 1 if warp_in else 0))
        for t, q in types:
            self.group_types.append((gid, t, q))
        return gid

    def trigger(self, room_id, ttype, group_id=0, object_id=0, value=0):
        self.trigger_n += 1
        tid = 2000000 + self.trigger_n
        self.triggers.append((tid, room_id, ttype, group_id, object_id, value))
        return tid

    def event(self, trigger_id, seq, etype, group_id=0, delay=0, value=0, type_id=0):
        self.event_n += 1
        self.events.append((2000000 + self.event_n, trigger_id, seq, etype, delay, group_id, value, type_id))


def wave_plan(pocket):
    """the pocket's waves:  [{'groups': [parsed group, ...], 'trigger': {...}}].  variants of a wave keep the first"""
    waves = []
    prev_variant_parent = None
    for g in pocket['groups']:
        if g['variant']:
            # 'Variant 2' of the wave before:  only the first variant is used
            if prev_variant_parent is not None and prev_variant_parent.get('_hasVariant'):
                continue
            if waves:
                waves[-1]['groups'].append(g)
                waves[-1]['_hasVariant'] = True
                prev_variant_parent = waves[-1]
            else:
                waves.append({'groups': [g], 'heading': g['heading'], '_hasVariant': True})
                prev_variant_parent = waves[-1]
            continue
        prev_variant_parent = None
        initial = bool(re.search(r'^(initial|group\s*\d|single group|warp.?in|first group|spawn\s*1\b|wave\s*1\b|'
                                 r'structures?|sentr|stationary|ships$|defen|guards?|enemies|npcs?|group [a-z]\b)',
                                 g['heading'], re.I))
        if not waves or (initial and all(w['wave'] == 0 for w in waves)):
            waves.append({'groups': [g], 'heading': g['heading'], 'wave': 0})
        else:
            waves.append({'groups': [g], 'heading': g['heading'], 'wave': len([w for w in waves if w['wave'] > 0]) + 1})
        if 'wave' not in waves[-1]:
            waves[-1]['wave'] = 0
    for w in waves:
        w.setdefault('wave', 0)
        text = ' '.join([w['heading']] + [t for g in w['groups'] for t in g['text']]).lower()
        tm = re.search(r'(\d+)\s*(?:-\s*\d+\s*)?(min|minute|sec|second)s?\b', text)
        if w['wave'] > 0 and tm and re.search(r'after|timer|delay|later|wait', text):
            secs = int(tm.group(1)) * (60 if tm.group(2).startswith('min') else 1)
            w['trigger'] = {'kind': 'timer', 'seconds': min(secs, 1800)}
        elif w['wave'] > 0 and re.search(r'(when|once|upon|after) (you )?(attack|aggress|shoot|engag)', text):
            w['trigger'] = {'kind': 'attack'}
        else:
            w['trigger'] = {'kind': 'kill'}
    return waves


def qty_of(ship):
    return max(1, (ship['qtyMin'] + ship['qtyMax'] + 1) // 2)


def briefing(rep, obj_type, obj_type_id, sd):
    faction = FACTION_NAMES.get(rep['enemyFactionID'], 'Hostile')
    where = 'a deadspace pocket' if rep['spaceType'] else 'a nearby system'
    if obj_type == 'Mine':
        qty = rep['objective']['qty']
        return ("We need ore, and we need it fast.  Our surveyors found a field of %s in %s.  Mine %s units of it and "
                "bring them to me.  Watch out for %s ships, they have been seen around the field." % (
                    sd.name(obj_type_id), where, '{:,}'.format(qty), faction.lower() if faction == 'Hostile' else faction))
    text = '%s forces have been reported in %s.  ' % (faction, where)
    if obj_type == 'KillTrigger':
        text += 'Destroy the %s; that will take care of the problem.' % sd.name(obj_type_id)
    elif obj_type == 'DestroyStructure':
        text += 'Destroy their %s.' % sd.name(obj_type_id)
    elif obj_type == 'Fetch':
        text += 'They have taken something that belongs to us:  get the %s back and bring it to me.' % sd.name(obj_type_id)
    else:
        text += 'Eliminate every last one of them.'
    if rep.get('damageDealt'):
        text += '  Expect %s damage.' % re.sub(r'\s+', ' ', rep['damageDealt'])[:80]
    return text


def build_mission(b, rep, content_id, ov):
    sd = b.sd
    rnd = random.Random(content_id)
    level = rep['level']
    dungeon_id = 101000000 + content_id
    obj = dict(rep['objective'])
    if 'objective' in ov:
        obj.update(ov['objective'])
    lo, hi = LEVEL_RANGE.get(level, (20, 40))
    rooms_contract = []
    room_ids = []
    groups_by_type = {}         # typeID -> [(roomIdx, groupID)]
    structure_groups = []       # (roomIdx, groupID, typeID)
    last_trigger_groups = []    # trigger groups of the last wave of the last room
    room_groups = []            # per room:  all group ids
    pockets = rep['pockets'] or [{'name': 'Single pocket', 'groups': [], 'structures': [], 'asteroids': []}]
    for ri, pocket in enumerate(pockets):
        rid = b.room(dungeon_id, pocket['name'] or ('Pocket %d' % (ri + 1)))
        room_ids.append(rid)
        rc = {'roomName': pocket['name'], 'objects': [], 'spawns': [], 'triggers': []}
        rgroups = []
        # structures:  npc-type structures (sentries, towers) are one-ship groups present from the start, the rest objects
        for s in pocket['structures']:
            t = s['typeIDs'][0]
            pos = bearing_point(rnd, rnd.uniform(5000, 25000))
            if sd.category(t) == sdata.CAT_ENTITY:
                gid = b.group(rid, 0, 0, pos, 0, False, [(t, qty_of(s))])
                structure_groups.append((ri, gid, t))
                groups_by_type.setdefault(t, []).append((ri, gid))
                rgroups.append(gid)
                rc['spawns'].append({'wave': 0, 'typeIDs': [t], 'qty': qty_of(s), 'x': pos[0], 'y': pos[1], 'z': pos[2],
                                     'kind': 'structure'})
            else:
                oid = b.obj(rid, t, pos)
                structure_groups.append((ri, -oid, t))
                rc['objects'].append({'typeID': t, 'x': pos[0], 'y': pos[1], 'z': pos[2], 'kind': 'structure'})
        # asteroids (mining)
        if rep['typeID'] == MT_MINING and obj['type'] == 'Mine' and ri == len(pockets) - 1:
            ore, qty = obj['typeID'], obj['qty']
            n = 6 if qty > 1000 else 3
            per = int(math.ceil(qty * 1.25 / n))
            for i in range(n):
                pos = bearing_point(rnd, rnd.uniform(8000, 30000))
                b.obj(rid, ore, pos, per)
                rc['objects'].append({'typeID': ore, 'x': pos[0], 'y': pos[1], 'z': pos[2], 'kind': 'asteroid', 'quantity': per})
        for a in pocket['asteroids']:
            t = a['typeIDs'][0]
            if rep['typeID'] == MT_MINING and obj['type'] == 'Mine' and t == obj['typeID']:
                continue
            pos = bearing_point(rnd, rnd.uniform(8000, 30000))
            b.obj(rid, t, pos, 5000)
            rc['objects'].append({'typeID': t, 'x': pos[0], 'y': pos[1], 'z': pos[2], 'kind': 'asteroid', 'quantity': 5000})

        # waves
        prev = None     # (main group ids, trigger group ids) of the wave before
        waves = wave_plan(pocket)
        for wi, w in enumerate(waves):
            ships = [s for g in w['groups'] for s in g['ships']]
            if not ships:
                continue
            dist = None
            for g in w['groups']:
                if g.get('distKm'):
                    dist = g['distKm']
                    break
            dkm = rnd.uniform(dist[0], dist[1]) if dist else rnd.uniform(lo, hi)
            pos = bearing_point(rnd, dkm * 1000)
            # a wave with nothing before it in this pocket is there on arrival
            first = (w['wave'] == 0) or (prev is None)
            mode = 0 if first else 2
            warp_in = not first
            main, trig = [], []
            normal_types = []
            for s in ships:
                ids = s['typeIDs']
                q = qty_of(s)
                own = s['isTrigger'] or (obj['type'] in ('KillTrigger', 'Fetch') and obj['typeID'] in ids)
                types = {}
                for i in range(q):
                    t = ids[i % len(ids)]
                    types[t] = types.get(t, 0) + 1
                if own:
                    tp = bearing_point(rnd, 2000)
                    gpos = (pos[0] + tp[0], pos[1] + tp[1], pos[2] + tp[2])
                    gid = b.group(rid, w['wave'], mode, gpos, 1500, warp_in, sorted(types.items()))
                    trig.append(gid)
                    for t in types:
                        groups_by_type.setdefault(t, []).append((ri, gid))
                    rc['spawns'].append({'wave': w['wave'], 'typeIDs': sorted(types), 'qty': q, 'isTrigger': True,
                                         'x': gpos[0], 'y': gpos[1], 'z': gpos[2]})
                else:
                    for t, n in types.items():
                        normal_types.append((t, n))
            if normal_types:
                merged = {}
                for t, n in normal_types:
                    merged[t] = merged.get(t, 0) + n
                gid = b.group(rid, w['wave'], mode, pos, 6000, warp_in, sorted(merged.items()))
                main.append(gid)
                for t in merged:
                    groups_by_type.setdefault(t, []).append((ri, gid))
                rc['spawns'].append({'wave': w['wave'], 'typeIDs': sorted(merged), 'qty': sum(merged.values()),
                                     'isTrigger': False, 'x': pos[0], 'y': pos[1], 'z': pos[2]})
            rgroups += main + trig
            # how this wave comes
            if not first:
                kind = w['trigger']['kind']
                new = main + trig
                if kind == 'timer':
                    tid = b.trigger(rid, TRIG_ROOM_ENTERED)
                    for i, gid in enumerate(new):
                        b.event(tid, i + 1, EV_SPAWN_GUARDS, gid, delay=w['trigger']['seconds'])
                    rc['triggers'].append({'wave': w['wave'], 'on': 'timer', 'seconds': w['trigger']['seconds']})
                else:
                    src = (prev[1] or prev[0])[0]
                    ttype = TRIG_ATTACKED if kind == 'attack' else TRIG_EXPLODING
                    tid = b.trigger(rid, ttype, src, 0, 0)
                    for i, gid in enumerate(new):
                        b.event(tid, i + 1, EV_SPAWN_GUARDS, gid)
                    rc['triggers'].append({'wave': w['wave'], 'on': 'attack' if kind == 'attack' else 'kill',
                                           'refTrigger': bool(prev[1])})
            prev = (main, trig)
            if ri == len(pockets) - 1:
                last_trigger_groups = trig or main
        room_groups.append(rgroups)
        # acceleration gate to the next pocket
        if ri < len(pockets) - 1:
            gpos = bearing_point(rnd, rnd.uniform(10000, 25000))
            oid = b.obj(rid, TYPE_ACCEL_GATE, gpos)
            allowed, denied = ship_restrictions(level)
            b.gates.append((oid, ','.join(map(str, allowed)), ','.join(map(str, denied))))
            rc['objects'].append({'typeID': TYPE_ACCEL_GATE, 'x': gpos[0], 'y': gpos[1], 'z': gpos[2], 'kind': 'gate'})
        rooms_contract.append(rc)

    # the objective
    last = len(room_ids) - 1
    obj_type, obj_tid, obj_qty = obj['type'], obj.get('typeID', 0), obj.get('qty', 0)
    note = ''
    if obj_type == 'KillTrigger':
        cands = groups_by_type.get(obj_tid, [])
        if cands:
            ri, gid = cands[-1]
            tid = b.trigger(room_ids[ri], TRIG_EXPLODING, gid)
            b.event(tid, 1, EV_MISSION_COMPLETION)
            rooms_contract[ri]['triggers'].append({'action': 'MissionCompletion', 'on': 'kill', 'refTypeID': obj_tid})
        else:
            obj_type, note = 'KillAll', 'objective npc not in the pocket'
    if obj_type == 'DestroyStructure':
        cands = [s for s in structure_groups if s[2] == obj_tid]
        if not cands:
            # the briefing's structure isn't on the report's lists:  put it in the last pocket
            pos = bearing_point(rnd, rnd.uniform(10000, 20000))
            if sd.category(obj_tid) == sdata.CAT_ENTITY:
                gid = b.group(room_ids[last], 0, 0, pos, 0, False, [(obj_tid, 1)])
                cands = [(last, gid, obj_tid)]
            else:
                oid = b.obj(room_ids[last], obj_tid, pos)
                cands = [(last, -oid, obj_tid)]
        ri, gid, _ = cands[-1]
        if gid > 0:
            tid = b.trigger(room_ids[ri], TRIG_EXPLODING, gid)
        else:
            tid = b.trigger(room_ids[ri], TRIG_EXPLODING, 0, -gid)
        b.event(tid, 1, EV_MISSION_COMPLETION)
        rooms_contract[ri]['triggers'].append({'action': 'MissionCompletion', 'on': 'kill', 'refTypeID': obj_tid})
    if obj_type == 'Fetch':
        carrier = None
        cands = [g for g in groups_by_type.get(obj_tid, [])]
        if last_trigger_groups:
            carrier = (last, last_trigger_groups[-1])
        elif structure_groups:
            s = structure_groups[-1]
            carrier = (s[0], s[1])
        elif room_groups and room_groups[last]:
            carrier = (last, room_groups[last][-1])
        if carrier is None:
            obj_type, note = 'KillAll', 'nothing to carry the fetch item'
        else:
            loot_id = 2000000 + content_id
            b.loot.append((loot_id, obj_tid, max(1, obj_qty), max(1, obj_qty)))
            ri, gid = carrier
            tid = b.trigger(room_ids[ri], TRIG_EXPLODING, gid if gid > 0 else 0, -gid if gid < 0 else 0)
            b.event(tid, 1, EV_DROP_LOOT, value=loot_id)
            rooms_contract[ri]['triggers'].append({'action': 'DropLoot', 'on': 'kill', 'typeID': obj_tid, 'qty': obj_qty})
    if obj_type == 'KillAll':
        # the last pocket that has npcs (a room without any is never 'wiped out')
        ri = max([i for i, g in enumerate(room_groups) if g] or [last])
        tid = b.trigger(room_ids[ri], TRIG_ROOM_WIPED)
        b.event(tid, 1, EV_DUNGEON_COMPLETION)
        rooms_contract[ri]['triggers'].append({'action': 'DungeonCompletion', 'on': 'roomWipedOut'})
    return obj_type, obj_tid, obj_qty, rooms_contract, note


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--reports', default=os.path.join(HERE, 'out', 'reports.json'))
    ap.add_argument('--overrides', default=os.path.join(HERE, 'overrides.json'))
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--json', default=os.path.join(HERE, 'out', 'missions.json'))
    args = ap.parse_args()

    sd = sdata.StaticData()
    reports = json.load(open(args.reports, encoding='utf-8'))
    overrides = json.load(open(args.overrides, encoding='utf-8')) if os.path.exists(args.overrides) else {}
    b = Build(sd)

    for content_id, rep in enumerate(sorted(reports, key=lambda r: r['pageKey']), 1):
        ov = overrides.get(rep['pageKey'], {})
        for k in ('name', 'level', 'enemyFactionID', 'typeID', 'storyline', 'spaceType'):
            if k in ov:
                rep[k] = ov[k]
        enabled, why = True, []
        if rep['source'] != 'wayback':
            enabled, why = False, why + ['2017 mirror']
        if rep['typeID'] not in (MT_ENCOUNTER, MT_MINING, MT_TRADE):
            enabled, why = False, why + ['type %d' % rep['typeID']]
        if rep['arc'] and rep.get('part', 1) != 1:
            enabled, why = False, why + ['later part of a chain']
        if rep['typeID'] == MT_MINING and rep['objective']['type'] != 'Mine':
            enabled, why = False, why + ['no ore/volume']
        if not 1 <= rep['level'] <= 5:
            enabled, why = False, why + ['level %d' % rep['level']]
        nships = sum(len(g['ships']) for p in rep['pockets'] for g in p['groups']) + \
            sum(len(p['structures']) for p in rep['pockets'])
        if rep['typeID'] == MT_ENCOUNTER and nships == 0:
            enabled, why = False, why + ['no npcs']
        if 'enabled' in ov:
            enabled = bool(ov['enabled'])
            why.append('override')

        obj_type, obj_tid, obj_qty, rooms, note = build_mission(b, rep, content_id, ov)
        dungeon_id = 101000000 + content_id
        fid = rep['enemyFactionID'] or 0
        fname = FACTION_NAMES.get(fid, '')
        b.dungeons.append((dungeon_id, ('%s (L%d%s)' % (rep['name'], rep['level'], ', ' + fname if fname else ''))[:100],
                           fid, rep['level']))
        text = ov.get('briefingText') or briefing(rep, obj_type, obj_tid, sd)
        kill = obj_type in ('KillAll', 'KillTrigger', 'DestroyStructure')
        b.missions.append({
            'contentID': content_id, 'pageKey': rep['pageKey'], 'snapshot': rep['snapshot'][:14],
            'name': rep['name'][:100], 'nameID': ov.get('nameID', rep.get('nameID') or 0), 'briefingText': text,
            'level': rep['level'], 'typeID': rep['typeID'], 'storyline': 1 if rep['storyline'] else 0,
            'important': 1 if rep['storyline'] else 0,
            'enemyFactionID': fid, 'agentRaceMask': ov.get('agentRaceMask', RACE_MASK.get(fid, 0)),
            'objectiveType': OBJ.get(obj_type, 1), 'objectiveTypeID': obj_tid or 0, 'objectiveQty': obj_qty or 0,
            'dungeonID': dungeon_id, 'spaceType': rep['spaceType'], 'remoteCompletable': 1 if kill else 0,
            'rewardISK': ov.get('rewardISK', 0), 'bonusISK': ov.get('bonusISK', 0), 'rewardLP': ov.get('rewardLP', 0),
            'rewardScale': ov.get('rewardScale', 1.0), 'enabled': 1 if enabled else 0})
        b.contract.append({'dungeonID': dungeon_id, 'dungeonName': b.dungeons[-1][1], 'factionID': fid, 'ArchetypeID': 1,
                           'contentID': content_id, 'pageKey': rep['pageKey'], 'rooms': rooms})
        b.review.append('%5d %-34s L%d %-7s %-16s %-6s %-24s %-15s pockets=%d npcs=%d %s%s' % (
            content_id, rep['pageKey'][:34], rep['level'], {2: 'enc', 4: 'trade', 5: 'mining', 3: 'courier'}.get(rep['typeID'], '?'),
            fname[:16], 'ON' if enabled else 'off', ('%s %s' % (obj_type, sd.name(obj_tid) if obj_tid else ''))[:24],
            rep['objective'].get('confidence', ''), len(rep['pockets']), nships,
            ('[' + ', '.join(why) + '] ') if why else '', note))

    out = []
    w = out.append
    w('-- F12 mission content:  missions from the eve-survival.org reports archived by the Wayback Machine before the\n')
    w('--  Crucible release (2011-11-29), and their pockets as dungeons for the F13 dungeon engine.  GENERATED by\n')
    w('--  utils/missiondata/build_content.py - do not edit by hand, change the overrides/generator and run it again.\n')
    w('-- +migrate Up\n')

    def insert(table, cols, rows, fmt):
        for i in range(0, len(rows), 200):
            w('INSERT INTO `%s` (%s) VALUES\n' % (table, ', '.join(cols)))
            w(',\n'.join(fmt(r) for r in rows[i:i + 200]))
            w(';\n')
        w('\n')

    insert('msnMissions', ['contentID', 'pageKey', 'snapshot', 'name', 'nameID', 'briefingID', 'briefingText', 'level',
                           'typeID', 'important', 'storyline', 'enemyFactionID', 'agentRaceMask', 'objectiveType',
                           'objectiveTypeID', 'objectiveQty', 'dungeonID', 'spaceType', 'rewardISK', 'bonusISK',
                           'rewardLP', 'rewardScale', 'remoteOfferable', 'remoteCompletable', 'enabled'],
           b.missions,
           lambda m: '(%u, %s, %s, %s, %u, 0, %s, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %.2f, 1, %u, %u)' % (
               m['contentID'], sql_str(m['pageKey']), sql_str(m['snapshot']), sql_str(m['name']), m['nameID'],
               sql_str(m['briefingText']), m['level'], m['typeID'], m['important'], m['storyline'], m['enemyFactionID'],
               m['agentRaceMask'], m['objectiveType'], m['objectiveTypeID'], m['objectiveQty'], m['dungeonID'],
               m['spaceType'], m['rewardISK'], m['bonusISK'], m['rewardLP'], m['rewardScale'], m['remoteCompletable'],
               m['enabled']))
    insert('dunDungeons', ['dungeonID', 'dungeonName', 'dungeonStatus', 'factionID', 'archetypeID', 'dungeonUUID',
                           'dungeonNameID', 'difficulty', 'minSecurity', 'maxSecurity', 'spawnWeight'], b.dungeons,
           lambda d: '(%u, %s, 1, %u, 1, %s, NULL, %u, -1.0, 1.0, 0)' % (d[0], sql_str(d[1]), d[2], sql_str('f12-msn-%u' % d[0]), d[3]))
    insert('dunRooms', ['roomID', 'roomName', 'dungeonID'], b.rooms, lambda r: '(%u, %s, %u)' % (r[0], sql_str(r[1]), r[2]))
    insert('dunRoomObjects', ['objectID', 'roomID', 'typeID', 'groupID', 'x', 'y', 'z', 'radius'], b.objects,
           lambda r: '(%u, %u, %u, 0, %d, %d, %d, %d)' % r)
    insert('dunGates', ['objectID', 'allowedGroups', 'deniedGroups'], b.gates,
           lambda g: '(%u, %s, %s)' % (g[0], sql_str(g[1]), sql_str(g[2])))
    insert('dunSpawnGroups', ['spawnGroupID', 'roomID', 'waveNumber', 'spawnMode', 'npcClassType', 'npcClassSub', 'x', 'y',
                              'z', 'spread', 'warpIn'], b.groups,
           lambda g: '(%u, %u, %u, %u, 0, 0, %d, %d, %d, %d, %u)' % (g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8]))
    insert('dunSpawnGroupTypes', ['spawnGroupID', 'typeID', 'quantity'], b.group_types, lambda r: '(%u, %u, %u)' % r)
    insert('dunTriggers', ['triggerID', 'roomID', 'triggerType', 'spawnGroupID', 'objectID', 'value'], b.triggers,
           lambda t: '(%u, %u, %u, %u, %u, %d)' % t)
    insert('dunTriggerEvents', ['eventID', 'triggerID', 'sequence', 'eventType', 'delaySeconds', 'spawnGroupID', 'value',
                                'typeID'], b.events, lambda e: '(%u, %u, %u, %u, %u, %u, %d, %u)' % e)
    if b.loot:
        insert('dunLoot', ['lootGroupID', 'typeID', 'minQty', 'maxQty', 'chance'], b.loot,
               lambda l: '(%u, %u, %u, %u, 1.0)' % l)

    w('-- +migrate Down\n')
    w('DELETE FROM dunLoot WHERE lootGroupID BETWEEN 2000001 AND 2099999;\n')
    w('DELETE FROM dunTriggerEvents WHERE eventID BETWEEN 2000000 AND 2999999;\n')
    w('DELETE FROM dunTriggers WHERE triggerID BETWEEN 2000000 AND 2999999;\n')
    w('DELETE FROM dunSpawnGroupTypes WHERE spawnGroupID BETWEEN 2000000 AND 2999999;\n')
    w('DELETE FROM dunSpawnGroups WHERE spawnGroupID BETWEEN 2000000 AND 2999999;\n')
    w('DELETE FROM dunGates WHERE objectID BETWEEN 6000000 AND 6999999;\n')
    w('DELETE FROM dunRoomObjects WHERE objectID BETWEEN 6000000 AND 6999999;\n')
    w('DELETE FROM dunRooms WHERE roomID BETWEEN 2000000 AND 2999999;\n')
    w('DELETE FROM dunDungeons WHERE dungeonID BETWEEN 101000001 AND 101099999;\n')
    w('DELETE FROM msnMissions WHERE contentID BETWEEN 1 AND 99999;\n')

    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(''.join(out))
    with open(args.json, 'w', encoding='utf-8') as f:
        json.dump(b.contract, f, indent=1)
    with open(os.path.join(os.path.dirname(args.json), 'review.txt'), 'w', encoding='utf-8') as f:
        f.write('  id  page                               lvl type    enemy            state  objective                confidence      notes\n')
        f.write('\n'.join(b.review) + '\n')
    on = sum(1 for m in b.missions if m['enabled'])
    print('%d missions (%d enabled), %d rooms, %d groups, %d triggers, %d events -> %s' % (
        len(b.missions), on, len(b.rooms), len(b.groups), len(b.triggers), len(b.events), args.out))


if __name__ == '__main__':
    main()
