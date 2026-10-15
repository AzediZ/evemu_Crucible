 /**
  * @name NpcSupport.cpp
  *   what an npc does for its friends:  remote repair, group assists, remote ecm (PLAN.md 4.7, 5.8, 5.9).  see NpcSupport.h
  */

#include "eve-server.h"

#include <iomanip>

#include "Client.h"
#include "EVEServerConfig.h"
#include "inventory/AttributeEnum.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/ai/AggroGroup.h"
#include "npc/ai/NpcProfile.h"
#include "npc/ai/NpcRng.h"
#include "npc/ai/NpcSupport.h"
#include "ship/modules/EwarModule.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/SpawnMgr.h"

namespace {
    // "under fire":  damaged this recently (ms)
    const int64 c_underFireMs = 10000;
    // how often an idle repairer looks for patients (ms):  in a fight, and out of one
    const int64 c_pollCombatMs = 1000;
    const int64 c_pollIdleMs = 5000;
}

NpcSupport::NpcSupport(NPC* pNPC, NPCAIMgr* pAI, const NpcProfile& profile, NpcRng& rng)
: m_npc(pNPC),
  m_ai(pAI),
  m_profile(profile),
  m_rng(rng),
  m_ecmAt(0),
  m_ecmBursts(0)
{
    m_armor = RepairState();
    m_shield = RepairState();
    m_assistAt.assign(m_profile.groupAssists.size(), 0);
    m_pending.reserve(8);
    m_patients.reserve(16);
}

bool NpcSupport::Needed(const NpcProfile& profile)
{
    return ((profile.remoteArmor.effectID != 0) or (profile.remoteShield.effectID != 0) or !profile.groupAssists.empty()
         or profile.remoteEcm.has);
}

void NpcSupport::StopAll()
{
    StopBeams(true, m_armor);
    StopBeams(false, m_shield);
    m_armor.cycleEnd = 0;
    m_shield.cycleEnd = 0;
    m_pending.clear();
    for (auto& cur : m_assistAt)
        cur = 0;
    m_ecmAt = 0;
}

template<typename F>
void NpcSupport::ForEachFriend(const F& fn)
{
    AggroGroup* pGroup(m_ai->GetGroup());
    if (pGroup == nullptr)
        return;
    SpawnMgr* pSpawnMgr(m_npc->SystemMgr()->GetSpawnMgr());
    if ((pGroup->GetRoomKey() != 0) and (pSpawnMgr != nullptr)) {
        // every group of our dungeon room (sleeper sites spawn their waves as groups of their own)
        pSpawnMgr->GetAggroGroups().ForEachRoomMember(pGroup->GetRoomKey(), fn);
        return;
    }
    pGroup->ForEachMember(fn);
}

bool NpcSupport::InCombat(int64 now, bool engaged)
{
    if (engaged or ((now - m_ai->GetLastDamagedAt()) < c_underFireMs))
        return true;
    // the group's answer is worked out once a second for all of its members
    AggroGroup* pGroup(m_ai->GetGroup());
    return ((pGroup != nullptr) and pGroup->AnyFighting(now, c_underFireMs));
}

bool NpcSupport::RepairDue(bool armor, const RepairState& state, int64 now) const
{
    if ((armor ? m_profile.remoteArmor.effectID : m_profile.remoteShield.effectID) == 0)
        return false;
    // switched off (incursions).  a cycle that was running ends with its beam
    if (m_ai->IsRemoteRepairsOff())
        return false;
    return ((state.cycleEnd != 0) ? (now >= state.cycleEnd) : (now >= state.pollAt));
}

bool NpcSupport::AssistDue(int64 now) const
{
    for (auto at : m_assistAt)
        if ((at == 0) or (now >= at))
            return true;
    return false;
}

void NpcSupport::StopBeams(bool armor, RepairState& state)
{
    // a cycle still running:  its beams stop with it (they'd run on to the end of the cycle otherwise)
    if ((state.cycleEnd != 0) and (m_npc->DestinyMgr() != nullptr))
        for (auto patientID : state.beams)
            m_npc->DestinyMgr()->SendSpecialEffect(m_npc->GetID(), m_npc->GetID(), m_npc->GetSelf()->typeID(), patientID, 0,
                                                   (armor ? "effects.RemoteArmourRepair" : "effects.ShieldTransfer"), false,
                                                   false, false, 0, 0);
    state.beams.clear();
}

