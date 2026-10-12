
/**
 * @name Tower.cpp
 *   Class for POS Towers.
 *
 * @Author:         Allan
 * @date:   28 December 17
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
#include "planet/Moon.h"
#include "pos/Tower.h"
#include "system/Container.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/CalendarDB.h"
#include "mail/NotificationMgr.h"
#include "system/CrimeWatch.h"
#include "faction/WarManager.h"
#include "../eve-common/EVE_Mail.h"
#include "../eve-common/EVE_Calendar.h"

/** @todo (Allan) this class needs more research to finish
 * see pics in ::GamePC/G/games/EvE/misc/POS
 * flagStructureActive             = 144,
 * flagStructureInactive           = 145,
 * AttrOperationConsumptionRate = 687,
 * AttrReinforcedConsumptionRate = 688,
 * AttrResourceReinforced1Type = 694,
 * AttrResourceReinforced2Type = 695,
 * AttrResourceReinforced3Type = 696,
 * AttrResourceReinforced4Type = 697,
 * AttrResourceReinforced5Type = 698,
 * AttrResourceReinforced1Quantity = 699,
 * AttrResourceReinforced2Quantity = 700,
 * AttrResourceReinforced3Quantity = 701,
 * AttrResourceReinforced4Quantity = 703,
 * AttrResourceReinforced5Quantity = 704,
 * AttrResourceOnline1Type = 705,
 * AttrResourceOnline2Type = 706,
 * AttrResourceOnline3Type = 707,
 * AttrResourceOnline4Type = 708,
 ***  many other attributes for towers and their modules.....
 * AttrControlTowerMissileVelocityBonus = 792,
 * AttrControlTowerSize = 1031,
 * AttrAnchoringSecurityLevelMax = 1032,
 * AttrAnchoringRequiresSovereignty = 1033,
 * AttrControlTowerMinimumDistance = 1165,
 * AttrPosPlayerControlStructure = 1167,
 * AttrIsIncapacitated = 1168,
 * AttrPosStructureControlAmount = 1174,
 * AttrOnliningRequiresSovereigntyLevel = 1185,
 * AttrPosAnchoredPerSolarSystemAmount = 1195,
 * AttrPosStructureControlDistanceMax = 1214,
 * AttrAnchoringRequiresSovereigntyLevel = 1215,
 * AttrHarvesterType = 709,
 * AttrHarvesterQuality = 710,
 * AttrMoonAnchorDistance = 711,
 * AttrUsageDamagePercent = 712,
 * AttrConsumptionType = 713,
 * AttrConsumptionQuantity = 714,
 * AttrMaxOperationalDistance = 715,    for sma, hangar, etc
 * AttrMaxOperationalUsers = 716,    for sma, hangar, etc
 * AttrRefiningYieldMultiplier = 717,
 * AttrOperationalDuration = 719,
 * AttrRefineryCapacity = 720,
 * AttrRefiningDelayMultiplier = 721,
 * AttrPosControlTowerPeriod = 722,
 * AttrMoonMiningAmount = 726,
 * AttrControlTowerLaserDamageBonus = 728,
 * AttrControlTowerLaserOptimalBonus = 750,
 * AttrControlTowerHybridOptimalBonus = 751,
 * AttrControlTowerProjectileOptimalBonus = 752,
 * AttrControlTowerProjectileFallOffBonus = 753,
 * AttrControlTowerProjectileROFBonus = 754,
 * AttrControlTowerMissileROFBonus = 755,
 * AttrControlTowerMoonHarvesterCPUBonus = 756,
 * AttrControlTowerSiloCapacityBonus = 757,
 * AttrControlTowerLaserProximityRangeBonus = 760,
 * AttrControlTowerProjectileProximityRangeBonus = 761,
 * AttrControlTowerHybridProximityRangeBonus = 762,
 * AttrMaxGroupActive = 763,
 * AttrControlTowerEwRofBonus = 764,
 * AttrScanRange = 765,
 * AttrControlTowerHybridDamageBonus = 766,
 * AttrTrackingSpeedBonus = 767,
 * AttrMaxRangeBonus2 = 769,
 * AttrControlTowerEwTargetSwitchDelayBonus = 770,
 * AttrAmmoCapacity = 771,
 * AttrActivationBlocked = 1349,
 * AttrActivationBlockedStrenght = 1350,
 * AttrPosCargobayAcceptType = 1351,
 * AttrPosCargobayAcceptGroup = 1352,
 */

TowerSE::TowerSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& fData)
: StructureSE(structure, services, system, fData),
m_pShieldSE(nullptr),
m_lastAttackNotify(0),
m_fuelOut(false),
m_catchUp(false),
m_replaying(false),
m_pgUsed(0.0f),
m_cpuUsed(0.0f)
{
    m_hasShield = false;
    m_structs.clear();

    // create AI object for tower here....not written yet.
    //m_ai = new POS_AI(this);

    m_pg = m_self->GetAttribute(AttrPowerOutput).get_int();
    m_cpu = m_self->GetAttribute(AttrCpuOutput).get_int();

    m_tsize = m_self->GetAttribute(AttrControlTowerSize).get_int();
    if ((m_tsize < 1) or (m_tsize > 3))
        m_tsize = 1;  // do something constructive here cause size is wrong
    m_soi = m_self->GetAttribute(AttrPosStructureControlDistanceMax).get_int() * m_tsize;

    m_tdata = EVEPOS::TowerData();

    /** @note these are defined, but i dunno what they are
     * AttrControlTowerMinimumDistance
     *
     *
     *
     *
     *
     */
}

void TowerSE::Init()
{
    StructureSE::Init();

    if (!m_db.GetTowerData(m_tdata, m_data)) {
        _log(SE__TRACE, "TowerSE %s(%u) has no saved data.  Initializing default set.", m_self->name(), m_self->itemID());
        // invalid data....init to 0 as this will only hit for currently-launching items (or errors)
        InitData();
    }
    // towers saved before standingOwnerID was kept
    if (m_tdata.standingOwnerID == 0)
        m_tdata.standingOwnerID = m_corpID;

    // an online tower catches up on its first tic (its structures aren't all initialized yet)
    m_catchUp = IsPowered();
    // moon process links
    m_links.clear();
    PosMgrDB::GetProcessLinks(m_data.itemID, m_links);

    // an online tower with a password has its force field
    m_harmonic = (IsPowered() ? EVEPOS::Harmonic::Online : EVEPOS::Harmonic::Offline);
    if (IsPowered() and !m_tdata.password.empty())
        CreateForceField(false);

    // if tower anchored, tell moon this is its tower
    if ((m_data.state > EVEPOS::StructureState::Unanchored) and (m_moonSE != nullptr))
        m_moonSE->SetTower(this);

    // set tower in bubble (a tower launched next to an anchored one doesn't take its place)
    if ((m_bubble != nullptr) and ((m_data.state > EVEPOS::StructureState::Unanchored) or !m_bubble->HasTower()))
        m_bubble->SetTowerSE(this);

    /** @todo
     * will have to check if Online or Operating
     *   in these two cases, remove resources based on time running.
     * if resources run out, reset state and set timer accordingly.
     * if reinforced, remove resources and continue until timer runs out.
     *
     * timer is in base StructureSE code.
     */

    // take resources, move items, process reactions or whatever needs to be done (follow PI proc code)
}

void TowerSE::InitData() {
    // init base data first
    StructureSE::InitData();
    m_tdata.harmonic = m_harmonic;     // set during base SE creation
    m_tdata.standingOwnerID = m_corpID;     // sentries use the owner corp's standings until alliance standings are chosen

    m_db.SaveTowerData(m_tdata, m_data);
}

bool TowerSE::ConsidersHostile(Client* pClient)
{
    if (pClient == nullptr)
        return false;
    if ((uint32)pClient->GetCorporationID() == m_corpID)
        return false;
    if (IsAlliance(m_allyID) and (pClient->GetAllianceID() == m_allyID))
        return false;

    if (!IsSentrySettingOff(m_tdata.standing)) {
        uint32 ownerID(m_tdata.standingOwnerID > 0 ? m_tdata.standingOwnerID : m_corpID);
        if (GetContactStanding(ownerID, pClient) < m_tdata.standing)
            return true;
    }

    if (!IsSentrySettingOff(m_tdata.status))
        if (pClient->GetSecurityRating() < m_tdata.status)
            return true;

    // corpWar:  those at war with the owner
    if (m_tdata.corpWar and sConfig.war.Enabled
    and sWarMgr.IsHostile(sWarMgr.WarOwnerOf(m_corpID), sWarMgr.WarOwnerOf(pClient->GetCorporationID())))
        return true;
    return false;
}

float TowerSE::GetContactStanding(uint32 ownerID, Client* pClient)
{
    // every battery asks every tick:  standings changes reach the guns within a minute
    int64 now(GetFileTimeNow());
    uint32 charID(pClient->GetCharacterID());
    std::map<uint32, CachedStanding>::iterator itr(m_standingCache.find(charID));
    if ((itr != m_standingCache.end()) and (itr->second.ownerID == ownerID) and (itr->second.expires > now))
        return itr->second.standing;

    float standing(0.0f);
    if (!PosMgrDB::GetContactStanding(ownerID, charID, standing))
        if (!PosMgrDB::GetContactStanding(ownerID, pClient->GetCorporationID(), standing))
            if (IsAlliance(pClient->GetAllianceID()))
                PosMgrDB::GetContactStanding(ownerID, pClient->GetAllianceID(), standing);

    // drop expired pilots now and then
    if (m_standingCache.size() > 200)
        for (itr = m_standingCache.begin(); itr != m_standingCache.end(); )
            if (itr->second.expires <= now)
                itr = m_standingCache.erase(itr);
            else
                ++itr;
    CachedStanding& entry(m_standingCache[charID]);
    entry.ownerID = ownerID;
    entry.standing = standing;
    entry.expires = now + EvE::Time::Minute;
    return standing;
}

