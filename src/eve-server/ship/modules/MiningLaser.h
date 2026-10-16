
 /**
  * @name MiningModule.h
  *   mining module class
  * @Author:         Allan
  * @date:   10 June 2015   -UD/RW 02 April 2017
  */


#ifndef _EVE_SHIP_MODULES_MININGLASER_H_
#define _EVE_SHIP_MODULES_MININGLASER_H_

#include "ship/modules/ActiveModule.h"

class AsteroidSE;

class MiningLaser: public ActiveModule
{
public:
    MiningLaser(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~MiningLaser()                              { /* do nothing here */ }

    /* GenericModule overrides */
    virtual MiningLaser*        GetMiningModule()       { return this; }
    virtual bool IsMiningLaser() const                  { return true; }
    virtual void DeactivateCycle(bool abort=false);

    /* ActiveModule overrides */
    virtual uint32 DoCycle();
    //virtual void Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    //virtual void Deactivate(std::string effect="");

    //  functions to be handled in derived classes as needed
    virtual void LoadCharge(InventoryItemRef charge, bool instant=false);
    virtual void UnloadCharge();
    virtual bool CanActivate();

    /* MiningLaser methods */
    float GetMiningVolume();

    // specific ProcessCycle call for depleted rocks
    void Depleted(std::multimap<float, MiningLaser*> &mMap);
    void AddOreAndDeactivate(uint16 typeID, float amt, bool slave=true);

    void CancelOnError()                                { ActiveModule::DeactivateCycle(true); }


protected:
	void ProcessCycle(bool abort=false);
	// takes oreAmount from the target rock into the hold (all of it when that empties the rock)
	void AddOre(InventoryItemRef roidRef, float oreAmount, float roidQuantity, float cycleVol, bool abort);


private:
    //cached item-type stuff
    bool m_rMiner, m_dcMiner, m_iMiner, m_gMiner;
    bool m_IsInitialCycle;
    bool m_depleting;           // emptying its rock (AddOre).  stopping the module meanwhile mustn't process another cycle

    float m_crystalDmg;
    float m_crystalDmgAmount;
    float m_crystalDmgChance;

    uint16 m_crystalRoidGrp;

    EVEItemFlags m_holdFlag;
};

#endif  // _EVE_SHIP_MODULES_MININGLASER_H_
