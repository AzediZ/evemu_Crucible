-- recent sovereignty activity for the starmap (map.GetRecentSovActivity, last 24 hours).  written by SovereigntyDB::LogSovChange
-- +migrate Up
CREATE TABLE IF NOT EXISTS `sovChangeLog` (
  `logID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `solarSystemID` int(10) unsigned NOT NULL,
  `stationID` int(10) unsigned NULL DEFAULT NULL,
  `ownerID` int(10) unsigned NULL DEFAULT NULL,
  `oldOwnerID` int(10) unsigned NULL DEFAULT NULL,
  `changeTime` bigint(20) NOT NULL,
  PRIMARY KEY (`logID`),
  KEY `changeTime` (`changeTime`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS `sovChangeLog`;
