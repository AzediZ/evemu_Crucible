-- F24 R&D agents:  a character's research project per agent (points banked up to lastUpdate, then accruing at
-- pointsPerDay) and the pre-Inferno research field multipliers (starship engineering x3, some physics x2, rest x1)
-- +migrate Up
CREATE TABLE IF NOT EXISTS chrAgentResearch (
  characterID INT(10) UNSIGNED NOT NULL,
  agentID INT(10) UNSIGNED NOT NULL,
  skillTypeID INT(10) UNSIGNED NOT NULL,
  pointsBanked DOUBLE NOT NULL DEFAULT 0,
  pointsPerDay DOUBLE NOT NULL DEFAULT 0,
  lastUpdate BIGINT(20) NOT NULL,
  startDate BIGINT(20) NOT NULL,
  PRIMARY KEY (characterID, agentID),
  KEY agentID (agentID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS agtResearchFieldMultipliers (
  skillTypeID INT(10) UNSIGNED NOT NULL,
  multiplier FLOAT NOT NULL DEFAULT 1,
  PRIMARY KEY (skillTypeID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
-- Amarrian, Minmatar, Gallentean, Caldari Starship Engineering; Graviton Physics, Plasma Physics
INSERT IGNORE INTO agtResearchFieldMultipliers (skillTypeID, multiplier) VALUES
 (11444, 3), (11445, 3), (11450, 3), (11454, 3),
 (11446, 2), (11441, 2);

-- +migrate Down
DROP TABLE IF EXISTS agtResearchFieldMultipliers;
DROP TABLE IF EXISTS chrAgentResearch;
