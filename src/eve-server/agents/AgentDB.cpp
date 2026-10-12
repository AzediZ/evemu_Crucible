
 /**
  * @name AgentDB.cpp
  *   Agent DB operations
  *    original agent code by zhur, this was completely rewritten based on new data.
  *
  * @Author:        Allan
  * @date:      24 June 2018
  *
  */

/** @todo  fix this....not all agents have an entry in chrNPCCharacters table.  */

#include "eve-server.h"

#include "agents/AgentDB.h"
#include "StaticDataMgr.h"
#include "inventory/AttributeEnum.h"


bool AgentDB::GetResearch(uint32 charID, uint32 agentID, Research& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT skillTypeID, pointsBanked, pointsPerDay, lastUpdate, startDate FROM chrAgentResearch"
        " WHERE characterID = %u AND agentID = %u", charID, agentID))
    {
        codelog(DATABASE__ERROR, "Error in GetResearch query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    into.skillTypeID = row.GetUInt(0);
    into.pointsBanked = row.GetDouble(1);
    into.pointsPerDay = row.GetDouble(2);
    into.lastUpdate = row.GetInt64(3);
    into.startDate = row.GetInt64(4);
    return true;
}

void AgentDB::SaveResearch(uint32 charID, uint32 agentID, const Research& data)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO chrAgentResearch (characterID, agentID, skillTypeID, pointsBanked, pointsPerDay, lastUpdate, startDate)"
        " VALUES (%u, %u, %u, %f, %f, %lli, %lli)"
        " ON DUPLICATE KEY UPDATE skillTypeID = VALUES(skillTypeID), pointsBanked = VALUES(pointsBanked),"
        " pointsPerDay = VALUES(pointsPerDay), lastUpdate = VALUES(lastUpdate)",
        charID, agentID, data.skillTypeID, data.pointsBanked, data.pointsPerDay, data.lastUpdate, data.startDate);
}

void AgentDB::DeleteResearch(uint32 charID, uint32 agentID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM chrAgentResearch WHERE characterID = %u AND agentID = %u", charID, agentID);
}

uint32 AgentDB::CountResearch(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM chrAgentResearch WHERE characterID = %u", charID))
        return 0;
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

void AgentDB::GetResearchList(uint32 charID, std::map<uint32, Research>& into, std::map<uint32, std::pair<uint8, uint32>>& agents)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT r.agentID, r.skillTypeID, r.pointsBanked, r.pointsPerDay, r.lastUpdate, r.startDate, a.level, a.locationID"
        " FROM chrAgentResearch AS r"
        " LEFT JOIN agtAgents AS a ON a.agentID = r.agentID"
        " WHERE r.characterID = %u", charID))
    {
        codelog(DATABASE__ERROR, "Error in GetResearchList query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        Research data = Research();
        data.skillTypeID = row.GetUInt(1);
        data.pointsBanked = row.GetDouble(2);
        data.pointsPerDay = row.GetDouble(3);
        data.lastUpdate = row.GetInt64(4);
        data.startDate = row.GetInt64(5);
        into[row.GetUInt(0)] = data;
        agents[row.GetUInt(0)] = std::make_pair((uint8)(row.IsNull(6) ? 0 : row.GetUInt(6)), (row.IsNull(7) ? 0 : row.GetUInt(7)));
    }
}

void AgentDB::LoadFieldMultipliers(std::map<uint16, float>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT skillTypeID, multiplier FROM agtResearchFieldMultipliers")) {
        codelog(DATABASE__ERROR, "Error in LoadFieldMultipliers query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into[row.GetUInt(0)] = row.GetFloat(1);
}

void AgentDB::LoadDatacores(std::map<uint16, uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT t.typeID, IF(a.valueInt IS NULL, a.valueFloat, a.valueInt)"
        " FROM invTypes AS t"
        " JOIN dgmTypeAttributes AS a ON a.typeID = t.typeID AND a.attributeID = %u"
        " WHERE t.groupID = %u AND t.published = 1"
        " ORDER BY t.typeID", AttrRequiredSkill1, EVEDB::invGroups::Datacores))
    {
        codelog(DATABASE__ERROR, "Error in LoadDatacores query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        uint16 skillID((uint16)row.GetDouble(1));
        if (into.find(skillID) == into.end())
            into[skillID] = row.GetUInt(0);
    }
}

