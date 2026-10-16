/**
 * @name Outpost.cpp
 *   Class for Outposts.
 *
 * @Author:           James
 * @date:   17 October 2021
 */

#include "station/Outpost.h"
#include "Client.h"
#include "system/SystemManager.h"
#include "StaticDataMgr.h"
#include "manufacturing/FactoryDB.h"
#include "station/StationDataMgr.h"
#include "../eve-common/EVE_Outpost.h"
#include "../eve-common/EVE_Station.h"
#include "../eve-common/EVE_Mail.h"
#include "../eve-common/EVE_Roles.h"
#include "ConsoleCommands.h"
#include "EntityList.h"
#include "mail/NotificationMgr.h"
#include "pos/PosMgrDB.h"
#include "station/StationDB.h"
#include "system/DestinyManager.h"
#include "system/TargetManager.h"
#include "system/sov/SovereigntyDataMgr.h"

OutpostSE::OutpostSE(StationItemRef station, EVEServiceManager &services, SystemManager* system)
: StationSE(station, services, system),
m_conquerable(false),
m_vulnerable(false),
m_timers(false),
m_dirty(false),
m_damagePending(false),
m_phase(OutpostSiege::Phase::Shield),
m_timerEnd(0),
m_lastProcess(GetFileTimeNow()),
m_lastSave(m_lastProcess),
m_lastOwnerCheck(0),
m_lastAttackNotify(0),
m_lastServiceNotify(0),
m_lastDamageSend(0)
{
    /* the type's hit points (100m shield, 150m armor, 100m structure).  the conquerable stations have a signature radius,
     *  the outposts none:  turrets and missiles need one */
    const ItemType& type(station->type());
    float shield(type.HasAttribute(AttrShieldCapacity) ? type.GetAttribute(AttrShieldCapacity).get_float() : 20000000.0f);
    station->SetAttribute(AttrOnline,             EvilOne, false);
    station->SetAttribute(AttrCapacity,           STATION_HANGAR_MAX_CAPACITY, false);
    station->SetAttribute(AttrInertia,            EvilOne, false);
    station->SetAttribute(AttrShieldCapacity,     shield, false);
    station->SetAttribute(AttrShieldCharge,       shield, false);
    station->SetAttribute(AttrArmorDamage,        EvilZero, false);
    station->SetAttribute(AttrDamage,             EvilZero, false);
    if (type.HasAttribute(AttrArmorHP))
        station->SetAttribute(AttrArmorHP,        type.GetAttribute(AttrArmorHP), false);
    if (type.HasAttribute(AttrHP))
        station->SetAttribute(AttrHP,             type.GetAttribute(AttrHP), false);
    if (type.HasAttribute(AttrShieldRechargeRate))
        station->SetAttribute(AttrShieldRechargeRate, type.GetAttribute(AttrShieldRechargeRate), false);
    station->SetAttribute(AttrSignatureRadius,    (type.HasAttribute(AttrSignatureRadius) ? type.GetAttribute(AttrSignatureRadius).get_float() : 10000.0f), false);
    station->SetAttribute(AttrArmorUniformity,    station->GetAttribute(AttrArmorUniformity), false);
    station->SetAttribute(AttrMass,               station->type().mass(), false);
    station->SetAttribute(AttrRadius,             station->type().radius(), false);
    station->SetAttribute(AttrVolume,             station->type().volume(), false);

    // outposts can be locked (DogmaIMService AddTarget), so they need a target manager
    m_targMgr = new TargetManager(this);

    // the npc conquerable stations in npc null-sec stay npc stations
    m_conquerable = (IsConquerableType(station->typeID()) and (system != nullptr) and svDataMgr.IsClaimable(system->GetID()));
    UpdateOwner();

    // a siege in progress
    uint8 phase(0);
    double charge(0), armorDamage(0), damage(0);
    int64 timerEnd(0), updated(0);
    if (m_conquerable and StationDB::LoadSiege(station->itemID(), phase, charge, armorDamage, damage, timerEnd, updated)) {
        if (phase <= (uint8)OutpostSiege::Phase::Structure)
            m_phase = (OutpostSiege::Phase)phase;
        station->SetAttribute(AttrShieldCharge,   EvE::min(charge, (double)shield), false);
        station->SetAttribute(AttrArmorDamage,    armorDamage, false);
        station->SetAttribute(AttrDamage,         damage, false);
        m_timerEnd = timerEnd;
        // shields came back while the system was unloaded
        if ((updated > 0) and (updated < m_lastProcess))
            m_lastProcess = updated;
        _log(POS__MESSAGE, "OutpostSE() - %s(%u) is under siege:  phase %u, shield %.0f, armor damage %.0f, damage %.0f",
             station->name(), station->itemID(), phase, charge, armorDamage, damage);
    }
    UpdateVulnerability();
}

OutpostSE::~OutpostSE()
{
    if (m_dirty)
        SaveSiege();
    if (m_targMgr != nullptr)
        if (!sEntityList.IsClosing()) {
            m_targMgr->ClearModules();
            m_targMgr->ClearAllTargets(false);
        }
    SafeDelete(m_targMgr);
}

