
 /**
  * @name HarnessData.cpp
  *   the harness' static data
  *
  * @date:          13Oct26
  *
  */

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "HarnessData.h"
#include "Json.h"
#include "market/sim/SimRng.h"

namespace MktHarness {

using namespace MktSim;

void AddHubs(StaticData& data)
{
    // Jita IV-4 CNAP, Amarr VIII EFA, Dodixie IX-20 FNAP, Rens VI-8 BTT, Hek VIII-12 BCF
    struct Hub { uint32_t station, system, region, owner; float sec; };
    static const Hub hubs[] = {
        { 60003760, 30000142, 10000002, 1000035, 0.946f },
        { 60008494, 30002187, 10000043, 1000086, 1.000f },
        { 60011866, 30002659, 10000032, 1000120, 0.868f },
        { 60004588, 30002510, 10000030, 1000049, 0.895f },
        { 60005686, 30002053, 10000042, 1000057, 0.550f },
    };
    data.nodes.clear();
    for (auto& h : hubs) {
        NodeData n = NodeData();
        n.nodeID = (uint32_t)data.nodes.size() + 1;
        n.stationID = h.station;
        n.systemID = h.system;
        n.regionID = h.region;
        n.kind = NodeKind::Hub;
        n.security = h.sec;
        data.nodes.push_back(n);
        data.stationOwner[h.station] = h.owner;
        // one corp per sector so the books show a few names
        for (uint8_t s = 0; s < Sector::Count; ++s)
            data.mmCorp[std::make_pair(n.nodeID, s)] = h.owner + s;
    }
    // the hubs' share of the units traded (PLAN 4.1 concentration, units ratios against the Forge)
    static const std::pair<uint32_t, float> shares[] = {
        { 10000002, 0.62f }, { 10000043, 0.13f }, { 10000032, 0.08f }, { 10000030, 0.075f }, { 10000042, 0.04f } };
    for (auto& s : shares) {
        RegionDefault d = RegionDefault();
        d.priceRatio = 1.0f;
        d.unitsShare = s.second;
        d.iskShare = s.second;
        data.regionDefault[s.first] = d;
    }
}

namespace {
    struct Gen {
        uint64_t seed, n;
        explicit Gen(uint64_t s) : seed(s), n(0) { }
        double U()      { return Rng::Uniform(Rng::Hash64(seed, ++n)); }
        double N()      { return Rng::Normal(Rng::Hash64(seed, ++n)); }
        double LogN(double median, double sd) { return median * std::exp(sd * N()); }
        size_t Pick(size_t count) { return (size_t)(U() * count) % count; }
    };

    const uint16_t Minerals[8] = { 34, 35, 36, 37, 38, 39, 40, 11399 };

