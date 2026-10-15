#!/usr/bin/env python3
"""Generates sql/migrations/20261022100000-incursions.sql:  the incursion tables and the eleven sites.

Layouts follow each site's EVE University wiki page.  The wiki gives rosters, wave identifiers, spawn distances and
bearings, but never per-wave counts or gate distances; those are picks.  Positions are km from the room's centre (its
beacon); +z is ahead, +x right, +y up.  Ship and structure stats are the client's (type-attributes migrations), so
nothing here touches dgmTypeAttributes.

    python3 utils/incursions/gen_incursion_sql.py sql/migrations/20261022100000-incursions.sql
"""
import sys

# ---- roles and special waves (Incursion::Role, Incursion::Wave in IncursionMgr.h) ----
SHIP, FREE, OBJ, PICK, RESPAWN, STRUCT, GATE, HGATE, CONT, ROID = range(10)
MINING, SCRIPTED = 254, 255
VAN, ASSAULT, HQ, BOSS = 3, 2, 1, 1000001

# ---- types ----
GATE_T = 17831
EYSTUR, RENYN, SCHMAEEL, TAMA, NIARJA, ORKASHU, YOUL, COMMANDER = 2966, 2190, 2907, 2939, 2909, 3526, 3525, 3492
AUGA, ANTEM, ARNON, MARA, ROMI, RAA, UITRA, VYLADE, LIRSAUTTON = 2936, 2191, 2209, 2931, 2859, 3527, 2208, 2207, 2950
DELTOLE, INTAKI, OSTINGELE, OUTUNI, YULAI = 2933, 2855, 2932, 2845, 2935
CITIZENS = (3484, 3485, 3486, 3487, 3490)
KUNDALINI, UROBORUS = 2192, 3071
BASTION, TPHQ = 3805, 19955                      # objective NPCs: the Battletower and the Mobile Headquarters
# scenery
BATTLETOWER, COLONY, OUTPOST, CONTROL_LCO, RUINS, SNAKE, OBELISK = 2234, 2928, 3056, 3059, 3626, 10787, 32137
LOGI_STATION, ARRAY_LCO = 3510, 3509
LYAVITE = 28620
# containers
LCA, MTAC_FACTORY, CONTROL_TOWER, REFINERY, EVAC, NEXUS, RELAY, COMMS = 3261, 3498, 3495, 3502, 3503, 3506, 3508, 3491
CONTAINERS = (LCA, MTAC_FACTORY, CONTROL_TOWER, REFINERY, EVAC, NEXUS, RELAY, COMMS)

def o(room, wave, role, typeID, at=(0, 0, 0), qty=1, param=0, name=''):
    return (room, wave, role, typeID, qty, param, name, at)

def gate(room, dest, at, lock=0, name=''):
    """acceleration gate to room dest; lock = room that has to be cleared first"""
    return o(room, lock, GATE, GATE_T, at, param=dest, name=name)

def hidden_gate(room, dest, at, name=''):
    """acceleration gate that appears once its own room is clear"""
    return o(room, room, HGATE, GATE_T, at, param=dest, name=name)

def wave(room, n, at, *ships):
    """ships: (typeID, qty[, role[, at[, param]]]).  role defaults to SHIP, at to the wave's point"""
    rows = []
    for s in ships:
        t, qty = s[0], s[1]
        role = s[2] if len(s) > 2 else SHIP
        pos = s[3] if (len(s) > 3 and s[3] is not None) else at
        param = s[4] if len(s) > 4 else 0
        rows.append(o(room, n, role, t, pos, qty, param))
    return rows

ENTRY = gate(0, 1, (0, 0, 14.5))     # every beacon's gate: its surface is ~2 km from where pilots land

# siteTypeID: (name, scene, designFleet, isk, lp, influence%, secBand, objectiveTypeID, objects)
#   secBand: 0 any, 1 high-sec, 2 low/null-sec, 3 high or null-sec, 4 low-sec
SITES = {}

