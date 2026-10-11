
/**
 * @name Tower.h
 *   Class for POS Towers.
 *
 * @Author:         Allan
 * @date:   28 December 17
 */


#ifndef EVEMU_POS_TOWER_H_
#define EVEMU_POS_TOWER_H_


#include "pos/Structure.h"

class Misile;

class TowerSE
: public StructureSE
{
public:
    TowerSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& fData);
    virtual ~TowerSE()                                  { /* do nothing here */ }

    /* class type pointer querys. */
    virtual TowerSE*            GetTowerSE()            { return this; }

    /* class type tests. */
    virtual bool                IsTowerSE()             { return true; }
    // a reinforced tower takes no damage
    virtual bool                IsInvul()               { return (IsReinforced() or StructureSE::IsInvul()); }

    /* SystemEntity interface */
    virtual void                Process();
    virtual PyDict*             MakeSlimItem();

    /* virtual functions default to base class and overridden as needed */
    virtual void                Init();
    virtual void                InitData();
    virtual void                Killed(Damage &damage);

    /* virtual functions to be overridden in derived classes */
    virtual void     MissileLaunched(Missile* pMissile) { /* Do nothing here */ }


    /* basic tower methods */
    virtual void                SetOnline();
    virtual void                SetOffline();

    virtual void                Online();
    virtual void                Operating();
    virtual void                Reinforced();

    virtual void                Scoop();

    void                        ReinforceTower();

    /* tower data methods */
    PyRep* GetDeployFlags();
    PyRep* GetUsageFlagList();
    PyRep* GetProcessInfo();

    void UpdatePassword();
    void SetDeployFlags(int8 anchor=0, int8 unanchor=0, int8 online=0, int8 offline=0);
    void SetUseFlags(uint32 itemID, int8 view=0, int8 take=0, int8 use=0);

    /* general tower data methods */
    void SetStanding(float set)                         { m_tdata.standing = set; }
    void SetStatus(float set)                           { m_tdata.status = set; }
    void SetStatusDrop(bool set)                        { m_tdata.statusDrop = set; }   // shoot if their status is dropping
    void SetCorpWar(bool set)                           { m_tdata.corpWar = set; }      // shoot if we are at war with them
    void SetCorpAccess(bool set)                        { m_tdata.allowCorp = set; }
    void SetAllyAccess(bool set)                        { m_tdata.allowAlliance = set; }
    void SetUseAllyStandings(bool set)                  { m_tdata.allyStandings = set; }    // use alliance standings
    void SetStandingOwnerID(uint32 id)                  { m_tdata.standingOwnerID = id; }    // whose standings the sentries use
    void SetPassword(std::string pass)                  { m_tdata.password = pass; }

    // sentry 'attack if standing/security status lower than' settings that are switched off (the client sends None)
    static constexpr float SentrySettingOff = -11.0f;
    static bool IsSentrySettingOff(float value)         { return (value < -10.5f); }

    bool HasForceField()                                { return (m_hasShield? true : false); }
    bool GetCorpAccess()                                { return m_tdata.allowCorp; }
    bool GetAllyAccess()                                { return m_tdata.allowAlliance; }
    // sentry security status limit (m_data.status is the structure's own status)
    float GetSentryStatus()                             { return m_tdata.status; }
    /* would the tower's guns shoot this pilot (sentry settings)?  never its own corp or alliance.
     *  standing: the pilot's (or corp's or alliance's, most specific first) standing from standingOwnerID below the limit
     *  status:   security status below the limit.   statusDrop is not used.   corpWar:  @todo F22 wars
     *  F18's tower guns use this too
     */
    bool ConsidersHostile(Client* pClient);
    bool GetStatusDrop()                                { return m_tdata.statusDrop; }
    bool GetCorpWar()                                   { return m_tdata.corpWar; }
    float GetStanding()                                 { return m_tdata.standing; }
    uint32 GetStandingOwnerID()                         { return m_tdata.standingOwnerID; }
    std::string GetPassword()                           { return m_tdata.password; }

    bool ShowInCalendar()                               { return m_tdata.showInCalendar; }
    bool SendFuelNotifications()                        { return m_tdata.sendFuelNotifications; }
    void SetShowInCalendar(bool set)                    { m_tdata.showInCalendar = set; }
    void SetSendFuelNotifications(bool set)             { m_tdata.sendFuelNotifications = set; }

    void GetTowerData(EVEPOS::TowerData& tData)         { tData = m_tdata; }

    uint16 GetSOI()                                     { return m_soi; }

    /* starbase rules */
    // EVEPOS::Access level of an EVEPOS::Deploy action
    int8 GetDeployFlag(int8 action);
    // the tower or the first of its structures that is anchoring, onlining or unanchoring
    StructureSE* GetBusyStructure();
    // one operation per tower at a time:  throws CantTowerIsAlreadyActive when another structure is busy
    void CheckNotBusy(StructureSE* pRequester);
    // a structure anchored (or anchoring, unanchoring) at this tower, nullptr if none
    StructureSE* GetAnchoredModule();
    // the tower is gone:  its structures go offline and belong to no tower
    void OrphanModules();
    const std::map<uint32, StructureSE*>& GetModules()  { return m_structs; }
    uint32 GetForceFieldID()                            { return (m_pShieldSE == nullptr ? 0 : m_pShieldSE->GetID()); }
    // inside the force field (only while the tower has one:  online with a password)
    bool InForceField(const GPoint& pos);
    double GetForceFieldRadius();

    /* fuel */
    /* invControlTowerResources for a tower in a system, as the client's fuel tab shows them:  rows of other factions,
     *  higher security or other wormhole classes skipped, x0.75 where the owner's alliance holds sovereignty, x pos.FuelRate,
     *  rounded up.  online:  fuel blocks and charters per hour (purpose 1).  strontium:  per hour of reinforcement (purpose 4)
     */
    static void GetFuelRequirements(uint16 towerTypeID, uint32 systemID, uint32 ownerAllianceID,
                                    std::map<uint16, uint32>& online, std::map<uint16, uint32>& strontium, bool applyFuelRate = true);
    // starbase charter types the tower needs in this system (empty where none are needed)
    static void GetCharterTypes(uint16 towerTypeID, uint32 systemID, std::vector<uint16>& into);
    // units of typeID in a bay (flagNone fuel bay, flagSecondaryStorage strontium bay)
    uint32 CountResource(uint16 typeID, EVEItemFlags flag);
    // CantOnlineTowerLacksResources without a cycle of every online resource in the fuel bay
    void CheckOnlineFuel();

    inline void AddModule(StructureSE* pSE)             { m_structs.emplace(pSE->GetID(), pSE); }
    inline void RemoveModule(StructureSE* pSE)          { m_structs.erase(pSE->GetID()); }

    // structure update methods
    // saves corp/alliance access and the password, and retunes the force field
    void UpdateAccess();
    void UpdateSentry()                                 { m_db.UpdateSentry(m_data.itemID, m_tdata); }
    void UpdateNotify()                                 { m_db.UpdateNotify(m_data.itemID, m_tdata); }
    void UpdatePermission()                             { m_db.UpdatePermission(m_data.itemID, m_tdata); }

    // fx methods
    void ApplyResist(uint16 type, float amount)         { /* do nothing here */ }
    void RemoveResist(uint16 type, float amount)        { /* do nothing here */ }

    // resource methods
    void OnlineModule(StructureSE* pSE)                 { RecalcLoad(); }
    void OfflineModule(StructureSE* pSE)                { RecalcLoad(); }
    // power and cpu used by the structures that are online or onlining
    void RecalcLoad();
    bool HasPG(float amount);
    bool HasCPU(float amount);
    float GetPGLoad()                                   { RecalcLoad(); return m_pgUsed; }
    float GetCPULoad()                                  { RecalcLoad(); return m_cpuUsed; }

    /* fuel cycle */
    // one hour of tower life (fuel, reinforcement end, moon processes, notifications, calendar)
    void ProcessCycle(int64 cycleTime);
    // takes one hour of every online resource from the fuel bay.  false (nothing taken) when anything is short
    bool ConsumeFuelCycle();
    // removes qty units of typeID from a bay
    void TakeResource(uint16 typeID, EVEItemFlags flag, uint32 qty);
    // full hours of fuel in the bay after the current one (UINT32_MAX when the tower needs none)
    uint32 FuelHoursLeft();
    // the tower and its structures go offline now
    void GoOffline(bool outOfFuel);

    /* reinforcement */
    // any structure of this tower took damage
    void OnStructureDamaged(StructureSE* pVictim, SystemEntity* pAttacker);
    // under 25% shield while online and armed:  reinforced for the whole hours of strontium in the bay
    void CheckReinforce();
    void EnterReinforced();
    void ExitReinforced();
    // who attacked this tower's structures (charID for pilots and their drones, itemID for npcs) and when (FILETIME)
    bool IsRecentAggressor(uint32 id, int64 window);

    /* notifications */
    // TowerAlertMsg to the starbase managers and defense operators, at most once an hour (final = the tower died)
    void NotifyAttack(StructureSE* pVictim, SystemEntity* pAttacker, bool final = false);
    // TowerResourceAlertMsg when the fuel lasts less than a day (once a day), or the tower ran out
    void CheckLowFuel(bool outOfFuel = false);
    // anchoring done:  AllAnchoringMsg to the alliance holding sovereignty here
    void OnAnchored();
    // the fuel expiry event in the corp calendar (showInCalendar)
    void UpdateFuelCalendar();

    /* bays:  fuel bay (flagNone) and strontium bay (flagSecondaryStorage) */
    virtual void ValidateAddItem(Client* pClient, InventoryItemRef iRef, EVEItemFlags toFlag);
    virtual void ValidateRemoveItem(Client* pClient, InventoryItemRef iRef);

protected:
    EVEPOS::TowerData m_tdata;

    SystemEntity* m_pShieldSE;
    //POS_AI* m_ai;

private:
    // the force field of an online tower with a password.  notify:  tell the bubble the tower's forceFieldID
    void CreateForceField(bool notify);
    void RemoveForceField();
    // harmonic and corp/alliance access of the field, to everyone in the bubble
    void UpdateForceField();
    // replays the hours missed while the system was unloaded
    void CatchUp();

    std::map<uint32, int64> m_aggressors;   // aggressor id / last attack
    int64 m_lastAttackNotify;               // last TowerAlertMsg
    bool m_fuelOut;         // ran out of fuel while reinforced:  offline when reinforcement ends
    bool m_catchUp;         // catch up on the first tic after loading
    bool m_replaying;       // replaying missed hours (saves once at the end)
    float m_pgUsed;
    float m_cpuUsed;

    bool m_hasShield;

    int8 m_tsize;   // tower size: small, med, large

    float m_pg;
    float m_cpu;

    uint16 m_soi;   // Sphere Of Influence, 45km max

    std::map<uint32, StructureSE*> m_structs;  // structID/pSSE

};

#endif  // EVEMU_POS_TOWER_H_
