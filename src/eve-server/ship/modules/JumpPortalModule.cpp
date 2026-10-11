 /**
  * @name JumpPortalModule.cpp
  *   Jump Portal Generator (titan and black ops bridges)
  *
  * @Author:        EVEmu Team
  * @date:          11 October 2026
  */

#include "eve-server.h"

#include "Client.h"
#include "ConsoleCommands.h"
#include "fleet/FleetService.h"
#include "ship/modules/JumpPortalModule.h"
#include "ship/modules/ModuleManager.h"
#include "system/DestinyManager.h"
#include "system/TargetManager.h"

JumpPortalModule::JumpPortalModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef),
m_bridge(BridgeData()),
m_maxVelocity(EvilZero),
m_bridgeRequested(false),
m_bridgeOpen(false)
{
}

JumpPortalModule::~JumpPortalModule()
{
    // the bridging ship is gone (destroyed, unloaded) with the portal open
    if (m_bridgeOpen and !sConsole.IsShutdown())
        sFltSvc.RemoveBridge(m_bridge.shipID);
}

int8 JumpPortalModule::GetHarmonics()
{
    if (!HasAttribute(AttrJumpHarmonics))
        return 0;
    return static_cast<int8>(GetAttribute(AttrJumpHarmonics).get_int());
}

uint16 JumpPortalModule::GetPortalEffectID()
{
    // Covert Jump Portal Generator I: jumpPortalGenerationBO (effects.JumpPortalBO).  the titan's: jumpPortalGeneration (effects.JumpPortal)
    return (m_modRef->type().HasEffect(3674) ? 3674 : 2152);
}

bool JumpPortalModule::OpenBridge(const BridgeData& data)
{
    m_bridge = data;
    m_bridgeRequested = true;
    try {
        // through the module manager, so its warp/cloak/jump activation checks apply.  one cycle
        m_shipRef->GetModuleManager()->Activate(itemID(), GetPortalEffectID(), 0, 1);
    } catch (...) {
        m_bridgeRequested = false;
        throw;
    }
    m_bridgeRequested = false;
    return m_bridgeOpen;
}

bool JumpPortalModule::CanActivate()
{
    // the portal needs a destination:  the generator can't be started from the module button
    if (!m_bridgeRequested) {
        Clear();
        throw CustomError("Use the Bridge To menu to open a jump portal with your %s.", m_modRef->name());
    }
    return ActiveModule::CanActivate();
}

void JumpPortalModule::Activate(uint16 effectID, uint32 targetID/*0*/, int16 repeat/*0*/)
{
    EvilNumber maxVelocity(m_shipRef->GetAttribute(AttrMaxVelocity));

    ActiveModule::Activate(effectID, targetID, repeat);

    if ((m_ModuleState != Module::State::Activated) or !m_shipRef->HasPilot())
        return;

    Client* pClient(m_shipRef->GetPilot());
    ShipSE* pShip(pClient->GetShipSE());

    /* the generator holds the ship (speedFactor -100, siegeModeWarpStatus) while the portal is open.
     *  maxVelocity is put back by DeactivateCycle(), as a -100% modifier can't be undone */
    m_maxVelocity = maxVelocity;
    m_shipRef->SetAttribute(AttrMaxVelocity, maxVelocity, false);
    if (GetAttribute(AttrActivationTargetLoss).get_bool())
        pShip->TargetMgr()->LoseAllTargets();
    pShip->DestinyMgr()->Halt();
    pShip->DestinyMgr()->SetFrozen(true);

    m_bridgeOpen = true;
    sFltSvc.AddBridge(m_bridge);
    _log(MODULE__DEBUG, "%s opened a jump portal from %u to %u in %u", pClient->GetName(), m_bridge.fromSystemID, m_bridge.beaconID, m_bridge.toSystemID);
}

void JumpPortalModule::DeactivateCycle(bool abort/*false*/)
{
    if (m_ModuleState < Module::State::Deactivating)
        return;

    if (m_bridgeOpen) {
        m_bridgeOpen = false;
        sFltSvc.RemoveBridge(m_bridge.shipID);
    }

    ActiveModule::DeactivateCycle(abort);
    if (m_maxVelocity > EvilZero) {
        m_shipRef->SetAttribute(AttrMaxVelocity, m_maxVelocity);
        m_maxVelocity = EvilZero;
    }

    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or (pClient->GetShipSE() == nullptr) or (pClient->GetShipSE()->DestinyMgr() == nullptr))
        return;
    pClient->GetShipSE()->DestinyMgr()->SetFrozen(false);
    pClient->GetShipSE()->DestinyMgr()->SpeedBoost();
}