bool OutpostSE::IsConquerableType(uint16 typeID)
{
    if (OutpostRules::OutpostRace(typeID) != OutpostRules::Race::None)
        return true;
    switch (typeID) {
        case 12242:     // Station ( Conquerable 1 )
        case 12294:     // Station ( Conquerable 2 )
        case 12295:     // Station ( Conquerable 3 )
            return true;
    }
    return false;
}

void OutpostSE::UpdateOwner()
{
    StationData data = StationData();
    if (!stDataMgr.GetStationData(m_self->itemID(), data))
        return;
    m_corpID = data.corporationID;
    m_ownerID = data.corporationID;
    m_allyID = (IsPlayerCorp(data.corporationID) ? (int32)PosMgrDB::GetCorpAllianceID(data.corporationID) : 0);
    m_lastOwnerCheck = GetFileTimeNow();
}

void OutpostSE::UpdateVulnerability()
{
    if (!m_conquerable or (m_system == nullptr)) {
        m_vulnerable = false;
        m_timers = false;
        return;
    }
    uint32 sovAllianceID(svDataMgr.GetSystemAllianceID(m_system->GetID()));
    uint32 ownerAllianceID(m_allyID > 0 ? (uint32)m_allyID : 0);
    bool wasVulnerable(m_vulnerable);
    m_timers = OutpostSiege::HasTimers(ownerAllianceID, sovAllianceID);
    m_vulnerable = OutpostSiege::Vulnerable(ownerAllianceID, sovAllianceID, svDataMgr.IsClaimVulnerable(m_system));

    // the blockade broke:  "the siege is considered over".  a reinforcement runs its course first
    if (wasVulnerable and !m_vulnerable and !OutpostSiege::IsReinforced(m_phase) and (m_phase != OutpostSiege::Phase::Shield)) {
        _log(POS__MESSAGE, "OutpostSE::UpdateVulnerability() - %s(%u):  the blockade is broken, the siege is over", GetName(), GetID());
        m_phase = OutpostSiege::Phase::Shield;
        m_dirty = true;
    }
}

bool OutpostSE::IsInvul()
{
    if (!m_conquerable or OutpostSiege::IsReinforced(m_phase))
        return true;
    return !m_vulnerable;
}

void OutpostSE::GetDamageLimits(DamageLimits& into)
{
    OutpostSiege::HitLimits limits(OutpostSiege::Limits(m_phase, m_timers));
    into.shieldFloor = limits.shieldFloor;
    into.armorFloor = limits.armorFloor;
    into.canDie = false;
}

PyDict* OutpostSE::MakeSlimItem()
{
    PyDict* slim(StationSE::MakeSlimItem());
    // the client shows the reinforcement effects from this (spaceObject.Station.HandleStateChange)
    slim->SetItemString("structureState", new PyInt(OutpostSiege::ClientState(m_phase)));
    return slim;
}

void OutpostSE::SendSlimUpdate()
{
    // stations are global balls with no destiny manager:  every pilot in space here has it
    if (m_system == nullptr)
        return;
    std::vector<Client*> clients;
    m_system->GetClientList(clients);
    for (auto cur : clients) {
        if ((cur == nullptr) or !cur->IsInSpace())
            continue;
        PyTuple* data = new PyTuple(2);
            data->SetItem(0, new PyLong(m_self->itemID()));
            data->SetItem(1, new PyObject("foo.SlimItem", MakeSlimItem()));
        PyTuple* update = new PyTuple(2);
            update->SetItem(0, new PyString("OnSlimItemChange"));
            update->SetItem(1, data);
        cur->QueueDestinyUpdate(&update);
    }
}

void OutpostSE::FlushDamageState()
{
    // everyone in space here sees the station's bars, not only those locking it
    m_damagePending = false;
    m_lastDamageSend = GetFileTimeNow();
    if (m_system == nullptr)
        return;
    std::vector<Client*> clients;
    m_system->GetClientList(clients);
    for (auto cur : clients) {
        if ((cur == nullptr) or !cur->IsInSpace())
            continue;
        OnDamageStateChange dmgChange;
            dmgChange.entityID = m_self->itemID();
            dmgChange.state = SystemEntity::MakeDamageState();
        PyTuple* up = dmgChange.Encode();
        cur->QueueDestinyUpdate(&up);
    }
}

