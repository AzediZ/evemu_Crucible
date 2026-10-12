/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
    Author:     Zhur, Bloody.Rabbit
    Updates:    Allan
    Version:    10.3
*/


#include "EVEServerConfig.h"

/*************************************************************************/
/* EVEServerConfig                                                       */
/*************************************************************************/
EVEServerConfig::EVEServerConfig()
{
    // register needed parsers
    AddMemberParser( "eve-server", &EVEServerConfig::ProcessEveServer );

    // Set sane defaults

    // items with a "N" behind them are NOT implemented
    // items with a "P" behind them are PARTIALLY implemented

    // items with /*x*/ behind them denote time identifier, with x = (s=seconds, m=minutes, etc)

    // server
    server.maxPlayers = 500;    // max authenticated sessions; 0 = unlimited.  VIP/GM roles bypass, others get a logon queue position
    server.BulkDataOD = false;
    server.BulkDataAutoVersion = true;  // derive the bulk data changeID from table checksums
    server.NoobShipCheck = true;
    server.ServerSleepTime = 10 /*ms*/;
    server.idleSleepTime = 1000;
    server.DisableIGB = true;
    server.MaxThreadReport = 20;
    server.ModuleAutoOff = false;
    server.ModuleDamageChance = 0.35;
    server.UnloadOnLinkAll = false;
    server.processTic = 1.0;
    server.AllowNonPublished = false;
    server.FleetShareDelayed = false;
    server.BountyPayoutDelayed = true;  // Crucible paid npc bounties as one 'Bounty Prizes' entry every 20 minutes
    server.BountyPayoutTimer = 20/*m*/;
    server.LoadOldMissions = false;
    server.AsteroidsOnDScan = false;
    server.CargoMassAdditive = false;
    server.ShieldBleedThrough = true;
    server.ArmorBleedThrough = false;
    server.T3SkillLoss = true;

    // world
    world.chatLogs = false;     // append every chat channel message to <logDir>/chat_YYYY-MM-DD.log  (F23)
    world.globalChat = true;    // create the server-wide static public channels at startup  (F23)
    world.gridUnload = true;
    world.gridUnloadTime = 300 /*s*/; // 5 mins
    world.loginInfo = true;     // send the server info MOTD into local on login
    world.loginMsg = true;      // show account.loginMessage after authentication
    world.saveOnMove = false;
    world.saveOnUpdate = false;
    world.shootRoids = false;
    world.shootWrecks = false;
    world.mailDelay = 5;        // min seconds between eve mails sent by one character; 0 = off  (F23)
    world.StationDockDelay = 4 /*s*/;
    world.apWarptoDistance = 15000;
    world.shipBoardDistance = 6550;         // surface distance, the client's maxBoardingDistance  (F09)
    world.SelfDestructDelay = 120 /*s*/;    // (F09)
    world.SelfDestructRetryDelay = 30 /*s*/;
    world.StoreShipWithCargo = false;       // Crucible: a stored ship may only carry charges  (F09)
    world.TrashRetentionDays = 30;          // trashed items restorable by a GM for 30 days  (F20)
    world.SecureContainerOwnerBypass = true;    // (F20)
    world.highSecCyno = false;
    world.empireInterdiction = false;
    world.fleetWarpOnGridOnly = false;      // Crucible fleet warp is system-wide (on-grid only came in 2015)  (F08)
    world.fleetWarpSlowestSpeed = true;     // every ship in a fleet warp uses the slowest warp speed  (F08)
    world.fleetInviteTimeout = 120 /*s*/;   // unanswered fleet invites time out (const.rejectFleetInviteTimeout)

    // rates
    rates.npcBountyMultiply = 1.0;
    rates.secRate = 1.0;
    rates.damageRate = 1.0;
    rates.missileDamage = 1.0;
    rates.missileTime = 1.0;
    rates.missileRoF = 1.0;
    rates.turretDamage = 1.0;
    rates.turretRoF = 1.0;
    rates.corpCost = 1599800;
    rates.allyCost = 1000000000;
    rates.medalAwardCost = 5000000;
    rates.medalCreateCost = 5000000;
    rates.WorldDecay = 120 /*m*/;
    rates.NPCDecay = 90 /*m*/;
    rates.DropItem = 1;         // integer multiplier on npc/structure loot quantities
    rates.DropMoney = 1.0;      // multiplier on agent mission isk reward and bonus  (F12)
    rates.DropSalvage = 1;//P
    rates.RepairCost = 1.0;
    rates.ShipRepairModifier = 0.0000075;
    rates.ModuleRepairModifier = 0.00125;
    rates.WebUpdate = 15 /*m*/;
    rates.TaxAmount = 5000;
    rates.TaxedAmount = 75000;
    // deprecated per-shooter crit chances.  -1 = not set;  a value set in the xml only gets a warning at startup
    rates.PlayerCritChance = -1.0f;
    rates.NpcCritChance = -1.0f;
    rates.SentryCritChance = -1.0f;
    rates.DroneCritChance = -1.0f;
    rates.ConcordCritChance = -1.0f;
    // the Crucible turret formula:  1% of shots are wrecking, and only inside a hit (npc ai plan 5.1)
    rates.WreckingChance = 0.01f;

    //market
    market.FindBuyOrder = 10;
    market.FindSellOrder = 10;
    market.StationOrderLimit = 10;
    market.SystemOrderLimit = 10;
    market.RegionOrderLimit = 10;
    market.OldPriceLimit = 10;
    market.NewPriceLimit = 10;
    market.HistoryUpdateTime = 6/*h*/;
    market.UseOrderRange = true;    // immediate buy/sell honours the order's range when matching; false = station-only  (F17)
    market.DeleteOldTransactions = false;
    market.salesTax = 1.0f;
    market.OrderModificationDelay = 300;
    market.HistoryDays = 365;
    market.UseMarginTrading = true;
    market.OrderCancelDelay = 300;
    market.ContractFees = true;
    market.ContractExpiredCleanupDays = 0;
    market.ContractCourierAutoFail = false;
    market.InsuranceUseMaterialIndex = true;
    market.InsuranceIndexDays = 30;
    market.InsuranceIndexMinTrades = 10;
    market.InsuranceDefaultPayout = 0.4f;
    market.InsuranceMultiplierT1 = 1.0f;
    market.InsuranceMultiplierT2 = 1.0f;
    market.InsuranceMultiplierT3 = 1.0f;

    // marketSim (PLAN 9.6 defaults;  MarketNoise and TransientNoise as tuned in the phase 2 harness)
    marketSim.Enabled = false;
    marketSim.Seed = 20111129;
    marketSim.EconomyScale = 0.01;
    marketSim.Hubs = "60003760,60008494,60011866,60004588,60005686";
    marketSim.RegionalNodes = "auto";
    marketSim.BudgetMsPerSecond = 4.0;
    marketSim.TickMinutes = 15;
    marketSim.HotCellMinutes = 15;
    marketSim.ColdCellMinutes = 120;
    marketSim.HotCellShare = 0.05;
    marketSim.QuoteLevels = 3;
    marketSim.MaxOrdersPerCellSide = 3;
    marketSim.SimMaxOrders = 300000;
    marketSim.QuoteDepthDays = 1.0;
    marketSim.RequoteThreshold = 0.01;
    marketSim.MinHalfSpread = 0.015;
    marketSim.MaxPriceDeviation = 2.0;
    marketSim.FairValueHalfLifeDays = 3.0;
    marketSim.MarketNoise = 1.0;
    marketSim.TransientNoise = 1.0;
    marketSim.PlayerTradeWeight = 0.1;
    marketSim.BuyBand = 0.10;
    marketSim.SellBand = 0.10;
    marketSim.IntakeShareCounterparty = 0.25;
    marketSim.MaxIskToCounterpartyPerDay = 0.02;
    marketSim.IntakeDays = 1.0;
    marketSim.SimPaysFees = true;
    marketSim.SimRecordNpcSide = false;
    marketSim.SimBrokerFee = 0.01;
    marketSim.EnableFacilities = true;
    marketSim.UseRealAssemblyLines = true;
    marketSim.MaxLineShare = 0.3;
    marketSim.MinFreeLinesPerStation = 2;
    marketSim.RefineryM3PerHour = 2000000.0;
    marketSim.MoneyDaysTarget = 30.0;
    marketSim.ControllerKp = 0.3;
    marketSim.ControllerKi = 0.02;
    marketSim.SupplyFactor = 1.0;
    marketSim.VendorStations = "corpHomeRegion";
    marketSim.VendorStock = 1000;
    marketSim.HistoryBackfillDays = 90;
    marketSim.MaxCatchUpHours = 48;
    marketSim.FlushSeconds = 30;
    marketSim.PauseOnAuditResidual = true;
    // marketSim phase 3 (PLAN 6.3, 6.4, 6.6;  KillmailCoverage per PLAN 4.2, line costs as Crucible's npc station lines)
    marketSim.Actors = true;
    marketSim.KillmailCoverage = 0.5;
    marketSim.PodLossRate = 0.0;
    marketSim.InternalSpreadShare = 0.5;
    marketSim.MinMargin = 0.02;
    marketSim.ProductionSlack = 1.0;
    marketSim.MiningSlack = 0.25;
    marketSim.MiningM3PerHullDay = 12000.0;
    marketSim.MaxSimJobHours = 24.0;
    marketSim.LineInstallCost = 1000.0;
    marketSim.LineCostPerHour = 333.0;
    // marketSim phase 6 (PLAN 6.4;  POS fuel:  a large tower's 40 blocks an hour shared by about four arrays)
    marketSim.EnableT2 = true;
    marketSim.EnableMoonPI = true;
    marketSim.InventBufferDays = 2.0;
    marketSim.HarvestSlack = 1.0;
    marketSim.PosFuelPerArrayHour = 10.0;
    marketSim.MoonHarvestPerHour = 1.0;

    // ram
    ram.AutoEvent = false;
    ram.ResME = 1.0;
    ram.ResPE = 1.0;
    ram.MatMod = 1.0;
    ram.WasteMod = 1.0;
    ram.ReTime = 1.0;
    ram.ProdTime = 1.0;
    ram.CopyTime = 1.0;
    ram.InventTime = 1.0;
    ram.InventChance = 1.0;
    ram.ReChance = 1.0;

    // account
    account.autoAccountRole = Acct::Role::STD;
    account.loginMessage = "";
    account.redeemEnabled = true;
    account.reverseRedeemTypes = "";
    account.cspaEnabled = true;
    account.cspaDefaultCost = 0;

    // character
    character.startBalance = 6666000000.0f;
    character.startAurBalance = 60000.0f;
    character.startStation = 0;
    character.startSecRating = 0.0;
    character.startCorporation = 0;
    character.terminationDelay = 180 /*s*/;
    character.statMultiplier = 1;
    character.allow3edChar = false;
    character.requirePausedTraining = true;
    character.jumpCloneCost = 100000.0;
    character.jumpCloneMinStanding = 8.0f;
    character.jumpCloneCooldownHours = 24;
    character.cloneSkillLoss = true;
    character.cloneSkillLossPercent = 5.0f;
    character.cloneSkillLossCap = 50.0f;
    character.boosterFallbackDurationHours = 24;

    // npc
    npc.IdleWander = false;
    npc.WarpOut = 600 /*s*/;
    npc.WarpFollowChance = 0.0; // Crucible rats never followed a ship through warp
    npc.ThreatRadius = 0;       // deprecated (npc ai phase 3):  ignored, a value > 0 logs a warning at boot
    npc.UseEwar = true;
    npc.GroupAggro = true;
    npc.DroneAggroChance = 0.10;
    npc.LeashRange = 0 /*m*/;   // deprecated (npc ai phase 2):  > 0 overrides the data leash's hard range
    npc.TargetSwitchTime = 0 /*s*/;    // 0 = the type's AI_TargetSwitchTimer (1416, dgm default 60s)
    npc.RoamingSpawns = false;
    npc.StaticSpawns = false;
    npc.RoamingTimer = 900 /*s*/;
    npc.StaticTimer = 600 /*s*/;
    npc.RespawnTimer = 1200 /*s*/;  // Crucible belt rats "reliably reappear in 20 to 25 minutes" (with RespawnJitter)
    npc.RatFaction = 0;
    npc.TargetPodSec = 0;
    npc.TargetPod = false;
    npc.UseDamageMultiplier = true;
    npc.DefenderMissileChance = 0.0;
    npc.LootDropChance = 0.75;
    // npc ai foundations (doc/code_and_design_notes/npc-ai/PLAN.md phases 0-1)
    npc.SleepEmptyBubbles = true;       // performance:  no ai for idle npcs where no player is
    npc.AuditOnStartup = false;         // logs every npc type's data problems at boot (slow-ish, for content work)
    npc.RequireWeaponEffects = true;    // Crucible dogma:  what an npc can fire comes from its effects (10, 569)
    npc.RngSeed = 0;                    // 0 = seeded from the clock
    npc.RespawnJitter = 300 /*s*/;      // 20 min + U(0, 5) min respawn (owner decision, npc ai plan 5.10)
    // npc ai phase 2:  standard ai (npc ai plan 4.2-4.4, 5.4-5.6, 5.8).  [DD] = design decision, no era number
    npc.AcquireRange = 100000 /*m*/;    // [DD] belt rats engage on landing;  frigates' entityAttackRange (~3km) can't be their sight (plan 10.4)
    npc.AcquireDrones = true;           // era: "new spawns may aggro drones"
    npc.DroneRetaliation = 1;           // era: drones aggro whole rooms;  owner default (plan 10.7)
    npc.DroneAggroInterval = 30 /*s*/;  // [DD] with DroneAggroChance 0.10
    npc.UseAttackDelay = true;          // data:  entityAttackDelayMin/Max (475/476)
    npc.AttackDelayScale = 1.0f;
    npc.UseReactionFactor = true;       // data:  entityReactionFactor (466) "chance of attacking the same person as its group members"
    npc.ChaseSigBloom = true;           // data:  1133 "used to fake MWD sig radius increase" (plan 10.8)
    npc.LeashSoftScale = 1.0f;          // data:  entityMaxWanderRange (584, 100km);  era: sleepers "fly back for a few seconds"
    npc.LeashHardScale = 2.0f;          // era:  "round about 200 km they will teleport back"
    npc.LeashHardMode = 1;              // warp (plan 10.9)
    npc.EwarUsageRoll = 0;              // per spawn:  "this rat carries a scram" (owner decision 6)
    npc.PassiveAggroRange = 15000 /*m*/;// [DD] era: "a ship or drone wandering into a group ... can cause the whole pocket to attack"
    npc.GroupAggroMemory = 900 /*s*/;   // [DD] era: returning after leaving re-engages an aggroed group
    npc.WaveWeakestChance = 0.5f;       // [DD] era: "mission respawns tend to target the weakest ship present first"
    npc.HaulerFleeDelay = 30 /*s*/;     // era: haulers "escape ... typically within 30 seconds if not attacked"
    npc.CommanderFlee = true;           // era (low-med):  commanders run when their escort is gone
    npc.RespectInterdiction = true;     // [DD] npcs can't warp out of (or through) warp disruption fields
    // npc ai phase 3:  level-one ai (npc ai plan 4.6, 4.7, 5.7-5.9).  the threat numbers are [DD] (plan 5.7), tune with /npcai
    npc.L1EvalInterval = 5 /*s*/;
    npc.L1SwitchMargin = 1.10f;
    npc.L1ThreatDecay = 0.90f;
    npc.L1ThreatDecayInterval = 10 /*s*/;
    npc.L1ThreatShare = 0.5f;           // era: sleepers' threat is shared by the group
    npc.L1EffectThreat = 0.02f;         // era: "ECM generates a very high threat level"
    npc.L1RepairThreat = 0.5f;          // owner default (plan 10.15)
    npc.L1DroneControllerShare = 0.25f;
    npc.L1NewcomerThreat = 1.2f;        // era: incursion sansha "most likely will change targets to any newcomers"
    npc.L1NewcomerMemory = 300 /*s*/;
    npc.RemoteEcmJamTime = 5 /*s*/;     // [DD] plan 5.8
    // npc ai phase 5:  polish (npc ai plan phase 5, 6.7).  [DD] unless noted
    npc.SoftLeashCooldown = 20 /*s*/;   // [DD] phase 2's constant:  without it a soft-leashed npc could never resume the chase
    npc.LinkAlertInterval = 5 /*s*/;    // [DD] phase 2's constant
    npc.SleeperCapitalReinforcements = 2;   // era (high):  "several battleships for each capital will spawn" (plan 4.6, phase 5)
    npc.SleeperReinforcementTypes = "";
    npc.TelegraphJoin = true;           // plan phase 5 readability:  npcs turn toward their target before the lock completes
    npc.WarpOutVanishChance = 0.04f;    // era (med):  "run-away spawns", "1 in 25 vanish"
    npc.WanderRepickMin = 60 /*s*/;     // plan phase 5:  U(60, 180) s
    npc.WanderRepickMax = 180 /*s*/;
    npc.BeltSpawnCap = 0.5f;            // era (med):  about half as many spawns as belts (plan phase 5 spawn generation)
    npc.LodFarInterval = 4 /*s*/;       // plan 6.7:  every 4s when players are only in the system
    npc.LodEmptySystem = true;          // plan 6.7:  nothing in empty systems
    npc.TickBudget = 50000 /*us*/;      // [DD] 5% of a tick per system;  only idle npcs away from players skip a tick
    npc.StatsInterval = 60 /*s*/;       // plan phase 5 telemetry (the line needs NPC__AI_STATS enabled)
    npc.Difficulty = 1.0f;              // plan phase 5:  1.0 = Crucible
    npc.DifficultyDamage = 0.0f;
    npc.DifficultyLockTime = 0.0f;
    npc.DifficultyReactionTime = 0.0f;

    // cosmic
    cosmic.PIEnabled = false;
    cosmic.AnomalyEnabled = false;
    cosmic.DungeonEnabled = false;
    cosmic.BeltEnabled = false;
    cosmic.BeltRespawn = 8 /*h*/;
    cosmic.BeltGrowth = 6 /*h*/;
    cosmic.roidRadiusMultiplier = 1.0;
    cosmic.WormHoleEnabled = false;
    cosmic.CiviliansEnabled = false;
    cosmic.BumpEnabled = false;
    cosmic.PIRestrictTradeHubs = true;
    cosmic.PISpawnAllCustomsOffices = true;
    cosmic.PICONCORDTaxMultiplier = 1.0f;
    cosmic.PIInterbusTaxMultiplier = 1.0f;
    cosmic.PIResourceSeed = 20111129;
    cosmic.PIResourceScale = 1.0f;
    cosmic.PIResourceSpectrum = 1.2f;
    cosmic.PIRichnessHighsec = 0.55f;
    cosmic.PIRichnessLowsec = 0.8f;
    cosmic.PIRichnessNullsec = 1.0f;
    cosmic.PIDepletionEnabled = true;
    cosmic.PIDepletionPerUnit = 0.0001f;
    cosmic.PIDepletionMaxFraction = 0.6f;
    cosmic.PIDepletionRadius = 1.5f;
    cosmic.PIDepletionRegenHours = 168.0f;
    cosmic.PIGantryMaxDistanceKm = 100000.0f;

    // exploring
    exploring.Gravametric = 5;
    exploring.Magnetometric = 3;
    exploring.Ladar = 6;
    exploring.Radar = 6;
    exploring.Unrated = 8;
    exploring.Complex = 4;
    exploring.DScanCooldown = 2000;
    exploring.SurveyProbeConeAngle = 30.0f;
    exploring.ProbeStrengthScale = 1.0f;
    exploring.ProbeShipSigDivisor = 25.0f;

    // dungeon
    dungeon.AnomalyLifetime = 0;
    dungeon.SignatureLifetime = 72;
    dungeon.CompletedDespawn = 120;
    dungeon.RespawnDelay = 300;
    dungeon.EscalationChance = 1.0f;
    dungeon.EscalationExpiry = 48;
    dungeon.EscalationShareFleet = false;
    dungeon.GateKeysRequired = true;
    dungeon.TriggerMessages = true;

    // missions
    missions.OfferExpiryHours = 168;
    missions.AcceptedExpiryHours = 168;
    missions.DeclinePenaltyHours = 4;
    missions.UseDivisionTypes = true;
    missions.FallbackToCourier = true;
    {
        const uint32 isk[5] = { 40000, 160000, 450000, 900000, 6000000 };
        const uint32 bonus[5] = { 30000, 120000, 350000, 750000, 4000000 };
        const uint16 minutes[5] = { 60, 90, 120, 180, 240 };
        const uint32 lp[5] = { 200, 600, 1500, 3500, 12000 };
        for (uint8 i = 0; i < 5; ++i) {
            missions.BaseISK[i] = isk[i];
            missions.BonusISK[i] = bonus[i];
            missions.BonusMinutes[i] = minutes[i];
            missions.BaseLP[i] = lp[i];
        }
    }
    missions.IskSecurityScaling = true;
    missions.FleetShare = true;
    missions.FleetShareMax = 10;
    missions.FleetShareSameSystem = true;
    missions.DungeonDespawnMinutes = 120;
    missions.StorylineEvery = 16;
    missions.GateRestrictions = true;

    // agents
    agents.researchDatacoreRP = 100;
    agents.researchDatacoreISK = 0.0;
    agents.researchFieldMultipliers = true;
    agents.locatorEnabled = true;

    // standings
    //  - mission
    standings.MissionBonus = 1.0;
    standings.MissionFailure = -0.5;
    standings.MissionDeclined = -0.2;
    standings.MissionCompleted = 1.0;
    standings.MissionOfferExpired = -0.1;
    standings.MissionFailedRollback = -0.5;
    standings.ImportantMissionBonus = 3.0;
    standings.BaseMissionMultiplier = 1.0;
    standings.FleetMissionMultiplier = 0.5;
    standings.Agent2CharMissionMultiplier = 1.0;
    standings.ACorp2CharMissionMultiplier = 0.25;
    standings.Agent2PCorpMissionMultiplier = 0.1;
    standings.ACorp2PCorpMissionMultiplier = 0.025;
    standings.AFaction2CharMissionMultiplier = 0.125;
    standings.AFaction2PCorpMissionMultiplier = 0.0125;
    //  - PVP
    //  - ratting/exploring

    // chat
    chat.EnableFleetChat = true;
    chat.EnableWingChat = false;
    chat.EnableSquadChat = false;
    chat.EnableVoiceChat = false;
    chat.EnforceRookieInHelp = false;
    chat.EnforceChannelAccess = true;
    chat.ContactStatusMutual = false;   // Crucible: any watchlisted character's online state is shown  (F23)
    chat.UserChannelCleanup = true;     // delete player channels nobody is subscribed to any more  (F23)

    // crime
    crime.Enabled = true;
    crime.AggFlagTime = 900 /*s*/;
    crime.CrimFlagTime = 900 /*s*/;
    crime.WeaponFlagTime = 60 /*s*/;
    crime.KillRightDays = 30;
    crime.LogoffNPCTime = 120 /*s*/;
    crime.LogoffWarpTime = 60 /*s*/;
    crime.SecGainWindow = 900 /*s*/;
    crime.SecAggressionPenalty = 1.5f;
    crime.SecShipKillPenalty = 6.0f;
    crime.SecPodKillPenalty = 37.5f;
    crime.SecPodKillMinPenalty = 12.5f;
    crime.CriminalLockdown = true;
    crime.SwitchShipWeaponsTimer = false;   // Retribution rule, off for Crucible  (F09)
    crime.Concord = true;
    crime.ConcordResponse = "2,5,10,15,20,25";   // closest pre-Crucible measurements (owner decision, npc ai plan 10.3)
    crime.ConcordWaves = false;
    crime.ConcordWaveDelay = 10 /*s*/;
    crime.ConcordLinger = 60 /*s*/;
    crime.FactionPolice = true;
    crime.PoliceDelay = 15 /*s*/;
    crime.PoliceTimeout = 600 /*s*/;
    crime.Sentries = true;
    crime.SentriesPerGate = 3;
    crime.SentriesPerStation = 2;
    crime.SentryRange = 150000 /*m*/;
    // npc ai plan phase 4 (PLAN.md 4.8 - 4.10).  EVElopedia "Sentry guns" (Dec 2011):  a random target every 30s,
    //  aggression with the guarded object for 15 minutes, renewed each time a gun picks you
    crime.SentryRetarget = 30 /*s*/;
    crime.SentryAggression = 900 /*s*/;
    crime.ConcordScriptedTackle = false;    // CONCORD uses its data ewar (scram 10, -95% web, 5s jams)
    crime.FactionNavy = false;              // plan decision 11:  off until navy content is verified
    crime.FactionNavyStanding = -5.0f;
    crime.NpcFactionAggression = false;     // plan decision 11 (EVElopedia "Combat Timers" 2012)

    // sov
    sov.MilitaryPointsPerBountyISK = 0.1f;
    sov.IndustrialPointsPerM3 = 1.0f;
    sov.DevIndexDecayPerHour = 100000;

    // pos
    pos.FuelRate = 1.0f;
    pos.RequireCharters = true;
    pos.HighSecStandingCheck = true;
    pos.ConcordAnchorRestriction = true;
    pos.KeepTowerSystemsLoaded = true;
    pos.MaxCatchUpCycles = 720;
    pos.MoonGooSeed = 1;
    pos.MoonGooEmptyChance = 0.5f;
    pos.GunsEnabled = true;
    pos.TowerSnapToMoon = false;

    // corp
    corp.StasisHours = 24;
    corp.CEOVoteMinSharePercent = 5;
    corp.CEOVoteCooldownDays = 14;
    corp.SanctionDays = 7;
    corp.EnforceRaceLimit = true;
    corp.JunkReleaseFraction = 0.5f;
    corp.BillsEnabled = true;
    corp.CloseAllianceOnUnpaidBill = true;
    corp.BillGraceDays = 0;
    war.Enabled = true;
    war.CorpWarCost = 2000000.0;
    war.AllianceWarCost = 50000000.0;
    war.MaxDeclaredWars = 0;
    war.BillDays = 7;
    war.HistoryDays = 30;
    alliance.JoinDelayHours = 24;
    alliance.ExecutorTallyHour = 11;
    alliance.ExecutorMinMemberDays = 7;

    // testing
    testing.EnableDrones = false;

    // debug
    debug.BeanCount = false;
    debug.StackTrace = false;
    debug.BubbleTrack = false;
    debug.IsTestServer = true;
    debug.UseProfiling = false;
    debug.PositionHack = false;
    debug.UseShipTracking = false;
    debug.DeleteTrackingCans = true;
    debug.SpawnTest = false;
    debug.AnomalyFaction = 0;
    debug.ProfileTraceTime = 150/*ms*/;

    // database
    database.host = "localhost";
    database.port = 3306;
    database.username = "eve";
    database.password = "eve";
    database.db = "evemu";
    database.compress = false;
    database.ssl = false;
    database.useSocket = false;
    database.autoReconnect = false;
    database.dbTimeout = 2/*s*/;
    database.pingTime = 10/*m*/;

    // files
    files.logDir = "../log/";
    files.logSettings = "../etc/log.ini";
    files.cacheDir = "../server_cache/";
    files.imageDir = "../image_cache/";

    // net
    net.port = 26000;
    net.imageServer = "localhost";
    net.imageServerPort = 26001;

    // threads  -parsed, ignored: the server main loop is single-threaded by design
    threads.ConsoleThreads = 1;//P
    threads.DatabaseThreads = 2;//N
    threads.ImageServerThreads = 1;//N
    threads.NetworkThreads = 2;//N
    threads.WorldThreads = 2;//N
}