void TowerSE::Killed(Damage& damage)
{
    // the force field goes, the moon is free again and the tower's structures are orphaned
    if (m_hasShield) {
        m_pShieldSE->Delete();
        SafeDelete(m_pShieldSE);
        m_hasShield = false;
    }
    if ((m_moonSE != nullptr) and (m_moonSE->GetMyTower() == this))
        m_moonSE->SetTower(nullptr);
    if ((m_bubble != nullptr) and (m_bubble->GetTowerSE() == this))
        m_bubble->SetTowerSE(nullptr);
    OrphanModules();
    // the last alert:  destroyed
    NotifyAttack(this, damage.srcSE, true);
    // no more fuel expiry
    if (m_tdata.fuelEventID != 0)
        CalendarDB::DeleteEvent(m_tdata.fuelEventID);

    StructureSE::Killed(damage);
}

int8 TowerSE::GetDeployFlag(int8 action)
{
    switch (action) {
        case EVEPOS::Deploy::Anchor:    return m_tdata.anchor;
        case EVEPOS::Deploy::Unanchor:  return m_tdata.unanchor;
        case EVEPOS::Deploy::Online:    return m_tdata.online;
        case EVEPOS::Deploy::Offline:   return m_tdata.offline;
    }
    return EVEPOS::Access::ConfigRole;
}

StructureSE* TowerSE::GetBusyStructure()
{
    if (IsStateChangePending())
        return this;
    for (auto cur : m_structs)
        if (cur.second->IsStateChangePending())
            return cur.second;
    return nullptr;
}

void TowerSE::CheckNotBusy(StructureSE* pRequester)
{
    StructureSE* pBusy(GetBusyStructure());
    if ((pBusy == nullptr) or (pBusy == pRequester))
        return;
    throw UserError("CantTowerIsAlreadyActive")
            .AddFormatValue("tower", new PyInt(GetTypeID()))
            .AddFormatValue("structure", new PyInt(pBusy->GetTypeID()))
            .AddFormatValue("action", new PyString(pBusy->GetPendingActionName()));
}

StructureSE* TowerSE::GetAnchoredModule()
{
    for (auto cur : m_structs)
        if ((cur.second->GetState() > EVEPOS::StructureState::Unanchored) or cur.second->IsStateChangePending())
            return cur.second;
    return nullptr;
}

void TowerSE::RemoveModule(StructureSE* pSE)
{
    m_structs.erase(pSE->GetID());
    // links to or from a structure that is gone would make every later LinkResourceForTower fail
    std::vector<EVEPOS::ProcessLink> kept;
    for (auto cur : m_links)
        if ((cur.sourceID != pSE->GetID()) and (cur.destID != pSE->GetID()))
            kept.push_back(cur);
    if (kept.size() != m_links.size()) {
        m_links = kept;
        PosMgrDB::ReplaceProcessLinks(m_data.itemID, m_links);
    }
}

void TowerSE::OrphanModules()
{
    // copy:  SetControlTower() takes the module out of m_structs
    std::map<uint32, StructureSE*> modules(m_structs);
    for (auto cur : modules) {
        cur.second->ForceOffline();
        cur.second->SetControlTower(nullptr);
        cur.second->SaveData();
        cur.second->SendSlimUpdate();
    }
}

void TowerSE::GetFuelRequirements(uint16 towerTypeID, uint32 systemID, uint32 ownerAllianceID,
                                  std::map<uint16, uint32>& online, std::map<uint16, uint32>& strontium, bool applyFuelRate/*true*/)
{
    SystemData sData = SystemData();
    sDataMgr.GetSystemData(systemID, sData);
    int8 whClass(sDataMgr.GetWHSystemClass(systemID));
    // the client's consumefactor:  sovereignty held by the owner's alliance saves a quarter
    float factor(applyFuelRate ? sConfig.pos.FuelRate : 1.0f);
    if (IsAlliance(ownerAllianceID) and (svDataMgr.GetSystemAllianceID(systemID) == ownerAllianceID))
        factor *= 0.75f;

    std::vector<EVEPOS::TowerResource> rows;
    sDataMgr.GetTowerResources(towerTypeID, rows);
    for (auto cur : rows) {
        // power and cpu rows (heavy water, liquid ozone) are gone since Crucible 1.1's fuel blocks
        if ((cur.purpose != EVEPOS::Purpose::Online) and (cur.purpose != EVEPOS::Purpose::Reinforce))
            continue;
        if ((cur.factionID != 0) and (cur.factionID != sData.factionID))
            continue;
        if ((cur.minSecurity >= 0) and (cur.minSecurity > sData.securityRating))
            continue;
        if ((cur.wormholeClassID >= 0) and (cur.wormholeClassID != whClass))
            continue;
        // starbase charters (the faction rows)
        if ((cur.factionID != 0) and !sConfig.pos.RequireCharters)
            continue;
        uint32 qty((uint32)std::ceil(cur.quantity * factor));
        if (qty == 0)
            continue;
        if (cur.purpose == EVEPOS::Purpose::Online) {
            online[cur.typeID] += qty;
        } else {
            strontium[cur.typeID] += qty;
        }
    }
}

void TowerSE::GetCharterTypes(uint16 towerTypeID, uint32 systemID, std::vector<uint16>& into)
{
    if (!sConfig.pos.RequireCharters)
        return;
    SystemData sData = SystemData();
    sDataMgr.GetSystemData(systemID, sData);
    std::vector<EVEPOS::TowerResource> rows;
    sDataMgr.GetTowerResources(towerTypeID, rows);
    for (auto cur : rows) {
        if ((cur.purpose != EVEPOS::Purpose::Online) or (cur.factionID == 0) or (cur.factionID != sData.factionID))
            continue;
        if ((cur.minSecurity >= 0) and (cur.minSecurity > sData.securityRating))
            continue;
        into.push_back(cur.typeID);
    }
}

uint32 TowerSE::CountResource(uint16 typeID, EVEItemFlags flag)
{
    std::vector<InventoryItemRef> items;
    m_self->GetMyInventory()->GetItemsByFlag(flag, items);
    uint32 qty(0);
    for (auto cur : items)
        if ((cur->typeID() == typeID) and !cur->isSingleton())
            qty += cur->quantity();
    return qty;
}

void TowerSE::CheckOnlineFuel()
{
    std::map<uint16, uint32> online, strontium;
    GetFuelRequirements(GetTypeID(), m_system->GetID(), (IsAlliance(m_allyID) ? m_allyID : 0), online, strontium);
    std::string missing;
    for (auto cur : online) {
        if (CountResource(cur.first, flagNone) >= cur.second)
            continue;
        if (!missing.empty())
            missing += ", ";
        const ItemType* pType(sItemFactory.GetType(cur.first));
        missing += (pType == nullptr ? std::to_string(cur.first) : pType->name());
    }
    if (!missing.empty())
        throw UserError("CantOnlineTowerLacksResources")
                .AddFormatValue("tower", new PyInt(GetTypeID()))
                .AddFormatValue("resource", new PyString(missing));
}

bool TowerSE::IsHostile(SystemEntity* pTarget)
{
    if ((pTarget == nullptr) or pTarget->IsDead())
        return false;
    const int64 aggressionWindow(EvE::Time::Minute * 15);

    // npcs:  only those that attacked us
    if (pTarget->IsNPCSE())
        return IsRecentAggressor(pTarget->GetID(), aggressionWindow);

    // whose is it
    uint32 charID(0), corpID(pTarget->GetCorporationID());
    int32 allyID(pTarget->GetAllianceID());
    Client* pClient(nullptr);
    if (pTarget->IsShipSE()) {
        if (pTarget->HasPilot()) {
            pClient = pTarget->GetPilot();
            charID = pClient->GetCharacterID();
            corpID = pClient->GetCorporationID();
            allyID = pClient->GetAllianceID();
        } else {
            // a ship left in space by a pilot who logged off
            charID = pTarget->GetSelf()->ownerID();
        }
    } else if (pTarget->IsDroneSE()) {
        charID = pTarget->GetSelf()->ownerID();
        pClient = sEntityList.FindClientByCharID(charID);
    } else {
        return false;
    }
    if (!IsCharacterID(charID))
        return false;
    if ((pClient != nullptr) and pClient->IsInvul())
        return false;

    // 1. never our own
    if (corpID == m_corpID)
        return false;
    if (IsAlliance(m_allyID) and (allyID == m_allyID))
        return false;
    // 2. flagged to the corp, or shot at its structures
    if (sCrimeWatch.IsFlaggedTo(charID, m_corpID) or IsRecentAggressor(charID, aggressionWindow))
        return true;
    // 3. at war with the owner
    if (m_tdata.corpWar and sConfig.war.Enabled and sWarMgr.IsHostile(sWarMgr.WarOwnerOf(m_corpID), sWarMgr.WarOwnerOf(corpID)))
        return true;
    // 4. criminals
    if (m_tdata.statusDrop and sCrimeWatch.IsCriminal(charID))
        return true;
    // 5. standings and security status, below high security space only
    if ((CrimeWatch::SecurityClass(m_system->GetSystemSecurityRating()) != Crime::SecClass::High) and (pClient != nullptr))
        return ConsidersHostile(pClient);
    return false;
}

void TowerSE::Delete()
{
    // same clean up as Killed() (which ends up here too), so the modules don't keep a pointer to a tower that is gone
    if (m_hasShield) {
        m_pShieldSE->Delete();
        SafeDelete(m_pShieldSE);
        m_hasShield = false;
    }
    if ((m_moonSE != nullptr) and (m_moonSE->GetMyTower() == this))
        m_moonSE->SetTower(nullptr);
    if ((m_bubble != nullptr) and (m_bubble->GetTowerSE() == this))
        m_bubble->SetTowerSE(nullptr);
    OrphanModules();
    StructureSE::Delete();
}

