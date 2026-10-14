-- sovereignty claims (mapSystemSovInfo) only exist in claimable space (src/eve-common/EVE_Sov.h).
--  empire and npc null-sec systems carry their faction in mapSolarSystems.factionID and the client shows that faction;
--  w-space can't be claimed (the client shows 'Unclaimable').  sovereignty structures used to launch in w-space, so a
--  claim may have been made there.  this removes any claim on a faction system or in w-space.  base data has none.
-- +migrate Up
DELETE s FROM mapSystemSovInfo AS s
  INNER JOIN mapSolarSystems AS m ON m.solarSystemID = s.solarSystemID
 WHERE m.factionID IS NOT NULL
    OR m.solarSystemID BETWEEN 31000000 AND 31999999;

-- +migrate Down
-- the removed claims were invalid and are not restored
