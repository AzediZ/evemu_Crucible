 /**
  * @name AggroGroup.cpp
  *   npcs that fight together (PLAN.md 4.2, 4.3, 6.5).  see AggroGroup.h
  */

#include "eve-server.h"

#include "EVEServerConfig.h"
#include "Client.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/ai/AggroGroup.h"

AggroGroup::AggroGroup(uint64_t key, uint8 mode, uint32 aggroRange, uint64_t linkKey, uint64_t roomKey)
: m_key(key),
  m_mode(mode),
  m_aggroRange(aggroRange),
  m_linkKey(linkKey),
  m_roomKey(roomKey),
  m_alerted(false),
  m_alertedAt(0)
{
}

uint32 AggroGroup::GetAggroRange() const
{
    return (m_aggroRange > 0 ? m_aggroRange : sConfig.npc.PassiveAggroRange);
}

void AggroGroup::Add(NPC* pNPC)
{
    if (pNPC == nullptr)
        return;
    if (std::find(m_members.begin(), m_members.end(), pNPC) == m_members.end())
        m_members.push_back(pNPC);
}

void AggroGroup::Remove(NPC* pNPC)
{
    auto itr = std::find(m_members.begin(), m_members.end(), pNPC);
    if (itr != m_members.end())
        m_members.erase(itr);
}

uint32 AggroGroup::GetCharID(SystemEntity* pSE)
{
    if (pSE == nullptr)
        return 0;
    if (pSE->HasPilot())
        return pSE->GetPilot()->GetCharacterID();
    if (pSE->IsDroneSE() and IsCharacterID(pSE->GetSelf()->ownerID()))
        return pSE->GetSelf()->ownerID();
    return 0;
}

void AggroGroup::RememberAggressor(SystemEntity* pSE, int64 now)
{
    uint32 charID(GetCharID(pSE));
    if (charID == 0)
        return;
    for (auto& cur : m_aggressors)
        if (cur.first == charID) {
            cur.second = now;
            return;
        }
    m_aggressors.emplace_back(charID, now);
}

bool AggroGroup::IsAggressor(SystemEntity* pSE, int64 now) const
{
    uint32 charID(GetCharID(pSE));
    if (charID == 0)
        return false;
    int64 memory(static_cast<int64>(sConfig.npc.GroupAggroMemory) * 1000);
    for (auto& cur : m_aggressors)
        if (cur.first == charID)
            return ((now - cur.second) <= memory);
    return false;
}

void AggroGroup::Alert(NPC* pSource, SystemEntity* pAggressor)
{
    if (pAggressor == nullptr)
        return;
    int64 now(GetSteadyTime());
    if (!m_alerted)
        _log(NPC__AI_TRACE, "AggroGroup %llx:  aggroed by %s(%u)%s.", m_key, pAggressor->GetName(), pAggressor->GetID(),
                (IsPassive() ? " (passive group)" : ""));
    m_alerted = true;
    m_alertedAt = now;
    RememberAggressor(pAggressor, now);
    // npc.GroupAggro off:  members only answer for themselves
    if (!sConfig.npc.GroupAggro)
        return;
    // by index:  a member's answer never adds or removes members
    for (size_t i = 0; i < m_members.size(); ++i) {
        NPC* pNPC(m_members[i]);
        if ((pNPC == pSource) or pNPC->IsDead() or (pNPC->GetAIMgr() == nullptr))
            continue;
        pNPC->GetAIMgr()->Aggro(pAggressor);
    }
}

std::string AggroGroup::Describe(int64 now) const
{
    std::ostringstream str;
    static const char* kinds[] = { "?", "belt", "dungeon", "room", "gm", "law" };
    uint8 kind(GetKind());
    str << (kind < 6 ? kinds[kind] : "?") << " " << (m_key & 0x00FFFFFFFFFFFFFFULL) << ", " << m_members.size() << " members, "
        << (IsPassive() ? "passive" : "proactive");
    if (IsPassive())
        str << " (range " << GetAggroRange() << "m)";
    if (m_linkKey != 0)
        str << ", link " << static_cast<uint32>(m_linkKey & 0xFFFFFFFF);
    if (m_alerted)
        str << ", aggroed " << ((now - m_alertedAt) / 1000) << "s ago by " << m_aggressors.size() << " pilot(s)";
    return str.str();
}

/* AggroGroupRegistry */
std::shared_ptr<AggroGroup> AggroGroupRegistry::Find(uint8 kind, uint64_t id)
{
    auto itr = m_groups.find(MakeKey(kind, id));
    if (itr == m_groups.end())
        return nullptr;
    std::shared_ptr<AggroGroup> group(itr->second.lock());
    if (group.get() == nullptr)
        m_groups.erase(itr);
    return group;
}

std::shared_ptr<AggroGroup> AggroGroupRegistry::Get(uint8 kind, uint64_t id, uint8 mode/*Proactive*/, uint32 aggroRange/*0*/,
                                                    uint64_t linkKey/*0*/, uint64_t roomKey/*0*/)
{
    std::shared_ptr<AggroGroup> group(Find(kind, id));
    if (group.get() != nullptr)
        return group;
    if (++m_sincePrune > 64)
        Prune();
    uint64_t key(MakeKey(kind, id));
    group = std::make_shared<AggroGroup>(key, mode, aggroRange, linkKey, roomKey);
    m_groups[key] = group;
    return group;
}

void AggroGroupRegistry::Prune()
{
    m_sincePrune = 0;
    for (auto itr = m_groups.begin(); itr != m_groups.end(); ) {
        if (itr->second.expired()) {
            itr = m_groups.erase(itr);
        } else {
            ++itr;
        }
    }
}

void AggroGroupRegistry::AlertLinked(AggroGroup* pFrom, SystemEntity* pAggressor)
{
    if ((pFrom == nullptr) or (pFrom->GetLinkKey() == 0) or (pAggressor == nullptr))
        return;
    // collect first:  an alert never changes the registry, but keep the walk simple
    std::vector<std::shared_ptr<AggroGroup>> linked;
    for (auto& cur : m_groups) {
        std::shared_ptr<AggroGroup> group(cur.second.lock());
        if ((group.get() != nullptr) and (group.get() != pFrom) and (group->GetLinkKey() == pFrom->GetLinkKey()))
            linked.push_back(group);
    }
    for (auto& cur : linked)
        cur->Alert(nullptr, pAggressor);
}

void AggroGroupRegistry::AlertRoom(uint64_t roomKey, SystemEntity* pAggressor)
{
    if ((roomKey == 0) or (pAggressor == nullptr))
        return;
    std::vector<std::shared_ptr<AggroGroup>> room;
    for (auto& cur : m_groups) {
        std::shared_ptr<AggroGroup> group(cur.second.lock());
        if ((group.get() != nullptr) and (group->GetRoomKey() == roomKey))
            room.push_back(group);
    }
    for (auto& cur : room)
        cur->Alert(nullptr, pAggressor);
}
