-- chat gags (/gag, /chatban, /gagiskspammer).  kept in the db so a restart doesn't lift them
-- +migrate Up
CREATE TABLE IF NOT EXISTS `chrChatGags` (
  `characterID` int(10) unsigned NOT NULL,
  `untilTime` bigint(20) NOT NULL,
  `reason` varchar(255) NOT NULL DEFAULT '',
  `gmCharacterID` int(10) unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`characterID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS `chrChatGags`;
