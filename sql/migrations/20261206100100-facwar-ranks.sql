-- factional warfare ranks (FacWarMgr.h):  a militia pilot's rank with each faction it fought for, the highest it
--  reached and when it last changed.  the rank is by the pilot's standing with the faction, as in Crucible.  signed
--  ints, as the client's rowset of them has (facWarMgr.GetMyCharacterRankOverview)
-- +migrate Up
CREATE TABLE IF NOT EXISTS facWarRanks (
    characterID INT(10) UNSIGNED NOT NULL,
    factionID INT(10) NOT NULL,
    currentRank INT(10) NOT NULL DEFAULT 0,
    highestRank INT(10) NOT NULL DEFAULT 0,
    lastModified BIGINT(20) NOT NULL DEFAULT 0,
    PRIMARY KEY (characterID, factionID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS facWarRanks;