void OutpostSE::OnSiegeDamage(SystemEntity* pAttacker)
{
    if (!m_conquerable)
        return;
    m_dirty = true;

    // the pilot behind the hit:  ships' pilots, and drones' controllers
    uint32 charID(0), corpID(0);
    if (pAttacker != nullptr) {
        if (pAttacker->HasPilot()) {
            charID = pAttacker->GetPilot()->GetCharacterID();
            corpID = pAttacker->GetPilot()->GetCorporationID();
        } else if (pAttacker->IsDroneSE() and IsCharacterID(pAttacker->GetSelf()->ownerID())) {
            charID = pAttacker->GetSelf()->ownerID();
            corpID = pAttacker->GetCorporationID();
        }
    }
    NotifyAttack(pAttacker);

    double shieldCap(m_self->GetAttribute(AttrShieldCapacity).get_double());
    double armorHP(m_self->GetAttribute(AttrArmorHP).get_double());
    double hp(m_self->GetAttribute(AttrHP).get_double());
    double shieldLeft(shieldCap > 0 ? m_self->GetAttribute(AttrShieldCharge).get_double() / shieldCap : 0.0);
    double armorLeft(armorHP > 0 ? 1.0 - (m_self->GetAttribute(AttrArmorDamage).get_double() / armorHP) : 0.0);

    // structure gone:  the station goes to the last pilot's corporation (npcs and npc corporations take nothing)
    if ((hp > 0) and (m_self->GetAttribute(AttrDamage).get_double() >= hp)) {
        if (IsPlayerCorp(corpID) and (corpID != m_corpID))
            Conquer(corpID, charID);
        return;
    }

    OutpostSiege::Phase next(OutpostSiege::AfterHit(m_phase, m_timers, shieldLeft, armorLeft));
    if (next != m_phase)
        EnterReinforcement(next);
}

void OutpostSE::EnterReinforcement(OutpostSiege::Phase phase)
{
    // the owner's exit time, plus the type's 48 hours, give or take its 3 hours
    int64 duration((int64)m_self->GetAttribute(AttrreinforcementDuration).get_float());
    int64 variance((int64)m_self->GetAttribute(AttrreinforcementVariance).get_float());
    if (duration <= 0)
        duration = 172800;
    int64 offset(variance > 0 ? MakeRandomInt(-variance, variance) : 0);
    int64 now(GetFileTimeNow());
    // filetime starts at midnight (1 Jan 1601), so its seconds work out the time of day as well
    int64 nowSec(now / EvE::Time::Second);
    int64 endSec(OutpostSiege::ReinforcementEnd(nowSec, StationDB::GetExitTime(m_self->itemID()), duration, offset));

    m_phase = phase;
    m_timerEnd = now + ((endSec - nowSec) * EvE::Time::Second);
    m_dirty = true;
    SaveSiege();
    SendSlimUpdate();
    SendDamageStateChanged();
    // the owner hears about it straight away
    NotifyAttack(nullptr, true);
    _log(POS__MESSAGE, "OutpostSE::EnterReinforcement() - %s(%u) is %s reinforced for %.1f hours", GetName(), GetID(),
         (phase == OutpostSiege::Phase::ShieldReinforced ? "shield" : "armor"), (double)(endSec - nowSec) / 3600.0);
}

void OutpostSE::EndReinforcement()
{
    // out at 0% shield, or at 25% armor
    if (m_phase == OutpostSiege::Phase::ShieldReinforced) {
        m_self->SetAttribute(AttrShieldCharge, m_self->GetAttribute(AttrShieldCapacity).get_double() * OutpostSiege::ShieldExit);
    } else if (m_phase == OutpostSiege::Phase::ArmorReinforced) {
        double armorHP(m_self->GetAttribute(AttrArmorHP).get_double());
        m_self->SetAttribute(AttrArmorDamage, armorHP * (1.0 - OutpostSiege::ArmorExit));
    }
    m_phase = OutpostSiege::AfterReinforcement(m_phase);
    m_timerEnd = 0;
    // the blockade broke while it was reinforced:  the siege is over
    UpdateVulnerability();
    if (!m_vulnerable)
        m_phase = OutpostSiege::Phase::Shield;
    m_lastProcess = GetFileTimeNow();
    m_dirty = true;
    SaveSiege();
    SendSlimUpdate();
    SendDamageStateChanged();
    _log(POS__MESSAGE, "OutpostSE::EndReinforcement() - %s(%u) is out of reinforcement (phase %u)%s", GetName(), GetID(),
         (uint8)m_phase, (m_vulnerable ? "" : ", and invulnerable"));
}

