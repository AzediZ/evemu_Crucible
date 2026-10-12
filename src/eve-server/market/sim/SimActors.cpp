
 /**
  * @name SimActors.cpp
  *   market simulation:  the phase 3 actors (PLAN 6.3, 6.4, 11 phase 3) and the loss model.  World's members that
  *   build, seed and run the miners, reprocessors, manufacturers and loot sources, and the consumers' ship losses.
  *
  *   every actor has a finite wallet and real stock in the ledger:  miners earn from ore and minerals and spend on the
  *   hulls (and fits) they lose;  manufacturers spend on inputs and lines and earn from what they build;  the reprocessor
  *   earns the gap between what it buys and what its minerals fetch;  the loot source's sales go to the consumers (the
  *   same pilots).  they trade with a node's market maker at the internal prices (inside the spread), and buy players'
  *   asks that are cheaper
  *
  * @date:          15Oct26
  *
  */

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <sstream>

#include "market/sim/SimEconomy.h"
#include "market/sim/SimRng.h"
#include "market/sim/SimWorld.h"

namespace MktSim {

namespace {
    // hash purposes (SimWorld.cpp uses 1..10)
    enum : uint64_t {
        hMine = 101, hHullLoss, hFitQty, hFitDrop, hLossCount, hLootSupply, hLossFit, hLossDrop
    };
    const double Ln2 = 0.6931471805599453;
    const double MaxActorDtDays = 2.0;          // an actor catches up at most two days per step (like a cell)
    const uint16_t CapsuleTypeID = 670;         // a pod (not a market type:  its losses are the implants in it)
    const double MinFleet = 20.0;               // hulls in a mining consortium's fleet at least

