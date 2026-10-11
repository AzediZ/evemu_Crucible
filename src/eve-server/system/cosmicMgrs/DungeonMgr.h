 /**
  * @name DungeonMgr.cpp
  *     Dungeon management system for EVEmu
  *
  * @Author:        James
  * @date:          13 December 2022
  */
 

#ifndef _EVEMU_SYSTEM_DUNGEONMGR_H
#define _EVEMU_SYSTEM_DUNGEONMGR_H

#include <deque>
#include <set>
#include <unordered_map>
#include "EVE_Dungeon.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/ManagerDB.h"

// this class is a singleton object to have a common place for all in-memory dungeon data
class DungeonDataMgr
: public Singleton< DungeonDataMgr >
{
public:
    DungeonDataMgr();
    ~DungeonDataMgr() { /* nothing do to yet */ }

    // Initializes the Table:
    int Initialize();

    // copies (dungeon editor).  an unknown dungeon comes back with dungeonID 0 and no rooms
    void GetRandomDungeon(Dungeon::Dungeon& dungeon, uint8 archetype);
    void GetDungeon(Dungeon::Dungeon& dungeon, uint32 dungeonID);
    // shared data, no copy.  live instances keep their dungeon alive across UpdateDungeon()
    std::shared_ptr<const Dungeon::Dungeon> GetDungeon(uint32 dungeonID);
    // reload one dungeon from the db (dungeon editor playtest)
    void UpdateDungeon(uint32 dungeonID);

    /* weighted random dungeon of this archetype for a system with this true security.
     *  factionID 0 = any faction, else that faction's dungeons, else faction-less ones.  0 if there is none
     *  in null sec the hard sites (difficulty 4+) get likelier as true sec drops, the easy ones (2-) rarer (2011 bands)
     */
    uint32 PickDungeon(uint8 archetype, uint32 factionID, float trueSec, uint8 minDifficulty = 0);

    // acceleration gate rules of a dunRoomObjects object (nullptr = plain open gate)
    const Dungeon::GateDef* GetGate(uint32 objectID);
    void GetLoot(uint32 lootGroupID, std::vector<Dungeon::LootEntry>& into);
    // escalation rules for this source dungeon (by id, or archetype and difficulty)
    void GetEscalations(uint32 dungeonID, uint8 archetype, uint8 difficulty, std::vector<const Dungeon::EscalationDef*>& into);
    // ship groups (invGroups) the dungeon's gates allow and deny.  capitals are always denied
    void GetShipRestrictions(uint32 dungeonID, std::vector<uint32>& allowedGroups, std::vector<uint32>& deniedGroups);
    std::string GetDisplayName(const Dungeon::Dungeon& dungeon, uint32 factionID);
    const char* GetDungeonType(int8 typeID);

    // titans, dreads, carriers, supercarriers, freighters, jump freighters, rorquals:  no acceleration gates
    static bool IsCapitalGroup(uint32 groupID);

private:
    // (re)loads one dungeon, or all of them for dungeonID 0
    void Load(uint32 dungeonID);
    void ParseGroupList(const std::string& csv, std::vector<uint32>& into);

    std::map<uint32, std::shared_ptr<Dungeon::Dungeon>> m_dungeons;
    std::multimap<uint8, uint32> m_byArchetype;     // archetypeID -> dungeonID
    std::map<uint32, uint32> m_gateDungeon;         // gate objectID -> dungeonID
    std::map<uint32, Dungeon::GateDef> m_gates;     // gate objectID -> rules
    std::multimap<uint32, Dungeon::LootEntry> m_loot;
    std::vector<Dungeon::EscalationDef> m_escalations;
};

#define sDunDataMgr \
( DungeonDataMgr::get() )

class AnomalyMgr;
class SpawnMgr;
class EVEServiceManager;
class Client;
class Damage;
class ShipSE;

