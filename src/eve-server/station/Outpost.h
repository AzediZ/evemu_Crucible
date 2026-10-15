/**
 * @name Outpost.h
 *   Class for Outposts.
 *
 * @Author:           James
 * @date:   17 October 2021
 */

#ifndef EVEMU_POS_OUTPOST_H_
#define EVEMU_POS_OUTPOST_H_

#include "DataClasses.h"
#include "pos/Structure.h"
#include "station/Station.h"
#include "../eve-common/EVE_Outpost.h"
#include "../eve-common/EVE_Station.h"

// Class for Construction Platform (egg)
class PlatformSE
: public StructureSE
{
public:
    PlatformSE(StructureItemRef structure, EVEServiceManager &services, SystemManager *system, const FactionData &fData)
    : StructureSE(structure, services, system, fData)   { /* do nothing here */ }

    virtual ~PlatformSE()                               { /* do nothing here */ }

    /* class type pointer querys. */
    virtual PlatformSE*         GetPlatformSE()         { return this; }

    /* class type tests. */
    virtual bool                IsPlatformSE()          { return true; }
    virtual bool                isGlobal()              { return true; }
    virtual bool                IsOperSE()              { return true; }

};

// Class for Outpost (StationSE derivative).  the npc conquerable stations use it too:  both can be besieged (EVE_Outpost.h)
class OutpostSE
: public StationSE
{
public:
    OutpostSE(StationItemRef station, EVEServiceManager &services, SystemManager* system);
    virtual ~OutpostSE();

    /* class type pointer querys. */
    virtual OutpostSE* GetOutpostSE()                   { return this; }
    /* Static */
    virtual bool IsOutpostSE()                          { return true; }
    // reinforced, or out of reach of attackers (OutpostSiege::Vulnerable).  outposts can always be locked
    virtual bool IsInvul();
    // the hit stops at the next reinforcement threshold, and the station is taken rather than destroyed
    virtual void GetDamageLimits(DamageLimits& into);
    virtual void MakeDamageState(DoDestinyDamageState &into)    { FillDamageState(into); }
    virtual PyDict* MakeSlimItem();
    // to every pilot in space here (stations are global and have no destiny manager), at most once a second (ProcessSiege)
    virtual void SendDamageStateChanged()               { m_damagePending = true; }
    void SendSlimUpdate();

    /* station services (OutpostServices):  their structures sit at the station and are shot like it */
    // the service structures of the services in newMask that oldMask lacked
    void SpawnStationServices(uint32 oldMask, uint32 newMask);
    // after the system loaded:  a besiegeable station gets the service structures it's missing
    void InitServices();
    // a service structure here (StationServiceSE c'tor)
    void AddService(uint32 itemID)                      { m_serviceIDs.push_back(itemID); }
    const std::vector<uint32>& GetServiceIDs()          { return m_serviceIDs; }
    StationServiceSE* GetService(uint16 typeID);
    // the services can be shot whenever the station can
    bool ServicesVulnerable();
    // a service's attack alerts are the station's
    void OnServiceAttacked(SystemEntity* pAttacker)     { NotifyAttack(pAttacker); }
    /* any of serviceIDs (Station:: bits) shot down at this station.  only loaded systems are asked:  a pilot docked there
     *  keeps its system loaded */
    static bool IsServiceOffline(uint32 stationID, uint32 serviceIDs);
    // throws when it is
    static void CheckServiceOnline(uint32 stationID, uint32 serviceIDs);

    /* works out what the outpost's upgrade level and improvements give (OutpostRules:  services, office slots, refinery,
     *  assembly lines) from its type's static data, saves it and updates the station data the clients get.
     *  false when the station is not a player outpost type */
    static bool Refresh(uint32 stationID);
    // outposts and the npc conquerable stations
    static bool IsConquerableType(uint16 typeID);

    /* sieges */
    // a hit we survived (Damage.cpp):  reinforcement, the owner's alerts, and the station changing hands at 0 structure
    void OnSiegeDamage(SystemEntity* pAttacker);
    // every tic (SystemManager::Process):  reinforcements end, sieges end, shields regenerate, damage is saved
    void ProcessSiege();
    void SaveSiege();
    OutpostSiege::Phase GetSiegePhase()                 { return m_phase; }
    int64 GetReinforcementEnd()                         { return m_timerEnd; }
    /* GM:  ends a reinforcement now (true), or puts it into its next reinforcement.  false if it can't */
    bool GMEndReinforcement();
    // GM:  full health, no siege
    void GMRepair();

protected:
    // the owner's corporation and alliance, from the station data
    void UpdateOwner();
    // owner, sovereignty and blockade:  can it be shot, and does it get reinforcement timers
    void UpdateVulnerability();
    void EnterReinforcement(OutpostSiege::Phase phase);
    void EndReinforcement();
    // the station goes to corpID (Dominion:  the corporation of the last pilot to damage it)
    void Conquer(uint32 corpID, uint32 charID);
    // attack alerts to the owner (StationAggression2), at most once an hour.  force:  always
    void NotifyAttack(SystemEntity* pAttacker, bool force = false);
    void FlushDamageState();

private:
    bool m_conquerable;             // a conquerable type in claimable space
    bool m_vulnerable;
    bool m_timers;                  // the owner holds sovereignty here:  reinforcement timers
    bool m_dirty;
    bool m_damagePending;
    OutpostSiege::Phase m_phase;
    int64 m_timerEnd;               // reinforcement end (filetime)
    int64 m_lastProcess;            // shield regeneration
    int64 m_lastSave;
    int64 m_lastOwnerCheck;
    int64 m_lastAttackNotify;
    int64 m_lastDamageSend;
    std::vector<uint32> m_serviceIDs;    // the service structures here (looked up by ID:  they unload on their own)
};

/* a station service structure (Destructible Station Services):  shot down to 1% structure, its service at the station stops
 *  until it is repaired to 50% shield and full armor and structure (OutpostServices).  it can't be destroyed */
class StationServiceSE
: public ObjectSystemEntity
{
public:
    StationServiceSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data);
    virtual ~StationServiceSE();

    /* class type pointer querys. */
    virtual StationServiceSE*   GetStationServiceSE()   { return this; }
    /* class type tests. */
    virtual bool                IsStationServiceSE()    { return true; }

    /* SystemEntity interface */
    virtual bool                IsInvul();
    virtual void                GetDamageLimits(DamageLimits& into)     { into.canDie = false; }
    virtual void                EncodeDestiny(Buffer& into);
    virtual void                Killed(Damage& damage)  { /* never */ }

    // a hit we took (Damage.cpp)
    void                        OnDamaged(SystemEntity* pAttacker);
    // from the station's ProcessSiege:  shields regenerate, the service comes back, the state is saved
    void                        ProcessService(int64 now);
    void                        Save();
    // the station changed hands (OutpostSE::Conquer)
    void                        SetOwner(uint32 corpID, int32 allianceID);
    // GM:  full health and online
    void                        GMRepair();

    bool                        IsOnline() const        { return m_online; }
    uint32                      GetStationID() const    { return m_stationID; }
    // the station service bits this one stands for (OutpostServices::Covers)
    uint32                      GetCovers() const       { return m_covers; }

private:
    OutpostSE*                  GetStation();
    void                        SetOnline(bool online);

    uint32                      m_stationID;
    uint32                      m_covers;
    bool                        m_online;
    bool                        m_dirty;
    int64                       m_lastProcess;
    int64                       m_lastSave;
};

#endif  // EVEMU_POS_OUTPOST_H_
