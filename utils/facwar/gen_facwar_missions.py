#!/usr/bin/env python3
"""Generates sql/migrations/20261206100000-facwar-missions.sql:  the militia missions of Crucible's factional warfare.

What's from sources and what's a pick:
  - militia agents (agent type 9, the Militia division of the four militia corporations) give their militia's pilots
    encounter missions of levels 1 to 4 against the militia they fight, in a war zone system it holds (Empyrean Age dev
    blogs, 2008).  the enemy's ships are its militia line from the client data (the Mission <empire> groups:  Imperial,
    State, Federation and Republic frigates up to battleships, plain and Templar/Shukuro/Praktor/Tribal), as in the
    complexes (gen_facwar_sql.py)
  - they pay more loyalty points than other agents' missions of the level (the militias' stores are where the faction
    navy gear is).  three times the level's base LP is a pick, and a strike group half as much again
  - two missions a level against each militia (a patrol, and a strike group of the level's every wave), their names,
    briefings, waves and where the ships sit are picks.  one pocket, no gate.  the pocket is cleared to finish

    python3 utils/facwar/gen_facwar_missions.py sql/migrations/20261206100000-facwar-missions.sql
"""
import math
import random
import sys

from gen_facwar_sql import SHIPS, AMARR, CALDARI, GALLENTE, MINMATAR, SPAWN_CREATED, SPAWN_TRIGGER, T_EXPLODING, E_SPAWN_GUARDS, ring

ARCH_MISSION = 1        # the agent missions' archetype (F12 mission content)
OBJ_KILL_ALL = 1
TYPE_ENCOUNTER = 2

# id ranges:  above the complexes' (150000000 / 15000000), and the content above the F12 missions'
DUNGEON_BASE, ROW_BASE, CONTENT_BASE = 151000000, 16000000, 15100000

# the battlecruiser and battleship lines (Mission <empire> Battlecruiser/Battleship):  plain, then the elite line
BIG = {
    AMARR: {'BC': [24956, 24957, 24958, 24959], 'BC+': [24960, 24962],
            'BS': [24976, 24977, 24978, 24979, 24982, 24983], 'BS+': [24985, 24988, 24991, 24992]},
    CALDARI: {'BC': [25063, 25064, 25065, 25066], 'BC+': [25067, 25069],
              'BS': [25083, 25084, 25085, 25086, 25089], 'BS+': [25090, 25092, 25094, 25097, 25098]},
    GALLENTE: {'BC': [25167, 25168, 25169], 'BC+': [25170, 25171, 25173],
               'BS': [25187, 25188, 25189, 25190, 25193], 'BS+': [25194, 25196, 25198, 25201, 25202]},
    MINMATAR: {'BC': [24843, 24847, 24851], 'BC+': [24848, 24855, 24857],
               'BS': [24867, 24868, 24869, 24870, 24875], 'BS+': [24876, 24883, 24884, 24886, 24888]},
}

MILITIA = {AMARR: '24th Imperial Crusade', CALDARI: 'State Protectorate', GALLENTE: 'Federal Defense Union',
           MINMATAR: 'Tribal Liberation Force'}
DAMAGE = {AMARR: 'EM and thermal', CALDARI: 'kinetic and thermal', GALLENTE: 'kinetic and thermal',
          MINMATAR: 'explosive and kinetic'}

# the level's waves:  (line, count min, count max) per wave.  the next wave comes when the last one is dead
WAVES = {
    1: [[('F', 3, 4)], [('F', 2, 3), ('D', 0, 1)]],
    2: [[('F', 2, 3), ('D', 1, 2)], [('F+', 1, 2), ('D', 1, 1), ('C', 1, 1)]],
    3: [[('D', 2, 2), ('C', 2, 2)], [('F+', 2, 2), ('C', 2, 3)], [('D+', 1, 1), ('C+', 2, 2)]],
    4: [[('C', 3, 3), ('BC', 1, 2)], [('C+', 2, 2), ('BC', 2, 2)], [('BC+', 1, 2), ('BS', 2, 2)], [('BS', 1, 1), ('BS+', 1, 2)]],
}
BASE_LP = {1: 200, 2: 600, 3: 1500, 4: 3500}    # the server's default base LP by level (EVEServerConfig missions.BaseLP)
LP_TIMES = 3

KINDS = [
    ('patrol', 'Intercept the %s Patrol', 1.0,
     'A %s patrol is moving through one of the war zone systems they hold.  Warp in and destroy every ship of it.  '
     'Expect %s damage.'),
    ('strike', 'Break the %s Strike Group', 1.5,
     '%s ships are gathering in a system they hold for a strike against our lines.  Break them before they move:  '
     'destroy every ship, wave after wave.  Expect %s damage.'),
]


class Out:
    def __init__(self):
        self.missions, self.dungeons, self.rooms, self.groups, self.types = ([] for _ in range(5))
        self.triggers, self.events = [], []
        self.nextDungeon, self.nextContent = DUNGEON_BASE, CONTENT_BASE
        self.next = {k: ROW_BASE for k in ('room', 'group', 'trigger', 'event')}

    def id(self, kind):
        self.next[kind] += 1
        return self.next[kind]


