
 /**
  * @name MissionDataMgr.cpp
  *   memory object caching system for managing and saving ingame data specific to missions
  *
  * @Author:        Allan
  * @date:      24 June 2018
  *
  */
#include "../EVEServerConfig.h"

#include "Client.h"
#include "EntityList.h"
#include "StaticDataMgr.h"
#include "agents/Agent.h"
#include "agents/AgentMgrService.h"
#include "database/EVEDBUtils.h"
#include "mail/NotificationMgr.h"
#include "missions/MissionDataMgr.h"
#include "missions/MissionInstance.h"
#include "missions/MissionObjectives.h"
#include "inventory/ItemFactory.h"
#include "map/MapData.h"
#include "station/StationDataMgr.h"
#include "../../eve-common/EVE_Character.h"
#include "../../eve-common/EVE_Mail.h"
#include "../../eve-common/EVE_Standings.h"

MissionDataMgr::MissionDataMgr()
: CourierPNG(nullptr),
MiningPNG(nullptr),
KillPNG(nullptr)
{
    m_names.clear();
    m_offers.clear();
    m_mining.clear();
    m_courier.clear();
    m_xoffers.clear();
    m_missions.clear();
    m_content.clear();
    m_contentIdx.clear();
}

MissionDataMgr::~MissionDataMgr()
{
    PySafeDecRef(KillPNG);
    PySafeDecRef(MiningPNG);
    PySafeDecRef(CourierPNG);
}

void MissionDataMgr::Clear()
{
    m_names.clear();
    m_offers.clear();
    m_aoffers.clear();
    m_mining.clear();
    m_miningImp.clear();
    m_courier.clear();
    m_courierImp.clear();
    m_xoffers.clear();
    m_missions.clear();
    m_missionsImp.clear();
    m_content.clear();
    m_contentIdx.clear();
}

int MissionDataMgr::Initialize()
{
    Populate();
    sLog.Blue("   MissionDataMgr", "Mission Data Manager Initialized.");
    return 1;
}

void MissionDataMgr::GetInfo()
{
    // not used yet
}

// called every minute from EntityList::Process()
void MissionDataMgr::Process()
{
    // released mission pockets that are due go first
    sMissionInstanceMgr.Process();

    // collect first:  failing/expiring an offer removes it from m_offers
    double now(GetFileTimeNow());
    std::vector<MissionOffer> expired;
    for (auto& cur : m_offers)
        if ((cur.second.expiryTime > 0) and (cur.second.expiryTime < now))
            expired.push_back(cur.second);

    for (auto& offer : expired) {
        Agent* pAgent(sEntityList.GetAgent(offer.agentID));
        if (pAgent == nullptr) {
            _log(AGENT__ERROR, "MissionDataMgr::Process() - agent %u of offer %u not found.", offer.agentID, offer.offerID);
            // drop it anyway, or it is looked at every minute forever
            offer.stateID = (offer.stateID == Mission::State::Accepted ? Mission::State::Failed : Mission::State::Expired);
            offer.dateCompleted = now;
            MissionDB::UpdateMissionOffer(offer);
            RemoveMissionOffer(offer.characterID, offer);
            continue;
        }

        if (offer.stateID == Mission::State::Accepted) {
            FailOffer(offer, true);
            continue;
        }

        // an offer nobody answered
        Client* pClient(sEntityList.FindClientByCharID(offer.characterID));
        offer.stateID = Mission::State::Expired;
        offer.dateCompleted = now;
        MissionDB::UpdateMissionOffer(offer);
        RemoveMissionOffer(offer.characterID, offer);
        pAgent->RemoveOffer(offer.characterID);
        m_xoffers.emplace(offer.characterID, offer);
        pAgent->SendMissionUpdate(pClient, "offer_expired");

        PyDict* data = new PyDict();
            data->SetItemString("header", new PyString("Mission offer expired"));
            data->SetItemString("body", new PyString("The mission offer \"" + offer.name + "\" has expired."));
        NotificationMgr::Send(offer.characterID, Notify::Types::MissionOfferExpiration, offer.agentID, data);
    }
}

void MissionDataMgr::FailOffer(MissionOffer& data, bool notify)
{
    Agent* pAgent(sEntityList.GetAgent(data.agentID));
    Client* pClient(sEntityList.FindClientByCharID(data.characterID));

    data.stateID = Mission::State::Failed;
    data.dateCompleted = GetFileTimeNow();
    if (MissionObjectives::SpawnsCargoOnAccept(data) and data.courierTypeID) {
        // remove item from player's possession
        if (pClient != nullptr) {
            pClient->RemoveMissionItem(data.courierTypeID, data.courierAmount);
        } else {
            MissionDB::RemoveMissionItem(data.characterID, data.courierTypeID, data.courierAmount);
        }
    }
    MissionDB::UpdateMissionOffer(data);
    RemoveMissionOffer(data.characterID, data);
    m_xoffers.emplace(data.characterID, data);
    sMissionInstanceMgr.Release(data.offerID);

    if (pAgent == nullptr)
        return;
    pAgent->RemoveOffer(data.characterID);
    if (!notify)
        return;

    pAgent->SendMissionUpdate(pClient, "failed");
    // the standings loss of a failed mission (applied while the pilot is online; it needs the pilot's skills)
    if (pClient != nullptr)
        pAgent->UpdateStandings(pClient, Standings::MissionFailure, data.important);

    PyDict* notice = new PyDict();
        notice->SetItemString("header", new PyString("Mission failed"));
        notice->SetItemString("body", new PyString("You have failed the mission \"" + data.name + "\".  The time allowed to complete it has run out."));
    NotificationMgr::Send(data.characterID, Notify::Types::MissionOfferExpiration, data.agentID, notice);
}


