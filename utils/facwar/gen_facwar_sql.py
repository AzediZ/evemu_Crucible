#!/usr/bin/env python3
"""Generates sql/migrations/20261205100000-facwar-complexes.sql:  the factional warfare complexes of Crucible.

What's from sources and what's a pick:
  - complexes are cosmic anomalies in the war zones, held by one of the zone's two factions, and taken by standing near
    their capture point long enough with nobody of the holder's side about (Empyrean Age dev blogs, 2008-2009).  minor,
    medium and major ones, the gates letting through what the militia missions of levels 1, 2 and 3 allow:  T1 frigates
    and destroyers;  then T1 cruisers and T2 frigates too;  then T1 battlecruisers and T2 destroyers and cruisers too.  a
    few complexes have no gate at all ("War is a full time job", 2009)
  - the capture point types (10, 20 and 30 km, each faction's) are the client's (group 922 Capture Point)
  - the guards are the faction's own militia line of ships from the client data (Imperial, State, Federation and Republic
    frigates, destroyers and cruisers, plain and Templar/Shukuro/Praktor/Tribal), a pick of which and how many
  - names, the capture point's place, the guards' waves and where they sit are picks.  FacWarMgr spawns these by id,
    so they have no spawn weight, and they last until they're taken

    python3 utils/facwar/gen_facwar_sql.py sql/migrations/20261205100000-facwar-complexes.sql
"""
import math
import random
import sys

ARCH_FACWAR = 35        # dunArchetypeFacwarOffensive;  a complex is offensive or defensive by who holds the system
SPAWN_CREATED, SPAWN_TRIGGER = 0, 2
T_EXPLODING = 3
E_SPAWN_GUARDS = 2
ACCEL_GATE = 17831

# id ranges (above the DED complexes' 130000000 / 13000000 and the gas sites')
DUNGEON_BASE, ROW_BASE = 150000000, 15000000

CALDARI, MINMATAR, AMARR, GALLENTE = 500001, 500002, 500003, 500004
FACTION_NAME = {AMARR: 'Amarr', CALDARI: 'Caldari', GALLENTE: 'Gallente', MINMATAR: 'Minmatar'}

# capture points (group 922) by faction and size
CAPTURE_POINT = {
    AMARR: {1: 29207, 2: 29310, 3: 29311},
    CALDARI: {1: 29414, 2: 29418, 3: 29421},
    GALLENTE: {1: 29415, 2: 29419, 3: 29422},
    MINMATAR: {1: 29416, 2: 29420, 3: 29423},
}

# the factions' militia ships (Mission <empire> Frigate/Destroyer/Cruiser groups):  plain, then the elite line
SHIPS = {
    AMARR: {
        'F': [24891, 24892, 24893, 24894, 24895, 24896, 24898, 24899, 24901, 24902, 24904, 24905],
        'F+': [24900, 24906, 24907, 24909],
        'D': [24911, 24912, 24913, 24915], 'D+': [24917, 24919],
        'C': [24933, 24935, 24938, 24940, 24941], 'C+': [24942, 24944, 24947, 24949, 24952],
    },
    CALDARI: {
        'F': [24997, 24999, 25001, 25004, 25007, 25008, 25011, 25012], 'F+': [25013, 25015],
        'D': [25020, 25021, 25022, 25024], 'D+': [25026, 25028],
        'C': [25043, 25045, 25046, 25048, 25050, 25052], 'C+': [25054, 25056, 25057, 25058],
    },
    GALLENTE: {
        'F': [25103, 25105, 25106, 25107, 25110, 25112, 25116, 25124], 'F+': [25117, 25118, 25120],
        'D': [25125, 25126, 25127, 25129], 'D+': [25131, 25133],
        'C': [25147, 25149, 25150, 25152, 25154], 'C+': [25156, 25158, 25160, 25161, 25162],
    },
    MINMATAR: {
        'F': [24777, 24779, 24781, 24784, 24787, 24790, 24791], 'F+': [24785, 24786, 24793],
        'D': [24797, 24798, 24799], 'D+': [24801, 24805, 24810],
        'C': [24821, 24823, 24824, 24826, 24828], 'C+': [24830, 24832, 24834, 24835, 24836],
    },
}

