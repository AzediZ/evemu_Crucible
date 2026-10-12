-- F24 tutorials and the career funnel:
--  chrTutorialEvents keeps each character's latest tutorial event (started/completed/aborted/closed), restored at login
--  tutorialConnections is Crucible's tutorial sequence per race (from sql/old_data/tutorialsvc_connections.sql)
--  message IDs:  the client shows tutorial names, page names, texts and criteria by localization message ID.  our tutorial
--  content predates Crucible, so only entries whose English text equals a Crucible message get an ID (the rest stay NULL)
--  agtCareerAgents maps the career funnel agents to career types (1 industry, 2 business, 3 military, 4 exploration,
--  5 advanced military).  no period source lists them, so it starts empty
-- +migrate Up
CREATE TABLE IF NOT EXISTS chrTutorialEvents (
  characterID INT(10) UNSIGNED NOT NULL,
  tutorialID INT(10) UNSIGNED NOT NULL,
  pageID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  eventTypeID SMALLINT(5) UNSIGNED NOT NULL,
  eventDateTime BIGINT(20) NOT NULL,
  secondsSpent INT(10) UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (characterID, tutorialID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS tutorialConnections (
  tutorialID INT(10) UNSIGNED NOT NULL,
  raceID TINYINT(3) UNSIGNED NOT NULL,
  nextTutorialID INT(10) UNSIGNED NOT NULL,
  PRIMARY KEY (tutorialID, raceID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
INSERT IGNORE INTO tutorialConnections (tutorialID, raceID, nextTutorialID) VALUES
(215, 0, 217),
(136, 0, 249),
(249, 0, 110),
(110, 0, 137),
(137, 0, 204),
(204, 0, 117),
(117, 0, 250),
(250, 0, 234),
(234, 4, 163),
(234, 1, 202),
(234, 8, 226),
(234, 2, 227),
(163, 0, 224),
(202, 0, 224),
(226, 0, 224),
(227, 0, 224),
(224, 1, 228),
(224, 8, 228),
(224, 4, 211),
(224, 2, 229),
(228, 0, 210),
(211, 0, 210),
(229, 0, 210),
(210, 0, 112),
(112, 0, 222),
(222, 0, 251),
(251, 0, 221),
(221, 0, 212),
(212, 0, 230),
(230, 0, 231),
(231, 0, 232),
(217, 0, 136),
(232, 0, 252),
(252, 0, 233);

CREATE TABLE IF NOT EXISTS agtCareerAgents (
  agentID INT(10) UNSIGNED NOT NULL,
  careerType TINYINT(3) UNSIGNED NOT NULL,
  PRIMARY KEY (agentID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

ALTER TABLE tutorials ADD COLUMN tutorialNameID INT(10) UNSIGNED NULL;
ALTER TABLE tutorial_pages ADD COLUMN pageNameID INT(10) UNSIGNED NULL, ADD COLUMN textID INT(10) UNSIGNED NULL;
ALTER TABLE tutorial_criteria ADD COLUMN messageTextID INT(10) UNSIGNED NULL;
ALTER TABLE tutorial_categories ADD COLUMN categoryNameID INT(10) UNSIGNED NULL, ADD COLUMN descriptionID INT(10) UNSIGNED NULL;
UPDATE tutorials SET tutorialNameID = 60459 WHERE tutorialID = 7;
UPDATE tutorials SET tutorialNameID = 60889 WHERE tutorialID = 11;
UPDATE tutorials SET tutorialNameID = 54130 WHERE tutorialID = 12;
UPDATE tutorials SET tutorialNameID = 54166 WHERE tutorialID = 14;
UPDATE tutorials SET tutorialNameID = 67271 WHERE tutorialID = 16;
UPDATE tutorials SET tutorialNameID = 61907 WHERE tutorialID = 21;
UPDATE tutorials SET tutorialNameID = 61957 WHERE tutorialID = 32;
UPDATE tutorials SET tutorialNameID = 54118 WHERE tutorialID = 35;
UPDATE tutorials SET tutorialNameID = 60546 WHERE tutorialID = 44;
UPDATE tutorials SET tutorialNameID = 60612 WHERE tutorialID = 45;
UPDATE tutorials SET tutorialNameID = 54171 WHERE tutorialID = 46;
UPDATE tutorial_pages SET pageNameID = 67271 WHERE pageID = 1001609;
UPDATE tutorial_pages SET pageNameID = 234899 WHERE pageID = 1001928;
UPDATE tutorial_pages SET pageNameID = 60243 WHERE pageID = 1001935;
UPDATE tutorial_pages SET pageNameID = 234819 WHERE pageID = 1002100;
UPDATE tutorial_pages SET pageNameID = 237183 WHERE pageID = 1002231;
UPDATE tutorial_pages SET pageNameID = 234688 WHERE pageID = 1002385;
UPDATE tutorial_pages SET pageNameID = 232677 WHERE pageID = 1002386;
UPDATE tutorial_pages SET pageNameID = 234743 WHERE pageID = 1002387;
UPDATE tutorial_pages SET pageNameID = 234744 WHERE pageID = 1002388;
UPDATE tutorial_pages SET pageNameID = 234659 WHERE pageID = 1002391;
UPDATE tutorial_pages SET pageNameID = 234745 WHERE pageID = 1002392;
UPDATE tutorial_pages SET pageNameID = 234746 WHERE pageID = 1002393;
UPDATE tutorial_pages SET pageNameID = 234737 WHERE pageID = 1002394;
UPDATE tutorial_pages SET pageNameID = 234641 WHERE pageID = 1002395;
UPDATE tutorial_pages SET pageNameID = 264322 WHERE pageID = 1002569;
UPDATE tutorial_pages SET pageNameID = 234668 WHERE pageID = 1002574;
UPDATE tutorial_pages SET pageNameID = 234894 WHERE pageID = 1002575;
UPDATE tutorial_pages SET pageNameID = 234672 WHERE pageID = 1002606;
UPDATE tutorial_pages SET pageNameID = 234818 WHERE pageID = 1002659;
UPDATE tutorial_pages SET pageNameID = 234664 WHERE pageID = 1002660;
UPDATE tutorial_pages SET pageNameID = 234819 WHERE pageID = 1002661;
UPDATE tutorial_pages SET pageNameID = 234963 WHERE pageID = 1002662;
UPDATE tutorial_pages SET pageNameID = 234912 WHERE pageID = 1002663;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 7;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 8;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 9;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 10;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 11;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 12;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 13;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 14;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 15;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 16;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 17;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 18;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 19;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 20;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 21;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 22;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 23;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 24;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 52;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 53;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 54;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 55;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 56;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 57;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 58;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 59;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 60;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 61;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 62;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 63;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 64;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 65;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 66;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 67;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 68;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 69;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 70;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 71;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 72;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 73;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 74;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 75;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 76;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 77;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 78;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 79;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 80;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 81;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 82;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 83;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 84;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 85;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 86;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 87;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 88;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 89;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 90;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 91;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 92;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 93;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 94;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 95;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 96;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 97;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 98;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 99;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 100;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 114;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 115;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 116;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 117;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 118;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 119;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 120;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 121;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 122;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 123;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 124;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 125;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 126;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 127;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 128;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 132;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 140;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 141;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 142;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 151;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 152;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 153;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 158;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 159;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 160;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 164;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 169;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 173;
UPDATE tutorial_criteria SET messageTextID = 36 WHERE criteriaID = 193;
UPDATE tutorial_criteria SET messageTextID = 66859 WHERE criteriaID = 195;
UPDATE tutorial_criteria SET messageTextID = 66860 WHERE criteriaID = 196;
UPDATE tutorial_criteria SET messageTextID = 66860 WHERE criteriaID = 197;
UPDATE tutorial_criteria SET messageTextID = 66860 WHERE criteriaID = 198;
UPDATE tutorial_criteria SET messageTextID = 66860 WHERE criteriaID = 199;

-- +migrate Down
ALTER TABLE tutorial_categories DROP COLUMN descriptionID, DROP COLUMN categoryNameID;
ALTER TABLE tutorial_criteria DROP COLUMN messageTextID;
ALTER TABLE tutorial_pages DROP COLUMN textID, DROP COLUMN pageNameID;
ALTER TABLE tutorials DROP COLUMN tutorialNameID;
DROP TABLE IF EXISTS agtCareerAgents;
DROP TABLE IF EXISTS tutorialConnections;
DROP TABLE IF EXISTS chrTutorialEvents;
