
 /**
  * @name SpawnMgr.h
  *     NPC Spawn management system for EVEmu
  *
  * @Author:    Allan
  * @date:      15Jul15
  *
  */


#ifndef _EVE_NPC_SPAWNMGR_H__
#define _EVE_NPC_SPAWNMGR_H__

#include <unordered_map>
#include "../../../eve-common/EVE_Spawn.h"
#include "npc/ai/AggroGroup.h"
#include "system/cosmicMgrs/ManagerDB.h"

class NPC;
class EVEServiceManager;
class SystemManager;
class SystemBubble;
class DungeonMgr;

class SpawnMgr
{
public:
    SpawnMgr(SystemManager* mgr, EVEServiceManager& svc);
    ~SpawnMgr()                                         { /* nothing do to yet */ }

    bool Init();

    void Process();
    void SetDungMgr(DungeonMgr* pDmgr)                  { m_dungMgr = pDmgr; }
    // warp pNPC's whole spawn group to where (in pBubble).  false if there was nothing to move
    bool WarpOutSpawn(NPC* pNPC, SystemBubble* pBubble, const GPoint& where);
    // update SpawnMgr on npcs new location (change bubbles)
    void MoveSpawn(NPC* pNPC, SystemBubble* pBubble);
    // a spawn (alive or waiting for a respawn) lives in this bubble
    bool HasSpawn(uint16 bubbleID);

    std::string GetSpawnClassName(int8 sClass);
    std::string GetSpawnGroupName(int8 sGroup);

    bool DoSpawnForBubble(SystemBubble* pBubble);

    /* dungeon (anomaly, mission, incursion) npcs, for DungeonMgr and other dungeon users.  they never respawn */
    // npc types of one npcSpawnClass row (type, sub;  sub 0 = a random row) for this faction.  false if there is nothing
    bool ResolveClassTypes(uint32 factionID, uint8 classType, uint8 classSub, std::vector<SpawnGroup>& out);
    // one npc at pos (warpIn: starts 100-150km away and warps to pos).  liveGroupID is its aggro group (NPC::SetSpawnGroupID)
    NPC* SpawnDungeonNPC(SystemBubble* pBubble, uint16 typeID, uint32 factionID, const GPoint& pos, bool warpIn, uint32 liveGroupID);

    // primitive test for chained spawns
    bool IsChaining(uint16 bubbleID);
    // this will be used for all spawn types
    void SpawnKilled(SystemBubble* pBubble, uint32 itemID);    // this DOES NOT remove entity from system or bubble.  user must do this BEFORE calling.
    /* a hauler warped off (npc ai plan 4.4):  it's removed on our next Process() (not counted as killed, no respawn).
     *  called from inside the npc's own Process(), so the removal waits */
    void SpawnFled(NPC* pNPC);

    // the aggro groups of this system (belt spawns, dungeon groups, GM batches;  npc ai plan 6.5)
    AggroGroupRegistry& GetAggroGroups()                { return m_aggroGroups; }

    void StartRatTimer();
    void StopRatTimer()                                 { m_ratTimer.Disable(); }
    void StartRatGroupTimer();
    void StopRatGroupTimer()                            { m_ratGroupTimer.Disable(); }

    bool IsInitialized()                                { return m_initalized; }
    bool IsRatTimerStarted()                            { return m_ratTimer.Enabled(); }


protected:
    bool FindSpawnForBubble(uint16 bubbleID);
    bool PrepSpawn(SystemBubble* pBubble, uint8 sClass = Spawn::Class::None, uint8 level = 0);
    void MakeSpawn(SystemBubble* pBubble, uint32 factionID, uint8 sClass, uint8 level, bool anomaly=false);
    // false when nothing was spawned (it is tried again later)
    bool ReSpawn(SystemBubble* pBubble, SpawnEntry& spawnEntry);
    void RemoveSpawn(uint16 bubbleID, uint32 itemID);

    uint8 GetSpawnGroup(uint8 sClass);
    // appends the npc types of one class row for this faction.  false if nothing was added
    bool AddClassTypes(uint32 factionID, uint8 sClass, const RatSpawnClass& entry, std::vector<SpawnGroup>& out);

    uint16 GetRandTypeID(uint8 sClass);

    typedef std::vector<NPC*> RatSpawningVec;
    typedef std::vector<SystemBubble*> RatBubbleVec;
    typedef std::vector<SpawnGroup> RatSpawnGroupVec;
    typedef std::vector<RatSpawnClass> RatSpawnClassVec;
    typedef std::vector<RatFactionGroups> RatFactionGroupsVec;
    typedef std::map<uint8, uint16> RatFactionGroupsMap;    //map to enable 'find'  shipClass is key
    typedef std::multimap<uint16, SpawnEntry> SpawnEntryDef;    //bubbleID is key
    //typedef std::vector<uint32, SystemSpawnGroup> SystemSpawnGroupVec;  //systemID is key  *unused at this time*

private:
    SystemManager* m_system;    //we do not own this
    EVEServiceManager& m_services;    //we do not own this
    DungeonMgr* m_dungMgr;    //we do not own this

    Timer m_ratTimer;
    Timer m_ratGroupTimer;
    Timer m_missionTimer;
    Timer m_incursionTimer;
    Timer m_deadspaceTimer;

    bool m_initalized;      //allow spawning?

    uint16 m_groupTimerSetTime;     //ms - this is for hard-coding the respawn timer time.

    uint32 m_spawnID;       //in case i need to track a specific spawn group.

    RatBubbleVec m_bubbles;
    SpawnEntryDef m_spawns;
    RatSpawningVec m_ratSpawns;
    RatSpawnGroupVec m_toSpawn;
    RatSpawnClassVec m_ratSpawnClass;               // not used
    //SystemSpawnGroupVec m_spawnGroups;
    RatFactionGroupsMap m_factionGroups;

    AggroGroupRegistry m_aggroGroups;
    std::vector<uint32> m_fled;     // haulers that got away, removed in Process()
};


#endif  // _EVE_NPC_SPAWNMGR_H__