void NpcSupport::Process(int64 now, bool engaged)
{
    if ((m_npc->SysBubble() == nullptr) or (m_npc->DestinyMgr() == nullptr) or m_npc->DestinyMgr()->IsWarping())
        return;
    if (!m_pending.empty())
        LandPending(now);
    /* our group's combat state (a walk over its members) only when something needs it:  a repair cycle ended, a poll for
     *  patients (1s in a fight, 5s out of one) or an assist is due.  otherwise this is a few comparisons a tick */
    bool armor(RepairDue(true, m_armor, now)), shield(RepairDue(false, m_shield, now)), assist(AssistDue(now));
    if (armor or shield or assist) {
        bool combat(InCombat(now, engaged));
        if (armor)
            ProcessRepair(true, m_armor, now, combat);
        if (shield)
            ProcessRepair(false, m_shield, now, combat);
        if (assist)
            ProcessAssists(now, combat);
    }
    if (m_profile.remoteEcm.has)
        ProcessRemoteEcm(now, engaged);
}

void NpcSupport::ProcessRepair(bool armor, RepairState& state, int64 now, bool combat)
{
    const NpcProfile::RemoteRepair& rr(armor ? m_profile.remoteArmor : m_profile.remoteShield);
    if (rr.effectID == 0)
        return;
    if (state.cycleEnd != 0) {
        if (now < state.cycleEnd)
            return;
        // that cycle is over (its beams ended with it):  the next one follows at once when someone needs it
        state.cycleEnd = 0;
        state.beams.clear();
    } else {
        // not cycling:  look for patients now and then
        if (now < state.pollAt)
            return;
    }
    state.pollAt = now + (combat ? c_pollCombatMs : c_pollIdleMs);
    if (StartCycle(armor, now)) {
        state.cycleEnd = now + rr.cycleMs;
        ++state.cycles;
    }
}

bool NpcSupport::StartCycle(bool armor, int64 now)
{
    const NpcProfile::RemoteRepair& rr(armor ? m_profile.remoteArmor : m_profile.remoteShield);
    SystemBubble* pBubble(m_npc->SysBubble());

    m_patients.clear();
    ForEachFriend([&](NPC* pNPC) {
        if ((pNPC == m_npc) or pNPC->IsDead() or (pNPC->GetAIMgr() == nullptr) or pNPC->IsLawEnforcer())
            return;
        if (pNPC->SysBubble() != pBubble)
            return;
        double distance(EvE::max(m_npc->GetPosition().distance(pNPC->GetPosition()) - m_npc->GetRadius() - pNPC->GetRadius()));
        if (distance > m_profile.assistRange)
            return;
        InventoryItemRef iRef(pNPC->GetSelf());
        Patient p = Patient();
            p.pNPC = pNPC;
        if (armor) {
            double hp(iRef->GetAttribute(AttrArmorHP).get_double());
            double damage(iRef->GetAttribute(AttrArmorDamage).get_double());
            double reserved(pNPC->GetAIMgr()->ReservedArmor(now));
            if (!NpcMath::ArmorRepairNeeded(hp, damage, reserved, rr.threshold))
                return;
            p.order.fraction = 1.0 - ((damage - reserved) / hp);
        } else {
            double capacity(iRef->GetAttribute(AttrShieldCapacity).get_double());
            double charge(iRef->GetAttribute(AttrShieldCharge).get_double());
            if (!NpcMath::ShieldRepairNeeded(capacity, charge, rr.threshold))
                return;
            p.order.fraction = charge / capacity;
        }
        p.order.priority = iRef->GetAttribute(AttrNpcAssistanceProirity).get_double();
        p.order.underFire = ((now - pNPC->GetAIMgr()->GetLastDamagedAt()) < c_underFireMs);
        m_patients.push_back(p);
    });
    if (m_patients.empty())
        return false;

    // "chance of the npc remote repairing its comrades", each cycle
    if (m_rng.Uniform() >= rr.chance) {
        m_patients.clear();
        return true;
    }
    std::sort(m_patients.begin(), m_patients.end(), [](const Patient& a, const Patient& b) {
        return NpcMath::RepairBefore(a.order, b.order);
    });

    DestinyManager* pDestiny(m_npc->DestinyMgr());
    size_t count(std::min(m_patients.size(), static_cast<size_t>(rr.maxTargets)));
    RepairState& state(armor ? m_armor : m_shield);
    state.beams.clear();
    for (size_t i = 0; i < count; ++i) {
        NPC* pNPC(m_patients[i].pNPC);
        state.beams.push_back(pNPC->GetID());
        if (armor) {
            // the repair lands at the end of the cycle.  until then it's reserved, so nobody else covers it again
            Pending pending = Pending();
                pending.patientID = pNPC->GetID();
                pending.amount = rr.amount;
                pending.at = now + rr.cycleMs;
            m_pending.push_back(pending);
            pNPC->GetAIMgr()->ReserveArmor(rr.amount, pending.at);
        } else {
            // shield boosts land at the start of the cycle
            InventoryItemRef iRef(pNPC->GetSelf());
            double capacity(iRef->GetAttribute(AttrShieldCapacity).get_double());
            double charge(iRef->GetAttribute(AttrShieldCharge).get_double() + rr.amount);
            iRef->SetAttribute(AttrShieldCharge, (charge > capacity ? capacity : charge));
            pNPC->SendDamageStateChanged();
        }
        pDestiny->SendSpecialEffect(m_npc->GetID(), m_npc->GetID(), m_npc->GetSelf()->typeID(), pNPC->GetID(), 0,
                                    (armor ? "effects.RemoteArmourRepair" : "effects.ShieldTransfer"), false, true, true,
                                    static_cast<int32>(rr.cycleMs), 1);
        _log(NPC__AI_TRACE, "%s(%u): Remote %s repair on %s(%u):  %.0f%% left, priority %.0f%s.", m_npc->GetName(), m_npc->GetID(),
                (armor ? "armor" : "shield"), pNPC->GetName(), pNPC->GetID(), m_patients[i].order.fraction * 100,
                m_patients[i].order.priority, (m_patients[i].order.underFire ? ", under fire" : ""));
    }
    m_patients.clear();
    return true;
}

