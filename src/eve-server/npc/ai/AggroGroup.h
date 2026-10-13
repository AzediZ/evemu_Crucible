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
  *   phase 3:  ForEachMember() is the level-one threat share (5.7) and the remote repairers' patients (5.9;  every group of
  *   a dungeon room through AggroGroupRegistry::ForEachRoomMember()).
  *   sentries (4.11, 6.5):  pirate and deadspace sentries are members too (AddSentry):  a dungeon spawn group's sentries are
  *   members of its live group, a room's placed sentries of the room's group, a GM /spawn's of its batch.  an alert reaches
  *   them through SentryAI::OnGroupAlert, and a sentry that is locked, attacked or acquires a target alerts its group.
  *   ForEachMember() is the npcs only (sentries don't escort, share threat or get repaired).
  *   law responses (GroupKind::Law):  one group per LawEnforcement response, for its identity (/npcai) and phase 3's member
  *   walks.  law npcs never alert or join (they answer only to the ship they were sent for, npc/ai/LawBrain.h)
  */

#ifndef _EVE_NPC_AI_AGGROGROUP_H_
#define _EVE_NPC_AI_AGGROGROUP_H_

#include <cstdint>

#include "eve-server.h"

class NPC;
class Sentry;
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
            DungeonRoom = 3,    // npcs placed in a dungeon room (no spawn group), id = (instanceID << 8) | room index
            GM          = 4,    // one /spawn command, id = AggroGroupRegistry::NextGMBatch()
            Law         = 5     // one law enforcement response (phase 4, LawEnforcement)
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
    void AddSentry(Sentry* pSentry);
    void RemoveSentry(Sentry* pSentry);
    // npc members
    size_t Size() const                                 { return m_members.size(); }
    size_t SentryCount() const                          { return m_sentries.size(); }
    // the npc members (not the sentries)
    template<typename F>
    void ForEachMember(const F& fn) const {
        for (size_t i = 0; i < m_members.size(); ++i)
            fn(m_members[i]);
    }

    /* pAggressor attacked, ewar'd or locked a member, or a member went after it (pSource:  that member, an npc or a sentry;
     *  nullptr = the room).  the group remembers the pilot and every other idle member joins (NPCAIMgr::Aggro,
     *  SentryAI::OnGroupAlert).  linked groups are told by the caller (AggroGroupRegistry::AlertLinked) */
    void Alert(SystemEntity* pSource, SystemEntity* pAggressor);
    // the pilot behind pSE (ship pilot, drone owner) aggroed us within npc.GroupAggroMemory
    bool IsAggressor(SystemEntity* pSE, int64 now) const;
    /* a member npc fights, or was hurt within underFireMs (NpcSupport).  worked out once a second for the whole group, so
     *  its repairers don't each walk it every tick (phase 5) */
    bool AnyFighting(int64 now, int64 underFireMs);
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
    bool m_fighting;                                    // AnyFighting()'s answer for the second m_fightingSec
    int64 m_alertedAt;                                  // ms
    int64 m_fightingSec;                                // s (ms / 1000).  -1 = not worked out yet
    std::vector<NPC*> m_members;                        // we do not own these
    std::vector<Sentry*> m_sentries;                    // nor these
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
    /* fn(NPC*) for every member of every group of a dungeon room (npc remote repair, phase 3).  fn must not change groups */
    template<typename F>
    void ForEachRoomMember(uint64_t roomKey, const F& fn) const {
        if (roomKey == 0)
            return;
        for (auto& cur : m_groups) {
            std::shared_ptr<AggroGroup> group(cur.second.lock());
            if ((group.get() != nullptr) and (group->GetRoomKey() == roomKey))
                group->ForEachMember(fn);
        }
    }

private:
    void Prune();

    std::unordered_map<uint64_t, std::weak_ptr<AggroGroup>> m_groups;
    uint32 m_gmBatch = 0;
    uint32 m_sincePrune = 0;
};

#endif  // _EVE_NPC_AI_AGGROGROUP_H_
