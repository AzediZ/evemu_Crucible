 /**
  * @name CynoModule.cpp
  *   Cynosural field generator module class
  * @Author: James
  * @date:   8 October 2021
  */

#include "ship/JumpDrive.h"
#include "ship/modules/CynoModule.h"
#include "system/SystemManager.h"
#include "fleet/FleetService.h"
#include "pos/Tower.h"
#include "system/sov/SovereigntyDataMgr.h"

CynoModule::CynoModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef),
pClient(nullptr),
pShipSE(nullptr),
cSE(nullptr),
m_firstRun(true),
m_shipVelocity(0.0f)
{
    /* the field lasts one cycle of the module (10 minutes), which no skill changes.
     *  (this used to add 3% a level of Survey, copied from the survey scanner code)
     */
}

void CynoModule::Activate(uint16 effectID, uint32 targetID, int16 repeat)
{
    // the pilot can change since the module was loaded (relog, someone else boarding the ship)
    pClient = m_shipRef->GetPilot();
    if (pClient == nullptr)
        return;
    pShipSE = pClient->GetShipSE();
    if (pShipSE == nullptr)
        return;

    m_firstRun = true;
    ActiveModule::Activate(effectID, targetID, repeat);

    // check to see if Activate() was denied.
    if (m_Stop)
        return;

    _log(MODULE__DEBUG, "Cynosural field generator activated by %s in %s", pClient->GetName(), m_sysMgr->GetName());

    // hack to disable ship movement here
    m_shipVelocity = pShipSE->DestinyMgr()->GetMaxVelocity();
    pShipSE->DestinyMgr()->Stop();
    pShipSE->DestinyMgr()->SetFrozen(true);
}

void CynoModule::DeactivateCycle(bool abort)
{
    if (cSE != nullptr) {
        SendOnJumpBeaconChange(false);
        cSE->Delete();
        SafeDelete(cSE);
    }
    ActiveModule::DeactivateCycle(abort);

    // hack to reinstate ship movement here
    // may have to reset/reapply all fx for ship movement
    pShipSE->GetSelf()->SetAttribute(AttrMaxVelocity, m_shipVelocity);
    pShipSE->DestinyMgr()->SetFrozen(false);
}

bool CynoModule::CanActivate()
{
    if ((pClient == nullptr) or (pShipSE == nullptr))
        return false;

    if (!pClient->InFleet())
        throw UserError("CynoMustBeInFleet");

    if (pShipSE->SysBubble()->HasTower()) {
        TowerSE* ptSE = pShipSE->SysBubble()->GetTowerSE();
        if (ptSE->HasForceField())
            if (pShipSE->GetPosition().distance(ptSE->GetPosition()) < ptSE->GetSOI())
                throw UserError("NoCynoInPOSShields");
    }

    /** @todo check for active cyno jammer */

    //if (m_sysMgr->HasCynoJammer())
    //    throw UserError("CynosuralGenerationJammed");

    //Send message if the system is being jammed.  covert fields aren't stopped by jammers
    SovereigntyData sovData = svDataMgr.GetSovereigntyData(pClient->GetLocationID());
    if ((sovData.jammerID != 0) and !IsCovert()) {
        pClient->SendNotifyMsg("This system is currently being jammed.");
        return false;
    }

    //Make sure player is not in high-sec (configurable)
    if (!sConfig.world.highSecCyno) {
        // GetSecValue() is 1.1 minus the security rating (0.1 in a 1.0 system, 0.7 in a 0.4), so it refused low-sec and allowed
        //  high-sec.  use the security rating itself.  0.45 and up shows as 0.5 in the client, which is high-sec
        if (pClient->SystemMgr()->GetSystemSecurityRating() >= 0.45f) {
            pClient->SendNotifyMsg("This module may not be used in high security space.");
            return false;
        }
    }

    // all specific checks pass.  run generic checks in base class
    return ActiveModule::CanActivate();
}

uint32 CynoModule::DoCycle()
{
    // i really dont like this, but cant get it to work anywhere else...
    uint32 retVal(0);
    if (retVal = ActiveModule::DoCycle())
        if (m_firstRun) {
            m_firstRun = false;
            CreateCyno();
        }

    return retVal;
}

void CynoModule::CreateCyno()
{
    // do we already have a cyno field created?
    if (cSE != nullptr)
        return;

    // Create Cyno field here
    ItemData cData((IsCovert() ? EVEDB::invTypes::CovertCynosuralFieldI : EVEDB::invTypes::CynosuralFieldI), pClient->GetCharacterID(), m_sysMgr->GetID(), flagNone);
    InventoryItemRef cRef = sItemFactory.SpawnItem(cData);

    _log(MODULE__DEBUG, "Creating Cynosural field");

    cSE = new ItemSystemEntity(cRef, pClient->services(), m_sysMgr);
    GPoint location(pShipSE->GetPosition());
    location.MakeRandomPointOnSphere(1500.0f + cRef->type().radius());
    cSE->SetPosition(location);
    cRef->SaveItem();
    m_sysMgr->AddEntity(cSE);

    SendOnJumpBeaconChange(true);
}

void CynoModule::SendOnJumpBeaconChange(bool active/*false*/) {
    // tells the fleet, so members get "Jump to" (and "Bridge to") for this field.  members joining the fleet later are told
    //  when they join (JumpDrive::SendActiveBeacons)
    if ((pClient == nullptr) or (cSE == nullptr))
        return;
    JumpDrive::SendBeaconChange(pClient, cSE->GetSelf()->locationID(), GetFieldID(), active);
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