void OutpostSE::Conquer(uint32 corpID, uint32 charID)
{
    uint32 stationID(m_self->itemID()), oldCorpID(m_corpID);
    int32 oldAllianceID(m_allyID);

    // the station's owner everywhere:  the database, the station data the clients get and the station item
    StationDB::SetStationOwner(stationID, corpID);
    StationData data = StationData();
    if (stDataMgr.GetStationData(stationID, data)) {
        data.corporationID = corpID;
        stDataMgr.UpdateStationData(data);
    }
    // the item's owner is kept in memory only:  staStations holds it (station items are static, SaveItem refuses them)
    StationItemRef sRef(sItemFactory.GetStationRef(stationID));
    if (sRef.get() != nullptr) {
        sRef->ChangeOwner(corpID, false);
        sRef->UpdateSettings();
    }
    UpdateOwner();
    // the service structures go with it, as they are (shot down ones stay down)
    for (auto id : m_serviceIDs) {
        SystemEntity* pSE(m_system->GetSE(id));
        if ((pSE != nullptr) and pSE->IsStationServiceSE())
            pSE->GetStationServiceSE()->SetOwner(corpID, m_allyID);
    }

    /* what the new owner gets isn't written down anywhere.  the structure is repaired and the shield and armor are left as
     *  they are, so the next attacker has to grind the structure again rather than take it with one shot */
    m_self->SetAttribute(AttrDamage, EvilZero);
    m_phase = OutpostSiege::Phase::Shield;
    m_timerEnd = 0;
    m_dirty = true;
    SaveSiege();
    UpdateVulnerability();
    SendSlimUpdate();
    SendDamageStateChanged();

    _log(POS__MESSAGE, "OutpostSE::Conquer() - %s(%u) taken from corp %u by corp %u (char %u)", GetName(), stationID, oldCorpID, corpID, charID);

    // 'Station conquered' to both owners
    for (auto toCorp : {oldCorpID, corpID}) {
        if (!IsPlayerCorp(toCorp))
            continue;
        PyDict* args = new PyDict();
            args->SetItemString("stationID",        new PyInt(stationID));
            args->SetItemString("solarSystemID",    new PyInt(m_system->GetID()));
            args->SetItemString("oldOwnerID",       new PyInt(oldCorpID));
            args->SetItemString("newOwnerID",       new PyInt(corpID));
            args->SetItemString("charID",           (charID == 0 ? PyStatic.NewNone() : new PyInt(charID)));
        NotificationMgr::SendToCorp(toCorp, 0, Notify::Types::StationConquer, corpCONCORD, args);
    }

    // the sovereignty holder lost (or got) a station here:  the claiming TCU's protection follows (SovereigntyDataMgr)
    if ((oldAllianceID != m_allyID) and (svDataMgr.GetSystemAllianceID(m_system->GetID()) > 0))
        svDataMgr.UpdateBlockade(m_system, 0, false);
}

void OutpostSE::NotifyAttack(SystemEntity* pAttacker, bool force/*false*/)
{
    // at most once an hour, as tower alerts.  reinforcements always tell
    int64 now(GetFileTimeNow());
    if (!force and ((now - m_lastAttackNotify) < EvE::Time::Hour))
        return;
    m_lastAttackNotify = now;
    if (!IsPlayerCorp(m_corpID))
        return;

    uint32 aggressorID(0), aggressorCorpID(0), aggressorAllyID(0);
    if (pAttacker != nullptr) {
        if (pAttacker->HasPilot()) {
            Client* pPilot(pAttacker->GetPilot());
            aggressorID = pPilot->GetCharacterID();
            aggressorCorpID = pPilot->GetCorporationID();
            aggressorAllyID = pPilot->GetAllianceID();
        } else {
            aggressorID = pAttacker->GetOwnerID();
            aggressorCorpID = pAttacker->GetCorporationID();
            aggressorAllyID = pAttacker->GetAllianceID();
        }
    }
    double shieldCap(m_self->GetAttribute(AttrShieldCapacity).get_double());
    double armorHP(m_self->GetAttribute(AttrArmorHP).get_double());
    double hp(m_self->GetAttribute(AttrHP).get_double());

    PyDict* data = new PyDict();
        data->SetItemString("aggressorID",          (aggressorID == 0 ? PyStatic.NewNone() : new PyInt(aggressorID)));
        data->SetItemString("aggressorCorpID",      (aggressorCorpID == 0 ? PyStatic.NewNone() : new PyInt(aggressorCorpID)));
        data->SetItemString("aggressorAllianceID",  (IsAlliance(aggressorAllyID) ? new PyInt(aggressorAllyID) : PyStatic.NewNone()));
        data->SetItemString("stationID",            new PyInt(m_self->itemID()));
        data->SetItemString("solarSystemID",        new PyInt(m_system->GetID()));
        data->SetItemString("typeID",               new PyInt(m_self->typeID()));
        data->SetItemString("shieldValue",          new PyFloat(shieldCap > 0 ? m_self->GetAttribute(AttrShieldCharge).get_double() / shieldCap : 0.0));
        data->SetItemString("armorValue",           new PyFloat(armorHP > 0 ? 1.0 - m_self->GetAttribute(AttrArmorDamage).get_double() / armorHP : 0.0));
        data->SetItemString("hullValue",            new PyFloat(hp > 0 ? 1.0 - m_self->GetAttribute(AttrDamage).get_double() / hp : 0.0));
    NotificationMgr::SendToCorp(m_corpID, Corp::Role::StationManager, Notify::Types::StationAggression2, m_corpID, data);
}