# --- Nation Commander Outpost: one pocket, 4 waves at 35-40 km.  wave 1 has a Romi, wave 2 none, wave 3 the
#     Commander, wave 4 frigates only.  no Mara or Deltole on this page ---
SITES[1] = ('Nation Commander Outpost', VAN, 10, 10395000, 1400, 5.0, 0, 0, [ENTRY,
    o(1, 0, STRUCT, OUTPOST, (-8, 0, 12)), o(1, 0, STRUCT, CONTROL_LCO, (10, -2, -8)),
    *wave(1, 1, (0, 0, 38), (ROMI, 2), (AUGA, 1), (EYSTUR, 3), (RENYN, 2), (SCHMAEEL, 1), (NIARJA, 1)),
    *wave(1, 2, (27, 3, 27), (AUGA, 2), (SCHMAEEL, 2), (TAMA, 2), (EYSTUR, 2), (RENYN, 2), (NIARJA, 1)),
    *wave(1, 3, (-27, -3, 27), (COMMANDER, 1), (TAMA, 3), (AUGA, 1), (EYSTUR, 2), (SCHMAEEL, 1)),
    *wave(1, 4, (10, 5, 36), (TAMA, 2), (RENYN, 2), (SCHMAEEL, 2), (NIARJA, 1), (EYSTUR, 3))])

# --- Nation Mining Colony: 3 waves at 70-75 km, the Mara in wave 1.  clearing wave 2 decloaks the Nation Ore
#     Refinery by the Asteroid Colony, right of the Battletower.  one Lyavite rock ~60 km out; mining it can
#     bring Orkashu, Raa and Youl with some regular frigates ---
SITES[2] = ('Nation Mining Colony', VAN, 10, 10395000, 1400, 5.0, 0, 0, [ENTRY,
    o(1, 0, STRUCT, BATTLETOWER, (0, 0, 30), name="Sansha's Battletower"),
    o(1, 0, STRUCT, COLONY, (14, 0, 30), name='Asteroid Colony'),
    o(1, 2, CONT, REFINERY, (14, 0, 26.5)),
    o(1, 0, ROID, LYAVITE, (-20, 5, 56), param=20000),
    *wave(1, 1, (0, 4, 72), (MARA, 1), (ROMI, 2), (AUGA, 1), (EYSTUR, 3), (SCHMAEEL, 2), (NIARJA, 1)),
    *wave(1, 2, (50, 0, 52), (ROMI, 2), (TAMA, 2), (SCHMAEEL, 2), (EYSTUR, 2), (NIARJA, 1)),
    *wave(1, 3, (-50, 3, 52), (AUGA, 2), (ROMI, 2), (TAMA, 2), (SCHMAEEL, 1), (NIARJA, 1), (EYSTUR, 2)),
    *wave(1, MINING, (-20, 5, 56), (ORKASHU, 2, FREE), (RAA, 1, FREE), (YOUL, 2, FREE), (EYSTUR, 1, FREE))])

# --- Override Transfer Array: the Remote Logistics Station ahead.  3 waves with one Deltole each: 70 km right of
#     the station, 70 km left, then 35-40 km from the beacon above it.  Eysturs respawn in front of the station.
#     Logistics Control Arrays ~30 km out, the first 90 degrees left, then clockwise ---
SITES[3] = ('Override Transfer Array', VAN, 10, 10395000, 1400, 5.0, 0, 0, [ENTRY,
    o(1, 0, STRUCT, LOGI_STATION, (0, 0, 40), name='Remote Logistics Station'),
    o(1, 0, CONT, LCA, (-30, 0, 0)), o(1, 0, CONT, LCA, (25, 0, 25)), o(1, 0, CONT, LCA, (40, 0, 45)),
    *wave(1, 1, (60, 0, 40), (DELTOLE, 1), (AUGA, 3), (TAMA, 2), (NIARJA, 1), (EYSTUR, 3, RESPAWN, (0, 0, 32), 60)),
    *wave(1, 2, (-60, 0, 40), (DELTOLE, 1), (AUGA, 2), (TAMA, 3), (NIARJA, 1)),
    *wave(1, 3, (0, 35, 15), (DELTOLE, 1), (AUGA, 3), (TAMA, 2), (NIARJA, 2))])

