-- petitions (petitioner service):  queues, categories, petitions, messages and the GM log
-- +migrate Up
CREATE TABLE IF NOT EXISTS `petQueues` (
  `queueID` tinyint(3) unsigned NOT NULL,
  `queueName` varchar(50) NOT NULL,
  `escalatesTo` tinyint(3) unsigned NULL DEFAULT NULL,
  PRIMARY KEY (`queueID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

INSERT INTO `petQueues` (`queueID`, `queueName`, `escalatesTo`) VALUES
(0, 'General', 5),
(1, 'Stuck', 5),
(2, 'Game Mechanics', 5),
(3, 'Conduct & Exploits', 5),
(4, 'Technical', 5),
(5, 'Senior GM', NULL);

CREATE TABLE IF NOT EXISTS `petCategories` (
  `categoryID` int(10) unsigned NOT NULL,
  `parentID` int(10) unsigned NOT NULL DEFAULT 0,
  `displayName` varchar(100) NOT NULL,
  `description` text NOT NULL,
  `queueID` tinyint(3) unsigned NOT NULL DEFAULT 0,
  `languageID` varchar(8) NOT NULL DEFAULT 'en-us',
  PRIMARY KEY (`categoryID`),
  KEY `parentID` (`parentID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

INSERT INTO `petCategories` (`categoryID`, `parentID`, `displayName`, `description`, `queueID`) VALUES
(1, 0, 'Game Play', 'Problems with the game world, your ship, your character or other players.', 0),
(2, 0, 'Technical', 'Problems with the client, your connection or the server.', 4),
(3, 0, 'Account', 'Questions about your account and your characters.', 0),
(11, 1, 'Stuck', 'Use this if your ship or character cannot move, dock, undock or jump.', 1),
(12, 1, 'Exploits', 'Report a bug that gives an unfair advantage, or a player using one.  Do not use the exploit while you wait.', 3),
(13, 1, 'Harassment / Conduct', 'Report harassment, scamming outside the game rules, or offensive names and language.', 3),
(14, 1, 'Missions & Agents', 'Missions that cannot be completed, wrong rewards, or agents that do not respond.', 2),
(15, 1, 'Ships, Modules & Combat', 'Ships, modules, drones or combat behaving wrongly.', 2),
(16, 1, 'Lost Items / Reimbursement', 'Items, ships or ISK lost because of a server problem.  Give the time, place and what was lost.', 2),
(17, 1, 'Market, Contracts & Trade', 'Problems with market orders, contracts, escrow or direct trades.', 2),
(18, 1, 'Corporations, Alliances & Wars', 'Problems with corporation management, roles, alliances or wars.', 2),
(19, 1, 'Other Game Play', 'Anything about the game that does not fit another category.', 0),
(21, 2, 'Connection / Login', 'Problems connecting, logging in, or getting disconnected.', 4),
(22, 2, 'Client Crash / Graphics', 'The client crashes, freezes or shows graphics errors.', 4),
(23, 2, 'Server Error / Bug', 'An error message from the server, or a feature that does not work.', 4),
(31, 3, 'Account & Characters', 'Questions about your account, character transfers or character deletion.', 0),
(32, 3, 'Name or Character Issues', 'Requests about character names, portraits or biographies.', 0);

CREATE TABLE IF NOT EXISTS `petPetitions` (
  `petitionID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `petitionerID` int(10) unsigned NOT NULL,
  `userID` int(10) unsigned NOT NULL,
  `categoryID` int(10) unsigned NOT NULL,
  `queueID` tinyint(3) unsigned NOT NULL DEFAULT 0,
  `subject` varchar(200) NOT NULL,
  `petition` text NOT NULL,
  `chatLog` mediumtext NULL,
  `combatLog` mediumtext NULL,
  `createDate` bigint(20) NOT NULL,
  `touchDate` bigint(20) NOT NULL,
  `claimedBy` int(10) unsigned NULL DEFAULT NULL,
  `closed` tinyint(1) NOT NULL DEFAULT 0,
  `deleted` tinyint(1) NOT NULL DEFAULT 0,
  `updated` tinyint(1) NOT NULL DEFAULT 0,
  `closedDate` bigint(20) NULL DEFAULT NULL,
  `responseTimeRating` tinyint(3) NULL DEFAULT NULL,
  `helpfulnessRating` tinyint(3) NULL DEFAULT NULL,
  `attitudeRating` tinyint(3) NULL DEFAULT NULL,
  `ratingComment` text NULL,
  `ratingDateTime` bigint(20) NULL DEFAULT NULL,
  PRIMARY KEY (`petitionID`),
  KEY `petitionerID` (`petitionerID`),
  KEY `queueClosed` (`queueID`, `closed`),
  KEY `claimedBy` (`claimedBy`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `petMessages` (
  `messageID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `petitionID` int(10) unsigned NOT NULL,
  `senderID` int(10) unsigned NULL DEFAULT NULL,
  `text` text NOT NULL,
  `sentDate` bigint(20) NOT NULL,
  `comment` tinyint(1) NOT NULL DEFAULT 0,
  `isRead` tinyint(1) NOT NULL DEFAULT 0,
  PRIMARY KEY (`messageID`),
  KEY `petitionID` (`petitionID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `petLog` (
  `logID` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `petitionID` int(10) unsigned NOT NULL,
  `logDateTime` bigint(20) NOT NULL,
  `characterID` int(10) unsigned NULL DEFAULT NULL,
  `userName` varchar(43) NOT NULL DEFAULT '',
  `eventID` tinyint(3) unsigned NOT NULL,
  PRIMARY KEY (`logID`),
  KEY `petitionID` (`petitionID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- +migrate Down
DROP TABLE IF EXISTS `petLog`;
DROP TABLE IF EXISTS `petMessages`;
DROP TABLE IF EXISTS `petPetitions`;
DROP TABLE IF EXISTS `petCategories`;
DROP TABLE IF EXISTS `petQueues`;
