
 /**
  * @name AgentBound.cpp
  *   agent specific code
  *    removed from AgentMgrService.cpp
  *
  * @Author:        Allan
  * @date:      26 June 2018
  *
  */


/*
 * # Agent Logging:
 * AGENT__ERROR
 * AGENT__WARNING
 * AGENT__MESSAGE
 * AGENT__DEBUG
 * AGENT__INFO
 * AGENT__TRACE
 * AGENT__DUMP
 * AGENT__RSP_DUMP
 */

#include "eve-server.h"
#include "../../eve-common/EVE_Missions.h"
//#include "../../eve-common/EVE_Skills.h"
#include "../../eve-common/EVE_Standings.h"

#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "corporation/LPService.h"
#include "agents/AgentBound.h"
#include "agents/AgentMgrService.h"
#include "fleet/FleetService.h"
#include "missions/MissionInstance.h"
#include "missions/MissionObjectives.h"
#include "ship/BeyonceService.h"
#include "station/Station.h"
#include "station/StationDataMgr.h"
#include "services/ServiceManager.h"
#include "system/DestinyManager.h"
#include "system/Interdiction.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/DungeonMgr.h"

AgentBound::AgentBound(EVEServiceManager& mgr, AgentMgrService& parent, Agent *agt) :
    EVEBoundObject(mgr, parent),
    m_agent(agt)
{
    this->Add("DoAction", &AgentBound::DoAction);
    this->Add("GetAgentLocationWrap", &AgentBound::GetAgentLocationWrap);
    this->Add("GetInfoServiceDetails", &AgentBound::GetInfoServiceDetails);
    this->Add("GetMissionBriefingInfo", &AgentBound::GetMissionBriefingInfo);
    this->Add("GetMissionObjectiveInfo", &AgentBound::GetMissionObjectiveInfo);
    this->Add("GetMissionKeywords", &AgentBound::GetMissionKeywords);
    this->Add("GetMissionJournalInfo", &AgentBound::GetMissionJournalInfo);
    this->Add("GetDungeonShipRestrictions", &AgentBound::GetDungeonShipRestrictions);
    this->Add("RemoveOfferFromJournal", &AgentBound::RemoveOfferFromJournal);
    this->Add("GetOfferJournalInfo", &AgentBound::GetOfferJournalInfo);
    this->Add("GetEntryPoint", &AgentBound::GetEntryPoint);
    this->Add("GotoLocation", &AgentBound::GotoLocation);
    this->Add("WarpToLocation", &AgentBound::WarpToLocation);
    this->Add("GetMyJournalDetails", &AgentBound::GetMyJournalDetails);
}
PyResult AgentBound::GetAgentLocationWrap(PyCallArgs &call) {
    // this is detailed info on agent's location
    return m_agent->GetLocationWrap();
}

PyResult AgentBound::GetInfoServiceDetails(PyCallArgs& call) {
    // this is agents personal info... level, division, station, etc.
    return m_agent->GetInfoServiceDetails();
}


namespace {
    // adds a (buttonID, buttonID) dialog button
    void AddButton(PyList* dialog, int32 buttonID) {
        PyTuple* button = new PyTuple(2);
            button->SetItem(0, new PyInt(buttonID));
            button->SetItem(1, new PyInt(buttonID));
        dialog->AddItem(button);
    }
}

bool AgentBound::IsAtAgent(Client* pClient)
{
    return ((uint32)pClient->GetStationID() == m_agent->GetStationID());
}

uint8 AgentBound::GetCompleteButton(Client* pClient, MissionOffer& offer)
{
    using namespace Dialog::Button;
    if ((offer.stateID != Mission::State::Accepted) or !pClient->IsMissionComplete(offer))
        return 0;
    // in person at the agent, or where the objective is turned in (courier drop-off)
    uint32 locationID(MissionObjectives::GetDeliveryLocation(offer));
    if (IsAtAgent(pClient) or ((locationID != 0) and ((uint32)pClient->GetStationID() == locationID)))
        return Complete;
    if (offer.remoteCompletable)
        return CompleteRemotely;
    return 0;
}

void AgentBound::AddMissionButtons(Client* pClient, MissionOffer& offer, PyList* dialog)
{
    using namespace Dialog::Button;
    if (offer.stateID < Mission::State::Accepted) {
        if (IsAtAgent(pClient)) {
            AddButton(dialog, Accept);
        } else if (offer.remoteOfferable) {
            AddButton(dialog, AcceptRemotely);
        }
        AddButton(dialog, Decline);
        AddButton(dialog, Defer);
    } else if (offer.stateID == Mission::State::Accepted) {
        uint8 button(GetCompleteButton(pClient, offer));
        if (button != 0)
            AddButton(dialog, button);
        AddButton(dialog, Quit);
    }
}

int64 AgentBound::GetDeclineWait(uint32 charID)
{
    int64 last(MissionDB::GetLastDecline(charID, m_agent->GetID()));
    if (last == 0)
        return 0;
    int64 wait(last + (sConfig.missions.DeclinePenaltyHours * EvE::Time::Hour) - GetFileTimeNow());
    return (wait > 0 ? wait : 0);
}

