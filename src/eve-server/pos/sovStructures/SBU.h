/**
 * @name SBU.h
 *   Class for Sovereignty Blockade Units.
 *
 * @Author:         James
 * @date:   8 April 2021
 */


#ifndef EVEMU_POS_SBU_H_
#define EVEMU_POS_SBU_H_

#include "pos/Structure.h"

class SBUSE
: public StructureSE
{
public:
    SBUSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& fData);
    virtual ~SBUSE()                                  { /* do nothing here */ }

    /* class type pointer querys. */
    virtual SBUSE*              GetSBUSE()            { return this; }
    virtual bool                isGlobal()              { return true; }
    virtual bool                IsOperSE()              { return true; }

    /* class type tests. */
    virtual bool                IsSBUSE()             { return true; }

    /* SystemEntity interface */
    virtual void                Process();
    // the system's blockade is recounted (SovereigntyDataMgr::UpdateBlockade) whenever an SBU goes online or offline,
    //  is destroyed or leaves space
    virtual void                SetOnline();
    virtual void                SetOffline();
    virtual void                OnRemovedFromSpace(bool killed);
    // removed outright (gm /unspawn)
    virtual void                Delete();

    /* virtual functions default to base class and overridden as needed */
    virtual void                Init();
    // attack alerts to the owner (SovereigntySBUDamage), at most once an hour
    virtual void                OnDamaged(SystemEntity* pAttacker);

    // anchoring done:  it holds its stargate, and the holding alliance hears of it
    void                        OnAnchored();
    // the stargate no longer has this SBU (destroyed, scooped, unanchored).  one SBU per gate (CantAnchorDisruptorTooClose)
    void                        ReleaseGate();

private:
    // OnSovereigntyAudioEvent to the holding alliance and this SBU's alliance.  {owner} is this SBU's owner
    void                        SendAudioEvent(uint8 eventID);
};

#endif  // EVEMU_POS_SBU_H_
