
 /**
  * @name SimChains.cpp
  *   market simulation:  the advanced chains of phase 6 (PLAN 6.4, 11 phase 6).  World's members that decide which cells
  *   the chains supply, work out what each chain needs at reference (the derived demand, from the final goods down to
  *   the raw ones), build the actors and run them:
  *     inventor (node)     T2:  invention on invention lines (a T1 copy, datacores, the decryptor it plans with) gives runs
  *                         of T2 copies by the Crucible chance (EvEMath::RAM::InventionChance, skills at V, ram.InventChance);
  *                         manufacturing on manufacturing lines uses a run of a copy and the bill at the copy's material level
  *     harvester (node)    raw moon materials (POS harvesting arrays, which burn fuel blocks) and gas, on demand up to its
  *                         capacity (the calibrated need with slack)
  *     reactor (node)      moon reactions (invTypeReactions:  intermediates, composites, polymers, booster materials) in its
  *                         arrays, which burn fuel blocks
  *     planetary (node)    extraction of planetary resources (P0) on demand, and the schematics' factories (P1 to P4)
  *   components and fuel blocks are built by the phase 3 goods manufacturer (components at their own bill, inputs the npc
  *   vendors sell bought from them).  every actor works like the phase 3 ones:  a finite wallet, real stock, inputs bought
  *   at the node (players' asks first), output sold to the market makers inside the spread;  every flow is in the ledger
  *   (faucets:  harvested, extracted, invented copies;  sinks:  materials, fuel, line costs, vendor purchases)
  *
  * @date:          16Oct26
  *
  */

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <set>
#include <sstream>

#include "market/sim/SimChains.h"
#include "market/sim/SimEconomy.h"
#include "market/sim/SimRng.h"
#include "market/sim/SimWorld.h"

namespace MktSim {

namespace {
    // hash purposes (SimWorld.cpp uses 1..10, SimActors.cpp 101..)
    enum : uint64_t {
        hInvent = 201, hHarvest, hUse, hFuel
    };
    // reaction and planetary goods follow their inputs' cost like components (PLAN 6.2 w_c)
    const double ChainPassThrough = 0.9;
    // a reaction or schematic runs while its output brings back at least this share of its inputs' and fuel's cost
    const double ChainMinRecovery = 0.5;

