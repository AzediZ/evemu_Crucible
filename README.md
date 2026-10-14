# EVEMu Revised - An EVE: Crucible Emulator

> **Crucible Sandbox:** what this repository changes on top of Renn's EVEMu Revised is in the **[changelog](CHANGELOG-SANDBOX.md)**, newest first and dated.


## Introduction
EVEmu Revised is a work-in-progress server emulator for the space MMO EVE Online. This is an educational project. Please see the disclaimer below for details. Originally forked from [EVEMu](https://github.com/EvEmu-Project/evemu_Crucible).
The focus of this repo is on creating a small-scale EVE simulation from the Crucible era.

> [!IMPORTANT]
> Despite implementing several features, many of these may only have partial implementations or be buggy.

## Features

### Living universe

**NPC AI**:
- **Standard AI:** a state machine with movement, leash, reaction delay, aggro groups and EWAR rolled per spawn.
- **Level-One AI** for Sleepers, Incursion NPCs and officers: threat tables, target switching, remote repair, group assists and remote ECM.
- **Law:** CONCORD, police, navy, and gate and station sentries, plus pirate sentries in aggro groups.
- **Server load:** level of detail and a CPU budget, telemetry, a shot-fairness audit, belt wander, a belt spawn cap and difficulty presets.

**Ambient NPCs**: background life in hi-sec and low-sec, run only where players are.
- **Traffic:** ships move through gates and stations, coherent across systems. Volume comes from market activity, security level and time of day.
- **Patrols:** CONCORD and navy squads.
- **Miners:** belt miners with strip-miner beams, and haulers that visit them.
- **Criminals:** criminal events handled by sentries and CONCORD. Whether criminals attack players can be set separately for hi-sec and low-sec.
- **Consequences:** full crimewatch for attacks on ambient ships.

**NPC loot:**
- Pools for deadspace, mission, officer, hauler and Sleeper NPCs.
- Faction and officer modules.
- The correct wreck type per faction and hull size.

**Simulated market**: a self-consistent NPC economy calibrated on Crucible-era history
- **Participants:** market makers, finite wallets, producers, consumers, miners, reprocessors and manufacturers. Consumers lose ships at killmail rates.
- **Logistics:** remote miners in low-sec and null-sec, and haulers on the real jump map.
- **Production chains:** T2 invention, moon reactions, planetary interaction and fuel blocks.

### Combat, ships and modules
- **Weapons:** ammo use per cycle, crystals that burn out, reload times, bank loading and the offline/online charge state.
- **Electronic warfare** on a shared EWAR state:
 - ECM with the client's jam formula; ECM bursts;
 - neutralizers and nosferatus;
 - dampeners, tracking disruptors and target painters;
 - warfare links;
 - siege and triage modules;
 - smart bombs.
- **Cloaking:** cloak modes, the proximity decloak, covert ops warp, and session (gate/login) cloaks that never hide the pilot from themselves.
- **Interdiction:** warp disruption fields (they stop warps inside them and drag warps that cross them), mobile warp disruptors, interdiction spheres, the HIC field generator and its focused script.
- **Capitals:** cyno fields, jump drives, titan and black ops bridges, and doomsday.
- **Damage model:** shield bleed-through, invulnerability, and T3 subsystem skill loss.
- **Propulsion and warp:**
 - Afterburner and MWD speed from the Crucible formula, with the MWD's signature and capacitor penalties.
 - Warp capacitor cost
 - Warp speed accuracy.
- **T3 fitting** from subsystems; weapon groups
- **Ships:** eject and board, ship maintenance bays, self-destruct, jettison rules, onlining
- **Drones and fighters**

### Exploration, PvE and missions
- **Scanning:** directional scanner, probe scanning, ship, cargo and survey scanners, and persistent probes.
- **Sites:** hacking, archaeology and salvaging with Crucible rolls.
- **Dungeon engine:**
 - rooms, NPC waves, triggers and events;
 - acceleration gates;
 - escalations;
 - generated combat anomalies and complexes.
- **Missions:**
 - pockets on the dungeon engine;
 - content from eve-survival archives dated before Crucible;
 - Crucible LP and ISK rewards;
 - storyline missions;
 - R&D agents.
- **Bounties:** a delayed bounty payout with fleet shares.

### Sovereignty, starbases and planets
- **Sovereignty basics:** claimed systems, warfare, unclaimed/unclaimable systems
- **Development indices:** military, industrial and strategic; hub upgrades with install rules; sovereignty bills every 14 days.
- **Starbases:**
 - permissions and anchoring rules;
 - power, CPU and fuel, including fuel blocks and charters;
 - defenses and manual gunnery;
 - force fields;
 - reinforcement;
 - moon mining and reactions;
 - assembly arrays and labs;
 - jump bridges;
 - notifications.
- **Structure countdowns:** the client shows countdowns for anchoring, onlining and unanchoring
- **Outposts:**
 - construction, with one outpost or station per system;
 - each race's base services, offices, refining and assembly lines;
 - Foundation, Pedestal and Monument upgrades;
 - race-specific improvements that change slots, refining and services, kept across restarts.
- **Planetary interaction:**
 - the client's own colony simulation and resource harmonics;
 - customs offices;
 - player-owned customs offices with corporation, alliance and standing tax levels.

### Economy, industry and corporations
- **Market:** order validation, escrow, margin trading, the 5-minute modify and cancel timers, and daily price history.
- **Contracts:** fees and taxes, courier rules and notifications.
- **Insurance** valued from the materials' market prices.
- **Trade window.**
- **Industry:**
 - job cost paid to the line owner;
 - invention and reverse engineering with Crucible chances;
 - POS lines;
 - outpost lines.
- **Station services:** repairs, reprocessing (with the 100% cap and implant bonus) and LP store purchases.
- **Corporations:**
 - members and roles;
 - shares and votes;
 - leaving and kicking (24-hour stasis);
 - CEO candidacy;
 - contacts, bookmarks and medals.
- **Bills:** one engine with manual and automatic payment.
- **Alliances and wars:** applications, contacts, wars, and faction warfare windows.

### Characters and communication
- **Clones:** jump clones, medical clone upgrades, implants and boosters (with side effects), and ship clones.
- **Skills:** training at the client's exact rate, injection, and free skill points that raise levels at once.
- **Mail and chat:** EVE mail, mailing lists, contacts and watchlist, persistent notifications, chat access lists, and private chat invites.
- **Chat channels and MOTD.**
- **Calendar, saved fittings, tutorials and account services**
- **Logging off in space:** warp-in and warp-out from your last position

## Running EVEMu

## `docker compose` Quickstart
 EVEmu can be run with Docker Compose:
```
git clone https://github.com/rennr/evemu_Revised.git
cd evemu_Revised
docker compose up -d
```
**NOTE:** Add `--build` to the `docker compose up` command to force a rebuild of the source. This is useful when making code changes.

Configuration files are stored in `./config/`. These can be modified and will persist across restarts.

To shutdown EVEmu:
```
docker compose stop
```

## Accounts
 Accounts will be created automatically when logging in with the client if the username is not already taken.

## Disclaimer
***EVEmu is an educational project.***
 This means, our primary interest is to learn and teach us
and our users more about C++ project development in a large
scale. Our software is not intended for running public servers,
and we do not support that. We are not responsible for what others
do with the source code downloaded from this project.

## Legal
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev/
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, see https://www.gnu.org/licenses/.
    ------------------------------------------------------------------------------------

