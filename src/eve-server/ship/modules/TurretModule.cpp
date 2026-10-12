
 /**
  * @name TurretModule.cpp
  *   turret module class
  * @Author:         Allan
  * @date:   10 June 2015   -UD/RW 02 April 2017
  */


#include "eve-server.h"

#include "StaticDataMgr.h"
#include "EVE_Charges.h"
#include "StatisticMgr.h"
#include "ship/modules/ModuleManager.h"
#include "ship/modules/TurretModule.h"
#include "system/Damage.h"

TurretModule::TurretModule(ModuleItemRef mRef, ShipItemRef sRef)
 : ActiveModule(mRef, sRef)
{
    /* this may lead to some weird shit, but civilian turrets dont use charges.
     *  these turrets have the damage attribs set in turret item, so set chargeRef to turret item for damage calcs
     */
    if (!m_needsCharge)
        m_chargeRef = mRef;
}

void TurretModule::LoadCharge(InventoryItemRef cRef, bool instant/*false*/)
{
    _log(MODULE__TRACE, "%s calling TM::LoadCharge()", m_modRef->name());

    ActiveModule::LoadCharge(cRef, instant);
}

void TurretModule::UnloadCharge()
{
    _log(MODULE__TRACE, "%s calling TM::UnloadCharge()", m_modRef->name());

    ActiveModule::UnloadCharge();
}

bool TurretModule::IsCrystal(uint16 groupID)
{
    switch (groupID) {
        case EVEDB::invGroups::Frequency_Crystal:
        case EVEDB::invGroups::Advanced_Beam_Laser_Crystal:
        case EVEDB::invGroups::Advanced_Pulse_Laser_Crystal:
            return true;
    }
    return false;
}

void TurretModule::DamageCrystal(GenericModule* pMod)
{
    /* Crucible:  crystals are not used up.  the ones that get damaged (crystalsGetDamaged on the crystal:  tech 2 and faction,
     *  never tech 1) take crystalVolatilityDamage with crystalVolatilityChance each shot, and are destroyed at their hp.
     *  a damaged crystal no longer stacks (singleton), and keeps its damage when unloaded
     */
    if ((pMod == nullptr) or !pMod->isOnline() or !pMod->IsLoaded())
        return;
    InventoryItemRef cRef(pMod->GetLoadedChargeRef());
    if ((cRef.get() == nullptr) or (cRef.get() == pMod->GetSelf().get()) or !IsCrystal(cRef->groupID()))
        return;
    if (!cRef->GetAttribute(AttrCrystalsGetDamaged).get_bool())
        return;
    if (MakeRandomFloat(0, 1) >= cRef->GetAttribute(AttrCrystalVolatilityChance).get_float())
        return;

    double damage(cRef->GetAttribute(AttrDamage).get_double() + cRef->GetAttribute(AttrCrystalVolatilityDamage).get_double());
    if (!cRef->isSingleton())
        cRef->ChangeSingleton(true, false);
    if (!Charges::CrystalBurnsOut(damage, cRef->GetAttribute(AttrHP).get_double())) {
        cRef->SetAttribute(AttrDamage, damage);
        return;
    }

    // the module deactivates (at the end of its cycle) and the crystal is gone
    Client* pClient(m_shipRef->GetPilot());
    if (pClient != nullptr) {
        PyDict* args = new PyDict();
            args->SetItemString("module", new PyInt(pMod->typeID()));
            args->SetItemString("type", new PyInt(cRef->typeID()));
        pClient->SendRemoteMessage("CrystalDestroyed", args);
    }
    m_shipRef->GetModuleManager()->DestroyCharge(pMod);
}

void TurretModule::ApplyDamage()
{
    if (m_chargeRef.get() == nullptr) {
        // catchall
        m_shipRef->GetPilot()->SendErrorMsg("Your %s in %s doesnt have a charge registered.<br>You can try Unload/Reload, but if this error happens again, dock or relog.", \
                m_modRef->name(), sDataMgr.GetFlagName(m_modRef->flag()));
        _log(MODULE__ERROR, "TurretModule::ApplyDamage() - module %s(%u) does not have a m_chargeRef.",  m_modRef->name(), m_modRef->itemID());
        Deactivate();
        m_Stop = true;
        return;
    }
    if (m_targetSE == nullptr) {
        m_Stop = true;
        return;
    }
    // add data to StatisticMgr
    sStatMgr.Increment(Stat::pcShots);

    /* grouped weapons each roll to hit (dev blog "Weapon Grouping").  only online, loaded weapons fire.
     *  bank members are the same type with the same attributes, so each roll uses this module's numbers.
     *  the damage is the sum of the rolls, sent as one hit whose quality (damage message) is the average roll
     */
    uint8 shots(1);
    if (m_linkMaster)
        shots = m_shipRef->GetLoadedLinkedCount(this);
    if (shots < 1)
        shots = 1;
    float toHit(0.0f);
    for (uint8 i = 0; i < shots; ++i)
        toHit += m_formula.GetToHit(m_shipRef, this, m_targetSE);
    toHit /= shots;

    Damage d(m_shipRef->GetPilot()->GetShipSE(),
            m_modRef,
            m_chargeRef->GetAttribute(AttrKineticDamage).get_float(),
            m_chargeRef->GetAttribute(AttrThermalDamage).get_float(),
            m_chargeRef->GetAttribute(AttrEmDamage).get_float(),
            m_chargeRef->GetAttribute(AttrExplosiveDamage).get_float(),
            toHit,
            m_effectID
    );

    d *= GetAttribute(AttrDamageMultiplier).get_float();
    d *= sConfig.rates.turretDamage;
    d *= shots;

    m_targetSE->ApplyDamage(d);

    /* projectile ammo and hybrid charges are used up, one per shot of each weapon.  crystals (lasers, and the civilian
     *  turrets' everlasting ammo, which are crystals too) are not, but some get damaged.  this goes by the charge, not the turret
     */
    if (m_chargeRef.get() == m_modRef.get())
        return;     // civilian turret without a charge
    if (!IsCrystal(m_chargeRef->groupID())) {
        ConsumeCharge();
        return;
    }

    std::vector<GenericModule*> modules;
    if (m_linkMaster)
        m_shipRef->GetLinkedWeaponMods(m_modRef->flag(), modules);
    if (modules.empty())
        modules.push_back(this);
    for (auto cur : modules)
        DamageCrystal(cur);
}
