/**
 * @name AgentServices.h
 *   the non-mission agent services (F24):  R&D research projects and datacores, and locator agents
 *
 *   CCP's server asked for the research field, datacore quantity and locate target with client popups
 *   (agents.RemoteNamePopup/GetQuantity/SingleChoiceBox).  we have no server->client calls with replies here, so the
 *   choices are dialogue links and buttons whose actionIDs carry the choice (Dialog::Action in EVE_Agent.h)
 */

#ifndef _EVE_SERVER_AGENT_SERVICES_H__
#define _EVE_SERVER_AGENT_SERVICES_H__

#include "../eve-server.h"

#include "agents/AgentDB.h"

class Agent;
class Character;
class Client;

/* research agents (pre-Inferno rules):
 *  RP/day = (agent skill + pilot skill)^2 * (1 + (20 quality + 5 * Negotiation + agent standing) / 100) * field multiplier
 *  one project plus one per level of Research Project Management; datacores cost RP (and optionally ISK)
 */
class ResearchAgentMgr
: public Singleton< ResearchAgentMgr >
{
public:
    void Initialize();

    float GetFieldMultiplier(uint16 skillID);
    uint32 GetDatacoreType(uint16 skillID);
    double GetRPPerDay(Agent* pAgent, Client* pClient, uint16 skillID);

    // banks the points accrued so far and sets the rate from the pilot's current skills and standing.  false if no project
    bool Refresh(Client* pClient, Agent* pAgent, AgentDB::Research& data);
    // agent info window's researchData:  dict(skillTypeID, points, pointsPerDay, rpMultiplier), or None
    PyRep* GetResearchData(Client* pClient, Agent* pAgent);
    // journal rows (agentID, skillTypeID, pointsPerDay, points, agentLevel, quality, stationID); agentID 0 = all
    PyList* GetJournalRows(uint32 charID, uint32 agentID = 0);

    // DoAction:  true if the action was handled (agentSays and dialog filled)
    bool HandleAction(Client* pClient, Agent* pAgent, int32 actionID, PyTuple* agentSays, PyList* dialog);
    // the buttons of the first conversation page
    void AddInitialButtons(Client* pClient, Agent* pAgent, PyList* dialog);

protected:
    void DoStartResearch(Client* pClient, Agent* pAgent, PyTuple* agentSays, PyList* dialog);
    void PickField(Client* pClient, Agent* pAgent, uint16 skillID, PyTuple* agentSays, PyList* dialog);
    void ShowDatacores(Client* pClient, Agent* pAgent, PyTuple* agentSays, PyList* dialog);
    void DoBuyDatacores(Client* pClient, Agent* pAgent, uint32 qty, PyTuple* agentSays, PyList* dialog);
    // the agent's fields the pilot has trained
    void GetEligibleFields(Client* pClient, Agent* pAgent, std::vector<uint16>& into);

private:
    std::map<uint16, float> m_multipliers;      // skillID -> research field multiplier
    std::map<uint16, uint32> m_datacores;       // skillID -> datacore typeID
};

/* locator agents:  cooldown and price by agent level and range (same system, constellation, region, other region).
 *  delayed results arrive as a LocateChar notification.  pending searches are memory only (a restart drops them)
 */
class LocatorAgentMgr
: public Singleton< LocatorAgentMgr >
{
public:
    // 1Hz, from EntityList::Process
    void Process();

    // agent info window's locate service details (frequency, delays, callbackID, lastUsed)
    void GetServiceInfo(Client* pClient, Agent* pAgent, PyDict* into);

    // DoAction:  true if the action was handled (agentSays and dialog filled)
    bool HandleAction(Client* pClient, Agent* pAgent, int32 actionID, PyTuple* agentSays, PyList* dialog);

protected:
    struct Target {
        uint32 stationID;
        uint32 systemID;
        uint32 constellationID;
        uint32 regionID;
    };
    struct Pending {
        uint32 charID;
        uint32 agentID;
        uint32 targetID;
        PyDict* data;               // the notification's data, taken when the search was paid
    };

    // cost (ISK) and delay (s) for range 0..3 at an agent level; false if the agent can't search that far
    static bool GetRate(uint8 level, uint8 range, uint32& cost, uint32& delay);
    static uint16 GetCooldownMinutes(uint8 level);

    bool IsBusy(uint32 charID, uint32 agentID);
    bool GetTarget(uint32 targetID, Target& into);
    uint8 GetRange(Agent* pAgent, const Target& target);
    // (label, kwargs) of the target's location as seen from the agent (notificationUtil.FormatLocateCharNotification)
    PyTuple* GetLocationString(Agent* pAgent, uint32 targetID, const Target& target);
    PyDict* GetNotifyData(Agent* pAgent, uint32 targetID, const Target& target);
    // checks and quotes a search; false (agentSays filled) if it can't be done
    bool Quote(Client* pClient, Agent* pAgent, uint32 targetID, Target& target, uint32& cost, uint32& delay, PyTuple* agentSays);

private:
    std::map<std::pair<uint32, uint32>, int64> m_lastUsed;      // (charID, agentID) -> filetime
    std::multimap<int64, Pending> m_pending;                    // due filetime -> search
};

#define sResearchMgr \
    ( ResearchAgentMgr::get() )

#define sLocatorMgr \
    ( LocatorAgentMgr::get() )

#endif  // _EVE_SERVER_AGENT_SERVICES_H__
