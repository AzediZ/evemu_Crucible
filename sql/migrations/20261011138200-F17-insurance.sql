-- F17 insurance: 64-bit payout, the insured type and corp (corp insurance), expiry notification flag, owner and expiry
--  indexes.  payOutAmount is informational: the payout is worked out from the insurance value when the ship is lost.
-- +migrate Up
ALTER TABLE shipInsurance MODIFY payOutAmount bigint NOT NULL DEFAULT 0,
  ADD COLUMN typeID int NOT NULL DEFAULT 0, ADD COLUMN corpID int NOT NULL DEFAULT 0,
  ADD COLUMN expiryNotified tinyint(1) NOT NULL DEFAULT 0, ADD KEY ownerID (ownerID), ADD KEY endDate (endDate);
UPDATE shipInsurance si JOIN entity e ON e.itemID = si.shipID SET si.typeID = e.typeID;

-- +migrate Down
ALTER TABLE shipInsurance DROP KEY endDate, DROP KEY ownerID, DROP COLUMN expiryNotified, DROP COLUMN corpID,
  DROP COLUMN typeID, MODIFY payOutAmount int NOT NULL DEFAULT 0;
