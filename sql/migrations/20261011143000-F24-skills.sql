-- F24 skills: unallocated (free) skill points per character
-- +migrate Up
ALTER TABLE chrCharacters ADD COLUMN freeSkillPoints INT UNSIGNED NOT NULL DEFAULT 0 AFTER skillPoints;

-- +migrate Down
ALTER TABLE chrCharacters DROP COLUMN freeSkillPoints;