void MissionDataMgr::Populate()
{
    double start = GetTimeMSeconds();
    double begin = GetTimeMSeconds();

    CourierPNG = new PyString("<img src='res:/UI/netres/mission_content/couriermission.png' align=center hspace=4 vspace=4>");
    MiningPNG = new PyString("<img src='res:/UI/netres/mission_content/miningmission.png' align=center hspace=4 vspace=4>");
    KillPNG = new PyString("<img src='res:/UI/netres/mission_content/killmission.png' align=center hspace=4 vspace=4>");
    /*  not sure if these are used/needed....
    PNG = new PyString("<img src='res:/UI/netres/mission_content/agent_interaction.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/agent_talkto.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/arc_amarr.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/arc_caldari.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/arc_gallente.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/arc_minmatar.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/arc_npe.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/blood_stained.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/angels_and_artifacts.png' align=center hspace=4 vspace=4>");
    PNG = new PyString("<img src='res:/UI/netres/mission_content/smash_and_grab.png' align=center hspace=4 vspace=4>");
    */

    DBQueryResult* res = new DBQueryResult();
    DBResultRow row;

    MissionDB::LoadCourierData(*res);
    while (res->GetRow(row)) {
        //SELECT id, briefingID, name, level, typeID, important, storyline, itemTypeID, itemQty, rewardISK, rewardItemID, rewardItemQty, bonusISK, bonusTime, sysRange, raceID FROM qstCourier
        CourierData data = CourierData();
        data.missionID     = row.GetInt(0);
        data.briefingID    = row.GetInt(1);
        data.name          = row.GetText(2);
        data.level         = row.GetInt(3);
        data.typeID        = row.GetInt(4);
        data.important     = row.GetBool(5);
        data.storyline     = row.GetBool(6);
        data.itemTypeID    = row.GetInt(7);
        data.itemQty       = row.GetInt(8);
        data.itemVolume    = row.GetFloat(9);
        data.rewardISK     = row.GetInt(10);
        data.rewardItemID  = row.GetInt(11);
        data.rewardItemQty = row.GetInt(12);
        data.bonusISK      = row.GetInt(13);
        data.bonusTime     = row.GetInt(14);
        data.range         = row.GetInt(15);
        data.raceID        = row.GetInt(16);
        if (data.important) {
            m_courierImp.emplace(row.GetInt(3), data);
        } else {
            m_courier.emplace(row.GetInt(3), data);
        }
    }
    sLog.Cyan("   MissionDataMgr", "%lu(%lu) Courier Mission Data Sets loaded in %.3fms.", m_courier.size(), m_courierImp.size(),(GetTimeMSeconds() - start));

    //res->Reset();
    start = GetTimeMSeconds();
    MissionDB::LoadMiningData(*res);
    while (res->GetRow(row)) {
        //SELECT id, briefingID, name, level, typeID, important, storyline, itemTypeID, itemQty, rewardISK, rewardItemID, rewardItemQty, bonusISK, bonusTime, sysRange, raceID FROM qstMining
        CourierData data = CourierData();
        data.missionID     = row.GetInt(0);
        data.briefingID    = row.GetInt(1);
        data.name          = row.GetText(2);
        data.level         = row.GetInt(3);
        data.typeID        = row.GetInt(4);
        data.important     = row.GetBool(5);
        data.storyline     = row.GetBool(6);
        data.itemTypeID    = row.GetInt(7);
        data.itemQty       = row.GetInt(8);
        data.itemVolume    = row.GetFloat(9);
        data.rewardISK     = row.GetInt(10);
        data.rewardItemID  = row.GetInt(11);
        data.rewardItemQty = row.GetInt(12);
        data.bonusISK      = row.GetInt(13);
        data.bonusTime     = row.GetInt(14);
        data.range         = row.GetInt(15);
        data.raceID        = row.GetInt(16);
        if (data.important) {
            m_miningImp.emplace(row.GetInt(3), data);
        } else {
            m_mining.emplace(row.GetInt(3), data);
        }
    }
    sLog.Cyan("   MissionDataMgr", "%lu(%lu) Legacy Mining Mission Data Sets loaded in %.3fms.", m_mining.size(), m_miningImp.size(), (GetTimeMSeconds() - start));

    // mission content (msnMissions, generated from the eve-survival.org reports by utils/missiondata)
    start = GetTimeMSeconds();
    MissionDB::LoadMissionContent(*res);
    std::map<uint8, std::pair<uint32, uint32>> counts;   // typeID -> (normal, storyline)
    while (res->GetRow(row)) {
        MissionContent data = MissionContent();
        data.contentID          = row.GetUInt(0);
        data.name               = row.GetText(1);
        data.nameID             = row.GetUInt(2);
        data.briefingID         = row.GetUInt(3);
        data.briefingText       = (row.IsNull(4) ? "" : row.GetText(4));
        data.level              = row.GetUInt(5);
        data.typeID             = row.GetUInt(6);
        data.important          = row.GetBool(7);
        data.storyline          = row.GetBool(8);
        data.enemyFactionID     = row.GetUInt(9);
        data.agentRaceMask      = row.GetUInt(10);
        data.objectiveType      = row.GetUInt(11);
        data.objectiveTypeID    = row.GetUInt(12);
        data.objectiveQty       = row.GetUInt(13);
        data.dungeonID          = row.GetUInt(14);
        data.spaceType          = row.GetUInt(15);
        data.restrictLevel      = row.GetUInt(16);
        data.rewardISK          = row.GetUInt(17);
        data.bonusISK           = row.GetUInt(18);
        data.bonusTime          = row.GetUInt(19);
        data.rewardLP           = row.GetUInt(20);
        data.rewardScale        = row.GetFloat(21);
        data.rewardItemTypeID   = row.GetUInt(22);
        data.rewardItemQty      = row.GetUInt(23);
        data.collateral         = row.GetUInt(24);
        data.remoteOfferable    = row.GetBool(25);
        data.remoteCompletable  = row.GetBool(26);
        data.pageKey            = row.GetText(27);
        if ((data.level < 1) or (data.level > 5)) {
            _log(AGENT__WARNING, "MissionDataMgr::Populate() - mission content %u has level %u.  skipped.", data.contentID, data.level);
            continue;
        }
        if (data.rewardScale <= 0.0f)
            data.rewardScale = 1.0f;
        m_contentIdx.emplace((uint16)((data.level << 8) | data.typeID), data.contentID);
        if (data.storyline) {
            ++counts[data.typeID].second;
        } else {
            ++counts[data.typeID].first;
        }
        m_content.emplace(data.contentID, data);
    }
    const uint8 types[4] = { Mission::Type::Encounter, Mission::Type::Mining, Mission::Type::Courier, Mission::Type::Trade };
    for (auto type : types)
        sLog.Cyan("   MissionDataMgr", "%u(%u) %s Mission Content Sets loaded.", counts[type].first, counts[type].second, GetTypeName(type).c_str());
    sLog.Cyan("   MissionDataMgr", "%lu Mission Content Sets loaded in %.3fms.", m_content.size(), (GetTimeMSeconds() - start));

    //res->Reset();
    start = GetTimeMSeconds();
    MissionDB::LoadMissionData(*res);
    while (res->GetRow(row)) {
        //SELECT id, briefingID, name, level, typeID, important, storyline, raceID, constellationID, corporationID, dungeonID,
        // rewardISK, rewardItemID, rewardISKQty, rewardItemQty, bonusISK, bonusTime FROM agtMissions
        MissionData data = MissionData();
        data.missionID = row.GetInt(0);
        data.briefingID = row.GetInt(1);
        data.name = row.GetText(2);
        data.level = row.GetInt(3);
        data.typeID = row.GetInt(4);
        data.important = row.GetBool(5);
        data.constellationID = row.GetInt(8);
        data.corporationID = row.GetInt(9);
        data.dungeonID = row.GetInt(10);
        if (data.important) {
            m_missionsImp.emplace(row.GetInt(3), data);
        } else {
            m_missions.emplace(row.GetInt(3), data);
        }
    }
    sLog.Cyan("   MissionDataMgr", "%lu(%lu) Unsorted Mission Data Sets loaded in %.3fms.", m_missions.size(), m_missionsImp.size(), (GetTimeMSeconds() - start));

    //res->Reset();
    start = GetTimeMSeconds();
    MissionDB::LoadOpenOffers(*res);
    while (res->GetRow(row)) {
        // see MissionDB::LoadOpenOffers() for the column order
        MissionOffer offer = MissionOffer();
        offer.acceptFee = row.GetInt(0);
        offer.agentID = row.GetInt(1);
        offer.characterID = row.GetInt(2);
        offer.courierAmount = row.GetUInt(3);
        offer.courierTypeID = row.GetInt(4);
        offer.courierItemVolume = row.GetFloat(5);
        offer.dateAccepted = row.GetInt64(6);
        offer.dateIssued = row.GetInt64(7);
        offer.destinationID = row.GetInt(8);
        offer.destinationTypeID = row.GetInt(9);
        offer.destinationOwnerID = row.GetInt(10);
        offer.destinationSystemID = row.GetInt(11);
        offer.expiryTime = row.GetInt64(12);
        offer.important = row.GetInt(13);
        offer.storyline = row.GetInt(14);
        offer.missionID = row.GetInt(15);
        offer.briefingID = row.GetInt(16);
        offer.name = row.GetText(17);
        offer.offerID = row.GetInt(18);
        offer.originID = row.GetInt(19);
        offer.originOwnerID = row.GetInt(20);
        offer.originSystemID = row.GetInt(21);
        offer.remoteCompletable = row.GetInt(22);
        offer.remoteOfferable = row.GetInt(23);
        offer.rewardISK = row.GetUInt(24);
        offer.rewardItemID = row.GetInt(25);
        offer.rewardItemQty = row.GetInt(26);
        offer.rewardLP = row.GetUInt(27);
        offer.bonusISK = row.GetUInt(28);
        offer.bonusTime = row.GetInt(29);
        offer.stateID = row.GetInt(30);
        offer.typeID = row.GetInt(31);
        offer.dungeonLocationID = row.GetInt(32);
        offer.dungeonSolarSystemID = row.GetInt(33);
        offer.contentID = row.GetUInt(34);
        offer.nameID = row.GetUInt(35);
        offer.objectiveType = row.GetUInt(36);
        offer.objectiveTypeID = row.GetUInt(37);
        offer.objectiveQty = row.GetUInt(38);
        offer.objectiveCompleted = row.GetBool(39);
        offer.enemyFactionID = row.GetUInt(40);
        offer.dungeonID = row.GetUInt(41);
        offer.dungeonPos = GPoint(row.GetDouble(42), row.GetDouble(43), row.GetDouble(44));
        offer.briefingText = (row.IsNull(45) ? "" : row.GetText(45));
        offer.dateCompleted = 0;
        m_offers.emplace(offer.characterID, offer);
        m_aoffers.emplace(offer.agentID, offer);    // do we really want dupe data here?  yes.  need offer by char and by agent
    }
    sLog.Cyan("   MissionDataMgr", "%lu Open Mission Offers loaded in %.3fms.", m_offers.size(), (GetTimeMSeconds() - start));

    //res->Reset();
    start = GetTimeMSeconds();
    // config switch to allow loading/displaying of expired/completed mission offers
    if (sConfig.server.LoadOldMissions)
        MissionDB::LoadClosedOffers(*res);
    while (res->GetRow(row)) {
        //SELECT agentID, characterID, courierAmount, courierTypeID, dateAccepted, dateCompleted, dateIssued, destinationID, expiryTime, important, storyline, missionID, name,
        // offerID, originID, rewardISK, rewardItemID, rewardItemQty, rewardLP, stateID, typeID FROM agtOffers
        MissionOffer offer = MissionOffer();
        offer.agentID = row.GetInt(0);
        offer.characterID = row.GetInt(1);
        offer.courierAmount = row.GetUInt(2);
        offer.courierTypeID = row.GetInt(3);
        offer.dateAccepted = row.GetInt64(4);
        offer.dateCompleted = row.GetInt64(5);
        offer.dateIssued = row.GetInt64(6);
        offer.destinationID = row.GetInt(7);
        offer.expiryTime = row.GetInt64(8);
        offer.important = row.GetInt(9);
        offer.storyline = row.GetInt(10);
        offer.missionID = row.GetInt(11);
        offer.name = row.GetText(12);
        offer.offerID = row.GetInt(13);
        offer.originID = row.GetInt(14);
        offer.rewardISK = row.GetUInt(15);
        offer.rewardItemID = row.GetInt(16);
        offer.rewardItemQty = row.GetInt(17);
        offer.rewardLP = row.GetUInt(18);
        offer.stateID = row.GetInt(19);
        offer.typeID = row.GetInt(20);
        m_xoffers.emplace(row.GetInt(1), offer);
    }
    sLog.Cyan("   MissionDataMgr", "%lu Closed Mission Offers loaded in %.3fms.", m_xoffers.size(), (GetTimeMSeconds() - start));

    // cleanup
    SafeDelete(res);
    sLog.Cyan("   MissionDataMgr", "Mission Data loaded in %.3fms.", (GetTimeMSeconds() - begin));
}

