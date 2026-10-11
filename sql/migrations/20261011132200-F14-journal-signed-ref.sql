-- journal referenceIDs are signed:  single npc bounty entries store -typeID, which the client's eveFormat resolves to the npc's type name
-- +migrate Up
-- older single bounty entries hold -typeID wrapped to unsigned (> 2^31), which int(11) can't take:  unwrap them on a bigint first
ALTER TABLE `jnlCharacters` MODIFY `referenceID` bigint(20) NOT NULL DEFAULT 0;
UPDATE `jnlCharacters` SET `referenceID` = `referenceID` - 4294967296 WHERE `referenceID` > 2147483647;
ALTER TABLE `jnlCharacters` MODIFY `referenceID` int(11) NOT NULL DEFAULT 0;
ALTER TABLE `jnlCorporations` MODIFY `referenceID` bigint(20) NOT NULL DEFAULT 0;
UPDATE `jnlCorporations` SET `referenceID` = `referenceID` - 4294967296 WHERE `referenceID` > 2147483647;
ALTER TABLE `jnlCorporations` MODIFY `referenceID` int(11) NOT NULL DEFAULT 0;

-- +migrate Down
-- negative referenceIDs (npc bounty type references) are clamped to 0 by MySQL when going back to unsigned
UPDATE `jnlCharacters` SET `referenceID` = 0 WHERE `referenceID` < 0;
UPDATE `jnlCorporations` SET `referenceID` = 0 WHERE `referenceID` < 0;
ALTER TABLE `jnlCharacters` MODIFY `referenceID` int(10) unsigned NOT NULL DEFAULT 0;
ALTER TABLE `jnlCorporations` MODIFY `referenceID` int(10) unsigned NOT NULL DEFAULT 0;
