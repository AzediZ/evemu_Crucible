
 /**
  * @name SimHaulers.cpp
  *   market simulation:  logistics and route risk (PLAN 6.5, 11 phase 4).  World's members for the haulers, the remote
  *   miners and their shipments.
  *
  *   hub traders (one per pair of hubs, and per regional node with its nearest hub) buy where a type is cheap and sell
  *   where it is dear when the gap pays for freight, the expected loss of the cargo and a margin:  they take a node's
  *   stock down to HaulerKeepShare of its target at most and fill the other's up to its target (at the internal price)
  *   and on into its bids (a glut, which takes its fair value down).  so the hubs' prices stay within a no-arbitrage
  *   band of each other, and production moves to where it is cheap (the buying is demand there).
  *
  *   the ores of low and null-sec (roidDistribution) are not in a high-sec node's belts:  a remote miner per node and
  *   band mines them at the node's site (the nearest station of the band), refines them there and ships the minerals in.
  *   it mines what the node's market needs of them, up to what pays:  its netback (what they sell for less freight and
  *   the expected loss on the route) against its reference (the calibration's prices at the default risk, which the
  *   observed prices carried), ^ RemoteSupplyElasticity.  more risk on the route:  less comes, the price rises until it
  *   pays again.  so the price of what comes from far and from dangerous space carries the trip
  *
  *   a shipment rolls its loss when it leaves (a hash of the seed and the trip, against the route's loss chance for the
  *   cargo's value);  a lost one is lost when it would have arrived:  the cargo is a goods sink (Goods::Transport), the
  *   hull is destroyed (Goods::Destroyed) and the actor buys another at its home node (industrial demand).  freight
  *   (FreightIskPerM3Jump, hull upkeep) is an isk sink.  goods on the way are the actor's at TransitLoc
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <set>
#include <sstream>

#include "market/sim/SimEconomy.h"
#include "market/sim/SimRng.h"
#include "market/sim/SimWorld.h"

namespace MktSim {

namespace {
    // hash purposes (SimWorld.cpp 1..10, SimActors.cpp 101..)
    enum : uint64_t {
        hTripLoss = 201, hRemoteMine, hRemoteHullLoss, hRemoteFitQty, hRemoteFitDrop
    };
    const double MinLocalFleet = 20.0;          // as PlanMining's MinFleet
    const double MinRemoteFleet = 10.0;
    const size_t MaxTripLines = 128;            // cargo lines in one hull
    const uint16_t GroupIndustrial = 28;
    const double DefaultCargoM3 = 5000.0;       // an industrial's hold when the static data has none
    const int64_t ShipEverySeconds = 12 * 3600; // a remote miner ships at least this often (when it has anything)
    const double RemoteSupplySlack = 0.005;     // remote supply at its reference netback:  the demand and this much over

    std::string Fmt(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    const char* BandName(uint8_t band)
    {
        switch (band) {
            case SecBand::High: return "high-sec";
            case SecBand::Low:  return "low-sec";
            case SecBand::Null: return "null-sec";
        }
        return "w-space";
    }
}

/************************************************************************************************************************
 * small helpers
 */

uint16_t World::MapLocation(uint16_t loc, const std::function<uint32_t(uint32_t)>& nodeMap)
{
    if (loc == TransitLoc)
        return loc;
    if (IsSiteLoc(loc))
        return SiteLoc(nodeMap(SiteNode(loc)), SiteBand(loc));
    return (uint16_t)nodeMap(loc);
}

double World::RemoteShare(uint32_t nodeID, uint16_t typeID, int band) const
{
    uint64_t key(((uint64_t)nodeID << 16) | typeID);
    if (band >= 0)
        key = (key << 2) | (uint64_t)(band & 3) | (1ULL << 63);
    std::map<uint64_t, double>::const_iterator itr = m_remoteShare.find(key);
    return (itr == m_remoteShare.end()) ? 0.0 : itr->second;
}

const Route* World::RemoteRoute(uint32_t nodeID, uint8_t band) const
{
    for (auto& cur : m_haulPlans)
        if ((cur.second.site != 0) and (cur.second.site == SiteLoc(nodeID, band)))
            return &cur.second.route[0];
    return nullptr;
}

const SiteInfo* World::Site(uint16_t loc) const
{
    for (auto& s : m_sites)
        if (s.loc == loc)
            return &s;
    return nullptr;
}

uint32_t World::LocSystem(uint16_t loc) const
{
    if (IsSiteLoc(loc)) {
        const SiteInfo* s(Site(loc));
        return (s == nullptr) ? 0 : s->station.systemID;
    }
    const NodeData* n(Node(loc));
    return (n == nullptr) ? 0 : n->systemID;
}

Units World::Transit(uint16_t to, uint16_t typeID) const
{
    std::map<uint32_t, Units>::const_iterator itr = m_transit.find(((uint32_t)to << 16) | typeID);
    return (itr == m_transit.end()) ? 0 : itr->second;
}

Units World::RemotePipeline(uint32_t nodeID, uint16_t typeID) const
{
    // what is coming:  on the way to the node (remote miners' and traders'), and what the node's remote miners hold at
    //  their sites and at the node
    Units q(Transit((uint16_t)nodeID, typeID));
    for (auto& cur : m_haulPlans) {
        if ((cur.second.site == 0) or (cur.second.nodeA != nodeID))
            continue;
        q += Held(cur.first, cur.second.site, typeID) + Held(cur.first, nodeID, typeID);
    }
    return q;
}

double World::UnitTransport(const Route& r, const TypeData& t, double price, const RiskParams& risk) const
{
    if (!r.ok)
        return 1e300;
    // freight for its m3, and its share of a full hull's expected loss (high-sec ganks follow the hull's value)
    return m_p.freightPerM3Jump * Logi::PackagedVolume(t) * (double)r.jumps
         + Logi::TripLoss(risk, r.bandJumps, m_p.haulerMaxTripValue) * price;
}

uint32_t World::HullsFree(const Actor& a, const HaulPlan& hp, int64_t t) const
{
    Units held(Held(a.actorID, hp.nodeA, hp.hullTypeID));
    Units busy(0);
    for (auto& b : hp.busy)
        if (b.second > t)
            ++busy;
    return (uint32_t)std::max<Units>(0, held - busy);
}

/************************************************************************************************************************
 * building
 */

void World::BuildRoutes()
{
    m_routes.reset(new RouteMap());
    m_routes->Build(m_static, m_p.risk, m_p.routeRiskScale);
    m_sites.clear();
    if (m_routes->Systems() == 0)
        return;
    std::vector<std::pair<uint32_t, uint32_t>> candidates;
    std::map<uint32_t, const MapStation*> byID;
    for (auto& st : m_static.stations) {
        candidates.push_back(std::make_pair(st.stationID, st.systemID));
        byID[st.stationID] = &st;
    }
    /* a node's sites (PLAN 6.1 production sites):  the nearest station with a refinery in each band less secure than the
     *  node's own */
    for (auto& node : m_static.nodes) {
        if (!m_routes->Has(node.systemID))
            continue;
        uint8_t own(m_routes->BandOf(node.systemID));
        for (uint8_t band = own + 1; band <= SecBand::Null; ++band) {
            uint16_t jumps(0);
            uint32_t stationID(m_routes->Nearest(node.systemID, band, candidates, &jumps));
            if (stationID == 0)
                continue;
            SiteInfo s;
            s.loc = SiteLoc(node.nodeID, band);
            s.nodeID = node.nodeID;
            s.band = band;
            s.station = *byID[stationID];
            s.jumps = jumps;
            m_sites.push_back(s);
        }
    }
}