void MissionDataMgr::AddMissionOffer(uint32 charID, MissionOffer& data)
{
    m_offers.emplace(charID, data);
    m_aoffers.emplace(data.agentID, data);
}

void MissionDataMgr::RemoveMissionOffer(uint32 charID, MissionOffer& data)
{
    auto itr = m_offers.equal_range(charID);
    for (auto it = itr.first; it != itr.second; ++it)
        if (it->second.agentID == data.agentID) {
            m_offers.erase(it);
            break;
        }

    itr = m_aoffers.equal_range(data.agentID);
    for (auto it = itr.first; it != itr.second; ++it)
        if (it->second.characterID == charID) {
            m_aoffers.erase(it);
            break;
        }
}

void MissionDataMgr::LoadAgentOffers(const uint32 agentID, std::map< uint32, MissionOffer >& data)
{
    auto itr = m_aoffers.equal_range(agentID);
    for (auto it = itr.first; it != itr.second; ++it)
        data[it->second.characterID] = (it->second);
}

void MissionDataMgr::LoadMissionOffers(uint32 charID, std::vector<MissionOffer>& data)
{
    auto itr = m_offers.equal_range(charID);
    for (auto it = itr.first; it != itr.second; ++it)
        data.push_back(it->second);

    // config switch to allow loading/displaying of expired/completed mission offers
    if (sConfig.server.LoadOldMissions) {
        auto itr = m_xoffers.equal_range(charID);
        for (auto it = itr.first; it != itr.second; ++it)
            data.push_back(it->second);
    }
}

