-- wallet transactions with the old market bots (Trader Joe, clientIDs 4000001-5000000, removed in market sim phase 0):  the
--  client can't resolve those ids (they are no characters, corporations or static owners) and the wallet's transaction
--  view failed on them.  the server used to show the owner as the counterparty instead;  the rows now say so
-- +migrate Up
UPDATE mktTransactions SET clientID = ownerID WHERE clientID BETWEEN 4000000 AND 5000000;

-- +migrate Down
-- the bots' ids are not kept (nothing can resolve them)
SELECT 1;
