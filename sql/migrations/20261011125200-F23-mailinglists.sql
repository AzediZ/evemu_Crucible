-- eve mail mailing lists: many members per list, normalized name key, welcome mail, entity access (allowed/blocked owners)
-- +migrate Up
ALTER TABLE `mailList`
  MODIFY `displayName` varchar(60) COLLATE utf8_unicode_ci NOT NULL,
  ADD COLUMN `name` varchar(60) COLLATE utf8_unicode_ci NOT NULL DEFAULT '' AFTER `id`,
  ADD COLUMN `welcomeMailTitle` text COLLATE utf8_unicode_ci NULL,
  ADD COLUMN `welcomeMailBody` text COLLATE utf8_unicode_ci NULL;
UPDATE `mailList` SET `name` = LOWER(REPLACE(REPLACE(`displayName`, ' ', ''), '\t', ''));
UPDATE `mailList` SET `name` = CONCAT(`name`, '_', `id`)
  WHERE `id` NOT IN (SELECT `keep` FROM (SELECT MIN(`id`) AS `keep` FROM `mailList` GROUP BY `name`) AS `k`);
ALTER TABLE `mailList` ADD UNIQUE KEY `name` (`name`);

ALTER TABLE `mailListUsers`
  DROP PRIMARY KEY,
  ADD PRIMARY KEY (`listID`, `characterID`),
  ADD KEY `characterID` (`characterID`);

CREATE TABLE IF NOT EXISTS `mailListAccess` (
  `listID` int(10) unsigned NOT NULL,
  `ownerID` int(10) unsigned NOT NULL,
  `access` tinyint(4) NOT NULL,
  PRIMARY KEY (`listID`, `ownerID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
-- best effort: the old single column primary key can't hold lists with more than one member
DROP TABLE IF EXISTS `mailListAccess`;
DELETE u1 FROM `mailListUsers` u1 JOIN `mailListUsers` u2 ON u1.`listID` = u2.`listID` AND u1.`characterID` > u2.`characterID`;
ALTER TABLE `mailListUsers`
  DROP KEY `characterID`,
  DROP PRIMARY KEY,
  ADD PRIMARY KEY (`listID`);
ALTER TABLE `mailList`
  DROP KEY `name`,
  DROP COLUMN `welcomeMailBody`,
  DROP COLUMN `welcomeMailTitle`,
  DROP COLUMN `name`,
  MODIFY `displayName` varchar(30) COLLATE utf8_unicode_ci NOT NULL;