void NpcSupport::LandPending(int64 now)
{
    size_t i(0);
    while (i < m_pending.size()) {
        if (m_pending[i].at > now) {
            ++i;
            continue;
        }
        SystemEntity* pSE(m_npc->SystemMgr()->GetSE(m_pending[i].patientID));
        if ((pSE != nullptr) and pSE->IsNPCSE() and !pSE->IsDead()) {
            InventoryItemRef iRef(pSE->GetSelf());
            double damage(iRef->GetAttribute(AttrArmorDamage).get_double() - m_pending[i].amount);
            iRef->SetAttribute(AttrArmorDamage, (damage < 0.0 ? 0.0 : damage));
            pSE->SendDamageStateChanged();
        }
        m_pending[i] = m_pending.back();
        m_pending.pop_back();
    }
}

void NpcSupport::ProcessAssists(int64 now, bool combat)
{
    AggroGroup* pGroup(m_ai->GetGroup());
    for (size_t i = 0; i < m_assistAt.size(); ++i) {
        const NpcProfile::GroupAssist& ga(m_profile.groupAssists[i]);
        if (!combat) {
            m_assistAt[i] = 0;
            continue;
        }
        // the first one when the fight starts, then every duration
        if ((m_assistAt[i] != 0) and (now < m_assistAt[i]))
            continue;
        m_assistAt[i] = now + ga.durationMs;
        if (m_rng.Uniform() >= ga.chance)
            continue;
        float mult(static_cast<float>(((ga.kind == NpcAI::Buff::ShieldResist) or (ga.kind == NpcAI::Buff::ArmorResist))
                ? NpcMath::AssistResonanceMult(ga.bonus) : NpcMath::AssistBonusMult(ga.bonus)));
        // one second of overlap keeps the bonus from dropping between cycles
        uint32 duration(ga.durationMs + 1000);
        uint32 count(0);
        auto give = [&](NPC* pNPC) {
            if (pNPC->IsDead() or (pNPC->GetAIMgr() == nullptr) or (pNPC->SysBubble() != m_npc->SysBubble()))
                return;
            double distance(EvE::max(m_npc->GetPosition().distance(pNPC->GetPosition()) - m_npc->GetRadius() - pNPC->GetRadius()));
            if ((pNPC != m_npc) and (distance > m_profile.assistRange))
                return;
            pNPC->GetAIMgr()->ApplyBuff(ga.kind, mult, duration);
            ++count;
        };
        if (pGroup != nullptr) {
            pGroup->ForEachMember(give);
        } else {
            give(m_npc);
        }
        m_npc->DestinyMgr()->SendSpecialEffect(m_npc->GetID(), m_npc->GetID(), m_npc->GetSelf()->typeID(), m_npc->GetID(), 0,
                                               "effects.ElectronicAttributeModifyActivate", false, true, true,
                                               static_cast<int32>(ga.durationMs), 1);
        _log(NPC__AI_TRACE, "%s(%u): Group assist %u (kind %u, x%.2f) on %u npcs for %ums.", m_npc->GetName(), m_npc->GetID(),
                ga.effectID, ga.kind, mult, count, ga.durationMs);
    }
}

