 /**
  * @name NPCEwar.h
  *   electronic warfare used by npcs and non-law sentries (scram, web, jam, neut, damp, tracking disruption, paint)
  *
  *   data driven from the type's dgm effects and attributes (PLAN.md 5.8).  two chances:
  *    usage chance (the effect's npcUsageChanceAttributeID:  entityWarpScrambleChance 504, modifyTargetSpeedChance 512):
  *     does this npc use the effect at all.  rolled once when it spawns ("this rat carries a scram"), per new target or
  *     every cycle (npc.EwarUsageRoll 0/1/2)
  *    activation chance (npcActivationChanceAttributeID:  entity*DurationChance 930-935):  "chance of npc effect to be
  *     activated each duration", rolled every cycle
  *   on success the effect is on its target for that duration (EwarState), otherwise it stops.
  *   the beam fx is sent once per session (start/stop), not every cycle.
  */

#ifndef _EVE_NPC_NPCEWAR_H_
#define _EVE_NPC_NPCEWAR_H_

#include "inventory/InventoryItem.h"

class NpcRng;
class SystemEntity;

class NPCEwar {
public:
    // pSelf = npc or sentry.  never for law enforcement.  rng:  the npc's own (nullptr = the global one)
    NPCEwar(SystemEntity* pSelf, NpcRng* rng = nullptr);
    ~NPCEwar()                          { /* StopAll() is the owner's job (it needs our destiny) */ }

    bool HasAny() const                 { return !m_effects.empty(); }
    // scram or web present
    bool HasTackle() const;
    // longest range of our effects (optimal + falloff).  0 if none
    double GetMaxRange() const;

    /* primary = our weapons target.  secondary = the advanced ai's ewar target (nullptr = use primary)
     * called every process tick while we're fighting */
    void Process(SystemEntity* pPrimary, SystemEntity* pSecondary = nullptr);
    /* the level-one secondary target (PLAN.md 5.7, 1649):  each effect goes on the first of the secondaries (best first) that is
     *  within its range (optimal + falloff), else on the primary */
    void Process(SystemEntity* pPrimary, SystemEntity* const* pSecondaries, size_t count);
    // ends every live session (idle, death, warp, leash)
    void StopAll();
    // a group's propulsion jamming assist (NPCGroupPropJamAssist, 4688):  our scram and web reach x mult (1 = none)
    void SetTackleRangeMult(float mult)  { m_tackleMult = (mult > 0.0f ? mult : 1.0f); }
    float GetTackleRangeMult() const    { return m_tackleMult; }

private:
    // kinds beyond EwarState's (things that don't last)
    enum : uint8 { Neut = 100 };

    struct Effect {
        uint16 effectID;
        uint8 kind;
        const char* guid;
        float usage;                    // usage chance (504, 512;  1 for the others)
        float chance;                   // activation chance each duration (930-935;  1 for scram and web)
        bool carried;                   // npc.EwarUsageRoll 0:  the spawn's usage roll came up
        bool usable;                    // npc.EwarUsageRoll 1:  the roll for usableFor came up
        uint32 usableFor;               //  the target that roll was for
        uint32 duration;                // ms
        double range;
        double falloff;
        float v1, v2;                   // values for EwarState (see EwarState::Apply)
        int64 nextAt;                   // ms (GetSteadyTime).  0 = not running
        uint32 targetID;                // live session target (0 = none)
    };
    // a draw in [0,1) from our rng
    double Rand();

    bool IsEwarTarget(SystemEntity* pTarget) const;
    // surface distance check, with the client's falloff roll past optimal
    bool InRange(const Effect& fx, SystemEntity* pTarget);
    // optimal of fx (scram and web x the propulsion jamming assist)
    double Optimal(const Effect& fx) const;
    // one cycle of fx on pTarget (nullptr = none) when it's due
    void Step(Effect& fx, SystemEntity* pTarget, int64 now);
    void Cycle(Effect& fx, SystemEntity* pTarget);
    // the usage chance allows this effect on pTarget now (npc.EwarUsageRoll)
    bool Uses(Effect& fx, SystemEntity* pTarget);
    void StartSession(Effect& fx, SystemEntity* pTarget);
    void EndSession(Effect& fx);
    void SendFX(const Effect& fx, uint32 targetID, bool start);

    // attribute with a default when the type doesn't have it
    float Attr(uint16 attrID, float def) const;
    /* adds an effect if the type has it (or, for types without effect data, if its chance attribute is set).
     *  usageAttr:  its usage chance attribute (0 = none), chanceAttr:  its activation chance attribute (0 = none) */
    void AddEffect(uint16 effectID, uint8 kind, const char* guid, uint16 usageAttr, uint16 chanceAttr, uint16 durationAttr,
                   uint32 defDuration, uint16 rangeAttr, double defRange, uint16 falloffAttr, float v1, float v2);

    SystemEntity* m_self;               // we do not own this
    NpcRng* m_rng;                      // we do not own this (nullptr = MakeRandomFloat)
    InventoryItemRef m_selfRef;
    std::vector<Effect> m_effects;
    float m_tackleMult;
};

#endif  // _EVE_NPC_NPCEWAR_H_
