-- F17 contracts: 64-bit isk columns (the client allows up to 1e12), long descriptions, broker's fee and deposit kept
--  per contract (the deposit is refunded when it ends), auction bids with their own id, corp flag, bidding character and
--  refund state, list indexes, and dismissed "requires attention" entries per owner (DeleteNotification).
-- +migrate Up
ALTER TABLE ctrContracts
  MODIFY price bigint NOT NULL, MODIFY reward bigint NOT NULL, MODIFY collateral bigint NOT NULL,
  MODIFY description text NOT NULL,
  ADD COLUMN brokersFee bigint NOT NULL DEFAULT 0,
  ADD COLUMN deposit bigint NOT NULL DEFAULT 0,
  ADD COLUMN depositRefunded tinyint(1) NOT NULL DEFAULT 0,
  ADD KEY issuerStatus (issuerID, status), ADD KEY issuerCorpStatus (issuerCorpID, status),
  ADD KEY assigneeStatus (assigneeID, status), ADD KEY acceptorStatus (acceptorID, status), ADD KEY crate (crateID);
ALTER TABLE ctrBids MODIFY amount bigint NOT NULL,
  ADD COLUMN bidID int NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST,
  ADD COLUMN forCorp tinyint(1) NOT NULL DEFAULT 0,
  ADD COLUMN bidderCharID int NOT NULL DEFAULT 0,
  ADD COLUMN refunded tinyint(1) NOT NULL DEFAULT 0,
  ADD KEY contract (contractId), ADD KEY bidder (bidderID);
ALTER TABLE ctrItems ADD KEY contract (contractId);
CREATE TABLE IF NOT EXISTS ctrDismissed (
  contractID int NOT NULL,
  ownerID int NOT NULL,
  PRIMARY KEY (contractID, ownerID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS ctrDismissed;
ALTER TABLE ctrItems DROP KEY contract;
ALTER TABLE ctrBids DROP KEY bidder, DROP KEY contract, DROP COLUMN refunded, DROP COLUMN bidderCharID, DROP COLUMN forCorp,
  DROP COLUMN bidID, MODIFY amount int NOT NULL;
ALTER TABLE ctrContracts DROP KEY crate, DROP KEY acceptorStatus, DROP KEY assigneeStatus, DROP KEY issuerCorpStatus,
  DROP KEY issuerStatus, DROP COLUMN depositRefunded, DROP COLUMN deposit, DROP COLUMN brokersFee,
  MODIFY description varchar(100) NOT NULL, MODIFY collateral int NOT NULL, MODIFY reward int NOT NULL, MODIFY price int NOT NULL;
