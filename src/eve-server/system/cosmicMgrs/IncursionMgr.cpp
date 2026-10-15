 /**
  * @name IncursionMgr.cpp
  *     Sansha incursion management for EVEmu
  *
  * @date:          12 October 2026
  *
  *  see IncursionMgr.h for what this does and doesnt cover yet.  the sites themselves are in IncursionSites.cpp
  *  ship stats, rewards and site mechanics follow the EVE University wiki; wave sizes and layouts are our picks where
  *  the wiki is silent (incSite* tables and the incursions migrations)
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
#include "system/cosmicMgrs/IncursionDB.h"
#include "system/cosmicMgrs/IncursionMgr.h"

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

static const char* SceneName(uint32 sceneType)
{
    switch (sceneType) {
        case Incursion::scenesType::headquarters:   return "Headquarters";
        case Incursion::scenesType::assault:        return "Assault";
        case Incursion::scenesType::vanguard:       return "Vanguard";
        case Incursion::scenesType::staging:        return "Staging";
        case Incursion::scenesType::boss:           return "Mothership";
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
    m_index.clear();
}

void IncursionMgr::Initialize()
{
    if (!sConfig.incursion.Enabled) {
        sLog.Warning("Incursion Manager", "Incursions Disabled.");
        return;
    }

    LoadSiteTypes();
    if (m_siteTypes.empty()) {
        sLog.Error("Incursion Manager", "No incursion site data found (incSiteTypes).  Have the incursions database updates been applied?  Incursions Disabled.");
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
            type.secBand = row.GetUInt(7);
            type.rooms = 0;
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
        IncSiteType& type = itr->second;
        IncSiteObject obj = IncSiteObject();
            obj.room = row.GetUInt(1);
            obj.wave = row.GetUInt(2);
            obj.role = row.GetUInt(3);
            obj.typeID = row.GetUInt(4);
            obj.quantity = row.GetUInt(5);
            obj.param = row.GetInt(6);
            obj.name = row.GetText(7);
            obj.offset = GPoint(row.GetDouble(8), row.GetDouble(9), row.GetDouble(10));
        if (obj.room > type.rooms)
            type.rooms = obj.room;
        // ships in numbered waves.  the mothership's later waves are reinforcements; they and respawns don't count for the payout
        if ((obj.role <= Incursion::Role::Respawn) and (obj.wave >= 1) and (obj.wave < Incursion::Wave::Mining)) {
            if (obj.wave > type.waves[obj.room])
                type.waves[obj.room] = obj.wave;
            if ((obj.role != Incursion::Role::Respawn) and (obj.role != Incursion::Role::PickOne)
            and ((type.sceneType != Incursion::scenesType::boss) or (obj.wave == 1)))
                type.designNPCs += obj.quantity;
        }
        type.objects.push_back(obj);
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
            inc.hasBoss = (row.GetUInt(8) > 0);
            inc.hqRun = (row.GetUInt(9) > 0);
            inc.endPending = false;
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
    IncursionDB::SaveIncursion(inc.incursionID, inc.state, inc.influence, inc.stateTime, inc.lastProgress, inc.hasBoss, inc.hqRun);
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
    std::vector<std::pair<uint32, bool>> ended;
    for (auto& cur : m_incursions) {
        ProcessIncursion(cur.second, now, dt);
        if (cur.second.endPending) {
            ended.push_back(std::make_pair(cur.first, true));
        } else if ((cur.second.state == Incursion::State::Withdrawing)
               and (now - cur.second.stateTime >= sConfig.incursion.WithdrawTime * 60u)) {
            ended.push_back(std::make_pair(cur.first, false));
        }
    }
    for (auto cur : ended)
        EndIncursion(cur.first, cur.second);

    // the sites: rooms, waves, scripts, and completed sites waiting for their pilots to leave
    std::vector<uint32> done;
    for (auto& cur : m_sites) {
        IncLiveSite& site = cur.second;
        if (!sEntityList.IsSystemLoaded(site.systemID)) {
            // system went away without telling us
            done.push_back(cur.first);
            continue;
        }
        SystemManager* pSys = sEntityList.FindOrBootSystem(site.systemID);
        if (pSys == nullptr) {
            done.push_back(cur.first);
            continue;
        }
        if (ProcessSite(cur.first, site, pSys, now)) {
            RemoveSite(site, pSys);
            done.push_back(cur.first);
        }
    }
    for (auto cur : done) {
        for (std::map<uint32, uint32>::iterator itr = m_index.begin(); itr != m_index.end();)
            itr = (itr->second == cur ? m_index.erase(itr) : ++itr);
        m_sites.erase(cur);
    }

    // keep the incursion's systems stocked while they're loaded.  the mothership waits in the headquarters system
    for (auto& cur : m_incursions) {
        IncData& inc = cur.second;
        if (inc.endPending)
            continue;
        for (auto sys : inc.systems) {
            if (sys.second == Incursion::scenesType::staging)
                continue;
            TopUpSites(inc, sys.first, sys.second, now);
            if (inc.hasBoss and (sys.second == Incursion::scenesType::headquarters))
                TopUpSites(inc, sys.first, Incursion::scenesType::boss, now);
        }
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

    // established for up to MobilizeAfter hours, then mobilizing until Lifetime, then withdrawing (Process ends it)
    if ((inc.state == Incursion::State::Established)
    and (now - inc.startTime >= sConfig.incursion.MobilizeAfter * 3600u)) {
        inc.state = Incursion::State::Mobilizing;
        inc.stateTime = now;
        Save(inc);
        Broadcast("Sansha's Nation is mobilizing in " + IncursionDB::GetConstellationName(inc.constellationID) + ".");
    }
    if (now - inc.startTime >= sConfig.incursion.Lifetime * 3600u) {
        inc.state = Incursion::State::Withdrawing;
        inc.stateTime = now;
        Save(inc);
        Broadcast("Sansha's Nation is withdrawing from " + IncursionDB::GetConstellationName(inc.constellationID) + ".");
        return;
    }

    // influence slowly comes back when nobody is clearing sites, until the Nation is beaten
    if (!inc.hqRun and (inc.influence < 1.0) and (dt > 0) and (now - inc.lastProgress >= sConfig.incursion.RegenDelay * 60u)) {
        inc.influence += sConfig.incursion.RegenRate / 100.0 * dt / 3600.0;
        if (inc.influence > 1.0)
            inc.influence = 1.0;
    }

    // a headquarters site run at zero influence brings out the mothership, once the incursion is old enough
    if (inc.hqRun and !inc.hasBoss) {
        float sec(1.0f);
        for (auto cur : inc.systems)
            if (cur.second == Incursion::scenesType::headquarters) {
                SystemData data = SystemData();
                if (sDataMgr.GetSystemData(cur.first, data))
                    sec = data.securityRating;
            }
        uint32 delay(0);
        if (sec >= 0.45f) {
            delay = sConfig.incursion.MothershipDelayHigh * 3600u;
        } else if (sec > 0.0f) {
            delay = sConfig.incursion.MothershipDelayLow * 3600u;
        }
        if (now - inc.startTime >= delay)
            SpawnBoss(inc, now);
    }
}

void IncursionMgr::SpawnBoss(IncData& inc, uint32 now)
{
    inc.influence = 0;
    inc.hqRun = true;
    inc.hasBoss = true;
    inc.lastProgress = now;
    Save(inc);

    std::string hq = "their headquarters";
    for (auto cur : inc.systems)
        if (cur.second == Incursion::scenesType::headquarters) {
            hq = sDataMgr.GetSystemName(cur.first);
            m_siteCooldown.erase(cur.first);
        }
    std::string msg = "Sansha influence in " + IncursionDB::GetConstellationName(inc.constellationID)
                     + " has been broken. Their mothership has been sighted in " + hq + ".";
    Broadcast(msg);
    _log(COSMIC_MGR__MESSAGE, "IncursionMgr - %s", msg.c_str());
}

void IncursionMgr::TopUpSites(IncData& inc, uint32 systemID, uint32 sceneType, uint32 now)
{
    if (!sEntityList.IsSystemLoaded(systemID))
        return;
    std::map<uint32, uint32>::iterator cd = m_siteCooldown.find(systemID);
    if ((cd != m_siteCooldown.end()) and (now < cd->second))
        return;

    uint8 limit(0);
    switch (sceneType) {
        case Incursion::scenesType::vanguard:       limit = sConfig.incursion.VanguardSites; break;
        case Incursion::scenesType::assault:        limit = sConfig.incursion.AssaultSites; break;
        case Incursion::scenesType::headquarters:   limit = sConfig.incursion.HQSites; break;
        case Incursion::scenesType::boss:           limit = 1; break;
    }

    // live sites of this kind here.  completed ones waiting for their pilots to leave don't count
    std::map<uint16, uint8> count;
    uint8 total(0);
    for (auto& cur : m_sites) {
        if ((cur.second.systemID != systemID) or cur.second.completed)
            continue;
        std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(cur.second.siteTypeID);
        if ((itr == m_siteTypes.end()) or (itr->second.sceneType != sceneType))
            continue;
        ++count[cur.second.siteTypeID];
        ++total;
    }
    if (total >= limit)
        return;

    SystemManager* pSys = sEntityList.FindOrBootSystem(systemID);
    if (pSys == nullptr)
        return;
    float sec = pSys->GetSystemSecurityRating();
    bool highSec = (sec >= 0.45f), nullSec = (sec <= 0.0f);

    // the site type of this kind with the fewest live copies here, random among ties
    std::vector<uint16> pick;
    uint8 best(255);
    for (auto& cur : m_siteTypes) {
        if (cur.second.sceneType != sceneType)
            continue;
        // 1 high-sec only, 2 low/null-sec only, 3 high or null-sec (Kundalini Manifest), 4 low-sec only (Uroborus)
        switch (cur.second.secBand) {
            case 1: if (!highSec) continue; break;
            case 2: if (highSec) continue; break;
            case 3: if (!highSec and !nullSec) continue; break;
            case 4: if (highSec or nullSec) continue; break;
        }
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

    IncLiveSite site = IncLiveSite();
        site.siteID = ++m_nextSiteID;
        site.incursionID = inc.incursionID;
        site.systemID = pSys->GetID();
        site.siteTypeID = siteTypeID;
        site.position = GetSitePoint(pSys);

    // the beacon grid is room 0.  each pocket is about a million km out in its own direction, with its own beacon
    for (uint8 i = 0; i <= type.rooms; ++i) {
        IncLiveRoom room = IncLiveRoom();
            room.center = site.position;
        if (i > 0) {
            GVector dir(MakeRandomFloat(-1.0, 1.0), MakeRandomFloat(-0.3, 0.3), MakeRandomFloat(-1.0, 1.0));
            if (dir.length() < 0.01)
                dir = GVector(1, 0, 0);
            dir.normalize();
            room.center += dir * (1.0e9 + i * 1.0e8);
        }
        site.rooms[i] = room;
    }

    IncLiveSite& live = m_sites.emplace(site.siteID, site).first->second;

    // the overview beacon.  global, so the whole system sees it and can warp to it
    SystemEntity* pBeacon = SpawnItem(live, pSys, 0, Incursion::Role::Structure, EVEDB::invTypes::CelestialBeacon, type.name, live.position, true);
    if (pBeacon == nullptr) {
        _log(COSMIC_MGR__ERROR, "IncursionMgr::SpawnSite() - unable to spawn beacon for %s in %s", type.name.c_str(), pSys->GetName());
        m_sites.erase(live.siteID);
        return false;
    }
    live.beaconID = pBeacon->GetID();
    live.rooms[0].beaconID = live.beaconID;

    for (uint8 i = 0; i <= type.rooms; ++i)
        SpawnRoomObjects(live, pSys, i);

    _log(COSMIC_MGR__MESSAGE, "IncursionMgr::SpawnSite() - spawned %s with %u pockets in %s(%u)", type.name.c_str(), type.rooms, pSys->GetName(), live.systemID);
    return true;
}

void IncursionMgr::CompleteSite(IncLiveSite& site, SystemManager* pSys, uint8 room)
{
    if (site.completed)
        return;
    site.completed = true;
    site.completedAt = TimeNow();
    site.emptySince = 0;

    // the overview beacon goes; the gates and pockets stay until everyone has left
    RemoveItem(site, pSys, site.beaconID);
    site.rooms[0].beaconID = 0;

    std::map<uint16, IncSiteType>::iterator itr = m_siteTypes.find(site.siteTypeID);
    if (itr == m_siteTypes.end())
        return;
    IncSiteType& type = itr->second;
    uint32 now = TimeNow();

    std::string place = IncursionDB::GetConstellationName(pSys->GetConstellationID());
    double influence(0);
    IncData* pInc(nullptr);
    std::map<uint32, IncData>::iterator iItr = m_incursions.find(site.incursionID);
    if (iItr != m_incursions.end()) {
        pInc = &iItr->second;
        pInc->influence -= type.influence / 100.0;
        if (pInc->influence < 0)
            pInc->influence = 0;
        pInc->lastProgress = now;
        influence = pInc->influence;
        if (type.sceneType == Incursion::scenesType::boss) {
            pInc->endPending = true;
            Broadcast("The Sansha mothership " + type.name + " has been destroyed in " + sDataMgr.GetSystemName(site.systemID) + ".");
        } else if ((type.sceneType == Incursion::scenesType::headquarters) and (pInc->influence <= 0)) {
            pInc->hqRun = true;
        }
        Save(*pInc);
    }

    // pay the pilots on grid.  the site's reward pool is what a full fleet would get for the ships that actually
    //  spawned, split between the pilots here, and nobody gets more than a full-fleet pilot would
    std::vector<Client*> pilots;
    PilotsInRoom(site, pSys, room, &pilots);
    if (!pilots.empty()) {
        double share = (double)type.designFleet * site.spawned / (type.designNPCs > 0 ? type.designNPCs : 1) / pilots.size();
        if (share > 1.0)
            share = 1.0;
        // low and null-sec pay 10/7 of the high-sec reward (wiki: 15M vs 10.395M for vanguards, and so on)
        if (pSys->GetSystemSecurityRating() < 0.45f)
            share *= 10.0 / 7.0;
        double isk = type.iskReward * share;
        uint32 lp = (uint32)(type.lpReward * share + 0.5);
        bool pool = (sConfig.incursion.DelayedLP and (pInc != nullptr));
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
            if (lp > 0) {
                if (pool) {
                    IncursionDB::AddPoolLP(site.incursionID, charID, lp);
                } else {
                    LPService::AddLP(charID, corpCONCORD, lp);
                }
            }
            if (type.sceneType == Incursion::scenesType::boss) {
                pClient->SendNotifyMsg("%s destroyed. CONCORD has paid you %.0f ISK.", type.name.c_str(), isk);
            } else if (pool) {
                pClient->SendNotifyMsg("%s completed. CONCORD has paid you %.0f ISK, and %u loyalty points go into this incursion's pool (paid when the mothership is destroyed). Sansha influence in %s is now %.0f%%.", \
                        type.name.c_str(), isk, lp, place.c_str(), influence * 100);
            } else {
                pClient->SendNotifyMsg("%s completed. CONCORD has paid you %.0f ISK and %u loyalty points. Sansha influence in %s is now %.0f%%.", \
                        type.name.c_str(), isk, lp, place.c_str(), influence * 100);
            }
        }
    }

    _log(COSMIC_MGR__MESSAGE, "IncursionMgr::CompleteSite() - %s completed in %s with %u pilots on grid.  Influence now %.1f%%", \
            type.name.c_str(), pSys->GetName(), pilots.size(), influence * 100);

    m_siteCooldown[site.systemID] = now + sConfig.incursion.SiteRespawn;
}

void IncursionMgr::RemoveSite(IncLiveSite& site, SystemManager* pSys)
{
    if (pSys == nullptr)
        return;

    // ships left over.  the NPC object stays allocated, as with killed NPCs, since missiles in flight may still point at it
    std::vector<uint32> ships;
    for (auto& cur : site.rooms) {
        ships.insert(ships.end(), cur.second.npcs.begin(), cur.second.npcs.end());
        ships.insert(ships.end(), cur.second.others.begin(), cur.second.others.end());
    }
    for (auto& cur : site.respawns)
        ships.insert(ships.end(), cur.npcs.begin(), cur.npcs.end());
    for (auto& cur : site.leaving)
        ships.push_back(cur.first);
    std::sort(ships.begin(), ships.end());
    ships.erase(std::unique(ships.begin(), ships.end()), ships.end());
    for (auto id : ships) {
        NPC* pNPC = pSys->GetNPCSE(id);
        if (pNPC == nullptr)
            continue;
        if (pNPC->TargetMgr() != nullptr)
            pNPC->TargetMgr()->ClearFromTargets();
        pSys->RemoveNPC(pNPC);
    }
    for (auto& cur : site.rooms) {
        cur.second.npcs.clear();
        cur.second.others.clear();
    }
    site.respawns.clear();
    site.leaving.clear();

    // the bubbles stop being Sansha ground unless another site shares them
    for (auto& cur : site.rooms) {
        SystemEntity* pSE = pSys->GetSE(cur.second.beaconID);
        if ((pSE == nullptr) or (pSE->SysBubble() == nullptr))
            continue;
        SystemBubble* pBubble = pSE->SysBubble();
        bool shared(false);
        for (auto& other : m_sites) {
            if ((&other.second == &site) or (other.second.systemID != site.systemID))
                continue;
            for (auto& oRoom : other.second.rooms) {
                SystemEntity* pOther = pSys->GetSE(oRoom.second.beaconID);
                if ((pOther != nullptr) and (pOther->SysBubble() == pBubble))
                    shared = true;
            }
        }
        if (!shared)
            pBubble->SetIncursion(false);
    }

    std::vector<uint32> items;
    for (auto& cur : site.items)
        items.push_back(cur.itemID);
    for (auto id : items)
        RemoveItem(site, pSys, id);
    site.items.clear();
    site.gates.clear();
}

void IncursionMgr::SystemUnloading(uint32 systemID)
{
    // the system deletes the entities itself
    std::map<uint32, IncLiveSite>::iterator itr = m_sites.begin();
    while (itr != m_sites.end()) {
        if (itr->second.systemID == systemID) {
            uint32 siteID = itr->first;
            for (std::map<uint32, uint32>::iterator idx = m_index.begin(); idx != m_index.end();)
                idx = (idx->second == siteID ? m_index.erase(idx) : ++idx);
            itr = m_sites.erase(itr);
        } else {
            ++itr;
        }
    }
    m_siteCooldown.erase(systemID);
}

void IncursionMgr::PayPool(IncData& inc)
{
    DBQueryResult res;
    DBResultRow row;
    IncursionDB::GetPool(res, inc.incursionID);
    uint32 pilots(0), total(0);
    while (res.GetRow(row)) {
        uint32 charID = row.GetUInt(0);
        uint32 lp = row.GetUInt(1);
        if (lp == 0)
            continue;
        LPService::AddLP(charID, corpCONCORD, lp);
        ++pilots;
        total += lp;
        Client* pClient = sEntityList.FindClientByCharID(charID);
        if (pClient != nullptr)
            pClient->SendNotifyMsg("The Sansha incursion in %s is over. CONCORD has paid you %u loyalty points for the sites you ran.", \
                    IncursionDB::GetConstellationName(inc.constellationID).c_str(), lp);
    }
    _log(COSMIC_MGR__MESSAGE, "IncursionMgr::PayPool() - incursion %u paid %u LP to %u pilots.", inc.incursionID, total, pilots);
}

void IncursionMgr::EndIncursion(uint32 incursionID, bool killed/*false*/)
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
        if (sEntityList.IsSystemLoaded(itr->second.systemID))
            RemoveSite(itr->second, sEntityList.FindOrBootSystem(itr->second.systemID));
        uint32 siteID = itr->first;
        for (std::map<uint32, uint32>::iterator idx = m_index.begin(); idx != m_index.end();)
            idx = (idx->second == siteID ? m_index.erase(idx) : ++idx);
        itr = m_sites.erase(itr);
    }
    for (auto cur : iItr->second.systems)
        m_siteCooldown.erase(cur.first);

    // the LP pool is paid when the mothership dies, and lost otherwise
    if (killed and sConfig.incursion.DelayedLP)
        PayPool(iItr->second);

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
    // the wiki's constellations have six systems or more.  fall back to four on small maps
    DBQueryResult res;
    DBResultRow row;
    std::vector<uint32> list;
    for (uint8 minSystems : { 6, 4 }) {
        IncursionDB::GetCandidateConstellations(res, sConfig.incursion.MinSecurity, minSystems);
        while (res.GetRow(row))
            if (FindByConstellation(row.GetUInt(0)) == nullptr)
                list.push_back(row.GetUInt(0));
        if (!list.empty())
            break;
    }
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

    // wiki layout: one staging, one headquarters (as far away as possible), and of the rest R:
    //  floor(R/2) + 1 vanguards next to staging and ceil(R/2) - 1 assaults between them and headquarters
    inc.systems.clear();
    inc.systems[inc.stagingSystemID] = Incursion::scenesType::staging;
    if (order.empty())
        return true;
    inc.systems[order.back().second] = Incursion::scenesType::headquarters;
    order.pop_back();
    size_t rest = order.size();
    size_t vanguards = rest / 2 + 1;
    if (vanguards > rest)
        vanguards = rest;
    // at least one assault system when there's room for one
    if ((vanguards == rest) and (rest > 1))
        --vanguards;
    for (size_t i = 0; i < order.size(); ++i)
        inc.systems[order[i].second] = (i < vanguards ? Incursion::scenesType::vanguard : Incursion::scenesType::assault);
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
        inc.hasBoss = false;
        inc.hqRun = false;
        inc.endPending = false;
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

