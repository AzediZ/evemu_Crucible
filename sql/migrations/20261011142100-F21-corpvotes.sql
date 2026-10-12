-- F21 shares and votes:  one share row per (corporation, shareholder), share-weighted votes with one vote per voter,
--  vote cases with their proposer, result and sanction deadline, longer texts
-- +migrate Up
CREATE TABLE crpSharesNew (
  corporationID INT(10) UNSIGNED NOT NULL,
  shareholderID INT(10) UNSIGNED NOT NULL,
  shares INT(10) UNSIGNED NOT NULL DEFAULT 0,
  shareholderCorporationID INT(10) NOT NULL DEFAULT 0,
  PRIMARY KEY (corporationID, shareholderID),
  KEY shareholderID (shareholderID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
INSERT INTO crpSharesNew (corporationID, shareholderID, shares, shareholderCorporationID)
  SELECT corporationID, shareholderID, SUM(shares), MAX(shareholderCorporationID) FROM crpShares
  GROUP BY corporationID, shareholderID HAVING SUM(shares) > 0;
DROP TABLE crpShares;
RENAME TABLE crpSharesNew TO crpShares;

UPDATE crpVoteItems SET inEffect = 0 WHERE actedUpon = 0;
ALTER TABLE crpVoteItems
  MODIFY voteCaseText VARCHAR(500) NOT NULL DEFAULT '',
  MODIFY description TEXT NOT NULL,
  MODIFY inEffect TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,
  ADD COLUMN characterID INT(10) UNSIGNED NOT NULL DEFAULT 0 AFTER corporationID,
  ADD COLUMN winningOptionID TINYINT(2) NULL DEFAULT NULL,
  ADD COLUMN expires BIGINT(20) NOT NULL DEFAULT 0,
  ADD KEY corpStatus (corporationID, status);

ALTER TABLE crpVoteOptions
  MODIFY optionText VARCHAR(500) NOT NULL DEFAULT '',
  MODIFY votesFor DOUBLE NOT NULL DEFAULT 0,
  ADD KEY voteCaseID (voteCaseID);

CREATE TABLE crpVotesNew (
  voteCaseID INT(11) NOT NULL,
  charID INT(10) UNSIGNED NOT NULL,
  corpID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  optionID TINYINT(2) NOT NULL DEFAULT 0,
  weight INT(10) UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (voteCaseID, charID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
INSERT IGNORE INTO crpVotesNew (voteCaseID, charID, corpID, optionID) SELECT voteCaseID, charID, corpID, optionID FROM crpVotes;
DROP TABLE crpVotes;
RENAME TABLE crpVotesNew TO crpVotes;

-- +migrate Down
CREATE TABLE crpVotesOld (
  charID INT(11) NOT NULL,
  corpID INT(11) NOT NULL,
  voteCaseID MEDIUMINT(11) NOT NULL,
  optionID TINYINT(1) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;
INSERT INTO crpVotesOld (charID, corpID, voteCaseID, optionID) SELECT charID, corpID, voteCaseID, optionID FROM crpVotes;
DROP TABLE crpVotes;
RENAME TABLE crpVotesOld TO crpVotes;

ALTER TABLE crpVoteOptions DROP KEY voteCaseID, MODIFY votesFor INT(10) NOT NULL DEFAULT 0;
ALTER TABLE crpVoteItems DROP KEY corpStatus, DROP COLUMN expires, DROP COLUMN winningOptionID, DROP COLUMN characterID;

CREATE TABLE crpSharesOld (
  id INT(11) NOT NULL AUTO_INCREMENT,
  shareholderID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  corporationID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  shares INT(10) UNSIGNED NOT NULL DEFAULT 0,
  shareholderCorporationID INT(10) NOT NULL DEFAULT 0,
  PRIMARY KEY (id, shareholderID),
  KEY shareholderID (shareholderID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
INSERT INTO crpSharesOld (shareholderID, corporationID, shares, shareholderCorporationID)
  SELECT shareholderID, corporationID, shares, shareholderCorporationID FROM crpShares;
DROP TABLE crpShares;
RENAME TABLE crpSharesOld TO crpShares;
