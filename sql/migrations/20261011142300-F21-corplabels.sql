-- F21 corp contacts and outposts:  corp contact labels keyed by bit value per owner (as F23 did for chrLabels; CreateLabel never
--  worked, so there are no real labels to keep), one row per (owner, contact) with 64-bit label masks, and the outpost exit time
--  and standing owner set in station management
-- +migrate Up
DELETE FROM `crpLabels`;
ALTER TABLE `crpLabels`
  MODIFY `labelID` bigint(20) NOT NULL,
  DROP PRIMARY KEY,
  MODIFY `color` int(10) NULL DEFAULT NULL,
  ADD PRIMARY KEY (`ownerID`, `labelID`);

DELETE c1 FROM `crpContacts` c1 JOIN `crpContacts` c2 ON c1.`ownerID` = c2.`ownerID` AND c1.`contactID` = c2.`contactID` AND c1.`id` > c2.`id`;
UPDATE `crpContacts` SET `labelMask` = 0;
ALTER TABLE `crpContacts`
  MODIFY `labelMask` bigint(20) NOT NULL DEFAULT 0,
  ADD UNIQUE KEY `ownerContact` (`ownerID`, `contactID`);

ALTER TABLE `staStations`
  ADD COLUMN `exitTime` int(10) NULL DEFAULT NULL,
  ADD COLUMN `standingOwnerID` int(10) NULL DEFAULT NULL;

-- +migrate Down
ALTER TABLE `staStations` DROP COLUMN `standingOwnerID`, DROP COLUMN `exitTime`;
ALTER TABLE `crpContacts` DROP KEY `ownerContact`, MODIFY `labelMask` int(20) NOT NULL;
DELETE FROM `crpLabels`;
ALTER TABLE `crpLabels`
  DROP PRIMARY KEY,
  MODIFY `color` int(10) NOT NULL DEFAULT 0,
  MODIFY `labelID` int(10) NOT NULL AUTO_INCREMENT PRIMARY KEY;
