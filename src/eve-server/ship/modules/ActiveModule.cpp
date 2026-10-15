 /**
  * @name ActiveModule.cpp
  *   active module class
  * @Author:         Allan
  * @date:   10 June 2015   -UD/RW 02 April 2017
  */


#include "eve-server.h"

#include <cmath>
#include <initializer_list>

#include "EVE_Charges.h"
#include "EVE_Propulsion.h"
#include "StatisticMgr.h"
#include "exploration/Probes.h"
#include "exploration/Scan.h"
#include "inventory/AttributeEnum.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/ambient/AmbientNPC.h"
#include "npc/Sentry.h"
#include "npc/SentryAI.h"
#include "ship/Missile.h"
#include "ship/modules/ActiveModule.h"
#include "ship/modules/EwarModule.h"
#include "ship/modules/ModuleItem.h"
#include "ship/modules/Prospector.h"
#include "manufacturing/Blueprint.h"
#include "pos/PosMgrDB.h"
#include "exploration/Exploration.h"
#include "system/Container.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/CrimeWatch.h"
#include "system/Interdiction.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "effects/EffectsDataMgr.h"

namespace {
    /* npc ai (level one, PLAN.md 5.7):  the hp of the layer a remote repair module works on (-1:  not one).  the difference
     *  over a cycle is what it repaired */
    double RemoteAssistHP(uint16 groupID, SystemEntity* pTarget)
    {
        if (pTarget == nullptr)
            return -1.0;
        InventoryItemRef iRef(pTarget->GetSelf());
        switch (groupID) {
            case EVEDB::invGroups::Shield_Transporter:
                return iRef->GetAttribute(AttrShieldCharge).get_double();
            case EVEDB::invGroups::Armor_Repair_Projector:
                return iRef->GetAttribute(AttrArmorHP).get_double() - iRef->GetAttribute(AttrArmorDamage).get_double();
            case EVEDB::invGroups::Remote_Hull_Repairer:
                return iRef->GetAttribute(AttrHP).get_double() - iRef->GetAttribute(AttrDamage).get_double();
        }
        return -1.0;
    }
}

ActiveModule::ActiveModule(ModuleItemRef mRef, ShipItemRef sRef)
: GenericModule(mRef, sRef),
m_timer(0, true),
m_reloadTimer(0),
m_shipSE(nullptr),
m_sysMgr(nullptr),
m_targMgr(nullptr),
m_targetSE(nullptr),
m_destinyMgr(nullptr),
m_usesCharge(false),
m_needsCharge(false),
m_needsTarget(false),
m_targetID(0),
m_reactivateAt(0),
m_effectID(0),
m_fieldActive(false),
m_Stop(true),
m_heating(false)
{
    m_repeat = 1000;    //based on client data

    m_needsCharge = mRef->HasAttribute(AttrChargeGroup1);
    if (m_needsCharge) {
        switch (mRef->groupID()) {
            // civilian turrets dont use charges.  this is checked in TurretModule()
            // these can use scripts as charge.  they can activate without scripts using 'default' values
            case EVEDB::invGroups::Remote_Sensor_Damper:
            case EVEDB::invGroups::Tracking_Link:
            case EVEDB::invGroups::Signal_Amplifier:
            case EVEDB::invGroups::Tracking_Enhancer:
            case EVEDB::invGroups::Sensor_Booster:
            case EVEDB::invGroups::Tracking_Computer:
            case EVEDB::invGroups::Projected_ECCM:
            case EVEDB::invGroups::Remote_Sensor_Booster:
            case EVEDB::invGroups::Tracking_Disruptor:
            case EVEDB::invGroups::Frequency_Mining_Laser:
                // this may need its' own class as it has different effects depending on usage
            case EVEDB::invGroups::Warp_Disrupt_Field_Generator: {
                m_usesCharge = true;
                m_needsCharge = false;
            } break;
        }
    }

    // this is an internal variable only.
    /* Crucible:  only hybrid turrets have reloadTime (5s).  everything else that loads charges (projectiles, crystals,
     *  launchers, probes, cap booster charges, scripts) takes the attribute's dogma default of 10s
     */
    m_reloadTime = 0;
    if (m_needsCharge or m_usesCharge)
        m_reloadTime = Charges::ReloadTimeMS(GetAttribute(AttrReloadTime).get_double());

    if (!m_shipRef->HasPilot())
        return;

    //Clear();
    //GM_Modules = 353,

    if (is_log_enabled(MODULE__TRACE)) {
        if (m_reloadTime or m_usesCharge) {
            _log(MODULE__TRACE, "Reload time for %s(%u) set to %ums. (uses charge: %s)", \
                mRef->name(), mRef->itemID(), m_reloadTime, m_usesCharge?"true":"false");
        } else {
            _log(MODULE__TRACE, "%s(%u) does not use reload time.", mRef->name(), mRef->itemID());
        }
    }

    // these groups receive a 3% increase in scan range
    switch (mRef->groupID()) {
        case EVEDB::invGroups::Ship_Scanner: {
            float range = GetAttribute(AttrShipScanRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrShipScanRange, range);
        } break;
        case EVEDB::invGroups::Cargo_Scanner: {
            float range = GetAttribute(AttrCargoScanRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrCargoScanRange, range);
        } break;
        case EVEDB::invGroups::Survey_Scanner: {
            float range = GetAttribute(AttrSurveyScanRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrSurveyScanRange, range);
        } break;
        case EVEDB::invGroups::Shield_Transporter: {
            float range = GetAttribute(AttrShieldTransferRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrShieldTransferRange, range);
        } break;
        case EVEDB::invGroups::Energy_Vampire:
        case EVEDB::invGroups::Energy_Transfer_Array: {
            float range = GetAttribute(AttrPowerTransferRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrPowerTransferRange, range);
        } break;
        case EVEDB::invGroups::Energy_Destabilizer: {
            float range = GetAttribute(AttrEnergyDestabilizationRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrEnergyDestabilizationRange, range);
        } break;
        case EVEDB::invGroups::Salvager:
        case EVEDB::invGroups::Target_Painter:
        case EVEDB::invGroups::Tracking_Disruptor:
        case EVEDB::invGroups::Gas_Cloud_Harvester:
        case EVEDB::invGroups::Remote_Sensor_Damper:
        case EVEDB::invGroups::Remote_Sensor_Booster:
        case EVEDB::invGroups::Armor_Repair_Projector: {
            float range = GetAttribute(AttrMaxRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrMaxRange, range);
        } break;
        /*  these are 50AU.  we dont need to increase it...
        case EVEDB::invGroups::System_Scanner:  {
            float range = GetAttribute(AttrScanRange).get_float();        // range in AU
            range *= (1 + (0.03 * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrScanRange, range);
        } break;
        */
    }
}

void ActiveModule::Update()
{
    if (!m_shipRef->HasPilot())
        return;

    // these groups receive a 3% increase in scan range
    switch (m_modRef->groupID()) {
        case EVEDB::invGroups::Ship_Scanner: {
            ResetAttribute(AttrShipScanRange);
            float range = GetAttribute(AttrShipScanRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrShipScanRange, range);
        } break;
        case EVEDB::invGroups::Cargo_Scanner: {
            ResetAttribute(AttrCargoScanRange);
            float range = GetAttribute(AttrCargoScanRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrCargoScanRange, range);
        } break;
        case EVEDB::invGroups::Survey_Scanner: {
            ResetAttribute(AttrSurveyScanRange);
            float range = GetAttribute(AttrSurveyScanRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrSurveyScanRange, range);
        } break;
        case EVEDB::invGroups::Shield_Transporter: {
            ResetAttribute(AttrShieldTransferRange);
            float range = GetAttribute(AttrShieldTransferRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrShieldTransferRange, range);
        } break;
        case EVEDB::invGroups::Energy_Vampire:
        case EVEDB::invGroups::Energy_Transfer_Array: {
            ResetAttribute(AttrPowerTransferRange);
            float range = GetAttribute(AttrPowerTransferRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrPowerTransferRange, range);
        } break;
        case EVEDB::invGroups::Energy_Destabilizer: {
            ResetAttribute(AttrEnergyDestabilizationRange);
            float range = GetAttribute(AttrEnergyDestabilizationRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrEnergyDestabilizationRange, range);
        } break;
        case EVEDB::invGroups::Salvager:
        case EVEDB::invGroups::Target_Painter:
        case EVEDB::invGroups::Tracking_Disruptor:
        case EVEDB::invGroups::Gas_Cloud_Harvester:
        case EVEDB::invGroups::Remote_Sensor_Damper:
        case EVEDB::invGroups::Remote_Sensor_Booster:
        case EVEDB::invGroups::Armor_Repair_Projector: {
            ResetAttribute(AttrMaxRange);
            float range = GetAttribute(AttrMaxRange).get_float();
            range *= (1 + (0.03f * (m_shipRef->GetPilot()->GetChar()->GetSkillLevel(EvESkill::LongRangeTargeting, true))));
            SetAttribute(AttrMaxRange, range);
        } break;
    }
}

void ActiveModule::Clear()
{
    _log(MODULE__TRACE, "%s(%u) calling Clear()", m_modRef->name(), m_modRef->itemID());
    UpdateCycleHeat(false);
    if (m_targetSE != nullptr)
        if (m_targetSE->TargetMgr() != nullptr)
            m_targetSE->TargetMgr()->RemoveTargetModule(this);

    m_targetSE = nullptr;

    m_Stop = true;
    m_repeat = 1;
    m_targetID = 0;
    m_effectID = 0;
    m_shipSE = nullptr;
    m_sysMgr = nullptr;
    m_targMgr = nullptr;
    m_targetSE = nullptr;
    m_destinyMgr = nullptr;
    m_needsTarget = false;

    m_timer.Disable();

    m_shipRef->ClearTargetRef();

    // an activation that ran (not one refused before starting) starts the reactivation delay
    if (m_ModuleState >= Module::State::Deactivating) {
        int64 delay(GetAttribute(AttrModuleReactivationDelay).get_int());
        if (delay > 0)
            m_reactivateAt = GetFileTimeNow() + (delay * (EvE::Time::Second / 1000));
    }

    SetModuleState(Module::State::Online);
}

void ActiveModule::CheckActivationRules()
{
    // moduleReactivationDelay (cloaks, ecm bursts, remote ecm bursts...)
    int64 now(GetFileTimeNow());
    if (m_reactivateAt > now)
        throw UserError("ModuleReactivationDelayed2")
                .AddTypeName("item", typeID())
                .AddFormatValue("delay", new PyLong(m_reactivateAt - now));

    // maxGroupActive (warfare links, cloaks...)
    if (HasAttribute(AttrMaxGroupActive)) {
        uint8 count(m_shipRef->GetModuleManager()->GetActiveCountByGroup(groupID(), this));
        if (count >= GetAttribute(AttrMaxGroupActive).get_uint32())
            throw UserError("EffectCrowdedOut")
                    .AddFormatValue("module", new PyInt(typeID()))
                    .AddFormatValue("count", new PyInt(count));
    }
}

void ActiveModule::CheckTargetRules(uint16 effectID)
{
    InventoryItemRef targRef(m_targetSE->GetSelf());
    if (sFxDataMgr.isAssistance(effectID))
        if (targRef->GetAttribute(AttrDisallowAssistance).get_bool()) {
            Clear();
            throw UserError ("DeniedActivateTargetAssistDisallowed");
        }
    // ewar immune targets (supercapitals, sieged/triaged ships)
    if (Ewar::IsOffensiveModifier(groupID()))
        if (targRef->GetAttribute(AttrDisallowOffensiveModifiers).get_bool()) {
            Clear();
            throw UserError ("DeniedActivateTargetOffModDisallowed");
        }
}

void ActiveModule::Process()
{
    // the order of Reload/Unload is significant.
    if (m_reloadTimer.Enabled())
        if (m_reloadTimer.Check(false))
            FinishLoading();

    if (m_ModuleState < Module::State::Deactivating)
        return;

    // if chargestate is loading or reloading, deny further processing and let m_reloadTimer handle it.
    if (m_ChargeState > Module::State::Loaded) {
        _log(MODULE__TRACE, "ActiveModule::Process - %s on %s is loading.", m_modRef->name(), m_shipRef->name());
        return;
    }

    // decrement repeat and check for single activation.
    if (m_repeat < 999) // do not decrement as default allowing active modules to stay active for extended periods
        --m_repeat;
    if (m_repeat < 1)
        m_Stop = true;

    // check for module cycle timer.  if module is stopped, this will process the deactivation and set stop
    if (m_timer.Check()) {
        if (m_needsTarget) {
            if (m_targetSE == nullptr) {
                DeactivateCycle(true);
                return;
            }
            if (m_targetSE->GetID() != m_targetID) {
                DeactivateCycle(true);
                return;
            }
        }

        ProcessActiveCycle();
    }
}

void ActiveModule::RemoveTarget(SystemEntity* pSE) {
    if (m_targetSE == pSE) {
        _log(MODULE__TRACE, "ActiveModule::RemoveTarget called on %s on %s to remove %s", m_modRef->name(), m_shipRef->name(), pSE->GetName());
        Deactivate();
    }
}