# --- Nation Commander Stronghold: waves placed around the Snake Shaped Asteroid and the L-shaped buildings.
#     1 Yulai snipers at 160 km behind the asteroid to the right, 2 Ostingele at 60 km between the buildings and the
#     asteroid, 3 Antem at 150 km behind it to the left, 4 Arnon at 20 km a little right, 5 one Citizen/Slave at
#     50 km right of the buildings, with Outuni and Vylade.  killing the Citizen/Slave ends it ---
SITES[4] = ('Nation Commander Stronghold', ASSAULT, 20, 18200000, 3500, 7.5, 0, 0, [ENTRY,
    o(1, 0, STRUCT, SNAKE, (0, 0, 70)),
    o(1, 0, STRUCT, OUTPOST, (32, 0, 22)), o(1, 0, STRUCT, OUTPOST, (38, 4, 30)),
    *wave(1, 1, (25, 0, 80), (YULAI, 3, SHIP, (40, 0, 155)), (MARA, 1, SHIP, (36, 4, 150)),
          (DELTOLE, 2), (AUGA, 2), (TAMA, 3), (RENYN, 2)),
    *wave(1, 2, (20, 0, 57), (OSTINGELE, 3), (DELTOLE, 2), (AUGA, 2), (MARA, 1), (SCHMAEEL, 2), (NIARJA, 1)),
    *wave(1, 3, (-25, 0, 80), (ANTEM, 3, SHIP, (-40, 0, 144.5)), (INTAKI, 1, SHIP, (-36, 4, 140)),
          (ROMI, 3), (TAMA, 3), (EYSTUR, 3)),
    *wave(1, 4, (6, 0, 19), (ARNON, 3), (DELTOLE, 2), (MARA, 2), (AUGA, 2), (RENYN, 2), (NIARJA, 1)),
    *wave(1, 5, (48, 0, 14), *[(c, 1, PICK) for c in CITIZENS], (OUTUNI, 2), (VYLADE, 1), (DELTOLE, 2), (MARA, 1))])

# --- Nation Consolidation Network: two gates on the beacon grid, cruisers and smaller to one pocket, battleships
#     to the other.  each pocket's Fleet Staging Area gate, 50+ km from the landing, opens when the other side's
#     pocket is clear.  the staging area has snipers out to 150 km ---
SITES[5] = ('Nation Consolidation Network', ASSAULT, 20, 18200000, 3500, 7.5, 0, 0, [
    gate(0, 1, (-13.5, 0, 6), name='Gate to Residential Areas (Cruisers)'),
    gate(0, 2, (13.5, 0, 6), name='Gate to Industrial Areas (Battleships)'),
    # cruiser side
    o(1, 0, STRUCT, CONTROL_LCO, (8, -2, 20)),
    gate(1, 3, (-10, 0, 62), lock=2, name='Fleet Staging Area'),
    *wave(1, 1, (0, 0, 40), (AUGA, 3), (ROMI, 2), (MARA, 1), (SCHMAEEL, 2), (RENYN, 2), (EYSTUR, 2), (NIARJA, 1)),
    *wave(1, 2, (25, 0, 32), (AUGA, 2), (ROMI, 2), (ANTEM, 2), (TAMA, 3), (SCHMAEEL, 2), (NIARJA, 1)),
    # battleship side
    o(2, 0, STRUCT, BATTLETOWER, (-10, 0, 25), name="Sansha's Battletower"),
    gate(2, 3, (10, 0, 62), lock=1, name='Fleet Staging Area'),
    *wave(2, 1, (0, 0, 40), (DELTOLE, 2), (OSTINGELE, 2), (VYLADE, 1), (MARA, 1), (TAMA, 2)),
    *wave(2, 2, (-25, 0, 32), (DELTOLE, 2), (OSTINGELE, 2), (YULAI, 1), (AUGA, 2), (SCHMAEEL, 2)),
    # the staging area
    o(3, 0, STRUCT, CONTROL_LCO, (0, -4, 22)),
    *wave(3, 1, (0, 0, 40), (YULAI, 3, SHIP, (0, 10, 140)), (ANTEM, 3, SHIP, (40, 0, 125)), (VYLADE, 1, SHIP, (-30, 0, 146)),
          (MARA, 2, SHIP, (-20, 0, 118)), (DELTOLE, 3), (OSTINGELE, 2), (AUGA, 2), (TAMA, 2), (NIARJA, 1)),
    *wave(3, 2, (30, 0, 35), (DELTOLE, 2), (OSTINGELE, 2), (ROMI, 2), (MARA, 1), (SCHMAEEL, 2), (NIARJA, 1), (EYSTUR, 2))])

