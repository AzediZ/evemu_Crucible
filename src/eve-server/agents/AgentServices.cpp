/**
 * @name AgentServices.cpp
 *   the non-mission agent services (F24):  R&D research projects and datacores, and locator agents
 */

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "agents/Agent.h"
#include "agents/AgentServices.h"
#include "character/CharacterDB.h"
#include "EVE_Mail.h"
#include "mail/NotificationMgr.h"
#include "station/StationDataMgr.h"
#include "../../eve-common/EVE_Agent.h"

/* dialogue helpers.  agentSays is (msgInfo, contentID):  a string is shown as it is, (label, kwargs) through GetByLabel */
static void SayLabel(PyTuple* agentSays, const char* label, PyDict* args = nullptr)
{
    PyTuple* msg = new PyTuple(2);
        msg->SetItem(0, new PyString(label));
        msg->SetItem(1, (args == nullptr) ? new PyDict() : args);
    agentSays->SetItem(0, msg);
    agentSays->SetItem(1, PyStatic.NewNone());
}

static void SayText(PyTuple* agentSays, const std::string& text)
{
    agentSays->SetItem(0, new PyString(text));
    agentSays->SetItem(1, PyStatic.NewNone());
}

// (actionID, buttonID):  a button labelled by buttonID that sends actionID
static void Button(PyList* dialog, int32 actionID, int32 buttonID)
{
    PyTuple* button = new PyTuple(2);
        button->SetItem(0, new PyInt(actionID));
        button->SetItem(1, new PyInt(buttonID));
    dialog->AddItem(button);
}

// (actionID, text):  a link in the conversation that sends actionID
static void Link(PyList* dialog, int32 actionID, const std::string& text)
{
    PyTuple* link = new PyTuple(2);
        link->SetItem(0, new PyInt(actionID));
        link->SetItem(1, new PyString(text));
    dialog->AddItem(link);
}


/*
 * ResearchAgentMgr
 */
void ResearchAgentMgr::Initialize()
{
    m_multipliers.clear();
    m_datacores.clear();
    AgentDB::LoadFieldMultipliers(m_multipliers);
    AgentDB::LoadDatacores(m_datacores);
    sLog.Cyan("   ResearchAgentMgr", "%u research field multipliers and %u datacores loaded.", (uint32)m_multipliers.size(), (uint32)m_datacores.size());
}

float ResearchAgentMgr::GetFieldMultiplier(uint16 skillID)
{
    if (!sConfig.agents.researchFieldMultipliers)
        return 1.0f;
    std::map<uint16, float>::iterator itr = m_multipliers.find(skillID);
    if (itr == m_multipliers.end())
        return 1.0f;
    return itr->second;
}

uint32 ResearchAgentMgr::GetDatacoreType(uint16 skillID)
{
    std::map<uint16, uint32>::iterator itr = m_datacores.find(skillID);
    if (itr == m_datacores.end())
        return 0;
    return itr->second;
}

double ResearchAgentMgr::GetRPPerDay(Agent* pAgent, Client* pClient, uint16 skillID)
{
    Character* pChar(pClient->GetChar().get());
    double agentSkill(pAgent->GetSkillLevel(skillID));
    double charSkill(pChar->GetSkillLevel(skillID));
    double standing(pAgent->GetEffectiveCharStanding(pClient));
    if (standing < -10.0)
        standing = -10.0;
    if (standing > 10.0)
        standing = 10.0;
    // agent quality is a fixed 20 since Incursion 1.5
    double eq(20.0 + 5.0 * pChar->GetSkillLevel(EvESkill::Negotiation) + standing);
    return pow(agentSkill + charSkill, 2) * (1.0 + eq / 100.0) * GetFieldMultiplier(skillID);
}

bool ResearchAgentMgr::Refresh(Client* pClient, Agent* pAgent, AgentDB::Research& data)
{
    uint32 charID(pClient->GetCharacterID());
    if (!AgentDB::GetResearch(charID, pAgent->GetID(), data))
        return false;
    int64 now((int64)GetFileTimeNow());
    if (now > data.lastUpdate)
        data.pointsBanked += data.pointsPerDay * double(now - data.lastUpdate) / EvE::Time::Day;
    // skills and standings may have changed since
    data.pointsPerDay = GetRPPerDay(pAgent, pClient, data.skillTypeID);
    data.lastUpdate = now;
    AgentDB::SaveResearch(charID, pAgent->GetID(), data);
    return true;
}

