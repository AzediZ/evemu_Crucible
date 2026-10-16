
/**
 * @name IHub.cpp
 *   Class for Infrastructure Hubs.
 *
 * @Author:         James
 * @date:   8 April 2021
 */

/*
 * POS__ERROR
 * POS__WARNING
 * POS__MESSAGE
 * POS__DUMP
 * POS__DEBUG
 * POS__DESTINY
 * POS__SLIMITEM
 * POS__TRACE
 */


#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "planet/Planet.h"
#include "pos/sovStructures/IHub.h"
#include "system/Container.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/sov/SovereigntyDB.h"
#include "../eve-common/EVE_Mail.h"

IHubSE::IHubSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& fData)
: StructureSE(structure, services, system, fData),
m_phase(OutpostSiege::Phase::Shield),
m_timerEnd(0),
m_lastProcess(GetFileTimeNow()),
m_lastSave(m_lastProcess),
m_dirty(false)
{
    // a siege that was running
    uint8 phase(0);
    double charge(0), armorDamage(0), damage(0);
    int64 timerEnd(0), updated(0);
    if (SovereigntyDB::LoadHubSiege(structure->itemID(), phase, charge, armorDamage, damage, timerEnd, updated)) {
        if (phase <= (uint8)OutpostSiege::Phase::Structure)
            m_phase = (OutpostSiege::Phase)phase;
        structure->SetAttribute(AttrShieldCharge,   EvE::min(charge, structure->GetAttribute(AttrShieldCapacity).get_double()), false);
        structure->SetAttribute(AttrArmorDamage,    armorDamage, false);
        structure->SetAttribute(AttrDamage,         damage, false);
        m_timerEnd = timerEnd;
        // shields came back while the system was unloaded
        if ((updated > 0) and (updated < m_lastProcess))
            m_lastProcess = updated;
        _log(POS__MESSAGE, "IHubSE() - %s(%u) is under siege:  phase %u, shield %.0f, armor damage %.0f, damage %.0f",
             structure->name(), structure->itemID(), phase, charge, armorDamage, damage);
    }
}

IHubSE::~IHubSE()
{
    if (m_dirty)
        SaveSiege();
}


void IHubSE::Init()
{
    _log(SE__TRACE, "IHubSE %s(%u) is being initialised", m_self->name(), m_self->itemID());
    StructureSE::Init();

    // check for valid bubble
    if (m_bubble == nullptr)
        assert(0);
    m_bubble->SetIHubSE(this);

    // set global attribute
    m_self->SetAttribute(AttrIsGlobal, EvilOne, false);
}

void IHubSE::Process()
{
    StructureSE::Process();

    // upgrades installed since the last tic
    if (svDataMgr.TakeUpgradesDirty(m_data.itemID))
        svDataMgr.RefreshUpgrades(m_system->GetID(), m_data.itemID);

    ProcessSiege();
}

void IHubSE::SetOnline()
{
    _log(SOV__DEBUG, "Onlining IHub... Updating claim'd hubID.");
    StructureSE::SetOnline();
    svDataMgr.UpdateSystemHubID(m_self->locationID(), m_self->itemID());
    svDataMgr.RefreshUpgrades(m_system->GetID(), m_data.itemID);
    // the claim's hub shares its vulnerability
    svDataMgr.UpdateBlockade(m_system, 0, false);
}

void IHubSE::SetOffline()
{
    _log(SOV__DEBUG, "Offlining IHub... Resetting claim's hubID.");
    if (svDataMgr.GetSovereigntyData(m_system->GetID()).hubID == m_data.itemID)
        svDataMgr.UpdateSystemHubID(m_self->locationID(), 0);
    svDataMgr.RefreshUpgrades(m_system->GetID(), m_data.itemID);
    StructureSE::SetOffline();
    // without its hub the claiming TCU is exposed to a blockade
    svDataMgr.UpdateBlockade(m_system, 0, false);
}

void IHubSE::OnRemovedFromSpace(bool killed)
{
    StructureSE::OnRemovedFromSpace(killed);

    // the siege goes with it
    m_dirty = false;
    SovereigntyDB::DeleteHubSiege(m_data.itemID);

    uint32 allianceID(svDataMgr.GetSystemAllianceID(m_system->GetID()));
    // upgrades are destroyed with the hub
    svDataMgr.RemoveHub(m_system->GetID(), m_data.itemID);
    // without its hub the claiming TCU is exposed to a blockade
    svDataMgr.UpdateBlockade(m_system, m_data.itemID, false);

    if (killed and (allianceID > 0)) {
        // 'The Infrastructure Hub belonging to {owner} has been destroyed'
        PyDict* params = new PyDict();
            params->SetItemString("owner", new PyString(sDataMgr.GetOwnerName(allianceID)));
        svDataMgr.SendAudioEvent(allianceID, Sov::AudioEvent::HubDestroyed, params);
    }
}

