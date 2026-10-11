-- +migrate Up
-- F19: extraction programs deplete the planet's resource around their heads; it grows back over time.
--  the base resource maps are generated from the planet and type, so they need no table
CREATE TABLE IF NOT EXISTS `piResourceDepletion` (
  `depletionID` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  `planetID` INT(10) NOT NULL,
  `typeID` SMALLINT(6) NOT NULL,
  `latitude` DOUBLE NOT NULL,
  `longitude` DOUBLE NOT NULL,
  `headRadius` DOUBLE NOT NULL,
  `amount` DOUBLE NOT NULL,
  `startTime` BIGINT(20) NOT NULL,
  `duration` BIGINT(20) NOT NULL,
  PRIMARY KEY (`depletionID`),
  KEY `planetType` (`planetID`, `typeID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='PI resource taken out by extraction programs';

-- +migrate Down
DROP TABLE IF EXISTS `piResourceDepletion`;
