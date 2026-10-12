# Client bulkdata sync

These tools compare the server's static tables with the Crucible client's own bulkdata, and write the migrations
that bring the server in line: `sql/migrations/20261012120000..120800-client-sync-*.sql`. The client data is
`bulkdata/*.cache2` and `bulkdata/mapbulk.db` in the client folder (dated 2012-03-21).

```
python -I utils/bulkdata/compare.py --client "<EVE client folder>" [--table invTypes ...]     # report only
python -I utils/bulkdata/gen_client_sync.py --client "<EVE client folder>" [--summary out.txt] # write the migrations
```

| File | Role |
|------|------|
| `evemarshal.py` | Reads EVE marshal streams, including packed DBRows. It is a port of `src/eve-common/marshal/EVEUnmarshal.cpp` and reads the string table from that source. Currency (`CY`) columns are scaled by 10000. |
| `serverdata.py` | Gives a static table as a fresh install has it: the `sql/base` dump plus the Up sections of the migrations, leaving out this generator's own files. It understands INSERT/REPLACE, simple UPDATE/DELETE, ALTER ADD/DROP/MODIFY, and the F16 `UPDATE ... JOIN invTypes`. Statements it can't follow are listed as unhandled. |
| `specs.py` | Pairs each client table with its server table or tables, and holds the column renames, keys and per-table rules. |
| `compare.py` | The comparison and the report. |
| `gen_client_sync.py` | Writes the migrations, one per group: inventory, dogma, industry, map, stations, characters, certificates, planets, resources. |

All of these are stdlib Python. The output is deterministic: change the generator rather than the SQL, run it again
and commit both. On a database that already has the previous version, migrate the sync files down, then up again.

## Rules

- **The client wins wherever it has a value and the server's differs.** Floats are compared at the client column's
  precision (single-precision columns as float32).
- **Where the client is empty, the server's value stays.** Empty means NULL, blank text, the literal `'None'`, or a
  column's placeholder (`'Unknown'` for w-space region and constellation names). The client leaves fields empty that
  it never reads: its text comes from localization, and system faction comes from the server. NULL against 0 or `''`
  counts as the same "nothing".
  - The exception is `invTypes.marketGroupID`, where the client's NULL means "not on the market". There the server's
    own "none" value is written.
- **NULLs are only written where safe.** A NULL is only written to a column that already holds NULLs.
  `DBResultRow::GetInt()` calls `strtol()` on the raw field, so a new NULL would crash code that doesn't check
  `IsNull()`. New rows get the column's default, or 0 / `''`.
- **Rows only the client has are inserted.** Rows only the server has are deleted, except in these cases:
  - `mapDenormalize`: the client's celestials leave out regions, constellations, systems, stations, outposts and
    w-space secondary suns.
  - `staStations` from 2209987: the NPC list; `mapbulk.db` has all 5185 stations.
  - `crpCorporation`: player corporations.
  - Attribute 0: the server's "null" placeholder.
  - Server-only expressions that a remaining effect still uses.
- **`crpCorporation` only takes the NPC corporations' identity from the client:** name, ticker, description, CEO,
  station, race and logo. `memberLimit`, `shares`, `taxRate` and the join rules are live game state. The client's
  `shares` and `memberLimit` (−1, 30 billion) don't fit the server's unsigned columns anyway.
- **Columns are widened or added where client values don't fit:**
  - `dgmAttributeTypes.attributeIdx` goes from TINYINT to SMALLINT. Attribute 1165 has index 1391, which the server
    stored as 127.
  - `crpCorporation.description` goes from VARCHAR(400) to VARCHAR(4000). Down keeps it wide, because player
    corporations may have used the room by then.
  - `crtRelationships.childTypeID` is added. `ObjCacheDB` already selected this column, so its certificate query
    failed before.

Each migration loads the changed rows into temporary tables and applies them with UPDATE/DELETE/INSERT ... JOIN:

- An update only writes the columns that changed in that row.
- The Down section writes the old values back the same way.
- The 342,167 moon `orbitIndex` values are the number at the end of the moon's name, so one UPDATE sets them. The 3
  moons with names instead of numbers are set individually. Down sets them NULL again; every moon's `orbitIndex` was
  NULL before.
