-- F21 bills:  one bill table for character, corporation and alliance bills (billsPayable/billsReceivable are no longer used),
--  the auto-pay wallet division, office rent state (impounded, still rented out, open bill) and alliance maintenance billing
-- +migrate Up
CREATE TABLE IF NOT EXISTS bills (
  billID INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  billTypeID TINYINT(3) UNSIGNED NOT NULL,
  debtorID INT(10) UNSIGNED NOT NULL,
  creditorID INT(10) UNSIGNED NOT NULL,
  amount DOUBLE NOT NULL,
  interest DOUBLE NOT NULL DEFAULT 0,
  dueDateTime BIGINT(20) NOT NULL,
  createdDateTime BIGINT(20) NOT NULL,
  externalID INT(10) NOT NULL DEFAULT -1,
  externalID2 INT(10) NOT NULL DEFAULT -1,
  paid TINYINT(3) UNSIGNED NOT NULL DEFAULT 0,      -- billUnpaid 0, billPaid 1, billCancelled 2
  paidDateTime BIGINT(20) NOT NULL DEFAULT 0,
  PRIMARY KEY (billID),
  KEY debtor (debtorID, paid),
  KEY creditor (creditorID, paid),
  KEY due (paid, dueDateTime)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

ALTER TABLE crpAutoPay ADD COLUMN divisionID SMALLINT(5) NOT NULL DEFAULT 1000;

ALTER TABLE staOffices
  ADD COLUMN impounded TINYINT(1) NOT NULL DEFAULT 0,
  ADD COLUMN publiclyAvailable TINYINT(1) NOT NULL DEFAULT 1,
  ADD COLUMN billID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  ADD KEY corpStation (corporationID, stationID);

CREATE TABLE IF NOT EXISTS alnBilling (
  allianceID INT(10) UNSIGNED NOT NULL,
  nextBillDateTime BIGINT(20) NOT NULL DEFAULT 0,
  PRIMARY KEY (allianceID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS alnBilling;
ALTER TABLE staOffices DROP KEY corpStation, DROP COLUMN billID, DROP COLUMN publiclyAvailable, DROP COLUMN impounded;
ALTER TABLE crpAutoPay DROP COLUMN divisionID;
DROP TABLE IF EXISTS bills;
