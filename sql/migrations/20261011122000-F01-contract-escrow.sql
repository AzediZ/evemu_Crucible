-- contract rewards are held by the system (ownerSystem) from creation until they are paid or refunded.
--  rows created before this column existed: only courier rewards were taken at creation
-- +migrate Up
ALTER TABLE ctrContracts ADD COLUMN rewardEscrowed TINYINT(1) NOT NULL DEFAULT 0;
UPDATE ctrContracts SET rewardEscrowed = 1 WHERE contractType = 3 AND reward > 0;

-- +migrate Down
ALTER TABLE ctrContracts DROP COLUMN rewardEscrowed;
