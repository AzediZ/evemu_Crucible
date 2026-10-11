 /**
  * @name EwarState.cpp
  *   electronic warfare received by an entity
  */

#include "eve-server.h"

#include "Client.h"
#include "ship/modules/EwarModule.h"
#include "system/EwarState.h"
#include "system/SystemBubble.h"
#include "system/SystemEntity.h"
#include "system/SystemManager.h"


EwarState::EwarState(SystemEntity* pOwner)
: m_owner(pOwner)
{
    m_entries.clear();
}

EwarState::~EwarState()
{
    // the owner is being deleted.  its destiny and target managers may already be gone, so no side effects here
    m_entries.clear();
}

void EwarState::Apply(uint32 sourceID, uint8 kind, uint32 durationMs, float v1/*0*/, float v2/*0*/,
                      InventoryItemRef srcRef/*nullptr*/, uint32 moduleID/*0*/)
{
    if (kind == Ewar::None)
        return;
    int64 now(GetFileTimeNow());
    uint64_t key(MakeKey((moduleID != 0 ? moduleID : sourceID), kind));
    std::map<uint64_t, Entry>::iterator itr = m_entries.find(key);
    if (itr != m_entries.end()) {
        // refresh only
        itr->second.expires = now + ((int64)durationMs * (EvE::Time::Second / 1000));
        itr->second.v1 = v1;
        itr->second.v2 = v2;
        return;
    }

    Entry e = Entry();
        e.kind = kind;
        e.v1 = v1;
        e.v2 = v2;
        e.start = now;
        e.expires = now + ((int64)durationMs * (EvE::Time::Second / 1000));
        e.durationMs = durationMs;
        e.sourceID = sourceID;
        e.moduleID = moduleID;
        e.srcRef = srcRef;
    std::pair<std::map<uint64_t, Entry>::iterator, bool> res = m_entries.emplace(key, e);
    _log(EFFECTS__TRACE, "EwarState::Apply() - %s(%u) receives kind %u from %u (module %u) for %ums.",
            m_owner->GetName(), m_owner->GetID(), kind, sourceID, moduleID, durationMs);
    OnStart(res.first->second);
}

void EwarState::Remove(uint32 sourceID, uint8 kind, uint32 moduleID/*0*/)
{
    std::map<uint64_t, Entry>::iterator itr = m_entries.find(MakeKey((moduleID != 0 ? moduleID : sourceID), kind));
    if (itr == m_entries.end())
        return;
    Entry e(itr->second);
    m_entries.erase(itr);
    OnEnd(e);
}

void EwarState::RemoveSource(uint32 sourceID)
{
    std::vector<Entry> ended;
    std::map<uint64_t, Entry>::iterator itr = m_entries.begin();
    while (itr != m_entries.end()) {
        if (itr->second.sourceID == sourceID) {
            ended.push_back(itr->second);
            itr = m_entries.erase(itr);
        } else {
            ++itr;
        }
    }
    for (auto& cur : ended)
        OnEnd(cur);
}

void EwarState::Clear()
{
    std::vector<Entry> ended;
    for (auto& cur : m_entries)
        ended.push_back(cur.second);
    m_entries.clear();
    for (auto& cur : ended)
        OnEnd(cur);
}

void EwarState::Process()
{
    if (m_entries.empty())
        return;

    int64 now(GetFileTimeNow());
    SystemManager* pSystem(m_owner->SystemMgr());
    std::vector<Entry> ended;
    std::map<uint64_t, Entry>::iterator itr = m_entries.begin();
    while (itr != m_entries.end()) {
        bool drop(itr->second.expires <= now);
        if (!drop) {
            // the source died, left the system, docked or warped off
            SystemEntity* pSource(pSystem == nullptr ? nullptr : pSystem->GetSE(itr->second.sourceID));
            if ((pSource == nullptr) or pSource->IsDead()
            or ((pSource->SysBubble() != nullptr) and (pSource->SysBubble() != m_owner->SysBubble())))
                drop = true;
        }
        if (drop) {
            ended.push_back(itr->second);
            itr = m_entries.erase(itr);
        } else {
            ++itr;
        }
    }
    for (auto& cur : ended)
        OnEnd(cur);
}

