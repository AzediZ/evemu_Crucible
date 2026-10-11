
/**
 * @name JumpBridge.h
 *   Class for POS JumpBridge Modules.
 *
 * @Author:           James
 * @date:   13 October 2021
 */


#ifndef EVEMU_POS_JUMPBRIDGE_H_
#define EVEMU_POS_JUMPBRIDGE_H_


#include "pos/Structure.h"


class JumpBridgeSE
: public StructureSE
{
public:
    JumpBridgeSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& data);
    virtual ~JumpBridgeSE()                                { /* do nothing here */ }

    /* class type pointer querys. */
    virtual JumpBridgeSE*       GetJumpBridgeSE()          { return this; }

    /* class type tests. */
    virtual bool                IsJumpBridgeSE()           { return true; }

    /* SystemEntity interface */
    virtual void                Process();
    virtual PyDict*             MakeSlimItem();
    virtual void                SetOnline();
    virtual void                SetOffline();
    // unlinks the remote bridge
    virtual void                OnRemovedFromSpace(bool killed);

    /* virtual functions default to base class and overridden as needed */
    virtual void Init();
    virtual void InitData();

    /* bridge link */
    const EVEPOS::JumpBridgeData& GetBridgeData()       { return m_bridgeData; }
    bool                        IsLinked()              { return (m_bridgeData.toItemID != 0); }
    // set/clear the link (db and slim item)
    void                        SetLink(uint32 toSystemID, uint32 toItemID, uint16 toTypeID);
    void                        ClearLink();
    // throws the client's errors when the bridge can't be onlined (not linked, high security space)
    void                        CheckOnline();

    /* fuel (jumpDriveConsumptionType in the bridge's own hold) */
    uint16                      GetFuelType();
    uint32                      CountFuel();
    void                        ConsumeFuel(uint32 qty);
    // fuel for a ship of shipMass to jump ly (client posConst.GetJumpFuelConsumption)
    uint32                      FuelNeeded(double ly, double shipMass);

protected:
    void SendSlimUpdate();

private:
    EVEPOS::JumpBridgeData      m_bridgeData;

};

#endif  // EVEMU_POS_JUMPBRIDGE_H_