bool IHubSE::IsInvul()
{
    // reinforced, it can't be shot or repaired
    if (OutpostSiege::IsReinforced(m_phase))
        return true;
    // the claim's hub can only be attacked while SBUs are online at more than half the stargates
    SovereigntyData sovData = svDataMgr.GetSovereigntyData(m_system->GetID());
    if ((sovData.claimID > 0) and (sovData.hubID == m_data.itemID))
        return !svDataMgr.IsClaimVulnerable(m_system);
    return StructureSE::IsInvul();
}

bool IHubSE::HasTimers()
{
    if (m_system == nullptr)
        return false;
    SovereigntyData sovData = svDataMgr.GetSovereigntyData(m_system->GetID());
    return ((sovData.claimID > 0) and (sovData.hubID == m_data.itemID)
        and OutpostSiege::HasTimers((m_allyID > 0 ? (uint32)m_allyID : 0), sovData.allianceID));
}

void IHubSE::GetDamageLimits(DamageLimits& into)
{
    OutpostSiege::HitLimits limits(OutpostSiege::Limits(m_phase, HasTimers()));
    into.shieldFloor = limits.shieldFloor;
    into.armorFloor = limits.armorFloor;
}

PyDict* IHubSE::MakeSlimItem()
{
    PyDict* slim(StructureSE::MakeSlimItem());
    // the client shows 'shield reinforced' / 'armor reinforced' (pos.STRUCTURE_SHIELD/ARMOR_REINFORCE) until posTimestamp
    if (OutpostSiege::IsReinforced(m_phase)) {
        slim->SetItemString("posState", new PyInt(OutpostSiege::ClientState(m_phase)));
        slim->SetItemString("posTimestamp", new PyLong(m_timerEnd));
    }
    return slim;
}

void IHubSE::OnDamaged(SystemEntity* pAttacker)
{
    StructureSE::OnDamaged(pAttacker);
    m_dirty = true;
    NotifyAttack(pAttacker);

    double shieldCap(m_self->GetAttribute(AttrShieldCapacity).get_double());
    double armorHP(m_self->GetAttribute(AttrArmorHP).get_double());
    double shieldLeft(shieldCap > 0 ? m_self->GetAttribute(AttrShieldCharge).get_double() / shieldCap : 0.0);
    double armorLeft(armorHP > 0 ? 1.0 - (m_self->GetAttribute(AttrArmorDamage).get_double() / armorHP) : 0.0);

    OutpostSiege::Phase next(OutpostSiege::AfterHit(m_phase, HasTimers(), shieldLeft, armorLeft));
    if (next != m_phase)
        EnterReinforcement(next);
}

void IHubSE::EnterReinforcement(OutpostSiege::Phase phase)
{
    // the type's 24 hours, give or take its 3 hours, from now
    int64 duration((int64)m_self->GetAttribute(AttrreinforcementDuration).get_float());
    int64 variance((int64)m_self->GetAttribute(AttrreinforcementVariance).get_float());
    if (duration <= 0)
        duration = 86400;
    int64 offset(variance > 0 ? MakeRandomInt(-variance, variance) : 0);
    int64 now(GetFileTimeNow());

    m_phase = phase;
    m_timerEnd = now + ((duration + offset) * EvE::Time::Second);
    m_dirty = true;
    SaveSiege();
    SendSlimUpdate();
    SendDamageStateChanged();
    // the owner hears about it straight away
    NotifyAttack(nullptr, true);
    if (m_allyID > 0) {
        // 'The Infrastructure Hub belonging to {owner} has been reinforced'
        PyDict* params = new PyDict();
            params->SetItemString("owner", new PyString(sDataMgr.GetOwnerName(m_allyID)));
            params->SetItemString("alliance", new PyString(sDataMgr.GetOwnerName(m_allyID)));
            params->SetItemString("system", new PyString(sDataMgr.GetSystemName(m_system->GetID())));
            params->SetItemString("solarSystemID", new PyInt(m_system->GetID()));
        svDataMgr.SendAudioEvent(m_allyID, Sov::AudioEvent::HubReinforced, params);
    }
    _log(POS__MESSAGE, "IHubSE::EnterReinforcement() - %s(%u) is %s reinforced for %.1f hours", GetName(), m_data.itemID,
         (phase == OutpostSiege::Phase::ShieldReinforced ? "shield" : "armor"), (double)(duration + offset) / 3600.0);
}