void TowerSE::Scoop() {
    // structures launched near it (all unanchored by now) belong to no tower; the bubble forgets it
    OrphanModules();
    if ((m_bubble != nullptr) and (m_bubble->GetTowerSE() == this))
        m_bubble->SetTowerSE(nullptr);
    StructureSE::Scoop();
    if ((m_moonSE != nullptr) and (m_moonSE->GetMyTower() == this))
        m_moonSE->SetTower(nullptr);
    m_tdata = EVEPOS::TowerData();
    m_self->ChangeSingleton(false);
    m_self->SaveItem();
}

void TowerSE::Process()
{
    /* called by EntityList::Process on every loop */

    // after a (re)load, once every structure of the system is initialized:  the hours missed while unloaded
    if (m_catchUp) {
        m_catchUp = false;
        if (IsPowered())
            CatchUp();
    }

    // reinforcement ends on time (the hourly cycle also checks, for missed hours)
    if (IsReinforced() and (GetFileTimeNow() >= m_tdata.reinforceUntil))
        ExitReinforced();
    // repaired back to 50% shield:  it can be reinforced again
    if (!m_tdata.reinforceArmed and !IsReinforced() and (GetShieldFraction() >= EVEPOS::ShieldFluctuationLevel)) {
        m_tdata.reinforceArmed = true;
        m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
    }

    // starbase charter checks for empire space

    // tower-specific tests here

    /*  Enable base call to Process Anchoring, Targeting and Movement  */
    StructureSE::Process();
}

/*
 * 473     prototypingBonus    250000  NULL
 * 556     anchoringDelay  1800000     NULL
 * 650     maxStructureDistance    50000   NULL
 * 676     unanchoringDelay    NULL    3600000
 * 677     onliningDelay   1800000     NULL
 * 680     shieldRadius    30000   NULL
 * 711     moonAnchorDistance  100000  NULL
 * 1214    posStructureControlDistanceMax  NULL    15000
 */

void TowerSE::SetOnline()
{
    // onlining is done:  the first hour of fuel is burned now (the bay may have been emptied while onlining)
    if (!ConsumeFuelCycle()) {
        _log(POS__MESSAGE, "TowerSE::SetOnline() - %s(%u) has no fuel left to come online.", GetName(), m_data.itemID);
        m_data.state = EVEPOS::StructureState::Online;     // so ForceOffline() takes it down
        GoOffline(true);
        return;
    }

    // online state, flag, hourly timer (posControlTowerPeriod), effect
    StructureSE::SetOnline();
    m_data.timestamp = GetFileTimeNow();
    m_harmonic = EVEPOS::Harmonic::Online;
    m_tdata.harmonic = m_harmonic;
    m_tdata.lastCycle = m_data.timestamp;

    if (!m_tdata.password.empty())
        CreateForceField(true);

    UpdateFuelCalendar();
    m_db.UpdateBaseData(m_data);
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
}

void TowerSE::SetOffline()
{
    RemoveForceField();
    m_harmonic = EVEPOS::Harmonic::Offline;

    // an offline tower powers nothing
    std::map<uint32, StructureSE*> modules(m_structs);
    for (auto cur : modules)
        cur.second->ForceOffline();

    StructureSE::SetOffline();
    // no fuel expiry while offline
    UpdateFuelCalendar();
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
}


void TowerSE::Online()
{
    // an hour of tower life (posControlTowerPeriod)
    ProcessCycle(GetFileTimeNow());

    // next hour, unless the tower went offline
    if (m_procState == EVEPOS::ProcState::Online)
        StructureSE::Online();
}

void TowerSE::ProcessCycle(int64 cycleTime)
{
    // reinforcement ended during this hour (missed hours)
    if (IsReinforced() and (cycleTime >= m_tdata.reinforceUntil))
        ExitReinforced();
    if (!IsPowered())
        return;

    // fuel blocks (and charters):  all of them or the tower goes offline.  a reinforced tower holds out until the end
    if (!ConsumeFuelCycle()) {
        if (IsReinforced()) {
            m_fuelOut = true;
        } else {
            GoOffline(true);
            return;
        }
    }

    // moon mining and reactions run while the tower is online (not reinforced)
    if (IsOnline())
        RunMoonProcesses();

    m_tdata.lastCycle = cycleTime;
    // replayed hours save once at the end
    if (m_replaying)
        return;
    CheckLowFuel();
    UpdateFuelCalendar();
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
}

void TowerSE::CatchUp()
{
    // the hours missed while the tower's system was not loaded (downtime), up to pos.MaxCatchUpCycles
    int64 now(GetFileTimeNow());
    int64 period(m_self->GetAttribute(AttrPosControlTowerPeriod).get_int() * (EvE::Time::Second / 1000));
    if (period < EvE::Time::Minute)
        period = EvE::Time::Hour;

    if (m_tdata.lastCycle < 1) {
        // online before cycles were kept:  start counting now
        m_tdata.lastCycle = now;
        m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
    }

    int64 missed((now - m_tdata.lastCycle) / period);
    if (missed > sConfig.pos.MaxCatchUpCycles) {
        _log(POS__MESSAGE, "TowerSE::CatchUp() - %s(%u) missed %lli cycles, replaying the last %u.", GetName(), m_data.itemID, missed, sConfig.pos.MaxCatchUpCycles);
        m_tdata.lastCycle += (missed - sConfig.pos.MaxCatchUpCycles) * period;
        missed = sConfig.pos.MaxCatchUpCycles;
    }
    int64 replayed(0);
    m_replaying = true;
    while ((replayed < missed) and IsPowered()) {
        ProcessCycle(m_tdata.lastCycle + period);
        ++replayed;
    }
    m_replaying = false;
    if (IsPowered())
        CheckLowFuel();
    UpdateFuelCalendar();
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
    if (replayed > 0)
        _log(POS__MESSAGE, "TowerSE::CatchUp() - %s(%u) replayed %lli hourly cycles.", GetName(), m_data.itemID, replayed);

    // the rest of the current hour
    if (IsPowered()) {
        int64 left((m_tdata.lastCycle + period - now) / (EvE::Time::Second / 1000));
        m_procState = EVEPOS::ProcState::Online;
        SetTimer(left > 0 ? (uint32)left : 1);
    }
}

bool TowerSE::ConsumeFuelCycle()
{
    std::map<uint16, uint32> online, strontium;
    GetFuelRequirements(GetTypeID(), m_system->GetID(), (IsAlliance(m_allyID) ? m_allyID : 0), online, strontium);
    for (auto cur : online)
        if (CountResource(cur.first, flagNone) < cur.second)
            return false;
    for (auto cur : online)
        TakeResource(cur.first, flagNone, cur.second);
    return true;
}

void TowerSE::TakeResource(uint16 typeID, EVEItemFlags flag, uint32 qty)
{
    std::vector<InventoryItemRef> items;
    m_self->GetMyInventory()->GetItemsByFlag(flag, items);
    for (auto cur : items) {
        if (qty == 0)
            break;
        if ((cur->typeID() != typeID) or cur->isSingleton())
            continue;
        if (cur->quantity() > qty) {
            cur->AlterQuantity(-((int32)qty), true);
            qty = 0;
        } else {
            qty -= cur->quantity();
            // removes the stack
            cur->SetQuantity(0, true, true);
        }
    }
    if (qty > 0)
        _log(POS__WARNING, "TowerSE::TakeResource() - %s(%u) was %u units of %u short", GetName(), m_data.itemID, qty, typeID);
}

uint32 TowerSE::FuelHoursLeft()
{
    // full hours the fuel bay holds after the current one (the client's 'time left' is the same floor)
    std::map<uint16, uint32> online, strontium;
    GetFuelRequirements(GetTypeID(), m_system->GetID(), (IsAlliance(m_allyID) ? m_allyID : 0), online, strontium);
    uint32 hours(UINT32_MAX);
    for (auto cur : online) {
        uint32 have(CountResource(cur.first, flagNone) / cur.second);
        if (have < hours)
            hours = have;
    }
    return hours;
}

void TowerSE::GoOffline(bool outOfFuel)
{
    // the tower and everything it powers go offline at once:  force field down, harmonic offline
    ForceOffline();
    m_tdata.lastCycle = 0;
    UpdateFuelCalendar();
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
    if (outOfFuel) {
        _log(POS__MESSAGE, "TowerSE::GoOffline() - %s(%u) in %s ran out of fuel.", GetName(), m_data.itemID, m_system->GetName());
        CheckLowFuel(true);
    }
}

void TowerSE::UpdateFuelCalendar()
{
    // a corp calendar event at the fuel expiry, moved when the expiry moves by more than an hour
    uint32 hours(0);
    if (m_tdata.showInCalendar and IsPowered())
        hours = FuelHoursLeft();
    if ((hours == 0) or (hours == UINT32_MAX)) {
        if (m_tdata.fuelEventID != 0) {
            CalendarDB::DeleteEvent(m_tdata.fuelEventID);
            m_tdata.fuelEventID = 0;
            m_tdata.fuelExpiry = 0;
            m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
        }
        return;
    }

    // this hour is paid for, then 'hours' more
    int64 base(m_tdata.lastCycle > 0 ? m_tdata.lastCycle : GetFileTimeNow());
    int64 expiry(base + (hours + 1) * EvE::Time::Hour);
    if ((m_tdata.fuelEventID != 0) and (std::llabs(expiry - m_tdata.fuelExpiry) <= EvE::Time::Hour))
        return;

    if (m_tdata.fuelEventID != 0)
        CalendarDB::DeleteEvent(m_tdata.fuelEventID);

    std::string title("Expiration of Tower fuel");
    std::string description("The Control Tower ");
    description += m_self->itemName();
    description += " at ";
    description += (m_moonSE == nullptr ? m_system->GetName() : m_moonSE->GetName());
    description += " will exhaust its current fuel supply at this time.";
    m_tdata.fuelEventID = CalendarDB::SaveSystemEvent(m_corpID, m_corpID, expiry, Calendar::AutoEvent::PosFuel, title, description, true);
    m_tdata.fuelExpiry = expiry;
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
}

