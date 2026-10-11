
/**
 * @name CustomsOffice.h
 *   Class for Customs Offices.
 *
 * @Author:         Allan
 * @date:   14 July 2019
 */


#ifndef EVEMU_POS_CUSTOMS_OFFICE_H_
#define EVEMU_POS_CUSTOMS_OFFICE_H_

#include "../eve-server.h"

#include "inventory/InventoryItem.h"
#include "pos/PosMgrDB.h"
#include "system/SystemEntity.h"

class PlanetSE;
class Call_TaxRateValuesDict;

class CustomsSE
: public ObjectSystemEntity
{
public:
    CustomsSE(StructureItemRef sRef, EVEServiceManager& services, SystemManager* system, const FactionData& data);
    virtual ~CustomsSE()  { /* do nothing here */ }


    /* class type pointer querys. */
    virtual CustomsSE*          GetCOSE()               { return this; }

    /* class type tests. */
    virtual bool                IsCOSE()                { return true; }
    //virtual bool                isGlobal()              { return true; }

    /* SystemEntity interface */
    virtual void                Process();
    virtual void                EncodeDestiny( Buffer& into );
    virtual PyDict*             MakeSlimItem();

    /* virtual functions default to base class and overridden as needed */
    virtual void                Killed(Damage &damage);
    // CONCORD's offices, unanchored gantries and reinforced offices can't be shot (CanBeAttacked)
    virtual bool                IsInvul();
    virtual void                InitData();
    virtual void                Init();

    /* virtual functions to be overridden in derived classes */
    virtual void     MissileLaunched(Missile* pMissile) { /* Do nothing here */ }

    /* specific functions handled in this class. */
    PlanetSE*                   GetPlanetSE()           { return m_planetSE; }
    void                        GetEffectState(PyList& into);
    int8                        GetState() const        { return m_cData.state; }
    float                       GetStatus()             { return m_cData.status; }
    bool                        IsReinforced()          { return (m_cData.state == EVEPOS::EntityState::ShieldReinforce); }
    bool                        IsGantry()              { return (m_self->groupID() == EVEDB::invGroups::Orbital_Construction_Platform); }
    // anchoring, upgrading, unanchoring or reinforced:  a timer is running on this orbital
    bool                        IsInTimedState()        { return (m_cData.state == EVEPOS::EntityState::Anchoring) or (m_cData.state == EVEPOS::EntityState::Onlining)
                                                              or (m_cData.state == EVEPOS::EntityState::Unanchoring) or (m_cData.state == EVEPOS::EntityState::ShieldReinforce); }

    /* orbital states (entityConst):  a gantry is launched Unanchored, anchors at a planet (Anchoring -> Anchored), is
     *  upgraded with its materials (Onlining) into a customs office, or unanchored again (Unanchoring -> Unanchored).
     *  customs offices are Anchored (the client's menus need that state) and go ShieldReinforce at 25% shield.
     *  orbitalTimestamp is the end of the running state.
     */
    // posMgr.AnchorOrbital / UnanchorOrbital / OnlineOrbital ("start upgrade").  these throw
    void                        AnchorOrbital(Client* pClient);
    void                        UnanchorOrbital(Client* pClient);
    void                        OnlineOrbital(Client* pClient);
    // ends the timed state when its time is up (SystemManager::ProcessTic, through the planet)
    void                        ProcessState();
    // GM:  ends the running state now
    void                        CompleteStateChange();
    // the gantry becomes the customs office it builds.  deletes this entity
    void                        Upgrade();
    bool                        CanBeAttacked();
    // after damage:  25% shield or less puts a player office into reinforcement
    void                        OnShieldDamaged();
    // posMgr callers:  the gantry or office, owned by the client's corp, and the role to change it
    void                        CheckCorpAccess(Client* pClient, int64 role);

    // for orbital infrastructure
    void                        SetPlanet(uint32 planetID);
    uint32                      GetPlanetID()           { return m_oData.planetID; }
    float                       GetTaxRate(Client* pClient);

    PyRep*                      GetSettingsInfo();
    void     GetSettingsInfo(EVEPOS::CustomsData &data) { data = m_cData; }

    void    UpdateSettings(int8 selectedHour, int8 standingValue, bool ally, bool standings, Call_TaxRateValuesDict& taxRateValues);

    void    VerifyAddItem(InventoryItemRef iRef);   // this must throw on failure
    // before an item is moved into the office's hangar (Inventory::ValidateAddItem).  throws
    void    ValidateAddItem(InventoryItemRef iRef);
    // planetCommon.CanPutTypeInCustomsOffice
    static bool CanStore(uint16 typeID);
    // the owner's standing towards the character as one of EVEPOS::StandingValues
    int8    GetStandingTier(Client* pClient);

    // the office's storage is per character (type capacity each)
    double  GetUsedSpace(uint32 ownerID);
    double  GetAvailableSpace(uint32 ownerID);
    // an exported commodity:  onto the owner's stack of that type in the hangar, or a new stack
    void    AddCommodity(uint32 ownerID, uint16 typeID, uint32 quantity);

    // GM: Take Control, and Directors' ownership transfers
    void    ChangeOwner(uint32 corpID, int32 allianceID);

protected:
    void                        SendSlimUpdate();
    void                        SendEffectUpdate(int16 effectID, bool active);
    // dunRotation:  the office faces its planet
    void                        UpdateRotation();
    void                        SaveState();
    // OnMajorPlanetStateUpdate to the planet's colonists online (their customs office changed)
    void                        NotifyColonists();

private:
    PosMgrDB                    m_db;

    EVEPOS::CustomsData         m_cData;
    EVEPOS::OrbitalData         m_oData;

    SystemManager*              m_system;
    PlanetSE*                   m_planetSE;

    // an office that came out of reinforcement goes again only once its shield was above 25%
    bool                        m_reinforceArmed;
};

#endif  // EVEMU_POS_CUSTOMS_OFFICE_H_
