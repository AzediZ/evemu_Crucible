-- Database health:  indexes for the lookups the server makes most, InnoDB for the tables it still writes to as
--  MyISAM, and corporation wallets in double like character wallets.
--  - entity:  items are loaded by location (every station, ship, container and system) and by owner (assets,
--    wallets of items), and with only the primary key each of those read the whole item table
--  - jnlCharacters:  the wallet journal is read by owner, account and date
--  - mktOrders:  a system's best sell prices (GetSystemAsks) grouped every order in the region
--  - staOffices, mapSystemSovInfo:  read by station and by system
--  - MyISAM tables are not crash safe:  a server or Docker stop part way through a write can leave them needing a
--    repair.  These are the ones the server writes to (missions, asteroid belts, offices, sovereignty, outposts,
--    the GM dungeon editor and the player count history).  The rest are static data and stay as they are
--  - crpWalletDivisons:  float keeps about 7 digits, so a player corporation's wallet lost its cents above
--    100,000 ISK and whole ISK above 16 million
-- +migrate Up
ALTER TABLE `entity`
    ADD INDEX IF NOT EXISTS `locationFlag` (`locationID`, `flag`),
    ADD INDEX IF NOT EXISTS `ownerFlag` (`ownerID`, `flag`);
ALTER TABLE `jnlCharacters` ADD INDEX IF NOT EXISTS `ownerKeyDate` (`ownerID`, `accountKey`, `transactionDate`);
ALTER TABLE `mktOrders` ADD INDEX IF NOT EXISTS `systemBidTypePrice` (`solarSystemID`, `bid`, `typeID`, `price`);

ALTER TABLE `agtOffers` ENGINE=InnoDB;
ALTER TABLE `sysAsteroids` ENGINE=InnoDB;
ALTER TABLE `staOffices` ENGINE=InnoDB, ADD INDEX IF NOT EXISTS `stationID` (`stationID`);
ALTER TABLE `mapSystemSovInfo` ENGINE=InnoDB, ADD INDEX IF NOT EXISTS `solarSystemID` (`solarSystemID`);
ALTER TABLE `staStations` ENGINE=InnoDB;
ALTER TABLE `ramAssemblyLineStations` ENGINE=InnoDB;
ALTER TABLE `mapDenormalize` ENGINE=InnoDB;
ALTER TABLE `dunTemplates` ENGINE=InnoDB;
ALTER TABLE `srvStatisticHistory` ENGINE=InnoDB;

ALTER TABLE `crpWalletDivisons`
    MODIFY `balance1` double NOT NULL DEFAULT 0,
    MODIFY `balance2` double NOT NULL DEFAULT 0,
    MODIFY `balance3` double NOT NULL DEFAULT 0,
    MODIFY `balance4` double NOT NULL DEFAULT 0,
    MODIFY `balance5` double NOT NULL DEFAULT 0,
    MODIFY `balance6` double NOT NULL DEFAULT 0,
    MODIFY `balance7` double NOT NULL DEFAULT 0;
-- and round off the float error already in them (123456.78 was kept as 123456.78125)
UPDATE `crpWalletDivisons` SET `balance1` = ROUND(`balance1`, 2), `balance2` = ROUND(`balance2`, 2),
    `balance3` = ROUND(`balance3`, 2), `balance4` = ROUND(`balance4`, 2), `balance5` = ROUND(`balance5`, 2),
    `balance6` = ROUND(`balance6`, 2), `balance7` = ROUND(`balance7`, 2);

-- +migrate Down
ALTER TABLE `crpWalletDivisons`
    MODIFY `balance1` float NOT NULL DEFAULT 0,
    MODIFY `balance2` float NOT NULL DEFAULT 0,
    MODIFY `balance3` float NOT NULL DEFAULT 0,
    MODIFY `balance4` float NOT NULL DEFAULT 0,
    MODIFY `balance5` float NOT NULL DEFAULT 0,
    MODIFY `balance6` float NOT NULL DEFAULT 0,
    MODIFY `balance7` float NOT NULL DEFAULT 0;

ALTER TABLE `srvStatisticHistory` ENGINE=MyISAM;
ALTER TABLE `dunTemplates` ENGINE=MyISAM;
ALTER TABLE `mapDenormalize` ENGINE=MyISAM;
ALTER TABLE `ramAssemblyLineStations` ENGINE=MyISAM;
ALTER TABLE `staStations` ENGINE=MyISAM;
ALTER TABLE `mapSystemSovInfo` DROP INDEX IF EXISTS `solarSystemID`, ENGINE=MyISAM;
ALTER TABLE `staOffices` DROP INDEX IF EXISTS `stationID`, ENGINE=MyISAM;
ALTER TABLE `sysAsteroids` ENGINE=MyISAM;
ALTER TABLE `agtOffers` ENGINE=MyISAM;

ALTER TABLE `mktOrders` DROP INDEX IF EXISTS `systemBidTypePrice`;
ALTER TABLE `jnlCharacters` DROP INDEX IF EXISTS `ownerKeyDate`;
ALTER TABLE `entity`
    DROP INDEX IF EXISTS `ownerFlag`,
    DROP INDEX IF EXISTS `locationFlag`;
