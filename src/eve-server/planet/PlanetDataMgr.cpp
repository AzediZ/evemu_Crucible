
 /**
  * @name PlanetDataMgr.cpp
  *   Specific Class for managing planet and pi data
  * @Author:         Allan
  * @date:   30 November 2016
  */


 /*
  * PLANET__ERROR
  * PLANET__WARNING
  * PLANET__MESSAGE
  * PLANET__DEBUG
  * PLANET__INFO
  * PLANET__TRACE
  * PLANET__DUMP
  * PLANET__RES_DUMP
  * PLANET__GC_DUMP
  * PLANET__PKT_TRACE
  * PLANET__DB_ERROR
  * PLANET__DB_WARNING
  */

#include "eve-server.h"

#include "inventory/ItemFactory.h"
#include "inventory/InventoryItem.h"
#include "planet/PlanetDataMgr.h"
#include "StaticDataMgr.h"

PlanetDataMgr::PlanetDataMgr()
{
}

int PlanetDataMgr::Initialize()
{
    _Populate();
    sLog.Blue("    PlanetDataMgr", "Planet Data Manager Initialized.");
    return 1;
}

void PlanetDataMgr::_Populate()
{
    double start = GetTimeMSeconds();
    DBQueryResult* res = new DBQueryResult();
    DBResultRow row;

    m_db.GetPlanetData(*res);
    while (res->GetRow(row)) {
        // SELECT planet.typeID, resource.typeID
        m_planetData.insert(std::pair<uint32, uint32>(row.GetInt(0), row.GetInt(1)));
    }

    //cleanup
    SafeDelete(res);
    sLog.Cyan("    PlanetDataMgr", "%u planet data groups in %u buckets loaded in %.3fms.",\
            m_planetData.size(), m_planetData.bucket_count(), (GetTimeMSeconds() - start));
}

void PlanetDataMgr::GetPlanetData(uint32 planetID, std::vector<uint16> &typeIDs)
{
    auto itr = m_planetData.equal_range(planetID);
    for (auto it = itr.first; it != itr.second; ++it)
        typeIDs.push_back(it->second);
}

const char* PlanetDataMgr::GetCommandName(int8 commandID)
{
    using namespace PI::Command;
    switch (commandID) {
        case Invalid:                       return "Invalid";
        case CreatePin:                     return "CreatePin";
        case RemovePin:                     return "RemovePin";
        case CreateLink:                    return "CreateLink";
        case RemoveLink:                    return "RemoveLink";
        case CreateRoute:                   return "CreateRoute";
        case SetLinkLevel:                  return "SetLinkLevel";
        case UpgradeCommandCenter:          return "UpgradeCommandCenter";
        case SetSchematic:                  return "SetSchematic";
        case RemoveRoute:                   return "RemoveRoute";
        case AddExtractorHead:              return "AddExtractorHead";
        case MoveExtractorHead:             return "MoveExtractorHead";
        case InstallProgram:                return "InstallProgram";
        case KillExtractorHead:             return "KillExtractorHead";
        case PrioritizeRoute:               return "PrioritizeRoute";
        default:                            return "UnknownCommandID";
    }
}


PIDataMgr::PIDataMgr()
{
}

int PIDataMgr::Initialize()
{
    _Populate();
    sLog.Blue("        PIDataMgr", "Planet Interaction Data Manager Initialized.");
    return 1;
}

