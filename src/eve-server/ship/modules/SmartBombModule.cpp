 /**
  * @name SmartBombModule.cpp
  *   smart bomb module class
  *
  *  each cycle damages everything within empFieldRange of the ship except the ship itself: ships, pods, drones (ours too), npcs.
  *  only resistances reduce it.  every victim is an offensive act (crimes in highsec, after the client's area-weapon warning).
  *  a module-cloaked ship in range is decloaked instead of damaged, and the bomb stops after that cycle.
  *  it can't run within 5000m of a station, stargate or acceleration gate.
  */

#include "eve-server.h"

#include "Client.h"
#include "inventory/AttributeEnum.h"
#include "ship/Missile.h"
#include "ship/modules/EwarModule.h"
#include "ship/modules/SmartBombModule.h"
#include "system/CrimeWatch.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"


SmartBombModule::SmartBombModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef)
{
}

SystemEntity* SmartBombModule::FindInterference(ShipSE* pShip)
{
    if ((pShip == nullptr) or (pShip->SysBubble() == nullptr))
        return nullptr;
    std::vector<SystemEntity*> entities;
    pShip->SysBubble()->GetStaticEntities(entities);
    std::map<uint32, SystemEntity*> dynamics;
    pShip->SysBubble()->GetAllEntities(dynamics);
    for (auto cur : dynamics)
        entities.push_back(cur.second);

    for (auto cur : entities) {
        if (!cur->IsStationSE() and !cur->IsGateSE() and (cur->GetGroupID() != EVEDB::invGroups::Warp_Gate))
            continue;
        if ((pShip->GetPosition().distance(cur->GetPosition()) - pShip->GetRadius() - cur->GetRadius()) < Ewar::SmartbombStructureRange)
            return cur;
    }
    return nullptr;
}

void SmartBombModule::SendInterference()
{
    if (!m_shipRef->HasPilot())
        return;
    PyTuple* item = new PyTuple(2);
        item->SetItem(0, new PyInt(UserError_Parameter_TypeName));
        item->SetItem(1, new PyInt(typeID()));
    PyDict* args = new PyDict();
        args->SetItemString("item", item);
        args->SetItemString("distance", new PyInt((int32)Ewar::SmartbombStructureRange));
    m_shipRef->GetPilot()->SendRemoteMessage("SmartBombFailsDueToInterference", args);
}

bool SmartBombModule::CanActivate()
{
    if (m_shipRef->HasPilot() and (FindInterference(m_shipRef->GetPilot()->GetShipSE()) != nullptr)) {
        uint16 modTypeID(typeID());
        Clear();
        throw UserError("SmartBombFailsDueToInterference")
                .AddTypeName("item", modTypeID)
                .AddFormatValue("distance", new PyInt((int32)Ewar::SmartbombStructureRange));
    }
    return ActiveModule::CanActivate();
}

uint32 SmartBombModule::DoCycle()
{
    uint32 cycleTime(ActiveModule::DoCycle());
    if ((cycleTime == 0) or !m_shipRef->HasPilot() or (m_bubble == nullptr) or (m_sysMgr == nullptr))
        return cycleTime;

    Client* pPilot(m_shipRef->GetPilot());
    ShipSE* pShip(pPilot->GetShipSE());
    if (FindInterference(pShip) != nullptr) {
        SendInterference();
        AbortCycle();
        return 0;
    }

    // everything in range, cloaked ships included.  damage can destroy (and delete) a victim, so collect IDs first
    double range(GetAttribute(AttrEmpFieldRange).get_double());
    const GPoint& pos(pShip->GetPosition());
    std::vector<uint32> ids;
    std::map<uint32, SystemEntity*> entities;
    m_bubble->GetAllEntities(entities);
    for (auto cur : entities) {
        SystemEntity* pSE(cur.second);
        if ((pSE == pShip) or pSE->IsDead() or pSE->IsInvul())
            continue;
        if ((pos.distance(pSE->GetPosition()) - pSE->GetRadius()) > range)
            continue;
        // missiles in flight (anyone's) are destroyed
        if (pSE->IsMissileSE()) {
            pSE->GetMissileSE()->Intercepted();
            continue;
        }
        // things with a pilot or a crew.  not wrecks, containers, probes, structures or celestials
        if (!pSE->IsShipSE() and !pSE->IsNPCSE() and !pSE->IsDroneSE() and !pSE->IsSentrySE())
            continue;
        ids.push_back(cur.first);
    }

    bool cloakHit(false);
    for (auto id : ids) {
        SystemEntity* pSE(m_sysMgr->GetSE(id));
        if ((pSE == nullptr) or pSE->IsDead())
            continue;
        bool ours(pSE->GetOwnerID() == (uint32)pPilot->GetCharacterID());
        DestinyManager* pDestiny(pSE->DestinyMgr());
        if ((pDestiny != nullptr) and pDestiny->IsCloaked()) {
            // gate and gm cloaks can't be touched
            if (!pDestiny->IsModuleCloaked() or !pSE->IsShipSE())
                continue;
            // the bomb finds the cloaked ship.  it still counts as aggression
            if (!ours)
                sCrimeWatch.OffensiveAct(pPilot, pSE);
            pSE->GetShipSE()->Decloak();
            cloakHit = true;
            continue;
        }

        if (!ours)
            sCrimeWatch.OffensiveAct(pPilot, pSE);
        Damage d(pShip,
                 m_modRef,
                 GetAttribute(AttrKineticDamage).get_float(),
                 GetAttribute(AttrThermalDamage).get_float(),
                 GetAttribute(AttrEmDamage).get_float(),
                 GetAttribute(AttrExplosiveDamage).get_float(),
                 1.0f,
                 m_effectID);
        pSE->ApplyDamage(d);
    }

    if (cloakHit) {
        PyTuple* item = new PyTuple(2);
            item->SetItem(0, new PyInt(UserError_Parameter_TypeName));
            item->SetItem(1, new PyInt(typeID()));
        PyDict* args = new PyDict();
            args->SetItemString("item", item);
        pPilot->SendRemoteMessage("AreaModuleDeactivatedCloakHit", args);
        // stops after this cycle
        if (m_ModuleState == Module::State::Activated) {
            Deactivate();
        } else {
            m_Stop = true;
        }
    }

    return cycleTime;
}