void AgentDB::GetLocateCandidates(uint32 charID, std::vector<std::pair<uint32, std::string>>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT c.contactID, ch.characterName FROM chrContacts AS c"
        " JOIN chrCharacters AS ch ON ch.characterID = c.contactID"
        " WHERE c.ownerID = %u AND c.contactID != %u"
        " ORDER BY ch.characterName LIMIT 30", charID, charID))
    {
        codelog(DATABASE__ERROR, "Error in GetLocateCandidates query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(std::make_pair(row.GetUInt(0), std::string(row.GetText(1))));
}

std::string AgentDB::GetMapName(uint32 itemID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemName FROM mapDenormalize WHERE itemID = %u", itemID))
        return "";
    DBResultRow row;
    if (!res.GetRow(row))
        return "";
    return row.GetText(0);
}

void AgentDB::LoadAgentData(uint32 agentID, AgentData& data)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "   agt.agentTypeID,"
        "   agt.divisionID,"
        "   agt.level,"
        "   agt.quality,"
        "   agt.corporationID,"
        "   agt.locationID, "   //5
        "   agt.isLocator,"
        "   chr.solarSystemID,"
        "   chr.stationID,"
        "   chr.gender,"
        "   bl.bloodlineID,"    //10
        "   itm.typeID, "
        "   crp.friendID, "
        "   crp.enemyID, "
        "   crp.factionID, "
        "   chr.characterName " //15
        " FROM agtAgents AS agt"
        " LEFT JOIN chrNPCCharacters AS chr ON chr.characterID = agt.agentID"
        " LEFT JOIN crpNPCCorporations AS crp ON crp.corporationID = agt.corporationID"
        " LEFT JOIN bloodlineTypes AS bl ON bl.typeID = chr.typeID"
        " LEFT JOIN mapDenormalize AS itm ON itm.itemID = agt.locationID"
        " WHERE agt.agentID = %u", agentID))
    {
        codelog(DATABASE__ERROR, "Error in GetAgents query: %s", res.error.c_str());
        return;
    }

    /** @todo  there may be some errors here with agents in space or NOT in stations....will have to test and fix as they come up.  */
    DBResultRow row;
    if (res.GetRow(row)) {
        if (row.GetUInt(10) == 0) {
            _log(DATABASE__MESSAGE, "No charTypeID for Agent %u", agentID);
            return;
        }
        data.typeID         = row.GetUInt(0);
        data.divisionID     = row.GetUInt(1);
        data.level          = row.GetUInt(2);
        data.quality        = row.GetInt(3);
        data.corporationID  = row.GetUInt(4);
        data.locationID     = row.GetUInt(5);
        data.locator        = row.GetBool(6);
        data.solarSystemID  = row.GetUInt(7);
        data.stationID      = row.GetUInt(8);
        data.gender         = row.GetBool(9);
        data.bloodlineID    = row.GetUInt(10);
        data.locationTypeID = row.GetUInt(11);
        data.friendCorp     = (row.IsNull(12) ? 0 : row.GetInt(12));
        data.enemyCorp      = (row.IsNull(13) ? 0 : row.GetInt(13));
        data.factionID      = row.GetInt(14);
        data.raceID         = sDataMgr.GetFactionRace(data.factionID);
        data.name           = row.GetText(15);
        data.research       = (data.typeID == Agents::Type::Research);
    }
}

void AgentDB::LoadAgentSkills(uint32 agentID, std::map< uint16, uint8 >& data)
{
    // SELECT typeID, level FROM agtSkillLevel WHERE agentID = %u
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT typeID, level FROM agtSkillLevel WHERE agentID = %u", agentID))
        codelog(DATABASE__ERROR, "Error in LoadAgentSkills query: %s", res.error.c_str());

    DBResultRow row;
    while (res.GetRow(row))
        data[row.GetInt(0)] = row.GetInt(1);
}