bool MissionDataMgr::GetOfferByID(uint32 offerID, MissionOffer& data)
{
    for (auto& cur : m_offers)
        if (cur.second.offerID == offerID) {
            data = cur.second;
            return true;
        }
    return false;
}

bool MissionDataMgr::HasStorylineContent(uint8 typeID, uint8 level)
{
    if (HasContent(typeID, level, true))
        return true;
    return ((typeID == Mission::Type::Courier) and (m_courierImp.count(level) > 0));
}

void MissionDataMgr::OnMissionCompleted(Client* pClient, Agent* pAgent, MissionOffer& data)
{
    // storyline missions come from the missions of basic and CONCORD agents
    if (data.storyline or (sConfig.missions.StorylineEvery == 0))
        return;
    if ((pAgent->GetTypeID() != Agents::Type::Basic) and (pAgent->GetTypeID() != 5 /*CONCORD*/))
        return;

    uint32 charID(pClient->GetCharacterID());
    uint8 level(pAgent->GetLevel());
    uint32 count(MissionDB::IncrementCounter(charID, pAgent->GetFactionID(), level));
    if (count < sConfig.missions.StorylineEvery)
        return;

    // an agent that already has an offer for the pilot is skipped;  the count stays and the next mission tries again
    Agent* pSL(FindStorylineAgent(pAgent->GetFactionID(), pAgent->GetSystemID()));
    if ((pSL == nullptr) or pSL->HasMission(charID))
        return;
    MissionOffer offer = MissionOffer();
    if (!pSL->MakeStorylineOffer(charID, level, offer))
        return;
    MissionDB::SetCounter(charID, pAgent->GetFactionID(), level, count - sConfig.missions.StorylineEvery);

    pSL->SendMissionUpdate(pClient, "offered");
    // "I am in need of your services ... for a very special mission"
    NotificationMgr::Send(charID, Notify::Types::StoryLineMissionAvailable, pSL->GetID(), new PyDict());
    _log(AGENT__DEBUG, "%s gets the level %u storyline mission '%s' from agent %u.", pClient->GetName(), level, offer.name.c_str(), pSL->GetID());
}

