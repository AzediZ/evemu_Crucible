-- chat channels: language restriction, longer comparison keys, one subscription row per (channel, char),
-- one access entry per (channel, accessor) with signed modes (DISALLOWED = -2, NOTSPECIFIED = -1).  accessor 0 = default
-- +migrate Up
ALTER TABLE `channels`
  ADD COLUMN `languageRestriction` tinyint(4) NOT NULL DEFAULT 0,
  MODIFY `comparisonKey` varchar(85) DEFAULT NULL;

CREATE TEMPORARY TABLE `tmpChannelChars` AS
  SELECT `channelID`, MAX(`corpID`) AS `corpID`, `charID`, MAX(`allianceID`) AS `allianceID`, MAX(`role`) AS `role`, MAX(`extra`) AS `extra`
  FROM `channelChars` GROUP BY `channelID`, `charID`;
DELETE FROM `channelChars`;
INSERT INTO `channelChars` (`channelID`, `corpID`, `charID`, `allianceID`, `role`, `extra`)
  SELECT `channelID`, `corpID`, `charID`, `allianceID`, `role`, `extra` FROM `tmpChannelChars`;
DROP TEMPORARY TABLE `tmpChannelChars`;
ALTER TABLE `channelChars` ADD PRIMARY KEY (`channelID`, `charID`);

UPDATE `channelMods` SET `accessor` = 0 WHERE `accessor` IS NULL;
DELETE m1 FROM `channelMods` m1 JOIN `channelMods` m2 ON m1.`channelID` = m2.`channelID` AND m1.`accessor` = m2.`accessor` AND m1.`id` < m2.`id`;
ALTER TABLE `channelMods`
  MODIFY `accessor` int(10) unsigned NOT NULL DEFAULT 0,
  MODIFY `mode` int(10) NOT NULL DEFAULT 0,
  MODIFY `originalMode` int(10) DEFAULT NULL,
  ADD UNIQUE KEY `channelAccessor` (`channelID`, `accessor`);

-- +migrate Down
-- the old unsigned columns can't hold kicks/blocks
DELETE FROM `channelMods` WHERE `mode` < 0 OR `originalMode` < 0;
ALTER TABLE `channelMods`
  DROP KEY `channelAccessor`,
  MODIFY `originalMode` int(10) unsigned DEFAULT NULL,
  MODIFY `mode` int(10) unsigned NOT NULL DEFAULT 0,
  MODIFY `accessor` int(10) unsigned DEFAULT NULL;
UPDATE `channelMods` SET `accessor` = NULL WHERE `accessor` = 0;
ALTER TABLE `channelChars` DROP PRIMARY KEY;
ALTER TABLE `channels`
  DROP COLUMN `languageRestriction`,
  MODIFY `comparisonKey` varchar(11) DEFAULT NULL;
