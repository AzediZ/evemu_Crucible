 /**
  * @name AggroGroup.h
  *   npcs that fight together (PLAN.md 4.2, 4.3, 6.5)
  *
  *   one group per belt/gate spawn (SpawnEntry.spawnID), per dungeon live group (DungeonMgr LiveGroup.liveGroupID),
  *   per dungeon room's placed npcs and per GM /spawn batch.  a respawned belt rat joins its old group.
  *   when one member acquires a target or is aggressed it alerts its group:  every idle member joins after its
  *   reaction delay (taking the same target with probability entityReactionFactor, the standard brain's rule).
  *   dungeon groups have an aggro mode (dunSpawnGroups.aggroMode):
  *    Proactive  (default) acquires on its own, like belt rats ("aggro at warp-in")
  *    Passive    never acquires on its own.  it aggroes when a member is damaged, ewar'd or locked, or a player ship or
  *               drone comes within its aggroRange (npc.PassiveAggroRange when 0) of a member.  once aggroed it remembers
  *               the pilots who did it for npc.GroupAggroMemory and re-engages them when they come back;  a group that
  *               was never aggroed stays passive
  *   groups with the same non-zero aggroLink (in one dungeon instance) aggro together;  damaging an overseer structure or
  *   a sentry aggroes every group of its room (DungeonMgr).
  *
  *   members hold the group (shared_ptr);  the registry (one per system, in SpawnMgr) only finds it.  a group goes away with
  *   its last member.
  *   phase 3 hooks:  ForEachMember() for the level-one threat share (5.7) and remote-repair reservations (5.9).
  *   phase 4:  sentries of a room are not members yet (SentryAI), the room rule covers attacks on them.
  */

#ifndef _EVE_NPC_AI_AGGROGROUP_H_
#define _EVE_NPC_AI_AGGROGROUP_H_

#include "eve-server.h"

class NPC;
class SystemEntity;

namespace NpcAI {
    namespace AggroMode {
        enum : uint8 {
            Proactive   = 0,
            Passive     = 1
        };
    }
    // what a group belongs to (the high byte of its key)
    namespace GroupKind {
        enum : uint8 {
            Belt        = 1,    // SpawnMgr spawn (belt or gate), id = spawnID
            Dungeon     = 2,    // DungeonMgr live group, id = liveGroupID
            DungeonRoom = 3,    // npcs placed in a dungeon room (no spawn group), id = instanceID
            GM          = 4     // one /spawn command
        };
    }
}

class AggroGroup {
public:
    AggroGroup(uint64_t key, uint8 mode, uint32 aggroRange, uint64_t linkKey, uint64_t roomKey);

    uint64_t GetKey() const                               { return m_key; }
    uint8 GetKind() const                               { return static_cast<uint8>(m_key >> 56); }
    bool IsPassive() const                              { return (m_mode == NpcAI::AggroMode::Passive); }
    uint8 GetMode() const                               { return m_mode; }
    // m.  how close a ship may come to a passive group's member (npc.PassiveAggroRange when the group has none)
    uint32 GetAggroRange() const;
    uint64_t GetLinkKey() const                           { return m_linkKey; }
    uint64_t GetRoomKey() const                           { return m_roomKey; }
    bool WasAlerted() const                             { return m_alerted; }
    int64 GetAlertedAt() const                          { return m_alertedAt; }

    void Add(NPC* pNPC);
    void Remove(NPC* pNPC);
    size_t Size() const                                 { return m_members.size(); }
    template<typename F>
    void ForEachMember(const F& fn) const {
        for (size_t i = 0; i < m_members.size(); ++i)
            fn(m_members[i]);
    }

    /* pAggressor attacked, ewar'd or locked a member, or a member went after it (pSource:  that member, nullptr = the room).
     *  the group remembers the pilot and every other idle member joins (NPCAIMgr::OnGroupAlert).  linked groups are told
     *  by the caller (AggroGroupRegistry::AlertLinked) */
    void Alert(NPC* pSource, SystemEntity* pAggressor);
    // the pilot behind pSE (ship pilot, drone owner) aggroed us within npc.GroupAggroMemory
    bool IsAggressor(SystemEntity* pSE, int64 now) const;
    void RememberAggressor(SystemEntity* pSE, int64 now);
    // charID of the pilot behind a ship or drone (0 = none)
    static uint32 GetCharID(SystemEntity* pSE);

    std::string Describe(int64 now) const;

private:
    uint64_t m_key;
    uint8 m_mode;
    uint32 m_aggroRange;
    uint64_t m_linkKey;
    uint64_t m_roomKey;
    bool m_alerted;
    int64 m_alertedAt;                                  // ms
    std::vector<NPC*> m_members;                        // we do not own these
    std::vector<std::pair<uint32, int64>> m_aggressors; // charID -> last aggression (ms)
};

/* the groups of one system (owned by its SpawnMgr) */
class AggroGroupRegistry {
public:
    static uint64_t MakeKey(uint8 kind, uint64_t id)        { return ((static_cast<uint64_t>(kind) << 56) | (id & 0x00FFFFFFFFFFFFFFULL)); }

    // the live group (nullptr = none)
    std::shared_ptr<AggroGroup> Find(uint8 kind, uint64_t id);
    // the live group, or a new one with these settings
    std::shared_ptr<AggroGroup> Get(uint8 kind, uint64_t id, uint8 mode = NpcAI::AggroMode::Proactive, uint32 aggroRange = 0,
                                    uint64_t linkKey = 0, uint64_t roomKey = 0);
    // every other group with pFrom's non-zero link key is alerted too (once:  they don't pass it on)
    void AlertLinked(AggroGroup* pFrom, SystemEntity* pAggressor);
    // every group of a dungeon room (an overseer structure or sentry was attacked)
    void AlertRoom(uint64_t roomKey, SystemEntity* pAggressor);
    // next free id for GM /spawn batches
    uint32 NextGMBatch()                                { return ++m_gmBatch; }

private:
    void Prune();

    std::unordered_map<uint64_t, std::weak_ptr<AggroGroup>> m_groups;
    uint32 m_gmBatch = 0;
    uint32 m_sincePrune = 0;
};

#endif  // _EVE_NPC_AI_AGGROGROUP_H_
