#!/usr/bin/env python3
"""Generates sql/migrations/20261022120000-sleeper-sites.sql:  the Sleeper sites of w-space as they were in Crucible.

Sources are in /reports/wormholes-research.md (sections 4.4-4.6):  the eve-survival.org pages for each site, the newest
revision dated before 2013.  What the pages don't give is a pick, and is marked (pick) below.

  - combat anomalies, radar and magnetometric signatures:  4 + 2 + 2 per class, 47 sites.  The Ruins of Enclave
    Cohort 27 (C2) has no data anywhere and is left out
  - gas (ladar) and ore (gravimetric) signatures:  Perimeter sites in C1-C2, Frontier in C3-C4, Core in C5-C6 (pick,
    by the Sleeper families).  one dungeon per class they spawn in

Each dungeon is one room, faction Sleepers (500023), difficulty = the w-space class (AnomalyMgr picks by it).

Waves:  the ships marked TRIG are a group of their own.  when that group is dead the next wave spawns.  a wave with
no TRIG spawns the next when all of it is dead.  the last wave (and the C4-C6 radar/mag escalation after it) ends it.
Sentries stand from the start and are not part of any wave.

Completion:  counter 1 counts dead groups (and, in radar/mag sites, opened cans).  the site completes when every group
is dead and every can is open, so nothing is despawned while it still has ships or loot.  gas and ore sites complete
when they're mined out.

    python3 utils/wormholes/gen_sleeper_sites_sql.py sql/migrations/20261022120000-sleeper-sites.sql
"""
import math
import random
import re
import sys

FACTION_SLEEPERS = 500023
GRAV, MAG, RADAR, LADAR, ANOM = 2, 3, 4, 5, 7

# dunTriggers.triggerType / dunTriggerEvents.eventType (EVE_Dungeon.h)
T_ATTACKED, T_EXPLODING, T_ROOM_MINED_OUT, T_HACK_OK, T_ARCH_OK, T_COUNTER_GE = 1, 3, 9, 11, 15, 36
E_SPAWN_GUARDS, E_SPAWN_OBJECT, E_COMPLETE, E_COUNTER_ADD = 2, 3, 11, 32
MODE_CREATED, MODE_ENTERED, MODE_TRIGGER = 0, 1, 2
COUNTER = 1

# id ranges (above everything the seed data uses)
DUNGEON_BASE, ROW_BASE = 120000000, 12000000

SHIPS = {
    # Perimeter
    'Emergent Patroller': 30209, 'Emergent Watchman': 30210, 'Emergent Escort': 30211,
    'Awakened Patroller': 30200, 'Awakened Watchman': 30201, 'Awakened Escort': 30202,
    'Sleepless Patroller': 30188, 'Sleepless Watchman': 30189, 'Sleepless Escort': 30190, 'Sleepless Outguard': 30191,
    # Frontier
    'Emergent Defender': 30212, 'Emergent Upholder': 30213, 'Emergent Preserver': 30214,
    'Awakened Defender': 30203, 'Awakened Upholder': 30204, 'Awakened Preserver': 30205,
    'Sleepless Defender': 30192, 'Sleepless Upholder': 30193, 'Sleepless Preserver': 30194, 'Sleepless Safeguard': 30195,
    # Core
    'Emergent Sentinel': 30215, 'Emergent Keeper': 30216, 'Emergent Warden': 30217,
    'Awakened Sentinel': 30206, 'Awakened Keeper': 30207, 'Awakened Warden': 30208,
    'Sleepless Sentinel': 30196, 'Sleepless Keeper': 30197, 'Sleepless Warden': 30198, 'Sleepless Guardian': 30199,
}
SENTRIES = {'Sirius': 30460, 'Argos': 30461, 'Orthrus': 30462}

# site containers by class (CLIENT group 306;  C1 radar and C1-C3 mag by the same order, pick)
RADAR_CAN = {1: 30516, 2: 30517, 3: 30518, 4: 30519, 5: 30520, 6: 30521}
MAG_CAN = {1: 30522, 2: 30523, 3: 30524, 4: 30525, 5: 30526, 6: 30527}
TALOCAN_BS, TALOCAN_CRUISER, TALOCAN_FRIGATE = 30528, 30529, 30530

# scenery (group 226), two per site (pick:  the real layouts aren't recorded)
DECO = {
    (ANOM, 'Perimeter'): (30299, 30302), (ANOM, 'Frontier'): (30901, 30300), (ANOM, 'Core'): (30293, 30513),
    (RADAR, None): (30301, 30905), (MAG, None): (30502, 30503),
    (LADAR, None): (30506,), (GRAV, None): (30298,),
}

def family(cls):
    return 'Perimeter' if cls <= 2 else ('Frontier' if cls <= 4 else 'Core')


# ---- the sites (research 4.4).  waves are 'N Ship [TRIG]' lists;  optional ("possibly", "sometimes") ships are left
#  out and ranges take the low end.  flags:  anykill = any kill spawns the next wave,  open = the first wave comes when
#  the first can is opened,  far = the 3rd wave spawns 60-80 km out.  TRIGHIT = the wave comes when it is first hit
def S(name, cls, kind, waves, cans=0, esc=None, wreck=0, flags=()):
    return dict(name=name, cls=cls, kind=kind, waves=waves, cans=cans, esc=esc, wreck=wreck, flags=set(flags))

