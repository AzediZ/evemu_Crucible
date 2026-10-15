#!/usr/bin/env python3
"""Generates sql/migrations/20261023100000-ded-complexes.sql:  the DED rated complexes of Crucible, 1/10 to 10/10.

What's from sources and what's a pick:
  - the 48 complexes, their factions and ratings, and the security each rating spawns in:  the DED complex list on
    gamingwithdaopa.ellatha.com/eveonline/?p=507 (1/10-5/10 high and low-sec, 6/10 low and null, 7/10-10/10 null)
  - gate limits by rating, the Overseer's Personal Effects tier of each rating and some bosses:  the research in
    reports/ded (EVE University and eve-survival pages of 2011-2012, and current ones where nothing older exists)
  - bosses where the client data names one for the complex (its description or name says so, see BOSSES)
  - in Crucible the 1/10s and most 2/10s were fixed beacons on the map (until Retribution);  here they are
    signatures like the rest (pick)
  - everything else is a pick, kept close to how the complexes played:  you warp to the signature and land at an
    acceleration gate, which (and every gate after it) only takes the ships the rating allows.  each pocket has two or
    three waves of the faction's deadspace ships, the next wave coming when the last one is dead, and the last pocket
    has the overseer, who drops Overseer's Personal Effects.  killing it completes the complex
  - waves are drawn from the faction's deadspace ships (ded_npcs.csv, from the Crucible client data), the weakest
    ones by bounty (hit points for rogue drones) for the low ratings and the strongest for the high ones.  room counts, wave sizes and spawn weights are
    in the tables below

ded_npcs.csv is the deadspace frigate to battleship groups of the six factions (typeID, groupID, name, bounty, hit
points), taken from the EVEmu database (invTypes;  attributes 481 entityKillBounty, 263 + 265 + 9 for the hit points).

    python3 utils/ded/gen_ded_sql.py sql/migrations/20261023100000-ded-complexes.sql
"""
import csv
import math
import os
import random
import re
import sys

ARCH_RATED = 10
SPAWN_CREATED, SPAWN_TRIGGER = 0, 2
T_EXPLODING = 3
E_SPAWN_GUARDS, E_COMPLETE = 2, 11
ACCEL_GATE = 17831

# id ranges (above the Sleeper sites' 120000000 / 12000000)
DUNGEON_BASE, ROW_BASE, LOOT_BASE = 130000000, 13000000, 13000000

ANGEL, BLOOD, GURISTAS, SANSHA, SERPENTIS, DRONES = 500011, 500012, 500010, 500019, 500020, 500022

# ---- the complexes (gamingwithdaopa DED list).  security:  HL high and low-sec, LN low and null, N null
COMPLEXES = [
    ('Minmatar Contracted Bio-Farm', ANGEL, 1), ('Angel Creo-Corp Mining', ANGEL, 2),
    ('Angel Repurposed Outpost', ANGEL, 3), ('Angel Cartel Occupied Mining Colony', ANGEL, 4),
    ("Angel's Red Light District", ANGEL, 5), ('Angel Mineral Acquisition Outpost', ANGEL, 6),
    ('Angel Military Operations Complex', ANGEL, 7), ('Cartel Prisoner Retention', ANGEL, 8),
    ('Angel Cartel Naval Shipyard', ANGEL, 10),

    ('Old Meanie - Cultivation Center', BLOOD, 1), ('Blood Raider Human Farm', BLOOD, 2),
    ('Blood Raider Intelligence Collection Point', BLOOD, 3), ('Mul-Zatah Monastery', BLOOD, 4),
    ('Blood Raider Psychotropics Depot', BLOOD, 5), ('Crimson Hand Supply Depot', BLOOD, 6),
    ('Blood Raider Coordination Center', BLOOD, 7), ('Blood Raider Prison Camp', BLOOD, 8),
    ('Blood Raider Naval Shipyard', BLOOD, 10),

    ('Pith-Robux Asteroid Mining Co.', GURISTAS, 1), ('Pith Deadspace Depot', GURISTAS, 2),
    ('Guristas Guerilla Grounds', GURISTAS, 3), ('Guristas Scout Outpost', GURISTAS, 4),
    ('Guristas Hallucinogen Supply Waypoint', GURISTAS, 5), ('Guristas Troop Reinvigoration Camp', GURISTAS, 6),
    ('Guristas Military Operations Complex', GURISTAS, 7), ("Pith's Penal Complex", GURISTAS, 8),
    ('The Maze', GURISTAS, 10),

    ('Sansha Military Outpost', SANSHA, 1), ('Sansha Acclimatization Facility', SANSHA, 2),
    ("Sansha's Command Relay Outpost", SANSHA, 3), ("Sansha's Nation Occupied Mining Colony", SANSHA, 4),
    ("Sansha's Nation Neural Paralytic Facility", SANSHA, 5), ('Sansha War Supply Complex', SANSHA, 6),
    ('Sansha Military Operations Complex', SANSHA, 7), ('Sansha Prison Camp', SANSHA, 8),
    ('Centus Assembly T.P. Co.', SANSHA, 10),

    ('Serpentis Drug Outlet', SERPENTIS, 1), ('Serpentis Live Cargo Distribution Facilities', SERPENTIS, 2),
    ('Serpentis Narcotic Warehouses', SERPENTIS, 3), ('Serpentis Phi-Outpost', SERPENTIS, 4),
    ('Serpentis Corporation Hydroponics Site', SERPENTIS, 5), ('Serpentis Logistical Outpost', SERPENTIS, 6),
    ('Serpentis Paramilitary Complex', SERPENTIS, 7), ('Serpentis Prison Camp', SERPENTIS, 8),
    ('Serpentis Fleet Shipyard', SERPENTIS, 10),

    ('Rogue Drone Infestation Sprout', DRONES, 2), ('Rogue Drone Asteroid Infestation', DRONES, 3),
    ('Outgrowth Rogue Drone Hive', DRONES, 10),
]