PyRep* ResearchAgentMgr::GetResearchData(Client* pClient, Agent* pAgent)
{
    AgentDB::Research data = AgentDB::Research();
    if (!Refresh(pClient, pAgent, data))
        return PyStatic.NewNone();
    PyDict* dict = new PyDict();
        dict->SetItemString("skillTypeID", new PyInt(data.skillTypeID));
        dict->SetItemString("points", new PyFloat(data.pointsBanked));
        dict->SetItemString("pointsPerDay", new PyFloat(data.pointsPerDay));
        dict->SetItemString("rpMultiplier", new PyFloat(GetFieldMultiplier(data.skillTypeID)));
    return dict;
}

PyList* ResearchAgentMgr::GetJournalRows(uint32 charID, uint32 agentID/*0*/)
{
    // journal.py:  agentID, typeID, ppd, points, level, quality, stationID = research[i]
    PyList* list = new PyList();
    std::map<uint32, AgentDB::Research> research;
    std::map<uint32, std::pair<uint8, uint32>> agents;
    AgentDB::GetResearchList(charID, research, agents);
    int64 now((int64)GetFileTimeNow());
    for (auto cur : research) {
        if ((agentID != 0) and (cur.first != agentID))
            continue;
        double points(cur.second.pointsBanked);
        if (now > cur.second.lastUpdate)
            points += cur.second.pointsPerDay * double(now - cur.second.lastUpdate) / EvE::Time::Day;
        PyTuple* row = new PyTuple(7);
            row->SetItem(0, new PyInt(cur.first));
            row->SetItem(1, new PyInt(cur.second.skillTypeID));
            row->SetItem(2, new PyFloat(cur.second.pointsPerDay));
            row->SetItem(3, new PyFloat(points));
            row->SetItem(4, new PyInt(agents[cur.first].first));
            row->SetItem(5, new PyInt(20));
            row->SetItem(6, new PyInt(agents[cur.first].second));
        list->AddItem(row);
    }
    return list;
}

void ResearchAgentMgr::AddInitialButtons(Client* pClient, Agent* pAgent, PyList* dialog)
{
    if (!pAgent->IsResearch())
        return;
    using namespace Dialog::Button;
    AgentDB::Research data = AgentDB::Research();
    if (AgentDB::GetResearch(pClient->GetCharacterID(), pAgent->GetID(), data)) {
        Button(dialog, CancelResearch, CancelResearch);
        Button(dialog, BuyDatacores, BuyDatacores);
    } else {
        Button(dialog, StartResearch, StartResearch);
    }
}

void ResearchAgentMgr::GetEligibleFields(Client* pClient, Agent* pAgent, std::vector<uint16>& into)
{
    Character* pChar(pClient->GetChar().get());
    for (auto cur : pAgent->GetSkills())
        if (pChar->GetSkillLevel(cur.first) > 0)
            into.push_back(cur.first);
}

