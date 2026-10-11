 /**
  * @name NPCEwar.h
  *   electronic warfare used by npcs and non-law sentries (scram, web, jam, neut, damp, tracking disruption, paint)
  *
  *   data driven from the type's dgm effects and attributes.  every effect duration the npc rolls the effect's chance;
  *   on success the effect is on its target for that duration (EwarState), otherwise it stops.
  *   the beam fx is sent once per session (start/stop), not every cycle.
  */

#ifndef _EVE_NPC_NPCEWAR_H_
#define _EVE_NPC_NPCEWAR_H_

#include "inventory/InventoryItem.h"

class SystemEntity;

class NPCEwar {
public:
    NPCEwar(SystemEntity* pSelf);       // pSelf = npc or sentry.  never for law enforcement
    ~NPCEwar()                          { /* StopAll() is the owner's job (it needs our destiny) */ }

    bool HasAny() const                 { return !m_effects.empty(); }
    // scram or web present
    bool HasTackle() const;
    // longest range of our effects (optimal + falloff).  0 if none
    double GetMaxRange() const;

    /* primary = our weapons target.  secondary = the advanced ai's ewar target (nullptr = use primary)
     * called every process tick while we're fighting */
    void Process(SystemEntity* pPrimary, SystemEntity* pSecondary = nullptr);
    // ends every live session (idle, death, warp, leash)
    void StopAll();

private:
    // kinds beyond EwarState's (things that don't last)
    enum : uint8 { Neut = 100 };

    struct Effect {
        uint16 effectID;
        uint8 kind;
        const char* guid;
        float chance;
        uint32 duration;                // ms
        double range;
        double falloff;
        float v1, v2;                   // values for EwarState (see EwarState::Apply)
        Timer timer;                    // fires each duration
        uint32 targetID;                // live session target (0 = none)
    };

    bool IsEwarTarget(SystemEntity* pTarget) const;
    // surface distance check, with the client's falloff roll past optimal
    bool InRange(const Effect& fx, SystemEntity* pTarget) const;
    void Cycle(Effect& fx, SystemEntity* pTarget);
    void StartSession(Effect& fx, SystemEntity* pTarget);
    void EndSession(Effect& fx);
    void SendFX(const Effect& fx, uint32 targetID, bool start);

    // attribute with a default when the type doesn't have it
    float Attr(uint16 attrID, float def) const;
    // adds an effect if the type has it (or, for types without effect data, if its chance attribute is set)
    void AddEffect(uint16 effectID, uint8 kind, const char* guid, uint16 chanceAttr, uint16 durationAttr, uint32 defDuration,
                   uint16 rangeAttr, double defRange, uint16 falloffAttr, float v1, float v2);

    SystemEntity* m_self;               // we do not own this
    InventoryItemRef m_selfRef;
    std::vector<Effect> m_effects;
};

#endif  // _EVE_NPC_NPCEWAR_H_
