-- Indexes for reading one character's rows, which every login, character creation and skill change does.  Without them
--  each of these read the whole table, and got slower with every character made
-- +migrate Up
ALTER TABLE `chrSkillHistory` ADD INDEX IF NOT EXISTS `characterID` (`characterID`);
ALTER TABLE `chrCharacterAttributes` ADD INDEX IF NOT EXISTS `charID` (`charID`);
ALTER TABLE `chrSkillQueue` ADD INDEX IF NOT EXISTS `characterID` (`characterID`);
ALTER TABLE `chrPausedSkillQueue` ADD INDEX IF NOT EXISTS `characterID` (`characterID`);

-- +migrate Down
ALTER TABLE `chrPausedSkillQueue` DROP INDEX IF EXISTS `characterID`;
ALTER TABLE `chrSkillQueue` DROP INDEX IF EXISTS `characterID`;
ALTER TABLE `chrCharacterAttributes` DROP INDEX IF EXISTS `charID`;
ALTER TABLE `chrSkillHistory` DROP INDEX IF EXISTS `characterID`;