void ActiveModule::Activate(uint16 effectID, uint32 targetID/*0*/, int16 repeat/*0*/)
{
    if (effectID == 16) {
        // catchall for elusive online/offline error, but should be caught in Ship::Activate(), backup in MM::Activate()
        sLog.Error("AM::Activate()", "effectID 16 got here.");
        Online();
        return;
    }
    // already running (or finishing its last cycle):  its effects must not go on twice
    if (m_ModuleState >= Module::State::Deactivating) {
        _log(MODULE__TRACE, "ActiveModule::Activate - %s(%u) is already %s.", m_modRef->name(), itemID(), GetModuleStateName(m_ModuleState));
        return;
    }
    CheckActivationRules();
    /* Quantum Rise (2008):  "The micro warp drive is now vulnerable to warp scramblers (the 7.5km version).  If you get
     *  scrambled your MWD will immediately end its cycle."  Crucible's data:  warp scramblers have
     *  warpScrambleTargetMWDBlockActivation, activationBlocked on the target's modules that need High Speed Maneuvering */
    if (IsActivationBlocked())
        throw UserError("ModuleIsBlocked")
                .AddFormatValue("moduleName", new PyString(m_modRef->itemName()));
    // a module (or any weapon in its bank) that is loading or reloading can't fire
    {
        std::vector<GenericModule*> modules;
        if (m_linkMaster)
            m_shipRef->GetLinkedWeaponMods(m_modRef->flag(), modules);
        if (modules.empty())
            modules.push_back(this);
        for (auto cur : modules)
            if (cur->IsLoading())
                throw UserError ("CantActivateReloadingModule")
                        .AddFormatValue ("moduleTypeID", new PyInt (typeID()));
    }
    if (m_needsCharge and (!m_chargeLoaded or (m_chargeRef.get() == nullptr))) {
        _log(MODULE__TRACE, "ActiveModule::Activate - %s: needsCharge: %s, chargeLoaded: %s, chargeRef: %s", \
                m_modRef->name(), m_needsCharge?"True":"False", m_chargeLoaded?"True":"False", \
                m_chargeRef.get() == nullptr ? "(none)": m_chargeRef->name());
        Clear();
        throw UserError ("NoCharges")
                .AddTypeName ("launcher", typeID());
    }
    // warp disruption field generator: a loaded script makes it a targeted point, otherwise it's a sphere around us
    if (groupID() == EVEDB::invGroups::Warp_Disrupt_Field_Generator) {
        if (IsScriptedDisruptor()) {
            if (!IsValidTarget(targetID)) {
                Clear();
                throw UserError ("DeniedActivateTargetNotPresent");
            }
        } else {
            // a locked target never turns the sphere into an attack
            targetID = 0;
            m_shipRef->ClearTargetRef();
        }
    }
    // defenders don't take a target:  they go after missiles coming at us (LaunchDefender).  nor do bombs (LaunchBomb)
    if (m_launcher and (m_chargeRef.get() != nullptr) and (m_chargeRef->groupID() == EVEDB::invGroups::Defender_Missile))
        targetID = 0;
    if (groupID() == EVEDB::invGroups::Missile_Launcher_Bomb)
        targetID = 0;
    if (IsValidTarget(targetID)) {
        // this is just a guess.  may have to use groupID test to verify if this doesnt work right.
        // also need to make check for modules acting on OUR ship....in which case this will be wrong.
        m_needsTarget = true;
        m_targetID = targetID;
        m_targetSE = m_shipRef->GetPilot()->SystemMgr()->GetSE(targetID);
        if (m_targetSE == nullptr) {
            Clear();
            throw UserError ("DeniedActivateTargetNotPresent");
        }
    }

    if (m_targetSE != nullptr) {
        /*
         * AttrdisallowAgainstEwImmuneTarget
         * AttrDisallowOffensiveModifiers
         * AttrDisallowOffensiveModifierBonus
         */

        /* missile launchers (useMissiles) and the scripted warp disruption field generator (warpDisruptSphere) aren't offensive
         *  effects in Crucible's data:  their charge's effect is.  they attack all the same */
        bool offensive(sFxDataMgr.isOffensive(effectID) or m_launcher);
        if (IsScriptedDisruptor())
            offensive = true;
        if (offensive) {
            // celestials, stations, beacons and the like can't be attacked.  wrecks and containers can (structure only)
            if (m_targetSE->IsItemEntity() or (m_targetSE->IsStaticEntity() and !m_targetSE->IsOutpostSE())) {
                Clear();
                throw UserError ("DeniedTargetInvulnerable");
            }
            // highsec customs offices, outposts out of reach or reinforced (OutpostSiege), reinforced structures
            if (m_targetSE->IsInvul()) {
                // an outpost whose owner's alliance holds sovereignty here says why
                if (m_targetSE->IsOutpostSE()) {
                    uint32 sovAllianceID(svDataMgr.GetSystemAllianceID(m_targetSE->SystemMgr()->GetID()));
                    if ((sovAllianceID > 0) and (PosMgrDB::GetCorpAllianceID(m_targetSE->GetSelf()->ownerID()) == sovAllianceID)) {
                        Clear();
                        throw UserError ("DeniedStationInvulnerableSovereign")
                                .AddFormatValue("module", new PyInt(typeID()))
                                .AddFormatValue("targetname", new PyString(m_targetSE->GetName()));
                    }
                }
                Clear();
                throw UserError ("DeniedTargetInvulnerable");
            }
        }

        CheckTargetRules(effectID);
        // assisting a criminal is not refused in Crucible (no safety):  CrimeCheck() flags the assistant (CrimeWatch::AssistanceAct)
        if (m_targetSE->TargetMgr() != nullptr)
            m_targetSE->TargetMgr()->AddTargetModule(this);
    }

    m_repeat = repeat;
    m_effectID = effectID;
    // single cycle modules (remote ecm burst, doomsday...)
    if (GetAttribute(AttrDisallowRepeatingActivation).get_bool())
        m_repeat = 1;

    if (!CanActivate()) {
        Clear();
        return;
    }

    m_Stop = false;
    m_isWarpSafe = sFxDataMgr.isWarpSafe(m_effectID);

    ShipSE* pShip = m_shipRef->GetPilot()->GetShipSE();
    m_shipSE = pShip;
    m_sysMgr = pShip->SystemMgr();
    m_targMgr = pShip->TargetMgr();
    m_destinyMgr = pShip->DestinyMgr();

    // Do initial cycle immediately while we start timer
    CrimeCheck();
    UpdateCycleHeat(true);
    // a cycle left over from before docking (modules aren't processed in a station) must not shorten this one
    m_timer.Disable();
    SetTimer(DoCycle());

    if (!m_timer.Enabled()) {
        // if the timer wasnt set (for whatever reason), kill activation and return
        Clear();
        return;
    }

    /* prop mods:  the velocity multiplier for the dogma effect (maxVelocity PostMul speedBoostFactorCalc2), from the ship's
     *  mass with this module's massAddition on it (the same effect adds it) */
    if (IsPropulsionModule())
        SetAttribute(AttrSpeedBoostFactorCalc2, PropSpeedMultiplier(m_shipRef->GetAttribute(AttrMass).get_double()
                + GetAttribute(AttrMassAddition).get_double()), false);
    if (ApplyActiveEffects())
        ApplyEffect(FX::State::Active, true);
    if (IsValidTarget(targetID) and ApplyTargetEffects())
        ApplyEffect(FX::State::Target, true);

    std::vector<GenericModule*> modules;
    if (m_linkMaster) {
        m_shipRef->GetLinkedWeaponMods(m_modRef->flag(), modules);
        for (auto cur : modules) {
            cur->GetActiveModule()->SetSlaveData(pShip, this);
            cur->GetActiveModule()->ShowEffect(true, false);
        }
    } else {
        ShowEffect(true, false);
    }

    SetModuleState(Module::State::Activated);

    // overview/tactical ewar icons on the target's client.
    //  modules that keep their effect off dogma (ecm, target ewar) send their own, through the target's EwarState
    if ((m_targetSE != nullptr) and ApplyTargetEffects())
        Ewar::SendEwarNotify(true, m_shipRef->itemID(), itemID(), m_targetSE, Ewar::GetEwarType(groupID()));

    switch (groupID()) {
        case EVEDB::invGroups::Afterburner:
        case EVEDB::invGroups::Microwarpdrive: {
            m_destinyMgr->SpeedBoost();
        } break;
        case EVEDB::invGroups::Stasis_Web: {
            if (m_targetSE != nullptr)
                m_targetSE->DestinyMgr()->WebbedMe(m_modRef, true);
        } break;
        case EVEDB::invGroups::Warp_Disrupt_Field_Generator: {
            if (m_targetSE == nullptr)
                StartDisruptField();
        } break;
    }
    /*def OnSpecialFX
     *     if start and guid == 'effects.WarpScramble*':
     *     if settings.user.ui.Get('notifyMessagesEnabled', 1) or eve.session.shipid in (shipID, targetID):
     *         jammerName = sm.GetService('bracket').GetBracketName2(shipID)
     *         targetName = sm.GetService('bracket').GetBracketName2(targetID)
     *         if jammerName and targetName:
     *            if eve.session.shipid == targetID:
     *                 eve.Message('WarpScrambledBy', {'scrambler': jammerName})
     *            elif eve.session.shipid == shipID:
     *                 eve.Message('WarpScrambledSuccess', {'scrambled': targetName})
     *            else:
     *                eve.Message('WarpScrambledOtherBy', {'scrambler': jammerName,
     *                'scrambled': targetName})
     */

    --m_repeat;
    if (m_repeat < 1)
        m_Stop = true;

    // check for one-hit kills and stop module after cycle completes
    if (m_needsTarget)
        if (m_targetSE->IsDead())
            m_Stop = true;
}

void ActiveModule::Deactivate(std::string effect/*""*/)
{
    if (m_ModuleState != Module::State::Activated) {
        // a module finishing its last cycle on a target that is gone stops now.  it must not keep the target pointer
        if ((m_ModuleState == Module::State::Deactivating) and (effect.compare("TargetDestroyed") == 0)) {
            AbortCycle();
            return;
        }
        _log(MODULE__TRACE, "ActiveModule::Deactivate - %s called Deactivate but is not currently Activated (%s).", m_modRef->name(), GetModuleStateName(m_ModuleState));
        return;
    }

    _log(MODULE__TRACE, "ActiveModule::Deactivate(%s) - module %s(%u) remaining time %ums.", \
            effect.c_str(), m_modRef->name(), m_modRef->itemID(), GetRemainingCycleTimeMS());

    SetModuleState(Module::State::Deactivating);

    if ((m_effectID == EVEEffectID::miningLaser) or (m_effectID == EVEEffectID::miningClouds)) {
        DeactivateCycle(true);
        return;
    }

    if (effect.compare("TargetDestroyed") == 0) {
        m_targetSE = nullptr;
        if (IsProspectModule())
            GetProspectModule()->TargetDestroyed();
    }

    // let module complete current cycle then shut it down.
    m_Stop = true;
}

void ActiveModule::SetSlaveData(ShipSE* pShip, ActiveModule* pMaster) {
    // a slave runs on its master's effect and target, so its own effects name them too (it sent effectID 0 before)
    if ((pMaster != nullptr) and (pMaster != this)) {
        m_effectID = pMaster->m_effectID;
        m_targetID = pMaster->m_targetID;
        m_targetSE = pMaster->m_targetSE;
        m_repeat = pMaster->m_repeat;
        // a slave's own cycle timer doesn't run (its master cycles for the group), so its effects use the master's
        m_slaveCycleMS = pMaster->GetRemainingCycleTimeMS();
    }
    m_shipSE = pShip;
    m_sysMgr = pShip->SystemMgr();
    m_targMgr = pShip->TargetMgr();
    m_destinyMgr = pShip->DestinyMgr();
}

SystemBubble* ActiveModule::Bubble() const {
    return (m_shipSE == nullptr ? nullptr : m_shipSE->SysBubble());
}

void ActiveModule::CrimeCheck()
{
    if (m_targetSE == nullptr)
        return;
    Client* pPilot(m_shipRef->GetPilot());
    if (pPilot == nullptr)
        return;
    // weapons always count (a launcher's effect becomes its charge's after the first cycle)
    if (m_turret or m_launcher or sFxDataMgr.isOffensive(m_effectID)) {
        sCrimeWatch.OffensiveAct(pPilot, m_targetSE);
    } else if (sFxDataMgr.isAssistance(m_effectID)) {
        sCrimeWatch.AssistanceAct(pPilot, m_targetSE);
    }
}

void ActiveModule::UpdateCycleHeat(bool cycling)
{
    std::vector<GenericModule*> modules;
    if (m_linkMaster)
        m_shipRef->GetLinkedWeaponMods(m_modRef->flag(), modules);
    if (modules.empty())
        modules.push_back(this);

    m_heating = false;
    for (auto cur : modules) {
        cur->ApplyPendingOverload();
        bool heating(cycling and cur->IsOverloaded());
        if (heating)
            m_heating = true;
        m_shipRef->SetModuleHeating(cur, heating);
    }
}

void ActiveModule::CheckHeatDamage()
{
    // after an overloaded cycle, each overloaded module may damage itself and its rack
    std::vector<GenericModule*> modules;
    if (m_linkMaster)
        m_shipRef->GetLinkedWeaponMods(m_modRef->flag(), modules);
    if (modules.empty())
        modules.push_back(this);

    for (auto cur : modules)
        if (cur->IsOverloaded() and !cur->IsBurntOut())
            m_shipRef->HeatDamageCheck(cur);
}

