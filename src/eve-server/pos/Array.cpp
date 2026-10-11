
/**
 * @name Array.cpp
 *   Class for POS Array Modules.
 *
 * @Author:         Allan
 * @date:   28 December 17
 */

/*
 * POS__ERROR
 * POS__WARNING
 * POS__MESSAGE
 * POS__DUMP
 * POS__DEBUG
 * POS__DESTINY
 * POS__SLIMITEM
 * POS__TRACE
 */


#include "pos/Array.h"
#include "StaticDataMgr.h"
#include "manufacturing/FactoryDB.h"
#include "manufacturing/RamMethods.h"
#include "inventory/Inventory.h"
#include "inventory/ItemFactory.h"
#include "Client.h"


ArraySE::ArraySE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& data)
: StructureSE(structure, services, system, data),
m_ram(false),
m_refinery(false),
m_refineTimer(0)
{
    m_refineTimer.Disable();
}

void ArraySE::Init()
{
    StructureSE::Init();

    // a batch that was refining when the server stopped starts over
    m_refinery = (m_self->groupID() == EVEDB::invGroups::Refining_Array);
    if (m_refinery and m_data.active)
        m_refineTimer.Start(m_self->GetAttribute(AttrOperationalDuration).get_float() * m_self->GetAttribute(AttrRefiningDelayMultiplier).get_float());

    // assembly arrays and labs have R.A.M. lines while online.  jobs pause while offline
    if ((m_self->groupID() == EVEDB::invGroups::Assembly_Array) or (m_self->groupID() == EVEDB::invGroups::Mobile_Laboratory)) {
        std::vector<std::pair<uint8, uint8>> contents;
        m_ram = sDataMgr.GetInstallationContents(m_self->typeID(), contents);
    }
    if (!m_ram)
        return;
    if (IsOnline()) {
        FactoryDB::EnsureStructureAssemblyLines(m_self->itemID(), m_self->typeID(), m_self->ownerID());
        FactoryDB::ResumeJobsInContainer(m_self->itemID());
    } else {
        FactoryDB::PauseJobsInContainer(m_self->itemID());
    }
}

void ArraySE::SetOnline()
{
    StructureSE::SetOnline();
    if (!m_ram)
        return;
    FactoryDB::EnsureStructureAssemblyLines(m_self->itemID(), m_self->typeID(), m_self->ownerID());
    FactoryDB::ResumeJobsInContainer(m_self->itemID());
}

void ArraySE::SetOffline()
{
    StructureSE::SetOffline();
    if (m_ram)
        FactoryDB::PauseJobsInContainer(m_self->itemID());
}

void ArraySE::Killed(Damage &damage)
{
    // installed items are lost with the structure
    if (m_ram)
        sRamMthd.TerminateStructureJobs(m_self->itemID(), EvERam::Status::Destruction);
    StructureSE::Killed(damage);
}

void ArraySE::Process()
{
    /* called by EntityList::Process on every loop */
    /*  Enable base call to Process state changes  */
    StructureSE::Process();

    if (m_refinery and m_refineTimer.Check(false)) {
        m_refineTimer.Disable();
        FinishRefining();
    }
}

void ArraySE::StartRefining(Client* pClient)
{
    if (!m_refinery)
        throw CustomError ("The %s is not a refinery.", m_self->name());
    if (!IsOnline())
        throw CustomError ("The %s must be online to refine.", m_self->name());
    if (!CanUseAccess(pClient, EVEPOS::Usage::Use))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("The starbase's access settings don't let you use this refinery."));
    if (m_refineTimer.Enabled())
        throw CustomError ("The %s is already refining.", m_self->name());

    // the ore in the processing area, within what the refinery takes in one batch
    std::vector<InventoryItemRef> items;
    m_self->GetMyInventory()->GetItemsByFlag(flagCargoHold, items);
    double volume(0.0);
    bool ore(false);
    for (auto cur : items) {
        if (cur->categoryID() != EVEDB::invCategories::Asteroid)
            continue;
        ore = true;
        volume += cur->quantity() * cur->type().volume();
    }
    if (!ore)
        throw CustomError ("There is no ore in the %s.", m_self->name());
    double capacity(m_self->GetAttribute(AttrRefineryCapacity).get_double());
    if ((capacity > 0) and (volume > capacity))
        throw CustomError ("The %s can refine %.0f m3 of ore at a time.  There are %.0f m3 in it.", m_self->name(), capacity, volume);

    float duration(m_self->GetAttribute(AttrOperationalDuration).get_float() * m_self->GetAttribute(AttrRefiningDelayMultiplier).get_float());
    m_refineTimer.Start(duration > 1000 ? duration : 1000);
    m_data.active = true;
    SaveData();
    _log(POS__MESSAGE, "ArraySE::StartRefining() - %s started %s(%u) refining %.0f m3 of ore (%.0f s).", pClient->GetName(), GetName(), GetID(), volume, duration / 1000);
}

void ArraySE::FinishRefining()
{
    m_data.active = false;
    SaveData();

    float yield(m_self->GetAttribute(AttrRefiningYieldMultiplier).get_float());
    std::map<uint16, uint32> minerals;
    std::vector<InventoryItemRef> items;
    m_self->GetMyInventory()->GetItemsByFlag(flagCargoHold, items);
    for (auto cur : items) {
        if (cur->categoryID() != EVEDB::invCategories::Asteroid)
            continue;
        uint16 portion(cur->type().portionSize() > 0 ? cur->type().portionSize() : 1);
        uint32 portions(cur->quantity() / portion);
        if (portions == 0)
            continue;   // less than a portion stays
        std::vector<EvERam::RamMaterials> materials;
        sDataMgr.GetRamMaterials(cur->typeID(), materials);
        for (auto mat : materials)
            minerals[mat.materialTypeID] += (uint32)std::floor(mat.quantity * portions * yield);
        uint32 used(portions * portion);
        if (cur->quantity() > used) {
            cur->AlterQuantity(-((int32)used), true);
        } else {
            cur->SetQuantity(0, true, true);
        }
    }

    // the minerals go back into the processing area
    for (auto cur : minerals) {
        if (cur.second == 0)
            continue;
        InventoryItemRef stack(m_self->GetMyInventory()->GetByTypeFlag(cur.first, flagCargoHold));
        if ((stack.get() != nullptr) and !stack->isSingleton()) {
            stack->AlterQuantity(cur.second, true);
            continue;
        }
        ItemData idata(cur.first, m_self->ownerID(), locTemp, flagCargoHold, cur.second);
        InventoryItemRef iRef(sItemFactory.SpawnItem(idata));
        if (iRef.get() != nullptr)
            iRef->Move(GetID(), flagCargoHold, true);
    }
    _log(POS__MESSAGE, "ArraySE::FinishRefining() - %s(%u) refined %u mineral types.", GetName(), GetID(), (uint32)minerals.size());
}

