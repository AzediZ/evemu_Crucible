-- F20 inventory operations: trashed items kept for GM restore (junkyard log), secure container passwords and audit log
--  container settings, the audit log container action log, and an owner/location index for corp item lockdown.
-- +migrate Up
CREATE TABLE IF NOT EXISTS invTrashLog (
  itemID INT(10) UNSIGNED NOT NULL,
  ownerID INT(10) UNSIGNED NOT NULL,
  locationID INT(10) UNSIGNED NOT NULL,               -- where it was trashed from (station, or office)
  flag SMALLINT(5) UNSIGNED NOT NULL,
  trashedBy INT(10) UNSIGNED NOT NULL,
  trashTime BIGINT(20) NOT NULL,
  PRIMARY KEY (itemID),
  KEY trashTime (trashTime),
  KEY ownerID (ownerID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS invContainerSecurity (
  itemID INT(10) UNSIGNED NOT NULL,
  generalPassword VARCHAR(32) NULL DEFAULT NULL,
  configPassword VARCHAR(32) NULL DEFAULT NULL,
  alscConfig TINYINT(3) UNSIGNED NOT NULL DEFAULT 15,  -- audit log container: which actions need a password
  PRIMARY KEY (itemID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS invAuditLog (
  logID INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  containerID INT(10) UNSIGNED NOT NULL,
  logDate BIGINT(20) NOT NULL,
  actorID INT(10) UNSIGNED NOT NULL,
  actionID TINYINT(3) UNSIGNED NOT NULL,
  status TINYINT(3) UNSIGNED NOT NULL DEFAULT 1,
  inventoryMgrLocationID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  locationID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  flagID SMALLINT(5) UNSIGNED NOT NULL DEFAULT 0,
  arg1 INT(10) NULL DEFAULT NULL,
  arg2 INT(10) NULL DEFAULT NULL,
  PRIMARY KEY (logID),
  KEY containerLog (containerID, logID),
  KEY containerDate (containerID, logDate)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

ALTER TABLE crpLockedItems ADD KEY ownerLocation (ownerID, locationID);

-- +migrate Down
ALTER TABLE crpLockedItems DROP KEY ownerLocation;
DROP TABLE IF EXISTS invAuditLog;
DROP TABLE IF EXISTS invContainerSecurity;
DROP TABLE IF EXISTS invTrashLog;