bool ResearchAgentMgr::HandleAction(Client* pClient, Agent* pAgent, int32 actionID, PyTuple* agentSays, PyList* dialog)
{
    if (!pAgent->IsResearch())
        return false;

    using namespace Dialog::Button;
    uint32 charID(pClient->GetCharacterID());
    switch (actionID) {
        case StartResearch: {
            DoStartResearch(pClient, pAgent, agentSays, dialog);
        } break;
        case CancelResearch: {
            AgentDB::Research data = AgentDB::Research();
            if (!AgentDB::GetResearch(charID, pAgent->GetID(), data)) {
                SayText(agentSays, "We aren't working on anything together.");
                AddInitialButtons(pClient, pAgent, dialog);
                break;
            }
            SayLabel(agentSays, "UI/Agents/Research/SureToCancelResearch");
            ::Button(dialog, Dialog::Action::ResearchCancelYes, Yes);
        } break;
        case Dialog::Action::ResearchCancelYes: {
            AgentDB::DeleteResearch(charID, pAgent->GetID());
            _log(AGENT__MESSAGE, "%s cancelled research with agent %u.", pClient->GetName(), pAgent->GetID());
            SayLabel(agentSays, "UI/Agents/Research/ResearchCancelled");
            AddInitialButtons(pClient, pAgent, dialog);
        } break;
        case BuyDatacores: {
            ShowDatacores(pClient, pAgent, agentSays, dialog);
        } break;
        default: {
            if ((actionID > Dialog::Action::ResearchPickField) and (actionID <= Dialog::Action::ResearchPickField + 0xFFFF)) {
                PickField(pClient, pAgent, actionID - Dialog::Action::ResearchPickField, agentSays, dialog);
            } else if ((actionID > Dialog::Action::DatacoreBuyBase) and (actionID < Dialog::Action::DatacoreBuyBase + 10000)) {
                DoBuyDatacores(pClient, pAgent, actionID - Dialog::Action::DatacoreBuyBase, agentSays, dialog);
            } else {
                return false;
            }
        }
    }
    return true;
}

void ResearchAgentMgr::DoStartResearch(Client* pClient, Agent* pAgent, PyTuple* agentSays, PyList* dialog)
{
    uint32 charID(pClient->GetCharacterID());
    AgentDB::Research data = AgentDB::Research();
    if (AgentDB::GetResearch(charID, pAgent->GetID(), data)) {
        SayLabel(agentSays, "UI/Agents/Research/ResearchStarted");
        AddInitialButtons(pClient, pAgent, dialog);
        return;
    }
    // one project, plus one per level of Research Project Management
    Character* pChar(pClient->GetChar().get());
    if (AgentDB::CountResearch(charID) >= (uint32)(1 + pChar->GetSkillLevel(EvESkill::ResearchProjectManagement))) {
        SayLabel(agentSays, "UI/Agents/Research/TooLowRpm");
        return;
    }
    std::vector<uint16> fields;
    GetEligibleFields(pClient, pAgent, fields);
    if (fields.empty()) {
        SayLabel(agentSays, "UI/Agents/Research/CharacterSkillsNotSufficient");
        return;
    }

    // one link per field the pilot can research (the client's SingleChoiceBox isn't available to us)
    SayLabel(agentSays, "UI/Agents/Research/SelectResearchTypeMessage");
    for (auto cur : fields) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f", GetRPPerDay(pAgent, pClient, cur));
        std::string text(sDataMgr.GetTypeName(cur));
        text += " (my level ";
        text += std::to_string(pAgent->GetSkillLevel(cur));
        text += ", yours ";
        text += std::to_string(pChar->GetSkillLevel(cur));
        text += "):  ";
        text += buf;
        text += " RP/day";
        Link(dialog, Dialog::Action::ResearchPickField + cur, text);
    }
}

void ResearchAgentMgr::PickField(Client* pClient, Agent* pAgent, uint16 skillID, PyTuple* agentSays, PyList* dialog)
{
    uint32 charID(pClient->GetCharacterID());
    AgentDB::Research data = AgentDB::Research();
    if (AgentDB::GetResearch(charID, pAgent->GetID(), data)) {
        SayLabel(agentSays, "UI/Agents/Research/ResearchStarted");
        AddInitialButtons(pClient, pAgent, dialog);
        return;
    }
    Character* pChar(pClient->GetChar().get());
    if (AgentDB::CountResearch(charID) >= (uint32)(1 + pChar->GetSkillLevel(EvESkill::ResearchProjectManagement))) {
        SayLabel(agentSays, "UI/Agents/Research/TooLowRpm");
        return;
    }
    if ((pAgent->GetSkillLevel(skillID) == 0) or (pChar->GetSkillLevel(skillID) < 1)) {
        SayLabel(agentSays, "UI/Agents/Research/CharacterSkillsNotSufficient");
        return;
    }

    int64 now((int64)GetFileTimeNow());
    data.skillTypeID = skillID;
    data.pointsBanked = 0.0;
    data.pointsPerDay = GetRPPerDay(pAgent, pClient, skillID);
    data.lastUpdate = now;
    data.startDate = now;
    AgentDB::SaveResearch(charID, pAgent->GetID(), data);
    _log(AGENT__MESSAGE, "%s started %s research with agent %u at %.2f RP/day.", pClient->GetName(), sDataMgr.GetTypeName(skillID), pAgent->GetID(), data.pointsPerDay);

    SayLabel(agentSays, "UI/Agents/Research/ResearchStarted");
    AddInitialButtons(pClient, pAgent, dialog);
}