// `DoCycle` performs checks before activating the module for its next cycle.
// These checks include applying all capacitor and item consumption
// requirements.
uint32 ActiveModule::DoCycle() {
    if (m_destinyMgr == nullptr) {
        // make error for no destiny/bubble
        AbortCycle();
        return 0;
    }

    // Check for (13) modules which consume items per-cycle (they only consume single type)
    if (m_modRef->HasAttribute(AttrConsumptionType)) {
        uint16 typeID(m_modRef->GetAttribute(AttrConsumptionType).get_uint32()); // cast uint32 to uint16
        uint32 qtyNeed(m_modRef->GetAttribute(AttrConsumptionQuantity).get_uint32());
        // verify character has require amount of consumption type available
        if (!m_shipRef->GetMyInventory()->ContainsTypeQtyByFlag(typeID, EVEItemFlags::flagCargoHold, qtyNeed)) {
            m_shipRef->GetPilot()->SendNotifyMsg("This module requires you to have %u units of %s in your cargo hold.", \
                    qtyNeed, sItemFactory.GetType(typeID)->name().c_str());
            AbortCycle();  // CHECK THIS  for initial activate
            return 0;
        } else if (!m_shipRef->GetMyInventory()->ConsumeTypeQtyByFlag(typeID, EVEItemFlags::flagCargoHold, qtyNeed)) {
            // the fuel can come from several stacks
            AbortCycle();  // CHECK THIS  for initial activate
            return 0;
        }
    }

    // check if ship has sufficient capacitor capacity - if not, abort the cycle
    if (m_modRef->HasAttribute(AttrCapacitorNeed)) {
        float remainingCapacitorCharge = m_shipRef->GetAttribute(AttrCapacitorCharge).get_float();
        float requiredCapacitorCharge = GetAttribute(AttrCapacitorNeed).get_float();

        float newCap = remainingCapacitorCharge - requiredCapacitorCharge;

        if (newCap < 0) {
            m_shipRef->GetPilot()->SendNotifyMsg(
                "This module requires %.0f GJ, but your capacitor only has %.0f GJ remaining.",
                requiredCapacitorCharge,
                remainingCapacitorCharge
            );

            AbortCycle();

            return 0;
        }

        float shipTotalCapacitor = m_shipRef->GetAttribute(AttrCapacitorCapacity).get_float();

        m_shipRef->SetShipCapacitorLevel(newCap / shipTotalCapacitor);
    }

    // a warp scrambler shuts the target's microwarpdrives down, every cycle
    if ((m_targetSE != nullptr) and m_modRef->type().HasEffect(EVEEffectID::warpScrambleTargetMWDBlockActivation))
        BlockTargetMWDs();

    // npcs remember who uses ewar (and neuts) on them, like damage, and which kind (level-one effect threat, npc ai plan 5.7)
    if ((m_targetSE != nullptr) and m_targetSE->IsNPCSE() and (Ewar::GetEwarType(groupID()) != nullptr) and m_shipRef->HasPilot())
        m_targetSE->GetNPCSE()->GetAIMgr()->OnEwarBy(m_shipRef->GetPilot()->GetShipSE(), NPCAIMgr::EwarEffectOfGroup(groupID()));
    // ...and sentries:  it's aggression, their group hears about it (npc ai phase 5)
    if ((m_targetSE != nullptr) and m_targetSE->IsSentrySE() and (Ewar::GetEwarType(groupID()) != nullptr) and m_shipRef->HasPilot())
        m_targetSE->GetSentrySE()->GetAIMgr()->OnEwarBy(m_shipRef->GetPilot()->GetShipSE());
    // remote repair of someone a level-one npc fights is threat (npc ai plan 5.7).  armour and hull reps land at the end of
    //  the cycle and are reported from CycleComplete()
    double assistBefore((groupID() == EVEDB::invGroups::Shield_Transporter) ? RemoteAssistHP(groupID(), m_targetSE) : -1.0);

    // not sure if this is entirely accurate...wip
    switch (m_modRef->groupID()) {
        case EVEDB::invGroups::Projectile_Weapon:
        case EVEDB::invGroups::Hybrid_Weapon:
        case EVEDB::invGroups::Energy_Weapon: {
            // turret weapons use specific code.
            ApplyDamage();
        } break;
        case EVEDB::invGroups::Missile_Launcher_Assault:
        case EVEDB::invGroups::Missile_Launcher_Bomb:   // not sure here
        case EVEDB::invGroups::Missile_Launcher_Citadel:
        case EVEDB::invGroups::Missile_Launcher_Cruise:
        case EVEDB::invGroups::Missile_Launcher_Heavy:
        case EVEDB::invGroups::Missile_Launcher_Heavy_Assault:
        case EVEDB::invGroups::Missile_Launcher_Rocket:
        case EVEDB::invGroups::Missile_Launcher_Siege:
        case EVEDB::invGroups::Missile_Launcher_Standard: {
            LaunchMissile();
        } break;
        case EVEDB::invGroups::Capacitor_Booster:{
            /* the charge holds the capacitor (capacitorBonus, effect powerBooster).  one charge at the start of each cycle,
             *  repeating like any other module until the charges run out
             */
            if (m_chargeRef.get() == nullptr) {
                m_Stop = true;
                break;
            }
            float bonus(m_chargeRef->GetAttribute(AttrCapacitorBonus).get_float());
            ConsumeCharge();
            float capacity(m_shipRef->GetAttribute(AttrCapacitorCapacity).get_float());
            float charge(m_shipRef->GetAttribute(AttrCapacitorCharge).get_float() + bonus);
            if (charge >= capacity) {
                charge = capacity;
                if (m_shipRef->HasPilot() and m_shipRef->GetPilot()->AutoStop())
                    Deactivate();
            }
            m_shipRef->SetAttribute(AttrCapacitorCharge, charge);
        } break;
        case EVEDB::invGroups::Energy_Destabilizer: {
            if (m_targetSE != nullptr)
                Ewar::DrainCapacitor(m_targetSE, GetAttribute(AttrEnergyDestabilizationAmount).get_float());
        } break;
        case EVEDB::invGroups::Energy_Vampire: {
            if (m_targetSE != nullptr)
                Ewar::LeechCapacitor(m_shipRef, m_targetSE, GetAttribute(AttrPowerTransferAmount).get_float());
        } break;
        case EVEDB::invGroups::Energy_Transfer_Array: {
            if (m_targetSE != nullptr)
                UpdateCharge(AttrCapacitorCharge, AttrCapacitorCapacity, AttrPowerTransferAmount, m_targetSE->GetSelf());
        } break;
        case EVEDB::invGroups::Shield_Transporter: {
            if (m_targetSE != nullptr)
                UpdateCharge(AttrShieldCharge, AttrShieldCapacity, AttrShieldBonus, m_targetSE->GetSelf());
        } break;
        case EVEDB::invGroups::Shield_Booster: {
            UpdateCharge(AttrShieldCharge, AttrShieldCapacity, AttrShieldBonus, m_shipRef);
        } break;
        // armour and hull repairers, local and remote, repair at the end of the cycle (CycleComplete).  shield boosters
        //  and transporters at the start
        case EVEDB::invGroups::Remote_Hull_Repairer:
        case EVEDB::invGroups::Hull_Repair_Unit:
        case EVEDB::invGroups::Armor_Repair_Projector:
        case EVEDB::invGroups::Armor_Repair_Unit: {
        } break;
        case EVEDB::invGroups::Missile_Launcher_Snowball: {
            LaunchSnowBall();
        } break;
        case EVEDB::invGroups::Scan_Probe_Launcher: {
            LaunchProbe();
        } break;
        case EVEDB::invGroups::Interdiction_Sphere_Launcher: {
            // one sphere per activation.  moduleReactivationDelay (118s) follows
            LaunchInterdictionSphere();
            m_repeat = 1;
        } break;
        case EVEDB::invGroups::Warp_Disrupt_Field_Generator: {
            /* focused script: infinite strength point (warpScrambleStrengthHidden 100), renewed every cycle.
             *   kept in the target's ewar state (it ends when we stop, die, dock or leave), which also sends the overview icon.
             *   sphere mode: the field is registered on activation (StartDisruptField) and moves with the ship
             */
            if (m_targetSE != nullptr)
                m_targetSE->GetEwar(true)->Apply(m_shipRef->itemID(), Ewar::Scramble, GetAttribute(AttrDuration).get_uint32() + 1000,
                        GetAttribute(AttrWarpScrambleStrengthHidden).get_float(), 0.0f, InventoryItemRef(nullptr), itemID());
        } break;
        // these neither require nor consume charges
        case EVEDB::invGroups::Salvager:    //working
        case EVEDB::invGroups::Target_Painter:  // working
        case EVEDB::invGroups::Signal_Amplifier:    //working
        case EVEDB::invGroups::Sensor_Booster:  //working
        case EVEDB::invGroups::Tracking_Computer:   //working
        case EVEDB::invGroups::Tracking_Disruptor:    //working
        case EVEDB::invGroups::Remote_Sensor_Damper:
        case EVEDB::invGroups::Tracking_Link:
        case EVEDB::invGroups::Tracking_Enhancer:
        case EVEDB::invGroups::Projected_ECCM:
        case EVEDB::invGroups::Remote_Sensor_Booster: {
        } break;
        case EVEDB::invGroups::Artifacts_and_Prototypes: {
        } break;
        case EVEDB::invGroups::Passive_Targeting_System: {
            // TargetSilently:  the lock starts with the cycle.  a target we already have is left alone
            if ((m_targetSE != nullptr) and (m_targMgr != nullptr))
                m_targMgr->StartPassiveTargeting(m_targetSE, m_shipRef);
        } break;
        // these passive modules will need specific code
        case EVEDB::invGroups::Missile_Launcher_Defender:
        case EVEDB::invGroups::Countermeasure_Launcher: {
        } break;
        // these active modules will need specific code
        case EVEDB::invGroups::Cynosural_Field_Generator:
        case EVEDB::invGroups::Covert_Cynosural_Field_Generator:
        case EVEDB::invGroups::Automated_Targeting_System: {
        } break;
        // these im not sure about yet
        case EVEDB::invGroups::ECM:
        case EVEDB::invGroups::ECCM:
        case EVEDB::invGroups::Gang_Coordinator:
        case EVEDB::invGroups::Cloaking_Device:
        case EVEDB::invGroups::Siege_Module:
        case EVEDB::invGroups::Super_Weapon:
        case EVEDB::invGroups::Jump_Portal_Generator:
        case EVEDB::invGroups::Remote_ECM_Burst:
        case EVEDB::invGroups::Smart_Bomb:
        case EVEDB::invGroups::ECM_Burst: {
        } break;
    }

    if ((assistBefore >= 0.0) and (m_targetSE != nullptr) and m_shipRef->HasPilot())
        NotifyRemoteAssist(m_shipRef->GetPilot()->GetShipSE(), m_targetSE,
                           static_cast<float>(RemoteAssistHP(groupID(), m_targetSE) - assistBefore));

    EvilNumber cycleTime = 10000;   // default to 10s
    if (m_modRef->HasAttribute(AttrSpeed, cycleTime)) {
        ; //return cycleTime.get_int();
    } else if (m_modRef->HasAttribute(AttrDuration, cycleTime)) {
        ; //return cycleTime.get_int();
    }
    return cycleTime.get_int();
}

void ActiveModule::AbortCycle(bool force/*false*/)
{
    _log(MODULE__TRACE, "%s(%u) calling AbortCycle() - state: %s, m_stop:%s", \
            m_modRef->name(), m_modRef->itemID(), GetModuleStateName(m_ModuleState), m_Stop?"true":"false");

    if (m_ModuleState < Module::State::Deactivating)
        return;

    /* a module finishing its last cycle (m_Stop set) stops now too:  every caller (target gone, unfit, warp, jump, eject,
     *  logoff, death) needs its effects off and its target pointer cleared.  DeactivateCycle() ends in Clear(), which
     *  sets the module online, so it runs once
     */
    // Immediately stop active cycle for things such as insufficient cap, remove module, init warp, target destroyed, target left bubble, or miner deactivated by player:
    SetModuleState(Module::State::Deactivating);
    DeactivateCycle(true);
    m_timer.Disable();
    m_Stop = true;
}

