
 /**
  * @name PlanetDataMgr.h
  *   Specific Class for managing planet and pi data
  * @Author:         Allan
  * @date:   30 November 2016
  */


#ifndef EVEMU_PLANET_PLANETDATAMGR_H_
#define EVEMU_PLANET_PLANETDATAMGR_H_

#include <set>
#include <unordered_map>
#include "planet/PlanetDB.h"


// this class is a singleton object to have a common place for all (cached) planet data
class PlanetDataMgr
: public Singleton< PlanetDataMgr >
{
public:
    PlanetDataMgr();
    ~PlanetDataMgr() { /* nothing do to yet */ }

    // Initializes the Table:
    int Initialize();

    void GetPlanetData(uint32 planetID, std::vector<uint16> &typeIDs);

    const char* GetCommandName(int8 commandID);

protected:
    void _Populate();

private:
    PlanetDB m_db;

    std::unordered_multimap<uint32, uint32> m_planetData;
};

#define sPlanetDataMgr \
( PlanetDataMgr::get() )


// this class is a singleton object to have a common place for all (cached) PI schematic, type and attribute data
class PIDataMgr
: public Singleton< PIDataMgr >
{
public:
    PIDataMgr();
    ~PIDataMgr() { /* nothing do to yet */ }

    // Initializes the Table:
    int Initialize();

    /* the type's attribute, or the attribute's default value when the type doesn't have it.
     *  this is ColonyData.GetTypeAttribute() - the ecu attributes (decay, noise, overlap, max volume...) only exist as defaults
     */
    double GetTypeAttribute(uint16 typeID, uint16 attributeID);
    // the type's own attribute only (planetCommon.GetUsageParametersForLinkType reads these without defaults)
    double GetTypeAttributeNoDefault(uint16 typeID, uint16 attributeID);
    bool HasTypeAttribute(uint16 typeID, uint16 attributeID);

    // static type data for PI types, commodities and resources (published or not)
    uint16 GetGroupID(uint16 typeID);
    uint8 GetCategoryID(uint16 typeID);
    float GetVolume(uint16 typeID);
    float GetCapacity(uint16 typeID);
    double GetBasePrice(uint16 typeID);
    // planetCommon.GetPinEntityType() as PI::Pin::Kind
    uint8 GetPinKind(uint16 typeID);

    const PI_Schematic* GetSchematic(uint16 schematicID);
    // cfg.schematicspinmap - can this pin type run this schematic
    bool IsSchematicAllowed(uint16 schematicID, uint16 pinTypeID);

    // spaceportPin.GetImportTax/GetExportTax.  (the client uses the import tax multiplier for both)
    double GetImportTax(uint16 pinTypeID, const std::map<uint16, uint32>& commodities, double taxRate);
    double GetExportTax(uint16 pinTypeID, const std::map<uint16, uint32>& commodities, double taxRate);
    double GetCommodityVolume(const std::map<uint16, uint32>& commodities);

    uint8 GetProductLevel(uint16 typeID);
    const char* GetProductName(uint16 typeID);
    // the extractor head type an ecu uses for a resource (harvesterType = resource, same planet type)
    uint16 GetHeadType(uint16 ecuTypeID, uint16 programType);

protected:
    void _Populate();

private:
    struct TypeInfo {
        uint8 categoryID;
        uint16 groupID;
        float volume;
        float capacity;
        double basePrice;
    };
    const TypeInfo& GetTypeInfo(uint16 typeID);

    PlanetDB m_db;

    std::map<uint16, PI_Schematic> m_schematicData;
    // schematicID, pin typeIDs
    std::map<uint16, std::set<uint16>> m_schematicPins;
    // attributeID, defaultValue
    std::unordered_map<uint16, double> m_attrDefaults;
    // typeID, (attributeID, value) for PI types, commodities and resources
    std::unordered_map<uint16, std::map<uint16, double>> m_typeAttrs;
    std::unordered_map<uint16, TypeInfo> m_typeInfo;
};

#define sPIDataMgr \
( PIDataMgr::get() )


#endif  // EVEMU_PLANET_PLANETDATAMGR_H_