- Strings with non-ASCII characters, or with `--` in them, go as hex literals. That avoids charset trouble, and
  sql-migrate cuts a line at `--`. Statements stay under 400 KB, with one row per line.

## Verification (MariaDB 10.11, strict mode)

1. `sql/base` was loaded and all 61 earlier migrations were run. Statements were split the way sql-migrate splits
   them.
2. Running the generator against that live database wrote byte-identical migrations, so the dump/migration model
   matches the database.
3. All nine Up sections ran cleanly. The generator then found nothing left to change against the migrated database.
4. All nine Down sections, run in reverse, gave tables byte-identical to the pre-sync snapshot (64 tables), and the
   schema changes were undone. Up ran cleanly again after Down.

## What changed (summary)

- **Inventory:**
  - 11,952 `invTypes` rows changed. The main differences:
    - `basePrice` in 10,361 rows: the old prices came from a different export.
    - `typeName` in 1,405 rows, e.g. Crucible's "Mjolnir Cruise Missile".
    - `mass` in 487 rows: stargates and similar were 1e35, the client has 1e11.
    - `published` in 337 rows and `marketGroupID` in 152.
  - 33 types added: the Gistum/Pithum invulnerability fields, the Quafe Iterons, and others.
  - 992 types removed: unpublished leftovers the Crucible client doesn't know, such as the old Capsule Home types and
    obsolete probes. Their blueprints (333), ship rows (60) and dogma rows go with them.
- **Dogma:** the server sends these tables to the client as its bulkdata, so they replace what the client shows.
  - `dgmTypeAttributes`: 4,915 values changed, 7,096 rows added, 9,112 removed. For example, frigate
    `warpSpeedMultiplier` goes from 6 to 2.
  - The removed rows are on types the client doesn't have, use attributes added after Crucible, or are rows the
    client lacks. Those include all-zero `isOnline` rows and turret attributes that had been added to some NPC types.
    An NPC left without a rate of fire simply doesn't fire turrets, because `Timer::Start(0)` disables the timer.
  - The client's AI attributes (`AI_ShouldUseTargetSwitching` and others), `disallowAssistance` and
    `entityOverviewShipGroupId` are added.
  - 164 post-Crucible attributes are removed: 1808–2003. 2,803 unused expressions are removed.
  - Effects 4737–4792, which had name `'0'` and expression 0 on the server, get their real names and expressions.
- **Industry:**
  - `damagePerJob` for invention data interfaces goes back to the client's 1.0. The F16 migration had set it to 0.
    "Interfaces aren't consumed" now lives in code instead: `FactoryDB::GetRAMRequirements` zeroes it for activity
    8 / group 716 when loading.
  - `ramCompletedStatuses` gets its text.
- **Map:**
  - Moon `orbitIndex` values are filled in, and 14,529 `itemNameID`s added.
  - `mapSolarSystems.factionID` and w-space names are unchanged, because the client leaves them empty.
- **Stations:** the four Polaris stations get their names, positions and owner.
- **Characters:**
  - 10,460 `chrNPCCharacters` rows changed. The server's corporations, careers and stations came from a much later
    export; it even had "Holy Empress Catiz".
  - 668 NPC characters added, 12 later-lore ones removed.
  - 905 agents change division or station. This refines the F12 division remap with the exact Crucible values.
  - Faction 500021 ('Unknown') and the four Polaris corporations are added.
  - Corporation 1000001 becomes Doomheim/666 again. The server code still uses it as the rogue-drone owner
    (`corpRogueDrones`), so rogue drones now show as Doomheim.
- **Certificates:** `crtRelationships.childTypeID` added and parent fixes; `certificateRelationShips` filled.
- **Planets:** schematic 137 and its two type rows added.
- **Resources:**
  - `graphics` is rebuilt to the client's list: 787 added, 618 removed, 281 paths and descriptions changed.
  - 351 icons and 23 sounds added.
  - The character-creation paperdoll tables are completed. `resPath` now uses the client's relative form.

The client bulkdata with no server table (Incarna, UI messages, nebulas) is listed in `specs.UNMAPPED`.