void OutpostSE::OnServiceAttacked(StationServiceSE* pService, SystemEntity* pAttacker)
{
    int64 now(GetFileTimeNow());
    if ((pService == nullptr) or ((now - m_lastServiceNotify) < EvE::Time::Hour))
        return;
    m_lastServiceNotify = now;
    if (!IsPlayerCorp(m_corpID))
        return;

    uint32 aggressorID(0), aggressorCorpID(0), aggressorAllyID(0);
    StructureSE::GetAggressor(pAttacker, aggressorID, aggressorCorpID, aggressorAllyID);
    InventoryItemRef sRef(pService->GetSelf());
    double shieldCap(sRef->GetAttribute(AttrShieldCapacity).get_double());
    double armorHP(sRef->GetAttribute(AttrArmorHP).get_double());
    double hp(sRef->GetAttribute(AttrHP).get_double());

    PyDict* data = new PyDict();
        data->SetItemString("aggressorID",          (aggressorID == 0 ? PyStatic.NewNone() : new PyInt(aggressorID)));
        data->SetItemString("aggressorCorpID",      (aggressorCorpID == 0 ? PyStatic.NewNone() : new PyInt(aggressorCorpID)));
        data->SetItemString("aggressorAllianceID",  (IsAlliance(aggressorAllyID) ? new PyInt(aggressorAllyID) : PyStatic.NewNone()));
        data->SetItemString("stationID",            new PyInt(m_self->itemID()));
        data->SetItemString("solarSystemID",        new PyInt(m_system->GetID()));
        // the service's structure type
        data->SetItemString("typeID",               new PyInt(sRef->typeID()));
        data->SetItemString("shieldValue",          new PyFloat(shieldCap > 0 ? sRef->GetAttribute(AttrShieldCharge).get_double() / shieldCap : 0.0));
        data->SetItemString("armorValue",           new PyFloat(armorHP > 0 ? 1.0 - sRef->GetAttribute(AttrArmorDamage).get_double() / armorHP : 0.0));
        data->SetItemString("hullValue",            new PyFloat(hp > 0 ? 1.0 - sRef->GetAttribute(AttrDamage).get_double() / hp : 0.0));
    NotificationMgr::SendToCorp(m_corpID, Corp::Role::StationManager, Notify::Types::StationAggression1, m_corpID, data);
}

void OutpostSE::ProcessSiege()
{
    int64 now(GetFileTimeNow());
    // the service structures (they come back online once repaired)
    for (auto id : m_serviceIDs) {
        SystemEntity* pSE(m_system->GetSE(id));
        if ((pSE != nullptr) and pSE->IsStationServiceSE())
            pSE->GetStationServiceSE()->ProcessService(now);
    }
    if (!m_conquerable)
        return;
    // the owner's alliance may change
    if ((now - m_lastOwnerCheck) >= EvE::Time::Minute)
        UpdateOwner();
    UpdateVulnerability();

    if (OutpostSiege::IsReinforced(m_phase)) {
        if ((m_timerEnd > 0) and (now >= m_timerEnd))
            EndReinforcement();
        m_lastProcess = now;
    } else if ((now - m_lastProcess) >= (10 * EvE::Time::Second)) {
        // shields come back by themselves.  armor and structure need repairing
        double capacity(m_self->GetAttribute(AttrShieldCapacity).get_double());
        double charge(m_self->GetAttribute(AttrShieldCharge).get_double());
        if (charge < capacity) {
            double rechargeSec(m_self->GetAttribute(AttrShieldRechargeRate).get_double() / 1000.0);
            double dt((double)(now - m_lastProcess) / EvE::Time::Second);
            m_self->SetAttribute(AttrShieldCharge, OutpostSiege::RechargedShield(charge, capacity, rechargeSec, dt), false);
            m_dirty = true;
        }
        m_lastProcess = now;
    }

    if (m_damagePending and ((now - m_lastDamageSend) >= EvE::Time::Second))
        FlushDamageState();
    if (m_dirty and ((now - m_lastSave) >= EvE::Time::Minute))
        SaveSiege();
}

void OutpostSE::SaveSiege()
{
    m_lastSave = GetFileTimeNow();
    m_dirty = false;
    if (!m_conquerable)
        return;
    StationDB::SaveSiege(m_self->itemID(), (uint8)m_phase, m_self->GetAttribute(AttrShieldCharge).get_double(),
                         m_self->GetAttribute(AttrArmorDamage).get_double(), m_self->GetAttribute(AttrDamage).get_double(), m_timerEnd);
}