void TowerSE::OnStructureDamaged(StructureSE* pVictim, SystemEntity* pAttacker)
{
    // who did it.  pilots (and their drones) by character, npcs by item
    if (pAttacker != nullptr) {
        uint32 id(pAttacker->GetID());
        if (pAttacker->HasPilot()) {
            id = pAttacker->GetPilot()->GetCharacterID();
        } else if (pAttacker->IsDroneSE() and IsCharacterID(pAttacker->GetSelf()->ownerID())) {
            id = pAttacker->GetSelf()->ownerID();
        }
        m_aggressors[id] = GetFileTimeNow();
    }

    NotifyAttack(pVictim, pAttacker);

    if (pVictim == this)
        CheckReinforce();
}

void TowerSE::NotifyAttack(StructureSE* pVictim, SystemEntity* pAttacker, bool final/*false*/)
{
    // at most once an hour per tower (the last word when it dies)
    int64 now(GetFileTimeNow());
    if (!final and ((now - m_lastAttackNotify) < EvE::Time::Hour))
        return;
    m_lastAttackNotify = now;

    // pilots (and their drones) by character, anything else by its owner
    uint32 aggressorID(0), aggressorCorpID(0), aggressorAllyID(0);
    if (pAttacker != nullptr) {
        if (pAttacker->HasPilot()) {
            Client* pPilot(pAttacker->GetPilot());
            aggressorID = pPilot->GetCharacterID();
            aggressorCorpID = pPilot->GetCorporationID();
            aggressorAllyID = pPilot->GetAllianceID();
        } else if (pAttacker->IsDroneSE() and IsCharacterID(pAttacker->GetSelf()->ownerID())) {
            aggressorID = pAttacker->GetSelf()->ownerID();
            aggressorCorpID = pAttacker->GetCorporationID();
            aggressorAllyID = pAttacker->GetAllianceID();
        } else {
            aggressorID = pAttacker->GetOwnerID();
            aggressorCorpID = pAttacker->GetCorporationID();
            aggressorAllyID = pAttacker->GetAllianceID();
        }
    }

    // what is left of the attacked structure (fractions)
    float shield(0.0f), armor(0.0f), hull(0.0f);
    if (!final and (pVictim != nullptr)) {
        InventoryItemRef vRef(pVictim->GetSelf());
        shield = pVictim->GetShieldFraction();
        float armorHP(vRef->GetAttribute(AttrArmorHP).get_float()), hp(vRef->GetAttribute(AttrHP).get_float());
        armor = (armorHP > 0.0f ? 1.0f - vRef->GetAttribute(AttrArmorDamage).get_float() / armorHP : 0.0f);
        hull = (hp > 0.0f ? 1.0f - vRef->GetAttribute(AttrDamage).get_float() / hp : 0.0f);
    }

    PyDict* data = new PyDict();
        data->SetItemString("aggressorID",          (aggressorID == 0 ? PyStatic.NewNone() : new PyInt(aggressorID)));
        data->SetItemString("aggressorCorpID",      (aggressorCorpID == 0 ? PyStatic.NewNone() : new PyInt(aggressorCorpID)));
        data->SetItemString("aggressorAllianceID",  (IsAlliance(aggressorAllyID) ? new PyInt(aggressorAllyID) : PyStatic.NewNone()));
        data->SetItemString("moonID",               (m_moonSE == nullptr ? PyStatic.NewNone() : new PyInt(m_moonSE->GetID())));
        data->SetItemString("solarSystemID",        new PyInt(m_system->GetID()));
        data->SetItemString("typeID",               new PyInt(pVictim == nullptr ? GetTypeID() : pVictim->GetTypeID()));
        data->SetItemString("shieldValue",          new PyFloat(shield));
        data->SetItemString("armorValue",           new PyFloat(armor));
        data->SetItemString("hullValue",            new PyFloat(hull));
    NotificationMgr::SendToCorp(m_corpID, Corp::Role::StarbaseConfig | Corp::Role::InfrastructureTacticalOfficer,
                                Notify::Types::TowerAlert, m_corpID, data);
}

void TowerSE::CheckLowFuel(bool outOfFuel/*false*/)
{
    if (!m_tdata.sendFuelNotifications)
        return;
    int64 now(GetFileTimeNow());
    if (!outOfFuel) {
        uint32 hours(FuelHoursLeft());
        if (hours >= EVEPOS::LowFuelHours)
            return;
        if ((now - m_tdata.lowFuelNotified) < EvE::Time::Day)
            return;
    }

    // what the bay holds of each fuel the tower burns
    std::map<uint16, uint32> online, strontium;
    GetFuelRequirements(GetTypeID(), m_system->GetID(), (IsAlliance(m_allyID) ? m_allyID : 0), online, strontium);
    PyList* wants = new PyList();
    for (auto cur : online) {
        PyDict* want = new PyDict();
            want->SetItemString("typeID",   new PyInt(cur.first));
            want->SetItemString("quantity", new PyInt(CountResource(cur.first, flagNone)));
        wants->AddItem(want);
    }

    PyDict* data = new PyDict();
        data->SetItemString("corpID",           new PyInt(m_corpID));
        data->SetItemString("allianceID",       (IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone()));
        data->SetItemString("solarSystemID",    new PyInt(m_system->GetID()));
        data->SetItemString("moonID",           (m_moonSE == nullptr ? PyStatic.NewNone() : new PyInt(m_moonSE->GetID())));
        data->SetItemString("typeID",           new PyInt(GetTypeID()));
        data->SetItemString("wants",            wants);
    NotificationMgr::SendToCorp(m_corpID, Corp::Role::StarbaseConfig | Corp::Role::StarbaseCaretaker,
                                Notify::Types::TowerResourceAlert, m_corpID, data);

    m_tdata.lowFuelNotified = now;
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
}

void TowerSE::OnAnchored()
{
    // the alliance holding sovereignty here hears about other people's towers
    uint32 sovAllianceID(svDataMgr.GetSystemAllianceID(m_system->GetID()));
    if (!IsAlliance(sovAllianceID) or (IsAlliance(m_allyID) and ((uint32)m_allyID == sovAllianceID)))
        return;

    // the other towers anchored in the system, by corp
    std::map<uint32, PyList*> towersByCorp;
    std::map<uint32, int32> allianceByCorp;
    for (auto cur : m_system->GetEntities()) {
        if ((cur.second == this) or !cur.second->IsTowerSE())
            continue;
        TowerSE* pTower(cur.second->GetTowerSE());
        if (!pTower->IsAnchored())
            continue;
        PyList*& list(towersByCorp[pTower->GetCorporationID()]);
        if (list == nullptr)
            list = new PyList();
        allianceByCorp[pTower->GetCorporationID()] = pTower->GetAllianceID();
        PyDict* tower = new PyDict();
            tower->SetItemString("moonID",  (pTower->GetMoonSE() == nullptr ? PyStatic.NewNone() : new PyInt(pTower->GetMoonSE()->GetID())));
            tower->SetItemString("typeID",  new PyInt(pTower->GetTypeID()));
        list->AddItem(tower);
    }
    PyRep* corpsPresent(nullptr);
    if (towersByCorp.empty()) {
        corpsPresent = PyStatic.NewNone();
    } else {
        PyList* corps = new PyList();
        for (auto cur : towersByCorp) {
            PyDict* corp = new PyDict();
                corp->SetItemString("corpID",       new PyInt(cur.first));
                corp->SetItemString("allianceID",   (IsAlliance(allianceByCorp[cur.first]) ? new PyInt(allianceByCorp[cur.first]) : PyStatic.NewNone()));
                corp->SetItemString("towers",       cur.second);
            corps->AddItem(corp);
        }
        corpsPresent = corps;
    }

    PyDict* data = new PyDict();
        data->SetItemString("corpID",           new PyInt(m_corpID));
        data->SetItemString("allianceID",       (IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone()));
        data->SetItemString("solarSystemID",    new PyInt(m_system->GetID()));
        data->SetItemString("moonID",           (m_moonSE == nullptr ? PyStatic.NewNone() : new PyInt(m_moonSE->GetID())));
        data->SetItemString("typeID",           new PyInt(GetTypeID()));
        data->SetItemString("corpsPresent",     corpsPresent);
    NotificationMgr::SendToAlliance(sovAllianceID, 0, Notify::Types::AllAnchoring, m_corpID, data);
}

bool TowerSE::IsRecentAggressor(uint32 id, int64 window)
{
    std::map<uint32, int64>::iterator itr(m_aggressors.find(id));
    if (itr == m_aggressors.end())
        return false;
    if ((GetFileTimeNow() - itr->second) > window) {
        m_aggressors.erase(itr);
        return false;
    }
    return true;
}

void TowerSE::CheckReinforce()
{
    if (IsReinforced() or !m_tdata.reinforceArmed or !IsOnline())
        return;
    if (GetShieldFraction() >= EVEPOS::ReinforceShieldLevel)
        return;
    EnterReinforced();
}

