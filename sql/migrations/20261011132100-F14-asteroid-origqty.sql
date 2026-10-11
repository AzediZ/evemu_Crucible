-- spawned quantity of each saved asteroid; mined rocks regrow to it (BeltMgr)
-- +migrate Up
ALTER TABLE `sysAsteroids` ADD COLUMN `originalQuantity` double NOT NULL DEFAULT 0 AFTER `quantity`;
UPDATE `sysAsteroids` SET `originalQuantity` = `quantity`;

-- +migrate Down
ALTER TABLE `sysAsteroids` DROP COLUMN `originalQuantity`;
