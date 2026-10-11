-- F17 market: exact ISK columns for escrow and transaction prices, transactions stored per owner (character or corp),
--  indexes for the daily price history rollup and the wallet views, one history row per region/type/day.
--  mktUpdates.timeStamp now holds the first day (filetime, midnight UTC) not yet rolled up into mktHistory.
-- +migrate Up
ALTER TABLE mktOrders MODIFY `escrow` decimal(17,2) NOT NULL DEFAULT '0';
ALTER TABLE mktTransactions MODIFY `price` decimal(16,2) NOT NULL DEFAULT '0',
  MODIFY `quantity` int(10) unsigned NOT NULL DEFAULT '0',
  ADD COLUMN `ownerID` int(10) unsigned NOT NULL DEFAULT 0 AFTER `transactionType`,
  ADD KEY `ownerDate` (`ownerID`, `transactionDate`),
  ADD KEY `regionTypeDate` (`regionID`, `typeID`, `transactionDate`);
-- before F17 the characterID column held the owner of the row (the member was never recorded)
UPDATE mktTransactions SET ownerID = characterID;
-- the old rollup never ran on a day boundary; drop anything it may have written
DELETE FROM mktHistory WHERE historyDate % 864000000000 <> 0;
ALTER TABLE mktHistory MODIFY `volume` bigint(20) unsigned NOT NULL, MODIFY `orders` int(10) unsigned NOT NULL,
  ADD UNIQUE KEY `regionTypeDate` (`regionID`, `typeID`, `historyDate`);
INSERT IGNORE INTO mktUpdates (server, timeStamp) VALUES (1, 0);
UPDATE mktUpdates SET timeStamp = 0 WHERE server = 1;

-- +migrate Down
ALTER TABLE mktHistory DROP KEY `regionTypeDate`, MODIFY `volume` int(10) unsigned NOT NULL, MODIFY `orders` mediumint(8) unsigned NOT NULL;
ALTER TABLE mktTransactions DROP KEY `regionTypeDate`, DROP KEY `ownerDate`, DROP COLUMN `ownerID`,
  MODIFY `price` float NOT NULL DEFAULT '0', MODIFY `quantity` mediumint(10) unsigned NOT NULL DEFAULT '0';
ALTER TABLE mktOrders MODIFY `escrow` float unsigned NOT NULL DEFAULT '0';