void NpcSupport::ProcessRemoteEcm(int64 now, bool engaged)
{
    if (!engaged) {
        m_ecmAt = 0;
        return;
    }
    SystemBubble* pBubble(m_npc->SysBubble());
    const NpcProfile::RemoteEcm& ecm(m_profile.remoteEcm);
    // "the current duration for the remote ecm (this is recalculated each time it is activated)"
    double interval(NpcMath::RemoteEcmDurationMs(ecm.baseMs, ecm.scale, static_cast<double>(pBubble->CountPlayers()),
                                                 ecm.intendedPlayers, ecm.playerStep, ecm.minMs));
    if (m_ecmAt == 0) {
        // the first burst one interval into the fight
        m_ecmAt = now + static_cast<int64>(interval);
        return;
    }
    if (now < m_ecmAt)
        return;
    m_ecmAt = now + static_cast<int64>(interval);
    if (m_rng.Uniform() >= ecm.chance)
        return;

    uint32 jamMs(std::max<uint32>(sConfig.npc.RemoteEcmJamTime, 1) * 1000);
    uint32 count(0);
    pBubble->ForEachPlayer([&](Client* pClient) {
        SystemEntity* pShip(pClient->GetShipSE());
        if ((pShip == nullptr) or pShip->IsDead() or pClient->InPod() or pClient->IsInvul())
            return;
        if ((pShip->DestinyMgr() == nullptr) or pShip->DestinyMgr()->IsCloaked() or pShip->DestinyMgr()->IsWarping())
            return;
        // sieged/triaged ships and supercapitals are immune
        if (pShip->GetSelf()->GetAttribute(AttrDisallowOffensiveModifiers).get_bool())
            return;
        Ewar::StartJam(m_npc, 0, pShip, jamMs);
        ++count;
    });
    ++m_ecmBursts;
    m_npc->DestinyMgr()->SendSpecialEffect(m_npc->GetID(), m_npc->GetID(), m_npc->GetSelf()->typeID(), m_npc->GetID(), 0,
                                           "effects.ECMBurst", true, true, true, static_cast<int32>(jamMs), 0);
    _log(NPC__AI_TRACE, "%s(%u): Remote ecm burst jams %u ships for %ums.  Next in %.0fms.", m_npc->GetName(), m_npc->GetID(),
            count, jamMs, interval);
}

std::string NpcSupport::Describe(int64 now) const
{
    std::ostringstream str;
    str << std::fixed << std::setprecision(0);
    str << "support:";
    auto repair = [&](const char* name, const NpcProfile::RemoteRepair& rr, const RepairState& state) {
        if (rr.effectID == 0)
            return;
        str << "  " << name << " " << rr.amount << "/" << rr.cycleMs << "ms (chance " << std::setprecision(2) << rr.chance
            << ", threshold " << rr.threshold << std::setprecision(0) << ", " << (uint32)rr.maxTargets << " targets, "
            << state.cycles << " cycles" << (state.cycleEnd != 0 ? ", cycling" : "") << ")";
    };
    repair("remote armor", m_profile.remoteArmor, m_armor);
    repair("remote shield", m_profile.remoteShield, m_shield);
    if (!m_profile.groupAssists.empty()) {
        str << "  assists";
        for (auto& cur : m_profile.groupAssists)
            str << " " << cur.effectID << "(" << cur.bonus << "%/" << cur.durationMs << "ms)";
    }
    if (m_profile.remoteEcm.has) {
        str << "  remote ecm " << m_ecmBursts << " bursts";
        if (m_ecmAt != 0)
            str << ", next in " << EvE::max(m_ecmAt - now) << "ms";
    }
    str << ", reach " << m_profile.assistRange << "m, " << m_pending.size() << " pending<br>";
    return str.str();
}
