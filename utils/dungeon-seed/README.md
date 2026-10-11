# Dungeon seed generator

`gen_dungeon_seed.py` writes `sql/migrations/20261011133100-F13-dungeon-seed.sql`: the combat anomalies, unrated
complexes and exploration sites used by the server's dungeon engine (`system/cosmicMgrs/DungeonMgr.cpp`).

It reads the legacy dungeon tables in `sql/base` (`dunTemplates`, `dunRoomData`, `dunGroupData`, `npcSpawnClass`,
`invTypes`, `invGroups`) and, optionally, the client's localization from `resLocalization.stuff` to give each dungeon
the messageID of its client name (shown in the journal).

```
python -I utils/dungeon-seed/gen_dungeon_seed.py --loc "<EVE client>/resLocalization.stuff"
```

The output is deterministic.  Change the generator rather than the generated SQL, run it again and commit both.
Applying it to a database that already has the previous version: migrate the seed down, then up again.

What it builds:
- combat anomalies (templates 7xxxx):  one per pirate faction (one for rogue drones), one room with the legacy
  deco, one npc group per `npcSpawnClass` wave of the template's class.  Each wave's death spawns the next; the last
  completes the site.  True security window from the template's band digit, difficulty from the class (1-10).
- unrated complexes (8xxxx):  2-4 rooms joined by open acceleration gates, 1-2 waves per room from the combat class of
  the same difficulty, an overseer group (commander class) in the last room whose death completes the site.
- exploration sites (2xxxx gravimetric, 3xxxx magnetometric, 4xxxx radar, 5xxxx ladar):  one room with the legacy
  deco (rocks, containers, structures), no npcs.  Sites without any legacy objects are never picked at random.
- escalation rules:  anomalies of difficulty 4+ may lead to an unrated complex (5%), complexes to a harder one (50%).
