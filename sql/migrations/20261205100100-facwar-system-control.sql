-- factional warfare's system control (FacWarMgr.h):  each war zone system's victory points (what its
--  occupier's enemy has taken from it in complexes, less what the occupier took back), the faction that destroyed its
--  control bunker and when (it takes the system at the next downtime), and the bunker's shield, armor and structure left
--  (1 = whole)
-- +migrate Up
ALTER TABLE facWarSystems
    ADD COLUMN victoryPoints INT(10) NOT NULL DEFAULT 0,
    ADD COLUMN capturedBy INT(10) NOT NULL DEFAULT 0,
    ADD COLUMN capturedAt BIGINT(20) NOT NULL DEFAULT 0,
    ADD COLUMN bunkerShield DOUBLE NOT NULL DEFAULT 1,
    ADD COLUMN bunkerArmor DOUBLE NOT NULL DEFAULT 1,
    ADD COLUMN bunkerHull DOUBLE NOT NULL DEFAULT 1;

-- +migrate Down
ALTER TABLE facWarSystems
    DROP COLUMN victoryPoints,
    DROP COLUMN capturedBy,
    DROP COLUMN capturedAt,
    DROP COLUMN bunkerShield,
    DROP COLUMN bunkerArmor,
    DROP COLUMN bunkerHull;