void ResearchAgentMgr::ShowDatacores(Client* pClient, Agent* pAgent, PyTuple* agentSays, PyList* dialog)
{
    AgentDB::Research data = AgentDB::Research();
    if (!Refresh(pClient, pAgent, data)) {
        SayText(agentSays, "We aren't working on anything together.");
        AddInitialButtons(pClient, pAgent, dialog);
        return;
    }
    uint32 dcType(GetDatacoreType(data.skillTypeID));
    if (dcType == 0) {
        SayLabel(agentSays, "UI/Agents/Research/DatacoresNoQuota");
        return;
    }
    uint32 price(sConfig.agents.researchDatacoreRP);
    if (price < 1)
        price = 1;
    uint32 most((uint32)(data.pointsBanked / price));
    if (most > 9999)
        most = 9999;
    PyDict* args = new PyDict();
        args->SetItemString("datacoreTypeID", new PyInt(dcType));
        args->SetItemString("rpAmount", new PyInt(price));
    if (most == 0) {
        SayLabel(agentSays, "UI/Agents/Research/NotEnoughRPForDatacores", args);
        return;
    }

    // quantity links (the client's GetQuantity popup isn't available to us)
    SayLabel(agentSays, "UI/Agents/Research/DatacorePrice", args);
    std::vector<uint32> amounts = {1, 5, 10};
    for (auto cur : amounts)
        if (cur < most)
            Link(dialog, Dialog::Action::DatacoreBuyBase + cur, "Buy " + std::to_string(cur) + " (" + std::to_string(cur * price) + " RP)");
    Link(dialog, Dialog::Action::DatacoreBuyBase + most, "Buy " + std::to_string(most) + " (" + std::to_string(most * price) + " RP, all you can afford)");
}

void ResearchAgentMgr::DoBuyDatacores(Client* pClient, Agent* pAgent, uint32 qty, PyTuple* agentSays, PyList* dialog)
{
    uint32 charID(pClient->GetCharacterID());
    AgentDB::Research data = AgentDB::Research();
    if (!Refresh(pClient, pAgent, data)) {
        SayText(agentSays, "We aren't working on anything together.");
        AddInitialButtons(pClient, pAgent, dialog);
        return;
    }
    uint32 dcType(GetDatacoreType(data.skillTypeID));
    if (dcType == 0) {
        SayLabel(agentSays, "UI/Agents/Research/DatacoresNoQuota");
        return;
    }
    uint32 price(sConfig.agents.researchDatacoreRP);
    if (price < 1)
        price = 1;
    if ((qty < 1) or (qty > 9999) or (double(qty) * price > data.pointsBanked + 1e-6)) {
        SayLabel(agentSays, "UI/Agents/Research/DatacoreInvalidInput");
        return;
    }
    // datacores go to the agent's station
    uint32 stationID(pAgent->GetStationID());
    if ((stationID == 0) or !sDataMgr.IsStation(stationID)) {
        SayText(agentSays, "I have no station to deliver datacores to.");
        return;
    }

    // ISK on top (Inferno; 0 at Crucible)
    double isk(sConfig.agents.researchDatacoreISK * qty);
    if (isk > 0.0) {
        if (pClient->GetBalance() < isk) {
            SayText(agentSays, "Those datacores would cost you " + std::to_string((int64)isk) + " ISK, which you don't have.");
            return;
        }
        AccountService::TransferFunds(charID, pAgent->GetCorpID(), isk, "Datacore purchase", Journal::EntryType::AgentMiscellaneous, pAgent->GetID());
    }

    data.pointsBanked -= double(qty) * price;
    if (data.pointsBanked < 0.0)
        data.pointsBanked = 0.0;
    AgentDB::SaveResearch(charID, pAgent->GetID(), data);

    sItemFactory.SetUsingClient(pClient);
    ItemData idata(dcType, charID, locTemp, flagNone, qty);
    InventoryItemRef iRef = sItemFactory.SpawnItem(idata);
    if (iRef.get() != nullptr)
        iRef->Move(stationID, flagHangar, true);
    sItemFactory.UnsetUsingClient();
    _log(AGENT__MESSAGE, "%s bought %u x %s from agent %u.", pClient->GetName(), qty, sDataMgr.GetTypeName(dcType), pAgent->GetID());

    SayLabel(agentSays, "UI/Agents/Research/BoughtDatacores");
    AddInitialButtons(pClient, pAgent, dialog);
}


