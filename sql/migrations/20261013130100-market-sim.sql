-- the market simulation's state (doc/code_and_design_notes/planning/market-sim/PLAN.md 8).  money is in cents (BIGINT), quantities
--  in units.  the calibration tables (mktCal*) come with 20261013120000-market-calibration-schema.sql
--   mktSimState      name/value:  schema version, seed, sim clock, start day, money controller, paused, GM overrides
--   mktSimNode       the market nodes (stations the sim quotes at)
--   mktSimActor      market makers, producers, consumers, npc vendors:  wallet and reserved (escrow behind bids)
--   mktSimInventory  per actor, node and type:  free units and units behind asks
--   mktSimCell       per node and type:  fair value (log), anchor (log), demand EWMA, target stock, schedule
--   mktSimOrder      which mktOrders rows are the sim's (and whose, at which level)
--   mktSimJob        manufacturing jobs on real assembly lines (phase 5;  unused before)
--   mktSimShipment   goods on the way between nodes (phase 4;  unused before)
--   mktSimHistory    the sim's internal trades per region/type/day, added to the price history the client sees
--   mktSimIntake     what each player sold to the sim per day and type (the intake caps survive restarts)
--   mktSimAudit      the daily books:  faucets, sinks, player crossings, money, residuals, price index
-- +migrate Up
CREATE TABLE IF NOT EXISTS mktSimState (
  `name` VARCHAR(64) NOT NULL,
  `value` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimNode (
  `nodeID` SMALLINT UNSIGNED NOT NULL,
  `stationID` INT UNSIGNED NOT NULL,
  `solarSystemID` INT UNSIGNED NOT NULL,
  `regionID` INT UNSIGNED NOT NULL,
  `kind` TINYINT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`nodeID`),
  UNIQUE KEY `stationID` (`stationID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimActor (
  `actorID` INT UNSIGNED NOT NULL,
  `kind` TINYINT UNSIGNED NOT NULL,
  `sector` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `ownerCorpID` INT UNSIGNED NOT NULL,
  `regionID` INT UNSIGNED NOT NULL,
  `homeNodeID` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `wallet` BIGINT NOT NULL DEFAULT 0,
  `reserved` BIGINT NOT NULL DEFAULT 0,
  `capacity` DOUBLE NOT NULL DEFAULT 0,
  `riskTolerance` FLOAT NOT NULL DEFAULT 0,
  `params` TEXT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`actorID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimInventory (
  `actorID` INT UNSIGNED NOT NULL,
  `nodeID` SMALLINT UNSIGNED NOT NULL,
  `typeID` SMALLINT UNSIGNED NOT NULL,
  `qty` BIGINT NOT NULL DEFAULT 0,
  `reservedQty` BIGINT NOT NULL DEFAULT 0,
  `avgCost` BIGINT NOT NULL DEFAULT 0,
  PRIMARY KEY (`actorID`, `nodeID`, `typeID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimCell (
  `nodeID` SMALLINT UNSIGNED NOT NULL,
  `typeID` SMALLINT UNSIGNED NOT NULL,
  `lnV` DOUBLE NOT NULL,
  `lnAnchor` DOUBLE NOT NULL,
  `demandEwma` DOUBLE NOT NULL,
  `targetStock` DOUBLE NOT NULL,
  `lastClear` DOUBLE NOT NULL DEFAULT 0,
  `lastTime` BIGINT NOT NULL DEFAULT 0,
  `nextDue` BIGINT NOT NULL DEFAULT 0,
  `flags` INT UNSIGNED NOT NULL DEFAULT 0,
  `clampDays` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`nodeID`, `typeID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimOrder (
  `orderID` INT UNSIGNED NOT NULL,
  `actorID` INT UNSIGNED NOT NULL,
  `nodeID` SMALLINT UNSIGNED NOT NULL,
  `typeID` SMALLINT UNSIGNED NOT NULL,
  `bid` TINYINT UNSIGNED NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`orderID`),
  KEY `actorID` (`actorID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimJob (
  `jobID` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `actorID` INT UNSIGNED NOT NULL,
  `kind` TINYINT UNSIGNED NOT NULL,
  `assemblyLineID` INT UNSIGNED NOT NULL DEFAULT 0,
  `stationID` INT UNSIGNED NOT NULL,
  `outputTypeID` SMALLINT UNSIGNED NOT NULL,
  `runs` INT UNSIGNED NOT NULL,
  `qtyOut` BIGINT NOT NULL,
  `startTime` BIGINT NOT NULL,
  `endTime` BIGINT NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`jobID`),
  KEY `endTime` (`endTime`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimShipment (
  `shipmentID` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `actorID` INT UNSIGNED NOT NULL,
  `fromNode` SMALLINT UNSIGNED NOT NULL,
  `toNode` SMALLINT UNSIGNED NOT NULL,
  `typeID` SMALLINT UNSIGNED NOT NULL,
  `qty` BIGINT NOT NULL,
  `valueCents` BIGINT NOT NULL,
  `depart` BIGINT NOT NULL,
  `arrive` BIGINT NOT NULL,
  `lossRoll` DOUBLE NOT NULL DEFAULT 0,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`shipmentID`),
  KEY `arrive` (`arrive`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimHistory (
  `regionID` INT UNSIGNED NOT NULL,
  `typeID` SMALLINT UNSIGNED NOT NULL,
  `historyDate` BIGINT NOT NULL,
  `low` BIGINT NOT NULL,
  `high` BIGINT NOT NULL,
  `sumValue` BIGINT NOT NULL,
  `volume` BIGINT NOT NULL,
  `trades` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`regionID`, `typeID`, `historyDate`),
  KEY `historyDate` (`historyDate`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimIntake (
  `day` INT NOT NULL,
  `ownerID` INT UNSIGNED NOT NULL,
  `typeID` SMALLINT UNSIGNED NOT NULL,
  `qty` BIGINT NOT NULL DEFAULT 0,
  `isk` BIGINT NOT NULL DEFAULT 0,
  PRIMARY KEY (`day`, `ownerID`, `typeID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
CREATE TABLE IF NOT EXISTS mktSimAudit (
  `day` INT NOT NULL,
  `category` VARCHAR(64) NOT NULL,
  `amount` BIGINT NOT NULL,
  PRIMARY KEY (`day`, `category`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
-- the sim's orders go with it (nothing would back them)
DELETE o FROM mktOrders o JOIN mktSimOrder s ON s.orderID = o.orderID;
DROP TABLE IF EXISTS mktSimAudit;
DROP TABLE IF EXISTS mktSimIntake;
DROP TABLE IF EXISTS mktSimHistory;
DROP TABLE IF EXISTS mktSimShipment;
DROP TABLE IF EXISTS mktSimJob;
DROP TABLE IF EXISTS mktSimOrder;
DROP TABLE IF EXISTS mktSimCell;
DROP TABLE IF EXISTS mktSimInventory;
DROP TABLE IF EXISTS mktSimActor;
DROP TABLE IF EXISTS mktSimNode;
DROP TABLE IF EXISTS mktSimState;
