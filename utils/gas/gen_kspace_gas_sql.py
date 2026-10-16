#!/usr/bin/env python3
"""Generates sql/migrations/20261024160000-kspace-gas-sites.sql:  the booster gas sites ("<Name> Nebula", Ladar
signatures) of known space as they were in Crucible.

Sources are in /reports/gas/kspace-gas-research.md:  the ATAP Guide to Booster Production (2010), the Trinity chronicle
news (2007), an ISD post with the null hot-spot constellations (2010), eve-survival's Forgotten Nebula (2012-01-20) and
the Evelopedia gas site list (signature strengths).  What they don't give is a pick, and is marked (pick) below.

Each gas colour has an empire region with a low-sec hot-spot constellation and a null region with a hot-spot
constellation:
  - Mykoserocin, small (2 clouds of 500, 10%) and large (3 clouds of 1,000, 5%), no NPCs.  mostly in the empire
    hot-spot constellation, now and then anywhere else in that region, high sec included
  - Cytoserocin, low sec (clouds of 300 and 200, 2.5%), no NPCs.  any low-sec system of the empire region
  - Cytoserocin, null sec:  only in the null hot-spot constellation.  the 10% and 5% sites are unguarded, the 2.5% and
    1.25% ones have the region's rats (pick).  units (pick, from 2013-2018 observations):  10% 500 + 1,500,
    5% and 2.5% 500 + 2,500, 1.25% one cloud of 9,000.  null sites without a known strength follow the 10/5/2.5/1.25%
    pattern in the order listed (pick)

spawnWeight:  AnomalyMgr weighs the Ladar sites a system could get against 100 of nothing (GAS_SITE_NONE_WEIGHT), so
the weights below are about how often a Ladar roll makes a gas site (pick).  Regions without booster gas get none.

The 24 nebulae of the dungeon seed (20261011133100-F13-dungeon-seed.sql, rooms and client names, no clouds) are used
as they are;  the rest are new dungeons.  The region-wide Mykoserocin sites are copies of the hot-spot ones.

    python3 utils/gas/gen_kspace_gas_sql.py sql/migrations/20261024160000-kspace-gas-sites.sql
"""
import math
import random
import sys

LADAR = 5
T_ROOM_MINED_OUT, E_COMPLETE = 9, 11
MODE_ENTERED = 1
DUNGEON_BASE, ROW_BASE = 140000000, 14000000
LOW_SEC_MAX = 0.4499            # below 0.45 (shown as 0.4 and lower)

# the dungeon seed's nebulae:  name -> (dungeonID, roomID, client name messageID)
SEED = {
    'Wild': (100511100, 1000415, 111028), 'Pipe': (100512100, 1000416, 111031), 'Calabash': (100513100, 1000417, 111030),
    'Blackeye': (100514100, 1000418, 111036), 'Smoking': (100515100, 1000419, 111026), 'Glass': (100516100, 1000420, 111038),
    'Flame': (100517100, 1000421, 111023), 'Ghost': (100518100, 1000422, 111024), 'Sister': (100521100, 1000423, 111027),
    'Bright': (100522100, 1000424, 111029), 'Crimson': (100523100, 1000425, 113132), 'Phoenix': (100524100, 1000426, 111308),
    'Eagle': (100525100, 1000427, 111032), 'Rapture': (100526100, 1000428, 113129), 'Ring': (100527100, 1000429, 111034),
    'Sparkling': (100528100, 1000430, 0), 'Forgotten': (100531100, 1000431, 111307), 'Diabolo': (100532100, 1000432, 0),
    'Saintly': (100533100, 1000433, 113130), 'Sunspark': (100534100, 1000434, 111025), 'Emerald': (100535100, 1000435, 113131),
    'Swarm': (100536100, 1000436, 113686), 'Boisterous': (100537100, 1000437, 110645), 'Thick': (100538100, 1000438, 113684),
}