void TowerSE::EnterReinforced()
{
    // whole hours of strontium in the strontium bay at the tower's hourly rate (pos.FuelRate only scales what is burned)
    std::map<uint16, uint32> online, strontium;
    float rate(sConfig.pos.FuelRate);
    GetFuelRequirements(GetTypeID(), m_system->GetID(), (IsAlliance(m_allyID) ? m_allyID : 0), online, strontium, false);

    uint32 hours(strontium.empty() ? 0 : UINT32_MAX);
    for (auto cur : strontium) {
        uint32 have(CountResource(cur.first, flagSecondaryStorage) / cur.second);
        if (have < hours)
            hours = have;
    }

    // no strontium, no reinforcement:  the tower stays vulnerable (re-armed at 50% shield)
    m_tdata.reinforceArmed = false;
    if (hours == 0) {
        _log(POS__MESSAGE, "TowerSE::EnterReinforced() - %s(%u) has no strontium and stays vulnerable.", GetName(), m_data.itemID);
        m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
        return;
    }

    for (auto cur : strontium)
        TakeResource(cur.first, flagSecondaryStorage, (uint32)std::ceil(hours * cur.second * rate));

    int64 now(GetFileTimeNow());
    m_tdata.reinforceUntil = now + hours * EvE::Time::Hour;
    m_data.state = EVEPOS::StructureState::Reinforced;
    // the client counts down to posTimestamp
    m_data.timestamp = m_tdata.reinforceUntil;
    m_delayTime = hours * 3600000;
    SendSlimUpdate();
    m_db.UpdateBaseData(m_data);
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
    _log(POS__MESSAGE, "TowerSE::EnterReinforced() - %s(%u) in %s is reinforced for %u hours.", GetName(), m_data.itemID, m_system->GetName(), hours);
}

void TowerSE::ExitReinforced()
{
    if (!IsReinforced())
        return;
    // back online as it is (shield unchanged, not armed until repaired to 50%)
    m_data.state = EVEPOS::StructureState::Online;
    m_data.timestamp = GetFileTimeNow();
    m_delayTime = 0;
    m_tdata.reinforceUntil = 0;
    SendSlimUpdate();
    m_db.UpdateBaseData(m_data);
    m_db.UpdateTowerRuntime(m_data.itemID, m_tdata);
    _log(POS__MESSAGE, "TowerSE::ExitReinforced() - %s(%u) left reinforced mode.", GetName(), m_data.itemID);

    // the fuel ran out while it was reinforced
    if (m_fuelOut) {
        m_fuelOut = false;
        GoOffline(true);
    }
}

void TowerSE::RecalcLoad()
{
    // power and cpu of the structures that are online or onlining
    m_pgUsed = 0.0f;
    m_cpuUsed = 0.0f;
    for (auto cur : m_structs) {
        if (!cur.second->IsPowered() and (cur.second->GetState() != EVEPOS::StructureState::Onlining))
            continue;
        m_pgUsed += cur.second->GetSelf()->GetAttribute(AttrPower).get_float();
        m_cpuUsed += cur.second->GetSelf()->GetAttribute(AttrCpu).get_float();
    }
}

bool TowerSE::HasPG(float amount)
{
    RecalcLoad();
    return ((m_pgUsed + amount) <= (m_self->GetAttribute(AttrPowerOutput).get_float() + 0.001f));
}

bool TowerSE::HasCPU(float amount)
{
    RecalcLoad();
    return ((m_cpuUsed + amount) <= (m_self->GetAttribute(AttrCpuOutput).get_float() + 0.001f));
}

void TowerSE::ValidateAddItem(Client* pClient, InventoryItemRef iRef, EVEItemFlags toFlag)
{
    StructureSE::ValidateAddItem(pClient, iRef, toFlag);
    if (pClient == nullptr)
        return;
    // fuel technicians and starbase managers fill the bays
    if (!HasRole(pClient, Corp::Role::StarbaseCaretaker | Corp::Role::StarbaseConfig)) {
        if ((uint32)pClient->GetCorporationID() != m_corpID)
            throw UserError("CantConfigureNotInRightCorp");
        throw UserError("CantConfigureCorpStarbaseOrCaretakerRoleLacking");
    }
    if (IsReinforced() or (IsPowered() and (GetShieldFraction() < EVEPOS::ShieldFluctuationLevel)))
        throw UserError("CannotPlaceInLowShieldTower")
                .AddFormatValue("item", new PyInt(iRef->typeID()))
                .AddFormatValue("type", new PyInt(GetTypeID()))
                .AddFormatValue("level", new PyInt((int32)(EVEPOS::ShieldFluctuationLevel * 100)));

    // the fuel bay takes this tower's fuel (purpose 1), the strontium bay its reinforcement fuel (purpose 4)
    uint8 purpose(0);
    if (toFlag == flagNone) {
        purpose = EVEPOS::Purpose::Online;
    } else if (toFlag == flagSecondaryStorage) {
        purpose = EVEPOS::Purpose::Reinforce;
    } else {
        throw CustomError ("Control towers only have a fuel bay and a strontium bay.");
    }
    std::vector<EVEPOS::TowerResource> rows;
    sDataMgr.GetTowerResources(GetTypeID(), rows);
    for (auto cur : rows)
        if ((cur.purpose == purpose) and (cur.typeID == iRef->typeID()))
            return;
    if (purpose == EVEPOS::Purpose::Online)
        throw UserError("CannotPlaceTypeInFuelBay").AddTypeName("type", iRef->typeID());
    throw UserError("CannotPlaceTypeInStructure")
            .AddFormatValue("container", new PyInt(GetTypeID()))
            .AddFormatValue("type", new PyInt(iRef->typeID()));
}

void TowerSE::ValidateRemoveItem(Client* pClient, InventoryItemRef iRef)
{
    StructureSE::ValidateRemoveItem(pClient, iRef);
    if (pClient == nullptr)
        return;
    if (!HasRole(pClient, Corp::Role::StarbaseCaretaker | Corp::Role::StarbaseConfig)) {
        if ((uint32)pClient->GetCorporationID() != m_corpID)
            throw UserError("CantConfigureNotInRightCorp");
        throw UserError("CantConfigureCorpStarbaseOrCaretakerRoleLacking");
    }
    if (IsReinforced() or (IsPowered() and (GetShieldFraction() < EVEPOS::ShieldFluctuationLevel)))
        throw UserError("CannotRemoveFromLowShieldTower")
                .AddFormatValue("item", new PyInt(iRef->typeID()))
                .AddFormatValue("type", new PyInt(GetTypeID()))
                .AddFormatValue("level", new PyInt((int32)(EVEPOS::ShieldFluctuationLevel * 100)));
}

void TowerSE::Operating()
{
    // structure operating
    // take resources or whatever needs to be done

    //1031    controlTowerSize    3

    // if fuel has run out, start reinforced mode.

    // reset timers
    StructureSE::Operating();
}

void TowerSE::ReinforceTower()
{
    //  see how many stront is in tower and set timer accordingly
    // Strontium Clathrates   s:100  m:200  l:400
}

void TowerSE::Reinforced()
{
    // dunno what to do here yet.
}

void TowerSE::UpdatePassword()
{
    // the field's harmonic is the password's.  no password, no field (the client asks for one when there is no field)
    m_tdata.harmonic = (m_tdata.password.empty() ? EVEPOS::Harmonic::Offline : EVEPOS::HarmonicFromPassword(m_tdata.password));
    if (IsPowered()) {
        if (m_tdata.password.empty()) {
            RemoveForceField();
        } else if (!m_hasShield) {
            CreateForceField(true);
        } else {
            UpdateForceField();
        }
    }

    m_db.UpdateHarmonicAndPassword(m_data.itemID, m_tdata);
}

void TowerSE::UpdateAccess()
{
    m_db.UpdateAccess(m_data.itemID, m_tdata);
    UpdateForceField();
}

bool TowerSE::InForceField(const GPoint& pos)
{
    if (m_pShieldSE == nullptr)
        return false;
    return (pos.distance(GetPosition()) < GetForceFieldRadius());
}

double TowerSE::GetForceFieldRadius()
{
    return m_self->GetAttribute(AttrShieldRadius).get_double();
}

void TowerSE::UpdateForceField()
{
    if ((m_pShieldSE == nullptr) or (m_pShieldSE->GetFieldSE() == nullptr))
        return;
    FieldSE* pField(m_pShieldSE->GetFieldSE());
    pField->SetHarmonic(EVEPOS::HarmonicFromPassword(m_tdata.password));
    pField->SetAccess(m_tdata.allowCorp, m_tdata.allowAlliance);

    // the client's SetBallHarmonic(ballID, harmonic, corpID, allianceID, field)
    std::vector<PyTuple*> updates;
    SetBallHarmonic sbh;
        sbh.itemID = pField->GetID();
        sbh.harmonic = pField->GetHarmonic();
        sbh.corpID = pField->GetCorpAccessID();
        sbh.allianceID = pField->GetAllianceAccessID();
        sbh.field = 1;
    updates.push_back(sbh.Encode());
    //  'massive' enables client-side bounce
    SetBallMassive sbm;
        sbm.entityID = pField->GetID();
        sbm.is_massive = 1;
    updates.push_back(sbm.Encode());
    if (m_destiny != nullptr)
        m_destiny->SendDestinyUpdate(updates); //consumed
}

void TowerSE::RemoveForceField()
{
    if (!m_hasShield)
        return;
    m_pShieldSE->Delete();
    SafeDelete(m_pShieldSE);
    m_hasShield = false;
    // forceFieldID
    SendSlimUpdate();
}

void TowerSE::SetDeployFlags(int8 anchor/*0*/, int8 unanchor/*0*/, int8 online/*0*/, int8 offline/*0*/)
{
    m_tdata.anchor = anchor;
    m_tdata.unanchor = unanchor;
    m_tdata.online = online;
    m_tdata.offline = offline;

    m_db.UpdateDeployFlags(m_data.itemID, m_tdata);
}

PyRep* TowerSE::GetDeployFlags()
{
    PyList* header = new PyList(4);
        header->SetItemString(0, "anchor");
        header->SetItemString(1, "unanchor");
        header->SetItemString(2, "online");
        header->SetItemString(3, "offline");
    PyList* line = new PyList(4);           // these are structure permissions for this tower
        line->SetItem(0, new PyInt(m_tdata.anchor));
        line->SetItem(1, new PyInt(m_tdata.unanchor));
        line->SetItem(2, new PyInt(m_tdata.online));
        line->SetItem(3, new PyInt(m_tdata.offline));

    PyDict* dict = new PyDict();
    dict->SetItemString("header", header);
    dict->SetItemString("line", line);

    return new PyObject("util.Row", dict);
}