namespace Dungeon {
    // engine events for listeners.  above the client's trigger event values, which are passed through as they are
    namespace Engine {
        enum {
            Completed       = 1001, // the dungeon was completed (charID did it)
            Despawned       = 1002, // the instance is gone.  the listener is never called again
            EntityKilled    = 1003, // an npc or object of the instance was destroyed (entityID, typeID, charID = killer)
            RoomEntered     = 1004  // the first player entered a room (roomID, charID)
        };
    }

    // what to spawn (DungeonMgr::SpawnInstance)
    struct SpawnParams {
        uint32 dungeonID = 0;           // 0 = DungeonDataMgr::PickDungeon(archetype, factionID, system true sec)
        uint8  archetype = 0;           // Type::*  (for the pick, and the signature type of anomalies/signatures)
        uint32 factionID = 0;           // npc faction.  0 = the dungeon's faction, else the region's pirates, else rogue drones
        uint8  kind = Kind::Anomaly;    // Kind::*
        bool   usePosition = false;     // else a random site position in the system
        GPoint position = GPoint(0, 0, 0);
        uint32 ownerID = 0;             // private owner (mission, escalation):  charID
        uint32 escalationID = 0;        // chrEscalations.instanceID of an escalation site
        uint32 lifetimeSec = 0;         // 0 = the dungeon's lifetimeHours, else the config default of its kind (0 there = forever)
        class DungeonListener* listener = nullptr;  // not owned.  see ClearListener()
    };

    // what happened in an instance
    struct DungeonEvent {
        uint32 instanceID;
        uint32 dungeonID;
        uint32 roomID;
        uint16 type;        // Engine::* or a passed-through Event::Trigger::* (MissionCompletion, MissionFailure, AgentTalkTo,
                            //   AgentMsg, GrantGroupReward, GrantDelayedGroupReward, GrantGroupRewardLimitedRestrictions,
                            //   AdjustSystemInfluence, FacWarVictoryPointsGranted)
        uint32 entityID;
        uint32 charID;
        uint32 typeID;
        uint32 messageID;
        int32 quantity;
        double value;
    };

    /* missions (F12) and incursions (F25) get their instance's events through this.
     *  the listener must call DungeonMgr::ClearListener() (or DespawnInstance()) before it is destroyed
     */
    class DungeonListener {
    public:
        virtual ~DungeonListener()                      { }
        virtual void OnDungeonEvent(const DungeonEvent& ev) = 0;
    };
}

/*  this class is in charge of creating/destroying and maintaining
 * dungeons in a system.
 *
 *  for other systems (missions, incursions, exploration):
 *   - spawn:  SpawnParams{dungeonID (or archetype + factionID for a random pick), kind, position, ownerID, listener}
 *      -> SpawnInstance() returns the instanceID.  Kind::Mission bubbles are flagged mission, Kind::Incursion incursion.
 *      dungeons only for missions/incursions/escalations get spawnWeight 0 so AnomalyMgr never picks them.
 *   - events:  the listener gets Engine::Completed/Despawned/EntityKilled/RoomEntered and the data's MissionCompletion,
 *      MissionFailure, AgentTalkTo, AgentMsg, GrantGroupReward*, AdjustSystemInfluence events (from Process()).
 *      call ClearListener() or DespawnInstance() before the listener is destroyed.
 *   - object triggers:  OnObjectEvent(itemID, Dungeon::Trigger::HackingSuccess..ArchaeologyFailure,
 *      ItemPlacedInMissionContainer, ItemRemovedFromSpawnContainer, ItemInCargo, charID, typeID)
 *   - ship restrictions for briefings:  sDunDataMgr.GetShipRestrictions(dungeonID, allowed, denied)  (invGroups)
 *   - deadspace rules:  IsDeadspace(pos) (UserError DunCantAnchor / DunCantUseMWD are the client's messages)
 *
 *  a new iteration of this class is created for each system as that system is booted.
 *
 *  an instance is one spawned dungeon:  its rooms (room 0 at the site, the others 1/3 AU apart, reached through
 *   acceleration gates), their objects, npc groups (waves), triggers and events, gates and counters.
 *  all trigger evaluation runs in Process() (once a second), never inside damage/kill callbacks,
 *   so events may freely create and remove entities.
 */