SITES = [
    # ---- C1 (Sirius) ----
    S('Perimeter Ambush Point', 1, ANOM, [
        '2 Sirius, 1 Awakened Escort TRIG, 1 Emergent Escort',
        '2 Awakened Escort TRIG, 3 Emergent Patroller',
        '2 Awakened Patroller, 2 Awakened Escort']),
    S('Perimeter Camp', 1, ANOM, [
        '2 Sirius, 2 Emergent Watchman TRIG, 4 Emergent Patroller',
        '3 Emergent Watchman, 3 Emergent Patroller, 1 Awakened Patroller TRIG',
        '2 Emergent Patroller, 2 Awakened Patroller, 1 Awakened Watchman']),
    S('Phase Catalyst Node', 1, ANOM, [
        '4 Sirius, 2 Emergent Escort TRIG, 2 Emergent Watchman',
        '2 Awakened Patroller, 1 Awakened Escort TRIG',
        '6 Emergent Patroller, 3 Emergent Watchman']),
    S('The Line', 1, ANOM, [
        '3 Emergent Watchman, 4 Emergent Patroller, 1 Awakened Watchman TRIG',
        '3 Emergent Escort, 2 Awakened Patroller TRIG',
        '5 Awakened Watchman']),
    S('Unsecured Perimeter Amplifier', 1, RADAR, [
        '2 Awakened Patroller TRIG',
        '3 Emergent Watchman, 3 Awakened Escort TRIG',
        '2 Awakened Escort, 2 Awakened Watchman'], cans=4),
    S('Unsecured Perimeter Information Center', 1, RADAR, [
        '3 Emergent Patroller, 1 Emergent Watchman, 1 Awakened Patroller TRIG',
        '1 Emergent Escort, 3 Awakened Patroller TRIG',
        '2 Awakened Patroller, 3 Awakened Watchman, 2 Emergent Patroller'], cans=3),
    S('Forgotten Perimeter Coronation Platform', 1, MAG, [
        '2 Awakened Patroller, 1 Emergent Escort, 2 Emergent Patroller TRIG',
        '3 Awakened Patroller TRIG, 1 Emergent Escort',
        '3 Awakened Watchman, 2 Awakened Patroller, 3 Emergent Patroller'], cans=4),
    S('Forgotten Perimeter Power Array', 1, MAG, [
        '2 Awakened Patroller TRIG',
        '2 Awakened Escort TRIG, 3 Emergent Watchman',
        '2 Awakened Escort, 2 Awakened Watchman'], cans=4, wreck=TALOCAN_FRIGATE),

    # ---- C2 (Sirius) ----
    S('Perimeter Checkpoint', 2, ANOM, [
        '2 Sirius, 2 Awakened Patroller TRIG, 2 Emergent Patroller',
        '2 Awakened Patroller TRIG, 2 Awakened Escort',
        '1 Sleepless Patroller, 3 Emergent Patroller']),
    S('Perimeter Hangar', 2, ANOM, [
        '3 Awakened Patroller TRIG, 2 Emergent Watchman',
        '1 Sleepless Watchman, 2 Awakened Escort TRIG',
        '1 Sleepless Escort, 2 Awakened Escort'], flags=('far',)),
    S('Sleeper Data Sanctuary', 2, ANOM, [
        '3 Sirius, 2 Awakened Escort TRIG, 3 Emergent Patroller',
        '1 Sleepless Escort TRIG, 4 Emergent Escort',
        '1 Sleepless Escort TRIG',
        '1 Sleepless Outguard']),
    S('Unsecured Perimeter Comms Relay', 2, RADAR, [
        '3 Awakened Patroller, 2 Awakened Watchman TRIG',
        '1 Sleepless Patroller TRIG, 2 Emergent Watchman',
        '1 Sleepless Watchman, 1 Sleepless Escort, 2 Emergent Patroller'], cans=8),
    S('Unsecured Perimeter Transponder Farm', 2, RADAR, [
        '3 Awakened Patroller, 1 Emergent Patroller, 2 Emergent Escort TRIG',
        '1 Awakened Watchman, 4 Awakened Patroller TRIG, 2 Emergent Escort',
        '1 Sleepless Patroller, 4 Emergent Patroller, 4 Emergent Escort TRIG',
        '5 Awakened Patroller, 3 Awakened Escort, 4 Emergent Escort'], cans=5),
    S('Forgotten Perimeter Gateway', 2, MAG, [
        '4 Awakened Patroller, 1 Awakened Watchman TRIG',
        '1 Sleepless Patroller TRIG, 2 Emergent Watchman',
        '1 Sleepless Escort, 1 Sleepless Watchman, 4 Emergent Patroller'], cans=8),
    S('Forgotten Perimeter Habitation Coils', 2, MAG, [
        '3 Awakened Patroller, 2 Emergent Escort TRIG, 2 Emergent Patroller',
        '4 Awakened Patroller TRIG, 4 Emergent Escort',
        '1 Sleepless Patroller TRIG, 4 Emergent Escort, 4 Emergent Patroller',
        '1 Sleepless Watchman, 1 Awakened Patroller, 2 Awakened Escort, 3 Emergent Escort'], cans=5),

    # ---- C3 (Argos) ----
    S('Fortification Frontier Stronghold', 3, ANOM, [
        '2 Awakened Defender TRIG, 2 Emergent Defender',
        '2 Awakened Upholder TRIG, 2 Awakened Defender',
        '1 Sleepless Upholder, 2 Awakened Defender, 1 Awakened Upholder, 1 Awakened Preserver']),
    S('Outpost Frontier Stronghold', 3, ANOM, [
        '3 Argos, 1 Sleepless Defender TRIG',
        '4 Awakened Defender TRIG',
        '4 Emergent Defender, 2 Sleepless Defender']),
    S('Solar Cell', 3, ANOM, [
        '1 Awakened Upholder, 1 Emergent Preserver TRIG',
        '1 Awakened Upholder, 3 Awakened Defender, 2 Emergent Defender, 2 Emergent Upholder TRIG',
        '1 Sleepless Preserver, 1 Sleepless Defender, 2 Awakened Defender']),
    S('The Oruze Construct', 3, ANOM, [
        '2 Argos, 4 Awakened Upholder TRIG',
        '1 Sleepless Defender TRIG, 4 Emergent Defender',
        '1 Sleepless Upholder, 2 Awakened Preserver']),
    S('Unsecured Frontier Database', 3, RADAR, [
        '3 Awakened Defender, 3 Emergent Preserver TRIG',
        '4 Awakened Upholder, 4 Awakened Defender TRIG',
        '1 Sleepless Upholder TRIG, 3 Awakened Upholder, 3 Emergent Defender',
        '2 Sleepless Upholder, 3 Awakened Upholder, 2 Emergent Preserver'], cans=10),
    S('Unsecured Frontier Receiver', 3, RADAR, [
        '5 Emergent Defender, 3 Awakened Preserver TRIG',
        '3 Awakened Defender, 1 Awakened Preserver TRIG, 3 Awakened Upholder',
        '4 Emergent Upholder, 1 Sleepless Defender TRIGHIT',
        '4 Emergent Defender, 2 Sleepless Preserver'], cans=7),
    S('Forgotten Frontier Quarantine Outpost', 3, MAG, [
        '3 Awakened Preserver TRIG, 6 Emergent Defender',
        '2 Awakened Upholder, 3 Awakened Defender TRIG, 2 Awakened Preserver',
        '1 Sleepless Defender, 4 Emergent Upholder TRIG',
        '2 Sleepless Preserver, 4 Emergent Defender'], cans=7),
    S('Forgotten Frontier Recursive Depot', 3, MAG, [
        '4 Emergent Preserver, 2 Awakened Defender TRIG',
        '3 Awakened Defender TRIG, 4 Awakened Upholder',
        '2 Emergent Defender, 2 Awakened Upholder, 2 Sleepless Upholder TRIG',
        '2 Emergent Preserver, 1 Awakened Upholder, 3 Sleepless Upholder'], cans=10),

    # ---- C4 (Argos).  radar/mag escalation:  a Sleepless battleship and a Talocan cruiser wreck ----
    S('Frontier Barracks', 4, ANOM, [
        '1 Sleepless Upholder, 1 Sleepless Preserver TRIG',
        '2 Awakened Preserver, 3 Sleepless Upholder TRIG',
        '3 Emergent Preserver, 4 Awakened Preserver, 2 Sleepless Preserver']),
    S('Frontier Command Post', 4, ANOM, [
        '5 Argos, 4 Emergent Defender, 4 Emergent Preserver TRIG',
        '4 Emergent Upholder, 6 Awakened Defender TRIG, 2 Sleepless Defender',
        '3 Sleepless Preserver, 2 Awakened Defender, 2 Awakened Preserver']),
    S('Integrated Terminus', 4, ANOM, [
        '4 Argos, 4 Awakened Defender TRIG, 3 Emergent Upholder',
        '2 Awakened Preserver, 4 Awakened Upholder TRIG, 2 Emergent Preserver',
        '4 Emergent Defender, 2 Emergent Upholder, 1 Sleepless Safeguard']),
    S('Sleeper Information Sanctum', 4, ANOM, [
        '2 Awakened Upholder, 2 Sleepless Preserver TRIG',
        '3 Sleepless Defender, 1 Sleepless Safeguard TRIG',
        '2 Awakened Preserver, 2 Awakened Upholder, 2 Emergent Preserver, 3 Emergent Upholder']),
    S('Unsecured Frontier Digital Nexus', 4, RADAR, [
        '4 Emergent Defender, 6 Awakened Defender TRIG',
        '4 Emergent Preserver, 4 Awakened Defender, 3 Sleepless Defender TRIG',
        '4 Sleepless Safeguard'], cans=12, esc='1 Sleepless Safeguard', wreck=TALOCAN_CRUISER),
    S('Unsecured Frontier Trinary Hub', 4, RADAR, [
        '4 Emergent Defender, 1 Sleepless Preserver TRIG',
        '4 Awakened Defender, 2 Sleepless Preserver TRIG',
        '2 Sleepless Defender, 2 Sleepless Preserver'], cans=10, esc='1 Sleepless Defender', wreck=TALOCAN_CRUISER),
    S('Forgotten Frontier Conversion Module', 4, MAG, [
        '1 Argos, 6 Emergent Defender, 6 Awakened Defender, 1 Sleepless Upholder',
        '5 Emergent Defender, 5 Awakened Defender, 3 Sleepless Defender TRIG',
        '4 Sleepless Safeguard'], cans=12, esc='1 Sleepless Safeguard', wreck=TALOCAN_CRUISER, flags=('open',)),
    S('Forgotten Frontier Evacuation Center', 4, MAG, [
        '4 Emergent Defender, 1 Sleepless Preserver TRIG',
        '4 Awakened Defender, 2 Sleepless Preserver TRIG',
        '2 Sleepless Defender, 2 Sleepless Preserver'], cans=12, esc='1 Sleepless Defender', wreck=TALOCAN_CRUISER),

    # ---- C5 (Orthrus).  radar/mag escalation:  a Sleepless battleship and a Talocan battleship wreck ----
    S('Core Garrison', 5, ANOM, [
        '3 Orthrus, 5 Emergent Sentinel, 5 Awakened Sentinel TRIG',
        '6 Emergent Keeper TRIG, 3 Sleepless Sentinel',
        '5 Awakened Sentinel, 3 Awakened Keeper, 3 Sleepless Sentinel TRIG',
        '3 Emergent Warden, 2 Awakened Sentinel, 2 Sleepless Keeper']),
    S('Core Stronghold', 5, ANOM, [
        '6 Orthrus, 4 Emergent Sentinel TRIG',
        '2 Sleepless Keeper, 2 Sleepless Warden TRIG',
        '6 Emergent Warden, 7 Awakened Sentinel TRIG',
        '4 Awakened Warden, 2 Sleepless Keeper, 3 Sleepless Warden']),
    S('Oruze Osobnyk', 5, ANOM, [
        '4 Emergent Sentinel, 1 Sleepless Keeper, 3 Sleepless Sentinel TRIG',
        '6 Awakened Sentinel, 1 Sleepless Guardian TRIG',
        '4 Emergent Keeper, 2 Emergent Warden, 3 Awakened Warden']),
    S('Quarantine Area', 5, ANOM, [
        '4 Orthrus, 3 Emergent Escort, 2 Awakened Keeper, 1 Sleepless Sentinel',
        '4 Awakened Sentinel, 2 Sleepless Sentinel',
        '5 Emergent Sentinel, 3 Awakened Warden, 2 Sleepless Keeper'], flags=('anykill',)),
    S('Unsecured Frontier Enclave Relay', 5, RADAR, [
        '3 Sleepless Keeper TRIG',
        '5 Emergent Keeper, 4 Awakened Warden, 2 Sleepless Keeper TRIG',
        '3 Emergent Keeper, 4 Emergent Warden, 3 Awakened Sentinel, 4 Awakened Warden, 2 Sleepless Keeper TRIG',
        '3 Emergent Warden, 3 Awakened Warden, 5 Sleepless Warden'], cans=12, esc='1 Sleepless Sentinel', wreck=TALOCAN_BS),
    S('Unsecured Frontier Server Bank', 5, RADAR, [
        '5 Emergent Sentinel, 5 Awakened Keeper TRIG',
        '3 Emergent Keeper, 3 Emergent Warden, 4 Awakened Keeper TRIG, 2 Awakened Sentinel, 4 Awakened Warden',
        '5 Emergent Warden, 2 Sleepless Keeper TRIG, 1 Sleepless Sentinel',
        '2 Awakened Keeper, 3 Awakened Warden, 2 Sleepless Keeper, 2 Sleepless Warden'],
        cans=15, esc='1 Sleepless Sentinel', wreck=TALOCAN_BS),      # escalation ship:  as the other C5 sites (pick)
    S('Forgotten Core Data Field', 5, MAG, [
        '5 Emergent Sentinel, 5 Awakened Keeper TRIG',
        '4 Emergent Keeper, 4 Emergent Warden, 3 Awakened Keeper TRIG, 2 Awakened Sentinel, 3 Awakened Warden',
        # "6 frigates (scram)":  the two Core frigates with a scram (pick)
        '3 Emergent Sentinel, 3 Emergent Warden, 2 Sleepless Keeper TRIG, 2 Sleepless Sentinel',
        '2 Awakened Keeper, 2 Awakened Warden, 2 Sleepless Keeper, 2 Sleepless Warden'],
        cans=12, esc='1 Sleepless Sentinel', wreck=TALOCAN_BS),
    S('Forgotten Core Information Pen', 5, MAG, [
        '3 Sleepless Keeper TRIG',
        # no TRIG on this wave:  the next comes when all of it is dead
        '4 Emergent Keeper, 4 Awakened Warden, 2 Sleepless Keeper',
        '3 Emergent Keeper, 3 Emergent Warden, 3 Awakened Sentinel, 4 Awakened Warden TRIG, 3 Sleepless Keeper',
        '4 Emergent Warden, 4 Awakened Keeper, 5 Sleepless Keeper'], cans=15, esc='1 Sleepless Sentinel', wreck=TALOCAN_BS),

    # ---- C6 (Orthrus, and Argos on radar sites).  radar/mag escalation:  a Sleepless Guardian and a Talocan battleship wreck ----
    S('Core Citadel', 6, ANOM, [
        '3 Emergent Keeper, 3 Emergent Sentinel, 3 Emergent Warden, 4 Awakened Keeper, 3 Awakened Warden, 1 Sleepless Keeper, '
        '2 Sleepless Sentinel, 1 Sleepless Warden TRIG',
        '8 Emergent Keeper, 3 Sleepless Sentinel, 2 Sleepless Warden TRIG',
        '8 Emergent Keeper, 3 Sleepless Keeper, 3 Sleepless Warden TRIG',
        '6 Emergent Keeper, 6 Sleepless Guardian']),
    S('Core Bastion', 6, ANOM, [
        '3 Orthrus, 2 Sleepless Sentinel, 3 Awakened Sentinel, 3 Awakened Warden, 3 Awakened Keeper, 3 Emergent Keeper TRIG, '
        '3 Emergent Sentinel',
        '4 Awakened Sentinel, 4 Awakened Warden TRIG, 4 Sleepless Keeper',
        '6 Emergent Warden, 5 Awakened Warden, 4 Sleepless Guardian TRIG',
        '4 Sleepless Sentinel, 2 Sleepless Guardian']),
    S('Strange Energy Readings', 6, ANOM, [
        '5 Awakened Warden TRIG, 5 Emergent Keeper',
        '6 Awakened Keeper, 3 Sleepless Guardian TRIG',
        '6 Emergent Keeper, 4 Awakened Warden, 5 Sleepless Keeper']),
    S('The Mirror', 6, ANOM, [
        '6 Orthrus, 2 Sleepless Sentinel, 3 Sleepless Warden TRIG, 4 Awakened Warden, 6 Emergent Warden',
        '6 Emergent Sentinel, 2 Sleepless Keeper, 3 Sleepless Guardian TRIG',
        '4 Emergent Keeper, 4 Emergent Warden, 4 Awakened Keeper, 4 Awakened Warden, 4 Sleepless Keeper']),
    S('Unsecured Core Backup Array', 6, RADAR, [
        '3 Argos, 3 Orthrus, 10 Emergent Sentinel, 3 Sleepless Keeper, 2 Sleepless Sentinel, 2 Sleepless Warden TRIG',
        '6 Awakened Keeper, 3 Sleepless Sentinel, 3 Sleepless Keeper, 2 Sleepless Warden TRIG',
        '4 Awakened Warden, 3 Sleepless Sentinel, 3 Sleepless Keeper, 3 Sleepless Warden TRIG',
        '10 Emergent Sentinel, 8 Awakened Sentinel, 6 Sleepless Guardian'],
        cans=10, esc='1 Sleepless Guardian', wreck=TALOCAN_BS),
    S('Unsecured Core Emergence', 6, RADAR, [
        '4 Argos, 4 Orthrus, 9 Emergent Sentinel, 9 Emergent Keeper, 9 Emergent Warden TRIG, 1 Sleepless Guardian',
        '4 Emergent Warden, 7 Awakened Sentinel, 7 Awakened Keeper, 7 Awakened Warden TRIG',
        '5 Emergent Sentinel, 3 Sleepless Sentinel, 3 Sleepless Keeper, 3 Sleepless Warden, 1 Sleepless Guardian TRIG',
        '4 Emergent Sentinel, 4 Emergent Keeper, 4 Awakened Sentinel, 4 Awakened Keeper, 4 Awakened Warden, '
        '2 Sleepless Sentinel, 2 Sleepless Keeper, 2 Sleepless Warden'],
        cans=15, esc='1 Sleepless Guardian', wreck=TALOCAN_BS),
    S('Forgotten Core Assembly Hall', 6, MAG, [
        '6 Orthrus, 5 Emergent Sentinel, 3 Sleepless Keeper, 3 Sleepless Sentinel, 3 Sleepless Warden TRIG',
        '6 Awakened Keeper, 3 Sleepless Keeper, 2 Sleepless Sentinel, 1 Sleepless Warden TRIG',
        '6 Awakened Warden, 3 Sleepless Keeper, 2 Sleepless Sentinel, 2 Sleepless Warden TRIG',
        '10 Emergent Sentinel, 6 Awakened Sentinel, 6 Sleepless Guardian'],
        cans=10, esc='1 Sleepless Guardian', wreck=TALOCAN_BS),
    # "same as Core Emergence plus 3 Sleepless Guardian in S3"
    S('Forgotten Core Circuitry Disassembler', 6, MAG, [
        '4 Argos, 4 Orthrus, 9 Emergent Sentinel, 9 Emergent Keeper, 9 Emergent Warden TRIG, 1 Sleepless Guardian',
        '4 Emergent Warden, 7 Awakened Sentinel, 7 Awakened Keeper, 7 Awakened Warden TRIG',
        '5 Emergent Sentinel, 3 Sleepless Sentinel, 3 Sleepless Keeper, 3 Sleepless Warden, 1 Sleepless Guardian TRIG',
        '4 Emergent Sentinel, 4 Emergent Keeper, 4 Awakened Sentinel, 4 Awakened Keeper, 4 Awakened Warden, '
        '2 Sleepless Sentinel, 2 Sleepless Keeper, 2 Sleepless Warden, 3 Sleepless Guardian'],
        cans=15, esc='1 Sleepless Guardian', wreck=TALOCAN_BS),
]