# --- Overwhelmed Civilian Facility: everything spawns ahead, a little above.  1 at 35-40 km, no Antem; 2 Antems
#     at 75-80 km that don't hold up the next wave; 3 the one Outuni at 75-80 km, more at 95-100 km; 4 Antems again.
#     the Obelisk ahead becomes the Emergency Evacuation Freighter once most of wave 4 is dead ---
SITES[6] = ('Overwhelmed Civilian Facility', ASSAULT, 20, 18200000, 3500, 7.5, 0, 0, [ENTRY,
    o(1, 0, STRUCT, OBELISK, (0, 0, 20), name='Emergency Evacuation Freighter'),
    *wave(1, 1, (0, 3, 38), (AUGA, 2), (ROMI, 3), (RENYN, 3), (SCHMAEEL, 2), (DELTOLE, 1)),
    *wave(1, 2, (5, 2, 37), (DELTOLE, 2), (AUGA, 2), (SCHMAEEL, 3), (ROMI, 1), (ANTEM, 3, FREE, (0, 5, 78))),
    *wave(1, 3, (-5, 3, 77), (OUTUNI, 1), (DELTOLE, 2), (AUGA, 2), (ROMI, 3, SHIP, (0, 6, 98)), (RENYN, 3, SHIP, (0, 6, 98))),
    *wave(1, 4, (0, 2, 38), (DELTOLE, 2), (AUGA, 2), (ROMI, 2), (RENYN, 2), (SCHMAEEL, 2), (ANTEM, 3, SHIP, (0, 5, 78)))])

# --- Nation Rebirth Facility: 4 waves in one area by the indestructible facility; part of wave 4 spawns ~100 km
#     from the rest ---
SITES[7] = ('Nation Rebirth Facility', HQ, 40, 31500000, 7000, 10.0, 0, 0, [ENTRY,
    o(1, 0, STRUCT, RUINS, (0, 0, 25), name='Nation Rebirth Facility'),
    *wave(1, 1, (15, 0, 45), (DELTOLE, 4), (OSTINGELE, 3), (ROMI, 4), (MARA, 3), (EYSTUR, 4), (TAMA, 4)),
    *wave(1, 2, (20, 3, 50), (YULAI, 3), (OUTUNI, 2), (ANTEM, 4), (UITRA, 3), (MARA, 3), (SCHMAEEL, 4)),
    *wave(1, 3, (10, -3, 48), (DELTOLE, 5), (OSTINGELE, 3), (VYLADE, 2), (ROMI, 4), (MARA, 3), (TAMA, 4), (EYSTUR, 3)),
    *wave(1, 4, (15, 0, 45), (OUTUNI, 3), (DELTOLE, 4), (UITRA, 3), (SCHMAEEL, 3),
          (YULAI, 4, SHIP, (-70, 10, 90)), (VYLADE, 2, SHIP, (-70, 10, 90)), (MARA, 3, SHIP, (-70, 10, 90)))])

# --- True Creations Research Center: the Battletower with shield transfer arrays beside it, the CONCORD MTAC
#     Factory to their left and the Shield Transfer Control Tower further left.  a few Schmaeels to start, two of
#     them by the control tower and respawning.  the first uncloaked pilot brings Deltoles, then frigates, then
#     Outunis ~90 s later.  frigate and cruiser groups respawn fast, battleships slowly ---
SITES[8] = ('True Creations Research Center', HQ, 40, 31500000, 7000, 10.0, 0, 0, [ENTRY,
    o(1, 0, STRUCT, ARRAY_LCO, (-9.5, 0, 38), name='Shield Transfer Array'),
    o(1, 0, STRUCT, ARRAY_LCO, (9.5, 0, 42), name='Shield Transfer Array'),
    o(1, 0, CONT, MTAC_FACTORY, (-25, 0, 35)),
    o(1, 0, CONT, CONTROL_TOWER, (-45, 0, 30)),
    *wave(1, 1, (0, 0, 40), (BASTION, 1, OBJ), (SCHMAEEL, 3, FREE, (-5, 0, 30)), (SCHMAEEL, 2, RESPAWN, (-45, 0, 25), 60)),
    *wave(1, 2, (5, 0, 32), (DELTOLE, 4, RESPAWN, None, 900), (MARA, 2, RESPAWN, (0, 10, 60), 90),
          (INTAKI, 2, FREE, (-10, 5, 70))),
    *wave(1, 3, (0, 0, 28), (SCHMAEEL, 3, RESPAWN, (-20, 0, 25), 60), (NIARJA, 2, RESPAWN, (10, 0, 25), 60),
          (AUGA, 3, RESPAWN, None, 75), (ROMI, 3, RESPAWN, (15, 0, 35), 90)),
    *wave(1, 4, (0, 0, 35), (OUTUNI, 2, RESPAWN, None, 900), (OSTINGELE, 3, RESPAWN, (20, 0, 50), 900),
          (YULAI, 2, FREE, (0, 15, 150)))])