bool OutpostSE::GMEndReinforcement()
{
    if (!m_conquerable)
        return false;
    if (OutpostSiege::IsReinforced(m_phase)) {
        EndReinforcement();
        return true;
    }
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

void OutpostSE::GMRepair()
{
    m_self->SetAttribute(AttrShieldCharge, m_self->GetAttribute(AttrShieldCapacity));
    m_self->SetAttribute(AttrArmorDamage, EvilZero);
    m_self->SetAttribute(AttrDamage, EvilZero);
    m_phase = OutpostSiege::Phase::Shield;
    m_timerEnd = 0;
    SaveSiege();
    SendSlimUpdate();
    SendDamageStateChanged();
    for (auto id : m_serviceIDs) {
        SystemEntity* pSE(m_system->GetSE(id));
        if ((pSE != nullptr) and pSE->IsStationServiceSE())
            pSE->GetStationServiceSE()->GMRepair();
    }
}

/* station services (OutpostServices).  their structures sit at the station's position, as Crucible's did */
void OutpostSE::SpawnStationServices(uint32 oldMask, uint32 newMask)
{
    if (m_system == nullptr)
        return;
    for (auto& cur : OutpostServices::All) {
        uint32 covers(OutpostServices::Covers(cur.typeID));
        if (((newMask & covers) == 0) or ((oldMask & covers) != 0) or (GetService(cur.typeID) != nullptr))
            continue;
        const ItemType* pType(sItemFactory.GetType(cur.typeID));
        ItemData data(cur.typeID, m_corpID, m_system->GetID(), flagStructureActive,
                      (pType != nullptr ? pType->name().c_str() : ""), GetPosition());
        InventoryItemRef iRef(sItemFactory.SpawnItem(data));
        if (iRef.get() == nullptr) {
            _log(POS__ERROR, "OutpostSE::SpawnStationServices() - %s(%u):  could not make service %u", GetName(), GetID(), cur.typeID);
            continue;
        }
        iRef->SaveItem();
        FactionData fData = FactionData();
            fData.ownerID = m_corpID;
            fData.corporationID = m_corpID;
            fData.allianceID = m_allyID;
        StationServiceSE* pSSE(new StationServiceSE(iRef, m_services, m_system, fData));
        pSSE->Save();
        m_system->AddEntity(pSSE, false);
        _log(POS__MESSAGE, "OutpostSE::SpawnStationServices() - %s(%u):  %s(%u) added", GetName(), GetID(), iRef->name(), iRef->itemID());
    }
}

void OutpostSE::InitServices()
{
    // npc stations in npc null-sec can't be besieged, so their services can't be shot either
    if (!m_conquerable)
        return;
    StationData data = StationData();
    if (stDataMgr.GetStationData(m_self->itemID(), data))
        SpawnStationServices(0, data.serviceMask);
}

StationServiceSE* OutpostSE::GetService(uint16 typeID)
{
    if (m_system == nullptr)
        return nullptr;
    for (auto id : m_serviceIDs) {
        SystemEntity* pSE(m_system->GetSE(id));
        if ((pSE != nullptr) and pSE->IsStationServiceSE() and (pSE->GetTypeID() == typeID))
            return pSE->GetStationServiceSE();
    }
    return nullptr;
}

bool OutpostSE::ServicesVulnerable()
{
    return (m_conquerable and m_vulnerable and !OutpostSiege::IsReinforced(m_phase));
}

bool OutpostSE::IsServiceOffline(uint32 stationID, uint32 serviceIDs)
{
    SystemManager* pSys(sEntityList.FindSystem(sDataMgr.GetStationSystem(stationID)));
    if (pSys == nullptr)
        return false;
    SystemEntity* pSE(pSys->GetSE(stationID));
    if ((pSE == nullptr) or !pSE->IsOutpostSE())
        return false;
    OutpostSE* pOSE(pSE->GetOutpostSE());
    for (auto id : pOSE->m_serviceIDs) {
        SystemEntity* pSvc(pSys->GetSE(id));
        if ((pSvc == nullptr) or !pSvc->IsStationServiceSE())
            continue;
        StationServiceSE* pSSE(pSvc->GetStationServiceSE());
        if (!pSSE->IsOnline() and ((pSSE->GetCovers() & serviceIDs) != 0))
            return true;
    }
    return false;
}

void OutpostSE::CheckServiceOnline(uint32 stationID, uint32 serviceIDs)
{
    if (!IsServiceOffline(stationID, serviceIDs))
        return;
    uint16 typeID(OutpostServices::TypeFor(serviceIDs));
    const ItemType* pType(typeID ? sItemFactory.GetType(typeID) : nullptr);
    throw CustomError("The %s of this station has been shot down. It can't be used until it is repaired to half its shield and full armor and structure.",
                      (pType != nullptr ? pType->name().c_str() : "service"));
}

StationServiceSE::StationServiceSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data)
: ObjectSystemEntity(self, services, system),
m_stationID(0),
m_covers(OutpostServices::Covers(self->typeID())),
m_online(true),
m_dirty(false),
m_lastProcess(GetFileTimeNow()),
m_lastSave(m_lastProcess)
{
    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;

    // the type's hit points.  shield, armor and structure damage are the item's own (saved attributes)
    const ItemType& type(self->type());
    double shield(type.HasAttribute(AttrShieldCapacity) ? type.GetAttribute(AttrShieldCapacity).get_double() : 0.0);
    self->SetAttribute(AttrShieldCapacity, shield, false);
    if (!self->HasAttribute(AttrShieldCharge))
        self->SetAttribute(AttrShieldCharge, shield, false);
    if (!self->HasAttribute(AttrArmorDamage))
        self->SetAttribute(AttrArmorDamage, EvilZero, false);
    if (!self->HasAttribute(AttrDamage))
        self->SetAttribute(AttrDamage, EvilZero, false);
    if (type.HasAttribute(AttrSignatureRadius))
        self->SetAttribute(AttrSignatureRadius, type.GetAttribute(AttrSignatureRadius), false);
    self->SetAttribute(AttrRadius, type.radius(), false);
    self->SetAttribute(AttrMass, type.mass(), false);

    // the station it belongs to sits at the same position
    if (system != nullptr)
        for (auto cur : system->GetOutposts())
            if ((cur != nullptr) and (cur->GetPosition().distance(GetPosition()) < 10.0)) {
                m_stationID = cur->GetID();
                cur->AddService(self->itemID());
                break;
            }
    if (m_stationID == 0)
        _log(POS__WARNING, "StationServiceSE() - %s(%u) has no station at its position", self->name(), self->itemID());

    // older outposts made theirs with no flag (GetStationServiceStates read that as off)
    m_online = (self->flag() != flagStructureInactive);
    if (m_online and (self->flag() != flagStructureActive)) {
        self->SetFlag(flagStructureActive, false);
        self->SaveItem();
    }
}