void PIDataMgr::_Populate()
{
    double start = GetTimeMSeconds();
    DBQueryResult* res = new DBQueryResult();
    DBResultRow row;

    m_db.GetSchematicData(*res);
    while (res->GetRow(row)) {
        // SELECT schematicID, typeID, quantity, isInput
        PI_Schematic& data = m_schematicData[row.GetInt(0)];
        data.schematicID = row.GetInt(0);
        if (row.GetBool(3)) {
            data.inputs[row.GetInt(1)] = row.GetUInt(2);
        } else {
            data.outputs[row.GetInt(1)] = row.GetUInt(2);
        }
    }

    m_db.GetSchematicTimes(*res);
    while (res->GetRow(row)) {
        // SELECT schematicID, cycleTime
        std::map<uint16, PI_Schematic>::iterator itr = m_schematicData.find(row.GetInt(0));
        if (itr != m_schematicData.end())
            itr->second.cycleTime = row.GetUInt(1);
    }

    m_db.GetSchematicPins(*res);
    while (res->GetRow(row)) {
        // SELECT schematicID, pinTypeID
        m_schematicPins[row.GetInt(0)].insert(row.GetInt(1));
    }

    m_db.GetAttributeDefaults(*res);
    while (res->GetRow(row)) {
        // SELECT attributeID, defaultValue
        m_attrDefaults[row.GetInt(0)] = row.IsNull(1) ? 0 : row.GetDouble(1);
    }

    m_db.GetPITypeAttributes(*res);
    uint32 count = 0;
    while (res->GetRow(row)) {
        // SELECT typeID, attributeID, value
        m_typeAttrs[row.GetInt(0)][row.GetInt(1)] = row.IsNull(2) ? 0 : row.GetDouble(2);
        ++count;
    }

    //cleanup
    SafeDelete(res);
    sLog.Cyan("        PIDataMgr", "%u PI Schematics, %u attribute defaults and %u type attributes of %u PI types loaded in %.3fms.", \
            m_schematicData.size(), m_attrDefaults.size(), count, m_typeAttrs.size(), (GetTimeMSeconds() - start));
}

double PIDataMgr::GetTypeAttribute(uint16 typeID, uint16 attributeID)
{
    std::unordered_map<uint16, std::map<uint16, double>>::const_iterator itr = m_typeAttrs.find(typeID);
    if (itr != m_typeAttrs.end()) {
        std::map<uint16, double>::const_iterator itr2 = itr->second.find(attributeID);
        if (itr2 != itr->second.end())
            return itr2->second;
    }
    std::unordered_map<uint16, double>::const_iterator dItr = m_attrDefaults.find(attributeID);
    if (dItr != m_attrDefaults.end())
        return dItr->second;
    return 0;
}

double PIDataMgr::GetTypeAttributeNoDefault(uint16 typeID, uint16 attributeID)
{
    std::unordered_map<uint16, std::map<uint16, double>>::const_iterator itr = m_typeAttrs.find(typeID);
    if (itr != m_typeAttrs.end()) {
        std::map<uint16, double>::const_iterator itr2 = itr->second.find(attributeID);
        if (itr2 != itr->second.end())
            return itr2->second;
    }
    return 0;
}

bool PIDataMgr::HasTypeAttribute(uint16 typeID, uint16 attributeID)
{
    std::unordered_map<uint16, std::map<uint16, double>>::const_iterator itr = m_typeAttrs.find(typeID);
    if (itr == m_typeAttrs.end())
        return false;
    return (itr->second.find(attributeID) != itr->second.end());
}

const PIDataMgr::TypeInfo& PIDataMgr::GetTypeInfo(uint16 typeID)
{
    std::unordered_map<uint16, TypeInfo>::const_iterator itr = m_typeInfo.find(typeID);
    if (itr != m_typeInfo.end())
        return itr->second;

    Inv::TypeData data = Inv::TypeData();
    sDataMgr.GetType(typeID, data);
    Inv::GrpData grp = Inv::GrpData();
    if (data.groupID)
        sDataMgr.GetGroup(data.groupID, grp);
    TypeInfo info = TypeInfo();
        info.categoryID = grp.catID;
        info.groupID = data.groupID;
        info.volume = data.volume;
        info.capacity = data.capacity;
        info.basePrice = data.basePrice;
    return m_typeInfo.emplace(typeID, info).first->second;
}

uint16 PIDataMgr::GetGroupID(uint16 typeID)
{
    return GetTypeInfo(typeID).groupID;
}

uint8 PIDataMgr::GetCategoryID(uint16 typeID)
{
    return GetTypeInfo(typeID).categoryID;
}