# colour: Mykoserocin, Cytoserocin, (empire region, hot-spot), (null region, hot-spot), small myko, large myko, low-sec cyto,
#  null sites [(name, strength %)]
COLOURS = [
    ('Amber', 28694, 25268, (10000002, 20000028), (10000003, 20000044), 'Sunspark', 'Diabolo', 'Bandit',
     [('Dewy', 0), ('Shimmering', 0), ('Foggy', 0), ('Rocky', 0), ('Spacious', 0)]),
    ('Golden', 28697, 25273, (10000016, 20000211), (10000045, 20000527), 'Smoking', 'Ring', 'Profiteer',
     [('Swarm', 10), ('Moth', 5), ('Lion', 2.5), ('Gaseous', 1.25), ('Shiny', 1.25)]),
    ('Viridian', 28701, 25274, (10000048, 20000561), (10000051, 20000594), 'Bright', 'Sparkling', 'Forgotten',
     [('Icy', 0), ('Pale', 0), ('Goose', 2.5), ('Polar Bear', 1.25)]),
    ('Celadon', 28696, 25275, (10000044, 20000524), (10000058, 20000665), 'Calabash', 'Glass', 'Phoenix',
     [('Flowing', 10), ('Thick', 5), ('Peacock', 2.5), ('Massive', 1.25), ('Diamond', 1.25)]),
    ('Lime', 28698, 25277, (10000001, 20000016), (10000014, 20000175), 'Sister', 'Helix', 'Emerald',
     [('Small', 0), ('Duo', 0), ('Cheetah', 0), ('Leopard', 0), ('Rimy', 0)]),
    ('Malachite', 28699, 25276, (10000054, 20000628), (10000060, 20000702), 'Wild', 'Blackeye', 'Crimson',
     [('Hazy', 0), ('Whirling', 0), ('Hidden', 0), ('Cobra', 0), ('Crab Spider', 0)]),
    ('Azure', 28695, 25279, (10000028, 20000351), (10000006, 20000086), 'Ghost', 'Eagle', 'Rapture',
     [('Crystal', 5), ('Murky', 1.25)]),
    ('Vermillion', 28700, 25278, (10000030, 20000372), (10000056, 20000646), 'Flame', 'Pipe', 'Saintly',
     [('Wispy', 0), ('Vaporous', 0), ('Boisterous', 0), ('Cardinal', 0), ('Red Dragonfly', 0)]),
]
PATTERN = [10, 5, 2.5, 1.25, 1.25]

# null Cytoserocin by strength:  clouds, weight, guards (npcSpawnClass type, random row;  0 = none) (pick)
NULL_TIER = {10: ([500, 1500], 40, 0), 5: ([500, 2500], 30, 0), 2.5: ([500, 2500], 20, 5), 1.25: ([9000], 10, 6)}

# (name suffix, clouds, strength, weight) (pick:  weights)
MYKO_SMALL = ([500, 500], 0.10)
MYKO_LARGE = ([1000, 1000, 1000], 0.05)
LOW_CYTO = ([300, 200], 0.025)
W_HOTSPOT_SMALL, W_HOTSPOT_LARGE = 60, 40
W_REGION_SMALL, W_REGION_LARGE = 3, 2
W_LOW_CYTO = 25


