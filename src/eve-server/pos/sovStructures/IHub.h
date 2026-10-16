/**
 * @name IHub.h
 *   Class for Infrastructure Hubs.
 *
 * @Author:         James
 * @date:   8 April 2021
 */


#ifndef EVEMU_POS_IHub_H_
#define EVEMU_POS_IHub_H_

#include "EVE_Outpost.h"
#include "pos/Structure.h"

class IHubSE
: public StructureSE
{
public:
    IHubSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& fData);
    virtual ~IHubSE();

    /* class type pointer querys. */
    virtual IHubSE*             GetIHubSE()            { return this; }
    virtual bool                isGlobal()              { return true; }
    virtual bool                IsOperSE()              { return true; }

    /* class type tests. */
    virtual bool                IsIHubSE()             { return true; }

    /* SystemEntity interface */
    virtual void                Process();
    virtual void                SetOnline();
    virtual void                SetOffline();
    virtual void                OnRemovedFromSpace(bool killed);
    // the claim's hub is only shootable while SBUs are online at more than half the stargates, and never while reinforced
    virtual bool                IsInvul();
    // the hit stops at the next reinforcement threshold (OutpostSiege)
    virtual void                GetDamageLimits(DamageLimits& into);
    virtual PyDict*             MakeSlimItem();

    /* virtual functions default to base class and overridden as needed */
    virtual void                Init();
    // a hit we survived:  reinforcement and the owner's alerts
    virtual void                OnDamaged(SystemEntity* pAttacker);

    /* sieges (Dominion rules):  reinforced at 25% shield (out at 0%) and at 50% armor (out at 25%), each for the type's
     *  24 hours give or take 3 (reinforcementDuration, reinforcementVariance).  destroyed at 0% structure, upgrades and all.
     *  only the claim's hub gets timers.  if the blockade breaks between the timers "the siege is considered over" */
    OutpostSiege::Phase         GetSiegePhase()         { return m_phase; }
    int64                       GetReinforcementEnd()   { return m_timerEnd; }
    // the claim's hub, its alliance holding the claim
    bool                        HasTimers();
    void                        SaveSiege();
    /* GM:  ends a reinforcement now (true), or puts it into its next reinforcement.  false if it can't */
    bool                        GMEndReinforcement();
    // GM:  full health, no siege
    void                        GMRepair();

protected:
    void                        ProcessSiege();
    void                        EnterReinforcement(OutpostSiege::Phase phase);
    void                        EndReinforcement();
    // attack alerts to the owner (SovereigntyIHDamage), at most once an hour.  force:  always
    void                        NotifyAttack(SystemEntity* pAttacker, bool force = false);

private:
    OutpostSiege::Phase m_phase;
    int64 m_timerEnd;               // reinforcement end (filetime)
    int64 m_lastProcess;            // shield regeneration, blockade check
    int64 m_lastSave;
    bool m_dirty;
};

#endif  // EVEMU_POS_IHub_H_