    std::string Fmt(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    uint64_t CellKey(uint32_t nodeID, uint16_t typeID)  { return ((uint64_t)nodeID << 16) | typeID; }
}

/************************************************************************************************************************
 * the pure part (SimChains.h)
 */

namespace Chains {

double InventionChance(double base, int encryption, int science1, int science2, int metaLevel, double decryptor)
{
    if (metaLevel > 4)
        metaLevel = 4;
    if (metaLevel < 0)
        metaLevel = 0;
    return base * (1.0 + 0.01 * encryption) * (1.0 + (science1 + science2) * (0.1 / (5.0 - metaLevel))) * decryptor;
}

int InventionRuns(int t1Runs, int t1Max, int t2Max, int decryptorRuns)
{
    if (t1Max < 1)
        t1Max = 1;
    if (t2Max < 1)
        t2Max = 1;
    int runs((int)std::floor(((double)t1Runs / t1Max) * (t2Max / 10.0)));
    if (runs < 1)
        runs = 1;
    runs += decryptorRuns;
    if (runs > t2Max)
        runs = t2Max;
    return (runs < 1) ? 1 : runs;
}

double Waste(double wasteFactor, double materialLevel)
{
    if (materialLevel >= 0.0)
        return wasteFactor / (1.0 + materialLevel);
    return wasteFactor * (1.0 - materialLevel);
}

void BillAt(const ChainBill& bill, double materialLevel, std::vector<std::pair<uint16_t, double>>& into)
{
    double waste(Waste(bill.wasteFactor, materialLevel));
    std::map<uint16_t, double> sum;
    for (auto& in : bill.base)
        sum[in.first] += in.second * (1.0 + waste);
    for (auto& in : bill.extra)
        sum[in.first] += in.second;
    into.clear();
    for (auto& cur : sum)
        if (cur.second > 0.0)
            into.push_back(cur);
}

void NetRecipe(const Recipe& r, std::vector<std::pair<uint16_t, double>>& inputs, std::vector<std::pair<uint16_t, double>>& outputs)
{
    std::map<uint16_t, double> net;
    for (auto& in : r.inputs)
        net[in.first] -= in.second;
    for (auto& out : r.outputs)
        net[out.first] += out.second;
    inputs.clear();
    outputs.clear();
    for (auto& cur : net) {
        if (cur.second < 0.0)
            inputs.push_back(std::make_pair(cur.first, -cur.second));
        else if (cur.second > 0.0)
            outputs.push_back(cur);
    }
}

Units Successes(Units attempts, double p, uint64_t hash)
{
    if ((attempts <= 0) or !(p > 0.0))
        return 0;
    if (p >= 1.0)
        return attempts;
    if (attempts > 2000) {
        double mean((double)attempts * p), sd(std::sqrt((double)attempts * p * (1.0 - p)));
        double x(std::floor(mean + sd * Rng::Normal(hash) + 0.5));
        return (Units)std::min((double)attempts, std::max(0.0, x));
    }
    Units k(0);
    for (Units i = 0; i < attempts; ++i)
        if (Rng::Uniform(Rng::Hash64(hash, (uint64_t)i)) < p)
            ++k;
    return k;
}

}

/************************************************************************************************************************
 * helpers
 */

const std::vector<std::pair<uint16_t, double>>& World::BomOf(const Cell& c) const
{
    if ((c.bill > 0) and (c.bill <= m_bills.size()))
        return m_bills[c.bill - 1];
    return m_static.Type(c.typeID)->bom;
}

double World::RefMade(uint32_t nodeID, uint16_t typeID) const
{
    std::map<uint64_t, double>::const_iterator itr = m_refMade.find(CellKey(nodeID, typeID));
    return (itr == m_refMade.end()) ? 0.0 : itr->second;
}

double World::RefDerived(uint32_t nodeID, uint16_t typeID) const
{
    std::map<uint64_t, double>::const_iterator itr = m_refDerived.find(CellKey(nodeID, typeID));
    return (itr == m_refDerived.end()) ? 0.0 : itr->second;
}

double World::VendorPrice(uint16_t typeID) const
{
    const TypeData* t(m_static.Type(typeID));
    if ((t == nullptr) or (t->simClass != SimClass::NpcVendor) or !(t->basePrice > 0.0))
        return 0.0;
    return t->basePrice;
}

Units World::BuyVendor(uint32_t actorID, uint32_t nodeID, uint16_t typeID, Units q)
{
    double price(VendorPrice(typeID));
    if ((q <= 0) or !(price > 0.0))
        return 0;
    // the npc vendors sell at basePrice (PLAN 6.4):  the isk is sunk, the goods come from the vendor faucet
    Cents unit(std::max<Cents>(1, Econ::ToCents(price)));
    const Ledger::Account* acct(m_ledger.FindAcct(actorID));
    q = std::min<Units>(q, ((acct == nullptr) ? 0 : acct->wallet) / unit);
    Cents value(Econ::MulCents(unit, q));
    if ((q <= 0) or (value <= 0) or (m_ledger.Sink(actorID, value, Isk::VendorSales, true) != value))
        return 0;
    GoodsTo(actorID, nodeID, typeID, q, Goods::Vendor);
    return q;
}

double World::InputCost(uint32_t nodeID, uint16_t typeID) const
{
    std::map<uint64_t, uint32_t>::const_iterator itr = m_cellIndex.find(CellKey(nodeID, typeID));
    if (itr != m_cellIndex.end())
        return ExpectBuy(m_cells[itr->second]);
    double vendor(VendorPrice(typeID));
    return (vendor > 0.0) ? vendor : -1.0;
}

Units World::BuyInput(uint32_t actorID, uint32_t nodeID, uint16_t typeID, Units q, int64_t t)
{
    if (q <= 0)
        return 0;
    Cell* ic(FindCell(nodeID, typeID));
    if (ic != nullptr) {
        /* what the market maker can spare (it keeps a tenth of its target);  of a thin cell, a few units on the shelf,
         *  the last ones too (else a T1 item a T2 build needs is never there):  the empty shelf raises the price */
        Units spare(Spare(*ic, q));
        if ((spare < q) and (ic->targetStock < 10.0))
            spare = std::min<Units>(q, MakerStock(*ic));
        return BuyAtNode(actorID, *ic, q, InternalBuy(*ic), spare, t);
    }
    return BuyVendor(actorID, nodeID, typeID, q);
}

double World::SupplyShockOf(uint16_t typeID, int64_t t) const
{
    std::map<uint16_t, std::pair<double, int64_t>>::const_iterator itr = m_supplyShock.find(typeID);
    return ((itr != m_supplyShock.end()) and (itr->second.second > t)) ? itr->second.first : 1.0;
}

void World::ShockSupply(uint16_t typeID, double mult, double days, int64_t now)
{
    m_supplyShock[typeID] = std::make_pair(std::max(0.0, mult), now + (int64_t)(days * SecondsPerDay));
}

/************************************************************************************************************************
 * building (BuildActors calls these:  after phase 3's supply, for the derived demand, after phase 3's actors)
 */

void World::ChainSupply()
{
    m_bills.clear();
    m_chainPending.clear();
    m_chainPlans.clear();
    double share(std::min(1.0, std::max(0.0, m_p.internalSpreadShare)));
    auto cellAt = [this](uint32_t nodeID, uint16_t typeID) -> Cell* {
        std::map<uint64_t, uint32_t>::iterator itr = m_cellIndex.find(CellKey(nodeID, typeID));
        return (itr == m_cellIndex.end()) ? nullptr : &m_cells[itr->second];
    };
    // an input's cost to plan the chains with, at reference:  its cell's internal buy price, or the npc vendors' (< 0:  none)
    auto refCost = [&](uint32_t nodeID, uint16_t typeID) -> double {
        Cell* ic(cellAt(nodeID, typeID));
        if (ic != nullptr)
            return ic->refLocal * (1.0 + share * ic->h);
        double v(VendorPrice(typeID));
        return (v > 0.0) ? v : -1.0;
    };
    auto listCost = [&](uint32_t nodeID, const std::vector<std::pair<uint16_t, double>>& in, uint16_t self) -> double {
        double cost(0.0);
        for (auto& i : in) {
            double c((i.first == self) ? -1.0 : refCost(nodeID, i.first));
            if (c < 0.0)
                return -1.0;
            cost += i.second * c;
        }
        return cost;
    };
    // a cell's own bill (CostNow, the manufacturer, the derived demand):  per unit.  its reference cost at the
    //  calibration's prices (as World::Build's costRef)
    auto setBill = [&](Cell& c, const std::vector<std::pair<uint16_t, double>>& perUnit) {
        m_bills.push_back(perUnit);
        c.bill = (uint32_t)m_bills.size();
        const NodeData& node(*Node(c.nodeID));
        c.costRef = 0.0;
        for (auto& in : perUnit) {
            const TypeData* it(m_static.Type(in.first));
            if ((it != nullptr) and (it->refPrice > 0.0))
                c.costRef += in.second * it->refPrice * m_static.RegionPrice(node.regionID, in.first);
            else
                c.costRef += in.second * VendorPrice(in.first);
        }
        if (!(c.costRef > 0.0))
            c.wc = 0.0;
    };

    // what makes what:  reactions and schematics by their outputs, inventions by the copy they give, decryptors by group
    std::map<uint16_t, std::vector<const Recipe*>> makers;
    std::map<uint16_t, const Recipe*> invention;
    for (auto& r : m_static.recipes) {
        if (r.kind == RecipeKind::Invention) {
            for (auto& out : r.outputs)
                if (invention.find(out.first) == invention.end())
                    invention[out.first] = &r;
        } else {
            for (auto& out : r.outputs)
                makers[out.first].push_back(&r);
        }
    }
    std::map<uint16_t, std::vector<const Decryptor*>> decryptorsOf;
    for (auto& d : m_static.decryptors)
        decryptorsOf[d.groupID].push_back(&d);

    // the fuel block each node's towers burn:  the one it trades most
    std::map<uint32_t, uint16_t> fuelOf;
    for (auto& c : m_cells) {
        const TypeData& t(*m_static.Type(c.typeID));
        if (t.groupID != Chains::Group::FuelBlock)
            continue;
        uint16_t& f(fuelOf[c.nodeID]);
        Cell* best((f != 0) ? cellAt(c.nodeID, f) : nullptr);
        if ((best == nullptr) or (c.d0 * c.refLocal > best->d0 * best->refLocal))
            f = c.typeID;
    }
    auto fuelCost = [&](uint32_t nodeID) -> double {
        std::map<uint32_t, uint16_t>::const_iterator itr = fuelOf.find(nodeID);
        return (itr == fuelOf.end()) ? 0.0 : std::max(0.0, refCost(nodeID, itr->second));
    };

    for (auto& c : m_cells) {
        if (c.supply != Supply::Abstract)
            continue;
        const TypeData& t(*m_static.Type(c.typeID));
        uint32_t node(c.nodeID);
        ChainProduct p;
        p.cell = c.index;

        // T2:  invented, then built from the copies (the plan picks the decryptor that makes a unit cheapest)
        if (ChainsT2() and (t.simClass == SimClass::MfgT2)) {
            std::map<uint16_t, uint16_t>::const_iterator bp = m_static.blueprints.find(t.typeID);
            std::map<uint16_t, ChainBill>::const_iterator bill = m_static.bills.find(t.typeID);
            if ((bp == m_static.blueprints.end()) or (bill == m_static.bills.end()))
                continue;
            std::map<uint16_t, const Recipe*>::const_iterator inv = invention.find(bp->second);
            if ((inv == invention.end()) or inv->second->outputs.empty())
                continue;
            const Recipe& r(*inv->second);
            double portion((double)std::max<uint32_t>(1, t.portionSize));
            double inventCost(listCost(node, r.inputs, t.typeID));
            if (inventCost < 0.0)
                continue;
            std::vector<const Decryptor*> options(1, nullptr);
            if (r.decryptorGroup != 0) {
                std::map<uint16_t, std::vector<const Decryptor*>>::const_iterator ds = decryptorsOf.find(r.decryptorGroup);
                if (ds != decryptorsOf.end())
                    for (auto d : ds->second)
                        if (cellAt(node, d->typeID) != nullptr)
                            options.push_back(d);
            }
            double best(-1.0);
            for (auto d : options) {
                double chance(std::min(1.0, Chains::InventionChance(r.chance, Chains::InventorSkillLevel, Chains::InventorSkillLevel,
                                                                    Chains::InventorSkillLevel, 0, (d != nullptr) ? d->chance : 1.0) * m_p.inventChance));
                double runs(r.outputs.front().second + ((d != nullptr) ? d->runs : 0.0));
                if (r.maxRuns > 0)
                    runs = std::min(runs, (double)r.maxRuns);
                runs = std::max(1.0, runs);
                double level(Chains::BaseInventionME + ((d != nullptr) ? d->me : 0.0));
                std::vector<std::pair<uint16_t, double>> materials;
                Chains::BillAt(bill->second, level, materials);
                double matCost(listCost(node, materials, t.typeID));
                double decCost((d != nullptr) ? refCost(node, d->typeID) : 0.0);
                if ((matCost < 0.0) or (decCost < 0.0) or !(chance > 0.0))
                    continue;
                double unit(matCost + (inventCost + decCost) / (chance * runs * portion));
                if ((best < 0.0) or (unit < best)) {
                    best = unit;
                    p.inputs = materials;
                    p.chance = chance;
                    p.runsPerSuccess = runs;
                    p.decryptor = (d != nullptr) ? d->typeID : 0;
                }
            }
            /* what sells for clearly less than its cheapest way of making it (the materials at the copy's level and its
             *  share of the attempts) isn't invented:  the stock model keeps supplying it (Crucible had researched T2
             *  originals from the lottery;  as phase 3 leaves T1 under its inputs' cost to the loot source) */
            if ((best < 0.0) or (c.refLocal * (1.0 - share * c.h) < m_p.lootCostShare * best))
                continue;
            p.process = Supply::Invented;
            p.blueprint = bp->second;
            p.unitsPerRun = portion;
            p.seconds = 3600.0 * Prod::BuildHours(t, m_p.buildTimeMultiplier, m_p.defaultBuildSeconds);
            p.invent = r.inputs;
            if (p.decryptor != 0)
                p.invent.push_back(std::make_pair(p.decryptor, 1.0));
            p.inventSeconds = std::max(60.0, r.seconds * m_p.inventionTimeMultiplier);
            // the bill a unit costs:  its materials and its share of the attempts
            std::vector<std::pair<uint16_t, double>> perUnit(p.inputs);
            double per(1.0 / (p.chance * p.runsPerSuccess * portion));
            for (auto& in : p.invent)
                perUnit.push_back(std::make_pair(in.first, in.second * per));
            c.supply = Supply::Invented;
            setBill(c, perUnit);
            m_chainPending[node].push_back(p);
            continue;
        }
        if (!ChainsMoonPI())
            continue;

        // components (T2, capital, R.A.M., hybrid tech):  the goods manufacturer, at the bill of an unresearched original
        if (t.simClass == SimClass::Component) {
            std::map<uint16_t, ChainBill>::const_iterator bill = m_static.bills.find(t.typeID);
            if (bill == m_static.bills.end())
                continue;
            std::vector<std::pair<uint16_t, double>> materials;
            Chains::BillAt(bill->second, Chains::ComponentMaterialLevel, materials);
            double cost(materials.empty() ? -1.0 : listCost(node, materials, t.typeID));
            if ((cost < 0.0) or (c.refLocal * (1.0 - share * c.h) < m_p.lootCostShare * cost))
                continue;
            c.supply = Supply::Manufactured;
            setBill(c, materials);
            continue;
        }
        /* T1 that needs what the npc vendors sell (fuel blocks:  oxygen):  as phase 3 decides T1 whose inputs all trade at
         *  the node (BuildActors), built unless it sells for clearly less than its inputs (then it is loot) */
        if ((t.simClass == SimClass::MfgT1) and !t.bom.empty()) {
            double cost(listCost(node, t.bom, t.typeID));
            if (!(cost > 0.0))
                continue;
            c.supply = (c.refLocal * (1.0 - share * c.h) < m_p.lootCostShare * cost) ? Supply::Loot : Supply::Manufactured;
            continue;
        }
        // raw moon materials (POS arrays, fuelled) and gas (ships)
        bool moon((t.simClass == SimClass::Reaction) and (t.groupID == Chains::Group::MoonMaterials));
        bool gas((t.simClass == SimClass::Mined) and (t.groupID == Chains::Group::HarvestableCloud) and t.refine.empty());
        if (moon or gas) {
            p.process = Supply::Harvested;
            p.fuelled = moon and (fuelOf.count(node) > 0) and (m_p.posFuelPerArrayHour > 0.0);
            std::map<uint16_t, double>::const_iterator amt = m_static.moonAmounts.find(t.typeID);
            p.perArrayHour = std::max(1.0, m_p.moonHarvestPerHour * ((amt != m_static.moonAmounts.end()) ? amt->second : 100.0));
            c.supply = Supply::Harvested;
            if (p.fuelled)
                setBill(c, { std::make_pair(fuelOf[node], m_p.posFuelPerArrayHour / p.perArrayHour) });
            c.wc = 0.0;
            m_chainPending[node].push_back(p);
            continue;
        }
        // planetary resources:  extracted
        if ((t.simClass == SimClass::PI) and (t.categoryID == Category::PIResource)) {
            p.process = Supply::Extracted;
            c.supply = Supply::Extracted;
            m_chainPending[node].push_back(p);
            continue;
        }
        // reaction products and planetary goods:  the recipe that makes them cheapest of those whose inputs trade here
        if ((t.simClass == SimClass::Reaction) or (t.simClass == SimClass::PI)) {
            std::map<uint16_t, std::vector<const Recipe*>>::const_iterator ms = makers.find(t.typeID);
            if (ms == makers.end())
                continue;
            double best(-1.0);
            for (auto r : ms->second) {
                if ((r->kind != RecipeKind::Reaction) and (r->kind != RecipeKind::Planetary))
                    continue;
                std::vector<std::pair<uint16_t, double>> in, out;
                Chains::NetRecipe(*r, in, out);
                double made(0.0);
                for (auto& o : out)
                    if (o.first == t.typeID)
                        made = o.second;
                double cost(listCost(node, in, t.typeID));
                if (!(made > 0.0) or (cost < 0.0) or !(r->seconds > 0.0))
                    continue;
                bool reaction(r->kind == RecipeKind::Reaction);
                if (reaction)
                    cost += fuelCost(node) * m_p.posFuelPerArrayHour * r->seconds / 3600.0;
                double unit(cost / made);
                if ((best < 0.0) or (unit < best)) {
                    best = unit;
                    p.inputs = in;
                    p.outputs = out;
                    p.unitsPerRun = made;
                    p.seconds = r->seconds;
                    p.process = reaction ? Supply::Reacted : Supply::Processed;
                    p.fuelled = reaction and (fuelOf.count(node) > 0) and (m_p.posFuelPerArrayHour > 0.0);
                }
            }
            // (a recipe that brings back less than ChainMinRecovery of its cost at reference prices is left to the stock model)
            if ((best < 0.0) or (c.refLocal * (1.0 - share * c.h) < ChainMinRecovery * best))
                continue;
            c.supply = p.process;
            std::vector<std::pair<uint16_t, double>> perUnit;
            for (auto& in : p.inputs)
                perUnit.push_back(std::make_pair(in.first, in.second / p.unitsPerRun));
            if (p.fuelled)
                perUnit.push_back(std::make_pair(fuelOf[node], m_p.posFuelPerArrayHour * p.seconds / 3600.0 / p.unitsPerRun));
            c.wc = ChainPassThrough;
            setBill(c, perUnit);
            m_chainPending[node].push_back(p);
            continue;
        }
    }
}

void World::ChainDerived(std::map<uint64_t, double>& derived)
{
    /* what the node's industry needs of each type at reference (PLAN 6.3 derived demand):  a cell made by the sim makes its
     *  final demand and what is made of it, max(dc, losses + derived) less what drops, and takes its bill of each input
     *  for that.  in depth order:  everything made of a type is worked out before the type */
    m_refMade.clear();
    auto made = [](const Cell& c) {
        switch (c.supply) {
            case Supply::Manufactured:
            case Supply::Invented:
            case Supply::Harvested:
            case Supply::Reacted:
            case Supply::Extracted:
            case Supply::Processed:
                return true;
        }
        return false;
    };
    std::vector<int> depth(m_cells.size(), -1);
    std::function<int(uint32_t)> depthOf = [&](uint32_t idx) -> int {
        if (depth[idx] >= 0)
            return depth[idx];
        depth[idx] = 0;         // (a cycle ends here)
        const Cell& c(m_cells[idx]);
        int d(0);
        if (made(c)) {
            for (auto& in : BomOf(c)) {
                std::map<uint64_t, uint32_t>::const_iterator itr = m_cellIndex.find(CellKey(c.nodeID, in.first));
                if ((itr != m_cellIndex.end()) and (itr->second != idx))
                    d = std::max(d, 1 + depthOf(itr->second));
            }
        }
        depth[idx] = d;
        return d;
    };
    std::vector<uint32_t> order;
    for (auto& c : m_cells)
        if (made(c)) {
            depthOf(c.index);
            order.push_back(c.index);
        }
    std::sort(order.begin(), order.end(), [&depth](uint32_t a, uint32_t b) {
        return (depth[a] != depth[b]) ? (depth[a] > depth[b]) : (a < b);
    });
    for (auto idx : order) {
        const Cell& c(m_cells[idx]);
        uint64_t key(CellKey(c.nodeID, c.typeID));
        std::map<uint64_t, double>::const_iterator d = derived.find(key);
        double der((d == derived.end()) ? 0.0 : d->second);
        double units(std::max(0.0, std::max(c.dc, c.lossRate + der) - c.dropRate));
        m_refMade[key] = units;
        for (auto& in : BomOf(c))
            if (in.first != c.typeID)
                derived[CellKey(c.nodeID, in.first)] += units * in.second;
    }
    for (auto& c : m_cells) {
        std::map<uint64_t, double>::const_iterator d = derived.find(CellKey(c.nodeID, c.typeID));
        c.derivedRef = (d == derived.end()) ? 0.0 : d->second;
    }
}

void World::ChainActors()
{
    double capitalDays(std::max(0.5, m_p.actorCapitalDays));
    for (auto& node : m_static.nodes) {
        std::map<uint32_t, std::vector<ChainProduct>>::iterator pitr = m_chainPending.find(node.nodeID);
        if (pitr == m_chainPending.end())
            continue;
        // the products by the actor that makes them
        std::map<uint8_t, std::vector<ChainProduct>> byKind;
        for (auto& p : pitr->second) {
            p.ref = RefMade(node.nodeID, m_cells[p.cell].typeID);
            uint8_t kind(0);
            switch (p.process) {
                case Supply::Invented:      kind = ActorKind::Inventor; break;
                case Supply::Harvested:     kind = ActorKind::Harvester; break;
                case Supply::Reacted:       kind = ActorKind::Reactor; break;
                case Supply::Extracted:
                case Supply::Processed:     kind = ActorKind::Planetary; break;
            }
            if (kind != 0)
                byKind[kind].push_back(p);
        }
        // the fuel block its towers burn
        uint16_t fuel(0);
        for (auto& kp : byKind)
            for (auto& p : kp.second)
                if (p.fuelled and (fuel == 0)) {
                    const std::vector<std::pair<uint16_t, double>>& bill(BomOf(m_cells[p.cell]));
                    for (auto& in : bill)
                        if (m_static.Type(in.first) and (m_static.Type(in.first)->groupID == Chains::Group::FuelBlock))
                            fuel = in.first;
                }
        for (auto& kp : byKind) {
            Actor a = Actor();
            a.actorID = (uint32_t)m_actors.size() + 1;
            a.kind = kp.first;
            a.ownerCorpID = m_static.ActorCorp(node.nodeID, kp.first, 0);
            a.regionID = node.regionID;
            a.nodeID = node.nodeID;
            ChainPlan& plan(m_chainPlans[a.actorID]);
            plan.products = kp.second;
            plan.fuel = fuel;
            double hours(0.0), inventHours(0.0), turnover(0.0), arrays(0.0);
            for (size_t i = 0; i < plan.products.size(); ++i) {
                ChainProduct& p(plan.products[i]);
                const Cell& c(m_cells[p.cell]);
                plan.index[c.typeID] = i;
                for (auto& in : p.inputs)
                    plan.inputs.insert(in.first);
                turnover += p.ref * std::max(c.costRef, 0.1 * c.refLocal);
                switch (p.process) {
                    case Supply::Invented:
                        hours += p.ref * p.seconds / 3600.0;
                        inventHours += p.ref / std::max(1e-9, p.chance * p.runsPerSuccess * p.unitsPerRun) * p.inventSeconds / 3600.0;
                        break;
                    case Supply::Reacted:
                    case Supply::Processed:
                        /* two arrays (factories) per product at least, and twice the slack of the lines:  towers and planets are
                         *  plentiful, and a chain's demand runs ahead of its reference while the stocks along it fill */
                        arrays += std::max(2.0, std::ceil(p.ref / std::max(1e-9, p.unitsPerRun) * p.seconds / 86400.0 * 2.0 * (1.0 + m_p.productionSlack)));
                        break;
                    case Supply::Harvested:
                    case Supply::Extracted:
                        plan.capacity[c.typeID] = p.ref * (1.0 + m_p.harvestSlack);
                        turnover += p.ref * c.refLocal;
                        break;
                }
            }
            /* lines:  the hours a day the calibrated output takes, with slack, and a few so a long job doesn't hold up the
             *  rest (as the phase 3 manufacturers:  a tenth of the products here, an invention takes hours);  arrays and
             *  factories:  one per product at least */
            size_t n(plan.products.size());
            if (hours > 0.0)
                plan.slots = (uint32_t)(std::ceil(hours / 24.0 * (1.0 + m_p.productionSlack)) + std::max(2.0, std::ceil(n / 20.0)));
            if (inventHours > 0.0)
                plan.inventSlots = (uint32_t)(std::ceil(inventHours / 24.0 * (1.0 + m_p.productionSlack)) + std::max(2.0, std::ceil(n / 10.0)));
            if (arrays > 0.0)
                plan.slots = (uint32_t)arrays + 1;
            a.capacity = (double)(plan.slots + plan.inventSlots);
            a.target = std::max(1e8, turnover * capitalDays * 100.0);
            m_actors.push_back(a);
            if (kp.first == ActorKind::Inventor)
                m_capacity->SetSlots(a.actorID, plan.slots + plan.inventSlots);
        }
    }
    m_chainPending.clear();
}

void World::ChainSeed(uint32_t nodeID, bool freshOnly)
{
    for (auto& cur : m_chainPlans) {
        const Actor& a(m_actors[cur.first - 1]);
        if ((a.kind != ActorKind::Inventor) or ((nodeID != 0) and (a.nodeID != nodeID)) or (a.state != 0))
            continue;
        // a loaded world that grew:  only an inventor that holds no copies at all
        bool holds(false);
        for (auto& p : cur.second.products)
            if (Held(a.actorID, a.nodeID, p.blueprint) > 0)
                holds = true;
        if (freshOnly and holds)
            continue;
        for (auto& p : cur.second.products) {
            const Cell& c(m_cells[p.cell]);
            Units want((Units)std::ceil(std::max(c.dBar, c.dRef) * m_p.inventBufferDays / std::max(1.0, p.unitsPerRun)));
            Units have(Held(a.actorID, a.nodeID, p.blueprint));
            if (want > have)
                GoodsTo(a.actorID, a.nodeID, p.blueprint, want - have, Goods::Seed);
        }
    }
}


/************************************************************************************************************************
 * the actors' steps (hourly, SimActors.cpp's StepActor)
 */

void World::ChainStep(Actor& a, int64_t t, double dt)
{
    std::map<uint32_t, ChainPlan>::iterator pitr = m_chainPlans.find(a.actorID);
    if (pitr == m_chainPlans.end())
        return;
    ChainPlan& plan(pitr->second);
    uint32_t node(a.nodeID);
    /* 1.  what it made goes to the market makers;  what they don't want stays (the jobs follow their stock gap).  of a
     *  product it also uses (a reactor's intermediates) only what it made and hasn't sold:  the rest are inputs it bought */
    for (auto& p : plan.products) {
        Cell& c(m_cells[p.cell]);
        Units have(Held(a.actorID, node, c.typeID));
        Units& unsold(plan.unsold[c.typeID]);
        if (plan.inputs.count(c.typeID) > 0)
            have = std::min(have, unsold);
        Units sold((have > 0) ? SellToMaker(a.actorID, c, have, 0, t) : 0);
        have -= sold;
        // harvests come whether wanted or not:  a glut beyond two days goes into the public bids, beyond five it spoils
        if ((p.process == Supply::Harvested) or (p.process == Supply::Extracted)) {
            double keep(std::max(2.0 * p.ref, 2.0 * (double)c.lot));
            if ((double)have > keep) {
                Units bid(SellToBids(a.actorID, c, have - (Units)std::ceil(keep), t));
                have -= bid;
                sold += bid;
            }
            if ((double)have > 2.5 * keep) {
                Units spoil(have - (Units)std::ceil(2.5 * keep));
                m_ledger.GoodsOut(a.actorID, (uint16_t)node, c.typeID, spoil, Goods::Spoiled, false);
                sold += spoil;
            }
        }
        unsold = std::max<Units>(0, unsold - sold);
    }
    // 2.  harvest and extraction, then jobs
    switch (a.kind) {
        case ActorKind::Harvester:
            HarvestStep(a, plan, t, dt);
            break;
        case ActorKind::Reactor:
            ProcessStep(a, plan, t);
            break;
        case ActorKind::Planetary:
            HarvestStep(a, plan, t, dt);
            ProcessStep(a, plan, t);
            break;
        case ActorKind::Inventor:
            InventorStep(a, plan, t);
            break;
    }
}

double World::BurnFuel(Actor& a, ChainPlan& plan, double arrayHours, int64_t t)
{
    if ((plan.fuel == 0) or !(m_p.posFuelPerArrayHour > 0.0) or !(arrayHours > 0.0))
        return 1.0;
    uint32_t node(a.nodeID);
    double want(arrayHours * m_p.posFuelPerArrayHour);
    Units need((Units)std::ceil(want - 1e-9));
    Units have(Held(a.actorID, node, plan.fuel));
    if (have < need)
        BuyInput(a.actorID, node, plan.fuel, need - have, t);
    have = Held(a.actorID, node, plan.fuel);
    // the towers burn what the hours take (the fraction of a block, now or later:  rounded at random)
    Units burn(std::min<Units>(have, Rng::RoundRandom(want, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hFuel))));
    if (burn > 0)
        m_ledger.GoodsOut(a.actorID, (uint16_t)node, plan.fuel, burn, Goods::Fuel, false);
    plan.fuelBurnt += (double)burn;
    return (have >= need) ? 1.0 : std::max(0.0, std::min(1.0, (double)have / want));
}

void World::HarvestStep(Actor& a, ChainPlan& plan, int64_t t, double dt)
{
    if (!(dt > 0.0))
        return;
    uint32_t node(a.nodeID);
    /* on demand (like the miners):  what the cell needs now, its demand plus refilling the market maker to its target within
     *  RestockDays, less what the actor holds, up to its capacity (x a supply shock).  fuelled arrays burn fuel blocks for
     *  the hours the harvest takes;  short of fuel, the harvest is short */
    for (auto& p : plan.products) {
        if ((p.process != Supply::Harvested) and (p.process != Supply::Extracted))
            continue;
        Cell& c(m_cells[p.cell]);
        double cap(plan.capacity[c.typeID] * SupplyShockOf(c.typeID, t));
        double gap(c.targetStock - (double)MakerStock(c) - (double)Held(a.actorID, node, c.typeID));
        double rate(std::min(cap, std::max(0.0, c.dBar + gap / std::max(0.1, m_p.restockDays))));
        double units(rate * dt);
        if (!(units > 0.0))
            continue;
        if (p.fuelled)
            units *= BurnFuel(a, plan, units / std::max(1.0, p.perArrayHour), t);
        Units u(Rng::RoundRandom(units, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hHarvest, c.typeID)));
        if (u <= 0)
            continue;
        GoodsTo(a.actorID, node, c.typeID, u, (p.process == Supply::Harvested) ? Goods::Harvested : Goods::Extracted);
        plan.made[c.typeID] += u;
        plan.unsold[c.typeID] += u - SellToMaker(a.actorID, c, u, 0, t);
    }
}