void IHubSE::EndReinforcement()
{
    // out at 0% shield, or at 25% armor (and no shield)
    m_self->SetAttribute(AttrShieldCharge, m_self->GetAttribute(AttrShieldCapacity).get_double() * OutpostSiege::ShieldExit);
    if (m_phase == OutpostSiege::Phase::ArmorReinforced) {
        double armorHP(m_self->GetAttribute(AttrArmorHP).get_double());
        m_self->SetAttribute(AttrArmorDamage, armorHP * (1.0 - OutpostSiege::ArmorExit));
    }
    m_phase = OutpostSiege::AfterReinforcement(m_phase);
    m_timerEnd = 0;
    // the blockade broke while it was reinforced:  the siege is over
    if (IsInvul())
        m_phase = OutpostSiege::Phase::Shield;
    m_lastProcess = GetFileTimeNow();
    m_dirty = true;
    SaveSiege();
    SendSlimUpdate();
    SendDamageStateChanged();
    _log(POS__MESSAGE, "IHubSE::EndReinforcement() - %s(%u) is out of reinforcement (phase %u)%s", GetName(), m_data.itemID,
         (uint8)m_phase, (IsInvul() ? ", and invulnerable" : ""));
}

void IHubSE::ProcessSiege()
{
    int64 now(GetFileTimeNow());
    if (OutpostSiege::IsReinforced(m_phase)) {
        if ((m_timerEnd > 0) and (now >= m_timerEnd))
            EndReinforcement();
        // a reinforced hub's shield is frozen
        m_lastProcess = now;
    } else if ((now - m_lastProcess) >= (10 * EvE::Time::Second)) {
        // the blockade broke between the timers:  "the siege is considered over"
        if ((m_phase != OutpostSiege::Phase::Shield) and (!HasTimers() or IsInvul())) {
            _log(POS__MESSAGE, "IHubSE::ProcessSiege() - %s(%u):  the blockade is broken, the siege is over", GetName(), m_data.itemID);
            m_phase = OutpostSiege::Phase::Shield;
            m_dirty = true;
            SendSlimUpdate();
        }
        // shields come back by themselves.  armor and structure need repairing
        double capacity(m_self->GetAttribute(AttrShieldCapacity).get_double());
        double charge(m_self->GetAttribute(AttrShieldCharge).get_double());
        if (charge < capacity) {
            double rechargeSec(m_self->GetAttribute(AttrShieldRechargeRate).get_double() / 1000.0);
            double dt((double)(now - m_lastProcess) / EvE::Time::Second);
            m_self->SetAttribute(AttrShieldCharge, OutpostSiege::RechargedShield(charge, capacity, rechargeSec, dt), false);
            m_dirty = true;
            SendDamageStateChanged();
        }
        m_lastProcess = now;
    }

    if (m_dirty and ((now - m_lastSave) >= EvE::Time::Minute))
        SaveSiege();
}

void IHubSE::SaveSiege()
{
    m_lastSave = GetFileTimeNow();
    m_dirty = false;
    SovereigntyDB::SaveHubSiege(m_data.itemID, (uint8)m_phase, m_self->GetAttribute(AttrShieldCharge).get_double(),
                                m_self->GetAttribute(AttrArmorDamage).get_double(), m_self->GetAttribute(AttrDamage).get_double(), m_timerEnd);
}

void IHubSE::NotifyAttack(SystemEntity* pAttacker, bool force/*false*/)
{
    NotifyStructureAttack(pAttacker, Notify::Types::SovereigntyIHDamage, force);
}

bool IHubSE::GMEndReinforcement()
{
    if (OutpostSiege::IsReinforced(m_phase)) {
        EndReinforcement();
        return true;
    }
    if (!HasTimers())
        return false;
    // into the next reinforcement, with the layer at its threshold
    if (m_phase == OutpostSiege::Phase::Shield) {
        m_self->SetAttribute(AttrShieldCharge, m_self->GetAttribute(AttrShieldCapacity).get_double() * OutpostSiege::ShieldReinforceAt);
        EnterReinforcement(OutpostSiege::Phase::ShieldReinforced);
        return true;
    }
    if (m_phase == OutpostSiege::Phase::Armor) {
        m_self->SetAttribute(AttrShieldCharge, EvilZero);
        m_self->SetAttribute(AttrArmorDamage, m_self->GetAttribute(AttrArmorHP).get_double() * (1.0 - OutpostSiege::ArmorReinforceAt));
        EnterReinforcement(OutpostSiege::Phase::ArmorReinforced);
        return true;
    }
    return false;
}

void IHubSE::GMRepair()
{
    m_self->SetAttribute(AttrShieldCharge, m_self->GetAttribute(AttrShieldCapacity));
    m_self->SetAttribute(AttrArmorDamage, EvilZero);
    m_self->SetAttribute(AttrDamage, EvilZero);
    m_phase = OutpostSiege::Phase::Shield;
    m_timerEnd = 0;
    m_dirty = false;
    SovereigntyDB::DeleteHubSiege(m_data.itemID);
    SendSlimUpdate();
    SendDamageStateChanged();
}
