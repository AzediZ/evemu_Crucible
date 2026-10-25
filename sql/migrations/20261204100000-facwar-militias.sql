-- factional warfare's militias (FacWarMgr.h):  the militia corporations are their faction's
--  (crpCorporation.warFactionID, which the session's warfactionid and crimewatch read), and who enlisted when:  a character
--  that enlisted on its own (the 24 hours before it may again), a player corporation in a militia or joining or leaving one
--  at downtime (status 0 joining, 1 in, 2 leaving, as FacWar::CorpStatus), and when one was warned its standing fell below
--  what its militia takes
-- +migrate Up
UPDATE crpCorporation SET warFactionID = 500003 WHERE corporationID = 1000179;
UPDATE crpCorporation SET warFactionID = 500001 WHERE corporationID = 1000180;
UPDATE crpCorporation SET warFactionID = 500004 WHERE corporationID = 1000181;
UPDATE crpCorporation SET warFactionID = 500002 WHERE corporationID = 1000182;
CREATE TABLE IF NOT EXISTS facWarEnlistments (
    ownerID INT(10) UNSIGNED NOT NULL,
    factionID INT(10) UNSIGNED NOT NULL,
    status TINYINT(3) UNSIGNED NOT NULL DEFAULT 1,
    startDate BIGINT(20) NOT NULL DEFAULT 0,
    effectiveAt BIGINT(20) NOT NULL DEFAULT 0,
    warnedAt BIGINT(20) NOT NULL DEFAULT 0,
    PRIMARY KEY (ownerID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS facWarEnlistments;
UPDATE crpCorporation SET warFactionID = 0 WHERE corporationID IN (1000179, 1000180, 1000181, 1000182);
