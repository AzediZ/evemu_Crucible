-- F24 character selection and recustomization:  keys on the avatar/portrait tables (recustomization deletes and
-- re-inserts a character's rows) and the account's paid game time (PLEX), shown as daysLeft at character selection
-- +migrate Up
ALTER TABLE avatars ADD KEY charID (charID);
ALTER TABLE avatar_colors ADD KEY charID (charID);
ALTER TABLE avatar_modifiers ADD KEY charID (charID);
ALTER TABLE avatar_sculpts ADD KEY charID (charID);
ALTER TABLE chrPortraitData ADD KEY charID (charID);
ALTER TABLE account ADD COLUMN paidUntil BIGINT(20) NOT NULL DEFAULT 0 AFTER lastLogin;

-- +migrate Down
ALTER TABLE account DROP COLUMN paidUntil;
ALTER TABLE chrPortraitData DROP KEY charID;
ALTER TABLE avatar_sculpts DROP KEY charID;
ALTER TABLE avatar_modifiers DROP KEY charID;
ALTER TABLE avatar_colors DROP KEY charID;
ALTER TABLE avatars DROP KEY charID;
