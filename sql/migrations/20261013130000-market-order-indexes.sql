-- market orders:  the order book of one type in a region, best price first (GetOrders, the immediate buy/sell matching)
--  and the orders of one owner (the client's My Orders, the corp's orders), which the market simulation's npc orders would
--  otherwise turn into a scan of every order
-- +migrate Up
ALTER TABLE mktOrders ADD KEY `regionTypeBidPrice` (`regionID`, `typeID`, `bid`, `price`), ADD KEY `ownerID` (`ownerID`);

-- +migrate Down
ALTER TABLE mktOrders DROP KEY `ownerID`, DROP KEY `regionTypeBidPrice`;