    std::string Fmt(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    // a trade with a player order and the ledger's writes for it, saved together (IPersist::FillBegin, as SimWorld.cpp)
    struct ActorFillScope {
        IPersist* p;
        explicit ActorFillScope(IPersist* persist) : p(persist)    { if (p != nullptr) p->FillBegin(); }
        ~ActorFillScope()                                           { if (p != nullptr) p->FillEnd(); }
    };
}

/************************************************************************************************************************
 * small helpers
 */

uint32_t World::NodeActor(uint32_t nodeID, uint8_t kind, uint8_t sector) const
{
    if ((nodeID == 0) or (nodeID > m_nodeActors.size()))
        return 0;
    const NodeActors& n(m_nodeActors[nodeID - 1]);
    switch (kind) {
        case ActorKind::Miner:          return n.miner;
        case ActorKind::Reprocessor:    return n.reprocessor;
        case ActorKind::LootSource:     return n.loot;
        case ActorKind::Manufacturer:   return n.maker[(sector == Sector::Ships) ? 0 : 1];
    }
    return 0;
}

Units World::MakerStock(const Cell& c) const
{
    const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
    return (s == nullptr) ? 0 : (s->qty + s->reserved);
}

Units World::Held(uint32_t actorID, uint32_t nodeID, uint16_t typeID) const
{
    const Ledger::Stock* s(m_ledger.FindInv(actorID, (uint16_t)nodeID, typeID));
    return (s == nullptr) ? 0 : s->qty;
}

void World::GoodsTo(uint32_t actorID, uint32_t nodeID, uint16_t typeID, Units q, uint8_t flow)
{
    if (q <= 0)
        return;
    m_ledger.GoodsIn(actorID, (uint16_t)nodeID, typeID, q, flow);
    m_holdings[actorID].insert(typeID);
}

void World::RebuildHoldings()
{
    m_holdings.clear();
    for (auto& cur : m_ledger.Stocks()) {
        uint32_t actorID(Ledger::KeyActor(cur.first));
        if ((actorID == 0) or (actorID > m_actors.size()) or (m_actors[actorID - 1].kind < ActorKind::Miner))
            continue;
        if ((cur.second.qty + cur.second.reserved) > 0)
            m_holdings[actorID].insert(Ledger::KeyType(cur.first));
    }
}

double World::NodeYield(uint32_t nodeID) const
{
    const NodeData* n(Node(nodeID));
    return Prod::RefineYield((n == nullptr) ? 0.5 : n->refineEfficiency);
}

double World::NodeTake(uint32_t nodeID) const
{
    const NodeData* n(Node(nodeID));
    double take((n == nullptr) ? 0.05 : n->refineTake);
    return std::min(1.0, std::max(0.0, take));
}

Cents World::InternalSell(const Cell& c) const
{
    double p(Mid(c) * (1.0 - std::min(1.0, std::max(0.0, m_p.internalSpreadShare)) * c.h));
    return std::max<Cents>(1, Econ::ToCents(p));
}

double World::ExpectSell(const Cell& c) const
{
    // planning looks through the day's noise to where V is going:  V*, the stock pressure target (PLAN 6.2)
    double v((c.vStar > 0.0) ? c.vStar : Mid(c));
    return v * (1.0 - std::min(1.0, std::max(0.0, m_p.internalSpreadShare)) * c.h);
}

double World::ExpectBuy(const Cell& c) const
{
    double v((c.vStar > 0.0) ? c.vStar : Mid(c));
    return v * (1.0 + std::min(1.0, std::max(0.0, m_p.internalSpreadShare)) * c.h);
}

Cents World::InternalBuy(const Cell& c) const
{
    double p(Mid(c) * (1.0 + std::min(1.0, std::max(0.0, m_p.internalSpreadShare)) * c.h));
    return std::max<Cents>(1, Econ::ToCents(p));
}

double World::RefineWorth(uint32_t nodeID, const TypeData& t) const
{
    // only what the node trades counts (anything else the refinery makes can't be sold here)
    double v(0.0);
    for (auto& out : t.refine) {
        std::map<uint64_t, uint32_t>::const_iterator itr = m_cellIndex.find(((uint64_t)nodeID << 16) | out.first);
        if (itr != m_cellIndex.end())
            v += out.second * Econ::ToIsk(InternalSell(m_cells[itr->second]));
    }
    return v * NodeYield(nodeID) * (1.0 - NodeTake(nodeID));
}

double World::LossShockOf(uint16_t hullTypeID, int64_t t) const
{
    double mult(1.0);
    for (uint16_t key : { (uint16_t)0, hullTypeID }) {
        std::map<uint16_t, std::pair<double, int64_t>>::const_iterator itr = m_lossShock.find(key);
        if ((itr != m_lossShock.end()) and (itr->second.second > t))
            mult *= itr->second.first;
        if (hullTypeID == 0)
            break;
    }
    return mult;
}

double World::MiningShockOf(int64_t t) const
{
    return (m_miningShock.second > t) ? m_miningShock.first : 1.0;
}

void World::ShockLosses(uint16_t hullTypeID, double mult, double days, int64_t now)
{
    m_lossShock[hullTypeID] = std::make_pair(mult, now + (int64_t)(days * SecondsPerDay));
}

void World::ShockMining(double mult, double days, int64_t now)
{
    m_miningShock = std::make_pair(mult, now + (int64_t)(days * SecondsPerDay));
}

/************************************************************************************************************************
 * building (World::Build, after the cells' parameters)
 */

void World::BuildActors(int64_t /*now*/)
{
    size_t nodes(m_static.nodes.size());
    m_nodeActors.assign(nodes, NodeActors());
    m_nodeLosses.assign(nodes, std::vector<std::pair<uint16_t, double>>());
    m_actorCells.clear();
    auto cellAt = [this](uint32_t nodeID, uint16_t typeID) -> Cell* {
        std::map<uint64_t, uint32_t>::iterator itr = m_cellIndex.find(((uint64_t)nodeID << 16) | typeID);
        return (itr == m_cellIndex.end()) ? nullptr : &m_cells[itr->second];
    };

    /* 1.  where each cell's goods come from:  ore and ice from the miner, loot from the loot source, T1 whose every input
     *  trades at the node from a manufacturer, the minerals the node's ore refines into from the miner and the
     *  reprocessor.  everything else stays with the stock model's producers */
    for (auto& c : m_cells) {
        const TypeData& t(*m_static.Type(c.typeID));
        c.supply = Supply::Abstract;
        if (StaticData::IsMineable(t)) {
            c.supply = Supply::Mined;
        } else if (SimClass::IsLoot(t.simClass)) {
            c.supply = Supply::Loot;
        } else if ((t.simClass == SimClass::MfgT1) and !t.bom.empty()) {
            bool ok(true);
            double cost(0.0), share(std::min(1.0, std::max(0.0, m_p.internalSpreadShare)));
            for (auto& in : t.bom) {
                Cell* ic(cellAt(c.nodeID, in.first));
                if ((in.first == t.typeID) or (ic == nullptr) or !(in.second > 0.0))
                    ok = false;
                else
                    cost += in.second * ic->refLocal * (1.0 + share * ic->h);
            }
            if (!ok)
                continue;
            /* what sells for clearly less than its inputs (Crucible T1 modules and charges at 0.5 to 0.9 of their
             *  minerals) isn't built:  rats drop it (PLAN 4.1, the price / mineral cost medians below 1).  the loot
             *  source supplies it */
            if (c.refLocal * (1.0 - share * c.h) < m_p.lootCostShare * cost)
                c.supply = Supply::Loot;
            else
                c.supply = Supply::Manufactured;
        }
    }
    for (auto& c : m_cells) {
        if (c.supply != Supply::Mined)
            continue;
        const TypeData& t(*m_static.Type(c.typeID));
        for (auto& out : t.refine) {
            Cell* m(cellAt(c.nodeID, out.first));
            if ((m != nullptr) and (m->simClass == SimClass::Refined))
                m->supply = Supply::Refined;
        }
    }
    // phase 6:  T2, components, moon materials, planetary goods, fuel blocks (SimChains.cpp)
    if (ChainsT2() or ChainsMoonPI())
        ChainSupply();

    /* 2.  the loss model (PLAN 6.3):  hulls lost a day = public killmails / KillmailCoverage x EconomyScale x the node's
     *  share of the hull's trade.  the consumers' losses come back as demand for the hull and its fit;  the fit's
     *  non-destroyed share drops (loot supply).  mining hulls are the miners' (step 4) */
    double scale(m_p.economyScale / std::max(0.01, m_p.killmailCoverage));
    double shipLosses(0.0);
    for (auto& cur : m_static.losses)
        if (cur.first != CapsuleTypeID)
            shipLosses += m_static.LossesPerDay(cur.first);
    for (auto& node : m_static.nodes) {
        std::vector<std::pair<uint16_t, double>>& list(m_nodeLosses[node.nodeID - 1]);
        for (auto& cur : m_static.losses) {
            uint16_t hull(cur.first);
            const TypeData* ht(m_static.Type(hull));
            if ((ht != nullptr) and StaticData::IsMiningHull(*ht))
                continue;
            double perDay(m_static.LossesPerDay(hull));
            if ((hull == CapsuleTypeID) and (m_p.podLossRate > 0.0))
                perDay = m_p.podLossRate * shipLosses;
            double lambda(perDay * scale * m_static.RegionUnits(node.regionID, hull));
            if (!(lambda > 0.0))
                continue;
            list.push_back(std::make_pair(hull, lambda));
            Cell* hc(cellAt(node.nodeID, hull));
            if (hc != nullptr)
                hc->lossRate += lambda;
            std::map<uint16_t, std::vector<FitItem>>::const_iterator fit = m_static.fits.find(hull);
            if (fit == m_static.fits.end())
                continue;
            for (auto& f : fit->second) {
                Cell* fc(cellAt(node.nodeID, f.typeID));
                if (fc == nullptr)
                    continue;
                fc->lossRate += lambda * f.qtyPerLoss;
                fc->dropRate += lambda * f.qtyPerLoss * (1.0 - std::min(1.0f, std::max(0.0f, f.destroyedShare)));
            }
        }
    }

    // 3.  the final demand left after the losses, and what building it takes from the minerals (derived demand)
    std::map<uint64_t, double> derived;             // (node << 16 | type) -> units a day
    auto expectedMade = [](const Cell& c) {
        return std::max(0.0, std::max(0.0, c.dc - c.lossRate) + c.lossRate - c.dropRate);
    };
    if (ChainsT2() or ChainsMoonPI()) {
        // phase 6:  through every chain, from the final goods down (SimChains.cpp)
        ChainDerived(derived);
    } else {
        for (auto& c : m_cells) {
            if (c.supply != Supply::Manufactured)
                continue;
            double made(expectedMade(c));
            for (auto& in : m_static.Type(c.typeID)->bom)
                derived[((uint64_t)c.nodeID << 16) | in.first] += made * in.second;
        }
    }
    m_refDerived = derived;

    // 4.  per node:  the miner's plan (its fleet loses hulls and fits like anyone), then the other actors
    for (auto& node : m_static.nodes)
        PlanMining(node.nodeID);
    for (auto& node : m_static.nodes) {
        uint32_t miner(m_nodeActors[node.nodeID - 1].miner);
        if (miner == 0)
            continue;
        const MinerPlan& plan(m_minerPlans[miner]);
        for (auto& h : plan.fleet) {
            double lost(h.second * plan.lossPerHullDay);
            Cell* hc(cellAt(node.nodeID, h.first));
            if (hc != nullptr)
                hc->minerRate += lost;
            std::map<uint16_t, std::vector<FitItem>>::const_iterator fit = m_static.fits.find(h.first);
            if (fit == m_static.fits.end())
                continue;
            for (auto& f : fit->second) {
                Cell* fc(cellAt(node.nodeID, f.typeID));
                if (fc == nullptr)
                    continue;
                fc->minerRate += lost * f.qtyPerLoss;
                fc->dropRate += lost * f.qtyPerLoss * (1.0 - std::min(1.0f, std::max(0.0f, f.destroyedShare)));
            }
        }
    }
    // a node without a miner keeps the stock model's supply of its ore and minerals
    for (auto& c : m_cells)
        if (((c.supply == Supply::Mined) or (c.supply == Supply::Refined)) and (m_nodeActors[c.nodeID - 1].miner == 0))
            c.supply = Supply::Abstract;

    /* the final demand consumers still buy apart from their losses (PLAN 6.3):  ore none (it is refined), minerals what
     *  the node's T1 building doesn't explain (T2, capitals, ... in the stock model), everything else dc less the losses.
     *  dRef, the reference demand, is the larger of the calibration and what the parts add up to */
    for (auto& c : m_cells) {
        double der(0.0);
        std::map<uint64_t, double>::const_iterator d = derived.find(((uint64_t)c.nodeID << 16) | c.typeID);
        if (d != derived.end())
            der = d->second;
        if (c.supply == Supply::Mined)
            c.dcGen = 0.0;
        else if (c.supply == Supply::Refined)
            c.dcGen = std::max(c.dc - der, m_p.mineralOtherShare * std::max(c.dc, der));
        else
            c.dcGen = std::max(0.0, c.dc - c.lossRate - c.minerRate - der);
        c.dRef = std::max(c.dc, c.dcGen + c.lossRate + c.minerRate + der);
    }

    // 5.  the actors.  per node:  reprocessor, manufacturers (ships, goods), loot source
    auto newActor = [this](uint8_t kind, uint8_t sector, const NodeData& node) -> uint32_t {
        Actor a = Actor();
        a.actorID = (uint32_t)m_actors.size() + 1;
        a.kind = kind;
        a.sector = sector;
        a.ownerCorpID = m_static.ActorCorp(node.nodeID, kind, sector);
        a.regionID = node.regionID;
        a.nodeID = node.nodeID;
        m_actors.push_back(a);
        return a.actorID;
    };
    double capitalDays(std::max(0.5, m_p.actorCapitalDays));
    for (auto& node : m_static.nodes) {
        NodeActors& na(m_nodeActors[node.nodeID - 1]);
        // what the node refines:  a reprocessor where anything that refines trades
        bool refinable(false);
        double mineralValue(0.0);
        std::vector<uint32_t> products[2], loot;
        double makerCost[2] = {0.0, 0.0}, makerHours[2] = {0.0, 0.0}, lootValue(0.0);
        for (auto& c : m_cells) {
            if (c.nodeID != node.nodeID)
                continue;
            const TypeData& t(*m_static.Type(c.typeID));
            if (!t.refine.empty())
                refinable = true;
            if (c.supply == Supply::Refined)
                mineralValue += c.dRef * c.refLocal;
            if (c.supply == Supply::Manufactured) {
                int s((c.sector == Sector::Ships) ? 0 : 1);
                products[s].push_back(c.index);
                double made(expectedMade(c));
                makerCost[s] += made * c.costRef;
                makerHours[s] += made * Prod::BuildHours(t, m_p.buildTimeMultiplier, m_p.defaultBuildSeconds);
            } else if (c.supply == Supply::Loot) {
                loot.push_back(c.index);
                lootValue += c.dRef * c.refLocal;
            }
        }
        if (refinable) {
            na.reprocessor = newActor(ActorKind::Reprocessor, 0, node);
            // working money:  a share of the node's mineral trade (it buys gluts and players' cheap asks)
            m_actors[na.reprocessor - 1].target = std::max(1e8, 0.05 * mineralValue * capitalDays * 100.0);
        }
        for (int s = 0; s < 2; ++s) {
            if (products[s].empty())
                continue;
            uint32_t id(newActor(ActorKind::Manufacturer, (s == 0) ? Sector::Ships : Sector::Goods, node));
            na.maker[s] = id;
            Actor& a(m_actors[id - 1]);
            // lines:  the hours a day its calibrated output takes, with slack, and a few more so a long job doesn't
            //  hold up the other products (a station has dozens)
            a.capacity = std::ceil(makerHours[s] / 24.0 * (1.0 + m_p.productionSlack)) + std::max(3.0, std::ceil(products[s].size() / 20.0));
            a.target = std::max(1e8, makerCost[s] * capitalDays * 100.0);
            m_capacity->SetSlots(id, (uint32_t)a.capacity);
            m_makerPlans[id].products = products[s];
        }
        if (!loot.empty() or (na.miner != 0) or !m_nodeLosses[node.nodeID - 1].empty()) {
            na.loot = newActor(ActorKind::LootSource, 0, node);
            // a day of its sales:  the rest goes to the consumers every day
            m_actors[na.loot - 1].target = std::max(1e7, lootValue * 100.0);
            m_actorCells[na.loot] = loot;
        }
    }
    // the miners' working money:  days of what their fleet brings in
    for (auto& cur : m_minerPlans)
        m_actors[cur.first - 1].target = std::max(1e8, cur.second.valuePerDay * capitalDays * 100.0);
    // phase 6:  inventors, harvesters, reactors, planetary producers (SimChains.cpp)
    if (ChainsT2() or ChainsMoonPI())
        ChainActors();
}

void World::PlanMining(uint32_t nodeID)
{
    const NodeData& node(*Node(nodeID));
    std::vector<uint32_t> ores, minerals;
    for (auto& c : m_cells) {
        if (c.nodeID != nodeID)
            continue;
        if (c.supply == Supply::Mined)
            ores.push_back(c.index);
        else if (c.supply == Supply::Refined)
            minerals.push_back(c.index);
    }
    if (ores.empty() or minerals.empty())
        return;
    std::map<uint16_t, size_t> mIndex;
    for (size_t m = 0; m < minerals.size(); ++m)
        mIndex[m_cells[minerals[m]].typeID] = m;
    double yield(NodeYield(nodeID)), take(NodeTake(nodeID));
    double share(std::min(1.0, std::max(0.0, m_p.internalSpreadShare)));

    /* what the node's minerals need a day (the calibrated consumption, or what its T1 building takes when that's more:
     *  BuildActors' derived demand), and what a m3 of each ore gives.  the mix starts at the ores' traded m3 */
    std::vector<double> need(minerals.size(), 0.0);
    for (size_t m = 0; m < minerals.size(); ++m) {
        const Cell& mc(m_cells[minerals[m]]);
        need[m] = mc.dc;
        // what the node's industry takes (BuildActors' derived demand)
        double der(RefDerived(nodeID, mc.typeID));
        // what the rest of industry uses (T2, capitals, ...:  the stock model's), at least MineralOtherShare
        need[m] = der + std::max(mc.dc - der, m_p.mineralOtherShare * std::max(mc.dc, der));
    }
    std::vector<std::vector<double>> y(ores.size(), std::vector<double>(minerals.size(), 0.0));
    std::vector<double> x0(ores.size(), 0.0), value0(ores.size(), 0.0);
    for (size_t o = 0; o < ores.size(); ++o) {
        const Cell& oc(m_cells[ores[o]]);
        const TypeData& t(*m_static.Type(oc.typeID));
        double refined(0.0);
        for (auto& out : t.refine) {
            std::map<uint16_t, size_t>::const_iterator itr = mIndex.find(out.first);
            if (itr == mIndex.end())
                continue;
            y[o][itr->second] = out.second * yield * (1.0 - take) / t.volume;
            const Cell& mc(m_cells[minerals[itr->second]]);
            refined += y[o][itr->second] * mc.refLocal * (1.0 - share * mc.h);
        }
        // isk per m3 at reference prices, as MinerStep reckons it (the internal sell prices)
        value0[o] = std::max(oc.refLocal * (1.0 - share * oc.h) / t.volume, refined);
        x0[o] = std::max(oc.dc * t.volume, 1e-3);
    }
    /* the reference mix:  the node's needs covered m3 by m3 (as MinerStep mines), within the m3 a least squares fit
     *  from the traded ores' m3 takes (a bound;  the covering uses less, it wastes no m3 on co-products) */
    std::vector<double> fit(Prod::FitMix(y, need, x0));
    double bound(0.0);
    for (auto v : fit)
        bound += v;
    std::vector<double> mix(Prod::CoverMix(y, need, 2.0 * bound, std::vector<bool>(ores.size(), true), 2000));
    double m3(0.0), value(0.0);
    for (size_t o = 0; o < ores.size(); ++o) {
        m3 += mix[o];
        value += mix[o] * value0[o];
    }
    if (!(m3 > 0.0))
        return;

    // the fleet:  the node's mining hulls, in the mix they are lost in (killmails), enough for the mix with slack
    double scale(m_p.economyScale / std::max(0.01, m_p.killmailCoverage));
    std::vector<std::pair<uint16_t, double>> hulls;
    double lossTotal(0.0), weightTotal(0.0);
    for (auto& c : m_cells) {
        if (c.nodeID != nodeID)
            continue;
        const TypeData& t(*m_static.Type(c.typeID));
        if (!StaticData::IsMiningHull(t))
            continue;
        double perDay(m_static.LossesPerDay(t.typeID));
        double w((perDay > 0.0) ? perDay : 0.0);
        hulls.push_back(std::make_pair(t.typeID, w));
        weightTotal += w;
        lossTotal += perDay * scale * m_static.RegionUnits(node.regionID, t.typeID);
    }
    // a fleet of at least MinFleet hulls (a small node's consortium isn't one barge:  losing it would stop the mining)
    double capacity(m3 * (1.0 + m_p.miningSlack));
    double fleetSize(std::max(MinFleet, std::round(capacity / std::max(1.0, m_p.miningM3PerHullDay))));
    MinerPlan plan;
    plan.ores = ores;
    plan.outputs = minerals;
    plan.yield = y;
    plan.need = need;
    plan.supply.assign(minerals.size(), 0.0);
    for (size_t o = 0; o < ores.size(); ++o)
        for (size_t m = 0; m < minerals.size(); ++m)
            plan.supply[m] += y[o][m] * mix[o];
    plan.mix = mix;
    plan.value0 = value0;
    plan.valuePerDay = value;
    plan.mined.assign(ores.size(), 0.0);
    plan.m3PerHull = capacity / fleetSize;
    for (auto& h : hulls)
        plan.fleet.push_back(std::make_pair(h.first, fleetSize * ((weightTotal > 0.0) ? h.second / weightTotal : 1.0 / hulls.size())));
    // lost hulls a day, per hull (at most a tenth of the fleet a day)
    plan.lossPerHullDay = hulls.empty() ? 0.0 : std::min(0.1, lossTotal / fleetSize);

    Actor a = Actor();
    a.actorID = (uint32_t)m_actors.size() + 1;
    a.kind = ActorKind::Miner;
    a.ownerCorpID = m_static.ActorCorp(nodeID, ActorKind::Miner, 0);
    a.regionID = node.regionID;
    a.nodeID = nodeID;
    a.capacity = capacity;
    m_actors.push_back(a);
    m_nodeActors[nodeID - 1].miner = a.actorID;
    m_minerPlans[a.actorID] = plan;
}

void World::SeedActors(int64_t /*now*/, uint32_t nodeID, bool freshOnly)
{
    // the miners start with their reference fleet (a one-off goods faucet, like the market makers' stock)
    std::set<uint32_t> stocked;
    if (freshOnly)
        for (auto& cur : m_ledger.Stocks())
            if ((cur.second.qty + cur.second.reserved) > 0)
                stocked.insert(Ledger::KeyActor(cur.first));
    for (auto& cur : m_minerPlans) {
        const Actor& a(m_actors[cur.first - 1]);
        if (((nodeID != 0) and (a.nodeID != nodeID)) or (a.state != 0) or (stocked.count(a.actorID) > 0))
            continue;
        for (auto& h : cur.second.fleet) {
            Units want((Units)std::llround(h.second));
            Units have(Held(a.actorID, a.nodeID, h.first));
            if (want > have)
                GoodsTo(a.actorID, a.nodeID, h.first, want - have, Goods::Seed);
        }
    }
}

bool World::LoadJob(const SimJob& job)
{
    if ((job.actorID == 0) or (job.actorID > m_actors.size()))
        return false;
    // phase 6 (SimChains.cpp):  a chain actor's (an invention that failed gives nothing:  qtyOut 0)
    if (m_actors[job.actorID - 1].kind >= ActorKind::Inventor)
        return ChainLoadJob(job);
    if (job.qtyOut <= 0)
        return false;
    const Actor& a(m_actors[job.actorID - 1]);
    if ((a.kind != ActorKind::Manufacturer) or (a.nodeID != job.nodeID) or (m_static.Type(job.typeID) == nullptr))
        return false;
    SimJob j(job);
    if (j.stationID == 0)
        j.stationID = Node(j.nodeID)->stationID;
    if ((j.jobID == 0) or (m_jobs.find(j.jobID) != m_jobs.end()))
        j.jobID = m_nextJobID++;
    m_jobs[j.jobID] = j;
    m_nextJobID = std::max(m_nextJobID, j.jobID + 1);
    m_capacity->Restore(j);
    m_makerPlans[j.actorID].wip[j.typeID] += j.qtyOut;
    return true;
}

std::string World::JobList(uint32_t actorID) const
{
    std::ostringstream out;
    uint32_t n(0);
    for (auto& cur : m_jobs) {
        const SimJob& j(cur.second);
        if ((actorID != 0) and (j.actorID != actorID))
            continue;
        if (++n > 20)
            continue;
        out << Fmt("  job %u:  %lld x type %u (%u runs) at node %u, line %u, %lld to %lld\n", j.jobID, (long long)j.qtyOut, j.typeID,
                   j.runs, j.nodeID, j.lineID, (long long)j.start, (long long)j.end);
    }
    if (n > 20)
        out << Fmt("  ... %u jobs in all\n", n);
    return out.str();
}

/************************************************************************************************************************
 * trading at a node
 */

Units World::SellToMaker(uint32_t actorID, Cell& c, Units q, Cents minPrice, int64_t /*t*/)
{
    q = std::min(q, Held(actorID, c.nodeID, c.typeID));
    // the market maker takes what brings its stock to the target (its bids for players reach further).  a glut stays
    //  with the suppliers, who see it in their own stock (miners chase other ores, loot spoils, jobs follow the gap)
    Units room((Units)std::ceil(c.targetStock) - MakerStock(c));
    q = std::min(q, room);
    if (q <= 0)
        return 0;
    Cents price(InternalSell(c));
    if (price < minPrice)
        return 0;
    uint32_t mm(c.mm);
    Units sold(0);
    /* its bids hold the money:  each unit bought frees the bid's escrow and the wallet pays the rest of the internal
     *  price.  what the wallet can't top up is paid at the bid (its money is all in bids:  the supplier gets the bid,
     *  like a player selling) */
    Cents bidValue(0);
    Units atBid(0), atPrice(0);
    for (int i = 0; (i < c.levels) and (sold < q); ++i) {
        if (c.bid[i] == 0)
            continue;
        std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(c.bid[i]);
        if ((itr == m_orders.end()) or (itr->second.qty == 0))
            continue;
        SimOrder& o(itr->second);
        Units k(std::min<Units>(q - sold, o.qty)), full(k);
        if (price > o.price) {
            const Ledger::Account* acct(m_ledger.FindAcct(mm));
            Cents wallet((acct == nullptr) ? 0 : acct->wallet);
            full = std::min<Units>(k, wallet / (price - o.price));
        }
        Cents freed(Econ::MulCents(o.price, full)), value(Econ::MulCents(price, full));
        if ((freed < 0) or (value < 0))
            break;
        if (full > 0) {
            m_ledger.Unreserve(mm, freed);
            if (!m_ledger.Transfer(mm, actorID, value)) {
                m_ledger.Reserve(mm, freed);
                full = 0;
            }
        }
        Units rest(k - full);
        Cents restValue(Econ::MulCents(o.price, rest));
        if ((rest > 0) and ((restValue < 0) or !m_ledger.PayFromReserved(mm, actorID, restValue)))
            rest = 0;
        k = full + rest;
        if (k <= 0)
            break;
        m_ledger.MoveGoods(actorID, mm, (uint16_t)c.nodeID, c.typeID, k, false);
        o.qty -= (uint32_t)k;
        sold += k;
        atPrice += full;
        atBid += rest;
        bidValue += restValue;
        if (o.qty == 0)
            CancelOrderNow(o.orderID);
        else
            QueueAlter(o);
    }
    if (atBid > 0) {
        Print(c, std::max<Cents>(1, bidValue / atBid), atBid);
        Changed(c);
    }
    // beyond its bids, out of its free money (keeping a tenth of its working money for fees)
    if (sold < q) {
        const Ledger::Account* acct(m_ledger.FindAcct(mm));
        Cents spare(((acct == nullptr) ? 0 : acct->wallet) - (Cents)(m_actors[mm - 1].target / 10.0));
        Units k(std::min<Units>(q - sold, (spare > 0) ? spare / price : 0));
        Cents value(Econ::MulCents(price, k));
        if ((k > 0) and (value >= 0) and m_ledger.Transfer(mm, actorID, value)) {
            m_ledger.MoveGoods(actorID, mm, (uint16_t)c.nodeID, c.typeID, k, false);
            sold += k;
            atPrice += k;
        }
    }
    if (atPrice > 0) {
        Print(c, price, atPrice);
        Changed(c);
    }
    return sold;
}

Units World::SellToBids(uint32_t actorID, Cell& c, Units q, int64_t /*t*/)
{
    q = std::min(q, Held(actorID, c.nodeID, c.typeID));
    Units sold(0);
    for (int i = 0; (i < c.levels) and (sold < q); ++i) {
        if (c.bid[i] == 0)
            continue;
        std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(c.bid[i]);
        if ((itr == m_orders.end()) or (itr->second.qty == 0))
            continue;
        SimOrder& o(itr->second);
        Units k(std::min<Units>(q - sold, o.qty));
        Cents value(Econ::MulCents(o.price, k));
        if ((value < 0) or !m_ledger.PayFromReserved(c.mm, actorID, value))
            break;
        m_ledger.MoveGoods(actorID, c.mm, (uint16_t)c.nodeID, c.typeID, k, false);
        o.qty -= (uint32_t)k;
        sold += k;
        Print(c, o.price, k);
        if (o.qty == 0)
            CancelOrderNow(o.orderID);
        else
            QueueAlter(o);
    }
    if (sold > 0)
        Changed(c);
    return sold;
}

Units World::Spare(const Cell& c, Units q) const
{
    /* what an actor may take of a market maker's stock at once:  down to a tenth of its target.  an input bought in
     *  one go for a day's jobs would otherwise empty the shelves (and spike the price) every time;  the rest comes as
     *  the suppliers refill them */
    return std::min<Units>(q, std::max<Units>(0, MakerStock(c) - (Units)std::ceil(0.1 * c.targetStock)));
}

Units World::BuyAtNode(uint32_t actorID, Cell& c, Units q, Cents limit, Units fromMaker, int64_t t)
{
    if ((q <= 0) or (limit <= 0))
        return 0;
    const NodeData& node(*Node(c.nodeID));
    const Actor& a(m_actors[actorID - 1]);
    Units got(0);
    auto wallet = [this, actorID]() -> Cents {
        const Ledger::Account* acct(m_ledger.FindAcct(actorID));
        return (acct == nullptr) ? 0 : acct->wallet;
    };
    // players' asks in the region at or under the limit, cheapest first (PLAN 7.3:  the sim buys players' goods)
    std::vector<PlayerOrder> pAsks, pBids;
    m_gw.PlayerOrders(node.regionID, c.typeID, pAsks, pBids);
    for (auto& po : pAsks) {
        if ((got >= q) or (po.price > limit))
            break;
        if (po.qty == 0)
            continue;
        Units take(std::min<Units>(q - got, po.qty));
        take = std::min<Units>(take, IntakeLeft(c, po.ownerID, po.price, t));
        take = std::min<Units>(take, wallet() / po.price);
        if (take <= 0)
            continue;
        ActorFillScope scope(m_persist);
        uint32_t bought(m_gw.TakePlayerAsk(po, (uint32_t)take, actorID, a.ownerCorpID));
        if (bought == 0)
            continue;
        Cents value(Econ::MulCents(po.price, bought));
        if (!m_ledger.ToPlayer(actorID, value, false)) {
            // can't happen (the wallet was checked):  counted as a reconciliation so the books still show it
            m_ledger.Faucet(actorID, value, Isk::Reconcile);
            m_ledger.ToPlayer(actorID, value, false);
        }
        GoodsTo(actorID, c.nodeID, c.typeID, bought, Goods::PlayerToSim);
        RecordIntake(c, po.ownerID, bought, value, t);
        if (m_persist != nullptr)
            m_persist->PlayerFill(actorID, -value, 0, c.nodeID, c.typeID, bought, 0);
        PlayerFeedback(c, po.ownerID, po.price, bought);
        po.qty -= bought;
        got += bought;
        ++m_playerTrades;
    }
    // the market maker's stock at the internal price:  what it keeps off the book first, then its asks
    Cents price(InternalBuy(c));
    if ((got < q) and (fromMaker > 0) and (price <= limit)) {
        Units k(std::min<Units>(q - got, fromMaker));
        k = std::min<Units>(k, wallet() / price);
        Units taken(0);
        if (k > 0) {
            const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
            Units loose(std::min<Units>(k, (s == nullptr) ? 0 : s->qty));
            if ((loose > 0) and m_ledger.MoveGoods(c.mm, actorID, (uint16_t)c.nodeID, c.typeID, loose, false))
                taken += loose;
            for (int i = 0; (i < c.levels) and (taken < k); ++i) {
                if (c.ask[i] == 0)
                    continue;
                std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(c.ask[i]);
                if ((itr == m_orders.end()) or (itr->second.qty == 0))
                    continue;
                SimOrder& o(itr->second);
                Units n(std::min<Units>(k - taken, o.qty));
                if (!m_ledger.MoveGoods(c.mm, actorID, (uint16_t)c.nodeID, c.typeID, n, true))
                    break;
                o.qty -= (uint32_t)n;
                taken += n;
                if (o.qty == 0)
                    CancelOrderNow(o.orderID);
                else
                    QueueAlter(o);
            }
        }
        if (taken > 0) {
            m_ledger.Transfer(actorID, c.mm, Econ::MulCents(price, taken));
            m_holdings[actorID].insert(c.typeID);
            Print(c, price, taken);
            got += taken;
        }
    }
    // derived demand (PLAN 6.3):  what was bought feeds the cell's demand (a shortfall shows as the stock running low)
    c.derived += (double)got;
    m_bought[c.typeID] += got;
    Changed(c);
    return got;
}

Units World::RefineHeld(uint32_t actorID, uint32_t nodeID, uint16_t typeID, Units q)
{
    const TypeData* t(m_static.Type(typeID));
    if ((t == nullptr) or t->refine.empty())
        return 0;
    q = std::min(q, Held(actorID, nodeID, typeID));
    Prod::RefineResult r;
    Prod::Refine(*t, q, NodeYield(nodeID), NodeTake(nodeID), r);
    if ((r.used <= 0) or !m_ledger.GoodsOut(actorID, (uint16_t)nodeID, typeID, r.used, Goods::Materials, false))
        return 0;
    for (auto& out : r.gross)
        GoodsTo(actorID, nodeID, out.first, out.second, Goods::Refined);
    for (auto& tax : r.tax)
        m_ledger.GoodsOut(actorID, (uint16_t)nodeID, tax.first, tax.second, Goods::RefineTax, false);
    // what the node doesn't trade stays with the station (counted with its take)
    for (auto& out : r.gross) {
        std::map<uint64_t, uint32_t>::const_iterator itr = m_cellIndex.find(((uint64_t)nodeID << 16) | out.first);
        if (itr != m_cellIndex.end())
            continue;
        Units left(Held(actorID, nodeID, out.first));
        if (left > 0)
            m_ledger.GoodsOut(actorID, (uint16_t)nodeID, out.first, left, Goods::RefineTax, false);
    }
    return r.used;
}

/************************************************************************************************************************
 * the actors' steps (hourly)
 */

void World::StepActor(Actor& a, int64_t t)
{
    if (a.state != 0)
        return;
    double dt(std::min(MaxActorDtDays, std::max(0.0, (double)(t - a.lastStep) / (double)SecondsPerDay)));
    a.lastStep = t;
    switch (a.kind) {
        case ActorKind::Miner:          MinerStep(a, t, dt); break;
        case ActorKind::Reprocessor:    ReprocessorStep(a, t, dt); break;
        case ActorKind::Manufacturer:   ManufacturerStep(a, t, dt); break;
        case ActorKind::LootSource:     LootStep(a, t, dt); break;
        case ActorKind::Inventor:
        case ActorKind::Harvester:
        case ActorKind::Reactor:
        case ActorKind::Planetary:      ChainStep(a, t, dt); break;     // phase 6 (SimChains.cpp)
    }
}

void World::MinerStep(Actor& a, int64_t t, double dt)
{
    std::map<uint32_t, MinerPlan>::iterator pitr = m_minerPlans.find(a.actorID);
    if (pitr == m_minerPlans.end())
        return;
    MinerPlan& plan(pitr->second);
    uint32_t node(a.nodeID), loot(NodeActor(node, ActorKind::LootSource));

    /* 1.  hulls lost since the last step (belt rats, gankers):  destroyed, and their fits with them (what drops goes to
     *  the loot source).  they are bought again */
    uint64_t salt(0);
    for (auto& h : plan.fleet) {
        Units have(Held(a.actorID, node, h.first));
        if ((have <= 0) or !(plan.lossPerHullDay > 0.0) or !(dt > 0.0))
            continue;
        Units lost(std::min<Units>(have, (Units)Rng::Poisson((double)have * plan.lossPerHullDay * dt,
                                                               Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hHullLoss, h.first))));
        if (lost <= 0)
            continue;
        m_ledger.GoodsOut(a.actorID, (uint16_t)node, h.first, lost, Goods::Destroyed, false);
        std::map<uint16_t, std::vector<FitItem>>::const_iterator fit = m_static.fits.find(h.first);
        if (fit == m_static.fits.end())
            continue;
        for (auto& f : fit->second) {
            if (FindCell(node, f.typeID) == nullptr)
                continue;
            Units q(Rng::RoundRandom((double)lost * f.qtyPerLoss, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hFitQty, ++salt)));
            if (q <= 0)
                continue;
            // at most a refit of the whole fleet waits (what the node can't supply doesn't pile up)
            double fleet(0.0);
            for (auto& hh : plan.fleet)
                fleet += hh.second;
            plan.needs[f.typeID] = std::min<Units>(plan.needs[f.typeID] + q, (Units)std::ceil(fleet * f.qtyPerLoss) + 1);
            Units drop(Rng::RoundRandom((double)q * (1.0 - f.destroyedShare), Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hFitDrop, salt)));
            if ((drop > 0) and (loot != 0))
                GoodsTo(loot, node, f.typeID, drop, Goods::Dropped);
        }
    }

    /* 2.  replacements (PLAN 6.4):  hulls back to the fleet target (a fifth more when the money is there, a smaller fleet
     *  when it isn't) and the fit items.  bought at the node;  fit items are fitted at once */
    Cents money(m_ledger.MoneyOf(a.actorID));
    double expand((money > 2.0 * a.target) ? 1.2 : ((money < 0.25 * a.target) ? 0.8 : 1.0));
    for (auto& h : plan.fleet) {
        Units want((Units)std::llround(h.second * expand)), have(Held(a.actorID, node, h.first));
        plan.needs[h.first] = std::max<Units>(0, want - have);
    }
    std::set<uint16_t> hullTypes;
    for (auto& h : plan.fleet)
        hullTypes.insert(h.first);
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

    /* 3.  mining (demand pull, like a market maker's restocking):  what each mineral needs now (the node's demand, its
     *  dBar, plus refilling its market maker to the target within RestockDays, less what the miner holds unsold), covered
     *  m3 by m3 with the ores of the reference mix that cover the most of it (Prod::CoverMix), up to the fleet's m3.  ores
     *  give several minerals:  a glut of the others is the price of the scarce one (step 5).  prices follow the stock
     *  (PLAN 6.2):  a mining shock empties the shelves of what the fleet is worst at, and V rises */
    double hulls(0.0);
    for (auto& h : plan.fleet)
        hulls += (double)Held(a.actorID, node, h.first);
    double capacity(hulls * plan.m3PerHull * MiningShockOf(t));
    std::vector<double> need(plan.outputs.size(), 0.0);
    for (size_t m = 0; m < plan.outputs.size(); ++m) {
        const Cell& mc(m_cells[plan.outputs[m]]);
        double gap(mc.targetStock - (double)MakerStock(mc) - (double)Held(a.actorID, node, mc.typeID));
        need[m] = std::max(0.0, mc.dBar + gap / std::max(0.1, m_p.restockDays));
        // a mineral in glut still counts a little:  the fit then won't pick ores for what nobody needs
        need[m] = std::max(need[m], 0.05 * plan.need[m]);
    }
    std::vector<bool> allowed(plan.ores.size(), true);
    for (size_t o = 0; o < plan.ores.size(); ++o)
        allowed[o] = (plan.mix[o] > 0.0);
    std::vector<double> x(Prod::CoverMix(plan.yield, need, capacity, allowed));
    plan.lastNeed = need;
    double wsum(0.0);
    for (auto v : x)
        wsum += v;
    double scale(1.0);
    plan.lastWanted = wsum;
    plan.lastMined = wsum * scale;
    plan.lastCapacity = capacity;
    plan.lastRel.assign(plan.ores.size(), 0.0);
    if ((wsum > 0.0) and (dt > 0.0)) {
        for (size_t o = 0; o < plan.ores.size(); ++o) {
            if (plan.mix[o] > 0.0)
                plan.lastRel[o] = x[o] * scale / plan.mix[o];
            if (!(x[o] > 0.0))
                continue;
            const Cell& oc(m_cells[plan.ores[o]]);
            const TypeData& ot(*m_static.Type(oc.typeID));
            double units(x[o] * scale * dt / ot.volume);
            Units u(Rng::RoundRandom(units, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hMine, oc.typeID)));
            if (u <= 0)
                continue;
            GoodsTo(a.actorID, node, oc.typeID, u, Goods::Mined);
            m_mined[oc.typeID] += u;
            if (plan.mined.size() == plan.ores.size())
                plan.mined[o] += (double)u * ot.volume;
        }
    }


    // 4.  the ore:  sold as it is when that pays as well as refining it (and the market maker wants it), else refined
    for (auto idx : plan.ores) {
        Cell& oc(m_cells[idx]);
        Units have(Held(a.actorID, node, oc.typeID));
        if (have <= 0)
            continue;
        const TypeData& ot(*m_static.Type(oc.typeID));
        double worth(RefineWorth(node, ot));
        if (Econ::ToIsk(InternalSell(oc)) >= worth)
            have -= SellToMaker(a.actorID, oc, have, 0, t);
        if ((have > 0) and (worth > 0.0))
            RefineHeld(a.actorID, node, oc.typeID, have);
    }
    /* 5.  the minerals go to the market makers.  what it holds beyond HoldDays of its output (a glut:  Tritanium comes
     *  with every ore) is sold into the market maker's public bids, at the bid like anyone's:  the stock over the target
     *  takes V down (PLAN 6.2), so consumers use more of it and the ores rich in it pay less.  what nobody takes is left
     *  behind (spoiled:  co-production waste, it was never worth hauling) */
    for (size_t m = 0; m < plan.outputs.size(); ++m) {
        Cell& mc(m_cells[plan.outputs[m]]);
        Units have(Held(a.actorID, node, mc.typeID));
        if (have > 0)
            have -= SellToMaker(a.actorID, mc, have, 0, t);
        Units hold((Units)std::ceil(std::max(0.1, m_p.holdDays) * plan.supply[m]));
        if (have > hold)
            have -= SellToBids(a.actorID, mc, have - hold, t);
        if (have > hold)
            m_ledger.GoodsOut(a.actorID, (uint16_t)node, mc.typeID, have - hold, Goods::Spoiled, false);
    }
}

