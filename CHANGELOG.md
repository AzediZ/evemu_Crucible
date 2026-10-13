# Changelog: movement, drones, jump drives, turrets, incursions and CONCORD

Changes on top of [EvEmu-Project/evemu_Crucible](https://github.com/EvEmu-Project/evemu_Crucible) `master` at `f55b488` ("Streamline docker (#318)"). **Newest first**, and every change is headed with the date it was made.

## 2026-10-13: Notes from testing

- **Titan and Black Ops bridges:** the bridge effect now works, so bridging should be fully working.
- **Incursion sites:** site spawn positions and timers need adjusting later.
- **True Power Provisional Headquarters:** it only has the final room so far, and its positions and timers need adjusting too.
- **Acceleration gates** don't line up with their pocket properly.
- **CONCORD** ships spawn at 0 m, right on the player's ship, and get bounced out to long range.

## 2026-10-13: CONCORD and crime rules (by Renn), off until you switch them on

From Renn's [`enhanced`](https://github.com/rennr/evemu_Crucible/tree/enhanced) branch (commit `b3b5d1a`, 11 October 2026). Credit for this section goes to Renn, apart from crime starting off. EVEmu had none of this: `CrimeWatch` was an empty shell and CONCORD wasn't even built.

- **Crime flags.** Aggression flags (15 minutes), the criminal countdown for a crime in 0.1 to 1.0 space, the 60-second weapons timer before you can dock or jump, criminals kept out of gates and stations in empire space unless in a pod, can flagging, flags for repairing someone who's in a fight, kill rights (30 days), outlaws (-5.0 and below) and enemy militias free to shoot. Shooting rats, police or navies is never a crime. Wars aren't in. The overview, the warnings and the neocom countdown show the flags.
- **CONCORD** in 0.5 and up: arrives 6 to 19 seconds after a crime, depending on the system's security; scrambles, webs and jams the criminal; destroys the ship but never the pod; can't be killed; follows a criminal who warps off. Ships CONCORD destroys get no insurance.
- **Faction police** chase pilots whose security status is too low for a high-sec system (-2.0 in 1.0, down to -4.5 in 0.5). They can be escaped.
- **Sentry guns** at every empire gate (3) and station (2) shoot criminals and anyone flagged to their owner.
- **Security status.** Crucible losses for aggression, ship kills and pod kills. Killing NPCs raises it, but only the best NPC per system counts every 15 minutes. The character sheet shows the security status and its history.
- **Logging off in space.** Your ship stays 15 minutes after a fight with players; otherwise it warps off and vanishes after a minute (two if NPCs were shooting it). It can be destroyed while you're away.
- **Small fixes.** Autopilot can't warp out of a warp scramble; standing changes reach the client with their values; NPCs with no spawn manager no longer risk a crash on warp-out; a dependency archive is extracted again if an earlier build was interrupted.
- **Off at server start.** `<crime><Enabled>` in `eve-server.xml` is `false`, so new things can be tested anywhere without CONCORD or flags. `/crime on` and `/crime off` switch it while the server runs. On puts the sentry guns in systems that are already loaded. Off clears every flag, sends CONCORD and the police away and removes the sentry guns. `/crime` on its own says whether it's on.
- **While crime is off**, killing NPCs still raises security status (with the 15-minute rule) and nothing lowers it, and taking from another pilot's can or wreck doesn't bring up the client's theft warning.
- **GM command:** `/crime (me|charID|entityID) [victimID]` commits a test crime.
- **Database:** migration `20261013170000-killrights.sql`, applied when the container starts.

First in-game look on 13 October 2026: CONCORD comes, but see the known issue below.

### Known issues

- **CONCORD spawns on top of the criminal.** CONCORD's ships spawn at 0 m, right on the player's ship, and get bounced out to long range.

## 2026-10-13: Site containers show in space

- **Containers in space were invisible.** A container's position and details went to the client with item ID 0 unless it was a can a player jettisoned. The client can't show an object with no ID. They now carry their real ID. This is an EVEmu bug: the same code is in upstream `master`.
- This affects every incursion container: the True Creations Research Center's MTAC Factory and Shield Transfer Control Tower, the Nation Ore Refinery, the Overwhelmed Civilian Facility's civilian wrecks and evacuation freighter, the Logistics Control Arrays you hack, and the mothership's Central Control Nexus and Sansha Control Relay. It also fixes jetcans dropped by NPCs and mining drones, and containers loaded again when a system starts up.

Checked without a game client: the MTAC Factory and the Shield Transfer Control Tower now arrive with their own item IDs. Not yet tested in game.

## 2026-10-13: Incursions: Journal tab, acceleration gates and site names

Fixes for the problems found in the first in-game look on 12 October 2026.

- **Journal tab.** The Crucible client sends the incursion IDs to `rewardMgr.GetDelayedRewardsByGroupIDs` as a tuple, and the server only accepted a list, so the tab got an error back and showed nothing. It now accepts a tuple, a list or a single ID. Each incursion's state is also sent the way the client counts it (Withdrawing 0, Mobilizing 1, Established 2). Before, a withdrawing incursion came through as 3 and the tab hit an error, and because the Journal reopens on the last tab you used, the whole window stayed blank.
- **Acceleration gates stay on their grid.** Each gate spawned in the right place, but about a minute later it dropped out of its grid. Once a minute the server checks each grid for ships that have flown off it, and it threw out anything that can't move (acceleration gates, beacons, site structures, deployables) while those objects still counted themselves on that grid. Anyone arriving after that minute saw the beacon but not the gate. Objects that can't move now stay on their grid. Mission acceleration gates and other site structures were probably hit by the same bug. This is an EVEmu bug: the same code is in upstream `master`.
- **Beacon names.** The server sent no name with beacons, gates and other plain space objects, so the overview showed the type name instead. It now sends each object's own name: a beacon shows its site's name and a gate shows "Acceleration Gate".
- **Name lookups.** When the client asked for the names of map objects, spawned objects and asteroids in one request, the server only answered for the last kind it looked up, so some names could stay blank. It now answers for all of them, in one query. An old EVEmu helper left invisible padding (NUL characters) on the list of IDs, which cut such a query off before its closing bracket; it now trims it. The database query function sent anything longer than 4,096 characters with junk on the end, and now sends long queries in full.

Checked without a game client: the site beacons arrive named after their sites, and all three acceleration gates are still on their beacon's grid 90 seconds after spawning. Not yet tested in game.

### Known issues

- **The star map's incursion view** doesn't show which systems are Vanguard, Assault and Headquarters correctly. The sites themselves spawn in the right systems; `/incursion info` lists each system's role.

## 2026-10-13: Leaving a fleet takes you out of it

- **Leaving.** On leaving, the server set your fleet, wing, squad and fleet role to 0. Out of a fleet the client expects "none", the value it has at login, and it checks for "none" to decide whether you are in a fleet, so a fleet of 0 still counted as one and the pilot was left in "fleet limbo". Leaving, being kicked and logging off now set them back to "none".
- **Commander slots.** When a fleet, wing or squad commander left, the server only freed the commander slot if they were also that unit's booster. A commander who wasn't the booster stayed in the slot after leaving or logging off, so nobody else could take command, and the boost code kept reading a pilot who was gone, which could crash the server. The slot is now freed for whoever leaves.
- **Fleet finder.** An advert posted by a pilot who leaves is taken down, because the fleet finder reads that pilot for every listing.
- **Boosts.** Your own fleet boost now ends when you leave, instead of at your next dock, undock or jump.
- Kicking a pilot who has already gone offline can no longer crash the server.

These are EVEmu bugs: the same code is in upstream `master`. Compiles; not yet tested in game.

## 2026-10-13: Titan and Black Ops bridges show their portal

- **Portal effect.** The server started the jump portal generator with the module's default effect. In the game data the portal effect isn't marked as the default, because the module button doesn't open a portal, so the client was told to play effect 0 and had nothing to show. The generator now always runs its portal effect: `jumpPortalGeneration` on a titan, `jumpPortalGenerationBO` on a Black Ops. The client then plays its own 60-second portal, and the module shows as active for that minute.
- **Black Ops portals.** The same mistake meant a Black Ops covert portal was never recognised as covert, so it couldn't open to a covert cyno and anything could use it like a titan bridge. It can now open to a covert cyno, and only covert ops ships, stealth bombers and force recon ships can jump through it.

Tested in game on 13 October 2026: the bridge effect works.

## 2026-10-12: Fleet commanders can undock without crashing the server

- **Undocking as a fleet booster crashed the server.** When a fleet commander (or another pilot with a booster role) undocked, the server worked out the fleet's boosts again and counted the pilot as already in space, but their ship isn't created until a moment later in the undock. Applying the boost to it read a null pointer (`FleetService::UpdateBoost()` calling `ShipSE::ApplyBoost()`). The boost update now skips a pilot with no ship yet; their own fleet timer applies the boost 1.5 seconds after undock, as it already did for pilots without a booster role. This is an EVEmu bug: the same code is in upstream `master`.
- **Docked squad members and squads without a booster.** The same update handed a boost to every squad member in the booster's system, docked or not, and read the squad's booster even when it had none. Either would have crashed the same way. Both are now skipped, and the wing commander's own boost has the same check.

Tested in game on 12 October 2026: undocked several times as fleet commander with two other pilots in the fleet, and the server stayed up.

## 2026-10-12: Incursions

EVEmu had almost no incursion code. This adds Sansha incursions following the EVE University wiki ([Incursion sites](https://wiki.eveuniversity.org/Incursion_sites) and each site's page, [Sansha's Manual](https://wiki.eveuniversity.org/Sansha%27s_Manual), [Incursions](https://wiki.eveuniversity.org/Incursions)). Where the wiki says nothing (ship counts, gate distances, structure hit points, timers), the values are our own picks, not CCP data.

- **Incursions** take over a high-sec constellation: a staging system, Vanguard, Assault and Headquarters systems. One starts on its own after the server boots (settings in a new `<incursion>` section of `eve-server.xml`). Online pilots get a notice.
- **All eleven sites**, each working like its wiki page: Nation Commander Outpost, Nation Mining Colony and Override Transfer Array (Vanguard); Nation Commander Stronghold, Nation Consolidation Network and Overwhelmed Civilian Facility (Assault); Nation Rebirth Facility, True Creations Research Center and True Power Provisional Headquarters (Headquarters); and the Kundalini Manifest and Uroborus motherships. That includes the objectives: Lyavite for the refinery, civilians for the evacuation freighter, MTACs for the shield transfer control tower, hacking the logistics arrays, the Citizen kill, the Consolidation Network's locked staging gate and the Provisional Headquarters' hidden gates.
- **Sites are overview beacons** with acceleration gates into pockets, as on live. The pocket's ships spawn when the first pilot arrives.
- **Sansha ships use the wiki's stats** (signature, speed, orbit, DPS, volley, range, hit points, resists) for the ships Sansha's Manual lists, fly at their listed orbit and use propulsion with signature bloom. Their EWAR works: warp disruption, webs, neutralizers, ECM, target painters, remote shield repairs, the Vylade Dien's resist bonus and the motherships' ECM burst.
- **The mothership** appears when a Headquarters site is completed at zero influence and the incursion is old enough (3 days in high-sec, 1 day in low-sec). Killing it ends the incursion; otherwise it withdraws after 24 hours.
- **Rewards** follow the wiki: CONCORD ISK per site completed, split across the fleet, with low and null-sec paying 10/7 of high-sec. LP goes into a pool paid when the mothership dies (`DelayedLP`).
- **Client screens:** the Journal's Incursions tab and the starmap's incursion view.
- **GM commands:** `/incursion info`, `/incursion start [here|<constellationID>]`, `/incursion influence <0-100> [here]`, `/incursion mothership [here]` and `/incursion end [here]`.
- **Database:** migrations `20261012120000-incursions.sql` and `20261012180000-incursions-sites.sql`, applied when the container starts.
- **EVEmu bugs fixed along the way:** NPCs with only missiles no longer fire a fake zero-damage laser; live dungeon items are removed when their system unloads; acceleration gates outside incursions warped you the wrong distance; space containers listed only your own items; mission containers loaded as plain items; a data analyzer on a non-wreck container tried to turn it into a loot can; stasis webs were lost when the target's speed was recalculated; the Sansha supercarrier left no wreck.

Not done yet: the constellation-wide system penalty, staging sites, True Sansha loot from the mothership, and the "most damage" rule between competing fleets. The server side was checked without a game client first.

First in-game look on 12 October 2026: the site beacons appear, but the problems below need fixing before the sites can be run.

### Known issues

- **Every site beacon is named "Celestial beacon"** instead of its site's name. Fixed on 13 October 2026.
- **No acceleration gate at the site beacons**, so the pockets can't be reached. Fixed on 13 October 2026.
- **The Journal's Incursions tab doesn't update** to show the incursions correctly. Fixed on 13 October 2026.

## 2026-10-12: Turret hit chance

Turrets now use CCP's own hit-chance formula, `Ballpark::GetAccuracy()` in [carbonengine/destiny](https://github.com/carbonengine/destiny) (`src/Ballpark.cpp`, released by CCP under MIT). This covers player turrets, NPC turrets and NPC sentry guns; drones already used it.

- **Tracking counts only sideways speed.** Angular velocity is the part of the relative velocity across the line between the two ships, divided by the distance. Before, the whole relative speed counted, so flying straight at or away from a target spoiled tracking.
- **Falloff uses surface-to-surface range**, not centre to centre, which put big ships and structures further out than they were.
- **No signature damage cut.** EVEmu multiplied damage by target signature ÷ signature resolution when a gun could track but its signature resolution was bigger, so a large turret did about a tenth of its damage to a slow frigate. Signature now only scales the tracking part, as in CCP's code.
- **A wrecking shot has to hit first.** A roll under the crit chance used to count as a wrecking shot even when the shot should have missed.
- A weapon with no falloff hits inside optimal and misses outside it; a weapon with no tracking never hits.

Compiles; not yet tested in game.

## 2026-10-11 to 2026-10-12: Jump drives, cynos and bridges

- **Jumping to a fleet member's cyno is checked properly.** The server checks the cyno is still lit and its pilot is still in your fleet, so a cyno going out mid-click gives a message instead of a likely crash. Jumps beyond the ship's jump drive range (with Jump Drive Calibration) are refused with the distance and your range. A jump needs and uses the ship's jump capacitor cost (less with Jump Drive Operation). Fuel is taken correctly when isotopes are split across several stacks; the amount is unchanged.
- **Cynos** last 10 minutes flat (Survey no longer makes them longer). The ship is held still while the field is lit and gets its normal speed back afterwards (before, its top speed was left at 0).
- **Covert cynos.** The Covert Cynosural Field Generator can be fitted and lit. Cyno jammers don't stop it, and only Black Ops can jump to it.
- **Late joiners.** A pilot who joins the fleet after a cyno is lit, or a portal opened, is told about it, so they get Jump to and Jump through too.
- **Titan and Black Ops bridges.** "Bridge to" on a fleet member with a cyno (or an alliance beacon) opens a jump portal for one cycle of the Jump Portal Generator, within the bridging ship's jump range, and holds the bridging ship in place. Fleet members within 2,500 m of it get "Jump through", which takes fuel from the bridging ship's fuel bay by the jumper's mass and the distance. A titan portal won't take capitals, Black Ops or freighters, and can't open to a covert cyno. A Black Ops covert portal only takes covert ops, stealth bombers and force recons.
- **Cynos were "jammed" everywhere outside claimed null-sec.** For a system with no sovereignty data (all of empire space), the server returned uninitialised data, so its "jammer" was random. It now returns empty data. The same fix covers the jump bridge onlining and structure anchoring checks that read it.
- **The high-sec check for cynos and POS jump bridges was upside down.** It read the server's "security value" (1.1 minus the system's security), so cynos were refused in low and null-sec and allowed in high-sec. It now reads the system's security: 0.45 and up (0.5 or above in the client) is high-sec.

Tested in game on 11 and 12 October 2026: cynos in low-sec, the high-sec refusal, a titan jumping to a fleet cyno, Bridge to and Jump through.

Not done: jumping through an alliance member's portal when not in their fleet; strategic cruisers with the covert subsystem can't use covert portals.

### Known issues

- **No visual effect on bridges.** The portal works, but no portal effect appears in space. Fixed on 13 October 2026.

## 2026-10-11: Drones

Renn's drone implementation (player control of drones and fighters: engage, mine, return, guard, assist, abandon, reconnect and delegate). Drones only engage targets the pilot has locked, as in Crucible.

We had written our own player drone and fighter control first, on 10 October 2026. It was built on EVEmu's old movement code. When we adopted Renn's movement rewrite, we switched to his drone implementation too, because it is built on the new movement. The two were written independently. These fixes are ours, on top of Renn's drones:

- **Drones no longer vanish when they return to the bay.** A returning drone joined the stack of the same type in the bay. If every drone of that type had been launched, the only "stack" was the drone itself, and merging it into itself deleted it. This hit both Return to Drone Bay and Scoop to Drone Bay. The fix is in `InventoryItem::MergeTypesInCargo()`, so mining, probes and loot that use it are protected too.
- **Light drones reach their orbit and the bay.** The client brakes hard within one second's travel of its goal, so a Warrior at 5 km/s crawled the last kilometre or two at a few m/s. Drones now fly at full speed while far away and step their speed down as they close in. Fast drones break orbit to chase only once well outside it.
- **Electronic warfare drones affect their target.** They were visual only. Each now applies its own attributes (as in its info window) to whatever it engages, and the change is undone when it stops, is recalled or dies:
  - **Target painters:** the target's signature radius grows by the drone's bonus (+4% / +8% / +20%).
  - **Sensor dampeners:** the target's lock range and scan resolution are multiplied by 0.88.
  - **Tracking disruptors:** the target's turret tracking, optimal range and falloff are reduced. For an NPC this is its own guns, which re-read their stats.
  - **Neutralizers:** they drain 5 / 10 / 25 GJ of the target's capacitor every 6 seconds.
  - **ECM drones:** they roll a jam every cycle. The chance is the drone's jam strength for the target's sensor type divided by the target's sensor strength. A jammed ship or NPC loses its locks and can't lock until the cycle ends (20 s), and the pilots involved get a message. This adds jamming to `TargetManager` (`Jam()`, `IsJammed()`), which ECM modules could use later.
- **Fighter bombers hit.** They missed every shot: bombers have no turret stats (no optimal, falloff or tracking), only a missile range (`entityAttackRange`, 20 km), so the drone hit formula counted every target as out of range. A bomber now always hits within that range and never beyond it. Damage scales like a missile's, by the target's signature radius and speed against the bomber's explosion radius and velocity, so big slow capitals take full damage. They orbit at their 7.5 km fly range instead of being pulled in under 1 km.
- **Fighter bombers attack only capitals, stations and structures.** That means ships with `isCapitalSize` (carriers, dreadnoughts, supercarriers, titans, freighters, jump freighters, the Rorqual), NPC mission carriers and Sansha incursion capitals, and stations, starbase and sovereignty structures. Ordering them onto anything else returns an error, and they skip other ships when aggressive, guarding or assisting (`DroneSE::CanAttack()`).
- **Fighter damage bonuses.** The Fighters and Fighter Bombers skills give +20% damage per level. Carrier hull bonuses follow the ship's own effects: +5% per Gallente Carrier level for Thanatos fighters and Nyx fighters and bombers, and +100% for the Revenant's fighters and bombers.

The fighter bomber changes and damage bonuses were tested in game on 11 October 2026. Bombers hit capitals with every volley and refuse other targets.

### Known issues

- **Fighter bomber damage is too uniform.** In testing, every bomber volley on a capital did full damage with the same hit message. That is what the missile formula gives against big, slow capitals, but the damage application needs tuning later.

## 2026-10-10 to 2026-10-11: Movement (by Renn)

From Renn's [`fix/movement`](https://github.com/rennr/evemu_Crucible/tree/fix/movement) branch. Credit for this section goes to Renn.

- **The server moves ships the same way the game client does.** `DestinyManager` was rewritten to run the Crucible client's own movement simulation (destiny.dll), tick for tick, using the client's formulas. Warp, approach, orbit, keep at range, align and stop now match what the client shows. That removes the server-and-client drift behind warp "bounces", ships landing in the wrong place and odd NPC movement.
- **Collisions and missiles.** Ships collide like the client's (`DestinyCollision.cpp`). Missiles fly and hit through the same simulation.
- **Autopilot.** It warps to 15 km from gates, as in Crucible. Setting a speed no longer starts a stopped ship moving, which the client treated as the pilot taking control, so it switched the autopilot off.
- **Build.** CMake 3.16 or later, and Boost found through its CMake package config (needed with newer CMake and Boost releases).

Fix on top of Renn's movement:

- **Gate jumps no longer switch the autopilot off.** Jumping stopped the ship with a Stop command sent to the client while it was still in the old system. The client takes a Stop for its own ship as the pilot taking control. The ship now stops quietly when it jumps.

