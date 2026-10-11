-- eve mail: one status row per (message, character), NULL list/corp ids, unique label bits per owner
-- +migrate Up
CREATE TEMPORARY TABLE `tmpMailStatus` AS
  SELECT `messageID`, `characterID`, COALESCE(MAX(`statusMask`), 0) AS `statusMask`, COALESCE(MAX(`labelMask`), 0) AS `labelMask`
  FROM `mailStatus` GROUP BY `messageID`, `characterID`;
DELETE FROM `mailStatus`;
INSERT INTO `mailStatus` (`messageID`, `characterID`, `statusMask`, `labelMask`)
  SELECT `messageID`, `characterID`, `statusMask`, `labelMask` FROM `tmpMailStatus`;
DROP TEMPORARY TABLE `tmpMailStatus`;
ALTER TABLE `mailStatus`
  MODIFY `statusMask` bigint(20) unsigned NOT NULL DEFAULT 0,
  MODIFY `labelMask` bigint(20) unsigned NOT NULL DEFAULT 0,
  ADD PRIMARY KEY (`messageID`, `characterID`),
  ADD KEY `characterID` (`characterID`);

UPDATE `mailMessage` SET `toListID` = NULL WHERE `toListID` = 0;
UPDATE `mailMessage` SET `toCorpOrAllianceID` = NULL WHERE `toCorpOrAllianceID` = 0;

DELETE l1 FROM `mailLabel` l1 JOIN `mailLabel` l2 ON l1.`ownerID` = l2.`ownerID` AND l1.`bit` = l2.`bit` AND l1.`id` > l2.`id`;
ALTER TABLE `mailLabel`
  MODIFY `color` int(11) NULL DEFAULT NULL,
  ADD UNIQUE KEY `ownerBit` (`ownerID`, `bit`);

-- +migrate Down
ALTER TABLE `mailLabel`
  DROP KEY `ownerBit`,
  MODIFY `color` int(11) DEFAULT 0;
UPDATE `mailMessage` SET `toListID` = 0 WHERE `toListID` IS NULL;
UPDATE `mailMessage` SET `toCorpOrAllianceID` = 0 WHERE `toCorpOrAllianceID` IS NULL;
ALTER TABLE `mailStatus`
  DROP PRIMARY KEY,
  DROP KEY `characterID`,
  MODIFY `statusMask` bigint(20) unsigned DEFAULT NULL,
  MODIFY `labelMask` bigint(20) unsigned DEFAULT NULL;
