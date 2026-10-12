 /**
  * @name IncursionMgr.cpp
  *     Sansha incursion management for EVEmu
  *
  * @date:          12 October 2026
  *
  *  see IncursionMgr.h for what this does and doesnt cover yet.
  *  site layouts, rewards and the Sansha ship stats are approximations (incSite* tables and the incursions migration)
  */

#include <algorithm>
#include <ctime>
#include <queue>
#include <set>

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "EVE_Incursion.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "corporation/LPService.h"
#include "inventory/ItemFactory.h"
#include "map/MapDB.h"
#include "npc/NPC.h"
#include "system/Celestial.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/IncursionDB.h"
#include "system/cosmicMgrs/IncursionMgr.h"
#include "../../../eve-common/EVE_Scanning.h"

// customInfo for every site item.  DBCleaner removes '%livedungeon%' items on server start,
//  and ManagerDB::ClearDungeons() removes them when their system unloads
static const char* IncursionCustomInfo = "livedungeon_incursion";

static uint32 TimeNow()
{
    return (uint32)std::time(nullptr);
}

// random index in [0, count)
static size_t RandomIndex(size_t count)
{
    if (count < 2)
        return 0;
    size_t idx = (size_t)MakeRandomFloat(0.0, (double)count);
    return (idx < count ? idx : count - 1);
}

static const char* SceneName(uint8 sceneType)
{
    switch (sceneType) {
        case Incursion::scenesType::headquarters:   return "Headquarters";
        case Incursion::scenesType::assault:        return "Assault";
        case Incursion::scenesType::vanguard:       return "Vanguard";
        case Incursion::scenesType::staging:        return "Staging";
    }
    return "Unknown";
}

static const char* StateName(uint8 state)
{
    switch (state) {
        case Incursion::State::Established:     return "Established";
        case Incursion::State::Mobilizing:      return "Mobilizing";
        case Incursion::State::Withdrawing:     return "Withdrawing";
    }
    return "Unknown";
}

IncursionMgr::IncursionMgr()
: m_initialized(false),
  m_nextSiteID(0),
  m_nextAutoStart(0),
  m_lastTick(0),
  m_lastSave(0),
  m_procTimer(0)
{
    m_siteTypes.clear();
    m_incursions.clear();
    m_sites.clear();
    m_siteCooldown.clear();
}

void IncursionMgr::Initialize()
{
    if (!sConfig.incursion.Enabled) {
        sLog.Warning("Incursion Manager", "Incursions Disabled.");
        return;
    }

    LoadSiteTypes();
    if (m_siteTypes.empty()) {
        sLog.Error("Incursion Manager", "No incursion site data found (incSiteTypes).  Has the incursions database update been applied?  Incursions Disabled.");
        return;
    }
    LoadIncursions();

    uint32 now = TimeNow();
    m_lastTick = now;
    m_lastSave = now;
    m_nextAutoStart = now + 300;    // first automatic incursion five minutes after boot
    m_procTimer.Start(2000);
    m_initialized = true;

    sLog.Blue("Incursion Manager", "Incursion Manager Initialized with %u site types and %u active incursions.", m_siteTypes.size(), m_incursions.size());
}

void IncursionMgr::LoadSiteTypes()
{
    DBQueryResult res;
    DBResultRow row;
    IncursionDB::GetSiteTypes(res);
    while (res.GetRow(row)) {
        IncSiteType type = IncSiteType();
            type.siteTypeID = row.GetUInt(0);
            type.name = row.GetText(1);
            type.sceneType = row.GetUInt(2);
            type.designFleet = row.GetUInt(3);
            type.iskReward = row.GetUInt(4);
            type.lpReward = row.GetUInt(5);
            type.influence = row.GetFloat(6);
            type.waves = 0;
            type.designNPCs = 0;
        if (type.designFleet < 1)
            type.designFleet = 1;
        m_siteTypes[type.siteTypeID] = type;
    }

    IncursionDB::GetSiteObjects(res);
    while (res.GetRow(row)) {
        std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(row.GetUInt(0));
        if (itr == m_siteTypes.end())
            continue;
        IncSiteObject obj = IncSiteObject();
            obj.wave = row.GetUInt(1);
            obj.typeID = row.GetUInt(2);
            obj.quantity = row.GetUInt(3);
            obj.offset = GPoint(row.GetDouble(4), row.GetDouble(5), row.GetDouble(6));
        if (obj.wave > itr->second.waves)
            itr->second.waves = obj.wave;
        if (obj.wave > 0)
            itr->second.designNPCs += obj.quantity;
        itr->second.objects.push_back(obj);
    }
}