    TypeData Base(uint16_t typeID, uint16_t groupID, uint8_t category, uint8_t simClass)
    {
        TypeData t;
        t.typeID = typeID;
        t.groupID = groupID;
        t.categoryID = category;
        t.simClass = simClass;
        t.portionSize = 1;
        t.lotSize = 1;
        t.activeShare = 1.0f;
        t.confidence = 1.0f;
        t.priceSource = 1;
        return t;
    }
}

void BuildSynthetic(StaticData& data, uint64_t seed, double scale)
{
    data.types.clear();
    data.groups.clear();
    data.region.clear();
    data.vendors.clear();
    data.losses.clear();
    data.fits.clear();
    data.recipes.clear();
    data.bills.clear();
    data.blueprints.clear();
    data.decryptors.clear();
    data.moonAmounts.clear();
    AddHubs(data);
    Gen g(seed ^ 0x5EEDDA7Aull);
    auto count = [scale](int n) { return std::max(1, (int)std::lround(n * scale)); };

    // volatilities throughout:  the median of the real calibration's history-priced types of the same class and
    //  category (mktCalType.volatility, 5-day horizon, PLAN 4.1 calibration note), with a log-normal spread

    // minerals (PLAN 4.1:  Crucible window Forge prices, units of the order of the expanded consumption)
    static const double mPrice[8] = { 4.5, 10.0, 42.0, 70.0, 420.0, 830.0, 2600.0, 7300.0 };
    static const double mUnits[8] = { 1.4e9, 3.5e8, 9e7, 2.2e7, 4e6, 1.5e6, 6e5, 9e4 };
    static const uint32_t mLot[8] = { 20000, 10000, 5000, 2000, 500, 300, 100, 30 };
    for (int i = 0; i < 8; ++i) {
        TypeData t(Base(Minerals[i], 18, Category::Material, SimClass::Refined));
        t.refPrice = mPrice[i];
        t.basePrice = mPrice[i];
        t.unitsPerDay = mUnits[i];
        t.lotSize = mLot[i];
        t.volatility = (float)g.LogN(0.014, 0.3);
        t.volume = 0.01;
        data.types[t.typeID] = t;
    }
    auto price = [&data](uint16_t typeID) { return data.types[typeID].refPrice; };
    auto bomValue = [&](const std::vector<std::pair<uint16_t, double>>& bom) {
        double v(0.0);
        for (auto& in : bom)
            v += in.second * price(in.first);
        return v;
    };
    // a mineral bill worth 'value', heavier on the cheap minerals
    auto mineralBill = [&](double value, int maxMineral) {
        std::vector<std::pair<uint16_t, double>> bom;
        int kinds(2 + (int)g.Pick((size_t)maxMineral - 1));
        double weights[8] = {}, total(0.0);
        for (int k = 0; k < kinds; ++k) {
            weights[k] = (k == 0 ? 3.0 : 1.0) * (0.3 + g.U());
            total += weights[k];
        }
        for (int k = 0; k < kinds; ++k)
            bom.push_back(std::make_pair(Minerals[k], std::max(1.0, std::round(value * weights[k] / total / mPrice[k]))));
        return bom;
    };
    uint16_t next(30000);

    // ores:  refine into minerals, priced at 0.95 of their refine value
    for (int i = 0; i < count(10); ++i) {
        TypeData t(Base(next++, 450 + (uint16_t)i, Category::Asteroid, SimClass::Mined));
        t.portionSize = 100;
        double perBatch(g.LogN(300.0, 0.5) * 100.0);
        for (auto& m : mineralBill(perBatch, 6))
            t.refine.push_back(std::make_pair(m.first, m.second / 100.0));
        t.refPrice = 0.95 * bomValue(t.refine);
        t.unitsPerDay = g.LogN(2e8, 1.0);
        t.lotSize = 5000;
        t.volatility = (float)g.LogN(0.021, 0.5);
        data.types[t.typeID] = t;
    }

    // T1 modules, charges, drones, ships and components:  price ~ mineral cost x markup (PLAN 4.1 price / mineral cost)
    struct Kind { int n; uint8_t cat; uint16_t group; uint8_t cls; double median, sd, markup, markupSd, a, b, resid, vol; uint32_t lot; int maxMineral; };
    static const Kind kinds[] = {
        { 120, Category::Module,    60,  SimClass::MfgT1,     3e5,  1.2, 0.97, 0.15, 5.24, -0.69, 0.5, 0.034, 2,    6 },
        {  40, Category::Charge,    80,  SimClass::MfgT1,     300,  1.0, 1.18, 0.10, 6.64, -0.89, 0.5, 0.068, 1000, 4 },
        {  15, Category::Drone,     100, SimClass::MfgT1,     1e5,  0.8, 1.15, 0.10, 5.24, -0.69, 0.4, 0.057, 5,    5 },
        {  40, Category::Ship,      25,  SimClass::MfgT1,     1.5e7,1.5, 1.15, 0.15, 3.75, -0.29, 0.3, 0.036, 1,    8 },
        {  20, Category::Commodity, 334, SimClass::Component, 5e4,  0.8, 1.10, 0.10, 5.78, -0.75, 0.4, 0.027, 50,   7 },
    };
    std::vector<uint16_t> t1Modules, t1Ships, components;
    for (auto& k : kinds) {
        for (int i = 0; i < count(k.n); ++i) {
            TypeData t(Base(next++, (uint16_t)(k.group + i % 8), k.cat, k.cls));
            double target(g.LogN(k.median, k.sd));
            t.bom = mineralBill(target, k.maxMineral);
            double cost(bomValue(t.bom));
            t.refPrice = cost * g.LogN(k.markup, k.markupSd);
            t.buildCostRef = cost;
            // reprocessing gives the materials back without the waste
            for (auto& in : t.bom)
                t.refine.push_back(std::make_pair(in.first, std::floor(in.second / 1.1)));
            t.unitsPerDay = std::pow(10.0, k.a + k.b * std::log10(t.refPrice) + k.resid * g.N());
            t.lotSize = k.lot;
            t.volatility = (float)g.LogN(k.vol, 0.4);
            data.types[t.typeID] = t;
            if (k.cat == Category::Module)
                t1Modules.push_back(t.typeID);
            else if (k.cat == Category::Ship)
                t1Ships.push_back(t.typeID);
            else if (k.cls == SimClass::Component)
                components.push_back(t.typeID);
        }
    }

    // T2:  the T1 item and components, at an invention premium (PLAN 4.1:  modules 2.73, ships 1.44 x the bill)
    for (int i = 0; i < count(50); ++i) {
        TypeData t(Base(next++, (uint16_t)(60 + i % 8), Category::Module, SimClass::MfgT2));
        t.metaGroupID = 2;
        t.bom.push_back(std::make_pair(t1Modules[g.Pick(t1Modules.size())], 1.0));
        t.bom.push_back(std::make_pair(components[g.Pick(components.size())], std::round(1.0 + 4.0 * g.U())));
        double cost(bomValue(t.bom));
        t.refPrice = cost * g.LogN(2.73, 0.3);
        t.buildCostRef = cost;
        t.unitsPerDay = std::pow(10.0, 5.0 - 0.69 * std::log10(t.refPrice) + 0.5 * g.N());
        t.lotSize = 1;
        t.volatility = (float)g.LogN(0.035, 0.4);
        data.types[t.typeID] = t;
    }
    for (int i = 0; i < count(12); ++i) {
        TypeData t(Base(next++, (uint16_t)(25 + i % 8), Category::Ship, SimClass::MfgT2));
        t.metaGroupID = 2;
        t.bom.push_back(std::make_pair(t1Ships[g.Pick(t1Ships.size())], 1.0));
        t.bom.push_back(std::make_pair(components[g.Pick(components.size())], std::round(20.0 + 200.0 * g.U())));
        double cost(bomValue(t.bom));
        t.refPrice = cost * g.LogN(1.44, 0.2);
        t.buildCostRef = cost;
        t.unitsPerDay = std::pow(10.0, 3.5 - 0.29 * std::log10(t.refPrice) + 0.3 * g.N());
        t.volatility = (float)g.LogN(0.0145, 0.4);
        data.types[t.typeID] = t;
    }

    // loot:  named modules (about the T1 price, PLAN 4.1 meta 1), faction modules (x517), implants;  no bill
    for (int i = 0; i < count(60); ++i) {
        const TypeData& parent(data.types[t1Modules[g.Pick(t1Modules.size())]]);
        TypeData t(Base(next++, parent.groupID, Category::Module, SimClass::Loot));
        t.metaGroupID = 1;
        t.refPrice = parent.refPrice * g.LogN(1.1, 0.8);
        t.refine = parent.refine;
        t.unitsPerDay = parent.unitsPerDay * g.LogN(0.3, 0.6);
        t.volatility = (float)g.LogN(0.072, 0.5);
        data.types[t.typeID] = t;
    }
    for (int i = 0; i < count(20); ++i) {
        const TypeData& parent(data.types[t1Modules[g.Pick(t1Modules.size())]]);
        TypeData t(Base(next++, parent.groupID, Category::Module, SimClass::Faction));
        t.metaGroupID = 4;
        t.refPrice = parent.refPrice * g.LogN(517.0, 0.5);
        t.refine = parent.refine;
        t.unitsPerDay = g.LogN(5.0, 0.8);
        t.volatility = (float)g.LogN(0.040, 0.4);
        data.types[t.typeID] = t;
    }
    for (int i = 0; i < count(15); ++i) {
        TypeData t(Base(next++, (uint16_t)(300 + i % 4), Category::Implant, SimClass::Faction));
        t.refPrice = g.LogN(5e6, 1.2);
        t.unitsPerDay = std::pow(10.0, 4.5 - 0.6 * std::log10(t.refPrice) + 0.4 * g.N());
        t.volatility = (float)g.LogN(0.047, 0.4);
        data.types[t.typeID] = t;
    }
    // PI and salvage:  raw goods without a bill in the stock model
    for (int i = 0; i < count(20); ++i) {
        TypeData t(Base(next++, (uint16_t)(1034 + i % 4), Category::PICommodity, SimClass::PI));
        t.refPrice = g.LogN(2000.0, 1.0);
        t.unitsPerDay = std::pow(10.0, 7.0 - 0.75 * std::log10(t.refPrice) + 0.4 * g.N());
        t.lotSize = 1700;
        t.volatility = (float)g.LogN(0.0385, 0.3);
        data.types[t.typeID] = t;
    }
    for (int i = 0; i < count(15); ++i) {
        TypeData t(Base(next++, 754, Category::Commodity, SimClass::Salvage));
        t.refPrice = g.LogN(1e4, 1.0);
        t.unitsPerDay = std::pow(10.0, 5.78 - 0.75 * std::log10(t.refPrice) + 0.5 * g.N());
        t.lotSize = 10;
        t.volatility = (float)g.LogN(0.197, 0.5);
        data.types[t.typeID] = t;
    }
    // npc vendor skills, sold at two hubs each
    for (int i = 0; i < count(30); ++i) {
        TypeData t(Base(next++, (uint16_t)(255 + i % 6), Category::Skill, SimClass::NpcVendor));
        t.basePrice = std::round(g.LogN(3e5, 1.2));
        t.refPrice = t.basePrice;
        t.priceSource = 7;
        t.unitsPerDay = g.LogN(200.0, 1.0);
        data.types[t.typeID] = t;
        for (int k = 0; k < 2; ++k) {
            const NodeData& node(data.nodes[(i + k * 2) % data.nodes.size()]);
            VendorOffer o = VendorOffer();
            o.corpID = 1000200 + (uint32_t)(i % 5);
            o.typeID = t.typeID;
            o.stationID = node.stationID;
            o.systemID = node.systemID;
            o.regionID = node.regionID;
            data.vendors.push_back(o);
        }
    }

    // the other hubs' prices sit near the Forge's (PLAN 4.1 regional spread:  medians 1.0, IQR about +-20%)
    for (auto& cur : data.types) {
        if (!SimClass::HasCells(cur.second.simClass))
            continue;
        for (size_t n = 1; n < data.nodes.size(); ++n) {
            RegionTypeCal r = RegionTypeCal();
            r.priceRatio = (float)std::exp(0.05 * g.N());
            r.unitsShare = (float)(data.regionDefault[data.nodes[n].regionID].unitsShare * std::exp(0.2 * g.N()));
            r.activeShare = 1.0f;
            data.region[std::make_pair(data.nodes[n].regionID, cur.first)] = r;
        }
    }
    AddSyntheticIndustry(data, seed);
    AddSyntheticChains(data, seed);
}

void AddSyntheticChains(StaticData& data, uint64_t seed)
{
    /* phase 6:  the advanced chains on top of the phase 2/3 world, with their own draws (the earlier types keep their
     *  prices and volumes).  moon:  8 raw materials -> 4 intermediates -> 4 composites (reactions, POS fuel), a gas and a
     *  polymer;  the world's 20 components are built of the composites;  T2:  invention recipes for the world's T2 (datacores,
     *  decryptors, the base chance of modules 0.4 and ships 0.3) and their bills (the T1 item, components, Morphite);
     *  planetary:  3 P0 -> 3 P1 -> 2 P2, and a fuel block of them and an npc vendor's oxygen.  the volumes of the
     *  intermediate goods are what the chains use (PLAN 4.1:  traded volume about consumption) */
    Gen g(seed ^ 0xC4A1D5ull);
    const uint16_t Morphite = 11399;
    std::vector<uint16_t> t2, components;
    for (auto& cur : data.types) {
        if (cur.second.simClass == SimClass::MfgT2)
            t2.push_back(cur.first);
        else if (cur.second.simClass == SimClass::Component)
            components.push_back(cur.first);
    }
    if (t2.empty() or components.empty() or (data.types.find(Morphite) == data.types.end()))
        return;
    uint16_t next(36000), nextRecipe(44000), nextBlueprint(40000);
    double tradesPerUnit(2.0);          // Params::tradesPerUnit:  traded volume = 2 x what is used
    auto addType = [&](uint16_t groupID, uint8_t category, uint8_t simClass, double price, double vol) -> TypeData& {
        TypeData t(Base(next++, groupID, category, simClass));
        t.refPrice = price;
        t.basePrice = price;
        t.volatility = (float)g.LogN(vol, 0.3);
        t.volume = 0.01;
        data.types[t.typeID] = t;
        return data.types[t.typeID];
    };
    // reactions:  'quantities' in units (as the server's loader gives them, x moonMiningAmount)
    auto reaction = [&](std::vector<std::pair<uint16_t, double>> in, uint16_t out, double qty) {
        Recipe r;
        r.kind = RecipeKind::Reaction;
        r.id = nextRecipe++;
        r.inputs = in;
        r.outputs.push_back(std::make_pair(out, qty));
        r.seconds = 3600.0;
        data.recipes.push_back(r);
    };
    const double fuelPerHour(10.0);
    // planetary:  P0 (extracted) -> P1 (3000 -> 20, half an hour) -> P2 (40 + 40 -> 5, an hour)
    std::vector<uint16_t> p0, p1, p2;
    for (int i = 0; i < 3; ++i) {
        TypeData& t(addType((uint16_t)(1032 + i), Category::PIResource, SimClass::PI, g.LogN(4.0, 0.3), 0.039));
        t.lotSize = 10000;
        p0.push_back(t.typeID);
    }
    for (int i = 0; i < 3; ++i) {
        TypeData& t(addType(1042, Category::PICommodity, SimClass::PI, 3000.0 * data.types[p0[i]].refPrice / 20.0 * 1.3, 0.039));
        t.lotSize = 1000;
        Recipe r;
        r.kind = RecipeKind::Planetary;
        r.id = nextRecipe++;
        r.inputs.push_back(std::make_pair(p0[i], 3000.0));
        r.outputs.push_back(std::make_pair(t.typeID, 20.0));
        r.seconds = 1800.0;
        data.recipes.push_back(r);
        p1.push_back(t.typeID);
    }
    for (int i = 0; i < 2; ++i) {
        uint16_t a(p1[i]), b(p1[i + 1]);
        TypeData& t(addType(1034, Category::PICommodity, SimClass::PI, (40.0 * data.types[a].refPrice + 40.0 * data.types[b].refPrice) / 5.0 * 1.3, 0.039));
        t.lotSize = 100;
        Recipe r;
        r.kind = RecipeKind::Planetary;
        r.id = nextRecipe++;
        r.inputs.push_back(std::make_pair(a, 40.0));
        r.inputs.push_back(std::make_pair(b, 40.0));
        r.outputs.push_back(std::make_pair(t.typeID, 5.0));
        r.seconds = 3600.0;
        data.recipes.push_back(r);
        p2.push_back(t.typeID);
    }
    // an npc vendor's oxygen and the fuel block (T1, 40 a run):  P1, P2 and oxygen
    TypeData oxygen(Base(next++, 280, Category::Commodity, SimClass::NpcVendor));
    oxygen.basePrice = 95.0;
    oxygen.refPrice = 95.0;
    oxygen.priceSource = 7;
    data.types[oxygen.typeID] = oxygen;
    TypeData fuel(Base(next++, 1136, Category::Material, SimClass::MfgT1));
    fuel.portionSize = 40;
    fuel.bom.push_back(std::make_pair(p1[2], 8.0 / 40.0));
    fuel.bom.push_back(std::make_pair(p2[0], 20.0 / 40.0));
    fuel.bom.push_back(std::make_pair(p2[1], 10.0 / 40.0));
    fuel.bom.push_back(std::make_pair(oxygen.typeID, 20.0 / 40.0));
    double fuelCost(0.0);
    for (auto& in : fuel.bom)
        fuelCost += in.second * data.types[in.first].refPrice;
    fuel.refPrice = fuelCost * 1.15;
    fuel.basePrice = fuel.refPrice;
    fuel.buildCostRef = fuelCost;
    fuel.buildSeconds = 300.0;
    fuel.lotSize = 400;
    fuel.volatility = 0.03f;
    fuel.volume = 5.0;
    data.types[fuel.typeID] = fuel;
    const double fuelPrice(fuel.refPrice);


    // moon:  raw (harvested, 100 a batch;  about 1.6 x the fuel an array burns for it), intermediates (2 raw -> 200), composites (2 intermediates -> 200), priced at
    //  their inputs and fuel x 1.15
    std::vector<uint16_t> raw, inter, comp;
    for (int i = 0; i < 8; ++i) {
        TypeData& t(addType(427, Category::Material, SimClass::Reaction, fuelPerHour * fuelPrice / 100.0 * g.LogN(1.6, 0.5), 0.039));
        t.lotSize = 1000;
        data.moonAmounts[t.typeID] = 100.0;
        raw.push_back(t.typeID);
    }
    for (int i = 0; i < 4; ++i) {
        uint16_t a(raw[(2 * i) % 8]), b(raw[(2 * i + 1) % 8]);
        double cost((100.0 * data.types[a].refPrice + 100.0 * data.types[b].refPrice + fuelPerHour * fuelPrice) / 200.0);
        TypeData& t(addType(428, Category::Material, SimClass::Reaction, cost * 1.15, 0.039));
        t.lotSize = 1000;
        reaction({ std::make_pair(a, 100.0), std::make_pair(b, 100.0) }, t.typeID, 200.0);
        inter.push_back(t.typeID);
    }
    for (int i = 0; i < 4; ++i) {
        uint16_t a(inter[i]), b(inter[(i + 1) % 4]);
        double cost((100.0 * data.types[a].refPrice + 100.0 * data.types[b].refPrice + fuelPerHour * fuelPrice) / 200.0);
        TypeData& t(addType(429, Category::Material, SimClass::Reaction, cost * 1.15, 0.039));
        t.lotSize = 1000;
        reaction({ std::make_pair(a, 100.0), std::make_pair(b, 100.0) }, t.typeID, 200.0);
        comp.push_back(t.typeID);
    }
    // gas (ships harvest it, no fuel) and a polymer of it and a raw material
    TypeData& gas(addType(711, 2, SimClass::Mined, 900.0, 0.039));
    gas.lotSize = 1000;
    double polyCost((100.0 * gas.refPrice + 100.0 * data.types[raw[0]].refPrice + fuelPerHour * fuelPrice) / 100.0);
    uint16_t gasID(gas.typeID);
    TypeData& poly(addType(974, Category::Material, SimClass::Reaction, polyCost * 1.15, 0.039));
    poly.lotSize = 100;
    poly.unitsPerDay = 4e4;
    reaction({ std::make_pair(gasID, 100.0), std::make_pair(raw[0], 100.0) }, poly.typeID, 100.0);

    // the components:  built of two composites (an unresearched original's bill is x1.1:  price / bill about 1.1)
    for (auto id : components) {
        TypeData& t(data.types[id]);
        ChainBill b;
        b.wasteFactor = 0.1;
        double value(t.refPrice / 1.2);
        uint16_t a(comp[g.Pick(comp.size())]), c(comp[g.Pick(comp.size())]);
        if (a == c)
            c = comp[(std::find(comp.begin(), comp.end(), a) - comp.begin() + 1) % comp.size()];
        b.base.push_back(std::make_pair(a, std::max(1.0, std::round(0.6 * value / data.types[a].refPrice))));
        b.base.push_back(std::make_pair(c, std::max(1.0, std::round(0.4 * value / data.types[c].refPrice))));
        data.bills[id] = b;
    }

    // datacores and decryptors (loot);  their volumes follow the inventions below
    std::vector<uint16_t> datacores, decryptors[2];
    for (int i = 0; i < 6; ++i) {
        TypeData& t(addType(333, Category::Commodity, SimClass::Salvage, g.LogN(1.0e5, 0.4), 0.05));
        t.lotSize = 10;
        datacores.push_back(t.typeID);
    }
    static const double decAttr[3][3] = { { 1.2, 1.0, 2.0 }, { 0.6, 9.0, -2.0 }, { 1.1, 0.0, 3.0 } };   // chance, runs, ME
    for (int k = 0; k < 2; ++k) {
        for (int i = 0; i < 3; ++i) {
            TypeData& t(addType((uint16_t)(728 + k), 35, SimClass::Salvage, g.LogN(4.0e5, 0.4), 0.06));
            Decryptor d;
            d.typeID = t.typeID;
            d.groupID = (uint16_t)(728 + k);
            d.chance = decAttr[i][0];
            d.runs = decAttr[i][1];
            d.me = decAttr[i][2];
            d.pe = 0.0;
            data.decryptors.push_back(d);
            decryptors[k].push_back(t.typeID);
        }
    }

    // T2:  invention (from the T1 item of its bill) and the bill (the extras it had, plus Morphite)
    std::map<uint16_t, double> datacoreUse;         // units a day at TQ scale
    double attempts(0.0);
    for (auto id : t2) {
        TypeData& t(data.types[id]);
        bool ship(t.categoryID == Category::Ship);
        double t2Max(ship ? 10.0 : 100.0), base(ship ? 0.3 : 0.4);
        uint16_t bp(nextBlueprint++);
        data.blueprints[id] = bp;
        Recipe r;
        r.kind = RecipeKind::Invention;
        r.id = (uint16_t)(bp + 2000);
        uint16_t d1(datacores[g.Pick(datacores.size())]), d2(datacores[g.Pick(datacores.size())]);
        if (d1 == d2)
            d2 = datacores[(std::find(datacores.begin(), datacores.end(), d1) - datacores.begin() + 1) % datacores.size()];
        double q(ship ? 8.0 : 2.0);
        r.inputs.push_back(std::make_pair(std::min(d1, d2), q));
        r.inputs.push_back(std::make_pair(std::max(d1, d2), q));
        r.outputs.push_back(std::make_pair(bp, (double)std::max(1, (int)std::floor(t2Max / 10.0))));
        r.seconds = ship ? 30000.0 : 9000.0;
        r.chance = base;
        r.decryptorGroup = (uint16_t)(ship ? 729 : 728);
        r.maxRuns = (uint32_t)t2Max;
        data.recipes.push_back(r);
        ChainBill b;
        b.wasteFactor = 0.1;
        double cost(0.0);
        for (auto& in : t.bom)
            cost += in.second * data.types[in.first].refPrice;
        b.extra = t.bom;
        b.base.push_back(std::make_pair(Morphite, std::max(1.0, std::round(0.04 * cost / data.types[Morphite].refPrice))));
        data.bills[id] = b;
        // what its invention uses a day (modules 10 runs a success, ships 1), at about the base chance x 1.26 (skills)
        double perAttempt(base * 1.26 * r.outputs.front().second);
        double a(t.unitsPerDay / tradesPerUnit / perAttempt);
        attempts += a;
        for (auto& in : r.inputs)
            datacoreUse[in.first] += a * in.second;
    }
    for (auto id : datacores)
        data.types[id].unitsPerDay = tradesPerUnit * std::max(10.0, datacoreUse[id]);
    for (int k = 0; k < 2; ++k)
        for (auto id : decryptors[k])
            data.types[id].unitsPerDay = tradesPerUnit * std::max(5.0, 0.05 * attempts);

    // the moon volumes:  what the components use (their own consumption and the T2's), down the reactions
    std::map<uint16_t, double> use;                 // units a day at TQ scale
    for (auto id : components)
        use[id] += data.types[id].unitsPerDay / tradesPerUnit;
    for (auto id : t2)
        for (auto& in : data.types[id].bom)
            if (data.types[in.first].simClass == SimClass::Component)
                use[in.first] += in.second * data.types[id].unitsPerDay / tradesPerUnit;
    double runsAll(0.0);
    use[poly.typeID] += poly.unitsPerDay / tradesPerUnit;
    for (auto id : components)
        for (auto& in : data.bills[id].base)
            use[in.first] += 1.1 * in.second * use[id];
    for (auto it = data.recipes.rbegin(); it != data.recipes.rend(); ++it) {
        if (it->kind != RecipeKind::Reaction)
            continue;
        double runs(use[it->outputs.front().first] / it->outputs.front().second);
        runsAll += runs;
        for (auto& in : it->inputs)
            use[in.first] += runs * in.second;
    }
    for (auto id : raw)
        data.types[id].unitsPerDay = tradesPerUnit * std::max(1e4, use[id]);
    for (auto id : inter)
        data.types[id].unitsPerDay = tradesPerUnit * std::max(1e4, use[id]);
    for (auto id : comp)
        data.types[id].unitsPerDay = tradesPerUnit * std::max(1e4, use[id]);
    data.types[gasID].unitsPerDay = tradesPerUnit * std::max(1e4, use[gasID]);
    double harvestHours(0.0);
    for (auto id : raw)
        harvestHours += data.types[id].unitsPerDay / tradesPerUnit / 100.0;

    // what the towers burn (harvesting arrays and reaction arrays) and as much again for the other towers (consumers)
    double fuelUse(fuelPerHour * (harvestHours + runsAll));
    fuel.unitsPerDay = tradesPerUnit * 2.0 * std::max(1e3, fuelUse);
    data.types[fuel.typeID].unitsPerDay = fuel.unitsPerDay;
    // the planetary volumes:  what the fuel block uses and as much again (other uses) of P1 and P2, what they use of P0
    std::map<uint16_t, double> piUse, total;
    for (auto& in : fuel.bom)
        piUse[in.first] += in.second * fuel.unitsPerDay / tradesPerUnit;
    for (auto it = data.recipes.rbegin(); it != data.recipes.rend(); ++it) {
        if (it->kind != RecipeKind::Planetary)
            continue;
        uint16_t out(it->outputs.front().first);
        total[out] = 2.0 * piUse[out];
        double runs(total[out] / it->outputs.front().second);
        for (auto& in : it->inputs)
            piUse[in.first] += runs * in.second;
    }
    for (auto id : p0)
        total[id] = piUse[id];
    for (auto ids : { &p0, &p1, &p2 })
        for (auto id : *ids)
            data.types[id].unitsPerDay = tradesPerUnit * std::max(100.0, total[id]);

    // every new type trades at every hub (the regional shares as the others')
    for (auto& cur : data.types) {
        if ((cur.first < 36000) or (cur.first >= next) or !SimClass::HasCells(cur.second.simClass))
            continue;
        for (size_t n = 1; n < data.nodes.size(); ++n) {
            RegionTypeCal r = RegionTypeCal();
            r.priceRatio = (float)std::exp(0.03 * g.N());
            r.unitsShare = (float)(data.regionDefault[data.nodes[n].regionID].unitsShare * std::exp(0.1 * g.N()));
            r.activeShare = 1.0f;
            data.region[std::make_pair(data.nodes[n].regionID, cur.first)] = r;
        }
    }
}

void AddSyntheticIndustry(StaticData& data, uint64_t seed)
{
    /* phase 3:  what the actors need on top of the phase 2 world, with its own draws (the phase 2 types are unchanged).
     *  ore volume (0.1 m3), blueprint times, a Drake and two mining barges, and killmails:  losses for every ship and
     *  capsules, with fits of the world's modules, charges, drones, loot and implants (PLAN 4.2) */
    Gen g(seed ^ 0x1A55E5ull);
    std::vector<uint16_t> t1Modules, t2Modules, lootModules, charges, drones, implants, ships;
    for (auto& cur : data.types) {
        TypeData& t(cur.second);
        if (t.categoryID == Category::Asteroid)
            t.volume = 0.1;
        // blueprint times per run (one unit):  of the order of Crucible's
        if ((t.simClass == SimClass::MfgT1) or (t.simClass == SimClass::MfgT2) or (t.simClass == SimClass::Component)) {
            switch (t.categoryID) {
                case Category::Ship:    t.buildSeconds = 6000.0 * std::pow(std::max(1.0, t.refPrice / 1e6), 0.3); break;
                case Category::Charge:  t.buildSeconds = 6.0; break;
                case Category::Drone:   t.buildSeconds = 1800.0; break;
                case Category::Module:  t.buildSeconds = 600.0 * std::pow(std::max(1.0, t.refPrice / 1e4), 0.25); break;
                default:                t.buildSeconds = 1200.0; break;
            }
        }
        if (t.simClass == SimClass::MfgT1) {
            if (t.categoryID == Category::Module)   t1Modules.push_back(t.typeID);
            if (t.categoryID == Category::Charge)   charges.push_back(t.typeID);
            if (t.categoryID == Category::Drone)    drones.push_back(t.typeID);
            if (t.categoryID == Category::Ship)     ships.push_back(t.typeID);
        } else if ((t.simClass == SimClass::MfgT2) and (t.categoryID == Category::Module)) {
            t2Modules.push_back(t.typeID);
        } else if ((t.simClass == SimClass::MfgT2) and (t.categoryID == Category::Ship)) {
            ships.push_back(t.typeID);
        } else if ((t.simClass == SimClass::Loot) and (t.categoryID == Category::Module)) {
            lootModules.push_back(t.typeID);
        } else if (t.categoryID == Category::Implant) {
            implants.push_back(t.typeID);
        }
    }
    if (t1Modules.empty() or charges.empty())
        return;

    /* the Crucible ores (refine batch, its minerals, m3 per unit):  the synthetic ores above are mostly Tritanium, and
     *  a node needs ores rich in the others too.  priced at 0.95 of what they refine into */
    struct Ore { uint16_t typeID, groupID; uint32_t batch; double volume, share; uint32_t q[8]; };
    static const Ore realOres[] = {
        { 1230, 462, 333, 0.10, 0.30, { 1000,   0,   0,   0,   0,   0,   0,   0 } },     // Veldspar
        { 1228, 460, 333, 0.15, 0.15, {  833, 416,   0,   0,   0,   0,   0,   0 } },     // Scordite
        { 1224, 459, 333, 0.30, 0.12, {  844,  59, 120,   0,  11,   0,   0,   0 } },     // Pyroxeres
        {   18, 458, 333, 0.35, 0.10, {  256, 512, 256,   0,   0,   0,   0,   0 } },     // Plagioclase
        { 1227, 469, 500, 0.60, 0.05, {  307, 123,   0, 307,   0,   0,   0,   0 } },     // Omber
        {   20, 457, 400, 1.20, 0.08, {  386,   0, 773, 386,   0,   0,   0,   0 } },     // Kernite
        { 1226, 456, 500, 2.00, 0.02, {  259, 437, 518,   0, 259,   8,   0,   0 } },     // Jaspet
        { 1231, 455, 500, 3.00, 0.02, {  212,   0,   0, 212, 424,  28,   0,   0 } },     // Hemorphite
        {   21, 454, 500, 3.00, 0.02, {    0,   0,   0, 708, 354,  32,   0,   0 } },     // Hedbergite
        { 1229, 467, 400, 5.00, 0.02, {  171,   0, 171, 343,   0, 171,   0,   0 } },     // Gneiss
        { 1232, 453, 400, 8.00, 0.02, {  250,   0,   0,   0, 500, 250,   0,   0 } },     // Dark Ochre
        { 1225, 452, 250, 16.0, 0.02, {  331,   0,   0,   0, 331, 663,   0,   0 } },     // Crokite
        {   19, 461, 250, 16.0, 0.01, {  700, 140,   0,   0,   0,   0, 140,   0 } },     // Spodumain
        { 1223, 451, 200, 16.0, 0.02, {    0, 170,   0,   0,   0, 341, 170,   0 } },     // Bistot
        {   22, 450, 200, 16.0, 0.02, {  300,   0,   0,   0,   0, 166, 333,   0 } },     // Arkonor
        { 11396, 468, 250, 40.0, 0.01, {   0,   0,   0,   0,   0,   0,   0, 530 } },    // Mercoxit
    };
    for (auto& o : realOres) {
        TypeData t;
        t.typeID = o.typeID;
        t.groupID = o.groupID;
        t.categoryID = Category::Asteroid;
        t.simClass = SimClass::Mined;
        t.portionSize = o.batch;
        t.lotSize = 5000;
        t.activeShare = 1.0f;
        t.confidence = 1.0f;
        t.priceSource = 1;
        t.volume = o.volume;
        double value(0.0);
        for (int i = 0; i < 8; ++i) {
            if (o.q[i] == 0)
                continue;
            t.refine.push_back(std::make_pair(Minerals[i], (double)o.q[i] / (double)o.batch));
            value += (double)o.q[i] / (double)o.batch * data.types[Minerals[i]].refPrice;
        }
        t.refPrice = 0.95 * value;
        t.basePrice = t.refPrice;
        // traded m3 a day of the order of the Forge's ore trade
        t.unitsPerDay = 3e8 * o.share / o.volume;
        t.volatility = (float)g.LogN(0.055, 0.3);
        data.types[t.typeID] = t;
        for (size_t n = 1; n < data.nodes.size(); ++n) {
            RegionTypeCal r = RegionTypeCal();
            r.priceRatio = 1.0f;
            r.unitsShare = (float)(data.regionDefault[data.nodes[n].regionID].unitsShare * std::exp(0.2 * g.N()));
            r.activeShare = 1.0f;
            data.region[std::make_pair(data.nodes[n].regionID, t.typeID)] = r;
        }
    }

    // the Drake (a battlecruiser that is lost a lot) and the T1 mining barges (Retriever, Covetor)
    struct Hull { uint16_t typeID, groupID; double cost, markup, units, buildSeconds; };
    static const Hull hulls[] = {
        { 24698, 419, 3.9e7, 1.10, 1000.0, 15000.0 },
        { 17478, Group::MiningBarge, 5.6e6, 1.10, 520.0, 9000.0 },
        { 17476, Group::MiningBarge, 2.0e7, 1.10, 280.0, 12000.0 },
    };
    static const double mPrice[8] = { 4.5, 10.0, 42.0, 70.0, 420.0, 830.0, 2600.0, 7300.0 };
    static const double mShare[8] = { 0.30, 0.20, 0.15, 0.10, 0.10, 0.07, 0.05, 0.03 };
    for (auto& h : hulls) {
        TypeData t;
        t.typeID = h.typeID;
        t.groupID = h.groupID;
        t.categoryID = Category::Ship;
        t.simClass = SimClass::MfgT1;
        t.portionSize = 1;
        t.lotSize = 1;
        t.activeShare = 1.0f;
        t.confidence = 1.0f;
        t.priceSource = 1;
        for (int i = 0; i < 8; ++i)
            t.bom.push_back(std::make_pair(Minerals[i], std::round(h.cost * mShare[i] / mPrice[i])));
        double cost(0.0);
        for (auto& in : t.bom)
            cost += in.second * data.types[in.first].refPrice;
        t.buildCostRef = cost;
        t.refPrice = cost * h.markup;
        for (auto& in : t.bom)
            t.refine.push_back(std::make_pair(in.first, std::floor(in.second / 1.1)));
        t.unitsPerDay = h.units;
        t.volatility = 0.03f;
        t.buildSeconds = h.buildSeconds;
        data.types[t.typeID] = t;
        for (size_t n = 1; n < data.nodes.size(); ++n) {
            RegionTypeCal r = RegionTypeCal();
            r.priceRatio = (float)std::exp(0.03 * g.N());
            r.unitsShare = (float)(data.regionDefault[data.nodes[n].regionID].unitsShare * std::exp(0.1 * g.N()));
            r.activeShare = 1.0f;
            data.region[std::make_pair(data.nodes[n].regionID, t.typeID)] = r;
        }
        ships.push_back(t.typeID);
    }

    /* losses:  public killmails a day = a share of the hull's traded units (PLAN 4.2:  0.12 to 0.46, the runtime divides
     *  by KillmailCoverage);  combat hulls more.  a fit of 4 to 8 items */
    double shipLosses(0.0);
    for (auto hull : ships) {
        const TypeData& t(data.types[hull]);
        double share(StaticData::IsMiningHull(t) ? 0.08 : ((hull == 24698) ? 0.2 : 0.06 + 0.12 * g.U()));
        std::array<float, SecBand::Count> perDay = {};
        perDay[SecBand::High] = (float)(t.unitsPerDay * share * 0.4);
        perDay[SecBand::Low] = (float)(t.unitsPerDay * share * 0.25);
        perDay[SecBand::Null] = (float)(t.unitsPerDay * share * 0.35);
        data.losses[hull] = perDay;
        shipLosses += t.unitsPerDay * share;
        std::vector<FitItem> fit;
        std::set<uint16_t> used;
        int items(4 + (int)g.Pick(5));
        for (int k = 0; k < items; ++k) {
            double r(g.U());
            uint16_t typeID;
            float qty;
            if (r < 0.45) {
                typeID = t1Modules[g.Pick(t1Modules.size())];
                qty = (float)(0.3 + 1.7 * g.U());
            } else if ((r < 0.6) and !t2Modules.empty()) {
                typeID = t2Modules[g.Pick(t2Modules.size())];
                qty = (float)(0.2 + 1.0 * g.U());
            } else if ((r < 0.7) and !lootModules.empty()) {
                typeID = lootModules[g.Pick(lootModules.size())];
                qty = (float)(0.1 + 0.5 * g.U());
            } else if ((r < 0.85) and !drones.empty()) {
                typeID = drones[g.Pick(drones.size())];
                qty = (float)(0.5 + 2.0 * g.U());
            } else {
                typeID = charges[g.Pick(charges.size())];
                qty = (float)(50.0 + 300.0 * g.U());
            }
            if (!used.insert(typeID).second)
                continue;
            fit.push_back(FitItem{typeID, qty, 0.5f});
        }
        data.fits[hull] = fit;
    }
    // pods:  a fifth of the ships' losses, with an implant now and then
    if (!implants.empty()) {
        std::array<float, SecBand::Count> perDay = {};
        perDay[SecBand::Null] = (float)(0.2 * shipLosses);
        data.losses[670] = perDay;
        std::vector<FitItem> fit;
        for (size_t k = 0; (k < implants.size()) and (k < 5); ++k)
            fit.push_back(FitItem{implants[k], (float)(0.02 + 0.05 * g.U()), 1.0f});
        data.fits[670] = fit;
    }
}

bool LoadExport(const std::string& path, StaticData& data, std::string& err)
{
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f) {
        err = "can't open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    Json doc;
    if (!Json::Parse(ss.str(), doc, err))
        return false;
    data.types.clear();
    data.groups.clear();
    data.region.clear();
    data.vendors.clear();
    data.losses.clear();
    data.fits.clear();
    data.recipes.clear();
    data.bills.clear();
    data.blueprints.clear();
    data.decryptors.clear();
    data.moonAmounts.clear();
    AddHubs(data);
    data.regionDefault.clear();

    for (auto& cur : doc["types"].o) {
        const Json& j(cur.second);
        TypeData t;
        t.typeID = (uint16_t)std::atoi(cur.first.c_str());
        t.groupID = (uint16_t)j["groupID"].Num();
        t.categoryID = (uint8_t)j["categoryID"].Num();
        t.portionSize = (uint32_t)std::max(1.0, j["portionSize"].Num(1.0));
        t.volume = j["volume"].Num();
        t.basePrice = j["basePrice"].Num();
        t.metaGroupID = (uint8_t)j["metaGroupID"].Num();
        data.types[t.typeID] = t;
    }
    // the mktCal* tables:  {columns: [...], rows: [[...]]}
    auto table = [&doc](const char* name, std::map<std::string, size_t>& cols) -> const Json& {
        const Json& t(doc["calibration"][name]);
        for (size_t i = 0; i < t["columns"].a.size(); ++i)
            cols[t["columns"][i].s] = i;
        return t["rows"];
    };
    {
        std::map<std::string, size_t> c;
        const Json& rows(table("mktCalType", c));
        for (auto& r : rows.a) {
            uint16_t typeID((uint16_t)r[c["typeID"]].Num());
            TypeData& t(data.types[typeID]);
            t.typeID = typeID;
            t.simClass = (uint8_t)r[c["simClass"]].Num();
            t.priceSource = (uint8_t)r[c["priceSource"]].Num();
            t.confidence = (float)r[c["confidence"]].Num();
            t.refPrice = r[c["refPrice"]].Num();
            t.priceP10 = r[c["priceP10"]].Num();
            t.priceP90 = r[c["priceP90"]].Num();
            t.volatility = (float)r[c["volatility"]].Num();
            t.unitsPerDay = r[c["unitsPerDay"]].Num();
            t.activeShare = (float)r[c["activeShare"]].Num();
            t.lotSize = (uint32_t)std::max(1.0, r[c["lotSize"]].Num(1.0));
            t.buildCostRef = r[c["buildCostRef"]].Num();
            t.consumeShare = (float)r[c["consumeShare"]].Num();
            t.elasticity = (float)r[c["elasticity"]].Num();
        }
    }
    // killmails:  losses per hull and band, fits per hull (phase 3)
    {
        std::map<std::string, size_t> c;
        for (auto& r : table("mktCalLoss", c).a) {
            uint8_t band((uint8_t)r[c["secBand"]].Num());
            if (band < SecBand::Count)
                data.losses[(uint16_t)r[c["hullTypeID"]].Num()][band] += (float)r[c["lossesPerDay"]].Num();
        }
    }
    {
        std::map<std::string, size_t> c;
        for (auto& r : table("mktCalFit", c).a)
            data.fits[(uint16_t)r[c["hullTypeID"]].Num()].push_back(FitItem{(uint16_t)r[c["typeID"]].Num(),
                (float)r[c["qtyPerLoss"]].Num(), (float)r[c["destroyedShare"]].Num()});
    }
    // blueprint times per run (gen_market_calibration.py's "buildTime":  typeID -> seconds)
    for (auto& cur : doc["buildTime"].o) {
        std::map<uint16_t, TypeData>::iterator itr = data.types.find((uint16_t)std::atoi(cur.first.c_str()));
        if (itr != data.types.end())
            itr->second.buildSeconds = cur.second.Num();
    }
    {
        std::map<std::string, size_t> c;
        for (auto& r : table("mktCalRegion", c).a) {
            RegionTypeCal v = RegionTypeCal();
            v.priceRatio = (float)r[c["priceRatio"]].Num();
            v.unitsShare = (float)r[c["unitsShare"]].Num();
            v.activeShare = (float)r[c["activeShare"]].Num();
            data.region[std::make_pair((uint32_t)r[c["regionID"]].Num(), (uint16_t)r[c["typeID"]].Num())] = v;
        }
    }
    {
        std::map<std::string, size_t> c;
        for (auto& r : table("mktCalRegionDefault", c).a) {
            RegionDefault v = RegionDefault();
            v.priceRatio = (float)r[c["priceRatio"]].Num();
            v.unitsShare = (float)r[c["unitsShare"]].Num();
            v.iskShare = (float)r[c["iskShare"]].Num();
            data.regionDefault[(uint32_t)r[c["regionID"]].Num()] = v;
        }
    }
    {
        std::map<std::string, size_t> c;
        for (auto& r : table("mktCalGroup", c).a) {
            GroupCal v;
            v.elasticity = (float)r[c["elasticity"]].Num(NAN);
            v.stockElasticity = (float)r[c["stockElasticity"]].Num(NAN);
            v.halfSpread = (float)r[c["halfSpread"]].Num(NAN);
            v.coverDays = (float)r[c["coverDays"]].Num(NAN);
            v.consumeShare = (float)r[c["consumeShare"]].Num(NAN);
            v.costMarkupT1 = (float)r[c["costMarkupT1"]].Num(NAN);
            v.costMarkupT2 = (float)r[c["costMarkupT2"]].Num(NAN);
            for (int d = 0; d < 7; ++d)
                v.wd[d] = (float)r[c["wd" + std::to_string(d)]].Num(NAN);
            data.groups[(uint16_t)r[c["groupID"]].Num()] = v;
        }
    }
    // bills of materials per run (base with waste, extra = consumed components) and refine outputs per batch
    for (auto& cur : doc["bom"].o) {
        std::map<uint16_t, TypeData>::iterator itr = data.types.find((uint16_t)std::atoi(cur.first.c_str()));
        if (itr == data.types.end())
            continue;
        double portion(std::max(1.0, cur.second["portion"].Num(1.0)));
        for (const char* part : { "base", "extra" })
            for (auto& line : cur.second[part].a)
                itr->second.bom.push_back(std::make_pair((uint16_t)line[0].Num(), line[1].Num() / portion));
        // reprocessing a built item gives back its materials (the snapshot has them with the waste:  a slightly high
        //  reprocessing floor, which errs on the safe side)
        for (auto& line : cur.second["base"].a)
            itr->second.refine.push_back(std::make_pair((uint16_t)line[0].Num(), line[1].Num() / portion));
    }
    for (auto& cur : doc["refine"].o) {
        std::map<uint16_t, TypeData>::iterator itr = data.types.find((uint16_t)std::atoi(cur.first.c_str()));
        if (itr == data.types.end())
            continue;
        double portion(std::max(1.0, cur.second["portion"].Num(1.0)));
        itr->second.refine.clear();
        for (auto& line : cur.second["materials"].a)
            itr->second.refine.push_back(std::make_pair((uint16_t)line[0].Num(), line[1].Num() / portion));
    }
    // phase 6:  the chains (gen_market_calibration.py's "chains", as MarketSimDB::LoadChains loads them)
    {
        const Json& ch(doc["chains"]);
        auto lines = [](const Json& a, std::vector<std::pair<uint16_t, double>>& into) {
            for (auto& l : a.a)
                into.push_back(std::make_pair((uint16_t)l[0].Num(), l[1].Num()));
        };
        for (auto& r : ch["recipes"].a) {
            Recipe rc;
            rc.kind = (uint8_t)r["kind"].Num();
            rc.id = (uint16_t)r["id"].Num();
            lines(r["inputs"], rc.inputs);
            lines(r["outputs"], rc.outputs);
            rc.seconds = r["seconds"].Num();
            rc.chance = r["chance"].Num(1.0);
            rc.decryptorGroup = (uint16_t)r["decryptorGroup"].Num();
            rc.maxRuns = (uint32_t)r["maxRuns"].Num();
            data.recipes.push_back(rc);
        }
        for (auto& cur : ch["bills"].o) {
            ChainBill b;
            lines(cur.second["base"], b.base);
            lines(cur.second["extra"], b.extra);
            b.wasteFactor = cur.second["wasteFactor"].Num(0.1);
            data.bills[(uint16_t)std::atoi(cur.first.c_str())] = b;
        }
        for (auto& cur : ch["blueprints"].o)
            data.blueprints[(uint16_t)std::atoi(cur.first.c_str())] = (uint16_t)cur.second.Num();
        for (auto& d : ch["decryptors"].a) {
            Decryptor x;
            x.typeID = (uint16_t)d[0].Num();
            x.groupID = (uint16_t)d[1].Num();
            x.chance = d[2].Num(1.0);
            x.runs = d[3].Num();
            x.me = d[4].Num();
            x.pe = d[5].Num();
            data.decryptors.push_back(x);
        }
        for (auto& cur : ch["moonAmounts"].o)
            data.moonAmounts[(uint16_t)std::atoi(cur.first.c_str())] = cur.second.Num();
    }
    if (data.types.empty()) {
        err = "no types in " + path;
        return false;
    }
    return true;
}

}