bool EVEServerConfig::ProcessEveServer( const TiXmlElement* ele )
{
    // entering element, extend allowed syntax
    AddMemberParser( "server",      &EVEServerConfig::ProcessServer );
    AddMemberParser( "world",       &EVEServerConfig::ProcessWorld );
    AddMemberParser( "rates",       &EVEServerConfig::ProcessRates );
    AddMemberParser( "market",      &EVEServerConfig::ProcessMarket );
    AddMemberParser( "marketSim",   &EVEServerConfig::ProcessMarketSim );
    AddMemberParser( "ram",     &EVEServerConfig::ProcessBPTimes );
    AddMemberParser( "account",     &EVEServerConfig::ProcessAccount );
    AddMemberParser( "character",   &EVEServerConfig::ProcessCharacter );
    AddMemberParser( "npc",         &EVEServerConfig::ProcessNPC );
    AddMemberParser( "cosmic",      &EVEServerConfig::ProcessCosmic );
    AddMemberParser( "exploring",   &EVEServerConfig::ProcessExploring );
    AddMemberParser( "dungeon",     &EVEServerConfig::ProcessDungeon );
    AddMemberParser( "missions",    &EVEServerConfig::ProcessMissions );
    AddMemberParser( "agents",      &EVEServerConfig::ProcessAgents );
    AddMemberParser( "crime",       &EVEServerConfig::ProcessCrime );
    AddMemberParser( "sov",         &EVEServerConfig::ProcessSov );
    AddMemberParser( "pos",         &EVEServerConfig::ProcessPOS );
    AddMemberParser( "corp",        &EVEServerConfig::ProcessCorp );
    AddMemberParser( "war",         &EVEServerConfig::ProcessWar );
    AddMemberParser( "alliance",    &EVEServerConfig::ProcessAlliance );
    AddMemberParser( "standings",   &EVEServerConfig::ProcessStandings );
    AddMemberParser( "chat",        &EVEServerConfig::ProcessChat );
    AddMemberParser( "debug",       &EVEServerConfig::ProcessDebug );
    AddMemberParser( "database",    &EVEServerConfig::ProcessDatabase );
    AddMemberParser( "files",       &EVEServerConfig::ProcessFiles );
    AddMemberParser( "net",         &EVEServerConfig::ProcessNet );
    AddMemberParser( "testing",     &EVEServerConfig::ProcessTesting );
    AddMemberParser( "threads",     &EVEServerConfig::ProcessThreads );

    // parse the element
    const bool result = ParseElementChildren( ele );

    // leaving element, reduce allowed syntax
    RemoveParser( "server" );
    RemoveParser( "world" );
    RemoveParser( "rates" );
    RemoveParser( "market" );
    RemoveParser( "marketSim" );
    RemoveParser( "ram" );
    RemoveParser( "account" );
    RemoveParser( "character" );
    RemoveParser( "npc" );
    RemoveParser( "cosmic" );
    RemoveParser( "exploring" );
    RemoveParser( "dungeon" );
    RemoveParser( "missions" );
    RemoveParser( "agents" );
    RemoveParser( "crime" );
    RemoveParser( "sov" );
    RemoveParser( "pos" );
    RemoveParser( "corp" );
    RemoveParser( "war" );
    RemoveParser( "alliance" );
    RemoveParser( "standings" );
    RemoveParser( "chat" );
    RemoveParser( "debug" );
    RemoveParser( "database" );
    RemoveParser( "files" );
    RemoveParser( "net" );
    RemoveParser( "testing" );
    RemoveParser( "threads" );

    // return status of parsing
    return result;
}