float PIDataMgr::GetVolume(uint16 typeID)
{
    return GetTypeInfo(typeID).volume;
}

float PIDataMgr::GetCapacity(uint16 typeID)
{
    return GetTypeInfo(typeID).capacity;
}

double PIDataMgr::GetBasePrice(uint16 typeID)
{
    return GetTypeInfo(typeID).basePrice;
}

uint8 PIDataMgr::GetPinKind(uint16 typeID)
{
    using namespace EVEDB::invGroups;
    switch (GetGroupID(typeID)) {
        case Command_Centers:           return PI::Pin::Kind::CommandCenter;
        case Spaceports:                return PI::Pin::Kind::Spaceport;
        case Storage_Facilities:        return PI::Pin::Kind::Storage;
        case Processors:                return PI::Pin::Kind::Process;
        case Extractor_Control_Units:   return PI::Pin::Kind::ECU;
        case Mercenary_Bases:
        case Capsuleer_Bases:           return PI::Pin::Kind::Base;
    }
    return PI::Pin::Kind::None;
}

const PI_Schematic* PIDataMgr::GetSchematic(uint16 schematicID)
{
    std::map<uint16, PI_Schematic>::const_iterator itr = m_schematicData.find(schematicID);
    if (itr != m_schematicData.end())
        return &itr->second;
    return nullptr;
}

bool PIDataMgr::IsSchematicAllowed(uint16 schematicID, uint16 pinTypeID)
{
    std::map<uint16, std::set<uint16>>::const_iterator itr = m_schematicPins.find(schematicID);
    if (itr == m_schematicPins.end())
        return false;
    return (itr->second.find(pinTypeID) != itr->second.end());
}

double PIDataMgr::GetImportTax(uint16 pinTypeID, const std::map<uint16, uint32>& commodities, double taxRate)
{
    // spaceportPin.GetImportTax
    double importTaxRate = GetTypeAttribute(pinTypeID, AttrImportTax);
    double totalTax = 0;
    for (auto cur : commodities)
        totalTax += cur.second * importTaxRate * GetTypeAttribute(cur.first, AttrImportTaxMultiplier);
    return totalTax * taxRate;
}

double PIDataMgr::GetExportTax(uint16 pinTypeID, const std::map<uint16, uint32>& commodities, double taxRate)
{
    // spaceportPin.GetExportTax - uses the import tax multiplier of the commodity, as the client does
    double exportTaxRate = GetTypeAttribute(pinTypeID, AttrExportTax);
    double totalTax = 0;
    for (auto cur : commodities)
        totalTax += cur.second * exportTaxRate * GetTypeAttribute(cur.first, AttrImportTaxMultiplier);
    return totalTax * taxRate;
}

double PIDataMgr::GetCommodityVolume(const std::map<uint16, uint32>& commodities)
{
    // planetCommon.GetCommodityTotalVolume
    double totalVolume = 0;
    for (auto cur : commodities)
        totalVolume += GetVolume(cur.first) * cur.second;
    return totalVolume;
}

uint16 PIDataMgr::GetHeadType(uint16 ecuTypeID, uint16 programType)
{
    // extractor head types are per planet type:  same planet restriction as the ecu, harvesting the program's resource
    double planetType = GetTypeAttribute(ecuTypeID, AttrPlanetRestriction);
    for (auto cur : m_typeAttrs) {
        if (GetGroupID(cur.first) != EVEDB::invGroups::Extractors)
            continue;
        std::map<uint16, double>::const_iterator itr = cur.second.find(AttrHarvesterType);
        if ((itr == cur.second.end()) or (itr->second != programType))
            continue;
        itr = cur.second.find(AttrPlanetRestriction);
        if ((itr != cur.second.end()) and (itr->second == planetType))
            return cur.first;
    }
    _log(PLANET__ERROR, "PIDataMgr::GetHeadType() - Extractor typeID not found using ECU typeID: %u and Resource typeID: %u", ecuTypeID, programType);
    return 0;
}

