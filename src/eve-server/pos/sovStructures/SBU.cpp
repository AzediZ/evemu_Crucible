
/**
 * @name SBU.cpp
 *   Class for Sovereignty Blockade Units.
 *
 * @Author:         James
 * @date:   8 April 2021
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

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "planet/Planet.h"
#include "pos/sovStructures/SBU.h"
#include "pos/sovStructures/TCU.h"
#include "system/Container.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "StaticDataMgr.h"

#include <set>
#include <string>

SBUSE::SBUSE(StructureItemRef structure, EVEServiceManager &services, SystemManager *system, const FactionData &fData)
    : StructureSE(structure, services, system, fData)
{
}

void SBUSE::Init()
{
    _log(SE__TRACE, "SBUSE %s(%u) is being initialised", m_self->name(), m_self->itemID());
    StructureSE::Init();

    // check for valid bubble
    if (m_bubble == nullptr)
        assert(0);
    m_bubble->SetSBUSE(this);

    // set global attribute
    m_self->SetAttribute(AttrIsGlobal, EvilOne, false);

    // an anchored SBU holds its stargate (the gate forgets it over a restart)
    if (IsAnchored() and (m_gateSE != nullptr) and !m_gateSE->HasSBU())
        m_gateSE->SetSBU(this);
}

void SBUSE::SetOnline()
{
    _log(SOV__DEBUG, "Onlining SBU %s(%u)...", GetName(), m_data.itemID);
    StructureSE::SetOnline();

    // '{owner} has onlined a Sovereignty Blockade Unit'
    SendAudioEvent(Sov::AudioEvent::StopOnline);
    // the claim is vulnerable once more than half the stargates have an online SBU
    svDataMgr.UpdateBlockade(m_system);
}

void SBUSE::SetOffline()
{
    _log(SOV__DEBUG, "Offlining SBU %s(%u)...", GetName(), m_data.itemID);
    // the SBU is no longer online here (Process and ForceOffline set it anchored first)
    StructureSE::SetOffline();
    svDataMgr.UpdateBlockade(m_system);
}

void SBUSE::OnRemovedFromSpace(bool killed)
{
    StructureSE::OnRemovedFromSpace(killed);
    ReleaseGate();

    // "One of {owner}'s Sovereignty Blockade Units has been destroyed"
    if (killed)
        SendAudioEvent(Sov::AudioEvent::StopDestroyed);
    // still listed in the system:  counted out by itemID
    svDataMgr.UpdateBlockade(m_system, m_data.itemID);
}

void SBUSE::Delete()
{
    ReleaseGate();
    svDataMgr.UpdateBlockade(m_system, m_data.itemID);
    StructureSE::Delete();
}

void SBUSE::OnAnchored()
{
    if (m_gateSE != nullptr)
        m_gateSE->SetSBU(this);
    // 'Sovereignty Blockade Unit activity detected in {solarSystemID}' (API:  'Sovereignty disruptor anchored')
    svDataMgr.NotifySbuAnchored(m_system->GetID());
}

void SBUSE::ReleaseGate()
{
    if ((m_gateSE != nullptr) and (m_gateSE->GetMySBU() == this))
        m_gateSE->SetSBU(nullptr);
}

void SBUSE::Process()
{
    StructureSE::Process();
}

void SBUSE::SendAudioEvent(uint8 eventID)
{
    if (m_system == nullptr)
        return;
    std::set<uint32> alliances;
    uint32 sovAllianceID(svDataMgr.GetSystemAllianceID(m_system->GetID()));
    if (sovAllianceID > 0)
        alliances.insert(sovAllianceID);
    if (IsAlliance(m_allyID))
        alliances.insert((uint32)m_allyID);

    std::string owner(sDataMgr.GetOwnerName(IsAlliance(m_allyID) ? (uint32)m_allyID : m_corpID));
    for (auto cur : alliances) {
        PyDict* params = new PyDict();
            params->SetItemString("owner", new PyString(owner));
        svDataMgr.SendAudioEvent(cur, eventID, params);
    }
}