/*
 * LocatorAgentMgr
 */
bool LocatorAgentMgr::GetRate(uint8 level, uint8 range, uint32& cost, uint32& delay)
{
    /* EVE University "Locator Agents" (unchanged since before Crucible)
     *  range:    same system   same constellation   same region   other region
     *  L1        1k, 0s        5k, 60s              -             -
     *  L2        5k, 0s        10k, 20s             25k, 120s     -
     *  L3        10k, 0s       25k, 30s             50k, 240s     100k, 480s
     *  L4        25k, 0s       50k, 20s             100k, 120s    250k, 240s
     */
    static const uint32 table[4][4][2] = {
        {{1000, 0},  {5000, 60},  {0, 0},      {0, 0}},
        {{5000, 0},  {10000, 20}, {25000, 120}, {0, 0}},
        {{10000, 0}, {25000, 30}, {50000, 240}, {100000, 480}},
        {{25000, 0}, {50000, 20}, {100000, 120}, {250000, 240}}
    };
    if (range > 3)
        return false;
    uint8 idx(level < 1 ? 0 : (level > 4 ? 3 : level - 1));
    cost = table[idx][range][0];
    delay = table[idx][range][1];
    return (cost > 0);
}

uint16 LocatorAgentMgr::GetCooldownMinutes(uint8 level)
{
    if (level <= 2)
        return 5;
    if (level == 3)
        return 15;
    return 30;
}

bool LocatorAgentMgr::IsBusy(uint32 charID, uint32 agentID)
{
    for (auto cur : m_pending)
        if ((cur.second.charID == charID) and (cur.second.agentID == agentID))
            return true;
    return false;
}

void LocatorAgentMgr::GetServiceInfo(Client* pClient, Agent* pAgent, PyDict* into)
{
    uint32 charID(pClient->GetCharacterID());
    PyList* delays = new PyList();
    for (uint8 i = 0; i < 4; ++i) {
        uint32 cost(0), delay(0);
        if (!GetRate(pAgent->GetLevel(), i, cost, delay))
            continue;
        PyTuple* row = new PyTuple(3);
            row->SetItem(0, new PyInt(i));
            row->SetItem(1, new PyInt(delay));
            row->SetItem(2, new PyInt(cost));
        delays->AddItem(row);
    }
    into->SetItemString("frequency", new PyLong(GetCooldownMinutes(pAgent->GetLevel()) * EvE::Time::Minute));
    into->SetItemString("delays", delays);
    into->SetItemString("callbackID", IsBusy(charID, pAgent->GetID()) ? (PyRep*)new PyBool(true) : PyStatic.NewNone());
    std::map<std::pair<uint32, uint32>, int64>::iterator itr = m_lastUsed.find(std::make_pair(charID, pAgent->GetID()));
    into->SetItemString("lastUsed", (itr == m_lastUsed.end()) ? PyStatic.NewNone() : (PyRep*)new PyLong(itr->second));
}

