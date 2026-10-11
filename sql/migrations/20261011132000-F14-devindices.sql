-- sovereignty development indices (military/industrial points per solar system).  written by SovereigntyDataMgr::SaveDevIndices
-- +migrate Up
CREATE TABLE IF NOT EXISTS `sovDevIndices` (
  `solarSystemID` int(10) unsigned NOT NULL,
  `militaryPoints` bigint(20) NOT NULL DEFAULT 0,
  `industrialPoints` bigint(20) NOT NULL DEFAULT 0,
  `militaryIncreasing` tinyint(1) NOT NULL DEFAULT 0,
  `industrialIncreasing` tinyint(1) NOT NULL DEFAULT 0,
  `lastDecayTime` bigint(20) NOT NULL DEFAULT 0,
  PRIMARY KEY (`solarSystemID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS `sovDevIndices`;