void IncursionMgr::LoadIncursions()
{
    DBQueryResult res;
    DBResultRow row;
    IncursionDB::GetIncursions(res);
    while (res.GetRow(row)) {
        IncData inc = IncData();
            inc.incursionID = row.GetUInt(0);
            inc.constellationID = row.GetUInt(1);
            inc.stagingSystemID = row.GetUInt(2);
            inc.state = row.GetUInt(3);
            inc.influence = row.GetDouble(4);
            inc.startTime = row.GetUInt(5);
            inc.stateTime = row.GetUInt(6);
            inc.lastProgress = row.GetUInt(7);
        m_incursions[inc.incursionID] = inc;
    }

    IncursionDB::GetIncursionSystems(res);
    while (res.GetRow(row)) {
        std::map<uint32, IncData>::iterator itr = m_incursions.find(row.GetUInt(0));
        if (itr != m_incursions.end())
            itr->second.systems[row.GetUInt(1)] = row.GetUInt(2);
    }
}

void IncursionMgr::Save(IncData& inc)
{
    IncursionDB::SaveIncursion(inc.incursionID, inc.state, inc.influence, inc.stateTime, inc.lastProgress);
}

IncData* IncursionMgr::FindByConstellation(uint32 constellationID)
{
    for (auto& cur : m_incursions)
        if (cur.second.constellationID == constellationID)
            return &cur.second;
    return nullptr;
}

void IncursionMgr::Process()
{
    if (!m_initialized)
        return;
    if (!m_procTimer.Check())
        return;

    uint32 now = TimeNow();
    uint32 dt = (now > m_lastTick ? now - m_lastTick : 0);
    m_lastTick = now;

    // start a new incursion when there are fewer than allowed
    if (sConfig.incursion.AutoStart and (m_incursions.size() < sConfig.incursion.MaxActive) and (now >= m_nextAutoStart)) {
        size_t before = m_incursions.size();
        std::string msg = Start();
        if (m_incursions.size() > before) {
            m_nextAutoStart = now + 60;
        } else {
            _log(COSMIC_MGR__WARNING, "IncursionMgr::Process() - automatic start failed: %s", msg.c_str());
            m_nextAutoStart = now + 600;
        }
    }

    // incursion state, influence and endings
    std::vector<uint32> ended;
    for (auto& cur : m_incursions) {
        ProcessIncursion(cur.second, now, dt);
        if ((cur.second.state == Incursion::State::Withdrawing)
        and (now - cur.second.stateTime >= sConfig.incursion.WithdrawTime * 60u))
            ended.push_back(cur.first);
    }
    for (auto cur : ended)
        EndIncursion(cur);

    // sites: drop dead ships, spawn the next wave, finish cleared sites
    std::vector<uint32> done;
    for (auto& cur : m_sites) {
        IncLiveSite& site = cur.second;
        if (!sEntityList.IsSystemLoaded(site.systemID)) {
            // system went away without telling us
            IncursionDB::DeleteSignature(site.sigID);
            done.push_back(cur.first);
            continue;
        }
        SystemManager* pSys = sEntityList.FindOrBootSystem(site.systemID);
        if (pSys == nullptr) {
            done.push_back(cur.first);
            continue;
        }
        site.npcs.erase(std::remove_if(site.npcs.begin(), site.npcs.end(),
                        [pSys](uint32 id) { return pSys->GetSE(id) == nullptr; }), site.npcs.end());
        if (!site.npcs.empty())
            continue;

        std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(site.siteTypeID);
        if ((itr != m_siteTypes.end()) and (site.wave < itr->second.waves)) {
            SpawnWave(site, pSys);
            continue;
        }

        CompleteSite(site, pSys);
        done.push_back(cur.first);
    }
    for (auto cur : done)
        m_sites.erase(cur);

    // keep vanguard systems stocked while they're loaded
    for (auto& cur : m_incursions) {
        if (cur.second.state == Incursion::State::Withdrawing)
            continue;
        for (auto sys : cur.second.systems)
            if (sys.second == Incursion::scenesType::vanguard)
                TopUpSites(cur.second, sys.first, now);
    }

    if (now - m_lastSave >= 60) {
        for (auto& cur : m_incursions)
            Save(cur.second);
        m_lastSave = now;
    }
}