uint8 PIDataMgr::GetProductLevel(uint16 typeID)
{
    switch (typeID) {
    // P0 - Raw Materials
        case  2267: //Base Metals
        case  2270: //Noble Metals
        case  2272: //Heavy Metals
        case  2306: //Non-CS Crystals
        case  2307: //Felsic Magma
        case  2268: //Aqueous Liquids
        case  2308: //Suspended Plasma
        case  2309: //Ionic Solutions
        case  2310: //Noble Gas
        case  2311: //Reactive Gas
        case  2073: //Microorganisms
        case  2286: //Planktic Colonies
        case  2287: //Complex Organisms
        case  2288: //Carbon Compounds
        case  2305: //Autotrophs
            return 0;

    // P1 - Basic Commodities
        case  2389: //Plasmoids
        case  2390: //Electrolytes
        case  2392: //Oxidizing Compound
        case  2393: //Bacteria
        case  2395: //Proteins
        case  2396: //Biofuels
        case  2397: //Industrial Fibers
        case  2398: //Reactive Metals
        case  2399: //Precious Metals
        case  2400: //Toxic Metals
        case  2401: //Chiral Structures
        case  3779: //Biomass
        case  9828: //Silicon
        case  3683: //Oxygen
        case  3645: //Water
            return 1;

    // P2 - Refined Commodities
        case    44: //Enriched Uranium
        case  2312: //Supertensile Plastics
        case  2317: //Oxides
        case  2319: //Test Cultures
        case  2321: //Polyaramids
        case  2327: //Microfiber Shielding
        case  2328: //Water-Cooled CPU
        case  2329: //Biocells
        case  2463: //Nanites
        case  3689: //Mechanical Parts
        case  3691: //Synthetic Oil
        case  3693: //Fertilizer
        case  3695: //Polytextiles
        case  3697: //Silicate Glass
        case  3725: //Livestock
        case  3775: //Viral Agent
        case  3828: //Construction Blocks
        case  9830: //Rocket Fuel
        case  9832: //Coolant
        case  9836: //Consumer Electronics
        case  9838: //Superconductors
        case  9840: //Transmitter
        case  9842: //Miniature Electronics
        case 15317: //Genetically Enhanced Livestock
            return 2;

    // P3 - Specialized Commodities
        case  2344: //Condensates
        case  2345: //Camera Drones
        case  2346: //Synthetic Synapses
        case  2348: //Gel-Matrix Biopaste
        case  2349: //Supercomputers
        case  2351: //Smartfab Units
        case  2352: //Nuclear Reactors
        case  2354: //Neocoms
        case  2358: //Biotech Research Reports
        case  2360: //Industrial Explosives
        case  2361: //Hermetic Membranes
        case  2366: //Hazmat Detection Systems
        case  2367: //Cryoprotectant Solution
        case  9834: //Guidance Systems
        case  9846: //Planetary Vehicles
        case  9848: //Robotics
        case 12836: //Transcranial Microcontrollers
        case 17136: //Ukomi Superconductors
        case 17392: //Data Chips
        case 17898: //High-Tech Transmitters
        case 28974: //Vaccines
            return 3;

    // P4 - Advanced Commodities
        case  2867: //Broadcast Node
        case  2868: //Integrity Response Drones
        case  2869: //Nano-Factory
        case  2870: //Organic Mortar Applicators
        case  2871: //Recursive Computing Module
        case  2872: //Self-Harmonizing Power Core
        case  2875: //Sterile Conduits
        case  2876: //Wetware Mainframe
            return 4;
    }
    _log(PLANET__ERROR, "PIDataMgr::GetProductLevel() - Commodity product level not found for typeID: %u", typeID);
    EvE::traceStack();
    return 0;
}