void ActiveModule::DeactivateCycle(bool abort/*false*/)
{
    if (m_ModuleState < Module::State::Deactivating)
        return;

    _log(MODULE__TRACE, "%s(%u) calling DeactivateCycle(abort: %s)", m_modRef->name(), m_modRef->itemID(), abort?"true":"false");
    if ((m_ModuleState == Module::State::Activated) and (!abort)) {
        _log(MODULE__ERROR, "ActiveModule::DeactivateCycle() - Called on %s(%u) with current state %s and !abort.",  \
                m_modRef->name(), m_modRef->itemID(), GetModuleStateName(m_ModuleState));
        EvE::traceStack();
    }

    std::vector<GenericModule*> modules;
    if (m_linkMaster) {
        m_shipRef->GetLinkedWeaponMods(m_modRef->flag(), modules);
        for (auto cur : modules)
            cur->GetActiveModule()->ShowEffect(false, abort);
    } else {
        ShowEffect(false, abort);
    }

    // Remove modifier added by module
    if (ApplyActiveEffects())
        ApplyEffect(FX::State::Active, false);
    if (IsValidTarget(m_targetID)
    and (m_targetSE != nullptr)
    and ApplyTargetEffects())
        ApplyEffect(FX::State::Target, false);
    if ((m_targetSE != nullptr) and ApplyTargetEffects())
        Ewar::SendEwarNotify(false, m_shipRef->itemID(), itemID(), m_targetSE, Ewar::GetEwarType(groupID()));
        /*
    else if (m_needsTarget) {
        _log(MODULE__INFO, "%s - DeactivateCycle() - need target = true and targetID: %u, targSE: %x", \
                m_modRef->name(), m_targetID, m_targetSE);
    } */

    switch (groupID()) {
        case EVEDB::invGroups::Tractor_Beam: {
            if (m_targetSE != nullptr)
                m_targetSE->DestinyMgr()->TractorBeamStop();
        } break;
        case EVEDB::invGroups::Afterburner:
        case EVEDB::invGroups::Microwarpdrive: {
            m_destinyMgr->SpeedBoost(true);
        } break;
        case EVEDB::invGroups::Stasis_Web: {
            if (m_targetSE != nullptr)
                m_targetSE->DestinyMgr()->WebbedMe(m_modRef, false);
        } break;
        case EVEDB::invGroups::Warp_Disrupt_Field_Generator: {
            StopDisruptField();
            if ((m_targetSE != nullptr) and (m_targetSE->GetEwar() != nullptr))
                m_targetSE->GetEwar()->Remove(m_shipRef->itemID(), Ewar::Scramble, itemID());
        } break;
        case EVEDB::invGroups::Survey_Scanner: {
            SystemBubble* pBubble(Bubble());
            if (abort or (pBubble == nullptr)) {
                Clear();
                return;
            }
            // this is the complete belt scanner rsp code here.
            PyTuple* result = new PyTuple(2);
            result->SetItem(0, new PyString("OnSurveyScanComplete"));
            PyList* list = new PyList();
            std::vector<AsteroidSE*> vList;
            m_sysMgr->GetBeltMgr()->GetList(sBubbleMgr.GetBeltID(pBubble->GetID()), vList);
            // when roids are spawned, BeltMgr sets this bubble "IsBelt = true", even in anomalies
            if (pBubble->IsBelt()) {
                float m_range = GetAttribute(AttrSurveyScanRange).get_float();
                float distance = 0;
                for (auto pASE : vList) {
                    distance = m_shipRef->position().distance(pASE->GetPosition());
                    distance -= pASE->GetRadius();
                    distance -= m_shipRef->radius(); // do we need this one here?
                    if (distance < m_range) {
                        PyTuple* tuple2 = new PyTuple(3);
                        tuple2->SetItem(0, new PyInt(pASE->GetID()));
                        tuple2->SetItem(1, new PyInt(pASE->GetTypeID()));
                        tuple2->SetItem(2, pASE->GetSelf()->GetAttribute(AttrQuantity).GetPyObject());
                        list->AddItem(tuple2);
                    }
                }
            } else if (pBubble->IsIce()) {
                // allow ice scanning without a radius check....may change later.
                for (auto pASE : vList) {
                    PyTuple* tuple2 = new PyTuple(3);
                    tuple2->SetItem(0, new PyInt(pASE->GetID()));
                    tuple2->SetItem(1, new PyInt(pASE->GetTypeID()));
                    tuple2->SetItem(2, pASE->GetSelf()->GetAttribute(AttrQuantity).GetPyObject());
                    list->AddItem(tuple2);
                }
            }

            result->SetItem(1, list);
            // Send results.
            m_shipRef->GetPilot()->QueueDestinyEvent(&result);
        } break;
        /*
        case EVEDB::invGroups::Warp_Scrambler: {
            if (m_targetSE != nullptr)
                m_targetSE->GetSelf()->SetAttribute(AttrWarpScrambleStatus);
        } break;
        case EVEDB::invGroups::Warp_Core_Stabilizer: {
            if (m_targetSE != nullptr)
                m_targetSE->GetSelf()->SetAttribute(AttrWarpScrambleStatus);
        } break;
        */
        // not sure just how this works yet
        // case EVEDB::invGroups::System_Scanner:
    }

    Clear();
}

void ActiveModule::ProcessActiveCycle() {
    // the cycle that just ended was overloaded
    if (m_heating) {
        CheckHeatDamage();
        // heat damage can burn this module out, which stops it
        if (m_ModuleState < Module::State::Deactivating)
            return;
    }

    // the cycle ran to its end
    CycleComplete();

    if (m_Stop) {
        SetModuleState(Module::State::Deactivating);
    }

    if (m_ModuleState == Module::State::Deactivating) {
        DeactivateCycle();
        return;
    }

    CrimeCheck();
    UpdateCycleHeat(true);
    // an overload that started or stopped with this cycle changed the speed factor
    if (IsPropulsionModule())
        RefreshPropBoost();
    SetTimer(DoCycle());
}

void ActiveModule::CycleComplete()
{
    switch (groupID()) {
        case EVEDB::invGroups::Ship_Scanner: {
            ShipScanResult();
        } break;
        case EVEDB::invGroups::Cargo_Scanner: {
            CargoScanResult();
        } break;
        case EVEDB::invGroups::Automated_Targeting_System: {
            TargetHostiles();
        } break;
        case EVEDB::invGroups::Hull_Repair_Unit: {
            UpdateDamage(AttrDamage, AttrStructureDamageAmount, m_shipRef);
        } break;
        case EVEDB::invGroups::Armor_Repair_Unit: {
            UpdateDamage(AttrArmorDamage, AttrArmorDamageAmount, m_shipRef);
        } break;
        case EVEDB::invGroups::Remote_Hull_Repairer:
        case EVEDB::invGroups::Armor_Repair_Projector: {
            if ((m_targetSE == nullptr) or !m_shipRef->HasPilot())
                break;
            double before(RemoteAssistHP(groupID(), m_targetSE));
            if (groupID() == EVEDB::invGroups::Remote_Hull_Repairer)
                UpdateDamage(AttrDamage, AttrStructureDamageAmount, m_targetSE->GetSelf());
            else
                UpdateDamage(AttrArmorDamage, AttrArmorDamageAmount, m_targetSE->GetSelf());
            if ((before >= 0.0) and (m_targetSE != nullptr))
                NotifyRemoteAssist(m_shipRef->GetPilot()->GetShipSE(), m_targetSE,
                                   static_cast<float>(RemoteAssistHP(groupID(), m_targetSE) - before));
        } break;
    }
}

void ActiveModule::TargetHostiles()
{
    /* "Targets any hostile ship within range on activation."  hostile:  rats (any npc that isn't police, a navy, CONCORD or a
     *  protected ambient npc) and pilots we can shoot without a crime (war, outlaw, criminal or suspect to us, kill right,
     *  enemy militia).  nearest first, up to our free target slots and within our own targeting range */
    if ((m_shipSE == nullptr) or (m_targMgr == nullptr) or !m_shipRef->HasPilot() or (m_shipSE->SysBubble() == nullptr))
        return;
    if (m_targMgr->IsJammed())
        return;
    uint8 maxTargets(m_targMgr->GetMaxLockedTargets(m_shipRef));
    if (m_targMgr->TargetCount() >= maxTargets)
        return;
    double range(GetAttribute(AttrTargetHostileRange).get_double());
    double maxRange(m_targMgr->GetMaxTargetRange(m_shipRef));
    if (range > maxRange)
        range = maxRange;

    Client* pClient(m_shipRef->GetPilot());
    std::vector<std::pair<double, SystemEntity*>> found;
    m_shipSE->SysBubble()->ForEachDynamic([&](SystemEntity* pSE) {
        if ((pSE == m_shipSE) or (pSE->TargetMgr() == nullptr) or pSE->IsDead())
            return;
        if (m_targMgr->GetTarget(pSE->GetID(), false) != nullptr)
            return;
        if (pSE->IsNPCSE()) {
            NPC* pNPC(pSE->GetNPCSE());
            if (pNPC->IsLawEnforcer() or (pSE->GetCorporationID() == corpCONCORD))
                return;
            if ((pNPC->GetAmbientNPC() != nullptr) and pNPC->GetAmbientNPC()->IsProtected())
                return;
        } else if (pSE->IsShipSE()) {
            if (!pSE->HasPilot() or (pSE->GetPilot() == pClient) or pSE->GetPilot()->InPod())
                return;
            if (!sCrimeWatch.IsFreeTarget(pClient, pSE))
                return;
        } else {
            return;
        }
        double dist(m_shipSE->GetPosition().distance(pSE->GetPosition()) - pSE->GetRadius());
        if (dist <= range)
            found.push_back(std::make_pair(dist, pSE));
    });
    std::sort(found.begin(), found.end(), [](const std::pair<double, SystemEntity*>& a, const std::pair<double, SystemEntity*>& b) {
        return a.first < b.first;
    });
    for (auto& cur : found) {
        if (m_targMgr->TargetCount() >= maxTargets)
            break;
        m_targMgr->StartTargeting(cur.second, m_shipRef);
    }
}

bool ActiveModule::ScanResisted(uint16 resistAttr)
{
    float resist(m_targetSE->GetSelf()->GetAttribute(resistAttr).get_float());
    if ((resist > 0) and (MakeRandomFloat(0, 100) < resist)) {
        // no client message for this
        m_shipRef->GetPilot()->SendNotifyMsg("The %s resisted your %s.", m_targetSE->GetName(), m_modRef->name());
        return true;
    }
    return false;
}

/* Crucible ship scanner (effect shipScan):  the target's fitted modules and loaded charges, and its capacitor.
 *  beyond shipScanRange each module shows with a chance falling off over shipScanFalloff ("the further it goes beyond scan
 *  range, the more inaccurate its results will be").  client:  OnShipScanCompleted(shipID, capCharge, capCapacity,
 *  [(typeID, quantity)]), charges are shown once.  npcs have nothing fitted
 */
void ActiveModule::ShipScanResult()
{
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or (m_targetSE == nullptr) or !(m_targetSE->IsShipSE() or m_targetSE->IsNPCSE()))
        return;
    if (ScanResisted(AttrShipScanResistance))
        return;

    float range(GetAttribute(AttrShipScanRange).get_float());
    float falloff(GetAttribute(AttrShipScanFalloff).get_float());
    float distance(m_shipRef->position().distance(m_targetSE->GetPosition()) - m_targetSE->GetRadius());
    float chance(1.0f);
    if ((distance > range) and (falloff > 0))
        chance = std::exp(-std::pow((distance - range) / falloff, 2));

    std::map<uint16, uint32> found;     // typeID, count
    if (m_targetSE->IsShipSE()) {
        ShipItemRef targetShip(m_targetSE->GetShipSE()->GetShipItemRef());
        std::vector<InventoryItemRef> modules;
        targetShip->GetModuleManager()->GetModuleListOfRefsAsc(modules);
        for (auto cur : modules) {
            // fitted hi/med/low modules (no rigs or subsystems)
            if ((cur->flag() < flagLowSlot0) or (cur->flag() > flagHiSlot7))
                continue;
            if ((chance < 1.0f) and (MakeRandomFloat() >= chance))
                continue;
            ++found[cur->typeID()];
            InventoryItemRef chargeRef(targetShip->GetModuleManager()->GetLoadedChargeOnModule(cur));
            if (chargeRef.get() != nullptr)
                found[chargeRef->typeID()] = 1;
        }
    }

    PyList* list = new PyList();
    for (auto cur : found) {
        PyTuple* entry = new PyTuple(2);
            entry->SetItem(0, new PyInt(cur.first));
            entry->SetItem(1, new PyInt(cur.second));
        list->AddItem(entry);
    }

    InventoryItemRef targetRef(m_targetSE->GetSelf());
    PyTuple* tuple = new PyTuple(4);
        tuple->SetItem(0, new PyInt(m_targetSE->GetID()));
        tuple->SetItem(1, new PyFloat(targetRef->GetAttribute(AttrCapacitorCharge).get_double()));
        tuple->SetItem(2, new PyFloat(targetRef->GetAttribute(AttrCapacitorCapacity).get_double()));
        tuple->SetItem(3, list);
    pClient->SendNotification("OnShipScanCompleted", "clientID", &tuple);
}

/* Crucible cargo scanner (effect cargoScan):  the target's cargo hold.  client:  OnCargoScanComplete(shipID,
 *  [(typeID, quantity)]), a blueprint copy has quantity -singletonBlueprintCopy (-2)
 */
void ActiveModule::CargoScanResult()
{
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or (m_targetSE == nullptr) or !(m_targetSE->IsShipSE() or m_targetSE->IsNPCSE()))
        return;
    if (ScanResisted(AttrCargoScanResistance))
        return;

    PyList* list = new PyList();
    if (m_targetSE->IsShipSE()) {
        std::vector<InventoryItemRef> cargo;
        m_targetSE->GetSelf()->GetMyInventory()->GetInventoryVec(cargo);
        for (auto cur : cargo) {
            if (cur->flag() != flagCargoHold)
                continue;
            int32 quantity(cur->quantity());
            if ((cur->categoryID() == EVEDB::invCategories::Blueprint) and BlueprintRef::StaticCast(cur)->copy())
                quantity = -2;      // -const.singletonBlueprintCopy
            PyTuple* entry = new PyTuple(2);
                entry->SetItem(0, new PyInt(cur->typeID()));
                entry->SetItem(1, new PyInt(quantity));
            list->AddItem(entry);
        }
    }

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(m_targetSE->GetID()));
        tuple->SetItem(1, list);
    pClient->SendNotification("OnCargoScanComplete", "clientID", &tuple);
}

void ActiveModule::SetTimer(uint32 time) {
    if (time < 100) {
        m_Stop = true;
        return;
    }
    // timer must be restarted for fleet boosts to activate
    if (m_timer.Enabled())
        return;
    // updated timer will reset cycle time if changed, but i DO NOT have client display coded to reset...this will fuck up timer time in client.  (worse than it already is)
    _log(MODULE__TRACE, "ActiveModule::SetTimer() - %s with %u ms for %s on %s.", \
            (m_timer.Enabled()? "Updated" : "Started"), time, m_modRef->name(), m_shipRef->name());
    m_timer.Start(time);
}

