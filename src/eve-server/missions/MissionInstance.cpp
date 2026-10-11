
 /**
  * @name MissionInstance.cpp
  *   the private deadspace pockets of encounter and mining missions, on the F13 dungeon engine
  */


#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "agents/Agent.h"
#include "missions/MissionDataMgr.h"
#include "missions/MissionInstance.h"
#include "system/SystemManager.h"


MissionInstanceMgr::MissionInstanceMgr()
{
    m_byOffer.clear();
    m_byInstance.clear();
}

uint32 MissionInstanceMgr::EnsureInstance(MissionOffer& offer)
{
    if ((offer.stateID != Mission::State::Accepted) or (offer.dungeonSolarSystemID == 0))
        return 0;

    std::map<uint32, Link>::iterator itr = m_byOffer.find(offer.offerID);
    if (itr != m_byOffer.end())
        return itr->second.instanceID;

    if (offer.dungeonID == 0) {
        // no content for the pocket:  the bookmark leads to empty space
        _log(AGENT__WARNING, "MissionInstanceMgr::EnsureInstance() - offer %u (%s) has no dungeon.", offer.offerID, offer.name.c_str());
        return 0;
    }
    SystemManager* pSystem(sEntityList.FindOrBootSystem(offer.dungeonSolarSystemID));
    if ((pSystem == nullptr) or (pSystem->GetDungMgr() == nullptr))
        return 0;

    Dungeon::SpawnParams params;
        params.dungeonID = offer.dungeonID;
        params.archetype = Dungeon::Type::Mission;
        params.factionID = offer.enemyFactionID;
        params.kind = Dungeon::Kind::Mission;
        params.usePosition = true;
        params.position = offer.dungeonPos;
        params.ownerID = offer.characterID;
        params.listener = this;
    uint32 instanceID(pSystem->GetDungMgr()->SpawnInstance(params));
    if (instanceID == 0) {
        _log(AGENT__ERROR, "MissionInstanceMgr::EnsureInstance() - dungeon %u of offer %u didn't spawn in %u.", offer.dungeonID, offer.offerID, offer.dungeonSolarSystemID);
        return 0;
    }

    Link link = Link();
        link.offerID = offer.offerID;
        link.charID = offer.characterID;
        link.agentID = offer.agentID;
        link.systemID = offer.dungeonSolarSystemID;
        link.instanceID = instanceID;
        link.despawnAt = 0;
    m_byOffer[offer.offerID] = link;
    m_byInstance[instanceID] = offer.offerID;
    _log(AGENT__TRACE, "MissionInstanceMgr::EnsureInstance() - offer %u (%s) pocket is instance %u in %u.", offer.offerID, offer.name.c_str(), instanceID, link.systemID);
    return instanceID;
}

void MissionInstanceMgr::Release(uint32 offerID)
{
    std::map<uint32, Link>::iterator itr = m_byOffer.find(offerID);
    if ((itr == m_byOffer.end()) or (itr->second.despawnAt != 0))
        return;
    // the wrecks are the pilot's to salvage for a while
    itr->second.despawnAt = GetFileTimeNow() + (sConfig.missions.DungeonDespawnMinutes * EvE::Time::Minute);
}

void MissionInstanceMgr::Despawn(Link& link)
{
    SystemManager* pSystem(sEntityList.FindSystem(link.systemID));
    if ((pSystem != nullptr) and (pSystem->GetDungMgr() != nullptr))
        pSystem->GetDungMgr()->DespawnInstance(link.instanceID);
}

void MissionInstanceMgr::Process()
{
    int64 now(GetFileTimeNow());
    std::vector<uint32> due;
    for (auto& cur : m_byOffer)
        if ((cur.second.despawnAt != 0) and (cur.second.despawnAt < now))
            due.push_back(cur.first);

    for (auto offerID : due) {
        std::map<uint32, Link>::iterator itr = m_byOffer.find(offerID);
        if (itr == m_byOffer.end())
            continue;
        Link link(itr->second);
        m_byInstance.erase(link.instanceID);
        m_byOffer.erase(itr);
        // the Despawned event that follows finds no link
        Despawn(link);
    }
}

void MissionInstanceMgr::Close()
{
    for (auto& cur : m_byOffer) {
        SystemManager* pSystem(sEntityList.FindSystem(cur.second.systemID));
        if ((pSystem != nullptr) and (pSystem->GetDungMgr() != nullptr))
            pSystem->GetDungMgr()->ClearListener(this);
    }
    m_byOffer.clear();
    m_byInstance.clear();
}

bool MissionInstanceMgr::SetObjectiveComplete(uint32 offerID)
{
    MissionOffer offer = MissionOffer();
    if (!sMissionDataMgr.GetOfferByID(offerID, offer) or (offer.stateID != Mission::State::Accepted))
        return false;
    if (offer.objectiveCompleted)
        return true;

    offer.objectiveCompleted = true;
    Agent* pAgent(sEntityList.GetAgent(offer.agentID));
    if (pAgent != nullptr) {
        // saves it, and updates the agent's and the mission manager's copies
        pAgent->UpdateOffer(offer.characterID, offer);
    } else {
        MissionDB::UpdateMissionOffer(offer);
        sMissionDataMgr.UpdateMissionData(offer.characterID, offer);
    }

    Client* pClient(sEntityList.FindClientByCharID(offer.characterID));
    if (pClient != nullptr) {
        pClient->SendNotifyMsg("Mission objectives complete:  report back to your agent.");
        // journal and an open agent conversation refresh
        if (pAgent != nullptr)
            pAgent->SendMissionUpdate(pClient, "modified");
    }
    _log(AGENT__TRACE, "MissionInstanceMgr::SetObjectiveComplete() - offer %u (%s) of %u.", offerID, offer.name.c_str(), offer.characterID);
    return true;
}

void MissionInstanceMgr::OnDungeonEvent(const Dungeon::DungeonEvent& ev)
{
    std::map<uint32, uint32>::iterator iItr = m_byInstance.find(ev.instanceID);
    if (iItr == m_byInstance.end())
        return;
    uint32 offerID(iItr->second);

    switch (ev.type) {
        case Dungeon::Event::Trigger::MissionCompletion: {
            // the objective npc or structure is destroyed
            SetObjectiveComplete(offerID);
        } break;
        case Dungeon::Engine::Completed: {
            // the pocket is cleared:  that is the objective of kill-all missions
            MissionOffer offer = MissionOffer();
            if (sMissionDataMgr.GetOfferByID(offerID, offer)
            and ((offer.objectiveType == Mission::ObjType::KillAll) or (offer.objectiveType == Mission::ObjType::None)))
                SetObjectiveComplete(offerID);
        } break;
        case Dungeon::Event::Trigger::MissionFailure: {
            MissionOffer offer = MissionOffer();
            if (sMissionDataMgr.GetOfferByID(offerID, offer) and (offer.stateID == Mission::State::Accepted))
                sMissionDataMgr.FailOffer(offer, true);
        } break;
        case Dungeon::Engine::Despawned: {
            // system unload (or our own despawn):  a later warp to the bookmark spawns it again
            m_byInstance.erase(iItr);
            m_byOffer.erase(offerID);
        } break;
    }
}
