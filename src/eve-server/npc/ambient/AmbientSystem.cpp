 /**
  * @name AmbientSystem.cpp
  *   the ambient npcs of one solar system (doc/code_and_design_notes/ambient-npc-plan.md 3, 4, 6, 8, 11).  see AmbientSystem.h
  */

#include "eve-server.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "Client.h"
#include "EVEServerConfig.h"
#include "EntityList.h"
#include "StaticDataMgr.h"
#include "inventory/InventoryItem.h"
#include "inventory/ItemFactory.h"
#include "map/MapData.h"
#include "npc/LawEnforcement.h"
#include "npc/NPCAI.h"
#include "npc/ai/AggroGroup.h"
#include "npc/ai/AmbientBrain.h"
#include "npc/ambient/AmbientMgr.h"
#include "npc/ambient/AmbientNPC.h"
#include "npc/ambient/AmbientPilot.h"
#include "npc/ambient/AmbientSystem.h"
#include "ship/Ship.h"
#include "station/StationDataMgr.h"
#include "system/Asteroid.h"
#include "system/BubbleManager.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/cosmicMgrs/SpawnMgr.h"

using namespace Ambient;
using AmbientRules::Kind::Through;
using AmbientRules::Kind::ArriveDock;
using AmbientRules::Kind::UndockLeave;
using AmbientRules::Kind::StationHop;
using AmbientRules::Kind::LocalRun;
namespace NodeKind = AmbientRules::Node;

namespace {
    const uint16 typeIceField = 17774;
    // a point on a sphere layer [rIn, rOut] about c, uniform in direction, from our own rng (ambient.RngSeed repeats it)
    GPoint OnSphere(NpcRng& rng, const GPoint& c, double rIn, double rOut)
    {
        static constexpr double twoPi = 6.28318530717958647692;
        double z(rng.Range(-1.0, 1.0));
        double phi(rng.Range(0.0, twoPi));
        double s(std::sqrt(std::max(0.0, 1.0 - (z * z))));
        double r(rng.Range(rIn, rOut));
        return GPoint(c.x + (r * s * std::cos(phi)), c.y + (r * s * std::sin(phi)), c.z + (r * z));
    }
    double HourUTC()
    {
        return static_cast<double>(static_cast<int64>(std::time(nullptr)) % 86400) / 3600.0;
    }
    LegData MakeLeg(uint8 kind, uint32 targetID = 0, uint8 node = 0, int64 holdMs = 0)
    {
        LegData leg;
        leg.kind = kind;
        leg.targetID = targetID;
        leg.node = node;
        leg.holdMs = holdMs;
        return leg;
    }
}

AmbientSystem::AmbientSystem(SystemManager* pSystem)
: m_system(pSystem),
  m_active(false),
  m_never(false),
  m_profiled(false),
  m_windDownAt(0),
  m_nextRates(0),
  m_nextLinger(0),
  m_lambda(0.0),
  m_diurnal(1.0),
  m_events(0),
  m_materialised(0),
  m_capHits(0),
  m_suppressed(0),
  m_killed(0),
  m_usSum(0.0),
  m_ticks(0),
  m_statsAt(0),
  m_statsEvents(0),
  m_statsMade(0),
  m_statsCapHits(0),
  m_statsUs(0.0),
  m_statsMaxUs(0.0),
  m_statsTicks(0)
{
    m_kinds.fill(0.0);
}

AmbientSystem::~AmbientSystem()
{
    if (m_active)
        sAmbientMgr.Register(this, false);
}

void AmbientSystem::Note(const std::string& what)
{
    m_log.push_back(what);
    while (m_log.size() > 10)
        m_log.pop_front();
}

/* ------------------------------------------------------------------------------------------------------------------
 *  lifecycle (11)
 * ------------------------------------------------------------------------------------------------------------------ */
void AmbientSystem::Process()
{
    // the idle cost of a system without players in space:  this branch
    if (m_never or !sAmbientMgr.IsEnabled())
        return;
    int64 now(GetSteadyTime());
    if (!m_active) {
        if (m_system->PlayersInSpace() == 0)
            return;
        if (!m_profiled) {
            m_profiled = true;
            if (sAmbientMgr.GetProfile(m_system->GetID()).band == AmbientRules::Band::None) {
                m_never = true;     // null sec, wormholes:  nothing ambient, ever (1)
                return;
            }
        }
        Activate(now);
    }

    double start(GetTimeUSeconds());
    UpdateObserved(now);
    if (m_observed.empty()) {
        // the last player left space:  keep running for a while (a pilot re-undocking), then go idle
        if (m_windDownAt == 0) {
            m_windDownAt = now + (static_cast<int64>(sConfig.ambient.WindDownSeconds) * 1000);
        } else if (now >= m_windDownAt) {
            Deactivate(now, "no players in space");
            return;
        }
    } else {
        m_windDownAt = 0;
    }
    if (now >= m_nextRates)
        UpdateRates(now);
    ProcessRemovals(now);
    ProcessEvents(now);
    ProcessLaunches(now);
    ProcessVirtual(now);
    ProcessBelts(now);
    ProcessSquads(now);
    if (now >= m_nextLinger) {
        m_nextLinger = now + 5000;
        ProcessLinger(now);
    }
    double us(GetTimeUSeconds() - start);
    m_usSum += us;
    ++m_ticks;
    m_statsUs += us;
    m_statsMaxUs = std::max(m_statsMaxUs, us);
    ++m_statsTicks;
    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(::Profile::npc, us);
    // telemetry (phase 10):  one AMBIENT__STATS line per active system every npc.StatsInterval
    if ((sConfig.npc.StatsInterval > 0) and ((now - m_statsAt) >= (static_cast<int64>(sConfig.npc.StatsInterval) * 1000)))
        WriteStats(now);
}

