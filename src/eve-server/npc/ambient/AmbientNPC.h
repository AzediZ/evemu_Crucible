 /**
  * @name AmbientNPC.h
  *   one ambient ship in space (doc/code_and_design_notes/ambient-npc-plan.md 4.2):  an NPC with its spec (archetype, role,
  *   name, owner), the reference hull's flight data written on its item, its own slim item and ball, and the pilot that flies
  *   its itinerary (AmbientPilot).  transient:  category 11 types get temp items, hull mode miners (category 6) temp items too.
  *   always removed through AmbientSystem::QueueRemoval(), never from inside its own Process()
  */

#ifndef _EVE_NPC_AMBIENT_AMBIENTNPC_H_
#define _EVE_NPC_AMBIENT_AMBIENTNPC_H_

#include "npc/NPC.h"
#include "npc/ambient/AmbientTypes.h"

class AmbientPilot;

class AmbientNPC
: public NPC
{
public:
    AmbientNPC(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data,
               const Ambient::Spec& spec);
    virtual ~AmbientNPC();

    virtual AmbientNPC* GetAmbientNPC()                 { return this; }

    /* SystemEntity interface */
    virtual void Process();
    virtual PyDict* MakeSlimItem();
    virtual void EncodeDestiny(Buffer& into);
    // the reference hull's agility and speed, no bounty or security gain, military hp, then NPC::Load()
    virtual bool Load();
    // crime consequences of killing a protected ambient ship (10.4), then NPC::Killed()
    virtual void Killed(Damage& damage);
    /* 10.4:  a little low-value loot.  haulers a few trade goods, miners some ore of their rock, criminals their own loot data,
     *  navy and CONCORD nothing (CONCORD wrecks were empty in Crucible) */
    virtual void DropLoot(WreckContainerRef wreckRef, uint32 groupID, uint32 owner);

    // takes ownership
    void AttachPilot(AmbientPilot* pPilot);
    AmbientPilot* GetAmbientPilot()                     { return m_pilot; }
    const Ambient::Spec& GetSpec() const                { return m_spec; }
    uint8 GetRole() const                               { return m_spec.role; }
    uint32 GetTravellerID() const                       { return m_spec.travellerID; }

    // a non-criminal civilian or navy ship:  attacking it is a crime (10.1).  CONCORD keeps CONCORD's own rule
    bool IsProtected() const;
    bool IsAmbientCriminal() const                      { return (m_spec.role == Ambient::Role::Criminal); }
    // hull mode (a player ship type):  type name shown, strip miner modules in the slim, interactive ball
    bool IsHull() const;

private:
    Ambient::Spec m_spec;
    AmbientPilot* m_pilot;      // we own this
};

#endif  // _EVE_NPC_AMBIENT_AMBIENTNPC_H_