bool EVEServerConfig::ProcessServer( const TiXmlElement* ele )
{
    AddValueParser( "DisableIGB",           server.DisableIGB );
    AddValueParser( "maxPlayers",           server.maxPlayers );
    AddValueParser( "NoobShipCheck",        server.NoobShipCheck );
    AddValueParser( "BulkDataOD",           server.BulkDataOD );
    AddValueParser( "BulkDataAutoVersion",  server.BulkDataAutoVersion );
    AddValueParser( "ServerSleepTime",      server.ServerSleepTime );
    AddValueParser( "idleSleepTime",        server.idleSleepTime );
    AddValueParser( "MaxThreadReport",      server.MaxThreadReport );
    AddValueParser( "ModuleAutoOff",        server.ModuleAutoOff );
    AddValueParser( "ModuleDamageChance",   server.ModuleDamageChance );
    AddValueParser( "UnloadOnLinkAll",      server.UnloadOnLinkAll );
    AddValueParser( "processTic",           server.processTic );
    AddValueParser( "AllowNonPublished",    server.AllowNonPublished );
    AddValueParser( "FleetShareDelayed",    server.FleetShareDelayed );
    AddValueParser( "BountyPayoutDelayed",  server.BountyPayoutDelayed );
    AddValueParser( "BountyPayoutTimer",    server.BountyPayoutTimer );
    AddValueParser( "LoadOldMissions",      server.LoadOldMissions );
    AddValueParser( "AsteroidsOnDScan",     server.AsteroidsOnDScan );
    AddValueParser( "CargoMassAdditive",    server.CargoMassAdditive );
    AddValueParser( "ShieldBleedThrough",   server.ShieldBleedThrough );
    AddValueParser( "ArmorBleedThrough",    server.ArmorBleedThrough );
    AddValueParser( "T3SkillLoss",          server.T3SkillLoss );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "DisableIGB" );
    RemoveParser( "maxPlayers" );
    RemoveParser( "NoobShipCheck" );
    RemoveParser( "BulkDataOD" );
    RemoveParser( "BulkDataAutoVersion" );
    RemoveParser( "ServerSleepTime" );
    RemoveParser( "idleSleepTime" );
    RemoveParser( "MaxThreadReport" );
    RemoveParser( "ModuleAutoOff" );
    RemoveParser( "ModuleDamageChance" );
    RemoveParser( "processTic" );
    RemoveParser( "AllowNonPublished" );
    RemoveParser( "FleetShareDelayed" );
    RemoveParser( "BountyPayoutDelayed" );
    RemoveParser( "BountyPayoutTimer" );
    RemoveParser( "LoadOldMissions" );
    RemoveParser( "AsteroidsOnDScan" );
    RemoveParser( "CargoMassAdditive" );
    RemoveParser( "ShieldBleedThrough" );
    RemoveParser( "ArmorBleedThrough" );
    RemoveParser( "T3SkillLoss" );

    return result;
}