class DungeonMgr {
public:
    DungeonMgr(SystemManager *system, EVEServiceManager& svc);
    ~DungeonMgr();

    bool Init(AnomalyMgr* anomMgr, SpawnMgr* spawnMgr);
    void Process();
    // system unload:  removes every live dungeon of this system (escalations stay in the db and come back on the next load)
    void Close();

    /* instances */
    // spawns a dungeon.  returns the instanceID (signature itemID for anomalies/signatures), 0 on failure.
    //  anomalies and signatures fill sigOut (item, bubble, name, owner) for AnomalyMgr
    uint32 SpawnInstance(const Dungeon::SpawnParams& params, CosmicSignature* sigOut = nullptr);
    // AnomalyMgr / dungeon editor wrapper:  dungeon of sig.dungeonType (or dungeonID) at sig.position.  true on success
    bool MakeDungeon(CosmicSignature& sig, uint32 dungeonID = 0);
    // removes the instance and all its entities (wrecks stay).  notifyAnomMgr: drop its scanner signal and queue a replacement
    void DespawnInstance(uint32 instanceID, bool notifyAnomMgr = true);
    // forget this listener in every instance (call before destroying it)
    void ClearListener(Dungeon::DungeonListener* pListener);

    /* queries */
    bool IsDungeonEntity(uint32 itemID)                 { return (m_entityIndex.find(itemID) != m_entityIndex.end()); }
    uint32 GetInstanceOf(uint32 itemID);
    // point is in a room of a live instance
    bool IsInDungeon(const GPoint& pos);
    // point is in deadspace:  any room past the first, or any room of a signature/escalation/mission site
    bool IsDeadspace(const GPoint& pos);
    // a warp to dest inside a room past a dungeon's first ends at the dungeon's entrance (into).  false = no change
    bool RedirectWarp(const GPoint& dest, GPoint& into);
    // warp-in point of an escalation site (CmdWarpToStuff 'epinstance').  false if it isn't here or isn't charID's
    bool GetInstanceWarpIn(uint32 escalationInstanceID, uint32 charID, GPoint& into);

    /* hooks (SystemEntity::Destroy, SystemEntity::ApplyDamage, ShipSE::Killed, hacking/salvage/containers/missions) */
    void OnEntityDestroyed(SystemEntity* pSE, Damage& d);
    void OnEntityAttacked(SystemEntity* pSE, SystemEntity* pAttacker);
    void OnPlayerKilled(ShipSE* pShip, uint32 charID);
    // Dungeon::Trigger::HackingSuccess..ArchaeologyFailure (11-16), ItemPlacedInMissionContainer (23),
    //  ItemRemovedFromSpawnContainer (32), ItemInCargo (33) on the dungeon object itemID (typeID for item triggers)
    void OnObjectEvent(uint32 itemID, uint16 triggerType, uint32 charID, uint32 typeID = 0);

    /* acceleration gates */
    struct LiveGate {
        uint32 instanceID;
        uint8 roomIdx;
        uint32 objectID;
        const Dungeon::GateDef* def;    // nullptr = plain open gate to the next room
        bool open;          // not locked (ActivateGate events open locked gates)
        int64 openUntil;    // FILETIME it opened (the model's 'on' curve starts there)
        bool shownOpen;     // what the slim item shows (open and, for requireClear gates, no npcs left)
    };
    const LiveGate* GetGate(uint32 gateItemID);
    // keeper.ActivateAccelerationGate.  throws the client's errors
    void ActivateGate(Client* pClient, uint32 gateItemID);
    // the dun* entries of an acceleration gate's slim item
    void MakeGateSlim(uint32 gateItemID, PyDict* slim);

