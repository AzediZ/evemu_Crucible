# Market simulation calibration data

These tools build the calibration tables of the simulated market economy
(`doc/code_and_design_notes/market-sim/PLAN.md`, sections 4.4–4.5): what every market type was worth, how much of it
traded, how noisy its price was, how trade spread over regions and weekdays, and which ships were lost with which
fits. The source is Crucible-era market history and killmails from EVE Ref; the output is a generated migration,
`sql/migrations/20261013120100-market-calibration.sql`, which fills the tables that
`sql/migrations/20261013120000-market-calibration-schema.sql` creates.

```
python -I utils/market-calibration/fetch.py --history <cache>/history --killmails <cache>/killmails
python -I utils/market-calibration/gen_market_calibration.py --history <cache>/history --killmails <cache>/killmails \
    --client "<EVE client folder>" [--summary report.txt] [--export-json snapshot.json] [--strict]
```

To rebuild from exactly the files of the committed run, fetch with the manifest:
`fetch.py --history ... --killmails ... --manifest utils/market-calibration/manifest.json`. It downloads only what
is missing and reports any file whose sha256 differs (EVE Ref warns that archives may be updated in place).

| File | Role |
|------|------|
| `fetch.py` | Downloads `market-history-YYYY-MM-DD.csv.bz2` for every day of the window and `killmails-YYYY-MM-DD.tar.bz2` for a sample of days (default 28 days, every 6th day, so each weekday is sampled four times) into a cache **outside the repo**. Checks that every file decompresses completely, keeps a `fetch-log.json` (URL, Last-Modified, ETag), never downloads a cached file again, sends a descriptive User-Agent and spaces requests. |
| `history.py` | Streams the history CSVs into per (region, type) daily series. |
| `killmails.py` | Streams the killmail archives into losses per (victim type, security band) and fit baskets. |
| `derive.py` | Static data, type classes, statistics, bill-of-materials costs, gap filling, regional/weekday shape, meta ratios. |
| `gen_market_calibration.py` | The CLI: writes the migration and `manifest.json`, prints the consistency report, optionally writes the harness snapshot (`--export-json`:  the types, bills of materials, refine outputs and `mktCal*` rows;  `buildTime`, the blueprints' production times (market sim phase 3);  `logistics`, the k-space jump graph with each system's security, the npc stations with a refinery, the security bands of each ore group's belts (`roidDistribution`), the hauling hulls' cargo holds and the regional node stations (market sim phase 4)). |
| `manifest.json` | Every source file of the committed run: URL, data date, Last-Modified, size, sha256, rows; the repo static files and the client bulkdata file used; the sha256 of the generated SQL. |

The harness snapshot (`--export-json`) carries, besides the types, bills, refine outputs and `mktCal*` rows,
`buildTime` (the blueprints' productionTime, phase 3) and `chains` (phase 6, `chains_json`): the invention, reaction
and planetary recipes, the decryptors, the bills of T2, T3 and components, the T2 blueprints, `moonMiningAmount` and
what T2/T3/components reprocess into, the same data `MarketSimDB::LoadChains` loads on the server. It goes into no
migration.

All of these are stdlib Python 3 and deterministic: two runs over the same inputs write byte-identical SQL, manifest
and snapshot. **Change the generator rather than the SQL, run it again and commit the SQL, the manifest and the
generator together.** On a database that already has the previous data, migrate `20261013120100` down and up again.

Static data comes from `utils/bulkdata/serverdata.load_synced()`: `sql/base` plus every migration, including the
client-sync migrations' temporary-table changes, i.e. what a fresh install has. With `--client` the client's own
`bulkdata/600004.cache2` (invTypes) decides which types exist and which are on the market (the client wins); the
report prints any difference with the synced server data (there is none at the time of writing: 18,710 types,
6,926 market types).

## Sources, attribution and licence

- **Market history:** `https://data.everef.net/market-history/<year>/market-history-<date>.csv.bz2`, every day
  2011-12-01 → 2012-05-21 (Crucible 1.0–1.6; Inferno shipped 2012-05-22), 173 files, about 114 MB. The files carry
  `Last-Modified: Fri, 09 Mar 2018`, matching CCP's own release of the history ("Market data published by CCP Quant",
  `data.everef.net/ccp/ccp_quant/EVEOnline_priceHistory_20031001_20180305.7z`), which is the fallback source if EVE
  Ref's files change.
