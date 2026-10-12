-- F22 wars and alliances:  the wars between player corporations and alliances (warRegistry), the legacy alliance
--  relationships, alliance contact labels keyed by bit value per owner (as F23/F21 did for chrLabels/crpLabels; the
--  alliance label calls never worked, so there are no real labels to keep), one alliance contact row per (owner, contact),
--  and the time an accepted alliance application becomes effective (24 hours after acceptance in Crucible)
-- +migrate Up
CREATE TABLE IF NOT EXISTS `crpWars` (
  `warID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `declaredByID` int(10) unsigned NOT NULL,
  `againstID` int(10) unsigned NOT NULL,
  `timeDeclared` bigint(20) NOT NULL,
  `timeFinished` bigint(20) NULL DEFAULT NULL,
  `retracted` bigint(20) NULL DEFAULT NULL,
  `retractedBy` int(10) unsigned NULL DEFAULT NULL,
  `billID` int(10) unsigned NULL DEFAULT NULL,
  `mutual` tinyint(1) NOT NULL DEFAULT 0,
  `endReason` tinyint(3) unsigned NOT NULL DEFAULT 0,
  `nextBillTime` bigint(20) NOT NULL DEFAULT 0,
  `weeklyCost` double NOT NULL DEFAULT 0,
  `legalNotified` tinyint(1) NOT NULL DEFAULT 0,
  PRIMARY KEY (`warID`),
  KEY `declaredByID` (`declaredByID`),
  KEY `againstID` (`againstID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `alnRelationships` (
  `allianceID` int(10) unsigned NOT NULL,
  `toID` int(10) unsigned NOT NULL,
  `relationship` tinyint(3) unsigned NOT NULL,
  PRIMARY KEY (`allianceID`, `toID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

DELETE FROM `alnLabels`;
ALTER TABLE `alnLabels`
  MODIFY `labelID` bigint(20) NOT NULL,
  DROP PRIMARY KEY,
  MODIFY `color` int(10) NULL DEFAULT NULL,
  ADD PRIMARY KEY (`ownerID`, `labelID`);

DELETE c1 FROM `alnContacts` c1 JOIN `alnContacts` c2 ON c1.`ownerID` = c2.`ownerID` AND c1.`contactID` = c2.`contactID` AND c1.`id` > c2.`id`;
UPDATE `alnContacts` SET `labelMask` = 0;
ALTER TABLE `alnContacts`
  MODIFY `labelMask` bigint(20) NOT NULL DEFAULT 0,
  ADD UNIQUE KEY `ownerContact` (`ownerID`, `contactID`);

ALTER TABLE `alnApplications` ADD COLUMN `effectiveAt` bigint(20) NOT NULL DEFAULT 0;

-- +migrate Down
ALTER TABLE `alnApplications` DROP COLUMN `effectiveAt`;
ALTER TABLE `alnContacts` DROP KEY `ownerContact`;
DELETE FROM `alnLabels`;
ALTER TABLE `alnLabels`
  DROP PRIMARY KEY,
  MODIFY `color` int(10) NOT NULL DEFAULT 0,
  MODIFY `labelID` int(10) NOT NULL AUTO_INCREMENT PRIMARY KEY;
DROP TABLE IF EXISTS `alnRelationships`;
DROP TABLE IF EXISTS `crpWars`;
