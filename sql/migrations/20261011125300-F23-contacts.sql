-- contacts: separate blocked list, one row per (owner, contact), contact labels keyed by bit value per owner
-- +migrate Up
CREATE TABLE IF NOT EXISTS `chrBlocked` (
  `ownerID` int(10) unsigned NOT NULL,
  `blockedID` int(10) unsigned NOT NULL,
  PRIMARY KEY (`ownerID`, `blockedID`),
  KEY `blockedID` (`blockedID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
INSERT IGNORE INTO `chrBlocked` (`ownerID`, `blockedID`) SELECT `ownerID`, `contactID` FROM `chrContacts` WHERE `blocked` = 1;
-- blocked-only rows were never real contacts
DELETE FROM `chrContacts` WHERE `blocked` = 1;

DELETE c1 FROM `chrContacts` c1 JOIN `chrContacts` c2 ON c1.`ownerID` = c2.`ownerID` AND c1.`contactID` = c2.`contactID` AND c1.`id` > c2.`id`;
ALTER TABLE `chrContacts`
  ADD UNIQUE KEY `ownerContact` (`ownerID`, `contactID`),
  ADD KEY `contactID` (`contactID`);

-- CreateLabel never worked, so there are no real labels to keep
DELETE FROM `chrLabels`;
UPDATE `chrContacts` SET `labelMask` = 0;
ALTER TABLE `chrLabels`
  MODIFY `labelID` bigint(20) NOT NULL,
  DROP PRIMARY KEY,
  MODIFY `color` int(10) NULL DEFAULT NULL,
  ADD PRIMARY KEY (`ownerID`, `labelID`);

-- +migrate Down
DELETE FROM `chrLabels`;
UPDATE `chrContacts` SET `labelMask` = 0;
ALTER TABLE `chrLabels`
  DROP PRIMARY KEY,
  MODIFY `color` int(10) NOT NULL DEFAULT 0,
  MODIFY `labelID` int(10) NOT NULL AUTO_INCREMENT PRIMARY KEY;
ALTER TABLE `chrContacts`
  DROP KEY `ownerContact`,
  DROP KEY `contactID`;
INSERT INTO `chrContacts` (`ownerID`, `contactID`, `inWatchlist`, `relationshipID`, `labelMask`, `blocked`)
  SELECT `ownerID`, `blockedID`, 0, 0, 0, 1 FROM `chrBlocked`;
DROP TABLE IF EXISTS `chrBlocked`;