std::string IncursionMgr::Mothership(uint32 constellationID)
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
    if (inc->hasBoss)
        return "The mothership is already out in " + IncursionDB::GetConstellationName(inc->constellationID) + ".";
    // skips the headquarters run and the minimum delay
    SpawnBoss(*inc, TimeNow());
    for (auto cur : inc->systems)
        if ((cur.second == Incursion::scenesType::headquarters) and !sEntityList.IsSystemLoaded(cur.first))
            return std::string("Influence broken. The mothership appears when someone enters ") + sDataMgr.GetSystemName(cur.first) + ".";
    return "Influence broken. The mothership is spawning in the headquarters system.";
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
        if (inc.hasBoss) {
            msg += "Mothership out.<br>";
        } else if (inc.hqRun) {
            msg += "Influence broken; the mothership is waiting out the minimum delay.<br>";
        }
        for (auto sys : inc.systems) {
            uint16 sites(0), done(0);
            for (auto& site : m_sites)
                if (site.second.systemID == sys.first)
                    ++(site.second.completed ? done : sites);
            snprintf(buf, sizeof(buf), "  %s: %s (%u live sites, %u completed%s)<br>", SceneName(sys.second), sDataMgr.GetSystemName(sys.first),
                     sites, done, sEntityList.IsSystemLoaded(sys.first) ? "" : ", system not loaded");
            msg += buf;
        }
    }
    return msg;
}

