
/**
 * @name IHub.cpp
 *   Class for Infrastructure Hubs.
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
#include "pos/sovStructures/IHub.h"
#include "system/Container.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"

IHubSE::IHubSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& fData)
: StructureSE(structure, services, system, fData)
{
}


void IHubSE::Init()
{
    _log(SE__TRACE, "IHubSE %s(%u) is being initialised", m_self->name(), m_self->itemID());
    StructureSE::Init();

    // check for valid bubble
    if (m_bubble == nullptr)
        assert(0);
    m_bubble->SetIHubSE(this);

    // set global attribute
    m_self->SetAttribute(AttrIsGlobal, EvilOne, false);
}

void IHubSE::Process()
{
    StructureSE::Process();

    // upgrades installed since the last tic
    if (svDataMgr.TakeUpgradesDirty(m_data.itemID))
        svDataMgr.RefreshUpgrades(m_system->GetID(), m_data.itemID);
}

void IHubSE::SetOnline()
{
    _log(SOV__DEBUG, "Onlining IHub... Updating claim'd hubID.");
    StructureSE::SetOnline();
    svDataMgr.UpdateSystemHubID(m_self->locationID(), m_self->itemID());
    svDataMgr.RefreshUpgrades(m_system->GetID(), m_data.itemID);
    // the claim's hub shares its vulnerability
    svDataMgr.UpdateBlockade(m_system, 0, false);
}

void IHubSE::SetOffline()
{
    _log(SOV__DEBUG, "Offlining IHub... Resetting claim's hubID.");
    if (svDataMgr.GetSovereigntyData(m_system->GetID()).hubID == m_data.itemID)
        svDataMgr.UpdateSystemHubID(m_self->locationID(), 0);
    svDataMgr.RefreshUpgrades(m_system->GetID(), m_data.itemID);
    StructureSE::SetOffline();
    // without its hub the claiming TCU is exposed to a blockade
    svDataMgr.UpdateBlockade(m_system, 0, false);
}

void IHubSE::OnRemovedFromSpace(bool killed)
{
    StructureSE::OnRemovedFromSpace(killed);

    uint32 allianceID(svDataMgr.GetSystemAllianceID(m_system->GetID()));
    // upgrades are destroyed with the hub
    svDataMgr.RemoveHub(m_system->GetID(), m_data.itemID);
    // without its hub the claiming TCU is exposed to a blockade
    svDataMgr.UpdateBlockade(m_system, m_data.itemID, false);

    if (killed and (allianceID > 0)) {
        // 'The Infrastructure Hub belonging to {owner} has been destroyed'
        PyDict* params = new PyDict();
            params->SetItemString("owner", new PyString(sDataMgr.GetOwnerName(allianceID)));
        svDataMgr.SendAudioEvent(allianceID, Sov::AudioEvent::HubDestroyed, params);
    }
}

bool IHubSE::IsInvul()
{
    // the claim's hub can only be attacked while SBUs are online at more than half the stargates
    SovereigntyData sovData = svDataMgr.GetSovereigntyData(m_system->GetID());
    if ((sovData.claimID > 0) and (sovData.hubID == m_data.itemID))
        return !svDataMgr.IsClaimVulnerable(m_system);
    return StructureSE::IsInvul();
}
