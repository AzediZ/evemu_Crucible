/*
 *  EVE_Dungeon.h
 *   dungeon-specific enumerators
 *
 */

#ifndef EVE_DUNGEON_H
#define EVE_DUNGEON_H

namespace Dungeon {
    namespace Type {
        enum {
            Mission         = 1, // npc mission
            Gravimetric     = 2, // roids
            Magnetometric   = 3, // salvage and archeology
            Radar           = 4, // hacking
            Ladar           = 5, // gas mining
            Wormhole        = 6, // wtf is a 'wormhole'??
            Anomaly         = 7, // non-rated dungeon that isnt required to scan with probes
            Unrated         = 8, // non-rated dungeon  no waves, possible escalation to complex
            Escalation      = 9, // new dungeon from previous site. very limited access
            Rated           = 10,// DED rated dungeon
            Incursion       = 11,// incursion site (server-only archetype)
            // factional warfare complexes (FacWarMgr.h):  one held by the system's occupier (offensive:  its enemy takes it to
            //  contest the system), or by its enemy (defensive:  the occupier takes it back).  the client's dunArchetypeFacwar*
            FacWarDefensive = 32,
            FacWarOffensive = 35
        };
        // a factional warfare complex's:  it lasts until it's taken, and FacWarMgr replaces it
        inline bool IsFacWar(int type)                  { return ((type == FacWarDefensive) or (type == FacWarOffensive)); }
    }

    // what a live dungeon instance is for (DungeonMgr::SpawnInstance)
    namespace Kind {
        enum {
            Anomaly         = 1, // combat anomaly (scanner, no probes)
            Signature       = 2, // probe-scanned site
            Mission         = 3, // private agent mission site
            Incursion       = 4, // incursion site
            Escalation      = 5, // private escalation (journal only)
            Test            = 6  // dungeon editor playtest
        };
    }

    namespace Spawn {
        enum {
            Belts           = 0,
            Gate            = 1,
            Near            = 2,
            Deep            = 3,
            Reinforcments   = 4,
            Stations        = 5,
            Faction         = 6,
            Concord         = 7
        };
    }
    namespace Status {
        enum {
            Started         = 0,
            Completed       = 1,
            Failed          = 2
        };
    }

    namespace Event {
        namespace Msg {
            enum {
                ImminentDanger      = 1,
                NPC                 = 2,
                Environment         = 3,
                Mood                = 4,
                Story               = 5,
                MissionObjective    = 6,
                MissionInstruction  = 7,
                Warning             = 8
            };
        }

        namespace Trigger {
            enum {
                ActivateGate = 1,
                SpawnGuards = 2,
                SpawnGuardObject = 3,
                RangedNPCHealing = 4,
                RangedPlayerDamageEM = 5,
                RangedPlayerDamageExplosive = 6,
                RangedPlayerDamageKinetic = 7,
                RangedPlayerDamageThermal = 8,
                MissionCompletion = 9,
                Msg = 10,
                DungeonCompletion = 11,
                EffectBeaconActivate = 13,
                EffectBeaconDeactivate = 14,
                ObjectDespawn = 15,
                ObjectExplode = 16,
                EntityDespawn = 18,
                EntityExplode = 19,
                FacWarVictoryPointsGranted = 20,
                AgentTalkTo = 22,
                AgentMsg = 23,
                DropLoot = 24,
                RangedPlayerHealing = 25,
                RangedNPCDamageEM = 26,
                RangedNPCDamageExplosive = 27,
                RangedNPCDamageKinetic = 28,
                RangedNPCDamageThermal = 29,
                SpawnItemInCargo = 30,
                MissionFailure = 31,
                CounterAdd = 32,
                CounterSubtract = 33,
                CounterMultiply = 34,
                CounterDivide = 35,
                CounterSet = 36,
                GrantGroupReward = 37,
                GrantDelayedGroupReward = 38,
                AdjustSystemInfluence = 39,
                WarpShipAwayDespawn = 40,
                WarpShipAwayAndComeBack = 41,
                SupressAllRespawn = 42,
                GrantGroupRewardLimitedRestrictions = 45,
                OpenTutorial = 46,
                SpawnShip = 47,
                // server-only: roll this dungeon's escalations for the triggering character
                Escalate = 100
            };
        }
    }