void World::ReprocessorAtCell(Cell& c, int64_t t)
{
    // the refine floor (PLAN 6.4):  what refines for more than it costs is bought (players' asks, and a market maker's
    //  glut), refined at the node and sold as minerals.  the minerals themselves aren't refined
    if (c.supply == Supply::Refined)
        return;
    const TypeData& type(*m_static.Type(c.typeID));
    if (type.refine.empty())
        return;
    uint32_t rp(NodeActor(c.nodeID, ActorKind::Reprocessor));
    if (rp == 0)
        return;
    double worth(RefineWorth(c.nodeID, type));
    Cents limit(Econ::ToCents(worth * (1.0 - m_p.reprocessorMargin)));
    if (limit <= 0)
        return;
    Units glut(std::max<Units>(0, MakerStock(c) - (Units)std::ceil(c.targetStock)));
    Units fromMaker((InternalBuy(c) <= limit) ? glut : 0);
    // what players offer under the limit:  up to a day of the cell's demand (and the intake caps)
    Units q(fromMaker + (Units)std::ceil(std::max(c.dBar, (double)c.lot)));
    // the reprocessor is no consumer of the type:  its buying is no demand for it
    double derived(c.derived);
    BuyAtNode(rp, c, q, limit, fromMaker, t);
    c.derived = derived;
}

