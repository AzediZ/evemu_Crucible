
 /**
  * @name HarnessData.cpp
  *   the harness' static data
  *
  * @date:          13Oct26
  *
  */

#include <cmath>
#include <fstream>
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
    AddHubs(data);
    Gen g(seed ^ 0x5EEDDA7Aull);
    auto count = [scale](int n) { return std::max(1, (int)std::lround(n * scale)); };

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
        t.volatility = (float)g.LogN(0.055, 0.3);
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
        t.volatility = (float)g.LogN(0.055, 0.3);
        data.types[t.typeID] = t;
    }

    // T1 modules, charges, drones, ships and components:  price ~ mineral cost x markup (PLAN 4.1 price / mineral cost)
    struct Kind { int n; uint8_t cat; uint16_t group; uint8_t cls; double median, sd, markup, markupSd, a, b, resid, vol; uint32_t lot; int maxMineral; };
    static const Kind kinds[] = {
        { 120, Category::Module,    60,  SimClass::MfgT1,     3e5,  1.2, 0.97, 0.15, 5.24, -0.69, 0.5, 0.107, 2,    6 },
        {  40, Category::Charge,    80,  SimClass::MfgT1,     300,  1.0, 1.18, 0.10, 6.64, -0.89, 0.5, 0.22,  1000, 4 },
        {  15, Category::Drone,     100, SimClass::MfgT1,     1e5,  0.8, 1.15, 0.10, 5.24, -0.69, 0.4, 0.10,  5,    5 },
        {  40, Category::Ship,      25,  SimClass::MfgT1,     1.5e7,1.5, 1.15, 0.15, 3.75, -0.29, 0.3, 0.031, 1,    8 },
        {  20, Category::Commodity, 334, SimClass::Component, 5e4,  0.8, 1.10, 0.10, 5.78, -0.75, 0.4, 0.15,  50,   7 },
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
        t.volatility = (float)g.LogN(0.11, 0.4);
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
        t.volatility = (float)g.LogN(0.04, 0.4);
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
        t.volatility = (float)g.LogN(0.107, 0.5);
        data.types[t.typeID] = t;
    }
    for (int i = 0; i < count(20); ++i) {
        const TypeData& parent(data.types[t1Modules[g.Pick(t1Modules.size())]]);
        TypeData t(Base(next++, parent.groupID, Category::Module, SimClass::Faction));
        t.metaGroupID = 4;
        t.refPrice = parent.refPrice * g.LogN(517.0, 0.5);
        t.refine = parent.refine;
        t.unitsPerDay = g.LogN(5.0, 0.8);
        t.volatility = (float)g.LogN(0.15, 0.4);
        data.types[t.typeID] = t;
    }
    for (int i = 0; i < count(15); ++i) {
        TypeData t(Base(next++, (uint16_t)(300 + i % 4), Category::Implant, SimClass::Faction));
        t.refPrice = g.LogN(5e6, 1.2);
        t.unitsPerDay = std::pow(10.0, 4.5 - 0.6 * std::log10(t.refPrice) + 0.4 * g.N());
        t.volatility = (float)g.LogN(0.091, 0.4);
        data.types[t.typeID] = t;
    }
    // PI and salvage:  raw goods without a bill in the stock model
    for (int i = 0; i < count(20); ++i) {
        TypeData t(Base(next++, (uint16_t)(1034 + i % 4), Category::PICommodity, SimClass::PI));
        t.refPrice = g.LogN(2000.0, 1.0);
        t.unitsPerDay = std::pow(10.0, 7.0 - 0.75 * std::log10(t.refPrice) + 0.4 * g.N());
        t.lotSize = 1700;
        t.volatility = (float)g.LogN(0.058, 0.3);
        data.types[t.typeID] = t;
    }
    for (int i = 0; i < count(15); ++i) {
        TypeData t(Base(next++, 754, Category::Commodity, SimClass::Salvage));
        t.refPrice = g.LogN(1e4, 1.0);
        t.unitsPerDay = std::pow(10.0, 5.78 - 0.75 * std::log10(t.refPrice) + 0.5 * g.N());
        t.lotSize = 10;
        t.volatility = (float)g.LogN(0.15, 0.5);
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
    if (data.types.empty()) {
        err = "no types in " + path;
        return false;
    }
    return true;
}

}