void IncursionMgr::ProcessIncursion(IncData& inc, uint32 now, uint32 dt)
{
    if (inc.state == Incursion::State::Withdrawing)
        return;

    if ((inc.state == Incursion::State::Established)
    and (now - inc.startTime >= sConfig.incursion.MobilizeAfter * 3600u)) {
        inc.state = Incursion::State::Mobilizing;
        inc.stateTime = now;
        Save(inc);
        _log(COSMIC_MGR__MESSAGE, "IncursionMgr - incursion %u in %s is now mobilizing.", inc.incursionID, IncursionDB::GetConstellationName(inc.constellationID).c_str());
    }

    if ((inc.influence <= 0) or (now - inc.startTime >= sConfig.incursion.Lifetime * 3600u)) {
        inc.state = Incursion::State::Withdrawing;
        inc.stateTime = now;
        Save(inc);
        std::string msg = "Sansha's Nation is withdrawing from ";
        msg += IncursionDB::GetConstellationName(inc.constellationID);
        msg += (inc.influence <= 0 ? ". Their influence there has been broken." : ".");
        Broadcast(msg);
        return;
    }

    // influence slowly comes back when nobody is clearing sites
    if ((inc.influence < 1.0) and (dt > 0) and (now - inc.lastProgress >= sConfig.incursion.RegenDelay * 60u)) {
        inc.influence += sConfig.incursion.RegenRate / 100.0 * dt / 3600.0;
        if (inc.influence > 1.0)
            inc.influence = 1.0;
    }
}

void IncursionMgr::TopUpSites(IncData& inc, uint32 systemID, uint32 now)
{
    if (!sEntityList.IsSystemLoaded(systemID))
        return;
    std::map<uint32, uint32>::iterator cd = m_siteCooldown.find(systemID);
    if ((cd != m_siteCooldown.end()) and (now < cd->second))
        return;

    std::map<uint16, uint8> count;
    uint8 total(0);
    for (auto& cur : m_sites)
        if (cur.second.systemID == systemID) {
            ++count[cur.second.siteTypeID];
            ++total;
        }
    if (total >= sConfig.incursion.SitesPerSystem)
        return;

    // the vanguard site type with the fewest live copies here, random among ties
    std::vector<uint16> pick;
    uint8 best(255);
    for (auto& cur : m_siteTypes) {
        if (cur.second.sceneType != Incursion::scenesType::vanguard)
            continue;
        uint8 have = count[cur.first];
        if (have < best) {
            best = have;
            pick.clear();
        }
        if (have == best)
            pick.push_back(cur.first);
    }
    if (pick.empty())
        return;

    SystemManager* pSys = sEntityList.FindOrBootSystem(systemID);
    if (pSys == nullptr)
        return;

    // one site per system per tick.  on failure, wait a minute before trying again
    if (!SpawnSite(inc, pSys, pick[RandomIndex(pick.size())]))
        m_siteCooldown[systemID] = now + 60;
}

GPoint IncursionMgr::GetSitePoint(SystemManager* pSys)
{
    // somewhere between a third of an AU and two AU from a random planet
    std::vector<DBGPointEntity> planets;
    uint8 total(0);
    MapDB::GetPlanets(pSys->GetID(), planets, total);

    GPoint pos(NULL_ORIGIN);
    if (!planets.empty()) {
        DBGPointEntity& planet = planets[RandomIndex(planets.size())];
        SystemEntity* pSE = pSys->GetSE(planet.itemID);
        pos = (pSE != nullptr ? pSE->GetPosition() : planet.position);
        pos.MakeRandomPointOnSphereLayer(ONE_AU_IN_METERS / 3, ONE_AU_IN_METERS * 2);
    } else {
        pos.MakeRandomPointOnSphereLayer(ONE_AU_IN_METERS * 2, ONE_AU_IN_METERS * 6);
    }
    return pos;
}

