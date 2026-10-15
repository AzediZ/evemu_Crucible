 /**
  * @name CynoModule.cpp
  *   Cynosural field generator module class
  * @Author: James
  * @date:   8 October 2021
  */

#include "ConsoleCommands.h"
#include "ship/modules/CynoModule.h"
#include "system/CrimeWatch.h"
#include "system/SystemManager.h"
#include "fleet/FleetService.h"
#include "pos/Tower.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/cosmicMgrs/IncursionMgr.h"

CynoFieldSE::CynoFieldSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data, uint32 charID, uint32 lifeMS)
: ItemSystemEntity(self, services, system),
m_charID(charID),
m_lifeTimer(lifeMS)
{
    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;
    m_lifeTimer.Start(lifeMS);
}

CynoFieldSE::~CynoFieldSE()
{
    // the fleet can't jump to a field that's gone (no-op when the generator already removed it)
    if (!sConsole.IsShutdown())
        sFltSvc.RemoveBeacon(m_charID, GetID());
}

void CynoFieldSE::Process()
{
    if (m_lifeTimer.Check(false)) {
        m_lifeTimer.Disable();
        _log(MODULE__DEBUG, "CynoFieldSE::Process() - %s(%u) expired.", GetName(), GetID());
        sFltSvc.RemoveBeacon(m_charID, GetID());
        // removes the ball and the temp item
        Delete();
        delete this;
        return;
    }

    SystemEntity::Process();
}

void CynoFieldSE::MakeDamageState(DoDestinyDamageState& into)
{
    SystemEntity::MakeDamageState(into);
}

CynoModule::CynoModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef),
m_fieldID(0),
m_systemID(0),
m_charID(0),
m_firstRun(true)
{
    // Cynosural Field Theory reduces the liquid ozone per cycle through dogma (consumptionQuantity).  nothing to do here
}

void CynoModule::Activate(uint16 effectID, uint32 targetID, int16 repeat)
{
    EvilNumber maxVelocity(m_shipRef->GetAttribute(AttrMaxVelocity));

    m_firstRun = true;
    ActiveModule::Activate(effectID, targetID, repeat);

    // check to see if Activate() was denied.
    if ((m_ModuleState != Module::State::Activated) or !m_shipRef->HasPilot())
        return;

    Client* pClient(m_shipRef->GetPilot());
    ShipSE* pShipSE(pClient->GetShipSE());
    _log(MODULE__DEBUG, "Cynosural field generator activated by %s in %s", pClient->GetName(), m_sysMgr->GetName());

    /* the field holds the ship in place (speedFactor -100, siegeModeWarpStatus) until the cycle ends.
     *  maxVelocity is put back by DeactivateCycle(), as a -100% modifier can't be undone */
    m_shipRef->SetAttribute(AttrMaxVelocity, maxVelocity, false);
    pShipSE->DestinyMgr()->Halt();
    pShipSE->DestinyMgr()->SetFrozen(true);
}

void CynoModule::DeactivateCycle(bool abort)
{
    if (m_ModuleState < Module::State::Deactivating)
        return;

    RemoveCyno();

    EvilNumber maxVelocity(m_shipRef->GetAttribute(AttrMaxVelocity));
    ActiveModule::DeactivateCycle(abort);
    m_shipRef->SetAttribute(AttrMaxVelocity, maxVelocity);

    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or (pClient->GetShipSE() == nullptr) or (pClient->GetShipSE()->DestinyMgr() == nullptr))
        return;
    pClient->GetShipSE()->DestinyMgr()->SetFrozen(false);
    pClient->GetShipSE()->DestinyMgr()->SpeedBoost();
}

bool CynoModule::CanActivate()
{
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or (pClient->GetShipSE() == nullptr) or (pClient->SystemMgr() == nullptr))
        return false;
    ShipSE* pShipSE(pClient->GetShipSE());
    SystemManager* pSystem(pClient->SystemMgr());

    try {
        if (!pClient->InFleet())
            throw UserError("CynoMustBeInFleet");

        // no cynos in wormhole space
        if (IsWSpaceID(pSystem->GetID()))
            throw CustomError("Cynosural fields cannot be established in wormhole space.");

        //Make sure player is not in high-sec (configurable)
        if (!sConfig.world.highSecCyno and (CrimeWatch::SecurityClass(pSystem->GetSystemSecurityRating()) == Crime::SecClass::High))
            throw CustomError("This module may not be used in high security space.");

        // a system cynosural field jammer blocks normal fields.  covert fields aren't affected
        if (!IsCovert()) {
            SovereigntyData sovData = svDataMgr.GetSovereigntyData(pSystem->GetID());
            if (sovData.jammerID != 0)
                throw UserError("CynosuralGenerationJammed");
            // so is a Sansha incursion's whole constellation
            if (sIncursionMgr.PenaltyRole(pSystem->GetID()) != 0)
                throw UserError("CynosuralGenerationJammed");
        }

        if ((pShipSE->SysBubble() != nullptr) and pShipSE->SysBubble()->HasTower()) {
            TowerSE* ptSE = pShipSE->SysBubble()->GetTowerSE();
            if (ptSE->InForceField(pShipSE->GetPosition()))
                throw UserError("NoCynoInPOSShields");
        }

        // maxVelocityActivationLimit (500 m/s)
        if (HasAttribute(AttrMaxVelocityActivationLimit) and (pShipSE->DestinyMgr() != nullptr))
            if (pShipSE->DestinyMgr()->GetSpeed() > GetAttribute(AttrMaxVelocityActivationLimit).get_double())
                throw CustomError("You are moving too fast to activate the %s.", m_modRef->name());
    } catch (...) {
        Clear();
        throw;
    }

    // all specific checks pass.  run generic checks in base class
    return ActiveModule::CanActivate();
}