void ActiveModule::LoadCharge(InventoryItemRef chargeRef, bool instant/*false*/)
{
    _log(MODULE__TRACE, "%s(%u) calling AM::LoadCharge()", m_modRef->name(), m_modRef->itemID());

    if (chargeRef.get() == nullptr) {
        _log(MODULE__WARNING, "ActiveModule::LoadCharge() for %s - Cannot find charge to load into this module", m_modRef->name());
        return;
    }

    /* ModuleManager passes the charge that is in the slot.  the same item again means more of it was merged in (a reload with
     *  the same type, or a second stack while loading), anything else is a new charge (a different type is unloaded first)
     */
    Client* pClient(m_shipRef->GetPilot());
    bool sameCharge(m_chargeRef.get() == chargeRef.get());
    if (!sameCharge) {
        // whatever was here before is gone.  its effects come off first
        if ((pClient != nullptr) and ParseChargeFx(false))
            sFxProc.ApplyEffects(m_modRef.get(), pClient->GetChar().get(), m_shipRef.get(), pClient->IsInSpace());
        m_chargeFx = false;
        m_chargeRef = chargeRef;
        m_chargeLoaded = false;
        m_ChargeState = Module::State::Unloaded;
        m_reloadTimer.Disable();

        // a charge loaded in space missed the ship's effects pass: give it the pilot's missile (etc) bonuses now.
        //  docked, login and undocking loads get them from ShipItem::ProcessEffects()
        if ((pClient != nullptr) and !pClient->IsLogin() and pClient->IsInSpace() and !m_shipRef->IsUndocking())
            sFxProc.ApplyOwnerModifiers(chargeRef, pClient->GetChar().get(), m_shipRef.get());
    }

    // the ship's own fitting (login, boarding, no pilot) is already loaded, and docked loading is instant
    if (instant or (pClient == nullptr) or pClient->IsLogin() or !pClient->IsInSpace() or (m_reloadTime < 1)) {
        FinishLoading();
        return;
    }

    // more charges going in while this one is still loading:  the reload already running covers them
    if (sameCharge and IsLoading())
        return;

    // an empty module is loading, a loaded one reloading (its charge keeps working once the reload is done, it can't fire meanwhile)
    m_ChargeState = (m_chargeLoaded ? Module::State::Reloading : Module::State::Loading);
    m_reloadTimer.Disable();
    m_reloadTimer.Start(m_reloadTime);

    /*  **** this sets "reload blink" status on weapon button, and the LauncherLoadDelay message
     * def OnChargeBeingLoadedToModule(self, moduleIDs, chargeTypeID, reloadTime):
     *  {returns}
     *        [PyTuple 3 items]
     *          [PyTuple 1 items]
     *            [PyIntegerVar 1005885547063]  << moduleID (can be multiple, each module of a bank sends its own)
     *          [PyInt 203]                     << chargeTypeID
     *          [PyFloat 10000]                 << reloadTime (ms)
     */
    PyTuple* module = new PyTuple(1);
        module->SetItem(0, new PyInt(m_modRef->itemID()));
    PyTuple* tmp = new PyTuple(3);
        tmp->SetItem(0, module);
        tmp->SetItem(1, new PyInt(chargeRef->typeID()));
        tmp->SetItem(2, new PyInt(m_reloadTime));
    pClient->SendNotification("OnChargeBeingLoadedToModule", "shipid", &tmp);
}

void ActiveModule::FinishLoading()
{
    m_reloadTimer.Disable();
    if (m_chargeRef.get() == nullptr) {
        // the charge went away while it was loading (unfit, destroyed)
        m_chargeLoaded = false;
        m_ChargeState = Module::State::Unloaded;
        return;
    }

    m_ChargeState = Module::State::Loaded;
    m_chargeLoaded = true;

    /* a new charge's effects go on an online module now.  a reload keeps the effects it had.  offline modules and logins get
     *  them from Online()
     */
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or pClient->IsLogin() or (m_ModuleState < Module::State::Online))
        return;
    if (ParseChargeFx(true))
        sFxProc.ApplyEffects(m_modRef.get(), pClient->GetChar().get(), m_shipRef.get(), pClient->IsInSpace());
}

//{'FullPath': u'UI/Messages', 'messageID': 259200, 'label': u'NoChargesBody'}(u'{launcher} has run out of charges', None, {u'{launcher}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'launcher'}})
//{'FullPath': u'UI/Messages', 'messageID': 259232, 'label': u'NotEnoughChargesBody'}(u'{launcher} has {[numeric]got} charges, but needs {[numeric]need} to fire.', None, {u'{[numeric]got}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'got'}, u'{launcher}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'launcher'}, u'{[numeric]need}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'need'}})
//{'FullPath': u'UI/Messages', 'messageID': 258889, 'label': u'TooManyChargesForLauncherBody'}(u'The launcher is currently holding {[numeric]excess} too many excess units.', None, {u'{[numeric]excess}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'excess'}})

//{'FullPath': u'UI/Messages', 'messageID': 259152, 'label': u'NoSpaceForReplacedItemBody'}(u'There is not enough space in the cargo hold for the charges currently in the {item} to be moved into.', None, {u'{item}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'item'}})

void ActiveModule::UnloadCharge()
{
    _log(MODULE__TRACE, "%s(%u) calling AM::UnloadCharge()", m_modRef->name(), m_modRef->itemID());

    /* make sure module isnt currently active.  a module that ran out (or lost its crystal) finishes the cycle it is in and stops.
     *  m_Stop is set too, for the first cycle of an activation (the module isn't Activated yet while that cycle fires)
     */
    Deactivate();
    m_Stop = true;
    // a reload in progress is cancelled with the charge
    m_reloadTimer.Disable();

    Client* pClient(m_shipRef->GetPilot());
    // civilian turrets keep their own item as the "charge"
    if ((m_chargeRef.get() != nullptr) and (m_chargeRef.get() != m_modRef.get())) {
        if (m_chargeRef->HasAttribute(AttrUnfitCapCost)) {
            float cap(m_shipRef->GetAttribute(AttrCapacitorCharge).get_float());
            cap -= m_chargeRef->GetAttribute(AttrUnfitCapCost).get_float();
            m_shipRef->SetAttribute(AttrCapacitorCharge, cap);
        }

        // the charge's effects come off, if they are on (an offline module has them off already, a loading charge never had them)
        if (pClient != nullptr) {
            m_modRef->ClearModifiers();
            // apply to containing module to properly remove effects  -this doesnt work right for scripts.
            if (ParseChargeFx(false))
                sFxProc.ApplyEffects(m_modRef.get(), pClient->GetChar().get(), m_shipRef.get(), pClient->IsInSpace());
        }
        m_chargeRef = InventoryItemRef(nullptr);       // Ensure ref is NULL
    }

    m_chargeFx = false;
    m_ChargeState = Module::State::Unloaded;
    m_chargeLoaded = false;

    // scripts boost one attrib, while reducing or deleting others.  once the attrib is deleted, i cant 'undo' without reload
    EvilNumber typeID(EvilZero);
    if (m_modRef->HasAttribute(AttrChargeGroup1, typeID))
        switch (typeID.get_int()) {
            // find what attribs were changed and reload them to default once charge is removed
            case 907:   //    Tracking Script
            case 909: { //    Tracking Disruption Script
                m_modRef->ResetAttribute(AttrFalloffBonus, true);
                m_modRef->ResetAttribute(AttrMaxRangeBonus, true);
                m_modRef->ResetAttribute(AttrTrackingSpeedBonus, true);
            } break;
            case 908: { //    Warp Disruption Script
                m_modRef->ResetAttribute(AttrSignatureRadiusBonus, true);
                m_modRef->ResetAttribute(AttrMassBonusPercentage, true);
                m_modRef->ResetAttribute(AttrSpeedFactorBonus, true);
                m_modRef->ResetAttribute(AttrSpeedBoostFactorBonus, true);
            } break;
            //case 910:   //    Sensor Booster Script
            case 911: { //    Sensor Dampener Script
                m_modRef->ResetAttribute(AttrFalloffBonus, true);
            } break;
        }
}

void ActiveModule::ConsumeCharge() {
    /* one charge per cycle from every online, loaded weapon of the bank (just this module when not banked).
     *  only used in space:  the stack size goes to the client as the charge's quantity attribute (OMAC), not as an ixStacksize
     *  update.  the last one empties the module:  the item is deleted, which unloads it (ShipItem::RemoveItem) and stops the
     *  module at the end of this cycle.  the client's auto reload (a client setting) then asks for a reload (LoadAmmoToBank)
     */
    std::vector<GenericModule*> modules;
    if (m_linkMaster)
        m_shipRef->GetLinkedWeaponMods(m_modRef->flag(), modules);
    if (modules.empty())
        modules.push_back(this);

    for (auto cur : modules) {
        if (!cur->isOnline() or !cur->IsLoaded())
            continue;
        InventoryItemRef cRef(cur->GetLoadedChargeRef());
        // civilian turrets keep their own item as the "charge"
        if ((cRef.get() == nullptr) or (cRef.get() == cur->GetSelf().get()) or (cRef->quantity() < 1))
            continue;
        cRef->AlterQuantity(-1, false);
    }
}

void ActiveModule::ApplyEffect(int8 state, bool active/*false*/)
{
    // target effects (remote sensor boosters, tracking links, projected eccm) go on this module's target, also when they are
    //  removed after the pilot has activated something on another target.  the undo snapshots make them reversible
    if (state == FX::State::Target)
        m_shipRef->SetTargetRef((m_targetSE != nullptr) ? m_targetSE->GetSelf() : InventoryItemRef(nullptr));
    // process and apply module's active effects
    ProcessEffects(state, active);
    sFxProc.ApplyEffects(m_modRef.get(), m_shipRef->GetPilot()->GetChar().get(), m_shipRef.get(), true);
    if (state == FX::State::Target)
        m_shipRef->ClearTargetRef();
}

void ActiveModule::UpdateCharge(uint16 attrID, uint16 testAttrID, uint16 srcAttrID, InventoryItemRef iRef)
{
    // Apply boost amount:
    EvilNumber newValue = iRef->GetAttribute(attrID);
    newValue += GetAttribute(srcAttrID);
    if (newValue > iRef->GetAttribute(testAttrID)) {
        newValue = iRef->GetAttribute(testAttrID);
        if (m_shipRef->GetPilot()->AutoStop())
            Deactivate();
    }
    iRef->SetAttribute(attrID, newValue);

    // the hp bars of whoever was boosted (us, or a remote target)
    if (attrID == AttrShieldCharge) {
        SystemEntity* pSE(iRef->itemID() == m_shipRef->itemID() ? m_shipRef->GetPilot()->GetShipSE() : m_targetSE);
        if (pSE != nullptr)
            pSE->SendDamageStateChanged();
    }
}

void ActiveModule::UpdateDamage(uint16 attrID, uint16 srcAttrID, InventoryItemRef iRef)
{
    EvilNumber newValue = iRef->GetAttribute(attrID);
    newValue -= GetAttribute(srcAttrID);
    if (newValue < EvilZero) {
        newValue = EvilZero;
        if (m_shipRef->GetPilot()->AutoStop())
            Deactivate();
    }
    iRef->SetAttribute(attrID, newValue);
    // the hp bars of whoever was repaired (us, or a remote target)
    SystemEntity* pSE(iRef->itemID() == m_shipRef->itemID() ? m_shipRef->GetPilot()->GetShipSE() : m_targetSE);
    if (pSE != nullptr)
        pSE->SendDamageStateChanged();
}

// not used
void ActiveModule::ReprocessCharge()
{
    if (m_chargeRef.get() == nullptr)
        return;
    /*  may not need to reset this...
    m_chargeRef->ClearModifiers();
    for (auto it : m_chargeRef->type().m_stateFxMap) {
        fxData data = fxData();
        data.action = FX::Action::dgmActInvalid;
        data.srcRef = m_chargeRef;
        sFxProc.ParseExpression(m_modRef.get(), sFxDataMgr.GetExpression(it.second.preExpression), data, this);
    } */
    sFxProc.ApplyEffects(m_modRef.get(), m_shipRef->GetPilot()->GetChar().get(), m_shipRef.get(), m_shipRef->GetPilot()->IsInSpace());
    m_chargeRef->ClearModifiers();
}