# ---- gas sites (research 4.5):  (name, family, clouds [(typeID, units)], guards).  guards come when the first ship
#  arrives (Crucible-era guides;  no dated source, pick).  the Crucible edits (Nov-Dec 2011) are used where there are two
FULLERITE = {'C50': 30370, 'C60': 30371, 'C70': 30372, 'C72': 30373, 'C84': 30374, 'C28': 30375, 'C32': 30376,
             'C320': 30377, 'C540': 30378}
GAS = [
    ('Barren Perimeter Reservoir', 'Perimeter', [('C50', 3000), ('C60', 1500)], '3 Emergent Patroller, 2 Emergent Watchman'),
    ('Token Perimeter Reservoir', 'Perimeter', [('C60', 3000), ('C70', 1500)], '2 Emergent Escort, 1 Awakened Watchman'),
    ('Minor Perimeter Reservoir', 'Perimeter', [('C70', 3000), ('C72', 1500)], '2 Awakened Patroller'),
    ('Ordinary Perimeter Reservoir', 'Perimeter', [('C72', 3000), ('C84', 1500)], '5 Sirius'),
    ('Sizable Perimeter Reservoir', 'Perimeter', [('C50', 1500), ('C84', 3000)], '3 Emergent Patroller, 3 Emergent Watchman'),
    ('Bountiful Frontier Reservoir', 'Frontier', [('C28', 5000), ('C32', 1000)], '6 Emergent Defender, 4 Awakened Defender'),
    ('Vast Frontier Reservoir', 'Frontier', [('C28', 1000), ('C32', 5000)], '4 Awakened Defender, 6 Awakened Upholder'),
    ('Instrumental Core Reservoir', 'Core', [('C320', 6000), ('C540', 500)], '4 Sleepless Sentinel'),
    ('Vital Core Reservoir', 'Core', [('C320', 500), ('C540', 6000)], '4 Emergent Keeper, 4 Sleepless Keeper'),
]