void EwarState::OnStart(Entry& e)
{
    uint32 moduleID(e.moduleID != 0 ? e.moduleID : e.sourceID);
    switch (e.kind) {
        case Ewar::Jam: {
            // the target loses all its locks and can't lock anything until the jam ends
            if (m_owner->TargetMgr() != nullptr)
                m_owner->TargetMgr()->LoseAllTargets();
            Ewar::SendJamNotify(true, e.sourceID, moduleID, m_owner, e.start, e.durationMs);
            // the 'trying to jam' overview icon belongs to whoever runs the jammer (it lasts longer than a jam)
            return;
        }
        case Ewar::Web: {
            if ((m_owner->DestinyMgr() != nullptr) and (e.srcRef.get() != nullptr))
                m_owner->DestinyMgr()->WebbedMe(e.srcRef, true);
        } break;
        case Ewar::Scramble: {
            if ((m_owner->DestinyMgr() != nullptr) and m_owner->DestinyMgr()->IsAligningForWarp()) {
                m_owner->DestinyMgr()->Stop();
                if (m_owner->HasPilot())
                    m_owner->GetPilot()->SendNotifyMsg("Your warp drive has been scrambled.");
            }
        } break;
    }
    Ewar::SendEwarNotify(true, e.sourceID, moduleID, m_owner, Ewar::GetKindEwarType(e.kind));
}

void EwarState::OnEnd(Entry& e)
{
    uint32 moduleID(e.moduleID != 0 ? e.moduleID : e.sourceID);
    switch (e.kind) {
        case Ewar::Jam: {
            Ewar::SendJamNotify(false, e.sourceID, moduleID, m_owner);
            return;
        }
        case Ewar::Web: {
            if ((m_owner->DestinyMgr() != nullptr) and (e.srcRef.get() != nullptr))
                m_owner->DestinyMgr()->WebbedMe(e.srcRef, false);
        } break;
    }
    Ewar::SendEwarNotify(false, e.sourceID, moduleID, m_owner, Ewar::GetKindEwarType(e.kind));
}

bool EwarState::IsJammed() const
{
    int64 now(GetFileTimeNow());
    for (auto& cur : m_entries)
        if ((cur.second.kind == Ewar::Jam) and (cur.second.expires > now))
            return true;
    return false;
}

bool EwarState::IsJammedBy(uint32 sourceID, uint32 moduleID/*0*/) const
{
    std::map<uint64_t, Entry>::const_iterator itr = m_entries.find(MakeKey((moduleID != 0 ? moduleID : sourceID), Ewar::Jam));
    if (itr == m_entries.end())
        return false;
    return (itr->second.expires > GetFileTimeNow());
}

float EwarState::GetScrambleStrength() const
{
    float strength(0.0f);
    for (auto& cur : m_entries)
        if (cur.second.kind == Ewar::Scramble)
            strength += cur.second.v1;
    return strength;
}

float EwarState::Penalized(uint8 kind, bool second) const
{
    // stacking penalty: strongest first, nth effect at exp(-(n/2.67)^2)
    std::vector<float> mults;
    for (auto& cur : m_entries)
        if (cur.second.kind == kind) {
            float m(second ? cur.second.v2 : cur.second.v1);
            if ((m > 0.0f) and (m != 1.0f))
                mults.push_back(m);
        }
    if (mults.empty())
        return 1.0f;

    std::sort(mults.begin(), mults.end(), [](float a, float b) { return std::fabs(a - 1.0f) > std::fabs(b - 1.0f); });
    float result(1.0f);
    for (uint8 i = 0; i < mults.size(); ++i)
        result *= (1.0f + ((mults[i] - 1.0f) * std::exp(-std::pow(i / 2.67f, 2.0f))));
    return result;
}

float EwarState::GetTargetRangeMult() const
{
    return Penalized(Ewar::Damp, false);
}

float EwarState::GetScanResMult() const
{
    return Penalized(Ewar::Damp, true);
}

float EwarState::GetTrackingMult() const
{
    return Penalized(Ewar::TrackDisrupt, false);
}

float EwarState::GetOptimalMult() const
{
    return Penalized(Ewar::TrackDisrupt, true);
}

float EwarState::GetSigRadiusMult() const
{
    return Penalized(Ewar::Paint, false);
}
