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

-- the rest:  pages that cover a whole chain, or don't say which part they are, get the chain's first part (or the part
--  the page names).  a few take the client's spelling.  Angel Strike, Intaki and Mordus Legion, Khanid Nobleman, Labor of Love,
--  Smash The Shipyard, The Kidnapping and Troop Build Up have no client title that clearly matches and keep their plain name.
UPDATE msnMissions SET nameID = 57176 WHERE pageKey = 'AreYouReceiving4am';
UPDATE msnMissions SET nameID = 57557 WHERE pageKey = 'AugumeneAllergen1am';
UPDATE msnMissions SET nameID = 57180 WHERE pageKey IN ('BigSting5am', 'BigSting5ca');
UPDATE msnMissions SET nameID = 57131 WHERE pageKey = 'DelvingIntoThePast2am';
UPDATE msnMissions SET nameID = 57692 WHERE pageKey = 'FatherAndSon3ga';
UPDATE msnMissions SET nameID = 56760 WHERE pageKey = 'FlameofPeace1';
UPDATE msnMissions SET nameID = 56894 WHERE pageKey = 'GreaterGood1';
UPDATE msnMissions SET nameID = 57192 WHERE pageKey = 'GuristasStrike3';
UPDATE msnMissions SET nameID = 57977 WHERE pageKey = 'HumanCattle2';
UPDATE msnMissions SET nameID = 54867 WHERE pageKey = 'IllegalActivity2ga';
UPDATE msnMissions SET nameID = 54885 WHERE pageKey = 'IllegalActivity4';
UPDATE msnMissions SET nameID = 56792 WHERE pageKey = 'InTheShadowOfWar1';
UPDATE msnMissions SET nameID = 56982 WHERE pageKey = 'InterstellarRailroad1mi';
UPDATE msnMissions SET nameID = 57193 WHERE pageKey = 'KidnappersStrike3';
UPDATE msnMissions SET nameID = 57195 WHERE pageKey = 'LaborOfWar1to3';
UPDATE msnMissions SET nameID = 57134 WHERE pageKey IN ('LocationLocation5am', 'LocationLocation5ca');
UPDATE msnMissions SET nameID = 57099 WHERE pageKey = 'MinmatarPlot2mi';
UPDATE msnMissions SET nameID = 54739 WHERE pageKey = 'MissingReporters3';
UPDATE msnMissions SET nameID = 56958 WHERE pageKey = 'MordusFolly4Mordus';
UPDATE msnMissions SET nameID = 56979 WHERE pageKey = 'MysteriousSightings1se';
UPDATE msnMissions SET nameID = 57004 WHERE pageKey = 'NewVenture1';
UPDATE msnMissions SET nameID = 54283 WHERE pageKey = 'PirateNuisance1sa';
UPDATE msnMissions SET nameID = 57784 WHERE pageKey = 'PortaltoWar3';
UPDATE msnMissions SET nameID = 58043 WHERE pageKey = 'PrisonBreak5cc';
UPDATE msnMissions SET nameID = 55369 WHERE pageKey IN ('Recon2an', 'Recon2br', 'Recon2gu', 'Recon2sa', 'Recon2se', 'Recon4an', 'Recon4br', 'Recon4gu', 'Recon4sa', 'Recon4se');
UPDATE msnMissions SET nameID = 55424 WHERE pageKey = 'Recon4';
UPDATE msnMissions SET nameID = 55513 WHERE pageKey = 'RogueSpy5';
UPDATE msnMissions SET nameID = 54396 WHERE pageKey = 'SecretProjectX3';
UPDATE msnMissions SET nameID = 54864 WHERE pageKey = 'TechnologicalSecrets2';
UPDATE msnMissions SET nameID = 54405 WHERE pageKey = 'Trespassers3mi';
UPDATE msnMissions SET nameID = 56806 WHERE pageKey = 'UndueAttention1ga';
UPDATE msnMissions SET nameID = 57221 WHERE pageKey IN ('Uprising2mo', 'Uprising3mo');
UPDATE msnMissions SET nameID = 58605 WHERE pageKey = 'WarSituation4am';
UPDATE msnMissions SET nameID = 57049 WHERE pageKey = 'WhatsInAName1';
UPDATE msnMissions SET nameID = 57119 WHERE pageKey = 'WhispersIntheDark2';

-- +migrate Down
UPDATE msnMissions SET nameID = 0 WHERE pageKey IN ('IntheMidstofDeadspace4p1', 'IntheMidstofDeadspace4p3', 'IntheMidstofDeadspace4p4',
    'IntheMidstofDeadspace4p5', 'RogueSlaveTrader3', 'RogueSlaveTrader4', 'BountifulBandine', 'CleartheTraderoute1', 'FallingNationComesCost3',
    'ForgottenOutposts4CaldariStateStoryline', 'IdEgonicsInc3amca', 'LambAmongstLions4', 'StrikingDownTwoFliesWithOneSwing1br',
    'StrikingDownTwoFliesWithOneSwing1gu', 'StrikingDownTwoFliesWithOneSwing1se', 'UnderstandingAugmene2');
UPDATE msnMissions SET nameID = 0 WHERE pageKey IN (
    'AreYouReceiving4am', 'AugumeneAllergen1am', 'BigSting5am', 'BigSting5ca', 'DelvingIntoThePast2am', 'FatherAndSon3ga',
    'FlameofPeace1', 'GreaterGood1', 'GuristasStrike3', 'HumanCattle2', 'IllegalActivity2ga', 'IllegalActivity4',
    'InTheShadowOfWar1', 'InterstellarRailroad1mi', 'KidnappersStrike3', 'LaborOfWar1to3', 'LocationLocation5am',
    'LocationLocation5ca', 'MinmatarPlot2mi', 'MissingReporters3', 'MordusFolly4Mordus', 'MysteriousSightings1se', 'NewVenture1',
    'PirateNuisance1sa', 'PortaltoWar3', 'PrisonBreak5cc', 'Recon2an', 'Recon2br', 'Recon2gu', 'Recon2sa', 'Recon2se', 'Recon4',
    'Recon4an', 'Recon4br', 'Recon4gu', 'Recon4sa', 'Recon4se', 'RogueSpy5', 'SecretProjectX3', 'TechnologicalSecrets2',
    'Trespassers3mi', 'UndueAttention1ga', 'Uprising2mo', 'Uprising3mo', 'WarSituation4am', 'WhatsInAName1', 'WhispersIntheDark2');