bool IncursionMgr::SpawnSite(IncData& inc, SystemManager* pSys, uint16 siteTypeID)
{
    std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(siteTypeID);
    if (itr == m_siteTypes.end())
        return false;
    IncSiteType& type = itr->second;
    uint32 corpID = sDataMgr.GetFactionCorp(factionSanshas);

    CosmicSignature sig = CosmicSignature();
        sig.dungeonType = Dungeon::Type::Anomaly;
        sig.sigTypeID = EVEDB::invTypes::CosmicAnomaly;
        sig.sigGroupID = EVEDB::invGroups::Cosmic_Anomaly;
        sig.scanGroupID = Scanning::Group::Anomaly;
        sig.scanAttributeID = AttrScanAllStrength;
        sig.sigStrength = 1.0f;
        sig.ownerID = factionSanshas;
        sig.systemID = pSys->GetID();
        sig.sigID = sEntityList.GetAnomalyID();
        sig.sigName = type.name;
        sig.position = GetSitePoint(pSys);

    // site beacon.  this is what the scanner and warp-to use
    ItemData bData(sig.sigTypeID, corpID, sig.systemID, flagNone, sig.sigName.c_str(), sig.position, IncursionCustomInfo);
    InventoryItemRef bRef = sItemFactory.SpawnItem(bData);
    if (bRef.get() == nullptr) {
        _log(COSMIC_MGR__ERROR, "IncursionMgr::SpawnSite() - unable to spawn beacon for %s in %s", type.name.c_str(), pSys->GetName());
        return false;
    }
    CelestialSE* bSE = new CelestialSE(bRef, pSys->GetServiceMgr(), pSys);
    pSys->AddEntity(bSE, false);
    sig.sigItemID = bRef->itemID();
    SystemBubble* pBubble = bSE->SysBubble();
    if (pBubble != nullptr) {
        sig.bubbleID = pBubble->GetID();
        pBubble->SetIncursion(true);   // keeps Sansha from warping off
    }
    pSys->GetAnomMgr()->RegisterAnomaly(sig);

    IncLiveSite site = IncLiveSite();
        site.incursionID = inc.incursionID;
        site.systemID = sig.systemID;
        site.siteTypeID = siteTypeID;
        site.wave = 0;
        site.spawned = 0;
        site.beaconID = sig.sigItemID;
        site.position = sig.position;
        site.sigID = sig.sigID;

    // structures that stay for the whole site
    for (auto& obj : type.objects) {
        if (obj.wave != 0)
            continue;
        GPoint pos(sig.position.x + obj.offset.x, sig.position.y + obj.offset.y, sig.position.z + obj.offset.z);
        ItemData sData(obj.typeID, corpID, sig.systemID, flagNone, sDataMgr.GetTypeName(obj.typeID), pos, IncursionCustomInfo);
        InventoryItemRef sRef = sItemFactory.SpawnItem(sData);
        if (sRef.get() == nullptr) {
            _log(COSMIC_MGR__ERROR, "IncursionMgr::SpawnSite() - unable to spawn structure type %u for %s", obj.typeID, type.name.c_str());
            continue;
        }
        CelestialSE* sSE = new CelestialSE(sRef, pSys->GetServiceMgr(), pSys);
        pSys->AddEntity(sSE, false);
        site.structures.push_back(sRef->itemID());
    }

    std::pair<std::map<uint32, IncLiveSite>::iterator, bool> res = m_sites.emplace(++m_nextSiteID, site);
    SpawnWave(res.first->second, pSys);

    _log(COSMIC_MGR__MESSAGE, "IncursionMgr::SpawnSite() - spawned %s (%s) in %s(%u)", type.name.c_str(), sig.sigID.c_str(), pSys->GetName(), sig.systemID);
    return true;
}

