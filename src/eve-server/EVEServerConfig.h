/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2008 The EVEmu Team
    For the latest information visit http://evemu.mmoforge.org
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
    Author:     Zhur, Bloody.Rabit
    Updates:    Allan
    Version:    10.3
*/

#ifndef __EVE_SERVER_CONFIG__H__INCL__
#define __EVE_SERVER_CONFIG__H__INCL__

#include "eve-server.h"

#include "utils/XMLParserEx.h"

/**
 * @brief Class which parses and stores eve-server configuration.
 *
 * @author Zhur, Bloody.Rabbit
 */
class EVEServerConfig
: public XMLParserEx,
  public Singleton< EVEServerConfig >
{
public:
    EVEServerConfig();
    ~EVEServerConfig()                                  { /* do nothing here */}

    // From <server>
    struct {
        bool TraderJoe;
        bool DisableIGB;
        bool BulkDataOD;
        bool BulkDataAutoVersion;
        bool NoobShipCheck;
        bool ModuleAutoOff;
        bool UnloadOnLinkAll;
        bool AllowNonPublished;
        bool FleetShareDelayed;
        bool BountyPayoutDelayed;
        bool LoadOldMissions;
        bool AsteroidsOnDScan;
        bool CargoMassAdditive;
        bool ShieldBleedThrough;        // damage leaks through shields below (1 - shieldUniformity)  (F10)
        bool ArmorBleedThrough;         // same for armor to structure (not confirmed for Crucible)  (F10)
        bool T3SkillLoss;               // a destroyed strategic cruiser costs its pilot one subsystem skill level  (F10)
        uint8 ServerSleepTime;
        uint8 MaxThreadReport;
        uint8 BountyPayoutTimer;
        uint16 idleSleepTime;
        uint16 maxPlayers;
        float processTic;
        float ModuleDamageChance;
    } server;

    // From <world>
    struct {
        bool chatLogs;
        bool globalChat;
        bool gridUnload;
        bool loginInfo;
        bool loginMsg;
        bool saveOnMove;
        bool saveOnUpdate;
        bool highSecCyno;
        bool empireInterdiction;
        bool fleetWarpOnGridOnly;
        bool fleetWarpSlowestSpeed;
        bool shootRoids;
        bool shootWrecks;
        bool StoreShipWithCargo;        // ships stored in a maintenance bay/array may carry cargo (Odyssey+).  Crucible: charges only  (F09)
        bool SecureContainerOwnerBypass;    // owners open their own password-protected containers in their station hangar or ship without the password  (F20)
        uint8 mailDelay;
        uint8 StationDockDelay;
        uint16 shipBoardDistance;       // surface distance (client maxBoardingDistance)
        uint16 SelfDestructDelay;       // s  (F09)
        uint16 SelfDestructRetryDelay;  // s after an abort before self-destruct may be started again  (F09)
        uint16 gridUnloadTime;
        uint16 apWarptoDistance;
        uint16 fleetInviteTimeout;      // s an unanswered fleet invite (and the position it holds) lasts.  0 = never times out
        uint16 TrashRetentionDays;      // days a trashed item is kept for GM restore before it's purged at startup.  0 = deleted at once  (F20)
    } world;

    // From <rates>
    struct {
        // Decay timer for item deletion (garbage collection)
        uint8 WorldDecay;
        uint8 WebUpdate;
        // min amount of tax accepted by corp.  an amount less than this will not be processed, resulting in no tax for the concerned payment.
        uint32 TaxAmount;
        // min amount to be taxed.  received amounts less than this will not be taxed.
        uint32 TaxedAmount;
        /// Modifier for npc bounties automatically awarded for shooting down npc enemies.
        float npcBountyMultiply;
        /// Modifier for damage from NPCs
        float damageRate;
        /// Modifier for damage from missiles
        float missileDamage;
        /// Modifier for missile flightTime
        float missileTime;
        /// Modifier for missile rate of fire
        float missileRoF;
        /// Modifier for damage from PC turrets
        float turretDamage;
        /// Modifier for turret rate of fire
        float turretRoF;
        // Decay timer for wreck deletion (garbage collection)
        float NPCDecay;
        uint8 DropItem;
        float DropMoney;
        uint8 DropSalvage;
        float RepairCost;
        float ShipRepairModifier;
        float ModuleRepairModifier;
        /// Modifier for security rating changes. Changes how fast it goes up/down based on actions
        double secRate;
        /// Startup Cost to create a corporation.
        uint32 corpCost;
        /// Startup Cost to create an alliance.
        uint32 allyCost;
        uint32 medalAwardCost;
        uint32 medalCreateCost;
        // Crit chance
        float PlayerCritChance;
        float NpcCritChance;
        float SentryCritChance;
        float DroneCritChance;
        float ConcordCritChance;
    } rates;

    // from <market>
    struct {
        bool UseOrderRange;
        bool DeleteOldTransactions;
        uint8 StationOrderLimit;
        uint8 SystemOrderLimit;
        uint8 RegionOrderLimit;
        uint8 FindBuyOrder;
        uint8 FindSellOrder;
        uint8 OldPriceLimit;
        uint8 NewPriceLimit;
        uint8 HistoryUpdateTime;
        float salesTax;
        uint32 OrderModificationDelay;  // seconds between modifications of one order (0 disables)
        uint16 HistoryDays;             // days of daily price history served and kept
        bool UseMarginTrading;          // buy order escrow is price*qty*0.75^MarginTrading (Crucible)
        uint32 OrderCancelDelay;        // seconds after placing before an order can be cancelled (0 disables)
        bool ContractFees;              // contract broker's fee, deposit and sales tax
        uint16 ContractExpiredCleanupDays;  // 0 = Crucible (issuer deletes expired contracts).  N = removed N days after expiry
        bool ContractCourierAutoFail;   // overdue couriers fail by themselves a day after they're due
        bool InsuranceUseMaterialIndex; // insurance value from market material prices (Tyrannis); false = invTypes.basePrice
        uint16 InsuranceIndexDays;      // days of sell transactions for the material index
        uint16 InsuranceIndexMinTrades; // trades a material needs before its market price replaces basePrice
        float InsuranceDefaultPayout;   // payout fraction for uninsured ships
        float InsuranceMultiplierT1;
        float InsuranceMultiplierT2;
        float InsuranceMultiplierT3;
    } market;

    // From <ram>
    struct {
        bool AutoEvent;
        float ProdTime;
        float CopyTime;
        float ReTime;
        float MatMod;
        float WasteMod;
        float ResPE;
        float ResME;
        float InventTime;
        float InventChance;         // multiplies the Crucible invention chance (1.0 = Crucible)
        float ReChance;             // multiplies the Crucible reverse engineering chance (1.0 = Crucible)
    } ram;

    // From <account>
    struct {
        /// Role to assign to auto created account; set to 0 to disable auto account creation.
        int64 autoAccountRole;
        /// A message shown to every client on login (if enabled in <World><LoginMsg>).
        std::string loginMessage;
        bool redeemEnabled;             // redeem window (accRedeemTokens)  (F24)
        std::string reverseRedeemTypes; // comma separated typeIDs that can be put back into the redeem list, "" = none  (F24)
        bool cspaEnabled;               // CONCORD Spam Prevention Act charges for contacting non-contacts  (F24)
        int32 cspaDefaultCost;          // CSPA charge stored for new characters (0 = free, -1 = only contacts may contact)  (F24)
    } account;

    // From <character>
    struct {
        bool allow3edChar;
        uint8 statMultiplier;
        /// Starting corp ID for new characters
        uint32 startCorporation;
        /// Delay for terminating a character in seconds
        uint32 terminationDelay;
        /// Starting station ID for new characters
        uint32 startStation;
        /// Money balance of new created characters.
        double startBalance;
        /// Aura balance of new created characters.   -allan 01/10/14
        double startAurBalance;
        /// Starting security rating for new characters.
        double startSecRating;
        /// Crucible: plugging in implants, consuming boosters and clone jumping need a paused skill queue  (F15)
        bool requirePausedTraining;
        /// jump clone installation fee  (Crucible: 100k, const.costJumpClone)
        double jumpCloneCost;
        /// standing needed with an npc station's owner to install a jump clone there.  -10 disables
        float jumpCloneMinStanding;
        /// hours between clone jumps
        uint32 jumpCloneCooldownHours;
        /// podded pilots with too small a clone lose skill points
        bool cloneSkillLoss;
        /// % of total SP lost
        float cloneSkillLossPercent;
        /// max % of the affected skill's SP lost
        float cloneSkillLossCap;
        /// duration of boosters without a boosterDuration attribute
        uint32 boosterFallbackDurationHours;
    } character;

    // From <NPC>
    struct {
        bool IdleWander;
        bool UseDamageMultiplier;
        bool RoamingSpawns;
        bool StaticSpawns;
        bool TargetPod;
        bool UseEwar;               // npcs and non-law sentries use their type's ewar (scram, web, jam, damp, td, paint, neut)
        bool GroupAggro;            // attacking one npc alerts the rest of its spawn group
        uint16 WarpOut;
        uint16 TargetSwitchTime;    // s.  how often advanced (sleeper) npcs re-evaluate their primary
        uint32 LeashRange;          // m.  sleepers and deadspace npcs pulled further than this from home give up.  0 = off
        uint16 RoamingTimer;
        uint16 StaticTimer;
        uint16 RespawnTimer;
        uint32 RatFaction;
        float ThreatRadius;
        float WarpFollowChance;
        float TargetPodSec;
        float DefenderMissileChance;
        float LootDropChance;
        float DroneAggroChance;     // chance (per 30s) that a regular rat switches to a drone that damaged it
    } npc;

    // From <database>
    struct {
        bool compress;
        bool ssl;
        bool useSocket;
        bool autoReconnect;
        uint dbTimeout;
        uint8 pingTime;
        /// A port at which the database server listens.
        uint16 port;
        /// Hostname of database server.
        std::string host;
        /// Name of database account to use.
        std::string username;
        /// Password for the database account.
        std::string password;
        /// A database to be used by server.
        std::string db;
    } database;

    // From <files>
    struct {
        /// A directory in which the log files are stored
        std::string logDir;
        /// A log configuration file.
        std::string logSettings;
        /// A directory at which the cache files should be stored.
        std::string cacheDir;
        // used as the base directory for the image server
        std::string imageDir;
        // used as the path for the MarketBot.xml settings file
        std::string marketBotSettings;
    } files;

    // From <net>
    struct {
        /// Port at which the server should listen.
        uint16 port;
        /// Port at which the imageServer should listen.
        uint16 imageServerPort;
        /// the imageServer for char images. should be the evemu server external ip/host
        std::string imageServer;
    } net;

    // From <thread>
    struct {
        uint8 NetworkThreads;
        uint8 DatabaseThreads;
        uint8 WorldThreads;
        uint8 ImageServerThreads;
        uint8 ConsoleThreads;
    } threads;

    // From <cosmic>
    struct {
        bool PIEnabled;
        bool AnomalyEnabled;
        bool DungeonEnabled;
        bool BeltEnabled;
        bool WormHoleEnabled;
        bool CiviliansEnabled;
        bool BumpEnabled;
        uint8 BeltRespawn;
        uint8 BeltGrowth;
        float roidRadiusMultiplier;
        // planetary interaction (F19)
        bool PIRestrictTradeHubs;
        bool PISpawnAllCustomsOffices;
        float PICONCORDTaxMultiplier;
        float PIInterbusTaxMultiplier;
        int32 PIResourceSeed;
        float PIResourceScale;
        float PIResourceSpectrum;
        float PIRichnessHighsec;
        float PIRichnessLowsec;
        float PIRichnessNullsec;
        bool PIDepletionEnabled;
        float PIDepletionPerUnit;
        float PIDepletionMaxFraction;
        float PIDepletionRadius;
        float PIDepletionRegenHours;
        float PIGantryMaxDistanceKm;
    } cosmic;

    // From exploring
    struct {
        int8 Gravametric;
        int8 Magnetometric;
        int8 Ladar;
        int8 Radar;
        int8 Unrated;
        int8 Complex;
        uint16 DScanCooldown;           // ms between directional scans per character.  0 = none  (F11)
        float SurveyProbeConeAngle;     // degrees off the ship's heading a moon may be to be a survey probe's target  (F11)
        float ProbeStrengthScale;       // multiplier on scanner probe strength  (F11)
        float ProbeShipSigDivisor;      // ship/drone/structure signal size = signature radius / this  (F11)
    } exploring;

    // From <dungeon>  (F13: dungeons, anomalies and escalations)
    struct {
        uint32 AnomalyLifetime;         // h an untouched combat anomaly lives.  0 = until completed (Crucible)
        uint32 SignatureLifetime;       // h an untouched signature site lives (+-25%).  0 = until completed
        uint32 CompletedDespawn;        // s after completion before the site's objects and npcs are removed
        uint32 RespawnDelay;            // s before a completed/expired site is replaced elsewhere in the system
        float EscalationChance;         // multiplier on dunEscalations.chance
        uint32 EscalationExpiry;        // h an escalation lasts when its rule has no expiry (client dunExpirationDelay)
        bool EscalationShareFleet;      // fleet members in the system get the escalation too (Crucible: no)
        bool GateKeysRequired;          // acceleration gate keys are checked and consumed
        bool TriggerMessages;           // dungeon trigger messages are sent to players
    } dungeon;

    // From <missions>  (F12: agent missions)
    struct {
        uint16 OfferExpiryHours;        // h a mission offer stays in the journal (Crucible ~7 days)
        uint16 AcceptedExpiryHours;     // h to complete an accepted mission before it fails (Crucible 7 days)
        uint16 DeclinePenaltyHours;     // a second decline within this many hours costs standings (Crucible 4)
        bool UseDivisionTypes;          // Security agents offer encounters, Distribution courier, Mining mining.  false = courier only
        bool FallbackToCourier;         // an agent without content for its mission type offers a courier mission
        uint32 BaseISK[5];              // default reward per level before security and Negotiation
        uint32 BonusISK[5];             // default time bonus per level
        uint16 BonusMinutes[5];         // default bonus window per level
        uint32 BaseLP[5];               // default base LP per level before x(1.6288 - security) x Connections
        bool IskSecurityScaling;        // ISK rewards are scaled by (1.6288 - security) like LP
        bool FleetShare;                // a pilot in a fleet is asked whether to share the rewards
        uint16 FleetShareMax;           // max pilots sharing, completer included (Crucible 10)
        bool FleetShareSameSystem;      // only fleet members in the completer's system share
        uint16 DungeonDespawnMinutes;   // a mission pocket stays this long after completion, quit or failure (salvage)
        uint16 StorylineEvery;          // missions of one level for one faction before a storyline offer (Crucible 16)
        bool GateRestrictions;          // mission acceleration gates enforce the level's ship restrictions
    } missions;

    // From <agents>  (F24:  research and locator agents)
    struct {
        uint32 researchDatacoreRP;      // research points per datacore (100)
        double researchDatacoreISK;     // ISK per datacore (0 at Crucible; Inferno added 10000)
        bool researchFieldMultipliers;  // pre-Inferno research field multipliers (agtResearchFieldMultipliers); false = every field x1
        bool locatorEnabled;            // locator agents find characters
    } agents;

    // From <standings>
    struct {
        float MissionBonus;
        float MissionFailure;
        float MissionDeclined;
        float MissionCompleted;
        float MissionOfferExpired;
        float ImportantMissionBonus;
        float MissionFailedRollback;
        float BaseMissionMultiplier;
        float FleetMissionMultiplier;       // unused since F12 (missions.FleetShare)
        float Agent2CharMissionMultiplier;
        float ACorp2CharMissionMultiplier;
        float Agent2PCorpMissionMultiplier;
        float ACorp2PCorpMissionMultiplier;
        float AFaction2CharMissionMultiplier;
        float AFaction2PCorpMissionMultiplier;
    } standings;

    // From <chat>
    struct {
        bool EnableFleetChat;
        bool EnableWingChat;
        bool EnableSquadChat;
        bool EnableVoiceChat;
        bool EnforceRookieInHelp;
        bool EnforceChannelAccess;
        bool ContactStatusMutual;
        bool UserChannelCleanup;
    } chat;

    // From <crime>
    struct {
        bool Enabled;
        uint8 WeaponFlagTime;           // s after an aggressive act before you may dock or jump
        uint16 AggFlagTime;             // s an aggression flag lasts
        uint16 CrimFlagTime;            // s the global criminal countdown lasts
        uint16 KillRightDays;
        uint16 LogoffNPCTime;           // s a logged-off ship stays when NPCs were attacking it
        uint16 LogoffWarpTime;          // s a logged-off ship takes to warp off and vanish otherwise
        uint16 SecGainWindow;           // s only the best npc kill per system in this window raises security status
        float SecAggressionPenalty;     // % of the distance to -10
        float SecShipKillPenalty;
        float SecPodKillPenalty;
        float SecPodKillMinPenalty;
        bool CriminalLockdown;          // criminals can't use gates/stations in empire space except in a capsule
        bool SwitchShipWeaponsTimer;    // no ejecting/boarding/storing ships while the weapons timer runs (Retribution rule)  (F09)
        bool Concord;
        std::string ConcordResponse;    // response seconds for 1.0, 0.9, 0.8, 0.7, 0.6, 0.5
        bool ConcordWaves;              // officers and captains first, the commander later
        uint16 ConcordWaveDelay;        // s
        uint16 ConcordLinger;           // s CONCORD stays after the criminal's ship is gone
        bool FactionPolice;
        uint16 PoliceDelay;             // s
        uint16 PoliceTimeout;           // s
        bool Sentries;
        uint8 SentriesPerGate;
        uint8 SentriesPerStation;
        uint32 SentryRange;             // m
    } crime;

    // From <sov>
    struct {
        float MilitaryPointsPerBountyISK;   // military index points per isk of npc bounty killed in the system
        float IndustrialPointsPerM3;        // industrial index points per m3 of ore and ice mined in the system
        uint32 DevIndexDecayPerHour;        // points (client devIndexUtil.devIndexDecayRate; other values desync the client's level bars)
    } sov;

    // From <pos>  (F18: starbases)
    struct {
        float FuelRate;                     // multiplier on hourly fuel, charter and strontium use.  0 = towers run without fuel
        bool RequireCharters;               // starbase charters needed to anchor (cargo) and burned hourly where the data asks for them
        bool HighSecStandingCheck;          // corp standing to the system's empire faction >= 10 x security to anchor a tower there
        bool ConcordAnchorRestriction;      // no anchoring at 0.8+, and the per-type anchoringSecurityLevelMax
        bool KeepTowerSystemsLoaded;        // boot systems with online towers at startup and never unload them
        uint32 MaxCatchUpCycles;            // most hourly tower cycles replayed when a tower's system loads after downtime
        uint32 MoonGooSeed;                 // seed of the generated moon materials
        float MoonGooEmptyChance;           // chance a moon has no materials
        bool GunsEnabled;                   // starbase defenses shoot on their own
        bool TowerSnapToMoon;               // legacy: anchoring moves the tower to a fixed point at its moon and warps the pilot there
    } pos;

    // From <corp>  (F21: corporation management)
    struct {
        uint32 StasisHours;                 // hours after a member's roles are removed before they may quit or be kicked.  0 = no stasis
        uint32 CEOVoteMinSharePercent;      // % of the corporation's shares a member needs to run for CEO.  0 = no share requirement
        uint32 CEOVoteCooldownDays;         // days after a CEO vote opens before another may be opened
        uint32 SanctionDays;                // days the CEO has to implement a passed sanctionable vote
        bool EnforceRaceLimit;              // allowedMemberRaceIDs and the Ethnic Relations quota on application acceptance
        float JunkReleaseFraction;          // impound release fee as a fraction of the office rental fee
        bool BillsEnabled;                  // office rent and alliance maintenance bills; false = offices never expire
        bool CloseAllianceOnUnpaidBill;     // an alliance whose maintenance bill goes overdue is closed
        uint32 BillGraceDays;               // days after a bill's due date before the overdue handling runs
    } corp;

    // From <war>  (F22: wars between player corporations and alliances)
    struct {
        bool Enabled;                       // false:  no new declarations, and wars give no free targets
        double CorpWarCost;                 // base cost of a corporation's war on a corporation (isk)
        double AllianceWarCost;             // base cost when either side is an alliance (isk)
        uint32 MaxDeclaredWars;             // most wars an owner may have declared at once.  0 = no limit
        uint32 BillDays;                    // days between war bills
        uint32 HistoryDays;                 // days a finished war still shows in the war lists
    } war;

    // From <alliance>  (F22)
    struct {
        uint32 JoinDelayHours;              // hours from an accepted application to membership.  0 = at once
        uint32 ExecutorTallyHour;           // UTC hour of the daily executor support count (downtime)
        uint32 ExecutorMinMemberDays;       // days of membership before a corporation may declare executor support
    } alliance;

    // From <testing>
    struct {
        bool EnableDrones;
    } testing;

    // From <debug>
    struct {
        bool BeanCount;
        bool StackTrace;
        bool BubbleTrack;
        bool SpawnTest;
        bool IsTestServer;    // to distinguish between live production server or experimental testing server
        bool UseProfiling;
        bool UseShipTracking;
        bool DeleteTrackingCans;
        bool PositionHack;
        uint16 ProfileTraceTime;
        uint32 AnomalyFaction;
    } debug;

protected:
    bool ProcessEveServer( const TiXmlElement* ele );
    bool ProcessServer( const TiXmlElement* ele );
    bool ProcessWorld( const TiXmlElement* ele );
    bool ProcessRates( const TiXmlElement* ele );
    bool ProcessMarket( const TiXmlElement* ele );
    bool ProcessAccount( const TiXmlElement* ele );
    bool ProcessCharacter( const TiXmlElement* ele );
    bool ProcessNPC( const TiXmlElement* ele );
    bool ProcessDatabase( const TiXmlElement* ele );
    bool ProcessFiles( const TiXmlElement* ele );
    bool ProcessNet( const TiXmlElement* ele );
    bool ProcessThreads( const TiXmlElement* ele );
    bool ProcessCosmic( const TiXmlElement* ele );
    bool ProcessExploring( const TiXmlElement* ele );
    bool ProcessDungeon( const TiXmlElement* ele );
    bool ProcessMissions( const TiXmlElement* ele );
    bool ProcessAgents( const TiXmlElement* ele );
    bool ProcessStandings( const TiXmlElement* ele );
    bool ProcessChat( const TiXmlElement* ele );
    bool ProcessCrime( const TiXmlElement* ele );
    bool ProcessSov( const TiXmlElement* ele );
    bool ProcessPOS( const TiXmlElement* ele );
    bool ProcessCorp( const TiXmlElement* ele );
    bool ProcessWar( const TiXmlElement* ele );
    bool ProcessAlliance( const TiXmlElement* ele );
    bool ProcessBPTimes( const TiXmlElement* ele );
    bool ProcessTesting( const TiXmlElement* ele );
    bool ProcessDebug( const TiXmlElement* ele );
};

/// A macro for easier access to the singleton.
#define sConfig \
    ( EVEServerConfig::get() )

#endif /* !__EVE_SERVER_CONFIG__H__INCL__ */
