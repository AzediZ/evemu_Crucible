-- F12 agent missions: offer columns for encounter/mining/storyline content, the mission content table (filled by
--  20261011136100-F12-mission-content.sql), storyline counters, epic arc completions (read by the holoscreen), and
--  the Incursion 1.5 agent divisions (May 2011, in force at Crucible):  Distribution (22) <- Accounting, Advisory,
--  Archives, Distribution, Financial, Marketing, Personnel, Public Relations, Storage;  Mining (23) <- Astrosurveying,
--  Manufacturing, Mining, Production;  Security (24) <- Administration, Command, Intelligence, Internal Security,
--  Legal, Security, Surveillance.  R&D (18) is unchanged.  the old division is kept in legacyDivisionID.
-- +migrate Up
ALTER TABLE agtOffers
  ADD COLUMN contentID INT(10) NOT NULL DEFAULT 0,
  ADD COLUMN nameID INT(10) NOT NULL DEFAULT 0,
  ADD COLUMN objectiveType TINYINT(3) NOT NULL DEFAULT 0,
  ADD COLUMN objectiveTypeID INT(10) NOT NULL DEFAULT 0,
  ADD COLUMN objectiveQty INT(10) NOT NULL DEFAULT 0,
  ADD COLUMN objectiveCompleted TINYINT(1) NOT NULL DEFAULT 0,
  ADD COLUMN enemyFactionID INT(10) NOT NULL DEFAULT 0,
  ADD COLUMN dungeonID INT(10) NOT NULL DEFAULT 0,
  ADD COLUMN dungeonX DOUBLE NOT NULL DEFAULT 0,
  ADD COLUMN dungeonY DOUBLE NOT NULL DEFAULT 0,
  ADD COLUMN dungeonZ DOUBLE NOT NULL DEFAULT 0,
  ADD COLUMN briefingText TEXT NULL,
  MODIFY rewardLP INT(10) NOT NULL DEFAULT 0,
  MODIFY courierAmount INT(10) NOT NULL DEFAULT 0,
  ADD INDEX agtOffers_IX_char (characterID),
  ADD INDEX agtOffers_IX_agent (agentID);

CREATE TABLE IF NOT EXISTS msnMissions (
  contentID INT(10) NOT NULL,                       -- stable id from the importer (sorted page key order)
  pageKey VARCHAR(64) NOT NULL DEFAULT '',          -- eve-survival.org page name (provenance)
  snapshot CHAR(14) NOT NULL DEFAULT '',            -- wayback timestamp of the page used
  name VARCHAR(100) NOT NULL,
  nameID INT(10) NOT NULL DEFAULT 0,                -- client title messageID (agtMissions.id of the same name), 0 = send name
  briefingID INT(10) NOT NULL DEFAULT 0,            -- client briefing messageID, 0 = send briefingText
  briefingText TEXT NULL,
  level TINYINT(3) NOT NULL,
  typeID TINYINT(3) NOT NULL,                       -- Mission::Type
  important TINYINT(1) NOT NULL DEFAULT 0,
  storyline TINYINT(1) NOT NULL DEFAULT 0,
  enemyFactionID INT(10) NOT NULL DEFAULT 0,
  agentRaceMask TINYINT(3) NOT NULL DEFAULT 0,      -- 0 = any agent; Caldari 1, Minmatar 2, Amarr 4, Gallente 8
  objectiveType TINYINT(3) NOT NULL DEFAULT 1,      -- Mission::ObjType
  objectiveTypeID INT(10) NOT NULL DEFAULT 0,
  objectiveQty INT(10) NOT NULL DEFAULT 0,
  dungeonID INT(10) NOT NULL DEFAULT 0,             -- dunDungeons.dungeonID of the mission pocket(s)
  spaceType TINYINT(3) NOT NULL DEFAULT 0,          -- 0 normal space, 1 deadspace without gates, 2 deadspace with gates
  restrictLevel TINYINT(3) NOT NULL DEFAULT 0,      -- 0 = mission level's ship restrictions, 255 = none
  rewardISK INT(10) NOT NULL DEFAULT 0,             -- 0 = the level's config default
  bonusISK INT(10) NOT NULL DEFAULT 0,
  bonusTime SMALLINT(6) NOT NULL DEFAULT 0,         -- minutes, 0 = the level's config default
  rewardLP INT(10) NOT NULL DEFAULT 0,              -- base LP before security and skills, 0 = the level's config default
  rewardScale FLOAT NOT NULL DEFAULT 1.0,
  rewardItemTypeID INT(10) NOT NULL DEFAULT 0,
  rewardItemQty INT(10) NOT NULL DEFAULT 0,
  collateral INT(10) NOT NULL DEFAULT 0,
  remoteOfferable TINYINT(1) NOT NULL DEFAULT 1,
  remoteCompletable TINYINT(1) NOT NULL DEFAULT 0,
  enabled TINYINT(1) NOT NULL DEFAULT 1,
  PRIMARY KEY (contentID),
  KEY msn_level_type (level, typeID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS chrMissionCounters (
  characterID INT(10) NOT NULL,
  factionID INT(10) NOT NULL,
  level TINYINT(3) NOT NULL,
  completed INT(10) NOT NULL DEFAULT 0,             -- missions of this level for this faction since the last storyline offer
  PRIMARY KEY (characterID, factionID, level)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS chrEpicArcCompletions (
  characterID INT(10) NOT NULL,
  epicArcID INT(10) NOT NULL,
  completionDate BIGINT(20) NOT NULL,
  PRIMARY KEY (characterID, epicArcID),
  KEY completionDate (completionDate)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

ALTER TABLE agtAgents ADD COLUMN legacyDivisionID TINYINT(3) UNSIGNED NULL;
UPDATE agtAgents SET legacyDivisionID = divisionID;
UPDATE agtAgents SET divisionID = 22 WHERE divisionID IN (1, 3, 4, 7, 8, 13, 15, 17, 20);
UPDATE agtAgents SET divisionID = 23 WHERE divisionID IN (5, 12, 14, 16);
UPDATE agtAgents SET divisionID = 24 WHERE divisionID IN (2, 6, 9, 10, 11, 19, 21);

-- +migrate Down
UPDATE agtAgents SET divisionID = legacyDivisionID WHERE legacyDivisionID IS NOT NULL;
ALTER TABLE agtAgents DROP COLUMN legacyDivisionID;
DROP TABLE IF EXISTS chrEpicArcCompletions;
DROP TABLE IF EXISTS chrMissionCounters;
DROP TABLE IF EXISTS msnMissions;
ALTER TABLE agtOffers
  DROP INDEX agtOffers_IX_char,
  DROP INDEX agtOffers_IX_agent,
  DROP COLUMN contentID,
  DROP COLUMN nameID,
  DROP COLUMN objectiveType,
  DROP COLUMN objectiveTypeID,
  DROP COLUMN objectiveQty,
  DROP COLUMN objectiveCompleted,
  DROP COLUMN enemyFactionID,
  DROP COLUMN dungeonID,
  DROP COLUMN dungeonX,
  DROP COLUMN dungeonY,
  DROP COLUMN dungeonZ,
  DROP COLUMN briefingText,
  MODIFY courierAmount SMALLINT(6) NOT NULL DEFAULT 0;