void World::ProcessStep(Actor& a, ChainPlan& plan, int64_t t)
{
    uint32_t node(a.nodeID);
    if (plan.used >= plan.slots)
        return;
    uint32_t room(plan.slots - plan.used);
    /* reactions and factories (PLAN 6.4):  the products ranked by margin and the market maker's stock gap, at the internal
     *  prices and the fuel the arrays burn;  a job runs as many cycles as the gap needs, up to MaxSimJobHours.  the sim
     *  is a fraction of TQ (EconomyScale):  a cycle's batch (200 units of a reaction) can be weeks of a small node's demand,
     *  so a job makes the units the gap needs and runs the share of the cycles they take (inputs and fuel in proportion) */
    struct Cand {
        double   score;
        size_t   product;
        Units    units;
    };
    std::vector<Cand> cands;
    double fuelUnit((plan.fuel != 0) ? std::max(0.0, InputCost(node, plan.fuel)) : 0.0);
    for (size_t i = 0; i < plan.products.size(); ++i) {
        const ChainProduct& p(plan.products[i]);
        if ((p.process != Supply::Reacted) and (p.process != Supply::Processed))
            continue;
        const Cell& c(m_cells[p.cell]);
        std::map<uint16_t, double>::const_iterator w = plan.wip.find(c.typeID);
        double gap(c.targetStock - (double)MakerStock(c) - (double)Held(a.actorID, node, c.typeID) - ((w == plan.wip.end()) ? 0.0 : w->second));
        // (a tenth of the target missing:  a job a day for a thin product, several for a busy one)
        if (gap < std::max(1.0, 0.1 * c.targetStock))
            continue;
        /* a job makes at most half a day of the product's demand (and MaxSimJobHours of cycles):  a day's batch bought at
         *  once would empty its inputs' shelves */
        double maxUnits(std::max(1.0, std::min(std::floor(m_p.maxJobHours * 3600.0 / std::max(1.0, p.seconds) * p.unitsPerRun),
                                               std::ceil(0.5 * std::max(c.dBar, c.dRef)))));
        double units(std::max(1.0, std::ceil(gap)));
        double cost(0.0);
        for (auto& in : p.inputs) {
            double ic(InputCost(node, in.first));
            if (ic < 0.0) {
                cost = -1.0;
                break;
            }
            cost += in.second * ic;
        }
        if (cost < 0.0)
            continue;
        if (p.fuelled)
            cost += fuelUnit * m_p.posFuelPerArrayHour * p.seconds / 3600.0;
        double value(0.0);
        for (auto& out : p.outputs) {
            std::map<uint64_t, uint32_t>::const_iterator oc = m_cellIndex.find(CellKey(node, out.first));
            if (oc != m_cellIndex.end())
                value += out.second * ExpectSell(m_cells[oc->second]);
        }
        /* the towers and planets run on demand, like the miners:  a moon alliance runs its reactions on its own goo, the
         *  chain pays as a whole (Crucible's prices leave some reactions and schematics under their inputs' cost:  the
         *  actor's money is topped up as the other actors' is).  only a cycle that would bring back less than half its
         *  cost waits for the price to rise */
        if (!(cost > 0.0) or (value < ChainMinRecovery * cost))
            continue;
        // as many jobs (arrays, factories) as the gap takes:  every product's first job before anyone's second
        double score((value / cost) * std::min(1.0, gap / std::max(1.0, c.targetStock)));
        for (int j = 0; (units > 0.0) and (j < 256); ++j) {
            cands.push_back(Cand{score / (1.0 + j), i, (Units)std::min(maxUnits, units)});
            units -= maxUnits;
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        return (x.score != y.score) ? (x.score > y.score) : (x.product < y.product);
    });
    for (auto& cand : cands) {
        if (room == 0)
            break;
        const ChainProduct& p(plan.products[cand.product]);
        const Cell& c(m_cells[p.cell]);
        // the inputs:  what it holds first, the rest bought.  only the units every input allows
        Units units(cand.units);
        double per(1.0 / std::max(1e-9, p.unitsPerRun));
        for (auto& in : p.inputs) {
            Units need((Units)std::ceil(in.second * per * (double)units - 1e-9) - Held(a.actorID, node, in.first));
            if (need > 0)
                BuyInput(a.actorID, node, in.first, need, t);
        }
        for (auto& in : p.inputs)
            units = std::min<Units>(units, (Units)std::floor((double)Held(a.actorID, node, in.first) / (in.second * per) + 1e-9));
        if (units <= 0)
            continue;
        if (p.fuelled) {
            double share(BurnFuel(a, plan, (double)units * per * p.seconds / 3600.0, t));
            units = (Units)std::floor((double)units * share + 1e-9);
            if (units <= 0)
                continue;
        }
        // (a fraction of a unit:  rounded at random)
        uint64_t salt(0);
        for (auto& in : p.inputs) {
            Units use(std::min<Units>(Held(a.actorID, node, in.first),
                                      Rng::RoundRandom(in.second * per * (double)units, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hUse, c.typeID + (++salt << 16)))));
            if (use > 0)
                m_ledger.GoodsOut(a.actorID, (uint16_t)node, in.first, use, Goods::Materials, false);
        }
        SimJob job;
        job.jobID = m_nextJobID++;
        job.actorID = a.actorID;
        job.nodeID = node;
        job.stationID = Node(node)->stationID;
        job.activity = (p.process == Supply::Reacted) ? Activity::Reaction : Activity::Planetary;
        job.typeID = c.typeID;
        job.runs = (uint32_t)std::ceil((double)units * per - 1e-9);
        job.qtyOut = units;
        job.start = t;
        job.end = t + std::max<int64_t>(60, (int64_t)std::ceil(p.seconds * per * (double)units));
        m_jobs[job.jobID] = job;
        plan.wip[c.typeID] += (double)job.qtyOut;
        ++plan.used;
        --room;
        m_due.push(Due{job.end, DueJob, job.jobID});
        m_jobsChanged = true;
    }
}