uint32 CynoModule::DoCycle()
{
    uint32 retVal(ActiveModule::DoCycle());
    if (retVal == 0)
        return retVal;

    if (m_firstRun) {
        m_firstRun = false;
        CreateCyno();
    } else if ((m_fieldID != 0) and (m_sysMgr != nullptr)) {
        // another cycle keeps the field up
        CynoFieldSE* pField(dynamic_cast<CynoFieldSE*>(m_sysMgr->GetSE(m_fieldID)));
        if (pField != nullptr)
            pField->Extend(retVal + 1000);
    }

    return retVal;
}

void CynoModule::CreateCyno()
{
    // do we already have a cyno field created?
    if (m_fieldID != 0)
        return;
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or (pClient->GetShipSE() == nullptr) or (m_sysMgr == nullptr))
        return;
    ShipSE* pShipSE(pClient->GetShipSE());

    // the field is a temp item (not saved).  covert generators light a covert field
    uint16 typeID(IsCovert() ? EVEDB::invTypes::CovertCynosuralFieldI : EVEDB::invTypes::CynosuralFieldI);
    GPoint location(pShipSE->GetPosition());
    const ItemType* pType(sItemFactory.GetType(typeID));
    location.MakeRandomPointOnSphere(1500.0f + (pType != nullptr ? pType->radius() : 0) + pShipSE->GetRadius());
    ItemData cData(typeID, pClient->GetCharacterID(), m_sysMgr->GetID(), flagNone, "", location);
    InventoryItemRef cRef = sItemFactory.SpawnTempItem(cData);
    if (cRef.get() == nullptr) {
        _log(MODULE__ERROR, "CynoModule::CreateCyno() - unable to spawn field type %u for %s", typeID, pClient->GetName());
        return;
    }
    sItemFactory.AddItem(cRef);

    FactionData data = FactionData();
        data.ownerID = pClient->GetCharacterID();
        data.corporationID = pClient->GetCorporationID();
        data.allianceID = pClient->GetAllianceID();
        data.factionID = pClient->GetWarFactionID();
    // the field lasts as long as this cycle (and is extended by the next one)
    uint32 lifeMS(GetAttribute(AttrDuration).get_uint32() + 1000);
    CynoFieldSE* cSE = new CynoFieldSE(cRef, pClient->services(), m_sysMgr, data, pClient->GetCharacterID(), lifeMS);
    cSE->SetPosition(location);
    m_sysMgr->AddEntity(cSE);

    m_fieldID = cSE->GetID();
    m_systemID = m_sysMgr->GetID();
    m_charID = pClient->GetCharacterID();

    _log(MODULE__DEBUG, "%s lit %s(%u) in %s", pClient->GetName(), cSE->GetName(), m_fieldID, m_sysMgr->GetName());

    // the fleet can jump (or bridge) to it now
    BeaconData bData = BeaconData();
        bData.charID = m_charID;
        bData.fleetID = pClient->GetFleetID();
        bData.systemID = m_systemID;
        bData.beaconID = m_fieldID;
        bData.moduleID = itemID();
        bData.harmonics = (HasAttribute(AttrJumpHarmonics) ? GetAttribute(AttrJumpHarmonics).get_int() : -5);
        bData.position = location;
    sFltSvc.AddBeacon(bData);
}

void CynoModule::RemoveCyno()
{
    if (m_fieldID == 0)
        return;

    sFltSvc.RemoveBeacon(m_charID, m_fieldID);
    // the field may already be gone (its own timer, system unload)
    SystemManager* pSystem(sEntityList.FindSystem(m_systemID));
    if (pSystem != nullptr) {
        SystemEntity* pSE(pSystem->GetSE(m_fieldID));
        if (pSE != nullptr) {
            pSE->Delete();
            SafeDelete(pSE);
        }
    }

    m_fieldID = 0;
    m_systemID = 0;
    m_charID = 0;
}

/*
 * {'messageKey': 'CynoMustBeInFleet', 'dataID': 17879144, 'suppressable': False, 'bodyID': 257897, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 257896, 'messageID': 2253}
 * {'messageKey': 'CynosuralGenerationJammed', 'dataID': 17880043, 'suppressable': False, 'bodyID': 258240, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2249}
 * {'messageKey': 'NoActiveBeacon', 'dataID': 17879973, 'suppressable': False, 'bodyID': 258214, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 258213, 'messageID': 2211}
 * {'messageKey': 'NoCynoInPOSShields', 'dataID': 17878872, 'suppressable': False, 'bodyID': 257791, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2339}
 *
 */

/*
 * {'FullPath': u'UI/Messages', 'messageID': 257896, 'label': u'CynoMustBeInFleetTitle'}(u'Cannot start Cynosural Field', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 257897, 'label': u'CynoMustBeInFleetBody'}(u'You must be in a fleet to activate your Cynosural Field.', None, None)
 *
 * {'FullPath': u'UI/Messages', 'messageID': 258213, 'label': u'NoActiveBeaconTitle'}(u'No Active Beacon', None, None)
 * {'FullPath': u'UI/Messages', 'messageID': 258214, 'label': u'NoActiveBeaconBody'}(u'You do not have an active Cynosural Field Beacon.', None, None)
 *
 * {'FullPath': u'UI/Messages', 'messageID': 258240, 'label': u'CynosuralGenerationJammedBody'}(u'You are unable to generate a cynosural field while there is a jammer present and active somewhere in this solar system.', None, None)
 *
 * {'FullPath': u'UI/Messages', 'messageID': 257791, 'label': u'NoCynoInPOSShieldsBody'}(u'You cannot set up a cynosural field within the force field of a Control Tower', None, None)
 */
