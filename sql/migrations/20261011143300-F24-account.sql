-- F24 account services:  CSPA contact cost per character (NULL = only contacts may contact), redeem tokens and
-- player reports (bots, ISK spammers)
-- +migrate Up
ALTER TABLE chrCharacters ADD COLUMN contactCost INT(11) NULL DEFAULT 0 AFTER paperDollState;

CREATE TABLE IF NOT EXISTS accRedeemTokens (
  tokenID INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  accountID INT(10) UNSIGNED NOT NULL,
  massTokenID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  typeID INT(10) UNSIGNED NOT NULL,
  quantity INT(10) UNSIGNED NOT NULL DEFAULT 1,
  label VARCHAR(100) NOT NULL DEFAULT '',
  description VARCHAR(1000) NOT NULL DEFAULT '',
  dateTime BIGINT(20) NOT NULL,
  expireDateTime BIGINT(20) NULL,
  availableDateTime BIGINT(20) NULL,
  stationID INT(10) UNSIGNED NULL,
  claimedBy INT(10) UNSIGNED NULL,
  claimedDateTime BIGINT(20) NULL,
  PRIMARY KEY (tokenID),
  KEY accountID (accountID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS accUserReports (
  reportID INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  reporterID INT(10) UNSIGNED NOT NULL,
  reportedID INT(10) UNSIGNED NOT NULL,
  reportType TINYINT(3) UNSIGNED NOT NULL,
  channel VARCHAR(100) NOT NULL DEFAULT '',
  entries TEXT NOT NULL,
  reportDateTime BIGINT(20) NOT NULL,
  PRIMARY KEY (reportID),
  KEY reportedID (reportedID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS accUserReports;
DROP TABLE IF EXISTS accRedeemTokens;
ALTER TABLE chrCharacters DROP COLUMN contactCost;
