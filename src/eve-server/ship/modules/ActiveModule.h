
 /**
  * @name ActiveModule.h
  *   active module class
  * @Author:         Allan
  * @date:   10 June 2015   -UD/RW 02 April 2017
  */


#ifndef _EVE_SHIP_MODULES_ACTIVE_MODULE_H_
#define _EVE_SHIP_MODULES_ACTIVE_MODULE_H_

#include "Client.h"
#include "ship/modules/GenericModule.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"

class MiningLaser;
class ShipSE;

class ActiveModule : public GenericModule
{
public:
    ActiveModule(ModuleItemRef mRef, ShipItemRef sRef);
    virtual             ~ActiveModule()                 { /* Do nothing here */ }

    /* class type pointer querys, public for anyone to access. */
    virtual ActiveModule* GetActiveModule()             { return this; }
    /* class type helpers.  public for anyone to access. */
    virtual bool        IsActiveModule() const          { return true; }

    /* GenericModule overrides */
    virtual void        Process();
    /* this will not move charge item.  charge is the charge in the slot (ModuleManager merges more of the same type into it).
     *  in space this takes the reload time (OnChargeBeingLoadedToModule), unless instant (the ship's own fitting loading)
     * must NOT throw
     */
    virtual void        LoadCharge(InventoryItemRef charge, bool instant=false);
    // this will not move charge item.
    // must NOT throw
    virtual void        UnloadCharge();
    virtual void        AbortCycle(bool force=false);
    virtual void        Deactivate(std::string effect="");
    /* cancel current cycle after timer ends. */
    virtual void        DeactivateCycle(bool abort=false); // this needs state=Deactivating for normal op.  set abort=true to cancel current cycle
    virtual void        Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    virtual void        RemoveTarget(SystemEntity* pSE);

    /* generic DoCycle() for active modules that only affect ship on Activate/Deactivate (not recurring on each cycle)
     *  for modules that perform action on each DoCycle(), they will override this call in their class implementation
     */
    virtual uint32      DoCycle();
    /* called once for every cycle that runs to its end (the last one too), before the module stops or starts its next
     *  cycle.  never on an aborted cycle.  results that come at the end of a cycle (scanners, data miners) go here */
    virtual void        CycleComplete();
    /* client message (key, args) shown when the effect stops (OnGodmaShipEffect error), e.g. SalvagingSuccess.  caller owns it */
    virtual PyTuple*    TakeStopMessage()               { return nullptr; }

    /* functions to be handled in derived classes as needed */
    virtual void        Update();  // this is used to set char mods on active modules created before pilot entered ship
    virtual void        ApplyDamage()                   { /* do nothing here */ }
    virtual uint16      GetReloadTime()                 { return m_reloadTime; }
    // this is a check for those active modules that need it (mining, weapons) and overridden as needed
    virtual bool        CanActivate();
    /* apply charge effects when undocking, as they are reset and not called anywhere else */
    virtual void        ReprocessCharge();
    /* dogma hooks.  modules whose target or active effects are handled in code (ecm jams, gang links) return false */
    virtual bool        ApplyTargetEffects()            { return true; }
    virtual bool        ApplyActiveEffects()            { return true; }

    /* ActiveModule methods */
    virtual uint32      GetTargetID()           { return m_targetID; }
    uint16              GetEffectID()           { return m_effectID; }
    SystemEntity*       GetTargetSE()           { return m_targetSE; }

    void                LaunchProbe();
    void                LaunchMissile();
    void                LaunchSnowBall();
    void                LaunchInterdictionSphere();

    /* warp disruption field generator (HIC).  without a script it projects a warp disruption sphere around the ship;
     *  with the focused warp disruption script it is a targeted point of infinite strength */
    bool                IsScriptedDisruptor();

    /* afterburners and microwarpdrives (group Propulsion Module).  see EVE_Propulsion.h for the speed formula */
    bool                IsPropulsionModule() const;
    /* a running prop module's velocity bonus follows changes of its speed factor, thrust and the ship's mass (overload,
     *  warp disruption field):  the ship's maxVelocity and the client's ball are brought up to date.  no-op when idle */
    void                RefreshPropBoost();

    /* new effects processing code and updates */
    void                ApplyEffect(int8 state, bool active=false);
    /* common method for all modules that have a visual effect when active */
    void                ShowEffect(bool active=false, bool abort=false);

protected:
    // we do not own any of these next 5
    ShipSE*             m_shipSE;       // our ship in space.  its bubble is looked up when needed (see Bubble())
    SystemEntity*       m_targetSE;
    DestinyManager*     m_destinyMgr;
    SystemManager*      m_sysMgr;
    TargetManager*      m_targMgr;

    /* our ship's current bubble (nullptr if none).  not cached:  the ship changes bubbles while a module cycles, and an
     *  empty bubble is freed (BubbleManager::RemoveEmpty()) */
    SystemBubble*       Bubble() const;

    void                Clear();
    /* generic activation rules: reactivation delay and group limits.  throws, before any state is changed */
    void                CheckActivationRules();
    /* target rules: assistance and offensive modifier immunity.  calls Clear() and throws */
    void                CheckTargetRules(uint16 effectID);
    /* heat.  a cycle starting (or the module stopping) applies pending overload requests, and tells the ship
     *  which modules are heating their rack.  linked weapons do this for the whole group */
    void                UpdateCycleHeat(bool cycling);
    void                CheckHeatDamage();
    /* crime: each cycle on a target is an aggressive (or assisting) act.  CrimeWatch decides if it's a crime */
    void                CrimeCheck();
    void                ProcessActiveCycle();           // checks and sets cap use
    void                UpdateCharge(uint16 attrID, uint16 testAttrID, uint16 srcAttrID, InventoryItemRef iRef);
    void                UpdateDamage(uint16 attrID, uint16 srcAttrID, InventoryItemRef iRef);