bool EVEServerConfig::ProcessWorld( const TiXmlElement* ele )
{
    AddValueParser( "chatLogs",          world.chatLogs );
    AddValueParser( "globalChat",        world.globalChat );
    AddValueParser( "gridUnload",        world.gridUnload );
    AddValueParser( "gridUnloadTime",    world.gridUnloadTime );
    AddValueParser( "loginInfo",         world.loginInfo );
    AddValueParser( "loginMsg",          world.loginMsg );
    AddValueParser( "saveOnMove",        world.saveOnMove );
    AddValueParser( "saveOnUpdate",      world.saveOnUpdate );
    AddValueParser( "highSecCyno",       world.highSecCyno );
    AddValueParser( "empireInterdiction", world.empireInterdiction );
    AddValueParser( "fleetWarpOnGridOnly", world.fleetWarpOnGridOnly );
    AddValueParser( "fleetWarpSlowestSpeed", world.fleetWarpSlowestSpeed );
    AddValueParser( "fleetInviteTimeout", world.fleetInviteTimeout );
    AddValueParser( "mailDelay",         world.mailDelay );
    AddValueParser( "shootRoids",        world.shootRoids );
    AddValueParser( "shootWrecks",       world.shootWrecks );
    AddValueParser( "StationDockDelay",  world.StationDockDelay );
    AddValueParser( "apWarptoDistance",  world.apWarptoDistance );
    AddValueParser( "shipBoardDistance", world.shipBoardDistance );
    AddValueParser( "SelfDestructDelay", world.SelfDestructDelay );
    AddValueParser( "SelfDestructRetryDelay", world.SelfDestructRetryDelay );
    AddValueParser( "StoreShipWithCargo", world.StoreShipWithCargo );
    AddValueParser( "TrashRetentionDays", world.TrashRetentionDays );
    AddValueParser( "SecureContainerOwnerBypass", world.SecureContainerOwnerBypass );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "chatLogs" );
    RemoveParser( "globalChat" );
    RemoveParser( "gridUnload" );
    RemoveParser( "gridUnloadTime" );
    RemoveParser( "loginInfo" );
    RemoveParser( "loginMsg" );
    RemoveParser( "saveOnMove" );
    RemoveParser( "saveOnUpdate" );
    RemoveParser( "highSecCyno" );
    RemoveParser( "empireInterdiction" );
    RemoveParser( "fleetWarpOnGridOnly" );
    RemoveParser( "fleetWarpSlowestSpeed" );
    RemoveParser( "fleetInviteTimeout" );
    RemoveParser( "mailDelay" );
    RemoveParser( "shootRoids" );
    RemoveParser( "shootWrecks" );
    RemoveParser( "StationDockDelay" );
    RemoveParser( "apWarptoDistance" );
    RemoveParser( "shipBoardDistance" );
    RemoveParser( "SelfDestructDelay" );
    RemoveParser( "SelfDestructRetryDelay" );
    RemoveParser( "StoreShipWithCargo" );
    RemoveParser( "TrashRetentionDays" );
    RemoveParser( "SecureContainerOwnerBypass" );

    return result;
}

bool EVEServerConfig::ProcessRates( const TiXmlElement* ele )
{
    AddValueParser( "secRate",              rates.secRate );
    AddValueParser( "npcBountyMultiply",    rates.npcBountyMultiply );
    AddValueParser( "damageRate",           rates.damageRate );
    AddValueParser( "missileRoF",           rates.missileRoF );
    AddValueParser( "missileDamage",        rates.missileDamage );
    AddValueParser( "missileTime",          rates.missileTime );
    AddValueParser( "turretDamage",         rates.turretDamage );
    AddValueParser( "turretRoF",            rates.turretRoF );
    AddValueParser( "corpCost",             rates.corpCost );
    AddValueParser( "allyCost",             rates.allyCost );
    AddValueParser( "medalAwardCost",       rates.medalAwardCost );
    AddValueParser( "medalCreateCost",      rates.medalCreateCost );
    AddValueParser( "WorldDecay",           rates.WorldDecay );
    AddValueParser( "NPCDecay",             rates.NPCDecay );
    AddValueParser( "DropItem",             rates.DropItem );
    AddValueParser( "DropMoney",            rates.DropMoney );
    AddValueParser( "DropSalvage",          rates.DropSalvage );
    AddValueParser( "RepairCost",           rates.RepairCost );
    AddValueParser( "ShipRepairModifier",   rates.ShipRepairModifier );
    AddValueParser( "ModuleRepairModifier", rates.ModuleRepairModifier );
    AddValueParser( "WebUpdate",            rates.WebUpdate );
    AddValueParser( "TaxAmount",            rates.TaxAmount );
    AddValueParser( "TaxedAmount",          rates.TaxedAmount );
    AddValueParser( "PlayerCritChance",     rates.PlayerCritChance );
    AddValueParser( "NpcCritChance",        rates.NpcCritChance );
    AddValueParser( "SentryCritChance",     rates.SentryCritChance );
    AddValueParser( "DroneCritChance",      rates.DroneCritChance );
    AddValueParser( "ConcordCritChance",    rates.ConcordCritChance );
    AddValueParser( "WreckingChance",       rates.WreckingChance );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "secRate" );
    RemoveParser( "npcBountyMultiply" );
    RemoveParser( "damageRate" );
    RemoveParser( "missileDamage" );
    RemoveParser( "missileRoF" );
    RemoveParser( "missileTime" );
    RemoveParser( "turretDamage" );
    RemoveParser( "turretRoF" );
    RemoveParser( "corpCost" );
    RemoveParser( "allyCost" );
    RemoveParser( "medalAwardCost" );
    RemoveParser( "medalCreateCost" );
    RemoveParser( "WorldDecay" );
    RemoveParser( "NPCDecay" );
    RemoveParser( "DropItem" );
    RemoveParser( "DropMoney" );
    RemoveParser( "DropSalvage" );
    RemoveParser( "RepairCost" );
    RemoveParser( "ShipRepairModifier" );
    RemoveParser( "ModuleRepairModifier" );
    RemoveParser( "WebUpdate" );
    RemoveParser( "TaxAmount" );
    RemoveParser( "TaxedAmount" );
    RemoveParser( "PlayerCritChance" );
    RemoveParser( "NpcCritChance" );
    RemoveParser( "SentryCritChance" );
    RemoveParser( "DroneCritChance" );
    RemoveParser( "ConcordCritChance" );
    RemoveParser( "WreckingChance" );

    return result;
}

bool EVEServerConfig::ProcessMarket(const TiXmlElement* ele)
{
    AddValueParser( "StationOrderLimit",            market.StationOrderLimit );
    AddValueParser( "SystemOrderLimit",             market.SystemOrderLimit);
    AddValueParser( "RegionOrderLimit",             market.RegionOrderLimit );
    AddValueParser( "FindBuyOrder",                 market.FindBuyOrder );
    AddValueParser( "FindSellOrder",                market.FindSellOrder);
    AddValueParser( "OldPriceLimit",                market.OldPriceLimit );
    AddValueParser( "NewPriceLimit",                market.NewPriceLimit);
    AddValueParser( "UseOrderRange",                market.UseOrderRange);
    AddValueParser( "DeleteOldTransactions",        market.DeleteOldTransactions);
    AddValueParser( "HistoryUpdateTime",            market.HistoryUpdateTime);
    AddValueParser( "SalesTax",                     market.salesTax);
    AddValueParser( "OrderModificationDelay",       market.OrderModificationDelay);
    AddValueParser( "HistoryDays",                 market.HistoryDays );
    AddValueParser( "UseMarginTrading",            market.UseMarginTrading );
    AddValueParser( "OrderCancelDelay",            market.OrderCancelDelay );
    AddValueParser( "ContractFees",                market.ContractFees );
    AddValueParser( "ContractExpiredCleanupDays",  market.ContractExpiredCleanupDays );
    AddValueParser( "ContractCourierAutoFail",     market.ContractCourierAutoFail );
    AddValueParser( "InsuranceUseMaterialIndex",   market.InsuranceUseMaterialIndex );
    AddValueParser( "InsuranceIndexDays",          market.InsuranceIndexDays );
    AddValueParser( "InsuranceIndexMinTrades",     market.InsuranceIndexMinTrades );
    AddValueParser( "InsuranceDefaultPayout",      market.InsuranceDefaultPayout );
    AddValueParser( "InsuranceMultiplierT1",       market.InsuranceMultiplierT1 );
    AddValueParser( "InsuranceMultiplierT2",       market.InsuranceMultiplierT2 );
    AddValueParser( "InsuranceMultiplierT3",       market.InsuranceMultiplierT3 );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "StationOrderLimit" );
    RemoveParser( "SystemOrderLimit" );
    RemoveParser( "RegionOrderLimit" );
    RemoveParser( "FindBuyOrder" );
    RemoveParser( "FindSellOrder" );
    RemoveParser( "OldPriceLimit" );
    RemoveParser( "NewPriceLimit" );
    RemoveParser( "UseOrderRange" );
    RemoveParser( "DeleteOldTransactions" );
    RemoveParser( "HistoryUpdateTime" );
    RemoveParser( "SalesTax" );
    RemoveParser( "OrderModificationDelay" );
    RemoveParser( "HistoryDays" );
    RemoveParser( "UseMarginTrading" );
    RemoveParser( "OrderCancelDelay" );
    RemoveParser( "ContractFees" );
    RemoveParser( "ContractExpiredCleanupDays" );
    RemoveParser( "ContractCourierAutoFail" );
    RemoveParser( "InsuranceUseMaterialIndex" );
    RemoveParser( "InsuranceIndexDays" );
    RemoveParser( "InsuranceIndexMinTrades" );
    RemoveParser( "InsuranceDefaultPayout" );
    RemoveParser( "InsuranceMultiplierT1" );
    RemoveParser( "InsuranceMultiplierT2" );
    RemoveParser( "InsuranceMultiplierT3" );

    return result;
}

