 /**
  * @name AmbientSystem.h
  *   the ambient npcs of one solar system (doc/code_and_design_notes/ambient-npc-plan.md 3, 4.1, 6, 11), owned by its
  *   SystemManager like LawEnforcement.  it does nothing until a player is in space here (and never at sec <= 0 or in
  *   wormholes), then:  keeps the observed grids (bubbles with players in space, and where they are warping to), runs the
  *   Poisson event scheduler, the off-grid travellers, materialises ships on observed grids only (caps), removes them when
  *   they jump, dock or leave the watched grids, and winds down ambient.WindDownSeconds after the last player left space.
  *   nothing is saved:  every ship is a temp item
  */

#ifndef _EVE_NPC_AMBIENT_AMBIENTSYSTEM_H_
#define _EVE_NPC_AMBIENT_AMBIENTSYSTEM_H_

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "npc/ai/AmbientRules.h"
#include "npc/ai/AmbientSchedule.h"
#include "npc/ai/NpcRng.h"
#include "npc/ambient/AmbientTypes.h"

class AggroGroup;
class AmbientNPC;
class AsteroidSE;
class Client;
class SystemBubble;
class SystemEntity;
class SystemManager;

class AmbientSystem {
public:
    explicit AmbientSystem(SystemManager* pSystem);
    ~AmbientSystem();

    // 1Hz, from SystemManager::ProcessTic() after LawEnforcement
    void Process();
    // the system unloads:  forget everything (it deletes the entities)
    void Close();

    bool IsActive() const                               { return m_active; }
    bool IsHighSec() const                              { return (m_profile.band == AmbientRules::Band::High); }
    SystemManager* GetSystem()                          { return m_system; }

    /* entities */
    // SystemManager::OnNPCRemoved():  an ambient npc left space (killed or removed)
    void OnNPCRemoved(uint32 itemID, bool killed);
    // takes an ambient npc out of space delayMs from now (Ambient::Removal), from our Process():  never from its own
    void QueueRemoval(uint32 npcID, uint8 reason, int64 delayMs = 0);
    // an npc that leaves the watched grids goes on as an off-grid traveller
    void Dematerialise(AmbientNPC* pNPC, const Ambient::Traveller& t);
    // an npc jumped through gateID:  it goes on in the next system (6.8 rule 1)
    void OnJumpOut(AmbientNPC* pNPC, uint32 gateID, const Ambient::Traveller& t);

    /* grids */
    bool IsObservedBubble(SystemBubble* pBubble) const;
    bool IsObservedPoint(const GPoint& pos) const;

    /* nodes (gates, stations, belts, planets and moons) */
    // nodeKind AmbientRules::Node::Entity:  any entity of the system
    bool NodePosition(uint32 nodeID, uint8 nodeKind, GPoint& pos, double& radius) const;
    GPoint DockPoint(uint32 stationID) const;
    GVector UndockDirection(uint32 stationID) const;
    // a station (weighted) for a ship leaving, not in pNotIn's grid when there is another (0 = none)
    uint32 PickStation(NpcRng& rng, SystemBubble* pNotIn) const;
    uint32 PickGate(NpcRng& rng, SystemBubble* pNotIn) const;
    // a station whose dock point is within range of pos (0 = none)
    uint32 StationNear(const GPoint& pos, double range) const;
    // a celestial (planet, moon, station, gate) off pNot's grid to flee to
    bool FleeNode(const GPoint& from, SystemBubble* pNot, NpcRng& rng, uint32& nodeID, uint8& nodeKind) const;
    // the asteroids of a belt bubble
    void BeltRocks(SystemBubble* pBubble, std::vector<AsteroidSE*>& into) const;
    // another ambient miner works pRock (or one within 5km of it)
    bool RockTaken(SystemEntity* pRock, AmbientNPC* pExcept) const;
    // a CONCORD ship (law enforcement or an ambient patrol) is in this bubble
    bool ConcordOnGrid(SystemBubble* pBubble) const;

    /* coherence (6.8) */
    // a ship that jumped into this system through gateID (AmbientMgr's transit queue).  false:  not taken
    bool AcceptTransit(const Ambient::Traveller& t, uint32 gateID);
    bool IsGateObserved(uint32 gateID) const;
    // the grid of a node (a station's dock point) is watched
    bool IsNodeObserved(uint32 nodeID) const;

    /* law (8.4):  LawEnforcement::Report() sent kind (Law::) after targetID */
    void OnLawDispatched(uint32 targetID, uint8 kind);