bool ActiveModule::CanActivate() {
    // there is still more to be done here.  wip
    //  modules that require specific tests are coded in their module class, which will call this if their specific checks pass

    /* modules and charges flagged disallowInEmpireSpace (bubbles, HIC field, bombs, doomsday) only work in 0.0 and w-space.
     *   type values, not the module's modified attribute: a loaded charge decides (the focused script (0) frees the
     *   field generator (1), the warp disrupt probe (1) restricts its launcher)
     */
    EvilNumber disallow(m_modRef->type().HasAttribute(AttrDisallowInEmpireSpace) ? m_modRef->type().GetAttribute(AttrDisallowInEmpireSpace) : EvilZero);
    if ((m_chargeRef.get() != nullptr) and m_chargeRef->type().HasAttribute(AttrDisallowInEmpireSpace))
        disallow = m_chargeRef->type().GetAttribute(AttrDisallowInEmpireSpace);
    if ((disallow > EvilZero) and m_shipRef->HasPilot() and InterdictionMgr::IsRestricted(m_shipRef->GetPilot()->SystemMgr())) {
        Clear();
        throw UserError("CantInEmpireSpace");
    }

    // Check for (13) modules which consume items.
    if (m_modRef->HasAttribute(AttrConsumptionType)) {
        uint16 typeID(m_modRef->GetAttribute(AttrConsumptionType).get_uint32()); // cast uint32 to uint16
        uint32 qtyNeed(m_modRef->GetAttribute(AttrConsumptionQuantity).get_uint32());
        // verify character has require amount of consumption type available
        if (!m_shipRef->GetMyInventory()->ContainsTypeQtyByFlag(typeID, EVEItemFlags::flagCargoHold, qtyNeed)) {
            m_shipRef->GetPilot()->SendNotifyMsg("This module requires you to have %u units of %s in your inventory.", \
                    qtyNeed, sItemFactory.GetType(typeID)->name().c_str());
            return false;
        }
    }

     // check if ship has sufficient capacitor capacity
    if (m_modRef->HasAttribute(AttrCapacitorNeed)) {
        float remainingCapacitorCharge = m_shipRef->GetAttribute(AttrCapacitorCharge).get_float();
        float requiredCapacitorCharge = GetAttribute(AttrCapacitorNeed).get_float();

        float newCap = remainingCapacitorCharge - requiredCapacitorCharge;

        if (newCap < 0) {
            m_shipRef->GetPilot()->SendNotifyMsg(
                "This module requires %.0f GJ, but your capacitor only has %.0f GJ remaining.",
                requiredCapacitorCharge,
                remainingCapacitorCharge
            );

            return false;
        }
    }

    // scanner probes:  Astrometrics allows 3 + 1 per level under control
    if ((groupID() == EVEDB::invGroups::Scan_Probe_Launcher) and (m_chargeRef.get() != nullptr)
    and (m_chargeRef->groupID() == EVEDB::invGroups::Scanner_Probe) and m_shipRef->HasPilot()) {
        Client* pClient(m_shipRef->GetPilot());
        uint8 limit(3 + pClient->GetChar()->GetSkillLevel(EvESkill::Astrometrics));
        if ((pClient->scan() != nullptr) and (pClient->scan()->GetProbeCount() >= limit)) {
            uint16 typeID(m_chargeRef->typeID());
            Clear();
            throw UserError("NoProbeManagementAbilitiesLeft")
                    .AddFormatValue("probeTypeID", new PyInt(typeID))
                    .AddFormatValue("limit", new PyInt(limit));
        }
    }

    // check distance for targetable actions
    if (m_targetSE != nullptr) {
        // weapons use ships AttrMaxTargetRange which is checked in TargetMgr
        if (m_turret or m_launcher)
            return true;

        // test for specific targets and distance here
        float range(0.0f);
        using namespace EVEDB::invGroups;
        switch (groupID()) {
            case Tractor_Beam: {
                /** @todo  add checks for other items vs cap tractors and maybe some items for small tractors */
                bool allowed(false);
                if ( m_targetSE->IsWreckSE()) {
                    allowed = true;
                } else if (m_targetSE->IsContainerSE()) {
                    if (m_targetSE->GetContSE()->IsAnchored()) {
                        m_shipRef->GetPilot()->SendNotifyMsg("The %s is anchored.  It cannot be tractored.", m_targetSE->GetName());
                        return false;
                    }
                    allowed = true;
                }

                if (allowed) {
                    range = GetAttribute(AttrMaxRange).get_float();
                } else {
                    m_shipRef->GetPilot()->SendNotifyMsg("You cannot tractor the %s.", m_targetSE->GetName());
                    return false;
                }

                    // test for ownership here...wip
                    // once crim shit is implemented, allow for tractoring no matter owner.
                    bool owner(false), fleet(false), corp(false), ally(false), war(false);
                    if (m_targetSE->GetOwnerID() == m_shipRef->ownerID())
                        owner = true;
                    if (m_targetSE->GetCorporationID() == m_shipRef->GetPilot()->GetCorporationID())
                        corp = true;
                    if (m_targetSE->GetAllianceID() == m_shipRef->GetPilot()->GetAllianceID())
                        ally = true;
                    if (m_targetSE->GetWarFactionID() == m_shipRef->GetPilot()->GetWarFactionID())
                        war = true;
                    if (m_shipRef->GetPilot()->InFleet())
                        if (m_shipRef->GetPilot()->GetFleetID() == m_targetSE->GetFleetID())
                            fleet = true;

                    if (owner or fleet or corp or ally or war) {
                        m_targetSE->DestinyMgr()->TractorBeamStart(m_shipRef->GetPilot()->GetShipSE(), GetAttribute(AttrMaxTractorVelocity));
                    } else {
                        int id = m_targetSE->GetID ();

                        Clear ();

                        throw UserError ("InvalidTargetCanOwner")
                                .AddFormatValue ("module", new PyInt (id));
                    }
            } break;
            case Shield_Transporter: {
                range = GetAttribute(AttrShieldTransferRange).get_float();
            } break;
            case Energy_Vampire:
            case Energy_Transfer_Array: {
                range = GetAttribute(AttrPowerTransferRange).get_float();
            } break;
            case Energy_Destabilizer: {
                range = GetAttribute(AttrEnergyDestabilizationRange).get_float();
            } break;
            case Warp_Scrambler:
            case Warp_Disrupt_Field_Generator: {    // focused script (+50% range)
                range = GetAttribute(AttrWarpScrambleRange).get_float();
            } break;
            case ECM: {
                // jams can land in falloff.  the jam roll decides
                range = GetAttribute(AttrMaxRange).get_float() + (3 * GetAttribute(AttrFalloff).get_float());
            } break;
            case Remote_ECM_Burst: {
                range = GetAttribute(AttrMaxRange).get_float();
            } break;
            case Cargo_Scanner: {
                range = GetAttribute(AttrCargoScanRange).get_float();
            } break;
            case Super_Weapon: {
                // no range attribute:  the target only has to stay locked (checked in SuperWeapon::CanActivate)
                return true;
            }
            case Passive_Targeting_System: {
                // the ship's own targeting range
                range = m_shipRef->GetPilot()->GetShipSE()->TargetMgr()->GetMaxTargetRange(m_shipRef);
            } break;
            case Ship_Scanner: {
                // results thin out over the falloff (ShipScanResult)
                range = GetAttribute(AttrShipScanRange).get_float() + GetAttribute(AttrShipScanFalloff).get_float();
            } break;
            case Shield_Disruptor:      // no modules in this group
            case Stasis_Web:
            case Salvager:
            case Data_Miner:
            case Strip_Miner:
            case Mining_Laser:
            case Target_Painter:
            case Tracking_Disruptor:
            case Gas_Cloud_Harvester:
            case Remote_Sensor_Damper:
            case Remote_Sensor_Booster:
            case Armor_Repair_Projector:
            case Frequency_Mining_Laser:  {
                range = GetAttribute(AttrMaxRange).get_float();
            } break;

            // these scanners *may* not hit here, as they dont need a target, and we really dont want them to test for target
            //  the pilot *may* have a target locked, in which case we really dont want these to hit
            /*
            case System_Scanner: {
                range = GetAttribute(AttrScanRange).get_float();    // this is in AU
                range *= oneauinmeters;
            } break;
            case Survey_Scanner: {
                range = GetAttribute(AttrSurveyScanRange).get_float();
            } break;
            'OnShipScanCompleted',
            'OnJamStart',
            'OnJamEnd',
            'OnCargoScanComplete',
            */
            default: {
                // make error here with group
                range = GetAttribute(AttrMaxRange).get_float();
                _log(SHIP__WARNING, "Activate::RangeTest - Default hit for %s(%u) group: %u.  Using %.1f", \
                            m_modRef->name(), m_modRef->itemID(), m_modRef->groupID(), range);
            } break;
        }

        float distance = m_shipRef->position().distance(m_targetSE->GetPosition());
        distance -= m_targetSE->GetRadius();

        _log(MODULE__MESSAGE, "Activate::RangeTest - distance between %s and target %s: %.1f.  range of %s is %.1f", \
                    m_shipRef->name(), m_targetSE->GetName(), distance, m_modRef->name(), range);

        if (distance > range) {
            m_shipRef->GetPilot()->SendNotifyMsg("The %s is %.0f meters from you, outside the effective range of your %s, which is %.0f meters.", \
                    m_targetSE->GetName(), distance, m_modRef->name(), range);
            return false;
        }
    }

    //AttrDeadspaceUnsafe
    //AttrMaxGroupActive
    //ModuleRequiresFuel
    return true;
}


void ActiveModule::ShowEffect(bool active/*false*/, bool abort/*false*/)
{
    if (m_effectID < 1)
        _log(EFFECTS__ERROR, "fxID = 0 for %s.", m_modRef->name());

    int64 abortTime(GetFileTimeNow());
    if (abort) {
        active = false;
        if ((m_effectID == EVEEffectID::miningLaser)
        or  (m_effectID == EVEEffectID::miningClouds)) {
            abortTime += (5 * EvE::Time::Second);    // delay abort for 5s to simulate module "completing" its' cycle and dumping ore to cargo
        } else {
            abortTime += (3 * EvE::Time::Second);    // delay abort for 3s to simulate module "completing" its' cycle
        }
    }

    // there may be others here like this...this is ONLY for OnSpecialFX data.  a launcher's graphic comes from its charge's
    //  default effect, but OnGodmaShipEffect below must name the launcher's own effect (useMissiles): that is what the
    //  client's module button tracks.  overwriting m_effectID here left the client thinking launchers never started, so
    //  they showed no cycle ring, couldn't be switched off, and every click started them again
    uint16 fxID(m_effectID);
    if ((fxID == EVEEffectID::useMissiles) and (m_chargeRef.get() != nullptr))   //operation defined by charge (use charge's default effectID)
        fxID = m_chargeRef->type().GetDefaultEffect();
    std::string guidStr = sFxDataMgr.GetEffectGuid(fxID);
    // the covert ops cloak's guid isn't in the client's effect repository.  it looks like any other cloak
    if (guidStr.compare("effects.CloakingCovertOps") == 0)
        guidStr = "effects.Cloaking";
    if (guidStr.empty())
        _log(EFFECTS__ERROR, "guid empty for %s using effectID %u", m_modRef->name(), fxID);

    uint16 chgTypeID(((m_chargeRef.get() != nullptr) ? m_chargeRef->typeID() : 0));
    uint32 timeLeft(GetRemainingCycleTimeMS());
    if ((timeLeft == 0) and IsLinked() and !IsMaster())
        timeLeft = m_slaveCycleMS;  // without a duration the client doesn't show this weapon firing
    bool isOffensive(sFxDataMgr.isOffensive(fxID));
    PyRep* graphicInfo(nullptr);
    if (groupID() == EVEDB::invGroups::Warp_Disrupt_Field_Generator) {
        if (IsValidTarget(m_targetID) and IsScriptedDisruptor()) {
            // the focused point shows as its script's effect (effects.WarpScramble -> tactical's WarpScrambledBy messages).
            //  m_effectID stays warpDisruptSphere, so the module button keeps showing active
            uint16 fxID(m_chargeRef->type().GetDefaultEffect());
            guidStr = sFxDataMgr.GetEffectGuid(fxID);
            isOffensive = sFxDataMgr.isOffensive(fxID);
        } else {
            // WarpDisruptFieldGenerating.py sizes the sphere from graphicInfo.range (else the type's base range, without hull bonuses)
            PyDict* dict = new PyDict();
                dict->SetItemString("range", new PyFloat(GetAttribute(AttrWarpScrambleRange).get_double()));
            graphicInfo = new PyObject("util.KeyVal", dict);
        }
    }

    if (m_destinyMgr != nullptr) {
        m_destinyMgr->SendSpecialEffect(
                m_shipRef->itemID(),
                m_modRef->itemID(),
                m_modRef->typeID(),
                IsValidTarget(m_targetID) ? m_targetID : m_shipRef->itemID(),
                chgTypeID,
                guidStr,
                isOffensive,
                active,         // start    - if (start = 0) THEN remove effect
                active,         // active   - if (start and active) THEN starting ONE-SHOT event of (duration)  (dunno what 'ONE-SHOT event' is)
                timeLeft,       // duration in ms
                m_repeat,       // repeat   - if (repeat > 0) THEN starting REPEAT event  ELSE (repeat == 0) THEN starting TOGGLE event
                graphicInfo);
    } else {
        PySafeDecRef(graphicInfo);
    }


    // Create Destiny Updates and GFx
    GodmaEnvironment ge;
        ge.selfID = m_modRef->itemID();         //ENV_IDX_SELF = 0
        ge.charID = m_shipRef->ownerID();       //ENV_IDX_CHAR = 1
        ge.shipID = m_shipRef->itemID();        //ENV_IDX_SHIP = 2
        ge.target = IsValidTarget(m_targetID) ? new PyInt(m_targetID) : PyStatic.NewNone();     //ENV_IDX_TARGET = 3
        ge.area = new PyList();                 //ENV_IDX_AREA = 5 still dont know what this is.
        ge.effectID = m_effectID;               //ENV_IDX_EFFECT = 6

    if (chgTypeID > 0) {
        GodmaSubLoc gsl;  // subLocation is for charges loaded into modules on ship
            gsl.shipID = ge.shipID;
            gsl.slotID = m_modRef->flag();
            gsl.typeID = chgTypeID;
        ge.subLoc = gsl.Encode();         //ENV_IDX_OTHER = 4
    } else {
        ge.subLoc = PyStatic.NewNone();  //ENV_IDX_OTHER = 4
    }

    timeLeft /= 1000;
    //def OnGodmaShipEffect(self, itemID, effectID, t, start, active, environment, startTime, duration, repeat, randomSeed, error, actualStopTime = None, stall = True):
    Notify_OnGodmaShipEffect shipEff;
        shipEff.itemID = ge.selfID;
        shipEff.effectID = ge.effectID;
        shipEff.timeNow = GetFileTimeNow();
        shipEff.start = (active ? 1 : 0);
        shipEff.active = (active ? 1 : 0);
        shipEff.environment = ge.Encode();
        shipEff.startTime = (abort ? (abortTime / EvE::Time::Second) : shipEff.timeNow - (timeLeft * EvE::Time::Second));
        shipEff.duration = (abort ? 2000 : timeLeft);  // duration in seconds
        shipEff.repeat = m_repeat;
        // results shown as the effect stops (salvagers and data miners:  SalvagingSuccess, LockedContainerOpened...)
        PyTuple* stopMsg(active ? nullptr : TakeStopMessage());
        if (stopMsg != nullptr) {
            shipEff.error = stopMsg;
        } else if (m_needsTarget and (m_targetSE == nullptr)) {
            /*   these both give client warning -  [no messageID: 258855]
            if (IsValidTarget(m_targetID)) {
                PyDict* dict = new PyDict();
                    dict->SetItemString("moduleID", new PyInt(m_modRef->itemID()));
                    dict->SetItemString("targetID", new PyInt(m_targetID));
                PyTuple* tuple = new PyTuple(2);
                    tuple->SetItem(0, new PyString("TargetNoLongerPresent"));
                    tuple->SetItem(1, dict);
                shipEff.error = tuple;
            } else {
                PyDict* dict = new PyDict();
                    dict->SetItemString("moduleID", new PyInt(m_modRef->itemID()));
                PyTuple* tuple = new PyTuple(2);
                    tuple->SetItem(0, new PyString("TargetNoLongerPresentGeneric"));
                    tuple->SetItem(1, dict);
                shipEff.error = tuple;
            }
            // this one doesnt work, either.
            PyDict* dict = new PyDict();
                dict->SetItemString("moduleID", new PyInt(m_modRef->itemID()));
            PyTuple* tuple = new PyTuple(2);
                tuple->SetItem(0, new PyString("TargetNoLongerPresentGeneric"));
                tuple->SetItem(1, dict);
            shipEff.error = PyStatic.NewNone();
            m_shipRef->GetPilot()->SendNotification("TargetNoLongerPresentGeneric", "charid", &tuple);
            */
            m_targetID = 0;
            // this is wrong....need to find error msg and insert here, but client throws error on above msgs
            shipEff.error = PyStatic.NewNone();
        /*
         * {'messageKey': 'TargetNoLongerPresent', 'dataID': 17881666, 'suppressable': False, 'bodyID': 258855, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1626}
         * u'TargetNoLongerPresentBody'}(u'{[item]moduleID.name} deactivates as the {[item]targetID.name} it was targeted at is no longer present.', None, {u'{[item]moduleID.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'moduleID'}, u'{[item]targetID.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'targetID'}})
         * {'messageKey': 'TargetNoLongerPresentGeneric', 'dataID': 17875297, 'suppressable': False, 'bodyID': 256459, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3742}
         * u'TargetNoLongerPresentGenericBody'}(u'{[item]moduleID.name} deactivates as the item it was targeted at is no longer present.', None, {u'{[item]moduleID.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'moduleID'}})
         *
         */
        } else {
            shipEff.error = PyStatic.NewNone();
        }

    PyTuple* tuple = shipEff.Encode();
    if (is_log_enabled(EFFECTS__DUMP))
        tuple->Dump(EFFECTS__DUMP, "");
    SystemBubble* pBubble(Bubble());
    if ((m_destinyMgr == nullptr)
    or  (pBubble == nullptr)
    or   m_destinyMgr->IsWarping()) {
        m_shipRef->GetPilot()->QueueDestinyEvent(&tuple);
    } else {
        pBubble->BubblecastDestinyEvent(&tuple, "destiny");
    }
}
/*
                  [PyTuple 12 items]
                    [PyString "OnGodmaShipEffect"]
                    [PyIntegerVar 1005902575207]
                    [PyInt 27]
                    [PyIntegerVar 129756563776224944]
                    [PyInt 0]
                    [PyInt 0]
                    [PyList 7 items]
                      [PyIntegerVar 1005902575207]
                      [PyIntegerVar 649670823]
                      [PyIntegerVar 1005885567714]
                      [PyNone]
                      [PyNone]
                      [PyList 0 items]
                      [PyInt 27]
                    [PyIntegerVar 129756563680133184]
                    [PyFloat 9600]
                    [PyBool True]
                    [PyNone]
                    [PyTuple 2 items]
                      [PyString "NotEnoughEnergy"]
                      [PyDict 3 kvp]
                        [PyString "need"]
                        [PyFloat 160]
                        [PyString "got"]
                        [PyFloat 99.9814376484626]
                        [PyString "effectname"]
                        [PyTuple 2 items]
                          [PyInt 4]
                          [PyInt 3530]
                          */