void TowerSE::SetUseFlags(uint32 itemID, int8 view, int8 take, int8 use/*0*/)
{
    std::map<uint32, StructureSE*>::iterator itr = m_structs.find(itemID);
    if (itr != m_structs.end()) {
        itr->second->SetUsageFlags(view, take, use);
        itr->second->UpdateUsageFlags();
    }
}

PyRep* TowerSE::GetUsageFlagList()
{
    /*
                [PyObject Name: eve.common.script.sys.rowset.Rowset]
                    [PyDict 3 kvp]
                        Key:[PyString "header"]
                        ==Value:[PyList 4 items]
                                    [PyString "structureID"]
                                    [PyString "viewput"]
                                    [PyString "viewputtake"]
                                    [PyString "use"]
                        Key:[PyString "RowClass"]
                        ==Value:[PyToken carbon.common.script.sys.row.Row]
                        Key:[PyString "lines"]
                        ==Value:[PyList 1 items]
                                    [PyList 4 items]
                                        [PyIntegerVar 1010759458081]
                                        [PyInt 3]
                                        [PyInt 0]
                                        [PyInt 0]
                                        */

    PyList* header = new PyList(4);
        header->SetItemString(0, "structureID");
        header->SetItemString(1, "viewput");
        header->SetItemString(2, "viewputtake");
        header->SetItemString(3, "use");
    PyList* lines = new PyList();
    for (auto cur : m_structs) {
        PyList* line = new PyList(4);
            line->SetItem(0, new PyInt(cur.first));
            line->SetItem(1, new PyInt(cur.second->CanView()));
            line->SetItem(2, new PyInt(cur.second->CanTake()));
            line->SetItem(3, new PyInt(cur.second->CanUse()));
        lines->AddItem(line);
    }

    PyDict* dict = new PyDict();
    dict->SetItemString("header", header);
    dict->SetItemString("RowClass", new PyToken("util.Row"));
    dict->SetItemString("lines", lines);

    return new PyObject("util.Rowset", dict);
}

bool TowerSE::IsProcessStructure(StructureSE* pSE)
{
    switch (pSE->GetSelf()->groupID()) {
        case EVEDB::invGroups::Moon_Mining:
        case EVEDB::invGroups::Silo:
        case EVEDB::invGroups::Mobile_Reactor:
            return true;
    }
    return false;
}

uint32 TowerSE::ProcessUnits(uint16 typeID, uint32 qty)
{
    // quantities in moon harvesting and reactions are in batches of the material's moonMiningAmount
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr)
        return qty;
    uint32 amount(pType->GetAttribute(AttrMoonMiningAmount).get_uint32());
    return (qty * (amount > 0 ? amount : 1));
}

float TowerSE::GetSiloCapacity(StructureSE* pSilo)
{
    // the silo's capacity with its tower's silo bonus
    float capacity(pSilo->GetSelf()->type().capacity());
    TowerSE* pTower(pSilo->GetControlTower());
    if (pTower != nullptr) {
        EvilNumber bonus(EvilZero);
        if (pTower->GetSelf()->HasAttribute(AttrControlTowerSiloCapacityBonus, bonus))
            capacity *= (1.0f + bonus.get_float() / 100.0f);
    }
    return capacity;
}

InventoryItemRef TowerSE::GetReaction(StructureSE* pReactor)
{
    // the reaction 'blueprint' sitting in a reactor
    std::vector<InventoryItemRef> items;
    pReactor->GetSelf()->GetMyInventory()->GetItemsByFlag(flagNone, items);
    for (auto cur : items)
        if (cur->categoryID() == EVEDB::invCategories::Reaction)
            return cur;
    return InventoryItemRef(nullptr);
}

uint32 TowerSE::GetSiloUnits(StructureSE* pSilo, uint16 typeID)
{
    std::vector<InventoryItemRef> items;
    pSilo->GetSelf()->GetMyInventory()->GetItemsByFlag(flagNone, items);
    uint32 units(0);
    for (auto cur : items)
        if ((cur->typeID() == typeID) and !cur->isSingleton())
            units += cur->quantity();
    return units;
}

uint32 TowerSE::AddToSilo(StructureSE* pSilo, uint16 typeID, uint32 units)
{
    // as much as fits
    const ItemType* pType(sItemFactory.GetType(typeID));
    if ((pType == nullptr) or (units == 0))
        return 0;
    double volume(pType->volume() > 0 ? pType->volume() : 1);
    double freeVolume(GetSiloCapacity(pSilo) - pSilo->GetSelf()->GetMyInventory()->GetStoredVolume(flagNone));
    uint32 room(freeVolume > 0 ? (uint32)(freeVolume / volume) : 0);
    if (units > room)
        units = room;
    if (units == 0)
        return 0;

    InventoryItemRef stack(pSilo->GetSelf()->GetMyInventory()->GetByTypeFlag(typeID, flagNone));
    if ((stack.get() != nullptr) and !stack->isSingleton()) {
        stack->AlterQuantity(units, true);
    } else {
        ItemData idata(typeID, pSilo->GetSelf()->ownerID(), locTemp, flagNone, units);
        InventoryItemRef iRef(sItemFactory.SpawnItem(idata));
        if (iRef.get() == nullptr)
            return 0;
        iRef->Move(pSilo->GetID(), flagNone, true);
    }
    return units;
}

void TowerSE::TakeFromSilo(StructureSE* pSilo, uint16 typeID, uint32 units)
{
    std::vector<InventoryItemRef> items;
    pSilo->GetSelf()->GetMyInventory()->GetItemsByFlag(flagNone, items);
    for (auto cur : items) {
        if (units == 0)
            break;
        if ((cur->typeID() != typeID) or cur->isSingleton())
            continue;
        if (cur->quantity() > units) {
            cur->AlterQuantity(-((int32)units), true);
            units = 0;
        } else {
            units -= cur->quantity();
            cur->SetQuantity(0, true, true);
        }
    }
}

uint32 TowerSE::GetLinkDest(uint32 sourceID, uint16 typeID)
{
    for (auto cur : m_links)
        if ((cur.sourceID == sourceID) and (cur.typeID == typeID))
            return cur.destID;
    return 0;
}

bool TowerSE::IsLinked(uint32 itemID)
{
    for (auto cur : m_links)
        if ((cur.sourceID == itemID) or (cur.destID == itemID))
            return true;
    return false;
}

bool TowerSE::Supplies(StructureSE* pSE, uint16 typeID)
{
    switch (pSE->GetSelf()->groupID()) {
        case EVEDB::invGroups::Moon_Mining:
        case EVEDB::invGroups::Silo:
            return ((typeID != 0) and (pSE->GetProvisionTypeID() == typeID));
        case EVEDB::invGroups::Mobile_Reactor: {
            InventoryItemRef reaction(GetReaction(pSE));
            std::vector<EVEPOS::ReactionRow> rows;
            if ((reaction.get() == nullptr) or !sDataMgr.GetReaction(reaction->typeID(), rows))
                return false;
            for (auto cur : rows)
                if (!cur.input and (cur.typeID == typeID))
                    return true;
        } break;
    }
    return false;
}

bool TowerSE::Demands(StructureSE* pSE, uint16 typeID)
{
    switch (pSE->GetSelf()->groupID()) {
        case EVEDB::invGroups::Silo:
            return ((typeID != 0) and (pSE->GetProvisionTypeID() == typeID));
        case EVEDB::invGroups::Mobile_Reactor: {
            InventoryItemRef reaction(GetReaction(pSE));
            std::vector<EVEPOS::ReactionRow> rows;
            if ((reaction.get() == nullptr) or !sDataMgr.GetReaction(reaction->typeID(), rows))
                return false;
            for (auto cur : rows)
                if (cur.input and (cur.typeID == typeID))
                    return true;
        } break;
    }
    return false;
}

bool TowerSE::SetProcessLinks(PyList* links)
{
    // [(sourceID, destID, typeID), ...]:  every link between two of this tower's process structures, the source
    //  supplying and the destination taking that material, one destination per source and material.  else nothing changes
    std::vector<EVEPOS::ProcessLink> newLinks;
    for (PyList::const_iterator itr = links->begin(); itr != links->end(); ++itr) {
        if (!(*itr)->IsTuple() and !(*itr)->IsList())
            return false;
        if (((*itr)->IsTuple() ? (*itr)->AsTuple()->size() : (*itr)->AsList()->size()) < 3)
            return false;
        PyRep* sRep((*itr)->IsTuple() ? (*itr)->AsTuple()->GetItem(0) : (*itr)->AsList()->GetItem(0));
        PyRep* dRep((*itr)->IsTuple() ? (*itr)->AsTuple()->GetItem(1) : (*itr)->AsList()->GetItem(1));
        PyRep* tRep((*itr)->IsTuple() ? (*itr)->AsTuple()->GetItem(2) : (*itr)->AsList()->GetItem(2));
        if ((sRep == nullptr) or (dRep == nullptr) or (tRep == nullptr) or !sRep->IsInt() or !dRep->IsInt() or !tRep->IsInt())
            return false;
        EVEPOS::ProcessLink link = EVEPOS::ProcessLink();
            link.sourceID = sRep->AsInt()->value();
            link.destID = dRep->AsInt()->value();
            link.typeID = tRep->AsInt()->value();
        std::map<uint32, StructureSE*>::iterator src(m_structs.find(link.sourceID)), dst(m_structs.find(link.destID));
        if ((src == m_structs.end()) or (dst == m_structs.end()) or (link.sourceID == link.destID))
            return false;
        if (!IsProcessStructure(src->second) or !IsProcessStructure(dst->second))
            return false;
        if (!Supplies(src->second, link.typeID) or !Demands(dst->second, link.typeID))
            return false;
        for (auto cur : newLinks)
            if ((cur.sourceID == link.sourceID) and (cur.typeID == link.typeID))
                return false;
        newLinks.push_back(link);
    }

    m_links = newLinks;
    PosMgrDB::ReplaceProcessLinks(m_data.itemID, m_links);
    NotifyProcessChange();
    return true;
}