# ---- ore sites (research 4.6):  (name, family, rocks, guards).  the ore mix is a pick ("all ore types" in Perimeter,
#  the better ores further in, Mercoxit only in Core).  big sites are capped at MAX_ROCKS with the ore spread over them
ORE = {'Veldspar': 1230, 'Scordite': 1228, 'Pyroxeres': 1224, 'Plagioclase': 18, 'Omber': 1227, 'Kernite': 20,
       'Jaspet': 1226, 'Hemorphite': 1231, 'Hedbergite': 21, 'Gneiss': 1229, 'Dark Ochre': 1232, 'Spodumain': 19,
       'Crokite': 1225, 'Bistot': 1223, 'Arkonor': 22, 'Mercoxit': 11396}
ORE_MIX = {
    'Perimeter': ['Veldspar', 'Scordite', 'Pyroxeres', 'Plagioclase', 'Omber', 'Kernite', 'Jaspet', 'Hemorphite',
                  'Hedbergite', 'Gneiss', 'Dark Ochre', 'Spodumain', 'Crokite', 'Bistot', 'Arkonor'],
    'Frontier': ['Kernite', 'Jaspet', 'Hemorphite', 'Hedbergite', 'Gneiss', 'Dark Ochre', 'Spodumain', 'Crokite',
                 'Bistot', 'Arkonor'],
    'Core': ['Gneiss', 'Dark Ochre', 'Spodumain', 'Crokite', 'Bistot', 'Arkonor'],
}
ROCK_UNITS = {'Perimeter': 10000, 'Frontier': 15000, 'Core': 20000}   # per rock (pick;  k-space sites use 10000)
MAX_ROCKS = 60
GRAVS = [
    ('Common Perimeter Deposit', 'Perimeter', 18, 0, '5 Emergent Patroller'),
    ('Unexceptional Frontier Deposit', 'Frontier', 19, 0, '1 Emergent Escort, 2 Emergent Patroller, 2 Emergent Watchman'),
    ('Average Frontier Deposit', 'Frontier', 176, 0, '1 Awakened Patroller, 3 Emergent Patroller'),
    ('Unusual Core Deposit', 'Core', 19, 1, '2 Awakened Patroller'),
    ('Infrequent Core Deposit', 'Core', 104, 4, '2 Awakened Defender'),
    ('Exceptional Core Deposit', 'Core', 125, 5, '1 Sleepless Sentinel'),
    ('Rarified Core Deposit', 'Core', 28, 2, '1 Sleepless Sentinel, 2 Awakened Sentinel'),
]
FAMILY_CLASSES = {'Perimeter': (1, 2), 'Frontier': (3, 4), 'Core': (5, 6)}