namespace {
    // the hauling hull of a node:  its most traded industrial
    uint16_t PickHull(const StaticData& data, const std::vector<Cell>& cells, uint32_t nodeID, double cargoFactor, double* cargo)
    {
        uint16_t best(0);
        double top(-1.0);
        for (auto& c : cells) {
            if (c.nodeID != nodeID)
                continue;
            const TypeData& t(*data.Type(c.typeID));
            if ((t.categoryID != Category::Ship) or (t.groupID != GroupIndustrial))
                continue;
            if (c.d0 > top) {
                top = c.d0;
                best = t.typeID;
            }
        }
        if ((best != 0) and (cargo != nullptr)) {
            double hold(data.Type(best)->capacity);
            *cargo = ((hold > 0.0) ? hold : DefaultCargoM3) * std::max(1.0, cargoFactor);
        }
        return best;
    }
}

void World::SplitMining(uint32_t nodeID, uint32_t minerID, const std::vector<double>& mix)
{
    /* the node's reference mix (PlanMining) has ores whose belts are only in less secure space:  a remote miner per band
     *  mines those at the node's site of the band;  the node's own miner keeps the rest.  nothing changes for a node
     *  without sites or a hauling hull (its miner mines everything, as in phase 3) */
    const NodeData& node(*Node(nodeID));
    if (!m_routes or !m_routes->Has(node.systemID))
        return;
    if (PickHull(m_static, m_cells, nodeID, m_p.haulerCargoFactor, nullptr) == 0)
        return;
    MinerPlan& plan(m_minerPlans[minerID]);
    uint8_t own(m_routes->BandOf(node.systemID));
    uint8_t safeOrBetter((uint8_t)((1 << (own + 1)) - 1));
    std::vector<int> where(plan.ores.size(), -1);
    bool any(false);
    for (size_t o = 0; o < plan.ores.size(); ++o) {
        if (!(mix[o] > 0.0))
            continue;
        uint8_t mask(m_static.OreBands(*m_static.Type(m_cells[plan.ores[o]].typeID)));
        if ((mask & safeOrBetter) != 0)
            continue;
        for (uint8_t b = own + 1; b <= SecBand::Null; ++b)
            if (((mask & (1 << b)) != 0) and (Site(SiteLoc(nodeID, b)) != nullptr)) {
                where[o] = b;
                any = true;
                break;
            }
    }
    if (!any)
        return;

    size_t outs(plan.outputs.size());
    std::vector<double> total(outs, 0.0);
    for (size_t o = 0; o < plan.ores.size(); ++o)
        for (size_t m = 0; m < outs; ++m)
            total[m] += plan.yield[o][m] * mix[o];
    double fleetHulls(0.0);
    for (auto& h : plan.fleet)
        fleetHulls += h.second;
    // a plan for the ores of a band (-1:  the node's own), its fleet sized like PlanMining's
    auto part = [&](int band, std::vector<double>& share, double& m3) -> MinerPlan {
        MinerPlan p(plan);
        m3 = 0.0;
        p.valuePerDay = 0.0;
        for (size_t o = 0; o < p.ores.size(); ++o) {
            if (where[o] != band)
                p.mix[o] = 0.0;
            m3 += p.mix[o];
            p.valuePerDay += p.mix[o] * p.value0[o];
        }
        share.assign(outs, 0.0);
        p.supply.assign(outs, 0.0);
        for (size_t o = 0; o < p.ores.size(); ++o)
            for (size_t m = 0; m < outs; ++m)
                p.supply[m] += p.yield[o][m] * p.mix[o];
        for (size_t m = 0; m < outs; ++m) {
            share[m] = (total[m] > 0.0) ? p.supply[m] / total[m] : 0.0;
            p.need[m] = plan.need[m] * share[m];
        }
        // a remote fleet has twice the slack:  how much of it mines is the netback's call (RemoteMinerStep)
        double capacity(m3 * (1.0 + ((band < 0) ? 1.0 : 2.0) * m_p.miningSlack));
        double size(std::max((band < 0) ? MinLocalFleet : MinRemoteFleet, std::round(capacity / std::max(1.0, m_p.miningM3PerHullDay))));
        p.m3PerHull = capacity / size;
        for (auto& h : p.fleet)
            h.second = size * ((fleetHulls > 0.0) ? h.second / fleetHulls : 1.0 / p.fleet.size());
        p.mined.assign(p.ores.size(), 0.0);
        return p;
    };
    for (uint8_t b = own + 1; b <= SecBand::Null; ++b) {
        bool has(false);
        for (auto w : where)
            has = has or (w == b);
        if (!has)
            continue;
        std::vector<double> share;
        double m3(0.0);
        MinerPlan rp(part(b, share, m3));
        Actor a = Actor();
        a.actorID = (uint32_t)m_actors.size() + 1;
        a.kind = ActorKind::RemoteMiner;
        a.sector = b;
        a.ownerCorpID = m_static.ActorCorp(nodeID, ActorKind::Miner, 0);
        a.regionID = node.regionID;
        a.nodeID = nodeID;
        a.capacity = m3 * (1.0 + 2.0 * m_p.miningSlack);
        m_actors.push_back(a);
        HaulPlan hp;
        hp.nodeA = nodeID;
        hp.site = SiteLoc(nodeID, b);
        hp.share = share;
        hp.refM3 = m3;
        m_haulPlans[a.actorID] = hp;
        for (size_t m = 0; m < outs; ++m) {
            uint64_t key(((uint64_t)nodeID << 16) | m_cells[rp.outputs[m]].typeID);
            m_remoteShare[key] += share[m];
            m_remoteShare[(key << 2) | b | (1ULL << 63)] += share[m];
        }
        m_minerPlans[a.actorID] = rp;
    }
    // the node's own miner keeps what is left
    std::vector<double> share;
    double m3(0.0);
    MinerPlan local(part(-1, share, m3));
    m_minerPlans[minerID] = local;
    m_actors[minerID - 1].capacity = m3 * (1.0 + m_p.miningSlack);
}