void IncursionMgr::SpawnWave(IncLiveSite& site, SystemManager* pSys)
{
    ++site.wave;
    std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(site.siteTypeID);
    if (itr == m_siteTypes.end())
        return;
    IncSiteType& type = itr->second;

    // scale the wave down for smaller fleets.  each entry is rounded up or down at random,
    //  so over a site the ship count matches the fleet size on average.  a wave never comes up empty.
    float ratio = (float)sConfig.incursion.FleetSize / type.designFleet;
    if (ratio > 1.0f)
        ratio = 1.0f;
    if (ratio < 0.01f)
        ratio = 0.01f;

    std::vector<std::pair<const IncSiteObject*, uint8>> spawn;
    std::vector<const IncSiteObject*> entries;
    uint16 total(0), weight(0);
    for (auto& obj : type.objects) {
        if (obj.wave != site.wave)
            continue;
        entries.push_back(&obj);
        weight += obj.quantity;
        float want = obj.quantity * ratio;
        uint8 count = (uint8)want;
        if (MakeRandomFloat(0.0, 1.0) < (want - count))
            ++count;
        if (count > 0) {
            spawn.push_back(std::make_pair(&obj, count));
            total += count;
        }
    }
    if (entries.empty())
        return;
    if (total == 0) {
        // pick one ship, weighted by how many of each the full wave has
        float roll = MakeRandomFloat(0.0, (double)weight);
        const IncSiteObject* pick = entries.back();
        for (auto obj : entries) {
            if (roll < obj->quantity) {
                pick = obj;
                break;
            }
            roll -= obj->quantity;
        }
        spawn.push_back(std::make_pair(pick, 1));
    }

    float strength = sConfig.incursion.Strength / 100.0f;
    if (strength < 0.05f)
        strength = 0.05f;

    uint32 corpID = sDataMgr.GetFactionCorp(factionSanshas);
    FactionData data = FactionData();
        data.allianceID = factionSanshas;   // wreck salvage tests for faction
        data.corporationID = corpID;
        data.factionID = factionSanshas;
        data.ownerID = corpID;

    for (auto cur : spawn) {
        const IncSiteObject* obj = cur.first;
        GPoint center(site.position.x + obj->offset.x, site.position.y + obj->offset.y, site.position.z + obj->offset.z);
        for (uint8 i = 0; i < cur.second; ++i) {
            GPoint pos(center);
            pos.MakeRandomPointOnSphere(MakeRandomInt(1, 8) * 1000);
            ItemData idata(obj->typeID, corpID, site.systemID, flagNone, "", pos);
            InventoryItemRef iRef = sItemFactory.SpawnItem(idata);
            if (iRef.get() == nullptr) {
                _log(COSMIC_MGR__ERROR, "IncursionMgr::SpawnWave() - unable to spawn NPC type %u", obj->typeID);
                continue;
            }

            // must happen before the NPC is built; it reads these in its constructor
            if (strength != 1.0f) {
                const uint16 scaled[] = { AttrShieldCapacity, AttrArmorHP, AttrHP, AttrEntityShieldBoostAmount, AttrDamageMultiplier };
                for (auto attr : scaled)
                    if (iRef->HasAttribute(attr))
                        iRef->SetAttribute(attr, iRef->GetAttribute(attr).get_float() * strength, false);
                // missile damage comes from the missile itself, so fire faster or slower instead
                if (iRef->HasAttribute(AttrMissileLaunchDuration)) {
                    float duration = iRef->GetAttribute(AttrMissileLaunchDuration).get_float();
                    if (duration > 100)
                        iRef->SetAttribute(AttrMissileLaunchDuration, duration / strength, false);
                }
            }

            NPC* pNPC = new NPC(iRef, pSys->GetServiceMgr(), pSys, data);
            if (!pNPC->Load()) {
                _log(COSMIC_MGR__ERROR, "IncursionMgr::SpawnWave() - failed to load NPC %u of type %u", pNPC->GetID(), obj->typeID);
                pNPC->Delete();
                continue;
            }
            pSys->AddNPC(pNPC);
            pNPC->DestinyMgr()->SetPosition(pos);
            if (pNPC->SysBubble() != nullptr)
                pNPC->SysBubble()->SetIncursion(true);

            site.npcs.push_back(iRef->itemID());
            ++site.spawned;
        }
    }

    _log(COSMIC_MGR__MESSAGE, "IncursionMgr::SpawnWave() - %s in %s: wave %u of %u, %u ships", \
            type.name.c_str(), pSys->GetName(), site.wave, type.waves, site.npcs.size());
}