    namespace Trigger {
        enum {
            Attacked = 1,
            ShipEnteredProximity = 2,
            Exploding = 3,
            ShieldConditionLevel = 4,
            ArmorConditionLevel = 5,
            StructureConditionLevel = 6,
            Mined = 7,
            RoomEntered = 8,
            RoomMinedOut = 9,
            RoomMined = 10,
            HackingSuccess = 11,
            HackingFailure = 12,
            SalvagingSuccess = 13,
            SalvagingFailure = 14,
            ArchaeologySuccess = 15,
            ArchaeologyFailure = 16,
            ShipsEnteredRoom = 17,
            RoomCapturedCorp = 18,
            RoomCapturedAlliance = 19,
            RoomCapturedFacWar = 20,
            FWShipEnteredProximity = 21,
            ItemPlacedInMissionContainer = 23,
            PlayerKilled = 26,
            EffectActivated = 27,
            ShipsLeftRoom = 28,
            ShipLeftProximity = 29,
            FWShipLeftProximity = 30,
            RoomWipedOut = 31,
            ItemRemovedFromSpawnContainer = 32,
            ItemInCargo = 33,
            CounterEQ = 34,
            CounterGT = 35,
            CounterGE = 36,
            CounterLT = 37,
            CounterLE = 38,
            FacWarLoyaltyPointsGranted = 48
        };
    }
    /* POD structure entries for dungeon data (static, managed by DungeonDataMgr) */

    struct RoomObject {
        uint32 objectID;
        uint32 roomID;
        uint32 typeID;
        uint32 groupID;     // dungeon editor group (dunGroups), not invGroups
        double x;
        double y;
        double z;
        double yaw;
        double pitch;
        double roll;
        double radius;
    };

    // one group of npcs (a wave) in a room
    struct SpawnGroupDef {
        uint32 spawnGroupID;
        uint32 roomID;
        uint8 waveNumber;
        uint8 spawnMode;        // SpawnMode::*
        uint8 npcClassType;     // npcSpawnClass.type (0 = types list)
        uint8 npcClassSub;      // npcSpawnClass.sub
        uint32 factionID;       // 0 = instance faction
        double x;
        double y;
        double z;
        double spread;
        bool warpIn;
        bool isOverseer;
        uint32 lootGroupID;
        uint8 aggroMode;        // AggroMode::*  (npc ai plan 4.3)
        uint32 aggroRange;      // m.  a passive group aggroes on ships this close (0 = npc.PassiveAggroRange)
        uint32 aggroLink;       // groups of one dungeon with the same non-zero link aggro together
        std::vector<std::pair<uint32, uint32>> types;   // typeID, quantity
    };
    namespace AggroMode {
        enum {
            Proactive       = 0,    // acquires on its own ("aggro at warp-in")
            Passive         = 1     // only when a member is attacked, ewar'd or locked, or a ship comes within aggroRange
        };
    }
    namespace SpawnMode {
        enum {
            RoomCreated     = 0,
            RoomEntered     = 1,
            TriggerOnly     = 2
        };
    }

    struct EventDef {
        uint32 eventID;
        uint32 triggerID;
        int32 sequence;
        uint16 eventType;       // Event::Trigger::*
        uint32 delaySeconds;
        uint32 spawnGroupID;
        uint32 objectID;
        uint8 messageType;      // Event::Msg::*
        uint32 messageID;
        std::string messageText;
        uint32 typeID;
        int32 quantity;
        double value;
        uint32 counterID;
    };

    struct TriggerDef {
        uint32 triggerID;
        uint32 roomID;
        uint16 triggerType;     // Trigger::*
        uint32 spawnGroupID;    // source group
        uint32 objectID;        // source object
        double value;
        uint32 counterID;
        bool repeatable;
        std::vector<EventDef> events;   // sorted by sequence
    };

    struct GateDef {
        uint32 objectID;
        uint32 destRoomID;      // 0 = next room
        bool locked;
        bool requireClear;
        uint32 keyTypeID;
        uint32 keyQuantity;
        bool keyConsumed;
        uint32 skillTypeID;
        uint8 skillLevel;
        std::vector<uint32> allowedGroups;
        std::vector<uint32> deniedGroups;
        std::string musicUrl;
    };

    struct LootEntry {
        uint32 typeID;
        uint32 minQty;
        uint32 maxQty;
        double chance;
    };

    struct EscalationDef {
        uint32 escalationID;
        uint32 srcDungeonID;
        uint8 srcArchetypeID;
        uint8 srcMinDifficulty;
        uint32 destDungeonID;
        uint8 destArchetypeID;
        int8 destDifficultyDelta;
        double chance;
        uint8 minJumps;
        uint8 maxJumps;
        bool lowerSecOnly;
        uint32 expiryHours;
        uint32 journalMessageID;
    };

