-- notifications tab of the mail window (notificationMgr).  kept per character until the player deletes them.
-- data is the YAML text the client loads with yaml.CSafeLoader
-- +migrate Up
CREATE TABLE IF NOT EXISTS `chrNotifications` (
  `notificationID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `typeID` smallint(5) unsigned NOT NULL,
  `senderID` int(10) unsigned NOT NULL DEFAULT 0,
  `receiverID` int(10) unsigned NOT NULL,
  `processed` tinyint(1) NOT NULL DEFAULT 0,
  `created` bigint(20) NOT NULL,
  `data` text DEFAULT NULL,
  PRIMARY KEY (`notificationID`),
  KEY `receiver` (`receiverID`, `processed`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS `chrNotifications`;
