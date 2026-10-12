-- F24 saved fittings: one header row per fitting (character or corporation owner) and its modules/drones
--  (chrShipFittings is obsolete and unused)
-- +migrate Up
CREATE TABLE IF NOT EXISTS chrFittings (
  fittingID INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  ownerID INT(10) UNSIGNED NOT NULL,
  name VARCHAR(100) NOT NULL,
  description VARCHAR(1000) NOT NULL DEFAULT '',
  shipTypeID INT(10) UNSIGNED NOT NULL,
  savedDate BIGINT(20) NOT NULL DEFAULT 0,
  PRIMARY KEY (fittingID),
  KEY ownerID (ownerID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS chrFittingItems (
  fittingID INT(10) UNSIGNED NOT NULL,
  typeID INT(10) UNSIGNED NOT NULL,
  flagID SMALLINT(5) UNSIGNED NOT NULL,
  quantity INT(10) UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (fittingID, flagID, typeID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS chrFittingItems;
DROP TABLE IF EXISTS chrFittings;
