/**
 * @name JumpPortalModule.cpp
 *   jump portal generator module class (titan and black ops bridges)
 *
 * @Author:  AzediZ
 * @date:    11 October 2026
 */

#include "ship/modules/JumpPortalModule.h"
#include "system/DestinyManager.h"

JumpPortalModule::JumpPortalModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef),
m_beacon(JumpDrive::Beacon()),
m_open(false)
{
}

uint16 JumpPortalModule::PortalEffect()
{
    // the portal effect isn't the module's default effect in the data (the module button doesn't open a portal), so
    //  GetDefaultEffect() is 0 here.  that sent the effect as 0, so the client showed no portal and threw on it
    if (m_modRef->type().HasEffect(EVEEffectID::jumpPortalGenerationBO))
        return EVEEffectID::jumpPortalGenerationBO;
    return EVEEffectID::jumpPortalGeneration;
}

bool JumpPortalModule::IsCovert()
{
    return (PortalEffect() == EVEEffectID::jumpPortalGenerationBO);
}

void JumpPortalModule::Activate(uint16 effectID, uint32 targetID, int16 repeat)
{
    // the module button.  portals are opened from the "Bridge to" menu, which knows where the portal goes
    throw CustomError("Open a jump portal with \"Bridge to\" on a fleet member who has a cynosural field lit.");
}

bool JumpPortalModule::OpenPortal(const JumpDrive::Beacon& beacon)
{
    Client* pClient = m_shipRef->GetPilot();
    if ((pClient == nullptr) or (pClient->GetShipSE() == nullptr) or m_open)
        return false;

    // one cycle.  this throws the usual activation errors (capacitor, fuel, offline...)
    ActiveModule::Activate(PortalEffect(), 0, 1);
    // check the state only.  a one-cycle activation always sets m_Stop (stop when this cycle ends), so checking it
    //  here refused every portal
    if (GetModuleState() != Module::State::Activated)
        return false;

    m_beacon = beacon;
    m_open = true;

    _log(MODULE__DEBUG, "%s opened a jump portal from %s to %s(%u) in system %u", \
            pClient->GetName(), m_shipRef->name(), m_beacon.item->name(), m_beacon.item->itemID(), m_beacon.systemID);

    // the ship stays put while the portal is open
    DestinyManager* pDestiny = pClient->GetShipSE()->DestinyMgr();
    pDestiny->Stop();
    pDestiny->SetFrozen(true);

    JumpDrive::SendBridgeChange(pClient, m_shipRef->itemID(), m_beacon.systemID, m_beacon.item->itemID(), true);
    return true;
}

void JumpPortalModule::DeactivateCycle(bool abort)
{
    if (m_open) {
        m_open = false;
        Client* pClient = m_shipRef->GetPilot();
        if (pClient != nullptr) {
            if (m_beacon.item.get() != nullptr)
                JumpDrive::SendBridgeChange(pClient, m_shipRef->itemID(), m_beacon.systemID, m_beacon.item->itemID(), false);
            if (pClient->GetShipSE() != nullptr)
                pClient->GetShipSE()->DestinyMgr()->SetFrozen(false);
        }
        m_beacon = JumpDrive::Beacon();
    }

    ActiveModule::DeactivateCycle(abort);
}
