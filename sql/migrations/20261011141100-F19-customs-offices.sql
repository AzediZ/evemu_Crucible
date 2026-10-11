-- +migrate Up
-- F19: every planet gets its npc customs office once (Crucible:  a destroyed Interbus office is not replaced)
CREATE TABLE IF NOT EXISTS `piCustomsOfficeSpawn` (
  `planetID` INT(10) NOT NULL,
  `spawnTime` BIGINT(20) NOT NULL DEFAULT 0,
  PRIMARY KEY (`planetID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='planets that had their npc customs office';
INSERT IGNORE INTO `piCustomsOfficeSpawn` (`planetID`)
  SELECT CAST(`customInfo` AS UNSIGNED) FROM `entity`
  WHERE `typeID` IN (2233, 4318) AND `customInfo` <> '' AND CAST(`customInfo` AS UNSIGNED) > 0;
ALTER TABLE `posCustomsOfficeData` ADD PRIMARY KEY (`itemID`);
ALTER TABLE `piLaunches` ADD KEY `charID` (`charID`), ADD KEY `itemID` (`itemID`);

-- +migrate Down
ALTER TABLE `piLaunches` DROP KEY `charID`, DROP KEY `itemID`;
ALTER TABLE `posCustomsOfficeData` DROP PRIMARY KEY;
DROP TABLE IF EXISTS `piCustomsOfficeSpawn`;
