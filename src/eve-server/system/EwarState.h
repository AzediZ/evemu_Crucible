 /**
  * @name EwarState.h
  *   electronic warfare received by an entity (jams, scrams, webs, damps, tracking disruption, painting)
  *
  *   one store for every source (player modules, drones, npcs, structures).
  *   it answers "is this entity jammed / scrambled / painted" and keeps the client's ewar notifications in step.
  *   module-facing helpers are in ship/modules/EwarModule.h (namespace Ewar)
  */

#ifndef _EVE_SYSTEM_EWAR_STATE_H_
#define _EVE_SYSTEM_EWAR_STATE_H_

#include "inventory/InventoryItem.h"

class SystemEntity;

namespace Ewar {
    // kinds of ewar an entity can receive
    enum : uint8 {
        None            = 0,
        Scramble        = 1,
        Web             = 2,
        Jam             = 3,
        Damp            = 4,
        TrackDisrupt    = 5,
        Paint           = 6
    };
    // client 'ewarType' key for a kind (state.py ewarStates), nullptr for none
    const char* GetKindEwarType(uint8 kind);
}

class EwarState {
public:
    EwarState(SystemEntity* pOwner);
    ~EwarState();                       // drops everything without side effects (the owner is going away)

    /* add or refresh.
     *  sourceID = attacking entity (ship/npc/drone/structure itemID).
     *  moduleID = the module doing it, if any.  entries are kept per moduleID (or per sourceID when there is no module)
     *  values by kind:
     *   Scramble: v1 = strength            Web: srcRef = item whose AttrSpeedFactor applies (npc or module)
     *   Jam: -                             Damp: v1 = maxTargetRange mult, v2 = scanResolution mult
     *   TrackDisrupt: v1 = tracking mult, v2 = optimal mult      Paint: v1 = signature radius mult
     * Apply on an existing entry only extends it.
     */
    void Apply(uint32 sourceID, uint8 kind, uint32 durationMs, float v1 = 0, float v2 = 0,
               InventoryItemRef srcRef = InventoryItemRef(nullptr), uint32 moduleID = 0);
    void Remove(uint32 sourceID, uint8 kind, uint32 moduleID = 0);
    void RemoveSource(uint32 sourceID);  // everything this entity is doing to us
    void Clear();                        // ends everything (with side effects)
    void Process();                      // from SystemEntity::Process(): expire, drop sources no longer near us

    bool  IsJammed() const;
    bool  IsJammedBy(uint32 sourceID, uint32 moduleID = 0) const;
    float GetScrambleStrength() const;
    float GetTargetRangeMult() const;    // Damp v1, stacking penalized
    float GetScanResMult() const;        // Damp v2
    float GetTrackingMult() const;       // TrackDisrupt v1
    float GetOptimalMult() const;        // TrackDisrupt v2
    float GetSigRadiusMult() const;      // Paint v1
    bool  Empty() const                  { return m_entries.empty(); }

private:
    struct Entry {
        uint8 kind;
        float v1, v2;
        int64 start;                     // filetime
        int64 expires;                   // filetime
        uint32 durationMs;
        uint32 sourceID;
        uint32 moduleID;
        InventoryItemRef srcRef;
    };
    static uint64_t MakeKey(uint32 id, uint8 kind)  { return ((uint64_t)id << 8) | kind; }

    void OnStart(Entry& e);
    void OnEnd(Entry& e);
    float Penalized(uint8 kind, bool second) const;

    SystemEntity* m_owner;               // we do not own this
    std::map<uint64_t, Entry> m_entries;
};

#endif  // _EVE_SYSTEM_EWAR_STATE_H_
