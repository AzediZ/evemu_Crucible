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
        if (!sConsole.IsShutdown()) {
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

void OutpostSE::ProcessSiege()
{
    if (!m_conquerable)
        return;
    int64 now(GetFileTimeNow());
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
}

// Each outpost has station service entities which are spawned at the same position as the station itself
void OutpostSE::SpawnStationService(Client* pClient, StationData stData, uint32 serviceType) {
    ItemData svcData(serviceType, stData.corporationID, stData.systemID, flagNone);
    InventoryItemRef svcRef = sItemFactory.SpawnItem(svcData);
    ItemSystemEntity* svcSE = new ItemSystemEntity(svcRef, pClient->services(), pClient->SystemMgr());
    svcSE->SetPosition(stData.position);
    svcRef->SaveItem();
    pClient->SystemMgr()->AddEntity(svcSE);
}

void OutpostSE::SpawnStationServices(Client* pClient, const StationData& stData, uint32 oldMask, uint32 newMask)
{
    // the service entities StationDB::GetStationServiceStates knows
    static const std::pair<uint32, uint32> services[6] = {
        {Station::Fitting,              EVEDB::invTypes::FittingService},
        {Station::ReprocessingPlant,    EVEDB::invTypes::ReprocessingService},
        {Station::Factory,              EVEDB::invTypes::FactoryService},
        {Station::Cloning,              EVEDB::invTypes::CloningService},
        {Station::RepairFacilities,     EVEDB::invTypes::RepairService},
        {Station::Laboratory,           EVEDB::invTypes::LaboratoryService}
    };
    for (auto& cur : services)
        if (((newMask & cur.first) != 0) and ((oldMask & cur.first) == 0))
            SpawnStationService(pClient, stData, cur.second);
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
