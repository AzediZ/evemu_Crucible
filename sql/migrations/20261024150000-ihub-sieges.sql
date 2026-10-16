-- Infrastructure hub sieges:  the damage and reinforcement of hubs that have been attacked (IHubSE).  phase is
--  OutpostSiege::Phase (EVE_Outpost.h), timerEnd the FILETIME its reinforcement ends and updated the FILETIME of the save
--  (shields regenerate while the system is unloaded).  a hub without a row is at full health
-- +migrate Up
CREATE TABLE IF NOT EXISTS `sovHubSiege` (
  `hubID` int(10) unsigned NOT NULL,
  `phase` tinyint(3) unsigned NOT NULL DEFAULT 0,
  `shieldCharge` double NOT NULL DEFAULT 0,
  `armorDamage` double NOT NULL DEFAULT 0,
  `damage` double NOT NULL DEFAULT 0,
  `timerEnd` bigint(20) NOT NULL DEFAULT 0,
  `updated` bigint(20) NOT NULL DEFAULT 0,
  PRIMARY KEY (`hubID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

-- +migrate Down
DROP TABLE IF EXISTS `sovHubSiege`;