# the objective's name, as the wiki has it
SITES[8][8][:] = [r if r[3] != BASTION else r[:6] + ('Sansha Battletower',) + r[7:] for r in SITES[8][8]]

# --- True Power Provisional Headquarters: three pockets.  each pocket's gate onward appears once it's clear.  in
#     the last, each wave comes when the last Ostingele of the one before dies; when every ship is dead the
#     headquarters' Large Collidable Object becomes the destructible Mobile Headquarters.  snipers to 150 km ---
SITES[9] = ('True Power Provisional Headquarters', HQ, 40, 31500000, 7000, 10.0, 0, 0, [ENTRY,
    o(1, 0, STRUCT, CONTROL_LCO, (12, 0, 18)),
    hidden_gate(1, 2, (-20, 0, 25)),
    *wave(1, 1, (0, 0, 40), (AUGA, 4), (ROMI, 3), (MARA, 2), (ARNON, 2), (EYSTUR, 4), (TAMA, 4)),
    *wave(1, 2, (20, 0, 45), (DELTOLE, 4), (OSTINGELE, 3), (INTAKI, 2), (ANTEM, 3, SHIP, (-30, 5, 140)), (MARA, 2), (SCHMAEEL, 4)),
    o(2, 0, STRUCT, OUTPOST, (-10, 0, 15)),
    hidden_gate(2, 3, (20, 0, 25)),
    *wave(2, 1, (0, 0, 45), (YULAI, 3, SHIP, (0, 10, 140)), (OSTINGELE, 3), (OUTUNI, 2), (VYLADE, 1), (MARA, 3), (TAMA, 4)),
    *wave(2, 2, (-20, 0, 40), (DELTOLE, 4), (OSTINGELE, 3), (OUTUNI, 2), (ARNON, 3), (MARA, 2), (AUGA, 3)),
    o(3, 0, STRUCT, BATTLETOWER, (0, 0, 35), name='True Power Headquarters'),
    o(3, SCRIPTED, OBJ, TPHQ, (0, 0, 35), name='True Power Mobile Headquarters'),
    *wave(3, 1, (15, 0, 45), (OSTINGELE, 3), (DELTOLE, 3, FREE), (AUGA, 3, FREE), (MARA, 2, FREE), (TAMA, 3, FREE),
          (ANTEM, 2, FREE, (-30, 0, 140))),
    *wave(3, 2, (-15, 0, 45), (OSTINGELE, 3), (OUTUNI, 2, FREE), (INTAKI, 1, FREE), (ARNON, 2, FREE), (SCHMAEEL, 3, FREE),
          (YULAI, 2, FREE, (20, 10, 145))),
    *wave(3, 3, (0, 5, 50), (OSTINGELE, 4), (DELTOLE, 3, FREE), (VYLADE, 1, FREE), (MARA, 2, FREE), (EYSTUR, 3, FREE))])

# --- the motherships: on grid behind the gate, escorts and groups of ten bombers on a random timer (waves 2+ are
#     the reinforcement groups).  Logistics Control Array ~120 km ahead, Central Control Nexus 90 km ahead and
#     30 km left of the mothership, Sansha Control Relay 15 km behind the beacon ---
def boss(mothership):
    return [ENTRY,
        o(1, 0, CONT, LCA, (0, 0, 120)),
        o(1, 0, CONT, NEXUS, (-30, 0, 140)),
        o(1, 0, CONT, RELAY, (0, 0, -15)),
        *wave(1, 1, (0, 0, 50), (mothership, 1, OBJ), (DELTOLE, 3, FREE, (10, 0, 45)), (MARA, 3, FREE, (0, 5, 60)),
              (INTAKI, 2, FREE, (-10, 0, 55)), (ARNON, 2, FREE, (5, -5, 50)), (OUTUNI, 2, FREE, (0, 0, 40)),
              (AUGA, 3, FREE, (0, 0, 35)), (YULAI, 2, FREE, (0, 10, 120)), (LIRSAUTTON, 10, FREE, (0, 3, 30))),
        *wave(1, 2, (0, 3, 30), (LIRSAUTTON, 10, FREE)),
        *wave(1, 3, (15, 0, 40), (DELTOLE, 2, FREE), (MARA, 2, FREE), (AUGA, 2, FREE), (SCHMAEEL, 3, FREE), (TAMA, 2, FREE)),
        *wave(1, 4, (-15, 0, 40), (YULAI, 2, FREE, (-20, 10, 130)), (ANTEM, 2, FREE, (20, 10, 130)), (ARNON, 1, FREE), (NIARJA, 2, FREE)),
        *wave(1, 5, (0, -5, 35), (OUTUNI, 1, FREE), (INTAKI, 1, FREE), (ROMI, 3, FREE), (RENYN, 2, FREE), (EYSTUR, 2, FREE)),
        *wave(1, 6, (0, 3, 30), (LIRSAUTTON, 10, FREE))]