def parse(text):
    """'2 Sirius, 1 Awakened Escort TRIG' -> [(typeID, qty, tag)]  tag '' / 'TRIG' / 'TRIGHIT' / 'SENTRY'"""
    out = []
    for part in text.split(','):
        m = re.fullmatch(r'\s*(\d+) ([A-Za-z ]+?)(?: (TRIG|TRIGHIT))?\s*', part)
        if not m:
            raise SystemExit('bad wave entry: %r' % part)
        qty, name, tag = int(m.group(1)), m.group(2), m.group(3) or ''
        if name in SENTRIES:
            out.append((SENTRIES[name], qty, 'SENTRY'))
        elif name in SHIPS:
            out.append((SHIPS[name], qty, tag))
        else:
            raise SystemExit('unknown ship: %r' % name)
    return out


class Out:
    def __init__(self):
        self.dungeons, self.rooms, self.objects, self.groups, self.types, self.triggers, self.events = ([] for _ in range(7))
        self.nextDungeon = DUNGEON_BASE
        self.next = {k: ROW_BASE for k in ('room', 'object', 'group', 'trigger', 'event')}

    def id(self, kind):
        self.next[kind] += 1
        return self.next[kind]

    def dungeon(self, name, kind, cls):
        self.nextDungeon += 1
        d = self.nextDungeon
        self.dungeons.append((d, name, FACTION_SLEEPERS, kind, 'wh-sleeper-%d' % d, cls))
        room = self.id('room')
        self.rooms.append((room, name, d))
        return d, room

    def obj(self, room, typeID, pos, radius=0):
        o = self.id('object')
        self.objects.append((o, room, typeID, pos[0], pos[1], pos[2], radius))
        return o

    def group(self, room, wave, mode, types, pos, spread):
        g = self.id('group')
        self.groups.append((g, room, wave, mode, pos[0], pos[1], pos[2], spread))
        for typeID, qty in types:
            self.types.append((g, typeID, qty))
        return g

    def trigger(self, room, ttype, group=0, value=0, repeat=0, counter=0):
        t = self.id('trigger')
        self.triggers.append((t, room, ttype, group, value, counter, repeat))
        return t

    def event(self, trig, seq, etype, group=0, typeID=0, qty=0, value=0, counter=0):
        self.events.append((self.id('event'), trig, seq, etype, group, typeID, qty, value, counter))


