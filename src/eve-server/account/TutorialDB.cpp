/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
    Author:        Zhur, Allan
*/

#include "eve-server.h"

#include "account/TutorialDB.h"

PyRep *TutorialDB::GetPageCriterias(uint32 tutorialID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT pageID, criteriaID"
        " FROM tutorial_pages"
        " JOIN tutorial_page_criteria USING (pageID)"
        " WHERE tutorialID=%u", tutorialID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *TutorialDB::GetPages(uint32 tutorialID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT pageID, pageNumber, pageName, pageNameID, text, textID, imagePath, audioPath, NULL AS pageActionID, 0 AS dataID"
        " FROM tutorial_pages"
        " WHERE tutorialID=%u"
        " ORDER BY pageNumber", tutorialID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *TutorialDB::GetTutorial(uint32 tutorialID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT tutorialID, tutorialName, tutorialNameID, nextTutorialID, categoryID, 0 AS dataID"
        " FROM tutorials"
        " WHERE tutorialID=%u", tutorialID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *TutorialDB::GetTutorialCriterias(uint32 tutorialID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT criteriaID"
        " FROM tutorials_criterias"
        " WHERE tutorialID=%u", tutorialID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *TutorialDB::GetAllTutorials() {
    /*  this is wrong...our db is incomplete */
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT tutorialID, tutorialName, tutorialNameID, nextTutorialID, categoryID, 0 AS dataID"
        " FROM tutorials"))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *TutorialDB::GetAllCriterias() {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT criteriaID, criteriaName, messageText, messageTextID, audioPath, 0 AS dataID"
        " FROM tutorial_criteria"))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *TutorialDB::GetCategories() {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        " categoryID, categoryName, categoryNameID, description, descriptionID, 0 AS dataID"
        " FROM tutorial_categories"))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep* TutorialDB::GetTutorialsAndConnections()
{
    // the client does t.Index('tutorialID') and tc.Filter('tutorialID'), so both are CRowsets
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT tutorialID, tutorialName, tutorialNameID, categoryID, 0 AS dataID FROM tutorials"))
    {
        codelog(DATABASE__ERROR, "Error in GetTutorialsAndConnections query: %s", res.error.c_str());
        return PyStatic.NewNone();
    }
    PyRep* tutorials = DBResultToCRowset(res);

    // Crucible's sequence (per race), plus the next tutorial of our own tutorials for every race
    DBQueryResult cres;
    if (!sDatabase.RunQuery(cres,
        "SELECT tutorialID, raceID, nextTutorialID FROM tutorialConnections"
        " UNION ALL"
        " SELECT t.tutorialID, 0, t.nextTutorialID FROM tutorials AS t"
        " WHERE t.nextTutorialID IS NOT NULL AND t.nextTutorialID != 0"
        " AND NOT EXISTS (SELECT 1 FROM tutorialConnections AS c WHERE c.tutorialID = t.tutorialID)"))
    {
        codelog(DATABASE__ERROR, "Error in GetTutorialsAndConnections query: %s", cres.error.c_str());
        PyDecRef(tutorials);
        return PyStatic.NewNone();
    }

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, tutorials);
        tuple->SetItem(1, DBResultToCRowset(cres));
    return tuple;
}

void TutorialDB::LogEvent(uint32 charID, uint32 tutorialID, uint32 pageID, uint16 eventTypeID, uint32 seconds)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO chrTutorialEvents (characterID, tutorialID, pageID, eventTypeID, eventDateTime, secondsSpent)"
        " VALUES (%u, %u, %u, %u, %lli, %u)"
        " ON DUPLICATE KEY UPDATE pageID = VALUES(pageID), eventTypeID = VALUES(eventTypeID),"
        " eventDateTime = VALUES(eventDateTime), secondsSpent = VALUES(secondsSpent)",
        charID, tutorialID, pageID, eventTypeID, (int64)GetFileTimeNow(), seconds);
}

PyRep* TutorialDB::GetCharacterTutorialState(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT tutorialID, pageID, eventTypeID FROM chrTutorialEvents WHERE characterID = %u ORDER BY eventDateTime", charID))
    {
        codelog(DATABASE__ERROR, "Error in GetCharacterTutorialState query: %s", res.error.c_str());
        return new PyList();
    }
    return DBResultToCRowset(res);
}

PyRep* TutorialDB::GetCareerAgents()
{
    PyDict* dict = new PyDict();
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT careerType, agentID FROM agtCareerAgents ORDER BY careerType, agentID")) {
        codelog(DATABASE__ERROR, "Error in GetCareerAgents query: %s", res.error.c_str());
        return dict;
    }
    std::map<uint8, PyList*> lists;
    DBResultRow row;
    while (res.GetRow(row)) {
        std::map<uint8, PyList*>::iterator itr = lists.find(row.GetUInt(0));
        if (itr == lists.end())
            itr = lists.emplace(row.GetUInt(0), new PyList()).first;
        itr->second->AddItem(new PyInt(row.GetUInt(1)));
    }
    for (auto cur : lists)
        dict->SetItem(new PyInt(cur.first), cur.second);
    return dict;
}

PyRep* TutorialDB::GetTutorialAgents(const std::vector<uint32>& agentIDs)
{
    std::ostringstream ids;
    ids << "0";
    for (auto cur : agentIDs)
        ids << "," << cur;

    // bloodline and gender come from the npc character, like AgentDB::LoadAgentData
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT agt.agentID, agt.agentTypeID, agt.divisionID, agt.level, agt.locationID AS stationID,"
        " bl.bloodlineID, agt.quality, agt.corporationID, chr.gender"
        " FROM agtAgents AS agt"
        " LEFT JOIN chrNPCCharacters AS chr ON chr.characterID = agt.agentID"
        " LEFT JOIN bloodlineTypes AS bl ON bl.typeID = chr.typeID"
        " WHERE agt.agentID IN (%s)", ids.str().c_str()))
    {
        codelog(DATABASE__ERROR, "Error in GetTutorialAgents query: %s", res.error.c_str());
        return new PyList();
    }
    return DBResultToRowset(res);
}
