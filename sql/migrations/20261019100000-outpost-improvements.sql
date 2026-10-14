-- outpost improvements:  the station management window's 'Station Improvements' tab
--  (base_stationmanagement.GetRemoteImprovementTiers) shows an improvement only when its requiredAssemblyLineTypeID is
--  NULL or one of the outpost's assembly line types.  20211021144025-outpost.sql gave every plain (tier 2) and Advanced
--  (tier 3) improvement the race's 'Factory Tier1' line (43 Amarr, 70 Caldari, 93 Gallente, 46 Minmatar), so an
--  outpost without a basic factory never saw them, offices and refineries included.  the improvements need only the
--  tier below them (requiredImprovementTypeID) and a free slot (src/eve-common/EVE_Outpost.h).  0 is sent as None
--  (CorpStationMgr::GetImprovementStaticData).  safe to run again.
-- +migrate Up
UPDATE staImprovements SET requiredAssemblyLineTypeID = 0
 WHERE typeID BETWEEN 28076 AND 28135;

-- +migrate Down
UPDATE staImprovements
   SET requiredAssemblyLineTypeID = CASE raceID WHEN 4 THEN 43 WHEN 1 THEN 70 WHEN 8 THEN 93 WHEN 2 THEN 46 ELSE 0 END
 WHERE typeID BETWEEN 28076 AND 28135 AND requiredImprovementTypeID > 0;