# guards' waves by size:  (line, count min, count max) per wave.  the next wave comes when the last one is dead
WAVES = {
    1: [[('F', 2, 3)], [('F', 2, 2), ('D', 0, 1)]],
    2: [[('F', 2, 2), ('D', 1, 1)], [('F+', 2, 2), ('D', 1, 1), ('C', 0, 1)]],
    3: [[('D', 2, 2), ('C', 1, 1)], [('F+', 2, 2), ('C', 2, 2)], [('D+', 1, 1), ('C+', 2, 2)]],
}
SIZE_NAME = {1: 'Minor', 2: 'Medium', 3: 'Major'}

# gate limits (invGroups) by size
L1 = [25, 237, 31, 1022, 420]                   # frigate, rookie ship, shuttle, prototype exploration ship, destroyer
L2 = L1 + [26, 324, 830, 831, 834, 893]         # + cruiser, assault frigate, covert ops, interceptor, stealth bomber, EAF
L3 = L2 + [419, 541, 358, 832, 833, 906, 894]   # + battlecruiser, interdictor, HAC, logistics, recons, HIC
GATE = {1: L1, 2: L2, 3: L3}

# what's made:  every faction a gated minor, medium and major, and an ungated major
KINDS = [(1, True), (2, True), (3, True), (3, False)]


class Out:
    def __init__(self):
        self.dungeons, self.rooms, self.objects, self.gates, self.groups, self.types = ([] for _ in range(6))
        self.triggers, self.events = [], []
        self.nextDungeon = DUNGEON_BASE
        self.next = {k: ROW_BASE for k in ('room', 'object', 'group', 'trigger', 'event')}

    def id(self, kind):
        self.next[kind] += 1
        return self.next[kind]


def ring(rng, dist, jitter=0.25):
    a = rng.uniform(0, 2 * math.pi)
    d = dist * rng.uniform(1 - jitter, 1 + jitter)
    return (round(d * math.cos(a)), round(rng.uniform(-0.1, 0.1) * d), round(d * math.sin(a)))


def build(out, faction, size, gated):
    name = '%s %s Military Complex' % (FACTION_NAME[faction], SIZE_NAME[size])
    if not gated:
        name = '%s %s Military Outpost' % (FACTION_NAME[faction], SIZE_NAME[size])
    rng = random.Random(name)
    out.nextDungeon += 1
    d = out.nextDungeon
    # difficulty = the size (FacWarMgr reads it back)
    out.dungeons.append((d, name, faction, 'facwar-%d' % d, size))

    room = out.id('room')
    out.rooms.append((room, name, d))
    if gated:
        o = out.id('object')
        out.objects.append((o, room, ACCEL_GATE, ring(rng, 8000, 0.3)))
        out.gates.append((o, ','.join(str(g) for g in GATE[size])))
        room = out.id('room')
        out.rooms.append((room, '%s - capture area' % name, d))

    # the capture point, a few km off where ships land
    o = out.id('object')
    out.objects.append((o, room, CAPTURE_POINT[faction][size], ring(rng, 5000, 0.2)))

    groups = []
    for w, wave in enumerate(WAVES[size]):
        entries = {}
        for line, lo, hi in wave:
            for _ in range(rng.randint(lo, hi)):
                t = rng.choice(SHIPS[faction][line])
                entries[t] = entries.get(t, 0) + 1
        if not entries:
            continue
        g = out.id('group')
        out.groups.append((g, room, w + 1, SPAWN_CREATED if w == 0 else SPAWN_TRIGGER, ring(rng, 12000, 0.3), faction))
        out.types.extend((g, t, q) for t, q in entries.items())
        groups.append(g)
    for i in range(len(groups) - 1):
        t = out.id('trigger')
        out.triggers.append((t, room, T_EXPLODING, groups[i]))
        out.events.append((out.id('event'), t, 1, E_SPAWN_GUARDS, groups[i + 1]))


def sql_rows(rows, fmt):
    return ',\n'.join(fmt % r for r in rows)


