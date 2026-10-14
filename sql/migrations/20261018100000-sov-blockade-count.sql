-- mapSystemSovInfo.contested is the number of stargates with an online Sovereignty Blockade Unit, not a flag.
--  the client (sovSvc.GetContestedState) shows 'Vulnerable' when contested * 2 > the system's stargates and 'Contested'
--  for any smaller count above 0 (src/eve-common/EVE_Sov.h).  the server used to store 1 only once more than half the
--  gates were blockaded.  this makes the column an unsigned count and recounts it from the SBUs in space
--  (group 1005, online states 4/5/6/9, one per stargate in posStructureData.anchorpointID).  the server recounts a
--  system again when it boots.  safe to run again.
-- +migrate Up
UPDATE mapSystemSovInfo SET contested = 0 WHERE contested IS NULL;
ALTER TABLE mapSystemSovInfo
MODIFY COLUMN `contested` tinyint(3) unsigned NOT NULL DEFAULT 0 AFTER hubID;
UPDATE mapSystemSovInfo AS s
   SET s.contested = (SELECT COUNT(DISTINCT p.anchorpointID)
                        FROM posStructureData AS p
                        INNER JOIN entity AS e ON e.itemID = p.itemID
                        INNER JOIN invTypes AS t ON t.typeID = e.typeID
                       WHERE t.groupID = 1005
                         AND e.locationID = s.solarSystemID
                         AND p.anchorpointID > 0
                         AND p.state IN (4, 5, 6, 9));

-- +migrate Down
-- back to the old flag:  1 when more than half the stargates (mapDenormalize group 10) are blockaded
UPDATE mapSystemSovInfo AS s
   SET s.contested = IF(s.contested * 2 > (SELECT COUNT(*) FROM mapDenormalize AS d
                                            WHERE d.solarSystemID = s.solarSystemID AND d.groupID = 10), 1, 0);
ALTER TABLE mapSystemSovInfo
MODIFY COLUMN `contested` tinyint(1) NULL DEFAULT 0 AFTER hubID;
