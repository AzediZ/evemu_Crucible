-- Market simulation calibration tables (doc/code_and_design_notes/market-sim/PLAN.md section 4.4).  Filled by the
--  generated 20261013120100-market-calibration.sql (utils/market-calibration/gen_market_calibration.py) from EVE Ref
--  (data.everef.net) market history and killmail archives;  EVE Online data (c) CCP hf.
--  Prices are ISK as DOUBLE (the sim's runtime ledger uses int64 cents).  Every column is NOT NULL, so readers never
--  need IsNull().  Security bands (mktCalLoss.secBand):  0 high-sec, 1 low-sec, 2 null-sec, 3 w-space, with the
--  client's displayed security (CrimeWatch::SecurityLevel).  mktCalMeta.categoryID 0 is the all-category fallback.
-- +migrate Up
CREATE TABLE mktCalType (
  typeID         SMALLINT UNSIGNED NOT NULL,  -- invTypes.typeID (market types:  published, marketGroupID > 0)
  simClass       TINYINT UNSIGNED NOT NULL,   -- 0 excluded, 1 mined, 2 refined, 3 mfgT1, 4 mfgT2, 5 mfgT3, 6 component,
                                              -- 7 reaction/moon, 8 PI, 9 loot/named, 10 faction/LP,
                                              -- 11 salvage/datacore/decryptor, 12 npcVendor, 13 notSimulated
  priceSource    TINYINT UNSIGNED NOT NULL,   -- 0 none, 1 history, 2 BOM, 3 meta-parent, 4 refine value, 5 group median,
                                              -- 6 basePrice, 7 vendor
  confidence     FLOAT NOT NULL,              -- 0..1 (trading days, method)
  refPrice       DOUBLE NOT NULL,             -- robust median of daily avg (Forge, else volume-weighted all-region)
  priceP10       DOUBLE NOT NULL,             -- band of daily avg in the window
  priceP90       DOUBLE NOT NULL,
  volatility     FLOAT NOT NULL,              -- daily log-return sd, 1.4826*MAD, from consecutive-day pairs
  unitsPerDay    DOUBLE NOT NULL,             -- TQ, all regions, mean units per window day (0 when never traded)
  activeShare    FLOAT NOT NULL,              -- share of window days with a trade (Forge)
  lotSize        INT UNSIGNED NOT NULL,       -- median volume/order_count, rounded to portionSize
  buildCostRef   DOUBLE NOT NULL DEFAULT 0,   -- BOM cost at calibrated input prices (0 = not built)
  consumeShare   FLOAT NOT NULL,              -- final-consumption share of traded units (killmails / group defaults)
  elasticity     FLOAT NOT NULL,              -- demand price elasticity (category default)
  PRIMARY KEY (typeID),
  KEY simClass (simClass)
) ENGINE=InnoDB;
CREATE TABLE mktCalRegion (
  regionID       INT UNSIGNED NOT NULL,
  typeID         SMALLINT UNSIGNED NOT NULL,
  priceRatio     FLOAT NOT NULL,              -- median same-day avg(region) / avg(Forge)
  unitsShare     FLOAT NOT NULL,              -- share of the type's all-region units
  activeShare    FLOAT NOT NULL,              -- share of window days with a trade in the region
  PRIMARY KEY (regionID, typeID)
) ENGINE=InnoDB;
CREATE TABLE mktCalRegionDefault (
  regionID       INT UNSIGNED NOT NULL,
  priceRatio     FLOAT NOT NULL,              -- median same-day avg(region) / avg(Forge) over all types
  unitsShare     FLOAT NOT NULL,              -- the region's share of all units traded
  iskShare       FLOAT NOT NULL,              -- the region's share of all ISK traded
  PRIMARY KEY (regionID)
) ENGINE=InnoDB;
CREATE TABLE mktCalGroup (
  groupID         SMALLINT UNSIGNED NOT NULL,
  categoryID      TINYINT UNSIGNED NOT NULL,
  elasticity      FLOAT NOT NULL,             -- demand price elasticity
  stockElasticity FLOAT NOT NULL,             -- theta in V* = Vanchor (S/S*)^-theta
  halfSpread      FLOAT NOT NULL,             -- quote half-spread h
  coverDays       FLOAT NOT NULL,             -- target stock in days of demand
  consumeShare    FLOAT NOT NULL,             -- median of the group's types
  volA            FLOAT NOT NULL,             -- log10 units/day = volA + volB log10 price (gap fill)
  volB            FLOAT NOT NULL,
  volResid        FLOAT NOT NULL,             -- residual sd of that fit, dex
  costMarkupT1    FLOAT NOT NULL,             -- median price / BOM cost, tech 1
  costMarkupT2    FLOAT NOT NULL,             -- median price / BOM cost, tech 2 (and tech 3)
  wd0 FLOAT NOT NULL, wd1 FLOAT NOT NULL, wd2 FLOAT NOT NULL, wd3 FLOAT NOT NULL,
  wd4 FLOAT NOT NULL, wd5 FLOAT NOT NULL, wd6 FLOAT NOT NULL,  -- weekday multipliers (Mon..Sun, mean 1)
  PRIMARY KEY (groupID)
) ENGINE=InnoDB;
CREATE TABLE mktCalMeta (
  metaGroupID    TINYINT UNSIGNED NOT NULL,
  categoryID     TINYINT UNSIGNED NOT NULL,   -- 0 = all categories
  priceRatio     FLOAT NOT NULL,              -- median price(variant) / price(parent)
  unitsRatio     FLOAT NOT NULL,              -- median units(variant) / units(parent)
  PRIMARY KEY (metaGroupID, categoryID)
) ENGINE=InnoDB;
CREATE TABLE mktCalLoss (
  hullTypeID     SMALLINT UNSIGNED NOT NULL,  -- victim type (ships, capsules, structures, deployables)
  secBand        TINYINT UNSIGNED NOT NULL,   -- 0 high, 1 low, 2 null, 3 w-space
  lossesPerDay   FLOAT NOT NULL,              -- public killmails per day (not divided by KillmailCoverage)
  PRIMARY KEY (hullTypeID, secBand)
) ENGINE=InnoDB;
CREATE TABLE mktCalFit (
  hullTypeID     SMALLINT UNSIGNED NOT NULL,
  typeID         SMALLINT UNSIGNED NOT NULL,  -- fitted module, rig, subsystem, loaded charge, drone, or implant
  qtyPerLoss     FLOAT NOT NULL,              -- units per loss of the hull (destroyed + dropped)
  destroyedShare FLOAT NOT NULL,              -- destroyed / (destroyed + dropped)
  PRIMARY KEY (hullTypeID, typeID)
) ENGINE=InnoDB;

-- +migrate Down
DROP TABLE IF EXISTS mktCalFit;
DROP TABLE IF EXISTS mktCalLoss;
DROP TABLE IF EXISTS mktCalMeta;
DROP TABLE IF EXISTS mktCalGroup;
DROP TABLE IF EXISTS mktCalRegionDefault;
DROP TABLE IF EXISTS mktCalRegion;
DROP TABLE IF EXISTS mktCalType;
