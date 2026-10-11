-- F18 starbases: deploy and usage permissions are 0-3 (not bits), tower flags readable as ints, tower fuel cycle,
--  reinforcement and fuel calendar state, moon process provision types and links, generated moon materials and
--  structure miniballs (collision shapes, empty until filled).
-- +migrate Up
ALTER TABLE posStructureData
  MODIFY canUse tinyint(2) NOT NULL DEFAULT 0,
  MODIFY canView tinyint(2) NOT NULL DEFAULT 0,
  MODIFY canTake tinyint(2) NOT NULL DEFAULT 0,
  ADD provisionTypeID int(10) NOT NULL DEFAULT 0,
  ADD active tinyint(1) NOT NULL DEFAULT 0;
ALTER TABLE posTowerData
  MODIFY anchor tinyint(2) NOT NULL DEFAULT 0,
  MODIFY unanchor tinyint(2) NOT NULL DEFAULT 0,
  MODIFY online tinyint(2) NOT NULL DEFAULT 0,
  MODIFY offline tinyint(2) NOT NULL DEFAULT 0,
  MODIFY corpWar tinyint(1) NOT NULL DEFAULT 0,
  MODIFY statusDrop tinyint(1) NOT NULL DEFAULT 0,
  MODIFY allyStandings tinyint(1) NOT NULL DEFAULT 0,
  MODIFY showInCalendar tinyint(1) NOT NULL DEFAULT 0,
  MODIFY sendFuelNotifications tinyint(1) NOT NULL DEFAULT 0,
  MODIFY allowCorp tinyint(1) NOT NULL DEFAULT 0,
  MODIFY allowAlliance tinyint(1) NOT NULL DEFAULT 0,
  MODIFY password varchar(50) NOT NULL DEFAULT '',
  ADD lastCycle bigint(20) NOT NULL DEFAULT 0,
  ADD reinforceUntil bigint(20) NOT NULL DEFAULT 0,
  ADD reinforceArmed tinyint(1) NOT NULL DEFAULT 1,
  ADD fuelEventID int(10) unsigned NOT NULL DEFAULT 0,
  ADD fuelExpiry bigint(20) NOT NULL DEFAULT 0,
  ADD lowFuelNotified bigint(20) NOT NULL DEFAULT 0;
UPDATE posTowerData SET password = '' WHERE password = '''''';   -- the old column default was the two characters ''
CREATE TABLE IF NOT EXISTS posProcessLinks (
  sourceID int(10) unsigned NOT NULL,
  typeID int(10) unsigned NOT NULL,
  destID int(10) unsigned NOT NULL,
  towerID int(10) unsigned NOT NULL,
  PRIMARY KEY (sourceID, typeID),
  KEY towerID (towerID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='POS moon process links (source structure -> destination structure, per material)';
CREATE TABLE IF NOT EXISTS moonResources (
  moonID int(10) unsigned NOT NULL,
  typeID int(10) unsigned NOT NULL,
  quantity int(10) unsigned NOT NULL DEFAULT 1,
  PRIMARY KEY (moonID, typeID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='moon materials';
CREATE TABLE IF NOT EXISTS moonResourcesGenerated (
  moonID int(10) unsigned NOT NULL,
  PRIMARY KEY (moonID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='moons whose materials were generated (an empty moon has no moonResources rows)';
CREATE TABLE IF NOT EXISTS invTypeMiniBalls (
  typeID int(10) unsigned NOT NULL,
  idx smallint(5) unsigned NOT NULL,
  x double NOT NULL,
  y double NOT NULL,
  z double NOT NULL,
  radius float NOT NULL,
  PRIMARY KEY (typeID, idx)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='collision miniballs of a type, offsets from the ball center';

-- +migrate Down
DROP TABLE IF EXISTS invTypeMiniBalls;
DROP TABLE IF EXISTS moonResourcesGenerated;
DROP TABLE IF EXISTS moonResources;
DROP TABLE IF EXISTS posProcessLinks;
ALTER TABLE posTowerData
  DROP lowFuelNotified,
  DROP fuelExpiry,
  DROP fuelEventID,
  DROP reinforceArmed,
  DROP reinforceUntil,
  DROP lastCycle;
ALTER TABLE posStructureData
  DROP active,
  DROP provisionTypeID;
