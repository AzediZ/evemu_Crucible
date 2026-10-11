#!/usr/bin/env python3
"""
gen_dungeon_seed.py - builds the dungeon seed migration (combat anomalies, unrated complexes and exploration sites)
from the legacy dungeon tables in sql/base and the npc spawn classes.

    python -I utils/dungeon-seed/gen_dungeon_seed.py [--loc <resLocalization.stuff>] [--out <file.sql>]

Inputs (sql/base):  dunTemplates, dunRoomData, dunGroupData, npcSpawnClass, invTypes, invGroups (.sql.gz dumps).
--loc reads the client's localization_en-us.pickle out of resLocalization.stuff to give the dungeons their client
name messageIDs (journal names);  without it dungeonNameID stays NULL.  The pickle is read with an unpickler that
refuses every class, so only plain data is loaded.
The output is deterministic (fixed random seed).  Stdlib only.

ID ranges (all below EVEDBTool's 120000000 dungeon start):
    dungeonID   100000000 + templateID * 10 + faction code (0 none, 1 Angel, 2 Blood, 3 Guristas, 4 Sansha,
                5 Serpentis, 6 Rogue Drones)
    roomID      1000000 + n         objectID    5000000 + n
    spawnGroupID / triggerID / eventID          1000000 + n
"""

import argparse
import gzip
import io
import math
import os
import pickle
import random
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BASE = os.path.join(ROOT, 'sql', 'base')
DEFAULT_OUT = os.path.join(ROOT, 'sql', 'migrations', '20261011133100-F13-dungeon-seed.sql')

# client constants
TYPE_ACCEL_GATE = 17831
GROUP_LCO = 226
CAT_ASTEROID = 25

# dungeon archetypes (EVE_Dungeon.h Dungeon::Type)
ARCH_GRAV, ARCH_MAG, ARCH_RADAR, ARCH_LADAR, ARCH_ANOMALY, ARCH_UNRATED = 2, 3, 4, 5, 7, 8

# trigger / event types (client dunTrigger* / dunTriggerEvent* values)
TRIG_EXPLODING = 3
EV_SPAWN_GUARDS = 2
EV_MSG = 10
EV_COMPLETION = 11

# faction code -> (factionID, name prefixes tried against the client names;  the first is used when none matches)
FACTIONS = {
    1: (500011, ['Angel']),
    2: (500012, ['Blood Raider', 'Blood']),
    3: (500010, ['Guristas']),
    4: (500019, ['Sansha', "Sansha's Nation"]),
    5: (500020, ['Serpentis']),
    6: (500022, ['Drone']),
}
PIRATES = [1, 2, 3, 4, 5]
# magnetometric/radar template last digit -> faction code
EXPLORE_FACTION = {1: 5, 2: 1, 3: 2, 4: 3, 5: 4}

# true security bands of the template's band digit
BANDS = {1: (0.45, 1.0), 2: (0.05, 0.4499), 3: (-1.0, 0.0499)}


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


def load_table(name):
    """list of dicts (column -> value) from sql/base/<name>.sql.gz"""
    data = gzip.open(os.path.join(BASE, name + '.sql.gz'), 'rt', encoding='utf-8', errors='replace').read()
    m = re.search(r'CREATE TABLE `%s` \((.*?)\n\)' % name, data, re.S)
    cols = re.findall(r'^\s+`(\w+)`', m.group(1), re.M)
    out = []
    for im in re.finditer(r'INSERT INTO `%s`(?: \([^)]*\))? VALUES\s*' % name, data):
        for row in parse_values(data, im.end()):
            if len(row) == len(cols):
                out.append(dict(zip(cols, row)))
    return out


class SafeUnpickler(pickle.Unpickler):
    def find_class(self, module, name):
        raise pickle.UnpicklingError('refused %s.%s' % (module, name))


