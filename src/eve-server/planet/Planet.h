
 /**
  * @name Planet.h
  *   Specific Class for individual planets.
  * this class will hold all planet data and relative info for each planet.
  *
  * @Author:         Allan
  * @date:   30 April 2016
  */


#ifndef EVEMU_PLANET_PLANET_H_
#define EVEMU_PLANET_PLANET_H_

#include "EntityList.h"
#include "StaticDataMgr.h"
#include "planet/ColonySim.h"
#include "planet/PlanetResource.h"
#include "system/SystemEntity.h"

/** @todo update this to create a planet item instead of the default celestial item */
class Planet
{
public:
    Planet();
    ~Planet()                                           { /* do nothing here */ }

protected:
private:

};


class Colony;
class EVEServiceManager;
class SystemManager;
class CustomsSE;
class Call_ResourceDataDict;

class PlanetSE
: public StaticSystemEntity
{
public:
    PlanetSE(InventoryItemRef self, EVEServiceManager&services, SystemManager* system);
    virtual ~PlanetSE();

    /* Process Calls - Overridden as needed in derived classes */
    virtual void                Process();

    /* class type pointer querys. */
    virtual PlanetSE*           GetPlanetSE()           { return this; }
    /* class type tests. */
    virtual bool                IsPlanetSE()            { return true; }

    /* virtual functions default to base class and overridden as needed */
    virtual bool                LoadExtras();

    /* specific functions for this class */
    // KeyVal(planetID, solarSystemID, planetTypeID, radius[, level, pins, links, routes, currentSimTime])
    PyRep*                      GetPlanetInfo(Colony* pColony);
    // is this one of the planet's resources
    bool                        IsResourceType(uint16 typeID);
    // the resource's value at a surface point, for extraction programs
    ColonySim::ResourceField    GetResourceField(uint16 typeID);
    // GetResourceData(info):  KeyVal(data, numBands, proximity) with the bands this character's skills and distance allow
    PyRep*                      GetResourceData(Client* pClient, Call_ResourceDataDict& dict);
    // {resourceTypeID: quality}
    PyRep*                      GetPlanetResourceInfo();
    // a resource's map:  base (generated), final (depleted) and richness
    struct ResourceState {
        ResourceState() : richness(0), builtTime(0), dirty(true) { }
        double richness;
        PIResource::Field base;
        PIResource::Field final;
        std::vector<PIResource::Depletion> depletion;
        int64 builtTime;
        bool dirty;
    };
    ResourceState&              GetResourceState(uint16 typeID);
    // extraction programs take resource out of the map (saved in piResourceDepletion)
    void                        AddDepletion(uint16 typeID, std::vector<PIResource::Depletion>& rows);
    // a program stopped early:  its heads only took out what they extracted until now
    void                        StopDepletion(uint16 typeID, int64 startTime, double latitude, double longitude);
    // the final map is rebuilt on its next use
    void                        InvalidateResource(uint16 typeID);
    const std::vector<uint16>&  GetResourceTypes()      { return m_resourceTypes; }
    PyRep*                      GetExtractorsForPlanet(int32 planetID);

    void                        AbandonColony(Client* pClient, Colony* pColony);
    // loads the character's colony on this planet on first use.  colonies are owned by the planet
    Colony*                     GetColony(uint32 charID);

    bool                        HasCOSE()               { return pCO != nullptr; }
    void                        CreateCustomsOffice();
    // the spot of this planet's customs office
    GPoint                      GetCustomsOfficePosition();
    void                        SetCustomsOffice(CustomsSE* pSE){ pCO = pSE; }
    CustomsSE*                  GetCustomsOffice()      { return pCO; }

    bool                        HasColony()             { return !m_colonies.empty(); }

protected:
    CustomsSE*                  pCO;  // our Customs Office SE  - we dont own this

private:
    // the planet type's five resources
    std::vector<uint16>         m_resourceTypes;
    // typeID, map.  generated on first use
    std::map<uint16, ResourceState> m_resources;

    /* map of charID, Colony* for this planet.
     *   this is a hack, as the client will not reuse planet bound objects, instead calling for a new object on every call.
     *  this scheme will prevent data races on colony calls
     *  Colony* is owned by its planetSE
     */
    std::map<uint32, Colony*>   m_colonies;


};

#endif  // EVEMU_PLANET_PLANET_H_


/*
        minBand, maxBand = const.planetResourceProximityLimits[info.proximity]
        info.newBand = min(maxBand, minBand + info.planetology + info.advancedPlanetology * 2)
        requiredSkill = 5 - info.proximity
        if info.remoteSensing < requiredSkill:
            info.requiredSkill = requiredSkill
        for i, scanRange in enumerate(const.planetResourceScanningRanges):
            if scanRange >= dist:
                info.proximity = i

planetResourceScanDistance = 1000000000

planetResourceProximityDistant = 0
planetResourceProximityRegion = 1
planetResourceProximityConstellation = 2
planetResourceProximitySystem = 3
planetResourceProximityPlanet = 4

planetResourceProximityLimits =
[(2, 6),
 (4, 10),
 (6, 15),
 (10, 20),
 (15, 30)]
planetResourceScanningRanges =
[9.0,
 7.0,
 5.0,
 3.0,
 1.0]
planetResourceUpdateTime = 1 * HOUR

planetResourceMaxValue = 1.21
MAX_DISPLAY_QUALTY = const.planetResourceMaxValue * 255 * 0.5
qualityRemapped = quality / MAX_DISPLAY_QUALTY
self.resourceList.AddItem(typeID, quality=max(0, min(1.0, qualityRemapped)))

*/