-- F16 industry: job output data for invention/reverse engineering, structure (POS) assembly lines, Crucible
--  invention inputs and the missing POS installation contents.
-- +migrate Up
ALTER TABLE ramJobs
  ADD COLUMN outputLocationID INT(10) UNSIGNED NOT NULL DEFAULT 0 AFTER outputFlag,
  ADD COLUMN containerTypeID INT(10) UNSIGNED NOT NULL DEFAULT 0 AFTER outputLocationID,
  ADD COLUMN installedItemTypeID INT(10) UNSIGNED NOT NULL DEFAULT 0 AFTER installedItemID,
  ADD COLUMN outputTypeID INT(10) UNSIGNED NOT NULL DEFAULT 0,
  ADD COLUMN outputRuns INT(10) NOT NULL DEFAULT 0,
  ADD COLUMN outputME TINYINT(4) NOT NULL DEFAULT 0,
  ADD COLUMN outputPE TINYINT(4) NOT NULL DEFAULT 0,
  ADD COLUMN successChance DOUBLE NOT NULL DEFAULT 0;
-- dynamic (structure) assembly lines need generated IDs
ALTER TABLE ramAssemblyLines MODIFY assemblyLineID INT(11) NOT NULL AUTO_INCREMENT;
-- Crucible: data interfaces are not consumed by invention (they are returned when the job ends)
UPDATE ramTypeRequirements r JOIN invTypes t ON t.typeID = r.requiredTypeID
   SET r.damagePerJob = 0 WHERE r.activityID = 8 AND t.groupID = 716;
-- missing POS installation contents (Large Ship Assembly Array, Subsystem Assembly Array; 3 lines each, pre-Crius)
INSERT IGNORE INTO ramInstallationTypeContents (installationTypeID, assemblyLineTypeID, quantity)
  VALUES (29613, 155, 3), (30389, 159, 3);
-- the Experimental Laboratory reverse engineering line accepts ancient relics
INSERT IGNORE INTO ramAssemblyLineTypeDetailPerCategory (assemblyLineTypeID, categoryID, timeMultiplier, materialMultiplier)
  VALUES (158, 34, 1, 1);

-- +migrate Down
DELETE FROM ramAssemblyLineTypeDetailPerCategory WHERE assemblyLineTypeID = 158 AND categoryID = 34;
DELETE FROM ramInstallationTypeContents WHERE (installationTypeID = 29613 AND assemblyLineTypeID = 155)
   OR (installationTypeID = 30389 AND assemblyLineTypeID = 159);
UPDATE ramTypeRequirements r JOIN invTypes t ON t.typeID = r.requiredTypeID
   SET r.damagePerJob = 1 WHERE r.activityID = 8 AND t.groupID = 716;
DELETE FROM ramAssemblyLines WHERE containerID NOT IN (SELECT stationID FROM staStations);
ALTER TABLE ramAssemblyLines MODIFY assemblyLineID INT(11) NOT NULL;
ALTER TABLE ramJobs DROP COLUMN outputLocationID, DROP COLUMN containerTypeID, DROP COLUMN installedItemTypeID,
  DROP COLUMN outputTypeID, DROP COLUMN outputRuns, DROP COLUMN outputME, DROP COLUMN outputPE, DROP COLUMN successChance;
