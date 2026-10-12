
 /**
  * @name SimStaticData.cpp
  *   market simulation:  static data, calibration and the plan's defaults
  *
  * @date:          13Oct26
  *
  */

#include <cmath>
#include <limits>

#include "market/sim/SimStaticData.h"

namespace MktSim {

namespace {
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    bool Set(float v)                                   { return (v == v) and (v > 0.0f); }   // not NaN, positive
}

int Isk::Sign(uint8_t flow)
{
    switch (flow) {
        case Income: case Seed: case PlayerToSim: case Reconcile:
            return 1;
        case SimToPlayer: case Production: case BrokerFee: case SalesTax: case Upkeep: case VendorSales: case LineCost:
            return -1;
    }
    return 0;
}

const char* Isk::Name(uint8_t flow)
{
    switch (flow) {
        case Income:        return "isk.faucet.income";
        case Seed:          return "isk.faucet.seed";
        case PlayerToSim:   return "isk.playerToSim";
        case SimToPlayer:   return "isk.simToPlayer";
        case Production:    return "isk.sink.production";
        case BrokerFee:     return "isk.sink.brokerFee";
        case SalesTax:      return "isk.sink.salesTax";
        case Upkeep:        return "isk.sink.upkeep";
        case VendorSales:   return "isk.sink.vendor";
        case Reconcile:     return "isk.reconcile";
        case LineCost:      return "isk.sink.lineCost";
    }
    return "isk.unknown";
}

int Goods::Sign(uint8_t flow)
{
    switch (flow) {
        case Production: case Seed: case Vendor: case PlayerToSim: case Reconcile:
        case Mined: case Loot: case Dropped: case Manufactured: case Refined:
        case Invented: case Harvested: case Extracted:
            return 1;
        case SimToPlayer: case Consumption: case Materials: case RefineTax: case Destroyed: case Spoiled:
        case Fuel:
            return -1;
    }
    return 0;
}

const char* Goods::Name(uint8_t flow)
{
    switch (flow) {
        case Production:    return "goods.faucet.production";
        case Seed:          return "goods.faucet.seed";
        case Vendor:        return "goods.faucet.vendor";
        case PlayerToSim:   return "goods.playerToSim";
        case SimToPlayer:   return "goods.simToPlayer";
        case Consumption:   return "goods.sink.consumption";
        case Reconcile:     return "goods.reconcile";
        case Mined:         return "goods.faucet.mined";
        case Loot:          return "goods.faucet.loot";
        case Dropped:       return "goods.faucet.dropped";
        case Manufactured:  return "goods.faucet.manufactured";
        case Refined:       return "goods.faucet.refined";
        case Materials:     return "goods.sink.materials";
        case RefineTax:     return "goods.sink.refineTax";
        case Destroyed:     return "goods.sink.destroyed";
        case Spoiled:       return "goods.sink.spoiled";
        case Invented:      return "goods.faucet.invented";
        case Harvested:     return "goods.faucet.harvested";
        case Extracted:     return "goods.faucet.extracted";
        case Fuel:          return "goods.sink.fuel";
    }
    return "goods.unknown";
}

const char* ActorKind::Name(uint8_t kind)
{
    switch (kind) {
        case MarketMaker:   return "market maker";
        case Producer:      return "producer";
        case Consumer:      return "consumer";
        case Vendor:        return "vendor";
        case Miner:         return "miner";
        case Reprocessor:   return "reprocessor";
        case Manufacturer:  return "manufacturer";
        case LootSource:    return "loot source";
        case Inventor:      return "inventor";
        case Harvester:     return "harvester";
        case Reactor:       return "reactor";
        case Planetary:     return "planetary";
    }
    return "?";
}

GroupCal::GroupCal()
: elasticity(NaN), stockElasticity(NaN), halfSpread(NaN), coverDays(NaN), consumeShare(NaN),
  volA(NaN), volB(NaN), volResid(NaN), costMarkupT1(NaN), costMarkupT2(NaN)
{
    for (int i = 0; i < 7; ++i)
        wd[i] = NaN;
}

const TypeData* StaticData::Type(uint16_t typeID) const
{
    std::map<uint16_t, TypeData>::const_iterator itr = types.find(typeID);
    return (itr == types.end()) ? nullptr : &itr->second;
}

const GroupCal* StaticData::Group(uint16_t groupID) const
{
    std::map<uint16_t, GroupCal>::const_iterator itr = groups.find(groupID);
    return (itr == groups.end()) ? nullptr : &itr->second;
}

float StaticData::HalfSpread(const TypeData& t) const
{
    const GroupCal* g(Group(t.groupID));
    if ((g != nullptr) and Set(g->halfSpread))
        return g->halfSpread;
    // PLAN 6.2:  minerals 1.5%, PI 3%, ships 4%, modules and charges 6%, implants, faction and BPC-type goods 12%
    switch (t.simClass) {
        case SimClass::Mined:
        case SimClass::Refined:
        case SimClass::Reaction:    return 0.015f;
        case SimClass::PI:
        case SimClass::Component:
        case SimClass::Salvage:     return 0.03f;
        case SimClass::Faction:     return 0.12f;
        case SimClass::Loot:        return (t.categoryID == Category::Implant) ? 0.12f : 0.08f;
    }
    if (t.categoryID == Category::Implant)
        return 0.12f;
    if (t.categoryID == Category::Ship)
        return 0.04f;
    return 0.06f;
}

float StaticData::StockElasticity(const TypeData& t) const
{
    const GroupCal* g(Group(t.groupID));
    if ((g != nullptr) and Set(g->stockElasticity))
        return g->stockElasticity;
    // PLAN 6.2:  0.25 commodities, 0.35 T1, 0.5 T2, 0.7 rare loot/faction
    switch (t.simClass) {
        case SimClass::MfgT1:       return 0.35f;
        case SimClass::MfgT2:
        case SimClass::MfgT3:       return 0.5f;
        case SimClass::Loot:
        case SimClass::Faction:     return 0.7f;
    }
    return 0.25f;
}

float StaticData::Elasticity(const TypeData& t) const
{
    if (Set(t.elasticity))
        return t.elasticity;
    const GroupCal* g(Group(t.groupID));
    if ((g != nullptr) and Set(g->elasticity))
        return g->elasticity;
    // PLAN 6.3:  0.8 ships, 1.0 modules and drones, 1.2 charges, 1.5 implants and luxuries.  materials (the stock
    //  model's abstract industry demand) 0.6
    switch (t.categoryID) {
        case Category::Ship:        return 0.8f;
        case Category::Charge:      return 1.2f;
        case Category::Implant:     return 1.5f;
        case Category::Module:
        case Category::Drone:
        case Category::Subsystem:
        case Category::Deployable:  return 1.0f;
    }
    if (t.simClass == SimClass::Faction)
        return 1.5f;
    if (SimClass::IsLoot(t.simClass))
        return 1.0f;
    return 0.6f;
}

float StaticData::CoverDays(const TypeData& t) const
{
    const GroupCal* g(Group(t.groupID));
    if ((g != nullptr) and Set(g->coverDays))
        return g->coverDays;
    switch (t.simClass) {
        case SimClass::MfgT1:
        case SimClass::MfgT2:
        case SimClass::MfgT3:       return 5.0f;
        case SimClass::Loot:
        case SimClass::Faction:     return 10.0f;
    }
    return 3.0f;
}

float StaticData::CostPassThrough(const TypeData& t) const
{
    // PLAN 6.2:  T1 ships and modules 0.9, charges and drones 0.8, T2 0.5, components 0.9, loot, vendor, raw 0
    if (t.bom.empty())
        return 0.0f;
    switch (t.simClass) {
        case SimClass::MfgT1:
            return ((t.categoryID == Category::Charge) or (t.categoryID == Category::Drone)) ? 0.8f : 0.9f;
        case SimClass::MfgT2:
        case SimClass::MfgT3:       return 0.5f;
        case SimClass::Component:   return 0.9f;
    }
    return 0.0f;
}

float StaticData::Weekday(const TypeData& t, int weekday) const
{
    const GroupCal* g(Group(t.groupID));
    if ((g != nullptr) and Set(g->wd[weekday % 7]))
        return g->wd[weekday % 7];
    // PLAN 4.1:  weekends trade about 30% more than weekdays.  the week averages 1
    return (weekday >= 5) ? 1.2f : 0.92f;
}

float StaticData::RegionPrice(uint32_t regionID, uint16_t typeID) const
{
    std::map<std::pair<uint32_t, uint16_t>, RegionTypeCal>::const_iterator itr = region.find(std::make_pair(regionID, typeID));
    if ((itr != region.end()) and Set(itr->second.priceRatio))
        return itr->second.priceRatio;
    std::map<uint32_t, RegionDefault>::const_iterator ditr = regionDefault.find(regionID);
    if ((ditr != regionDefault.end()) and Set(ditr->second.priceRatio))
        return ditr->second.priceRatio;
    return 1.0f;
}

float StaticData::RegionUnits(uint32_t regionID, uint16_t typeID) const
{
    std::map<std::pair<uint32_t, uint16_t>, RegionTypeCal>::const_iterator itr = region.find(std::make_pair(regionID, typeID));
    if ((itr != region.end()) and (itr->second.unitsShare == itr->second.unitsShare) and (itr->second.unitsShare >= 0.0f))
        return itr->second.unitsShare;
    std::map<uint32_t, RegionDefault>::const_iterator ditr = regionDefault.find(regionID);
    if ((ditr != regionDefault.end()) and Set(ditr->second.unitsShare))
        return ditr->second.unitsShare;
    return 0.0f;
}

float StaticData::RegionActive(uint32_t regionID, uint16_t typeID) const
{
    std::map<std::pair<uint32_t, uint16_t>, RegionTypeCal>::const_iterator itr = region.find(std::make_pair(regionID, typeID));
    if (itr == region.end())
        return 0.0f;
    return itr->second.activeShare;
}

bool StaticData::HasRegionRow(uint32_t regionID, uint16_t typeID) const
{
    return region.find(std::make_pair(regionID, typeID)) != region.end();
}

uint8_t StaticData::SectorOf(const TypeData& t)
{
    if (SimClass::IsLoot(t.simClass) and (t.simClass != SimClass::Salvage))
        return Sector::Loot;
    switch (t.simClass) {
        case SimClass::MfgT1:
        case SimClass::MfgT2:
        case SimClass::MfgT3:
            return (t.categoryID == Category::Ship) ? Sector::Ships : Sector::Goods;
    }
    return Sector::Raw;
}

uint32_t StaticData::MarketMakerCorp(uint32_t nodeID, uint8_t sector) const
{
    std::map<std::pair<uint32_t, uint8_t>, uint32_t>::const_iterator itr = mmCorp.find(std::make_pair(nodeID, sector));
    if ((itr != mmCorp.end()) and (itr->second != 0))
        return itr->second;
    if ((nodeID == 0) or (nodeID > nodes.size()))
        return 0;
    std::map<uint32_t, uint32_t>::const_iterator oitr = stationOwner.find(nodes[nodeID - 1].stationID);
    return (oitr == stationOwner.end()) ? 0 : oitr->second;
}

uint32_t StaticData::ProducerCorp(uint32_t nodeID, uint8_t simClass) const
{
    std::map<std::pair<uint32_t, uint8_t>, uint32_t>::const_iterator itr = producerCorp.find(std::make_pair(nodeID, simClass));
    if ((itr != producerCorp.end()) and (itr->second != 0))
        return itr->second;
    TypeData t;
    t.simClass = simClass;
    return MarketMakerCorp(nodeID, SectorOf(t));
}

uint32_t StaticData::ActorCorp(uint32_t nodeID, uint8_t kind, uint8_t sector) const
{
    std::map<std::tuple<uint32_t, uint8_t, uint8_t>, uint32_t>::const_iterator itr = actorCorp.find(std::make_tuple(nodeID, kind, sector));
    if ((itr != actorCorp.end()) and (itr->second != 0))
        return itr->second;
    // the market maker of the sector the actor supplies
    switch (kind) {
        case ActorKind::Manufacturer:   return MarketMakerCorp(nodeID, (sector == Sector::Ships) ? Sector::Ships : Sector::Goods);
        case ActorKind::LootSource:     return MarketMakerCorp(nodeID, Sector::Loot);
    }
    return MarketMakerCorp(nodeID, Sector::Raw);
}

float StaticData::LossesPerDay(uint16_t hullTypeID) const
{
    std::map<uint16_t, std::array<float, SecBand::Count>>::const_iterator itr = losses.find(hullTypeID);
    if (itr == losses.end())
        return 0.0f;
    float sum(0.0f);
    for (auto v : itr->second)
        sum += v;
    return sum;
}

size_t StaticData::Validate(std::vector<std::string>& problems) const
{
    size_t start(problems.size());
    size_t bad(0);
    for (auto& cur : types) {
        const TypeData& t(cur.second);
        if (!SimClass::HasCells(t.simClass) and (t.simClass != SimClass::NpcVendor))
            continue;
        if (!(t.refPrice > 0.0) or !std::isfinite(t.refPrice)) {
            if (++bad <= 10)
                problems.push_back("type " + std::to_string(t.typeID) + " has no refPrice");
        }
        if (!(t.unitsPerDay >= 0.0) or !std::isfinite(t.unitsPerDay))
            problems.push_back("type " + std::to_string(t.typeID) + " has a bad unitsPerDay");
        if (t.portionSize == 0)
            problems.push_back("type " + std::to_string(t.typeID) + " has portionSize 0");
    }
    if (bad > 10)
        problems.push_back(std::to_string(bad) + " types without a refPrice in all");
    if (nodes.empty())
        problems.push_back("no market nodes");
    for (auto& node : nodes)
        if ((node.stationID == 0) or (node.regionID == 0))
            problems.push_back("node " + std::to_string(node.nodeID) + " has no station or region");
    return problems.size() - start;
}

}
