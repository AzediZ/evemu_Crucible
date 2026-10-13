 /**
  * @name AmbientMgr.h
  *   the ambient npcs' server-wide part (doc/code_and_design_notes/ambient-npc-plan.md 3, 4.1):  the ship catalog, the market
  *   hub scores and region shares (read once at boot with SELECTs, never from the running market sim), the empire npc corps,
  *   the system busyness profiles, the inter-system transit queue (coherence, 6.8), the global cap and the /ambient texts.
  *   each loaded system's own part is its AmbientSystem
  */

#ifndef _EVE_NPC_AMBIENT_AMBIENTMGR_H_
#define _EVE_NPC_AMBIENT_AMBIENTMGR_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "utils/Singleton.h"
#include "npc/ai/NpcRng.h"
#include "npc/ambient/AmbientCatalog.h"
#include "npc/ambient/AmbientTypes.h"

class AmbientSystem;

class AmbientMgr
: public Singleton<AmbientMgr>
{
public:
    AmbientMgr();
    ~AmbientMgr()                                       { /* nothing to do */ }

    // boot:  catalog, market scores, region shares, npc corps.  nothing when <ambient><Enabled> is off
    void Initialize();
    // 1Hz (EntityList):  due transits, the hourly market refresh
    void Process();

    bool IsEnabled() const                              { return m_enabled; }
    const AmbientCatalog& Catalog() const               { return m_catalog; }
    // the server-wide random numbers (seeded from ambient.RngSeed)
    NpcRng& Rng()                                       { return m_rng; }
    // a seed for one ambient object's own rng
    uint64_t Seed() const                               { return m_seed; }
    uint32 NextTravellerID()                            { return ++m_travellerID; }
    uint32 NextGroupID()                                { return ++m_groupID; }

    // the busyness profile of a system (cached;  computed from static data, the system need not be loaded)
    const Ambient::Profile& GetProfile(uint32 systemID);
    // a station's market node score h_n (0:  not a market node)
    double StationScore(uint32 stationID) const;
    // a random npc corp of this empire faction (prefers corps with stations;  0 = none)
    uint32 RandomCorp(uint32 factionID, NpcRng& rng) const;

    /* coherence (6.8):  a ship that jumped out of fromSystem through a gate whose destination is destGate in destSystem.  it
     *  appears there at dueMs if that system is active then */
    void QueueTransit(const Ambient::Traveller& t, uint32 fromSystem, uint32 destSystem, uint32 destGate, int64 dueMs);
    size_t TransitCount() const                         { return m_transits.size(); }

    // active systems (AmbientSystem::Activate / Deactivate), for /ambient global
    void Register(AmbientSystem* pSystem, bool active);

    /* the global cap (5:  GlobalCap) and counters */
    void AddMaterialised(int32 delta);
    uint32 Materialised() const                         { return m_materialised; }
    void CountCapHit()                                  { ++m_capHits; }
    void CountEvent()                                   { ++m_events; }

    /* /ambient */
    std::string DescribeProfile(uint32 systemID);
    std::string DescribeGlobal();

private:
    void LoadMarket();
    void LoadCorps();

    struct Node {
        uint32 stationID;
        uint32 systemID;
        uint8 kind;         // 1 hub, 2 regional
        double h;           // NodeScore
    };
    struct Transit {
        Ambient::Traveller traveller;
        uint32 fromSystem;
        uint32 destSystem;
        uint32 destGate;
        int64 due;
    };

    bool m_enabled;
    uint64_t m_seed;
    uint32 m_travellerID;
    uint32 m_groupID;
    uint32 m_materialised;
    uint64_t m_capHits;
    uint64_t m_events;
    uint64_t m_transitsDone;
    uint64_t m_transitsDropped;
    int64 m_nextMarketRefresh;

    NpcRng m_rng;
    AmbientCatalog m_catalog;
    std::vector<Node> m_nodes;
    std::map<uint32, double> m_regionShare;     // regionID -> R
    std::map<uint32, std::vector<std::pair<uint32, bool>>> m_corps;    // factionID -> (corpID, has stations)
    std::map<uint32, Ambient::Profile> m_profiles;
    std::vector<Transit> m_transits;
    std::set<AmbientSystem*> m_active;
};

//Singleton
#define sAmbientMgr \
( AmbientMgr::get() )

#endif  // _EVE_NPC_AMBIENT_AMBIENTMGR_H_
