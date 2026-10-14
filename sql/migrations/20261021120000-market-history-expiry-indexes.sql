-- The daily price history older than <market><HistoryDays> is deleted each day (by date), and the order expiry
--  check reads player corporation orders by isCorp (MarketDB::GetExpiredOrders)
-- +migrate Up
ALTER TABLE `mktHistory` ADD INDEX IF NOT EXISTS `historyDate` (`historyDate`);
ALTER TABLE `mktOrders` ADD INDEX IF NOT EXISTS `isCorp` (`isCorp`);

-- +migrate Down
ALTER TABLE `mktOrders` DROP INDEX IF EXISTS `isCorp`;
ALTER TABLE `mktHistory` DROP INDEX IF EXISTS `historyDate`;
