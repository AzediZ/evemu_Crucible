-- market order ranges:  the station range is -1 (the client's const.rangeStation), but orderRange was unsigned, so it was
--  stored as 4294967295.  GetOrders/GetOrderRow sent that back (packed rows of type UI4):  the client compares the range
--  with -1, so station orders showed "4294967295 jumps" and every station-range buy order looked in range from anywhere.
--  the column is signed now.  valid ranges are -1, 0-5, 10, 20, 30, 40 and 32767 (region);  anything above that can
--  only be the old -1
-- +migrate Up
ALTER TABLE mktOrders MODIFY `orderRange` bigint(20) NOT NULL DEFAULT 0;
UPDATE mktOrders SET orderRange = -1 WHERE orderRange > 32767;
ALTER TABLE mktOrders MODIFY `orderRange` int(10) NOT NULL DEFAULT 0;

-- +migrate Down
ALTER TABLE mktOrders MODIFY `orderRange` bigint(20) NOT NULL DEFAULT 0;
UPDATE mktOrders SET orderRange = 4294967295 WHERE orderRange < 0;
ALTER TABLE mktOrders MODIFY `orderRange` int(5) unsigned NOT NULL DEFAULT 0;
