-- An index on character names.  Character creation checks a new name isn't taken by looking it up, and without the
--  index each check read the whole table, and got slower with every character made
-- +migrate Up
ALTER TABLE `chrCharacters` ADD INDEX IF NOT EXISTS `characterName` (`characterName`);

-- +migrate Down
ALTER TABLE `chrCharacters` DROP INDEX IF EXISTS `characterName`;