StationServiceSE::~StationServiceSE()
{
    if (m_dirty)
        Save();
}

OutpostSE* StationServiceSE::GetStation()
{
    if ((m_stationID == 0) or (m_system == nullptr))
        return nullptr;
    SystemEntity* pSE(m_system->GetSE(m_stationID));
    return ((pSE != nullptr) ? pSE->GetOutpostSE() : nullptr);
}

bool StationServiceSE::IsInvul()
{
    if (ObjectSystemEntity::IsInvul())
        return true;
    OutpostSE* pOSE(GetStation());
    return ((pOSE == nullptr) or !pOSE->ServicesVulnerable());
}

void StationServiceSE::EncodeDestiny(Buffer& into)
{
    // a rigid ball inside the station.  not massive:  ships fly through it to dock
    using namespace Destiny;
    BallHeader head = BallHeader();
        head.entityID = m_self->itemID();
        head.mode = Ball::Mode::RIGID;
        head.radius = m_radius;
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = 0;
    into.Append( head );
    RIGID_Struct main;
        main.formationID = 0xFF;
    into.Append( main );
}

void StationServiceSE::OnDamaged(SystemEntity* pAttacker)
{
    m_dirty = true;
    OutpostSE* pOSE(GetStation());
    if (pOSE != nullptr)
        pOSE->OnServiceAttacked(this, pAttacker);
    if (m_online and OutpostServices::Incapacitated(m_self->GetAttribute(AttrDamage).get_double(), m_self->GetAttribute(AttrHP).get_double(),
                     (m_self->HasAttribute(AttrIncapacitationRatio) ? m_self->GetAttribute(AttrIncapacitationRatio).get_double() : OutpostServices::IncapacitationRatio)))
        SetOnline(false);
}

void StationServiceSE::ProcessService(int64 now)
{
    // shields come back by themselves (very slowly:  the types' recharge time is 23 days).  armor and structure need repairing
    if ((now - m_lastProcess) >= (10 * EvE::Time::Second)) {
        double capacity(m_self->GetAttribute(AttrShieldCapacity).get_double());
        double charge(m_self->GetAttribute(AttrShieldCharge).get_double());
        if (charge < capacity) {
            double rechargeSec(m_self->GetAttribute(AttrShieldRechargeRate).get_double() / 1000.0);
            double dt((double)(now - m_lastProcess) / EvE::Time::Second);
            m_self->SetAttribute(AttrShieldCharge, OutpostSiege::RechargedShield(charge, capacity, rechargeSec, dt), false);
            m_dirty = true;
        }
        m_lastProcess = now;
        // remote repairs change the attributes directly
        if (!m_online or (m_self->GetAttribute(AttrArmorDamage).get_double() > 0) or (m_self->GetAttribute(AttrDamage).get_double() > 0))
            m_dirty = true;
    }
    if (!m_online and OutpostServices::BackOnline(m_self->GetAttribute(AttrShieldCharge).get_double(), m_self->GetAttribute(AttrShieldCapacity).get_double(),
                                                  m_self->GetAttribute(AttrArmorDamage).get_double(), m_self->GetAttribute(AttrDamage).get_double()))
        SetOnline(true);
    if (m_dirty and ((now - m_lastSave) >= EvE::Time::Minute))
        Save();
}

void StationServiceSE::Save()
{
    m_lastSave = GetFileTimeNow();
    m_dirty = false;
    m_self->SaveAttributes();
}

