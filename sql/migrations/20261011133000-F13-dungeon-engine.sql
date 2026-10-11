-- dungeon engine: per-dungeon selection data, gate arrival points, npc spawn groups, triggers and their events,
--  acceleration gate rules, loot groups, escalation rules and the escalations granted to characters.
--  read by DungeonDataMgr (system/cosmicMgrs/DungeonMgr.cpp); chrEscalations is written by DungeonMgr
-- +migrate Up
ALTER TABLE `dunDungeons`
  ADD COLUMN `dungeonNameID` int(11) NULL DEFAULT NULL,         -- client localization messageID of the name (journal)
  ADD COLUMN `difficulty` tinyint(3) unsigned NOT NULL DEFAULT 0, -- site tier 1-10
  ADD COLUMN `minSecurity` double NOT NULL DEFAULT -1.0,         -- true security window for random selection
  ADD COLUMN `maxSecurity` double NOT NULL DEFAULT 1.0,
  ADD COLUMN `spawnWeight` int(11) NOT NULL DEFAULT 1,           -- 0 = never picked at random (missions, escalation targets)
  ADD COLUMN `lifetimeHours` int(11) NOT NULL DEFAULT 0;         -- 0 = config default of its kind
ALTER TABLE `dunRooms`
  ADD COLUMN `arrivalX` double NOT NULL DEFAULT 0,               -- acceleration gate arrival point, relative to the room origin
  ADD COLUMN `arrivalY` double NOT NULL DEFAULT 0,
  ADD COLUMN `arrivalZ` double NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS `dunSpawnGroups` (
  `spawnGroupID` int(11) NOT NULL AUTO_INCREMENT,
  `roomID` int(11) NOT NULL,
  `waveNumber` tinyint(3) unsigned NOT NULL DEFAULT 0,           -- informational
  `spawnMode` tinyint(3) unsigned NOT NULL DEFAULT 0,            -- 0 room created, 1 first player enters the room, 2 only by a trigger event
  `npcClassType` tinyint(3) unsigned NOT NULL DEFAULT 0,         -- npcSpawnClass.type (0 = dunSpawnGroupTypes rows)
  `npcClassSub` tinyint(3) unsigned NOT NULL DEFAULT 0,          -- npcSpawnClass.sub
  `factionID` int(11) NOT NULL DEFAULT 0,                        -- 0 = the instance's faction
  `x` double NOT NULL DEFAULT 0,
  `y` double NOT NULL DEFAULT 0,
  `z` double NOT NULL DEFAULT 0,
  `spread` double NOT NULL DEFAULT 5000,                         -- m, random scatter around x,y,z
  `warpIn` tinyint(1) NOT NULL DEFAULT 0,                        -- warp in from outside the grid instead of appearing
  `isOverseer` tinyint(1) NOT NULL DEFAULT 0,
  `lootGroupID` int(11) NOT NULL DEFAULT 0,                      -- dunLoot group dropped into each member's wreck
  PRIMARY KEY (`spawnGroupID`),
  KEY `roomID` (`roomID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `dunSpawnGroupTypes` (
  `spawnGroupID` int(11) NOT NULL,
  `typeID` int(11) NOT NULL,
  `quantity` int(11) NOT NULL DEFAULT 1,
  KEY `spawnGroupID` (`spawnGroupID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `dunTriggers` (
  `triggerID` int(11) NOT NULL AUTO_INCREMENT,
  `roomID` int(11) NOT NULL,
  `triggerType` int(11) NOT NULL,                                -- client dunTrigger* value
  `spawnGroupID` int(11) NOT NULL DEFAULT 0,                     -- source spawn group, or
  `objectID` int(11) NOT NULL DEFAULT 0,                         --  source dunRoomObjects.objectID
  `value` double NOT NULL DEFAULT 0,                             -- condition fraction, proximity m, counter value, ...
  `counterID` int(11) NOT NULL DEFAULT 0,
  `repeatable` tinyint(1) NOT NULL DEFAULT 0,
  PRIMARY KEY (`triggerID`),
  KEY `roomID` (`roomID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `dunTriggerEvents` (
  `eventID` int(11) NOT NULL AUTO_INCREMENT,
  `triggerID` int(11) NOT NULL,
  `sequence` int(11) NOT NULL DEFAULT 0,
  `eventType` int(11) NOT NULL,                                  -- client dunTriggerEvent* value (+ server-only Escalate = 100)
  `delaySeconds` int(11) NOT NULL DEFAULT 0,
  `spawnGroupID` int(11) NOT NULL DEFAULT 0,
  `objectID` int(11) NOT NULL DEFAULT 0,
  `messageType` tinyint(3) unsigned NOT NULL DEFAULT 0,          -- client dunEventMessage* value
  `messageID` int(11) NOT NULL DEFAULT 0,                        -- client localization messageID
  `messageText` varchar(400) NOT NULL DEFAULT '',                -- plain text when there is no messageID
  `typeID` int(11) NOT NULL DEFAULT 0,
  `quantity` int(11) NOT NULL DEFAULT 0,
  `value` double NOT NULL DEFAULT 0,
  `counterID` int(11) NOT NULL DEFAULT 0,
  PRIMARY KEY (`eventID`),
  KEY `triggerID` (`triggerID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `dunGates` (
  `objectID` int(11) NOT NULL,                                   -- dunRoomObjects.objectID of an acceleration gate
  `destRoomID` int(11) NOT NULL DEFAULT 0,                       -- 0 = next room
  `locked` tinyint(1) NOT NULL DEFAULT 0,                        -- closed until an ActivateGate event
  `requireClear` tinyint(1) NOT NULL DEFAULT 0,                  -- closed while hostile npcs are in the room
  `keyTypeID` int(11) NOT NULL DEFAULT 0,
  `keyQuantity` int(11) NOT NULL DEFAULT 1,
  `keyConsumed` tinyint(1) NOT NULL DEFAULT 1,
  `skillTypeID` int(11) NOT NULL DEFAULT 0,
  `skillLevel` tinyint(3) unsigned NOT NULL DEFAULT 0,
  `allowedGroups` varchar(400) NOT NULL DEFAULT '',              -- comma separated invGroups ids; empty = any (except capitals)
  `deniedGroups` varchar(400) NOT NULL DEFAULT '',
  `musicUrl` varchar(200) NOT NULL DEFAULT '',
  PRIMARY KEY (`objectID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `dunLoot` (
  `lootGroupID` int(11) NOT NULL,
  `typeID` int(11) NOT NULL,
  `minQty` int(11) NOT NULL DEFAULT 1,
  `maxQty` int(11) NOT NULL DEFAULT 1,
  `chance` double NOT NULL DEFAULT 1.0,
  KEY `lootGroupID` (`lootGroupID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `dunEscalations` (
  `escalationID` int(11) NOT NULL AUTO_INCREMENT,
  `srcDungeonID` int(11) NOT NULL DEFAULT 0,                     -- a specific dungeon, or
  `srcArchetypeID` int(11) NOT NULL DEFAULT 0,                   --  any dungeon of this archetype
  `srcMinDifficulty` tinyint(3) unsigned NOT NULL DEFAULT 0,     --  with at least this difficulty
  `destDungeonID` int(11) NOT NULL DEFAULT 0,                    -- a specific dungeon, or
  `destArchetypeID` int(11) NOT NULL DEFAULT 0,                  --  a dungeon of this archetype
  `destDifficultyDelta` tinyint(4) NOT NULL DEFAULT 0,           --  with at least the source difficulty + this
  `chance` double NOT NULL DEFAULT 0.05,
  `minJumps` tinyint(3) unsigned NOT NULL DEFAULT 0,
  `maxJumps` tinyint(3) unsigned NOT NULL DEFAULT 5,
  `lowerSecOnly` tinyint(1) NOT NULL DEFAULT 1,                  -- destination true security <= source
  `expiryHours` int(11) NOT NULL DEFAULT 0,                      -- 0 = config default
  `journalMessageID` int(11) NOT NULL DEFAULT 63235,             -- client localization messageID of the journal text
  PRIMARY KEY (`escalationID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `chrEscalations` (
  `instanceID` int(11) NOT NULL AUTO_INCREMENT,
  `characterID` int(11) NOT NULL,
  `escalationID` int(11) NOT NULL,
  `srcDungeonID` int(11) NOT NULL,
  `destDungeonID` int(11) NOT NULL,
  `factionID` int(11) NOT NULL,
  `solarSystemID` int(11) NOT NULL,
  `x` double NOT NULL,
  `y` double NOT NULL,
  `z` double NOT NULL,
  `creationTime` bigint(20) NOT NULL,
  `expiryTime` bigint(20) NOT NULL,
  `journalMessageID` int(11) NOT NULL,
  `state` tinyint(3) unsigned NOT NULL DEFAULT 0,                -- 0 granted, 1 entered, 2 completed, 3 removed by the player
  PRIMARY KEY (`instanceID`),
  KEY `characterID` (`characterID`),
  KEY `solarSystemID` (`solarSystemID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

INSERT INTO `dunArchetypes` (archetypeID, archetypeName) VALUES (11, "Incursion");

-- +migrate Down
DELETE FROM `dunArchetypes` WHERE archetypeID = 11;
DROP TABLE IF EXISTS `chrEscalations`;
DROP TABLE IF EXISTS `dunEscalations`;
DROP TABLE IF EXISTS `dunLoot`;
DROP TABLE IF EXISTS `dunGates`;
DROP TABLE IF EXISTS `dunTriggerEvents`;
DROP TABLE IF EXISTS `dunTriggers`;
DROP TABLE IF EXISTS `dunSpawnGroupTypes`;
DROP TABLE IF EXISTS `dunSpawnGroups`;
ALTER TABLE `dunRooms`
  DROP COLUMN `arrivalX`,
  DROP COLUMN `arrivalY`,
  DROP COLUMN `arrivalZ`;
ALTER TABLE `dunDungeons`
  DROP COLUMN `dungeonNameID`,
  DROP COLUMN `difficulty`,
  DROP COLUMN `minSecurity`,
  DROP COLUMN `maxSecurity`,
  DROP COLUMN `spawnWeight`,
  DROP COLUMN `lifetimeHours`;