    /* for linked weapons */
    void                SetSlaveData(ShipSE* pShip, ActiveModule* pMaster = nullptr);

    /* for modules that use charges */
    void                ConsumeCharge();                // common code to reduce ammo by one unit.

    uint32              GetRemainingCycleTimeMS()       { return m_timer.GetRemainingTime(); }

    void                SetTimer(uint32 time);
    void                StopTimer()                     { m_timer.Disable(); }

    uint16              m_reloadTime;
    uint16              m_effectID;                     //passed to us by activate
    uint32              m_targetID;                     //passed to us by activate
    int64               m_reactivateAt;                 // filetime the module can be activated again (moduleReactivationDelay)

    // protected to allow derived usage
    bool                m_Stop :1;
    bool                m_heating :1;                   // the current cycle is overloaded
    bool                m_usesCharge :1;
    bool                m_needsCharge :1;
    bool                m_needsTarget :1;

private:
    Timer               m_timer;
    Timer               m_reloadTimer;

    // the charge is in:  loaded, and a new charge's effects go on the (online) module
    void                FinishLoading();

    /* ship and cargo scanners:  results at the end of the cycle (OnShipScanCompleted / OnCargoScanComplete) */
    void                ShipScanResult();
    void                CargoScanResult();
    // the target resists this scanner (shipScanResistance / cargoScanResistance, % chance)
    bool                ScanResisted(uint16 resistAttr);

    /* warp disruption field generator, sphere mode */
    void                StartDisruptField();
    void                StopDisruptField();
    void                FieldPenalties(bool add);       // signature radius, mass and prop mod penalties, no remote assistance
    bool                m_fieldActive;

    /* propulsion modules.  speedBoostFactorCalc2 holds the velocity multiplier while the module runs (the dogma effect
     *  multiplies the ship's maxVelocity by it, and divides it out again when it stops) */
    double              PropSpeedMultiplier(double shipMass);
    // a warp scrambler (effect warpScrambleTargetMWDBlockActivation) on our ship blocks modules that need High Speed Maneuvering
    bool                IsActivationBlocked();
    // a warp scrambler's cycle on a ship ends that ship's running microwarpdrives
    void                BlockTargetMWDs();

    uint32              m_slaveCycleMS = 0;     // cycle time copied from the group's master (see SetSlaveData)
};


#endif  // _EVE_SHIP_MODULES_ACTIVE_MODULE_H_

/* {'messageKey': 'DeniedActivateCloaked', 'dataID': 17883388, 'suppressable': False, 'bodyID': 259487, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 771}
 * {'messageKey': 'DeniedActivateControlling', 'dataID': 17880010, 'suppressable': False, 'bodyID': 258228, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2230}
 * {'messageKey': 'DeniedActivateFrozen', 'dataID': 17883391, 'suppressable': False, 'bodyID': 259488, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 772}
 * {'messageKey': 'DeniedActivateInJump', 'dataID': 17883394, 'suppressable': False, 'bodyID': 259489, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 773}
 * {'messageKey': 'DeniedActivateInWarp', 'dataID': 17883704, 'suppressable': False, 'bodyID': 259597, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 774}
 * {'messageKey': 'DeniedActivateTargetAssistDisallowed', 'dataID': 17883397, 'suppressable': False, 'bodyID': 259490, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 775}
 * {'messageKey': 'DeniedActivateTargetModuleDisallowed', 'dataID': 17883400, 'suppressable': False, 'bodyID': 259491, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 776}
 * {'messageKey': 'DeniedActivateTargetNotPresent', 'dataID': 17883403, 'suppressable': False, 'bodyID': 259492, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 777}
 * {'messageKey': 'DeniedActivateTargetOffModDisallowed', 'dataID': 17883406, 'suppressable': False, 'bodyID': 259493, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 778}
 * {'FullPath': u'UI/Messages', 'messageID': 259487, 'label': u'DeniedActivateCloakedBody'}(u'Interference from the cloaking you are doing is preventing your systems from functioning at this time.', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 259488, 'label': u'DeniedActivateFrozenBody'}(u'You are unable to activate any modules because you have been frozen by a GM.', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 259489, 'label': u'DeniedActivateInJumpBody'}(u'Interference from the jump you are doing is preventing your systems from functioning at this time.', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 259490, 'label': u'DeniedActivateTargetAssistDisallowedBody'}(u'You cannot activate that module on the target as interference prevents assistance from being given to them.', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 259491, 'label': u'DeniedActivateTargetModuleDisallowedBody'}(u'You cannot activate that module on the target as interference prevents modules of that type from being used on them.', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 259492, 'label': u'DeniedActivateTargetNotPresentBody'}(u'You cannot activate that module as the target is no longer present.', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 259493, 'label': u'DeniedActivateTargetOffModDisallowedBody'}(u'You cannot activate that module on the target as interference prevents modules of that type from being used on them.', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 259597, 'label': u'DeniedActivateInWarpBody'}(u'Interference from your warp prevents your systems from functioning at this time.', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 258228, 'label': u'DeniedActivateControllingBody'}(u'You are unable to activate any modules while you are controlling some other objects.', None, None)
 */