def ring(rng, dist, jitter=0.25):
    """a point dist m from the warp-in, roughly level"""
    a = rng.uniform(0, 2 * math.pi)
    d = dist * rng.uniform(1 - jitter, 1 + jitter)
    return (round(d * math.cos(a)), round(rng.uniform(-0.1, 0.1) * d), round(d * math.sin(a)))


def merge(entries):
    """sum quantities per type, keep order"""
    seen = {}
    for typeID, qty in entries:
        seen[typeID] = seen.get(typeID, 0) + qty
    return list(seen.items())


def build_site(out, site):
    rng = random.Random(site['name'])
    cls, kind, flags = site['cls'], site['kind'], site['flags']
    d, room = out.dungeon(site['name'], kind, cls)
    waves = [parse(w) for w in site['waves']]

    # scenery, sentries (from the first wave), cans and the fixed wreck
    for typeID in DECO.get((kind, family(cls)), DECO.get((kind, None), ())):
        out.obj(room, typeID, ring(rng, 45000, 0.2))
    for typeID, qty, tag in waves[0]:
        if tag == 'SENTRY':
            for _ in range(qty):
                out.obj(room, typeID, ring(rng, 20000, 0.35))
    waves = [[e for e in w if e[2] != 'SENTRY'] for w in waves]
    can = RADAR_CAN[cls] if kind == RADAR else MAG_CAN[cls]
    for _ in range(site['cans']):
        out.obj(room, can, ring(rng, 15000, 0.6))
    if site['wreck'] and not site['esc']:
        out.obj(room, site['wreck'], ring(rng, 12000, 0.5))

    # groups:  per wave the TRIG ships and the rest
    last = len(waves) - 1
    plan = []       # per wave:  [(groupID, isTrig, onHit)]
    for i, wave in enumerate(waves):
        mode = MODE_TRIGGER if (i > 0 or 'open' in flags) else MODE_CREATED
        dist = 70000 if ('far' in flags and i == 2) else 25000
        pos = ring(rng, dist, 0.2)
        trig = [(t, q) for t, q, tag in wave if tag in ('TRIG', 'TRIGHIT')]
        rest = [(t, q) for t, q, tag in wave if tag == '']
        onHit = any(tag == 'TRIGHIT' for _, _, tag in wave)
        groups = []
        if i == last or not trig or 'anykill' in flags:
            groups.append((out.group(room, i + 1, mode, merge(trig + rest), pos, 12000), True, False))
        else:
            groups.append((out.group(room, i + 1, mode, merge(trig), pos, 8000), True, onHit))
            if rest:
                groups.append((out.group(room, i + 1, mode, merge(rest), pos, 12000), False, False))
        plan.append(groups)

    esc = None
    if site['esc']:
        escTypes = [(t, q) for t, q, _ in parse(site['esc'])]
        esc = out.group(room, len(waves) + 1, MODE_TRIGGER, escTypes, ring(rng, 20000, 0.2), 5000)

    # triggers:  every group's death counts.  the trigger group's death (or first hit, or any kill) brings the next wave
    groupCount = sum(len(g) for g in plan) + (1 if esc else 0)
    for i, groups in enumerate(plan):
        for groupID, isTrig, onHit in groups:
            t = out.trigger(room, T_EXPLODING, groupID)
            seq = 1
            # an on-hit trigger ship that dies in one volley never fires 'attacked':  its death brings the wave too
            if isTrig and i < last and 'anykill' not in flags:
                for nextID, _, _ in plan[i + 1]:
                    out.event(t, seq, E_SPAWN_GUARDS, group=nextID)
                    seq += 1
            if i == last and esc:
                out.event(t, seq, E_SPAWN_GUARDS, group=esc)
                out.event(t, seq + 1, E_SPAWN_OBJECT, typeID=site['wreck'], qty=1)
                seq += 2
            out.event(t, seq, E_COUNTER_ADD, value=1, counter=COUNTER)
            if isTrig and i < last and (onHit or 'anykill' in flags):
                t2 = out.trigger(room, T_ATTACKED if onHit else T_EXPLODING, groupID, value=(0 if onHit else 1))
                for seq2, (nextID, _, _) in enumerate(plan[i + 1], 1):
                    out.event(t2, seq2, E_SPAWN_GUARDS, group=nextID)
    if esc:
        t = out.trigger(room, T_EXPLODING, esc)
        out.event(t, 1, E_COUNTER_ADD, value=1, counter=COUNTER)

    # the first wave of a site that wakes when a can is opened
    if 'open' in flags:
        t = out.trigger(room, T_ARCH_OK if kind == MAG else T_HACK_OK)
        for seq, (groupID, _, _) in enumerate(plan[0], 1):
            out.event(t, seq, E_SPAWN_GUARDS, group=groupID)

    # opened cans count too
    need = groupCount
    if site['cans']:
        t = out.trigger(room, T_ARCH_OK if kind == MAG else T_HACK_OK, repeat=1)
        out.event(t, 1, E_COUNTER_ADD, value=1, counter=COUNTER)
        need += site['cans']
    t = out.trigger(room, T_COUNTER_GE, value=need, counter=COUNTER)
    out.event(t, 1, E_COMPLETE)