void IncursionMgr::CompleteSite(IncLiveSite& site, SystemManager* pSys)
{
    std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(site.siteTypeID);
    if (itr == m_siteTypes.end()) {
        RemoveSite(site, pSys);
        return;
    }
    IncSiteType& type = itr->second;
    uint32 now = TimeNow();

    std::string place = IncursionDB::GetConstellationName(pSys->GetConstellationID());
    double influence(0);
    std::map<uint32, IncData>::iterator iItr = m_incursions.find(site.incursionID);
    if (iItr != m_incursions.end()) {
        IncData& inc = iItr->second;
        inc.influence -= type.influence / 100.0;
        if (inc.influence < 0)
            inc.influence = 0;
        inc.lastProgress = now;
        influence = inc.influence;
        Save(inc);
    }

    // pay the pilots on grid.  the site's reward pool is what a full fleet would get for the ships that actually
    //  spawned, split between the pilots here, and nobody gets more than a full-fleet pilot would
    std::vector<Client*> pilots;
    SystemEntity* pBeacon = pSys->GetSE(site.beaconID);
    if ((pBeacon != nullptr) and (pBeacon->SysBubble() != nullptr))
        pBeacon->SysBubble()->GetPlayers(pilots);
    if (!pilots.empty()) {
        double share = (double)type.designFleet * site.spawned / (type.designNPCs > 0 ? type.designNPCs : 1) / pilots.size();
        if (share > 1.0)
            share = 1.0;
        double isk = type.iskReward * share;
        uint32 lp = (uint32)(type.lpReward * share + 0.5);
        std::string reason = "Incursion site completed: ";
        reason += type.name;
        for (auto pClient : pilots) {
            if (pClient == nullptr)
                continue;
            uint32 charID = pClient->GetCharacterID();
            try {
                AccountService::TransferFunds(corpCONCORD, charID, isk, reason, Journal::EntryType::RewardManager, pSys->GetID());
            } catch (...) {
                _log(COSMIC_MGR__ERROR, "IncursionMgr::CompleteSite() - ISK payout to %u failed", charID);
            }
            if (lp > 0)
                LPService::AddLP(charID, corpCONCORD, lp);
            pClient->SendNotifyMsg("%s completed. CONCORD has paid you %.0f ISK and %u loyalty points. Sansha influence in %s is now %.0f%%.", \
                    type.name.c_str(), isk, lp, place.c_str(), influence * 100);
        }
    }

    _log(COSMIC_MGR__MESSAGE, "IncursionMgr::CompleteSite() - %s completed in %s with %u pilots on grid.  Influence now %.1f%%", \
            type.name.c_str(), pSys->GetName(), pilots.size(), influence * 100);

    RemoveSite(site, pSys);
    m_siteCooldown[site.systemID] = now + sConfig.incursion.SiteRespawn;
}

void IncursionMgr::RemoveSite(IncLiveSite& site, SystemManager* pSys)
{
    if (pSys != nullptr) {
        // ships left over (incursion ended mid-site).  the NPC object stays allocated, as with killed NPCs,
        //  since missiles in flight may still point at it
        for (auto id : site.npcs) {
            NPC* pNPC = pSys->GetNPCSE(id);
            if (pNPC == nullptr)
                continue;
            if (pNPC->TargetMgr() != nullptr)
                pNPC->TargetMgr()->ClearFromTargets();
            pSys->RemoveNPC(pNPC);
        }
        site.npcs.clear();

        SystemEntity* pBeacon = pSys->GetSE(site.beaconID);
        if ((pBeacon != nullptr) and (pBeacon->SysBubble() != nullptr)) {
            // clear the bubble flag unless another site shares this bubble
            SystemBubble* pBubble = pBeacon->SysBubble();
            bool shared(false);
            for (auto& cur : m_sites) {
                if ((&cur.second == &site) or (cur.second.systemID != site.systemID))
                    continue;
                SystemEntity* pOther = pSys->GetSE(cur.second.beaconID);
                if ((pOther != nullptr) and (pOther->SysBubble() == pBubble))
                    shared = true;
            }
            if (!shared)
                pBubble->SetIncursion(false);
        }

        for (auto id : site.structures) {
            SystemEntity* pSE = pSys->GetSE(id);
            if (pSE == nullptr)
                continue;
            pSE->Delete();
            SafeDelete(pSE);
        }
        site.structures.clear();

        // this also takes the site off the scanner
        if (pBeacon != nullptr) {
            pBeacon->Delete();
            SafeDelete(pBeacon);
        }
    }
    IncursionDB::DeleteSignature(site.sigID);
}

void IncursionMgr::SystemUnloading(uint32 systemID)
{
    // the system deletes the entities itself
    std::map<uint32, IncLiveSite>::iterator itr = m_sites.begin();
    while (itr != m_sites.end()) {
        if (itr->second.systemID == systemID) {
            itr = m_sites.erase(itr);
        } else {
            ++itr;
        }
    }
    m_siteCooldown.erase(systemID);
}