void TowerSE::ChangeProvisionType(uint32 itemID, uint16 typeID)
{
    std::map<uint32, StructureSE*>::iterator itr(m_structs.find(itemID));
    if (itr == m_structs.end())
        throw CustomError ("That structure isn't controlled by this tower.");
    StructureSE* pSE(itr->second);
    uint16 groupID(pSE->GetSelf()->groupID());
    if (groupID == EVEDB::invGroups::Moon_Mining) {
        // only what this moon has
        if ((typeID != 0) and ((m_moonSE == nullptr) or (m_moonSE->GetResource(typeID) == 0)))
            throw UserError("MoonHarvesterInvalidType").AddFormatValue("type", new PyInt(typeID));
    } else if (groupID == EVEDB::invGroups::Silo) {
        if (typeID != 0) {
            const ItemType* pType(sItemFactory.GetType(typeID));
            if (pType == nullptr)
                throw UserError("MoonInvalidSiloType");
            // coupling arrays and the like hold one group, other silos moon materials and their products
            EvilNumber cargoGroup(EvilZero);
            if (pSE->GetSelf()->HasAttribute(AttrCargoGroup, cargoGroup) and (cargoGroup.get_uint32() != 0)) {
                if (pType->groupID() != cargoGroup.get_uint32())
                    throw UserError("MoonInvalidSiloType");
            } else {
                switch (pType->groupID()) {
                    case EVEDB::invGroups::Moon_Materials:
                    case EVEDB::invGroups::Intermediate_Materials:
                    case EVEDB::invGroups::Composite:
                    case EVEDB::invGroups::Gas_Isotopes:
                    case EVEDB::invGroups::Mineral:
                        break;
                    default:
                        throw UserError("MoonInvalidSiloType");
                }
            }
        }
        // a silo with something else in it can't change
        std::vector<InventoryItemRef> items;
        pSE->GetSelf()->GetMyInventory()->GetItemsByFlag(flagNone, items);
        for (auto cur : items)
            if (cur->typeID() != typeID)
                throw UserError("MoonSilosCannotChangeWithContents")
                        .AddFormatValue("newType", new PyInt(typeID))
                        .AddFormatValue("contenttype", new PyInt(cur->typeID()));
    } else {
        throw CustomError ("Only moon harvesters and silos are set to a material.");
    }

    pSE->SetProvisionTypeID(typeID);
    pSE->SaveData();

    // the links that carried the old material go
    std::vector<EVEPOS::ProcessLink> kept;
    for (auto cur : m_links)
        if (((cur.sourceID != itemID) and (cur.destID != itemID)) or (cur.typeID == typeID))
            kept.push_back(cur);
    if (kept.size() != m_links.size()) {
        m_links = kept;
        PosMgrDB::ReplaceProcessLinks(m_data.itemID, m_links);
    }
    NotifyProcessChange();
}

void TowerSE::RunMoonProcesses()
{
    /* an hour of moon mining and reactions.  only online process structures take part.
     *  harvesters deliver to their linked silo or reactor (no link, no harvest kept).  a reactor runs when it gets every
     *  input it needs (fed straight this hour, or from its linked silos) and every output has a linked place with room;
     *  else it stays idle and takes nothing.  its first hour after onlining is the start-up.  silos just store
     */
    std::map<uint32, std::map<uint16, uint32>> fed;     // reactor itemID / (typeID, units) delivered this hour
    std::map<uint32, bool> wasActive;
    std::vector<StructureSE*> harvesters, reactors;
    for (auto cur : m_structs) {
        if (!IsProcessStructure(cur.second))
            continue;
        wasActive[cur.first] = cur.second->IsProcessActive();
        if (!cur.second->IsOnline()) {
            cur.second->SetProcessActive(false);
            continue;
        }
        if (cur.second->GetSelf()->groupID() == EVEDB::invGroups::Moon_Mining)
            harvesters.push_back(cur.second);
        if (cur.second->GetSelf()->groupID() == EVEDB::invGroups::Mobile_Reactor)
            reactors.push_back(cur.second);
        if (cur.second->GetSelf()->groupID() == EVEDB::invGroups::Silo)
            cur.second->SetProcessActive(true);
    }

    // deliver to a silo or a reactor.  returns what was taken
    auto deliver = [&](uint32 destID, uint16 typeID, uint32 units) -> uint32 {
        std::map<uint32, StructureSE*>::iterator itr(m_structs.find(destID));
        if ((itr == m_structs.end()) or !itr->second->IsOnline())
            return 0;
        if (itr->second->GetSelf()->groupID() == EVEDB::invGroups::Silo)
            return AddToSilo(itr->second, typeID, units);
        if (itr->second->GetSelf()->groupID() == EVEDB::invGroups::Mobile_Reactor) {
            fed[destID][typeID] += units;
            return units;
        }
        return 0;
    };

    for (auto pSE : harvesters) {
        uint16 typeID(pSE->GetProvisionTypeID());
        uint8 moonQty((typeID == 0) or (m_moonSE == nullptr) ? 0 : m_moonSE->GetResource(typeID));
        if (moonQty == 0) {
            pSE->SetProcessActive(false);
            continue;
        }
        uint32 quality(pSE->GetSelf()->GetAttribute(AttrHarvesterQuality).get_uint32());
        uint32 units(ProcessUnits(typeID, std::min<uint32>(quality > 0 ? quality : 1, moonQty)));
        uint32 destID(GetLinkDest(pSE->GetID(), typeID));
        if (destID != 0)
            deliver(destID, typeID, units);
        pSE->SetProcessActive(true);
    }

    // reactors, those fed by other reactors after them (a few passes:  chains are short, loops never run)
    std::set<uint32> done;
    for (uint8 pass = 0; pass < 4; ++pass) {
        for (auto pSE : reactors) {
            if (done.find(pSE->GetID()) != done.end())
                continue;
            InventoryItemRef reaction(GetReaction(pSE));
            std::vector<EVEPOS::ReactionRow> rows;
            if ((reaction.get() == nullptr) or !sDataMgr.GetReaction(reaction->typeID(), rows)) {
                pSE->SetProcessActive(false);
                done.insert(pSE->GetID());
                continue;
            }
            // wait for reactors that feed this one
            bool waiting(false);
            for (auto link : m_links)
                if ((link.destID == pSE->GetID()) and (done.find(link.sourceID) == done.end()))
                    for (auto other : reactors)
                        if (other->GetID() == link.sourceID)
                            waiting = true;
            if (waiting and (pass < 3))
                continue;
            done.insert(pSE->GetID());

            // start-up hour
            if (!pSE->IsProcessActive()) {
                pSE->SetProcessActive(true);
                continue;
            }

            // inputs:  what was fed, the rest from linked silos
            bool ok(true);
            std::map<uint32, std::map<uint16, uint32>> takeFromSilo;    // silo / (type, units)
            for (auto row : rows) {
                if (!row.input)
                    continue;
                uint32 need(ProcessUnits(row.typeID, row.quantity));
                uint32 have(fed[pSE->GetID()][row.typeID]);
                if (have >= need)
                    continue;
                need -= have;
                for (auto link : m_links) {
                    if ((need == 0) or (link.destID != pSE->GetID()) or (link.typeID != row.typeID))
                        continue;
                    std::map<uint32, StructureSE*>::iterator src(m_structs.find(link.sourceID));
                    if ((src == m_structs.end()) or (src->second->GetSelf()->groupID() != EVEDB::invGroups::Silo)
                    or !src->second->IsOnline())
                        continue;
                    uint32 stored(GetSiloUnits(src->second, row.typeID) - takeFromSilo[link.sourceID][row.typeID]);
                    uint32 take(std::min(stored, need));
                    takeFromSilo[link.sourceID][row.typeID] += take;
                    need -= take;
                }
                if (need > 0)
                    ok = false;
            }
            // outputs:  every one needs a linked place with room
            if (ok)
                for (auto row : rows) {
                    if (row.input)
                        continue;
                    uint32 destID(GetLinkDest(pSE->GetID(), row.typeID));
                    std::map<uint32, StructureSE*>::iterator dst(m_structs.find(destID));
                    if ((destID == 0) or (dst == m_structs.end()) or !dst->second->IsOnline()) {
                        ok = false;
                        break;
                    }
                    if (dst->second->GetSelf()->groupID() == EVEDB::invGroups::Silo) {
                        const ItemType* pType(sItemFactory.GetType(row.typeID));
                        double volume((pType != nullptr) and (pType->volume() > 0) ? pType->volume() : 1);
                        double freeVolume(GetSiloCapacity(dst->second) - dst->second->GetSelf()->GetMyInventory()->GetStoredVolume(flagNone));
                        if (freeVolume < ProcessUnits(row.typeID, row.quantity) * volume) {
                            ok = false;
                            break;
                        }
                    }
                }
            if (!ok) {
                _log(POS__DEBUG, "TowerSE::RunMoonProcesses() - reactor %s(%u) has nothing to do this hour.", pSE->GetName(), pSE->GetID());
                continue;
            }

            for (auto silo : takeFromSilo)
                for (auto cur : silo.second)
                    if (cur.second > 0)
                        TakeFromSilo(m_structs[silo.first], cur.first, cur.second);
            for (auto row : rows)
                if (!row.input)
                    deliver(GetLinkDest(pSE->GetID(), row.typeID), row.typeID, ProcessUnits(row.typeID, row.quantity));
        }
    }

    // keep the activity, tell the managers watching
    bool changed(false);
    for (auto cur : wasActive) {
        StructureSE* pSE(m_structs[cur.first]);
        if (pSE->IsProcessActive() != cur.second) {
            pSE->SaveData();
            changed = true;
        }
    }
    if (changed and !m_replaying)
        NotifyProcessChange();
}