# bosses the client data ties to a complex (the type's description, or its name).  the rest get the strongest deadspace
#  ship of their size (BOSS_SIZE)
BOSSES = {
    "Angel's Red Light District": 17413,                    # Captain Rouge:  "If Angel's Red Light District ..."
    'Angel Cartel Occupied Mining Colony': 2846,            # Domination Excavator
    'Cartel Prisoner Retention': 17973,                     # Angel Cartel Jailor
    'Blood Raider Human Farm': 4355,                        # Dark Blood Keeper:  "protect the human farm's living produce"
    'Mul-Zatah Monastery': 17616,                           # Mul-Zatath Gatekeeper
    'Blood Raider Coordination Center': 3825,               # Dark Blood Hunter
    'Guristas Scout Outpost': 17284,                        # Guristas Scout Officer
    'Guristas Military Operations Complex': 3821,            # Dread Guristas Commanding Officer
    "Pith's Penal Complex": 19592,                          # 'Screaming' Dewak Humfry (Dewak's Dot names the complex)
    "Sansha's Nation Occupied Mining Colony": 2847,         # True Sansha Foreman
    "Sansha's Nation Neural Paralytic Facility": 3889,      # Slave Ation09:  "maintaining Nation's supply of neural paralytics"
    'Sansha Military Operations Complex': 3823,             # True Sansha Archduke:  "favored by Sansha military outpost commanders"
    'Serpentis Drug Outlet': 17890,                         # Serpentis Drugstore Overseer
    'Serpentis Live Cargo Distribution Facilities': 4356,   # Shadow Serpentis Warden
    'Serpentis Phi-Outpost': 17412,                         # Phi-Operation Protector
    'Serpentis Paramilitary Complex': 3827,                 # Shadow Serpentis Big Boss
    'Outgrowth Rogue Drone Hive': 19704,                    # Supreme Hive Defender
    # from the research (reports/ded):  the 2011-2012 and current pages' overseer, where it is a Crucible type
    'Sansha Military Outpost': 17903,                       # Outpost Security Officer
    'Sansha Acclimatization Facility': 4354,                # Sansha Tenebrus
    "Sansha's Command Relay Outpost": 17285,                # Centus Black Ops Commander
    'Sansha War Supply Complex': 20080,                     # Overseer Skomener Effotber
    'Serpentis Corporation Hydroponics Site': 3890,         # Sarpati Family Enforcer
}

# ---- by rating (picks)
SECURITY = {1: 'HL', 2: 'HL', 3: 'HL', 4: 'HL', 5: 'HL', 6: 'LN', 7: 'N', 8: 'N', 10: 'N'}
SEC_RANGE = {'HL': (0.05, 1.0), 'LN': (-1.0, 0.4499), 'N': (-1.0, 0.0499)}
POCKETS = {1: 2, 2: 2, 3: 3, 4: 3, 5: 3, 6: 4, 7: 4, 8: 4, 10: 5}
WEIGHT = {1: 10, 2: 10, 3: 10, 4: 8, 5: 6, 6: 6, 7: 4, 8: 4, 10: 2}
OPE_TIER = {1: 3, 2: 6, 3: 6, 4: 8, 5: 18, 6: 19, 7: 20, 8: 21, 10: 23}     # the tiers the 2011-2012 pages give (research, OPE)
OPE = {1: 19400, 2: 19401, 3: 19402, 4: 19403, 5: 19404, 6: 19405, 7: 19406, 8: 19407, 9: 19408, 10: 19422,
       11: 19409, 12: 19410, 13: 19411, 14: 19412, 15: 19413, 16: 19414, 17: 19415, 18: 19416, 19: 19417,
       20: 19418, 21: 19419, 22: 19420, 23: 19421}