void IncursionMgr::EndIncursion(uint32 incursionID)
{
    std::map<uint32, IncData>::iterator iItr = m_incursions.find(incursionID);
    if (iItr == m_incursions.end())
        return;

    std::map<uint32, IncLiveSite>::iterator itr = m_sites.begin();
    while (itr != m_sites.end()) {
        if (itr->second.incursionID != incursionID) {
            ++itr;
            continue;
        }
        SystemManager* pSys(nullptr);
        if (sEntityList.IsSystemLoaded(itr->second.systemID))
            pSys = sEntityList.FindOrBootSystem(itr->second.systemID);
        RemoveSite(itr->second, pSys);
        itr = m_sites.erase(itr);
    }
    for (auto cur : iItr->second.systems)
        m_siteCooldown.erase(cur.first);

    std::string msg = "The Sansha incursion in ";
    msg += IncursionDB::GetConstellationName(iItr->second.constellationID);
    msg += " has ended.";

    IncursionDB::DeleteIncursion(incursionID);
    m_incursions.erase(iItr);
    m_nextAutoStart = TimeNow() + sConfig.incursion.StartDelay * 60u;

    Broadcast(msg);
}

uint32 IncursionMgr::PickConstellation()
{
    DBQueryResult res;
    DBResultRow row;
    IncursionDB::GetCandidateConstellations(res, sConfig.incursion.MinSecurity, 4);
    std::vector<uint32> list;
    while (res.GetRow(row))
        if (FindByConstellation(row.GetUInt(0)) == nullptr)
            list.push_back(row.GetUInt(0));
    if (list.empty())
        return 0;
    return list[RandomIndex(list.size())];
}

bool IncursionMgr::AssignSystems(IncData& inc)
{
    DBQueryResult res;
    DBResultRow row;
    std::vector<uint32> systems;
    std::set<uint32> stations, border;
    IncursionDB::GetConstellationSystems(res, inc.constellationID);
    while (res.GetRow(row)) {
        uint32 systemID = row.GetUInt(0);
        systems.push_back(systemID);
        if (row.GetUInt(2) > 0)
            stations.insert(systemID);
        if (row.GetUInt(3) > 0)
            border.insert(systemID);
    }
    if (systems.empty())
        return false;

    std::map<uint32, std::vector<uint32>> links;
    IncursionDB::GetConstellationJumps(res, inc.constellationID);
    while (res.GetRow(row))
        links[row.GetUInt(0)].push_back(row.GetUInt(1));

    // staging: a station system, preferably on the constellation's edge
    std::vector<uint32> pool;
    for (auto cur : systems)
        if (stations.count(cur) and border.count(cur))
            pool.push_back(cur);
    if (pool.empty())
        for (auto cur : systems)
            if (stations.count(cur))
                pool.push_back(cur);
    if (pool.empty())
        pool = systems;
    inc.stagingSystemID = pool[RandomIndex(pool.size())];

    // jumps from staging
    std::map<uint32, uint16> dist;
    std::queue<uint32> queue;
    dist[inc.stagingSystemID] = 0;
    queue.push(inc.stagingSystemID);
    while (!queue.empty()) {
        uint32 cur = queue.front();
        queue.pop();
        for (auto next : links[cur])
            if (dist.find(next) == dist.end()) {
                dist[next] = dist[cur] + 1;
                queue.push(next);
            }
    }

    // nearest first, ties broken at random
    std::vector<std::pair<uint32, uint32>> order;
    for (auto cur : systems) {
        if (cur == inc.stagingSystemID)
            continue;
        uint32 jumps = (dist.find(cur) != dist.end() ? dist[cur] : 99);
        order.push_back(std::make_pair(jumps * 1000 + MakeRandomInt(0, 999), cur));
    }
    std::sort(order.begin(), order.end());

    // the staging system is a vanguard system too, plus the two closest.  headquarters is farthest away, assault the rest
    inc.systems.clear();
    inc.systems[inc.stagingSystemID] = Incursion::scenesType::vanguard;
    if (order.empty())
        return true;
    inc.systems[order.back().second] = Incursion::scenesType::headquarters;
    order.pop_back();
    uint8 vanguards(0);
    for (auto cur : order) {
        if (vanguards < 2) {
            inc.systems[cur.second] = Incursion::scenesType::vanguard;
            ++vanguards;
        } else {
            inc.systems[cur.second] = Incursion::scenesType::assault;
        }
    }
    return true;
}

