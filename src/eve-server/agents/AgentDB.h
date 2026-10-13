
 /**
  * @name AgentDB.h
  *   Agent DB operations
  *    original agent code by zhur, this was completely rewritten based on new data.
  *
  * @Author:        Allan
  * @date:      24 June 2018
  *
  */


#ifndef _EVE_SERVER_AGENT_DATABASE_H__
#define _EVE_SERVER_AGENT_DATABASE_H__


#include "../eve-server.h"

#include "packets/Missions.h"
#include "../../eve-common/EVE_Agent.h"

class AgentDB
{
public:
    // false (data left as is) when agentID has no agtAgents row, i.e. isn't an agent (NPC CEOs and other NPC characters)
    static bool LoadAgentData(uint32 agentID, AgentData& data);
    static void LoadAgentSkills(uint32 agentID, std::map<uint16, uint8>& data);

    /* R&D research projects (chrAgentResearch) */
    struct Research {
        uint16 skillTypeID;
        double pointsBanked;        // points up to lastUpdate
        double pointsPerDay;        // rate since lastUpdate
        int64 lastUpdate;
        int64 startDate;
    };
    static bool GetResearch(uint32 charID, uint32 agentID, Research& into);
    static void SaveResearch(uint32 charID, uint32 agentID, const Research& data);
    static void DeleteResearch(uint32 charID, uint32 agentID);
    static uint32 CountResearch(uint32 charID);
    // agentID -> research, and the agent's level and station (journal rows)
    static void GetResearchList(uint32 charID, std::map<uint32, Research>& into, std::map<uint32, std::pair<uint8, uint32>>& agents);
    static void LoadFieldMultipliers(std::map<uint16, float>& into);
    // research field skill -> its datacore (group Datacores, requiredSkill1)
    static void LoadDatacores(std::map<uint16, uint32>& into);

    // locator agents:  the pilot's player contacts (charID, name), by name
    static void GetLocateCandidates(uint32 charID, std::vector<std::pair<uint32, std::string>>& into);
    // mapDenormalize name of a solar system, constellation or region
    static std::string GetMapName(uint32 itemID);

};

#endif  // _EVE_SERVER_AGENT_DATABASE_H__