Agent* MissionDataMgr::FindStorylineAgent(uint32 factionID, uint32 systemID)
{
    std::vector<std::pair<uint32, uint32>> agents;
    sDataMgr.GetStorylineAgents(factionID, agents);
    if (agents.empty())
        return nullptr;

    // the nearest by jumps, else one of the same region, else any
    std::vector<std::pair<uint32, uint8>> systems;
    sMapData.GetSystemsWithinJumps(systemID, 15, systems);
    std::map<uint32, uint8> jumps(systems.begin(), systems.end());
    uint32 best(0);
    uint8 bestJumps(255);
    for (auto& cur : agents) {
        std::map<uint32, uint8>::iterator itr = jumps.find(cur.second);
        if ((itr != jumps.end()) and (itr->second < bestJumps)) {
            best = cur.first;
            bestJumps = itr->second;
        }
    }
    if (best == 0) {
        SystemData from = SystemData();
        sDataMgr.GetSystemData(systemID, from);
        for (auto& cur : agents) {
            SystemData to = SystemData();
            if (sDataMgr.GetSystemData(cur.second, to) and (to.regionID == from.regionID)) {
                best = cur.first;
                break;
            }
        }
    }
    if (best == 0)
        best = agents[MakeRandomInt(0, agents.size() - 1)].first;
    return sEntityList.GetAgent(best);
}

const MissionContent* MissionDataMgr::GetContent(uint32 contentID)
{
    std::map<uint32, MissionContent>::iterator itr = m_content.find(contentID);
    if (itr == m_content.end())
        return nullptr;
    return &itr->second;
}

bool MissionDataMgr::HasContent(uint8 typeID, uint8 level, bool storyline)
{
    auto range = m_contentIdx.equal_range((uint16)((level << 8) | typeID));
    for (auto itr = range.first; itr != range.second; ++itr) {
        const MissionContent* pContent(GetContent(itr->second));
        if ((pContent != nullptr) and (pContent->storyline == storyline))
            return true;
    }
    return false;
}

bool MissionDataMgr::IsEligible(const MissionContent& content, Agent* pAgent)
{
    uint32 agentFaction(pAgent->GetFactionID());
    // nobody sends pilots against their own side
    if ((content.enemyFactionID != 0) and (content.enemyFactionID == agentFaction))
        return false;

    // missions against an empire are only given by its enemies
    switch (content.enemyFactionID) {
        case factionAmarr:
        case factionAmmatar:
        case factionKhanid:
            if (agentFaction != factionMinmatar)
                return false;
            break;
        case factionMinmatar:
            if ((agentFaction != factionAmarr) and (agentFaction != factionAmmatar) and (agentFaction != factionKhanid))
                return false;
            break;
        case factionCaldari:
            if (agentFaction != factionGallente)
                return false;
            break;
        case factionGallente:
            if (agentFaction != factionCaldari)
                return false;
            break;
    }

    // content limited to agents of some races
    if (content.agentRaceMask != 0) {
        uint8 race(sDataMgr.GetFactionRace(agentFaction));
        if ((race & (Char::Race::Caldari | Char::Race::Minmatar | Char::Race::Amarr | Char::Race::Gallente)) == 0)
            return true;    // pirate/other agents aren't limited by race
        if ((content.agentRaceMask & race) == 0)
            return false;
    }
    return true;
}

bool MissionDataMgr::PickContent(uint8 typeID, uint8 level, Agent* pAgent, bool storyline, MissionContent& out)
{
    std::vector<const MissionContent*> candidates, local;
    auto range = m_contentIdx.equal_range((uint16)((level << 8) | typeID));
    for (auto itr = range.first; itr != range.second; ++itr) {
        const MissionContent* pContent(GetContent(itr->second));
        if ((pContent == nullptr) or (pContent->storyline != storyline))
            continue;
        if (!IsEligible(*pContent, pAgent))
            continue;
        candidates.push_back(pContent);
    }
    if (candidates.empty())
        return false;

    // pirate missions are against the pirates of the agent's region (any pirates when the region has none of this level)
    SystemData sysData = SystemData();
    sDataMgr.GetSystemData(pAgent->GetSystemID(), sysData);
    uint32 ratFaction(sDataMgr.GetRegionRatFaction(sysData.regionID));
    for (auto cur : candidates) {
        switch (cur->enemyFactionID) {
            case factionAngel:
            case factionBloodRaider:
            case factionGuristas:
            case factionSanshas:
            case factionSerpentis:
                if (cur->enemyFactionID == ratFaction)
                    local.push_back(cur);
                break;
            default:
                // rogue drones, mercenaries and the empires are fought everywhere
                local.push_back(cur);
                break;
        }
    }
    std::vector<const MissionContent*>& pick(local.empty() ? candidates : local);
    out = *pick[MakeRandomInt(0, pick.size() - 1)];
    return true;
}

