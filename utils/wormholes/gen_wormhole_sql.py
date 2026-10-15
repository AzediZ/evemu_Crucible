#!/usr/bin/env python3
"""Writes sql/migrations/20261022110000-wormholes.sql:  the statics and phenomenon beacon of every w-space system
 (mapWormholeSystems) and the live wormhole table (sysWormholes).

 wspace_systems_crucible.csv:  one row per w-space system.  class and phenomenon from the Crucible static data, the
  statics from exodus4d/pathfinder (MIT, see LICENSE-pathfinder.txt), which lists today's statics.  Those match
  Crucible except for class 4 systems:  Hyperion (2014) gave each C4 a second static, and in Crucible a C4 had one.
  C4_ORIGINAL below is the class the original static leads to, per constellation, from the Collapsed Behind list of
  the Hyperion test server (2014-08, "first static / second *new* static").  Its pairs match the CSV for all 79 C4
  constellations.

 usage:  gen_wormhole_sql.py [csv] [out]
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'wspace_systems_crucible.csv')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', '..', 'sql', 'migrations', '20261022110000-wormholes.sql')

# constellation number (constellationID - 21000000) -> class the original static leads to
C4_ORIGINAL = {
    153: 'C2', 154: 'C2', 155: 'C2', 156: 'C2', 157: 'C2', 158: 'C2', 159: 'C2', 160: 'C2', 161: 'C2',
    162: 'C6', 163: 'C6', 164: 'C6', 165: 'C6',
    166: 'C1', 167: 'C1', 168: 'C1', 169: 'C1', 170: 'C1', 171: 'C1', 172: 'C1',
    173: 'C4', 174: 'C4', 175: 'C4', 176: 'C4', 177: 'C4', 178: 'C4', 179: 'C4', 180: 'C4', 181: 'C4',
    182: 'C4', 183: 'C4', 184: 'C4', 185: 'C4', 186: 'C4',
    187: 'C4', 188: 'C4', 189: 'C4', 190: 'C4', 191: 'C4', 192: 'C4', 193: 'C4', 194: 'C4',
    195: 'C3', 196: 'C3', 197: 'C3', 198: 'C3', 199: 'C3', 200: 'C3', 201: 'C3', 202: 'C3', 203: 'C3',
    204: 'C3', 205: 'C3', 206: 'C3', 207: 'C3', 208: 'C3', 209: 'C3', 210: 'C3', 211: 'C3', 212: 'C3',
    213: 'C5', 214: 'C5', 215: 'C5', 216: 'C5', 217: 'C5', 218: 'C5', 219: 'C5', 220: 'C5', 221: 'C5',
    222: 'C5', 223: 'C5', 224: 'C5', 225: 'C5', 226: 'C5',
    227: 'C3', 228: 'C3', 229: 'C3', 230: 'C3', 231: 'C3',
}


def main():
    rows = []
    for r in csv.DictReader(open(CSV, newline='')):
        statics = [(int(r[k + 'TypeID']), r[k + 'Target']) for k in ('static1', 'static2') if r[k + 'TypeID']]
        if r['wormholeClass'] == '4':
            want = C4_ORIGINAL[int(r['constellationID']) - 21000000]
            statics = [s for s in statics if s[1] == want]
            if len(statics) != 1:
                sys.exit('C4 system %s: no single static to %s' % (r['solarSystemID'], want))
        types = [s[0] for s in statics] + [0, 0]
        beacon = int(r['effectBeaconTypeID'] or 0)
        rows.append('(%d, %d, %d, %d, %d)' % (int(r['solarSystemID']), int(r['wormholeClass']), types[0], types[1], beacon))

    with open(OUT, 'w', newline='\n') as f:
        w = lambda s='': f.write(s + '\n')
        w('-- Wormholes:  the statics and phenomenon of each w-space system, and the wormholes that are open.')
        w('--  utils/wormholes/gen_wormhole_sql.py writes this')
        w('--  - mapWormholeSystems:  the 2,498 w-space systems.  statics as they were in Crucible:  one per system, two in')
        w('--    class 2 (k-space and w-space).  class 4 systems had one static until Hyperion (2014) added the second.')
        w('--    effectBeaconTypeID is the system effect beacon of its phenomenon (black hole, magnetar and so on), 0 for none')
        w('--  - sysWormholes:  one row per open wormhole (WormholeMgr).  both ends share its mass and lifetime.  the K162')
        w('--    (the far end) is made when the first ship warps to the wormhole')
        w('-- +migrate Up')
        w('DROP TABLE IF EXISTS mapWormholeSystems;')
        w('CREATE TABLE mapWormholeSystems (')
        w('  solarSystemID INT UNSIGNED NOT NULL,')
        w('  wormholeClassID TINYINT UNSIGNED NOT NULL,')
        w('  static1 INT UNSIGNED NOT NULL DEFAULT 0 COMMENT \'wormhole typeID always open here\',')
        w('  static2 INT UNSIGNED NOT NULL DEFAULT 0,')
        w('  effectBeaconTypeID INT UNSIGNED NOT NULL DEFAULT 0 COMMENT \'system effect beacon (group 920), 0 = none\',')
        w('  PRIMARY KEY (solarSystemID)')
        w(') ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;')
        for i in range(0, len(rows), 500):
            w('INSERT INTO mapWormholeSystems (solarSystemID, wormholeClassID, static1, static2, effectBeaconTypeID) VALUES')
            w(',\n'.join(rows[i:i + 500]) + ';')
        w('CREATE TABLE IF NOT EXISTS sysWormholes (')
        w('  wormholeID INT UNSIGNED NOT NULL COMMENT \'itemID of the typed end\',')
        w('  typeID INT UNSIGNED NOT NULL,')
        w('  systemID INT UNSIGNED NOT NULL,')
        w('  isStatic TINYINT UNSIGNED NOT NULL DEFAULT 0,')
        w('  exitID INT UNSIGNED NOT NULL DEFAULT 0 COMMENT \'itemID of the K162, 0 until a ship warps to the wormhole\',')
        w('  exitSystemID INT UNSIGNED NOT NULL DEFAULT 0,')
        w('  massTotal BIGINT UNSIGNED NOT NULL COMMENT \'kg\',')
        w('  massUsed BIGINT UNSIGNED NOT NULL DEFAULT 0,')
        w('  jumps INT UNSIGNED NOT NULL DEFAULT 0,')
        w('  createTime BIGINT NOT NULL COMMENT \'filetime\',')
        w('  expiryTime BIGINT NOT NULL,')
        w('  PRIMARY KEY (wormholeID),')
        w('  KEY systemID (systemID),')
        w('  KEY exitSystemID (exitSystemID)')
        w(') ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;')
        w('-- +migrate Down')
        w('DROP TABLE IF EXISTS sysWormholes;')
        w('DROP TABLE IF EXISTS mapWormholeSystems;')
    print('wrote %s (%d systems)' % (OUT, len(rows)))


if __name__ == '__main__':
    main()
