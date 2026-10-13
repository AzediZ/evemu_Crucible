-- npc loot and wrecks (src/eve-common/EVE_Loot.h, StaticDataMgr::GetLoot, SystemEntity::DropLoot).
--  how much an npc drops is its type's data:  entityEquipmentMin/Max (456/457) loot rolls, entityEquipmentGroupMax (465).
--  each roll picks one of the npc's loot groups by groupDropChance (its share of the rolls), then one of the group's items,
--  which drops with its itemDropChance.  the npc's pool is lootNpcType by its type, else lootGroup by its group.
--
--  this migration:
--   1. lootNpcType:  pools for single npc types (officers, hauler tiers)
--   2. pools for npc groups that had none (deadspace and mission rats, sleepers), from the belt rats' pools
--   3. new loot item groups:  sleeper components (blue loot), hauler minerals, each officer's modules
--   4. wrecks:  every belt, deadspace, mission, sleeper, rogue drone, overseer and incursion npc gets the wreck of its faction
--      and size (missing types fell back to the generic Frigate Wreck), and wrong ones are put right
--  sources:
--   - hauler minerals:  EVE University, "Classifications of Space - NPC NullSec" (last modified 17 November 2010,
--     https://web.archive.org/web/20111210103201/http://wiki.eveuniversity.org/NPC_nullsec).
--     "The lowest hauler spawns start at 250K ISK bounties and drop tritanium or pyerite (usually about 1 million units).
--     500K bounty rat haulers contain isogen, 750K bounty rate haulers contain noxium and isogen, and 1m ISK bounty rat
--     haulers contain zydrine."  only the 250K quantity is given;  the others are the same mineral value per bounty ISK
--     (1M tritanium x 2 ISK basePrice = 8 x 250K), split over the hauler's 1-2 rolls (each roll V/2 .. V)
--   - sleepers:  EVE University, "Sleeper" (last modified 3 January 2011,
--     https://web.archive.org/web/20110502150804/http://wiki.eveuniversity.org/Sleeper).  "Neural Network Analyzers ... drop in all
--     Sleeper ship classes.  Sleeper Data Libraries ... drop in all Sleeper ship classes.  Ancient Coordinates ... can drop
--     from cruisers and battleships.  Sleeper Drone AI Nexuses ... drop mostly from battleships."  the shares of the rolls
--     are design values (no era numbers)
--   - officers:  EVE University, "NPC NullSec" (2010, as above):  "Most importantly, they drop 'officer modules'".  their modules
--     drop at the loot tables' faction item chance (0.15) and take half the officer's rolls (design values);  the other half
--     goes to the pool of their faction's commander battleship (faction modules and ammo)
--   - mission rats:  the belt pool of the pirate faction that flies their race's weapons and tank (Amarr/Khanid: Blood
--     Raiders, Caldari/Mordu: Guristas, Gallente: Serpentis, Minmatar/Thukker: Angel Cartel).  a design value
-- +migrate Up

-- 1. pools for single npc types.  an npc type with rows here doesn't use its group's lootGroup rows
CREATE TABLE IF NOT EXISTS `lootNpcType` (
  `npcTypeID` int(11) NOT NULL,
  `npcTypeName` text DEFAULT NULL,
  `itemGroupID` int(11) NOT NULL,
  `groupDropChance` float(8,4) NOT NULL COMMENT 'the group''s share of the npc''s loot rolls',
  PRIMARY KEY (`npcTypeID`, `itemGroupID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

-- 2. deadspace pirates and mission rats use the belt pool of their faction (or race's pirate faction) and size
DROP TEMPORARY TABLE IF EXISTS `_lootPoolCopy`;
CREATE TEMPORARY TABLE `_lootPoolCopy` (`dst` int(11) NOT NULL PRIMARY KEY, `src` int(11) NOT NULL);
INSERT INTO `_lootPoolCopy` (`dst`, `src`) VALUES
  -- deadspace:  Angel Cartel, Blood Raiders, Guristas, Sansha's Nation, Serpentis (BC, BS, cruiser, destroyer, frigate)
  (593, 576), (594, 552), (595, 551), (596, 575), (597, 550),
  (602, 578), (603, 556), (604, 555), (605, 577), (606, 557),
  (611, 580), (612, 560), (613, 561), (614, 579), (615, 562),
  (620, 582), (621, 565), (622, 566), (623, 581), (624, 567),
  (629, 584), (630, 570), (631, 571), (632, 583), (633, 572),
  -- mission Amarr Empire (frigate, BC, BS, cruiser, destroyer) and Khanid:  Blood Raiders
  (665, 557), (666, 578), (667, 556), (668, 555), (669, 577),
  (687, 557), (689, 555), (691, 556),
  -- mission Caldari State (frigate, BC, cruiser, BS, destroyer) and Mordu:  Guristas
  (671, 562), (672, 580), (673, 561), (674, 560), (676, 579),
  (699, 562), (701, 561), (703, 560),
  -- mission Gallente Federation (frigate, cruiser, destroyer, BS, BC):  Serpentis
  (677, 572), (678, 571), (679, 583), (680, 570), (681, 584),
  -- mission Minmatar Republic (frigate, destroyer, BC, cruiser, BS) and Thukker:  Angel Cartel
  (683, 550), (684, 575), (685, 576), (705, 551), (706, 552),
  (824, 551), (826, 550);
INSERT INTO `lootGroup` (`npcGroupID`, `npcGroupName`, `groupDropChance`, `itemGroupID`, `itemGroupName`)
  SELECT c.`dst`, g.`groupName`, l.`groupDropChance`, l.`itemGroupID`, l.`itemGroupName`
    FROM `_lootPoolCopy` AS c
    JOIN `lootGroup` AS l ON l.`npcGroupID` = c.`src`
    JOIN `invGroups` AS g ON g.`groupID` = c.`dst`
   WHERE NOT EXISTS (SELECT 1 FROM `lootGroup` AS x WHERE x.`npcGroupID` = c.`dst` AND x.`itemGroupID` = l.`itemGroupID`);
DROP TEMPORARY TABLE IF EXISTS `_lootPoolCopy`;

-- 3a. sleeper components (blue loot), one unit a roll.  135-138
INSERT INTO `lootItemGroup` (`itemGroupID`, `itemGroupName`, `itemID`, `itemName`, `itemMetaLevel`, `itemDropChance`, `minAmount`, `maxAmount`)
  SELECT v.`grp`, v.`grpName`, t.`typeID`, t.`typeName`, 0, 1.0, 1, 1
    FROM (SELECT 135 AS `grp`, 'Sleeper Neural Network Analyzer' AS `grpName`, 30744 AS `typeID`
          UNION ALL SELECT 136, 'Sleeper Data Library', 30745
          UNION ALL SELECT 137, 'Sleeper Ancient Coordinates Database', 30746
          UNION ALL SELECT 138, 'Sleeper Drone AI Nexus', 30747) AS v
    JOIN `invTypes` AS t ON t.`typeID` = v.`typeID`
   WHERE NOT EXISTS (SELECT 1 FROM `lootItemGroup` AS x WHERE x.`itemGroupID` = v.`grp` AND x.`itemID` = v.`typeID`);

-- 3b. hauler minerals.  139-144
INSERT INTO `lootItemGroup` (`itemGroupID`, `itemGroupName`, `itemID`, `itemName`, `itemMetaLevel`, `itemDropChance`, `minAmount`, `maxAmount`)
  SELECT v.`grp`, v.`grpName`, t.`typeID`, t.`typeName`, 0, 1.0, v.`minQty`, v.`maxQty`
    FROM (SELECT 139 AS `grp`, 'Hauler Tritanium (250K)' AS `grpName`, 34 AS `typeID`, 500000 AS `minQty`, 1000000 AS `maxQty`
          UNION ALL SELECT 140, 'Hauler Pyerite (250K)', 35, 500000, 1000000
          UNION ALL SELECT 141, 'Hauler Isogen (500K)', 37, 15625, 31250
          UNION ALL SELECT 142, 'Hauler Nocxium (750K)', 38, 2930, 5859
          UNION ALL SELECT 143, 'Hauler Isogen (750K)', 37, 11719, 23437
          UNION ALL SELECT 144, 'Hauler Zydrine (1M)', 39, 1953, 3906) AS v
    JOIN `invTypes` AS t ON t.`typeID` = v.`typeID`
   WHERE NOT EXISTS (SELECT 1 FROM `lootItemGroup` AS x WHERE x.`itemGroupID` = v.`grp` AND x.`itemID` = v.`typeID`);

-- 3c. each officer's modules (the Officer meta group, named after him:  Tobias Kruzhor - Tobias' Modified ...).  the item
--  group is the officer's typeID
INSERT INTO `lootItemGroup` (`itemGroupID`, `itemGroupName`, `itemID`, `itemName`, `itemMetaLevel`, `itemDropChance`, `minAmount`, `maxAmount`)
  SELECT o.`typeID`, CONCAT(o.`typeName`, ' Modules'), m.`typeID`, m.`typeName`,
         COALESCE((SELECT COALESCE(a.`valueInt`, a.`valueFloat`) FROM `dgmTypeAttributes` AS a WHERE a.`typeID` = m.`typeID` AND a.`attributeID` = 633), 0),
         0.15, 1, 1
    FROM `invTypes` AS o
    JOIN `invTypes` AS m ON m.`typeName` LIKE CONCAT(SUBSTRING_INDEX(o.`typeName`, ' ', 1), '''%') AND m.`published` = 1
    JOIN `invMetaTypes` AS mt ON mt.`typeID` = m.`typeID` AND mt.`metaGroupID` = 5
   WHERE o.`groupID` IN (553, 559, 564, 569, 574)
     AND NOT EXISTS (SELECT 1 FROM `lootItemGroup` AS x WHERE x.`itemGroupID` = o.`typeID` AND x.`itemID` = m.`typeID`);

-- 1a. officers:  their modules take half the rolls, their faction's commander battleship pool the other half (its groups
--  with no published item never come up, so they don't count)
DROP TEMPORARY TABLE IF EXISTS `_lootOfficer`;
CREATE TEMPORARY TABLE `_lootOfficer` (`officerGroupID` int(11) NOT NULL PRIMARY KEY, `commanderGroupID` int(11) NOT NULL);
INSERT INTO `_lootOfficer` VALUES (553, 848), (559, 849), (564, 850), (569, 851), (574, 852);
INSERT IGNORE INTO `lootNpcType` (`npcTypeID`, `npcTypeName`, `itemGroupID`, `groupDropChance`)
  SELECT t.`typeID`, t.`typeName`, l.`itemGroupID`, l.`groupDropChance`
    FROM `_lootOfficer` AS f
    JOIN `invTypes` AS t ON t.`groupID` = f.`officerGroupID`
    JOIN `lootGroup` AS l ON l.`npcGroupID` = f.`commanderGroupID`;
INSERT IGNORE INTO `lootNpcType` (`npcTypeID`, `npcTypeName`, `itemGroupID`, `groupDropChance`)
  SELECT t.`typeID`, t.`typeName`, t.`typeID`,
         (SELECT SUM(l.`groupDropChance`) FROM `lootGroup` AS l WHERE l.`npcGroupID` = f.`commanderGroupID`
             AND EXISTS (SELECT 1 FROM `lootItemGroup` AS i JOIN `invTypes` AS it ON it.`typeID` = i.`itemID`
                          WHERE i.`itemGroupID` = l.`itemGroupID` AND it.`published` = 1))
    FROM `_lootOfficer` AS f
    JOIN `invTypes` AS t ON t.`groupID` = f.`officerGroupID`
   WHERE EXISTS (SELECT 1 FROM `lootItemGroup` AS x WHERE x.`itemGroupID` = t.`typeID`);
DROP TEMPORARY TABLE IF EXISTS `_lootOfficer`;

-- 1b. belt haulers by bounty (entityKillBounty, 481).  the smaller haulers (8.5K-85K) have no era source and stay empty
INSERT IGNORE INTO `lootNpcType` (`npcTypeID`, `npcTypeName`, `itemGroupID`, `groupDropChance`)
  SELECT t.`typeID`, t.`typeName`, v.`grp`, v.`share`
    FROM `invTypes` AS t
    JOIN `dgmTypeAttributes` AS b ON b.`typeID` = t.`typeID` AND b.`attributeID` = 481
    JOIN (SELECT 250000 AS `bounty`, 139 AS `grp`, 0.5 AS `share`
          UNION ALL SELECT 250000, 140, 0.5
          UNION ALL SELECT 500000, 141, 1.0
          UNION ALL SELECT 750000, 142, 0.5
          UNION ALL SELECT 750000, 143, 0.5
          UNION ALL SELECT 1000000, 144, 1.0) AS v ON v.`bounty` = ROUND(COALESCE(b.`valueFloat`, b.`valueInt`))
   WHERE t.`groupID` IN (554, 558, 563, 568, 573);

-- 2b. sleepers:  Emergent (frigates), Awakened (cruisers), Sleepless (battleships).  sentinels, defenders and patrollers alike
DROP TEMPORARY TABLE IF EXISTS `_lootSleeper`;
CREATE TEMPORARY TABLE `_lootSleeper` (`npcGroupID` int(11) NOT NULL, `itemGroupID` int(11) NOT NULL, `share` float NOT NULL,
  PRIMARY KEY (`npcGroupID`, `itemGroupID`));
INSERT INTO `_lootSleeper` VALUES
  (961, 135, 0.5), (961, 136, 0.5), (986, 135, 0.5), (986, 136, 0.5), (987, 135, 0.5), (987, 136, 0.5),
  (960, 135, 0.4), (960, 136, 0.4), (960, 137, 0.2), (984, 135, 0.4), (984, 136, 0.4), (984, 137, 0.2),
  (985, 135, 0.4), (985, 136, 0.4), (985, 137, 0.2),
  (959, 135, 0.3), (959, 136, 0.3), (959, 137, 0.25), (959, 138, 0.15),
  (982, 135, 0.3), (982, 136, 0.3), (982, 137, 0.25), (982, 138, 0.15),
  (983, 135, 0.3), (983, 136, 0.3), (983, 137, 0.25), (983, 138, 0.15);
INSERT INTO `lootGroup` (`npcGroupID`, `npcGroupName`, `groupDropChance`, `itemGroupID`, `itemGroupName`)
  SELECT s.`npcGroupID`, g.`groupName`, s.`share`, s.`itemGroupID`,
         (SELECT MIN(i.`itemGroupName`) FROM `lootItemGroup` AS i WHERE i.`itemGroupID` = s.`itemGroupID`)
    FROM `_lootSleeper` AS s
    JOIN `invGroups` AS g ON g.`groupID` = s.`npcGroupID`
   WHERE NOT EXISTS (SELECT 1 FROM `lootGroup` AS x WHERE x.`npcGroupID` = s.`npcGroupID` AND x.`itemGroupID` = s.`itemGroupID`);
DROP TEMPORARY TABLE IF EXISTS `_lootSleeper`;

-- 4. wrecks by npc group.  a type of the group whose wreck is neither wreckTypeID nor altWreckTypeID (another faction or
--  size) gets wreckTypeID;  a type without a wreck gets it too
DROP TEMPORARY TABLE IF EXISTS `_wreckRule`;
CREATE TEMPORARY TABLE `_wreckRule` (`groupID` int(11) NOT NULL PRIMARY KEY, `wreckTypeID` int(11) NOT NULL,
  `altWreckTypeID` int(11) NOT NULL COMMENT 'also right:  a deadspace elite or commander type''s commander wreck');
INSERT INTO `_wreckRule` (`groupID`, `wreckTypeID`, `altWreckTypeID`) VALUES
  -- belt rats:  frigates and destroyers small, cruisers and battlecruisers medium, battleships and haulers large
  (550, 26561, 0), (575, 26561, 0), (551, 26562, 0), (576, 26562, 0),
  (552, 26563, 0), (554, 26563, 0), (553, 26566, 0),
  (557, 26567, 0), (577, 26567, 0), (555, 26568, 0), (578, 26568, 0),
  (556, 26569, 0), (558, 26569, 0), (559, 26572, 0),
  (562, 26573, 0), (579, 26573, 0), (561, 26574, 0), (580, 26574, 0),
  (560, 26575, 0), (563, 26575, 0), (564, 26578, 0),
  (567, 26579, 0), (581, 26579, 0), (566, 26580, 0), (582, 26580, 0),
  (565, 26581, 0), (568, 26581, 0), (569, 26584, 0),
  (572, 26585, 0), (583, 26585, 0), (571, 26586, 0), (584, 26586, 0),
  (570, 26587, 0), (573, 26587, 0), (574, 26590, 0),
  -- belt commanders (the faction spawns)
  (789, 26564, 0), (794, 26564, 0), (790, 26699, 0),
  (793, 26699, 0), (848, 26565, 0),
  (792, 26570, 0), (796, 26570, 0), (791, 26571, 0),
  (795, 26571, 0), (849, 26700, 0),
  (800, 26576, 0), (799, 26576, 0), (798, 26577, 0),
  (797, 26577, 0), (850, 26701, 0),
  (810, 26582, 0), (809, 26582, 0), (808, 26583, 0),
  (807, 26583, 0), (851, 26702, 0),
  (814, 26588, 0), (813, 26588, 0), (812, 26589, 0),
  (811, 26589, 0), (852, 26703, 0),
  -- deadspace pirates (their elite and commander types keep commander wrecks of the right faction and size)
  (597, 26561, 26564), (596, 26561, 26564), (595, 26562, 26699), (593, 26562, 26699), (594, 26563, 26565),
  (606, 26567, 26570), (605, 26567, 26570), (604, 26568, 26571), (602, 26568, 26571), (603, 26569, 26700),
  (615, 26573, 26576), (614, 26573, 26576), (613, 26574, 26577), (611, 26574, 26577), (612, 26575, 26701),
  (624, 26579, 26582), (623, 26579, 26582), (622, 26580, 26583), (620, 26580, 26583), (621, 26581, 26702),
  (633, 26585, 26588), (632, 26585, 26588), (631, 26586, 26589), (629, 26586, 26589), (630, 26587, 26703),
  -- rogue drones (belt and deadspace)
  (759, 26591, 0), (758, 26591, 0), (761, 26591, 0), (757, 26592, 0),
  (755, 26592, 0), (756, 26593, 0),
  (805, 26591, 0), (804, 26591, 0), (806, 26591, 0), (803, 26592, 0),
  (801, 26592, 0), (802, 26593, 0),
  -- deadspace overseers
  (819, 26918, 0), (820, 26919, 0), (821, 26920, 0),
  -- mission rats:  the empire and minor faction npc wrecks (Amarr/Caldari/Gallente/Minmatar/Khanid/Mordu/Thukker/CONCORD)
  (665, 27052, 0), (669, 27052, 0), (668, 27051, 0), (666, 27051, 0), (667, 27050, 0),
  (671, 27049, 0), (676, 27049, 0), (673, 27048, 0), (672, 27048, 0), (674, 27047, 0),
  (677, 27055, 0), (679, 27055, 0), (678, 27054, 0), (681, 27054, 0), (680, 27053, 0),
  (683, 27043, 0), (684, 27043, 0), (705, 27042, 0), (685, 27042, 0), (706, 27041, 0),
  (687, 27046, 0), (689, 27045, 0), (691, 27044, 0),
  (699, 27062, 0), (701, 27061, 0), (703, 27060, 0),
  (826, 27058, 0), (824, 27057, 0),
  (693, 26941, 0), (695, 26940, 0), (697, 26939, 0),
  (818, 26935, 0), (829, 26935, 0), (817, 26934, 0),
  (828, 26934, 0), (816, 26933, 0),
  (520, 26935, 0), (527, 26935, 0), (522, 26934, 0),
  (533, 26934, 0), (523, 26933, 0), (534, 26933, 0),
  (865, 27927, 0), (866, 27926, 0), (867, 27929, 0),
  (868, 27928, 0),
  (875, 28255, 0), (927, 29365, 0),
  (924, 29347, 0), (1006, 29347, 0), (1007, 29347, 0),
  -- incursion Sansha's Nation (the motherships keep the Sansha supercarrier wreck)
  (1053, 26579, 0), (1054, 26580, 0), (1056, 26581, 0), (1052, 3260, 0);
INSERT IGNORE INTO `invTypesToWrecks` (`typeID`, `typeName`, `wreckTypeID`, `wreckName`)
  SELECT t.`typeID`, t.`typeName`, r.`wreckTypeID`, w.`typeName`
    FROM `_wreckRule` AS r
    JOIN `invTypes` AS t ON t.`groupID` = r.`groupID`
    JOIN `invTypes` AS w ON w.`typeID` = r.`wreckTypeID`;
UPDATE `invTypesToWrecks` AS x
  JOIN `invTypes` AS t ON t.`typeID` = x.`typeID`
  JOIN `_wreckRule` AS r ON r.`groupID` = t.`groupID`
  JOIN `invTypes` AS w ON w.`typeID` = r.`wreckTypeID`
   SET x.`wreckTypeID` = r.`wreckTypeID`, x.`wreckName` = w.`typeName`
 WHERE x.`wreckTypeID` <> r.`wreckTypeID` AND x.`wreckTypeID` <> r.`altWreckTypeID`;
DROP TEMPORARY TABLE IF EXISTS `_wreckRule`;

-- +migrate Down
-- the wreck rows this migration added or put right stay (the old ones were wrong or missing)
DELETE FROM `lootGroup` WHERE `npcGroupID` IN (593, 594, 595, 596, 597, 602, 603, 604, 605, 606, 611, 612, 613, 614, 615,
  620, 621, 622, 623, 624, 629, 630, 631, 632, 633, 665, 666, 667, 668, 669, 671, 672, 673, 674, 676, 677, 678, 679, 680,
  681, 683, 684, 685, 687, 689, 691, 699, 701, 703, 705, 706, 824, 826, 959, 960, 961, 982, 983, 984, 985, 986, 987);
DELETE FROM `lootItemGroup` WHERE `itemGroupID` BETWEEN 135 AND 144;
DELETE FROM `lootItemGroup` WHERE `itemGroupID` IN (SELECT `typeID` FROM `invTypes` WHERE `groupID` IN (553, 559, 564, 569, 574));
DROP TABLE IF EXISTS `lootNpcType`;