bool EVEServerConfig::ProcessMarketSim(const TiXmlElement* ele)
{
    AddValueParser( "Enabled",                      marketSim.Enabled );
    AddValueParser( "Seed",                         marketSim.Seed );
    AddValueParser( "EconomyScale",                 marketSim.EconomyScale );
    AddValueParser( "Hubs",                         marketSim.Hubs );
    AddValueParser( "RegionalNodes",                marketSim.RegionalNodes );
    AddValueParser( "BudgetMsPerSecond",            marketSim.BudgetMsPerSecond );
    AddValueParser( "TickMinutes",                  marketSim.TickMinutes );
    AddValueParser( "HotCellMinutes",               marketSim.HotCellMinutes );
    AddValueParser( "ColdCellMinutes",              marketSim.ColdCellMinutes );
    AddValueParser( "HotCellShare",                 marketSim.HotCellShare );
    AddValueParser( "QuoteLevels",                  marketSim.QuoteLevels );
    AddValueParser( "MaxOrdersPerCellSide",         marketSim.MaxOrdersPerCellSide );
    AddValueParser( "SimMaxOrders",                 marketSim.SimMaxOrders );
    AddValueParser( "QuoteDepthDays",               marketSim.QuoteDepthDays );
    AddValueParser( "RequoteThreshold",             marketSim.RequoteThreshold );
    AddValueParser( "MinHalfSpread",                marketSim.MinHalfSpread );
    AddValueParser( "MaxPriceDeviation",            marketSim.MaxPriceDeviation );
    AddValueParser( "FairValueHalfLifeDays",        marketSim.FairValueHalfLifeDays );
    AddValueParser( "MarketNoise",                  marketSim.MarketNoise );
    AddValueParser( "TransientNoise",               marketSim.TransientNoise );
    AddValueParser( "PlayerTradeWeight",            marketSim.PlayerTradeWeight );
    AddValueParser( "BuyBand",                      marketSim.BuyBand );
    AddValueParser( "SellBand",                     marketSim.SellBand );
    AddValueParser( "IntakeShareCounterparty",      marketSim.IntakeShareCounterparty );
    AddValueParser( "MaxIskToCounterpartyPerDay",   marketSim.MaxIskToCounterpartyPerDay );
    AddValueParser( "IntakeDays",                   marketSim.IntakeDays );
    AddValueParser( "SimPaysFees",                  marketSim.SimPaysFees );
    AddValueParser( "SimRecordNpcSide",             marketSim.SimRecordNpcSide );
    AddValueParser( "SimBrokerFee",                 marketSim.SimBrokerFee );
    AddValueParser( "EnableFacilities",             marketSim.EnableFacilities );
    AddValueParser( "UseRealAssemblyLines",         marketSim.UseRealAssemblyLines );
    AddValueParser( "MaxLineShare",                 marketSim.MaxLineShare );
    AddValueParser( "MinFreeLinesPerStation",       marketSim.MinFreeLinesPerStation );
    AddValueParser( "RefineryM3PerHour",            marketSim.RefineryM3PerHour );
    AddValueParser( "MoneyDaysTarget",              marketSim.MoneyDaysTarget );
    AddValueParser( "ControllerKp",                 marketSim.ControllerKp );
    AddValueParser( "ControllerKi",                 marketSim.ControllerKi );
    AddValueParser( "SupplyFactor",                 marketSim.SupplyFactor );
    AddValueParser( "VendorStations",               marketSim.VendorStations );
    AddValueParser( "VendorStock",                  marketSim.VendorStock );
    AddValueParser( "HistoryBackfillDays",          marketSim.HistoryBackfillDays );
    AddValueParser( "MaxCatchUpHours",              marketSim.MaxCatchUpHours );
    AddValueParser( "FlushSeconds",                 marketSim.FlushSeconds );
    AddValueParser( "PauseOnAuditResidual",         marketSim.PauseOnAuditResidual );
    AddValueParser( "Actors",                       marketSim.Actors );
    AddValueParser( "KillmailCoverage",             marketSim.KillmailCoverage );
    AddValueParser( "PodLossRate",                  marketSim.PodLossRate );
    AddValueParser( "InternalSpreadShare",          marketSim.InternalSpreadShare );
    AddValueParser( "MinMargin",                    marketSim.MinMargin );
    AddValueParser( "ProductionSlack",              marketSim.ProductionSlack );
    AddValueParser( "MiningSlack",                  marketSim.MiningSlack );
    AddValueParser( "MiningM3PerHullDay",           marketSim.MiningM3PerHullDay );
    AddValueParser( "MaxSimJobHours",               marketSim.MaxSimJobHours );
    AddValueParser( "LineInstallCost",              marketSim.LineInstallCost );
    AddValueParser( "LineCostPerHour",              marketSim.LineCostPerHour );
    AddValueParser( "EnableT2",                     marketSim.EnableT2 );
    AddValueParser( "EnableMoonPI",                 marketSim.EnableMoonPI );
    AddValueParser( "InventBufferDays",             marketSim.InventBufferDays );
    AddValueParser( "HarvestSlack",                 marketSim.HarvestSlack );
    AddValueParser( "PosFuelPerArrayHour",          marketSim.PosFuelPerArrayHour );
    AddValueParser( "MoonHarvestPerHour",           marketSim.MoonHarvestPerHour );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "Enabled" );
    RemoveParser( "Seed" );
    RemoveParser( "EconomyScale" );
    RemoveParser( "Hubs" );
    RemoveParser( "RegionalNodes" );
    RemoveParser( "BudgetMsPerSecond" );
    RemoveParser( "TickMinutes" );
    RemoveParser( "HotCellMinutes" );
    RemoveParser( "ColdCellMinutes" );
    RemoveParser( "HotCellShare" );
    RemoveParser( "QuoteLevels" );
    RemoveParser( "MaxOrdersPerCellSide" );
    RemoveParser( "SimMaxOrders" );
    RemoveParser( "QuoteDepthDays" );
    RemoveParser( "RequoteThreshold" );
    RemoveParser( "MinHalfSpread" );
    RemoveParser( "MaxPriceDeviation" );
    RemoveParser( "FairValueHalfLifeDays" );
    RemoveParser( "MarketNoise" );
    RemoveParser( "TransientNoise" );
    RemoveParser( "PlayerTradeWeight" );
    RemoveParser( "BuyBand" );
    RemoveParser( "SellBand" );
    RemoveParser( "IntakeShareCounterparty" );
    RemoveParser( "MaxIskToCounterpartyPerDay" );
    RemoveParser( "IntakeDays" );
    RemoveParser( "SimPaysFees" );
    RemoveParser( "SimRecordNpcSide" );
    RemoveParser( "SimBrokerFee" );
    RemoveParser( "EnableFacilities" );
    RemoveParser( "UseRealAssemblyLines" );
    RemoveParser( "MaxLineShare" );
    RemoveParser( "MinFreeLinesPerStation" );
    RemoveParser( "RefineryM3PerHour" );
    RemoveParser( "MoneyDaysTarget" );
    RemoveParser( "ControllerKp" );
    RemoveParser( "ControllerKi" );
    RemoveParser( "SupplyFactor" );
    RemoveParser( "VendorStations" );
    RemoveParser( "VendorStock" );
    RemoveParser( "HistoryBackfillDays" );
    RemoveParser( "MaxCatchUpHours" );
    RemoveParser( "FlushSeconds" );
    RemoveParser( "PauseOnAuditResidual" );
    RemoveParser( "Actors" );
    RemoveParser( "KillmailCoverage" );
    RemoveParser( "PodLossRate" );
    RemoveParser( "InternalSpreadShare" );
    RemoveParser( "MinMargin" );
    RemoveParser( "ProductionSlack" );
    RemoveParser( "MiningSlack" );
    RemoveParser( "MiningM3PerHullDay" );
    RemoveParser( "MaxSimJobHours" );
    RemoveParser( "LineInstallCost" );
    RemoveParser( "LineCostPerHour" );
    RemoveParser( "EnableT2" );
    RemoveParser( "EnableMoonPI" );
    RemoveParser( "InventBufferDays" );
    RemoveParser( "HarvestSlack" );
    RemoveParser( "PosFuelPerArrayHour" );
    RemoveParser( "MoonHarvestPerHour" );

    return result;
}

bool EVEServerConfig::ProcessBPTimes(const TiXmlElement* ele)
{
    AddValueParser( "AutoEvent",        ram.AutoEvent);
    AddValueParser( "ProdTime",         ram.ProdTime);
    AddValueParser( "CopyTime",         ram.CopyTime);
    AddValueParser( "WasteMod",         ram.WasteMod);
    AddValueParser( "MatMod",           ram.MatMod);
    AddValueParser( "ResME",            ram.ResME);
    AddValueParser( "ResPE",            ram.ResPE);
    AddValueParser( "ReTime",           ram.ReTime);
    AddValueParser( "InventTime",       ram.InventTime);
    AddValueParser( "InventChance",     ram.InventChance);
    AddValueParser( "ReChance",         ram.ReChance);

    const bool result = ParseElementChildren( ele );

    RemoveParser( "AutoEvent" );
    RemoveParser( "ProdTime" );
    RemoveParser( "CopyTime" );
    RemoveParser( "WasteMod" );
    RemoveParser( "MatMod" );
    RemoveParser( "ResME" );
    RemoveParser( "ResPE" );
    RemoveParser( "ReTime" );
    RemoveParser( "InventTime" );
    RemoveParser( "InventChance" );
    RemoveParser( "ReChance" );

    return result;
}

bool EVEServerConfig::ProcessAccount( const TiXmlElement* ele )
{
    AddValueParser( "autoAccountRole",  account.autoAccountRole );
    AddValueParser( "loginMessage",     account.loginMessage );
    AddValueParser( "redeemEnabled",    account.redeemEnabled );
    AddValueParser( "reverseRedeemTypes", account.reverseRedeemTypes );
    AddValueParser( "cspaEnabled",      account.cspaEnabled );
    AddValueParser( "cspaDefaultCost",  account.cspaDefaultCost );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "autoAccountRole" );
    RemoveParser( "loginMessage" );
    RemoveParser( "redeemEnabled" );
    RemoveParser( "reverseRedeemTypes" );
    RemoveParser( "cspaEnabled" );
    RemoveParser( "cspaDefaultCost" );

    return result;
}

bool EVEServerConfig::ProcessCharacter( const TiXmlElement* ele )
{
    AddValueParser( "startBalance",     character.startBalance );
    AddValueParser( "startAurBalance",  character.startAurBalance );  // added config entry and implemented  -allan 01/10/14
    AddValueParser( "startStation",     character.startStation );
    AddValueParser( "startSecRating",   character.startSecRating );
    AddValueParser( "startCorporation", character.startCorporation );
    AddValueParser( "terminationDelay", character.terminationDelay );
    AddValueParser( "statMultiplier",   character.statMultiplier );
    AddValueParser( "allow3edChar",     character.allow3edChar );
    AddValueParser( "requirePausedTraining",        character.requirePausedTraining );
    AddValueParser( "jumpCloneCost",                character.jumpCloneCost );
    AddValueParser( "jumpCloneMinStanding",         character.jumpCloneMinStanding );
    AddValueParser( "jumpCloneCooldownHours",       character.jumpCloneCooldownHours );
    AddValueParser( "cloneSkillLoss",               character.cloneSkillLoss );
    AddValueParser( "cloneSkillLossPercent",        character.cloneSkillLossPercent );
    AddValueParser( "cloneSkillLossCap",            character.cloneSkillLossCap );
    AddValueParser( "boosterFallbackDurationHours", character.boosterFallbackDurationHours );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "startBalance" );
    RemoveParser( "startAurBalance" );
    RemoveParser( "startStation" );
    RemoveParser( "startSecRating" );
    RemoveParser( "startCorporation" );
    RemoveParser( "terminationDelay" );
    RemoveParser( "statMultiplier" );
    RemoveParser( "allow3edChar" );
    RemoveParser( "requirePausedTraining" );
    RemoveParser( "jumpCloneCost" );
    RemoveParser( "jumpCloneMinStanding" );
    RemoveParser( "jumpCloneCooldownHours" );
    RemoveParser( "cloneSkillLoss" );
    RemoveParser( "cloneSkillLossPercent" );
    RemoveParser( "cloneSkillLossCap" );
    RemoveParser( "boosterFallbackDurationHours" );

    return result;
}