void World::ReprocessorStep(Actor& a, int64_t t, double /*dt*/)
{
    std::map<uint32_t, std::set<uint16_t>>::iterator hitr = m_holdings.find(a.actorID);
    if (hitr == m_holdings.end())
        return;
    std::set<uint16_t> types(hitr->second);
    // refine everything that refines, then sell the minerals
    for (auto typeID : types) {
        const TypeData* tt(m_static.Type(typeID));
        if ((tt == nullptr) or tt->refine.empty())
            continue;
        Cell* c(FindCell(a.nodeID, typeID));
        if ((c != nullptr) and (c->supply == Supply::Refined))
            continue;
        RefineHeld(a.actorID, a.nodeID, typeID, Held(a.actorID, a.nodeID, typeID));
    }
    std::set<uint16_t> now(m_holdings[a.actorID]);
    for (auto typeID : now) {
        Units have(Held(a.actorID, a.nodeID, typeID));
        Cell* c(FindCell(a.nodeID, typeID));
        if ((have > 0) and (c != nullptr))
            SellToMaker(a.actorID, *c, have, 0, t);
        if (Held(a.actorID, a.nodeID, typeID) <= 0)
            m_holdings[a.actorID].erase(typeID);
    }
}

void World::ManufacturerStep(Actor& a, int64_t t, double /*dt*/)
{
    std::map<uint32_t, MakerPlan>::iterator pitr = m_makerPlans.find(a.actorID);
    if (pitr == m_makerPlans.end())
        return;
    MakerPlan& plan(pitr->second);
    uint32_t node(a.nodeID);
    // 1.  what it built goes to the market makers
    for (auto idx : plan.products) {
        Cell& c(m_cells[idx]);
        Units have(Held(a.actorID, node, c.typeID));
        if (have > 0)
            SellToMaker(a.actorID, c, have, 0, t);
    }

    /* 2.  planning (PLAN 6.4):  candidates ranked by profit over the market maker's stock gap, at the internal prices
     *  (output sold, inputs bought) and the line's cost;  jobs as long as the lines allow, up to MaxSimJobHours */
    uint32_t lines(m_capacity->Free(a.actorID, node, Activity::Manufacturing, t));
    if (lines == 0)
        return;
    struct Cand {
        double   score;
        uint32_t cell;
        Units    units;
        double   hoursPerUnit;
    };
    std::vector<Cand> cands;
    for (auto idx : plan.products) {
        Cell& c(m_cells[idx]);
        const TypeData& type(*m_static.Type(c.typeID));
        Units portion((Units)std::max<uint32_t>(1, type.portionSize));
        std::map<uint16_t, Units>::const_iterator w = plan.wip.find(c.typeID);
        double gap(c.targetStock - (double)MakerStock(c) - (double)Held(a.actorID, node, c.typeID)
                   - ((w == plan.wip.end()) ? 0.0 : (double)w->second));
        // a job makes whole runs:  one is worth it when the gap is half a run, or half the target of a thin type
        if (gap < std::min(0.5 * (double)portion, std::max(1.0, 0.5 * c.targetStock)))
            continue;
        double hours(Prod::BuildHours(type, m_p.buildTimeMultiplier, m_p.defaultBuildSeconds));
        double maxUnits(std::max((double)portion, std::floor(m_p.maxJobHours / std::max(1e-6, hours))));
        Units units(std::max<Units>(1, (Units)std::ceil(std::min(gap, maxUnits) / (double)portion)) * portion);
        double cost(0.0);
        for (auto& in : BomOf(c)) {
            Cell* ic(FindCell(node, in.first));
            // phase 6:  an input the npc vendors sell (a fuel block's oxygen)
            double vendor((ic == nullptr) ? VendorPrice(in.first) : 0.0);
            if ((ic == nullptr) and !(vendor > 0.0)) {
                cost = -1.0;
                break;
            }
            cost += in.second * ((ic == nullptr) ? vendor : ExpectBuy(*ic));
        }
        if (cost < 0.0)
            continue;
        SimJob probe;
        probe.start = t;
        probe.end = t + (int64_t)std::ceil(hours * (double)units * 3600.0);
        cost += m_capacity->JobCost(probe) / (double)units;
        double sell(ExpectSell(c));
        if (!(cost > 0.0) or (sell < cost * (1.0 + m_p.minMargin)))
            continue;
        // the most urgent first:  the margin times the share of the target that is missing (a cheap charge sold out
        //  comes before a dear module a little short)
        cands.push_back(Cand{(sell / cost - 1.0) * std::min(1.0, gap / std::max(1.0, c.targetStock)), c.index, units, hours});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        return (x.score != y.score) ? (x.score > y.score) : (x.cell < y.cell);
    });
    for (auto& cand : cands) {
        if (lines == 0)
            break;
        Cell& c(m_cells[cand.cell]);
        const TypeData& type(*m_static.Type(c.typeID));
        Units portion((Units)std::max<uint32_t>(1, type.portionSize));
        Units runs(cand.units / portion);
        // the inputs:  what it holds first, the rest bought at the node
        std::vector<std::pair<uint16_t, Units>> perRun;
        for (auto& in : BomOf(c))
            perRun.push_back(std::make_pair(in.first, std::max<Units>(1, (Units)std::llround(in.second * (double)portion))));
        /* only as many runs as every input allows (what it holds and what the market maker can spare):  inputs bought for
         *  a job that then can't start would sit idle while others need them.  a shortage shows in the input's stock */
        Units feasible(runs);
        for (auto& in : perRun) {
            Cell* ic(FindCell(node, in.first));
            // (the npc vendors have any amount)
            Units avail(Held(a.actorID, node, in.first) + ((ic != nullptr) ? Spare(*ic, in.second * runs)
                                                           : ((VendorPrice(in.first) > 0.0) ? in.second * runs : 0)));
            feasible = std::min<Units>(feasible, avail / in.second);
        }
        if (feasible <= 0)
            continue;
        for (auto& in : perRun) {
            Units need(in.second * feasible - Held(a.actorID, node, in.first));
            if (need <= 0)
                continue;
            Cell* ic(FindCell(node, in.first));
            if (ic != nullptr)
                BuyAtNode(a.actorID, *ic, need, InternalBuy(*ic), Spare(*ic, need), t);
            else
                BuyVendor(a.actorID, node, in.first, need);
        }
        feasible = runs;
        for (auto& in : perRun)
            feasible = std::min<Units>(feasible, Held(a.actorID, node, in.first) / in.second);
        if (feasible <= 0)
            continue;           // the money ran out
        SimJob job;
        job.actorID = a.actorID;
        job.nodeID = node;
        job.stationID = Node(node)->stationID;
        job.activity = Activity::Manufacturing;
        job.typeID = c.typeID;
        job.runs = (uint32_t)feasible;
        job.qtyOut = feasible * portion;
        job.start = t;
        job.end = t + std::max<int64_t>(60, (int64_t)std::ceil(cand.hoursPerUnit * (double)job.qtyOut * 3600.0));
        if (!m_capacity->Reserve(job, t))
            break;
        Cents lineCost(Econ::ToCents(m_capacity->JobCost(job)));
        if (m_ledger.Sink(a.actorID, lineCost, Isk::LineCost, true) != lineCost) {
            m_capacity->Release(job);
            continue;       // can't pay the line:  the inputs wait
        }
        for (auto& in : perRun) {
            m_ledger.GoodsOut(a.actorID, (uint16_t)node, in.first, in.second * feasible, Goods::Materials, false);
            plan.used[in.first] += in.second * feasible;
        }
        job.jobID = m_nextJobID++;
        m_jobs[job.jobID] = job;
        plan.wip[job.typeID] += job.qtyOut;
        m_due.push(Due{job.end, DueJob, job.jobID});
        m_jobsChanged = true;
        --lines;
    }
}

