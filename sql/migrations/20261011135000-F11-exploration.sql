-- F11 exploration: hacking/archaeology/salvage site containers.  family and faction per locked container type, and the
--  loot table per family.  chances and quantities are design values (no period drop data exists).
-- +migrate Up
CREATE TABLE IF NOT EXISTS expContainerFamily (
  typeID INT(10) UNSIGNED NOT NULL,
  familyID TINYINT(3) UNSIGNED NOT NULL,              -- Exploration::Family
  factionPrefix VARCHAR(32) NOT NULL DEFAULT '',      -- first word(s) of this container's faction item names ('Guristas', 'Blood Raider')
  PRIMARY KEY (typeID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS expContainerLoot (
  lootID INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  familyID TINYINT(3) UNSIGNED NOT NULL,
  itemGroupID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  itemTypeID INT(10) UNSIGNED NOT NULL DEFAULT 0,     -- 0 = a random published type of itemGroupID
  namePattern VARCHAR(48) NOT NULL DEFAULT '',        -- '' = any, '*' = the container's faction prefix, 'x' = starts with x, '%x' = contains x
  chance FLOAT NOT NULL DEFAULT 0,                    -- per container, 0..1
  minQty INT(10) UNSIGNED NOT NULL DEFAULT 1,         -- stack size (blueprint rows: runs)
  maxQty INT(10) UNSIGNED NOT NULL DEFAULT 1,
  isCopy TINYINT(3) UNSIGNED NOT NULL DEFAULT 0,      -- 1 = blueprint copy (tech 1 only), quantity is its runs
  PRIMARY KEY (lootID),
  KEY familyID (familyID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- families from the static data:  locked spawn containers (accessDifficulty 901) by requiredSkill1 (182) and name
INSERT IGNORE INTO expContainerFamily (typeID, familyID, factionPrefix)
SELECT t.typeID,
  CASE
    WHEN t.typeName LIKE 'Sleeper%' AND IFNULL(r.valueInt, r.valueFloat) = 21718 THEN 4
    WHEN t.typeName LIKE 'Sleeper%' THEN 5
    WHEN (t.typeName LIKE 'Talocan%' OR t.typeName LIKE 'Yan Jung%' OR t.typeName LIKE 'Takmahl%') AND t.typeName LIKE '%Data%' THEN 6
    WHEN (t.typeName LIKE 'Talocan%' OR t.typeName LIKE 'Yan Jung%' OR t.typeName LIKE 'Takmahl%') THEN 7
    WHEN IFNULL(r.valueInt, r.valueFloat) = 21718 THEN 1
    WHEN IFNULL(r.valueInt, r.valueFloat) = 13278 THEN 2
    WHEN IFNULL(r.valueInt, r.valueFloat) = 25863 THEN 3
    ELSE 1 END,
  CASE
    WHEN t.typeName LIKE 'Blood%' THEN 'Blood Raider'
    WHEN t.typeName LIKE 'Yan Jung%' THEN 'Yan Jung'
    WHEN t.typeName LIKE '%Angel%' THEN 'Angel'
    WHEN t.typeName LIKE '%Guristas%' THEN 'Guristas'
    WHEN t.typeName LIKE '%Serpentis%' THEN 'Serpentis'
    WHEN t.typeName LIKE '%Sansha%' THEN 'Sansha'
    WHEN t.typeName LIKE 'Sleeper%' THEN 'Sleeper'
    WHEN t.typeName LIKE 'Talocan%' THEN 'Talocan'
    WHEN t.typeName LIKE 'Takmahl%' THEN 'Takmahl'
    ELSE '' END
FROM invTypes t
JOIN dgmTypeAttributes a ON a.typeID = t.typeID AND a.attributeID = 901
LEFT JOIN dgmTypeAttributes r ON r.typeID = t.typeID AND r.attributeID = 182
WHERE t.groupID = 306;

-- loot rows:  (family, group, type, name pattern, chance, min, max, copy)
INSERT INTO expContainerLoot (familyID, itemGroupID, itemTypeID, namePattern, chance, minQty, maxQty, isCopy) VALUES
 -- hacking (radar):  datacores, racial decryptors, faction artifacts, racial data interface copies
 (1, 333, 0, '',  0.80, 1, 3, 0),
 (1, 728, 0, '',  0.10, 1, 1, 0),
 (1, 729, 0, '',  0.10, 1, 1, 0),
 (1, 730, 0, '',  0.10, 1, 1, 0),
 (1, 731, 0, '',  0.10, 1, 1, 0),
 (1, 528, 0, '*', 0.30, 1, 2, 0),
 (1, 356, 0, '%Data Interface Blueprint', 0.05, 1, 5, 1),
 -- archaeology (magnetometric):  salvage, faction artifacts, tech 1 rig copies
 (2, 754, 0, '',  0.80, 1, 3, 0),
 (2, 528, 0, '*', 0.40, 1, 2, 0),
 (2, 787, 0, '',  0.25, 1, 5, 1),
 -- salvage spawn containers (hulks and wrecks in sites)
 (3, 754, 0, '',  0.90, 2, 4, 0),
 -- sleeper data:  datacores, sleeper and hybrid decryptors
 (4, 333, 0, '',  0.70, 2, 4, 0),
 (4, 732, 0, '',  0.35, 1, 2, 0),
 (4, 979, 0, '',  0.35, 1, 2, 0),
 -- sleeper debris:  ancient salvage and relics
 (5, 966, 0, '',  0.85, 1, 4, 0),
 (5, 971, 0, '',  0.10, 1, 1, 0),
 (5, 990, 0, '',  0.10, 1, 1, 0),
 (5, 991, 0, '',  0.10, 1, 1, 0),
 (5, 992, 0, '',  0.10, 1, 1, 0),
 (5, 993, 0, '',  0.10, 1, 1, 0),
 (5, 997, 0, '',  0.10, 1, 1, 0),
 -- ancient empire data:  that empire's decryptors (the prefix leaves the other empires' groups empty), datacores
 (6, 735, 0, '*', 0.50, 1, 2, 0),
 (6, 734, 0, '*', 0.50, 1, 2, 0),
 (6, 733, 0, '*', 0.50, 1, 2, 0),
 (6, 333, 0, '',  0.50, 1, 2, 0),
 -- ancient empire debris:  that empire's artifacts, salvage
 (7, 528, 0, '*', 0.70, 1, 2, 0),
 (7, 754, 0, '',  0.40, 1, 2, 0);

-- +migrate Down
DROP TABLE IF EXISTS expContainerLoot;
DROP TABLE IF EXISTS expContainerFamily;
