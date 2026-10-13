 /**
  * @name AmbientCatalog.h
  *   the ambient npcs' ship types (doc/code_and_design_notes/ambient-npc-plan.md section 7):  haulers, couriers, freighters,
  *   navy, CONCORD, criminals and miners, each with its reference player hull's flight data.  every type is validated at
  *   boot (it exists, its category, a graphic, a neutral bracket for civilians and law);  a bad one is dropped with an
  *   AMBIENT__WARN line
  */

#ifndef _EVE_NPC_AMBIENT_AMBIENTCATALOG_H_
#define _EVE_NPC_AMBIENT_AMBIENTCATALOG_H_

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "EVE_Loot.h"
#include "npc/ambient/AmbientTypes.h"

class NpcRng;

class AmbientCatalog {
public:
    AmbientCatalog();

    // validates and loads every archetype.  graphics:  typeID -> graphicID from invTypes
    void Load(const std::map<uint32, uint32>& graphics);

    /* a random archetype of this role (race Race::None:  any race;  hullClass 255:  any class;  pirate 0:  any).  hull:  hull
     *  mode miners (category 6) or entities.  nullptr when the catalog has none */
    const Ambient::Archetype* Pick(NpcRng& rng, uint8 role, uint8 race = Ambient::Race::None, uint8 hullClass = 255,
                                   uint32 pirateFaction = 0, bool hull = false) const;
    // the archetype of this type (nullptr:  not in the catalog)
    const Ambient::Archetype* Find(uint16 typeID) const;
    // hull mode miners of this class (Procurer 0, Retriever 1, Covetor 2, Hulk 3)
    bool HasHullMiners() const;

    // the strip miner fitted on hull mode miners:  typeID, cycle (ms) and range (m)
    uint16 MinerModuleType() const                      { return m_minerModule; }
    uint32 MinerCycleMs() const                         { return m_minerCycleMs; }
    double MinerRange() const                           { return m_minerRange; }

    /* a little low-value loot (10.4):  the trade goods haulers carry, resolved by name at boot (A8).  miners carry a little ore
     *  of their rock (AmbientNPC), navy and CONCORD nothing, criminals their own loot data */
    const std::vector<Loot::Item>& TradeGoods() const  { return m_tradeGoods; }

    size_t Valid() const                                { return m_types.size(); }
    size_t Dropped() const                              { return m_dropped; }
    std::string Describe() const;

private:
    struct Entry {
        uint16 typeID;
        uint8 role;
        uint8 race;
        uint8 hullClass;
        uint32 pirateFaction;
        uint16 refTypeID;
        uint8 modules;
    };
    bool Validate(const Entry& e, const std::map<uint32, uint32>& graphics, Ambient::Archetype& into);

    void LoadLoot();

    std::deque<Ambient::Archetype> m_types;     // stable pointers
    std::vector<Loot::Item> m_tradeGoods;
    std::map<uint16, const Ambient::Archetype*> m_byType;
    size_t m_dropped;
    uint16 m_minerModule;
    uint32 m_minerCycleMs;
    double m_minerRange;
};

#endif  // _EVE_NPC_AMBIENT_AMBIENTCATALOG_H_
