# Crucible Sandbox changelog

Changes made on top of EVEmu Crucible for the single-player Crucible-era sandbox. **Newest first**, and every change is headed with the date it was made. [Known issues](#known-issues), [what this is built on](#about-this-changelog) and [which changes suit the main EVEmu project](#offering-changes-to-the-main-evemu-project) are at the end.

---

## 2026-10-14: Crash fixes, safer defaults, backups and database health (branch `revised`)

Patches `0051` to `0063` · Status: **compiles and boot-tested** (14 Oct 2026): the server was started natively against a fresh MariaDB 11.8 universe, came online in 13 s and stopped cleanly; the bill shift, a database outage and the backup and restore scripts were tested. Not yet built in Docker or tested in game.

**Crashes**
- `0051` A **wormhole** whose exit lands in a system that's already loaded stopped the server (it looked up the wrong system and used a missing one). A wormhole with no destination is no longer created, and its signature isn't saved.
- `0052` A **pod kill** left no frozen corpse in space (the check after creating it was the wrong way round).
- `0053` **An error in one part of a server tick** (a system, a client, a manager, a timed task) is logged and the rest carries on, instead of stopping the server. The log shows the first 20, then every 100th. A bad packet from a client is dropped.

**Settings and defaults**
- `0054` A **first database install** that was stopped part way (for example with Stop during the first start) is finished at the next start. Before, the server ran for good on a half-installed universe. A failed install or update now stops the start rather than starting the server on it.
- `0055` **Bills, office rent and war bills** don't fall due while the server is off. The server writes a heartbeat every minute and at shutdown; at the next start the due dates move on by the time it was off (only if that was over 5 minutes). New setting `<corp><PauseBillsWhileOff>` (default `true`).
- `0056` **Test server mode is off** by default (`<debug><IsTestServer>false`). It made sites respawn after 10 seconds instead of 2 minutes and limited each system to 3 signatures, one of them always a wormhole. Taking a rig off a ship destroys it, like Crucible; on a test server it goes to the cargo hold.
- `0057` **Star map kill counters** count per system again. Every 5 minutes each loaded system wrote its stale numbers over every system. "Last hour" counters reset every hour and "last 24 hours" every 24 hours; a player ship loss no longer also counts as an NPC kill.

**Backups and database health**
- `0058` **Back up and restore** on the control panel, in a new Universe row: *Back up now* saves the whole universe to `backups\universe-<date>_<time>.sql.gz` in the server folder (a running server is stopped first so its save is in the backup, and started again afterwards). *Restore backup...* puts the universe back to a backup you pick, after backing up the current one first. *Wipe universe* (moved to this row) offers a backup first. `backups/` is ignored by git.
- `0059` Every **packet sent to a client leaked** a little memory for as long as the server ran.
- `0060` **Indexes** for item loads by location and owner (every station, ship and container read the whole item table), the wallet journal and a system's best market prices. The **MyISAM tables the server writes to** are InnoDB, so a crash can't leave them needing a repair. **Corporation wallets** keep their cents (they were `float`: wrong above 100,000 ISK). New migration `20261021110000-db-indexes-innodb` (about 6 s on a fresh universe).
- `0061` **Price history** older than `<market><HistoryDays>` (365) is deleted daily; it was never read and grew by up to 1 GB a year. The order expiry check reads only player orders (0.6 ms instead of 48 ms with 180,000 orders). New migration `20261021120000-market-history-expiry-indexes`.
- `0062` **Docker logs are capped**: the server keeps the last 100 MB of console output, the database 30 MB, and the newest 20 of the server's own log files. The panel now also sees the server online from the database, since the "Online" line can be rotated away on a long run.
- `0063` **A lost database** is retried every 5 seconds instead of on every query, and a failed reconnect no longer counts as success. After 10 seconds players are told nothing is saved; if it isn't back within `<database><haltAfterMinutes>` (new, default 5; 0 keeps running), the server stops.

**Before the first start**
- Your `config/eve-server.xml` is kept, so it doesn't get the new settings. The code defaults apply (`PauseBillsWhileOff` on, `haltAfterMinutes` 5), but **`IsTestServer` stays `true` in your file until you set it to `false`**.
- Rebuild from the control panel. The next start applies the two migrations (a few seconds on a young universe) and recreates both containers for the log caps. The universe is in its volume and isn't touched.

**How to test.** Rebuild and start. Then:
- *Back up now*: a file appears in `backups` and the server comes back online. *Restore backup...* with that file: the server comes back with everything as it was.
- Stop the server for more than 5 minutes, start it, and look for "The server was off for ... hours" in the log.
- In game: pod someone (or yourself) and look for the frozen corpse; a gate jump into a system with a wormhole; take a rig off (it's destroyed).
- Optional: `docker stop db` while the server runs. You get the "lost its database" message, and after 5 minutes the server stops. `docker start db` within 5 minutes and it carries on.

---

## 2026-10-14: Moved onto Renn's EVEMu Revised (branch `revised`)

Patches `0033` to `0050` · Status: **compiles** (full cloud Release build, 14 Oct 2026). Not yet started or tested in game.

**Why.** Renn moved his work to a new repository, [EVEMu Revised](https://github.com/rennr/evemu_Revised) (branch `revised`, `db8e5b16`, 14 Oct 2026): about 400 commits after the CONCORD commit we took as `0031`. It reworks most of the server and covers most of what was still missing here. It rewrites the same files as most of our patches, so picking parts of it into our code wasn't practical. This moves onto it instead. The new code is the branch `revised` in both repos; `master` here (and `movement` in the private repo) stay as they were, so there's a way back if this goes wrong.

**What Renn's base brings** (his work, not ours; see his README)
- **NPCs:** rats, Sleepers, incursion NPCs, officers, CONCORD and sentries fight much more like Crucible (reaction delays, group aggro, leashing, rat EWAR, target switching, remote repair).
- **Background life** in high-sec and low-sec, only where you are in space: haulers and freighters using gates and stations, CONCORD and navy patrols, belt miners, pirate events.
- **A simulated NPC market** that posts real buy and sell orders at the trade hubs and buys and sells against your orders. It replaces the old market bot and the seeded orders: with it switched off there is no NPC market.
- **Loot:** faction and officer modules drop, with the right wreck per faction and hull size.
- **Much more:** missions with pockets, sites and combat anomalies, acceleration gates, escalations, hacking and archaeology, invention, implants and boosters, jump clones, cloaking, bubbles, smartbombs, warfare links, T3 fitting, POS, sov, outposts, wars, corp fixes, his own cynos, jump drives and bridges.

**What we brought over**
- `0033` **Containers** in space send their real item ID (was `0030`).
- `0034` **POS jump bridges** were refused in low-sec and null-sec and allowed in high-sec; now the other way round, like cynos (rest of `0019`).
- `0035` **Fleet boosts:** a squad with no booster, or a docked member, no longer risks a crash (rest of `0025`).
- `0036` **Leaving a fleet** really takes you out of it; the client no longer thinks you're still in fleet 0, and your fleet boost ends (rest of `0027`).
- `0037`, `0038` **Launchers** show their cycle and can be switched off; grouped weapons show every weapon firing (rest of `0010`, `0011`).
- `0039` **Fixes from the incursion work:** space containers show everything in them, mission containers load as containers, the Sansha supercarrier wreck counts as a wreck, site beacons show their own names, and mixed name lookups no longer lose names (generic parts of `0024`, `0028`, `0029`).
- `0045`, `0046` **Crime is off when the server starts** (was `0032`, rewritten for Renn's new CONCORD code). `/crime on` and `/crime off` switch it while the server runs, `/crime` says which. Off removes CONCORD, police, navy and the gate and station guns, clears every flag and timer, and sends ships left in space by pilots who logged off out of space. Kill rights are kept. With crime off, Renn's background pirates still turn up and shoot background ships, but leave you alone unless you or your drones shoot them first.
- `0047` to `0049` **Docker:** our Release build with the compile cache and a slim runtime image; a Stop now lets the server save before it exits; and a **fresh universe** in a new database volume, `db_revised`, so your old universe stays untouched in the old `db` volume. The image and caches are renamed too (`evemu_server_revised`, `server_cache_revised`, `image_cache_revised`), so nothing from the old universe is mixed in.
- `0050` **Control panel** adapted to the new layout (volume names, no market seeding).

**Bugs still in Renn's code, fixed here**
- `0041` Any **fleet broadcast** (Need Backup, Align To, Target...) stopped the server.
- `0040` Every incoming network packet **leaked memory** (upstream fixed the same leak).
- `0042` A pilot name with an **apostrophe** broke the item save, including the shutdown save.
- `0043` **Wormholes** were never created (missing braces).
- `0044` A **paused skill queue** was deleted at every server start and stop.

**Not brought over**
- **Incursions** (`0020`, `0021` and their follow-ups). Renn's branch has no incursion manager, but his dungeon engine and NPC AI have hooks for them. Rebuilding them on his site code is planned for later.
- **Replaced by Renn's own work:** weapon groups (`0009`, `0012`), drone EWAR and ECM (`0013`, `0014`), fighters (`0015`, `0016`), cynos, jump drives and portals (`0017`, `0018`, `0022`, `0026`), turret hit chance (`0023`), CONCORD (`0031`). `0007` and `0008` were already in his branch.
- **Test bot:** Renn's new login handshake breaks it. Skipped for now.

**Before the first start**
- Rename `config/eve-server.xml` and `config/log.ini` (for example to `.movement`). The server only copies its new config files when they're missing, and the old ones lack Renn's settings: with the old `eve-server.xml` the market sim stays off, so there's no NPC market.
- The first Start builds the new image from scratch: roughly 15 to 30 minutes.
- The new universe starts empty: new account, new characters. The market fills over about 2 hours, or straight away with `/mktsim seed`.

**How to test.** Start from the control panel and wait for the build. Log in with a new character and check: undock and dock, a warp and a gate jump, the market (buy something at a hub), a belt rat fight, loot. `/crime` should say crime is off. Then `/crime on`, `/crime me` in high-sec (CONCORD should come), and `/crime off`. Stop from the panel: the server log should end with the shutdown save, not a kill.

---

## 2026-10-13: Notes from testing

Notes from testing in game:

- **Titan and Black Ops bridges:** the bridge effect now works, so bridging should be fully working (`0026`).
- **Incursion sites:** site spawn positions and timers need adjusting later.
- **True Power Provisional Headquarters:** it only has the final room so far, and its positions and timers need adjusting too.
- **Acceleration gates** don't line up with their pocket properly.
- **CONCORD (`0031`)** ships spawn at 0 m, right on the player's ship, and get bounced out to long range.

---

## 2026-10-13: CONCORD and crime rules (Renn's), off until you switch them on

Patches `0031` (Renn's) and `0032` · Status: **Built**. First in-game look (13 Oct 2026): CONCORD ships spawn at 0 m on the player's ship and get bounced out to long range (see [Known issues](#known-issues)).

**Why.** Renn added Crucible's crime rules and CONCORD on a new branch of his repo, [`enhanced`](https://github.com/rennr/evemu_Crucible/tree/enhanced) (commit `b3b5d1a`, 11 Oct 2026). EVEmu had none of this: `CrimeWatch` was an empty shell and CONCORD wasn't even built. It's in, but switched off when the server starts, so new things can still be tested anywhere in peace.

**What changed**
- `0031`, Renn's commit, unchanged:
  - **Crime flags.** Aggression flags (15 minutes), the criminal countdown for a crime in 0.1 to 1.0 space, the 60-second weapons timer before you can dock or jump, criminals kept out of gates and stations in empire space unless in a pod, can flagging, flags for repairing someone who's in a fight, kill rights (30 days), outlaws (-5.0 and below) and enemy militias free to shoot. Shooting rats, police or navies is never a crime. Wars aren't in. The overview, the warnings and the neocom countdown show the flags.
  - **CONCORD** in 0.5 and up: arrives 6 to 19 seconds after a crime, depending on the system's security; scrambles, webs and jams the criminal; destroys the ship but never the pod; can't be killed; follows a criminal who warps off. Ships CONCORD destroys get no insurance.
  - **Faction police** chase pilots whose security status is too low for a high-sec system (-2.0 in 1.0, down to -4.5 in 0.5). They can be escaped.
  - **Sentry guns** at every empire gate (3) and station (2) shoot criminals and anyone flagged to their owner.
  - **Security status.** Crucible losses for aggression, ship kills and pod kills. Killing NPCs raises it, but only the best NPC per system counts every 15 minutes. The character sheet shows the security status and its history.
  - **Logging off in space.** Your ship stays 15 minutes after a fight with players; otherwise it warps off and vanishes after a minute (two if NPCs were shooting it). It can be destroyed while you're away.
  - **Small fixes.** Autopilot can't warp out of a warp scramble; standing changes reach the client with their values; NPCs with no spawn manager no longer risk a crash on warp-out; a dependency archive is extracted again if an earlier build was interrupted.
  - `/crime (me|charID|entityID) [victimID]` commits a test crime.
- `0032`, ours:
  - **Off at server start.** `<crime><Enabled>` in `eve-server.xml` is now `false`, and so is the code default.
  - **`/crime on` and `/crime off`** switch it while the server runs. On puts the sentry guns in systems that are already loaded. Off clears every flag, sends CONCORD and the police away and removes the sentry guns. `/crime` on its own says whether it's on.
  - With crime off, taking from another pilot's can or wreck no longer brings up the client's theft warning.
  - The kill rights migration is renamed to `20261013170000-killrights.sql`, so it runs after the incursion migrations on a database that already has them.
- While crime is off, killing NPCs still raises security status (with the 15-minute rule), and nothing lowers it.

**Files.** `0031`: 40 files (see the patch), including the new `npc/LawEnforcement.cpp/.h`, a rewritten `system/CrimeWatch.cpp/.h`, the kill rights migration and the `<crime>` section of `utils/config/eve-server.xml`. `0032`: `EVEServerConfig.cpp`, `EntityList.cpp/.h`, `admin/GMToolCommands.cpp/.h`, `npc/LawEnforcement.cpp/.h`, `system/CrimeWatch.cpp/.h`, `utils/config/eve-server.xml`, and the migration rename.

**How to test.** Rebuild and Start. With crime off, everything should behave as before; the console says "CrimeWatch disabled" at start. Then type `/crime on` in chat and, in a high-sec system, `/crime me`: the criminal countdown should start, and CONCORD should arrive within about 6 to 19 seconds and destroy your ship but not your pod. With a second character, shooting it in high-sec should do the same. `/crime off` should clear the countdown and send CONCORD away. If the server crashes after you log off in space with crime on, the logoff timer is the first place to look.

---

## 2026-10-13: Site containers show in space (the TCRC's MTAC Factory)

Patch `0030` · Status: **Built**. In a local test with the test bot, warping into a True Creations Research Center pocket now brings the CONCORD MTAC Factory and the Shield Transfer Control Tower with their own item IDs; before, both arrived as object 0. Not tested in game yet.

**Why.** The MTAC Factory in a True Creations Research Center was nowhere to be found. The server did create it, but the client never showed it.

**What changed**
- **Containers in space** (an EVEmu bug, also in upstream `master`). A container's position and details went to the client with item ID 0 unless it was a can a player jettisoned. The client can't show an object with no ID, so these containers were invisible. They now carry their real ID.
- This affects every incursion container: the TCRC's MTAC Factory and Shield Transfer Control Tower, the Nation Ore Refinery, the Overwhelmed Civilian Facility's civilian wrecks and evacuation freighter, the Logistics Control Arrays you hack in the Override Transfer Array and at the mothership, and the mothership's Central Control Nexus and Sansha Control Relay. It also fixes jetcans dropped by NPCs and mining drones, and containers loaded again when a system starts up.

**Files.** `src/eve-server/system/Container.cpp`.

**How to test.** Rebuild and Start. Warp to a True Creations Research Center and take the gate. The CONCORD MTAC Factory (about 40 km from where you land) and the Shield Transfer Control Tower should be in space and on the overview, if your overview shows Spawn Containers and Mission Containers. Open the factory and take the MTAC, which needs a ship with 7,500 m3 of cargo space, then put it in the control tower: the arrays should stop repairing the Battletower for 90 seconds.

---

## 2026-10-13: Ship Hangar and Journal windows show again

Patch `0029` (fixes `0028` and `0024`) · Status: **Built** (compiles). Checked outside the game: the name lookup the hangar asks for now reaches the database whole, with its closing bracket; before, it was cut off and rejected. Not tested in game yet.

**Why.** After the rebuild with `0026` to `0028`, the Ship Hangar and Journal windows each showed once and then stayed empty. Two client errors were behind it.

**What changed**
- **Ship Hangar.** `0028` changed how the server looks up the names of ships, locations and other objects. The list of item IDs it put in the query still had some invisible padding (NUL characters) left over by an old EVEmu helper, and the padding cut every lookup off before its closing bracket, so the database rejected all of them. The client got nothing back, raised an error and stopped drawing the hangar (the cargo hold too). The helper now trims the padding.
- **Long lookups.** The server's database query function only had room for 4,096 characters and sent anything longer with junk on the end. `0028` merged three name lookups into one, which makes long ones more likely (a big hangar or asset list), so long queries are now sent in full.
- **Journal.** The Journal's Incursions tab got each incursion's state as a number the client doesn't use. `0024` guessed "our number plus one", but the client counts Withdrawing 0, Mobilizing 1, Established 2. The running incursion is withdrawing, so it came through as 3 and the tab hit an error. The Journal reopens on the last tab you used, so the whole window stayed blank after that. The state is now sent the client's way, which also stops established and mobilizing incursions showing the wrong state.

**Files.** `src/eve-core/utils/utils_string.cpp`, `src/eve-core/database/dbcore.cpp`, `src/eve-server/system/cosmicMgrs/IncursionMgr.cpp/.h`.

**How to test.** Rebuild and Start. Open the Ship Hangar and a cargo hold a few times: the ships and items should be listed every time. Open the Journal on its Incursions tab: the incursion should be listed with its state ("Incursion Withdrawing" for the current one).

---

## 2026-10-13: Incursions: Journal tab, acceleration gates and site names

Patch `0028` (on top of `0024`) · Status: **Built**. In a local test with the test bot, the site beacons arrived named after their sites ("Override Transfer Array", "Nation Mining Colony", "Nation Commander Outpost"), and all three acceleration gates were still on their beacon's grid 90 seconds after spawning; before the fix they were gone after a minute. The Journal fix matches the rejected call in the server console. Not tested in game yet. In game (13 Oct 2026) it broke the Ship Hangar, and the Journal tab still failed on the incursion state; `0029` fixes both.

**Why.** The first in-game look at `0024` (12 Oct 2026) found three problems: the Journal's Incursions tab didn't show the incursions, the site beacons had no acceleration gate, and every beacon was named "Celestial Beacon".

**What changed**
- **Journal tab.** The server console showed it rejecting the client's `rewardMgr.GetDelayedRewardsByGroupIDs` call. The Crucible client sends the incursion IDs as a tuple, and the server only accepted a list, so the tab got an error back and showed nothing. It now accepts a tuple, a list or a single ID. This also confirms the `0024` guess that the client asks a service named `rewardMgr`.
- **Acceleration gates.** Each gate spawned in the right place, but about a minute later it dropped out of its grid. This is an EVEmu bug, also in upstream `master`: once a minute the server checks each grid for ships that have flown off it, and it threw out anything that can't move (acceleration gates, beacons, site structures, deployables) while those objects still counted themselves on that grid. Anyone arriving after that minute saw the beacon but not the gate. Objects that can't move now stay on their grid. Mission acceleration gates and other site structures were probably hit by the same bug.
- **Beacon names.** The server sent no name with beacons, gates and other plain space objects, so the overview showed the type name instead. It now sends each object's own name: a beacon shows its site's name and a gate shows "Acceleration Gate".
- **Name lookups.** A second EVEmu bug: when the client asked for the names of map objects, spawned objects and asteroids in one request, the server only answered for the last kind it looked up, so some names could stay blank. It now answers for all of them.

**Files.** `map/RewardMgrService.cpp/.h`, `system/cosmicMgrs/IncursionMgr.cpp/.h`, `system/SystemBubble.cpp`, `system/SystemEntity.cpp`, `config/ConfigDB.cpp` (all under `src/eve-server/`).

**How to test.**
1. Rebuild and Start. Use the running incursion, or `/incursion start here`.
2. In a Vanguard, Assault or Headquarters system, the overview should list the site beacons by their site names.
3. Warp to one. An Acceleration Gate should sit next to the beacon. Wait a couple of minutes, warp off and come back: the gate should still be there.
4. Use the gate to reach the first pocket.
5. Open the Journal's Incursions tab: each incursion should be listed with its staging system, state and Sansha influence.

---

## 2026-10-13: Leaving a fleet takes you out of it

Patch `0027` · Status: **Built** (compiles). Not tested in game yet.

**Why.** Leaving a fleet didn't take the character out of it: they were left stuck in "fleet limbo". This is an EVEmu bug, also in upstream `master`. The fleet change in `0025` wasn't involved.

**What changed**
- **Leaving.** On leaving, the server set your fleet, wing, squad and fleet role to 0. Out of a fleet the client expects "none", the value it has at login, and it checks for "none" to decide whether you are in a fleet, so a fleet of 0 still counted as one. Leaving, being kicked and logging off now set them back to "none".
- **Commander slots.** When a fleet, wing or squad commander left, the server only freed the commander slot if they were also that unit's booster. A commander who wasn't the booster stayed in the slot after leaving or logging off, so nobody else could take command, and the boost code kept reading a pilot who was gone, which could crash the server. The slot is now freed for whoever leaves.
- **Fleet finder.** An advert posted by a pilot who leaves is taken down, because the fleet finder reads that pilot for every listing.
- **Boosts.** Your own fleet boost now ends when you leave, instead of at your next dock, undock or jump.
- Kicking a pilot who has already gone offline can no longer crash the server.

**Files.** `Client.cpp`, `fleet/FleetService.cpp` (under `src/eve-server/`).

**How to test.** Two characters: one forms a fleet and invites the other. The member leaves the fleet. Their fleet window should close, the fleet commander should see them drop off the member list, and the member should be able to join or form another fleet. Then try it the other way round, with the fleet commander leaving.

---

## 2026-10-13: Titan and Black Ops bridges show their portal

Patch `0026` · Status: **Tested** (13 Oct 2026): the bridge effect works, and bridging should be fully working.

**Why.** Bridging worked, but no portal appeared in space. The client log showed the server announcing the portal as effect number 0 with no effect name, so the client had nothing to play ("FxSequencer::OnSpecialFx: No guid in trigger") and threw a `RecordNotFound 0` error in its module code.

**What changed**
- **Portal effect.** The server started the jump portal generator with the module's default effect. In the game data the portal effect isn't marked as the default, because the module button doesn't open a portal, so that came out as 0. The generator now always runs its portal effect: `jumpPortalGeneration` on a titan, `jumpPortalGenerationBO` on a Black Ops. The client then plays its own portal (`effects.JumpPortal`, a 60 second animation in `res:/Model/Effect3/jumpportal.red`), and the module shows as active for that minute.
- **Black Ops portals.** The same mistake meant a Black Ops covert portal was never recognised as covert, so it couldn't open to a covert cyno and anything could use it like a titan bridge. It can now open to a covert cyno, and only covert ops ships, stealth bombers and force recon ships can jump through it, as the bridge rules from `0017` intended.

**Files.** `ship/modules/JumpPortalModule.cpp`, `ship/modules/JumpPortalModule.h` (under `src/eve-server/`).

**How to test.** The same setup as `0022`: a cyno pilot in the target system, a titan elsewhere with fuel, both in one fleet. Use "Bridge to" on the cyno pilot. The portal should appear in front of the titan for a minute, and the generator's module button should show it cycling. For Black Ops: a covert cyno on a Force Recon or Covert Ops ship, "Bridge to" from the Black Ops, then a stealth bomber should be able to jump through and a non-covert ship shouldn't.

---

## 2026-10-12: Undocking as fleet commander no longer crashes the server

Patch `0025` · Status: **Tested** (12 Oct 2026): undocked several times as fleet commander with two alts in the fleet, and the server stayed up.

**Why.** The server crashed (segfault) the moment the fleet commander of a three-pilot fleet undocked. This is an EVEmu bug, not one of our patches: the same code is in upstream `master`.

**What changed**
- **Undocking.** When a fleet booster undocks, the server works out the fleet's boosts again. It counted the undocking pilot as already in space, but their ship isn't made until a moment later in the undock, so applying the boost to it read a null pointer. The boost update now skips a pilot with no ship yet; their own fleet timer applies the boost 1.5 seconds after undock, as it already did for pilots who aren't boosters.
- **Docked squad members.** The same update also handed a boost to every squad member in the booster's system, docked or not, and to a squad with no booster at all. Either would have crashed the same way. Docked members and squads without a booster are now skipped.
- The wing commander's own boost has the same check.

**Files.** `fleet/FleetService.cpp` (under `src/eve-server/`).

**How to test.** Make a fleet with you as fleet commander and two alts in it, all docked in the same station. Undock the commander, then an alt, then dock one alt and undock again. The server should stay up. Optional: give a squad commander the booster role and dock or undock squad members.

---

## 2026-10-12: Incursions: overview beacons, acceleration gates, the wiki's site mechanics, Sansha EWAR and the Journal tab

Patch `0024` (on top of `0021`) · Status: **Built**. In a local test without a game client, every one of the eleven sites spawned with its overview beacon, gate and pockets, and ran from its first wave to completion through its own mechanic: Lyavite into the refinery, civilians into the freighter, MTACs into the control tower, the Citizen kill, the Consolidation Network's gate locks, the Provisional Headquarters' hidden gates and Mobile Headquarters, and the mothership's transmitter and relay. Killing the mothership ended the incursion. Not tested in game: flying through the gates, hacking, mining spawns, how the rats fly and use EWAR, the Journal tab and payouts. First in-game look (12 Oct 2026): the beacons appear but have no acceleration gate, the Journal tab doesn't update, and the beacon names are still wrong (see [Known issues](#known-issues)). Fixes for all three built in `0028` (13 Oct 2026).

**Why.** The rats fought but flew wrong, the sites worked nothing like their wiki pages, they were scanner anomalies instead of overview beacons, and the Journal's Incursions tab was empty. Sources: the EVE University wiki's [Incursion sites](https://wiki.eveuniversity.org/Incursion_sites) and each site's own page, [Sansha's Manual](https://wiki.eveuniversity.org/Sansha%27s_Manual), [Incursions](https://wiki.eveuniversity.org/Incursions) and [Anatomy of Incursions](https://wiki.eveuniversity.org/Anatomy_of_Incursions). Where the wiki says nothing (gate distances, structure hit points, timers, ship counts), the values are our picks.

**What changed**
- **Sites are beacons on the overview**, visible to everyone in the system without scanning; they are no longer scanner anomalies. Warp to one and you land next to its acceleration gate (the Consolidation Network has two). A gate works within 2,500 m of its surface, but not while you are warp disrupted. It puts you in the site's pocket, about a million km away, next to the pocket's own beacon. The pocket's ships spawn when the first pilot arrives. A completed site's beacon disappears at once. Its gate and pockets go a minute after the last pilot has left, or 30 minutes after completion at the latest.
- **Each site works like its wiki page:**
  - **Nation Commander Outpost:** 4 waves at 35 to 40 km. Wave 1 has a Romi, wave 3 the Commander, and wave 4 is frigates only. The roster no longer includes Maras or Deltoles.
  - **Nation Mining Colony:** 3 waves at 70 to 75 km, with the Mara in wave 1. Clearing wave 2 decloaks the Nation Ore Refinery by the Asteroid Colony. A Lyavite rock sits about 60 km out. Each time someone mines it, there is a chance that Orkashu, Raa and Youl ships turn up. Putting 255 Lyavite in the refinery destroys the Battletower and completes the site.
  - **Override Transfer Array:** the Remote Logistics Station repairs the three most damaged Sansha ships near it every 5 s. Hacking one of the three Logistics Control Arrays with a data analyzer stops it for 6 minutes; each array works once. Waves come 70 km right of the station, then 70 km left, then above it, with one Deltole each. Eysturs respawn in front of the station and don't count towards anything.
  - **Nation Commander Stronghold:** the Snake Shaped Asteroid and two L-shaped buildings. The five waves are where the wiki puts them: Yulais at 160 km, Ostingeles at 60, Antems at 150, Arnons at 20, and one random Citizen or Slave at 50 km with Outunis and a Vylade. Killing the Citizen or Slave completes the site, and the rest warp off.
  - **Nation Consolidation Network:** the beacon has a cruiser gate (cruisers, battlecruisers, destroyers, frigates, logistics and industrials) and a battleship gate (battleships, mining barges and large industrials), checked against the wiki's class lists when `GateRestrictions` is on. It is off by default, so any ship, supercarriers included, can take either gate. Each side's pocket has a Fleet Staging Area gate 50+ km from the landing. It opens when the other side's pocket is clear. The staging area has snipers out to 150 km.
  - **Overwhelmed Civilian Facility:** waves come ahead of you at the wiki's distances. Wave 2's Antems don't hold up the next wave. The last ship of each wave drops a wreck with 15 Captured Civilians. Once every Deltole and Auga in wave 4 is dead, plus either its Romis and Renyns or its Antems, the Obelisk becomes the Emergency Evacuation Freighter. Putting 40 civilians in it completes the site.
  - **Nation Rebirth Facility:** 4 waves by the facility, which can't be destroyed. Part of wave 4 spawns about 100 km from the rest.
  - **True Creations Research Center:** the Battletower is the objective, and its shield transfer arrays put it back to full health every second. The CONCORD MTAC Factory holds one MTAC at a time and makes a new one 20 s after it is taken. Each MTAC dropped into the Shield Transfer Control Tower stops the repairs for 90 s *(our pick; the wiki says "a short period")*. A few Schmaeels are there at first, and the two by the control tower respawn. The first uncloaked pilot brings Deltoles after 5 s, frigates after 45 s and Outunis after 90 s. Frigate and cruiser groups respawn 60 to 90 s after they are wiped out; battleship groups take 15 minutes. An MTAC is 7,500 m³, as the wiki says it is too big for any battleship hold: it needs a hauler, or a jetcan and a tractor beam.
  - **True Power Provisional Headquarters:** 3 pockets. Each pocket's gate onward appears once the pocket is clear. In the last pocket, each new wave comes when the last Ostingele of the previous one dies. When every ship there is dead, the headquarters structure is replaced by the destructible True Power Mobile Headquarters, and destroying it completes the site.
  - **The Kundalini Manifest and Uroborus:** the mothership is in the pocket behind the gate, 50 km from the landing. Escorts and groups of ten Lirsautton bombers arrive every 1 to 5 minutes. Hacking the Logistics Control Array, 120 km ahead, stops Sansha logistics repairing each other for 6 minutes, and turns them onto your fleet instead. The Central Control Nexus makes a Tactical Response Transmitter every 2 minutes. Dropping it into the Sansha Control Relay, 15 km behind the beacon, sends a third of the escorts away (Maras first) for 2 minutes.
- **How Sansha ships fly.** Each ship orbits at its own range from Sansha's Manual and at its listed orbit speed. To close in or chase, it runs its propulsion at the listed top speed, with the same signature bloom a player's microwarpdrive has. It turns propulsion off near its orbit. Ships pick targets among the pilots on grid rather than all on the first one, and switch now and then.
- **Sansha EWAR, from Sansha's Manual:**
  - Warp disruption: Schmaeel, Tama, Auga, Deltole, Outuni and the Commander.
  - Stasis webs: Renyn, Schmaeel, Auga and Outuni.
  - Energy neutralizers: Niarja, Deltole, Outuni and the motherships.
  - ECM jams: Niarja and Arnon. A jam breaks your locks and blocks new ones while it lasts.
  - Target painters: Deltole.
  - Remote shield repairs: Mara (70 km), Intaki (25 km) and the motherships (52 km), up to 3 ships at a time.
  - The Vylade Dien gives every Sansha ship in its pocket +20% shield resists while it lives.
  - The motherships' remote ECM burst goes off every 1 to 5 minutes and reaches 60 km *(our pick; the wiki gives no range)*.
  - A ship that dies or warps off lets go of its points, webs and painters.
- **The Journal's Incursions tab** lists each incursion with its staging system, state (Established, Mobilizing or Withdrawing), Sansha influence, whether the mothership is out, and the LP reward so far. The starmap's incursion view shows the incursion's systems.
- **LP is paid the wiki way.** ISK is still paid when a site completes. LP goes into the incursion's pool, and the whole pool is paid when the mothership dies. If the incursion withdraws with the mothership alive, the pool is lost. The `DelayedLP` setting turns this off.
- **When the mothership appears.** The mothership comes when a Headquarters site is completed at zero influence and the incursion is old enough: 3 days in high-sec, 1 day in low-sec, and no wait in null-sec. `/incursion mothership` skips the wait. Influence no longer recovers once a Headquarters site has been run at zero. The Kundalini Manifest appears in high-sec and null-sec, and the Uroborus in low-sec only, as on the wiki.
- **More sites per system:** up to 7 Vanguard, 5 Assault and 5 Headquarters sites at once (the wiki's limits).
- **Objectives and containers.** The Battletower is the Crucible data's "Sansha's Nation Central Bastion" with 1.5 million EHP, and the Mobile Headquarters is "'True Power' Assembly Security HQ" with 1 million EHP. Both are stationary and unarmed *(our picks)*, and both are named as the wiki names them. The Shield Transfer Control Tower and the Sansha Communications Array are containers again, not ships. Every site container has 1 million hit points, so it can't be shot away by accident.
- **EVEmu bugs fixed along the way:**
  - Acceleration gates outside incursions warped you to the wrong distance: the warp range was set to the length of the jump.
  - A container in space listed only the items you owned, so loot or site items owned by others were invisible.
  - Mission containers loaded as plain items instead of containers.
  - A successful data analyzer cycle on a container that wasn't a wreck tried to turn it into a loot can.
  - A stasis web was lost whenever the webbed ship's speed was recalculated, for example when its propulsion module cycled.
  - The Sansha supercarrier wreck's type ID is outside the range the server treated as wrecks, so motherships left no wreck.

**Settings** (`<incursion>` in `eve-server.xml`):
- `VanguardSites` (7), `AssaultSites` (5) and `HQSites` (5) replace `SitesPerSystem`.
- `DelayedLP` (true) pays LP from the pool when the mothership dies; false pays it with each site.
- `GateRestrictions` (false) makes the Consolidation Network's gates check ship class when set to true. It is off by default because capitals fit neither side's list.
- `MothershipDelayHigh` (72) and `MothershipDelayLow` (24) are the hours before the mothership can appear.

**Database.** New migration `sql/migrations/20261012180000-incursions-sites.sql`, applied on top of the first incursions migration when the container starts. It adds rooms, roles and names to the site objects and lays out every site again. It also adds the mothership flags and the LP pool table, objective and container stats, and the logistics and ECM burst ranges. Its Down part goes back to the `0021` data.

**Files.** New: `system/cosmicMgrs/IncursionSites.cpp` (pockets, gates, waves and each site's script), `npc/NPCEwar.cpp` (Sansha flight, EWAR and repairs), and `map/RewardMgrService.cpp/.h` (the Journal's LP column). Changed: `system/cosmicMgrs/IncursionMgr.cpp/.h`, `IncursionDB.cpp/.h`, `npc/NPCAI.cpp/.h`, `npc/NPC.cpp`, `npc/DroneAI.cpp`, `system/KeeperService.cpp`, `system/SystemManager.cpp/.h`, `system/DestinyManager.cpp/.h`, `system/Container.h`, `inventory/Inventory.cpp`, `inventory/InventoryItem.cpp`, `ship/modules/Prospector.cpp`, `ship/modules/MiningLaser.cpp`, `map/MapService.cpp`, `EVEServerConfig.cpp/.h`, `eve-server.cpp` and `CMakeLists.txt` (all under `src/eve-server/`). Also `src/eve-common/EVE_Defines.h`, `utils/config/eve-server.xml` and the new migration.

**How to test.**
1. Rebuild and start. The new migration runs on its own.
2. Run `/incursion start here`, then `/incursion info` to see the systems.
3. Fly to a Vanguard system. The sites are on the overview as beacons, with no scanning needed. Warp to one at 0, take the gate, and fight the pocket.
4. Open the Journal's Incursions tab.
5. For the mothership, run `/incursion mothership here`.

**Not done yet.**
- The constellation-wide resist and damage penalties.
- Staging sites.
- The mothership's True Sansha loot.
- The "most damage" rule between competing fleets: pay still goes to the pilots on grid.
- Two guesses that need checking in game: the Journal's state names may be off by one, and the LP column relies on the client asking a service named `rewardMgr`.

---

## 2026-10-12: Turret hit chance follows CCP's formula

Patch `0023` · Status: **Compiles** (the changed file builds; the server wasn't booted with it). Not tested in game yet.

**Why.** CCP open-sourced its Carbon engine in 2026. Its `destiny` module includes CCP's own turret hit-chance function, `Ballpark::GetAccuracy()` ([carbonengine/destiny](https://github.com/carbonengine/destiny), `src/Ballpark.cpp`, unchanged since its first 2014 snapshot). EVEmu's player, NPC and sentry turret maths differed from it in three ways.

**What changed** (player turrets; NPC turrets, Sansha incursion NPCs included; NPC sentry guns)
- **Tracking counts only sideways speed.** Angular velocity is now the part of the relative velocity across the line between the two ships, divided by the centre-to-centre distance. Before, the whole relative speed counted, so flying straight at or away from a target spoiled tracking.
- **Range is surface to surface** (centre distance less both radii) for the falloff part. Before, it was centre to centre, which put big ships and structures further out than they were.
- **No signature damage cut.** EVEmu had its own rule: when a gun could track but its signature resolution was bigger than the target's signature, it ignored signature for the hit roll and multiplied the damage by target signature ÷ signature resolution, so a large turret did about a tenth of its damage to a slow frigate. Signature now only scales the tracking part, as in CCP's code, and a hit does normal damage.
- **A wrecking shot has to hit first.** A roll under the crit chance used to count as a wrecking shot even when the shot should have missed.
- A weapon with no falloff in its data hits normally inside optimal and misses outside it. A weapon with no tracking never hits, as in CCP's code. Drones already used this formula and are unchanged.
- The `DAMAGE__TRACE` and `DAMAGE__TRACE_NPC` log lines now show range, angular velocity, chance and roll.

**Files.** `ship/modules/TurretFormulas.cpp` (under `src/eve-server/`).

**How to test**
1. In a battleship with large turrets, shoot a slow or webbed frigate inside optimal. Hits should do normal damage (before: about a tenth).
2. Approach an NPC battleship head-on (approach, not orbit). Its guns should hit you about as often as when you sit still, instead of missing.
3. Optional: turn on the `DAMAGE__TRACE` / `DAMAGE__TRACE_NPC` log types to see each shot's chance and roll.

---

## 2026-10-12: "Bridge to" opens the jump portal; cyno ships stay put

Patch `0022` · Status: **Tested** (12 Oct 2026): drive jumps to a fleet cyno, Bridge to and Jump through all work; the portal visual effect is missing, shelved. (`0020` and `0021` are the incursion patches.)

**Why.** In testing, the titan jumped to the cyno fine, but "Bridge to" did nothing: no message, no effect, and the other ship had no "Jump through" option on the titan.

**What changed**
- **Bridges.** The server started the jump portal generator, then wrongly read "this runs for one cycle" as "it didn't start", and quietly gave up. It now opens the portal and tells the fleet, so members get "Jump through" on the titan or Black Ops. If the generator really doesn't start, the pilot now gets a message saying so.
- **Cyno ships.** The same mistake in the cyno meant the ship was never held in place while the field was up, and when the field went out the ship's top speed was set to 0. The ship is now held still while the cyno is lit and gets its normal speed back afterwards.

**Not done.** The portal's visual effect may still be missing; that needs the client's effect data checked.

**Files.** `ship/modules/JumpPortalModule.cpp`, `ship/modules/CynoModule.cpp`, `ship/BeyonceService.cpp` (under `src/eve-server/`).

**How to test.** Three characters in one fleet: a cyno pilot in the target system, a titan (or Black Ops with a covert cyno) elsewhere with fuel in its fuel bay, and a subcapital next to the titan. Light the cyno; on the titan, right-click the cyno pilot in the fleet window and pick "Bridge to". You should get "Jump portal open". Fly the subcapital within 2,500 m of the titan and right-click the titan for "Jump through". Also check the cyno ship can't move while the cyno is lit and moves normally after.

---

## 2026-10-12: Incursions: Assault, Headquarters and Mothership sites

Patch `0021` (on top of `0020`) · Status: **Built**. In a local test without a game client, an incursion fills its Vanguard, Assault and Headquarters systems with sites, runs through every wave including the objective structures, brings out the Mothership after a Headquarters site at zero influence, and ends when the Mothership dies. Low-sec gets the Uroborus instead of the Kundalini. Everything is cleaned up when a system unloads or the incursion ends. Not tested in game: combat, payouts and the scanner view.

**Why.** The first cut only had Vanguard sites. The Assault and Headquarters sites are built from the EVE University wiki ([Incursion sites](https://wiki.eveuniversity.org/Incursion_sites), its pages for each site, and [Sansha's Manual](https://wiki.eveuniversity.org/Sansha%27s_Manual) for the ships).

**What changed**
- **All nine sites from the wiki.** Each system type now has its own three sites, three live at a time per system:
  - Vanguard (10 pilots): Nation Commander Outpost, Nation Mining Colony, Override Transfer Array.
  - Assault (20 pilots): Nation Commander Stronghold, Nation Consolidation Network, Overwhelmed Civilian Facility.
  - Headquarters (40 pilots): Nation Rebirth Facility, True Creations Research Center, True Power Provisional Headquarters.
  - "Nation Consolidation Networks" was a Vanguard site in `0020`; the wiki lists it as an Assault site, so it moved.
- **Waves follow the wiki where it says something.** The ship rosters, the number of waves and the distances come from the wiki: for example, the Stronghold's Yulai snipers at 160 km and the Citizen Astur and Slaves wave at 50 km; Override Transfer Array has one Deltole per wave; the Overwhelmed Civilian Facility has its waves at 35, 75 and 95 km and one Outuni. Live randomises how many of each ship spawn and doesn't publish it, so the counts here are fixed picks sized for each site's fleet.
- **Objective structures.** True Creations Research Center finishes when its battletower falls (it comes in the last wave with guards). True Power Provisional Headquarters finishes when its headquarters structure is destroyed (it appears on its own after the last ship dies). The Crucible data has neither structure, so the Shield Transfer Control Tower and the Sansha Communications Array stand in. Their hit points (500,000 EHP) are a guess.
- **Rewards from the wiki.** High-sec pays per pilot, up to the site's full fleet: Vanguard 10,395,000 ISK and 1,400 LP; Assault 18,200,000 ISK and 3,500 LP; Headquarters 31,500,000 ISK and 7,000 LP; Mothership 63,000,000 ISK and 14,000 LP. Low and null-sec pay 10/7 of that, as on the wiki (15M, 26M, 45M and 90M). Bigger fleets split the pool, which is close to the wiki's reduced payouts (about 91% of full for 11 pilots in a Vanguard, against the wiki's 92.5%).
- **The Mothership.** When Sansha influence is at zero and someone completes a Headquarters site, the Kundalini Manifest (high-sec) or the Uroborus (low/null-sec) appears in the Headquarters system and the incursion starts withdrawing. It has the wiki's 14 million EHP. Every 1 to 5 minutes while it lives, a new wave of escorts and Lirsautton bombers arrives. Destroying it pays the pilots on grid and ends the incursion. If nobody kills it within 24 hours, the incursion ends on its own. Influence at zero no longer ends the incursion by itself.
- **Sansha ships use the wiki's numbers.** Signature radius, speed, orbit distance and speed, damage per second, volley, weapon range, hit points and resists now match the table on the wiki's [Sansha's Manual](https://wiki.eveuniversity.org/Sansha%27s_Manual) for all 25 ships it lists, including Citizen Astur and the Slaves, which share Deltole's numbers. Turrets do EM and thermal in the wiki's split, and their falloff is read the wiki's way (the listed range runs from optimal to optimal plus twice the falloff). Torpedo ships fire kinetic or explosive torpedoes with the wiki's volley, rate of fire and range. They no longer have shield boosters; their tank is plain hit points and omni resists. The exception is the Lirsautton bomber, which is weaker to thermal and stronger to explosive, as the wiki says. The wiki gives only total hit points, so they are split 60% shield, 25% armor and 15% hull. Turret tracking is still a guess, because the wiki doesn't list it.
- **Sansha's Nation Commander** (the Nation Commander Outpost's third wave, 1,250 DPS torpedoes) isn't in the Crucible data. An unused frigate type stands in, so it shows up as "Sansha" in space.
- **Settings.** `FleetSize` is replaced by `FleetScale`, in percent of each site's full fleet (default 100). `50` halves every wave, and `10` gives a Vanguard about one pilot's worth of ships. The payout scales the same way. `MobilizeAfter` is now 120 hours (the wiki's five days established). `WithdrawTime` is now 1440 minutes (24 hours), which is also how long the Mothership stays.
- **GM command.** `/incursion mothership [here]` breaks influence and brings out the Mothership straight away, for testing.

**Database.** The incursions migration from `0020` is replaced in full (new stats, all eleven sites, two new columns on `incSiteTypes`). If you already ran `0020`'s migration, run its Down part before starting the container.

**Files.** Changed `system/cosmicMgrs/IncursionMgr.cpp/.h`, `system/cosmicMgrs/IncursionDB.cpp`, `admin/IncursionCommands.cpp/.h`, `npc/NPCAI.cpp`, `EVEServerConfig.cpp/.h` (all under `src/eve-server/`), `sql/migrations/20261012120000-incursions.sql` and `utils/config/eve-server.xml`.

**How to test.** `/incursion start here` in a high-sec system with a station, then `/incursion info` to see which systems are Vanguard, Assault and Headquarters. Each has three combat anomalies. For the Mothership, run `/incursion influence 0 here` and complete a Headquarters site, or use `/incursion mothership here`, then warp to the new anomaly in the Headquarters system.

**Not done yet.** Most Sansha EWAR: only warp disruption works for NPCs in EVEmu. Webs, jams, neuts, painters, remote repairs and the Vylade's resist bonus are in the ship data but nothing uses them yet. Also missing: the acceleration gates and rooms in the multi-room sites, the Mining Colony's mining-triggered spawns, the system penalty, and the client screens.

---

## 2026-10-12: Incursions, first playable cut

Patch `0020` · Status: **Built**. Patch `0021` replaces the stand-in ship stats, the site list and `FleetSize` described here. The server boots and, in a local test without a game client, starts an incursion, spawns sites, moves through all three waves, respawns sites and cleans everything up when the incursion ends. Not tested in game: payouts, combat and the scanner view.

**Why.** EVEmu had almost nothing for incursions. This is the first playable part: Vanguard sites you can find, fight and get paid for.

**What changed**
- **Incursions.** Sansha's Nation takes over a high-sec constellation. One system with a station is the staging system and also a Vanguard system, the two nearest systems are Vanguard systems too, the farthest is the Headquarters system and the rest are Assault systems. Only Vanguard systems have sites so far.
- **Starting one.** With the default settings an incursion starts on its own five minutes after the server boots, and again an hour after the last one ends. Every online pilot gets a notice naming the constellation and staging system. GMs can use `/incursion start` (random), `/incursion start here`, or `/incursion start <constellationID>`.
- **Vanguard sites.** Each Vanguard system keeps three sites, one of each type: Nation Mining Colony, Override Transfer Array and Nation Consolidation Networks. They show on the ship's scanner as combat anomalies and you can warp to them or bookmark them. Each site has Sansha structures and three waves of Sansha ships; the next wave appears when the current one is destroyed. A replacement site appears three minutes after one is cleared.
- **Rewards.** Clearing the last wave pays every pilot on grid ISK from CONCORD (journal entry "Incursion site completed") and CONCORD loyalty points. A full-size site pays about 10.5 million ISK and 1,050 LP per pilot for a fleet of up to 10; bigger fleets split it. When `FleetSize` scales a site down, the payout is scaled the same way, so a solo pilot on solo-sized sites gets about the full per-pilot amount. Sansha ships pay no bounty.
- **Influence.** Each cleared site removes 5% of Sansha influence (20 sites to break it). Influence slowly comes back (1% an hour) if nobody clears a site for 30 minutes. At zero influence, or after 7 days, the Sansha withdraw: no new sites for an hour, then the incursion ends.
- **Sansha ships have stats now.** The 25 Sansha incursion combat ships had no hit points, damage or speed in the data. They now have **stand-in** stats built from the True Sansha and Guristas NPCs: lasers (EM and thermal) or missiles, shield tanks that are weakest to EM and thermal, and a shield booster. Roughly 4,300 / 12,500 / 34,000 hit points and 40 / 90 / 220 damage per second for frigates, cruisers and battleships. These are guesses to tune, not CCP's numbers. They don't use their EWAR or remote repairs yet.
- **Fleet size setting.** New settings in a new `<incursion>` section of `eve-server.xml` (defaults apply if it's missing): `FleetSize` (default 10, full-size sites) shrinks each wave to roughly that many pilots' worth of ships, so `1` gives solo sites with one to three ships per wave; `Strength` (default 100) scales Sansha hit points and damage in percent. Also `Enabled`, `AutoStart`, `MaxActive`, `SitesPerSystem`, `SiteRespawn`, `MinSecurity`, `StartDelay`, `MobilizeAfter`, `Lifetime`, `WithdrawTime`, `RegenDelay` and `RegenRate`.
- **GM commands.** `/incursion info` lists active incursions, their systems and live sites. `/incursion influence <0-100> [here]` sets influence. `/incursion end [here]` ends one or all.
- **Fixes along the way.** NPCs with missiles only no longer fire a fake zero-damage laser. Live dungeon items are now removed when their system unloads, instead of reloading as orphans the next time it boots.

**Database.** New migration `sql/migrations/20261012120000-incursions.sql` (the stand-in ship stats, Sansha wrecks, site definitions and the tables that save incursions). It's applied when the container starts.

**Files.** New `system/cosmicMgrs/IncursionMgr.cpp/.h`, `system/cosmicMgrs/IncursionDB.cpp/.h`, `admin/IncursionCommands.cpp/.h`; changed `EVEServerConfig.cpp/.h`, `EntityList.cpp`, `eve-server.cpp`, `system/SystemManager.cpp`, `system/cosmicMgrs/AnomalyMgr.cpp/.h`, `system/cosmicMgrs/ManagerDB.cpp`, `npc/NPCAI.cpp`, `admin/AllCommandsList.h`, `CMakeLists.txt` (all under `src/eve-server/`), `src/eve-common/EVE_Incursion.h` and `utils/config/eve-server.xml`.

**How to test.** Log in as a GM, run `/incursion start here` in a high-sec system with a station, or `/incursion info` to find the automatic one. Fly to a Vanguard system, open the scanner and warp to a site. Kill all three waves and check the wallet journal, the CONCORD LP, and `/incursion info` for the influence drop.

**Not done yet.** Sansha EWAR and remote repairs, Assault and Headquarters sites, the Mothership, the system penalty, the end-of-incursion LP bonus, CONCORD LP exchange, and the client screens (journal tab, starmap, influence bar).

---

## 2026-10-11: Cynos refused in low-sec as "high security space"

Patch `0019` · Status: **Tested** (11 Oct 2026): a cyno lights in a 0.4 system.

**Why.** After `0018`, a cyno in a 0.4 system said the module can't be used in high security space.

**What changed**
- The high-sec check read the server's "security value", which is 1.1 minus the system's security (0.1 in a 1.0 system, 0.7 in a 0.4 system), so it was upside down: cynos were refused in low-sec and null-sec and allowed in high-sec. It now reads the system's security itself. Anything that shows as 0.5 or above in the client (0.45 and up) counts as high-sec.
- POS jump bridges had the same upside-down check when onlining; fixed the same way.

**Files.** `ship/modules/CynoModule.cpp`, `pos/JumpBridge.cpp` (under `src/eve-server/`).

**How to test.** Light a cyno in a 0.4 or lower system (in a fleet, Liquid Ozone in the cargo hold); it should light. In a 0.5 or higher system it should still refuse, unless `highSecCyno` is on in `eve-server.xml`.

---

## 2026-10-11: Cynos no longer "jammed" outside claimed null-sec

Patch `0018` · Status: **Tested** (11 Oct 2026): no more "jammed" message, and a cyno lights in low-sec.

**Why.** The first cyno test (11 Oct 2026) got "This system is currently being jammed" in a system with no jammer.

**What changed**
- When a system had no sovereignty data (all of empire space, including low-sec), the server handed back uninitialised data, so its "jammer" was random garbage. It now comes back empty, so cynos only see a jammer that is really there.
- The same garbage could also upset jump bridge onlining and structure anchoring checks; those are fixed by the same change.

**Files.** `system/sov/SovereigntyDataMgr.cpp` (under `src/eve-server/`).

**How to test.** Light a cyno in low-sec (in a fleet, Liquid Ozone in the cargo hold). In high-sec it should now say the module can't be used in high security space instead.

---

## 2026-10-11: Cynos, jump drives, and titan and Black Ops bridges

Patch `0017` · Status: **Tested** (12 Oct 2026): drive jumps to a fleet cyno, Bridge to and Jump through all work; the portal visual effect is missing, shelved. On 11 Oct "Bridge to" did nothing; that was fixed in `0022`.

**Why.** Cynos and capital jumps half-worked: no range limit, no capacitor cost, a likely server crash when the cyno went out just before a jump, and too much fuel taken when isotopes were split across stacks. Covert cynos couldn't be fitted and titan or Black Ops bridges weren't there at all.

**What changed**
- **Jump to a fleet member's cyno:** the server checks the cyno is still lit and its pilot is still in your fleet, so a cyno going out mid-click is just "no longer has a cynosural field lit" instead of a crash.
- **Range:** jumps beyond your ship's jump drive range (with Jump Drive Calibration) are refused with the distance and your range.
- **Capacitor:** a jump needs and uses the ship's jump capacitor cost (75% of capacitor on capitals, less with Jump Drive Operation).
- **Fuel:** taken correctly from several isotope stacks. The amount is unchanged (distance, Jump Fuel Conservation, and Jump Freighters in a jump freighter).
- **Cyno:** lasts 10 minutes flat (Survey skill no longer makes it longer), and the ship stops when the cyno lights.
- **Covert cyno:** the Covert Cynosural Field Generator can be fitted and lit. Cyno jammers don't stop it. Only Black Ops can jump to it.
- **Late joiners:** someone who joins the fleet after a cyno is lit, or a portal opened, is told about it, so they get Jump to and Jump through too.
- **Titan and Black Ops bridges:** "Bridge to" on a fleet member with a cyno (or an alliance beacon) opens a jump portal for one cycle of the Jump Portal Generator. The bridging ship is held in place while the portal is open, and the jump must be within its jump drive range. Fleet members within 2,500 m of it get "Jump through", which takes fuel from the bridging ship's fuel bay by the jumper's mass and the distance.
  - A titan portal won't take capitals, Black Ops or freighters, and can't open to a covert cyno.
  - A Black Ops covert portal only takes covert ops, stealth bombers and force recons.
- **Not done:** jumping through an alliance member's portal when not in their fleet (replies "not supported yet"); strategic cruisers with the covert subsystem can't use covert portals; the 2,500 m portal range is from memory of Crucible and easy to change; the cyno's speed hack (restores the top speed recorded when it lit) is unchanged until the cyno's speed effect can be checked in a test.

**Files.** New: `ship/JumpDrive.cpp/.h` (shared checks), `ship/modules/JumpPortalModule.cpp/.h`. Changed: `ship/BeyonceService.cpp/.h`, `ship/modules/CynoModule.cpp/.h`, `ship/modules/ModuleFactory.h`, `ship/modules/GenericModule.h`, `fleet/FleetService.cpp`, `fleet/FleetBound.cpp`, `CMakeLists.txt` (all under `src/eve-server/`).

**How to test.** Needs two characters online in one fleet (two clients), three for a bridge. Cynos can't be lit in high-sec unless `highSecCyno` is set in `eve-server.xml`.
1. Character A: a ship with a Cynosural Field Generator I and Liquid Ozone in the cargo hold, in low-sec. Character B: a carrier or dread with isotopes in the fuel bay, a few light years away. A lights the cyno; B right-clicks A in the fleet window and picks Jump to.
2. Try a jump out of range, with low capacitor, with too little fuel, and with the cyno switched off just before clicking.
3. Join the fleet with B after A has lit the cyno: B should still get Jump to.
4. Covert: A in a force recon with a Covert Cynosural Field Generator; a Black Ops jumps to it and a carrier is refused.
5. Bridge: B in a titan with a Jump Portal Generator I and isotopes; B uses Bridge to on A. Character C in a subcap within 2,500 m of B uses Jump through on B. If Jump through never shows on B, check the server log for `Handle_CmdBridgeToMember` (the portal request arrived) and any message B got back.

---

## 2026-10-11: Fighter and bomber damage bonuses

Patch `0016` · Status: **Tested** in game with patch `0015` (11 Oct 2026).

**Why.** On Renn's base, fighters and bombers got only the Drone Interfacing bonus. Our own patch `0002` had the rest, but Renn's code replaced it.

**What changed**
- **Fighters** skill: +20% fighter damage per level.
- **Fighter Bombers** skill: +20% bomber damage per level.
- **Thanatos and Nyx:** +5% fighter damage per level of Gallente Carrier. The Nyx gets the same for bombers.
- **Revenant:** +100% fighter and bomber damage (role bonus).
- These follow the game data. Other carriers have no fighter damage bonus in Crucible. The Chimera and Wyvern bonus that patch `0002` gave was wrong; theirs is a shield resistance bonus.
- Bonuses are worked out when the fighters are launched.

**Files.** `npc/Drone.cpp` (under `src/eve-server/`).

**How to test.** Launch Firbolgs from a Thanatos, or bombers from a Nyx, and compare hit damage with the skills trained and untrained (`/giveskill`), or against a Chimera.

---

## 2026-10-11: Fighter bombers hit, and only attack capitals

Patch `0015` · Status: **Tested** in game (11 Oct 2026). Bombers hit capitals every volley and refuse other targets.

**Why.** Fighter bombers missed every shot. Their data has no turret range (no optimal, falloff or tracking), only a missile range, so the drone hit formula counted every target as out of range.

**What changed**
- **Always hit in range.** A bomber's attack hits whenever the target is within its missile range (`entityAttackRange`, 20 km for all four bombers), and misses only when the target is further away.
- **Missile-style damage.** Damage scales with the target's signature radius and speed against the bomber's explosion radius and velocity, as for missiles. Capitals are big and slow, so they take full or nearly full damage.
- **Capital targets only.** Bombers attack only capital ships (carriers, dreadnoughts, supercarriers, titans, freighters, jump freighters, the Rorqual), NPC mission carriers and Sansha incursion capitals, stations, starbase structures and sovereignty structures. Ordering them onto anything else replies "can only attack capital ships, stations and structures", and they won't pick other ships when set to aggressive, guard or assist.
- Bombers orbit at their usual 7.5 km; before, they were pulled in to under 1 km.

**Not done yet.** Skill and carrier damage bonuses: done in `0016`.

**Files.** `npc/Drone.cpp`, `npc/Drone.h`, `npc/DroneAI.cpp`, `npc/EntityService.cpp`, `ship/modules/TurretFormulas.cpp` (under `src/eve-server/`).

**How to test**
1. In a supercarrier (`/create` a Nyx or Hel, plus Cyclops or Tyrfing bombers), launch the bombers.
2. Spawn or find a capital target, lock it and engage. Every volley should hit unless the target is over 20 km from the bombers.
3. Lock a frigate or cruiser and engage: the bombers should refuse with the message above.

---

## 2026-10-11: ECM drones jam

Patch `0014` · Status: **Tested** with the test bot. Waiting for a test in the game client.

**Why.** ECM drones showed their effect but never jammed. EVEmu had no jamming at all, not even for ECM modules.

**What changed**
- **Jam chance.** Every cycle (20 seconds for these drones), an ECM drone tries to jam its target. The chance is the drone's jam strength for the target's sensor type, divided by the target's sensor strength. A Hornet EC-300 (strength 1) against a sensor strength of 9 has an 11% chance; a Vespa EC-600 (1.5) has 17%; a Wasp EC-900 (2) has 22%.
- **A jammed ship or NPC** loses all its locks and can't lock anything until the end of that cycle. NPCs stop shooting until they can lock again. A jammed player trying to lock gets "Your sensors are jammed."
- **Messages.** You get "Your Hornet EC-300 jams the …" when a jam lands. A jammed player is told who jammed them.
- The jamming is built into the server's targeting, so ECM modules could use it later.

**Tested.** With the test bot: two Hornets and a Vespa against a Pithi Arrogator (sensor strength 9). The rolls came out at 11% and 17% as expected, and one Hornet jam landed in two cycles, with its message.

**Files.** `npc/DroneAI.cpp`, `npc/DroneAI.h`, `system/TargetManager.cpp`, `system/TargetManager.h` (under `src/eve-server/`).

---

## 2026-10-11: Electronic warfare drones work (painters, dampeners, disruptors, neutralizers)

Patch `0013` · Status: **Tested** with the test bot. Waiting for a test in the game client. ECM drones: see the next entry.

**Why.** Electronic warfare drones flew, orbited and showed their beam, but did nothing to their target. Renn's drone code left them as visual only.

**What changed.** Each drone now does what its own attributes (its info window) say, to whatever it's engaging:

| Drones | Effect while working |
|---|---|
| Target painters (Warrior, Valkyrie, Berserker TP) | target's signature radius +4% / +8% / +20% |
| Sensor dampeners (Hobgoblin, Hammerhead, Ogre SD) | target's lock range and scan resolution ×0.88 |
| Tracking disruptors (Acolyte, Infiltrator, Praetor TD) | target's turret tracking, optimal range and falloff reduced; for an NPC, its guns |
| Neutralizers (Acolyte, Infiltrator, Praetor EV) | drain 5 / 10 / 25 GJ of the target's capacitor every 6 seconds |

The effect is undone exactly when the drone stops, switches target, is recalled or is destroyed. NPCs keep their own copy of their weapon stats, so they're told to re-read them each time. Several drones on one target multiply together, with no stacking penalties. Crucible didn't show these effects in the log, so none appears here either.

**Tested.** With the test bot, against a Pithi Arrogator:
- **Painter:** signature radius 33 → 35.6 m.
- **Dampener:** scan resolution 1050 → 924.
- **Disruptor:** tracking 0.365 → 0.321, optimal 500 → 440 m, falloff 3000 → 2640 m.
- **Neutralizer:** capacitor 70 → 60 → 50 GJ.

All of these were restored exactly when the drones were recalled. The same checks against a spawned Raven hull also passed.

**Files.** `npc/DroneAI.cpp`, `npc/DroneAI.h`, `npc/NPCAI.cpp`, `npc/NPCAI.h` (under `src/eve-server/`). Test bot: `drones mine`, and `spaceobject` takes a list of types.

---

## 2026-10-11: Launcher groups show one combined hit message per volley

Patch `0012` · Status: **Built**, not yet tested in game.

**Why.** Each launcher in a group fires its own missile, so a volley gave one hit message per missile. The message at the top of the screen only shows one at a time, which made it look like only one launcher was doing damage. A turret group fires one shot for the whole group and gives one message with the combined damage.

**What changed.** Hits from a launcher group on the same target in the same second are now shown as one message, with the total damage, using the group wording that turret groups use. This affects the combat log too. Each missile still does its own damage, so kills, wrecks and aggression are unchanged.

**Files.** `Client.cpp`, `Client.h`, `system/Damage.cpp` (under `src/eve-server/`).

---

## 2026-10-11: Launchers show their cycle, switch off, and can't be spam-clicked

Patch `0010` · Status: **Built**, not yet tested in game.

**Why.** Missile launchers, grouped or not, showed no cycle ring and couldn't be switched off once started. Clicking them again made them fire faster than their rate of fire. This is an old EVEmu bug.

**Cause.** When a module starts, the server tells your client which of the module's effects is now running. Your client's module button watches the launcher's own effect ("use missiles"). For launchers, the server swapped in the missile's effect instead, because it needs that one for the launch graphic. So your client never thought the launcher was on. It showed no ring, and every click sent "start" instead of "stop". The server then restarted the launcher on every click.

**What changed**
- The missile's effect is used only for the launch graphic. Your client is told the launcher's own effect is running.
- Starting a module that is already running no longer restarts it; the server just tells your client again that it's running. A module that is finishing its last cycle can't be restarted until that cycle ends.
- Weapons in a group take the group leader's target and settings, so their effects match the group's.
- **Patch `0011`:** the other weapons in a group now send their firing effect with the group's cycle time. Before, it went without one, so your client probably didn't show those launchers firing. Tested with the test bot: two grouped heavy missile launchers fired three volleys at a Raven. That was two missiles per volley, all six hit, and both launchers' firing effects had their cycle time. Both missiles start from the ship's centre on the same path, so they can look like one missile in flight; that matches how the client places missiles.

**Files.** `ship/modules/ActiveModule.cpp`, `ship/modules/ActiveModule.h` (under `src/eve-server/`).

---

## 2026-10-11: Weapon groups load, keep their charges and all fire

Patch `0009` · Status: **Built**, not yet tested. It compiles, but wasn't run against the test bot first; test it in game.

**Why.** Grouping weapons, for example two missile launchers, had three problems, all old EVEmu bugs:
- Loading charges into the group loaded only the first weapon.
- Grouping weapons that were already loaded emptied them all (Group All), or refused (dragging one onto another).
- A group fired only one missile.

**What changed**
- **Loading a group** loads every weapon in it. If there aren't enough charges to fill them all, they're shared evenly.
- **Grouping loaded weapons** keeps their charges. A group uses one charge type, the first weapon's. A weapon loaded with a different type is unloaded to cargo and reloaded with the group's type, if there is any in cargo. If the first weapon is empty, it takes the type of the one joining it. Weapons that are loading or active still can't be grouped.
- **Group All** no longer empties the weapons first. This was the `UnloadOnLinkAll` setting in `eve-server.xml`; it's now off, in your `config` folder too.
- **Group All also left its groups half-made.** The first weapon of each group wasn't marked as the group's leader, so a group could fire as a single weapon. Fixed.
- **Firing a group of launchers** launches one missile from every loaded launcher, as in Crucible. Before, one missile flew carrying the whole group's damage (and, by mistake, a longer flight time), which looked like only one launcher firing. Grouped turrets already fired together and are unchanged.

**Files.** `ship/Ship.cpp`, `ship/Ship.h`, `ship/modules/ActiveModule.cpp`, `ship/modules/ActiveModule.h`, `ship/Missile.cpp` (under `src/eve-server/`), `utils/config/eve-server.xml`. Test bot: new `dogma` and `moveto` commands, for testing modules.

---

## 2026-10-11: Drones no longer vanish when they return; light drones orbit and come home

Patch `0008` · Status: **Tested** with the test bot. Waiting for a test in the game client.

**Why.** In the first in-game test of Renn's version, fighters worked, but other drones went wrong:
- An Ogre and four of five Hammerheads disappeared on Return to Drone Bay. They never reached the bay, and were gone for good.
- Warriors reached their target and sat still instead of orbiting. They then never made it back to the bay, and Scoop to Drone Bay made them disappear too.

**Cause**
- **Disappearing drones.** A returning drone is put in the bay and then added to the stack of the same type already there. If you had launched every drone of that type, there was no other stack, and the server found the drone itself and merged it into itself. That deleted it. The one Hammerhead that survived was probably damaged, because damaged drones aren't stacked. Fighters were fine because 3 of the 13 stayed in the bay, so there was always a stack to join. Drones lost this way can't be brought back; make new ones with `/create`.
- **Light drones crawling.** The game client slows a ship down when it is within one second's travel of where it is heading, and slows it hard. A Warrior flies at 5,250 m/s with your skills, so it started braking about 5 km out and crawled the last kilometre or two at a few m/s. It never got close enough to switch to orbit or to enter the bay.

**What changed**
- A drone going back into the bay only joins a different stack, never itself. This also protects anything else that uses the same stacking code: mining, probes and loot.
- Drones fly at full speed while far away, then step their speed down as they close in, but not below their cruise speed. They now arrive in a few seconds.
- Fast drones swing wide of their orbit, so they only break orbit to chase once they are well outside it.

**Tested.** With the test bot in a Dominix against a Raven: five Warriors engage, orbit the Raven and all five return to the bay. The same with five Hammerheads. One Ogre returns to the bay.

**Files.** `inventory/InventoryItem.cpp`, `npc/DroneAI.cpp` (under `src/eve-server/`).

---

## 2026-10-11: Switched to Renn's movement rewrite (branch `movement`)

Patch `0007` · Status: **Built**. Renn's branch and patch `0007` were also run against the test bot; waiting for a test in the game client.

**Why.** EVEmu's server never really knew where ships were. It estimated movement with its own formulas, so it drifted away from what the game client showed. That caused the warp "bounce", the autopilot switching off, and drones flying odd paths. Our patches `0001`, `0003` and `0005` worked around some of the symptoms. Renn ([rennr/evemu_Crucible, `fix/movement`](https://github.com/rennr/evemu_Crucible/tree/fix/movement)) rewrote movement so the server runs the same simulation as the Crucible client, tick for tick, using the client's own formulas. Server and client now agree on where everything is.

**What Renn's branch brings** (9 commits, 10 to 11 Oct 2026)

- Movement: warp, approach, orbit, keep at range and align now match the client. Collisions between ships, and missiles flying and hitting like the client's.
- Drones and fighters: a full implementation that replaces our patches `0002` and `0003`. **Drones now only engage a target you have locked**, as in the real game.
- Autopilot: the autopilot warps to 15 km from gates, as in Crucible, and setting a speed no longer starts a stopped ship moving. Your client treated that as you taking control, which switched the autopilot off.
- Heat (overheating modules), more GM commands, `/kill`, invulnerability, and a GM tools menu in the client (`gmtools.py`).
- A newer CMake and Boost setup.

**What we added on top**

- Patch `0007`. In Renn's code, a gate jump sent your ship a Stop command while you were still in the old system. Your client's autopilot takes that as you taking control and switches off, the same problem patch `0005` fixed before. The ship now stops quietly when it jumps.
- The test bot (patch `0004`), with a new `lock` command, because drones now need a locked target.
- Our Dockerfile (`0006`), control panel and this changelog.

**Your settings.** The server reads its settings from the `config` folder in your project folder, so these were updated there (the old files are in `backup-before-movement`):
- `eve-server.xml`: autopilot warp distance 15 km instead of 1 km (Renn's setting; landing 1 km from a gate's centre puts you inside the gate now that ships can collide). The removed `ShipHeat` setting is gone from the file.
- `devtools.raw` and `gmtools.py`: Renn's GM tools menu.

**Tested with the test bot** (local server, before patch `0007`): undock, warp to a gate, jump, approach the arrival gate, jump back, all worked. Fighters launch up to the limit of 10, kill a Raven and all 13 return to the bay. Light drones engage, then return and orbit, but don't go back into the bay (see Known issues).

**Files.** Renn's branch (75 files, see its commits). Patch `0007`: `Client.cpp`, `system/DestinyManager.cpp`, `system/DestinyManager.h` (under `src/eve-server/`).

**Going back.** The previous version is still on GitHub (`master`), and the files this replaced are in `backup-before-movement` in your project folder.

---

## 2026-10-10: Control panel: wrong-folder check and per-job progress files

Status: **Tested** with the panel's automated tests (84 checks)

- **Wrong folder.** Docker names each setup after the folder it runs from, but the containers are always called `server` and `db`. Running the panel from a second copy of the code (for example `EvEmu-Etherium` next to `evemu_Crucible`) made Docker try to create a second, empty universe, and it failed with "The container name "/db" is already in use". Start, Stop, Restart, Rebuild and Wipe now check which folder the database belongs to first. If it is a different one, they name that folder and stop without changing anything. Free up space still works from any folder.
- **"The process cannot access the file … evemu_panel_task.log".** Every job reused one progress file in the Windows temp folder, and deleting it failed when another panel window or a virus scanner still had it open. Each job now gets its own file; old ones are removed when possible, and anything older than a day is cleared.

**Files.** `EVEmuControlPanel.ps1`.

---

## 2026-10-10: Faster, smaller rebuilds (Dockerfile and control panel)

Patch `0006` · Status: **Tested** with a local Docker build, awaiting a rebuild on your PC

**Why.** Every rebuild compiled a large debugging build of the whole server from scratch (10 to 20 minutes) and produced a 2 to 3 GB image packed with compilers. That filled C: once already.

**What changed**

- The server is built as a normal optimised (Release) build.
- A compile cache is kept between builds, so a rebuild recompiles only the files that changed. In testing, after a one-file change the compile step took about 20 seconds instead of about 8 minutes. The very first rebuild after this change still compiles everything once.
- The image that runs is a slim Debian image with just the server, its libraries and the tools the start scripts need: about 420 MB instead of 2 to 3 GB. The build folder is deleted within the build step, and file ownership is set while copying, so no layer stores a second copy of the files.
- `.git` is no longer copied into the build. That makes the build context smaller, and new commits no longer force a full recompile. (The server log now shows `invalid_hash` instead of the commit hash.)
- Control panel: cleaning up after a rebuild keeps the compile cache, and keeps build files used in the last 3 days so the build tools aren't downloaded each time. **Free up space** removes everything except the compile cache, which is capped at 3 GB.

**Files.** `Dockerfile`, `.dockerignore`, `EVEmuControlPanel.ps1`.

---

## 2026-10-10: Autopilot keeps running through jumps; no more warp "bounce"

Patch `0005` · Status: **Tested** (test bot and in game). It improved things, but the autopilot still has open problems; see Known issues.

**Why.** Three things went wrong with the autopilot and warping. The autopilot switched itself off after every jump. It approached a nearby gate and then never jumped. Warps landed, then the ship turned round and bounced a few km away.

**Cause.** All three came from the server sending your ship a "Stop" command you didn't ask for. The client's autopilot treats that as you taking manual control and switches itself off. The server sent Stop:
- every second to any ship that had already stopped, including straight after a warp while your client was still landing (the bounce), and to every parked ship in the area;
- when your ship arrived in a new system through a gate;
- when the server thought your ship had reached the thing it was approaching, such as a gate the autopilot was flying to.

**What changed.** Stop is now sent once, when a ship actually stops. Arriving through a gate stops the ship without sending a command. Approaching something no longer sends a Stop to player ships, because your client holds its own position at the approach range. Server traffic in busy areas drops too.

**Files.** `Client.cpp`, `system/DestinyManager.cpp` (under `src/eve-server/`). Test bot: new `beyonce` and `rebind` commands for movement and gate tests.

**Tested.** With the test bot against a local server: undock, warp to a gate, jump, approach the arrival gate, jump back. One Stop is sent on undock, as before, and none at warp ends, jump arrivals or gate approaches. The drone and fighter tests still all pass.

---

## 2026-10-10: Drone and fighter fixes after the first in-game test

Patch `0003` · Status: **Tested** (test bot, then in game on 10 Oct 2026)

**Why.** In the first in-game test, fighters showed "Fighting" but never attacked. They flew off in stair-step lines, wouldn't return to the drone bay, and dragging a whole stack out launched 13 when the limit was 10.

**What changed**

- **Orbit bug (existed in EVEmu before these changes).** One calculation in the server's orbit code overflowed, so an orbiting object was sometimes placed about 170,000 km away. Fighters then tried to warp back to their target over and over, which caused the stair-step lines. This probably also affected NPC rats that orbit you.
- Drones now tell the game client their real top speed (a Dragonfly flies at 2,000 m/s, not its 225 m/s NPC cruise speed), so the client and server agree on where they are.
- Drones and fighters, which turn within a second, now turn straight toward where they are going. Before, the server turned them a few degrees a second, so returning drones circled the ship in wide loops and were never scooped.
- Ships following something now steer toward where it is now, not where it was when the order was given.
- Dragging a whole stack into space launches drones only up to your limit; the rest stay in the bay with a message.
- Fixed a server crash when drones lost a target that had just been destroyed.

**Files.** `npc/Drone.cpp`, `npc/DroneAI.cpp`, `ship/ShipService.cpp`, `system/DestinyManager.cpp` (under `src/eve-server/`).

**Tested.** With the test bot against a local server: 13 Dragonflies dragged out launched 10; they engaged and destroyed a Raven with no warping, and all 13 went back to the bay on recall. Five Hobgoblin Is engaged, returned and orbited, then went back to the bay.

---

## 2026-10-10: Test bot (tooling, not server code)

Patch `0004` · Status: **In use**

**What.** `src/eve-bot`: a headless client that logs in like the game client and runs a script: create a character, run GM commands, undock, launch drones, engage, recall, check results. It lets server changes be tested before you rebuild. It is only built when CMake is run with `-DEVEMU_BUILD_BOT=ON`, so the Docker build is unaffected.

**Files.** `CMakeLists.txt` (opt-in build switch), `src/eve-bot/`.

---

## 2026-10-10: Control panel cleans up Docker's disk space (tooling, not server code)

Status: **In use**

**Why.** Each rebuild left the previous server image and several GB of build files behind inside Docker's disk image, which lived on C:. The first rebuild from the panel filled C: completely and crashed WSL and Docker.

**What changed**

- Rebuild server now removes leftover build files when it finishes, whether the build worked or not, and the old server image once the new one is running. The next rebuild may take a few minutes longer because Docker downloads the build tools again.
- New **Free up space** button: shows how much space Docker is using, cleans up the same way, then shows the space used afterwards.
- New **Disk space** status: free space on the drive that holds Docker's disk image (read from Docker Desktop's "Disk image location" setting). Amber below 40 GB, red below 20 GB, and red if the Windows drive is below 5 GB.
- Before anything that builds the server (Rebuild, or Start or Wipe when the server image doesn't exist yet), the panel warns if either drive is low and asks before going ahead.
- Start and Wipe build the server first if its image is missing, and clean up afterwards. A failed first build is not retried automatically.
- When a task fails because Docker ran out of space or stopped responding, the Activity tab says what to do next.

**Never touched by clean-up:** volumes (the universe database and server caches) and containers. The only thing that deletes the database is Wipe universe.

**Files.** `EVEmuControlPanel.ps1`.

---

## 2026-10-10: Drone and fighter control

Patch `0002` · Status: **Tested** together with `0003` (the first in-game test found problems, fixed in `0003`)

**Why.** Every drone command from the client replied "Drone Control is not implemented yet". Drones could be launched but not engaged, recalled or scooped by command, and carriers had no working fighters.

**What changed**

- Drone commands now work: Engage, Return and Orbit, Return to Drone Bay (drones fly back and are scooped on arrival), Guard, Assist, Abandon, and Reconnect to Lost Drones.
- Drones look up their controlling ship by ID every tick instead of holding a pointer to it. Docking, jumping, losing the ship or logging off can no longer leave a drone pointing at a ship that no longer exists.
- Drones beyond the pilot's control range, or left behind when the ship docks or leaves the system, go idle in space. They stay owned by the pilot and can be reconnected or scooped later.
- Drones set to aggressive defend their ship from anything that locks it.
- Fighters have no control range limit, warp after their target when "attack and follow" is on, and warp back to the carrier when recalled from another grid.
- Fighter bombers use missile-style damage based on the target's signature radius and speed, so they do little to small, fast ships.
- Damage bonuses: Drone Interfacing (+20%/level), Combat and Heavy Drone Operation (+5%/level), Fighters and Fighter Bombers (+20%/level), and the Caldari and Gallente carrier hull bonus.
- Carriers and supercarriers can field their extra drones and fighters from the carrier skills.
- A destroyed drone frees its ship's drone bandwidth and drone count.
- Drone behaviour settings from the drone window (aggressive, focus fire, attack and follow) are remembered for the session.

**Not done yet.** Mining drones, handing drones to another pilot (delegation) and drone unanchoring reply that they aren't supported yet.

**Files.** `Client.cpp`, `Client.h`, `dogmaim/DogmaIMService.cpp`, `npc/Drone.cpp`, `npc/Drone.h`, `npc/DroneAI.cpp` (rewritten), `npc/DroneAI.h`, `npc/EntityService.cpp`, `ship/Ship.cpp`, `ship/Ship.h`, `ship/ShipService.cpp`, `system/TargetManager.cpp`, `system/TargetManager.h` (all under `src/eve-server/`).

**How to test**

1. Launch light drones, `/spawn` a rat, and use Engage, Return and Orbit, and Return to Drone Bay.
2. Warp away with drones out, come back, and use Reconnect to Lost Drones.
3. `/create` a Thanatos and Dragonflies, engage a rat with fighters, then recall them from another grid.

---

## 2026-10-10: Autopilot and remote Jump and Dock

Patch `0001` · Status: **Tested** in game. Jumps work; the autopilot switching off afterwards is fixed in `0005`.

**Why.** The client and server time warps slightly differently, so the client often lands a few seconds before the server's own warp has finished. The autopilot (and Jump or Dock clicked from a distance) asks to jump the moment the client lands. The server refused with "You can't do this while warping", and the autopilot never asked again. This is upstream issue [#263](https://github.com/EvEmu-Project/evemu_Crucible/issues/263).

**What changed.** When a jump or dock request arrives while the server still thinks the ship is warping, the server now finishes the warp at that moment, provided the warp is already slowing down and is ending at that same gate or station. Requests made earlier in a warp, or for somewhere else, are still refused.

**Files.** `ship/BeyonceService.cpp`, `system/DestinyManager.cpp`, `system/DestinyManager.h` (under `src/eve-server/`).

**How to test.** Set an autopilot route 3 or more jumps long and let it fly. Separately, right-click a gate from far away and choose Jump.

---

## 2026-10-10: Control panel (tooling, not server code)

Status: **In use**

**What.** `EVEmu Control Panel.bat` and `EVEmuControlPanel.ps1` in the server folder: a Windows window for running the server without typing Docker commands.

**Features.** Server, database and Docker status, plus players online. Start, Stop, Restart, Rebuild and Wipe universe (with a confirmation). Launch the game pointed at `127.0.0.1`. Make an account admin. A live server log with Copy, and an Activity tab showing what each button did. A desktop shortcut.

**Notes.** Settings are kept in `%APPDATA%\EVEmuControlPanel\settings.json`. Closing the panel does not stop the server.

---

## Known issues

- **Since the move to Renn's base (14 Oct 2026):** the incursion issues below belong to the old base (`master`); incursions aren't on the new base yet. The others need checking again on the new base.
- **CONCORD spawns on top of the criminal (found 13 Oct 2026).** CONCORD's ships spawn at 0 m, right on the player's ship, and get bounced out to long range (`0031`, Renn's). Renn's code places them 4 to 9 km from the target, so the old NPC flying was the likelier cause; his new NPC AI replaces it. Check again on the new base.
- **Incursion acceleration gates don't line up with their pocket (found 13 Oct 2026, for later).** A site's acceleration gate should line up with the direction you warp in from and with its pocket, and it doesn't (`0024`).
- **Incursion site spawn positions and timers (found 13 Oct 2026, for later).** Where things spawn in the sites, and the sites' timers, need adjusting (`0024`).
- **True Power Provisional Headquarters only has its final room (found 13 Oct 2026, for later).** Its positions and timers need adjusting too (`0024`).
- **Incursion map shows the systems wrong (found 13 Oct 2026, for later).** The star map's incursion view doesn't show which systems are Vanguard, Assault and Headquarters correctly (`0024`). The sites themselves spawn in the right systems; `/incursion info` lists each system's role.
- **Incursion Journal tab, missing acceleration gates and beacon names (found 12 Oct 2026).** The Journal's Incursions tab didn't show the incursions, the site beacons had no gate, and every beacon was named "Celestial beacon" (`0024`). Fixes built in `0028` (13 Oct 2026), not yet tested in game.
- **Fighter bomber damage too uniform (open since 11 Oct 2026, shelved).** In the first in-game test every bomber volley on a capital did full damage with the same hit message. Expected from the missile formula against big, slow capitals, but the damage application needs tuning later.
- **Autopilot (open since 10 Oct 2026, parked).** On Renn's base with patch `0007` it completed one route, then on a later try wouldn't jump a gate (11 Oct 2026). Parked to do properly later.

## About this changelog

- **Base (since 14 Oct 2026, branch `revised`):** Renn's [EVEMu Revised](https://github.com/rennr/evemu_Revised) branch `revised` at `db8e5b16` (14 Oct 2026, "ci: remove for now"). It is Renn's earlier `fix/movement` and `enhanced` work (`0031`) plus his rework of most of the server. Our patches `0033` onwards apply to it with `git am`; see the 14 Oct entry for what was carried over.
- **Previous base (11 to 14 Oct 2026, branch `master`, kept unchanged):** Renn's [`fix/movement`](https://github.com/rennr/evemu_Crucible/tree/fix/movement) branch at `8f915a8` (11 Oct 2026, "feat: auto pilot"), EVEmu Crucible `master` at `f55b488` plus Renn's rewrite of movement, drones, missiles, heat and GM tools. Patches `0007` to `0032` apply to that branch. Our own patches `0001`, `0002`, `0003` and `0005` were not applied on it; Renn's code replaced them.
- **Upstream base:** [EvEmu-Project/evemu_Crucible](https://github.com/EvEmu-Project/evemu_Crucible) `master` at `f55b488` (30 Oct 2025, "Streamline docker (#318)").
- **Patches:** each server change has a matching file in `sandbox-patches/`, numbered in the order they were made. `0001` to `0006` apply to the upstream base with `git am`. `0007` to `0032` apply to the previous base (`master`), and `0033` onwards to the current one (`revised`). `0031` is Renn's own commit `b3b5d1a` from his `enhanced` branch. Patches up to `0032` are kept as a record; on the current base they are replaced as the 14 Oct entry lists.
- **Status labels:** *Built* means it compiles and the server boots. *Tested* means it has been checked in the game client.

## Offering changes to the main EVEmu project

Some changes here fix bugs that exist in EVEmu itself; others only make sense for this sandbox. If these are offered upstream later, send them as separate pull requests, one per row, each starting from the upstream base.

| Change | Patch | Suitable for upstream? |
|---|---|---|
| Autopilot and remote Jump/Dock while the server still thinks the ship is warping (upstream issue #263) | `0001` | Yes |
| Player drone and fighter control (EntityService, DroneAI rewrite) | `0002` | Yes, together with `0003` |
| Orbit overflow, agile turning, current turn target, stack launch limit, orbit-target crash | `0003` | Yes. The orbit overflow and turn fixes in `DestinyManager.cpp` also help NPCs and could go on their own. |
| Stop sent once instead of every tic; no Stop on jump arrival or on reaching an approach target | `0005` | Yes |
| eve-bot headless test client | `0004` | Possibly, as an optional developer tool (off by default) |
| Release build, compile cache and slim runtime image (Dockerfile) | `0006` | Possibly; it changes how everyone's image is built, so offer it separately |
| Gate jumps stop the ship without sending a Stop (on Renn's branch) | `0007` | Yes, to Renn's branch |
| Weapon groups: load every weapon, keep charges when grouping, every launcher fires (on Renn's branch) | `0009` | Yes. These are EVEmu bugs too. |
| Electronic warfare drones: painters, dampeners, disruptors and neutralizers have real effects | `0013` | Yes, to Renn's branch |
| ECM drones jam (and jamming in general: locks lost, no relocking while jammed) | `0014` | Yes |
| Jump portals open (one-cycle activation was read as a failure); cyno ships freeze | `0022` | Yes, with `0017` |
| Cyno and POS jump bridge high-sec check used an inverted security value | `0019` | Yes. An EVEmu bug. |
| Empty sovereignty data zeroed, so empire systems no longer look cyno-jammed | `0018` | Yes. An EVEmu bug. |
| Cynos and jump drives: jump checks, covert cynos, titan and Black Ops bridges | `0017` | Yes. The jump crash and fuel fixes are EVEmu bugs; bridges are new. Test first. |
| Fighter and bomber damage skills and carrier hull bonuses (on Renn's branch) | `0016` | Yes, to Renn's branch |
| Fighter bombers hit capitals, and only attack capitals, stations and structures (on Renn's branch) | `0015` | Yes, to Renn's branch |
| Launcher groups: one combined hit message per volley, like turret groups | `0012` | Possibly; a display choice |
| Launchers show their cycle, can be switched off, and can't be spam-clicked to fire faster | `0010`, `0011` | Yes. An EVEmu bug. |
| Scooped drones no longer deleted; light drones reach their orbit and the bay (on Renn's branch) | `0008` | Yes, to Renn's branch. The stack-merge fix in `InventoryItem.cpp` is an EVEmu bug too. |
| Incursions, first cut: incursion manager, Vanguard sites, CONCORD ISK and LP payouts | `0020` | Partly. The manager and site code could go upstream; the Sansha ship stats and site layouts are stand-ins, not CCP data. |
| Incursions: Assault, Headquarters and Mothership sites; wiki ship stats; NPC missile damage multiplier for incursion ships | `0021` | Partly, together with `0020`. Stats and rewards come from the EVE University wiki, not CCP's data; wave sizes are our own picks. |
| Turret hit chance follows CCP's formula (sideways speed only, surface range, no signature damage cut) | `0023` | Yes. An EVEmu bug. |
| Incursions: overview beacons, acceleration gates and pockets, each site's wiki mechanics, Sansha flight and EWAR, Journal tab, LP pool; fixes for acceleration gate warps, space container listings, lost webs and the mothership wreck | `0024` | Partly, together with `0020` and `0021`. The gate warp range, container listing, web and wreck fixes are EVEmu bugs; site layouts and timers follow the EVE University wiki, not CCP's data. |
| Undocking as a fleet booster crashed the server (fleet boost applied to a ship not made yet, or to docked members) | `0025` | Yes. An EVEmu bug. |
| Leaving a fleet left the client still in it (fleet session values set to 0 instead of None); commander slots not freed on leaving | `0027` | Yes. An EVEmu bug. |
| Jump portals play their portal effect, and Black Ops portals count as covert (the portal effect isn't the module's default effect) | `0026` | Yes, with `0017` and `0022` |
| Objects that can't move (acceleration gates, beacons, structures) dropped from their grid after a minute; plain space objects sent without their name; mixed name lookups answered only in part; incursion Journal LP call rejected | `0028` | Yes, apart from the Journal part (with `0024`). The grid, name and lookup fixes are EVEmu bugs. |
| Containers in space sent to the client as item 0 unless a player jettisoned them | `0030` | Yes. An EVEmu bug. |
| CONCORD and Crucible crime rules: flags, criminal countdown, CONCORD, faction police, sentry guns, kill rights, security status, logoff timer (Renn's `enhanced` branch) | `0031` | Renn's own work; his to offer |
| Crime starts off; `/crime on` and `/crime off` switch it while the server runs | `0032` | Possibly, to Renn's branch, as a server option |
| Containers in space sent as item 0 (on Renn's `revised`) | `0033` | Yes, to Renn's branch |
| POS jump bridge high-sec check inverted (on Renn's `revised`) | `0034` | Yes, to Renn's branch |
| Squad boosts with no booster or docked members; fleet session left at 0 after leaving; leaver's boost kept (on Renn's `revised`) | `0035`, `0036` | Yes, to Renn's branch |
| Launcher cycle ring and switch-off; grouped weapons' effects (on Renn's `revised`) | `0037`, `0038` | Yes, to Renn's branch |
| Space container listing, mission containers, wreck 3260, beacon names, mixed name lookups, ListToINString padding (on Renn's `revised`) | `0039` | Yes, to Renn's branch |
| Incoming packet leak; fleet broadcast crash; apostrophe in the item save; wormholes never created; paused skill queue wiped (on Renn's `revised`) | `0040` to `0044` | Yes, to Renn's branch. All are bugs in his code; the packet leak is already fixed upstream. |
| Crime off at server start, `/crime on` and `/crime off`; ambient pirates leave players alone while crime is off (on Renn's `revised`) | `0045`, `0046` | Possibly, to Renn's branch, as a server option |
| Release Docker image, Stop that lets the server save, fresh universe volume, control panel | `0047` to `0050` | The Stop fix (`0048`) suits Renn's branch; the rest is sandbox tooling |
| Windows control panel, changelog, patch files | none (tooling) | No, sandbox-only |

Each patch's commit message describes the change in developer terms; the dated entries above describe it in player terms.
