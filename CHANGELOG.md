# Changelog: movement and drones

Changes on top of [EvEmu-Project/evemu_Crucible](https://github.com/EvEmu-Project/evemu_Crucible) `master` at `f55b488` ("Streamline docker (#318)").

This file comes from AzediZ's drones repository (`master` up to `2e131b9`), merged into this branch. The rest of this branch's work (F01-F24, the NPC AI and the market simulation) is described in `doc/TODO_COMPLETION_REPORT.md` and the plans under `doc/code_and_design_notes/`.

## Movement (by Renn)

From Renn's [`fix/movement`](https://github.com/rennr/evemu_Crucible/tree/fix/movement) branch. Credit for this section goes to Renn. This branch already had these commits (the same patches) before the merge.

- **The server moves ships the same way the game client does.** `DestinyManager` was rewritten to run the Crucible client's own movement simulation (destiny.dll), tick for tick, using the client's formulas. Warp, approach, orbit, keep at range, align and stop now match what the client shows. That removes the server-and-client drift behind warp "bounces", ships landing in the wrong place and odd NPC movement.
- **Collisions and missiles.** Ships collide like the client's (`DestinyCollision.cpp`). Missiles fly and hit through the same simulation.
- **Autopilot.** It warps to 15 km from gates, as in Crucible. Setting a speed no longer starts a stopped ship moving, which the client treated as the pilot taking control, so it switched the autopilot off.
- **Build.** CMake 3.16 or later, and Boost found through its CMake package config (needed with newer CMake and Boost releases).

Fix on top of Renn's movement (AzediZ):

- **Gate jumps no longer switch the autopilot off.** Jumping stopped the ship with a Stop command sent to the client while it was still in the old system. The client takes a Stop for its own ship as the pilot taking control. The ship now stops quietly when it jumps.

## Drones

Renn's drone implementation (player control of drones and fighters: engage, mine, return, guard, assist, abandon, reconnect and delegate). Drones only engage targets the pilot has locked, as in Crucible. This branch already had it too, and built on it: crime for drone actions, neutralizer and ECM drones (F05), mining drones on the shared ore path (F14), and drones as NPC targets (NPC AI plan).

AzediZ's fixes on top of Renn's drones, as merged here:

- **Drones no longer vanish when they return to the bay.** A returning drone joined the stack of the same type in the bay. If every drone of that type had been launched, the only "stack" was the drone itself, and merging it into itself deleted it. This hit both Return to Drone Bay and Scoop to Drone Bay. The fix is in `InventoryItem::MergeTypesInCargo()`, so mining, probes and loot that use it are protected too.
- **Light drones reach their orbit and the bay.** The client brakes hard within one second's travel of its goal, so a Warrior at 5 km/s crawled the last kilometre or two at a few m/s. Drones now fly at full speed while far away and step their speed down as they close in. Fast drones break orbit to chase only once well outside it.
- **Electronic warfare drones affect their target.** Damp, tracking disruptor and painter drones were visual only. They now put their own values (as in their info window) into the target's EWAR state, the same store the modules use, so stacking penalties, the client's EWAR icons and EWAR immunity apply, and the effect ends when the drone stops, is recalled or dies:
  - **Target painters:** the target's signature radius grows by the drone's bonus (+4% / +8% / +20%).
  - **Sensor dampeners:** the target's lock range and scan resolution are multiplied by 0.92 / 0.88 / 0.75.
  - **Tracking disruptors:** the target's turret tracking and optimal range are multiplied by 0.95 / 0.88 / 0.75 (an NPC's own guns too). Falloff is not changed: the EWAR state has no falloff multiplier, as for the modules.
  - AzediZ's version changed the target's attributes directly; this branch's EWAR state replaces that. His neutralizer and ECM drones (with a `TargetManager::Jam()`) are not merged: this branch already had both (F05), with the client's jam formula (range and strength rolls) and the client's jam timers.
- **Fighter bombers hit.** They missed every shot: bombers have no turret stats (no optimal, falloff or tracking), only a missile range (`entityAttackRange`, 20 km), so the drone hit formula counted every target as out of range. A bomber now always hits within that range (surface to surface) and never beyond it. Damage scales like a missile's, through the same formula missiles use, by the target's signature radius (with painters) and speed against the bomber's explosion radius and velocity, so big slow capitals take full damage. They orbit at their 7.5 km fly range instead of being pulled in under 1 km.
- **Fighter bombers attack only capitals, stations and structures.** That means ships with `isCapitalSize` (carriers, dreadnoughts, supercarriers, titans, freighters, jump freighters, the Rorqual), NPC mission carriers and Sansha incursion capitals, and stations, starbase and sovereignty structures. Ordering them onto anything else returns an error, and they skip other ships when aggressive, guarding or assisting (`DroneSE::CanAttack()`).
- **Fighter damage bonuses.** The Fighters and Fighter Bombers skills give +20% damage per level. Carrier hull bonuses follow the ship's own effects: +5% per Gallente Carrier level for Thanatos fighters and Nyx fighters and bombers, and +100% for the Revenant's fighters and bombers.

AzediZ tested the fighter bomber changes and damage bonuses in game on 11 October 2026 (on his repository). Bombers hit capitals with every volley and refuse other targets.

### Known issues

- **Fighter bomber damage is too uniform.** In testing, every bomber volley on a capital did full damage with the same hit message. That is what the missile formula gives against big, slow capitals, but the damage application needs tuning later.
