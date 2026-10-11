# Changelog: movement and drones

Changes on top of [EvEmu-Project/evemu_Crucible](https://github.com/EvEmu-Project/evemu_Crucible) `master` at `f55b488` ("Streamline docker (#318)").

## Movement (by Renn)

From Renn's [`fix/movement`](https://github.com/rennr/evemu_Crucible/tree/fix/movement) branch. Credit for this section goes to Renn.

- **The server moves ships the same way the game client does.** `DestinyManager` was rewritten to run the Crucible client's own movement simulation (destiny.dll), tick for tick, using the client's formulas. Warp, approach, orbit, keep at range, align and stop now match what the client shows. That removes the server-and-client drift behind warp "bounces", ships landing in the wrong place and odd NPC movement.
- **Collisions and missiles.** Ships collide like the client's (`DestinyCollision.cpp`). Missiles fly and hit through the same simulation.
- **Autopilot.** It warps to 15 km from gates, as in Crucible. Setting a speed no longer starts a stopped ship moving, which the client treated as the pilot taking control, so it switched the autopilot off.
- **Build.** CMake 3.16 or later, and Boost found through its CMake package config (needed with newer CMake and Boost releases).

Fix on top of Renn's movement:

- **Gate jumps no longer switch the autopilot off.** Jumping stopped the ship with a Stop command sent to the client while it was still in the old system. The client takes a Stop for its own ship as the pilot taking control. The ship now stops quietly when it jumps.

## Drones

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