void World::BuildHaulers()
{
    if (!m_routes or (m_routes->Systems() == 0))
        return;
    double maxValue(std::max(1e6, m_p.haulerMaxTripValue));

    // the remote miners' hauling:  their hull, the route from the site, their reference netback and fleet
    for (auto& cur : m_haulPlans) {
        HaulPlan& hp(cur.second);
        if (hp.site == 0)
            continue;
        Actor& a(m_actors[cur.first - 1]);
        const SiteInfo* site(Site(hp.site));
        const NodeData* node(Node(a.nodeID));
        hp.hullTypeID = PickHull(m_static, m_cells, a.nodeID, m_p.haulerCargoFactor, &hp.cargoM3);
        const MinerPlan& plan(m_minerPlans[cur.first]);
        double m3(0.0), value(0.0);
        for (size_t m = 0; m < plan.outputs.size(); ++m) {
            const Cell& mc(m_cells[plan.outputs[m]]);
            m3 += plan.supply[m] * Logi::PackagedVolume(*m_static.Type(mc.typeID));
            value += plan.supply[m] * mc.refLocal;
        }
        hp.route[0] = m_routes->Best(site->station.systemID, node->systemID, hp.cargoM3, maxValue, m_p.freightPerM3Jump, m_p.risk);
        // hulls:  the trips a day (by value and by m3), each away there and back
        double trips(std::max(1.0, std::max(value / maxValue, m3 / std::max(1.0, hp.cargoM3))));
        double hours(2.0 * ((double)hp.route[0].jumps * m_p.secondsPerJump + m_p.dockSeconds) / 3600.0);
        hp.fleet = std::max(2.0, std::ceil(1.5 * trips * std::max(hours, 1.0) / 24.0) + 1.0);
    }

    /* hub traders (PLAN 6.4 hauler/trader per hub pair and regional route set):  every pair of hubs, and each regional
     *  node with its nearest hub */
    std::vector<uint32_t> hubs, regional;
    for (auto& n : m_static.nodes)
        if (m_routes->Has(n.systemID))
            ((n.kind == NodeKind::Hub) ? hubs : regional).push_back(n.nodeID);
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    for (size_t i = 0; i < hubs.size(); ++i)
        for (size_t j = i + 1; j < hubs.size(); ++j)
            pairs.push_back(std::make_pair(hubs[i], hubs[j]));
    for (auto r : regional) {
        uint32_t best(0);
        uint16_t jumps(0xFFFF);
        for (auto h : hubs) {
            const Route& route(m_routes->Get(Node(r)->systemID, Node(h)->systemID, 0));
            if (route.ok and (route.jumps < jumps)) {
                jumps = route.jumps;
                best = h;
            }
        }
        if (best != 0)
            pairs.push_back(std::make_pair(std::min(best, r), std::max(best, r)));
    }
    for (auto& pr : pairs) {
        const NodeData& na(*Node(pr.first));
        const NodeData& nb(*Node(pr.second));
        HaulPlan hp;
        hp.nodeA = pr.first;
        hp.nodeB = pr.second;
        hp.hullTypeID = PickHull(m_static, m_cells, pr.first, m_p.haulerCargoFactor, &hp.cargoM3);
        if (hp.hullTypeID == 0)
            continue;
        hp.route[0] = m_routes->Best(na.systemID, nb.systemID, hp.cargoM3, maxValue, m_p.freightPerM3Jump, m_p.risk);
        hp.route[1] = m_routes->Best(nb.systemID, na.systemID, hp.cargoM3, maxValue, m_p.freightPerM3Jump, m_p.risk);
        if (!hp.route[0].ok or !hp.route[1].ok)
            continue;
        for (auto& c : m_cells) {
            if ((c.nodeID != pr.first) or ((c.flags & CellFlag::Disabled) != 0))
                continue;
            std::map<uint64_t, uint32_t>::const_iterator other = m_cellIndex.find(((uint64_t)pr.second << 16) | c.typeID);
            if ((other == m_cellIndex.end()) or ((m_cells[other->second].flags & CellFlag::Disabled) != 0))
                continue;
            double m3(Logi::PackagedVolume(*m_static.Type(c.typeID)));
            if (m3 > hp.cargoM3)
                continue;           // doesn't fit in the hull
            hp.pairs.push_back(std::make_pair(c.index, other->second));
            hp.pairM3.push_back(m3);
        }
        hp.fleet = std::max(1, m_p.haulerFleet);
        Actor a = Actor();
        a.actorID = (uint32_t)m_actors.size() + 1;
        a.kind = ActorKind::Hauler;
        a.sector = (uint8_t)pr.second;          // its other node
        a.ownerCorpID = m_static.ActorCorp(pr.first, ActorKind::Hauler, 0);
        a.regionID = na.regionID;
        a.nodeID = pr.first;
        a.capacity = hp.fleet;
        // working money:  half its hulls full
        a.target = hp.fleet * maxValue * 0.5 * 100.0;
        m_actors.push_back(a);
        m_haulPlans[a.actorID] = hp;
    }
}

void World::SeedHaulers(uint32_t nodeID, bool freshOnly)
{
    // the hauling hulls at the start (a one-off goods faucet, like the miners' fleets).  freshOnly:  actors with no stock
    std::set<uint32_t> stocked;
    if (freshOnly)
        for (auto& cur : m_ledger.Stocks())
            if ((cur.second.qty + cur.second.reserved) > 0)
                stocked.insert(Ledger::KeyActor(cur.first));
    for (auto& cur : m_haulPlans) {
        const Actor& a(m_actors[cur.first - 1]);
        const HaulPlan& hp(cur.second);
        if (((nodeID != 0) and (a.nodeID != nodeID)) or (a.state != 0) or (hp.hullTypeID == 0))
            continue;
        if (a.kind == ActorKind::Hauler ? (stocked.count(a.actorID) > 0) : (freshOnly and (Held(a.actorID, hp.nodeA, hp.hullTypeID) > 0)))
            continue;
        Units want((Units)std::llround(hp.fleet)), have(Held(a.actorID, hp.nodeA, hp.hullTypeID));
        if (want > have)
            GoodsTo(a.actorID, hp.nodeA, hp.hullTypeID, want - have, Goods::Seed);
    }
}

/************************************************************************************************************************
 * shipments
 */

bool World::Dispatch(Actor& a, HaulPlan& hp, uint16_t from, uint16_t to, const Route& route, std::vector<SimTrip::Line>& lines, int64_t t)
{
    if (!route.ok)
        return false;
    SimTrip trip;
    double m3(0.0);
    Cents value(0);
    for (auto& l : lines) {
        if ((l.qty <= 0) or !m_ledger.Relocate(a.actorID, from, TransitLoc, l.typeID, l.qty))
            continue;
        trip.lines.push_back(l);
        m3 += (double)l.qty * Logi::PackagedVolume(*m_static.Type(l.typeID));
        value += std::max<Cents>(0, l.value);
    }
    if (trip.lines.empty())
        return false;
    trip.tripID = m_nextTripID++;
    trip.actorID = a.actorID;
    trip.from = from;
    trip.to = to;
    trip.hullTypeID = hp.hullTypeID;
    trip.depart = t;
    int64_t leg((int64_t)std::llround((double)route.jumps * m_p.secondsPerJump + m_p.dockSeconds));
    trip.arrive = t + std::max<int64_t>(60, leg);
    // the loss is rolled now (PLAN 6.5), against the chance for this cargo on this route
    double p(Logi::TripLoss(m_p.risk, route.bandJumps, Econ::ToIsk(value)));
    trip.lossRoll = Rng::Uniform(Rng::Hash64(m_p.seed, a.actorID, trip.tripID, hTripLoss));
    trip.lost = (trip.lossRoll < p);
    for (auto& l : trip.lines)
        m_transit[((uint32_t)to << 16) | l.typeID] += l.qty;
    // freight:  the hull's upkeep for the m3 and jumps (a sink)
    m_ledger.Sink(a.actorID, Econ::ToCents(m_p.freightPerM3Jump * m3 * (double)route.jumps), Isk::Freight, false);
    hp.busy[trip.tripID] = trip.arrive + leg;   // and back home
    m_due.push(Due{trip.arrive, DueTrip, trip.tripID});
    m_trips[trip.tripID] = trip;
    m_tripsChanged = true;
    return true;
}