# waves:  (size, count min, count max) per wave, every pocket rolls 2-3 of these.  sizes F D C BC BS
WAVES = {
    1: [('F', 3, 4)],
    2: [('F', 3, 5), ('D', 1, 2)],
    3: [('F', 2, 4), ('D', 1, 2), ('C', 1, 2)],
    4: [('F', 2, 3), ('D', 2, 2), ('C', 2, 3)],
    5: [('D', 1, 2), ('C', 3, 4), ('BC', 1, 2)],
    6: [('C', 2, 3), ('BC', 2, 3), ('BS', 1, 2)],
    7: [('C', 2, 2), ('BC', 2, 3), ('BS', 2, 3)],
    8: [('C', 2, 2), ('BC', 2, 2), ('BS', 3, 4)],
    10: [('BC', 3, 3), ('BS', 4, 5)],
}
# which part of each size's ships (sorted by bounty) a rating draws from, as fractions of the list
BAND = {1: (0.0, 0.4), 2: (0.0, 0.5), 3: (0.1, 0.6), 4: (0.2, 0.7), 5: (0.3, 0.8), 6: (0.4, 0.9), 7: (0.5, 1.0),
        8: (0.6, 1.0), 10: (0.7, 1.0)}
BOSS_SIZE = {1: 'F', 2: 'D', 3: 'D', 4: 'C', 5: 'C', 6: 'BC', 7: 'BS', 8: 'BS', 10: 'BS'}

# gate limits (invGroups).  capitals never use gates (DungeonMgr refuses them)
FRIGATES = [25, 237, 324, 830, 831, 834, 893, 1022, 31]     # frigate, rookie, assault, covops, inty, bomber, EAF, proto exploration, shuttle
DESTROYERS = [420, 541]
CRUISERS = [26, 358, 832, 833, 906, 894]
BATTLECRUISERS = [419, 540]
def gate_groups(rating):
    """T2 counts by its hull;  strategic cruisers and the Orca could use 3/10 and 4/10 gates until Odyssey (research)"""
    if rating == 1:
        return FRIGATES
    if rating == 2:
        return FRIGATES + DESTROYERS
    if rating == 3:
        return FRIGATES + DESTROYERS + CRUISERS + [963, 941]
    if rating == 4:
        return FRIGATES + DESTROYERS + CRUISERS + [963, 941] + BATTLECRUISERS
    return []   # 5/10 and up:  battleships and smaller, so anything but capitals (DungeonMgr refuses those)

# the factions' deadspace groups and name patterns (the plain line;  Domination/Dark/Dread/True/Shadow and the Arch/Elder/
#  Dire lines are the same ships stronger, so they all count, sorted by bounty)
GROUPS = {
    ANGEL: {'F': 597, 'D': 596, 'C': 595, 'BC': 593, 'BS': 594},
    BLOOD: {'F': 606, 'D': 605, 'C': 604, 'BC': 602, 'BS': 603},
    GURISTAS: {'F': 615, 'D': 614, 'C': 613, 'BC': 611, 'BS': 612},
    SANSHA: {'F': 624, 'D': 623, 'C': 622, 'BC': 620, 'BS': 621},
    SERPENTIS: {'F': 633, 'D': 632, 'C': 631, 'BC': 629, 'BS': 630},
    DRONES: {'F': 805, 'D': 804, 'C': 803, 'BC': 801, 'BS': 802},
}
STEMS = {ANGEL: 'Gist', BLOOD: 'Corp', GURISTAS: 'Pith', SANSHA: 'Cent', SERPENTIS: 'Core', DRONES: 'Alv'}
SKIP = re.compile(r'Fugitive|Transport|Vessel|Renegade|_|COSMOS|Replica|MISSION|Spawn|Boss|Parasite|Unidentified|'
                  r'Anti-Stabilizer|Spider Drone|Domination Alvus|Arch Angel|Karkoti|Raelek')