void MissionDataMgr::ApplyContent(const MissionContent& content, uint8 level, MissionOffer& data)
{
    uint8 idx(LevelIndex(level));
    data.contentID          = content.contentID;
    data.name               = content.name;
    data.nameID             = content.nameID;
    data.missionID          = 0;
    data.briefingID         = content.briefingID;
    data.briefingText       = content.briefingText;
    data.typeID             = content.typeID;
    data.important          = content.important;
    data.storyline          = content.storyline;
    data.objectiveType      = content.objectiveType;
    data.objectiveTypeID    = content.objectiveTypeID;
    data.objectiveQty       = content.objectiveQty;
    data.enemyFactionID     = content.enemyFactionID;
    data.dungeonID          = content.dungeonID;
    data.remoteOfferable    = content.remoteOfferable;
    data.remoteCompletable  = content.remoteCompletable;
    data.rewardItemID       = content.rewardItemTypeID;
    data.rewardItemQty      = content.rewardItemQty;
    // base rewards.  the agent scales them for the pilot (MissionRewards)
    data.rewardISK          = (uint32)((content.rewardISK ? content.rewardISK : sConfig.missions.BaseISK[idx]) * content.rewardScale);
    data.bonusISK           = (uint32)((content.bonusISK ? content.bonusISK : sConfig.missions.BonusISK[idx]) * content.rewardScale);
    data.bonusTime          = (content.bonusTime ? content.bonusTime : sConfig.missions.BonusMinutes[idx]);
    data.rewardLP           = (uint32)((content.rewardLP ? content.rewardLP : sConfig.missions.BaseLP[idx]) * content.rewardScale);

    // item objectives (mine, fetch, trade) use the courier fields MissionObjectives counts with
    switch (content.objectiveType) {
        case Mission::ObjType::Mine:
        case Mission::ObjType::Fetch:
        case Mission::ObjType::Trade: {
            data.courierTypeID = content.objectiveTypeID;
            data.courierAmount = content.objectiveQty;
            const ItemType* pType(sItemFactory.GetType(content.objectiveTypeID));
            data.courierItemVolume = (pType != nullptr ? pType->volume() : 0.0f);
        } break;
    }
}

bool MissionDataMgr::CreateMissionOffer(uint8 typeID, uint8 level, Agent* pAgent, bool important, bool storyline, MissionOffer& data)
{
    uint8 raceID(sDataMgr.GetFactionRace(pAgent->GetFactionID()));
    switch (typeID) {
        case Mission::Type::Courier: {
            // storyline courier missions come from the content table, else the important courier rows
            if (storyline) {
                MissionContent content = MissionContent();
                if (PickContent(typeID, level, pAgent, true, content)) {
                    ApplyContent(content, level, data);
                    break;
                }
                important = true;
            }
            std::vector<CourierData> cVec, raceVec;
            auto itr = (important ? m_courierImp.equal_range(level) : m_courier.equal_range(level));
            for (auto it = itr.first; it != itr.second; ++it) {
                cVec.push_back(it->second);
                // verify mission race acceptable
                if ((it->second.raceID == 0) or ((it->second.raceID & raceID) != 0))
                    raceVec.push_back(it->second);
            }
            if (cVec.empty())
                return false;
            std::vector<CourierData>& pick(raceVec.empty() ? cVec : raceVec);
            const CourierData& cData(pick[MakeRandomInt(0, (pick.size() -1))]);

            data.name               = cData.name;
            data.typeID             = cData.typeID;
            data.bonusISK           = cData.bonusISK;
            data.rewardISK          = cData.rewardISK;
            data.bonusTime          = cData.bonusTime;
            data.important          = cData.important;
            data.storyline          = cData.storyline;
            data.missionID          = cData.missionID;
            data.briefingID         = cData.briefingID;
            data.rewardItemID       = cData.rewardItemID;
            data.rewardItemQty      = cData.rewardItemQty;
            data.courierTypeID      = cData.itemTypeID;
            data.courierAmount      = cData.itemQty;
            data.courierItemVolume  = cData.itemVolume;
            data.range              = cData.range;
            data.objectiveType      = Mission::ObjType::Deliver;
            data.objectiveTypeID    = cData.itemTypeID;
            data.objectiveQty       = cData.itemQty;
            // couriers are offered remotely, and completed where they are delivered
            data.remoteOfferable    = true;
            data.remoteCompletable  = false;
            // base LP of the level (the agent scales it for the pilot)
            data.rewardLP           = sConfig.missions.BaseLP[LevelIndex(level)];
        } break;
        case Mission::Type::Mining: {
            // the content table has the mining missions (ore, quantity, pocket).  the old qstMining rows have no ore
            MissionContent content = MissionContent();
            if (PickContent(typeID, level, pAgent, storyline, content)) {
                ApplyContent(content, level, data);
                break;
            }
            std::vector<CourierData> cVec;
            auto itr = (important ? m_miningImp.equal_range(level) : m_mining.equal_range(level));
            for (auto it = itr.first; it != itr.second; ++it)
                cVec.push_back(it->second);
            if (cVec.empty())
                return false;
            const CourierData& cData(cVec[MakeRandomInt(0, (cVec.size() -1))]);

            data.name               = cData.name;
            data.typeID             = cData.typeID;
            data.bonusISK           = cData.bonusISK;
            data.rewardISK          = cData.rewardISK;
            data.bonusTime          = cData.bonusTime;
            data.important          = cData.important;
            data.storyline          = cData.storyline;
            data.missionID          = cData.missionID;
            data.briefingID         = cData.briefingID;
            data.rewardItemID       = cData.rewardItemID;
            data.rewardItemQty      = cData.rewardItemQty;
            data.courierTypeID      = cData.itemTypeID;
            data.courierAmount      = cData.itemQty;
            data.courierItemVolume  = cData.itemVolume;
            data.range              = cData.range;
            data.objectiveType      = Mission::ObjType::Mine;
            data.objectiveTypeID    = cData.itemTypeID;
            data.objectiveQty       = cData.itemQty;
            data.remoteOfferable    = true;
            data.rewardLP           = sConfig.missions.BaseLP[LevelIndex(level)];
        } break;
        case Mission::Type::Encounter:
        case Mission::Type::Trade: {
            MissionContent content = MissionContent();
            if (!PickContent(typeID, level, pAgent, storyline, content))
                return false;
            ApplyContent(content, level, data);
        } break;
        default:
            // tutorial, research, data, cosmos, epic arc and anomic content isn't served (see F12 plan, deferred)
            return false;
    }

    // variable mission data based on agent, init to 0 here.
    data.stateID                = Mission::State::Allocated;
    data.dateIssued             = GetFileTimeNow();
    data.offerID                = 0;
    data.agentID                = 0;
    data.originID               = 0;
    data.originOwnerID          = 0;
    data.originSystemID         = 0;
    data.acceptFee              = 0;
    data.expiryTime             = 0;
    data.characterID            = 0;
    data.dateAccepted           = 0;
    data.dateCompleted          = 0;
    data.destinationID          = 0;
    data.destinationTypeID      = 0;
    data.destinationOwnerID     = 0;
    data.destinationSystemID    = 0;
    data.dungeonLocationID      = 0;
    data.dungeonSolarSystemID   = 0;
    data.objectiveCompleted     = false;
    if (important)
        data.important          = true;
    if (storyline) {
        data.storyline          = true;
        data.important          = true;
    }

    _log(AGENT__DEBUG, "Created %s level %u %s offer - '%s'", (data.important?"an important":"a"), level, GetTypeName(data.typeID).c_str(), data.name.c_str());
    return true;
}