void World::ArriveTrip(uint32_t tripID, int64_t t)
{
    std::map<uint32_t, SimTrip>::iterator itr = m_trips.find(tripID);
    if ((itr == m_trips.end()) or (itr->second.arrive > t))
        return;
    SimTrip trip(itr->second);
    m_trips.erase(itr);
    m_tripsChanged = true;
    for (auto& l : trip.lines) {
        Units& q(m_transit[((uint32_t)trip.to << 16) | l.typeID]);
        q = std::max<Units>(0, q - l.qty);
    }
    if ((trip.actorID == 0) or (trip.actorID > m_actors.size()))
        return;
    Actor& a(m_actors[trip.actorID - 1]);
    std::map<uint32_t, HaulPlan>::iterator pitr = m_haulPlans.find(trip.actorID);
    if (trip.lost) {
        // the cargo is gone (a goods sink), and the hull with it:  the actor buys another
        for (auto& l : trip.lines) {
            Units q(std::min<Units>(l.qty, Held(trip.actorID, TransitLoc, l.typeID)));
            m_ledger.GoodsOut(trip.actorID, TransitLoc, l.typeID, q, Goods::Transport, false);
            m_transportLost[l.typeID] += q;
        }
        if (pitr != m_haulPlans.end()) {
            pitr->second.busy.erase(tripID);
            if (Held(trip.actorID, pitr->second.nodeA, trip.hullTypeID) > 0)
                m_ledger.GoodsOut(trip.actorID, (uint16_t)pitr->second.nodeA, trip.hullTypeID, 1, Goods::Destroyed, false);
        }
        ++m_tripsLost;
        return;
    }
    ++m_tripsArrived;
    for (auto& l : trip.lines) {
        Units q(std::min<Units>(l.qty, Held(trip.actorID, TransitLoc, l.typeID)));
        if (q > 0) {
            m_ledger.Relocate(trip.actorID, TransitLoc, trip.to, l.typeID, q);
            m_holdings[trip.actorID].insert(l.typeID);
        }
    }
    if (IsSiteLoc(trip.to))
        return;
    // delivered:  sold to the node's market maker (to its target, then into its bids)
    for (auto& l : trip.lines) {
        double hold(0.0);
        if (a.kind == ActorKind::RemoteMiner) {
            const MinerPlan& plan(m_minerPlans[a.actorID]);
            for (size_t m = 0; m < plan.outputs.size(); ++m)
                if (m_cells[plan.outputs[m]].typeID == l.typeID)
                    hold = std::ceil(std::max(0.1, m_p.holdDays) * plan.supply[m]);
        }
        SellHeld(trip.actorID, trip.to, l.typeID, hold, t);
    }
}

void World::SellHeld(uint32_t actorID, uint32_t nodeID, uint16_t typeID, double holdUnits, int64_t t)
{
    Cell* c(FindCell(nodeID, typeID));
    Units have(Held(actorID, nodeID, typeID));
    if ((c == nullptr) or (have <= 0))
        return;
    have -= SellToMaker(actorID, *c, have, 0, t);
    Units hold((Units)std::ceil(std::max(0.0, holdUnits)));
    if (have > hold)
        SellToBids(actorID, *c, have - hold, t);
}

void World::BuyHulls(Actor& a, HaulPlan& hp, int64_t t)
{
    if (hp.hullTypeID == 0)
        return;
    Cell* hc(FindCell(hp.nodeA, hp.hullTypeID));
    if (hc == nullptr)
        return;
    Units want((Units)std::llround(hp.fleet)), have(Held(a.actorID, hp.nodeA, hp.hullTypeID));
    if (want <= have)
        return;
    Units got(BuyAtNode(a.actorID, *hc, want - have, InternalBuy(*hc), Spare(*hc, want - have), t));
    m_haulHullsBought[hp.hullTypeID] += got;
}

/************************************************************************************************************************
 * the hub traders (hourly)
 */

void World::HaulerStep(Actor& a, int64_t t)
{
    std::map<uint32_t, HaulPlan>::iterator pitr = m_haulPlans.find(a.actorID);
    if (pitr == m_haulPlans.end())
        return;
    HaulPlan& hp(pitr->second);
    for (std::map<uint32_t, int64_t>::iterator itr = hp.busy.begin(); itr != hp.busy.end(); ) {
        if ((itr->second <= t) and (m_trips.find(itr->first) == m_trips.end()))
            itr = hp.busy.erase(itr);
        else
            ++itr;
    }
    // what is left over from earlier deliveries
    std::map<uint32_t, std::set<uint16_t>>::const_iterator hitr = m_holdings.find(a.actorID);
    if (hitr != m_holdings.end()) {
        std::set<uint16_t> types(hitr->second);
        for (auto typeID : types) {
            if (typeID == hp.hullTypeID)
                continue;
            SellHeld(a.actorID, hp.nodeA, typeID, 0.0, t);
            SellHeld(a.actorID, hp.nodeB, typeID, 0.0, t);
        }
    }
    BuyHulls(a, hp, t);
    hp.shortOfHulls = false;
    // one direction per half hour step (the trader steps every 1800s:  A -> B and B -> A take turns)
    HaulDirection(a, hp, ((t / 1800) % 2) == 0, t);
    // profitable hauls waited for hulls and the money is there:  one more (PLAN 6.4 haulers buy capacity)
    if (hp.shortOfHulls and ((double)m_ledger.MoneyOf(a.actorID) > 1.5 * a.target) and (hp.fleet < 4.0 * std::max(1, m_p.haulerFleet))) {
        hp.fleet += 1.0;
        a.capacity = hp.fleet;
    }
}

