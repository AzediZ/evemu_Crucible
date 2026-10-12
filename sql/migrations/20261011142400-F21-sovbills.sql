-- F21 sovereignty bills:  the billing period of each claimed system (one bill per system every 14 days from its claim)
-- +migrate Up
CREATE TABLE IF NOT EXISTS sovBilling (
  solarSystemID INT(10) UNSIGNED NOT NULL,
  claimID INT(10) UNSIGNED NOT NULL,
  nextBillDateTime BIGINT(20) NOT NULL DEFAULT 0,
  PRIMARY KEY (solarSystemID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS sovBilling;