void World::InventorStep(Actor& a, ChainPlan& plan, int64_t t)
{
    uint32_t node(a.nodeID);
    struct Cand {
        double   score;
        size_t   product;
        Units    n;         // units to build, attempts to make
    };
    auto inputsCost = [&](const std::vector<std::pair<uint16_t, double>>& in) -> double {
        double cost(0.0);
        for (auto& i : in) {
            double ic(InputCost(node, i.first));
            if (ic < 0.0)
                return -1.0;
            cost += i.second * ic;
        }
        return cost;
    };

    /* 1.  manufacturing from the copies it holds (PLAN 6.4):  ranked by margin and the market maker's stock gap, the copies'
     *  cost is spent already;  a job as long as MaxSimJobHours allows */
    uint32_t room((plan.slots > plan.used) ? std::min(plan.slots - plan.used, m_capacity->Free(a.actorID, node, Activity::Manufacturing, t)) : 0);
    std::vector<Cand> cands;
    if (room > 0) {
        for (size_t i = 0; i < plan.products.size(); ++i) {
            const ChainProduct& p(plan.products[i]);
            const Cell& c(m_cells[p.cell]);
            Units copies(Held(a.actorID, node, p.blueprint));
            if (copies <= 0)
                continue;
            std::map<uint16_t, double>::const_iterator w = plan.wip.find(c.typeID);
            double gap(c.targetStock - (double)MakerStock(c) - (double)Held(a.actorID, node, c.typeID) - ((w == plan.wip.end()) ? 0.0 : w->second));
            /* the sim is a fraction of TQ (EconomyScale):  a run of a T2 charge (5000) is days of a small node's demand, so a
             *  job makes what the gap needs (a share of a run:  the copy's runs are used in proportion, rounded at random) */
            if (gap < std::max(1.0, 0.1 * c.targetStock))
                continue;
            // (at most half a day of its demand at once, as the reactions:  a long job bought at once empties its inputs' shelves)
            double maxUnits(std::max(1.0, std::min(std::floor(m_p.maxJobHours * 3600.0 / std::max(1e-6, p.seconds)),
                                                   std::ceil(0.5 * std::max(c.dBar, c.dRef)))));
            Units units((Units)std::max(1.0, std::min(std::ceil(gap), maxUnits)));
            // but enough that the line's installation stays a small part of the cost (at most a run:  the rest is held)
            {
                SimJob one;
                one.start = t;
                one.end = t + (int64_t)std::ceil(p.seconds);
                double sell(ExpectSell(c));
                if (sell > 0.0)
                    units = std::max<Units>(units, (Units)std::min(p.unitsPerRun, std::ceil(m_capacity->JobCost(one) / (0.05 * sell))));
            }
            units = std::min<Units>(units, copies * (Units)p.unitsPerRun);
            double cost(inputsCost(p.inputs));
            if (cost < 0.0)
                continue;
            SimJob probe;
            probe.start = t;
            probe.end = t + (int64_t)std::ceil(p.seconds * (double)units);
            cost += m_capacity->JobCost(probe) / (double)units;
            double sell(ExpectSell(c));
            if (!(cost > 0.0) or (sell < cost * (1.0 + m_p.minMargin)))
                continue;
            cands.push_back(Cand{(sell / cost - 1.0) * std::min(1.0, gap / std::max(1.0, c.targetStock)), i, units});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        return (x.score != y.score) ? (x.score > y.score) : (x.product < y.product);
    });
    for (auto& cand : cands) {
        if (room == 0)
            break;
        const ChainProduct& p(plan.products[cand.product]);
        const Cell& c(m_cells[p.cell]);
        Units units(cand.n);
        for (auto& in : p.inputs) {
            Units need((Units)std::ceil(in.second * (double)units - 1e-9) - Held(a.actorID, node, in.first));
            if (need > 0)
                BuyInput(a.actorID, node, in.first, need, t);
        }
        // the units every material allows
        for (auto& in : p.inputs)
            if (in.second > 0.0)
                units = std::min<Units>(units, (Units)std::floor((double)Held(a.actorID, node, in.first) / in.second + 1e-9));
        if (units <= 0)
            continue;
        Units runs((Units)std::ceil((double)units / p.unitsPerRun - 1e-9));
        SimJob job;
        job.actorID = a.actorID;
        job.nodeID = node;
        job.stationID = Node(node)->stationID;
        job.activity = Activity::Manufacturing;
        job.typeID = c.typeID;
        job.runs = (uint32_t)runs;
        job.qtyOut = units;
        job.start = t;
        job.end = t + std::max<int64_t>(60, (int64_t)std::ceil(p.seconds * (double)units));
        if (!m_capacity->Reserve(job, t))
            break;
        Cents lineCost(Econ::ToCents(m_capacity->JobCost(job)));
        if (m_ledger.Sink(a.actorID, lineCost, Isk::LineCost, true) != lineCost) {
            m_capacity->Release(job);
            continue;
        }
        // the materials (a fraction of a unit, R.A.M.'s wear:  rounded at random) and the copy's runs
        uint64_t salt(0);
        for (auto& in : p.inputs) {
            Units use(std::min<Units>(Held(a.actorID, node, in.first),
                                      Rng::RoundRandom(in.second * (double)units, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hUse, ++salt))));
            if (use > 0)
                m_ledger.GoodsOut(a.actorID, (uint16_t)node, in.first, use, Goods::Materials, false);
        }
        Units used(std::min<Units>(Held(a.actorID, node, p.blueprint),
                                   Rng::RoundRandom((double)units / p.unitsPerRun, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hUse, ++salt))));
        if (used > 0)
            m_ledger.GoodsOut(a.actorID, (uint16_t)node, p.blueprint, used, Goods::Materials, false);
        job.jobID = m_nextJobID++;
        m_jobs[job.jobID] = job;
        plan.wip[c.typeID] += (double)units;
        ++plan.used;
        --room;
        m_due.push(Due{job.end, DueJob, job.jobID});
        m_jobsChanged = true;
    }

    /* 2.  invention (PLAN 6.4):  copies for InventBufferDays of each product's demand and its stock gap, less what it holds
     *  and the runs its attempts running now are expected to give.  an attempt is made when its expected runs are worth more
     *  (at the product's margin over its materials) than the datacores, the decryptor and the line.  the attempts' outcome
     *  (the Crucible chance) is drawn when they start, the copies come when the job ends */
    room = (plan.inventSlots > plan.inventUsed) ? std::min(plan.inventSlots - plan.inventUsed, m_capacity->Free(a.actorID, node, Activity::Invention, t)) : 0;
    if (room == 0)
        return;
    cands.clear();
    for (size_t i = 0; i < plan.products.size(); ++i) {
        const ChainProduct& p(plan.products[i]);
        const Cell& c(m_cells[p.cell]);
        std::map<uint16_t, double>::const_iterator w = plan.wip.find(c.typeID);
        double gap(c.targetStock - (double)MakerStock(c) - (double)Held(a.actorID, node, c.typeID) - ((w == plan.wip.end()) ? 0.0 : w->second));
        double target(std::ceil((std::max(c.dBar, c.dRef) * m_p.inventBufferDays + std::max(0.0, gap)) / p.unitsPerRun));
        std::map<uint16_t, double>::const_iterator cw = plan.wip.find(p.blueprint);
        double have((double)Held(a.actorID, node, p.blueprint) + ((cw == plan.wip.end()) ? 0.0 : cw->second));
        double missing(target - have);
        if (missing < 0.5)
            continue;
        double perAttempt(p.chance * p.runsPerSuccess);
        if (!(perAttempt > 0.0))
            continue;
        double maxAttempts(std::max(1.0, std::floor(m_p.maxJobHours * 3600.0 / std::max(1.0, p.inventSeconds))));
        double attempts(std::max(1.0, std::ceil(missing / perAttempt)));
        Units n((Units)std::min(maxAttempts, attempts));
        double cost(inputsCost(p.invent)), materials(inputsCost(p.inputs));
        if ((cost < 0.0) or (materials < 0.0))
            continue;
        SimJob probe;
        probe.start = t;
        probe.end = t + (int64_t)std::ceil(p.inventSeconds * (double)n);
        cost += m_capacity->JobCost(probe) / (double)n;
        double value(perAttempt * p.unitsPerRun * (ExpectSell(c) - materials));
        if (!(cost > 0.0) or (value < cost * (1.0 + m_p.minMargin)))
            continue;
        // as many jobs as the attempts take (a line runs them one after the other):  every product's first job first
        double score((value / cost - 1.0) * std::min(1.0, missing / std::max(1.0, target)));
        for (int j = 0; (attempts > 0.0) and (j < 16); ++j) {
            cands.push_back(Cand{score / (1.0 + j), i, (Units)std::min(maxAttempts, attempts)});
            attempts -= maxAttempts;
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        return (x.score != y.score) ? (x.score > y.score) : (x.product < y.product);
    });
    for (auto& cand : cands) {
        if (room == 0)
            break;
        const ChainProduct& p(plan.products[cand.product]);
        Units n(cand.n);
        for (auto& in : p.invent) {
            Units need((Units)std::ceil(in.second * (double)n - 1e-9) - Held(a.actorID, node, in.first));
            if (need > 0)
                BuyInput(a.actorID, node, in.first, need, t);
        }
        for (auto& in : p.invent)
            if (in.second > 0.0)
                n = std::min<Units>(n, (Units)std::floor((double)Held(a.actorID, node, in.first) / in.second + 1e-9));
        if (n <= 0)
            continue;
        // (salted with the job number:  a product's several jobs of a step draw apart)
        Units successes(Chains::Successes(n, p.chance, Rng::Hash64(m_p.seed, a.actorID, (uint64_t)t, hInvent, ((uint64_t)m_nextJobID << 16) | p.blueprint)));
        SimJob job;
        job.actorID = a.actorID;
        job.nodeID = node;
        job.stationID = Node(node)->stationID;
        job.activity = Activity::Invention;
        job.typeID = p.blueprint;
        job.runs = (uint32_t)n;
        job.qtyOut = successes * (Units)std::llround(p.runsPerSuccess);
        job.start = t;
        job.end = t + std::max<int64_t>(60, (int64_t)std::ceil(p.inventSeconds * (double)n));
        if (!m_capacity->Reserve(job, t))
            break;
        Cents lineCost(Econ::ToCents(m_capacity->JobCost(job)));
        if (m_ledger.Sink(a.actorID, lineCost, Isk::LineCost, true) != lineCost) {
            m_capacity->Release(job);
            continue;
        }
        for (auto& in : p.invent) {
            Units use((Units)std::llround(in.second * (double)n));
            if (use > 0)
                m_ledger.GoodsOut(a.actorID, (uint16_t)node, in.first, std::min(use, Held(a.actorID, node, in.first)), Goods::Materials, false);
        }
        m_inventStats.attempts += n;
        m_inventStats.successes += successes;
        m_inventStats.expected += (double)n * p.chance;
        m_inventStats.variance += (double)n * p.chance * (1.0 - p.chance);
        job.jobID = m_nextJobID++;
        m_jobs[job.jobID] = job;
        plan.wip[p.blueprint] += (double)n * p.chance * p.runsPerSuccess;
        ++plan.inventUsed;
        --room;
        m_due.push(Due{job.end, DueJob, job.jobID});
        m_jobsChanged = true;
    }
}