void World::HaulDirection(Actor& a, HaulPlan& hp, bool aToB, int64_t t)
{
    uint32_t from(aToB ? hp.nodeA : hp.nodeB), to(aToB ? hp.nodeB : hp.nodeA);
    const Route& route(hp.route[aToB ? 0 : 1]);
    if (!route.ok)
        return;
    double margin(std::max(0.0, m_p.haulerMargin));
    double maxValue(std::max(1e6, m_p.haulerMaxTripValue));
    double share(std::min(1.0, std::max(0.0, m_p.internalSpreadShare)));
    struct Cand {
        uint32_t cx, cy;
        Units    q;
        double   m3;
        double   buy;
        double   profit;    // per unit, expected
        double   margin;
    };
    std::vector<Cand> cands;
    // a full hull's chance to be lost on the route (UnitTransport's share of it per unit)
    double loss(Logi::TripLoss(m_p.risk, route.bandJumps, maxValue));
    for (size_t p = 0; p < hp.pairs.size(); ++p) {
        const Cell& cx(m_cells[aToB ? hp.pairs[p].first : hp.pairs[p].second]);
        const Cell& cy(m_cells[aToB ? hp.pairs[p].second : hp.pairs[p].first]);
        double buy(Econ::ToIsk(InternalBuy(cx)));
        // the best it could sell at:  under V or V*, whichever is higher
        if (!(std::max(cy.vStar, Mid(cy)) > buy * (1.0 + margin)))
            continue;
        double m3(hp.pairM3[p]);
        double transport(m_p.freightPerM3Jump * m3 * (double)route.jumps + loss * buy);
        /* how much:  its buying takes X's stock down and its selling takes Y's up, and the stock moves the fair values
         *  (PLAN 6.2 stock pressure):  as much as still pays at the fair values the trade leaves behind.  so a haul never
         *  overshoots into a gap the other way (and back) */
        double sx((double)MakerStock(cx)), sy((double)(MakerStock(cy) + Transit((uint16_t)to, cy.typeID) + Held(a.actorID, to, cy.typeID)));
        double ax(std::exp(cx.lnAnchor)), ay(std::exp(cy.lnAnchor));
        /* Y is filled against its calibrated target (its demand at the calibration, not today's):  a glut's low price
         *  brings demand that raises Y's target, which would take more, which lowers the price further, until the demand
         *  falls back and leaves the stock at the clamp */
        double ty(std::min(cy.targetStock, std::max(cy.dRef * cy.coverDays, 2.0 * cy.lot)));
        auto buyAt = [&](double q) {
            return Econ::TargetValue(ax, sx - q + cx.lot, cx.targetStock + cx.lot, cx.theta, m_p.kMax) * (1.0 + share * cx.h);
        };
        auto sellAt = [&](double q) {
            double v(Econ::TargetValue(ay, sy + q + cy.lot, ty + cy.lot, cy.theta, m_p.kMax));
            return v * ((sy + q <= ty) ? (1.0 - share * cy.h) : (1.0 - cy.h));
        };
        auto pays = [&](double q) {
            return sellAt(q) >= (std::max(buy, buyAt(q)) + transport) * (1.0 + margin);
        };
        double hi(std::min(sx - std::max(0.0, m_p.haulerKeepShare) * cx.targetStock, m_p.bidStockFactor * ty - sy));
        hi = std::floor(hi);
        if ((hi < 1.0) or !pays(1.0))
            continue;
        double lo(1.0);
        if (pays(hi)) {
            lo = hi;
        } else {
            for (int k = 0; (k < 30) and (hi - lo > 1.0); ++k) {
                double mid(std::floor(0.5 * (lo + hi)));
                if (pays(mid))
                    lo = mid;
                else
                    hi = mid;
            }
        }
        double q(lo);
        double unitCost(std::max(buy, buyAt(q)) + transport);
        double unitSell(0.5 * (sellAt(1.0) + sellAt(q)));
        cands.push_back(Cand{cx.index, cy.index, (Units)q, m3, buy, unitSell - unitCost, unitSell / unitCost - 1.0});
    }
    if (cands.empty())
        return;
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        return (x.margin != y.margin) ? (x.margin > y.margin) : (x.cx < y.cx);
    });
    // a run is worth a hull's while when it earns at least 0.05% of a full hull's value (250k ISK at 5e8)
    double minProfit(0.0005 * maxValue);
    uint32_t idle(HullsFree(a, hp, t));
    size_t i(0);
    while (i < cands.size()) {
        if (idle == 0) {
            hp.shortOfHulls = true;
            return;
        }
        // a hull's load:  the best margins first, as far as its m3, the value cap and its lines go
        double m3(0.0), value(0.0), profit(0.0);
        std::vector<std::pair<size_t, Units>> load;
        for (; (i < cands.size()) and (load.size() < MaxTripLines); ++i) {
            const Cand& c(cands[i]);
            double byM3(std::floor((hp.cargoM3 - m3) / std::max(1e-9, c.m3)));
            double byValue(std::floor((maxValue - value) / std::max(0.01, c.buy)));
            Units q(std::min<Units>(c.q, (Units)std::max(0.0, std::min(byM3, byValue))));
            if (q <= 0) {
                if ((hp.cargoM3 - m3 < 1.0) or (maxValue - value < 1e5))
                    break;      // full
                continue;
            }
            load.push_back(std::make_pair(i, q));
            m3 += (double)q * c.m3;
            value += (double)q * c.buy;
            profit += (double)q * c.profit;
        }
        // the rest isn't worth a run either (the loads only get worse)
        if (load.empty() or (profit < minProfit))
            break;
        std::vector<SimTrip::Line> lines;
        for (auto& l : load) {
            const Cand& c(cands[l.first]);
            Cell& cx(m_cells[c.cx]);
            Cents price(InternalBuy(cx));
            Units got(BuyAtNode(a.actorID, cx, l.second, price, l.second, t));
            if (got <= 0)
                continue;
            SimTrip::Line line;
            line.typeID = cx.typeID;
            line.qty = got;
            line.value = Econ::MulCents(price, got);
            lines.push_back(line);
        }
        if (Dispatch(a, hp, (uint16_t)from, (uint16_t)to, route, lines, t))
            --idle;
    }
}

/************************************************************************************************************************
 * the remote miners (hourly)
 */

double World::RemoteResponse(const HaulPlan& hp, const Cell& mc) const
{
    /* a mineral's netback:  at the fair value its stock is heading to, less freight and the expected loss at the
     *  configured risk, against the same at its reference price and the default risk (which the calibrated prices
     *  carried), ^ RemoteSupplyElasticity */
    RiskParams reference;
    double share(std::min(1.0, std::max(0.0, m_p.internalSpreadShare)));
    const TypeData& t(*m_static.Type(mc.typeID));
    double sell(ExpectSell(mc)), sellRef(mc.refLocal * (1.0 - share * mc.h));
    double now(sell - UnitTransport(hp.route[0], t, sell, m_p.risk));
    double ref(sellRef - UnitTransport(hp.route[0], t, sellRef, reference));
    if (!(ref > 0.0))
        return 1.0;
    double r(std::pow(std::max(1e-6, now / ref), std::max(0.0, m_p.remoteSupplyElasticity)));
    return std::min(4.0, std::max(0.25, r));
}