bool EVEServerConfig::ProcessNPC( const TiXmlElement* ele )
{
    AddValueParser( "IdleWander",               npc.IdleWander );
    AddValueParser( "WarpOut",                  npc.WarpOut );
    AddValueParser( "WarpFollowChance",         npc.WarpFollowChance );
    AddValueParser( "ThreatRadius",             npc.ThreatRadius );
    AddValueParser( "RoamingSpawns",            npc.RoamingSpawns );
    AddValueParser( "StaticSpawns",             npc.StaticSpawns );
    AddValueParser( "RoamingTimer",             npc.RoamingTimer );
    AddValueParser( "StaticTimer",              npc.StaticTimer );
    AddValueParser( "RespawnTimer",             npc.RespawnTimer );
    AddValueParser( "RatFaction",               npc.RatFaction );
    AddValueParser( "TargetPod",                npc.TargetPod );
    AddValueParser( "TargetPodSec",             npc.TargetPodSec );
    AddValueParser( "UseDamageMultiplier",      npc.UseDamageMultiplier );
    AddValueParser( "LootDropChance",           npc.LootDropChance );
    AddValueParser( "DefenderMissileChance",    npc.DefenderMissileChance );
    AddValueParser( "UseEwar",                  npc.UseEwar );
    AddValueParser( "GroupAggro",               npc.GroupAggro );
    AddValueParser( "DroneAggroChance",         npc.DroneAggroChance );
    AddValueParser( "LeashRange",               npc.LeashRange );
    AddValueParser( "TargetSwitchTime",         npc.TargetSwitchTime );
    // npc ai foundations (npc ai plan phases 0-1)
    AddValueParser( "SleepEmptyBubbles",        npc.SleepEmptyBubbles );
    AddValueParser( "AuditOnStartup",           npc.AuditOnStartup );
    AddValueParser( "RequireWeaponEffects",     npc.RequireWeaponEffects );
    AddValueParser( "RngSeed",                  npc.RngSeed );
    AddValueParser( "RespawnJitter",            npc.RespawnJitter );
    // npc ai phase 2 (npc ai plan 4.2-4.4, 5.4-5.6, 5.8)
    AddValueParser( "AcquireRange",             npc.AcquireRange );
    AddValueParser( "AcquireDrones",            npc.AcquireDrones );
    AddValueParser( "DroneRetaliation",         npc.DroneRetaliation );
    AddValueParser( "DroneAggroInterval",       npc.DroneAggroInterval );
    AddValueParser( "UseAttackDelay",           npc.UseAttackDelay );
    AddValueParser( "AttackDelayScale",         npc.AttackDelayScale );
    AddValueParser( "UseReactionFactor",        npc.UseReactionFactor );
    AddValueParser( "ChaseSigBloom",            npc.ChaseSigBloom );
    AddValueParser( "LeashSoftScale",           npc.LeashSoftScale );
    AddValueParser( "LeashHardScale",           npc.LeashHardScale );
    AddValueParser( "LeashHardMode",            npc.LeashHardMode );
    AddValueParser( "EwarUsageRoll",            npc.EwarUsageRoll );
    AddValueParser( "PassiveAggroRange",        npc.PassiveAggroRange );
    AddValueParser( "GroupAggroMemory",         npc.GroupAggroMemory );
    AddValueParser( "WaveWeakestChance",        npc.WaveWeakestChance );
    AddValueParser( "HaulerFleeDelay",          npc.HaulerFleeDelay );
    AddValueParser( "CommanderFlee",            npc.CommanderFlee );
    AddValueParser( "RespectInterdiction",      npc.RespectInterdiction );
    // npc ai phase 3 (npc ai plan 4.6, 4.7, 5.7-5.9)
    AddValueParser( "L1EvalInterval",           npc.L1EvalInterval );
    AddValueParser( "L1SwitchMargin",           npc.L1SwitchMargin );
    AddValueParser( "L1ThreatDecay",            npc.L1ThreatDecay );
    AddValueParser( "L1ThreatDecayInterval",    npc.L1ThreatDecayInterval );
    AddValueParser( "L1ThreatShare",            npc.L1ThreatShare );
    AddValueParser( "L1EffectThreat",           npc.L1EffectThreat );
    AddValueParser( "L1RepairThreat",           npc.L1RepairThreat );
    AddValueParser( "L1DroneControllerShare",   npc.L1DroneControllerShare );
    AddValueParser( "L1NewcomerThreat",         npc.L1NewcomerThreat );
    AddValueParser( "L1NewcomerMemory",         npc.L1NewcomerMemory );
    AddValueParser( "RemoteEcmJamTime",         npc.RemoteEcmJamTime );
    // npc ai phase 5 (npc ai plan phase 5, 6.7)
    AddValueParser( "SoftLeashCooldown",        npc.SoftLeashCooldown );
    AddValueParser( "LinkAlertInterval",        npc.LinkAlertInterval );
    AddValueParser( "SleeperCapitalReinforcements", npc.SleeperCapitalReinforcements );
    AddValueParser( "SleeperReinforcementTypes",    npc.SleeperReinforcementTypes );
    AddValueParser( "TelegraphJoin",            npc.TelegraphJoin );
    AddValueParser( "WarpOutVanishChance",      npc.WarpOutVanishChance );
    AddValueParser( "WanderRepickMin",          npc.WanderRepickMin );
    AddValueParser( "WanderRepickMax",          npc.WanderRepickMax );
    AddValueParser( "BeltSpawnCap",             npc.BeltSpawnCap );
    AddValueParser( "LodFarInterval",           npc.LodFarInterval );
    AddValueParser( "LodEmptySystem",           npc.LodEmptySystem );
    AddValueParser( "TickBudget",               npc.TickBudget );
    AddValueParser( "StatsInterval",            npc.StatsInterval );
    AddValueParser( "Difficulty",               npc.Difficulty );
    AddValueParser( "DifficultyDamage",         npc.DifficultyDamage );
    AddValueParser( "DifficultyLockTime",       npc.DifficultyLockTime );
    AddValueParser( "DifficultyReactionTime",   npc.DifficultyReactionTime );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "IdleWander" );
    RemoveParser( "WarpOut" );
    RemoveParser( "WarpFollowChance" );
    RemoveParser( "ThreatRadius" );
    RemoveParser( "RoamingSpawns" );
    RemoveParser( "StaticSpawns" );
    RemoveParser( "RoamingTimer" );
    RemoveParser( "StaticTimer" );
    RemoveParser( "RespawnTimer" );
    RemoveParser( "RatFaction" );
    RemoveParser( "TargetPod" );
    RemoveParser( "TargetPodSec" );
    RemoveParser( "UseDamageMultiplier" );
    RemoveParser( "LootDropChance" );
    RemoveParser( "DefenderMissileChance" );
    RemoveParser( "UseEwar" );
    RemoveParser( "GroupAggro" );
    RemoveParser( "DroneAggroChance" );
    RemoveParser( "LeashRange" );
    RemoveParser( "TargetSwitchTime" );
    RemoveParser( "SleepEmptyBubbles" );
    RemoveParser( "AuditOnStartup" );
    RemoveParser( "RequireWeaponEffects" );
    RemoveParser( "RngSeed" );
    RemoveParser( "RespawnJitter" );
    RemoveParser( "AcquireRange" );
    RemoveParser( "AcquireDrones" );
    RemoveParser( "DroneRetaliation" );
    RemoveParser( "DroneAggroInterval" );
    RemoveParser( "UseAttackDelay" );
    RemoveParser( "AttackDelayScale" );
    RemoveParser( "UseReactionFactor" );
    RemoveParser( "ChaseSigBloom" );
    RemoveParser( "LeashSoftScale" );
    RemoveParser( "LeashHardScale" );
    RemoveParser( "LeashHardMode" );
    RemoveParser( "EwarUsageRoll" );
    RemoveParser( "PassiveAggroRange" );
    RemoveParser( "GroupAggroMemory" );
    RemoveParser( "WaveWeakestChance" );
    RemoveParser( "HaulerFleeDelay" );
    RemoveParser( "CommanderFlee" );
    RemoveParser( "RespectInterdiction" );
    RemoveParser( "L1EvalInterval" );
    RemoveParser( "L1SwitchMargin" );
    RemoveParser( "L1ThreatDecay" );
    RemoveParser( "L1ThreatDecayInterval" );
    RemoveParser( "L1ThreatShare" );
    RemoveParser( "L1EffectThreat" );
    RemoveParser( "L1RepairThreat" );
    RemoveParser( "L1DroneControllerShare" );
    RemoveParser( "L1NewcomerThreat" );
    RemoveParser( "L1NewcomerMemory" );
    RemoveParser( "RemoteEcmJamTime" );
    RemoveParser( "SoftLeashCooldown" );
    RemoveParser( "LinkAlertInterval" );
    RemoveParser( "SleeperCapitalReinforcements" );
    RemoveParser( "SleeperReinforcementTypes" );
    RemoveParser( "TelegraphJoin" );
    RemoveParser( "WarpOutVanishChance" );
    RemoveParser( "WanderRepickMin" );
    RemoveParser( "WanderRepickMax" );
    RemoveParser( "BeltSpawnCap" );
    RemoveParser( "LodFarInterval" );
    RemoveParser( "LodEmptySystem" );
    RemoveParser( "TickBudget" );
    RemoveParser( "StatsInterval" );
    RemoveParser( "Difficulty" );
    RemoveParser( "DifficultyDamage" );
    RemoveParser( "DifficultyLockTime" );
    RemoveParser( "DifficultyReactionTime" );

    return result;
}

bool EVEServerConfig::ProcessDatabase( const TiXmlElement* ele )
{
    AddValueParser( "host",             database.host );
    AddValueParser( "port",             database.port );
    AddValueParser( "username",         database.username );
    AddValueParser( "password",         database.password );
    AddValueParser( "db",               database.db );
    AddValueParser( "compress",         database.compress );
    AddValueParser( "ssl",              database.ssl );
    AddValueParser( "useSocket",        database.useSocket );
    AddValueParser( "autoReconnect",    database.autoReconnect );
    AddValueParser( "dbTimeout",        database.dbTimeout );
    AddValueParser( "pingTime",         database.pingTime );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "host" );
    RemoveParser( "port" );
    RemoveParser( "username" );
    RemoveParser( "password" );
    RemoveParser( "db" );
    RemoveParser( "compress" );
    RemoveParser( "ssl" );
    RemoveParser( "useSocket" );
    RemoveParser( "autoReconnect" );
    RemoveParser( "dbTimeout" );
    RemoveParser( "pingTime" );

    return result;
}

bool EVEServerConfig::ProcessFiles( const TiXmlElement* ele )
{
    AddValueParser( "logDir",           files.logDir );
    AddValueParser( "logSettings",      files.logSettings );
    AddValueParser( "cacheDir",         files.cacheDir );
    AddValueParser( "imageDir",         files.imageDir );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "logDir" );
    RemoveParser( "logSettings" );
    RemoveParser( "cacheDir" );
    RemoveParser( "imageDir" );

    return result;
}

bool EVEServerConfig::ProcessNet( const TiXmlElement* ele )
{
    AddValueParser( "port",             net.port );
    AddValueParser( "imageServerPort",  net.imageServerPort);
    AddValueParser( "imageServer",      net.imageServer);

    const bool result = ParseElementChildren( ele );

    RemoveParser( "port" );
    RemoveParser( "imageServerPort" );
    RemoveParser( "imageServer" );

    return result;
}

bool EVEServerConfig::ProcessThreads( const TiXmlElement* ele )
{
    AddValueParser( "ConsoleThreads",       threads.ConsoleThreads);
    AddValueParser( "DatabaseThreads",      threads.DatabaseThreads);
    AddValueParser( "ImageServerThreads",   threads.ImageServerThreads);
    AddValueParser( "NetworkThreads",       threads.NetworkThreads );
    AddValueParser( "WorldThreads",         threads.WorldThreads);

    const bool result = ParseElementChildren( ele );

    RemoveParser( "ConsoleThreads" );
    RemoveParser( "DatabaseThreads" );
    RemoveParser( "ImageServerThreads" );
    RemoveParser( "NetworkThreads" );
    RemoveParser( "WorldThreads" );

    return result;
}