std::string MissionDataMgr::GetTypeName(uint8 typeID)
{
    using namespace Mission::Type;
    switch (typeID) {
        case Tutorial:          return "Tutorial";
        case Encounter:         return "Encounter";
        case Courier:           return "Courier";
        case Trade:             return "Trade";
        case Mining:            return "Mining";
        case Research:          return "Research";
        case Data:              return "Data";
        case Storyline:         return "Storyline";
        case Cosmos:            return "Cosmos";
        case EpicArc:           return "Arc";
        case Anomic:            return "Anomic";
        case Burner:            return "Burner";
        default:                return "Invalid";
    }
}

std::string MissionDataMgr::GetTypeLabel(uint8 typeID, bool storyline/*false*/)
{
    // only labels the Crucible client has (UI/Agents/MissionTypes/*).  the journal throws on an unknown label
    using namespace Mission::Type;
    if (storyline) {
        switch (typeID) {
            case Encounter:     return "UI/Agents/MissionTypes/StorylineEncounter";
            case Courier:       return "UI/Agents/MissionTypes/StorylineCourier";
            case Mining:        return "UI/Agents/MissionTypes/StorylineMining";
            case Trade:         return "UI/Agents/MissionTypes/StorylineTrade";
            default:            return "UI/Agents/MissionTypes/Storyline";
        }
    }
    switch (typeID) {
        case Encounter:
        case Cosmos:
        case Anomic:            return "UI/Agents/MissionTypes/Encounter";
        case Trade:             return "UI/Agents/MissionTypes/Trade";
        case Mining:            return "UI/Agents/MissionTypes/Mining";
        case Research:
        case Data:              return "UI/Agents/MissionTypes/Research";
        case Storyline:         return "UI/Agents/MissionTypes/Storyline";
        case EpicArc:           return "UI/Agents/MissionTypes/EpicArc";
        case Tutorial:
        case Courier:
        case Burner:
        default:                return "UI/Agents/MissionTypes/Courier";
    }
}

PyString* MissionDataMgr::GetMissionImage(uint8 typeID)
{
    switch (typeID) {
        case Mission::Type::Mining:         return GetMiningRes();
        case Mission::Type::Encounter:      return GetKillRes();
        default:                            return GetCourierRes();
    }
}

uint8 MissionDataMgr::GetClientState(uint8 stateID)
{
    // the client knows allocated, offered, accepted and failed (appConst agentMissionState*)
    switch (stateID) {
        case Mission::State::Allocated:
        case Mission::State::Defered:       return Mission::State::Allocated;
        case Mission::State::Offered:       return Mission::State::Offered;
        case Mission::State::Accepted:      return Mission::State::Accepted;
        default:                            return Mission::State::Failed;
    }
}

PyRep* MissionDataMgr::GetTitle(const MissionOffer& data)
{
    if (data.nameID != 0)
        return new PyInt(data.nameID);
    if (data.missionID != 0)
        return new PyInt(data.missionID);
    return new PyString(data.name);
}

PyRep* MissionDataMgr::GetBriefing(const MissionOffer& data)
{
    if (data.briefingID != 0)
        return new PyInt(data.briefingID);
    return new PyString(data.briefingText);
}

PyTuple* MissionDataMgr::GetJournalEntry(MissionOffer& data)
{
    //missionState, importantMission, missionTypeLabel, missionNameID, agentID, expirationTime, bookmarks, remoteOfferable, remoteCompletable
    PyTuple* mData = new PyTuple(9);
        mData->SetItem(0, new PyInt(GetClientState(data.stateID)));
        mData->SetItem(1, new PyInt(data.important ? 1 : 0));
        mData->SetItem(2, new PyString(GetTypeLabel(data.typeID, data.storyline)));
        mData->SetItem(3, GetTitle(data));
        mData->SetItem(4, new PyInt(data.agentID));
        mData->SetItem(5, new PyLong((int64)data.expiryTime));
        mData->SetItem(6, BuildBookmarks(data));
        mData->SetItem(7, new PyBool(data.remoteOfferable));
        mData->SetItem(8, new PyBool(data.remoteCompletable));
    return mData;
}