/* the Journal's Incursions tab (map.GetIncursionGlobalReport).  one util.KeyVal per incursion, as TQ sent them:
 *   graceTime float, decayRate float, influence float, lastUpdated FILETIME, state int, hasBoss int,
 *   stagingSolarSystemID, rewardGroupID, taleID
 *  the client works out the current value as influence - decayRate * (minutes since lastUpdated - graceTime), so
 *  influence here is the pilots' progress (1 - Sansha influence) and decayRate its loss per minute once the grace
 *  time is over.  state is the client's numbering, the reverse of ours (see ClientState()).  rewardGroupID is the
 *  incursion; rewardMgr answers for it
 */
PyRep* IncursionMgr::GetGlobalReport()
{
    PyList* list = new PyList();
    if (!m_initialized)
        return list;
    uint32 now = TimeNow();
    int64 fileNow = Win32TimeNow();
    for (auto& cur : m_incursions) {
        IncData& inc = cur.second;
        double grace(0), decay(0);
        if (!inc.hqRun) {
            uint32 since = now - inc.lastProgress;
            uint32 delay = sConfig.incursion.RegenDelay * 60u;
            grace = (since < delay ? (delay - since) / 60.0 : 0.0);
            decay = sConfig.incursion.RegenRate / 100.0 / 60.0;
        }
        PyDict* dict = new PyDict();
            dict->SetItemString("graceTime", new PyFloat(grace));
            dict->SetItemString("decayRate", new PyFloat(decay));
            dict->SetItemString("influence", new PyFloat(1.0 - inc.influence));
            dict->SetItemString("lastUpdated", new PyLong(fileNow));
            dict->SetItemString("state", new PyInt(Incursion::State::ClientState(inc.state)));
            dict->SetItemString("hasBoss", new PyInt(inc.hasBoss ? 1 : 0));
            dict->SetItemString("stagingSolarSystemID", new PyInt(inc.stagingSystemID));
            dict->SetItemString("rewardGroupID", new PyInt(inc.incursionID));
            dict->SetItemString("taleID", new PyInt(inc.incursionID));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
    return list;
}

// map.GetSystemsInIncursions(): (solarSystemID, sceneType) for every system of every incursion
PyRep* IncursionMgr::GetSystems()
{
    PyList* list = new PyList();
    if (!m_initialized)
        return list;
    for (auto& cur : m_incursions)
        for (auto sys : cur.second.systems) {
            PyTuple* tuple = new PyTuple(2);
                tuple->SetItem(0, new PyInt(sys.first));
                tuple->SetItem(1, new PyInt(sys.second));
            list->AddItem(tuple);
        }
    return list;
}

// rewardMgr.GetDelayedRewardsByGroupIDs(): {rewardGroupID: [util.KeyVal(rewardQuantity = LP in the pool)]}
PyRep* IncursionMgr::GetDelayedRewards(uint32 charID, PyRep* groupIDs)
{
    PyDict* dict = new PyDict();
    std::vector<PyRep*> ids;
    if (groupIDs == nullptr)
        return dict;
    if (groupIDs->IsTuple()) {
        for (PyTuple::const_iterator itr = groupIDs->AsTuple()->begin(); itr != groupIDs->AsTuple()->end(); ++itr)
            ids.push_back(*itr);
    } else if (groupIDs->IsList()) {
        for (PyList::const_iterator itr = groupIDs->AsList()->begin(); itr != groupIDs->AsList()->end(); ++itr)
            ids.push_back(*itr);
    } else if (groupIDs->IsInt()) {
        ids.push_back(groupIDs);
    }
    for (auto cur : ids) {
        if ((cur == nullptr) or !cur->IsInt())
            continue;
        uint32 incursionID = cur->AsInt()->value();
        uint32 lp = (sConfig.incursion.DelayedLP ? IncursionDB::GetPoolLP(incursionID, charID) : 0);
        PyDict* reward = new PyDict();
            reward->SetItemString("rewardQuantity", new PyInt(lp));
        PyList* rewards = new PyList();
            rewards->AddItem(new PyObject("util.KeyVal", reward));
        dict->SetItem(new PyInt(incursionID), rewards);
    }
    return dict;
}

void IncursionMgr::Broadcast(const std::string& msg)
{
    std::vector<Client*> list;
    sEntityList.GetClients(list);
    for (auto cur : list)
        if (cur != nullptr)
            cur->SendNotifyMsg("%s", msg.c_str());
}
