-- dates written from GetFileTimeNow() (a double) through a %lli format:  on Windows x64 the double's bit pattern was
--  stored (about 4.86e18, the year ~17000) instead of the filetime.  the same decode as for mktTransactions
--  (20261015130200):  a positive double with the exponent of 2^56..2^57 (filetimes of the years 1829 to 2057) is
--  (2^52 + mantissa) * 2^4.  only rows whose top bits mark them as such a pattern are touched.
-- +migrate Up
UPDATE ctrBids SET bidDateTime = ((bidDateTime & 4503599627370495) | 4503599627370496) << 4 WHERE (bidDateTime >> 52) = 1079;
UPDATE ctrContracts SET dateExpired = ((dateExpired & 4503599627370495) | 4503599627370496) << 4 WHERE (dateExpired >> 52) = 1079;
UPDATE bills SET paidDateTime = ((paidDateTime & 4503599627370495) | 4503599627370496) << 4 WHERE (paidDateTime >> 52) = 1079;
UPDATE crpMedals SET `date` = ((`date` & 4503599627370495) | 4503599627370496) << 4 WHERE (`date` >> 52) = 1079;
UPDATE invTrashLog SET trashTime = ((trashTime & 4503599627370495) | 4503599627370496) << 4 WHERE (trashTime >> 52) = 1079;
UPDATE invAuditLog SET logDate = ((logDate & 4503599627370495) | 4503599627370496) << 4 WHERE (logDate >> 52) = 1079;
UPDATE piLaunches SET launchTime = ((launchTime & 4503599627370495) | 4503599627370496) << 4 WHERE (launchTime >> 52) = 1079;
UPDATE piCustomsOfficeSpawn SET spawnTime = ((spawnTime & 4503599627370495) | 4503599627370496) << 4 WHERE (spawnTime >> 52) = 1079;
UPDATE chrCharacters SET nextRespecDateTime = ((nextRespecDateTime & 4503599627370495) | 4503599627370496) << 4 WHERE (nextRespecDateTime >> 52) = 1079;

-- +migrate Down
-- the broken values are not restored
SELECT 1;
