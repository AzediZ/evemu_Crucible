-- F24 calendar:  personal event invitations (sysCalendarInvitees held a broken comma list and is unused now) and one
-- response per character and event (duplicates dropped) so responses can be updated
-- +migrate Up
CREATE TABLE IF NOT EXISTS sysCalendarEventInvitees (
  eventID INT(10) UNSIGNED NOT NULL,
  characterID INT(10) UNSIGNED NOT NULL,
  PRIMARY KEY (eventID, characterID),
  KEY characterID (characterID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE sysCalendarResponses_new LIKE sysCalendarResponses;
ALTER TABLE sysCalendarResponses_new ADD PRIMARY KEY (eventID, charID);
INSERT IGNORE INTO sysCalendarResponses_new SELECT * FROM sysCalendarResponses;
DROP TABLE sysCalendarResponses;
RENAME TABLE sysCalendarResponses_new TO sysCalendarResponses;
ALTER TABLE sysCalendarResponses ADD KEY charID (charID);

-- +migrate Down
ALTER TABLE sysCalendarResponses DROP KEY charID;
ALTER TABLE sysCalendarResponses DROP PRIMARY KEY;
DROP TABLE IF EXISTS sysCalendarEventInvitees;
