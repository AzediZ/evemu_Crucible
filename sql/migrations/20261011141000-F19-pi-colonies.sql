-- +migrate Up
-- F19: colonies are simulated with the client's code.  positions and head radii are doubles like the client's,
--  route IDs and quantities don't fit 16 bits, the per-planet pin count can pass 127
-- qtyPerCycle was added nullable (20210514214534); strict mode refuses NOT NULL over existing NULLs
UPDATE `piPins` SET `qtyPerCycle` = 0 WHERE `qtyPerCycle` IS NULL OR `qtyPerCycle` < 0;
ALTER TABLE `piPins`
  MODIFY `latitude` DOUBLE NOT NULL DEFAULT 0,
  MODIFY `longitude` DOUBLE NOT NULL DEFAULT 0,
  MODIFY `headRadius` DOUBLE NOT NULL DEFAULT 0,
  MODIFY `qtyPerCycle` INT(10) UNSIGNED NOT NULL DEFAULT 0,
  ADD KEY `ccPinID` (`ccPinID`);
ALTER TABLE `piRoutes`
  MODIFY `routeID` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,
  MODIFY `itemQty` INT(10) UNSIGNED NOT NULL DEFAULT 0,
  MODIFY `path` VARCHAR(255) NOT NULL DEFAULT '',
  ADD KEY `ccPinID` (`ccPinID`);
ALTER TABLE `piCCPin`
  ADD PRIMARY KEY (`pinID`),
  ADD KEY `planetID` (`planetID`),
  ADD KEY `charID` (`charID`);
ALTER TABLE `piPlanets`
  MODIFY `numberOfPins` SMALLINT(5) UNSIGNED NOT NULL DEFAULT 1;
ALTER TABLE `piLinks` ADD KEY `ccPinID` (`ccPinID`);
ALTER TABLE `piPinContents` ADD KEY `ccPinID` (`ccPinID`);
ALTER TABLE `piECUHeads` ADD KEY `ecuID` (`ecuID`);

-- Crucible pin prices.  the client charges a pin's base price (ClientColony.PostValidateCreatePin), the db had 0
UPDATE `invTypes` SET `basePrice` = 45000  WHERE `groupID` = 1063;
UPDATE `invTypes` SET `basePrice` = 250000 WHERE `groupID` = 1029;
UPDATE `invTypes` SET `basePrice` = 900000 WHERE `groupID` = 1030;
UPDATE `invTypes` SET `basePrice` = 75000  WHERE `typeID` IN (2469, 2471, 2473, 2481, 2483, 2490, 2492, 2493);
UPDATE `invTypes` SET `basePrice` = 250000 WHERE `typeID` IN (2470, 2472, 2474, 2480, 2484, 2485, 2491, 2494);
UPDATE `invTypes` SET `basePrice` = 525000 WHERE `typeID` IN (2475, 2482);

-- +migrate Down
UPDATE `invTypes` SET `basePrice` = 0 WHERE `groupID` IN (1029, 1030, 1063);
UPDATE `invTypes` SET `basePrice` = 0 WHERE `typeID` IN (2469, 2471, 2473, 2481, 2483, 2490, 2492, 2493, 2470, 2472, 2474, 2480, 2484, 2485, 2491, 2494, 2475, 2482);
ALTER TABLE `piECUHeads` DROP KEY `ecuID`;
ALTER TABLE `piPinContents` DROP KEY `ccPinID`;
ALTER TABLE `piLinks` DROP KEY `ccPinID`;
ALTER TABLE `piPlanets`
  MODIFY `numberOfPins` TINYINT(3) NOT NULL DEFAULT 1;
ALTER TABLE `piCCPin`
  DROP PRIMARY KEY,
  DROP KEY `planetID`,
  DROP KEY `charID`;
ALTER TABLE `piRoutes`
  DROP KEY `ccPinID`,
  MODIFY `routeID` SMALLINT(6) UNSIGNED NOT NULL AUTO_INCREMENT,
  MODIFY `itemQty` INT(10) NOT NULL DEFAULT 0,
  MODIFY `path` VARCHAR(200) NOT NULL DEFAULT '0';
ALTER TABLE `piPins`
  DROP KEY `ccPinID`,
  MODIFY `latitude` FLOAT NOT NULL DEFAULT 0,
  MODIFY `longitude` FLOAT NOT NULL DEFAULT 0,
  MODIFY `headRadius` FLOAT NOT NULL DEFAULT 0,
  MODIFY `qtyPerCycle` INT(20) DEFAULT NULL;