    /* escalations */
    // spawns the escalation site of this chrEscalations row (its system must be this one)
    void SpawnEscalation(uint32 escalationInstanceID);
    // the escalation's live instance (0 = not spawned here)
    uint32 GetEscalationInstance(uint32 escalationInstanceID);
    // the pilot removed the journal entry (the site goes when nobody else has it)
    void OnEscalationRemoved(uint32 escalationInstanceID);

protected:
    ManagerDB m_db;

private:
    struct Ctx {
        uint32 entityID = 0;        // item that caused it (npc, object, ship)
        uint32 objectID = 0;        // dunRoomObjects.objectID of that item
        uint32 liveGroupID = 0;     // live group of that npc
        uint32 charID = 0;
        uint32 typeID = 0;
        bool groupWiped = false;    // Exploding:  the npc's whole group is dead
        GPoint pos = GPoint(0, 0, 0);
    };
    struct LiveGroup {
        uint32 liveGroupID;
        const Dungeon::SpawnGroupDef* def;
        std::set<uint32> alive;
        bool spawned = false;
        bool destroyed = false;
    };
    struct LiveRoom {
        uint32 roomID;
        const Dungeon::Room* def;
        GPoint origin;
        uint32 beltID = 0;                          // temp belt of the room's asteroids
        bool entered = false;
        bool populated = false;                     // its spawnMode 0 groups were spawned
        std::map<uint32, uint32> objects;           // objectID -> itemID
        std::set<uint32> asteroids;
        std::set<uint32> extraNPCs;                 // npcs placed in the room and from SpawnGuardObject/SpawnShip events
        std::set<uint32> extraObjects;              // objects from SpawnGuardObject events
        std::vector<LiveGroup> groups;
        std::set<uint32> firedTriggers;
        std::set<uint32> playersInside;             // charIDs
        std::map<uint32, std::set<uint32>> proximity;   // triggerID -> charIDs inside its range
        std::set<std::pair<uint32, uint32>> conditionFired;   // (triggerID, itemID)
    };
    struct Instance {
        uint32 instanceID = 0;
        std::shared_ptr<const Dungeon::Dungeon> dungeon;
        uint32 factionID = 0;
        uint8 kind = 0;
        uint32 ownerID = 0;
        uint32 escalationID = 0;
        std::string name;
        std::vector<LiveRoom> rooms;
        std::map<uint32, double> counters;
        int64 created = 0;          // FILETIME
        int64 expires = 0;          // FILETIME, 0 = never
        int64 completedAt = 0;
        bool completed = false;
        bool hasAttackTriggers = false;
        uint32 lastCharID = 0;
        Dungeon::DungeonListener* listener = nullptr;
        CosmicSignature sig;
        bool hasSignal = false;
        std::set<uint32> visitors;  // charIDs who entered (entered-notifications sent)
        std::map<uint32, uint32> escalationOwners;  // escalation site:  chrEscalations.instanceID -> charID (fleet members may share it)
    };
    struct PendingTrigger {
        uint32 instanceID;
        uint8 roomIdx;
        uint16 triggerType;
        Ctx ctx;
    };
    struct ReturnEntry {
        uint32 instanceID;
        uint8 roomIdx;
        uint32 typeID;
        uint32 liveGroupID;
        uint32 oldItemID;
        GPoint pos;
    };
    struct PendingEvent {
        uint32 instanceID;
        uint8 roomIdx;
        const Dungeon::EventDef* event;
        Ctx ctx;
    };

    // instance building
    uint32 ResolveFaction(const Dungeon::SpawnParams& params, const Dungeon::Dungeon& dungeon);
    GPoint NextRoomOrigin(const GPoint& from);
    bool IsNPCType(uint32 typeID);
    uint32 SpawnObject(Instance& inst, uint8 roomIdx, uint32 typeID, const GPoint& pos, uint32 objectID, double radius);
    void PopulateRoom(Instance& inst, uint8 roomIdx);
    void SpawnGroup(Instance& inst, uint8 roomIdx, LiveGroup& group);
    uint32 SpawnNPC(Instance& inst, uint8 roomIdx, uint32 typeID, uint32 factionID, const GPoint& pos, bool warpIn, uint32 liveGroupID);
    LiveGroup* FindGroup(Instance& inst, uint32 spawnGroupID, uint8& roomIdx);
    void Index(uint32 itemID, uint32 instanceID, uint8 roomIdx)  { m_entityIndex[itemID] = std::make_pair(instanceID, roomIdx); }
    // an entity leaves the instance without dying
    void ForgetEntity(Instance& inst, uint32 itemID);