class Out:
    def __init__(self):
        self.updates, self.dungeons, self.rooms, self.objects, self.groups, self.triggers, self.events = ([] for _ in range(7))
        self.nextDungeon = DUNGEON_BASE
        self.next = {k: ROW_BASE for k in ('room', 'object', 'group', 'trigger', 'event')}
        self.used = set()

    def id(self, kind):
        self.next[kind] += 1
        return self.next[kind]

    def site(self, name, clouds, strength, weight, minSec, maxSec, regionID, constellationID, guards=0):
        """one nebula:  the clouds (radius = units), the guards when the first ship arrives, done when mined out"""
        full = name + ' Nebula'
        rng = random.Random('%s %d %d' % (full, regionID, constellationID))
        if name in SEED and name not in self.used:
            # the seed's dungeon and room
            self.used.add(name)
            d, room, _ = SEED[name]
            self.updates.append((weight, minSec, maxSec, regionID, constellationID, strength, d))
        else:
            self.nextDungeon += 1
            d = self.nextDungeon
            nameID = SEED[name][2] if name in SEED else 0
            self.dungeons.append((d, full, 'kspace-gas-%d' % d, nameID, minSec, maxSec, weight, regionID, constellationID, strength))
            room = self.id('room')
            self.rooms.append((room, full, d))
        for units in clouds:
            self.obj(room, self.colour, ring(rng, 8000, 0.8), units)
        if guards:
            self.groups.append((self.id('group'), room, MODE_ENTERED, guards) + ring(rng, 20000, 0.3) + (8000,))
        t = self.id('trigger')
        self.triggers.append((t, room, T_ROOM_MINED_OUT))
        self.events.append((self.id('event'), t, 1, E_COMPLETE))

    def obj(self, room, typeID, pos, units):
        self.objects.append((self.id('object'), room, typeID) + pos + (units,))


def ring(rng, dist, jitter=0.25):
    """a point dist m from the warp-in, roughly level"""
    a = rng.uniform(0, 2 * math.pi)
    d = dist * rng.uniform(1 - jitter, 1 + jitter)
    return (round(d * math.cos(a)), round(rng.uniform(-0.1, 0.1) * d), round(d * math.sin(a)))


def sql_rows(rows, fmt):
    return ',\n'.join(fmt % r for r in rows)