void World::CompleteJob(uint32_t jobID, int64_t t)
{
    std::map<uint32_t, SimJob>::iterator itr = m_jobs.find(jobID);
    if ((itr == m_jobs.end()) or (itr->second.end > t))
        return;
    SimJob job(itr->second);
    m_jobs.erase(itr);
    m_jobsChanged = true;
    if (ChainJob(job)) {
        ChainJobDone(job, t);       // phase 6 (SimChains.cpp)
        return;
    }
    m_capacity->Release(job);
    MakerPlan& plan(m_makerPlans[job.actorID]);
    Units& wip(plan.wip[job.typeID]);
    wip = std::max<Units>(0, wip - job.qtyOut);
    if ((job.actorID == 0) or (job.actorID > m_actors.size()))
        return;
    GoodsTo(job.actorID, job.nodeID, job.typeID, job.qtyOut, Goods::Manufactured);
    m_made[job.typeID] += job.qtyOut;
    Cell* c(FindCell(job.nodeID, job.typeID));
    if (c != nullptr)
        SellToMaker(job.actorID, *c, job.qtyOut, 0, t);
}

void World::LootStep(Actor& a, int64_t t, double dt)
{
    uint32_t node(a.nodeID), rp(NodeActor(node, ActorKind::Reprocessor));
    /* loot, LP and salvage at the rate consumers use them, less what drops from lost fits (PLAN 6.4 loot source).  more
     *  pilots rat and run missions for what fetches more:  the rate follows the price, (V / anchor)^LootPriceResponse */
    std::map<uint32_t, std::vector<uint32_t>>::const_iterator citr = m_actorCells.find(a.actorID);
    if ((citr != m_actorCells.end()) and (dt > 0.0)) {
        for (auto idx : citr->second) {
            const Cell& c(m_cells[idx]);
            double expected((c.vStar > 0.0) ? c.vStar : Mid(c));
            double response(std::pow(expected / std::max(1e-9, std::exp(c.lnAnchor)), m_p.lootPriceResponse));
            response = std::min(4.0, std::max(0.25, response));
            // (phase 6:  and what the sim's industry uses of it, datacores, decryptors, T1 modules for T2)
            double rate(std::max(0.0, c.dcGen + c.lossRate + c.minerRate + c.derivedRef - c.dropRate) * m_p.supplyFactor * response);
            Units u((Units)Rng::Poisson(rate * dt, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hLootSupply, c.typeID)));
            /* a type down to half its target is refilled within LootRestockDays, deterministically (a random refill of a
             *  thin type, a lot or two a day, leaves some shelf somewhere empty for days):  an empty shelf gets a
             *  lot at once */
            double gap(c.targetStock - (double)MakerStock(c) - (double)Held(a.actorID, node, c.typeID));
            if (gap > 0.5 * c.targetStock)
                u = std::max<Units>(u, (Units)std::ceil(gap * std::min(1.0, dt / std::max(0.01, m_p.lootRestockDays))));
            if (u > 0)
                GoodsTo(a.actorID, node, c.typeID, u, Goods::Loot);
        }
    }
    // everything it holds is offered to the market makers;  what they don't want and refines for about what they would
    //  pay goes to the reprocessor;  what nobody wants for LootHoldDays spoils
    std::map<uint32_t, std::set<uint16_t>>::iterator hitr = m_holdings.find(a.actorID);
    if (hitr == m_holdings.end())
        return;
    std::set<uint16_t> types(hitr->second);
    for (auto typeID : types) {
        Units have(Held(a.actorID, node, typeID));
        Cell* c(FindCell(node, typeID));
        if ((have > 0) and (c != nullptr)) {
            const TypeData& type(*m_static.Type(typeID));
            double worth(type.refine.empty() ? 0.0 : RefineWorth(node, type) * (1.0 - m_p.reprocessorMargin));
            have -= SellToMaker(a.actorID, *c, have, 0, t);
            if ((have > 0) and (rp != 0) and (worth > 0.0) and (worth >= 0.9 * Econ::ToIsk(InternalSell(*c)))) {
                // straight to the reprocessor at its price (an internal trade at the node)
                Cents price(Econ::ToCents(worth));
                const Ledger::Account* racct(m_ledger.FindAcct(rp));
                Units k(std::min<Units>(have, (price > 0) ? ((racct == nullptr) ? 0 : racct->wallet) / price : 0));
                if ((k > 0) and m_ledger.Transfer(rp, a.actorID, Econ::MulCents(price, k))) {
                    m_ledger.MoveGoods(a.actorID, rp, (uint16_t)node, typeID, k, false);
                    m_holdings[rp].insert(typeID);
                    Print(*c, price, k);
                    have -= k;
                }
            }
            // spoils:  more than LootHoldDays of the cell's demand (at least a few lots)
            double keep(std::max(m_p.lootHoldDays * std::max(c->dBar, c->dRef), 4.0 * c->lot));
            if ((double)have > keep) {
                Units spoil(have - (Units)std::ceil(keep));
                m_ledger.GoodsOut(a.actorID, (uint16_t)node, typeID, spoil, Goods::Spoiled, false);
            }
        } else if ((have > 0) and (c == nullptr)) {
            m_ledger.GoodsOut(a.actorID, (uint16_t)node, typeID, have, Goods::Spoiled, false);
        }
        if (Held(a.actorID, node, typeID) <= 0)
            m_holdings[a.actorID].erase(typeID);
    }
}

