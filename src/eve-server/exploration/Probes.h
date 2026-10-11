 /**
  * @name Probes.h
  *     Probe Item/SE class for EVEmu
  *
  * @Author:        Allan
  * @date:          10 March 2018
  *
  */

#ifndef EVEMU_EXPLORE_PROBES_H_
#define EVEMU_EXPLORE_PROBES_H_


#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "../../eve-common/EVE_Scanning.h"
#include "inventory/InventoryItem.h"
#include "system/SystemEntity.h"


/**
 * InventoryItem for probe object.
 */
class ProbeItem
: public InventoryItem
{
    friend class InventoryItem; // to let it construct us
public:
    ProbeItem(uint32 itemID, const ItemType &_type, const ItemData &_data);
    virtual ~ProbeItem()                                { /* Do nothing here */ }

    /* virtual functions default to base class and overridden as needed */
    virtual void Delete();  //totally removes item from game and deletes from the DB.


    static ProbeItemRef Load( uint32 itemID);
    static ProbeItemRef Spawn( ItemData &data);

protected:
    using InventoryItem::_Load;
    //virtual bool _Load();

    // Template loader:
    template<class _Ty>
    static RefPtr<_Ty> _LoadItem( uint32 itemID, const ItemType &type, const ItemData &data)
    {
        if ((type.groupID() != EVEDB::invGroups::Scanner_Probe)
        and (type.groupID() != EVEDB::invGroups::Survey_Probe)
        //and (type.groupID() != EVEDB::invGroups::Warp_Disruption_Probe)  this wont work here...
        and (type.groupID() != EVEDB::invGroups::Obsolete_Probes)) {
            _log(ITEM__ERROR, "Trying to load %s as Probe.", sDataMgr.GetCategoryName(type.categoryID()));
            if (sConfig.debug.StackTrace)
                EvE::traceStack();
            return RefPtr<_Ty>(nullptr);
        }

        return ProbeItemRef( new ProbeItem(itemID, type, data));
    }

    static uint32 CreateItemID( ItemData &data);
};



/**
 * DynamicSystemEntity which represents celestial object in space
 */
class EVEServiceManager;
class Scan;

class ProbeSE : public DynamicSystemEntity {
public:
    // abandoned probe c'tor
    ProbeSE(ProbeItemRef self, EVEServiceManager& services, SystemManager* system);
    // launched probe c'tor
    ProbeSE(ProbeItemRef self, EVEServiceManager& services, SystemManager* system, InventoryItemRef moduleRef, ShipItemRef shipRef);
    virtual ~ProbeSE();

    /* Process Calls - Overridden as needed in derived classes */
    virtual bool ProcessTic();

    /* class type pointer querys. */
    virtual ProbeSE*            GetProbeSE()            { return this; }
    /* class type tests. */
    /* Base */
    virtual bool                IsProbeSE()             { return true; }

    /* virtual functions default to base class and overridden as needed */
    virtual void                MakeDamageState(DoDestinyDamageState &into);
    virtual PyDict*             MakeSlimItem();

    /* specific functions handled in this class. */
    void RecoverProbe(PyList* list);
    void UpdateProbe(ProbeData& data);
    // removes probe from system and scan map, and sends RemoveProbe call to client.
    //  fromEntityList=false when called from EntityList's probe loop (the loop erases it)
    void RemoveProbe(bool fromEntityList=true);

    /* lost probes:  probes stay in space when their pilot docks, jumps, changes ship or logs off.  they keep their owner
     *  and life, and can be reconnected (ScanBound::ReconnectToLostProbes) from a ship with their launcher online */
    void Abandon();
    void Reconnect(Client* pClient, ShipItemRef shipRef, InventoryItemRef launcherRef);
    bool IsAbandoned()                                  { return (m_client == nullptr); }
    uint32 GetOwnerCharID()                             { return m_ownerID; }
    uint32 GetLauncherID()                              { return (m_moduleRef.get() != nullptr ? m_moduleRef->itemID() : 0); }
    // probe scan time (ms):  the launcher's duration, with its dogma (Astrometric Acquisition, hulls)
    uint32 GetLauncherDuration();
    void SendNewProbe();
    void SendSlimChange();
    void SendStateChange(uint8 state);
    void SendWarpStart(float travelTime);
    void SendWarpEnd();

    void SetScan(Scan* pScan)                           { m_scan = pScan; }
    void RemoveScan()                                   { m_scan = nullptr; }
    void StartStateTimer(uint32 time)                   { m_stateTimer.Start(time); }

    bool IsMoving();

    void SetState(uint8 state=Probe::State::Idle)       { m_state = state; }
    uint8 GetState()                                    { return m_state; }
    uint8 GetRangeStep()                                { return m_rangeStep; }
    // range step 1..8 -> scan range = baseScanRange * rangeFactor^(step-1) AU (client GetScanRangeStepsByTypeID)
    void SetRangeStep(uint8 step);

    // remaining move time in ms
    uint16 GetMoveTime()                                { return m_stateTimer.GetRemainingTime(); }
    int64 GetExpiryTime()                               { return m_expiry; }

    // max deviation in meters at the current range
    float GetMaxDeviation()                             { return m_scanRange * m_scanDeviation; }
    // probe scan range in meters
    float GetScanRange()                                { return m_scanRange; }
    // probe strength (skills, launcher, rigs, hull) at the current range:  halves each time the range doubles
    float GetEffectiveStrength();

    GPoint GetDestination()                             { return m_destination; }
    const char* GetStateName(uint8 state);

    bool CanScanShips()                                 { return m_scanShips; }

private:
    // strength and deviation from the probe, skills, launcher, ship and fittings
    void CalcStats();
    // the pilot can take this probe back aboard (same ship, in space, in this system)
    bool CanReturn();

    Timer m_lifeTimer;
    Timer m_returnTimer;
    Timer m_stateTimer;

    Scan* m_scan;
    Client* m_client;

    GPoint m_destination;

    ShipItemRef m_shipRef;
    InventoryItemRef m_moduleRef;

    bool m_scanShips;

    uint8 m_state;
    uint8 m_rangeStep;          // range 'click'.  values are 1 to 8.  sent from client

    int64 m_expiry;             // probe lifetime as filetime

    float m_secStatus;
    float m_rangeFactor;        // range multiplier per step (2)
    float m_scanRange;          // in m, from the range step
    float m_scanStrength;       // strength at the smallest range (baseSensorStrength with all bonuses)
    float m_scanDeviation;      // max scan deviation as a fraction of the scan range (baseMaxScanDeviation with bonuses)

};

#endif  // EVEMU_EXPLORE_PROBES_H_