Units World::RefineAtSite(uint32_t actorID, const SiteInfo& site, uint16_t typeID, Units q)
{
    const TypeData* t(m_static.Type(typeID));
    if ((t == nullptr) or t->refine.empty())
        return 0;
    q = std::min(q, Held(actorID, site.loc, typeID));
    Prod::RefineResult r;
    Prod::Refine(*t, q, Prod::RefineYield(site.station.refineEfficiency), std::min(1.0, std::max(0.0, (double)site.station.refineTake)), r);
    if ((r.used <= 0) or !m_ledger.GoodsOut(actorID, site.loc, typeID, r.used, Goods::Materials, false))
        return 0;
    for (auto& out : r.gross)
        GoodsTo(actorID, site.loc, out.first, out.second, Goods::Refined);
    for (auto& tax : r.tax)
        m_ledger.GoodsOut(actorID, site.loc, tax.first, tax.second, Goods::RefineTax, false);
    // what the node doesn't trade isn't worth the trip:  it stays with the station (counted with its take)
    for (auto& out : r.gross) {
        if (m_cellIndex.find(((uint64_t)site.nodeID << 16) | out.first) != m_cellIndex.end())
            continue;
        Units left(Held(actorID, site.loc, out.first));
        if (left > 0)
            m_ledger.GoodsOut(actorID, site.loc, out.first, left, Goods::RefineTax, false);
    }
    return r.used;
}

void World::RemoteMinerStep(Actor& a, int64_t t, double dt)
{
    std::map<uint32_t, MinerPlan>::iterator mitr = m_minerPlans.find(a.actorID);
    std::map<uint32_t, HaulPlan>::iterator hitr = m_haulPlans.find(a.actorID);
    if ((mitr == m_minerPlans.end()) or (hitr == m_haulPlans.end()))
        return;
    MinerPlan& plan(mitr->second);
    HaulPlan& hp(hitr->second);
    const SiteInfo* site(Site(hp.site));
    if (site == nullptr)
        return;
    uint32_t node(a.nodeID), loot(NodeActor(node, ActorKind::LootSource));
    for (std::map<uint32_t, int64_t>::iterator itr = hp.busy.begin(); itr != hp.busy.end(); ) {
        if ((itr->second <= t) and (m_trips.find(itr->first) == m_trips.end()))
            itr = hp.busy.erase(itr);
        else
            ++itr;
    }

    // 1.  hulls lost since the last step, with their fits (as MinerStep:  what drops goes to the node's loot source)
    uint64_t salt(0);
    double fleet(0.0);
    for (auto& h : plan.fleet)
        fleet += h.second;
    for (auto& h : plan.fleet) {
        Units have(Held(a.actorID, node, h.first));
        if ((have <= 0) or !(plan.lossPerHullDay > 0.0) or !(dt > 0.0))
            continue;
        Units lost(std::min<Units>(have, (Units)Rng::Poisson((double)have * plan.lossPerHullDay * dt,
                                                               Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hRemoteHullLoss, h.first))));
        if (lost <= 0)
            continue;
        m_ledger.GoodsOut(a.actorID, (uint16_t)node, h.first, lost, Goods::Destroyed, false);
        std::map<uint16_t, std::vector<FitItem>>::const_iterator fit = m_static.fits.find(h.first);
        if (fit == m_static.fits.end())
            continue;
        for (auto& f : fit->second) {
            if (FindCell(node, f.typeID) == nullptr)
                continue;
            Units q(Rng::RoundRandom((double)lost * f.qtyPerLoss, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hRemoteFitQty, ++salt)));
            if (q <= 0)
                continue;
            plan.needs[f.typeID] = std::min<Units>(plan.needs[f.typeID] + q, (Units)std::ceil(fleet * f.qtyPerLoss) + 1);
            Units drop(Rng::RoundRandom((double)q * (1.0 - f.destroyedShare), Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hRemoteFitDrop, salt)));
            if ((drop > 0) and (loot != 0))
                GoodsTo(loot, node, f.typeID, drop, Goods::Dropped);
        }
    }
    // 2.  replacements:  mining hulls back to the fleet (fit items fitted at once), and the hauling hulls
    Cents money(m_ledger.MoneyOf(a.actorID));
    double expand((money > 2.0 * a.target) ? 1.2 : ((money < 0.25 * a.target) ? 0.8 : 1.0));
    std::set<uint16_t> hullTypes;
    for (auto& h : plan.fleet) {
        Units want((Units)std::llround(h.second * expand)), have(Held(a.actorID, node, h.first));
        plan.needs[h.first] = std::max<Units>(0, want - have);
        hullTypes.insert(h.first);
    }
    for (std::map<uint16_t, Units>::iterator itr = plan.needs.begin(); itr != plan.needs.end(); ) {
        Cell* c(FindCell(node, itr->first));
        if ((c == nullptr) or (itr->second <= 0)) {
            itr = plan.needs.erase(itr);
            continue;
        }
        Units got(BuyAtNode(a.actorID, *c, itr->second, InternalBuy(*c), Spare(*c, itr->second), t));
        if ((got > 0) and (hullTypes.count(itr->first) == 0))
            m_ledger.GoodsOut(a.actorID, (uint16_t)node, itr->first, got, Goods::Consumption, false);
        itr->second -= got;
        ++itr;
    }
    BuyHulls(a, hp, t);

    /* 3.  mining at the site:  its share of what each mineral needs (the node's demand and refilling its market maker,
     *  less what is already coming), up to what its fleet does and what pays (the netback response) */
    double hulls(0.0);
    for (auto& h : plan.fleet)
        hulls += (double)Held(a.actorID, node, h.first);
    /* what pays (the supply curve of each mineral it mines):  its share of the node's demand for the mineral (in the long
     *  run more pilots mine out there at the same netback), a little more to refill the shelves, x the mineral's netback
     *  response (in the short run its price has to pay for the trip).  what it is short of shows in the stock and the
     *  price rises until the trip pays again.  a co-product (its glut) doesn't count */
    std::vector<double> need(plan.outputs.size(), 0.0);
    for (size_t m = 0; m < plan.outputs.size(); ++m) {
        const Cell& mc(m_cells[plan.outputs[m]]);
        double share((m < hp.share.size()) ? hp.share[m] : 0.0);
        if (!(share > 0.0))
            continue;
        Units mine(Held(a.actorID, hp.site, mc.typeID) + Held(a.actorID, node, mc.typeID));
        for (auto& cur : m_trips)
            if (cur.second.actorID == a.actorID)
                for (auto& l : cur.second.lines)
                    if (l.typeID == mc.typeID)
                        mine += l.qty;
        double gap(share * (mc.targetStock - (double)MakerStock(mc)) - (double)mine);
        need[m] = std::max(0.0, share * mc.dBar + gap / std::max(0.1, m_p.restockDays));
        need[m] = std::min(need[m], share * mc.dBar * (1.0 + RemoteSupplySlack) * RemoteResponse(hp, mc));
        need[m] = std::max(need[m], 0.05 * plan.need[m]);
    }
    double capacity(hulls * plan.m3PerHull * MiningShockOf(t));
    std::vector<bool> allowed(plan.ores.size(), true);
    for (size_t o = 0; o < plan.ores.size(); ++o)
        allowed[o] = (plan.mix[o] > 0.0);
    std::vector<double> x(Prod::CoverMix(plan.yield, need, capacity, allowed));
    plan.lastNeed = need;
    double wsum(0.0);
    for (auto v : x)
        wsum += v;
    plan.lastWanted = wsum;
    plan.lastMined = wsum;
    plan.lastCapacity = capacity;
    plan.lastRel.assign(plan.ores.size(), 0.0);
    if ((wsum > 0.0) and (dt > 0.0)) {
        for (size_t o = 0; o < plan.ores.size(); ++o) {
            if (plan.mix[o] > 0.0)
                plan.lastRel[o] = x[o] / plan.mix[o];
            if (!(x[o] > 0.0))
                continue;
            const Cell& oc(m_cells[plan.ores[o]]);
            const TypeData& ot(*m_static.Type(oc.typeID));
            Units u(Rng::RoundRandom(x[o] * dt / ot.volume, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hRemoteMine, oc.typeID)));
            if (u <= 0)
                continue;
            GoodsTo(a.actorID, hp.site, oc.typeID, u, Goods::Mined);
            m_mined[oc.typeID] += u;
            if (plan.mined.size() == plan.ores.size())
                plan.mined[o] += (double)u * ot.volume;
        }
    }
    // 4.  refined at the site's station
    for (auto idx : plan.ores) {
        uint16_t ore(m_cells[idx].typeID);
        Units have(Held(a.actorID, hp.site, ore));
        if (have > 0)
            RefineAtSite(a.actorID, *site, ore, have);
    }

    /* 5.  shipped to the node:  a hull's load at a time (its m3 and the value cap), when there is a good load or it has
     *  waited long enough */
    double maxValue(std::max(1e6, m_p.haulerMaxTripValue));
    std::vector<SimTrip::Line> waiting;
    double value(0.0);
    for (size_t m = 0; m < plan.outputs.size(); ++m) {
        const Cell& mc(m_cells[plan.outputs[m]]);
        Units have(Held(a.actorID, hp.site, mc.typeID));
        if (have <= 0)
            continue;
        SimTrip::Line l;
        l.typeID = mc.typeID;
        l.qty = have;
        l.value = Econ::MulCents(std::max<Cents>(1, Econ::ToCents(Mid(mc))), have);
        waiting.push_back(l);
        value += Econ::ToIsk(l.value);
    }
    if (!waiting.empty() and ((value >= 0.25 * maxValue) or ((t - hp.lastShip) >= ShipEverySeconds))) {
        uint32_t idle(HullsFree(a, hp, t));
        size_t i(0);
        Units part(0);              // of waiting[i], already loaded
        while ((idle > 0) and (i < waiting.size())) {
            std::vector<SimTrip::Line> lines;
            double m3(0.0), v(0.0);
            while ((i < waiting.size()) and (lines.size() < MaxTripLines)) {
                SimTrip::Line l(waiting[i]);
                Units left(l.qty - part);
                double unitM3(Logi::PackagedVolume(*m_static.Type(l.typeID)));
                double unitValue(Econ::ToIsk(l.value) / (double)std::max<Units>(1, l.qty));
                double byM3(std::floor((hp.cargoM3 - m3) / std::max(1e-9, unitM3)));
                double byValue(std::floor((maxValue - v) / std::max(0.01, unitValue)));
                Units q(std::min<Units>(left, (Units)std::max(0.0, std::min(byM3, byValue))));
                if (q <= 0)
                    break;
                l.qty = q;
                l.value = (Cents)std::llround(unitValue * 100.0 * (double)q);
                lines.push_back(l);
                m3 += unitM3 * (double)q;
                v += unitValue * (double)q;
                part += q;
                if (part >= waiting[i].qty) {
                    ++i;
                    part = 0;
                }
            }
            if (lines.empty() or !Dispatch(a, hp, hp.site, (uint16_t)node, hp.route[0], lines, t))
                break;
            --idle;
            hp.lastShip = t;
        }
    }

    /* 6.  what arrived goes to the market makers;  beyond HoldDays of its output into their bids, and what nobody takes
     *  is left behind (as MinerStep) */
    for (size_t m = 0; m < plan.outputs.size(); ++m) {
        const Cell& mc(m_cells[plan.outputs[m]]);
        double hold(std::ceil(std::max(0.1, m_p.holdDays) * plan.supply[m]));
        SellHeld(a.actorID, node, mc.typeID, hold, t);
        Units have(Held(a.actorID, node, mc.typeID));
        if ((double)have > hold)
            m_ledger.GoodsOut(a.actorID, (uint16_t)node, mc.typeID, have - (Units)hold, Goods::Spoiled, false);
    }
}