/************************************************************************************************************************
 * jobs
 */

bool World::ChainJob(const SimJob& job) const
{
    if ((job.actorID == 0) or (job.actorID > m_actors.size()))
        return false;
    return m_actors[job.actorID - 1].kind >= ActorKind::Inventor;
}

void World::ChainJobDone(const SimJob& job, int64_t t)
{
    std::map<uint32_t, ChainPlan>::iterator pitr = m_chainPlans.find(job.actorID);
    bool line((job.activity == Activity::Manufacturing) or (job.activity == Activity::Invention));
    if (line)
        m_capacity->Release(job);
    if (pitr == m_chainPlans.end())
        return;
    ChainPlan& plan(pitr->second);
    const Actor& a(m_actors[job.actorID - 1]);
    uint32_t node(a.nodeID);
    if (job.activity == Activity::Invention) {
        if (plan.inventUsed > 0)
            --plan.inventUsed;
        // the runs the attempts were expected to give leave the work in progress, the copies they gave come in
        double expected(0.0);
        for (auto& p : plan.products)
            if (p.blueprint == job.typeID) {
                expected = (double)job.runs * p.chance * p.runsPerSuccess;
                break;
            }
        double& wip(plan.wip[job.typeID]);
        wip = std::max(0.0, wip - expected);
        if (job.qtyOut > 0)
            GoodsTo(job.actorID, node, job.typeID, job.qtyOut, Goods::Invented);
        return;
    }
    if (plan.used > 0)
        --plan.used;
    double& wip(plan.wip[job.typeID]);
    wip = std::max(0.0, wip - (double)job.qtyOut);
    GoodsTo(job.actorID, node, job.typeID, job.qtyOut, Goods::Manufactured);
    plan.made[job.typeID] += job.qtyOut;
    m_made[job.typeID] += job.qtyOut;
    // a reaction's other outputs (what the node trades of them;  the rest stays in the tower)
    std::map<uint16_t, size_t>::const_iterator pi = plan.index.find(job.typeID);
    if (pi != plan.index.end()) {
        for (auto& out : plan.products[pi->second].outputs) {
            if ((out.first == job.typeID) or (FindCell(node, out.first) == nullptr))
                continue;
            const ChainProduct& p(plan.products[pi->second]);
            Units q((Units)std::floor(out.second / std::max(1e-9, p.unitsPerRun) * (double)job.qtyOut + 1e-9));
            GoodsTo(job.actorID, node, out.first, q, Goods::Manufactured);
            Cell* oc(FindCell(node, out.first));
            plan.unsold[out.first] += q - ((oc != nullptr) ? SellToMaker(job.actorID, *oc, q, 0, t) : 0);
        }
    }
    Cell* c(FindCell(node, job.typeID));
    plan.unsold[job.typeID] += job.qtyOut - ((c != nullptr) ? SellToMaker(job.actorID, *c, job.qtyOut, 0, t) : 0);
}

