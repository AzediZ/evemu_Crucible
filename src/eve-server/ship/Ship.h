/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
    Author:        Bloody.Rabbit
    Updates:    Allan
*/

#ifndef __SHIP__H__INCL__
#define __SHIP__H__INCL__

#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "effects/EffectsData.h"
#include "fleet/FleetData.h"
#include "inventory/ItemType.h"
#include "inventory/InventoryItem.h"
#include "ship/ShipDB.h"
#include "ship/modules/ModuleManager.h"
#include "system/SystemEntity.h"

/**
 * InventoryItem which represents ShipItem.
 */
class Client;
class GenericModule;
class DroneSE;

class ShipItem
: public InventoryItem
{
    friend class InventoryItem;    // to let it construct us

protected:
    ShipItem(uint32 shipID, const ItemType &type, const ItemData &data);
    ~ShipItem() noexcept;

public:
    /* class type pointer querys. */
    virtual ShipItem* GetShipItem()                     { return this; }
    /* class type tests. */
    virtual bool IsShipItem()                           { return true; }

    void Init();
    void InitPod();
    void InitAttribs();
    void LogOut();
    // adds cargo mass to ship's mass
    void UpdateMass();  // will reset ship mass to default, then loop thru cargo
    // tells destiny about a mass change (cargo mass, server.CargoMassAdditive)
    void UpdateDestinyMass();
    static ShipItemRef Load( uint32 shipID);
    static ShipItemRef Spawn( ItemData &data);

    virtual void SetPlayer(Client* pClient);
    virtual bool HasPilot()                             { return (m_pilot != nullptr); }
    virtual Client* GetPilot()                          { return m_pilot; }

    // add item to our inventory and set singleton where applicable
    virtual void AddItem(InventoryItemRef iRef);
    // remove item from our inventory and call Remove() on module/charge
    virtual void RemoveItem(InventoryItemRef iRef);

    bool HasModuleManager()                             { return (m_ModuleManager != nullptr); }
    ModuleManager* GetModuleManager()                   { return m_ModuleManager; }

    virtual void Delete();

    float GetRemainingVolumeByFlag(EVEItemFlags flag) const;
    // this checks destination flag vs item type/group/category for proper placement
    void VerifyHoldType(EVEItemFlags flag, InventoryItemRef iRef, Client* pClient=nullptr);     // this will throw on error.
    bool ValidateItemSpecifics(InventoryItemRef iRef);

    bool IsPopped()                                     { return m_isPopped; }
    void SetPopped(bool set=false)                      { m_isPopped = set; }
    std::string GetShipDNA();

    /*
     * Primary public packet builders:
     */
    PyDict* GetShipInfo();
    PyDict* GetShipState();
    PyList* ShipGetModuleList();
    PyDict* GetChargeState();

    bool ValidateBoardShip(CharacterRef who);
    void SaveShip();
    void RepairShip(float fraction);

    /* Inform Ship that a state change is taking place  */
    void Dock();
    void Heal();
    void Jump();
    void Warp();
    void Eject();
    void Undock();      // reset effects, online mods in list, recharge shield and cap (if session change complete)
    void AddModuleToOnlineVec(uint32 modID);

    /* begin new module manager interface */
    void ProcessModules();
    //this can throw
    void Online(uint32 modID);
    //this can throw
    void Offline(uint32 modID);
    void OnlineAll();
    void OfflineAll();
    void Activate(int32 itemID, std::string effectName, int32 targetID, int32 repeat);
    void Deactivate(int32 itemID, std::string effectName) { m_ModuleManager->Deactivate(itemID, effectName); }
    bool Overload(uint32 itemID, bool set=true)         { return m_ModuleManager->Overload(itemID, set); }
    void CancelOverloading(uint32 itemID, bool stop=false) { m_ModuleManager->CancelOverloading(itemID, stop); }
    void OverloadRack(uint32 itemID, bool set, std::vector<uint32>& changed) { m_ModuleManager->OverloadRack(itemID, set, changed); }
    void ReplaceCharges(EVEItemFlags flag, InventoryItemRef newCharge);
    void RemoveRig(InventoryItemRef iRef);
    void UpdateModules();       // called when undocking to online modules in OnlineModuleList sent from client
    void UpdateModules(EVEItemFlags flag);
    void UnloadModule(uint32 itemID)                    { m_ModuleManager->UnloadModule(itemID); }
    void UnloadModule(GenericModule* pMod)              { m_ModuleManager->UnloadModule(pMod); }
    void UnloadAllModules()                             { m_ModuleManager->UnloadAllModules();  }
    // this will test for active modules and throw where applicable
    void MoveModuleSlot(EVEItemFlags slot1, EVEItemFlags slot2);
    // the client's strip fitting leaves rigs and subsystems (they can't be unfitted).  all = repackaging, which takes them too
    void StripFitting(bool all=false);
    // this will remove all items from all ship cargo holds
    void EmptyCargo();
    // after a subsystem change: modules past the ship's slots or hardpoints go to the hangar (cargo in space)
    void EvictExcessModules();
    // after a pilot change: shield/capacitor charge and armor/structure damage are cut to the new capacities
    void ClampDamageState();

    // loop thru modules that 'get' things and deactivate them
    void CargoFull();

    void RepairModules(std::vector<InventoryItemRef>& itemRefVec, float fraction);

    void RepairModules()                                { m_ModuleManager->RepairModules(); }

    void AbortCycle()                                   { m_ModuleManager->AbortCycle(); }
    bool IsActive()                                     { return m_isActive; }
    bool IsDocking()                                    { return m_isDocking; }
    bool IsUndocking()                                  { return m_isUndocking; }
    void SetDocked()                                    { m_isDocking = false; }
    void SetUndocking(bool set=false)                   { m_isUndocking = set; }
    InventoryItemRef GetTargetRef()                     { return m_targetRef; }
    void ClearTargetRef()                               { m_targetRef = InventoryItemRef(); }
    void SetTargetRef(InventoryItemRef ref)             { m_targetRef = ref; }
    // this is for repairing modules with nanite paste
    PyRep* ModuleRepair(uint32 modID)                   { return m_ModuleManager->ModuleRepair(modID); }
    void StopModuleRepair(uint32 modID)                 { m_ModuleManager->StopModuleRepair(modID); }

    /* rack heat.  racks are 0=high, 1=medium, 2=low (-1 for any other slot).
     *  a rack heats up while it has overloaded modules cycling, and cools down otherwise.  heat isn't ticked:
     *  it is worked out from the last change with the same formulas the client uses to animate the HUD gauges.
     */
    static int8 GetHeatRack(EVEItemFlags flag);
    // a module started (or stopped) an overloaded cycle.  updates its rack's heating rate and tells the client
    void SetModuleHeating(GenericModule* pMod, bool heating);
    // current heat of a rack, 0 to its heat capacity
    double GetRackHeat(int8 rack);
    // GM: sets a rack's heat (0 to its heat capacity).  it keeps heating or cooling from there
    void SetRackHeat(int8 rack, double heat);
    // brings the heat attributes up to date, for ship info sent to the client
    void UpdateHeatAttributes();

    /* doomsday lockouts (filetime the lockout ends).  kept on the ship item, so a new ShipSE (relog) keeps them */
    void SetWarpLockout(int64 until)                    { m_warpLockUntil = until; }
    void SetJumpLockout(int64 until)                    { m_jumpLockUntil = until; }
    // remaining lockout in filetime units, 0 when over
    int64 WarpLockoutRemaining();
    int64 JumpLockoutRemaining();
    // docking: stops all rack heating
    void StopHeating();
    // after one of pMod's overloaded cycles, pMod and its rack may take heat damage
    void HeatDamageCheck(GenericModule* pMod);
    void DamageModule(uint32 modID, float amt=1)        { m_ModuleManager->DamageModule(modID, amt); }
    // pMod's damage reached its hp.  offlines it (it can't be onlined until repaired)
    void BurnOut(GenericModule* pMod);
    void DamageRandModule()                             { m_ModuleManager->DamageRandModule(); }

    // get list of fitted modules
    void GetModuleRefVec(std::vector<InventoryItemRef>& iRefVec); // for active ships
    // get list of fitted modules
    void GetModuleItemVec(std::vector<InventoryItemRef>& iRefVec); // for inactive ships
    GenericModule* GetModule(EVEItemFlags flag)         { return m_ModuleManager->GetModule(flag); }
    InventoryItemRef GetModuleRef(EVEItemFlags flag);
    InventoryItemRef GetModuleRef(uint32 modID);

    // this also checks for fit-by-type and rig restrictions
    // swap: a subsystem replacing the fitted one in its slot
    void TryModuleLimitChecks(EVEItemFlags flag, InventoryItemRef iRef, bool swap=false);     // this must throw on error
    // T3 subsystem fitting rules (hull, slot).  throws the client's errors
    void CheckSubSystemFit(EVEItemFlags flag, InventoryItemRef iRef);
    EVEItemFlags FindAvailableModuleSlot( InventoryItemRef iRef );
    // make sure this does NOT throw.
    // must return integer
    // will remove item from previous container
    uint32 AddItemByFlag( EVEItemFlags flag, InventoryItemRef iRef, Client* pClient=nullptr);
    // this can throw. returns nothing
    void LoadCharge(InventoryItemRef cRef, EVEItemFlags flag);
    // this can throw. returns nothing
    void LoadChargesToBank(EVEItemFlags flag, std::vector<int32>& chargeIDs);
    // called by client for subLocation (charge) removal
    void RemoveCharge(EVEItemFlags fromFlag);
    /* end new module manager interface */

    // Tactical Interface:
    void SetShipShield(float fraction);
    void SetShipArmor(float fraction);
    void SetShipHull(float fraction);
    void SetShipCapacitorLevel(float fraction);
    float GetShipHullHP()                               { return GetAttribute(AttrHP).get_float(); }
    float GetShipArmorHP()                              { return GetAttribute(AttrArmorHP).get_float(); }
    float GetShipPGLevel()                              { return GetAttribute(AttrPowerOutput).get_float(); }
    float GetShipCPULevel()                             { return GetAttribute(AttrCpuOutput).get_float(); }
    float GetShipShieldHP()                             { return GetAttribute(AttrShieldCharge).get_float(); }
    float GetShipCapacitorLevel()                       { return GetAttribute(AttrCapacitorCharge).get_float(); }
    EvilNumber GetShipHullPercent()                     { return 1 -(GetAttribute(AttrDamage) / GetAttribute(AttrHP)); }
    EvilNumber GetShipCPUPercent()                      { return 1 -(GetAttribute(AttrCpuLoad) / GetAttribute(AttrCpuOutput)); }
    EvilNumber GetShipPGPercent()                       { return 1 -(GetAttribute(AttrPowerLoad) / GetAttribute(AttrPowerOutput)); }
    EvilNumber GetShipArmorPercent()                    { return 1 -(GetAttribute(AttrArmorDamage) / GetAttribute(AttrArmorHP)); }
    EvilNumber GetShipShieldPercent()                   { return (GetAttribute(AttrShieldCharge) / GetAttribute(AttrShieldCapacity)); }
    EvilNumber GetShipCapacitorPercent()                { return (GetAttribute(AttrCapacitorCharge) / GetAttribute(AttrCapacitorCapacity)); }

    // template loading system
    using InventoryItem::_Load;
    virtual bool _Load();
protected:
    // Template loader:
    template<class _Ty>
    static RefPtr<_Ty> _LoadItem( uint32 shipID, const ItemType &type, const ItemData &data) {
        if (type.categoryID() == EVEDB::invCategories::Ship)
            return ShipItemRef( new ShipItem(shipID, type, data ));

        _log(ITEM__ERROR, "Trying to load %s as Ship.", sDataMgr.GetCategoryName(type.categoryID()));
        if (sConfig.debug.StackTrace)
            EvE::traceStack();
        return RefPtr<_Ty>(nullptr);
    }

    //bool LoadAttributes();
    bool m_loaded;

    static uint32 CreateItemID( ItemData &data);

public:
    /* new effects system */
    // only called by ResetEffects
    void ClearModuleModifiers();        // this calls OfflineAll() then ClearModifiers() on all modules and their charges
    // only called by Undock
    void ResetEffects();                // this clears all modifiers, resets all attributes, then calls ProcessEffects()
    // called by AggressionManager::GetCriminalTimeStamps
    void PrepForUndock();               // called by AggresMgr to try saving a bit of runtime by clearing beforehand
    void ProcessEffects(bool add=false, bool update=false);
    void CharacterBoardingShip()                        { m_ModuleManager->CharacterBoardingShip(); }

    /* linking weapons methods */
    // number of weapons in pMod's bank (1 if not banked)
    uint8 GetLinkedCount(GenericModule* pMod);
    // number of online, loaded weapons in pMod's bank (1 if not banked)
    uint8 GetLoadedLinkedCount(GenericModule* pMod);
    bool HasLinkedWeapons()                             { return (!m_linkedWeapons.empty()); }
    // master of the bank pMod is in (pMod itself if it is the master), nullptr if not banked
    GenericModule* GetBankMaster(GenericModule* pMod);
    // a bank with an offline member can't be activated
    bool BankHasOfflineMember(GenericModule* pMod);
    /* dogma calls.  these throw the client's CantLink* / CantUngroup* errors */
    void LinkAllWeapons();
    void LinkWeapon(uint32 masterID, uint32 slaveID);
    void PeelAndLink(uint32 masterID, uint32 slaveID);
    void MergeModuleGroups(uint32 masterID, uint32 slaveID);
    uint32 UnlinkWeapon(uint32 moduleID);           // returns the slave taken out of moduleID's bank
    // dissolves the bank memberID is in.  force skips the active/loading checks (unfitting)
    void UnlinkGroup(uint32 memberID, bool update=false, bool force=false);
    void UnlinkAllWeapons(bool force=false);
    PyRep* GetLinkedWeapons();
    // to load with ammo
    void LoadLinkedWeapons(GenericModule* pMod, std::vector<int32>& chargeIDs);
    void GetLinkedWeaponMods(EVEItemFlags flag, std::vector<GenericModule*> &modules);

protected:
    /* linking weapons methods */
    // adds pSlave to pMaster's bank.  no checks
    void LinkWeapon(GenericModule* pMaster, GenericModule* pSlave);
    void UnlinkWeapon(uint32 masterID, uint32 slaveID);
    // hi slot turret or launcher of a groupable group
    bool IsGroupable(GenericModule* pMod);
    // pMod's bank members, master first.  just pMod when not banked
    void GetBankMembers(GenericModule* pMod, std::vector<GenericModule*>& modules);
    // these throw
    void CheckLinkable(GenericModule* pMod);
    void CheckLinkable(GenericModule* pMod1, GenericModule* pMod2);
    void CheckUnlinkable(GenericModule* pMod);
    // checks the modules joining a bank and the bank, then unloads them (server.UnloadOnLinkAll)
    void PrepareLink(std::vector<GenericModule*>& joining, std::vector<GenericModule*>& bank);
    // takes one module out of its bank.  a bank left with one module is dissolved
    void RemoveFromBank(GenericModule* pMod);
    // OnWeaponBanksChanged to the pilot
    void SendWeaponBanks();
    // to load saved linked weapons
    void LoadWeaponGroups();
    // to save linked weapons
    void SaveWeaponGroups();

private:
    Client* m_pilot;

    int64 m_warpLockUntil;              // no warp until (30s after firing the doomsday)
    int64 m_jumpLockUntil;              // no jump drive, jump portal or cloak until (10min after firing the doomsday)

    //the ship's module manager.  We own this
    ModuleManager* m_ModuleManager;

    InventoryItemRef m_targetRef;       // this is only used for module effects that require a target.  is here because of the ease of aquiring/sending (common code)

    std::vector<uint32> m_onlineModuleVec;      // for onlining modules when undocking
    std::map<GenericModule*, std::list<GenericModule*>> m_linkedWeapons; // masterID/data (slaveIDs)

    struct RackHeat {
        bool init = false;
        double heat = 0.0;          // heat when last changed
        int64 time = 0;             // when it last changed (filetime)
        double rate = 0.0;          // heating rate (sum of heatAbsorbtionRateModifier of the heating modules)
        std::set<uint32> modules;   // modules cycling overloaded
    };
    RackHeat m_rackHeat[3];
    RackHeat& GetRackState(int8 rack);
    double RackHeatAt(int8 rack, int64 time);
    void SyncRackHeat(int8 rack);
    void SendHeatChange(uint16 attrID, double value, int64 time);

    void ProcessShipEffects(bool update=false);

    bool m_isActive :1;
    bool m_isPopped :1;
    bool m_isDocking :1;
    bool m_isUndocking :1;
};