    struct Room {
        uint32 roomID;
        std::string roomName;
        GPoint arrival;                         // acceleration gate arrival point, relative to the room origin
        std::vector<RoomObject> objects;
        std::vector<SpawnGroupDef> groups;
        std::vector<TriggerDef> triggers;
    };

    struct Dungeon {
        uint32 dungeonID;
        std::string name;
        uint8 status;
        uint32 factionID;
        uint8 archetypeID;
        uint32 nameID;                          // client localization messageID of the name (0 = none)
        uint8 difficulty;
        float minSec;
        float maxSec;
        uint32 weight;
        uint32 lifetimeHours;
        uint32 regionID;                        // only spawns in this region (0 = anywhere)
        uint32 constellationID;                 // only spawns in this constellation (0 = anywhere)
        float sigStrength;                      // the signature's scan strength (0 = AnomalyMgr's default)
        bool hasCompletionEvent;                // some trigger completes the dungeon (else: all groups destroyed)
        std::vector<uint32> roomOrder;          // roomIDs ascending.  index 0 is the entry room
        std::map<uint32, Room> rooms;
        std::map<uint32, GateDef> gates;        // acceleration gate rules by objectID (live gates point in here)
    };
}

/*
dunArchetypeAgentMissionDungeon = 20
dunArchetypeFacwarDefensive = 32
dunArchetypeFacwarOffensive = 35
dunArchetypeFacwarDungeons = (dunArchetypeFacwarDefensive, dunArchetypeFacwarOffensive)
dunArchetypeWormhole = 38
dunArchetypeZTest = 19
dunExpirationDelay = 48
dungeonGateUnlockPeriod = 66
DUNGEON_EVENT_TYPE_AFFECTS_ENTITY = [dunTriggerEventEntityExplode,
 dunTriggerEventEntityDespawn,
 dunTriggerEventSpawnGuards,
 dunTriggerEventWarpShipAwayDespawn,
 dunTriggerEventWarpShipAwayAndComeBack]
DUNGEON_EVENT_TYPE_AFFECTS_OBJECT = [dunTriggerEventSpawnGuardObject,
 dunTriggerEventEffectBeaconActivate,
 dunTriggerEventEffectBeaconDeactivate,
 dunTriggerEventObjectExplode,
 dunTriggerEventObjectDespawn,
 dunTriggerEventActivateGate]
DUNGEON_ORIGIN_UNDEFINED = None
DUNGEON_ORIGIN_STATIC = 1
DUNGEON_ORIGIN_AGENT = 2
DUNGEON_ORIGIN_PLAYTEST = 3
DUNGEON_ORIGIN_EDIT = 4
DUNGEON_ORIGIN_DISTRIBUTION = 5
DUNGEON_ORIGIN_PATH = 6
DUNGEON_ORIGIN_TUTORIAL = 7
*/

#endif  // EVE_DUNGEON_H


/*{'FullPath': u'UI/Agents/Dialogue', 'messageID': 233757, 'label': u'DungeonShipRestrictionsListShipIsRestricted'}(u'This site is restricted to certain ship types; <font color=red>your {[item]shipTypeID.name} will not be able to access it.</font> You must use one of the following types of ship:<br><br>{shipList}', None, {u'{[item]shipTypeID.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'shipTypeID'}, u'{shipList}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'shipList'}})
 * {'FullPath': u'UI/Agents/Dialogue', 'messageID': 233758, 'label': u'DungeonShipRestrictionsListShipIsNotRestricted'}(u'This site is restricted to certain ship types; you may use your {[item]shipTypeID.name} to access it, or one of the following types of ship:<br><br>{shipList}', None, {u'{[item]shipTypeID.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'shipTypeID'}, u'{shipList}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'shipList'}})
 * {'FullPath': u'UI/Agents/Dialogue', 'messageID': 233759, 'label': u'DungeonShipRestrictionShipIsNotRestricted'}(u'This site is restricted to the following ship type: {groupName}<br/><br />You may use your {[item]typeID.name} to access it.', None, {u'{[item]typeID.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'typeID'}, u'{groupName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'groupName'}})
 * {'FullPath': u'UI/Agents/Dialogue', 'messageID': 233760, 'label': u'DungeonShipRestrictionsShowList'}(u'This site is restricted to certain ship types. It can only be accessed by flying one of the following:<br/><br/>{shipList}', None, {u'{shipList}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'shipList'}})
 * {'FullPath': u'UI/Agents/Dialogue', 'messageID': 233761, 'label': u'DungeonShipRestrictionsHeader'}(u'Ships permitted', None, None)
 */