bool World::ChainLoadJob(const SimJob& job)
{
    const Actor& a(m_actors[job.actorID - 1]);
    std::map<uint32_t, ChainPlan>::iterator pitr = m_chainPlans.find(job.actorID);
    if ((pitr == m_chainPlans.end()) or ((job.activity != Activity::Invention) and (job.qtyOut <= 0)))
        return false;
    ChainPlan& plan(pitr->second);
    const ChainProduct* p(nullptr);
    for (auto& cur : plan.products)
        if ((job.activity == Activity::Invention) ? (cur.blueprint == job.typeID) : (m_cells[cur.cell].typeID == job.typeID)) {
            p = &cur;
            break;
        }
    if (p == nullptr)
        return false;
    SimJob j(job);
    if (j.nodeID == 0)
        j.nodeID = a.nodeID;
    if (j.stationID == 0)
        j.stationID = Node(j.nodeID)->stationID;
    if ((j.jobID == 0) or (m_jobs.find(j.jobID) != m_jobs.end()))
        j.jobID = m_nextJobID++;
    m_jobs[j.jobID] = j;
    m_nextJobID = std::max(m_nextJobID, j.jobID + 1);
    if ((j.activity == Activity::Manufacturing) or (j.activity == Activity::Invention))
        m_capacity->Restore(j);
    if (j.activity == Activity::Invention) {
        ++plan.inventUsed;
        plan.wip[j.typeID] += (double)j.runs * p->chance * p->runsPerSuccess;
    } else {
        ++plan.used;
        plan.wip[j.typeID] += (double)j.qtyOut;
    }
    return true;
}

