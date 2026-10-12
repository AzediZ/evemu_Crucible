# GM commands

The server has 97 slash commands. Type them in a chat window, starting with `/` (for example `/tr me home`). The
client sends them to the server's `slash` service, as do the client's GM menus and `utils/config/gmtools.py`. The server registers them from `src/eve-server/admin/AllCommandsList.h`;
the implementations are in `GMCommands.cpp`, `SystemCommands.cpp`, `debug_commands.cpp`, `DungeonCommands.cpp` and
`GMToolCommands.cpp` in the same folder. In game, `/commandlist` lists them with their roles.

## Parameter notation

- `(x)` is required, `[x]` is optional, and `a|b` means one of the alternatives.
- Plain words such as `me`, `all`, `list` or `deploy` are typed literally.
- Wrap arguments that contain spaces in double or single quotes, for example `/giveskill "Some Pilot" 3300 5`.
- `me` usually means your active ship or your character, depending on the command (see each row).
- Command names are not case-sensitive.

## Permissions

A command runs only if the account's role (`account.role`, a bit mask from `Acct::Role` in
`src/eve-common/EVE_Roles.h`) has **every** bit of:

1. `SLASH`: needed for every slash command (`SLASH = VIP | GML | LEGIONEER | SPAWN | HEALOTHERS`); and
2. the role in the command's "Permissions required" column.

The role presets that satisfy each required role:

| Required role | Lowest preset that has it | Presets |
|---|---|---|
| `PLAYER`, `VIP`, `HEALSELF`, `GML`, `LEGIONEER`, `SPAWN` | `SLASH` | `SLASH`, `CREATOR`, `DEV`, `BOSS` |
| `GMH`, `CONTENT` | `CREATOR` (`SLASH \| GMH \| WORLDMOD \| CONTENT`) | `CREATOR`, `DEV`, `BOSS` |
| `PROGRAMMER` | `DEV` (`CREATOR \| QA \| PROGRAMMER`) | `DEV`, `BOSS` |
| `ADMIN` | `BOSS` (`DEV \| ADMIN`) | `BOSS` |

So a `PLAYER`-role command still needs `SLASH`; an ordinary player account (`STD`/`VIP`) can't use any of them.

Some commands that move ships or add objects to your bubble (`goto`, `hop`, `warpto`, `bubblewarp`, `update`,
`addball`, `addball2`, `sendstate`, `syncpos`, `syncloc`, `spawn`, `spawnn`, `entity`, `cloak`, `pos`, `sov`) are refused
while you are warping. `/tr` and `/halt` stay allowed so you can get out of a broken warp.

## Commands

