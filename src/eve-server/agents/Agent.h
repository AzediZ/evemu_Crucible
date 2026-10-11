
 /**
  * @name Agent.h
  *   agent specific code
  *    original agent code by zhur, this was completely rewritten based on new data.
  *
  * @Author:        Allan
  * @date:      19 June 2018
  *
  */

#ifndef _EVE_SERVER_AGENT_H
#define _EVE_SERVER_AGENT_H


#include "agents/AgentDB.h"
#include "missions/MissionDataMgr.h"

class Client;

class Agent {
public:
    Agent(uint32 id);
    ~Agent()                                            { /* do nothing here */ }

    bool Load();

    PyDict* GetLocationWrap();
    PyObject* GetInfoServiceDetails();

    bool IsLocator()                                    { return m_agentData.locator; }
    bool IsResearch()                                   { return m_agentData.research; }

    uint8 GetLevel()                                    { return m_agentData.level; }
    int8 GetQuality()                                   { return m_agentData.quality; }
    uint8 GetDivisionID()                               { return m_agentData.divisionID; }
    uint16 GetTypeID()                                  { return m_agentData.typeID; }
    const std::string& GetName()                        { return m_agentData.name; }

    uint32 GetID()                                      { return m_agentID; }
    uint32 GetCorpID()                                  { return m_agentData.corporationID; }
    uint32 GetSystemID()                                { return m_agentData.solarSystemID; }
    uint32 GetStationID()                               { return m_agentData.stationID; }
    uint32 GetLocTypeID()                               { return m_agentData.locationTypeID; }
    uint32 GetFactionID()                               { return m_agentData.factionID; }
    uint32 MakeButtonID()                               { return ++m_buttonID; }

    bool HasMission(uint32 charID);
    bool HasMission(uint32 charID, MissionOffer& offer);

    /* the mission type this agent gives (Incursion 1.5:  Security -> Encounter, Distribution -> Courier, Mining -> Mining).
     *  0 = none (storyline agents only have the storyline offers made for a pilot, event/arc/tutorial/FW agents are not served) */
    uint8 GetMissionType();
    // makes, saves and keeps a new offer for charID.  false (nothing saved) when the agent has nothing to offer
    bool MakeOffer(uint32 charID, MissionOffer& offer);
    // an important storyline mission of this level for charID (storyline agents), its type picked from the content there is
    bool MakeStorylineOffer(uint32 charID, uint8 level, MissionOffer& offer);
    void GetOffer(uint32 charID, MissionOffer& offer);
    void UpdateOffer(uint32 charID, MissionOffer& offer);
    // closes the offer (declined = Rejected, quit = Failed, removed = Expired):  saved with that state and dropped from agent and missionMgr data
    void DeleteOffer(uint32 charID, uint8 stateID = Mission::State::Rejected);
    void RemoveOffer(uint32 charID);    // removes offer from agent data

    uint32 GetQuitRsp(uint32 charID);
    uint32 GetAcceptRsp(uint32 charID);
    uint32 GetDeclineRsp(uint32 charID);
    uint32 GetCompleteRsp(uint32 charID);
    uint32 GetStandingsRsp(uint32 charID);
    std::string GetNoMissionRsp(uint32 charID);

    // the fleet share question (Complete -> Yes/No)
    void SetPendingCompletion(uint32 charID)            { m_pendingShare.insert(charID); }
    bool TakePendingCompletion(uint32 charID)           { return (m_pendingShare.erase(charID) > 0); }

    void SendMissionUpdate(Client* pClient, std::string action);

    bool CanUseAgent(Client* pClient);

    // standing/quality/level/reward methods...
    // sharers:  the pilots sharing a completed mission (MissionRewards::GetSharers), the completer included
    void UpdateStandings(Client* pClient, uint8 eventID, bool important=false, const std::vector<Client*>* sharers=nullptr);

protected:
    std::string GetMinReqStanding(uint8 level);
    // destination, expiry and agent data of a new offer, then saved.  false if it can't be placed
    bool FinishOffer(uint32 charID, uint8 level, MissionOffer& offer);
    const uint32 m_agentID;
    AgentData m_agentData;

    std::map<uint16, uint8>             m_skills;       // skillID/level
    std::map<uint32, MissionOffer>      m_offers;       // charID/data      -- shouldnt this be in mission data??
    std::map<uint16, AgentActions>      m_actions;      // buttonID/data
    std::set<uint32>                    m_pendingShare; // charIDs asked whether to share the rewards with their fleet

private:
    bool m_important;

    uint16 m_buttonID;

};

#endif  // _EVE_SERVER_AGENT_H

/*
(235998, `Your instructors have been telling me great things about you, {[character]charID.name, linkify}.<br><br>I've talked it over with my superiors, and we've decided to invite you to participate in {corpName} tutorial agent program for further instruction.<br><br>I'll be awaiting you at {[location]stationID.name, linkify}. You can find me there in the station's Agent listing in the lower right hand corner of your display.<br><br>With regards,<br>     {[character]agentID.name, linkify},<br>  {corpName}<br>`)
(235999, `You have failed the mission I gave you. I am disappointed in you. I was hoping for a little more competence.`)
(236000, `Insurance Contract Issued`)
(236001, `Report: Starbase low on resources in {[location]solarSystemID.name}`)
(236002, `Authentication code for {[character]charID.name}`)
(236003, `Report: "{[item]typeID.name, linkify}" at "{[location]stationID.name, linkify}" has been disabled`)
(236004, `Demotion`)
(236005, `Bill issued`)
(236006, `{[character]newCeoID.name} is the new CEO of {corporationName}`)
(236007, `Welcome to {corporationName}`)
(236008, `Rejected application to join {corporationName}`)
(236009, `Report: "{[item]typeID.name, linkify}" at "{[location]stationID.name, linkify}" has been reenabled`)
(236010, `Bounty payment`)
(236011, `Time's up, {[character]notification_receiverID.name , linkify}`)
(236012, `Tutorial Program Started`)
(236013, `Pend Insurance Inc.`)
(236014, `Report: Infrastructure hub %22{name}%22 has been conquered`)
(236015, `Jump clone destruction`)
(236016, `Contraband Confiscation`)
(236017, `CEO roles revoked`)
(236018, `Report: Starbase in {[location]solarSystemID.name, linkify} is under attack`)
(236019, `Report: Station '{[location]stationID.name}' has been conquered`)
(236020, `I've encountered a problem.`)
(236021, `Our research has been fruitful, but I've encountered a snag and our research has been halted.  Please contact me as soon as possible.`)
(236023, `Dread Guristas Irregular`)
(236024, `Dark Blood Alpha`)
(236025, `Sorry mate you don't have enough research points to buy any datacores. <br>Each {[item]datacoreTypeID.name} costs {[numeric]rpAmount} research points.`)
(236026, `{[item]datacoreTypeID.name}: {[numeric]rpAmount} RP`)
*/