/************************************************************************************************************************
 * GM
 */

std::string World::ChainCellInfo(const Cell& c) const
{
    const char* how(nullptr);
    switch (c.supply) {
        case Supply::Invented:      how = "invented and built"; break;
        case Supply::Harvested:     how = "harvested"; break;
        case Supply::Reacted:       how = "reacted"; break;
        case Supply::Extracted:     how = "extracted"; break;
        case Supply::Processed:     how = "planetary factories"; break;
        case Supply::Manufactured:  how = (c.bill != 0) ? "manufactured (its own bill)" : nullptr; break;
    }
    if (how == nullptr)
        return "";
    std::ostringstream out;
    out << Fmt("  chain:  %s;  reference %.4g/day made, %.4g/day used by the sim's industry\n", how, RefMade(c.nodeID, c.typeID),
               RefDerived(c.nodeID, c.typeID));
    const ChainProduct* p(nullptr);
    uint32_t actorID(0);
    for (auto& cur : m_chainPlans) {
        if (m_actors[cur.first - 1].nodeID != c.nodeID)
            continue;
        std::map<uint16_t, size_t>::const_iterator pi = cur.second.index.find(c.typeID);
        if (pi == cur.second.index.end())
            continue;
        p = &cur.second.products[pi->second];
        actorID = cur.first;
        std::map<uint16_t, double>::const_iterator w = cur.second.wip.find(c.typeID);
        out << Fmt("  actor %u:  holds %lld, in jobs %.0f", actorID, (long long)Held(actorID, c.nodeID, c.typeID),
                   (w == cur.second.wip.end()) ? 0.0 : w->second);
        if (p->process == Supply::Invented) {
            std::map<uint16_t, double>::const_iterator cw = cur.second.wip.find(p->blueprint);
            out << Fmt(";  copies of %u:  %lld runs held, %.1f expected from attempts;  chance %.3f, %.0f runs a success%s",
                       p->blueprint, (long long)Held(actorID, c.nodeID, p->blueprint), (cw == cur.second.wip.end()) ? 0.0 : cw->second,
                       p->chance, p->runsPerSuccess, (p->decryptor != 0) ? Fmt(", decryptor %u", p->decryptor).c_str() : "");
        } else if ((p->process == Supply::Harvested) or (p->process == Supply::Extracted)) {
            std::map<uint16_t, double>::const_iterator cap = cur.second.capacity.find(c.typeID);
            int64_t now(c.lastTime);
            out << Fmt(";  capacity %.4g/day (x%.2f)", (cap == cur.second.capacity.end()) ? 0.0 : cap->second, SupplyShockOf(c.typeID, now));
        }
        out << "\n";
        break;
    }
    // the inputs:  what the node's market maker has of each (of its target), and what a unit costs against what it sells for
    const std::vector<std::pair<uint16_t, double>>& bill(BomOf(c));
    if (!bill.empty()) {
        double cost(0.0);
        out << "  inputs a unit (market maker's stock / target):";
        for (auto& in : bill) {
            std::map<uint64_t, uint32_t>::const_iterator ic = m_cellIndex.find(CellKey(c.nodeID, in.first));
            double unit(InputCost(c.nodeID, in.first));
            cost += in.second * std::max(0.0, unit);
            if (ic != m_cellIndex.end())
                out << Fmt("  %u x%.4g %lld/%.0f", in.first, in.second, (long long)MakerStock(m_cells[ic->second]), m_cells[ic->second].targetStock);
            else
                out << Fmt("  %u x%.4g %s", in.first, in.second, (unit > 0.0) ? "(npc vendor)" : "(NOT TRADED HERE)");
        }
        out << Fmt("\n  a unit costs %.2f (inputs at the internal price), sells for %.2f\n", cost, ExpectSell(c));
    }
    return out.str();
}