std::string IncursionMgr::Start(uint32 constellationID/*0*/)
{
    if (!m_initialized)
        return "Incursions are disabled on this server.";
    if (constellationID == 0)
        constellationID = PickConstellation();
    if (constellationID == 0)
        return "No constellation is available for an incursion.";
    if (FindByConstellation(constellationID) != nullptr)
        return "There is already an incursion in " + IncursionDB::GetConstellationName(constellationID) + ".";

    uint32 now = TimeNow();
    IncData inc = IncData();
        inc.constellationID = constellationID;
        inc.state = Incursion::State::Established;
        inc.influence = 1.0;
        inc.startTime = now;
        inc.stateTime = now;
        inc.lastProgress = now;
    if (!AssignSystems(inc))
        return "Constellation " + std::to_string(constellationID) + " has no solar systems.";

    inc.incursionID = IncursionDB::CreateIncursion(constellationID, inc.stagingSystemID, now);
    if (inc.incursionID == 0)
        return "Could not save the new incursion to the database.";
    for (auto cur : inc.systems)
        IncursionDB::AddIncursionSystem(inc.incursionID, cur.first, cur.second);
    m_incursions[inc.incursionID] = inc;

    std::string msg = "Sansha's Nation has launched an incursion into ";
    msg += IncursionDB::GetConstellationName(constellationID);
    msg += ". Staging system: ";
    msg += sDataMgr.GetSystemName(inc.stagingSystemID);
    msg += ".";
    Broadcast(msg);
    _log(COSMIC_MGR__MESSAGE, "IncursionMgr::Start() - %s", msg.c_str());
    return msg;
}

std::string IncursionMgr::End(uint32 constellationID/*0*/)
{
    if (!m_initialized)
        return "Incursions are disabled on this server.";
    if (m_incursions.empty())
        return "There are no active incursions.";

    std::vector<uint32> list;
    for (auto& cur : m_incursions)
        if ((constellationID == 0) or (cur.second.constellationID == constellationID))
            list.push_back(cur.first);
    if (list.empty())
        return "There is no incursion in " + IncursionDB::GetConstellationName(constellationID) + ".";
    for (auto cur : list)
        EndIncursion(cur);
    return "Ended " + std::to_string(list.size()) + " incursion(s).";
}

std::string IncursionMgr::SetInfluence(uint32 constellationID, float percent)
{
    if (!m_initialized)
        return "Incursions are disabled on this server.";
    IncData* inc(nullptr);
    if (constellationID != 0) {
        inc = FindByConstellation(constellationID);
    } else if (!m_incursions.empty()) {
        inc = &m_incursions.begin()->second;
    }
    if (inc == nullptr)
        return "No matching incursion.";
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    inc->influence = percent / 100.0;
    inc->lastProgress = TimeNow();
    Save(*inc);
    char buf[160];
    snprintf(buf, sizeof(buf), "Sansha influence in %s set to %.0f%%.", IncursionDB::GetConstellationName(inc->constellationID).c_str(), percent);
    return buf;
}

std::string IncursionMgr::Info()
{
    if (!m_initialized)
        return "Incursions are disabled on this server.";
    uint32 now = TimeNow();
    if (m_incursions.empty()) {
        std::string msg = "There are no active incursions.";
        if (sConfig.incursion.AutoStart)
            msg += " The next one starts in about " + std::to_string(m_nextAutoStart > now ? (m_nextAutoStart - now) / 60 + 1 : 1) + " minutes.";
        return msg;
    }

    std::string msg;
    char buf[256];
    for (auto& cur : m_incursions) {
        IncData& inc = cur.second;
        snprintf(buf, sizeof(buf), "%s (%u): %s, Sansha influence %.0f%%, %u hours old.<br>",
                 IncursionDB::GetConstellationName(inc.constellationID).c_str(), inc.constellationID,
                 StateName(inc.state), inc.influence * 100, (now - inc.startTime) / 3600);
        msg += buf;
        msg += "Staging: ";
        msg += sDataMgr.GetSystemName(inc.stagingSystemID);
        msg += "<br>";
        for (auto sys : inc.systems) {
            uint16 sites(0);
            for (auto& site : m_sites)
                if (site.second.systemID == sys.first)
                    ++sites;
            snprintf(buf, sizeof(buf), "  %s: %s (%u live sites%s)<br>", SceneName(sys.second), sDataMgr.GetSystemName(sys.first),
                     sites, sEntityList.IsSystemLoaded(sys.first) ? "" : ", system not loaded");
            msg += buf;
        }
    }
    return msg;
}

void IncursionMgr::Broadcast(const std::string& msg)
{
    std::vector<Client*> list;
    sEntityList.GetClients(list);
    for (auto cur : list)
        if (cur != nullptr)
            cur->SendNotifyMsg("%s", msg.c_str());
}