def main(path):
    out = Out()
    for colour, myko, cyto, (empire, hot), (null, nullHot), small, large, lowCyto, nullSites in COLOURS:
        out.colour = myko
        # the hot-spot constellation (seed rows), then the rest of the region
        out.site(small, MYKO_SMALL[0], MYKO_SMALL[1], W_HOTSPOT_SMALL, -1, 1, empire, hot)
        out.site(large, MYKO_LARGE[0], MYKO_LARGE[1], W_HOTSPOT_LARGE, -1, 1, empire, hot)
        out.site(small, MYKO_SMALL[0], MYKO_SMALL[1], W_REGION_SMALL, -1, 1, empire, 0)
        out.site(large, MYKO_LARGE[0], MYKO_LARGE[1], W_REGION_LARGE, -1, 1, empire, 0)
        out.colour = cyto
        out.site(lowCyto, LOW_CYTO[0], LOW_CYTO[1], W_LOW_CYTO, 0.0, LOW_SEC_MAX, empire, 0)
        for i, (name, strength) in enumerate(nullSites):
            strength = strength or PATTERN[i]
            clouds, weight, guards = NULL_TIER[strength]
            out.site(name, clouds, strength / 100.0, weight, -1, LOW_SEC_MAX, null, nullHot, guards)
    missing = set(SEED) - out.used
    if missing:
        raise SystemExit('seed nebulae left over: %s' % ', '.join(sorted(missing)))

    def q(s):
        return "'" + s.replace("'", "''") + "'"

    L = []
    L.append('-- Known space gas sites:  the booster gas nebulae (Ladar signatures) as they were in Crucible.')
    L.append('--  utils/gas/gen_kspace_gas_sql.py writes this (the sources and picks are listed there)')
    L.append('--  - dunDungeons gets regionID and constellationID (the site only spawns there, 0 = anywhere) and sigStrength')
    L.append('--    (the signature\'s scan strength, 0 = the default)')
    L.append('--  - %d sites:  the 24 nebulae of the dungeon seed get their clouds, %d more are new' %
             (len(out.updates) + len(out.dungeons), len(out.dungeons)))
    L.append('-- +migrate Up')
    L.append('ALTER TABLE dunDungeons')
    L.append('  ADD COLUMN `regionID` int(10) unsigned NOT NULL DEFAULT 0,')
    L.append('  ADD COLUMN `constellationID` int(10) unsigned NOT NULL DEFAULT 0,')
    L.append('  ADD COLUMN `sigStrength` double NOT NULL DEFAULT 0;')
    for u in out.updates:
        L.append('UPDATE dunDungeons SET spawnWeight = %d, minSecurity = %g, maxSecurity = %g, regionID = %d, '
                 'constellationID = %d, sigStrength = %g WHERE dungeonID = %d;' % u)
    L.append('INSERT INTO dunDungeons (dungeonID, dungeonName, dungeonStatus, factionID, archetypeID, dungeonUUID, '
             'dungeonNameID, difficulty, minSecurity, maxSecurity, spawnWeight, lifetimeHours, regionID, constellationID, '
             'sigStrength) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), LADAR, q(r[2]), (str(r[3]) if r[3] else 'NULL')) + r[4:] for r in out.dungeons],
                      '(%d, %s, 1, 0, %d, %s, %s, 0, %g, %g, %d, 0, %d, %d, %g)') + ';')
    L.append('INSERT INTO dunRooms (roomID, roomName, dungeonID, arrivalX, arrivalY, arrivalZ) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2]) for r in out.rooms], '(%d, %s, %d, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunRoomObjects (objectID, roomID, typeID, groupID, x, y, z, yaw, pitch, roll, radius) VALUES')
    L.append(sql_rows(out.objects, '(%d, %d, %d, 0, %d, %d, %d, 0, 0, 0, %d)') + ';')
    L.append('INSERT INTO dunSpawnGroups (spawnGroupID, roomID, waveNumber, spawnMode, npcClassType, npcClassSub, '
             'factionID, x, y, z, spread, warpIn, isOverseer, lootGroupID, aggroMode, aggroRange, aggroLink) VALUES')
    L.append(sql_rows(out.groups, '(%d, %d, 1, %d, %d, 0, 0, %d, %d, %d, %d, 0, 0, 0, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunTriggers (triggerID, roomID, triggerType, spawnGroupID, objectID, value, counterID, repeatable) VALUES')
    L.append(sql_rows(out.triggers, '(%d, %d, %d, 0, 0, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunTriggerEvents (eventID, triggerID, sequence, eventType, delaySeconds, spawnGroupID, objectID, '
             'messageType, messageID, messageText, typeID, quantity, value, counterID) VALUES')
    L.append(sql_rows(out.events, "(%d, %d, %d, %d, 0, 0, 0, 0, 0, '', 0, 0, 0, 0)") + ';')
    L.append('')
    L.append('-- +migrate Down')
    for table, col in (('dunTriggerEvents', 'eventID'), ('dunTriggers', 'triggerID'), ('dunSpawnGroups', 'spawnGroupID'),
                       ('dunRoomObjects', 'objectID'), ('dunRooms', 'roomID')):
        L.append('DELETE FROM %s WHERE %s > %d AND %s < %d;' % (table, col, ROW_BASE, col, ROW_BASE + 1000000))
    L.append('DELETE FROM dunDungeons WHERE dungeonID > %d AND dungeonID < %d;' % (DUNGEON_BASE, DUNGEON_BASE + 1000000))
    L.append('UPDATE dunDungeons SET spawnWeight = 0, minSecurity = -1, maxSecurity = 1 WHERE dungeonID IN (%s);'
             % ', '.join(str(u[-1]) for u in out.updates))
    L.append('ALTER TABLE dunDungeons DROP COLUMN `regionID`, DROP COLUMN `constellationID`, DROP COLUMN `sigStrength`;')
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L) + '\n')
    print('%d seed sites, %d new, %d clouds, %d guard groups' %
          (len(out.updates), len(out.dungeons), len(out.objects), len(out.groups)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'sql/migrations/20261024160000-kspace-gas-sites.sql')