def load_npcs(path):
    by_group = {}
    with open(path, newline='') as f:
        for row in csv.DictReader(f):
            by_group.setdefault(int(row['groupID']), []).append((int(row['typeID']), row['name'], int(row['bounty']), int(row['hp'])))
    return by_group


def ship_lists(npcs):
    """faction -> size -> [typeID] weakest first:  by bounty, then hit points (rogue drones have no bounty)"""
    out = {}
    for faction, groups in GROUPS.items():
        stem = STEMS[faction]
        out[faction] = {}
        for size, groupID in groups.items():
            ships = [(b, hp, t) for t, name, b, hp in npcs.get(groupID, [])
                     if stem in name and not SKIP.search(name) and hp > 0]
            ships.sort()
            if not ships:
                raise SystemExit('no %s ships for faction %d' % (size, faction))
            out[faction][size] = [t for _, _, t in ships]
    return out


def pick(rng, ships, rating):
    lo, hi = BAND[rating]
    n = len(ships)
    a = min(int(lo * n), n - 1)
    b = max(a + 1, int(math.ceil(hi * n)))
    return rng.choice(ships[a:b])


class Out:
    def __init__(self):
        self.dungeons, self.rooms, self.objects, self.gates, self.groups, self.types = ([] for _ in range(6))
        self.triggers, self.events, self.loot = [], [], []
        self.nextDungeon = DUNGEON_BASE
        self.next = {k: ROW_BASE for k in ('room', 'object', 'group', 'trigger', 'event')}

    def id(self, kind):
        self.next[kind] += 1
        return self.next[kind]


def ring(rng, dist, jitter=0.25):
    a = rng.uniform(0, 2 * math.pi)
    d = dist * rng.uniform(1 - jitter, 1 + jitter)
    return (round(d * math.cos(a)), round(rng.uniform(-0.1, 0.1) * d), round(d * math.sin(a)))


def build(out, ships, name, faction, rating):
    rng = random.Random(name)
    out.nextDungeon += 1
    d = out.nextDungeon
    lo, hi = SEC_RANGE[SECURITY[rating]]
    out.dungeons.append((d, name, faction, 'ded-%d' % d, rating, lo, hi, WEIGHT[rating]))
    allowed = ','.join(str(g) for g in gate_groups(rating))

    def gate(room):
        o = out.id('object')
        out.objects.append((o, room, ACCEL_GATE, ring(rng, 10000, 0.3)))
        out.gates.append((o, allowed))

    # the warp-in:  just the gate
    entry = out.id('room')
    out.rooms.append((entry, name, d))
    gate(entry)

    pockets = POCKETS[rating]
    for p in range(1, pockets + 1):
        room = out.id('room')
        out.rooms.append((room, '%s - pocket %d' % (name, p), d))
        last = (p == pockets)
        if not last:
            gate(room)
        nwaves = rng.choice((2, 3))
        groups = []
        for w in range(nwaves):
            entries = {}
            for size, lo_n, hi_n in WAVES[rating]:
                for _ in range(rng.randint(lo_n, hi_n)):
                    t = pick(rng, ships[faction][size], rating)
                    entries[t] = entries.get(t, 0) + 1
            g = out.id('group')
            out.groups.append((g, room, w + 1, SPAWN_CREATED if w == 0 else SPAWN_TRIGGER, ring(rng, 25000, 0.3), 0, 0))
            out.types.extend((g, t, q) for t, q in entries.items())
            groups.append(g)
        boss = None
        if last:
            bossType = BOSSES.get(name) or ships[faction][BOSS_SIZE[rating]][-1]
            boss = out.id('group')
            out.groups.append((boss, room, nwaves + 1, SPAWN_TRIGGER, ring(rng, 30000, 0.2), 1, LOOT_BASE + rating))
            out.types.append((boss, bossType, 1))
        # each wave's death brings the next;  the last wave brings the overseer, whose death completes the complex
        chain = groups + ([boss] if boss else [])
        for i in range(len(chain) - 1):
            t = out.id('trigger')
            out.triggers.append((t, room, T_EXPLODING, chain[i]))
            out.events.append((out.id('event'), t, 1, E_SPAWN_GUARDS, chain[i + 1]))
        if boss:
            t = out.id('trigger')
            out.triggers.append((t, room, T_EXPLODING, boss))
            out.events.append((out.id('event'), t, 1, E_COMPLETE, 0))


def sql_rows(rows, fmt):
    return ',\n'.join(fmt % r for r in rows)


