-- the market simulation's phase 4 logistics (doc/code_and_design_notes/market-sim/PLAN.md 6.5, 8):  a shipment is a hull
--  carrying goods between a node and a node, or from a node's low/null-sec site to it.  mktSimShipment (made by
--  20261013130100-market-sim.sql) has one row per cargo line;  the rows of one hull share tripID, and hullTypeID is the
--  hull its loss destroys.  state:  0 arrives, 1 is lost on the way (rolled when it left).  the goods themselves are the
--  carrier's stock at nodeID 65535 in mktSimInventory, the sites' at 16384 + 4 x nodeID + band
-- +migrate Up
ALTER TABLE mktSimShipment ADD COLUMN `tripID` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `state`, ADD COLUMN `hullTypeID` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `tripID`, ADD KEY `tripID` (`tripID`);

-- +migrate Down
ALTER TABLE mktSimShipment DROP KEY `tripID`, DROP COLUMN `hullTypeID`, DROP COLUMN `tripID`;