SITES[10] = ('The Kundalini Manifest', BOSS, 80, 63000000, 14000, 0.0, 3, KUNDALINI, boss(KUNDALINI))
SITES[11] = ('Uroborus', BOSS, 80, 63000000, 14000, 0.0, 4, UROBORUS, boss(UROBORUS))

# the objective npcs have no wreck in the shipped data
WRECKS = {BASTION: "Sansha's Nation Central Bastion", TPHQ: "'True Power' Assembly Security HQ"}
LARGE_WRECK, LARGE_WRECK_NAME = 26581, 'Sanshas Large Wreck'

def esc(s):
    return s.replace("'", "''")

def km(v):
    return round(v * 1000)

def main(path):
    out = []
    w = out.append
    w('-- Incursions:  the incursion state tables and the eleven sites (vanguard, assault, headquarters and the two')
    w('--  motherships), laid out from their EVE University wiki pages.  utils/incursions/gen_incursion_sql.py writes this')
    w('--  - incSiteTypes, incSiteObjects:  site data.  each object has a room (0 the beacon grid, 1+ the pockets behind')
    w('--    acceleration gates), a wave and a role (IncursionMgr.h)')
    w('--  - incIncursions, incSystems, incLPPool:  running incursions, their systems and the LP paid when the mothership dies')
    w('--  - wrecks for the two objective npcs, which have none in the shipped data')
    w('--  a database that ran the incursion migrations of the old branch keeps its state tables (the site data is replaced)')
    w('-- +migrate Up')
    w('''DROP TABLE IF EXISTS incSiteTypes;
CREATE TABLE incSiteTypes (
  siteTypeID SMALLINT UNSIGNED NOT NULL,
  siteName VARCHAR(100) NOT NULL,
  sceneType INT UNSIGNED NOT NULL COMMENT '3 vanguard, 2 assault, 1 headquarters, 1000001 mothership (EVE_Incursion.h)',
  designFleet TINYINT UNSIGNED NOT NULL COMMENT 'fleet size the site is built for',
  iskReward INT UNSIGNED NOT NULL COMMENT 'per pilot, full fleet, high-sec',
  lpReward INT UNSIGNED NOT NULL COMMENT 'CONCORD LP per pilot, full fleet, high-sec',
  influence FLOAT NOT NULL COMMENT 'percent of Sansha influence removed when completed',
  secBand TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 any system, 1 high-sec, 2 low/null-sec, 3 high or null-sec, 4 low-sec',
  objectiveTypeID INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'the npc whose death completes the site (motherships)',
  PRIMARY KEY (siteTypeID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
DROP TABLE IF EXISTS incSiteObjects;
CREATE TABLE incSiteObjects (
  objectID INT UNSIGNED NOT NULL AUTO_INCREMENT,
  siteTypeID SMALLINT UNSIGNED NOT NULL,
  room TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '0 = beacon grid, 1+ = pockets behind acceleration gates',
  wave TINYINT UNSIGNED NOT NULL COMMENT 'ships: wave number, 254 = mining spawn, 255 = scripted.  gates and containers: see Incursion::Role',
  role TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Incursion::Role in IncursionMgr.h',
  typeID INT UNSIGNED NOT NULL,
  quantity TINYINT UNSIGNED NOT NULL DEFAULT 1,
  param INT NOT NULL DEFAULT 0 COMMENT 'gate: destination room.  respawn: seconds.  asteroid: units',
  itemName VARCHAR(100) NOT NULL DEFAULT '' COMMENT 'empty = the type name',
  x DOUBLE NOT NULL DEFAULT 0 COMMENT 'offset from the room centre, metres',
  y DOUBLE NOT NULL DEFAULT 0,
  z DOUBLE NOT NULL DEFAULT 0,
  PRIMARY KEY (objectID),
  KEY siteTypeID (siteTypeID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
CREATE TABLE IF NOT EXISTS incIncursions (
  incursionID INT UNSIGNED NOT NULL AUTO_INCREMENT,
  constellationID INT UNSIGNED NOT NULL,
  stagingSystemID INT UNSIGNED NOT NULL,
  state TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 established, 1 mobilizing, 2 withdrawing',
  influence DOUBLE NOT NULL DEFAULT 1 COMMENT 'Sansha influence, 1 = full, 0 = defeated',
  startTime INT UNSIGNED NOT NULL COMMENT 'unix time',
  stateTime INT UNSIGNED NOT NULL COMMENT 'unix time the current state began',
  lastProgress INT UNSIGNED NOT NULL COMMENT 'unix time a site was last completed',
  hasBoss TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'the mothership site is up',
  hqRun TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'a headquarters site was completed at zero influence',
  PRIMARY KEY (incursionID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
ALTER TABLE incIncursions
  ADD COLUMN IF NOT EXISTS hasBoss TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'the mothership site is up',
  ADD COLUMN IF NOT EXISTS hqRun TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'a headquarters site was completed at zero influence';
CREATE TABLE IF NOT EXISTS incSystems (
  incursionID INT UNSIGNED NOT NULL,
  systemID INT UNSIGNED NOT NULL,
  sceneType TINYINT UNSIGNED NOT NULL COMMENT '3 vanguard, 2 assault, 1 headquarters',
  PRIMARY KEY (incursionID, systemID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
CREATE TABLE IF NOT EXISTS incLPPool (
  incursionID INT UNSIGNED NOT NULL,
  characterID INT UNSIGNED NOT NULL,
  lp INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'CONCORD LP earned in this incursion, paid when the mothership dies',
  PRIMARY KEY (incursionID, characterID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
''')
    w('INSERT INTO incSiteTypes (siteTypeID, siteName, sceneType, designFleet, iskReward, lpReward, influence, secBand, objectiveTypeID) VALUES')
    w(',\n'.join("(%d, '%s', %d, %d, %d, %d, %s, %d, %d)" % ((k, esc(v[0])) + v[1:8]) for k, v in sorted(SITES.items())) + ';')
    w('INSERT INTO incSiteObjects (siteTypeID, room, wave, role, typeID, quantity, param, itemName, x, y, z) VALUES')
    rows = []
    for k, v in sorted(SITES.items()):
        for (room, wv, role, t, qty, param, name, at) in v[8]:
            rows.append("(%d, %d, %d, %d, %d, %d, %d, '%s', %d, %d, %d)" % (k, room, wv, role, t, qty, param, esc(name), km(at[0]), km(at[1]), km(at[2])))
    w(',\n'.join(rows) + ';')
    w('')
    w('DELETE FROM invTypesToWrecks WHERE typeID IN (%s);' % ','.join(map(str, sorted(WRECKS))))
    w('INSERT INTO invTypesToWrecks (typeID, typeName, wreckTypeID, wreckName) VALUES')
    w(',\n'.join("(%d, '%s', %d, '%s')" % (t, esc(n), LARGE_WRECK, LARGE_WRECK_NAME) for t, n in sorted(WRECKS.items())) + ';')
    w('')
    w('-- +migrate Down')
    w('DELETE FROM invTypesToWrecks WHERE typeID IN (%s);' % ','.join(map(str, sorted(WRECKS))))
    w('DROP TABLE IF EXISTS incLPPool;')
    w('DROP TABLE IF EXISTS incSystems;')
    w('DROP TABLE IF EXISTS incIncursions;')
    w('DROP TABLE IF EXISTS incSiteObjects;')
    w('DROP TABLE IF EXISTS incSiteTypes;')
    open(path, 'w', newline='\n').write('\n'.join(out) + '\n')

    for k, v in sorted(SITES.items()):
        rooms = sorted(set(r[0] for r in v[8]))
        ships = sum(r[4] for r in v[8] if r[2] in (SHIP, FREE, OBJ) and r[1] < MINING and (v[1] != BOSS or r[1] == 1))
        print('%2d %-36s rooms %s, %3d design ships, %d objects' % (k, v[0], rooms, ships, len(v[8])), file=sys.stderr)

if __name__ == '__main__':
    main(sys.argv[1])
