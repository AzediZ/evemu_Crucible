-- F15 clones, implants and boosters: jump clones (station and ship) with their implants as typeIDs, active boosters
--  with their expiry and fired side effects, the clone jump timer, and the Crucible clone grade prices (client bulkdata
--  2012-03; the server's values were later ones and Phi..Omega were free).
-- +migrate Up
CREATE TABLE IF NOT EXISTS chrJumpClones (
  jumpCloneID INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  characterID INT(10) UNSIGNED NOT NULL,              -- the clone's owner
  locationID INT(10) UNSIGNED NOT NULL,               -- stationID, or shipID for ship clones
  locationOwnerID INT(10) UNSIGNED NOT NULL DEFAULT 0, -- ship clones: the pilot that installed it.  0 for stations
  installDateTime BIGINT(20) NOT NULL DEFAULT 0,
  PRIMARY KEY (jumpCloneID),
  KEY characterID (characterID),
  KEY locationID (locationID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS chrJumpCloneImplants (
  jumpCloneID INT(10) UNSIGNED NOT NULL,
  typeID INT(10) UNSIGNED NOT NULL,
  PRIMARY KEY (jumpCloneID, typeID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS chrBoosters (
  itemID INT(10) UNSIGNED NOT NULL,
  characterID INT(10) UNSIGNED NOT NULL,
  expiryTime BIGINT(20) NOT NULL,                     -- filetime
  sideEffectMask TINYINT(3) UNSIGNED NOT NULL DEFAULT 0, -- bit n set = the side effect using boosterEffectChance(n+1) fired
  PRIMARY KEY (itemID),
  KEY characterID (characterID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

ALTER TABLE chrCharacters ADD COLUMN lastCloneJumpTime BIGINT(20) NOT NULL DEFAULT 0;

UPDATE invTypes SET basePrice = 40000 WHERE typeID = 165;
UPDATE invTypes SET basePrice = 65000 WHERE typeID = 166;
UPDATE invTypes SET basePrice = 95000 WHERE typeID = 9917;
UPDATE invTypes SET basePrice = 130000 WHERE typeID = 9919;
UPDATE invTypes SET basePrice = 180000 WHERE typeID = 9921;
UPDATE invTypes SET basePrice = 250000 WHERE typeID = 9923;
UPDATE invTypes SET basePrice = 335000 WHERE typeID = 9925;
UPDATE invTypes SET basePrice = 470000 WHERE typeID = 9927;
UPDATE invTypes SET basePrice = 650000 WHERE typeID = 9929;
UPDATE invTypes SET basePrice = 930000 WHERE typeID = 9931;
UPDATE invTypes SET basePrice = 1340000 WHERE typeID = 9933;
UPDATE invTypes SET basePrice = 1980000 WHERE typeID = 9935;
UPDATE invTypes SET basePrice = 2990000 WHERE typeID = 9937;
UPDATE invTypes SET basePrice = 4700000 WHERE typeID = 9939;
UPDATE invTypes SET basePrice = 7800000 WHERE typeID = 16715;
UPDATE invTypes SET basePrice = 13000000 WHERE typeID = 16718;
UPDATE invTypes SET basePrice = 20000000 WHERE typeID = 23734;
UPDATE invTypes SET basePrice = 30000000 WHERE typeID = 29138;
UPDATE invTypes SET basePrice = 45000000 WHERE typeID = 29139;
UPDATE invTypes SET basePrice = 65000000 WHERE typeID = 29140;
UPDATE invTypes SET basePrice = 90000000 WHERE typeID = 29141;
UPDATE invTypes SET basePrice = 120000000 WHERE typeID = 29142;
UPDATE invTypes SET basePrice = 150000000 WHERE typeID = 29143;

-- +migrate Down
DROP TABLE IF EXISTS chrBoosters;
DROP TABLE IF EXISTS chrJumpCloneImplants;
DROP TABLE IF EXISTS chrJumpClones;
ALTER TABLE chrCharacters DROP COLUMN lastCloneJumpTime;

UPDATE invTypes SET basePrice = 42992.8 WHERE typeID = 165;
UPDATE invTypes SET basePrice = 66443.3 WHERE typeID = 166;
UPDATE invTypes SET basePrice = 95983.5 WHERE typeID = 9917;
UPDATE invTypes SET basePrice = 135250 WHERE typeID = 9919;
UPDATE invTypes SET basePrice = 187604 WHERE typeID = 9921;
UPDATE invTypes SET basePrice = 258501 WHERE typeID = 9923;
UPDATE invTypes SET basePrice = 356666 WHERE typeID = 9925;
UPDATE invTypes SET basePrice = 494097 WHERE typeID = 9927;
UPDATE invTypes SET basePrice = 690972 WHERE typeID = 9929;
UPDATE invTypes SET basePrice = 980559 WHERE typeID = 9931;
UPDATE invTypes SET basePrice = 1414670 WHERE typeID = 9933;
UPDATE invTypes SET basePrice = 2087100 WHERE typeID = 9935;
UPDATE invTypes SET basePrice = 3165820 WHERE typeID = 9937;
UPDATE invTypes SET basePrice = 4987330 WHERE typeID = 9939;
UPDATE invTypes SET basePrice = 8296030 WHERE typeID = 16715;
UPDATE invTypes SET basePrice = 15077600 WHERE typeID = 16718;
UPDATE invTypes SET basePrice = 32721700 WHERE typeID = 23734;
UPDATE invTypes SET basePrice = 49082500 WHERE typeID = 29138;
UPDATE invTypes SET basePrice = 73623700 WHERE typeID = 29139;
UPDATE invTypes SET basePrice = 0 WHERE typeID = 29140;
UPDATE invTypes SET basePrice = 0 WHERE typeID = 29141;
UPDATE invTypes SET basePrice = 0 WHERE typeID = 29142;
UPDATE invTypes SET basePrice = 0 WHERE typeID = 29143;
