
/**
 * @name Moon.cpp
 *   Specific Class for individual moons.
 * this class will hold all moon data and relative info for each moon.
 *
 * @Author:         Allan
 * @date:   30 April 2016
 */

#include "Moon.h"
#include "pos/Structure.h"
#include "StaticDataMgr.h"
#include "system/Celestial.h"
#include "system/SystemManager.h"
#include "pos/PosMgrDB.h"
#include "EVEServerConfig.h"
#include <random>

/** @note  general design notes
 * moonse will have a Moon class to hold data and call other functions/methods as needed
 * the PlanetMgr class will manage all aspects of moon data, init'd as a single instance (no reason for multiples)
 *
 *
 */

/*  moon probes, cause i dont where else to put them offhand...
 *   Probe     Travel_Time  Skill_Needed
 *   Quest     40 minutes   Survey III, Astrometrics III
 *   Discovery 10 minutes   Survey III, Astrometrics III
 *   Gaze      5 Minutes    Survey V, Astrometrics V
 *

    About 50% of moons will yield nothing.
    About 39% of moons will yield only gases.
    The rest will yield some combination of gasses and/or minerals.

    Normal skills to reduce probing time do not apply to survey probes

    The Covert Ops 10% time reduction skill per level DOES apply to survey probes when fired from a covops. If you are scanning a large number of moons, it may be better time-wise to use a covops. A covops is also highly recommended when scanning in hostile systems.


   What you need to Probe Moons

    Skills
    Survey III
    Astrometrics III
    Expanded Probe Launcher (NOT a Core Probe Launcher)
    Survey Probes
 */

Moon::Moon()
{

}


MoonSE::MoonSE(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: StaticSystemEntity(self, services, system),
m_towerSE(nullptr),
m_resourcesLoaded(false)
{
    m_resources.clear();
/*
          [PyTuple 2 items]
            [PyInt 40126701]
            [PyList 20 items]
              [PyTuple 2 items]
                [PyInt 16633]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16634]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16635]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16636]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16637]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16638]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16639]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16640]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16641]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16642]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16643]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16644]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16646]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16647]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16648]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16649]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16650]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16651]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16652]
                [PyInt 4]
              [PyTuple 2 items]
                [PyInt 16653]
                [PyInt 4]
                */
}

bool MoonSE::LoadExtras() {
    if (!StaticSystemEntity::LoadExtras())
        return false;

    /** @todo use this to initialize moongoo data, create planet manager for moon, or whatever else
     * i decide is needed for moon management
     *  this is called after SE is created.
     */

    /*  moon goo data (notes for me)
     * goo rarity 4/8/16/32/64
     *
     *

Moon materials have different rarity classes, starting with R4 being the most common and R64 being the most rare.

    R4 (Gases) : Atmospheric Gases, Evaporite, Hydrocarbons, Silicates.
    R8 (Metals): Cobalt, Scandium, Titanium, Tungsten
    R16 (Metals): Cadmium, Chromium, Platinum, Vanadium
    R32 (Metals): Caesium, Hafnium, Mercury, Technetium
    R64 (Metals): Dysprosium, Neodymium, Promethium, Thulium

    Intermediate Materials include:

    Caesarium Cadmide
    Carbon Polymers
    Ceramic Powder
    Crystallite Alloy
    Dysporite
    Fernite Alloy
    Ferrofluid
    Fluxed Condensates
    Hexite
    Hyperflurite
    Neo Mercurite
    Platinum Technite
    Prometium
    Rolled Tungsten Alloy
    Silicon Diborite
    Solerium
    Sulfuric Acid
    Titanium Chromide
    Vanadium Hafnite
     */
    // the materials load on first use (moonResources, generated once per moon)
    return true;
}

void MoonSE::LoadResources()
{
    if (m_resourcesLoaded)
        return;
    m_resourcesLoaded = true;
    m_resources.clear();
    if (PosMgrDB::GetMoonResources(GetID(), m_resources))
        return;
    GenerateResources();
    PosMgrDB::SaveMoonResources(GetID(), m_resources);
}

uint8 MoonSE::GetResource(uint16 typeID)
{
    LoadResources();
    std::map<uint16, uint8>::iterator itr(m_resources.find(typeID));
    return (itr == m_resources.end() ? 0 : itr->second);
}

void MoonSE::GenerateResources()
{
    /* deterministic per moon and pos.MoonGooSeed.  half the moons (MoonGooEmptyChance) have nothing, the rest one (50%),
     *  two (35%) or three (15%) different materials, picked by rarity class with weights for the system's security:
     *  high-sec R4 80 / R8 20;  low-sec R4 50 / R8 25 / R16 15 / R32 7 / R64 3;  null-sec and w-space R4 40 / R8 25 / R16 18 /
     *  R32 11 / R64 6.  each at quantity 1 (100 units an hour for a harvester)
     */
    static const uint16 classes[5][4] = {
        { 16634, 16635, 16633, 16636 },     // R4:  atmospheric gases, evaporite deposits, hydrocarbons, silicates
        { 16640, 16639, 16638, 16637 },     // R8:  cobalt, scandium, titanium, tungsten
        { 16643, 16641, 16644, 16642 },     // R16: cadmium, chromium, platinum, vanadium
        { 16647, 16648, 16646, 16649 },     // R32: caesium, hafnium, mercury, technetium
        { 16650, 16651, 16652, 16653 }      // R64: dysprosium, neodymium, promethium, thulium
    };
    std::mt19937 rng(sConfig.pos.MoonGooSeed ^ GetID());
    std::uniform_real_distribution<double> roll(0.0, 1.0);
    if (roll(rng) < sConfig.pos.MoonGooEmptyChance)
        return;

    double weights[5] = { 40, 25, 18, 11, 6 };
    float trueSec(m_system->GetSystemSecurityRating());
    if (IsKSpaceID(m_system->GetID()) and (trueSec >= 0.45f)) {
        weights[0] = 80; weights[1] = 20; weights[2] = 0; weights[3] = 0; weights[4] = 0;
    } else if (IsKSpaceID(m_system->GetID()) and (trueSec > 0.0f)) {
        weights[0] = 50; weights[1] = 25; weights[2] = 15; weights[3] = 7; weights[4] = 3;
    }

    double count(roll(rng));
    uint8 materials(count < 0.5 ? 1 : (count < 0.85 ? 2 : 3));
    double total(0);
    for (int i = 0; i < 5; ++i)
        total += weights[i];
    for (uint8 picked = 0, tries = 0; (picked < materials) and (tries < 50); ++tries) {
        double pick(roll(rng) * total);
        int rarity(0);
        while ((rarity < 4) and (pick >= weights[rarity])) {
            pick -= weights[rarity];
            ++rarity;
        }
        uint16 typeID(classes[rarity][std::uniform_int_distribution<int>(0, 3)(rng)]);
        if (m_resources.find(typeID) != m_resources.end())
            continue;
        m_resources[typeID] = 1;
        ++picked;
    }
}