/**
 * DynamicSystemEntity which represents ship object in space
 */
class EVEServiceManager;
class InventoryItem;
class DestinyManager;
class Missile;
class SystemManager;
class ServiceDB;

// warfare link multipliers on a ship: (itemID of the ship or a fitted module, attributeID) -> multiplier
typedef std::map<std::pair<uint32, uint16>, float> LinkMap;

class ShipSE
: public DynamicSystemEntity
{
public:
    ShipSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* pSystem, const FactionData& data);
    virtual ~ShipSE()                                   { /* do nothing here */ }

    /* class type pointer querys. */
    virtual ShipSE* GetShipSE()                         { return this; }
    /* class type tests. */
    virtual bool IsShipSE()                             { return true; }

    /* SystemEntity interface */
    virtual void Process();
    virtual void EncodeDestiny( Buffer& into );
    virtual void MakeDamageState(DoDestinyDamageState &into);
    virtual PyDict *MakeSlimItem();

    /* virtual functions default to base class and overridden as needed */
    virtual void Killed(Damage &fatal_blow);            /* This method is defined in Damage.cpp */
    // a ship left in space by a pilot who logged off was destroyed (Damage.cpp)
    void KilledLoggedOff(Damage& fatal_blow, SystemEntity* killer, uint32 killerID, Client* pKillerClient, uint32 ownerID,
                         uint32 wreckTypeID, const std::string& wreck_name, const GPoint& wreckPosition);
    // a destroyed strategic cruiser costs its pilot a subsystem skill level (Damage.cpp).  pClient may be null (logged off)
    void T3SkillLoss(Character* pChar, Client* pClient, Damage& fatal_blow);

    /* virtual functions to be overridden in derived classes */
    virtual void MissileLaunched(Missile* pMissile)     { /* Do nothing here */ }
    virtual bool IsInvul();
    virtual bool IsFrozen()                             { return false; }
    virtual bool IsLogin();

    /* virtual functions in base to allow common interface calls specific to ship entities */
    virtual void SetPilot(Client* pClient);
    // the pilot's corp, alliance or militia changed:  the cached IDs and everyone's slim item follow
    void RefreshOwnerIDs();
    virtual bool HasPilot()                             { return ((m_shipRef.get() == nullptr) ? false : m_shipRef->HasPilot()); }
    virtual Client* GetPilot()                          { return ((m_shipRef.get() == nullptr) ? nullptr : m_shipRef->GetPilot()); }

    /* specific functions handled here. */
    void Dock();
    void Jump(bool showCloak=true);        // this sets ship cloak
    void Warp();
    void RemoveTarget(SystemEntity* pSE);
    // stops an active cloaking device at once (proximity, smart bombs, police).  msgKey is sent to the pilot.  gm and session cloaks are left alone
    void Decloak(const char* msgKey = nullptr);
    // starbase force field password:  the ship's harmonic follows it (sent to the bubble)
    void SetPassword(std::string pass);
    // the starbase force field password this ship entered
    const std::string& GetPassword()                    { return m_towerPass; }
    // fleet
    void ClearBoostData();
    bool IsBoosted()                                    { return m_boosted; }
    void SetBoost(bool set=false)                       { m_boosted = set; }
    void RemoveBoost();
    void ApplyBoost(BoostData& bData);
    uint8 GetMiningBoostAmount()                        { return m_boost.mining; }
    /* warfare links.  wanted is everything the ship should have now.  old multipliers are divided out, new ones multiplied in */
    void ApplyLinkBonuses(const LinkMap& wanted);
    void RemoveLinkBonuses()                            { ApplyLinkBonuses(LinkMap()); }

    // misc
    void PayInsurance();
    void DamageModule(uint32 modID)                     { m_shipRef->DamageModule(modID); }
    void DamageRandModule(float chance);
    void ResetShipSystemMgr(SystemManager* pSystem);    // this is to reset system manager for jumps, etc.
    void SaveShip()                                     { m_shipRef->SaveShip(); }

    //cancel all active modules
    void AbortCycle()                                   { m_shipRef->AbortCycle(); }

    // this is the itemID of the ship that ejected the pod, for setting in slimItemUpdate (launcherID)
    void SetLauncherID(uint32 shipID)                   { m_podShipID = shipID; }
    uint32 GetLauncherID()                              { return m_podShipID; }

    ShipItemRef GetShipItemRef()                        { return m_shipRef; }

    float CalculateRechargeRate(float Capacity, float RechargeTimeMS, float Current);

    DroneSE* LaunchDrone(InventoryItemRef dRef);

    bool GetFleetSMBUsage()                             { return m_allowFleetSMBUsage; }

    /* doomsday lockouts (see ShipItem).  remaining time in filetime units, 0 when over */
    void SetWarpLockout(int64 until)                    { m_shipRef->SetWarpLockout(until); }
    void SetJumpLockout(int64 until)                    { m_shipRef->SetJumpLockout(until); }
    int64 WarpLockoutRemaining()                        { return m_shipRef->WarpLockoutRemaining(); }
    int64 JumpLockoutRemaining()                        { return m_shipRef->JumpLockoutRemaining(); }
    void SetFleetSMBUsage(bool set=false)               { m_allowFleetSMBUsage = set; }

protected:
    ShipItemRef m_shipRef;

    const uint32 m_processTimerTick;

private:
    ShipDB m_db;

    Timer m_processTimer;
    Timer m_cloakCheckTimer;            // proximity decloak, once a second

    uint32 m_podShipID;

    bool m_allowFleetSMBUsage;

    /*  boost data */
    BoostData m_boost = BoostData();
    bool m_boosted;
    uint16 m_oldArmor;
    uint16 m_oldShield;
    uint16 m_oldScanRes;
    uint32 m_oldTargetRange;
    float m_oldInertia;

    /*  for POS field */
    std::string m_towerPass;

    /* warfare link multipliers applied to this ship and its modules */
    LinkMap m_linkMults;

};

#endif /* !__SHIP__H__INCL__ */
