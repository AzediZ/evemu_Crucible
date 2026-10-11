
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


ArraySE::ArraySE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& data)
: StructureSE(structure, services, system, data),
m_ram(false)
{

}

void ArraySE::Init()
{
    StructureSE::Init();

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
}

