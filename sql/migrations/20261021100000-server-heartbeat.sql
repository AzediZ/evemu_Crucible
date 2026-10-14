-- The last time the server was running (FILETIME), written every minute and at shutdown.  At the next start the
--  bill, office rent and war bill due dates move on by the time the server was off (<corp><PauseBillsWhileOff>)
-- +migrate Up
ALTER TABLE `srvStatus` ADD COLUMN IF NOT EXISTS `lastHeartbeat` bigint(20) NOT NULL DEFAULT 0;

-- +migrate Down
ALTER TABLE `srvStatus` DROP COLUMN IF EXISTS `lastHeartbeat`;
