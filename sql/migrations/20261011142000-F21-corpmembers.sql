-- F21 corporation members:  start of corporate stasis (roles removed) per member, last CEO vote per corporation,
--  and own general roles kept in both corpRole and rolesAtAll
-- +migrate Up
ALTER TABLE chrCharacters ADD COLUMN rolesClearedDateTime BIGINT(20) NOT NULL DEFAULT 0 AFTER blockRoles;
ALTER TABLE crpCorporation ADD COLUMN lastCEOVoteDateTime BIGINT(20) NOT NULL DEFAULT 0;
UPDATE chrCharacters SET rolesAtAll = rolesAtAll | corpRole, corpRole = rolesAtAll | corpRole WHERE corporationID >= 98000000;

-- +migrate Down
ALTER TABLE crpCorporation DROP COLUMN lastCEOVoteDateTime;
ALTER TABLE chrCharacters DROP COLUMN rolesClearedDateTime;