namespace {
    PyObject* MakeAgentBookmark(const MissionOffer& data, const char* locationType, const char* hint, uint32 itemID, uint32 typeID,
                                uint32 systemID, const GPoint& pos, bool deadspace)
    {
        // the client's journal turns 'hint' into the bookmark name with localization.GetByLabel(hint, location=locationID, index=locationNumber + 1)
        PyDict* bm = new PyDict();
            bm->SetItemString("agentID", new PyInt(data.agentID));
            bm->SetItemString("locationType", new PyString(locationType));
            bm->SetItemString("locationNumber", PyStatic.NewZero());
            bm->SetItemString("hint", new PyString(hint));
            bm->SetItemString("itemID", new PyInt(itemID));
            bm->SetItemString("typeID", new PyInt(typeID));
            bm->SetItemString("locationID", new PyInt(systemID));
            bm->SetItemString("solarsystemID", new PyInt(systemID));
            bm->SetItemString("x", new PyFloat(pos.x));
            bm->SetItemString("y", new PyFloat(pos.y));
            bm->SetItemString("z", new PyFloat(pos.z));
            bm->SetItemString("ownerID", new PyInt(data.characterID));
            bm->SetItemString("memo", new PyString(""));
            bm->SetItemString("created", new PyLong((int64)(data.dateAccepted > 0 ? data.dateAccepted : data.dateIssued)));
            bm->SetItemString("flag", PyStatic.NewNone());
            if (deadspace)
                bm->SetItemString("deadspace", PyStatic.NewOne());
        return new PyObject("util.KeyVal", bm);
    }

    uint32 StationTypeID(uint32 stationID)
    {
        StationData data = StationData();
        if (stDataMgr.GetStationData(stationID, data))
            return data.typeID;
        return 0;
    }
}

PyList* MissionDataMgr::BuildBookmarks(MissionOffer& data)
{
    // bookmarks are made from the offer (never stored).  only accepted missions have them
    PyList* list = new PyList();
    if (data.stateID != Mission::State::Accepted)
        return list;

    Agent* pAgent(sEntityList.GetAgent(data.agentID));
    uint32 agentStationID(pAgent != nullptr ? pAgent->GetStationID() : 0);
    switch (data.typeID) {
        case Mission::Type::Courier: {
            list->AddItem(MakeAgentBookmark(data, "objective.source",
                    (data.originID == agentStationID ? "UI/Agents/Bookmarks/Objective/PickupAgentBase" : "UI/Agents/Bookmarks/Objective/Pickup"),
                    data.originID, StationTypeID(data.originID), data.originSystemID, NULL_ORIGIN, false));
            list->AddItem(MakeAgentBookmark(data, "objective.destination",
                    (data.destinationID == agentStationID ? "UI/Agents/Bookmarks/Objective/DropOffAgentBase" : "UI/Agents/Bookmarks/Objective/DropOff"),
                    data.destinationID, StationTypeID(data.destinationID), data.destinationSystemID, NULL_ORIGIN, false));
        } break;
        case Mission::Type::Encounter:
        case Mission::Type::Mining: {
            if (pAgent != nullptr)
                list->AddItem(MakeAgentBookmark(data, "agenthomebase", "UI/Agents/Bookmarks/AgentBase",
                        agentStationID, StationTypeID(agentStationID), pAgent->GetSystemID(), NULL_ORIGIN, false));
            if (data.dungeonSolarSystemID != 0)
                list->AddItem(MakeAgentBookmark(data, "dungeon",
                        (data.typeID == Mission::Type::Mining ? "UI/Agents/Bookmarks/Mining/Deadspace" : "UI/Agents/Bookmarks/Encounter/Deadspace"),
                        data.dungeonSolarSystemID, EVEDB::invTypes::SolarSystem, data.dungeonSolarSystemID, data.dungeonPos, true));
        } break;
        case Mission::Type::Trade: {
            if (pAgent != nullptr)
                list->AddItem(MakeAgentBookmark(data, "objective", "UI/Agents/Bookmarks/Objective/AgentBase",
                        agentStationID, StationTypeID(agentStationID), pAgent->GetSystemID(), NULL_ORIGIN, false));
        } break;
    }
    return list;
}

void MissionDataMgr::GetShipRestrictions(uint8 level, std::vector<uint32>& allowed, std::vector<uint32>& denied)
{
    using namespace EVEDB::invGroups;
    // capitals never use acceleration gates
    const uint32 capitals[] = { Titan, Dreadnought, Freighter, Carrier, Supercarrier, CapitalIndustrialShip, JumpFreighter };
    denied.insert(denied.end(), std::begin(capitals), std::end(capitals));
    if (level > 2)
        return;

    // level 1:  frigates, destroyers and cruisers (and the industrials of mining missions).  level 2 adds battlecruisers
    const uint32 small[] = { Frigate, Rookieship, AssaultShip, CovertOps, Interceptor, StealthBomber, ElecAttackShip,
        Prototype_Exploration_Ship, Shuttle, Destroyer, Interdictor, Cruiser, HeavyAssaultShip, Logistics, CombatRecon,
        833 /*Force Recon Ship*/, HeavyInterdictors, StrategicCruiser, Industrial, TransportShip, MiningBarge, Exhumer };
    allowed.insert(allowed.end(), std::begin(small), std::end(small));
    const uint32 big[] = { Battleship, EliteBattleship, BlackOps, Marauder, IndustrialCommandShip };
    denied.insert(denied.end(), std::begin(big), std::end(big));
    if (level == 2) {
        allowed.push_back(Battlecruiser);
        allowed.push_back(CommandShip);
    } else {
        denied.push_back(Battlecruiser);
        denied.push_back(CommandShip);
    }
}

void MissionDataMgr::UpdateMissionData(uint32 charID, MissionOffer& data)
{
    auto itr = m_offers.equal_range(charID);
    for (auto it = itr.first; it != itr.second; ++it)
        if (it->second.agentID == data.agentID) {
            it->second = data;
            break;
        }

    itr = m_aoffers.equal_range(data.agentID);
    for (auto it = itr.first; it != itr.second; ++it)
        if (it->second.characterID == charID) {
            it->second = data;
            break;
        }
}