bool EVEServerConfig::ProcessCosmic( const TiXmlElement* ele )
{
    AddValueParser( "PIEnabled",            cosmic.PIEnabled );
    AddValueParser( "AnomalyEnabled",       cosmic.AnomalyEnabled );
    AddValueParser( "DungeonEnabled",       cosmic.DungeonEnabled );
    AddValueParser( "BeltEnabled",          cosmic.BeltEnabled );
    AddValueParser( "BeltRespawn",          cosmic.BeltRespawn );
    AddValueParser( "BeltGrowth",           cosmic.BeltGrowth );
    AddValueParser( "roidRadiusMultiplier", cosmic.roidRadiusMultiplier );
    AddValueParser( "WormHoleEnabled",      cosmic.WormHoleEnabled );
    AddValueParser( "CiviliansEnabled",     cosmic.CiviliansEnabled);
    AddValueParser( "BumpEnabled",          cosmic.BumpEnabled );
    AddValueParser( "PIRestrictTradeHubs", cosmic.PIRestrictTradeHubs );
    AddValueParser( "PISpawnAllCustomsOffices", cosmic.PISpawnAllCustomsOffices );
    AddValueParser( "PICONCORDTaxMultiplier", cosmic.PICONCORDTaxMultiplier );
    AddValueParser( "PIInterbusTaxMultiplier", cosmic.PIInterbusTaxMultiplier );
    AddValueParser( "PIResourceSeed", cosmic.PIResourceSeed );
    AddValueParser( "PIResourceScale", cosmic.PIResourceScale );
    AddValueParser( "PIResourceSpectrum", cosmic.PIResourceSpectrum );
    AddValueParser( "PIRichnessHighsec", cosmic.PIRichnessHighsec );
    AddValueParser( "PIRichnessLowsec", cosmic.PIRichnessLowsec );
    AddValueParser( "PIRichnessNullsec", cosmic.PIRichnessNullsec );
    AddValueParser( "PIDepletionEnabled", cosmic.PIDepletionEnabled );
    AddValueParser( "PIDepletionPerUnit", cosmic.PIDepletionPerUnit );
    AddValueParser( "PIDepletionMaxFraction", cosmic.PIDepletionMaxFraction );
    AddValueParser( "PIDepletionRadius", cosmic.PIDepletionRadius );
    AddValueParser( "PIDepletionRegenHours", cosmic.PIDepletionRegenHours );
    AddValueParser( "PIGantryMaxDistanceKm", cosmic.PIGantryMaxDistanceKm );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "PIEnabled" );
    RemoveParser( "AnomalyEnabled" );
    RemoveParser( "DungeonEnabled" );
    RemoveParser( "BeltEnabled" );
    RemoveParser( "BeltRespawn" );
    RemoveParser( "BeltGrowth" );
    RemoveParser( "roidRadiusMultiplier" );
    RemoveParser( "WormHoleEnabled" );
    RemoveParser( "CiviliansEnabled" );
    RemoveParser( "BumpEnabled" );
    RemoveParser( "PIRestrictTradeHubs" );
    RemoveParser( "PISpawnAllCustomsOffices" );
    RemoveParser( "PICONCORDTaxMultiplier" );
    RemoveParser( "PIInterbusTaxMultiplier" );
    RemoveParser( "PIResourceSeed" );
    RemoveParser( "PIResourceScale" );
    RemoveParser( "PIResourceSpectrum" );
    RemoveParser( "PIRichnessHighsec" );
    RemoveParser( "PIRichnessLowsec" );
    RemoveParser( "PIRichnessNullsec" );
    RemoveParser( "PIDepletionEnabled" );
    RemoveParser( "PIDepletionPerUnit" );
    RemoveParser( "PIDepletionMaxFraction" );
    RemoveParser( "PIDepletionRadius" );
    RemoveParser( "PIDepletionRegenHours" );
    RemoveParser( "PIGantryMaxDistanceKm" );

    return result;
}

bool EVEServerConfig::ProcessDungeon( const TiXmlElement* ele )
{
    AddValueParser( "AnomalyLifetime",      dungeon.AnomalyLifetime );
    AddValueParser( "SignatureLifetime",    dungeon.SignatureLifetime );
    AddValueParser( "CompletedDespawn",     dungeon.CompletedDespawn );
    AddValueParser( "RespawnDelay",         dungeon.RespawnDelay );
    AddValueParser( "EscalationChance",     dungeon.EscalationChance );
    AddValueParser( "EscalationExpiry",     dungeon.EscalationExpiry );
    AddValueParser( "EscalationShareFleet", dungeon.EscalationShareFleet );
    AddValueParser( "GateKeysRequired",     dungeon.GateKeysRequired );
    AddValueParser( "TriggerMessages",      dungeon.TriggerMessages );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "AnomalyLifetime" );
    RemoveParser( "SignatureLifetime" );
    RemoveParser( "CompletedDespawn" );
    RemoveParser( "RespawnDelay" );
    RemoveParser( "EscalationChance" );
    RemoveParser( "EscalationExpiry" );
    RemoveParser( "EscalationShareFleet" );
    RemoveParser( "GateKeysRequired" );
    RemoveParser( "TriggerMessages" );

    return result;
}

bool EVEServerConfig::ProcessAgents( const TiXmlElement* ele )
{
    AddValueParser( "researchDatacoreRP",       agents.researchDatacoreRP );
    AddValueParser( "researchDatacoreISK",      agents.researchDatacoreISK );
    AddValueParser( "researchFieldMultipliers", agents.researchFieldMultipliers );
    AddValueParser( "locatorEnabled",           agents.locatorEnabled );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "researchDatacoreRP" );
    RemoveParser( "researchDatacoreISK" );
    RemoveParser( "researchFieldMultipliers" );
    RemoveParser( "locatorEnabled" );

    return result;
}

bool EVEServerConfig::ProcessMissions( const TiXmlElement* ele )
{
    AddValueParser( "OfferExpiryHours",     missions.OfferExpiryHours );
    AddValueParser( "AcceptedExpiryHours",  missions.AcceptedExpiryHours );
    AddValueParser( "DeclinePenaltyHours",  missions.DeclinePenaltyHours );
    AddValueParser( "UseDivisionTypes",     missions.UseDivisionTypes );
    AddValueParser( "FallbackToCourier",    missions.FallbackToCourier );
    AddValueParser( "BaseISK1",             missions.BaseISK[0] );
    AddValueParser( "BaseISK2",             missions.BaseISK[1] );
    AddValueParser( "BaseISK3",             missions.BaseISK[2] );
    AddValueParser( "BaseISK4",             missions.BaseISK[3] );
    AddValueParser( "BaseISK5",             missions.BaseISK[4] );
    AddValueParser( "BonusISK1",            missions.BonusISK[0] );
    AddValueParser( "BonusISK2",            missions.BonusISK[1] );
    AddValueParser( "BonusISK3",            missions.BonusISK[2] );
    AddValueParser( "BonusISK4",            missions.BonusISK[3] );
    AddValueParser( "BonusISK5",            missions.BonusISK[4] );
    AddValueParser( "BonusMinutes1",        missions.BonusMinutes[0] );
    AddValueParser( "BonusMinutes2",        missions.BonusMinutes[1] );
    AddValueParser( "BonusMinutes3",        missions.BonusMinutes[2] );
    AddValueParser( "BonusMinutes4",        missions.BonusMinutes[3] );
    AddValueParser( "BonusMinutes5",        missions.BonusMinutes[4] );
    AddValueParser( "BaseLP1",              missions.BaseLP[0] );
    AddValueParser( "BaseLP2",              missions.BaseLP[1] );
    AddValueParser( "BaseLP3",              missions.BaseLP[2] );
    AddValueParser( "BaseLP4",              missions.BaseLP[3] );
    AddValueParser( "BaseLP5",              missions.BaseLP[4] );
    AddValueParser( "IskSecurityScaling",   missions.IskSecurityScaling );
    AddValueParser( "FleetShare",           missions.FleetShare );
    AddValueParser( "FleetShareMax",        missions.FleetShareMax );
    AddValueParser( "FleetShareSameSystem", missions.FleetShareSameSystem );
    AddValueParser( "DungeonDespawnMinutes", missions.DungeonDespawnMinutes );
    AddValueParser( "StorylineEvery",       missions.StorylineEvery );
    AddValueParser( "GateRestrictions",     missions.GateRestrictions );

    const bool result = ParseElementChildren( ele );

    const char* names[] = { "OfferExpiryHours", "AcceptedExpiryHours", "DeclinePenaltyHours", "UseDivisionTypes", "FallbackToCourier",
        "BaseISK1", "BaseISK2", "BaseISK3", "BaseISK4", "BaseISK5", "BonusISK1", "BonusISK2", "BonusISK3", "BonusISK4", "BonusISK5",
        "BonusMinutes1", "BonusMinutes2", "BonusMinutes3", "BonusMinutes4", "BonusMinutes5", "BaseLP1", "BaseLP2", "BaseLP3", "BaseLP4",
        "BaseLP5", "IskSecurityScaling", "FleetShare", "FleetShareMax", "FleetShareSameSystem", "DungeonDespawnMinutes", "StorylineEvery",
        "GateRestrictions" };
    for (auto cur : names)
        RemoveParser( cur );

    return result;
}

bool EVEServerConfig::ProcessExploring ( const TiXmlElement* ele ) {
    AddValueParser( "Radar",                exploring.Radar );
    AddValueParser( "Ladar",                exploring.Ladar );
    AddValueParser( "Unrated",              exploring.Unrated );
    AddValueParser( "Complex",              exploring.Complex );
    AddValueParser( "Gravametric",          exploring.Gravametric );
    AddValueParser( "Magnetometric",        exploring.Magnetometric );
    AddValueParser( "DScanCooldown",        exploring.DScanCooldown );
    AddValueParser( "SurveyProbeConeAngle", exploring.SurveyProbeConeAngle );
    AddValueParser( "ProbeStrengthScale",   exploring.ProbeStrengthScale );
    AddValueParser( "ProbeShipSigDivisor",  exploring.ProbeShipSigDivisor );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "Radar" );
    RemoveParser( "Ladar" );
    RemoveParser( "Unrated" );
    RemoveParser( "Complex" );
    RemoveParser( "Gravametric" );
    RemoveParser( "Magnetometric" );
    RemoveParser( "DScanCooldown" );
    RemoveParser( "SurveyProbeConeAngle" );
    RemoveParser( "ProbeStrengthScale" );
    RemoveParser( "ProbeShipSigDivisor" );

    return result;
}

bool EVEServerConfig::ProcessChat(const TiXmlElement* ele)
{
    AddValueParser( "EnableFleetChat",      chat.EnableFleetChat );
    AddValueParser( "EnableWingChat",       chat.EnableWingChat );
    AddValueParser( "EnableSquadChat",      chat.EnableSquadChat );
    AddValueParser( "EnableVoiceChat",      chat.EnableVoiceChat );
    AddValueParser( "EnforceRookieInHelp",  chat.EnforceRookieInHelp );
    AddValueParser( "EnforceChannelAccess", chat.EnforceChannelAccess );
    AddValueParser( "ContactStatusMutual",  chat.ContactStatusMutual );
    AddValueParser( "UserChannelCleanup",   chat.UserChannelCleanup );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "EnableFleetChat" );
    RemoveParser( "EnableWingChat" );
    RemoveParser( "EnableSquadChat" );
    RemoveParser( "EnableVoiceChat" );
    RemoveParser( "EnforceRookieInHelp" );
    RemoveParser( "EnforceChannelAccess" );
    RemoveParser( "ContactStatusMutual" );
    RemoveParser( "UserChannelCleanup" );

    return result;
}

