-- chat:  the server no longer makes the hard-coded public channels (LSCService::CreateStaticChannels):  Rookie Help (1),
--  Help (2), the faction (10-14), corporate (20-21), trade (40-44), science and industry (50-55) and content (60-63)
--  channels, Free Wrecks (100) and the GM "Command" channel (2900000000, kept as int32 -1394967296).  they were never
--  rows in `channels` (only player channels, ids >= 2100000000, are), but GM kicks, gags and operator entries on them were
--  kept in `channelMods`.  those entries belong to no channel now:  drop them.  the new Global channel (world.globalChat)
--  is id 3, which none of the old channels used.  safe to run again.
-- +migrate Up
DELETE FROM `channelMods`
 WHERE `channelID` IN (1, 2, 10, 11, 12, 13, 14, 20, 21, 40, 41, 42, 43, 44, 50, 51, 52, 53, 54, 55, 60, 61, 62, 63, 100,
                       -1394967296);

-- +migrate Down
-- nothing to restore:  the entries were for channels that no longer exist