bool LocatorAgentMgr::GetTarget(uint32 targetID, Target& into)
{
    // online:  where the pilot is now.  offline:  where the pilot logged off
    Client* pTarget(sEntityList.FindClientByCharID(targetID));
    if (pTarget != nullptr) {
        into.stationID = (pTarget->IsDocked() ? pTarget->GetStationID() : 0);
        into.systemID = pTarget->GetSystemID();
    } else {
        CharacterData data = CharacterData();
        if (!CharacterDB::GetCharacterData(targetID, data))
            return false;
        into.stationID = data.stationID;
        into.systemID = data.solarSystemID;
    }
    SystemData sData = SystemData();
    if ((into.systemID == 0) or !sDataMgr.GetSystemData(into.systemID, sData))
        return false;
    into.constellationID = sData.constellationID;
    into.regionID = sData.regionID;
    return true;
}

uint8 LocatorAgentMgr::GetRange(Agent* pAgent, const Target& target)
{
    SystemData sData = SystemData();
    sDataMgr.GetSystemData(pAgent->GetSystemID(), sData);
    if (target.systemID == pAgent->GetSystemID())
        return 0;
    if (target.constellationID == sData.constellationID)
        return 1;
    if (target.regionID == sData.regionID)
        return 2;
    return 3;
}

PyTuple* LocatorAgentMgr::GetLocationString(Agent* pAgent, uint32 targetID, const Target& target)
{
    // the client's FormatLocateCharNotification wording, with names
    SystemData sData = SystemData();
    sDataMgr.GetSystemData(pAgent->GetSystemID(), sData);
    bool otherRegion(target.regionID != sData.regionID);
    bool otherConst(target.constellationID != sData.constellationID);

    PyDict* args = new PyDict();
        args->SetItemString("charID", new PyInt(targetID));
    const char* label(nullptr);
    if ((target.stationID != 0) and (target.stationID == pAgent->GetStationID())) {
        label = "UI/Agents/Locator/InYourStation";
    } else if ((target.stationID != 0) and (target.systemID == pAgent->GetSystemID())) {
        label = "UI/Agents/Locator/InYourSystemInStation";
        args->SetItemString("stationName", new PyString(stDataMgr.GetStationName(target.stationID)));
    } else if (target.systemID == pAgent->GetSystemID()) {
        label = "UI/Agents/Locator/InYourSystem";
    } else {
        args->SetItemString("systemName", new PyString(sDataMgr.GetSystemName(target.systemID)));
        if (otherConst or otherRegion)
            args->SetItemString("constellationName", new PyString(AgentDB::GetMapName(target.constellationID)));
        if (otherRegion)
            args->SetItemString("regionName", new PyString(AgentDB::GetMapName(target.regionID)));
        if (target.stationID == 0) {
            label = (otherRegion ? "UI/Agents/Locator/InOtherRegion"
                    : (otherConst ? "UI/Agents/Locator/InOtherConstellation" : "UI/Agents/Locator/InOtherSystem"));
        } else {
            args->SetItemString("stationName", new PyString(stDataMgr.GetStationName(target.stationID)));
            label = (otherRegion ? "UI/Agents/Locator/InOtherRegionInStation"
                    : (otherConst ? "UI/Agents/Locator/InOtherConstellationInStation" : "UI/Agents/Locator/InOtherSystemInStation"));
        }
    }
    PyTuple* rsp = new PyTuple(2);
        rsp->SetItem(0, new PyString(label));
        rsp->SetItem(1, args);
    return rsp;
}