PyResult AgentBound::DoAction(PyCallArgs &call, std::optional <PyInt*> actionID) {
    // this is first call when initiating agent convo
    _log(AGENT__DUMP,  "AgentBound::Handle_DoAction() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    // this actually returns a complicated tuple depending on other variables involving this agent and char.
    /*
        agentSays, dialogue, extraInfo = self.__GetConversation(agentDialogueWindow, actionID)

    def __GetConversation(self, wnd, actionID):
       tmp = wnd.sr.agentMoniker.DoAction(actionID)
        ret, wnd.sr.oob = tmp
        agentSays, wnd.sr.dialogue = ret
        firstActionID = wnd.sr.dialogue[0][0]
        firstActionDialogue = wnd.sr.dialogue[0][1]
        wnd.sr.agentSays = self.ProcessMessage(agentSays, wnd.sr.agentID)
        return (wnd.sr.agentSays, wnd.sr.dialogue, wnd.sr.oob)
     *
     *  agentSays is (msgInfo, contentID):  msgInfo is a messageID, a string (shown as it is), or (label, kwargs).
     *   contentID is our charID (the client asks GetMissionKeywords(contentID) for the messages' keywords)
     */

    Character* pchar = call.client->GetChar().get();
    uint32 charID(pchar->itemID());

    bool missionQuit = false, missionCompleted = false, missionDeclined = false;
    // set by mission actions that refuse the request
    const char* errorKey(nullptr);
    std::string errorText;

    PyTuple* agentSays = new PyTuple(2);
    // dialog is button info
    PyList* dialog = new PyList();

    using namespace Dialog::Button;

    // to set 'admin dialog options' (which i dont know wtf they are yet), i *think* you add a tuple of *something* that is NOT dict or int.
    if (false /*admin options*/) {
        PyTuple* adminButton = new PyTuple(2);
        adminButton->SetItem(0, new PyInt(Admin));
        adminButton->SetItem(1, new PyString("Admin Options"));
        dialog->AddItem(adminButton);
    }

    if (m_agent->CanUseAgent(call.client)) {
        switch (actionID.has_value() ? actionID.value()->value() : 0) {
            case 0: {
                //  if char has current mission with this agent, add this one.
                MissionOffer offer = MissionOffer();
                if (m_agent->HasMission(charID, offer)) {
                    AddButton(dialog, ViewMission);
                    uint8 button(GetCompleteButton(call.client, offer));
                    if (button != 0)
                        AddButton(dialog, button);
                    agentSays->SetItem(0, MissionDataMgr::GetBriefing(offer));
                    agentSays->SetItem(1, new PyInt(offer.characterID));
                } else if (m_agent->GetMissionType() == 0) {
                    // storyline agents without a storyline offer for this pilot, and agents we don't serve
                    agentSays->SetItem(0, new PyString(m_agent->GetNoMissionRsp(charID)));
                    agentSays->SetItem(1, PyStatic.NewNone());
                } else {
                    // dialogue data.  if RequestMission is only option, client auto-responds with DoAction(RequestMission optionID)
                    AddButton(dialog, RequestMission);
                    std::string response = "Why the fuck am I looking at you again, ";
                    response += call.client->GetName();
                    response += "?";
                    agentSays->SetItem(0, new PyString(response));  //msgInfo  -- if tuple[0].string then return msgInfo
                    agentSays->SetItem(1, PyStatic.NewNone());      // ContentID  -- PyNone used when msgInfo is string (mostly for initial greetings)
                }

                // if agent does location, add this one...
                if (m_agent->IsLocator())
                    AddButton(dialog, LocateCharacter);
                // if agent does research, add this one...
                if (m_agent->IsResearch())
                    AddButton(dialog, StartResearch);
            } break;
            case RequestMission:    //2
            case ViewMission: {     //1
                MissionOffer offer = MissionOffer();
                if (!m_agent->HasMission(charID, offer)) {
                    if (!m_agent->MakeOffer(charID, offer)) {
                        agentSays->SetItem(0, new PyString(m_agent->GetNoMissionRsp(charID)));
                        agentSays->SetItem(1, PyStatic.NewNone());
                        break;
                    }
                    m_agent->SendMissionUpdate(call.client, "offered");
                }
                // one offer per agent.  answer with it instead of making (and spawning cargo for) another one
                agentSays->SetItem(0, MissionDataMgr::GetBriefing(offer));
                agentSays->SetItem(1, new PyInt(offer.characterID));
                AddMissionButtons(call.client, offer, dialog);
            } break;
            case Accept:            //3
            case AcceptRemotely: {  //5
                MissionOffer offer = MissionOffer();
                // accepting twice (or after Defer) used to spawn the courier cargo again
                if (!m_agent->HasMission(charID, offer) or (offer.stateID >= Mission::State::Accepted)) {
                    errorKey = "MisNoSuchMission";
                    break;
                }
                if (offer.expiryTime < GetFileTimeNow()) {
                    errorKey = "MisMissionExpired";
                    break;
                }
                if (!IsAtAgent(call.client) and !offer.remoteOfferable) {
                    errorText = "This mission cannot be accepted remotely; go to " + m_agent->GetName() + "'s location and ask in person.";
                    break;
                }
                offer.stateID = Mission::State::Accepted;
                offer.dateAccepted = GetFileTimeNow();
                offer.expiryTime = GetFileTimeNow() + (sConfig.missions.AcceptedExpiryHours * EvE::Time::Hour);
                if (MissionObjectives::SpawnsCargoOnAccept(offer) and offer.courierTypeID) {
                    // add item to players hangar
                    sItemFactory.SetUsingClient(call.client);
                    ItemData data(offer.courierTypeID, charID, locTemp, flagNone, offer.courierAmount);
                    InventoryItemRef iRef = sItemFactory.SpawnItem(data);
                    if (iRef.get() != nullptr)
                        iRef->Move(offer.originID, flagHangar, true);
                    sItemFactory.UnsetUsingClient();
                }
                m_agent->UpdateOffer(charID, offer);
                m_agent->SendMissionUpdate(call.client, "offer_accepted");
                agentSays->SetItem(0, new PyInt(m_agent->GetAcceptRsp(charID)));
                agentSays->SetItem(1, new PyInt(charID));
                AddMissionButtons(call.client, offer, dialog);
            } break;
            case Complete:              //6
            case CompleteRemotely: {    //7
                //  need to verify all requirements have been met.
                MissionOffer offer = MissionOffer();
                if (!m_agent->HasMission(charID, offer) or (offer.stateID != Mission::State::Accepted)) {
                    errorKey = "MisNoSuchMission";
                    break;
                }
                // MissionDataMgr::Process() fails these, but only once a minute
                if (offer.expiryTime < GetFileTimeNow()) {
                    errorKey = "MisMissionExpired";
                    break;
                }
                switch (MissionObjectives::Check(call.client, offer)) {
                    case Mission::Objective::Complete:
                        break;
                    case Mission::Objective::WrongLocation:
                    case Mission::Objective::MissingItems:
                        if (offer.typeID == Mission::Type::Courier) {
                            errorKey = "MisPackageNotHere";
                        } else {
                            errorText = "You have not completed the mission objectives yet.";
                        }
                        break;
                    default:
                        errorText = "You have not completed the mission objectives yet.";
                        break;
                }
                if ((errorKey != nullptr) or !errorText.empty())
                    break;
                // away from the agent and the drop-off, only missions that can be completed remotely
                if (GetCompleteButton(call.client, offer) == 0) {
                    errorText = "This mission cannot be completed remotely; go to " + m_agent->GetName() + "'s location.";
                    break;
                }
                // take the delivered items (all or nothing) before any reward is given
                if (!MissionObjectives::Consume(call.client, offer)) {
                    errorText = "You have not completed the mission objectives yet.";
                    break;
                }
                missionCompleted = true;
                offer.stateID = Mission::State::Completed;
                offer.dateCompleted = GetFileTimeNow();
                m_agent->UpdateOffer(charID, offer);
                m_agent->SendMissionUpdate(call.client, "completed");
                agentSays->SetItem(0, new PyInt(m_agent->GetCompleteRsp(charID)));
                agentSays->SetItem(1, new PyInt(charID));
                if (offer.rewardItemID) {
                    // add reward item to players hangar
                    sItemFactory.SetUsingClient(call.client);
                    ItemData data(offer.rewardItemID, charID, locTemp, flagNone, offer.rewardItemQty);
                    InventoryItemRef iRef = sItemFactory.SpawnItem(data);
                    if (iRef.get() != nullptr)
                        iRef->Move(m_agent->GetStationID(), flagHangar, true);
                    sItemFactory.UnsetUsingClient();
                }
                if (offer.rewardISK)
                    AccountService::TransferFunds(m_agent->GetID(), charID, offer.rewardISK, "Mission Reward", Journal::EntryType::AgentMissionReward, m_agent->GetID());
                // bonusTime is in minutes from acceptance
                if ((offer.bonusISK > 0) and (offer.bonusTime > 0)
                and ((GetFileTimeNow() - offer.dateAccepted) <= ((double)offer.bonusTime * EvE::Time::Minute)))
                    AccountService::TransferFunds(m_agent->GetID(), charID, offer.bonusISK, "Mission Bonus Reward", Journal::EntryType::AgentMissionTimeBonusReward, m_agent->GetID());
                if (offer.rewardLP)
                    LPService::AddLP(charID, m_agent->GetCorpID(), offer.rewardLP);
                m_agent->UpdateStandings(call.client, Standings::MissionCompleted, offer.important);
                m_agent->RemoveOffer(charID);
                sMissionInstanceMgr.Release(offer.offerID);
            } break;
            case Defer: {   //10
                // the offer stays in the journal until it expires
                MissionOffer offer = MissionOffer();
                // only an offer can be deferred.  deferring an accepted mission made it acceptable (and its cargo spawnable) again
                if (m_agent->HasMission(charID, offer) and (offer.stateID < Mission::State::Accepted)) {
                    offer.stateID = Mission::State::Allocated; //Defered
                    m_agent->UpdateOffer(charID, offer);
                    m_agent->SendMissionUpdate(call.client, "prolong");
                    agentSays->SetItem(0, new PyString("Very well.  The offer stands until it expires; you'll find it in your journal."));    //msgInfo  -- if tuple[0].string then return msgInfo
                    agentSays->SetItem(1, PyStatic.NewNone());    // ContentID  -- PyNone used when msgInfo is string to return without processing
                }
            } break;
            case Decline: { //9
                MissionOffer offer = MissionOffer();
                if (!m_agent->HasMission(charID, offer) or (offer.stateID >= Mission::State::Accepted))
                    break;
                // a second decline within DeclinePenaltyHours costs standings (important missions always do)
                bool penalty(offer.important or (GetDeclineWait(charID) > 0));
                m_agent->DeleteOffer(charID);
                m_agent->SendMissionUpdate(call.client, "offer_declined");
                if (penalty)
                    m_agent->UpdateStandings(call.client, Standings::MissionDeclined, offer.important);

                // "I have another assignment prepared for you already"
                MissionOffer next = MissionOffer();
                if (!offer.storyline and m_agent->MakeOffer(charID, next)) {
                    m_agent->SendMissionUpdate(call.client, "offered");
                    agentSays->SetItem(0, MissionDataMgr::GetBriefing(next));
                    agentSays->SetItem(1, new PyInt(charID));
                    AddMissionButtons(call.client, next, dialog);
                } else {
                    missionDeclined = true;
                    agentSays->SetItem(0, new PyInt(m_agent->GetDeclineRsp(charID)));
                    agentSays->SetItem(1, new PyInt(charID));
                }
            } break;
            case Quit: {    //11
                MissionOffer offer = MissionOffer();
                if (!m_agent->HasMission(charID, offer) or (offer.stateID != Mission::State::Accepted))
                    break;
                missionQuit = true;
                if (MissionObjectives::SpawnsCargoOnAccept(offer) and offer.courierTypeID) {
                    // remove item from player possession
                    call.client->RemoveMissionItem(offer.courierTypeID, offer.courierAmount);
                }
                // remove mission offer and set standings accordingly
                m_agent->DeleteOffer(charID, Mission::State::Failed);
                sMissionInstanceMgr.Release(offer.offerID);
                m_agent->SendMissionUpdate(call.client, "quit");
                agentSays->SetItem(0, new PyInt(m_agent->GetQuitRsp(charID)));
                agentSays->SetItem(1, new PyInt(charID));
                m_agent->UpdateStandings(call.client, Standings::MissionFailure, offer.important);
            } break;
            case Continue: {    //8
                // not sure what to do here.
            } break;
            case StartResearch: {   //12
                // not sure what to do here.
            } break;
            case CancelResearch: {  //13
                // not sure what to do here.
            } break;
            case BuyDatacores: {    //14
                // not sure what to do here.
            } break;
            case LocateCharacter:   //15
            case LocateAccept: {    //16
                // not sure what to do here.
            } break;
            case LocateReject: {    //17
                // not sure what to do here.
            } break;
            case Yes: {             //18
                // not sure what to do here.
            } break;
            case No: {              //19
                // not sure what to do here.
            } break;
            case AcceptChoice:{     //4
                // i think this is for options
            } break;
            case Admin: {           //20
                // not sure what to do here.
            } break;
            default: {
                // error
                _log(AGENT__ERROR, "AgentBound::Handle_DoAction() - unhandled buttonID %u", actionID.value()->value() );
                call.client->SendErrorMsg("Internal Server Error. Ref: ServerError xxxxx.");
                PySafeDecRef(dialog);
                PySafeDecRef(agentSays);
                return nullptr;
            }
        }
    } else {
        agentSays->SetItem(0, new PyInt(m_agent->GetStandingsRsp(charID)));
        agentSays->SetItem(1, PyStatic.NewNone() /*new PyInt(charID)*/);
    }

    if ((errorKey != nullptr) or !errorText.empty()) {
        PySafeDecRef(dialog);
        PySafeDecRef(agentSays);
        if (errorKey != nullptr)
            throw UserError(errorKey);
        throw CustomError("%s", errorText.c_str());
    }

    // extraInfo data....
    PyDict* xtraInfo = new PyDict();
        xtraInfo->SetItemString("loyaltyPoints",    new PyInt(LPService::GetLPBalanceForCorp(charID, m_agent->GetCorpID())));  // this is char current LP
        xtraInfo->SetItemString("missionCompleted", new PyBool(missionCompleted));
        xtraInfo->SetItemString("missionQuit",      new PyBool(missionQuit));
        xtraInfo->SetItemString("missionDeclined",  new PyBool(missionDeclined));

    if (agentSays->GetItem(0) == nullptr) {
        agentSays->SetItem(0, PyStatic.NewNone());  // briefingID
        agentSays->SetItem(1, PyStatic.NewNone());  // ContentID
    }
    PyTuple* inner = new PyTuple(2);
        inner->SetItem(0, agentSays);
        inner->SetItem(1, dialog);
    PyTuple* outer = new PyTuple(2);
        outer->SetItem(0, inner);
        outer->SetItem(1, xtraInfo);

    if (is_log_enabled(AGENT__RSP_DUMP)) {
        _log(AGENT__RSP_DUMP, "AgentBound::Handle_DoAction RSP:" );
        outer->Dump(AGENT__RSP_DUMP, "    ");
    }

    PySafeDecRef(agentSays);

    return outer;
}

PyDict* AgentBound::BuildMissionKeywords(MissionOffer& offer)
{
    // the keywords embedded in the mission messages ({[location]objectiveDestinationID.name} ...)
    PyDict* keywords = new PyDict();
        keywords->SetItemString("objectiveLocationID", new PyInt(offer.originID));
        keywords->SetItemString("objectiveLocationSystemID", new PyInt(offer.originSystemID));
        keywords->SetItemString("objectiveTypeID", new PyInt(offer.courierTypeID));
        keywords->SetItemString("objectiveQuantity", new PyInt(offer.courierAmount));
        keywords->SetItemString("objectiveDestinationID", new PyInt(offer.destinationID));
        keywords->SetItemString("objectiveDestinationSystemID", new PyInt(offer.destinationSystemID));
        if (offer.rewardISK) {
            keywords->SetItemString("rewardTypeID", new PyInt(itemTypeCredits));
            keywords->SetItemString("rewardQuantity", new PyInt(offer.rewardISK));
        } else {
            // wouldnt these be in 'extra' or ?
            keywords->SetItemString("rewardTypeID", new PyInt(offer.rewardItemID));
            keywords->SetItemString("rewardQuantity", new PyInt(offer.rewardItemQty));
        }
        keywords->SetItemString("dungeonLocationID", new PyInt(offer.dungeonLocationID));
        keywords->SetItemString("dungeonSolarSystemID", new PyInt(offer.dungeonSolarSystemID));
    return keywords;
}

PyDict* AgentBound::GetBriefingInfo(Client* pClient, MissionOffer& offer)
{
    PyDict *briefingInfo = new PyDict();
        // contentID is the key for the keywords data on live.  we use the charID
        briefingInfo->SetItemString("ContentID", new PyInt(offer.characterID));
        briefingInfo->SetItemString("Mission Keywords", BuildMissionKeywords(offer));
        briefingInfo->SetItemString("Mission Title ID", MissionDataMgr::GetTitle(offer));
        briefingInfo->SetItemString("Mission Briefing ID", MissionDataMgr::GetBriefing(offer));
        briefingInfo->SetItemString("Mission Image", sMissionDataMgr.GetMissionImage(offer.typeID));
        // decline time OR expiration time.  -1 is the generic 'more than once every 4 hours' warning, a time is what is left of the window
        if (offer.stateID < Mission::State::Accepted) {
            int64 wait(GetDeclineWait(offer.characterID));
            if (wait > 0) {
                briefingInfo->SetItemString("Decline Time", new PyLong(wait));
            } else {
                briefingInfo->SetItemString("Decline Time", new PyInt(-1));
            }
        } else {
            briefingInfo->SetItemString("Decline Time", PyStatic.NewNone());
        }
        briefingInfo->SetItemString("Expiration Time", new PyLong((int64)offer.expiryTime));
    return briefingInfo;
}

PyResult AgentBound::GetMissionBriefingInfo(PyCallArgs &call) {
    // called from iniate agent convo... should be populated when mission available
    // will return PyNone if no mission avalible
    _log(AGENT__MESSAGE,  "AgentBound::Handle_GetMissionBriefingInfo()");

    MissionOffer offer = MissionOffer();
    if (!m_agent->HasMission(call.client->GetCharacterID(), offer))
        return PyStatic.NewNone();
    if (offer.stateID > Mission::State::Accepted)
        return PyStatic.NewNone();

    PyDict* briefingInfo(GetBriefingInfo(call.client, offer));
    if (is_log_enabled(AGENT__RSP_DUMP)) {
        _log(AGENT__RSP_DUMP, "AgentBound::Handle_GetMissionBriefingInfo() RSP:" );
        briefingInfo->Dump(AGENT__RSP_DUMP, "    ");
    }

    return briefingInfo;
}

PyResult AgentBound::GetMissionKeywords(PyCallArgs &call, PyInt* contentID) {
    // thse are the variables embedded in the messageIDs
    //self.missionArgs[contentID] = self.GetAgentMoniker(agentID).GetMissionKeywords(contentID)
    _log(AGENT__DUMP,  "AgentBound::Handle_GetMissionKeywords() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    // contentID is the charID we sent.  only the caller's own offer is ever looked up
    MissionOffer offer = MissionOffer();
    if (!m_agent->HasMission(call.client->GetCharacterID(), offer))
        return PyStatic.NewNone();

    PyDict* keywords(BuildMissionKeywords(offer));
    if (is_log_enabled(AGENT__RSP_DUMP)) {
        _log(AGENT__RSP_DUMP, "AgentBound::Handle_GetMissionKeywords() RSP:" );
        keywords->Dump(AGENT__RSP_DUMP, "    ");
    }

    return keywords;
}

PyResult AgentBound::GetMissionObjectiveInfo(PyCallArgs &call, std::optional <PyInt*> characterID, std::optional <PyInt*> contentID)
{
    // sends charID, contentID (although there's another call without any parameters, hence the optionals used)
    // returns PyDict loaded with mission info  or PyNone
    //  returning mission info sets double-pane view, where PyNone sets single-pane view
    _log(AGENT__DUMP,  "AgentBound::Handle_GetMissionObjectiveInfo() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    // always the caller's own offer (contentID is the charID we sent)
    MissionOffer offer = MissionOffer();
    if (!m_agent->HasMission(call.client->GetCharacterID(), offer))
        return PyStatic.NewNone();

    switch (offer.stateID) {
        case Mission::State::Completed:
        case Mission::State::Rejected:
        case Mission::State::Expired: {
            return PyStatic.NewNone();
        }
    }

    return GetMissionObjectiveInfo(call.client, offer);
}

PyResult AgentBound::GetMyJournalDetails(PyCallArgs &call) {
    //parallelCalls.append((sm.GetService('agents').GetAgentMoniker(agentID).GetMyJournalDetails, ()))
    //missionState, importantMission, missionType, missionName, agentID, expirationTime, bookmarks, remoteOfferable, remoteCompletable = each
    // this is to update ONLY info with this agent....
    _log(AGENT__DUMP,  "AgentBound::Handle_GetMyJournalDetails() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    PyTuple *tuple = new PyTuple(2);
    //missions:
    PyList* missions = new PyList();
    MissionOffer offer = MissionOffer();
    if (m_agent->HasMission(call.client->GetCharacterID(), offer))
        if ((offer.stateID != Mission::State::Completed) and (offer.stateID != Mission::State::Rejected))
            missions->AddItem(sMissionDataMgr.GetJournalEntry(offer));
    tuple->SetItem(0, missions);

    //research:
    PyList* research = new PyList();
    tuple->SetItem(1, research);

    if (is_log_enabled(AGENT__RSP_DUMP))
        tuple->Dump(AGENT__RSP_DUMP, "   ");
    return tuple;
}

PyResult AgentBound::GetMissionJournalInfo(PyCallArgs &call, std::optional <PyInt*> characterID, std::optional <PyInt*> contentID) {
    //called on rclick in journal to "read details"
    //ret = self.GetAgentMoniker(agentID).GetMissionJournalInfo(charID, contentID)
    _log(AGENT__DUMP,  "AgentBound::Handle_GetMissionJournalInfo() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    MissionOffer offer = MissionOffer();
    if (!m_agent->HasMission(call.client->GetCharacterID(), offer))
        return PyStatic.NewNone();

    PyDict* journalInfo = new PyDict();
    journalInfo->SetItemString("contentID", new PyInt(offer.characterID));
    journalInfo->SetItemString("missionNameID", MissionDataMgr::GetTitle(offer));
    journalInfo->SetItemString("briefingTextID", MissionDataMgr::GetBriefing(offer));
    journalInfo->SetItemString("missionState", new PyInt(MissionDataMgr::GetClientState(offer.stateID)));
    journalInfo->SetItemString("expirationTime", new PyLong((int64)offer.expiryTime));
    journalInfo->SetItemString("objectives", GetMissionObjectiveInfo(call.client, offer));
    journalInfo->SetItemString("missionImage", sMissionDataMgr.GetMissionImage(offer.typeID));

    if (is_log_enabled(AGENT__RSP_DUMP)) {
        _log(AGENT__RSP_DUMP, "AgentBound::Handle_GetMissionJournalInfo() RSP:" );
        journalInfo->Dump(AGENT__RSP_DUMP, "    ");
    }

    return journalInfo;
}

PyDict* AgentBound::GetStationLocation(uint32 stationID)
{
    StationData data = StationData();
    stDataMgr.GetStationData(stationID, data);
    PyDict* location = new PyDict();
        location->SetItemString("typeID", new PyInt(data.typeID));
        location->SetItemString("locationID", new PyInt(stationID));
        location->SetItemString("solarsystemID", new PyInt(data.systemID));
    return location;
}

PyDict* AgentBound::GetMissionObjectiveInfo(Client* pClient, MissionOffer& offer)
{
    PyDict* objectiveData = new PyDict();
    objectiveData->SetItemString("missionTitleID", MissionDataMgr::GetTitle(offer));
    objectiveData->SetItemString("contentID", new PyInt(offer.characterID));
    objectiveData->SetItemString("importantStandings", new PyInt(offer.important));     // boolean integer
    // Mission::Status:: 0=no, 1=yes, 2=cheat
    if (pClient->IsMissionComplete(offer)) {
        objectiveData->SetItemString("completionStatus", new PyInt(Mission::Status::Complete));
    } else {
        objectiveData->SetItemString("completionStatus", new PyInt(Mission::Status::Incomplete));
    }
    // client state:  accepted/failed show the agent gift as granted
    objectiveData->SetItemString("missionState", new PyInt(MissionDataMgr::GetClientState(offer.stateID)));
    objectiveData->SetItemString("loyaltyPoints", new PyInt(offer.rewardLP));
    objectiveData->SetItemString("researchPoints", new PyInt(0));

    // systems of the mission, for the route security warning
    PyList* locList = new PyList();
    if ((offer.typeID == Mission::Type::Encounter) or (offer.typeID == Mission::Type::Mining)) {
        locList->AddItem(new PyInt(offer.originSystemID));
        locList->AddItem(new PyInt(offer.dungeonSolarSystemID));
    } else {
        locList->AddItem(new PyInt(offer.originSystemID));
        locList->AddItem(new PyInt(offer.destinationSystemID));
    }
    objectiveData->SetItemString("locations", locList);

    // agentGift, normalRewards, collateral:  lists of (typeID, quantity, extra).  'extra' is either specificItemID or blueprint data.
    objectiveData->SetItemString("agentGift", new PyList());
    PyList* normList = new PyList();
    if (offer.rewardISK) {
        PyTuple* normalRewards = new PyTuple(3);
            normalRewards->SetItem(0, new PyInt(itemTypeCredits));
            normalRewards->SetItem(1, new PyInt(offer.rewardISK));
            normalRewards->SetItem(2, new PyDict());
        normList->AddItem(normalRewards);
    }
    if (offer.rewardItemID) {
        PyTuple* normalRewards = new PyTuple(3);
            normalRewards->SetItem(0, new PyInt(offer.rewardItemID));
            normalRewards->SetItem(1, new PyInt(offer.rewardItemQty));
            normalRewards->SetItem(2, new PyDict());
        normList->AddItem(normalRewards);
    }
    objectiveData->SetItemString("normalRewards", normList);
    objectiveData->SetItemString("collateral", new PyList());

    // bonusRewards: list of (timeRemaining, typeID, quantity, extra).  the time is counted from acceptance
    PyList* bonusList = new PyList();
    if ((offer.bonusTime > 0) and (offer.bonusISK > 0)) {
        int64 remaining((int64)offer.bonusTime * EvE::Time::Minute);
        if (offer.dateAccepted > 0)
            remaining -= (int64)(GetFileTimeNow() - offer.dateAccepted);
        PyTuple* bonusRewards = new PyTuple(4);
            bonusRewards->SetItem(0, new PyLong(remaining));
            bonusRewards->SetItem(1, new PyInt(itemTypeCredits));
            bonusRewards->SetItem(2, new PyInt(offer.bonusISK));
            bonusRewards->SetItem(3, new PyDict());
        bonusList->AddItem(bonusRewards);
    }
    objectiveData->SetItemString("bonusRewards", bonusList);

    objectiveData->SetItemString("objectives", GetMissionObjectives(pClient, offer));

    // the mission pocket (dunData)
    PyList* dunList = new PyList();
    if (((offer.typeID == Mission::Type::Encounter) or (offer.typeID == Mission::Type::Mining)) and (offer.dungeonSolarSystemID != 0)) {
        PyTuple* coords = new PyTuple(3);
            coords->SetItem(0, new PyFloat(offer.dungeonPos.x));
            coords->SetItem(1, new PyFloat(offer.dungeonPos.y));
            coords->SetItem(2, new PyFloat(offer.dungeonPos.z));
        PyDict* location = new PyDict();
            location->SetItemString("typeID", new PyInt(EVEDB::invTypes::SolarSystem));
            location->SetItemString("locationID", new PyInt(offer.dungeonSolarSystemID));
            location->SetItemString("solarsystemID", new PyInt(offer.dungeonSolarSystemID));
            location->SetItemString("coords", coords);
            location->SetItemString("agentID", new PyInt(offer.agentID));
            location->SetItemString("locationType", new PyString("dungeon"));
        PyDict* dunData = new PyDict();
            dunData->SetItemString("dungeonID", new PyInt(offer.dungeonID));
            dunData->SetItemString("optional", PyStatic.NewFalse());
            // None = circle, False = cross, True = check
            if (offer.objectiveCompleted) {
                dunData->SetItemString("objectiveCompleted", PyStatic.NewTrue());
                // index into [completed, failed]
                dunData->SetItemString("completionStatus", PyStatic.NewZero());
            } else {
                dunData->SetItemString("objectiveCompleted", PyStatic.NewNone());
            }
            dunData->SetItemString("ownerID", new PyInt(m_agent->GetCorpID()));
            dunData->SetItemString("location", location);
            // the gated (multi-room) pockets have ship restrictions (0 = normal, 1 = special)
            std::shared_ptr<const Dungeon::Dungeon> pDungeon(sDunDataMgr.GetDungeon(offer.dungeonID));
            if ((pDungeon.get() != nullptr) and (pDungeon->roomOrder.size() > 1))
                dunData->SetItemString("shipRestrictions", PyStatic.NewZero());
        dunList->AddItem(dunData);
    }
    objectiveData->SetItemString("dungeons", dunList);

    if (is_log_enabled(AGENT__RSP_DUMP)) {
        _log(AGENT__RSP_DUMP, "AgentBound::Handle_GetMissionObjectiveInfo() RSP:" );
        objectiveData->Dump(AGENT__RSP_DUMP, "    ");
    }

    return objectiveData;
}

PyList* AgentBound::GetMissionObjectives(Client* pClient, MissionOffer& offer)
{
    // list of (objType, objData).  objType is 'agent' (report to), 'transport' (courier) or 'fetch' (bring items)
    PyList* objectives = new PyList();
    switch (offer.typeID) {
        case Mission::Type::Courier: {
            PyDict* cargo = new PyDict();
                cargo->SetItemString("hasCargo", new PyBool(pClient->ContainsTypeQty(offer.courierTypeID, offer.courierAmount)));
                cargo->SetItemString("typeID", new PyInt(offer.courierTypeID));
                cargo->SetItemString("quantity", new PyInt(offer.courierAmount));
                cargo->SetItemString("volume", new PyFloat(offer.courierItemVolume * offer.courierAmount));    // calculated shipment volume.  *this is direct to window*
            PyTuple* objData = new PyTuple(5);
                objData->SetItem(0, new PyInt(offer.originOwnerID));
                objData->SetItem(1, GetStationLocation(offer.originID));
                objData->SetItem(2, new PyInt(offer.destinationOwnerID));
                objData->SetItem(3, GetStationLocation(offer.destinationID));
                objData->SetItem(4, cargo);
            PyTuple* objType = new PyTuple(2);
                objType->SetItem(0, new PyString("transport"));
                objType->SetItem(1, objData);
            objectives->AddItem(objType);
            // a courier mission ends with the delivery
            return objectives;
        }
    }

    // item objectives:  bring the goods to the agent
    switch (offer.objectiveType) {
        case Mission::ObjType::Mine:
        case Mission::ObjType::Fetch:
        case Mission::ObjType::Trade: {
            if (offer.courierTypeID == 0)
                break;
            PyDict* cargo = new PyDict();
                cargo->SetItemString("hasCargo", new PyBool(pClient->ContainsTypeQty(offer.courierTypeID, offer.courierAmount)));
                cargo->SetItemString("typeID", new PyInt(offer.courierTypeID));
                cargo->SetItemString("quantity", new PyInt(offer.courierAmount));
                cargo->SetItemString("volume", new PyFloat(offer.courierItemVolume * offer.courierAmount));
            PyTuple* objData = new PyTuple(3);
                objData->SetItem(0, new PyInt(m_agent->GetCorpID()));
                objData->SetItem(1, GetStationLocation(m_agent->GetStationID()));
                objData->SetItem(2, cargo);
            PyTuple* objType = new PyTuple(2);
                objType->SetItem(0, new PyString("fetch"));
                objType->SetItem(1, objData);
            objectives->AddItem(objType);
        } break;
    }

    // and report back
    PyTuple* objData = new PyTuple(2);
        objData->SetItem(0, new PyInt(m_agent->GetID()));
        objData->SetItem(1, m_agent->GetLocationWrap());
    PyTuple* objType = new PyTuple(2);
        objType->SetItem(0, new PyString("agent"));
        objType->SetItem(1, objData);
    objectives->AddItem(objType);
    return objectives;
}


PyResult AgentBound::GetDungeonShipRestrictions(PyCallArgs &call, PyInt* dungeonID) {
    //restrictions = self.GetAgentMoniker(agentID).GetDungeonShipRestrictions(dungeonID)
    //  .restrictedShipTypes and .allowedShipTypes are lists of ship groupIDs
    _log(AGENT__DUMP,  "AgentBound::Handle_GetDungeonShipRestrictions() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    std::vector<uint32> allowed, denied;
    if (!sConfig.missions.GateRestrictions) {
        // the gates only turn capitals away
        MissionDataMgr::GetShipRestrictions(5, allowed, denied);
    } else {
        // the pocket's gates say, else the mission level's rules
        sDunDataMgr.GetShipRestrictions(dungeonID->value(), allowed, denied);
    }
    if (sConfig.missions.GateRestrictions and allowed.empty()) {
        std::vector<uint32> levelDenied;
        MissionDataMgr::GetShipRestrictions(m_agent->GetLevel(), allowed, levelDenied);
        for (auto cur : levelDenied)
            if (std::find(denied.begin(), denied.end(), cur) == denied.end())
                denied.push_back(cur);
    }

    PyList* restricted = new PyList();
    for (auto cur : denied)
        restricted->AddItem(new PyInt(cur));
    PyList* permitted = new PyList();
    for (auto cur : allowed)
        permitted->AddItem(new PyInt(cur));
    PyDict* res = new PyDict();
        res->SetItemString("restrictedShipTypes", restricted);
        res->SetItemString("allowedShipTypes", permitted);
    return new PyObject("util.KeyVal", res);
}

PyResult AgentBound::RemoveOfferFromJournal(PyCallArgs &call) {
    //called on rclick in journal to "remove offer"
    //self.GetAgentMoniker(agentID).RemoveOfferFromJournal()
    _log(AGENT__DUMP,  "AgentBound::Handle_RemoveOfferFromJournal() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    // only an expired (or never answered) offer, no standing change
    MissionOffer offer = MissionOffer();
    uint32 charID(call.client->GetCharacterID());
    if (m_agent->HasMission(charID, offer) and (offer.stateID < Mission::State::Accepted)) {
        m_agent->DeleteOffer(charID, Mission::State::Expired);
        m_agent->SendMissionUpdate(call.client, "offer_removed");
    }
    return PyStatic.NewNone();
}

PyResult AgentBound::GetOfferJournalInfo(PyCallArgs &call) {
    //html = self.GetAgentMoniker(agentID).GetOfferJournalInfo()
    _log(AGENT__DUMP,  "AgentBound::Handle_GetOfferJournalInfo() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    MissionOffer offer = MissionOffer();
    if (!m_agent->HasMission(call.client->GetCharacterID(), offer) or (offer.stateID >= Mission::State::Accepted))
        return PyStatic.NewNone();

    // the offer popup's page
    std::string html = "<html><head><LINK REL=\"stylesheet\" TYPE=\"text/css\" HREF=\"res:/ui/css/missionjournal.css\"></head><body>";
    html += "<span id=mainheader>" + offer.name + "</span><br><hr><br>";
    if (offer.briefingID == 0)
        html += "<div id=basetext>" + offer.briefingText + "</div><br>";
    html += "<span id=subheader>Rewards</span><div id=basetext>";
    if (offer.rewardISK)
        html += std::to_string(offer.rewardISK) + " ISK<br>";
    if (offer.rewardLP)
        html += std::to_string(offer.rewardLP) + " Loyalty Points<br>";
    if ((offer.bonusISK) and (offer.bonusTime))
        html += "Bonus: " + std::to_string(offer.bonusISK) + " ISK if completed within " + std::to_string(offer.bonusTime) + " minutes<br>";
    html += "</div><br>";
    int64 left(((int64)offer.expiryTime - GetFileTimeNow()) / EvE::Time::Hour);
    html += "<span id=ip>This offer expires in " + std::to_string(left > 0 ? left : 0) + " hours.</span>";
    html += "</body></html>";
    return new PyString(html);
}

PyResult AgentBound::GetEntryPoint(PyCallArgs &call) {
    //entryPoint = sm.StartService('agents').GetAgentMoniker(bookmark.agentID).GetEntryPoint()
    //  the in-system point of the agent's site, for the bookmark's distance (x, y, z) or None
    _log(AGENT__DUMP,  "AgentBound::Handle_GetEntryPoint() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    MissionOffer offer = MissionOffer();
    if (!m_agent->HasMission(call.client->GetCharacterID(), offer) or (offer.stateID != Mission::State::Accepted))
        return PyStatic.NewNone();
    if ((offer.dungeonSolarSystemID == 0) or (call.client->GetSystemID() != offer.dungeonSolarSystemID))
        return PyStatic.NewNone();

    PyTuple* point = new PyTuple(3);
        point->SetItem(0, new PyFloat(offer.dungeonPos.x));
        point->SetItem(1, new PyFloat(offer.dungeonPos.y));
        point->SetItem(2, new PyFloat(offer.dungeonPos.z));
    return point;
}

void AgentBound::ResolveLocation(Client* pClient, MissionOffer& offer, const std::string& locationType, GPoint& point, SystemEntity*& pSE, uint32& toID)
{
    if ((offer.stateID != Mission::State::Accepted) and (locationType != "agenthomebase"))
        throw UserError("MisNoSuchMission");

    SystemManager* pSystem(pClient->SystemMgr());
    if (pSystem == nullptr)
        throw CustomError("You must be in space to do that.");

    if (locationType == "dungeon") {
        if (pClient->GetSystemID() != offer.dungeonSolarSystemID)
            throw CustomError("That location is in another solar system.");
        // the pocket is made when the pilot first goes there (again after its system was unloaded)
        sMissionInstanceMgr.EnsureInstance(offer);
        point = offer.dungeonPos;
        return;
    }

    // station bookmarks
    if (locationType == "objective.source") {
        toID = offer.originID;
    } else if (locationType == "objective.destination") {
        toID = offer.destinationID;
    } else if (locationType == "objective") {
        toID = MissionObjectives::GetDeliveryLocation(offer);
        if (toID == 0)
            toID = m_agent->GetStationID();
    } else if (locationType == "agenthomebase") {
        toID = m_agent->GetStationID();
    } else {
        throw CustomError("Unknown mission location '%s'.", locationType.c_str());
    }

    pSE = pSystem->GetSE(toID);
    if (pSE == nullptr)
        throw CustomError("That location is in another solar system.");
}

PyResult AgentBound::GotoLocation(PyCallArgs &call, PyRep* locationType, PyInt* locationNumber, std::optional<PyRep*> referringAgentID) {
    //sm.StartService('agents').GetAgentMoniker(bookmark.agentID).GotoLocation(bookmark.locationType, bookmark.locationNumber, referringAgentID)
    _log(AGENT__DUMP,  "AgentBound::Handle_GotoLocation() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    Client* pClient(call.client);
    if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr))
        throw CustomError("You must be in space to do that.");
    MissionOffer offer = MissionOffer();
    if (!m_agent->HasMission(pClient->GetCharacterID(), offer))
        throw UserError("MisNoSuchMission");

    GPoint point(NULL_ORIGIN);
    SystemEntity* pSE(nullptr);
    uint32 toID(0);
    ResolveLocation(pClient, offer, PyRep::StringContent(locationType), point, pSE, toID);
    if (pSE != nullptr)
        point = pSE->GetPosition();

    DestinyManager* pDestiny(pClient->GetShipSE()->DestinyMgr());
    if (pDestiny == nullptr)
        return PyStatic.NewNone();
    if (pDestiny->IsWarping()) {
        pClient->SendNotifyMsg("You can't do this while warping");
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        pClient->SendNotifyMsg("Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }
    // approach is only for things on grid
    if (pClient->GetShipSE()->GetPosition().distance(point) > BUBBLE_RADIUS_METERS * 2)
        throw CustomError("That location is too far away to approach.  Warp to it instead.");

    pClient->SetInvul(false);
    pClient->SetUndock(false);
    pClient->SetAutoPilot(false);
    pDestiny->GotoPoint(point);
    return PyStatic.NewNone();
}

PyResult AgentBound::WarpToLocation(PyCallArgs &call, PyRep* locationType, PyInt* locationNumber, PyRep* warpRange, PyRep* fleet, std::optional<PyRep*> referringAgentID) {
    //sm.StartService('agents').GetAgentMoniker(bookmark.agentID).WarpToLocation(bookmark.locationType, bookmark.locationNumber, warpRange, fleet, referringAgentID)
    _log(AGENT__DUMP,  "AgentBound::Handle_WarpToLocation() - size=%lli", call.tuple->size());
    call.Dump(AGENT__DUMP);

    Client* pClient(call.client);
    if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr))
        throw CustomError("You must be in space to do that.");
    MissionOffer offer = MissionOffer();
    if (!m_agent->HasMission(pClient->GetCharacterID(), offer))
        throw UserError("MisNoSuchMission");

    // the same rules as any other warp (BeyonceBound::CmdWarpToStuff)
    ShipSE* pShip(pClient->GetShipSE());
    if (pShip->IsWarpScrambled())
        throw UserError ("WarpScrambled");
    if (pClient->SystemMgr() != nullptr)
        pClient->SystemMgr()->GetInterdictionMgr()->CheckCanWarp(pShip);
    BeyonceBound::CheckCloakedWarp(pClient);
    if (BeyonceBound::CheckWarpLockout(pClient))
        return PyStatic.NewNone();

    DestinyManager* pDestiny(pShip->DestinyMgr());
    if (pDestiny == nullptr)
        return PyStatic.NewNone();
    if (pDestiny->IsWarping()) {
        pClient->SendNotifyMsg("You are already warping");
        return PyStatic.NewNone();
    } else if (pDestiny->AbortIfLoginWarping(true)) {
        return PyStatic.NewNone();
    } else if (pDestiny->IsFrozen()) {
        pClient->SendNotifyMsg("Your ship is frozen and cannot move");
        return PyStatic.NewNone();
    }

    GPoint point(NULL_ORIGIN);
    SystemEntity* pSE(nullptr);
    uint32 toID(0);
    ResolveLocation(pClient, offer, PyRep::StringContent(locationType), point, pSE, toID);

    int32 distance(0);
    if (warpRange->IsFloat()) {
        distance = (int32)warpRange->AsFloat()->value();
    } else if (warpRange->IsInt()) {
        distance = warpRange->AsInt()->value();
    }
    bool doFleet(false);
    if (fleet->IsBool()) {
        doFleet = fleet->AsBool()->value();
    } else if (fleet->IsInt()) {
        doFleet = (fleet->AsInt()->value() != 0);
    }

    // a commander warps his subordinates too (the group's warp speed is taken before anyone moves)
    std::vector<Client*> members;
    int32 speed(0);
    if (doFleet and sFltSvc.IsCommander(pClient))
        BeyonceBound::GetFleetWarpMembers(pClient, 0, members, speed);

    int32 myDistance(distance);
    GPoint warpToPoint(BeyonceBound::GetWarpInPoint(pSE, toID, point, pShip->GetPosition(), myDistance));
    pClient->SetInvul(false);
    pClient->SetUndock(false);
    pClient->SetAutoPilot(false);
    myDistance += (pShip->GetRadius() * 2); // add ship diameter to distance
    pDestiny->WarpTo(warpToPoint, myDistance, false, nullptr, speed);

    if (!members.empty())
        BeyonceBound::FleetWarp(pClient, members, point, pSE, toID, distance, speed);

    return PyStatic.NewNone();
}