void AmbientSystem::WriteStats(int64 now)
{
    if ((m_statsAt != 0) and is_log_enabled(AMBIENT__STATS)) {
        uint32 roles[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (auto& cur : m_npcs)
            if (cur.second->GetRole() < 8)
                ++roles[cur.second->GetRole()];
        _log(AMBIENT__STATS, "%s(%u): %us:  %u ships (traffic %u, miners %u, patrols %u, criminals %u), %u off grid, %u grids watched, "
             "%llu events, %llu materialised, %llu cap hits (global %u/%u), %.0f us/tick avg, %.0f max",
             m_system->GetName(), m_system->GetID(), static_cast<uint32>((now - m_statsAt) / 1000), (uint32)m_npcs.size(),
             roles[Role::Hauler] + roles[Role::Courier] + roles[Role::Freighter], roles[Role::Miner], roles[Role::Navy] + roles[Role::Concord],
             roles[Role::Criminal], (uint32)m_virtual.size(), (uint32)m_observed.size(),
             static_cast<unsigned long long>(m_events - m_statsEvents), static_cast<unsigned long long>(m_materialised - m_statsMade),
             static_cast<unsigned long long>(m_capHits - m_statsCapHits), sAmbientMgr.Materialised(), sConfig.ambient.GlobalCap,
             (m_statsTicks > 0 ? m_statsUs / m_statsTicks : 0.0), m_statsMaxUs);
    }
    m_statsAt = now;
    m_statsEvents = m_events;
    m_statsMade = m_materialised;
    m_statsCapHits = m_capHits;
    m_statsUs = 0.0;
    m_statsMaxUs = 0.0;
    m_statsTicks = 0;
}

void AmbientSystem::Activate(int64 now)
{
    m_profile = sAmbientMgr.GetProfile(m_system->GetID());
    m_rng.Seed(sAmbientMgr.Seed() ^ static_cast<uint64_t>(now), m_system->GetID());
    BuildNodes();
    m_kinds = AmbientRules::KindWeights(static_cast<uint32>(m_gates.size()), static_cast<uint32>(m_stations.size()), m_profile.H);
    UpdateRates(now);
    m_sched.Clear();
    if (sConfig.ambient.Traffic and (m_lambda > 0.0))
        m_sched.Push(now + AmbientRules::NextArrivalMs(m_rng, m_lambda), evTraffic);
    if (CriminalRatePerMin() > 0.0)
        m_sched.Push(now + AmbientRules::NextArrivalMs(m_rng, CriminalRatePerMin()), evCriminal);
    BuildSquads(now);
    m_active = true;
    m_windDownAt = 0;
    m_nextLinger = now + 5000;
    m_statsAt = 0;
    WriteStats(now);
    m_observed.clear();
    m_gridPlayers.clear();
    sAmbientMgr.Register(this, true);
    _log(AMBIENT__INFO, "%s(%u): ambient active.  A %.3f, %.2f traffic events/min, %u gates, %u stations, %u belts",
         m_system->GetName(), m_system->GetID(), m_profile.A, m_lambda, (uint32)m_gates.size(), (uint32)m_stations.size(),
         (uint32)m_belts.size());
    Note("active");
}

void AmbientSystem::Deactivate(int64 now, const char* why)
{
    // nobody sees it:  everything goes at once, silently
    std::vector<uint32> ids;
    for (auto& cur : m_npcs)
        ids.push_back(cur.first);
    for (auto id : ids)
        QueueRemoval(id, Removal::WindDown);
    ProcessRemovals(std::numeric_limits<int64>::max());
    m_virtual.clear();
    m_launches.clear();
    m_removals.clear();
    m_sched.Clear();
    m_observed.clear();
    m_gridPlayers.clear();
    m_unwatched.clear();
    m_beltStates.clear();
    m_squads.clear();
    m_active = false;
    m_windDownAt = 0;
    sAmbientMgr.Register(this, false);
    _log(AMBIENT__INFO, "%s(%u): ambient idle (%s).", m_system->GetName(), m_system->GetID(), why);
}

void AmbientSystem::Close()
{
    // the system deletes the entities right after this:  just forget them
    sAmbientMgr.AddMaterialised(-static_cast<int32>(m_npcs.size()));
    m_npcs.clear();
    m_virtual.clear();
    m_launches.clear();
    m_removals.clear();
    m_sched.Clear();
    m_observed.clear();
    m_gridPlayers.clear();
    m_unwatched.clear();
    m_beltStates.clear();
    m_squads.clear();
    if (m_active)
        sAmbientMgr.Register(this, false);
    m_active = false;
    m_windDownAt = 0;
}

void AmbientSystem::BuildNodes()
{
    m_gates.clear();
    m_stations.clear();
    m_belts.clear();
    m_planets.clear();
    std::map<uint32, SystemEntity*> entities(m_system->GetEntities());
    for (auto& cur : entities) {
        SystemEntity* pSE(cur.second);
        if (pSE == nullptr)
            continue;
        Node n;
        n.id = pSE->GetID();
        n.pos = pSE->GetPosition();
        n.radius = pSE->GetRadius();
        if (pSE->IsGateSE()) {
            n.kind = NodeKind::Gate;
            const std::vector<uint32>& dests(pSE->GetGateSE()->GetDestinations());
            if (!dests.empty()) {
                n.destGate = dests[0];
                StaticData sData;
                if (sDataMgr.GetStaticInfo(n.destGate, sData)) {
                    n.destSystem = sData.systemID;
                    const Ambient::Profile& dp(sAmbientMgr.GetProfile(n.destSystem));
                    n.destA = dp.A;
                    n.destNone = (dp.band == AmbientRules::Band::None);
                }
            }
            // a gate toward our nearest hub carries more traffic (6.5)
            bool toward(false);
            if ((n.destSystem != 0) and (m_profile.nearestHubSystem != 0) and (m_profile.nearestHubJumps > 0)) {
                int32 j(sMapData.GetJumpCount(n.destSystem, m_profile.nearestHubSystem, 8));
                toward = ((j >= 0) and (j < m_profile.nearestHubJumps));
            }
            n.weight = AmbientRules::GateWeight(n.destA, toward, n.destNone);
            n.hub = (m_profile.H >= sConfig.ambient.HubThreshold);
            m_gates.push_back(n);
        } else if (pSE->IsStationSE()) {
            n.kind = NodeKind::Station;
            StationData sData;
            if (stDataMgr.GetStationData(n.id, sData)) {
                n.dockPos = sData.dockPosition;
                n.dockDir = sData.dockOrientation;
                n.ownerID = sData.corporationID;
            } else {
                n.dockPos = n.pos;
                n.dockDir = GVector(1.0, 0.0, 0.0);
            }
            if (n.dockDir.isZero())
                n.dockDir = GVector(1.0, 0.0, 0.0);
            n.dockDir.normalize();
            n.h = sAmbientMgr.StationScore(n.id);
            n.hub = (n.h >= sConfig.ambient.HubThreshold);
            n.weight = AmbientRules::StationWeight(n.h);
            m_stations.push_back(n);
        } else if (pSE->IsBeltSE()) {
            n.kind = NodeKind::Belt;
            n.ice = (pSE->GetTypeID() == typeIceField);
            m_belts.push_back(n);
        } else if (pSE->IsPlanetSE() or pSE->IsMoonSE()) {
            n.kind = NodeKind::Planet;
            m_planets.push_back(n);
        }
    }
    m_gateW.clear();
    for (auto& cur : m_gates)
        m_gateW.push_back(cur.weight);
    m_stationW.clear();
    for (auto& cur : m_stations)
        m_stationW.push_back(cur.weight);
}

void AmbientSystem::UpdateRates(int64 now)
{
    // 6.4:  re-evaluated every minute (the day/night factor).  pending events keep their times
    m_nextRates = now + 60000;
    m_diurnal = AmbientRules::Diurnal(HourUTC(), sConfig.ambient.DiurnalAmplitude, sConfig.ambient.DiurnalPeakHour);
    double secMult(m_profile.band == AmbientRules::Band::Low ? sConfig.ambient.LowSecMultiplier : sConfig.ambient.HighSecMultiplier);
    m_lambda = AmbientRules::TrafficRate(m_profile.A, sConfig.ambient.BusynessExponent, sConfig.ambient.TrafficRateAtHub,
                                         sConfig.ambient.TrafficRateFloor, secMult, sConfig.ambient.DensityMultiplier, m_diurnal);
    // traffic restarts if it was off (no rate before)
    if (sConfig.ambient.Traffic and (m_lambda > 0.0) and !m_sched.HasKind(evTraffic))
        m_sched.Push(now + AmbientRules::NextArrivalMs(m_rng, m_lambda), evTraffic);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  observed grids (3, 11)
 * ------------------------------------------------------------------------------------------------------------------ */
void AmbientSystem::UpdateObserved(int64 now)
{
    // bubbles with a player in space, and the bubble a player is warping to (warm before they land, A10)
    std::vector<Grid> grids;
    auto add = [&](SystemBubble* pBubble, bool player) {
        if (pBubble == nullptr)
            return;
        for (auto& cur : grids) {
            if (cur.id == pBubble->GetID()) {
                if (player)
                    ++cur.players;
                return;
            }
        }
        Grid g;
        g.id = pBubble->GetID();
        g.center = pBubble->GetCenter();
        g.players = (player ? 1 : 0);
        grids.push_back(g);
    };
    std::vector<Client*> clients;
    m_system->GetClientList(clients);
    for (auto pClient : clients) {
        if ((pClient == nullptr) or !pClient->IsInSpace())
            continue;
        ShipSE* pShip(pClient->GetShipSE());
        if ((pShip == nullptr) or (pShip->SysBubble() == nullptr))
            continue;
        add(pShip->SysBubble(), true);
        DestinyManager* pDestiny(pShip->DestinyMgr());
        if ((pDestiny != nullptr) and (pDestiny->IsWarping() or pDestiny->IsAligningForWarp()))
            add(sBubbleMgr.FindBubble(m_system->GetID(), pDestiny->GetTargetPoint()), false);
    }
    m_observed.swap(grids);

    std::map<uint32, uint32> players;
    for (auto& cur : m_observed) {
        std::map<uint32, uint32>::iterator itr(m_gridPlayers.find(cur.id));
        if (itr == m_gridPlayers.end()) {
            OnGridObserved(cur, now, false);
        } else if (cur.players > itr->second) {
            OnGridObserved(cur, now, true);
        }
        players[cur.id] = cur.players;
    }
    m_gridPlayers.swap(players);
}

void AmbientSystem::OnGridObserved(const Grid& grid, int64 now, bool newPlayers)
{
    if (newPlayers) {
        // a pilot joined a grid we already show:  the miners' beams again, so the newcomer sees them (9.4)
        for (auto& cur : m_npcs) {
            AmbientNPC* pNPC(cur.second);
            if ((pNPC->SysBubble() != nullptr) and (pNPC->SysBubble()->GetID() == grid.id) and (pNPC->GetAmbientPilot() != nullptr))
                pNPC->GetAmbientPilot()->ResendBeams(now);
        }
        return;
    }
    _log(AMBIENT__TRACE, "%s(%u): grid %u observed.", m_system->GetName(), m_system->GetID(), grid.id);
    // warm start (6.5):  the grid gets its steady-state population at once
    for (auto& cur : m_stations)
        if (grid.center.distance(cur.pos) <= BUBBLE_RADIUS_METERS)
            WarmStartTraffic(cur, now);
    for (auto& cur : m_gates)
        if (grid.center.distance(cur.pos) <= BUBBLE_RADIUS_METERS)
            WarmStartTraffic(cur, now);
    OnBeltObserved(grid, now);
}

bool AmbientSystem::IsObservedBubble(SystemBubble* pBubble) const
{
    if (pBubble == nullptr)
        return false;
    uint32 id(pBubble->GetID());
    for (auto& cur : m_observed)
        if (cur.id == id)
            return true;
    return false;
}

bool AmbientSystem::IsObservedPoint(const GPoint& pos) const
{
    for (auto& cur : m_observed)
        if (cur.center.distance(pos) <= BUBBLE_RADIUS_METERS)
            return true;
    return false;
}

bool AmbientSystem::IsGateObserved(uint32 gateID) const
{
    const Node* pNode(FindNode(gateID));
    return ((pNode != nullptr) and m_active and IsObservedPoint(pNode->pos));
}

bool AmbientSystem::IsNodeObserved(uint32 nodeID) const
{
    const Node* pNode(FindNode(nodeID));
    return ((pNode != nullptr) and IsObservedPoint(pNode->kind == NodeKind::Station ? pNode->dockPos : pNode->pos));
}

/* ------------------------------------------------------------------------------------------------------------------
 *  nodes
 * ------------------------------------------------------------------------------------------------------------------ */
const AmbientSystem::Node* AmbientSystem::FindNode(uint32 id) const
{
    for (auto& cur : m_gates)
        if (cur.id == id)
            return &cur;
    for (auto& cur : m_stations)
        if (cur.id == id)
            return &cur;
    for (auto& cur : m_belts)
        if (cur.id == id)
            return &cur;
    for (auto& cur : m_planets)
        if (cur.id == id)
            return &cur;
    return nullptr;
}

const AmbientSystem::Node* AmbientSystem::NodeAt(const GPoint& pos, uint8 kind) const
{
    const std::vector<Node>* pList(nullptr);
    switch (kind) {
        case NodeKind::Gate:    pList = &m_gates;       break;
        case NodeKind::Station: pList = &m_stations;    break;
        case NodeKind::Belt:    pList = &m_belts;       break;
        default:                pList = &m_planets;     break;
    }
    const Node* pBest(nullptr);
    double best(BUBBLE_RADIUS_METERS);
    for (auto& cur : *pList) {
        double distance(cur.pos.distance(pos));
        if (distance <= best) {
            best = distance;
            pBest = &cur;
        }
    }
    return pBest;
}

bool AmbientSystem::NodePosition(uint32 nodeID, uint8 nodeKind, GPoint& pos, double& radius) const
{
    const Node* pNode((nodeKind == NodeKind::Entity) ? nullptr : FindNode(nodeID));
    if (pNode != nullptr) {
        // ships warp to a station's dock point (its undock is where they arrive and leave)
        pos = (pNode->kind == NodeKind::Station ? pNode->dockPos : pNode->pos);
        radius = pNode->radius;
        return true;
    }
    SystemEntity* pSE(m_system->GetSE(nodeID));
    if ((pSE == nullptr) or pSE->IsDead())
        return false;
    pos = pSE->GetPosition();
    radius = pSE->GetRadius();
    return true;
}

GPoint AmbientSystem::DockPoint(uint32 stationID) const
{
    const Node* pNode(FindNode(stationID));
    if (pNode != nullptr)
        return pNode->dockPos;
    SystemEntity* pSE(m_system->GetSE(stationID));
    return (pSE != nullptr ? pSE->GetPosition() : NULL_ORIGIN);
}

GVector AmbientSystem::UndockDirection(uint32 stationID) const
{
    const Node* pNode(FindNode(stationID));
    return (pNode != nullptr ? pNode->dockDir : GVector(1.0, 0.0, 0.0));
}

uint32 AmbientSystem::PickStation(NpcRng& rng, SystemBubble* pNotIn) const
{
    if (m_stations.empty())
        return 0;
    std::vector<double> w(m_stationW);
    bool other(false);
    if (pNotIn != nullptr) {
        for (size_t i = 0; i < m_stations.size(); ++i)
            if (pNotIn->GetCenter().distance(m_stations[i].pos) <= BUBBLE_RADIUS_METERS)
                w[i] = 0.0;
            else
                other = true;
    }
    size_t idx(AmbientRules::PickWeighted(rng, other ? w : m_stationW));
    return (idx == AmbientRules::npos ? 0 : m_stations[idx].id);
}

uint32 AmbientSystem::PickGate(NpcRng& rng, SystemBubble* pNotIn) const
{
    if (m_gates.empty())
        return 0;
    std::vector<double> w(m_gateW);
    bool other(false);
    if (pNotIn != nullptr) {
        for (size_t i = 0; i < m_gates.size(); ++i)
            if (pNotIn->GetCenter().distance(m_gates[i].pos) <= BUBBLE_RADIUS_METERS)
                w[i] = 0.0;
            else
                other = true;
    }
    size_t idx(AmbientRules::PickWeighted(rng, other ? w : m_gateW));
    return (idx == AmbientRules::npos ? 0 : m_gates[idx].id);
}

uint32 AmbientSystem::StationNear(const GPoint& pos, double range) const
{
    for (auto& cur : m_stations)
        if (cur.dockPos.distance(pos) <= range)
            return cur.id;
    return 0;
}

bool AmbientSystem::FleeNode(const GPoint& from, SystemBubble* pNot, NpcRng& rng, uint32& nodeID, uint8& nodeKind) const
{
    // one of the three nearest celestials off this grid (8, Flee)
    std::vector<std::pair<double, const Node*>> found;
    auto consider = [&](const std::vector<Node>& list) {
        for (auto& cur : list) {
            double distance(cur.pos.distance(from));
            if (distance <= (BUBBLE_RADIUS_METERS + cur.radius))
                continue;
            if ((pNot != nullptr) and (pNot->GetCenter().distance(cur.pos) <= BUBBLE_RADIUS_METERS))
                continue;
            found.push_back(std::make_pair(distance, &cur));
        }
    };
    consider(m_planets);
    consider(m_stations);
    consider(m_gates);
    if (found.empty())
        return false;
    std::sort(found.begin(), found.end(), [](const std::pair<double, const Node*>& a, const std::pair<double, const Node*>& b) {
        return (a.first < b.first);
    });
    size_t n(std::min<size_t>(3, found.size()));
    size_t idx(std::min(static_cast<size_t>(rng.Uniform() * static_cast<double>(n)), n - 1));
    nodeID = found[idx].second->id;
    nodeKind = found[idx].second->kind;
    return true;
}

void AmbientSystem::BeltRocks(SystemBubble* pBubble, std::vector<AsteroidSE*>& into) const
{
    if ((pBubble == nullptr) or !pBubble->IsBelt())
        return;
    uint32 beltID(sBubbleMgr.GetBeltID(pBubble->GetID()));
    if (beltID != 0)
        m_system->GetBeltMgr()->GetList(beltID, into);
}

bool AmbientSystem::RockTaken(SystemEntity* pRock, AmbientNPC* pExcept) const
{
    if (pRock == nullptr)
        return true;
    for (auto& cur : m_npcs) {
        AmbientNPC* pNPC(cur.second);
        if ((pNPC == pExcept) or (pNPC->GetRole() != Role::Miner) or (pNPC->GetAmbientPilot() == nullptr))
            continue;
        if (pNPC->GetAmbientPilot()->GetRockID() == pRock->GetID())
            return true;
        if (pNPC->GetPosition().distance(pRock->GetPosition()) < (5000.0 + pRock->GetRadius()))
            return true;
    }
    return false;
}

bool AmbientSystem::ConcordOnGrid(SystemBubble* pBubble) const
{
    if (pBubble == nullptr)
        return false;
    bool found(false);
    pBubble->ForEachDynamic([&](SystemEntity* cur) {
        if (found or !cur->IsNPCSE() or cur->IsDead())
            return;
        if (cur->GetNPCSE()->IsConcord() or (cur->GetCorporationID() == corpCONCORD))
            found = true;
    });
    return found;
}

/* ------------------------------------------------------------------------------------------------------------------
 *  entities
 * ------------------------------------------------------------------------------------------------------------------ */
uint32 AmbientSystem::CountOnGrid(const GPoint& pos) const
{
    SystemBubble* pBubble(sBubbleMgr.FindBubble(m_system->GetID(), pos));
    uint32 count(0);
    for (auto& cur : m_npcs) {
        if (pBubble != nullptr) {
            if (cur.second->SysBubble() == pBubble)
                ++count;
        } else if (cur.second->GetPosition().distance(pos) <= BUBBLE_RADIUS_METERS) {
            ++count;
        }
    }
    return count;
}

bool AmbientSystem::IsHubGrid(const GPoint& pos) const
{
    for (auto& cur : m_stations)
        if (cur.hub and (cur.pos.distance(pos) <= BUBBLE_RADIUS_METERS))
            return true;
    if (m_profile.H >= sConfig.ambient.HubThreshold)
        for (auto& cur : m_gates)
            if (cur.pos.distance(pos) <= BUBBLE_RADIUS_METERS)
                return true;
    return false;
}

bool AmbientSystem::CapAllows(const GPoint& pos)
{
    uint32 gridCap(AmbientRules::GridCapFor(IsHubGrid(pos), sConfig.ambient.GridCap, sConfig.ambient.HubGridCap));
    if (AmbientRules::CapAllows(CountOnGrid(pos), gridCap, static_cast<uint32>(m_npcs.size()), sConfig.ambient.SystemCap,
                                sAmbientMgr.Materialised(), sConfig.ambient.GlobalCap))
        return true;
    ++m_capHits;
    sAmbientMgr.CountCapHit();
    return false;
}

AmbientNPC* AmbientSystem::Materialise(Traveller& t, uint8 start, size_t firstLeg, int64 now, const GPoint* placeAt/*nullptr*/,
                                       const std::shared_ptr<AggroGroup>* group/*nullptr*/)
{
    const Archetype* pArch(t.spec.arch);
    if (pArch == nullptr)
        return nullptr;
    const Node* pNode(FindNode(t.atNode));
    GPoint pos;
    if (placeAt != nullptr) {
        pos = *placeAt;
    } else if (pNode == nullptr) {
        return nullptr;
    } else {
        switch (start) {
            case Start::JumpIn:
                // where a jumping pilot appears (Client::StargateJump):  6.5 - 9.5 km off the gate
                pos = OnSphere(m_rng, pNode->pos, pNode->radius + 6500.0, pNode->radius + 9500.0);
                break;
            case Start::Undock:
                pos = pNode->dockPos;
                break;
            case Start::WarpIn:
            default:
                switch (pNode->kind) {
                    case NodeKind::Gate:    pos = OnSphere(m_rng, pNode->pos, pNode->radius + 2000.0, pNode->radius + 8000.0); break;
                    case NodeKind::Station: pos = OnSphere(m_rng, pNode->dockPos, 1000.0, 6000.0); break;
                    case NodeKind::Belt:    pos = OnSphere(m_rng, pNode->pos, 5000.0, 20000.0); break;
                    default:                pos = OnSphere(m_rng, pNode->pos, pNode->radius + 10000.0, pNode->radius + 40000.0); break;
                }
                break;
        }
    }

    Spec spec(t.spec);
    spec.fakeModules.clear();
    if (pArch->hull)
        for (uint8 i = 0; i < pArch->modules; ++i)
            spec.fakeModules.push_back(sItemFactory.GetNextTempID());
    // ships without a pilot show their type name (9.1):  their item keeps it
    ItemData idata(pArch->typeID, spec.ownerID, m_system->GetID(), flagNone, (pArch->hull ? "" : spec.name.c_str()), pos);
    // transient (D6):  entities get temp ids from SpawnItem, ship types must never go through SpawnShip/SpawnItem (saved)
    InventoryItemRef iRef(pArch->hull ? sItemFactory.SpawnTempItem(idata) : sItemFactory.SpawnItem(idata));
    if (iRef.get() == nullptr) {
        _log(AMBIENT__WARN, "%s(%u): unable to spawn %s (%u).", m_system->GetName(), m_system->GetID(), pArch->typeName.c_str(),
             pArch->typeID);
        return nullptr;
    }
    FactionData data = FactionData();
        data.allianceID = spec.factionID;   // the salvage faction of its wreck (as LawEnforcement does)
        data.factionID = spec.factionID;
        data.corporationID = spec.ownerID;
        data.ownerID = spec.ownerID;
    AmbientNPC* pNPC(new AmbientNPC(iRef, m_system->GetServiceMgr(), m_system, data, spec));
    if (!pNPC->Load()) {
        SafeDelete(pNPC);
        iRef->Delete();
        return nullptr;
    }
    AmbientPilot* pPilot(new AmbientPilot(pNPC, this, sAmbientMgr.Seed() ^ (static_cast<uint64_t>(spec.travellerID) << 16)));
    pPilot->SetLegs(t.legs, firstLeg);
    pNPC->AttachPilot(pPilot);
    // the brain and the pilot hooks, before the npc's first Process() (8.6, 4.3)
    uint8 mode(IsMilitary(spec.role) ? AmbientBrain::Military
              : (spec.role == Role::Criminal ? AmbientBrain::Criminal : AmbientBrain::Civilian));
    pNPC->GetAIMgr()->SetBrain(new AmbientBrain(mode, pPilot));
    pNPC->GetAIMgr()->SetAmbient(pPilot);
    if ((group != nullptr) and (group->get() != nullptr))
        pNPC->GetAIMgr()->SetGroup(*group);
    // a gate arrival is session cloaked before anyone gets its ball (9.2, A6)
    if (start == Start::JumpIn)
        pNPC->DestinyMgr()->SetSessionCloakSilently();
    pNPC->DestinyMgr()->SetPosition(pos, false);
    m_system->AddNPC(pNPC);
    m_npcs[pNPC->GetID()] = pNPC;
    sAmbientMgr.AddMaterialised(1);
    ++m_materialised;
    pPilot->Begin(start, t.atNode, t.atNodeKind, now);
    _log(AMBIENT__TRACE, "%s(%u): %s %s(%u) [%u] %s at node %u, leg %u/%u.", m_system->GetName(), m_system->GetID(),
         RoleName(spec.role), pNPC->GetName(), pNPC->GetID(), spec.travellerID,
         (start == Start::JumpIn ? "jumps in" : (start == Start::Undock ? "undocks" : (start == Start::WarpIn ? "warps in" : "placed"))),
         t.atNode, (uint32)firstLeg + 1, (uint32)t.legs.size());
    return pNPC;
}

void AmbientSystem::QueueRemoval(uint32 npcID, uint8 reason, int64 delayMs/*0*/)
{
    for (auto& cur : m_removals)
        if (cur.npcID == npcID)
            return;
    PendingRemoval r;
    r.npcID = npcID;
    r.reason = reason;
    r.at = GetSteadyTime() + delayMs;
    m_removals.push_back(r);
    std::map<uint32, AmbientNPC*>::iterator itr(m_npcs.find(npcID));
    if ((itr != m_npcs.end()) and (itr->second->GetAmbientPilot() != nullptr))
        itr->second->GetAmbientPilot()->SetRemoving();
}

void AmbientSystem::ProcessRemovals(int64 now)
{
    if (m_removals.empty())
        return;
    std::vector<PendingRemoval> due;
    for (auto itr = m_removals.begin(); itr != m_removals.end();) {
        if (itr->at <= now) {
            due.push_back(*itr);
            itr = m_removals.erase(itr);
        } else {
            ++itr;
        }
    }
    // the SpawnMgr fled-hauler pattern (4.2):  out of everyone's locks, then out of space.  the system frees it next tick
    for (auto& cur : due) {
        std::map<uint32, AmbientNPC*>::iterator itr(m_npcs.find(cur.npcID));
        if (itr == m_npcs.end())
            continue;
        AmbientNPC* pNPC(itr->second);
        if (pNPC->IsDead())
            continue;
        if (pNPC->GetAmbientPilot() != nullptr)
            pNPC->GetAmbientPilot()->OnRemoving();
        _log(AMBIENT__TRACE, "%s(%u): %s(%u) [%u] leaves space (%s).", m_system->GetName(), m_system->GetID(), pNPC->GetName(),
             pNPC->GetID(), pNPC->GetTravellerID(), RemovalName(cur.reason));
        if (pNPC->TargetMgr() != nullptr)
            pNPC->TargetMgr()->ClearFromTargets();
        m_system->RemoveNPC(pNPC);
        // (OnNPCRemoved took it off our list;  in case it didn't)
        if (m_npcs.erase(cur.npcID) > 0)
            sAmbientMgr.AddMaterialised(-1);
    }
}

void AmbientSystem::OnNPCRemoved(uint32 itemID, bool killed)
{
    std::map<uint32, AmbientNPC*>::iterator itr(m_npcs.find(itemID));
    if (itr == m_npcs.end())
        return;
    m_npcs.erase(itr);
    sAmbientMgr.AddMaterialised(-1);
    m_unwatched.erase(itemID);
    if (killed) {
        ++m_killed;
        Note("killed " + std::to_string(itemID));
        for (auto& s : m_squads)
            if (std::find(s.npcIDs.begin(), s.npcIDs.end(), itemID) != s.npcIDs.end())
                ++s.killed;
    }
}

void AmbientSystem::Dematerialise(AmbientNPC* pNPC, const Traveller& t)
{
    Traveller tr(t);
    tr.nextAt = GetSteadyTime();
    if (BeginVirtualLeg(tr, tr.nextAt))
        AddVirtual(tr);
}

void AmbientSystem::OnJumpOut(AmbientNPC* pNPC, uint32 gateID, const Traveller& t)
{
    const Node* pGate(FindNode(gateID));
    if ((pGate == nullptr) or (pGate->destSystem == 0))
        return;
    Traveller tr(t);
    tr.legs.clear();
    tr.leg = 0;
    int64 now(GetSteadyTime());
    sAmbientMgr.QueueTransit(tr, m_system->GetID(), pGate->destSystem, pGate->destGate,
                             now + AmbientRules::TransitDelayMs(m_rng, sConfig.ambient.JumpTransitMin, sConfig.ambient.JumpTransitMax));
}

void AmbientSystem::ProcessLinger(int64 now)
{
    // stationary ambients on a grid nobody watches go after ambient.GridLingerSeconds (11).  anything else after 10 minutes
    int64 linger(static_cast<int64>(sConfig.ambient.GridLingerSeconds) * 1000);
    for (auto& cur : m_npcs) {
        AmbientNPC* pNPC(cur.second);
        AmbientPilot* pPilot(pNPC->GetAmbientPilot());
        if (pNPC->IsDead() or (pPilot == nullptr) or pPilot->IsRemoving())
            continue;
        if (IsObservedBubble(pNPC->SysBubble())) {
            m_unwatched.erase(cur.first);
            continue;
        }
        std::map<uint32, int64>::iterator itr(m_unwatched.find(cur.first));
        if (itr == m_unwatched.end()) {
            m_unwatched[cur.first] = now;
            continue;
        }
        if (((now - itr->second) >= linger) and (pPilot->IsStationary() or ((now - itr->second) >= 600000)))
            QueueRemoval(cur.first, Removal::Linger);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  traffic (6.4, 6.5, 8.1)
 * ------------------------------------------------------------------------------------------------------------------ */
void AmbientSystem::ProcessEvents(int64 now)
{
    AmbientSchedule::Event e;
    while (m_sched.PopDue(now, e)) {
        switch (e.kind) {
            case evTraffic: {
                if (!sConfig.ambient.Traffic or !(m_lambda > 0.0))
                    break;
                TrafficEvent(now);
                m_sched.Push(now + AmbientRules::NextArrivalMs(m_rng, m_lambda), evTraffic);
            } break;
            case evCriminal: {
                double rate(CriminalRatePerMin());
                if (!(rate > 0.0))
                    break;
                CriminalEvent(now);
                m_sched.Push(now + AmbientRules::NextArrivalMs(m_rng, rate), evCriminal);
            } break;
            default:
                break;
        }
    }
}

void AmbientSystem::ProcessLaunches(int64 now)
{
    if (m_launches.empty())
        return;
    std::vector<PendingLaunch> due;
    for (auto itr = m_launches.begin(); itr != m_launches.end();) {
        if (itr->at <= now) {
            due.push_back(*itr);
            itr = m_launches.erase(itr);
        } else {
            ++itr;
        }
    }
    for (auto& cur : due)
        Launch(cur.t, cur.start, now, true);
}

std::string AmbientSystem::RaceName(uint8 race, uint8 role, const Archetype* pArch) const
{
    // 7.1:  "<Race> Hauler", "<Race> Courier", "<Race> Freighter", "<Race> Miner";  navy, CONCORD and criminals:  the type name
    std::string name(Ambient::RaceName(race));
    switch (role) {
        case Role::Hauler:      return name + " Hauler";
        case Role::Courier:     return name + " Courier";
        case Role::Freighter:   return name + " Freighter";
        case Role::Miner:       return name + " Miner";
        default:                return (pArch != nullptr ? pArch->typeName : name);
    }
}

uint32 AmbientSystem::PickOwner(uint8 race, uint32 stationID)
{
    // 7.1:  half the dock and undock traffic belongs to the station's owner, the rest to an npc corp of the race's faction
    if (stationID != 0) {
        const Node* pNode(FindNode(stationID));
        if ((pNode != nullptr) and (pNode->ownerID != 0) and m_rng.Chance(0.5)
        and (FactionRace(sDataMgr.GetCorpFaction(pNode->ownerID)) == race))
            return pNode->ownerID;
    }
    return sAmbientMgr.RandomCorp(RaceFaction(race), m_rng);
}

bool AmbientSystem::BuildTraffic(const AmbientRules::Itinerary& it, Traveller& t, uint8& start, int64 now)
{
    // the hull class (freighters only at hubs, navy transit only for the system's own empire) and race (6.5)
    AmbientRules::HullArray cw(AmbientRules::ClassWeights(m_profile.band, m_profile.H));
    if (m_profile.H < sConfig.ambient.HubThreshold)
        cw[AmbientRules::Hull::Freighter] = 0.0;
    if (m_profile.homeRace >= Race::Count)
        cw[AmbientRules::Hull::Navy] = 0.0;
    size_t cls(AmbientRules::PickWeighted(m_rng, cw.data(), cw.size()));
    if (cls == AmbientRules::npos)
        return false;
    uint8 race(AmbientRules::PickRace(m_rng, m_profile.homeRace, m_profile.H));
    const AmbientCatalog& catalog(sAmbientMgr.Catalog());
    const Archetype* pArch(nullptr);
    uint8 role(Role::Hauler);
    switch (cls) {
        case AmbientRules::Hull::Courier:
            role = Role::Courier;
            pArch = catalog.Pick(m_rng, Role::Courier, race);
            // A4:  the courier types fail validation, so couriers fly hauler hulls
            if (pArch == nullptr)
                pArch = catalog.Pick(m_rng, Role::Hauler, race);
            break;
        case AmbientRules::Hull::Freighter:
            role = Role::Freighter;
            pArch = catalog.Pick(m_rng, Role::Freighter, race);
            break;
        case AmbientRules::Hull::Navy:
            role = Role::Navy;
            race = m_profile.homeRace;
            // navy transit:  a frigate or a cruiser (80/20)
            pArch = catalog.Pick(m_rng, Role::Navy, race, (m_rng.Chance(0.8) ? 0 : 1));
            break;
        default:
            pArch = catalog.Pick(m_rng, Role::Hauler, race);
            break;
    }
    if (pArch == nullptr) {
        role = Role::Hauler;
        pArch = catalog.Pick(m_rng, Role::Hauler, race);
        if (pArch == nullptr)
            pArch = catalog.Pick(m_rng, Role::Hauler);
        if (pArch == nullptr)
            return false;
        race = pArch->race;
    }

    t = Traveller();
    t.spec.arch = pArch;
    t.spec.role = role;
    t.spec.race = race;
    t.spec.name = RaceName(race, role, pArch);
    t.spec.factionID = RaceFaction(race);
    t.spec.travellerID = sAmbientMgr.NextTravellerID();
    t.created = now;
    t.nextAt = now;

    // the visible legs of each kind (6.5)
    std::vector<LegData>& legs(t.legs);
    int64 arrivalPause(AmbientRules::UniformMs(m_rng, 2000.0, 20000.0));
    int64 undockRun(AmbientRules::UniformMs(m_rng, 6000.0, 15000.0));
    uint32 stationID(0);
    switch (it.kind) {
        case Through: {
            const Node& in(m_gates[it.entryGate]);
            const Node& out(m_gates[it.exitGate]);
            t.atNode = in.id;
            t.atNodeKind = NodeKind::Gate;
            start = Start::JumpIn;
            legs.push_back(MakeLeg(Leg::Hold, 0, 0, arrivalPause));
            legs.push_back(MakeLeg(Leg::WarpTo, out.id, NodeKind::Gate));
            legs.push_back(MakeLeg(Leg::ApproachGate, out.id));
            legs.push_back(MakeLeg(Leg::JumpOut, out.id));
        } break;
        case ArriveDock: {
            const Node& in(m_gates[it.entryGate]);
            const Node& st(m_stations[it.stationA]);
            stationID = st.id;
            t.atNode = in.id;
            t.atNodeKind = NodeKind::Gate;
            start = Start::JumpIn;
            legs.push_back(MakeLeg(Leg::Hold, 0, 0, arrivalPause));
            legs.push_back(MakeLeg(Leg::WarpTo, st.id, NodeKind::Station));
            legs.push_back(MakeLeg(Leg::ApproachDock, st.id));
            legs.push_back(MakeLeg(Leg::Dock, st.id));
        } break;
        case UndockLeave: {
            const Node& st(m_stations[it.stationA]);
            const Node& out(m_gates[it.exitGate]);
            stationID = st.id;
            t.atNode = st.id;
            t.atNodeKind = NodeKind::Station;
            start = Start::Undock;
            legs.push_back(MakeLeg(Leg::Hold, 0, 0, undockRun));
            legs.push_back(MakeLeg(Leg::WarpTo, out.id, NodeKind::Gate));
            legs.push_back(MakeLeg(Leg::ApproachGate, out.id));
            legs.push_back(MakeLeg(Leg::JumpOut, out.id));
        } break;
        case StationHop: {
            const Node& a(m_stations[it.stationA]);
            const Node& b(m_stations[it.stationB]);
            stationID = a.id;
            t.atNode = a.id;
            t.atNodeKind = NodeKind::Station;
            start = Start::Undock;
            legs.push_back(MakeLeg(Leg::Hold, 0, 0, undockRun));
            legs.push_back(MakeLeg(Leg::WarpTo, b.id, NodeKind::Station));
            legs.push_back(MakeLeg(Leg::ApproachDock, b.id));
            legs.push_back(MakeLeg(Leg::Dock, b.id));
        } break;
        case LocalRun: {
            if (m_planets.empty())
                return false;
            const Node& st(m_stations[it.stationA]);
            const Node& p(m_planets[std::min(static_cast<size_t>(m_rng.Uniform() * m_planets.size()), m_planets.size() - 1)]);
            stationID = st.id;
            t.atNode = st.id;
            t.atNodeKind = NodeKind::Station;
            start = Start::Undock;
            legs.push_back(MakeLeg(Leg::Hold, 0, 0, undockRun));
            legs.push_back(MakeLeg(Leg::WarpTo, p.id, NodeKind::Planet));
            legs.push_back(MakeLeg(Leg::Hold, 0, 0, AmbientRules::UniformMs(m_rng, 30000.0, 120000.0)));
            legs.push_back(MakeLeg(Leg::WarpTo, st.id, NodeKind::Station));
            legs.push_back(MakeLeg(Leg::ApproachDock, st.id));
            legs.push_back(MakeLeg(Leg::Dock, st.id));
        } break;
        default:
            return false;
    }
    t.spec.ownerID = (role == Role::Navy ? RaceNavyCorp(race) : PickOwner(race, stationID));
    if (t.spec.ownerID == 0)
        t.spec.ownerID = sDataMgr.GetFactionCorp(t.spec.factionID);
    return true;
}

void AmbientSystem::TrafficEvent(int64 now)
{
    ++m_events;
    sAmbientMgr.CountEvent();
    AmbientRules::Itinerary it;
    if (!AmbientRules::PickItinerary(m_rng, m_kinds, m_gateW, m_stationW, it))
        return;
    Traveller t;
    uint8 start(Start::JumpIn);
    if (!BuildTraffic(it, t, start, now))
        return;
    bool convoy(m_rng.Chance(sConfig.ambient.ConvoyChance));
    if (!Launch(t, start, now))
        return;
    if (!convoy)
        return;
    // a convoy (6.4):  2 - 4 ships of one owner on one itinerary, 5 - 20s apart
    uint32 more(1 + static_cast<uint32>(m_rng.Uniform() * 3.0));
    int64 at(now);
    for (uint32 i = 0; i < more; ++i) {
        at += AmbientRules::UniformMs(m_rng, 5000.0, 20000.0);
        PendingLaunch p;
        p.t = t;
        p.t.spec.travellerID = sAmbientMgr.NextTravellerID();
        const Archetype* pArch(sAmbientMgr.Catalog().Pick(m_rng, (t.spec.role == Role::Courier ? Role::Hauler : t.spec.role),
                                                          t.spec.race));
        if (pArch != nullptr)
            p.t.spec.arch = pArch;
        p.start = start;
        p.at = at;
        m_launches.push_back(p);
    }
    Note("convoy of " + std::to_string(more + 1) + " " + RoleName(t.spec.role));
}

bool AmbientSystem::Launch(Traveller& t, uint8 start, int64 now, bool transit/*false*/)
{
    const Node* pNode(FindNode(t.atNode));
    if (pNode == nullptr)
        return false;
    // 6.8 rule 3:  nothing comes out of a gate whose other side is watched:  that side's departures feed it
    if ((start == Start::JumpIn) and !transit and (pNode->destSystem != 0)) {
        SystemManager* pDest(sEntityList.FindSystem(pNode->destSystem));
        bool active((pDest != nullptr) and (pDest->GetAmbient() != nullptr) and pDest->GetAmbient()->IsActive());
        if (AmbientRules::SuppressArrival(active and pDest->GetAmbient()->IsGateObserved(pNode->destGate), active)) {
            ++m_suppressed;
            return false;
        }
    }
    GPoint at(pNode->kind == NodeKind::Station ? pNode->dockPos : pNode->pos);
    if (IsObservedPoint(at)) {
        if (CapAllows(at) and (Materialise(t, start, 0, now) != nullptr)) {
            Note(std::string(RoleName(t.spec.role)) + (start == Start::JumpIn ? " jumps in" : " undocks"));
            return true;
        }
    }
    // off grid:  it goes on as a traveller and shows up where somebody watches (6.4)
    t.leg = 0;
    t.nextAt = now;
    if (BeginVirtualLeg(t, now))
        AddVirtual(t);
    return true;
}

void AmbientSystem::WarmStartTraffic(const Node& node, int64 now)
{
    if (!sConfig.ambient.WarmStart or !sConfig.ambient.Traffic or !(m_lambda > 0.0))
        return;
    // the grid's share of the visible legs, times how long a ship stays on it (6.5)
    double share(0.0), dwell(0.0), sum(0.0);
    size_t idx(AmbientRules::npos);
    bool station(node.kind == NodeKind::Station);
    const std::vector<Node>& list(station ? m_stations : m_gates);
    const std::vector<double>& weights(station ? m_stationW : m_gateW);
    for (size_t i = 0; i < list.size(); ++i) {
        sum += weights[i];
        if (list[i].id == node.id)
            idx = i;
    }
    if ((idx == AmbientRules::npos) or !(sum > 0.0))
        return;
    if (station) {
        share = (m_kinds[ArriveDock] + m_kinds[UndockLeave] + (2.0 * m_kinds[StationHop]) + m_kinds[LocalRun]) * weights[idx] / sum;
        dwell = 70.0;
    } else {
        share = ((2.0 * m_kinds[Through]) + m_kinds[ArriveDock] + m_kinds[UndockLeave]) * weights[idx] / sum;
        dwell = 45.0;
    }
    uint32 n(AmbientRules::PoissonCount(m_rng, m_lambda * share * dwell / 60.0));
    uint32 gridCap(AmbientRules::GridCapFor(node.hub, sConfig.ambient.GridCap, sConfig.ambient.HubGridCap));
    uint32 here(CountOnGrid(node.pos));
    n = std::min<uint32>(n, (gridCap > here ? gridCap - here : 0));
    uint32 made(0);
    for (uint32 i = 0; i < n; ++i) {
        // a random phase per ship (R7):  just arrived, on the way in or out, or just undocked
        AmbientRules::Itinerary it;
        double u(m_rng.Uniform());
        GPoint pos;
        size_t first(0);
        uint8 start(Start::Placed);
        if (station) {
            it.stationA = idx;
            if ((u < 0.4) and !m_gates.empty()) {
                // on the way to the dock, 30 - 90% along from where it landed
                it.kind = ArriveDock;
                it.entryGate = AmbientRules::PickWeighted(m_rng, m_gateW);
                GPoint landing(OnSphere(m_rng, node.dockPos, 2000.0, 12000.0));
                double f(m_rng.Range(0.3, 0.9));
                pos = landing + ((node.dockPos - landing) * f);
                first = 2;  // ApproachDock
            } else if (!m_gates.empty()) {
                it.kind = UndockLeave;
                it.exitGate = AmbientRules::PickWeighted(m_rng, m_gateW);
                if (u < 0.7) {
                    start = Start::Undock;
                } else {
                    // aligning out
                    pos = OnSphere(m_rng, node.dockPos, 3000.0, 15000.0);
                    first = 1;  // WarpTo
                }
            } else if (m_stations.size() > 1) {
                it.kind = StationHop;
                it.stationB = AmbientRules::PickExcept(m_rng, m_stationW, idx);
                start = Start::Undock;
            } else {
                continue;
            }
        } else {
            if (u < 0.75) {
                // arrived through this gate:  holding, or aligning out
                it.entryGate = idx;
                if (!m_stations.empty() and ((m_gates.size() < 2) or m_rng.Chance(0.5))) {
                    it.kind = ArriveDock;
                    it.stationA = AmbientRules::PickWeighted(m_rng, m_stationW);
                } else if (m_gates.size() >= 2) {
                    it.kind = Through;
                    it.exitGate = AmbientRules::PickExcept(m_rng, m_gateW, idx);
                } else {
                    continue;
                }
                pos = OnSphere(m_rng, node.pos, node.radius + 6500.0, node.radius + 9500.0);
                first = (u < 0.4 ? 0 : 1);
            } else {
                // on the way to jump out here
                it.exitGate = idx;
                if (!m_stations.empty() and ((m_gates.size() < 2) or m_rng.Chance(0.5))) {
                    it.kind = UndockLeave;
                    it.stationA = AmbientRules::PickWeighted(m_rng, m_stationW);
                } else if (m_gates.size() >= 2) {
                    it.kind = Through;
                    it.entryGate = AmbientRules::PickExcept(m_rng, m_gateW, idx);
                } else {
                    continue;
                }
                pos = OnSphere(m_rng, node.pos, node.radius + 2500.0, node.radius + 12000.0);
                first = 2;  // ApproachGate
            }
        }
        if ((it.entryGate == AmbientRules::npos) and ((it.kind == Through) or (it.kind == ArriveDock)))
            continue;
        if ((it.exitGate == AmbientRules::npos) and ((it.kind == Through) or (it.kind == UndockLeave)))
            continue;
        if ((it.stationA == AmbientRules::npos) and (it.kind != Through))
            continue;
        if ((it.kind == StationHop) and (it.stationB == AmbientRules::npos))
            continue;
        Traveller t;
        uint8 dummy(0);
        if (!BuildTraffic(it, t, dummy, now))
            continue;
        if (first >= t.legs.size())
            continue;
        // the first hold is part over
        if (t.legs[first].kind == Leg::Hold)
            t.legs[first].holdMs = AmbientRules::UniformMs(m_rng, 0.0, static_cast<double>(t.legs[first].holdMs));
        if (!CapAllows(start == Start::Undock ? node.dockPos : pos))
            break;
        if (Materialise(t, start, first, now, (start == Start::Undock ? nullptr : &pos)) != nullptr)
            ++made;
    }
    if (made > 0)
        _log(AMBIENT__TRACE, "%s(%u): warm start at %s %u:  %u ships (mean %.2f).", m_system->GetName(), m_system->GetID(),
             (station ? "station" : "gate"), node.id, made, m_lambda * share * dwell / 60.0);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  miners (6.6, 8.2, 9.4)
 * ------------------------------------------------------------------------------------------------------------------ */
double AmbientSystem::MinerTarget(bool ice) const
{
    return AmbientRules::MinerTarget(sConfig.ambient.MinersPerBelt, m_profile.A, m_profile.band, sConfig.ambient.LowSecMinerMultiplier,
                                     ice, sConfig.ambient.IceMinerFactor, sConfig.ambient.DensityMultiplier, m_diurnal);
}

uint32 AmbientSystem::CountRole(SystemBubble* pBubble, uint8 role) const
{
    uint32 count(0);
    for (auto& cur : m_npcs)
        if ((cur.second->SysBubble() == pBubble) and (cur.second->GetRole() == role) and (cur.second->GetAmbientPilot() != nullptr)
        and !cur.second->GetAmbientPilot()->IsRemoving())
            ++count;
    return count;
}

bool AmbientSystem::BeltOpen(SystemBubble* pBubble) const
{
    // no miners where rats live, at sites, or on a mined out belt (6.6)
    if ((pBubble == nullptr) or !pBubble->IsBelt() or pBubble->IsAnomaly() or pBubble->IsMission() or pBubble->IsIncursion())
        return false;
    if ((m_system->GetSpawnMgr() != nullptr) and m_system->GetSpawnMgr()->HasSpawn(pBubble->GetID()))
        return false;
    std::vector<AsteroidSE*> rocks;
    BeltRocks(pBubble, rocks);
    return !rocks.empty();
}

void AmbientSystem::OnBeltObserved(const Grid& grid, int64 now)
{
    if (!sConfig.ambient.Miners)
        return;
    for (auto& cur : m_belts) {
        if (grid.center.distance(cur.pos) > BUBBLE_RADIUS_METERS)
            continue;
        BeltState& state(m_beltStates[grid.id]);
        if (state.bubbleID == 0) {
            state.bubbleID = grid.id;
            state.beltID = cur.id;
            state.ice = cur.ice;
            state.nextArrival = now + AmbientRules::UniformMs(m_rng, sConfig.ambient.MinerArrivalMin * 1000.0,
                                                               sConfig.ambient.MinerArrivalMax * 1000.0);
            double mean(static_cast<double>(std::max<uint16>(sConfig.ambient.MinerHaulerInterval, 60)));
            state.nextVisit = now + static_cast<int64>(-std::log(1.0 - m_rng.Uniform()) * mean * 1000.0);
        }
        state.warmed = false;
        return;
    }
}

const AmbientSystem::Node* AmbientSystem::OriginNode(const GPoint& target)
{
    // a station off the target's grid (weighted), else a gate
    std::vector<double> w(m_stationW);
    bool any(false);
    for (size_t i = 0; i < m_stations.size(); ++i) {
        if (m_stations[i].pos.distance(target) <= BUBBLE_RADIUS_METERS)
            w[i] = 0.0;
        else if (w[i] > 0.0)
            any = true;
    }
    if (any) {
        size_t idx(AmbientRules::PickWeighted(m_rng, w));
        if (idx != AmbientRules::npos)
            return &m_stations[idx];
    }
    w = m_gateW;
    for (size_t i = 0; i < m_gates.size(); ++i)
        if (m_gates[i].pos.distance(target) <= BUBBLE_RADIUS_METERS)
            w[i] = 0.0;
    size_t idx(AmbientRules::PickWeighted(m_rng, w));
    return (idx == AmbientRules::npos ? nullptr : &m_gates[idx]);
}

AmbientNPC* AmbientSystem::SpawnMiner(const Node& belt, SystemBubble* pBubble, bool warm, int64 now)
{
    const AmbientCatalog& catalog(sAmbientMgr.Catalog());
    // hull mode:  barges with strip miner beams (9.4).  entity mode, or no barge in the catalog:  Civilian Hulks (A1 fallback)
    bool hull((sConfig.ambient.MinerModel != "entity") and catalog.HasHullMiners());
    const Archetype* pArch(nullptr);
    if (hull) {
        std::array<double, 4> w(AmbientRules::MinerHullWeights(m_profile.band));
        size_t cls(AmbientRules::PickWeighted(m_rng, w.data(), w.size()));
        pArch = catalog.Pick(m_rng, Role::Miner, Race::None, static_cast<uint8>(cls == AmbientRules::npos ? 1 : cls), 0, true);
        if (pArch == nullptr)
            pArch = catalog.Pick(m_rng, Role::Miner, Race::None, 255, 0, true);
    }
    if (pArch == nullptr)
        pArch = catalog.Pick(m_rng, Role::Miner, Race::None, 255, 0, false);
    if (pArch == nullptr)
        return nullptr;

    Traveller t;
    uint8 race(AmbientRules::PickRace(m_rng, m_profile.homeRace, m_profile.H));
    t.spec.arch = pArch;
    t.spec.role = Role::Miner;
    t.spec.race = race;
    t.spec.name = RaceName(race, Role::Miner, pArch);
    t.spec.factionID = RaceFaction(race);
    t.spec.travellerID = sAmbientMgr.NextTravellerID();
    // 7.1:  a corp of the race's faction, preferably one with a station here
    t.spec.ownerID = 0;
    if (m_rng.Chance(0.6)) {
        std::vector<uint32> local;
        for (auto& cur : m_stations)
            if ((cur.ownerID != 0) and (FactionRace(sDataMgr.GetCorpFaction(cur.ownerID)) == race))
                local.push_back(cur.ownerID);
        if (!local.empty())
            t.spec.ownerID = local[std::min(static_cast<size_t>(m_rng.Uniform() * local.size()), local.size() - 1)];
    }
    if (t.spec.ownerID == 0)
        t.spec.ownerID = sAmbientMgr.RandomCorp(t.spec.factionID, m_rng);
    t.created = now;
    t.nextAt = now;

    if (warm) {
        // already at work:  near a rock, mining (6.5 warm start)
        std::vector<AsteroidSE*> rocks;
        BeltRocks(pBubble, rocks);
        if (rocks.empty())
            return nullptr;
        AsteroidSE* pRock(rocks[std::min(static_cast<size_t>(m_rng.Uniform() * rocks.size()), rocks.size() - 1)]);
        double range(catalog.MinerRange());
        GPoint pos(OnSphere(m_rng, pRock->GetPosition(), pRock->GetRadius() + (0.3 * range), pRock->GetRadius() + (0.7 * range)));
        t.atNode = belt.id;
        t.atNodeKind = NodeKind::Belt;
        t.legs.push_back(MakeLeg(Leg::Mine, belt.id, NodeKind::Belt));
        AmbientNPC* pNPC(Materialise(t, Start::Placed, 0, now, &pos));
        if ((pNPC != nullptr) and (pNPC->GetAmbientPilot() != nullptr))
            pNPC->GetAmbientPilot()->WarmMiner();
        return pNPC;
    }
    // arriving:  warps in from a station of the system (or a gate)
    const Node* pFrom(OriginNode(belt.pos));
    if (pFrom == nullptr)
        return nullptr;
    t.atNode = pFrom->id;
    t.atNodeKind = pFrom->kind;
    t.legs.push_back(MakeLeg(Leg::WarpTo, belt.id, NodeKind::Belt));
    t.legs.push_back(MakeLeg(Leg::Mine, belt.id, NodeKind::Belt));
    return Materialise(t, Start::WarpIn, 0, now);
}

AmbientNPC* AmbientSystem::SpawnHaulerVisit(const Node& belt, SystemBubble* pBubble, int64 now)
{
    // 8.2:  a hauler warps in to one of the miners, follows it at 1.5km for 45 - 90s, then leaves and docks
    std::vector<AmbientNPC*> miners;
    for (auto& cur : m_npcs)
        if ((cur.second->SysBubble() == pBubble) and (cur.second->GetRole() == Role::Miner))
            miners.push_back(cur.second);
    if (miners.empty())
        return nullptr;
    AmbientNPC* pMiner(miners[std::min(static_cast<size_t>(m_rng.Uniform() * miners.size()), miners.size() - 1)]);
    uint8 race(pMiner->GetSpec().race < Race::Count ? pMiner->GetSpec().race : AmbientRules::PickRace(m_rng, m_profile.homeRace, m_profile.H));
    const Archetype* pArch(sAmbientMgr.Catalog().Pick(m_rng, Role::Hauler, race));
    if (pArch == nullptr)
        return nullptr;
    const Node* pFrom(OriginNode(belt.pos));
    if (pFrom == nullptr)
        return nullptr;
    Traveller t;
    t.spec.arch = pArch;
    t.spec.role = Role::Hauler;
    t.spec.race = race;
    t.spec.name = RaceName(race, Role::Hauler, pArch);
    t.spec.factionID = RaceFaction(race);
    t.spec.ownerID = pMiner->GetSpec().ownerID;     // the miners' own hauler
    t.spec.travellerID = sAmbientMgr.NextTravellerID();
    t.atNode = pFrom->id;
    t.atNodeKind = pFrom->kind;
    t.created = now;
    t.nextAt = now;
    t.legs.push_back(MakeLeg(Leg::WarpTo, pMiner->GetID(), NodeKind::Entity));
    t.legs.push_back(MakeLeg(Leg::Visit, pMiner->GetID(), 0, AmbientRules::UniformMs(m_rng, 45000.0, 90000.0)));
    t.legs.push_back(MakeLeg(Leg::Leave));
    AmbientNPC* pNPC(Materialise(t, Start::WarpIn, 0, now));
    if (pNPC != nullptr)
        Note("hauler visits the miners");
    return pNPC;
}

void AmbientSystem::ProcessBelts(int64 now)
{
    if (m_beltStates.empty())
        return;
    for (auto itr = m_beltStates.begin(); itr != m_beltStates.end();) {
        BeltState& state(itr->second);
        // a belt nobody watches any more is forgotten (its miners linger, ProcessLinger)
        bool watched(false);
        for (auto& cur : m_observed)
            if (cur.id == state.bubbleID)
                watched = true;
        if (!watched or !sConfig.ambient.Miners) {
            itr = m_beltStates.erase(itr);
            continue;
        }
        ++itr;
        SystemBubble* pBubble(sBubbleMgr.FindBubbleByID(state.bubbleID));
        const Node* pBelt(FindNode(state.beltID));
        if ((pBubble == nullptr) or (pBelt == nullptr) or !BeltOpen(pBubble))
            continue;
        uint32 miners(CountRole(pBubble, Role::Miner));
        double target(MinerTarget(state.ice));
        if (!state.warmed) {
            // the steady state at once:  Poisson(M_target) miners at work, less those still here (6.6)
            state.warmed = true;
            if (sConfig.ambient.WarmStart) {
                uint32 n(AmbientRules::PoissonCount(m_rng, target));
                for (uint32 i = miners; i < n; ++i) {
                    if (!CapAllows(pBelt->pos))
                        break;
                    if (SpawnMiner(*pBelt, pBubble, true, now) != nullptr)
                        ++miners;
                }
            }
        }
        // under the target:  one arrives every U(MinerArrivalMin, Max)s
        if ((miners < static_cast<uint32>(std::floor(target + 0.5))) and (now >= state.nextArrival)) {
            state.nextArrival = now + AmbientRules::UniformMs(m_rng, sConfig.ambient.MinerArrivalMin * 1000.0,
                                                               sConfig.ambient.MinerArrivalMax * 1000.0);
            if (CapAllows(pBelt->pos) and (SpawnMiner(*pBelt, pBubble, false, now) != nullptr))
                Note("a miner arrives");
        }
        // a hauler visit to belts with two miners or more (exponential, mean MinerHaulerInterval)
        if (sConfig.ambient.MinerHaulerVisits and (now >= state.nextVisit)) {
            double mean(static_cast<double>(std::max<uint16>(sConfig.ambient.MinerHaulerInterval, 60)));
            state.nextVisit = now + static_cast<int64>(-std::log(1.0 - m_rng.Uniform()) * mean * 1000.0);
            if ((miners >= 2) and (CountRole(pBubble, Role::Hauler) == 0) and CapAllows(pBelt->pos))
                SpawnHaulerVisit(*pBelt, pBubble, now);
        }
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  off-grid travellers and coherence (6.8)
 * ------------------------------------------------------------------------------------------------------------------ */
void AmbientSystem::AddVirtual(const Traveller& t)
{
    while (!m_virtual.empty() and (m_virtual.size() >= sConfig.ambient.VirtualCap))
        m_virtual.pop_front();      // the oldest go first
    if (sConfig.ambient.VirtualCap > 0)
        m_virtual.push_back(t);
}

int64 AmbientSystem::VirtualLegMs(const Traveller& t, const LegData& leg) const
{
    switch (leg.kind) {
        case Leg::Hold:
            return leg.holdMs;
        case Leg::WarpTo: {
            // align (-ln(0.25) x mass x agility) and the warp at the reference hull's speed, plus its acceleration and landing
            const Archetype* pArch(t.spec.arch);
            if (pArch == nullptr)
                return 30000;
            const Node* pFrom(FindNode(t.atNode));
            const Node* pTo(FindNode(leg.targetID));
            double distance((pFrom != nullptr) and (pTo != nullptr) ? pFrom->pos.distance(pTo->pos) : ONE_AU_IN_METERS);
            double align(1.386294 * (pArch->mass / 1.0e6) * pArch->agility);
            double warp(distance / (std::max(1, pArch->warpFactor) * 0.1 * ONE_AU_IN_METERS));
            return static_cast<int64>((align + warp + 10.0) * 1000.0);
        }
        case Leg::ApproachGate:
        case Leg::ApproachDock:
            return 25000;
        default:
            return 0;
    }
}

bool AmbientSystem::BeginVirtualLeg(Traveller& t, int64 now)
{
    while (t.leg < t.legs.size()) {
        const LegData& leg(t.legs[t.leg]);
        switch (leg.kind) {
            case Leg::WarpTo: {
                // 6.8 rule 4:  a warp toward (or away from) a watched grid is flown for real:  the ship appears at its origin
                //  and observers see it land.  the caps count where it lands
                const Node* pFrom(FindNode(t.atNode));
                const Node* pTo(FindNode(leg.targetID));
                if ((pFrom != nullptr) and (pTo != nullptr)) {
                    GPoint dest(pTo->kind == NodeKind::Station ? pTo->dockPos : pTo->pos);
                    GPoint orig(pFrom->kind == NodeKind::Station ? pFrom->dockPos : pFrom->pos);
                    if ((IsObservedPoint(dest) or IsObservedPoint(orig)) and CapAllows(dest)
                    and (Materialise(t, Start::WarpIn, t.leg, now) != nullptr)) {
                        Note(std::string(RoleName(t.spec.role)) + " warps in");
                        return false;
                    }
                }
            } break;
            case Leg::JumpOut: {
                // through the gate unseen:  it goes on in the next system
                const Node* pGate(FindNode(leg.targetID));
                if ((pGate != nullptr) and (pGate->destSystem != 0)) {
                    Traveller tr(t);
                    tr.legs.clear();
                    tr.leg = 0;
                    sAmbientMgr.QueueTransit(tr, m_system->GetID(), pGate->destSystem, pGate->destGate,
                                             now + AmbientRules::TransitDelayMs(m_rng, sConfig.ambient.JumpTransitMin,
                                                                                sConfig.ambient.JumpTransitMax));
                }
                return false;
            }
            case Leg::Dock:
            case Leg::Mine:
            case Leg::Hunt:
            case Leg::Leave:
            case Leg::Squad:
                // docked, or nothing an off-grid ship goes on doing
                return false;
            default:
                break;
        }
        t.nextAt = now + VirtualLegMs(t, leg);
        return true;
    }
    return false;
}

bool AmbientSystem::StepVirtual(Traveller& t, int64 now)
{
    if (now < t.nextAt)
        return true;
    if (t.leg < t.legs.size()) {
        const LegData& leg(t.legs[t.leg]);
        if (leg.kind == Leg::WarpTo) {
            t.atNode = leg.targetID;
            t.atNodeKind = leg.node;
        }
    }
    ++t.leg;
    return BeginVirtualLeg(t, now);
}

void AmbientSystem::ProcessVirtual(int64 now)
{
    for (size_t i = 0; i < m_virtual.size();) {
        if (StepVirtual(m_virtual[i], now)) {
            ++i;
        } else {
            m_virtual.erase(m_virtual.begin() + i);
        }
    }
}

bool AmbientSystem::AcceptTransit(const Traveller& t, uint32 gateID)
{
    if (!m_active or !sConfig.ambient.Traffic)
        return false;
    const Node* pGate(FindNode(gateID));
    if ((pGate == nullptr) or (pGate->kind != NodeKind::Gate))
        return false;
    size_t idx(AmbientRules::npos);
    for (size_t i = 0; i < m_gates.size(); ++i)
        if (m_gates[i].id == gateID)
            idx = i;
    // where it goes here, by this system's weights:  on through another gate, or to a station
    Traveller tr(t);
    tr.atNode = gateID;
    tr.atNodeKind = NodeKind::Gate;
    tr.legs.clear();
    tr.leg = 0;
    int64 now(GetSteadyTime());
    tr.nextAt = now;
    double wThrough(m_gates.size() >= 2 ? m_kinds[Through] : 0.0);
    double wDock(m_stations.empty() ? 0.0 : m_kinds[ArriveDock]);
    if (!(wThrough + wDock > 0.0))
        return false;
    tr.legs.push_back(MakeLeg(Leg::Hold, 0, 0, AmbientRules::UniformMs(m_rng, 2000.0, 20000.0)));
    if (m_rng.Uniform() * (wThrough + wDock) < wThrough) {
        size_t out(AmbientRules::PickExcept(m_rng, m_gateW, idx));
        if (out == AmbientRules::npos)
            return false;
        tr.legs.push_back(MakeLeg(Leg::WarpTo, m_gates[out].id, NodeKind::Gate));
        tr.legs.push_back(MakeLeg(Leg::ApproachGate, m_gates[out].id));
        tr.legs.push_back(MakeLeg(Leg::JumpOut, m_gates[out].id));
    } else {
        size_t st(AmbientRules::PickWeighted(m_rng, m_stationW));
        if (st == AmbientRules::npos)
            return false;
        tr.legs.push_back(MakeLeg(Leg::WarpTo, m_stations[st].id, NodeKind::Station));
        tr.legs.push_back(MakeLeg(Leg::ApproachDock, m_stations[st].id));
        tr.legs.push_back(MakeLeg(Leg::Dock, m_stations[st].id));
    }
    _log(AMBIENT__TRACE, "%s(%u): %s [%u] arrives through gate %u.", m_system->GetName(), m_system->GetID(), tr.spec.name.c_str(),
         tr.spec.travellerID, gateID);
    return Launch(tr, Start::JumpIn, now, true);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  criminals (6.7, 8.5)
 * ------------------------------------------------------------------------------------------------------------------ */
double AmbientSystem::CriminalRatePerMin() const
{
    if (!sConfig.ambient.Criminals)
        return 0.0;
    return AmbientRules::CriminalRatePerHour(m_profile.band, m_profile.level, m_profile.A, sConfig.ambient.CriminalsPerHourHigh,
                                             sConfig.ambient.CriminalsPerHourLow, sConfig.ambient.DensityMultiplier) / 60.0;
}

uint32 AmbientSystem::PirateFaction()
{
    // the faction belt rats here belong to (sDataMgr.GetRegionRatFaction), else any of the five
    static const uint32 pirates[] = {factionAngel, factionBloodRaider, factionGuristas, factionSanshas, factionSerpentis};
    uint32 faction(sDataMgr.GetRegionRatFaction(m_profile.regionID));
    for (auto cur : pirates)
        if (cur == faction)
            return faction;
    return pirates[std::min(static_cast<size_t>(m_rng.Uniform() * 5.0), static_cast<size_t>(4))];
}

void AmbientSystem::CriminalEvent(int64 now)
{
    /* where they go (6.7):  a watched belt with miners, a watched gate with traffic, and in low sec (when they attack players)
     *  any watched belt or gate with a player.  nowhere:  skipped */
    bool low(m_profile.band == AmbientRules::Band::Low);
    bool players(low ? sConfig.ambient.LowSecCriminalsAttackPlayers : sConfig.ambient.HighSecCriminalsAttackPlayers);
    std::vector<std::pair<const Node*, uint32>> targets;    // node, bubbleID
    for (auto& grid : m_observed) {
        SystemBubble* pBubble(sBubbleMgr.FindBubbleByID(grid.id));
        if (pBubble == nullptr)
            continue;
        const Node* pBelt(NodeAt(grid.center, NodeKind::Belt));
        const Node* pGate(NodeAt(grid.center, NodeKind::Gate));
        if ((pBelt != nullptr) and pBubble->IsBelt()) {
            if ((CountRole(pBubble, Role::Miner) > 0) or (players and pBubble->HasPlayers()))
                targets.push_back(std::make_pair(pBelt, grid.id));
        } else if ((pGate != nullptr) and pBubble->IsGate()) {
            uint32 traffic(CountRole(pBubble, Role::Hauler) + CountRole(pBubble, Role::Courier) + CountRole(pBubble, Role::Freighter));
            if ((traffic > 0) or (players and pBubble->HasPlayers()))
                targets.push_back(std::make_pair(pGate, grid.id));
        }
    }
    if (targets.empty())
        return;
    const auto& target(targets[std::min(static_cast<size_t>(m_rng.Uniform() * targets.size()), targets.size() - 1)]);
    SpawnCriminals(*target.first, AmbientRules::CriminalGroupSize(m_rng), now);
}

uint32 AmbientSystem::SpawnCriminals(const Node& node, uint32 count, int64 now)
{
    uint32 faction(PirateFaction());
    uint32 groupID(sAmbientMgr.NextGroupID());
    std::shared_ptr<AggroGroup> group(m_system->GetSpawnMgr()->GetAggroGroups().Get(NpcAI::GroupKind::Ambient, groupID));
    // at a gate they jump in;  at a belt they warp in from a planet (or a station) off grid
    const Node* pFrom(nullptr);
    if (node.kind != NodeKind::Gate) {
        std::vector<std::pair<double, const Node*>> found;
        for (auto& cur : m_planets)
            if (cur.pos.distance(node.pos) > BUBBLE_RADIUS_METERS)
                found.push_back(std::make_pair(cur.pos.distance(node.pos), &cur));
        if (found.empty())
            pFrom = OriginNode(node.pos);
        else
            pFrom = found[std::min(static_cast<size_t>(m_rng.Uniform() * found.size()), found.size() - 1)].second;
        if (pFrom == nullptr)
            return 0;
    }
    uint32 made(0);
    for (uint32 i = 0; i < count; ++i) {
        const Archetype* pArch(sAmbientMgr.Catalog().Pick(m_rng, Role::Criminal, Race::None,
                                                          AmbientRules::CriminalClass(m_rng, m_profile.band), faction));
        if (pArch == nullptr)
            pArch = sAmbientMgr.Catalog().Pick(m_rng, Role::Criminal, Race::None, 0, faction);
        if (pArch == nullptr)
            pArch = sAmbientMgr.Catalog().Pick(m_rng, Role::Criminal);
        if (pArch == nullptr)
            break;
        Traveller t;
        t.spec.arch = pArch;
        t.spec.role = Role::Criminal;
        t.spec.name = pArch->typeName;
        t.spec.factionID = pArch->pirateFaction;
        t.spec.ownerID = sDataMgr.GetFactionCorp(pArch->pirateFaction);
        t.spec.travellerID = sAmbientMgr.NextTravellerID();
        t.spec.groupID = groupID;
        t.created = now;
        t.nextAt = now;
        uint8 start(Start::JumpIn);
        if (node.kind == NodeKind::Gate) {
            t.atNode = node.id;
            t.atNodeKind = NodeKind::Gate;
            t.legs.push_back(MakeLeg(Leg::Hold, 0, 0, AmbientRules::UniformMs(m_rng, 1000.0, 6000.0)));
        } else {
            t.atNode = pFrom->id;
            t.atNodeKind = pFrom->kind;
            t.legs.push_back(MakeLeg(Leg::WarpTo, node.id, node.kind));
            start = Start::WarpIn;
        }
        t.legs.push_back(MakeLeg(Leg::Hunt));
        if (!CapAllows(node.pos))
            break;
        if (Materialise(t, start, 0, now, nullptr, &group) != nullptr)
            ++made;
    }
    if (made > 0) {
        Note(std::to_string(made) + " criminals at " + std::to_string(node.id));
        _log(AMBIENT__INFO, "%s(%u): %u %s criminals arrive at %u.", m_system->GetName(), m_system->GetID(), made,
             sDataMgr.GetFactionName(faction).c_str(), node.id);
    }
    return made;
}

/* ------------------------------------------------------------------------------------------------------------------
 *  CONCORD and navy patrol squads (6.7, 8.3, 8.4)
 * ------------------------------------------------------------------------------------------------------------------ */
void AmbientSystem::BuildSquads(int64 now)
{
    m_squads.clear();
    m_wpIDs.clear();
    m_wpW.clear();
    // waypoints:  every station and gate, weighted like traffic (8.3)
    for (auto& cur : m_stations) {
        m_wpIDs.push_back(cur.id);
        m_wpW.push_back(cur.weight);
    }
    for (auto& cur : m_gates) {
        m_wpIDs.push_back(cur.id);
        m_wpW.push_back(cur.weight);
    }
    if (m_wpIDs.empty() or (m_profile.band != AmbientRules::Band::High))
        return;
    uint32 concord(sConfig.ambient.ConcordPatrols ? AmbientRules::ConcordSquads(m_profile.band, m_profile.A, m_profile.level,
                   sConfig.ambient.PatrolSquadsBase, sConfig.ambient.PatrolSquadsMax, sConfig.ambient.DensityMultiplier) : 0);
    uint32 navy(sConfig.ambient.NavyPatrols ? AmbientRules::NavySquads(m_profile.band, (m_profile.homeRace < Race::Count),
                m_profile.A, sConfig.ambient.NavySquadsBase, sConfig.ambient.DensityMultiplier) : 0);
    for (uint32 i = 0; i < (concord + navy); ++i) {
        Squad s;
        s.id = sAmbientMgr.NextGroupID();
        s.role = (i < concord ? Role::Concord : Role::Navy);
        s.race = (s.role == Role::Navy ? m_profile.homeRace : Race::None);
        FormSquad(s);
        if (s.members.empty())
            continue;
        s.wp = AmbientRules::PickWeighted(m_rng, m_wpW);
        if (s.wp == AmbientRules::npos)
            s.wp = 0;
        // a random point of its loiter, so squads don't all move together
        s.loiterUntil = now + AmbientRules::UniformMs(m_rng, 0.0, sConfig.ambient.PatrolLoiterMax * 1000.0);
        m_squads.push_back(s);
    }
    if (!m_squads.empty())
        _log(AMBIENT__INFO, "%s(%u): %u CONCORD and %u navy patrol squads.", m_system->GetName(), m_system->GetID(), concord, navy);
}

void AmbientSystem::FormSquad(Squad& s)
{
    // 7.2:  CONCORD 2 officers and a captain (a commander too, 25%, in busy systems).  navy 2 frigates and a cruiser, or a
    //  cruiser and a battleship (20%)
    const AmbientCatalog& catalog(sAmbientMgr.Catalog());
    s.members.clear();
    auto add = [&](uint8 cls) {
        const Archetype* pArch(catalog.Pick(m_rng, s.role, s.race, cls));
        if (pArch != nullptr)
            s.members.push_back(pArch);
    };
    if (s.role == Role::Concord) {
        add(0);
        add(0);
        add(1);
        if ((m_profile.A >= 0.6) and m_rng.Chance(0.25))
            add(2);
    } else if (m_rng.Chance(0.2)) {
        add(1);
        add(2);
    } else {
        add(0);
        add(0);
        add(1);
    }
    s.npcIDs.clear();
    s.killed = 0;
    s.respawnAt = 0;
    s.respondTo = 0;
    s.group = m_system->GetSpawnMgr()->GetAggroGroups().Get(NpcAI::GroupKind::Ambient, s.id);
}

bool AmbientSystem::MaterialiseSquad(Squad& s, uint32 atNode, uint32 warpTo, uint8 warpKind, int64 now)
{
    // the squad appears at its waypoint:  orbiting it (8.3), or warping from it to warpTo (observers see them land)
    const Node* pNode(FindNode(atNode));
    if (pNode == nullptr)
        return false;
    GPoint centre(pNode->pos);
    if (now < s.retryAt)
        return false;
    if (!CapAllows(centre)) {
        s.retryAt = now + 10000;
        return false;
    }
    s.npcIDs.clear();
    for (size_t i = 0; i < s.members.size(); ++i) {
        const Archetype* pArch(s.members[i]);
        Traveller t;
        t.spec.arch = pArch;
        t.spec.role = s.role;
        t.spec.race = s.race;
        t.spec.name = pArch->typeName;
        if (s.role == Role::Concord) {
            t.spec.ownerID = corpCONCORD;
            t.spec.factionID = factionCONCORD;
        } else {
            t.spec.ownerID = RaceNavyCorp(s.race);
            t.spec.factionID = RaceFaction(s.race);
        }
        t.spec.travellerID = sAmbientMgr.NextTravellerID();
        t.spec.squadID = s.id;
        t.atNode = atNode;
        t.atNodeKind = pNode->kind;
        t.created = now;
        t.nextAt = now;
        GPoint pos(OnSphere(m_rng, centre, pNode->radius + 15000.0, pNode->radius + 30000.0));
        if (warpTo != 0) {
            // the leader goes first, the others 0 - 3s later (8.3)
            if (i > 0)
                t.legs.push_back(MakeLeg(Leg::Hold, 0, 0, AmbientRules::UniformMs(m_rng, 0.0, 3000.0)));
            t.legs.push_back(MakeLeg(Leg::WarpTo, warpTo, warpKind));
            t.legs.push_back(MakeLeg(Leg::Squad));
            pos = OnSphere(m_rng, centre, pNode->radius + 5000.0, pNode->radius + 15000.0);
        } else {
            int64 loiter(std::max<int64>(s.loiterUntil - now, 30000));
            t.legs.push_back(MakeLeg(Leg::Loiter, atNode, pNode->kind, loiter));
            t.legs.push_back(MakeLeg(Leg::Squad));
        }
        AmbientNPC* pNPC(Materialise(t, Start::Placed, 0, now, &pos, &s.group));
        if (pNPC != nullptr)
            s.npcIDs.push_back(pNPC->GetID());
    }
    return !s.npcIDs.empty();
}

AmbientSystem::Squad* AmbientSystem::FindSquad(uint32 squadID)
{
    for (auto& cur : m_squads)
        if (cur.id == squadID)
            return &cur;
    return nullptr;
}

void AmbientSystem::ProcessSquads(int64 now)
{
    for (auto& s : m_squads) {
        // destroyed:  a new squad forms 15 minutes later
        if (s.respawnAt != 0) {
            if (now < s.respawnAt)
                continue;
            FormSquad(s);
            s.loiterUntil = now + AmbientRules::UniformMs(m_rng, sConfig.ambient.PatrolLoiterMin * 1000.0,
                                                          sConfig.ambient.PatrolLoiterMax * 1000.0);
        }
        // the members still in space
        std::vector<AmbientNPC*> members;
        for (auto itr = s.npcIDs.begin(); itr != s.npcIDs.end();) {
            std::map<uint32, AmbientNPC*>::iterator nItr(m_npcs.find(*itr));
            if ((nItr == m_npcs.end()) or nItr->second->IsDead()) {
                itr = s.npcIDs.erase(itr);
            } else {
                members.push_back(nItr->second);
                ++itr;
            }
        }
        if ((s.killed > 0) and (s.killed >= s.members.size())) {
            s.respawnAt = now + 900000;
            s.npcIDs.clear();
            continue;
        }
        if (s.wp >= m_wpIDs.size())
            s.wp = 0;
        uint32 wpID(m_wpIDs.empty() ? 0 : m_wpIDs[s.wp]);
        const Node* pWp(FindNode(wpID));
        if (pWp == nullptr)
            continue;
        bool fighting(false);
        for (auto pNPC : members)
            if (!pNPC->GetAIMgr()->IsIdle())
                fighting = true;

        // answering a crime (8.4):  engage once on the target's grid, until it is gone
        if (s.respondTo != 0) {
            SystemEntity* pTarget(m_system->GetSE(s.respondTo));
            if ((pTarget == nullptr) or pTarget->IsDead() or (now > s.respondUntil)) {
                s.respondTo = 0;
            } else {
                for (auto pNPC : members)
                    if ((pNPC->SysBubble() == pTarget->SysBubble()) and (pNPC->GetAmbientPilot() != nullptr))
                        pNPC->GetAmbientPilot()->SquadEngage(pTarget);
                continue;
            }
        }

        bool watched(IsObservedPoint(pWp->pos));
        if (members.empty() and watched) {
            // its waypoint became watched:  it is there, orbiting (8.3)
            if (MaterialiseSquad(s, wpID, 0, 0, now))
                Note(std::string(RoleName(s.role)) + " squad at " + std::to_string(wpID));
            continue;
        }
        // members that landed wait for orders:  loiter at the waypoint
        for (auto pNPC : members) {
            AmbientPilot* pPilot(pNPC->GetAmbientPilot());
            if ((pPilot != nullptr) and pPilot->AwaitingOrders() and pNPC->GetAIMgr()->IsIdle())
                pPilot->SquadLoiter(wpID, std::max<int64>(s.loiterUntil - now, 30000), now);
        }
        if (now < s.loiterUntil)
            continue;
        if (fighting) {
            s.loiterUntil = now + 30000;
            continue;
        }
        // move on:  the next waypoint (not this one), weighted
        size_t next(AmbientRules::PickExcept(m_rng, m_wpW, s.wp));
        if (next == AmbientRules::npos)
            next = s.wp;
        uint32 nextID(m_wpIDs[next]);
        const Node* pNext(FindNode(nextID));
        if (pNext == nullptr)
            continue;
        int64 loiter(AmbientRules::UniformMs(m_rng, sConfig.ambient.PatrolLoiterMin * 1000.0, sConfig.ambient.PatrolLoiterMax * 1000.0));
        bool nextWatched(IsObservedPoint(pNext->pos));
        if (!members.empty()) {
            // the leader warps first, the others 0 - 3s later
            for (size_t i = 0; i < members.size(); ++i)
                if (members[i]->GetAmbientPilot() != nullptr)
                    members[i]->GetAmbientPilot()->SquadWarp(nextID, pNext->kind,
                                                             (i == 0 ? 0 : AmbientRules::UniformMs(m_rng, 0.0, 3000.0)), now);
            loiter += 60000;    // the trip
        } else if (nextWatched) {
            // from its old waypoint to a watched one:  observers see it land
            MaterialiseSquad(s, wpID, nextID, pNext->kind, now);
            loiter += 60000;
        }
        s.wp = next;
        s.loiterUntil = now + loiter;
    }
}

void AmbientSystem::OnLawDispatched(uint32 targetID, uint8 kind)
{
    // 8.4:  the nearest patrol of that kind warps to the crime, as well as CONCORD's own response (not instead of it)
    if (!m_active or !sConfig.ambient.PatrolResponse or m_squads.empty())
        return;
    uint8 role(Role::None);
    if (kind == Law::Concord)
        role = Role::Concord;
    else if (kind == Law::Navy)
        role = Role::Navy;
    if (role == Role::None)
        return;
    SystemEntity* pTarget(nullptr);
    if (IsCharacterID(targetID)) {
        Client* pClient(sEntityList.FindClientByCharID(targetID));
        if ((pClient != nullptr) and (pClient->SystemMgr() == m_system) and pClient->IsInSpace() and !pClient->InPod())
            pTarget = pClient->GetShipSE();
    } else {
        pTarget = m_system->GetSE(targetID);
    }
    if ((pTarget == nullptr) or pTarget->IsDead() or (pTarget->SysBubble() == nullptr) or !IsObservedBubble(pTarget->SysBubble()))
        return;
    // on the target's grid first, else the one whose waypoint is nearest
    Squad* pBest(nullptr);
    double best(0.0);
    for (auto& s : m_squads) {
        if ((s.role != role) or (s.respawnAt != 0) or (s.respondTo != 0) or (s.wp >= m_wpIDs.size()))
            continue;
        const Node* pWp(FindNode(m_wpIDs[s.wp]));
        if (pWp == nullptr)
            continue;
        double distance(pWp->pos.distance(pTarget->GetPosition()));
        for (auto id : s.npcIDs) {
            SystemEntity* pMember(m_system->GetSE(id));
            if ((pMember != nullptr) and (pMember->SysBubble() == pTarget->SysBubble()))
                distance = 0.0;
        }
        if ((pBest == nullptr) or (distance < best)) {
            pBest = &s;
            best = distance;
        }
    }
    if (pBest == nullptr)
        return;
    Squad& s(*pBest);
    s.respondTo = pTarget->GetID();
    s.respondUntil = GetSteadyTime() + 300000;
    int64 now(GetSteadyTime());
    if (s.npcIDs.empty()) {
        MaterialiseSquad(s, m_wpIDs[s.wp], pTarget->GetID(), NodeKind::Entity, now);
    } else {
        size_t i(0);
        for (auto id : s.npcIDs) {
            std::map<uint32, AmbientNPC*>::iterator itr(m_npcs.find(id));
            if ((itr == m_npcs.end()) or (itr->second->GetAmbientPilot() == nullptr))
                continue;
            if (itr->second->SysBubble() == pTarget->SysBubble())
                itr->second->GetAmbientPilot()->SquadEngage(pTarget);
            else
                itr->second->GetAmbientPilot()->SquadWarp(pTarget->GetID(), NodeKind::Entity,
                                                          (i == 0 ? 0 : AmbientRules::UniformMs(m_rng, 0.0, 3000.0)), now);
            ++i;
        }
    }
    Note(std::string(RoleName(role)) + " patrol answers a crime");
    _log(AMBIENT__INFO, "%s(%u): %s squad %u answers the crime of %s(%u).", m_system->GetName(), m_system->GetID(), RoleName(role),
         s.id, pTarget->GetName(), pTarget->GetID());
}


/* ------------------------------------------------------------------------------------------------------------------
 *  /ambient (4.5)
 * ------------------------------------------------------------------------------------------------------------------ */
std::string AmbientSystem::Summary() const
{
    std::ostringstream str;
    str << m_system->GetName() << " (" << m_system->GetID() << ") A " << std::fixed << std::setprecision(2) << m_profile.A
        << ", " << m_lambda << "/min, " << m_npcs.size() << " ships, " << m_virtual.size() << " off grid, "
        << m_observed.size() << " grids watched";
    return str.str();
}

std::string AmbientSystem::Describe()
{
    std::ostringstream str;
    str << std::fixed << std::setprecision(2);
    str << "<b>ambient in " << m_system->GetName() << " (" << m_system->GetID() << ")</b>:  "
        << (m_active ? "active" : (m_never ? "never (null sec or wormhole)" : "idle"));
    if (m_windDownAt != 0)
        str << " (winding down in " << ((m_windDownAt - GetSteadyTime()) / 1000) << "s)";
    str << "<br>";
    str << "A " << m_profile.A << " (H " << m_profile.H << ", P " << m_profile.P << ", R " << m_profile.R << ", S " << m_profile.S
        << "), level " << std::setprecision(1) << m_profile.level << std::setprecision(2) << "<br>";
    str << "traffic " << m_lambda << " events/min (day/night x" << m_diurnal << ").  kinds:  through " << m_kinds[Through]
        << ", arrive " << m_kinds[ArriveDock] << ", undock " << m_kinds[UndockLeave] << ", hop " << m_kinds[StationHop]
        << ", local " << m_kinds[LocalRun] << "<br>";
    str << "watched grids " << m_observed.size() << ":";
    for (auto& cur : m_observed)
        str << " " << cur.id << "(" << cur.players << ")";
    str << "<br>";
    uint32 roles[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (auto& cur : m_npcs)
        if (cur.second->GetRole() < 8)
            ++roles[cur.second->GetRole()];
    str << "materialised " << m_npcs.size() << ":";
    for (uint8 i = 1; i < 8; ++i)
        if (roles[i] > 0)
            str << " " << RoleName(i) << " " << roles[i];
    str << ".  off grid " << m_virtual.size() << ", queued " << m_launches.size() << "<br>";
    str << "events " << m_events << ", materialised " << m_materialised << ", killed " << m_killed << ", cap hits " << m_capHits
        << ", suppressed arrivals " << m_suppressed << ", " << (m_ticks > 0 ? m_usSum / m_ticks : 0.0) << " us/tick<br>";
    for (auto& s : m_squads) {
        str << RoleName(s.role) << " squad " << s.id << ":  " << s.members.size() << " ships, waypoint "
            << (s.wp < m_wpIDs.size() ? m_wpIDs[s.wp] : 0) << ", " << (s.npcIDs.empty() ? "off grid" : "in space");
        if (s.respondTo != 0)
            str << ", answering a crime on " << s.respondTo;
        if (s.respawnAt != 0)
            str << ", destroyed";
        str << "<br>";
    }
    str << "last events:";
    for (auto& cur : m_log)
        str << " | " << cur;
    str << "<br>";
    return str.str();
}

uint32 AmbientSystem::ForceSpawn(Client* pClient, const std::string& what, uint32 count, std::string& why)
{
    if (!sAmbientMgr.IsEnabled()) {
        why = "ambient npcs are disabled (<ambient><Enabled>)";
        return 0;
    }
    if (m_never or !m_active) {
        why = (m_never ? "no ambient npcs in this system (sec <= 0 or wormhole)" : "ambient isn't active here yet (try again in a second)");
        return 0;
    }
    ShipSE* pShip(pClient->GetShipSE());
    if ((pShip == nullptr) or (pShip->SysBubble() == nullptr)) {
        why = "you have to be in space";
        return 0;
    }
    GPoint here(pShip->GetPosition());
    int64 now(GetSteadyTime());
    uint8 role(Role::None);
    if (what == "hauler")           role = Role::Hauler;
    else if (what == "courier")     role = Role::Courier;
    else if (what == "freighter")   role = Role::Freighter;
    else if (what == "navy")        role = Role::Navy;
    else if (what == "concord")     role = Role::Concord;
    else if (what == "miner")       role = Role::Miner;
    else if (what == "criminal")    role = Role::Criminal;
    if (role == Role::None) {
        why = "unknown kind " + what;
        return 0;
    }
    uint32 made(0);
    const Node* pGate(NodeAt(here, NodeKind::Gate));
    const Node* pStation(NodeAt(here, NodeKind::Station));
    for (uint32 i = 0; i < count; ++i) {
        if ((role >= Role::Hauler) and (role <= Role::Navy)) {
            // traffic on your grid:  through your gate, or out of your station
            AmbientRules::Itinerary it;
            if (pGate != nullptr) {
                for (size_t g = 0; g < m_gates.size(); ++g)
                    if (m_gates[g].id == pGate->id)
                        it.entryGate = g;
                if (!m_stations.empty()) {
                    it.kind = ArriveDock;
                    it.stationA = AmbientRules::PickWeighted(m_rng, m_stationW);
                } else {
                    it.kind = Through;
                    it.exitGate = AmbientRules::PickExcept(m_rng, m_gateW, it.entryGate);
                }
            } else if ((pStation != nullptr) and !m_gates.empty()) {
                for (size_t s = 0; s < m_stations.size(); ++s)
                    if (m_stations[s].id == pStation->id)
                        it.stationA = s;
                it.kind = UndockLeave;
                it.exitGate = AmbientRules::PickWeighted(m_rng, m_gateW);
            } else {
                why = "traffic needs a gate or station grid";
                break;
            }
            if (((it.kind == Through) and (it.exitGate == AmbientRules::npos)) or ((it.kind != Through) and (it.stationA == AmbientRules::npos)))
                break;
            Traveller t;
            uint8 start(Start::JumpIn);
            if (!BuildTraffic(it, t, start, now))
                break;
            // the kind asked for
            uint8 race(t.spec.race);
            const Archetype* pArch(nullptr);
            if (role == Role::Navy) {
                race = (m_profile.homeRace < Race::Count ? m_profile.homeRace : race);
                pArch = sAmbientMgr.Catalog().Pick(m_rng, Role::Navy, race, (m_rng.Chance(0.8) ? 0 : 1));
                t.spec.ownerID = RaceNavyCorp(race);
            } else {
                pArch = sAmbientMgr.Catalog().Pick(m_rng, role, race);
                if ((pArch == nullptr) and (role == Role::Courier))
                    pArch = sAmbientMgr.Catalog().Pick(m_rng, Role::Hauler, race);
            }
            if (pArch == nullptr) {
                why = std::string("no ") + what + " type in the catalog";
                break;
            }
            t.spec.arch = pArch;
            t.spec.role = role;
            t.spec.race = race;
            t.spec.factionID = RaceFaction(race);
            t.spec.name = RaceName(race, role, pArch);
            if (Materialise(t, start, 0, now) != nullptr)
                ++made;
        } else if ((role == Role::Concord) or (role == Role::Navy)) {
            // a squad of that kind:  at your waypoint, or warping to you from the nearest one
            if ((role == Role::Navy) and (m_profile.homeRace >= Race::Count)) {
                why = "no empire navy in this system";
                break;
            }
            if (m_wpIDs.empty()) {
                why = "no waypoint (station or gate) for a squad";
                break;
            }
            Squad s;
            s.id = sAmbientMgr.NextGroupID();
            s.role = role;
            s.race = (role == Role::Navy ? m_profile.homeRace : Race::None);
            FormSquad(s);
            size_t nearest(0);
            double best(-1.0);
            for (size_t w = 0; w < m_wpIDs.size(); ++w) {
                const Node* pWp(FindNode(m_wpIDs[w]));
                double distance(pWp != nullptr ? pWp->pos.distance(here) : 1e30);
                if ((best < 0.0) or (distance < best)) {
                    best = distance;
                    nearest = w;
                }
            }
            s.wp = nearest;
            s.loiterUntil = now + AmbientRules::UniformMs(m_rng, sConfig.ambient.PatrolLoiterMin * 1000.0,
                                                          sConfig.ambient.PatrolLoiterMax * 1000.0);
            bool atWp(best <= BUBBLE_RADIUS_METERS);
            if (MaterialiseSquad(s, m_wpIDs[nearest], (atWp ? 0 : pShip->GetID()), NodeKind::Entity, now)) {
                made += static_cast<uint32>(s.npcIDs.size());
                m_squads.push_back(s);
            }
            break;
        } else if (role == Role::Miner) {
            const Node* pBelt(NodeAt(here, NodeKind::Belt));
            if ((pBelt == nullptr) or !BeltOpen(pShip->SysBubble())) {
                why = "miners need a belt grid with asteroids and no rats";
                break;
            }
            if (SpawnMiner(*pBelt, pShip->SysBubble(), false, now) != nullptr)
                ++made;
        } else if (role == Role::Criminal) {
            // at your gate or belt:  the whole group at once
            const Node* pNode(NodeAt(here, NodeKind::Gate));
            if (pNode == nullptr)
                pNode = NodeAt(here, NodeKind::Belt);
            if (pNode == nullptr) {
                why = "criminals need a gate or belt grid";
                break;
            }
            made += SpawnCriminals(*pNode, count, now);
            break;
        } else {
            why = std::string(what) + " can't be spawned";
            break;
        }
    }
    return made;
}

uint32 AmbientSystem::ClearAll()
{
    uint32 count(static_cast<uint32>(m_npcs.size()));
    std::vector<uint32> ids;
    for (auto& cur : m_npcs)
        ids.push_back(cur.first);
    for (auto id : ids)
        QueueRemoval(id, Removal::GM);
    m_virtual.clear();
    m_launches.clear();
    return count;
}