void TowerSE::NotifyProcessChange()
{
    // OnMoonProcessChange(towerID, structures) to the tower's corp members in its bubble (the tower window refreshes)
    if (m_bubble == nullptr)
        return;
    std::vector<Client*> players;
    m_bubble->GetPlayers(players);
    for (auto pClient : players) {
        if ((uint32)pClient->GetCorporationID() != m_corpID)
            continue;
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, new PyInt(m_data.itemID));
            tuple->SetItem(1, GetProcessInfo());
        pClient->SendNotification("OnMoonProcessChange", "charid", &tuple);
    }
}

PyRep* TowerSE::GetProcessInfo()
{
    /*
            info = self.posMgr.GetMoonProcessInfoForTower(self.slimItem.itemID)
            itemID, active, reaction, connections, demands, supplies in info:

            if reaction and rec.groupID == const.groupMobileReactor and not demands and not supplies:
                demands = [ (row.typeID, row.quantity) for row in cfg.invtypereactions[reaction[1]] if row.input == 1 ]
                supplies = [ (row.typeID, row.quantity) for row in cfg.invtypereactions[reaction[1]] if row.input == 0 ]
                demand = {}
                demands = demands or []
                for tID, quant in demands:
                    demand[tID] = quant

                    supply = {}
                    supplies = supplies or []
                    for tID, quant in supplies:
                        supply[tID] = quant

                for sourceID, tID in connections:
                    self.sr.structureConnections[tID, sourceID] = itemID

        [PyList 2 items]
          [PyTuple 6 items]
            [PyIntegerVar 1002332982210]
            [PyBool False]
            [PyNone]
            [PyList 0 items]
            [PyList 0 items]
            [PyList 0 items]
          [PyTuple 6 items]
            [PyIntegerVar 1002331680835]
            [PyBool False]
            [PyNone]
            [PyList 0 items]
            [PyList 0 items]
            [PyList 0 items]
            */

    PyList* list = new PyList();
    for (auto cur : m_structs) {
        if (!IsProcessStructure(cur.second))
            continue;
        StructureSE* pSE(cur.second);
        uint16 groupID(pSE->GetSelf()->groupID());
        uint16 typeID(pSE->GetProvisionTypeID());

        // (reactionItemID, reactionTypeID)
        PyRep* reaction(nullptr);
        if (groupID == EVEDB::invGroups::Mobile_Reactor) {
            InventoryItemRef rRef(GetReaction(pSE));
            if (rRef.get() != nullptr) {
                PyTuple* rTuple = new PyTuple(2);
                    rTuple->SetItem(0, new PyInt(rRef->itemID()));
                    rTuple->SetItem(1, new PyInt(rRef->typeID()));
                reaction = rTuple;
            }
        }
        // links into this structure:  (sourceID, typeID)
        PyList* connections = new PyList();
        for (auto link : m_links)
            if (link.destID == cur.first) {
                PyTuple* lTuple = new PyTuple(2);
                    lTuple->SetItem(0, new PyInt(link.sourceID));
                    lTuple->SetItem(1, new PyInt(link.typeID));
                connections->AddItem(lTuple);
            }
        // reactors:  the client works them out from the reaction
        PyList* demands = new PyList();
        PyList* supplies = new PyList();
        if ((typeID != 0) and (groupID == EVEDB::invGroups::Silo)) {
            const ItemType* pType(sItemFactory.GetType(typeID));
            double volume((pType != nullptr) and (pType->volume() > 0) ? pType->volume() : 1);
            uint32 capacity((uint32)(GetSiloCapacity(pSE) / volume));
            PyTuple* dTuple = new PyTuple(2);
                dTuple->SetItem(0, new PyInt(typeID));
                dTuple->SetItem(1, new PyInt(capacity));
            demands->AddItem(dTuple);
            PyTuple* sTuple = new PyTuple(2);
                sTuple->SetItem(0, new PyInt(typeID));
                sTuple->SetItem(1, new PyInt(capacity));
            supplies->AddItem(sTuple);
        } else if ((typeID != 0) and (groupID == EVEDB::invGroups::Moon_Mining)) {
            uint32 quality(pSE->GetSelf()->GetAttribute(AttrHarvesterQuality).get_uint32());
            uint8 moonQty(m_moonSE == nullptr ? 0 : m_moonSE->GetResource(typeID));
            PyTuple* sTuple = new PyTuple(2);
                sTuple->SetItem(0, new PyInt(typeID));
                sTuple->SetItem(1, new PyInt(std::min<uint32>(quality > 0 ? quality : 1, moonQty)));
            supplies->AddItem(sTuple);
        }

        PyTuple* tuple = new PyTuple(6);
            tuple->SetItem(0, new PyInt(cur.first));
            tuple->SetItem(1, new PyBool(pSE->IsProcessActive()));
            tuple->SetItem(2, (reaction == nullptr ? PyStatic.NewNone() : reaction));
            tuple->SetItem(3, connections);
            tuple->SetItem(4, demands);
            tuple->SetItem(5, supplies);
        list->AddItem(tuple);
    }

    if (is_log_enabled(POS__RSP_DUMP))
        list->Dump(POS__RSP_DUMP, "   ");
    return list;
}

void TowerSE::CreateForceField(bool notify)
{
    if (m_hasShield)
        return;
    // create and add force field to tower
    ItemData idata(EVEDB::invTypes::ForceField, m_corpID, m_system->GetID(), flagNone, m_ownerID);
    InventoryItemRef ifRef = sItemFactory.SpawnItem(idata);
    if (ifRef.get() == nullptr)
        return;  // we'll get over it
    ifRef->SetPosition(GetPosition());
    ifRef->SetAttribute(AttrRadius, m_self->GetAttribute(AttrShieldRadius), false);
    FactionData data = FactionData();
        data.allianceID = m_allyID;
        data.corporationID = m_corpID;
        data.factionID = m_warID;
        data.ownerID = m_ownerID;
    FieldSE* iSE = new FieldSE(ifRef, m_services, m_system, data);
    // the password's harmonic, and the tower's corp/alliance if they are let in
    m_tdata.harmonic = EVEPOS::HarmonicFromPassword(m_tdata.password);
    iSE->SetHarmonic(m_tdata.harmonic);
    iSE->SetAccess(m_tdata.allowCorp, m_tdata.allowAlliance);
    m_system->AddEntity(iSE);
    m_pShieldSE = iSE;
    m_hasShield = true;
    // forceFieldID
    if (notify)
        SendSlimUpdate();
}

PyDict* TowerSE::MakeSlimItem()
{
    _log(SE__SLIMITEM, "MakeSlimItem for TowerSE %u", m_self->itemID());
    _log(POS__SLIMITEM, "MakeSlimItem for TowerSE %u", m_self->itemID());

    PyDict *slim = StructureSE::MakeSlimItem();
    // the client compares ship and field harmonics through this (pwn.CompareShipStructureHarmonic)
    slim->SetItemString("forceFieldID",             (m_pShieldSE == nullptr ? PyStatic.NewNone() : new PyLong(m_pShieldSE->GetID())));

    if (is_log_enabled(POS__SLIMITEM)) {
        _log( POS__SLIMITEM, "TowerSE::MakeSlimItem() - %s(%u)", GetName(), GetID());
        slim->Dump(POS__SLIMITEM, "     ");
    }
    return slim;
}
/*
                      [PyString "OnSlimItemChange"]
                      [PyTuple 2 items]
                        [PyIntegerVar 1006120578679]
                        [PyObjectData Name: foo.SlimItem]
                          [PyDict 11 kvp]
                            [PyString "itemID"]
                            [PyIntegerVar 1006120578679]
                            [PyString "typeID"]
                            [PyInt 20060]               <<--  Amarr Control Tower Small
                            [PyString "name"]
                            [PyString "Pix0r monto la torre de al lado"]
                            [PyString "incapacitated"]
                            [PyFloat 0]
                            [PyString "posTimestamp"]
                            [PyIntegerVar 129773067243437304]
                            [PyString "posState"]
                            [PyInt 4]
                            [PyString "warFactionID"]
                            [PyNone]
                            [PyString "allianceID"]
                            [PyInt 99001691]
                            [PyString "corpID"]
                            [PyInt 717154310]
                            [PyString "ownerID"]
                            [PyInt 717154310]
                            [PyString "nameID"]
                            [PyNone]
                    */

/*
 *    def IsShipInRangeOfStructureControlTower(self, shipID, structureID):
 *        """
 *        Returns True if structureID is associated with a control tower (or is itself a tower), and shipID is in range of that tower.
 *        'In range of the tower' means within of the force-field radius.
 *        This function should mirror the behaviour of the equivalent server-side function in park.py
 *        """
 *        structureSlim = self.slimItems.get(structureID)
 *        if structureSlim is None:
 *            return False
 *        controlTowerID = None
 *        if structureSlim.groupID == const.groupControlTower:
 *            controlTowerID = structureID
 *        elif structureSlim.controlTowerID is not None:
 *            controlTowerID = structureSlim.controlTowerID
 *        if controlTowerID is None:
 *            return False
 *        towerSlim = self.slimItems.get(controlTowerID)
 *        if towerSlim is None:
 *            return False
 *        towerShieldRadius = self.broker.godma.GetStateManager().GetType(towerSlim.typeID).shieldRadius
 *        return self.GetCenterDist(controlTowerID, shipID) < towerShieldRadius
 */