def build_resource_site(out, name, kind, cls, resources, guards):
    """gas or ore:  the clouds/rocks (radius = quantity), the guards when the first ship arrives, done when mined out"""
    rng = random.Random('%s %d' % (name, cls))
    d, room = out.dungeon(name, kind, cls)
    for typeID in DECO[(kind, None)]:
        out.obj(room, typeID, ring(rng, 40000, 0.2))
    spread = 8000 if kind == LADAR else 25000
    for typeID, units in resources:
        out.obj(room, typeID, ring(rng, spread, 0.8), units)
    sentries = [(t, q) for t, q, tag in parse(guards) if tag == 'SENTRY']
    ships = [(t, q) for t, q, tag in parse(guards) if tag != 'SENTRY']
    if sentries:
        out.group(room, 1, MODE_ENTERED, sentries, ring(rng, 15000, 0.3), 10000)
    if ships:
        out.group(room, 1, MODE_ENTERED, ships, ring(rng, 20000, 0.3), 8000)
    t = out.trigger(room, T_ROOM_MINED_OUT)
    out.event(t, 1, E_COMPLETE)


def rocks(fam, count, mercoxit):
    """[(typeID, units)] for an ore site of count rocks, capped at MAX_ROCKS with the units spread over them"""
    scale = max(1.0, count / MAX_ROCKS)
    count = min(count, MAX_ROCKS)
    mercoxit = max(1, int(round(mercoxit / scale))) if mercoxit else 0
    names = [ORE_MIX[fam][i % len(ORE_MIX[fam])] for i in range(count - mercoxit)] + ['Mercoxit'] * mercoxit
    units = int(ROCK_UNITS[fam] * scale)
    return [(ORE[n], units // 2 if n == 'Mercoxit' else units) for n in names]


def sql_rows(rows, fmt):
    return ',\n'.join(fmt % r for r in rows)


def main(path):
    out = Out()
    for site in SITES:
        build_site(out, site)
    for name, fam, clouds, guards in GAS:
        for cls in FAMILY_CLASSES[fam]:
            build_resource_site(out, name, LADAR, cls, [(FULLERITE[g], u) for g, u in clouds], guards)
    for name, fam, count, merc, guards in GRAVS:
        for cls in FAMILY_CLASSES[fam]:
            build_resource_site(out, name, GRAV, cls, rocks(fam, count, merc), guards)

    def q(s):
        return "'" + s.replace("'", "''") + "'"

    L = []
    L.append('-- Wormholes:  the Sleeper sites, and the gas and ore sites of w-space, as they were in Crucible.')
    L.append('--  utils/wormholes/gen_sleeper_sites_sql.py writes this (the sources and picks are listed there)')
    L.append('--  - %d combat anomalies, radar and magnetometric sites (C1-C6), %d gas and %d ore site dungeons'
             % (len(SITES), sum(len(FAMILY_CLASSES[g[1]]) for g in GAS), sum(len(FAMILY_CLASSES[g[1]]) for g in GRAVS)))
    L.append('--  - faction Sleepers, difficulty = the w-space class.  AnomalyMgr spawns them in w-space only')
    L.append('--  - the Sleeper databanks and artifacts get the Sleeper loot families (datacores and decryptors;  ancient')
    L.append('--    salvage and relics).  the Talocan wrecks give ancient salvage')
    L.append('-- +migrate Up')
    L.append('INSERT INTO dunDungeons (dungeonID, dungeonName, dungeonStatus, factionID, archetypeID, dungeonUUID, '
             'dungeonNameID, difficulty, minSecurity, maxSecurity, spawnWeight, lifetimeHours) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2], r[3], q(r[4]), r[5]) for r in out.dungeons],
                      '(%d, %s, 1, %d, %d, %s, 0, %d, -1, 1, 1, 0)') + ';')
    L.append('INSERT INTO dunRooms (roomID, roomName, dungeonID, arrivalX, arrivalY, arrivalZ) VALUES')
    L.append(sql_rows([(r[0], q(r[1]), r[2]) for r in out.rooms], '(%d, %s, %d, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunRoomObjects (objectID, roomID, typeID, groupID, x, y, z, yaw, pitch, roll, radius) VALUES')
    L.append(sql_rows(out.objects, '(%d, %d, %d, 0, %d, %d, %d, 0, 0, 0, %d)') + ';')
    L.append('INSERT INTO dunSpawnGroups (spawnGroupID, roomID, waveNumber, spawnMode, npcClassType, npcClassSub, '
             'factionID, x, y, z, spread, warpIn, isOverseer, lootGroupID, aggroMode, aggroRange, aggroLink) VALUES')
    L.append(sql_rows(out.groups, '(%d, %d, %d, %d, 0, 0, 0, %d, %d, %d, %d, 0, 0, 0, 0, 0, 0)') + ';')
    L.append('INSERT INTO dunSpawnGroupTypes (spawnGroupID, typeID, quantity) VALUES')
    L.append(sql_rows(out.types, '(%d, %d, %d)') + ';')
    L.append('INSERT INTO dunTriggers (triggerID, roomID, triggerType, spawnGroupID, objectID, value, counterID, repeatable) VALUES')
    L.append(sql_rows([(t[0], t[1], t[2], t[3], t[4], t[5], t[6]) for t in out.triggers], '(%d, %d, %d, %d, 0, %g, %d, %d)') + ';')
    L.append('INSERT INTO dunTriggerEvents (eventID, triggerID, sequence, eventType, delaySeconds, spawnGroupID, objectID, '
             'messageType, messageID, messageText, typeID, quantity, value, counterID) VALUES')
    L.append(sql_rows(out.events, "(%d, %d, %d, %d, 0, %d, 0, 0, 0, '', %d, %d, %g, %d)") + ';')
    L.append('')
    L.append('-- site containers:  databanks are Sleeper data, artifacts Sleeper debris, the Talocan wrecks ancient salvage')
    L.append('REPLACE INTO expContainerFamily (typeID, familyID, factionPrefix) VALUES')
    fam = [(t, 4, 'Sleeper') for t in sorted(RADAR_CAN.values())] + [(t, 5, 'Sleeper') for t in sorted(MAG_CAN.values())]
    fam += [(t, 5, 'Talocan') for t in (TALOCAN_BS, TALOCAN_CRUISER, TALOCAN_FRIGATE)]
    L.append(sql_rows(fam, "(%d, %d, '%s')") + ';')
    L.append('')
    L.append('-- +migrate Down')
    L.append('-- back to the F11 exploration families (20261011135000-F11-exploration.sql)')
    L.append('DELETE FROM expContainerFamily WHERE typeID BETWEEN 30516 AND 30530;')
    L.append("INSERT IGNORE INTO expContainerFamily (typeID, familyID, factionPrefix)\n"
             "SELECT t.typeID,\n"
             "  CASE WHEN t.typeName LIKE 'Sleeper%' THEN 5\n"
             "       WHEN IFNULL(r.valueInt, r.valueFloat) = 13278 THEN 2\n"
             "       WHEN IFNULL(r.valueInt, r.valueFloat) = 25863 THEN 3\n"
             "       ELSE 1 END,\n"
             "  CASE WHEN t.typeName LIKE 'Sleeper%' THEN 'Sleeper' ELSE '' END\n"
             "FROM invTypes t\n"
             "JOIN dgmTypeAttributes a ON a.typeID = t.typeID AND a.attributeID = 901\n"
             "LEFT JOIN dgmTypeAttributes r ON r.typeID = t.typeID AND r.attributeID = 182\n"
             "WHERE t.groupID = 306 AND t.typeID BETWEEN 30516 AND 30530;")
    for table, col in (('dunTriggerEvents', 'eventID'), ('dunTriggers', 'triggerID'), ('dunSpawnGroupTypes', 'spawnGroupID'),
                       ('dunSpawnGroups', 'spawnGroupID'), ('dunRoomObjects', 'objectID'), ('dunRooms', 'roomID')):
        L.append('DELETE FROM %s WHERE %s > %d AND %s < %d;' % (table, col, ROW_BASE, col, ROW_BASE + 1000000))
    L.append('DELETE FROM dunDungeons WHERE dungeonID > %d AND dungeonID < %d;' % (DUNGEON_BASE, DUNGEON_BASE + 1000000))
    with open(path, 'w', newline='\n') as f:
        f.write('\n'.join(L) + '\n')
    print('%d dungeons, %d objects, %d groups, %d triggers, %d events' %
          (len(out.dungeons), len(out.objects), len(out.groups), len(out.triggers), len(out.events)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'sql/migrations/20261022120000-sleeper-sites.sql')