void ActiveModule::LaunchMissile()
{
    // must not throw here...
    //throw UserError ("TargetingMissileToSelf");
    //throw UserError ("NoCharges");

    //{'FullPath': u'UI/Messages', 'messageID': 259200, 'label': u'NoChargesBody'}(u'{launcher} has run out of charges', None, {u'{launcher}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'launcher'}})

    // AttrAimedLaunch ?  what is this used for?

    // Launch a missile, creating a new Destiny object for it
    Client* pClient = m_shipRef->GetPilot();
    if (pClient == nullptr)
        return;
    if ((m_chargeRef.get() != nullptr) and (m_chargeRef->groupID() == EVEDB::invGroups::Defender_Missile)) {
        LaunchDefender();
        return;
    }
    if (groupID() == EVEDB::invGroups::Missile_Launcher_Bomb) {
        LaunchBomb();
        return;
    }
    // the launcher ran out (or was unloaded) since it was activated.  it stops at the end of this cycle
    if ((m_chargeRef.get() == nullptr) or (m_targetSE == nullptr)) {
        m_Stop = true;
        return;
    }
    ItemData idata(m_chargeRef->typeID(), pClient->GetCharacterID(), pClient->GetLocationID(), flagMissile, m_chargeRef->name(), m_shipRef->position() );
    InventoryItemRef missileRef = sItemFactory.SpawnItem(idata);
    if (missileRef.get() == nullptr) {
        _log(ITEM__ERROR ,"Unable to spawn item #%u:'%s' of type %u.", m_chargeRef->itemID(), m_chargeRef->name(), m_chargeRef->typeID());
        pClient->SendErrorMsg("Your %s in %s experienced a loading error and was disabled.", m_chargeRef->name(), m_modRef->name());
        AbortCycle();
        return;
    }

    /* the missile is a fresh item of the charge's type.  it flies and hits with the loaded charge's attributes,
     *  which carry the pilot's skill, implant, hull, rig and subsystem bonuses (owner-required-skill modifiers)
     */
    static const uint16 missileAttrs[] = {
        AttrEmDamage, AttrExplosiveDamage, AttrKineticDamage, AttrThermalDamage, AttrExplosionDelay, AttrMaxVelocity,
        AttrAoeCloudSize, AttrAoeVelocity, AttrAoeFalloff, AttrAoeDamageReductionFactor, AttrAoeDamageReductionSensitivity,
        AttrHP, AttrSignatureRadius, AttrDetonationRange };
    for (auto attrID : missileAttrs)
        if (m_chargeRef->HasAttribute(attrID))
            missileRef->SetAttribute(attrID, m_chargeRef->GetAttribute(attrID), false);

    SystemManager* pSystem = pClient->SystemMgr();
    Missile* pMissile = new Missile(missileRef, pSystem->GetServiceMgr(), pSystem, m_modRef, m_targetSE, m_shipRef->GetPilot()->GetShipSE(), this);
    if (pMissile == nullptr) {
        _log(ITEM__ERROR ,"Unable to create SE #%u:'%s' of type %u.", m_chargeRef->itemID(), m_chargeRef->name(), m_chargeRef->typeID());
        pClient->SendErrorMsg("Your %s in %s experienced a launching error and was disabled.", m_chargeRef->name(), m_modRef->name());
        AbortCycle();
        return;
    }

    // the missile flies itself (see DestinyManager).  it hits when its path reaches the target
    pMissile->SetSpeed(pMissile->GetSelf()->GetAttribute(AttrMaxVelocity).get_float());
    pMissile->DestinyMgr()->MakeMissile(pMissile);

    // Reduce ammo charge by 1 unit:
    ConsumeCharge();

    // tell target a missile has been launched at them.. (defender missile trigger for ship, tower, pos, npc, others?)
    m_targetSE->MissileLaunched(pMissile);

    // add data to StatisticMgr
    sStatMgr.Increment(Stat::pcMissiles);
}

void ActiveModule::LaunchBomb()
{
    Client* pClient(m_shipRef->GetPilot());
    if (pClient == nullptr)
        return;
    ShipSE* pShip(pClient->GetShipSE());
    if ((m_chargeRef.get() == nullptr) or (pShip == nullptr) or (pShip->SysBubble() == nullptr) or !Bomb::IsBombGroup(m_chargeRef->groupID())) {
        m_Stop = true;
        return;
    }
    ItemData idata(m_chargeRef->typeID(), pClient->GetCharacterID(), pClient->GetLocationID(), flagMissile, m_chargeRef->name(), m_shipRef->position());
    InventoryItemRef bombRef(sItemFactory.SpawnItem(idata));
    if (bombRef.get() == nullptr) {
        _log(ITEM__ERROR, "Unable to spawn bomb of type %u.", m_chargeRef->typeID());
        AbortCycle();
        return;
    }
    // the loaded bomb's attributes, with the pilot's skill bonuses (Bomb Deployment)
    static const uint16 bombAttrs[] = {
        AttrEmDamage, AttrExplosiveDamage, AttrKineticDamage, AttrThermalDamage, AttrExplosionDelay, AttrMaxVelocity,
        AttrAoeCloudSize, AttrExplosionRange, AttrEmpFieldRange, AttrHP, AttrArmorHP, AttrSignatureRadius,
        AttrArmorEmDamageResonance, AttrArmorExplosiveDamageResonance, AttrArmorKineticDamageResonance, AttrArmorThermalDamageResonance,
        AttrScanGravimetricStrengthBonus, AttrScanLadarStrengthBonus, AttrScanMagnetometricStrengthBonus, AttrScanRadarStrengthBonus,
        AttrEnergyDestabilizationAmount };
    for (auto attrID : bombAttrs)
        if (m_chargeRef->HasAttribute(attrID))
            bombRef->SetAttribute(attrID, m_chargeRef->GetAttribute(attrID), false);

    SystemManager* pSystem(pClient->SystemMgr());
    Bomb* pBomb(new Bomb(bombRef, pSystem->GetServiceMgr(), pSystem, m_modRef, pShip, this));
    pBomb->SetSpeed(bombRef->GetAttribute(AttrMaxVelocity).get_float());
    pBomb->DestinyMgr()->MakeMissile(pBomb);
    ConsumeCharge();
    sStatMgr.Increment(Stat::pcMissiles);
}

void ActiveModule::LaunchDefender()
{
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or (m_chargeRef.get() == nullptr))
        return;
    ShipSE* pShip(pClient->GetShipSE());
    if ((pShip == nullptr) or (pShip->SysBubble() == nullptr))
        return;

    // incoming missiles, and the ones our defenders are already after
    std::vector<Missile*> incoming;
    std::set<SystemEntity*> chased;
    pShip->SysBubble()->ForEachDynamic([&](SystemEntity* pSE) {
        if (!pSE->IsMissileSE() or pSE->IsDead())
            return;
        Missile* pMissile(pSE->GetMissileSE());
        if (!pMissile->IsAlive())
            return;
        if (pMissile->GetLauncherID() == pShip->GetID())
            chased.insert(pMissile->GetTargetSE());
        else if (pMissile->GetTargetSE() == pShip)
            incoming.push_back(pMissile);
    });
    Missile* pTarget(nullptr);
    double best(0.0);
    for (auto cur : incoming) {
        if (chased.find(cur) != chased.end())
            continue;
        double dist(pShip->GetPosition().distance(cur->GetPosition()));
        if ((pTarget == nullptr) or (dist < best)) {
            pTarget = cur;
            best = dist;
        }
    }
    if (pTarget == nullptr)
        return;

    ItemData idata(m_chargeRef->typeID(), pClient->GetCharacterID(), pClient->GetLocationID(), flagMissile, m_chargeRef->name(), m_shipRef->position());
    InventoryItemRef missileRef(sItemFactory.SpawnItem(idata));
    if (missileRef.get() == nullptr)
        return;
    static const uint16 missileAttrs[] = {
        AttrEmDamage, AttrExplosiveDamage, AttrKineticDamage, AttrThermalDamage, AttrExplosionDelay, AttrMaxVelocity, AttrHP };
    for (auto attrID : missileAttrs)
        if (m_chargeRef->HasAttribute(attrID))
            missileRef->SetAttribute(attrID, m_chargeRef->GetAttribute(attrID), false);

    SystemManager* pSystem(pClient->SystemMgr());
    Missile* pDefender(new Missile(missileRef, pSystem->GetServiceMgr(), pSystem, m_modRef, pTarget, pShip, this));
    pDefender->SetSpeed(missileRef->GetAttribute(AttrMaxVelocity).get_float());
    pDefender->DestinyMgr()->MakeMissile(pDefender);
    ConsumeCharge();
    _log(MODULE__MESSAGE, "%s launched a defender at %s(%u)", pClient->GetName(), pTarget->GetName(), pTarget->GetID());
}