bool EVEServerConfig::ProcessStandings(const TiXmlElement* ele)
{
    AddValueParser( "MissionBonus",                     standings.MissionBonus );
    AddValueParser( "MissionFailure",                   standings.MissionFailure );
    AddValueParser( "MissionDeclined",                  standings.MissionDeclined );
    AddValueParser( "MissionCompleted",                 standings.MissionCompleted );
    AddValueParser( "MissionOfferExpired",              standings.MissionOfferExpired );
    AddValueParser( "MissionFailedRollback",            standings.MissionFailedRollback );
    AddValueParser( "ImportantMissionBonus",            standings.ImportantMissionBonus );
    AddValueParser( "BaseMissionMultiplier",            standings.BaseMissionMultiplier );
    AddValueParser( "FleetMissionMultiplier",           standings.FleetMissionMultiplier );
    AddValueParser( "Agent2CharMissionMultiplier",      standings.Agent2CharMissionMultiplier );
    AddValueParser( "ACorp2CharMissionMultiplier",      standings.ACorp2CharMissionMultiplier );
    AddValueParser( "Agent2PCorpMissionMultiplier",     standings.Agent2PCorpMissionMultiplier );
    AddValueParser( "ACorp2PCorpMissionMultiplier",     standings.ACorp2PCorpMissionMultiplier );
    AddValueParser( "AFaction2CharMissionMultiplier",   standings.AFaction2CharMissionMultiplier );
    AddValueParser( "AFaction2PCorpMissionMultiplier",  standings.AFaction2PCorpMissionMultiplier );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "MissionBonus" );
    RemoveParser( "MissionFailure" );
    RemoveParser( "MissionDeclined" );
    RemoveParser( "MissionCompleted" );
    RemoveParser( "MissionOfferExpired" );
    RemoveParser( "MissionFailedRollback" );
    RemoveParser( "ImportantMissionBonus" );
    RemoveParser( "BaseMissionMultiplier" );
    RemoveParser( "FleetMissionMultiplier" );
    RemoveParser( "Agent2CharMissionMultiplier" );
    RemoveParser( "ACorp2CharMissionMultiplier" );
    RemoveParser( "Agent2PCorpMissionMultiplier" );
    RemoveParser( "ACorp2PCorpMissionMultiplier" );
    RemoveParser( "AFaction2CharMissionMultiplier" );
    RemoveParser( "AFaction2PCorpMissionMultiplier" );

    return result;
}

bool EVEServerConfig::ProcessCrime( const TiXmlElement* ele )
{
    AddValueParser( "Enabled",              crime.Enabled );
    AddValueParser( "AggFlagTime",          crime.AggFlagTime );
    AddValueParser( "CrimFlagTime",         crime.CrimFlagTime );
    AddValueParser( "WeaponFlagTime",       crime.WeaponFlagTime );
    AddValueParser( "KillRightDays",        crime.KillRightDays );
    AddValueParser( "LogoffNPCTime",        crime.LogoffNPCTime );
    AddValueParser( "LogoffWarpTime",       crime.LogoffWarpTime );
    AddValueParser( "SecGainWindow",        crime.SecGainWindow );
    AddValueParser( "SecAggressionPenalty", crime.SecAggressionPenalty );
    AddValueParser( "SecShipKillPenalty",   crime.SecShipKillPenalty );
    AddValueParser( "SecPodKillPenalty",    crime.SecPodKillPenalty );
    AddValueParser( "SecPodKillMinPenalty", crime.SecPodKillMinPenalty );
    AddValueParser( "CriminalLockdown",     crime.CriminalLockdown );
    AddValueParser( "SwitchShipWeaponsTimer", crime.SwitchShipWeaponsTimer );
    AddValueParser( "Concord",              crime.Concord );
    AddValueParser( "ConcordResponse",      crime.ConcordResponse );
    AddValueParser( "ConcordWaves",         crime.ConcordWaves );
    AddValueParser( "ConcordWaveDelay",     crime.ConcordWaveDelay );
    AddValueParser( "ConcordLinger",        crime.ConcordLinger );
    AddValueParser( "FactionPolice",        crime.FactionPolice );
    AddValueParser( "PoliceDelay",          crime.PoliceDelay );
    AddValueParser( "PoliceTimeout",        crime.PoliceTimeout );
    AddValueParser( "Sentries",             crime.Sentries );
    AddValueParser( "SentriesPerGate",      crime.SentriesPerGate );
    AddValueParser( "SentriesPerStation",   crime.SentriesPerStation );
    AddValueParser( "SentryRange",          crime.SentryRange );
    // npc ai plan phase 4
    AddValueParser( "SentryRetarget",       crime.SentryRetarget );
    AddValueParser( "SentryAggression",     crime.SentryAggression );
    AddValueParser( "ConcordScriptedTackle", crime.ConcordScriptedTackle );
    AddValueParser( "FactionNavy",          crime.FactionNavy );
    AddValueParser( "FactionNavyStanding",  crime.FactionNavyStanding );
    AddValueParser( "NpcFactionAggression", crime.NpcFactionAggression );

    const bool result = ParseElementChildren( ele );

    for (const char* name : {"Enabled", "AggFlagTime", "CrimFlagTime", "WeaponFlagTime", "KillRightDays", "LogoffNPCTime",
            "LogoffWarpTime", "SecGainWindow", "SecAggressionPenalty", "SecShipKillPenalty", "SecPodKillPenalty",
            "SecPodKillMinPenalty", "CriminalLockdown", "SwitchShipWeaponsTimer", "Concord", "ConcordResponse", "ConcordWaves", "ConcordWaveDelay",
            "ConcordLinger", "FactionPolice", "PoliceDelay", "PoliceTimeout", "Sentries", "SentriesPerGate",
            "SentriesPerStation", "SentryRange",
            "SentryRetarget", "SentryAggression", "ConcordScriptedTackle", "FactionNavy", "FactionNavyStanding", "NpcFactionAggression"})
        RemoveParser( name );

    return result;
}

bool EVEServerConfig::ProcessSov( const TiXmlElement* ele )
{
    AddValueParser( "MilitaryPointsPerBountyISK",   sov.MilitaryPointsPerBountyISK );
    AddValueParser( "IndustrialPointsPerM3",        sov.IndustrialPointsPerM3 );
    AddValueParser( "DevIndexDecayPerHour",         sov.DevIndexDecayPerHour );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "MilitaryPointsPerBountyISK" );
    RemoveParser( "IndustrialPointsPerM3" );
    RemoveParser( "DevIndexDecayPerHour" );

    return result;
}

bool EVEServerConfig::ProcessPOS( const TiXmlElement* ele )
{
    AddValueParser( "FuelRate",                 pos.FuelRate );
    AddValueParser( "RequireCharters",          pos.RequireCharters );
    AddValueParser( "HighSecStandingCheck",     pos.HighSecStandingCheck );
    AddValueParser( "ConcordAnchorRestriction", pos.ConcordAnchorRestriction );
    AddValueParser( "KeepTowerSystemsLoaded",   pos.KeepTowerSystemsLoaded );
    AddValueParser( "MaxCatchUpCycles",         pos.MaxCatchUpCycles );
    AddValueParser( "MoonGooSeed",              pos.MoonGooSeed );
    AddValueParser( "MoonGooEmptyChance",       pos.MoonGooEmptyChance );
    AddValueParser( "GunsEnabled",              pos.GunsEnabled );
    AddValueParser( "TowerSnapToMoon",          pos.TowerSnapToMoon );

    const bool result = ParseElementChildren( ele );

    for (const char* name : {"FuelRate", "RequireCharters", "HighSecStandingCheck", "ConcordAnchorRestriction",
            "KeepTowerSystemsLoaded", "MaxCatchUpCycles", "MoonGooSeed", "MoonGooEmptyChance", "GunsEnabled", "TowerSnapToMoon"})
        RemoveParser( name );

    return result;
}

bool EVEServerConfig::ProcessCorp( const TiXmlElement* ele )
{
    AddValueParser( "StasisHours",                  corp.StasisHours );
    AddValueParser( "CEOVoteMinSharePercent",       corp.CEOVoteMinSharePercent );
    AddValueParser( "CEOVoteCooldownDays",          corp.CEOVoteCooldownDays );
    AddValueParser( "SanctionDays",                 corp.SanctionDays );
    AddValueParser( "EnforceRaceLimit",             corp.EnforceRaceLimit );
    AddValueParser( "JunkReleaseFraction",          corp.JunkReleaseFraction );
    AddValueParser( "BillsEnabled",                 corp.BillsEnabled );
    AddValueParser( "CloseAllianceOnUnpaidBill",    corp.CloseAllianceOnUnpaidBill );
    AddValueParser( "BillGraceDays",                corp.BillGraceDays );

    const bool result = ParseElementChildren( ele );

    for (const char* name : {"StasisHours", "CEOVoteMinSharePercent", "CEOVoteCooldownDays", "SanctionDays", "EnforceRaceLimit",
            "JunkReleaseFraction", "BillsEnabled", "CloseAllianceOnUnpaidBill", "BillGraceDays"})
        RemoveParser( name );

    return result;
}

bool EVEServerConfig::ProcessWar( const TiXmlElement* ele )
{
    AddValueParser( "Enabled",                      war.Enabled );
    AddValueParser( "CorpWarCost",                  war.CorpWarCost );
    AddValueParser( "AllianceWarCost",              war.AllianceWarCost );
    AddValueParser( "MaxDeclaredWars",              war.MaxDeclaredWars );
    AddValueParser( "BillDays",                     war.BillDays );
    AddValueParser( "HistoryDays",                  war.HistoryDays );

    const bool result = ParseElementChildren( ele );

    for (const char* name : {"Enabled", "CorpWarCost", "AllianceWarCost", "MaxDeclaredWars", "BillDays", "HistoryDays"})
        RemoveParser( name );

    if (war.BillDays < 1)
        war.BillDays = 1;

    return result;
}

bool EVEServerConfig::ProcessAlliance( const TiXmlElement* ele )
{
    AddValueParser( "JoinDelayHours",               alliance.JoinDelayHours );
    AddValueParser( "ExecutorTallyHour",            alliance.ExecutorTallyHour );
    AddValueParser( "ExecutorMinMemberDays",        alliance.ExecutorMinMemberDays );

    const bool result = ParseElementChildren( ele );

    for (const char* name : {"JoinDelayHours", "ExecutorTallyHour", "ExecutorMinMemberDays"})
        RemoveParser( name );

    if (alliance.ExecutorTallyHour > 23)
        alliance.ExecutorTallyHour = 11;

    return result;
}

bool EVEServerConfig::ProcessDebug(const TiXmlElement* ele)
{
    AddValueParser( "UseBeanCount",         debug.BeanCount );
    AddValueParser( "UseStackTrace",        debug.StackTrace );
    AddValueParser( "IsTestServer",         debug.IsTestServer );
    AddValueParser( "UseProfiling",         debug.UseProfiling );
    AddValueParser( "UseShipTracking",      debug.UseShipTracking );
    AddValueParser( "PositionHack",         debug.PositionHack );
    AddValueParser( "AnomalyFaction",       debug.AnomalyFaction );
    AddValueParser( "BubbleTrack",          debug.BubbleTrack );
    AddValueParser( "SpawnTest",            debug.SpawnTest );
    AddValueParser( "DeleteTrackingCans",   debug.DeleteTrackingCans );
    AddValueParser( "ProfileTraceTime",     debug.ProfileTraceTime );

    const bool result = ParseElementChildren( ele );

    RemoveParser( "UseBeanCount" );
    RemoveParser( "UseStackTrace" );
    RemoveParser( "IsTestServer" );
    RemoveParser( "UseProfiling" );
    RemoveParser( "UseShipTracking" );
    RemoveParser( "PositionHack" );
    RemoveParser( "DeleteTrackingCans" );
    RemoveParser( "AnomalyFaction" );
    RemoveParser( "SpawnTest" );
    RemoveParser( "BubbleTrack" );
    RemoveParser( "ProfileTraceTime" );

    return result;
}

bool EVEServerConfig::ProcessTesting(const TiXmlElement* ele)
{
    AddValueParser( "EnableDrones",         testing.EnableDrones);

    const bool result = ParseElementChildren( ele );

    RemoveParser( "EnableDrones" );

    return result;
}