/************************************************************************************************************************
 * the loss model (hourly)
 */

void World::LossEvents(int64_t t, double hours)
{
    if (!(hours > 0.0))
        return;
    /* the losses (Poisson) become what consumers want back (lossWant);  the expected losses feed the cells' demand rate
     *  (lossNew):  a thin type's single loss is no burst of demand, its stock target stays where it is */
    for (auto& node : m_static.nodes) {
        uint32_t loot(NodeActor(node.nodeID, ActorKind::LootSource));
        for (auto& cur : m_nodeLosses[node.nodeID - 1]) {
            uint16_t hull(cur.first);
            double mean(cur.second * hours / 24.0 * LossShockOf(hull, t));
            Units lost((Units)Rng::Poisson(mean, Rng::Hash64(m_p.seed, node.nodeID, (uint64_t)t, hLossCount, hull)));
            Cell* hc(FindCell(node.nodeID, hull));
            if (hc != nullptr) {
                hc->lossWant += (double)lost;
                hc->lossNew += mean;
            }
            std::map<uint16_t, std::vector<FitItem>>::const_iterator fit = m_static.fits.find(hull);
            if (fit == m_static.fits.end())
                continue;
            for (auto& f : fit->second) {
                Cell* fc(FindCell(node.nodeID, f.typeID));
                if (fc == nullptr)
                    continue;
                fc->lossNew += mean * f.qtyPerLoss;
                if (lost <= 0)
                    continue;
                uint64_t salt(((uint64_t)hull << 16) | f.typeID);
                Units q(Rng::RoundRandom((double)lost * f.qtyPerLoss, Rng::Hash64(m_p.seed, node.nodeID, (uint64_t)t, hLossFit, salt)));
                if (q <= 0)
                    continue;
                fc->lossWant += (double)q;
                Units drop(Rng::RoundRandom((double)q * (1.0 - f.destroyedShare), Rng::Hash64(m_p.seed, node.nodeID, (uint64_t)t, hLossDrop, salt)));
                if ((drop > 0) and (loot != 0))
                    GoodsTo(loot, node.nodeID, f.typeID, drop, Goods::Dropped);
            }
        }
    }
}

}