/************************************************************************************************************************
 * loading
 */

bool World::LoadTrip(const SimTrip& trip)
{
    if ((trip.actorID == 0) or (trip.actorID > m_actors.size()) or trip.lines.empty())
        return false;
    std::map<uint32_t, HaulPlan>::iterator pitr = m_haulPlans.find(trip.actorID);
    if (pitr == m_haulPlans.end())
        return false;
    SimTrip t(trip);
    if ((t.tripID == 0) or (m_trips.find(t.tripID) != m_trips.end()))
        t.tripID = m_nextTripID++;
    m_nextTripID = std::max(m_nextTripID, t.tripID + 1);
    m_trips[t.tripID] = t;
    for (auto& l : t.lines)
        m_transit[((uint32_t)t.to << 16) | l.typeID] += l.qty;
    pitr->second.busy[t.tripID] = t.arrive + std::max<int64_t>(0, t.arrive - t.depart);
    return true;
}

std::string World::ReconcileTransit()
{
    // the ledger is the books:  shipments carry what the actors have on the way, no more, and what nothing carries goes
    //  back to the actor's home node
    std::map<std::pair<uint32_t, uint16_t>, Units> have, want;
    for (auto& cur : m_ledger.Stocks())
        if ((Ledger::KeyNode(cur.first) == TransitLoc) and (cur.second.qty > 0))
            have[std::make_pair(Ledger::KeyActor(cur.first), Ledger::KeyType(cur.first))] = cur.second.qty;
    for (auto& cur : m_trips)
        for (auto& l : cur.second.lines)
            want[std::make_pair(cur.second.actorID, l.typeID)] += l.qty;
    uint32_t cut(0), back(0);
    for (std::map<uint32_t, SimTrip>::reverse_iterator itr = m_trips.rbegin(); itr != m_trips.rend(); ++itr) {
        for (auto& l : itr->second.lines) {
            std::pair<uint32_t, uint16_t> k(itr->second.actorID, l.typeID);
            Units over(want[k] - have[k]);
            if (over <= 0)
                continue;
            Units drop(std::min(over, l.qty));
            l.qty -= drop;
            want[k] -= drop;
            ++cut;
        }
    }
    for (std::map<uint32_t, SimTrip>::iterator itr = m_trips.begin(); itr != m_trips.end(); ) {
        std::vector<SimTrip::Line>& lines(itr->second.lines);
        lines.erase(std::remove_if(lines.begin(), lines.end(), [](const SimTrip::Line& l) { return l.qty <= 0; }), lines.end());
        if (lines.empty()) {
            std::map<uint32_t, HaulPlan>::iterator pitr = m_haulPlans.find(itr->second.actorID);
            if (pitr != m_haulPlans.end())
                pitr->second.busy.erase(itr->first);
            itr = m_trips.erase(itr);
        } else {
            ++itr;
        }
    }
    for (auto& cur : have) {
        Units extra(cur.second - want[cur.first]);
        if (extra <= 0)
            continue;
        uint32_t actorID(cur.first.first);
        uint16_t home((actorID <= m_actors.size()) ? (uint16_t)m_actors[actorID - 1].nodeID : 0);
        if ((home != 0) and m_ledger.Relocate(actorID, TransitLoc, home, cur.first.second, extra))
            ++back;
    }
    m_transit.clear();
    for (auto& cur : m_trips)
        for (auto& l : cur.second.lines)
            m_transit[((uint32_t)cur.second.to << 16) | l.typeID] += l.qty;
    if ((cut > 0) or (back > 0))
        m_tripsChanged = true;
    return Fmt("%u shipments on the way (%u cargo lines cut to what the books hold, %u returned home)", (uint32_t)m_trips.size(), cut, back);
}