PyDict* LocatorAgentMgr::GetNotifyData(Agent* pAgent, uint32 targetID, const Target& target)
{
    /* LocateChar (69) notification data (notificationUtil.FormatLocateCharNotification):
     *  characterID, messageIndex (0/1 intro text), agentLocation {groupSolarSystem: name, groupStation: name},
     *  targetLocation {groupRegion: name, groupConstellation: name (only when not the agent's), groupSolarSystem, groupStation (when docked)}.
     *  the client compares the agent's and the target's entries and prints them as they are, so they hold names
     */
    SystemData sData = SystemData();
    sDataMgr.GetSystemData(pAgent->GetSystemID(), sData);
    PyDict* agentLoc = new PyDict();
        agentLoc->SetItem(new PyInt(EVEDB::invGroups::Solar_System), new PyString(sDataMgr.GetSystemName(pAgent->GetSystemID())));
    if (pAgent->GetStationID() != 0)
        agentLoc->SetItem(new PyInt(EVEDB::invGroups::Station), new PyString(stDataMgr.GetStationName(pAgent->GetStationID())));
    PyDict* targetLoc = new PyDict();
        targetLoc->SetItem(new PyInt(EVEDB::invGroups::Solar_System), new PyString(sDataMgr.GetSystemName(target.systemID)));
    if (target.stationID != 0)
        targetLoc->SetItem(new PyInt(EVEDB::invGroups::Station), new PyString(stDataMgr.GetStationName(target.stationID)));
    if (target.regionID != sData.regionID) {
        targetLoc->SetItem(new PyInt(EVEDB::invGroups::Region), new PyString(AgentDB::GetMapName(target.regionID)));
        targetLoc->SetItem(new PyInt(EVEDB::invGroups::Constellation), new PyString(AgentDB::GetMapName(target.constellationID)));
    } else if (target.constellationID != sData.constellationID) {
        targetLoc->SetItem(new PyInt(EVEDB::invGroups::Constellation), new PyString(AgentDB::GetMapName(target.constellationID)));
    }
    PyDict* data = new PyDict();
        data->SetItemString("characterID", new PyInt(targetID));
        data->SetItemString("messageIndex", new PyInt((int32)MakeRandomInt(0, 1)));
        data->SetItemString("agentLocation", agentLoc);
        data->SetItemString("targetLocation", targetLoc);
    return data;
}

bool LocatorAgentMgr::Quote(Client* pClient, Agent* pAgent, uint32 targetID, Target& target, uint32& cost, uint32& delay, PyTuple* agentSays)
{
    uint32 charID(pClient->GetCharacterID());
    if (targetID == charID) {
        SayLabel(agentSays, "UI/Agents/Locator/CantLocateSelf");
        return false;
    }
    if (IsAgent(targetID)) {
        SayLabel(agentSays, "UI/Agents/Locator/CantLocateAgent");
        return false;
    }
    if (!IsCharacterID(targetID) or !GetTarget(targetID, target)) {
        PyDict* args = new PyDict();
            args->SetItemString("characterSearch", new PyString(std::to_string(targetID)));
        SayLabel(agentSays, "UI/Agents/Locator/CharacterDoesntExist", args);
        return false;
    }
    // one search at a time, then the agent's cooldown
    std::map<std::pair<uint32, uint32>, int64>::iterator itr = m_lastUsed.find(std::make_pair(charID, pAgent->GetID()));
    if (IsBusy(charID, pAgent->GetID()) or ((itr != m_lastUsed.end())
    and ((int64)GetFileTimeNow() < itr->second + GetCooldownMinutes(pAgent->GetLevel()) * EvE::Time::Minute))) {
        SayLabel(agentSays, "UI/Agents/Locator/BusyCantLocate");
        return false;
    }
    // wormhole space is out of everyone's reach
    if (IsWSpaceID(target.systemID) or !GetRate(pAgent->GetLevel(), GetRange(pAgent, target), cost, delay)) {
        PyDict* args = new PyDict();
            args->SetItemString("locateCharID", new PyInt(targetID));
        SayLabel(agentSays, "UI/Agents/Locator/CharacterOutOfRange", args);
        return false;
    }
    return true;
}