- **Killmails:** `https://data.everef.net/killmails/<year>/killmails-<date>.tar.bz2`, ESI-format JSON (EVE Ref takes
  the killmail IDs from zKillboard and the killmails from ESI). 30 days of the window (the 28-day sample plus
  2012-02-15 and 2012-02-18 from the plan's analysis), 331,801 killmails. EVE Ref's `totals.json` shows 7,050–19,380
  killmails for every day of the window, so coverage is not thin.
- **Licence as checked on 2026-10-12:** the EVE Ref dataset documentation (`docs.everef.net/datasets/...`) states no
  data licence. `data.everef.net` says "EVE Ref is the property of Autonomous Logic. All rights reserved." together
  with CCP's notice ("EVE Online and the EVE logo are the registered trademarks of CCP hf. All rights are reserved
  worldwide. ... CCP hf. has granted permission to EVE Ref to use EVE Online and all associated logos and designs for
  promotional and information purposes on its website but does not endorse, and is not in any way affiliated with,
  EVE Ref."). EVE Ref's code (github.com/autonomouslogic/eve-ref) is MIT-0; that licence covers the code, not the
  data. The underlying market history and killmails are CCP's game data.
- **What is committed:** only derived per-type, per-region and per-hull aggregates (medians, shares, rates), never
  the raw CSVs or killmails. The generated SQL and the manifest carry the credit "Derived from EVE Ref
  (data.everef.net) market history and killmail archives; EVE Online data (c) CCP hf.". The plan's recommendation
  stands: ask the EVE Ref maintainer (Discord, linked from the docs) to confirm that redistributing derived aggregates
  is fine.

## Method

**Type universe.** Market types are `published = 1` with a `marketGroupID` (what `PlaceCharOrder` accepts):
6,926 types, one row each in `mktCalType`. History rows of types the client doesn't know (only 44992, the modern PLEX
split: 6,401 rows) and of known types that aren't market types (1,263 types, mostly faction, officer and deadspace
modules without a client `marketGroupID`: 35,920 rows) are dropped. `highest`/`lowest` are not read (equal to
`average` in most rows).

