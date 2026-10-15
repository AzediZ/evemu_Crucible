-- Mission titles: client title IDs for missions whose names did not match
-- +migrate Up
-- client title messageIDs for missions whose eve-survival name didn't match the client's:
--  the numbered parts of a chain, and names spelled differently.  without one the journal gets the plain name.
UPDATE msnMissions SET nameID = 57108 WHERE pageKey = 'IntheMidstofDeadspace4p1';
UPDATE msnMissions SET nameID = 57107 WHERE pageKey = 'IntheMidstofDeadspace4p3';
UPDATE msnMissions SET nameID = 57110 WHERE pageKey = 'IntheMidstofDeadspace4p4';
UPDATE msnMissions SET nameID = 57111 WHERE pageKey = 'IntheMidstofDeadspace4p5';
UPDATE msnMissions SET nameID = 54974 WHERE pageKey IN ('RogueSlaveTrader3', 'RogueSlaveTrader4');
UPDATE msnMissions SET nameID = 57031 WHERE pageKey = 'BountifulBandine';
UPDATE msnMissions SET nameID = 54856 WHERE pageKey = 'CleartheTraderoute1';
UPDATE msnMissions SET nameID = 54320 WHERE pageKey = 'FallingNationComesCost3';
UPDATE msnMissions SET nameID = 58037 WHERE pageKey = 'ForgottenOutposts4CaldariStateStoryline';
UPDATE msnMissions SET nameID = 56959 WHERE pageKey = 'IdEgonicsInc3amca';
UPDATE msnMissions SET nameID = 58035 WHERE pageKey = 'LambAmongstLions4';
UPDATE msnMissions SET nameID = 55442 WHERE pageKey IN ('StrikingDownTwoFliesWithOneSwing1br', 'StrikingDownTwoFliesWithOneSwing1gu', 'StrikingDownTwoFliesWithOneSwing1se');
UPDATE msnMissions SET nameID = 56877 WHERE pageKey = 'UnderstandingAugmene2';

-- +migrate Down
UPDATE msnMissions SET nameID = 0 WHERE pageKey IN ('IntheMidstofDeadspace4p1', 'IntheMidstofDeadspace4p3', 'IntheMidstofDeadspace4p4',
    'IntheMidstofDeadspace4p5', 'RogueSlaveTrader3', 'RogueSlaveTrader4', 'BountifulBandine', 'CleartheTraderoute1', 'FallingNationComesCost3',
    'ForgottenOutposts4CaldariStateStoryline', 'IdEgonicsInc3amca', 'LambAmongstLions4', 'StrikingDownTwoFliesWithOneSwing1br',
    'StrikingDownTwoFliesWithOneSwing1gu', 'StrikingDownTwoFliesWithOneSwing1se', 'UnderstandingAugmene2');