/************************************************************************************************************************
 * GM
 */

std::string World::RouteInfo(uint32_t nodeID)
{
    if (!m_routes or (m_routes->Systems() == 0))
        return "Logistics is off (marketSim EnableLogistics, or no map data).";
    std::ostringstream out;
    out << Fmt("Map:  %u systems, %u jumps.  Risk per jump:  high-sec %.2g at 1e9 ISK (^%.2g), low-sec %.2g, null-sec %.2g.  Freight %.2f ISK/m3/jump.\n",
               (uint32_t)m_routes->Systems(), (uint32_t)m_routes->Edges(), m_p.risk.highSec, m_p.risk.highSecBeta, m_p.risk.lowSec,
               m_p.risk.nullSec, m_p.freightPerM3Jump);
    double value(std::max(1e6, m_p.haulerMaxTripValue));
    auto describe = [&](const Route& r) {
        if (!r.ok)
            return std::string("no route");
        return Fmt("%u jumps (%u high, %u low, %u null) on the %s route;  a %.0fM ISK load is lost %.3f%% of the time", r.jumps,
                   r.bandJumps[SecBand::High], r.bandJumps[SecBand::Low], r.bandJumps[SecBand::Null],
                   (r.profile == 0) ? "shortest" : ((r.profile == 1) ? "balanced" : "safest"), value / 1e6,
                   100.0 * Logi::TripLoss(m_p.risk, r.bandJumps, value));
    };
    for (auto& s : m_sites) {
        if ((nodeID != 0) and (s.nodeID != nodeID))
            continue;
        out << Fmt("node %u (station %u):  %s site at station %u (system %u), %u jumps away\n", s.nodeID, Node(s.nodeID)->stationID,
                   BandName(s.band), s.station.stationID, s.station.systemID, s.jumps);
    }
    for (auto& cur : m_haulPlans) {
        const HaulPlan& hp(cur.second);
        const Actor& a(m_actors[cur.first - 1]);
        if ((nodeID != 0) and (hp.nodeA != nodeID) and (hp.nodeB != nodeID))
            continue;
        if (hp.site != 0) {
            out << Fmt("actor %u (remote miner, %s) site -> node %u:  %s\n", a.actorID, BandName(SiteBand(hp.site)), hp.nodeA, describe(hp.route[0]).c_str());
        } else {
            out << Fmt("actor %u (hauler) node %u -> %u:  %s\n", a.actorID, hp.nodeA, hp.nodeB, describe(hp.route[0]).c_str());
            out << Fmt("actor %u (hauler) node %u -> %u:  %s\n", a.actorID, hp.nodeB, hp.nodeA, describe(hp.route[1]).c_str());
        }
    }
    return out.str();
}

std::string World::TripList(uint32_t actorID) const
{
    std::ostringstream out;
    uint32_t n(0);
    Units units(0);
    for (auto& cur : m_trips) {
        const SimTrip& tr(cur.second);
        if ((actorID != 0) and (tr.actorID != actorID))
            continue;
        if (++n > 20)
            continue;
        Cents value(0);
        for (auto& l : tr.lines) {
            value += l.value;
            units += l.qty;
        }
        out << Fmt("  shipment %u:  actor %u, %u -> %u in a type %u, %u cargo lines worth %.0f ISK, %lld to %lld\n", tr.tripID, tr.actorID,
                   tr.from, tr.to, tr.hullTypeID, (uint32_t)tr.lines.size(), Econ::ToIsk(value), (long long)tr.depart, (long long)tr.arrive);
    }
    if (n > 20)
        out << Fmt("  ... %u shipments in all\n", n);
    return out.str();
}

std::string World::HaulerInfo(const Actor& a) const
{
    std::map<uint32_t, HaulPlan>::const_iterator itr = m_haulPlans.find(a.actorID);
    if (itr == m_haulPlans.end())
        return "";
    const HaulPlan& hp(itr->second);
    uint32_t inFlight(0);
    for (auto& cur : m_trips)
        if (cur.second.actorID == a.actorID)
            ++inFlight;
    std::ostringstream out;
    out << Fmt("  hauling hull type %u (%.0f m3):  %lld of %.0f, %u shipments on the way\n", hp.hullTypeID, hp.cargoM3,
               (long long)Held(a.actorID, hp.nodeA, hp.hullTypeID), hp.fleet, inFlight);
    if (hp.site != 0) {
        const SiteInfo* s(Site(hp.site));
        std::map<uint32_t, MinerPlan>::const_iterator mp = m_minerPlans.find(a.actorID);
        if ((s != nullptr) and (mp != m_minerPlans.end())) {
            out << Fmt("  %s site:  station %u, %u jumps;  reference %.0f m3/day of ore;  netback response:", BandName(s->band),
                       s->station.stationID, s->jumps, hp.refM3);
            for (size_t m = 0; (m < mp->second.outputs.size()) and (m < hp.share.size()); ++m)
                if (hp.share[m] >= 0.05)
                    out << Fmt("  %u x%.2f", m_cells[mp->second.outputs[m]].typeID, RemoteResponse(hp, m_cells[mp->second.outputs[m]]));
            out << "\n";
        }
    } else {
        out << Fmt("  nodes %u and %u, %u types both trade\n", hp.nodeA, hp.nodeB, (uint32_t)hp.pairs.size());
    }
    return out.str();
}

std::string World::LogisticsStatus(int64_t /*now*/) const
{
    if (!m_routes or (m_routes->Systems() == 0))
        return "";
    uint32_t haulers(0), remote(0);
    for (auto& a : m_actors) {
        if (a.state != 0)
            continue;
        if (a.kind == ActorKind::Hauler)
            ++haulers;
        else if (a.kind == ActorKind::RemoteMiner)
            ++remote;
    }
    Units lost(0), hulls(0);
    for (auto& cur : m_transportLost)
        lost += cur.second;
    for (auto& cur : m_haulHullsBought)
        hulls += cur.second;
    return Fmt("Phase 4:  %u haulers, %u remote miners at %u sites;  %u shipments on the way, %llu arrived and %llu lost (%lld units) since the start, %lld hauling hulls bought.\n",
               haulers, remote, (uint32_t)m_sites.size(), (uint32_t)m_trips.size(), (unsigned long long)m_tripsArrived,
               (unsigned long long)m_tripsLost, (long long)lost, (long long)hulls);
}

}