void ActiveModule::LaunchProbe()
{
    Client* pClient = m_shipRef->GetPilot();
    if (pClient == nullptr)
        return;
    if (m_chargeRef.get() == nullptr) {
        m_Stop = true;
        return;
    }

    // survey probes fly to a moon and report its materials.  they aren't scanner probes
    if (m_chargeRef->groupID() == EVEDB::invGroups::Survey_Probe) {
        sExplorationMgr.LaunchSurveyProbe(pClient, m_shipRef, m_chargeRef);
        ConsumeCharge();
        sStatMgr.Increment(Stat::probesLaunched);
        return;
    }

    if (pClient->scan() == nullptr)
        pClient->SetScan(new Scan(pClient));

    // Astrometrics:  3 probes + one per level.  (CanActivate refuses the activation, this stops a repeating launcher)
    uint8 limit(3 + pClient->GetChar()->GetSkillLevel(EvESkill::Astrometrics));
    if (pClient->scan()->GetProbeCount() >= limit) {
        PyDict* args = new PyDict();
            args->SetItemString("probeTypeID", new PyInt(m_chargeRef->typeID()));
            args->SetItemString("limit", new PyInt(limit));
        pClient->SendRemoteMessage("NoProbeManagementAbilitiesLeft", args);
        m_Stop = true;
        return;
    }

    GPoint pos(m_shipRef->position());
    pos.MakeRandomPointOnSphere(MakeRandomFloat(500 + m_shipRef->radius(), 1500 + m_shipRef->radius()));

    //ItemData( uint32 _typeID, uint32 _ownerID, uint32 _locationID, EVEItemFlags _flag, uint32 _quantity);
    // we are not changing singleton status of probes
    ItemData idata(m_chargeRef->typeID(), pClient->GetCharacterID(), pClient->GetLocationID(), flagNone, 1);
    ProbeItemRef probeRef = sItemFactory.SpawnProbe(idata);
    if (probeRef.get() == nullptr) {
        // must not throw here:  this runs in the module's cycle too
        _log(ITEM__ERROR, "Unable to spawn item #%u:'%s' of type %u.", m_chargeRef->itemID(), m_chargeRef->name(), m_chargeRef->typeID());
        pClient->SendErrorMsg("Your %s in %s experienced a launching error and was disabled.", m_chargeRef->name(), m_modRef->name());
        m_Stop = true;
        return;
    }

    probeRef->SetPosition(pos);
    SystemManager* pSystem = pClient->SystemMgr();
    ProbeSE* pProbe = new ProbeSE(probeRef, pSystem->GetServiceMgr(), pSystem, m_modRef, m_shipRef);
    if (pProbe == nullptr)
        return; // make error here

    pProbe->SendNewProbe();
    pSystem->AddEntity(pProbe, false);
    pClient->scan()->AddProbe(pProbe);

    // Reduce ammo charge by 1 unit:
    ConsumeCharge();

    // add data to StatisticMgr
    sStatMgr.Increment(Stat::probesLaunched);
}

void ActiveModule::LaunchInterdictionSphere()
{
    // must not throw here (called from the module cycle)
    Client* pClient = m_shipRef->GetPilot();
    if ((pClient == nullptr) or (m_chargeRef.get() == nullptr))
        return;
    SystemManager* pSystem = pClient->SystemMgr();
    if (pSystem == nullptr)
        return;

    // the sphere is a temp item (flagMissile -> temp itemID, not saved).  it doesn't move, so it sits where the ship launched it
    ItemData idata(m_chargeRef->typeID(), pClient->GetCharacterID(), pClient->GetLocationID(), flagMissile, m_chargeRef->name(), m_shipRef->position());
    InventoryItemRef sRef = sItemFactory.SpawnItem(idata);
    if (sRef.get() == nullptr) {
        _log(ITEM__ERROR, "Unable to spawn item #%u:'%s' of type %u.", m_chargeRef->itemID(), m_chargeRef->name(), m_chargeRef->typeID());
        pClient->SendErrorMsg("Your %s in %s experienced a loading error and was disabled.", m_chargeRef->name(), m_modRef->name());
        AbortCycle();
        return;
    }

    FactionData data = FactionData();
        data.ownerID = pClient->GetCharacterID();
        data.corporationID = pClient->GetCorporationID();
        data.allianceID = pClient->GetAllianceID();
        data.factionID = pClient->GetWarFactionID();

    // explosionDelay is the sphere's life (120s)
    uint32 lifeMS(m_chargeRef->type().HasAttribute(AttrExplosionDelay) ? m_chargeRef->type().GetAttribute(AttrExplosionDelay).get_uint32() : 120000);
    InterdictionSphereSE* pSE = new InterdictionSphereSE(sRef, pSystem->GetServiceMgr(), pSystem, data, lifeMS);
    pSE->SetPosition(m_shipRef->position());
    pSystem->AddEntity(pSE, false);

    double range(m_chargeRef->GetAttribute(AttrWarpScrambleRange).get_double());
    pSystem->GetInterdictionMgr()->AddField(pSE, range, Interdiction::Source::Probe, sRef->typeID());

    _log(MODULE__MESSAGE, "%s launched %s(%u) in %s: %.0fm warp disruption field for %us", m_shipRef->name(), pSE->GetName(), pSE->GetID(),
            pSystem->GetName(), range, lifeMS / 1000);

    // Reduce ammo charge by 1 unit:
    ConsumeCharge();
}

bool ActiveModule::IsScriptedDisruptor()
{
    return ((groupID() == EVEDB::invGroups::Warp_Disrupt_Field_Generator)
        and (m_chargeRef.get() != nullptr)
        and (m_chargeRef->groupID() == EVEDB::invGroups::Warp_Disrupt_Field_Script));
}

void ActiveModule::StartDisruptField()
{
    ShipSE* pShipSE(m_shipRef->HasPilot() ? m_shipRef->GetPilot()->GetShipSE() : nullptr);
    if ((pShipSE == nullptr) or (pShipSE->SystemMgr() == nullptr))
        return;
    if (m_fieldActive) {
        if (pShipSE->SystemMgr()->GetInterdictionMgr()->HasField(pShipSE->GetID()))
            return;
        /* the ship docked while the field was up: docking stops the module without a DeactivateCycle() and resets the ship's
         *  and modules' attributes, and leaving space removed the field.  only the bookkeeping is left over
         */
        m_fieldActive = false;
    }

    FieldPenalties(true);
    // the field is centred on (and moves with) the ship.  messages name the HIC's hull
    pShipSE->SystemMgr()->GetInterdictionMgr()->AddField(pShipSE, GetAttribute(AttrWarpScrambleRange).get_double(),
            Interdiction::Source::Generator, m_shipRef->typeID());
    m_fieldActive = true;
}

void ActiveModule::StopDisruptField()
{
    if (!m_fieldActive)
        return;
    m_fieldActive = false;
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient != nullptr) and (pClient->SystemMgr() != nullptr))
        pClient->SystemMgr()->GetInterdictionMgr()->RemoveField(m_shipRef->itemID());
    // a docked ship's attributes were already reset (undocking rebuilds them), so only undo the penalties in space
    if ((pClient != nullptr) and pClient->IsInSpace())
        FieldPenalties(false);
}

void ActiveModule::FieldPenalties(bool add)
{
    /* "increases its signature radius and agility whilst penalizing the velocity bonus of any afterburner or microwarpdrive modules.
     *   It also prevents any friendly remote effects from being rendered to the parent ship."
     * every change here is undone exactly: multiplications are divided out again, additions subtracted.
     */
    Client* pClient(m_shipRef->GetPilot());
    if (pClient == nullptr)
        return;

    fxData data = fxData();
        data.action = FX::Action::Invalid;
        data.srcRef = m_modRef;
        data.fxSrc = FX::Source::Self;
        data.targLoc = FX::Target::Ship;
    std::vector<fxData> mods;
    // signature radius +50%
    data.math = FX::Math::PostPercent;
    data.targAttr = AttrSignatureRadius;
    data.srcAttr = AttrSignatureRadiusBonus;
    mods.push_back(data);
    // mass -80% (agility)
    data.targAttr = AttrMass;
    data.srcAttr = AttrMassBonusPercentage;
    mods.push_back(data);
    // no remote assistance (checked by value on the target)
    data.math = FX::Math::ModAdd;
    data.targAttr = AttrDisallowAssistance;
    data.srcAttr = AttrDisallowAssistance;
    mods.push_back(data);
    /* prop mods (warpDisruptionFieldEffectOnline):  speed factor -90%, thrust -80%.
     *  their mass addition shrinks with the hull, or switching them during the field would leave the mass wrong */
    data.math = FX::Math::PostPercent;
    data.fxSrc = FX::Source::Group;
    for (uint16 grpID : {EVEDB::invGroups::Afterburner, EVEDB::invGroups::Microwarpdrive}) {
        data.grpID = grpID;
        data.targAttr = AttrMassAddition;
        data.srcAttr = AttrMassBonusPercentage;
        mods.push_back(data);
        data.targAttr = AttrSpeedFactor;
        data.srcAttr = AttrSpeedFactorBonus;
        mods.push_back(data);
        data.targAttr = AttrSpeedBoostFactor;
        data.srcAttr = AttrSpeedBoostFactorBonus;
        mods.push_back(data);
    }

    for (auto& cur : mods) {
        if (add) {
            m_modRef->AddModifier(cur);
        } else {
            m_modRef->RemoveModifier(cur);
        }
    }

    sFxProc.ApplyEffects(m_modRef.get(), pClient->GetChar().get(), m_shipRef.get(), true);

    /* a prop mod running now gets its velocity bonus worked out again from the new speed factor, thrust and mass
     *  (net -90%:  0.1 x 0.2 / 0.2).  the others only work it out when they start */
    std::vector<InventoryItemRef> modules;
    m_shipRef->GetModuleManager()->GetModuleListOfRefsAsc(modules);
    for (auto modRef : modules) {
        GenericModule* pMod(m_shipRef->GetModuleManager()->GetModule(modRef->itemID()));
        if ((pMod == nullptr) or !pMod->IsActive() or (pMod->GetActiveModule() == nullptr)
        or !pMod->GetActiveModule()->IsPropulsionModule())
            continue;
        pMod->GetActiveModule()->RefreshPropBoost();
    }

    // resend mass, agility and speed
    if ((pClient->GetShipSE() != nullptr) and (pClient->GetShipSE()->DestinyMgr() != nullptr))
        pClient->GetShipSE()->DestinyMgr()->SpeedBoost(!add);
}

bool ActiveModule::IsPropulsionModule() const
{
    return ((m_modRef->groupID() == EVEDB::invGroups::Afterburner) or (m_modRef->groupID() == EVEDB::invGroups::Microwarpdrive));
}

double ActiveModule::PropSpeedMultiplier(double shipMass)
{
    return Propulsion::SpeedMultiplier(GetAttribute(AttrSpeedFactor).get_double(), GetAttribute(AttrSpeedBoostFactor).get_double(), shipMass);
}

void ActiveModule::RefreshPropBoost()
{
    if (!IsPropulsionModule() or !IsActive() or !m_shipRef->HasPilot())
        return;
    // running, so the ship's mass has our massAddition on it already
    double oldMult(GetAttribute(AttrSpeedBoostFactorCalc2).get_double());
    double newMult(PropSpeedMultiplier(m_shipRef->GetAttribute(AttrMass).get_double()));
    if ((oldMult <= 0.0) or (std::fabs(newMult - oldMult) < 1e-9))
        return;
    // swap our multiplier on the ship.  the effect divides the new one out again when the module stops
    double maxVelocity(m_shipRef->GetAttribute(AttrMaxVelocity).get_double());
    m_shipRef->SetAttribute(AttrMaxVelocity, maxVelocity / oldMult * newMult, true);
    SetAttribute(AttrSpeedBoostFactorCalc2, newMult, false);
    _log(MODULE__TRACE, "ActiveModule::RefreshPropBoost - %s(%u) velocity multiplier %.4f -> %.4f.", m_modRef->name(), itemID(), oldMult, newMult);

    ShipSE* pShipSE(m_shipRef->GetPilot()->GetShipSE());
    if ((pShipSE != nullptr) and (pShipSE->DestinyMgr() != nullptr))
        pShipSE->DestinyMgr()->SpeedBoost();
}

bool ActiveModule::IsActivationBlocked()
{
    // only modules that need High Speed Maneuvering (microwarpdrives) are blocked
    if (!m_modRef->HasReqSkill(EvESkill::HighSpeedManeuvering) or !m_shipRef->HasPilot())
        return false;
    ShipSE* pShipSE(m_shipRef->GetPilot()->GetShipSE());
    if ((pShipSE == nullptr) or (pShipSE->TargetMgr() == nullptr) or !pShipSE->TargetMgr()->HasActiveModules())
        return false;
    std::vector<ActiveModule*> modules;
    pShipSE->TargetMgr()->GetTargetModules(modules);
    for (auto pMod : modules)
        if ((pMod != nullptr) and pMod->IsActive()
        and pMod->GetSelf()->type().HasEffect(EVEEffectID::warpScrambleTargetMWDBlockActivation))
            return true;
    return false;
}

void ActiveModule::BlockTargetMWDs()
{
    // player ships only:  npcs work their own "mwd" out (NpcMovement::CheckMWDBlock)
    if ((m_targetSE == nullptr) or !m_targetSE->IsShipSE())
        return;
    ShipItemRef targetShip(m_targetSE->GetShipSE()->GetShipItemRef());
    if ((targetShip.get() == nullptr) or !targetShip->HasPilot())
        return;
    std::vector<InventoryItemRef> modules;
    targetShip->GetModuleManager()->GetModuleListByReqSkill(EvESkill::HighSpeedManeuvering, modules);
    for (auto modRef : modules) {
        GenericModule* pMod(targetShip->GetModuleManager()->GetModule(modRef->itemID()));
        if ((pMod == nullptr) or !pMod->IsActive() or !pMod->IsActiveModule())
            continue;
        _log(MODULE__TRACE, "ActiveModule::BlockTargetMWDs - %s(%u) shuts down %s(%u) on %s.", m_modRef->name(), itemID(),
                modRef->name(), modRef->itemID(), m_targetSE->GetName());
        pMod->AbortCycle();
    }
}

void ActiveModule::LaunchSnowBall()
{
    // not used yet
}