def lines(faction):
    out = dict(SHIPS[faction])
    out.update(BIG[faction])
    return out


def build(out, enemy, level, kind):
    key, title, scale, brief = kind
    name = title % MILITIA[enemy]
    rng = random.Random('%s L%d' % (name, level))
    out.nextDungeon += 1
    d = out.nextDungeon
    out.nextContent += 1
    waves = WAVES[level] if key == 'strike' else WAVES[level][:math.ceil(len(WAVES[level]) / 2)]
    page = 'facwar-%s-%d-L%d' % (key, enemy, level)
    out.dungeons.append((d, '%s (L%d, %s)' % (name, level, MILITIA[enemy]), enemy, page, level))
    out.missions.append((out.nextContent, page, name, brief % (MILITIA[enemy], DAMAGE[enemy]), level, enemy, d,
                         BASE_LP[level] * LP_TIMES, scale))

    room = out.id('room')
    out.rooms.append((room, 'Single Pocket', d))
    ships = lines(enemy)
    groups = []
    for w, wave in enumerate(waves):
        entries = {}
        for line, lo, hi in wave:
            for _ in range(rng.randint(lo, hi)):
                t = rng.choice(ships[line])
                entries[t] = entries.get(t, 0) + 1
        if not entries:
            continue
        g = out.id('group')
        out.groups.append((g, room, w + 1, SPAWN_CREATED if w == 0 else SPAWN_TRIGGER, ring(rng, 20000 + 5000 * level, 0.3), enemy))
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
    for enemy in (AMARR, CALDARI, GALLENTE, MINMATAR):
        for level in (1, 2, 3, 4):
            for kind in KINDS:
                build(out, enemy, level, kind)

    def q(s):
        return "'" + s.replace("'", "''") + "'"

    L = []
    L.append('-- militia missions:  two missions a level, 1 to 4, against each militia, given by the militia agents of')
    L.append('--  the militias at war with it (MissionDataMgr::IsMilitiaContent(), the "facwar-" page keys) in a war zone system it')
    L.append('--  holds.  utils/facwar/gen_facwar_missions.py writes this (the sources and picks are listed there)')
    L.append('--  - one pocket each, archetype 1 (the agent missions\'), the enemy militia\'s ships wave after wave.  cleared to finish')
    L.append('-- +migrate Up')
    L.append('INSERT INTO dunDungeons (dungeonID, dungeonName, dungeonStatus, factionID, archetypeID, dungeonUUID, '
             'dungeonNameID, difficulty, minSecurity, maxSecurity, spawnWeight, lifetimeHours) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2], ARCH_MISSION, q(r[3]), r[4]) for r in out.dungeons],
                      '(%d, %s, 1, %d, %d, %s, 0, %d, -1, 1, 0, 0)') + ';')
    L.append('INSERT INTO dunRooms (roomID, roomName, dungeonID, arrivalX, arrivalY, arrivalZ) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2]) for r in out.rooms], '(%d, %s, %d, 0, 0, 0)') + ';')
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
    L.append('INSERT INTO msnMissions (contentID, pageKey, snapshot, name, nameID, briefingID, briefingText, level, typeID, '
             'important, storyline, enemyFactionID, agentRaceMask, objectiveType, objectiveTypeID, objectiveQty, dungeonID, '
             'spaceType, restrictLevel, rewardISK, bonusISK, bonusTime, rewardLP, rewardScale, rewardItemTypeID, rewardItemQty, '
             'collateral, remoteOfferable, remoteCompletable, enabled) VALUES')
    L.append(sql_rows([(c, q(k), q(n), q(b), lv, TYPE_ENCOUNTER, e, OBJ_KILL_ALL, d, lp, sc)
                       for c, k, n, b, lv, e, d, lp, sc in out.missions],
                      "(%d, %s, '20111129000000', %s, 0, 0, %s, %d, %d, 0, 0, %d, 0, %d, 0, 0, %d, 1, 0, 0, 0, 0, %d, %.1f, 0, 0, 0, 1, 1, 1)") + ';')
    L.append('')
    L.append('-- +migrate Down')
    L.append('DELETE FROM msnMissions WHERE contentID > %d AND contentID < %d;' % (CONTENT_BASE, CONTENT_BASE + 100000))
    for table, col in (('dunTriggerEvents', 'eventID'), ('dunTriggers', 'triggerID'), ('dunSpawnGroupTypes', 'spawnGroupID'),
                       ('dunSpawnGroups', 'spawnGroupID'), ('dunRooms', 'roomID')):
        L.append('DELETE FROM %s WHERE %s > %d AND %s < %d;' % (table, col, ROW_BASE, col, ROW_BASE + 1000000))
    L.append('DELETE FROM dunDungeons WHERE dungeonID > %d AND dungeonID < %d;' % (DUNGEON_BASE, DUNGEON_BASE + 1000000))
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L) + '\n')
    print('%d missions, %d groups, %d triggers' % (len(out.missions), len(out.groups), len(out.triggers)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'sql/migrations/20261206100000-facwar-missions.sql')