def main(path):
    out = Out()
    for faction in (AMARR, CALDARI, GALLENTE, MINMATAR):
        for size, gated in KINDS:
            build(out, faction, size, gated)

    def q(s):
        return "'" + s.replace("'", "''") + "'"

    L = []
    L.append('-- factional warfare complexes:  each faction\'s minor, medium and major military complex, the gates')
    L.append('--  taking what militia missions of levels 1, 2 and 3 allow, and an ungated major.  utils/facwar/gen_facwar_sql.py')
    L.append('--  writes this (the sources and picks are listed there)')
    L.append('--  - archetype 35 (dunArchetypeFacwarOffensive), the holding faction, difficulty = the size (1 minor, 2 medium, 3')
    L.append('--    major).  spawn weight 0:  FacWarMgr spawns them in the war zones, by id')
    L.append('--  - the last room has the faction\'s capture point and its militia ships, wave after wave')
    L.append('-- +migrate Up')
    L.append('INSERT INTO dunDungeons (dungeonID, dungeonName, dungeonStatus, factionID, archetypeID, dungeonUUID, '
             'dungeonNameID, difficulty, minSecurity, maxSecurity, spawnWeight, lifetimeHours) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2], ARCH_FACWAR, q(r[3]), r[4]) for r in out.dungeons],
                      '(%d, %s, 1, %d, %d, %s, 0, %d, -1, 1, 0, 0)') + ';')
    L.append('INSERT INTO dunRooms (roomID, roomName, dungeonID, arrivalX, arrivalY, arrivalZ) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2]) for r in out.rooms], '(%d, %s, %d, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunRoomObjects (objectID, roomID, typeID, groupID, x, y, z, yaw, pitch, roll, radius) VALUES')
    L.append(sql_rows([(o, r, t, p[0], p[1], p[2]) for o, r, t, p in out.objects], '(%d, %d, %d, 0, %d, %d, %d, 0, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunGates (objectID, destRoomID, locked, requireClear, keyTypeID, keyQuantity, keyConsumed, '
             'skillTypeID, skillLevel, allowedGroups, deniedGroups, musicUrl) VALUES')
    L.append(sql_rows([(o, q(a)) for o, a in out.gates], "(%d, 0, 0, 0, 0, 1, 1, 0, 0, %s, '', '')") + ';')
    L.append('INSERT INTO dunSpawnGroups (spawnGroupID, roomID, waveNumber, spawnMode, npcClassType, npcClassSub, '
             'factionID, x, y, z, spread, warpIn, isOverseer, lootGroupID, aggroMode, aggroRange, aggroLink) VALUES')
    L.append(sql_rows([(g, r, w, m, f, p[0], p[1], p[2]) for g, r, w, m, p, f in out.groups],
                      '(%d, %d, %d, %d, 0, 0, %d, %d, %d, %d, 6000, 0, 0, 0, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunSpawnGroupTypes (spawnGroupID, typeID, quantity) VALUES')
    L.append(sql_rows(out.types, '(%d, %d, %d)') + ';')
    L.append('INSERT INTO dunTriggers (triggerID, roomID, triggerType, spawnGroupID, objectID, value, counterID, repeatable) VALUES')
    L.append(sql_rows(out.triggers, '(%d, %d, %d, %d, 0, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunTriggerEvents (eventID, triggerID, sequence, eventType, delaySeconds, spawnGroupID, objectID, '
             'messageType, messageID, messageText, typeID, quantity, value, counterID) VALUES')
    L.append(sql_rows(out.events, "(%d, %d, %d, %d, 0, %d, 0, 0, 0, '', 0, 0, 0, 0)") + ';')
    L.append('')
    L.append('-- +migrate Down')
    L.append('DELETE FROM dunGates WHERE objectID > %d AND objectID < %d;' % (ROW_BASE, ROW_BASE + 1000000))
    for table, col in (('dunTriggerEvents', 'eventID'), ('dunTriggers', 'triggerID'), ('dunSpawnGroupTypes', 'spawnGroupID'),
                       ('dunSpawnGroups', 'spawnGroupID'), ('dunRoomObjects', 'objectID'), ('dunRooms', 'roomID')):
        L.append('DELETE FROM %s WHERE %s > %d AND %s < %d;' % (table, col, ROW_BASE, col, ROW_BASE + 1000000))
    L.append('DELETE FROM dunDungeons WHERE dungeonID > %d AND dungeonID < %d;' % (DUNGEON_BASE, DUNGEON_BASE + 1000000))
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L) + '\n')
    print('%d complexes, %d rooms, %d gates, %d groups, %d triggers' %
          (len(out.dungeons), len(out.rooms), len(out.gates), len(out.groups), len(out.triggers)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'sql/migrations/20261205100000-facwar-complexes.sql')