| Command | Description | Parameters | Description of each parameter | Permissions required |
|---|---|---|---|---|
| `/addball` | Sends your client the bubble's AddBalls packet (resyncs the balls around you). | none | - | `PLAYER` |
| `/addball2` | Sends your client the bubble's AddBalls2 packet. | none | - | `PLAYER` |
| `/attrlist` | Shows the current attributes of an item. | `(itemID)` | itemID: the item to list. | `PROGRAMMER` |
| `/autostop` | Toggles module auto-stop for you: repairers, boosters and similar modules stop when their target is full. | none | - | `PLAYER` |
| `/ban` | Bans the character's account from the server. | `(charID\|charName)` | charID: an online character.<br>charName: a character name (online or offline); a two-word name can be written without quotes. | `ADMIN` |
| `/baniskspammer` | Gags an ISK spammer in chat and bans their account. | `(charID)` | charID: an online character. | `GML` |
| `/beltlist` | Lists all asteroids in the inventory of your current belt. | none | - | `PLAYER` |
| `/bindList` | Lists the current bound objects with their client counts, or only those one character holds. | `[me\|charID\|all]` | me / charID: list only the objects that character holds (must be online).<br>all or nothing: list every bound object. | `PROGRAMMER` |
| `/bubblelist` | Shows the objects in your bubble with their destiny state and speed. | none | - | `PROGRAMMER` |
| `/bubbletrack` | Marks bubble outlines in space so you can see them. | `[help\|bubble\|system\|universe]` | none / bubble: mark your current bubble.<br>system: mark every bubble in your system.<br>universe: mark every bubble in every system.<br>help: show the usage. | `PROGRAMMER` |
| `/bubblewarp` | Warps your ship to the centre of a bubble in your system. | `(bubbleID)` | bubbleID: a bubble in your current system. | `GMH` |
| `/cargo` | Lists the contents and volumes of all your ship's holds. | none | - | `PLAYER` |
| `/chatban` | Gags a character in every chat channel, online or offline. Lift it with /ungag. | `(charID\|"name") [days\|perm] ["reason"]` | charID / "name": the character (quote names with spaces).<br>days: length of the ban in days (default: permanent).<br>perm: a permanent ban.<br>"reason": recorded with the ban and shown to the character if online. | `GML` |
| `/cloak` | Instantly and unconditionally toggles your ship's cloak (GM cloak). If a cloaking module is running it is stopped instead. | none | - | `VIP` |
| `/commandlist` | Lists all slash commands with their required role and description. | none | - | `PLAYER` |
| `/create` | Creates an item in your cargo hold (in space) or station hangar (docked). | `(typeID\|"Type Name") [qty]` | typeID / "Type Name": the type; a name must match one type exactly if several match.<br>qty: how many (default 1).<br>A trailing [where] shown in the usage text is ignored. | `VIP` |
| `/createitem` | Same as /create. | `(typeID\|"Type Name") [qty]` | typeID / "Type Name": the type to create.<br>qty: how many (default 1). | `ADMIN` |
| `/crime` | Commits a crime for testing CONCORD. A pilot attacks the victim through the normal crime rules; an NPC or other entity is simply made a criminal. Needs CrimeWatch enabled and a non-null-sec system. | `(me\|charID\|entityID) [victimID]` | me / charID: the pilot who commits the crime (online, in space).<br>entityID: an NPC or other entity in your system to flag as criminal.<br>victimID: the victim, in the aggressor's system (default: you, or a civilian when the aggressor is you). | `GML` |
| `/destinyvars` | Shows some of your ship's current destiny variables. | none | - | `GMH` |
| `/dogma` | Sets a live attribute value until the next refit or undock (like /setattr, with an '='). | `(itemID\|me) (attributeName\|attributeID) = (value)` | itemID / me: the item, or your ship.<br>attributeName / attributeID: the attribute.<br>=: a literal equals sign.<br>value: the new number. | `ADMIN` |
| `/dropLoot` | Makes every NPC in a bubble jettison its loot. | `(bubbleID)` | bubbleID: a bubble in your current system. | `PROGRAMMER` |
| `/entity` | Spawns NPCs next to you (the client's /entity deploy form; runs /spawn). | `deploy (count) (typeID\|"type name")` | deploy: literal keyword.<br>count: how many to spawn.<br>typeID / "type name": the NPC type; a name must match one type exactly if several match. | `SPAWN` |
| `/fit` | Creates a module and fits it to a character's active ship. | `(me\|charID) (typeID)` | me / charID: the pilot (online, not in a capsule).<br>typeID: the module type to create and fit. | `VIP` |
| `/fleetboost` | Shows detailed fleet boost information for you. | none | - | `PLAYER` |
| `/fleetinvite` | Sets your fleet's Fleet Finder invite scope. You must be fleet boss. | `(none\|corp\|alliance\|militia\|public\|all\|0-15)` | none: remove the advert.<br>corp / alliance / militia (or faction) / public / all: who may join; several words can be joined with commas, e.g. corp,alliance.<br>0-15: the raw invite bit mask. | `PLAYER` |
| `/freesp` | Adds unallocated skill points to a character, or takes them with a negative amount. | `(charID\|me) (amount)` | charID / me: the character.<br>amount: skill points (non-zero; negative takes). | `GMH` |
| `/gag` | Stops a character talking in chat channels. | `(charID\|name) ["reason"] [minutes]` | charID / name: the character (online).<br>"reason": optional reason.<br>minutes: length of the gag (default 30). | `GML` |
| `/gagiskspammer` | Gags an ISK spammer in chat for 30 days. | `(charID)` | charID: an online character. | `LEGIONEER` |
| `/getattr` | Shows an item's current attribute value and its type's default value. | `(itemID\|me\|char) (attributeID\|name)` | itemID: a player item, an online character, or an NPC/structure/etc. in your system.<br>me: your ship.<br>char: your character.<br>attributeID / name: the attribute. | `VIP` |
| `/getposition` | Shows position data between your ship and the nearest moon (for POS placement testing). | none | - | `PLAYER` |
| `/giveallskills` | Gives ALL skills to an online character. | `(me\|charID\|"name")` | me / charID / "name": the character (online). | `ADMIN` |
| `/giveisk` | Gives ISK to a character. | `(charID\|me) (amount)` | charID / me: the recipient.<br>amount: ISK to give (non-zero). | `GMH` |
| `/givelp` | Adds loyalty points to a character for one corporation. | `(me\|charID) (corporationID) (amount)` | me / charID: the character (online).<br>corporationID: the corporation the LP is for.<br>amount: LP to add (positive). | `ADMIN` |
| `/giveskill` | Gives a skill to a character at the given level. | `(me\|charID\|"name") (skillID) (level)` | me / charID / "name": the character (online).<br>skillID: the skill's typeID.<br>level: the level to train it to (0-5). | `VIP` |
| `/giveskills` | Same as /giveskill (kept for the client's macros). | `(me\|charID\|"name") (skillID) (level)` | Same parameters as /giveskill. | `ADMIN` |
| `/goto` | Moves your ship to a point in your current system and stops it. | `(x) (y) (z)` | x, y, z: coordinates in meters. | `VIP` |
| `/halt` | Immediately stops your ship (destiny state halt). | none | - | `VIP` |
| `/heal` | Repairs a ship, entity or item. With no argument repairs your own ship. | `[charID\|shipID\|moduleID\|entityID]` | none: your ship.<br>charID: that online character's ship.<br>entityID: an NPC, structure, drone, etc. in your system (shield, armor and hull restored).<br>shipID / moduleID: that item's damage is repaired. | `HEALSELF` |
| `/healtarget` | Repairs a ship. Note: despite the name, it does not use your target; with no argument it repairs your own ship. | `[charID]` | none: your ship.<br>charID: that online character's ship. | `HEALSELF` |
| `/heat` | Shows or sets a ship's rack heat. With no arguments shows yours. | `[hi\|med\|low\|all] [percent] [me\|charID]` | hi / med / low / all: the rack to set.<br>percent: heat level to set (0-100).<br>me / charID: whose ship (default you). Given alone, shows that pilot's heat. | `VIP` |
| `/hop` | Moves your ship the given distance along its current heading. | `(distance)[m\|km\|au]` | distance: how far, up to 100 AU; the unit may be attached or a separate word (default meters). | `CONTENT` |
| `/inventory` | Lists every item in your current location's inventory (station or solar system). | none | - | `PROGRAMMER` |
| `/kick` | Disconnects a character from the server. | `(charID\|charName)` | charID: the character.<br>charName: the name; a two-word name can be written without quotes. | `ADMIN` |
| `/kill` | Destroys a ship, NPC, drone, structure or container in your system. A charID kills that pilot's ship. | `(itemID\|charID\|me)` | itemID: an entity in space in your system (not a static object).<br>charID: an online pilot in space.<br>me: your own ship. | `VIP` |
| `/killallnpcs` | Instantly destroys every NPC ship in your bubble. | none | - | `ADMIN` |
| `/list` | Lists all dynamic entities and players in your bubble with their destiny state. | none | - | `PLAYER` |
| `/load` | Creates an item in your cargo hold or hangar (the client's /load form). | `me (typeID\|"Type Name") [qty]` | me: literal word (any value is accepted and ignored).<br>typeID / "Type Name": the type to create.<br>qty: how many (default 1). | `VIP` |
| `/location` | Shows your current location in space. | none | - | `PLAYER` |
| `/loop` | Runs another slash command several times. | `(count) (command...)` | count: number of runs (max 100).<br>command...: the command to run, e.g. /loop 3 /fit me 2048. /loop can't run /loop. | `VIP` |
| `/missioncomplete` | Completes the kill objective of an accepted mission. | `(agentID) [charID]` | agentID: the mission's agent.<br>charID: the character (default you). | `GML` |
| `/mktsim` | The simulated market (doc/code_and_design_notes/market-sim/PLAN.md): NPC corporations quoting the hubs, npc vendors, its books. | `status \| pause \| resume \| cell (typeID) [stationID] \| actor (actorID) \| actors [kind] \| anchor (typeID) (price) [regionID] \| shock (typeID\|g:groupID\|c:categoryID) (demandMult) (days) \| requote (typeID\|all) \| seed [stationID] \| audit [days] \| step (ticks) \| purge \| purgelegacy \| export (file)` | status: clock, cells, actors, orders, money against its target, the last audit residuals.<br>pause / resume: stop / restart its processing (orders stay).<br>cell: fair value, anchor, stock, demand and quotes of a type at the hubs (or one station).<br>actor / actors: wallet, escrow and stock of an actor; the actor list (kind 1 market maker, 2 producer, 3 consumer, 4 vendor).<br>anchor: override a type's anchor price (0 removes it; logged in the audit).<br>shock: multiply demand for a type, group or category for some days (logged).<br>requote: re-quote a type or everything now.<br>seed: top a node (or all) up to its target stock and quote it at once.<br>audit: the daily books (faucets, sinks, player crossings, residuals) of the last days.<br>step: run the sim ahead by economic ticks, at most a day per command (`PROGRAMMER` on a test server only; the offset is not kept over a restart, and the main loop waits meanwhile).<br>purge: remove all the sim's orders and pause it.<br>purgelegacy: remove npc orders nothing backs (old seed scripts, Trader Joe).<br>export: cells and audit as csv into the log folder. | `GMH` |
| `/simfacility` | The simulated market's use of npc station assembly lines (market-sim PLAN.md 6.6): at most `MaxLineShare` of a station's lines of an activity, `MinFreeLinesPerStation` always free, and a player who picks a line the sim holds gets it at once. | `status \| station (stationID) \| reserve (stationID) (activity) (jobs) (hours) \| release (jobID\|test)` | status: the settings, the sim's jobs (on lines, without), pending line writes.<br>station: per activity (1 manufacturing, 3 PE, 4 ME, 5 copying, 8 invention) the lines the sim may use, free ones, the sim's jobs, its cap and how many more it may start.<br>reserve: GM test jobs that hold lines like the sim's (they keep to the same limits; not kept over a restart, their lines free up at their end).<br>release: a test job, or all of them (`test`). reserve and release need `PROGRAMMER`. | `GMH` |
| `/nukem` | Kills every NPC in your bubble (same as /killallnpcs). | none | - | `ADMIN` |
| `/online` | Onlines all modules on a ship. | `(me\|charID\|shipID)` | me: your ship.<br>charID: that character's ship.<br>shipID: that ship. | `VIP` |
| `/players` | Lists the online players. | none | - | `PLAYER` |
| `/pop` | Sends yourself an OnRemoteMessage popup (for testing client messages). | `(type) (key) (value)` | type: the message type, e.g. CustomNotify, ServerMessage or CustomError.<br>key: the argument name the message uses (e.g. notify, msg, error).<br>value: the text. | `ADMIN` |
| `/pos` | Changes a starbase structure's state immediately, or fills a control tower's fuel and strontium bays. | `(online\|offline\|anchor\|unanchor) (itemID) \| fuel [towerID] [hours]` | online / offline / anchor / unanchor: the state change.<br>itemID: the structure.<br>fuel: fill a tower's fuel bay and strontium bay.<br>towerID: the tower (default: the nearest tower in your bubble).<br>hours: hours of fuel to add (default: fill the bays). | `PROGRAMMER` |
| `/printdungeon` | Not implemented (always reports that the command is incomplete). | none | - | `CONTENT` |
| `/recustomize` | Sets a character's paperdoll (appearance) state. | `(charID\|me) (state)` | charID / me: the character.<br>state: 0 none, 1 resculpt, 3 full recustomization (bloodline), 4 forced at login. | `GMH` |
| `/removedungeon` | Not implemented (always reports that the command is incomplete). | none | - | `CONTENT` |
| `/repairmodules` | Repairs all modules on a ship. With no argument repairs your own. | `[charID]` | none: your ship.<br>charID: that online character's ship. Note: the help text also names an (itemID); passing a second argument makes the command do nothing. | `HEALSELF` |
| `/runtest` | Runs the developer position test (testing::posTest). You must be in space. | none | - | `PROGRAMMER` |
| `/savedungeon` | Meant to save a dungeon to the database. Not implemented: only 'help' works. | `(dungeonID\|help)` | dungeonID: the dungeon to save.<br>help: show the usage. | `CONTENT` |
| `/search` | Searches item types by name. More than 10 results are sent to you by EVE mail. | `(text)` | text: part of a type name. | `VIP` |
| `/secstatus` | Shows your character's current security status. | none | - | `PLAYER` |
| `/sendstate` | Sends your client a destiny SetState for your bubble. | none | - | `CONTENT` |
| `/setattr` | Sets a live attribute value until the next refit or undock. | `(itemID\|me\|char) (attributeID\|name) (value)` | itemID: a player item, an online character, or an NPC/structure/etc. in your system.<br>me: your ship.<br>char: your character.<br>attributeID / name: the attribute.<br>value: the new number. | `CONTENT` |
| `/setbpattr` | Changes a blueprint's attributes. | `(itemID) (copy) (materialLevel) (productivityLevel) (runs)` | itemID: the blueprint.<br>copy: 0 original, 1 copy.<br>materialLevel: ME level.<br>productivityLevel: PE level.<br>runs: licensed production runs remaining. | `CONTENT` |
| `/shipdna` | Shows your current ship DNA string. | none | - | `PLAYER` |
| `/shipinventory` | Lists every item in your current ship's inventory. | none | - | `PLAYER` |
| `/shipvars` | Shows more of your ship's current destiny variables. | none | - | `GMH` |
| `/showall` | Toggles showing every dynamic item in the system on your ship scanner. | none | - | `GMH` |
| `/showsession` | Lists your current session values. | none | - | `PLAYER` |
| `/shutdown` | Saves all items, disconnects everyone and halts the server, now or after an announced countdown. | `[minutes [reason] \| cancel]` | none or 0: shut down now.<br>minutes: announce a shutdown in that many minutes.<br>reason: text shown with the announcement.<br>cancel: cancel an announced shutdown. | `GMH` |
| `/siglist` | Lists all active cosmic signatures with location, name and type. | none | - | `CONTENT` |
| `/skilllist` | Lists all skills loaded for your character. | none | - | `PROGRAMMER` |
| `/sov` | Finishes a sovereignty structure's current anchoring or onlining now. | `complete (itemID)` | complete: literal keyword.<br>itemID: the sovereignty structure. | `CONTENT` |
| `/spawn` | Spawns NPCs or other entities next to you. You must be in space. | `(typeID) [count] [x y z]` | typeID: the type to spawn.<br>count: how many (default 1, max 100).<br>x y z: offset from your ship in meters (all three needed). | `SPAWN` |
| `/spawndungeon` | Meant to spawn a dungeon in your system. Not implemented (always reports that the command is incomplete). | `(dungeonTemplateID)` | dungeonTemplateID: the dungeon template. | `CONTENT` |
| `/spawnn` | Spawns several entities spread around you (the client's WM: /Spawn this type). | `(count) (spread) (typeID)` | count: how many (default 1 if not a number).<br>spread: radius in meters to scatter them within.<br>typeID: the type to spawn. | `ADMIN` |
| `/status` | Shows detailed status information for your ship. | none | - | `PLAYER` |
| `/syncloc` | Resynchronizes your client's position with the server's. | none | - | `PLAYER` |
| `/syncpos` | Resynchronizes the positions of all dynamic entities in your bubble for every player in it. | none | - | `PLAYER` |
| `/targlist` | Shows your ship's current target list. | none | - | `PROGRAMMER` |
| `/token` | Adds an item to the redeem list of a character's account. | `(charID\|me) (typeID) [quantity]` | charID / me: the character whose account gets the item.<br>typeID: the item type.<br>quantity: how many (default 1). | `GMH` |
| `/tr` | Translocates pilots: moves them instantly to a pilot, station, system, celestial, item or coordinates. '/tr help' shows the full usage. | `(who) [where] [offset=x,y,z \| offset=randvec(distance)] \| [who] (systemID) (x) (y) (z)` | who: me, fleet (your fleet's online pilots), charID, shipID/itemID (its owner) or "pilot name". With one argument, that argument is where and you are moved.<br>where: me, home, last, moon, planet, charID, stationID, solarSystemID, celestialID, stargateID, an itemID in your system, or a "pilot, system or station name".<br>offset=x,y,z: move that many meters away from the destination; offset=randvec(100km) picks a random direction. With no where, moves within the system.<br>systemID x y z: move to coordinates in a system.<br>noblock: accepted and ignored. | `VIP` |
| `/track` | Toggles server-side movement tracking of objects. | none | - | `PROGRAMMER` |
| `/translocate` | Same as /tr. | `(who) [where] [offset=...]` | Same parameters as /tr. | `VIP` |
| `/unban` | Removes the ban from a character's account. | `(charID\|charName)` | charID: the character.<br>charName: the name; a two-word name can be written without quotes. | `ADMIN` |
| `/ungag` | Lets a gagged or chat-banned character talk again. | `(charID\|name)` | charID / name: the character. | `GML` |
| `/unkillable` | Toggles whether something can be destroyed. Unkillable things stop at 1 structure HP. | `(itemID\|charID\|me) [on\|off]` | itemID: an entity in space in your system.<br>charID: an online pilot's ship.<br>me: your ship.<br>on / off: set instead of toggling. | `VIP` |
| `/unload` | Unloads (unfits) a module, every module of a type, or all modules from a pilot's ship. | `(me\|charID) (itemID\|typeID\|all)` | me / charID: the pilot.<br>itemID: the fitted module to unload.<br>typeID: unload every fitted module of that type.<br>all: unload every module. | `VIP` |
| `/unspawn` | Removes and deletes an item in space, or everything of a group or category within range. Ships with a pilot are never removed. | `(itemID) \| [range=meters] only=(group or category)` | itemID: the entity to delete (not static, not piloted).<br>range=meters: search radius (default 500 km).<br>only=: groupWreck, groupCargoContainer, groupSecureCargoContainer, groupAuditLogSecureContainer, groupFreightContainer, categoryDrone, categoryEntity, categoryShip, group&lt;N&gt; or category&lt;N&gt;. | `SPAWN` |
| `/untrash` | Restores a trashed item to where it was trashed from, or lists an owner's newest 50 trashed items. | `(itemID) \| list (me\|charID\|corpID)` | itemID: the trashed item.<br>list: list trashed items.<br>me / charID / corpID: whose items to list. | `GML` |
| `/update` | Resets your position to the server's, then resends your bubble's balls and destiny state (refreshes your space view). | none | - | `PLAYER` |
| `/war` | War testing tools. | `list [ownerID] \| declare (declarerID) (againstID) \| retract (warID) \| mutual (warID) (0\|1) \| start (warID) \| end (warID)` | list [ownerID]: list wars (optionally of one corp or alliance).<br>declare: declarerID declares on againstID, skipping the vote and fee.<br>retract: retract a war.<br>mutual (0\|1): set or clear mutual.<br>start: skip the 24 hour spool-up.<br>end: end the war now (24 hour grace).<br>warID / ownerID / declarerID / againstID: IDs of wars, corporations or alliances. | `GML` |
| `/warpto` | Warps your ship now, ignoring warp scramblers. | `(itemID\|bookmarkID\|x y z) [distance]` | itemID: something in space in your system.<br>bookmarkID: a bookmark in your system.<br>x y z: coordinates in meters.<br>distance: warp-to distance, with an optional unit (m, km, au). | `GMH` |
| `/whois` | Shows the account, corporation, location and status of a character, online or not. | `(charID\|"name")` | charID / "name": the character. | `GMH` |
