# Mission content (F12)

Agent mission content for the Crucible server:  encounter and mining missions with their deadspace pockets, built from
the [eve-survival.org](http://eve-survival.org) mission reports **as the Wayback Machine archived them before the
Crucible release (2011-11-29)**.  The reports are community content:  only derived facts (npc types and counts, pockets,
waves, objectives) are committed, never the pages themselves (`cache/` is git-ignored).

## Pipeline

All tools are Python 3, standard library only.  Run them with `python -I` from anywhere.

1. `fetch_wayback.py [--cutoff 20111129] [--out cache/] [--delay 4]`
   One CDX query for every archived `eve-survival.org/wikka.php` page up to the cutoff, then the index pages
   (`MissionReports`, `MissionReportsLevel1..5`, `MissionReportsMining`, `CourierMissionsList`) and every report page
   they link, each at its latest snapshot <= the cutoff (raw `id_` form).  Throttled (the Wayback Machine answers 503
   at ~1 request/s), retried with backoff, resumable.  `--mirror` takes pages without a snapshot from the 2017 GitHub
   mirror (they stay disabled).  About 45 minutes for ~650 pages.
2. `parse_reports.py [--cache cache/] [--aliases aliases.json] [--out out/reports.json]`
   Title/level, enemy faction, type, space type, pockets, groups/waves and their ship lines resolved to `invTypes`
   (`sql/base/*.sql.gz`), structures, asteroids, wave triggers, the objective and its confidence, the client title
   messageID (`agtMissions`/`qstMining`/`qstCourier` names).  Unresolved names go to `out/unresolved.txt`.
   `out/reports.json` keeps text from the pages, so it is not committed (re-run the fetch and parse to rebuild).
3. `build_content.py [--reports out/reports.json] [--overrides overrides.json] [--out <migration>] [--json out/missions.json]`
   Writes `sql/migrations/20261011136100-F12-mission-content.sql` (msnMissions rows and the F13 dungeon rows of every
   pocket), `out/missions.json` (the dungeon contract, one entry per mission) and `out/review.txt` (one line per mission
   for curation).  Deterministic:  the geometry is seeded with the contentID.

## Curation

* `aliases.json`:  `"name on the page": "invTypes typeName"` (or a typeID, or `null` to ignore a name).  Add entries for
  the names in `out/unresolved.txt` that are real npcs.
* `overrides.json`:  per page key, applied after parsing.  Keys:  `enabled` (bool), `objective`
  (`{"type": "KillAll|KillTrigger|DestroyStructure|Fetch|Mine|Trade", "typeID": n, "qty": n}`), `name`, `level`,
  `enemyFactionID`, `typeID`, `storyline`, `spaceType`, `nameID`, `agentRaceMask`, `briefingText`, `rewardISK`,
  `bonusISK`, `rewardLP`, `rewardScale`.
* Read `out/review.txt` after a build:  missions whose objective was only guessed (`default`) are killed-all;  pages with
  mechanics the server can't do (talk-to chains, later parts of multi-part missions, courier/COSMOS/arc pages) are
  generated disabled.

## Generated data

* `msnMissions.contentID` 1..99999 (sorted page key order), `dunDungeons.dungeonID` = 101000000 + contentID
  (archetype 1 Mission, spawnWeight 0:  never picked as an anomaly).
* `dunRooms` 2000000+, `dunRoomObjects` 6000000+, `dunSpawnGroups`/`dunTriggers`/`dunTriggerEvents` 2000000+,
  `dunLoot` group 2000000 + contentID (fetch items).
* Pockets:  wave 0 is there on arrival, later waves are spawned by the TRIGGER ship of the wave before (or when it is
  cleared, attacked, or after a timer), only the first variant of a wave is used.  The objective npc/structure fires
  MissionCompletion when destroyed, kill-all missions complete when the last pocket is cleared, fetch items drop from the
  objective's carrier.  Multi-pocket missions get an acceleration gate per pocket with the mission level's ship
  restrictions (L1:  up to cruisers, L2: up to battlecruisers, L3+: no capitals).
* Briefings are generated English text (the client shows strings); titles use the client's localized name when the
  report's title matches one.
* Rewards are left to the server's `<missions>` config (no period source has the per-mission numbers).