const char* PIDataMgr::GetProductName(uint16 typeID)
{
    switch (typeID) {
        // P0 - Raw Materials
        case  2267:        return "Base Metals";
        case  2270:        return "Noble Metals";
        case  2272:        return "Heavy Metals";
        case  2306:        return "Non-CS Crystals";
        case  2307:        return "Felsic Magma";
        case  2268:        return "Aqueous Liquids";
        case  2308:        return "Suspended Plasma";
        case  2309:        return "Ionic Solutions";
        case  2310:        return "Noble Gas";
        case  2311:        return "Reactive Gas";
        case  2073:        return "Microorganisms";
        case  2286:        return "Planktic Colonies";
        case  2287:        return "Complex Organisms";
        case  2288:        return "Carbon Compounds";
        case  2305:        return "Autotrophs";
            // P1 - Basic Commodities
        case  2389:        return "Plasmoids";
        case  2390:        return "Electrolytes";
        case  2392:        return "Oxidizing Compound";
        case  2393:        return "Bacteria";
        case  2395:        return "Proteins";
        case  2396:        return "Biofuels";
        case  2397:        return "Industrial Fibers";
        case  2398:        return "Reactive Metals";
        case  2399:        return "Precious Metals";
        case  2400:        return "Toxic Metals";
        case  2401:        return "Chiral Structures";
        case  3779:        return "Biomass";
        case  9828:        return "Silicon";
        case  3683:        return "Oxygen";
        case  3645:        return "Water";
            // P2 - Refined Commodities
        case    44:        return "Enriched Uranium";
        case  2312:        return "Supertensile Plastics";
        case  2317:        return "Oxides";
        case  2319:        return "Test Cultures";
        case  2321:        return "Polyaramids";
        case  2327:        return "Microfiber Shielding";
        case  2328:        return "Water-Cooled CPU";
        case  2329:        return "Biocells";
        case  2463:        return "Nanites";
        case  3689:        return "Mechanical Parts";
        case  3691:        return "Synthetic Oil";
        case  3693:        return "Fertilizer";
        case  3695:        return "Polytextiles";
        case  3697:        return "Silicate Glass";
        case  3725:        return "Livestock";
        case  3775:        return "Viral Agent";
        case  3828:        return "Construction Blocks";
        case  9830:        return "Rocket Fuel";
        case  9832:        return "Coolant";
        case  9836:        return "Consumer Electronics";
        case  9838:        return "Superconductors";
        case  9840:        return "Transmitter";
        case  9842:        return "Miniature Electronics";
        case 15317:        return "Genetically Enhanced Livestock";
            // P3 - Specialized Commodities
        case  2344:        return "Condensates";
        case  2345:        return "Camera Drones";
        case  2346:        return "Synthetic Synapses";
        case  2348:        return "Gel-Matrix Biopaste";
        case  2349:        return "Supercomputers";
        case  2351:        return "Smartfab Units";
        case  2352:        return "Nuclear Reactors";
        case  2354:        return "Neocoms";
        case  2358:        return "Biotech Research Reports";
        case  2360:        return "Industrial Explosives";
        case  2361:        return "Hermetic Membranes";
        case  2366:        return "Hazmat Detection Systems";
        case  2367:        return "Cryoprotectant Solution";
        case  9834:        return "Guidance Systems";
        case  9846:        return "Planetary Vehicles";
        case  9848:        return "Robotics";
        case 12836:        return "Transcranial Microcontrollers";
        case 17136:        return "Ukomi Superconductors";
        case 17392:        return "Data Chips";
        case 17898:        return "High-Tech Transmitters";
        case 28974:        return "Vaccines";
            // P4 - Advanced Commodities
        case  2867:        return "Broadcast Node";
        case  2868:        return "Integrity Response Drones";
        case  2869:        return "Nano-Factory";
        case  2870:        return "Organic Mortar Applicators";
        case  2871:        return "Recursive Computing Module";
        case  2872:        return "Self-Harmonizing Power Core";
        case  2875:        return "Sterile Conduits";
        case  2876:        return "Wetware Mainframe";
    }
    _log(PLANET__ERROR, "PIDataMgr::GetProductName() - Commodity product not found for typeID: %u", typeID);
    return "NULL";
}