bool LocatorAgentMgr::HandleAction(Client* pClient, Agent* pAgent, int32 actionID, PyTuple* agentSays, PyList* dialog)
{
    if (!pAgent->IsLocator() or !sConfig.agents.locatorEnabled)
        return false;

    using namespace Dialog::Button;
    uint32 charID(pClient->GetCharacterID());
    if ((actionID == LocateCharacter) or (actionID == LocateAccept)) {
        std::map<std::pair<uint32, uint32>, int64>::iterator itr = m_lastUsed.find(std::make_pair(charID, pAgent->GetID()));
        if (IsBusy(charID, pAgent->GetID()) or ((itr != m_lastUsed.end())
        and ((int64)GetFileTimeNow() < itr->second + GetCooldownMinutes(pAgent->GetLevel()) * EvE::Time::Minute))) {
            SayLabel(agentSays, "UI/Agents/Locator/BusyCantLocate");
            return true;
        }
        // the pilot's contacts (the client's name popup isn't available to us)
        std::vector<std::pair<uint32, std::string>> candidates;
        AgentDB::GetLocateCandidates(charID, candidates);
        if (candidates.empty()) {
            SayText(agentSays, "Who do you want to find?  Add the pilot to your contacts first, then ask me again.");
            return true;
        }
        SayLabel(agentSays, "UI/Agents/Locator/WhoDoYouWantToFind");
        for (auto cur : candidates)
            Link(dialog, Dialog::Action::LocatePickBase + cur.first, cur.second);
        return true;
    }
    if (actionID == LocateReject) {
        SayText(agentSays, "Suit yourself.");
        return true;
    }

    if ((actionID > Dialog::Action::LocatePickBase) and (actionID < Dialog::Action::LocateAcceptBase)) {
        uint32 targetID(actionID - Dialog::Action::LocatePickBase);
        Target target = Target();
        uint32 cost(0), delay(0);
        if (!Quote(pClient, pAgent, targetID, target, cost, delay, agentSays))
            return true;
        PyDict* args = new PyDict();
            args->SetItemString("locateCharID", new PyInt(targetID));
            args->SetItemString("iskAmount", new PyFloat(cost));
        SayLabel(agentSays, "UI/Agents/Locator/LocateCharacterPrice", args);
        ::Button(dialog, Dialog::Action::LocateAcceptBase + targetID, LocateAccept);
        ::Button(dialog, LocateReject, LocateReject);
        return true;
    }

    if (actionID > Dialog::Action::LocateAcceptBase) {
        uint32 targetID(actionID - Dialog::Action::LocateAcceptBase);
        Target target = Target();
        uint32 cost(0), delay(0);
        if (!Quote(pClient, pAgent, targetID, target, cost, delay, agentSays))
            return true;
        if (pClient->GetBalance() < cost) {
            PyDict* args = new PyDict();
                args->SetItemString("iskAmount", new PyFloat(cost));
            SayLabel(agentSays, "UI/Agents/Locator/TooPoorToLocate", args);
            return true;
        }
        AccountService::TransferFunds(charID, pAgent->GetCorpID(), cost, "Locator agent fee", Journal::EntryType::AgentLocationServices, pAgent->GetID());
        int64 now((int64)GetFileTimeNow());
        m_lastUsed[std::make_pair(charID, pAgent->GetID())] = now;
        _log(AGENT__MESSAGE, "%s paid agent %u %u ISK to locate %u (%us).", pClient->GetName(), pAgent->GetID(), cost, targetID, delay);

        PyDict* args = new PyDict();
            args->SetItemString("locateCharID", new PyInt(targetID));
        if (delay == 0) {
            args->SetItemString("locationString", GetLocationString(pAgent, targetID, target));
            SayLabel(agentSays, "UI/Agents/Locator/FoundCharacterImmediately", args);
        } else {
            // the result describes where the target was when the search was paid for
            Pending search = Pending();
                search.charID = charID;
                search.agentID = pAgent->GetID();
                search.targetID = targetID;
                search.data = GetNotifyData(pAgent, targetID, target);
            m_pending.emplace(now + delay * EvE::Time::Second, search);
            SayLabel(agentSays, "UI/Agents/Locator/WillFindCharacterWithDelay", args);
        }
        return true;
    }

    return false;
}

void LocatorAgentMgr::Process()
{
    if (m_pending.empty())
        return;
    int64 now((int64)GetFileTimeNow());
    std::multimap<int64, Pending>::iterator itr = m_pending.begin();
    while ((itr != m_pending.end()) and (itr->first <= now)) {
        // delivered as a LocateChar notification from the agent (persistent, shown at the next login if offline)
        NotificationMgr::Send(itr->second.charID, Notify::Types::LocateChar, itr->second.agentID, itr->second.data);
        itr = m_pending.erase(itr);
    }
}
