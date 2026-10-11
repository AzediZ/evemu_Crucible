-- journal referenceIDs are signed:  single npc bounty entries store -typeID, which the client's eveFormat resolves to the npc's type name
-- +migrate Up
ALTER TABLE `jnlCharacters` MODIFY `referenceID` int(11) NOT NULL DEFAULT 0;
ALTER TABLE `jnlCorporations` MODIFY `referenceID` int(11) NOT NULL DEFAULT 0;

-- +migrate Down
-- negative referenceIDs (npc bounty type references) are clamped to 0 by MySQL when going back to unsigned
UPDATE `jnlCharacters` SET `referenceID` = 0 WHERE `referenceID` < 0;
UPDATE `jnlCorporations` SET `referenceID` = 0 WHERE `referenceID` < 0;
ALTER TABLE `jnlCharacters` MODIFY `referenceID` int(10) unsigned NOT NULL DEFAULT 0;
ALTER TABLE `jnlCorporations` MODIFY `referenceID` int(10) unsigned NOT NULL DEFAULT 0;