def main(path):
    here = os.path.dirname(os.path.abspath(__file__))
    ships = ship_lists(load_npcs(os.path.join(here, 'ded_npcs.csv')))
    out = Out()
    for name, faction, rating in COMPLEXES:
        build(out, ships, name, faction, rating)
    for rating in sorted(OPE_TIER):
        out.loot.append((LOOT_BASE + rating, OPE[OPE_TIER[rating]]))

    def q(s):
        return "'" + s.replace("'", "''") + "'"

    L = []
    L.append('-- DED complexes:  the 48 rated complexes of Crucible, 1/10 to 10/10.')
    L.append('--  utils/ded/gen_ded_sql.py writes this (the sources and picks are listed there)')
    L.append('--  - archetype 10 (rated), difficulty = the DED rating, security by rating:  1-5 high and low-sec, 6 low and')
    L.append('--    null, 7-10 null.  AnomalyMgr picks the region\'s pirate faction')
    L.append('--  - an entry gate and 2-5 pockets.  the gates take what the rating allows;  the overseer in the last pocket')
    L.append('--    drops Overseer\'s Personal Effects and its death completes the complex')
    L.append('-- +migrate Up')
    L.append('INSERT INTO dunDungeons (dungeonID, dungeonName, dungeonStatus, factionID, archetypeID, dungeonUUID, '
             'dungeonNameID, difficulty, minSecurity, maxSecurity, spawnWeight, lifetimeHours) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2], ARCH_RATED, q(r[3]), r[4], r[5], r[6], r[7]) for r in out.dungeons],
                      '(%d, %s, 1, %d, %d, %s, 0, %d, %g, %g, %d, 0)') + ';')
    L.append('INSERT INTO dunRooms (roomID, roomName, dungeonID, arrivalX, arrivalY, arrivalZ) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2]) for r in out.rooms], '(%d, %s, %d, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunRoomObjects (objectID, roomID, typeID, groupID, x, y, z, yaw, pitch, roll, radius) VALUES')
    L.append(sql_rows([(o, r, t, p[0], p[1], p[2]) for o, r, t, p in out.objects], '(%d, %d, %d, 0, %d, %d, %d, 0, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunGates (objectID, destRoomID, locked, requireClear, keyTypeID, keyQuantity, keyConsumed, '
             'skillTypeID, skillLevel, allowedGroups, deniedGroups, musicUrl) VALUES')
    L.append(sql_rows([(o, q(a)) for o, a in out.gates], "(%d, 0, 0, 0, 0, 1, 1, 0, 0, %s, '', '')") + ';')
    L.append('INSERT INTO dunSpawnGroups (spawnGroupID, roomID, waveNumber, spawnMode, npcClassType, npcClassSub, '
             'factionID, x, y, z, spread, warpIn, isOverseer, lootGroupID, aggroMode, aggroRange, aggroLink) VALUES')
    L.append(sql_rows([(g, r, w, m, p[0], p[1], p[2], o, l) for g, r, w, m, p, o, l in out.groups],
                      '(%d, %d, %d, %d, 0, 0, 0, %d, %d, %d, 8000, 0, %d, %d, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunSpawnGroupTypes (spawnGroupID, typeID, quantity) VALUES')
    L.append(sql_rows(out.types, '(%d, %d, %d)') + ';')
    L.append('INSERT INTO dunTriggers (triggerID, roomID, triggerType, spawnGroupID, objectID, value, counterID, repeatable) VALUES')
    L.append(sql_rows(out.triggers, '(%d, %d, %d, %d, 0, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunTriggerEvents (eventID, triggerID, sequence, eventType, delaySeconds, spawnGroupID, objectID, '
             'messageType, messageID, messageText, typeID, quantity, value, counterID) VALUES')
    L.append(sql_rows(out.events, "(%d, %d, %d, %d, 0, %d, 0, 0, 0, '', 0, 0, 0, 0)") + ';')
    L.append("-- Overseer's Personal Effects by rating")
    L.append('INSERT INTO dunLoot (lootGroupID, typeID, minQty, maxQty, chance) VALUES')
    L.append(sql_rows(out.loot, '(%d, %d, 1, 1, 1)') + ';')
    L.append('')
    L.append('-- +migrate Down')
    L.append('DELETE FROM dunLoot WHERE lootGroupID > %d AND lootGroupID < %d;' % (LOOT_BASE, LOOT_BASE + 100))
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
    main(sys.argv[1] if len(sys.argv) > 1 else 'sql/migrations/20261023100000-ded-complexes.sql')