std::string World::ChainInfo(uint32_t actorID) const
{
    std::map<uint32_t, ChainPlan>::const_iterator pitr = m_chainPlans.find(actorID);
    if (pitr == m_chainPlans.end())
        return "";
    const ChainPlan& plan(pitr->second);
    std::ostringstream out;
    out << Fmt("  %u products;  lines/arrays %u (%u busy)", (uint32_t)plan.products.size(), plan.slots, plan.used);
    if (plan.inventSlots > 0)
        out << Fmt(", invention lines %u (%u busy)", plan.inventSlots, plan.inventUsed);
    if (plan.fuel != 0)
        out << Fmt(";  burns fuel block %u (%.0f since the start)", plan.fuel, plan.fuelBurnt);
    out << "\n";
    if (m_actors[actorID - 1].kind == ActorKind::Inventor)
        out << Fmt("  invention (all inventors):  %lld attempts, %lld successes, %.1f expected\n", (long long)m_inventStats.attempts,
                   (long long)m_inventStats.successes, m_inventStats.expected);
    std::vector<std::pair<Units, uint16_t>> top;
    for (auto& cur : plan.made)
        top.push_back(std::make_pair(cur.second, cur.first));
    std::sort(top.begin(), top.end(), [](const std::pair<Units, uint16_t>& x, const std::pair<Units, uint16_t>& y) {
        return (x.first != y.first) ? (x.first > y.first) : (x.second < y.second);
    });
    if (!top.empty()) {
        out << "  made since the start:";
        for (size_t i = 0; (i < top.size()) and (i < 12); ++i) {
            std::map<uint16_t, size_t>::const_iterator pi = plan.index.find(top[i].second);
            double ref((pi != plan.index.end()) ? plan.products[pi->second].ref : 0.0);
            out << Fmt("  %u x%lld (ref %.4g/day)", top[i].second, (long long)top[i].first, ref);
        }
        out << "\n";
    }
    return out.str();
}

}
