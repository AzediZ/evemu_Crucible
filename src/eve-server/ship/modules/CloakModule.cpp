 /**
  * @name CloakModule.cpp
  *   cloaking device module class
  *
  *  a cloak can't start within 2000m of anything, while locked by anyone, or while a module that forbids it runs.
  *  starting it drops our locks.  it drops at once when something comes within 2000m (ShipSE::Process) or the pilot stops it,
  *  and then the ship can't lock anything for the module's cloakingTargetingDelay.
  *  normal cloaks can't warp, no cloak can dock or jump (BeyonceService)
  */

#include "eve-server.h"

#include "Client.h"
#include "npc/LawEnforcement.h"
#include "ship/modules/CloakModule.h"
#include "ship/modules/EwarModule.h"
#include "ship/modules/ModuleManager.h"
#include "system/SystemManager.h"


CloakModule::CloakModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef)
{
}

bool CloakModule::CanActivate()
{
    Client* pClient(m_shipRef->GetPilot());
    if (pClient == nullptr)
        return false;
    ShipSE* pShip(pClient->GetShipSE());
    if ((pShip == nullptr) or (pShip->DestinyMgr() == nullptr)) {
        Clear();
        return false;
    }

    if (pShip->DestinyMgr()->IsModuleCloaked()) {
        Clear();
        throw UserError("AlreadyCloaking");
    }
    if (m_shipRef->GetModuleManager()->GetFittedModuleCountByGroup(EVEDB::invGroups::Cloaking_Device) > 1) {
        Clear();
        throw UserError("CantCloakTooManyDevices");
    }
    if ((pShip->SystemMgr() != nullptr) and pShip->SystemMgr()->GetLaw()->IsJammed(pShip->GetID())) {
        Clear();
        throw UserError("CantCloakPolicePursuit");
    }
    GenericModule* pMod(m_shipRef->GetModuleManager()->GetActiveNoCloakModule());
    if (pMod != nullptr) {
        uint16 modTypeID(pMod->typeID());
        Clear();
        throw UserError("CantCloakModuleActive")
                .AddTypeName("module", modTypeID);
    }
    // only completed locks on us stop the cloak.  locks still in progress just fail
    if (pShip->TargetMgr()->HasLockedTargeters()) {
        Clear();
        throw UserError("CantCloakTargeted");
    }
    SystemEntity* pNear(Ewar::FindDecloaker(pShip));
    if (pNear != nullptr) {
        uint16 nearTypeID(pNear->GetTypeID());
        Clear();
        throw UserError("CantCloakProximity")
                .AddFormatValue("distance", new PyInt((int32)Ewar::DecloakRange))
                .AddTypeName("item", nearTypeID);
    }

    return ActiveModule::CanActivate();
}

void CloakModule::Activate(uint16 effectID, uint32 targetID/*0*/, int16 repeat/*0*/)
{
    ActiveModule::Activate(effectID, targetID, repeat);

    if (m_ModuleState != Module::State::Activated)
        return;

    // a cloak is a toggle: its effect has no duration attribute, so the client sends repeat 0.  it runs until it is stopped
    m_repeat = 1000;
    m_Stop = false;

    Client* pClient(m_shipRef->GetPilot());
    ShipSE* pShip(pClient->GetShipSE());
    // our own locks (and locks in progress) drop
    pShip->TargetMgr()->LoseAllTargets();
    // the dogma velocity penalty is in.  tell everyone while they can still see us
    pShip->DestinyMgr()->SpeedBoost();
    pShip->DestinyMgr()->Cloak(Destiny::Cloak::Module);
    // a running targeting delay from an earlier cloak no longer matters
    pClient->SetUncloakTimer(0);
    _log(MODULE__DEBUG, "%s(%u) cloaked %s(%u)", m_modRef->name(), m_modRef->itemID(), pShip->GetName(), pShip->GetID());
}

void CloakModule::Deactivate(std::string effect/*""*/)
{
    if (m_ModuleState != Module::State::Activated) {
        _log(MODULE__TRACE, "CloakModule::Deactivate - %s called Deactivate but is not currently Activated (%s).", m_modRef->name(), GetModuleStateName(m_ModuleState));
        return;
    }
    SetModuleState(Module::State::Deactivating);
    DeactivateCycle();
}

void CloakModule::DeactivateCycle(bool abort/*false*/)
{
    if (m_ModuleState < Module::State::Deactivating)
        return;

    // removes the dogma penalty, stops the effect and starts the reactivation delay
    ActiveModule::DeactivateCycle(abort);

    Client* pClient(m_shipRef->GetPilot());
    if (pClient == nullptr)
        return;
    ShipSE* pShip(pClient->GetShipSE());
    if ((pShip == nullptr) or (pShip->DestinyMgr() == nullptr))
        return;
    pShip->DestinyMgr()->UnCloak(Destiny::Cloak::Module);
    pShip->DestinyMgr()->SpeedBoost();
    // DeniedTargetAfterCloak until the sensors recalibrate
    uint32 delay(GetAttribute(AttrCloakingTargetingDelay).get_uint32());
    if (delay > 0)
        pClient->SetUncloakTimer(delay);
    _log(MODULE__DEBUG, "%s(%u) decloaked %s(%u).  targeting delay %ums", m_modRef->name(), m_modRef->itemID(), pShip->GetName(), pShip->GetID(), delay);
}
