
/**
 * @name TCU.cpp
 *   Class for Territorial Claim Units.
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
#include "pos/sovStructures/TCU.h"
#include "packets/Sovereignty.h"
#include "system/Container.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "StaticDataMgr.h"

TCUSE::TCUSE(StructureItemRef structure, EVEServiceManager&services, SystemManager *system, const FactionData &fData)
    : StructureSE(structure, services, system, fData)
{
}

void TCUSE::Init()
{
    _log(SE__TRACE, "TCUSE %s(%u) is being initialized", m_self->name(), m_self->itemID());
    StructureSE::Init();

    // check for valid bubble
    if (m_bubble == nullptr)
        assert(0);
    m_bubble->SetTCUSE(this);

    // set global attribute
    m_self->SetAttribute(AttrIsGlobal, EvilOne, false);
}

void TCUSE::SetOnline()
{
    _log(SOV__DEBUG, "Onlining TCU... Creating a new claim.");

    StructureSE::SetOnline();

    // an online hub of the claiming alliance serves the new claim
    uint32 hubID(0);
    for (auto cur : m_system->GetOperationalStatics()) {
        if ((cur.second == nullptr) or !cur.second->IsIHubSE())
            continue;
        StructureSE* pHub(cur.second->GetPOSSE());
        if (pHub->IsOnline() and (pHub->GetAllianceID() == m_allyID))
            hubID = pHub->GetID();
    }

    SovereigntyData sovData = SovereigntyData();
        sovData.solarSystemID = m_system->GetID();
        sovData.hubID = hubID;
        sovData.regionID = m_system->GetRegionID();
        sovData.constellationID = m_system->GetConstellationID();
        sovData.corporationID = m_corpID;
        sovData.allianceID = m_allyID;
        sovData.claimStructureID = m_data.itemID;
        sovData.claimTime = GetFileTimeNow();
    svDataMgr.AddSovClaim(sovData);
    // SBUs left at a minority of the gates count against the new claim (a majority can't let the TCU online)
    svDataMgr.UpdateBlockade(m_system, 0, false);
    // AddSovClaim re-reads the claim from the db (claimID and hub)
    sovData = svDataMgr.GetSovereigntyData(sovData.solarSystemID);

    // recent sov activity (map.GetRecentSovActivity)
    SovereigntyDB::LogSovChange(sovData.solarSystemID, 0, m_allyID, 0);

    _log(SOV__DEBUG, "Sending ProcessSovStatusChanged for %u:%u", sovData.solarSystemID, sovData.allianceID);
    svDataMgr.BroadcastSovStatus(sovData.solarSystemID);

    // '{owner} has claimed sovereignty in {system} on behalf of {alliance}'
    PyDict* params = new PyDict();
        params->SetItemString("owner", new PyString(sDataMgr.GetOwnerName(m_corpID)));
        params->SetItemString("system", new PyString(sDataMgr.GetSystemName(sovData.solarSystemID)));
        params->SetItemString("alliance", new PyString(sDataMgr.GetOwnerName(m_allyID)));
    svDataMgr.SendAudioEvent(m_allyID, Sov::AudioEvent::FlagClaimed, params);
}

void TCUSE::SetOffline()
{
    _log(SOV__DEBUG, "Offlining TCU... Removing claim.");
    DropClaim();

    StructureSE::SetOffline();
}

void TCUSE::DropClaim()
{
    // only the claiming TCU holds the system
    SovereigntyData sovData = svDataMgr.GetSovereigntyData(m_system->GetID());
    if ((sovData.claimID == 0) or (sovData.claimStructureID != m_data.itemID))
        return;

    svDataMgr.DropClaim(m_system->GetID(), sovData.allianceID);
}

void TCUSE::OnRemovedFromSpace(bool killed)
{
    StructureSE::OnRemovedFromSpace(killed);

    SovereigntyData sovData = svDataMgr.GetSovereigntyData(m_system->GetID());
    if ((sovData.claimID == 0) or (sovData.claimStructureID != m_data.itemID))
        return;

    DropClaim();

    if (killed) {
        // 'The Territorial Claim Unit has been destroyed - {owner} of {alliance} has lost sovereignty'
        PyDict* params = new PyDict();
            params->SetItemString("owner", new PyString(sDataMgr.GetOwnerName(sovData.corporationID)));
            params->SetItemString("alliance", new PyString(sDataMgr.GetOwnerName(sovData.allianceID)));
        svDataMgr.SendAudioEvent(sovData.allianceID, Sov::AudioEvent::FlagDestroyed, params);
    }
}

bool TCUSE::IsInvul()
{
    /* the claiming TCU only while SBUs are online at more than half the stargates and the holder has neither an online
     *  hub nor an outpost in the system.  others can always be shot
     */
    SovereigntyData sovData = svDataMgr.GetSovereigntyData(m_system->GetID());
    if ((sovData.claimID > 0) and (sovData.claimStructureID == m_data.itemID))
        return !svDataMgr.IsTCUVulnerable(m_system);
    return StructureSE::IsInvul();
}

void TCUSE::Process()
{
    StructureSE::Process();
}