    // triggers and events
    void QueueTrigger(uint32 instanceID, uint8 roomIdx, uint16 triggerType, const Ctx& ctx);
    void FireTriggers(Instance& inst, uint8 roomIdx, uint16 triggerType, const Ctx& ctx);
    void RunEvent(Instance& inst, uint8 roomIdx, const Dungeon::EventDef& event, const Ctx& ctx);
    void CheckCounters(Instance& inst, uint8 roomIdx, uint32 counterID);
    void ProcessPending(PendingTrigger& pending);
    void ProcessPresence(Instance& inst, const std::vector<std::pair<uint32, GPoint>>& ships);
    void ProcessConditions(Instance& inst, uint8 roomIdx);
    void ProcessGates(Instance& inst, uint8 roomIdx);
    void FlushNotices();
    void CheckImplicitCompletion(Instance& inst);
    void CompleteInstance(Instance& inst, uint32 charID);
    void SendTriggerMessage(Instance& inst, uint8 roomIdx, const Dungeon::EventDef& event);
    void Notify(Instance& inst, uint16 type, uint32 roomID, const Ctx& ctx, const Dungeon::EventDef* pEvent = nullptr);
    void DropLoot(uint32 lootGroupID, uint32 wreckID, const GPoint& pos, uint32 ownerID);
    uint32 FindWreck(uint32 deadItemID, const GPoint& pos);

    // entity removal
    void RemoveEntity(uint32 itemID, bool explode);
    void WarpAway(uint32 itemID, bool comeBack, Instance& inst, uint8 roomIdx);
    uint32 CountLiveNPCs(const LiveRoom& room);

    // gates
    void SendGateSlimUpdate(uint32 gateItemID);
    void OpenGate(uint32 gateItemID, LiveGate& gate, bool open);
    void ProcessGateState(uint32 gateItemID, LiveGate& gate);
    bool IsGateOpen(const LiveGate& gate);
    uint8 GetGateDestination(Instance& inst, const LiveGate& gate);

    // escalations
    void TryEscalate(Instance& inst, uint32 charID);
    void LoadEscalations();
    void OnVisitor(Instance& inst, uint8 roomIdx, uint32 charID);
    void OnEscalationCompleted(Instance& inst);

    bool m_initalized;

    /* we do not own any of these */
    AnomalyMgr* m_anomMgr;
    SpawnMgr* m_spawnMgr;
    SystemManager* m_system;
    EVEServiceManager& m_services;

    std::map<uint32, Instance> m_instances;
    std::map<uint32, std::pair<uint32, uint8>> m_entityIndex;  // itemID -> (instanceID, roomIdx):  npcs, objects, asteroids
    std::map<uint32, LiveGate> m_gates;                         // gate itemID ->
    std::deque<PendingTrigger> m_pending;
    std::multimap<int64, PendingEvent> m_delayed;               // due time (FILETIME) ->
    std::multimap<int64, ReturnEntry> m_returns;                // WarpShipAwayAndComeBack:  due time ->
    std::map<uint32, int64> m_warpedAway;                       // npc itemID -> removal time (WarpShipAway*)
    std::deque<std::pair<Dungeon::DungeonListener*, Dungeon::DungeonEvent>> m_notices;   // listener calls, made at the end of Process()
    std::set<uint16> m_unsupportedLogged;                       // trigger/event types already warned about
    uint32 m_nextLiveGroupID;
};

#endif  // _EVEMU_SYSTEM_DUNGEONMGR_H
