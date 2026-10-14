
/**
 * @name JumpBridge.cpp
 *   Class for POS JumpBridge Modules.
 *
 * @Author:           James
 * @date:   13 October 2021
 */

#include "pos/JumpBridge.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "inventory/Inventory.h"
#include "ship/JumpDrive.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/SystemManager.h"
#include "planet/Moon.h"

JumpBridgeSE::JumpBridgeSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& data)
: StructureSE(structure, services, system, data)
{
    m_bridgeData = EVEPOS::JumpBridgeData();
}

void JumpBridgeSE::Init()
{
    StructureSE::Init();
    m_bridgeData.itemID = m_self->itemID();
    if (!m_db.GetBridgeData(m_bridgeData))
    {
        _log(POS__MESSAGE, "Jump Bridge %s(%u) has no saved bridge data.  Initializing default set.", m_self->name(), m_data.itemID);
        InitData();
        m_db.SaveBridgeData(m_bridgeData);
    }
}

void JumpBridgeSE::InitData()
{
    StructureSE::InitData();
    // the base found the moon (nullptr in a system without moons)
    if (m_moonSE != nullptr)
        m_data.anchorpointID = m_moonSE->GetID();

    m_bridgeData.itemID = m_data.itemID;
    m_bridgeData.towerID = m_data.towerID;
    m_bridgeData.corpID = m_corpID;
    m_bridgeData.allyID = m_allyID;
    m_bridgeData.systemID = m_system->GetID();
    m_bridgeData.allowCorp = false;
    m_bridgeData.allowAlliance = false;
}

void JumpBridgeSE::Process()
{
    StructureSE::Process();
}

void JumpBridgeSE::SetOnline()
{
    _log(SOV__DEBUG, "Onlining Jump Bridge...");

    /* the onlining checks are in CheckOnline(), called when onlining starts.
     * cyno jammers don't affect jump bridges.
     */

    // Reload data from DB changes
    m_db.GetBridgeData(m_bridgeData);

    // Send a slim update which will contain the latest changes
    SendSlimUpdate();

    StructureSE::SetOnline();
}

void JumpBridgeSE::SetOffline()
{
    _log(SOV__DEBUG, "Offlining Jump Bridge...");
    StructureSE::SetOffline();
}

void JumpBridgeSE::CheckOnline()
{
    // 'You cannot online the {structure} as it needs to be bridged first.'
    if (!IsLinked())
        throw UserError("CantOnlineNotLinked")
                .AddFormatValue("structure", new PyString(m_self->itemName()));

    // Make sure player is not in high-sec (configurable)
    if (!sConfig.world.highSecCyno)
        if (m_system->GetSecValue() >= 0.5f)
            throw CustomError("The %s may not be used in high security space.", m_self->name());
}

void JumpBridgeSE::SetLink(uint32 toSystemID, uint32 toItemID, uint16 toTypeID)
{
    m_bridgeData.toSystemID = toSystemID;
    m_bridgeData.toItemID = toItemID;
    m_bridgeData.toTypeID = toTypeID;
    m_bridgeData.corpID = m_corpID;
    m_bridgeData.allyID = m_allyID;
    m_db.UpdateBridgeData(m_bridgeData);
    // the client's 'Unbridge' menu and jump entries come from the slim item
    SendSlimUpdate();
}

void JumpBridgeSE::ClearLink()
{
    SetLink(0, 0, 0);
}

void JumpBridgeSE::OnRemovedFromSpace(bool killed)
{
    if (IsLinked()) {
        m_db.UninstallRemoteBridgeLink(m_data.itemID);
        // a loaded remote bridge loses its link and can't stay online without one
        SystemManager* pRemoteSys(sEntityList.FindSystem(m_bridgeData.toSystemID));
        if (pRemoteSys != nullptr) {
            SystemEntity* pSE(pRemoteSys->GetSE(m_bridgeData.toItemID));
            if ((pSE != nullptr) and (pSE->GetJumpBridgeSE() != nullptr)) {
                JumpBridgeSE* pRemote(pSE->GetJumpBridgeSE());
                pRemote->ClearLink();
                if (pRemote->IsOnline() or (pRemote->GetProcState() == EVEPOS::ProcState::Onlining))
                    pRemote->Deactivate(0);
            }
        }
        m_bridgeData.toSystemID = 0;
        m_bridgeData.toItemID = 0;
        m_bridgeData.toTypeID = 0;
    }

    StructureSE::OnRemovedFromSpace(killed);
}

uint16 JumpBridgeSE::GetFuelType()
{
    return m_self->GetAttribute(AttrJumpDriveConsumptionType).get_uint32();
}

uint32 JumpBridgeSE::CountFuel()
{
    // the client opens the bridge's resources without a flag, older code used the hangar.  count both
    uint16 fuelType(GetFuelType());
    std::map<uint32, InventoryItemRef> contents;
    m_self->GetMyInventory()->GetInventoryMap(contents);
    uint32 count(0);
    for (auto& cur : contents)
        if (cur.second->typeID() == fuelType)
            count += cur.second->quantity();
    return count;
}

void JumpBridgeSE::ConsumeFuel(uint32 qty)
{
    uint16 fuelType(GetFuelType());
    std::map<uint32, InventoryItemRef> contents;
    m_self->GetMyInventory()->GetInventoryMap(contents);
    for (auto& cur : contents) {
        if (qty == 0)
            break;
        if (cur.second->typeID() != fuelType)
            continue;
        if ((uint32)cur.second->quantity() > qty) {
            cur.second->AlterQuantity(-(int32)qty, true);
            qty = 0;
        } else {
            qty -= cur.second->quantity();
            cur.second->SetQuantity(0, true, true);
        }
    }
}

uint32 JumpBridgeSE::FuelNeeded(double ly, double shipMass)
{
    return JumpDrive::FuelNeeded(ly, m_self->GetAttribute(AttrJumpDriveConsumptionAmount).get_double(),
            shipMass * m_self->GetAttribute(AttrJumpPortalConsumptionMassFactor).get_double());
}

PyDict* JumpBridgeSE::MakeSlimItem()
{
    _log(SE__SLIMITEM, "MakeSlimItem for JumpBridgeSE %u", m_self->itemID());
    _log(POS__SLIMITEM, "MakeSlimItem for JumpBridgeSE %u", m_self->itemID());

    // the structure's fields (posState, posTimestamp the client counts down from, controlTowerID...)
    PyDict *slim = StructureSE::MakeSlimItem();
    // Jump Bridge specific entries
    slim->SetItemString("remoteStructureID", new PyInt(m_bridgeData.toItemID));
    slim->SetItemString("remoteSystemID", new PyInt(m_bridgeData.toSystemID));

    if (is_log_enabled(POS__SLIMITEM))
    {
        _log(POS__SLIMITEM, "JumpBridgeSE::MakeSlimItem() - %s(%u)", GetName(), m_data.itemID);
        slim->Dump(POS__SLIMITEM, "     ");
    }
    return slim;
}

void JumpBridgeSE::SendSlimUpdate()
{
    // the same slim item as everywhere else (MakeSlimItem)
    StructureSE::SendSlimUpdate();
}