**Classes** (`simClass`, first match wins):
0 excluded: PLEX (29668, group Game Time) and Apparel (category 30);
12 NPC vendor: in `crpNPCCorporationTrades` and a skill, blueprint, reaction, commodity (trade goods), structure,
sovereignty structure or upgrade, container/outpost platform, command center, or shuttle;
13 not simulated: other skills and blueprints (T2 BPOs and a few skills no NPC sells; players only);
8 PI (categories 42/43); 1 mined (asteroids, harvestable gas clouds); 2 refined (minerals, ice products);
7 reaction/moon (moon, intermediate, composite, biochemical materials, hybrid polymers);
11 salvage/datacore/decryptor (incl. sleeper parts, relics, rogue drone components, metal scraps);
10 faction/LP (tags, insignias, overseer effects, charters, metaGroups 3–6, implants without a blueprint, special
hulls, and every **BPC-only** product: a T1 or component whose blueprint isn't on the market, or a T2 whose blueprint
isn't published, e.g. Utu, Freki, Revenant, capital named modules);
6 component (T2, capital, advanced capital and T3 components, R.A.M./R.Db); 3/4/5 manufactured T1/T2/T3;
9 loot/named (metaGroup 1 without a blueprint, and everything else).

**Prices** (`refPrice`, `priceSource`, `confidence`; the first applicable rule wins):
1. NPC vendor with a basePrice: `refPrice = basePrice` (source 7), as the Crucible NPC sell orders. The band is
   basePrice; volumes still come from history.
2. History with at least 5 Forge trading days: the median of the Forge daily `average` (source 1).
3. History with at least 5 rows in any regions: the volume-weighted median of `average / regionPriceRatio`.
4. Manufactured (T1/T2/T3/component with an obtainable blueprint): `BOM × costMarkup[group][tech]` (source 2).
5. Meta variant: `parent × ratio` (source 3). Named meta-1 items use a per-group fit of ln(price/parent) on the
   `metaLevel` attribute (633), clipped to [0.5, 1000]; otherwise `mktCalMeta` for (metaGroup, category), else
   (metaGroup, all).
6. Raw/reprocessable (mined, refined, salvage, loot without a blueprint, with `invTypeMaterials`): 0.95 × refine
   value at calibrated mineral prices (source 4).
7. Thin history (1–4 rows): the volume-weighted median, unless it is more than 1 dex from the group-median estimate.
8. Group (else category) median of price/basePrice × basePrice (source 5, confidence 0.2); else basePrice (6).
9. Last resorts: thin history at any distance, then refine value at any class (confidence 0.1).
10. Otherwise no price: `simClass` 13 (players only).

**BOM** (`buildCostRef`, and rule 4): Crucible manufacturing per run: the product's `invTypeMaterials` minus the
materials of any `ramTypeRequirements` row flagged recycle (`extra = 1`, the T1 item of a T2 build), with waste
`wasteFactor × (ME ≥ 0 ? 1/(1+ME) : 1−ME)` and per-material rounding; ME 0 for T1/T3 and −4 for invented T2 BPCs;
plus the activity-1 `ramTypeRequirements` (not skills) × `damagePerJob`; divided by `portionSize`. Inputs are priced
recursively at their calibrated `refPrice`. Perfect production skills (no skill waste).

**Cost markups** (`mktCalGroup.costMarkupT1/T2`): median price/BOM of Forge-history types whose inputs all have
history, per group (≥ 3 types), else category, else tech level, else 1.1 (T1) / 1.5 (T2). `costMarkupT2` holds the T3
markup for T3 groups (no group mixes T2 and T3).

**Statistics.**
- `priceP10/P90`: the 10th/90th percentile of the same daily series as `refPrice` (gap-filled types: the group's
  median band ratios).
- `volatility` (per √day, `derive.horizon_volatility`): 1.4826 × the MAD of the log return between traded days
  5 calendar days apart, divided by √5. It uses Forge, else the region with the most days, else the group median,
  and is clipped to [0.005, 0.6].
  - **Why not the day-to-day MAD:** liquid hub types trade on a sticky ask, so their daily average doesn't change on
    most days. Their 1-day MAD is about 0 however far the price moves in a week: it put 31 % of modules, 31 % of
    commodities and 36 % of drones on the 0.005 floor. Thin types show the opposite: bid/ask bounce between single
    prints inflates the 1-day figure.
  - **What the 5-day horizon measures:** the movement of the price level, which is what the sim's
    `MarketNoise × volatility` drives.
  - **Consistency:** the harness's acceptance test (d) (`src/eve-market-sim/Harness.cpp`, `HorizonVolatility`)
    measures the simulated hub history with the same estimator.
  - **Report:** each run prints the 5-day and 1-day figures per category.
- `unitsPerDay`: all regions, the mean over every window day (no trade counts 0), each day capped at 5× the type's
  median trading day. Types with fewer than 5 trading days are capped at, and types that never traded get, the
  group's median units scaled by `(price/groupMedianPrice)^volB` (an upper bound: they didn't trade in 173 days).
- `activeShare`: share of window days with a Forge trade. `lotSize`: the median `volume/order_count` (Forge),
  rounded to `portionSize`.
- `volA/volB/volResid`: least squares of log10 units/day on log10 price over the group's history types (≥ 8), else
  the category, else all.
- Weekday multipliers `wd0..wd6` (Mon..Sun, mean 1): the median of a day's ISK traded over its centred 7-day mean;
  per group when the group trades on ≥ 90 % of days, else per category, else all types.