def load_names(stuff_path):
    """client text -> lowest messageID, from localization_en-us.pickle in resLocalization.stuff"""
    f = open(stuff_path, 'rb')
    count = struct.unpack('<I', f.read(4))[0]
    entries = []
    for _ in range(count):
        size, plen = struct.unpack('<II', f.read(8))
        path = f.read(plen).decode('latin1')
        f.read(1)
        entries.append((path, size))
    offset = f.tell()
    data = None
    for path, size in entries:
        if path.lower().endswith('localization_en-us.pickle'):
            f.seek(offset)
            data = SafeUnpickler(io.BytesIO(f.read(size)), encoding='latin1').load()
            break
        offset += size
    if data is None:
        sys.exit('localization_en-us.pickle not found in %s' % stuff_path)
    names = {}
    for mid, value in data[1].items():
        text = value[0] if isinstance(value, tuple) else value
        if isinstance(text, str):
            text = text.strip()
            if text not in names or mid < names[text]:
                names[text] = mid
    return names


def sql_str(s):
    return "'" + s.replace('\\', '\\\\').replace("'", "''") + "'"


class Seed:
    def __init__(self, names, types):
        self.names = names
        self.types = types
        self.dungeons, self.rooms, self.objects, self.gates = [], [], [], []
        self.groups, self.triggers, self.events, self.escalations = [], [], [], []
        self.room_n = self.object_n = self.group_n = self.trigger_n = self.event_n = 0

    def name_of(self, prefixes, base):
        """display name and client messageID ('<prefix> <base>', first prefix when nothing matches)"""
        for p in prefixes:
            text = (p + ' ' + base).strip()
            if text in self.names:
                return text, self.names[text]
        if base in self.names and not prefixes:
            return base, self.names[base]
        return (prefixes[0] + ' ' + base).strip() if prefixes else base, None

    def dungeon(self, dungeon_id, name, name_id, faction, archetype, difficulty, sec, weight):
        self.dungeons.append((dungeon_id, name, faction, archetype, name_id, difficulty, sec[0], sec[1], weight))

    def room(self, dungeon_id, name, arrival=(0, 0, 0)):
        self.room_n += 1
        room_id = 1000000 + self.room_n
        self.rooms.append((room_id, name, dungeon_id) + tuple(arrival))
        return room_id

    def obj(self, room_id, type_id, x, y, z, radius=0):
        self.object_n += 1
        object_id = 5000000 + self.object_n
        self.objects.append((object_id, room_id, type_id, round(x), round(y), round(z), radius))
        return object_id

    def group(self, room_id, wave, mode, ctype, csub, spread=15000, overseer=0):
        self.group_n += 1
        group_id = 1000000 + self.group_n
        self.groups.append((group_id, room_id, wave, mode, ctype, csub, spread, overseer))
        return group_id

    def trigger(self, room_id, ttype, group_id=0, value=0):
        self.trigger_n += 1
        trigger_id = 1000000 + self.trigger_n
        self.triggers.append((trigger_id, room_id, ttype, group_id, value))
        return trigger_id

    def event(self, trigger_id, seq, etype, group_id=0, msg_type=0, msg_id=0):
        self.event_n += 1
        self.events.append((1000000 + self.event_n, trigger_id, seq, etype, group_id, msg_type, msg_id))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--loc', help='path to resLocalization.stuff (client dungeon name messageIDs)')
    ap.add_argument('--out', default=DEFAULT_OUT)
    args = ap.parse_args()

    rnd = random.Random(13)
    names = load_names(args.loc) if args.loc else {}

    templates = load_table('dunTemplates')
    room_rows = load_table('dunRoomData')
    group_rows = load_table('dunGroupData')
    classes = load_table('npcSpawnClass')
    inv_types = {r['typeID']: r for r in load_table('invTypes')}
    inv_groups = {r['groupID']: r for r in load_table('invGroups')}

    rooms_by_id = {}
    for r in room_rows:
        rooms_by_id.setdefault(r['dunRoomID'], []).append(r)
    groups_by_id = {}
    for g in group_rows:
        groups_by_id.setdefault(g['dunGroupID'], []).append(g)
    subs_by_class = {}
    for c in classes:
        subs_by_class.setdefault(c['type'], []).append(c['sub'])
    for v in subs_by_class.values():
        v.sort()

    seed = Seed(names, inv_types)
    missing_types = set()

    def deco(room_id, legacy_room):
        """legacy deco of a room:  room row offset + group row offset, zero positions scattered 5-30 km"""
        n = 0
        for rr in rooms_by_id.get(legacy_room, []):
            for gr in groups_by_id.get(rr['dunGroupID'], []):
                type_id = gr['itemTypeID']
                if type_id not in inv_types:
                    missing_types.add(type_id)
                    continue
                x = rr['xpos'] + gr['xpos']
                y = rr['ypos'] + gr['ypos']
                z = rr['zpos'] + gr['zpos']
                if x == 0 and y == 0 and z == 0:
                    d = rnd.uniform(5000, 30000)
                    a = rnd.uniform(0, 6.283185)
                    x, y, z = d * math.cos(a), rnd.uniform(-3000, 3000), d * math.sin(a)
                seed.obj(room_id, type_id, x, y, z)
                n += 1
        return n

    def waves(room_id, class_type, first_mode, wave_subs, done_events):
        """one group per sub;  each group's death spawns the next, the last runs done_events(trigger)"""
        ids = []
        for i, sub in enumerate(wave_subs):
            ids.append(seed.group(room_id, i + 1, first_mode if i == 0 else 2, class_type, sub,
                                  spread=15000 if i == 0 else 20000))
        for i, gid in enumerate(ids):
            if i + 1 < len(ids):
                trig = seed.trigger(room_id, TRIG_EXPLODING, gid, 0)
                seed.event(trig, 1, EV_SPAWN_GUARDS, ids[i + 1])
            elif done_events is not None:
                done_events(seed.trigger(room_id, TRIG_EXPLODING, gid, 0))
        return ids

    counts = {}

    for t in templates:
        tid = t['dunTemplateID']
        tname = t['dunTemplateName'].strip()
        legacy_room = t['dunRoomID']
        digits = str(tid)
        arch = int(digits[0])

        if arch == 7 and len(digits) == 5:
            # combat anomaly:  7 band tier variant faction
            band, tier, variant, fdigit = int(digits[1]), int(digits[2]), int(digits[3]), int(digits[4])
            spawn = t['dunSpawnID'] or t['dunEntryID']     # 73116 has the two swapped
            if spawn not in subs_by_class:
                print('skipping %u %s: no spawn class %u' % (tid, tname, spawn))
                continue
            drone = (fdigit == 6)
            difficulty = spawn - (29 if drone else 19)
            weight = 3 if variant == 1 else 1
            for f in ([6] if drone else PIRATES):
                faction, prefixes = FACTIONS[f]
                name, name_id = seed.name_of([] if drone else prefixes, tname)
                did = 100000000 + tid * 10 + f
                seed.dungeon(did, name, name_id, faction, ARCH_ANOMALY, difficulty, BANDS[band], weight)
                room = seed.room(did, name)
                deco(room, legacy_room)
                waves(room, spawn, 0, subs_by_class[spawn], lambda trig: seed.event(trig, 1, EV_COMPLETION))
                counts['anomaly'] = counts.get('anomaly', 0) + 1

        elif arch == 8 and len(digits) == 5:
            # unrated complex:  8 band tier variant faction.  2-4 rooms joined by acceleration gates, overseer at the end
            band, tier, fdigit = int(digits[1]), int(digits[2]), int(digits[4])
            drone = (fdigit == 6)
            difficulty = min(10, (band - 1) * 3 + tier)
            nrooms = 2 if tier <= 2 else (3 if tier <= 4 else 4)
            class_type = (30 if drone else 20) + min(9, difficulty - 1)
            subs = subs_by_class.get(class_type, [1])
            for f in ([6] if drone else PIRATES):
                faction, prefixes = FACTIONS[f]
                name, name_id = seed.name_of([] if drone else prefixes, tname)
                did = 100000000 + tid * 10 + f
                seed.dungeon(did, name, name_id, faction, ARCH_UNRATED, difficulty, BANDS[band], 1)
                for r in range(nrooms):
                    room = seed.room(did, name if r == 0 else '%s - room %u' % (name, r + 1))
                    deco(room, legacy_room)
                    mode = 0 if r == 0 else 1
                    first = subs[(2 * r) % len(subs)]
                    second = subs[(2 * r + 1) % len(subs)]
                    wave_subs = [first, second] if (r % 2 == 0 or difficulty > 5) else [first]
                    if r + 1 < nrooms:
                        gate = seed.obj(room, TYPE_ACCEL_GATE, 15000, 0, 0)
                        seed.gates.append(gate)
                        waves(room, class_type, mode, wave_subs, None)
                    else:
                        waves(room, class_type, mode, wave_subs, None)
                        # the overseer and his escort:  killing him completes the site
                        overseer = seed.group(room, 9, mode, 11, max(1, min(10, difficulty)), spread=10000, overseer=1)
                        trig = seed.trigger(room, TRIG_EXPLODING, overseer, 0)
                        seed.event(trig, 1, EV_COMPLETION)
                counts['unrated'] = counts.get('unrated', 0) + 1

        elif arch in (ARCH_GRAV, ARCH_MAG, ARCH_RADAR, ARCH_LADAR) and len(digits) == 5:
            # exploration site:  one room of legacy deco, no npcs.  magnetometric/radar sites belong to a faction
            fcode = EXPLORE_FACTION.get(int(digits[4])) if arch in (ARCH_MAG, ARCH_RADAR) else None
            faction = FACTIONS[fcode][0] if fcode else 0
            did = 100000000 + tid * 10 + (fcode or 0)
            room_name = tname
            name_id = names.get(tname)
            weight = 1
            seed.dungeon(did, tname, name_id, faction, arch, 0, (-1.0, 1.0), weight)
            room = seed.room(did, room_name)
            if deco(room, legacy_room) == 0:
                # nothing to find there:  never picked at random
                seed.dungeons[-1] = seed.dungeons[-1][:8] + (0,)
                counts['empty %u' % arch] = counts.get('empty %u' % arch, 0) + 1
            counts['explore %u' % arch] = counts.get('explore %u' % arch, 0) + 1

    out = io.StringIO()
    w = out.write
    w('-- F13 dungeon seed:  combat anomalies, unrated complexes and exploration sites built from the legacy dungeon\n')
    w('--  templates, rooms and npc spawn classes.  GENERATED by utils/dungeon-seed/gen_dungeon_seed.py - do not edit\n')
    w('--  by hand, change the generator and run it again.\n')
    w('-- +migrate Up\n')
    w('-- the randomly placed copies of the legacy rooms (20211118111620-dungeonEditor) are replaced by these dungeons\n')
    w('DELETE FROM dunRoomObjects WHERE roomID < 10000;\n\n')

    def insert(table, cols, rows, fmt):
        for i in range(0, len(rows), 200):
            w('INSERT INTO `%s` (%s) VALUES\n' % (table, ', '.join(cols)))
            w(',\n'.join(fmt(r) for r in rows[i:i + 200]))
            w(';\n')
        w('\n')

    insert('dunDungeons', ['dungeonID', 'dungeonName', 'dungeonStatus', 'factionID', 'archetypeID', 'dungeonUUID',
                           'dungeonNameID', 'difficulty', 'minSecurity', 'maxSecurity', 'spawnWeight'],
           seed.dungeons,
           lambda r: '(%u, %s, 1, %u, %u, %s, %s, %u, %.4f, %.4f, %u)' % (
               r[0], sql_str(r[1]), r[2], r[3], sql_str('f13-seed-%u' % r[0]),
               'NULL' if r[4] is None else str(r[4]), r[5], r[6], r[7], r[8]))
    insert('dunRooms', ['roomID', 'roomName', 'dungeonID', 'arrivalX', 'arrivalY', 'arrivalZ'], seed.rooms,
           lambda r: '(%u, %s, %u, %d, %d, %d)' % (r[0], sql_str(r[1]), r[2], r[3], r[4], r[5]))
    insert('dunRoomObjects', ['objectID', 'roomID', 'typeID', 'groupID', 'x', 'y', 'z', 'radius'], seed.objects,
           lambda r: '(%u, %u, %u, 0, %d, %d, %d, %d)' % r)
    insert('dunGates', ['objectID'], [(g,) for g in seed.gates], lambda r: '(%u)' % r)
    insert('dunSpawnGroups', ['spawnGroupID', 'roomID', 'waveNumber', 'spawnMode', 'npcClassType', 'npcClassSub',
                              'spread', 'isOverseer'], seed.groups, lambda r: '(%u, %u, %u, %u, %u, %u, %d, %u)' % r)
    insert('dunTriggers', ['triggerID', 'roomID', 'triggerType', 'spawnGroupID', 'value'], seed.triggers,
           lambda r: '(%u, %u, %u, %u, %d)' % r)
    insert('dunTriggerEvents', ['eventID', 'triggerID', 'sequence', 'eventType', 'spawnGroupID', 'messageType',
                                'messageID'], seed.events, lambda r: '(%u, %u, %u, %u, %u, %u, %u)' % r)
    # escalation rules:  harder combat anomalies may lead to an unrated complex, a complex to a harder one
    w('INSERT INTO `dunEscalations` (srcArchetypeID, srcMinDifficulty, destArchetypeID, destDifficultyDelta, chance,'
      ' minJumps, maxJumps, lowerSecOnly, journalMessageID) VALUES\n')
    w('(%u, 4, %u, 0, 0.05, 0, 5, 1, 63235),\n' % (ARCH_ANOMALY, ARCH_UNRATED))
    w('(%u, 1, %u, 1, 0.5, 1, 5, 1, 63235);\n\n' % (ARCH_UNRATED, ARCH_UNRATED))

    w('-- +migrate Down\n')
    w('-- (the deleted legacy copies of 20211118111620-dungeonEditor are not restored;  they were placed at random)\n')
    w('DELETE FROM dunEscalations WHERE journalMessageID = 63235 AND srcDungeonID = 0 AND destDungeonID = 0;\n')
    w('DELETE FROM dunTriggerEvents WHERE eventID BETWEEN 1000000 AND 1999999;\n')
    w('DELETE FROM dunTriggers WHERE triggerID BETWEEN 1000000 AND 1999999;\n')
    w('DELETE FROM dunSpawnGroupTypes WHERE spawnGroupID BETWEEN 1000000 AND 1999999;\n')
    w('DELETE FROM dunSpawnGroups WHERE spawnGroupID BETWEEN 1000000 AND 1999999;\n')
    w('DELETE FROM dunGates WHERE objectID BETWEEN 5000000 AND 5999999;\n')
    w('DELETE FROM dunRoomObjects WHERE objectID BETWEEN 5000000 AND 5999999;\n')
    w('DELETE FROM dunRooms WHERE roomID BETWEEN 1000000 AND 1999999;\n')
    w('DELETE FROM dunDungeons WHERE dungeonID BETWEEN 100000000 AND 100999999;\n')

    with open(args.out, 'w', newline='\n', encoding='utf-8') as f:
        f.write(out.getvalue())

    named = sum(1 for d in seed.dungeons if d[4] is not None)
    print('wrote %s' % args.out)
    print('dungeons %u (%u with client names), rooms %u, objects %u, gates %u, groups %u, triggers %u, events %u'
          % (len(seed.dungeons), named, len(seed.rooms), len(seed.objects), len(seed.gates), len(seed.groups),
             len(seed.triggers), len(seed.events)))
    print(', '.join('%s %u' % kv for kv in sorted(counts.items())))
    if missing_types:
        print('legacy types not in invTypes (skipped): %s' % sorted(missing_types))


if __name__ == '__main__':
    main()
