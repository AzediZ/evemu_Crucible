-- market simulation phase 5 (doc/code_and_design_notes/market-sim/PLAN.md 6.6):  the sim's jobs take npc station assembly
--  lines.  the sim writes its jobs (mktSimJob) and their lines' nextFreeTime in one transaction, with the stock the jobs
--  used and made.  ramAssemblyLines was MyISAM, which ignores transactions:  after a crash a line would stay busy for a
--  job the rest of the transaction never saved
-- +migrate Up
ALTER TABLE ramAssemblyLines ENGINE=InnoDB;

-- +migrate Down
ALTER TABLE ramAssemblyLines ENGINE=MyISAM;
