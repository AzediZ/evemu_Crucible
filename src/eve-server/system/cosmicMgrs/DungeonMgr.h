 /**
  * @name DungeonMgr.cpp
  *     Dungeon management system for EVEmu
  *
  * @Author:        James
  * @date:          13 December 2022
  */
 

#ifndef _EVEMU_SYSTEM_DUNGEONMGR_H
#define _EVEMU_SYSTEM_DUNGEONMGR_H

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

/*  this class is in charge of creating/destroying and maintaining
 * dungeons in a system.
 *
 *  a new iteration of this class is created for each system as that system is booted.
 */

class AnomalyMgr;
class SpawnMgr;
class EVEServiceManager;

class DungeonMgr {
public:
    DungeonMgr(SystemManager *system, EVEServiceManager& svc);
    ~DungeonMgr();

    bool Init(AnomalyMgr* anomMgr, SpawnMgr* spawnMgr);
    void Process();
    // system unload:  removes every live dungeon of this system
    void Close();

    bool MakeDungeon(CosmicSignature& sig, uint32 dungeonID = 0);

    // warp-in point of a dungeon instance (escalation sites, CmdWarpToStuff 'epinstance').  false if it isn't here or isn't charID's
    bool GetInstanceWarpIn(uint32 instanceID, uint32 charID, GPoint& into);

protected:
    ManagerDB m_db;

    void CreateDeco(uint32 templateID, CosmicSignature& sig);

    /* we do not own any of these */
private:
    bool m_initalized;

    AnomalyMgr* m_anomMgr;
    SpawnMgr* m_spawnMgr;
    SystemManager* m_system;
    EVEServiceManager& m_services;

    int8 GetFaction(uint32 factionID);
    int8 GetRandLevel();

    // live dungeons in this system (anomaly itemID -> rooms)
    struct LiveRoom {
        GPoint position;
        std::vector<uint32> items;
    };
    struct LiveDungeon {
        uint32 systemID;
        uint32 anomalyID;
        std::map<uint16, LiveRoom> rooms;
    };
    std::map<uint32, LiveDungeon> m_dungeonList; // This holds all live dungeons in the current system
};

#endif  // _EVEMU_SYSTEM_DUNGEONMGR_H