    /* /ambient */
    std::string Describe();
    std::string Summary() const;
    // force count ships of 'what' on pClient's grid.  the number made
    uint32 ForceSpawn(Client* pClient, const std::string& what, uint32 count, std::string& why);
    // every ambient npc of the system goes, silently
    uint32 ClearAll();

private:
    struct Node {
        uint32 id = 0;
        uint8 kind = 0;             // AmbientRules::Node
        GPoint pos;
        double radius = 0.0;
        double weight = 0.0;        // gates and stations (6.5)
        double h = 0.0;             // stations:  market node score
        bool hub = false;           // its grid is a hub grid (6.5)
        bool ice = false;           // belts
        uint32 ownerID = 0;         // stations
        GPoint dockPos;             // stations
        GVector dockDir;
        uint32 destGate = 0;        // gates
        uint32 destSystem = 0;
        bool destNone = false;
        double destA = 0.0;
    };
    struct Grid {
        uint32 id = 0;
        GPoint center;
        uint32 players = 0;
    };
    struct PendingRemoval {
        uint32 npcID;
        uint8 reason;
        int64 at;
    };
    struct PendingLaunch {
        Ambient::Traveller t;
        uint8 start;
        int64 at;
    };
    // m_sched's event kinds
    enum EventKind : uint8 {
        evTraffic   = 1,    // a traffic event at system level (6.4)
        evCriminal  = 2     // a criminal event (6.7)
    };

    void Activate(int64 now);
    void Deactivate(int64 now, const char* why);
    void BuildNodes();
    void UpdateRates(int64 now);
    void UpdateObserved(int64 now);
    void OnGridObserved(const Grid& grid, int64 now, bool newPlayers);
    void ProcessRemovals(int64 now);
    void ProcessEvents(int64 now);
    void ProcessLaunches(int64 now);
    void ProcessVirtual(int64 now);
    void ProcessLinger(int64 now);

    /* traffic (8.1) */
    void TrafficEvent(int64 now);
    // a traffic ship for this itinerary:  spec and legs, starting at its first node.  false:  no archetype fits
    bool BuildTraffic(const AmbientRules::Itinerary& it, Ambient::Traveller& t, uint8& start, int64 now);
    void WarmStartTraffic(const Node& node, int64 now);
    // a new ship at its first node:  materialised when that grid is watched (and the caps allow), else a traveller
    bool Launch(Ambient::Traveller& t, uint8 start, int64 now, bool transit = false);
    // an entity for t, starting at leg firstLeg.  placeAt:  where (nullptr:  by start at t.atNode)
    AmbientNPC* Materialise(Ambient::Traveller& t, uint8 start, size_t firstLeg, int64 now, const GPoint* placeAt = nullptr,
                            const std::shared_ptr<AggroGroup>* group = nullptr);
    // the caps (6.5) for a ship that will be on the grid at pos
    bool CapAllows(const GPoint& pos);
    uint32 CountOnGrid(const GPoint& pos) const;
    bool IsHubGrid(const GPoint& pos) const;
    // runs travellers' legs off grid.  false:  the traveller is gone (materialised, jumped, docked)
    bool StepVirtual(Ambient::Traveller& t, int64 now);
    bool BeginVirtualLeg(Ambient::Traveller& t, int64 now);
    void AddVirtual(const Ambient::Traveller& t);
    int64 VirtualLegMs(const Ambient::Traveller& t, const Ambient::LegData& leg) const;

    const Node* FindNode(uint32 id) const;
    const Node* NodeAt(const GPoint& pos, uint8 kind) const;
    uint32 PickOwner(uint8 race, uint32 stationID);
    std::string RaceName(uint8 race, uint8 role, const Ambient::Archetype* pArch) const;

    SystemManager* m_system;    // we do not own this
    bool m_active;
    bool m_never;               // null sec or wormhole:  never active
    bool m_profiled;
    int64 m_windDownAt;         // ms (0 = players in space)
    int64 m_nextRates;          // ms
    int64 m_nextLinger;         // ms
    double m_lambda;            // traffic events per minute now
    double m_diurnal;
    Ambient::Profile m_profile;

    std::vector<Node> m_gates;
    std::vector<Node> m_stations;
    std::vector<Node> m_belts;
    std::vector<Node> m_planets;
    std::vector<double> m_gateW;
    std::vector<double> m_stationW;
    AmbientRules::KindArray m_kinds;

    std::vector<Grid> m_observed;
    std::map<uint32, uint32> m_gridPlayers;     // bubbleID -> players last tick (beams for newcomers)
    std::map<uint32, int64> m_unwatched;        // npcID -> ms it was first seen on an unwatched grid

    std::map<uint32, AmbientNPC*> m_npcs;       // we do not own these (the system does)
    std::deque<Ambient::Traveller> m_virtual;
    std::vector<PendingRemoval> m_removals;
    std::vector<PendingLaunch> m_launches;
    AmbientSchedule m_sched;
    NpcRng m_rng;

    /* counters (/ambient, AMBIENT__STATS) */
    uint64_t m_events;
    uint64_t m_materialised;
    uint64_t m_capHits;
    uint64_t m_suppressed;
    uint64_t m_killed;
    double m_usSum;
    uint32 m_ticks;
    std::deque<std::string> m_log;              // the last events
    void Note(const std::string& what);
};

#endif  // _EVE_NPC_AMBIENT_AMBIENTSYSTEM_H_
