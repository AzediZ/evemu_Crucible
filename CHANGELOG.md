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
- **Fighter bombers attack only capitals, stations and structures.** That means ships with `isCapitalSize` (carriers, dreadnoughts, supercarriers, titans, freighters, jump freighters, the Rorqual), NPC mission carriers and Sansha incursion capitals, and stations, starbase and sovereignty structures. Ordering them onto anything else returns an error, and they skip other ships when aggressive, guarding or assisting (`DroneSE::CanAttack()`). *Replaced:* Crucible had no such rule (see "Fighters and fighter bombers: the Crucible rules" below).
- **Fighter damage bonuses.** The Fighters and Fighter Bombers skills give +20% damage per level. Carrier hull bonuses follow the ship's own effects: +5% per Gallente Carrier level for Thanatos fighters and Nyx fighters and bombers, and +100% for the Revenant's fighters and bombers.

AzediZ tested the fighter bomber changes and damage bonuses in game on 11 October 2026 (on his repository). Bombers hit capitals with every volley and refuse other targets.

### Fighters and fighter bombers: the Crucible rules

Research into what Crucible actually did, from CCP's patch notes and devblogs and the Crucible client's own data and code (dogma effects, menus, messages), then changes to match:

- **Bombers can attack anything.** The capital-only rule above is gone (`DroneSE::CanAttack()` and its error are removed). CCP never had one: the Incursion devblog says bombers "will be much less effective against sub-capital class ships now and in this scenario you should switch to using fighters" (CCP Chronotis, "Attention on deck", 2010-10-18), and the Incursion patch notes "They do much less damage to sub-capital class ships now" (2010-11-30). The only capital-only rule Crucible added was for doomsdays ("Capital Ship Balancing", CCP Tallest, 2011-10-10). The client has no target restriction for bombers in its data or messages, and its drone menu offers "Engage target" for them like any combat drone. The explosion radius (2250 m) already does the rest: a bomber does about 2% of its damage to a frigate, 10-20% to a battleship and full damage to capitals and control towers.
  - A direct engage order is always obeyed. When bombers pick a target themselves (aggressive, focus fire, guard, assist), they prefer one they hit in full, with a signature at least their explosion radius, if there is one (`DroneSE::PrefersTarget()`).
  - Carriers still cannot use bombers: they have 375 Mbit/s of drone bandwidth and a bomber needs 500, which the launch already checks ("fighters (but not fighter bombers)", EVE University wiki, Capital ships, 2011-10-13).
- **Bomber damage varies like a turret's.** Since Incursion bombers fire "fake missiles" whose "damage output now varies like a turret does rather be constant like old missiles (they can wreck or lightly hit now for example)" (CCP Chronotis, same devblog). Each volley is now the missile explosion factor times the shared turret quality roll: 0.49 to 1.49, or a 3x wrecking hit at `rates.WreckingChance` (`NpcMath::ResolveBomberShot()`, unit tested). The hit message ("lightly hits", "places an excellent hit", "wrecking") comes from that roll, not from the damage, so a good volley on a battleship no longer reads "misses completely". Range (`entityAttackRange`, 20 km), orbit (`entityFlyRange`, 7.5 km) and rate of fire (15 s) are still the bomber's own data. Their `optimalSigRadius` (2500) is not used: nothing says what it did after Incursion. The exact formula CCP used was never published and the client does no hit calculation, so the combination of the two is our reading of the devblog.
- **The universal drone skills no longer boost fighters and bombers.** Drone Interfacing, Drone Navigation and Drone Sharpshooting are "drones requiring Drones" effects in the client's dogma, and fighters (Fighters only) and bombers (Fighter Bombers only) don't require Drones. CCP extended these to fighters only in Kronos: "we are expanding all universal drone bonuses from skills and modules to Fighters and Fighter Bombers" (CCP Fozzie, "Giving drones an assist", 2014-05-30). Mining drones don't require Drones either, so Navigation and Sharpshooting no longer apply to them (their Drone Interfacing yield bonus is a separate character bonus and stays).
- **Fighters use their owner's skills and carrier, even when delegated.** Every fighter and bomber bonus in the client's dogma modifies items owned by the character, and a delegated fighter stays its owner's, so the delegate's skills and ship don't matter. Stats come from the owner and the owner's ship while it is in space in the same system; otherwise the last values are kept. Also applied now:
  - **Revenant:** +10% fighter and bomber speed per Amarr Carrier level, and +100% fighter and bomber shield, armor and structure (the hitpoints go back to normal when the drone returns to a bay).
  - **Carriers and supercarriers:** fighters get 3x their pilot's drone control range ("200% bonus to Fighter control range"). The bonus applies to fighters only: bombers don't require the Fighters skill it keys on.
- **Only fighters can be delegated.** Bombers "have to be directly controlled by the Supercarrier's pilot, unlike standard fighters, and cannot be delegated to anyone else" (Dominion 1.1 patch notes, 2010-01-21), and the client's menu offers Delegate only for fighters. Delegating a bomber or an ordinary drone now returns "does not recognize the command" (`EntityUnknownCommand`; the client has no message of its own for this). The delegate's free drone slots are checked as before (`EntityNoTargetDroneManagementAbilitiesLeft`), and the delegate must be in the fighters' bubble (see known issues).

### Known issues

- ~~**Fighter bomber damage is too uniform.**~~ Fixed: bomber volleys now vary like turret shots (see above). Full damage on capitals was right; the missing part was the Incursion quality roll.
- **Fighters don't warp.** In Crucible fighters followed their controller through warp within the system, and with the "Attack and Follow" setting followed a warping target. Here they wait where they are, like drones, so fighters can only be delegated to a pilot in their own bubble (otherwise the client's "cannot be delegated to someone who the drones cannot warp to", `EntityTargetWarpDisrupted`). The force field rule (`EntityDelegateInsideField`, Revelations 1.4) is not checked.