**Regions.** `mktCalRegion` has a row per (region, simulated type) with `activeShare ≥ 0.2`, and every traded cell of
the five hub regions (The Forge, Domain, Sinq Laison, Heimatar, Metropolis). `priceRatio` is the median same-day
avg(region)/avg(Forge) (≥ 3 common days, else median avg / refPrice), clipped to [0.1, 10]. `mktCalRegionDefault`:
the region's median same-day ratio over all types, and its share of all units and ISK.

**Killmails.** Security bands follow the client's displayed security (`CrimeWatch::SecurityLevel`): 0 high
(≥ 0.5 shown), 1 low, 2 null (0.0 and below), 3 w-space (region ≥ 11000000). `mktCalLoss.lossesPerDay` is public
killmails per day per (victim type, band), not divided by any coverage. Fits: the Crucible killmails came from the
old killmail API, which didn't record slots, so fitted modules, rigs, subsystems and loaded charges all have flag 0
(drones 87, implants 89, cargo 5); flags 0, 11–34, 87, 89, 92–99, 125–132 count as fitted and cargo doesn't.
`mktCalFit` keeps, for hulls with at least 5 losses, the top 25 items by ISK per loss with `qtyPerLoss ≥ 0.02`;
`destroyedShare` is destroyed / (destroyed + dropped).

**consumeShare** (final consumption / traded units): ships, modules, drones, subsystems, implants, structures,
deployables: (losses, or destroyed + dropped fitted units) per day / `--killmail-coverage` (0.5) / `unitsPerDay`,
clipped to [0.01, 1], when at least 3 units were seen; else the group median, else a category default (0.3 hulls and
structures, 0.1 modules/drones/subsystems/implants). Charges 0.8 (fired; killmails only show what was loaded).
Skills, blueprints, reactions, boosters, charters, fuel blocks 1.0. NPC trade goods 0.5. Raw, intermediate,
component, PI, moon and salvage goods 0 (derived demand only, PLAN.md 6.3).

**Group shape defaults** (`mktCalGroup`, by the group's most common class; PLAN.md 6.2): `stockElasticity` 0.25
commodities, 0.35 T1, 0.5 T2/T3, 0.7 loot/faction, 0 vendor; `halfSpread` 1.5 % minerals/ore/moon, 3 % PI, 4 % ships
and components/salvage, 6 % modules and charges, 12 % implants, loot and faction; `coverDays` 3 raw, 4 PI/moon,
5 T1/components, 7 T2/T3, 10 loot/faction/salvage, 30 vendor. `elasticity`: ships 0.8, modules/drones/subsystems 1.0,
charges 1.2, implants/boosters/faction 1.5, skills/blueprints 0.5, raw and intermediate 0.5. These defaults are not
calibrated from data.

## Results (committed run)

| Check | Result |
|---|---|
| Deterministic | two runs byte-identical (SQL, manifest, snapshot) |
| Coverage | 6,661 of 6,716 non-excluded market types have `refPrice > 0` (99.18 %); the 55 without are T2 BPOs, medals, DNA samples and similar collectibles that never traded |
| Tritanium | 4.55 (in [3.5, 5.0]) |
| Raven | 107.0 M vs 104.9 M of minerals (1.02); `buildCostRef` at ME 0 waste 115.5 M |
| Hub `priceRatio` medians | Domain 1.000, Sinq Laison 1.000, Heimatar 1.003, Metropolis 1.012 |
| T1 price/BOM (ME 0) | modules 1.07, charges 1.10, ships 1.04, drones 1.06 |
| Ship losses / traded | 0.18 (public killmails); capsules 2,302/day |
| Rows | mktCalType 6,926, mktCalRegion 86,932, mktCalRegionDefault 65, mktCalGroup 474, mktCalMeta 14, mktCalLoss 1,278, mktCalFit 5,599 |

The full consistency report (inputs, coverage per class, spot checks, implied vs observed mineral volume, T1
price/BOM distribution, markups, meta ratios, consumeShare per category, sample fits, weekday shape, top movers
between the window halves) is printed by every run.

## Verification (MariaDB 10.11, strict mode)

`sql/base`, all migrations up, then `20261013120100` and `20261013120000` down, and both up again, with statements
split as sql-migrate splits them: no errors, and the row counts match the generator's.
