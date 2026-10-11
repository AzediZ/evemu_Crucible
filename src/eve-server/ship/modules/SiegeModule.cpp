 /**
  * @name SiegeModule.cpp
  *   siege and triage module class
  *
  *  a 300s cycle that can't be cut short (a stop request ends it when the cycle completes).
  *  while it runs the ship can't move, warp (siegeModeWarpStatus), dock or use a jump drive, is immune to offensive ewar
  *  and can't receive remote assistance.  siege also drops the ship's locks when it starts (activationTargetLoss)
  */

#include "eve-server.h"

#include "Client.h"
#include "effects/EffectsDataMgr.h"
#include "inventory/AttributeEnum.h"
#include "ship/modules/SiegeModule.h"
#include "system/DestinyManager.h"
#include "system/TargetManager.h"


SiegeModule::SiegeModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef),
m_maxLockedTargets(EvilZero),
m_maxVelocity(EvilZero),
m_disallowOffensiveModifiers(EvilZero),
m_disallowAssistance(EvilZero),
m_hasSnapshot(false)
{
}

void SiegeModule::Activate(uint16 effectID, uint32 targetID/*0*/, int16 repeat/*0*/)
{
    EvilNumber maxLockedTargets(m_shipRef->GetAttribute(AttrMaxLockedTargets));
    EvilNumber maxVelocity(m_shipRef->GetAttribute(AttrMaxVelocity));
    EvilNumber disallowOffMods(m_shipRef->GetAttribute(AttrDisallowOffensiveModifiers));
    EvilNumber disallowAssistance(m_shipRef->GetAttribute(AttrDisallowAssistance));

    ActiveModule::Activate(effectID, targetID, repeat);

    if (m_ModuleState != Module::State::Activated)
        return;

    m_maxLockedTargets = maxLockedTargets;
    m_maxVelocity = maxVelocity;
    m_disallowOffensiveModifiers = disallowOffMods;
    m_disallowAssistance = disallowAssistance;
    m_hasSnapshot = true;

    Client* pClient(m_shipRef->GetPilot());
    ShipSE* pShip(pClient->GetShipSE());

    // the module carries disallowAssistance.  the ship is what remote modules look at
    if (GetAttribute(AttrDisallowAssistance).get_bool())
        m_shipRef->SetAttribute(AttrDisallowAssistance, EvilOne);

    // siege drops our own locks
    if (GetAttribute(AttrActivationTargetLoss).get_bool())
        pShip->TargetMgr()->LoseAllTargets();

    // remote assistance already running on us stops when its cycle ends
    if (GetAttribute(AttrDisallowAssistance).get_bool()) {
        std::vector<ActiveModule*> modules;
        pShip->TargetMgr()->GetTargetModules(modules);
        for (auto cur : modules)
            if (sFxDataMgr.isAssistance(cur->GetEffectID()))
                cur->Deactivate();
    }

    // immobile, and ten times the mass
    pShip->DestinyMgr()->Halt();
    pShip->DestinyMgr()->SpeedBoost();
    _log(MODULE__DEBUG, "%s(%u) started on %s(%u)", m_modRef->name(), m_modRef->itemID(), pShip->GetName(), pShip->GetID());
}

void SiegeModule::DeactivateCycle(bool abort/*false*/)
{
    if (m_ModuleState < Module::State::Deactivating)
        return;

    // removes the mode's modifiers (what the effects processor can undo)
    ActiveModule::DeactivateCycle(abort);

    if (m_hasSnapshot) {
        m_shipRef->SetAttribute(AttrMaxLockedTargets, m_maxLockedTargets);
        m_shipRef->SetAttribute(AttrMaxVelocity, m_maxVelocity);
        m_shipRef->SetAttribute(AttrDisallowOffensiveModifiers, m_disallowOffensiveModifiers);
        m_shipRef->SetAttribute(AttrDisallowAssistance, m_disallowAssistance);
        m_hasSnapshot = false;
    }

    Client* pClient(m_shipRef->GetPilot());
    if (pClient == nullptr)
        return;
    ShipSE* pShip(pClient->GetShipSE());
    if ((pShip == nullptr) or (pShip->DestinyMgr() == nullptr))
        return;
    pShip->DestinyMgr()->SpeedBoost();
    _log(MODULE__DEBUG, "%s(%u) ended on %s(%u)", m_modRef->name(), m_modRef->itemID(), pShip->GetName(), pShip->GetID());
}
