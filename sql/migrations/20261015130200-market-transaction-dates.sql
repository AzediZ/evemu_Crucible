-- market transactions:  the server passed transactionDate (a double) to a %lli format, so on Windows x64 the double's bit
--  pattern was stored:  about 4.86e18 instead of a filetime of about 1.34e17 (the wallet showed the year ~17000, and
--  the daily price history and the insurance price index never found those trades).  the bits are decoded back:  a
--  positive double with the exponent of 2^56..2^57 (filetimes of the years 1829 to 2057) is (2^52 + mantissa) * 2^4.
--  (a Linux build stored an unrelated register instead;  such rows are left as they are)
-- +migrate Up
UPDATE mktTransactions SET transactionDate = ((transactionDate & 4503599627370495) | 4503599627370496) << 4 WHERE (transactionDate >> 52) = 1079;

-- +migrate Down
-- the broken values are not restored
SELECT 1;
