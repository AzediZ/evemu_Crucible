-- the market simulation's phase 3 actors (doc/code_and_design_notes/market-sim/PLAN.md 6.3, 6.4, 8):  what the sim's
--  consumers still want to replace of the ships and fits they lost, per node and type.  the actors themselves use the
--  tables 20261013130100-market-sim.sql made:  mktSimActor (miners, reprocessors, manufacturers, loot sources),
--  mktSimInventory (their stock, the miners' fleets) and mktSimJob (the manufacturers' jobs on virtual lines,
--  assemblyLineID 0 until phase 5)
-- +migrate Up
ALTER TABLE mktSimCell ADD COLUMN `lossWant` DOUBLE NOT NULL DEFAULT 0 AFTER `clampDays`;

-- +migrate Down
ALTER TABLE mktSimCell DROP COLUMN `lossWant`;