void StationServiceSE::SetOnline(bool online)
{
    if (m_online == online)
        return;
    m_online = online;
    m_self->SetFlag(online ? flagStructureActive : flagStructureInactive, false);
    m_self->SaveItem();
    Save();

    // factory and laboratory jobs here stand still while their service is off
    if ((m_covers & OutpostServices::Factory) != 0) {
        if (online)
            FactoryDB::ResumeJobsInContainer(m_stationID, 1);
        else
            FactoryDB::PauseJobsInContainer(m_stationID, 1);
    } else if ((m_covers & OutpostServices::Laboratory) != 0) {
        if (online)
            FactoryDB::ResumeJobsInContainer(m_stationID, 2);
        else
            FactoryDB::PauseJobsInContainer(m_stationID, 2);
    }

    uint32 serviceID(OutpostServices::ServiceID(m_self->typeID()));
    uint32 systemID(m_system != nullptr ? m_system->GetID() : 0);
    // the service buttons of the pilots docked there (lobby.OnProcessStationServiceItemChange)
    if (m_stationID != 0) {
        PyTuple* tuple = new PyTuple(5);
            tuple->SetItem(0, new PyInt(m_stationID));
            tuple->SetItem(1, new PyInt(systemID));
            tuple->SetItem(2, new PyInt(serviceID));
            tuple->SetItem(3, new PyInt(m_self->itemID()));
            tuple->SetItem(4, new PyBool(online));
        sEntityList.Multicast("OnProcessStationServiceItemChange", "stationid", &tuple, NOTIF_DEST__LOCATION, m_stationID);
    }
    // 'Outpost service disabled' and 're-enabled' (StationStateChange:  entities.STATE_INCAPACITATED and STATE_IDLE)
    if (IsPlayerCorp(m_corpID) and (m_stationID != 0)) {
        PyDict* data = new PyDict();
            data->SetItemString("state",                new PyInt(online ? 0 : -1));
            data->SetItemString("stationID",            new PyInt(m_stationID));
            data->SetItemString("solarSystemID",        new PyInt(systemID));
            data->SetItemString("typeID",               new PyInt(m_self->typeID()));
            data->SetItemString("serviceID",            new PyInt(serviceID));
            data->SetItemString("stationServiceItemID", new PyInt(m_self->itemID()));
        NotificationMgr::SendToCorp(m_corpID, Corp::Role::StationManager, Notify::Types::StationStateChange, m_corpID, data);
    }
    _log(POS__MESSAGE, "StationServiceSE::SetOnline() - %s(%u) at station %u is %s", GetName(), GetID(), m_stationID, (online ? "back online" : "shot down"));
}

void StationServiceSE::SetOwner(uint32 corpID, int32 allianceID)
{
    m_corpID = corpID;
    m_ownerID = corpID;
    m_allyID = allianceID;
    m_self->ChangeOwner(corpID, false);
    m_self->SaveItem();
}

void StationServiceSE::GMRepair()
{
    m_self->SetAttribute(AttrShieldCharge, m_self->GetAttribute(AttrShieldCapacity), false);
    m_self->SetAttribute(AttrArmorDamage, EvilZero, false);
    m_self->SetAttribute(AttrDamage, EvilZero, false);
    SetOnline(true);
    Save();
    SendDamageStateChanged();
}

bool OutpostSE::Refresh(uint32 stationID)
{
    StationData data = StationData();
    if (!stDataMgr.GetStationData(stationID, data))
        return false;
    OutpostRules::Race race(OutpostRules::OutpostRace(data.typeID));
    if (race == OutpostRules::Race::None)
        return false;

    OutpostRules::Slots slots = {{0, 0, 0, 0, 0, 0}};
    for (uint8 i = 0; i < OutpostRules::SlotCount; ++i)
        slots[i] = data.improvements[i];

    // services:  the operation's (staOperationServices) and what the improvements add
    data.serviceMask = stDataMgr.GetOperationServiceMask(data.operationID) | OutpostRules::AddedServices(slots);
    data.officeSlots = (uint8)OutpostRules::OfficeSlots(race, OutpostRules::BaseOfficeSlots(race), slots);
    data.reprocessingEfficiency = (float)OutpostRules::RefineryEfficiency(race, OutpostRules::TierOf(slots, OutpostRules::Kind::Refinery),
                                                                          StationDB::GetStationTypeReprocessingEfficiency(data.typeID));

    // assembly lines:  the type's own (ramInstallationTypeContents) as the improvements change them
    std::vector<std::pair<uint8, uint8>> contents;
    sDataMgr.GetInstallationContents(data.typeID, contents);
    std::map<uint8, uint16> base;
    for (auto& cur : contents)
        base[cur.first] += cur.second;
    FactoryDB::SyncStationAssemblyLines(stationID, data.typeID, data.corporationID, data.systemID, data.regionID,
                                        OutpostRules::AssemblyLines(race, base, slots));

    uint16 saved[6];
    for (uint8 i = 0; i < OutpostRules::SlotCount; ++i)
        saved[i] = slots[i];
    StationDB::SaveOutpostImprovements(stationID, saved);
    StationDB::SaveOutpostStats(stationID, data.upgradeLevel, data.officeSlots, data.reprocessingEfficiency);
    stDataMgr.UpdateStationData(data);

    // the station item keeps its own copy (office count, refining)
    StationItemRef sRef(sItemFactory.GetStationRef(stationID));
    if (sRef.get() != nullptr)
        sRef->UpdateSettings();

    _log(POS__MESSAGE, "OutpostSE::Refresh() - %s(%u):  level %u, services 0x%x, %u offices, refining %.2f",
         data.name.c_str(), stationID, data.upgradeLevel, (uint32)data.serviceMask, data.officeSlots, data.reprocessingEfficiency);
    return true;
}
