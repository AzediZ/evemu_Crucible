 /**
  * @name AmbientTypes.h
  *   the ambient npcs' shared data (doc/code_and_design_notes/planning/ambient-npc-plan.md 4.1):  roles, races, archetypes, the spec of
  *   one ambient ship, its legs, an off-grid traveller and a system's busyness profile
  */

#ifndef _EVE_NPC_AMBIENT_AMBIENTTYPES_H_
#define _EVE_NPC_AMBIENT_AMBIENTTYPES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "eve-server.h"

namespace Ambient {

    namespace Role {
        enum : uint8 {
            None        = 0,
            Hauler      = 1,    // traffic (industrial)
            Courier     = 2,    // traffic (industrial hull, "Courier" name)
            Freighter   = 3,    // traffic at hubs
            Navy        = 4,    // navy transit or squad
            Concord     = 5,    // CONCORD squad
            Miner       = 6,    // belt miner
            Criminal    = 7     // pirate
        };
    }
    const char* RoleName(uint8 role);
    // the role never fights:  it flees when hurt
    inline bool IsCivilian(uint8 role)                  { return ((role >= Role::Hauler) and (role <= Role::Freighter)) or (role == Role::Miner); }
    inline bool IsMilitary(uint8 role)                  { return ((role == Role::Navy) or (role == Role::Concord)); }

    // the four empires (AmbientRules::PickRace)
    namespace Race {
        enum : uint8 {
            Amarr       = 0,
            Caldari     = 1,
            Gallente    = 2,
            Minmatar    = 3,
            Count       = 4,
            None        = 255
        };
    }
    const char* RaceName(uint8 race);
    uint32 RaceFaction(uint8 race);
    uint32 RaceNavyCorp(uint8 race);
    // the empire of a faction (Ammatar and Khanid:  Amarr, Thukker:  Minmatar, ...).  Race::None for the others
    uint8 FactionRace(uint32 factionID);

    // a ship type the catalog validated, with its reference player hull's flight data (7.2)
    struct Archetype {
        uint16 typeID = 0;
        uint8 role = Role::None;
        uint8 race = Race::None;
        uint8 hullClass = 0;        // navy 0 frigate 1 cruiser 2 battleship;  criminal 0 frigate 1 destroyer 2 cruiser;  miner 0 - 3
        uint32 pirateFaction = 0;   // criminals
        uint16 refTypeID = 0;       // reference player hull (0:  the type's own data)
        bool hull = false;          // a player ship type (category 6):  hull mode miners
        uint8 modules = 0;          // hull mode:  strip miners fitted
        float agility = 1.0f;       // inertia modifier (70)
        float maxVelocity = 0.0f;   // m/s speed cap (37 of the reference x SpeedOverRef, never over the type's own)
        float mass = 0.0f;          // kg
        int32 warpFactor = 30;      // AU/s x 10
        std::string typeName;
    };

    // a stop of an itinerary
    namespace Leg {
        enum : uint8 {
            None        = 0,
            Hold        = 1,    // wait holdMs where we are
            WarpTo      = 2,    // warp to targetID (node kind 'node'), landing by the node kind
            ApproachGate= 3,    // fly to jump range of gate targetID
            JumpOut     = 4,    // jump through gate targetID (transit to its destination)
            ApproachDock= 5,    // fly to the dock point of station targetID
            Dock        = 6,    // dock at station targetID (silent removal)
            Loiter      = 7,    // orbit targetID for holdMs
            Mine        = 8,    // mine the belt targetID until full
            Visit       = 9,    // follow the miner targetID for holdMs (hauler visit)
            Hunt        = 10,   // criminal:  look for prey until the hunt times out
            Leave       = 11,   // dock at a station of the system, or jump out (the pilot picks)
            Squad       = 12    // the squad (AmbientSystem) gives the orders
        };
    }
    const char* LegName(uint8 kind);

    struct LegData {
        uint8 kind = Leg::None;
        uint8 node = 0;             // AmbientRules::Node of targetID
        uint32 targetID = 0;
        int64 holdMs = 0;
    };

    // how an ambient ship comes into space
    namespace Start {
        enum : uint8 {
            JumpIn      = 0,    // session cloaked at a gate, uncloaks after ArrivalCloakMin - Max
            Undock      = 1,    // at a station's undock point, flying out
            WarpIn      = 2,    // at an origin node, warping to its first WarpTo leg at once
            Placed      = 3     // already there (warm start, squads, miners at work)
        };
    }

    // why an ambient ship left space (AmbientSystem::QueueRemoval).  every one of them is silent:  no explosion
    namespace Removal {
        enum : uint8 {
            Done        = 0,    // nothing left to do
            Jumped      = 1,    // through a gate (effects.JumpOut, then removed)
            Docked      = 2,
            Fled        = 3,    // warped off grid hurt, or a criminal got away
            OffGrid     = 4,    // landed on a grid nobody watches:  goes on as a traveller (or ends)
            Linger      = 5,    // stationary on a grid nobody watched for ambient.GridLingerSeconds
            WindDown    = 6,    // the system went idle
            GM          = 7     // /ambient clear
        };
    }
    const char* RemovalName(uint8 reason);

    // everything about one ambient ship that outlives its entity (a gate jump, a trip off grid)
    struct Spec {
        const Archetype* arch = nullptr;
        uint8 role = Role::None;
        uint8 race = Race::None;
        std::string name;           // shown for entities (slim name).  hull mode ships show their type name
        uint32 ownerID = 0;         // owner corp
        uint32 factionID = 0;
        uint32 travellerID = 0;     // identity across systems (coherence, trace)
        uint32 squadID = 0;         // patrols
        uint32 groupID = 0;         // criminal group
        std::vector<uint32> fakeModules;    // hull mode:  ids of the fitted strip miners (no items)
    };

    // an ambient ship off grid:  its legs run on timers, no entity (4.4, 6.8)
    struct Traveller {
        Spec spec;
        std::vector<LegData> legs;
        size_t leg = 0;             // the next leg to run
        uint32 atNode = 0;          // the node it is at (itemID)
        uint8 atNodeKind = 0;
        int64 nextAt = 0;           // ms:  the current leg is done
        int64 created = 0;          // ms
    };

    // a system's busyness (6.3), computed once and cached by AmbientMgr
    struct Profile {
        uint32 systemID = 0;
        uint32 regionID = 0;
        uint32 factionID = 0;       // the system's (or its region's) faction
        uint8 homeRace = Race::None;
        float trueSec = 0.0f;
        float level = 0.0f;         // the client's rounded security level
        uint8 band = 0;             // AmbientRules::Band
        double H = 0.0;             // market
        double P = 0.0;             // hub proximity
        double R = 0.0;             // region share
        double S = 0.0;             // stations
        double A = 0.0;             // busyness
        uint32 stations = 0;
        uint32 nearestHubSystem = 0;
        int32 nearestHubJumps = -1;
    };

}   // namespace Ambient

#endif  // _EVE_NPC_AMBIENT_AMBIENTTYPES_H_
