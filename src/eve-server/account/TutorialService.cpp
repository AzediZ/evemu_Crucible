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


#include "Client.h"
#include "account/TutorialService.h"

TutorialService::TutorialService() :
    Service("tutorialSvc")
{
    this->Add("GetTutorials", &TutorialService::GetTutorials);
    this->Add("GetTutorialInfo", &TutorialService::GetTutorialInfo);
    this->Add("GetTutorialAgents", &TutorialService::GetTutorialAgents);
    this->Add("GetCriterias", &TutorialService::GetCriterias);
    this->Add("GetCategories", &TutorialService::GetCategories);
    this->Add("GetCharacterTutorialState", &TutorialService::GetCharacterTutorialState);
    this->Add("GetTutorialsAndConnections", &TutorialService::GetTutorialsAndConnections);
    this->Add("GetCareerAgents", &TutorialService::GetCareerAgents);
    this->Add("GetActions", &TutorialService::GetActions);
    this->Add("LogStarted", &TutorialService::LogStarted);
    this->Add("LogCompleted", &TutorialService::LogCompleted);
    this->Add("LogAborted", &TutorialService::LogAborted);
    this->Add("LogClosed", &TutorialService::LogClosed);
    this->Add("LogAppClosed", &TutorialService::LogAppClosed);
}

PyResult TutorialService::GetTutorials(PyCallArgs &call) {
  sLog.Warning( "TutorialService::Handle_GetTutorials()", "size=%lu", call.tuple->size());
  call.Dump(SERVICE__CALL_DUMP);
    return(m_db.GetAllTutorials());
}

PyResult TutorialService::GetTutorialInfo(PyCallArgs &call, PyInt* tutorialID) {
  sLog.Warning( "TutorialService::Handle_GetTutorialInfo()", "size=%lu", call.tuple->size());
  call.Dump(SERVICE__CALL_DUMP);
    // KeyVal(pagecriterias, pages, tutorial, criterias, actions, pageactions).  rows use message IDs (pageNameID, textID, ...)
    PyRep* pagecriterias = m_db.GetPageCriterias(tutorialID->value());
    PyRep* pages = m_db.GetPages(tutorialID->value());
    PyRep* tutorial = m_db.GetTutorial(tutorialID->value());
    PyRep* criterias = m_db.GetTutorialCriterias(tutorialID->value());
    if ((pagecriterias == nullptr) or (pages == nullptr) or (tutorial == nullptr) or (criterias == nullptr)) {
        codelog(SERVICE__ERROR, "An error occured while getting tutorial %u.", tutorialID->value());
        PySafeDecRef(pagecriterias);
        PySafeDecRef(pages);
        PySafeDecRef(tutorial);
        PySafeDecRef(criterias);
        return nullptr;
    }

    PyDict* dict = new PyDict();
        dict->SetItemString("pagecriterias", pagecriterias);
        dict->SetItemString("pages", pages);
        dict->SetItemString("tutorial", tutorial);
        dict->SetItemString("criterias", criterias);
        // tutorial and page actions (rows with actionID / pageID, actionID):  none in our content
        dict->SetItemString("actions", new PyList());
        dict->SetItemString("pageactions", new PyList());
    return new PyObject("util.KeyVal", dict);
}

PyResult TutorialService::GetTutorialAgents(PyCallArgs &call, PyList* agentIDs) {
    /*  this should be cached
          [PyTuple 4 items]
            [PyInt 1]
            [PyString "GetTutorialAgents"]
            [PyTuple 1 items]
              [PyList 12 items]
                [PyInt 3018921]
                [PyInt 3019349]
                [PyInt 3019337]
                [PyInt 3018935]
                [PyInt 3019371]
                [PyInt 3019355]
                [PyInt 3018923]
                [PyInt 3019341]
                [PyInt 3019333]
                [PyInt 3018920]
                [PyInt 3019346]
                [PyInt 3019343]
            [PyDict 1 kvp]
              [PyString "machoVersion"]
              [PyInt 1]

      [PySubStream 377 bytes]
        [PyList 12 items]
          [PyPackedRow 28 bytes]
            ["agentID" => <3018920> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015037> [I4]]
            ["bloodlineID" => <14> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <0> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3018921> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015021> [I4]]
            ["bloodlineID" => <6> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3018923> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015029> [I4]]
            ["bloodlineID" => <12> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3018935> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015027> [I4]]
            ["bloodlineID" => <11> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3019333> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015036> [I4]]
            ["bloodlineID" => <7> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3019337> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015010> [I4]]
            ["bloodlineID" => <5> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3019341> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015016> [I4]]
            ["bloodlineID" => <8> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3019343> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015041> [I4]]
            ["bloodlineID" => <3> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3019346> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015046> [I4]]
            ["bloodlineID" => <4> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3019349> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015020> [I4]]
            ["bloodlineID" => <13> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <0> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3019355> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015005> [I4]]
            ["bloodlineID" => <11> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <0> [Bool]]
          [PyPackedRow 28 bytes]
            ["agentID" => <3019371> [I4]]
            ["agentTypeID" => <8> [I4]]
            ["divisionID" => <22> [I4]]
            ["level" => <1> [UI1]]
            ["stationID" => <60015001> [I4]]
            ["bloodlineID" => <1> [UI1]]
            ["quality" => <0> [I4]]
            ["corporationID" => <0> [I4]]
            ["gender" => <1> [Bool]]
                */
    std::vector<uint32> ids;
    for (PyList::const_iterator itr = agentIDs->begin(); itr != agentIDs->end(); ++itr)
        ids.push_back(PyRep::IntegerValueU32(*itr));
    return TutorialDB::GetTutorialAgents(ids);
}

PyResult TutorialService::GetCriterias(PyCallArgs &call) {
  sLog.White( "TutorialService::Handle_GetCriterias()", "size=%lu", call.tuple->size());
  call.Dump(SERVICE__CALL_DUMP);
    return(m_db.GetAllCriterias());
}

PyResult TutorialService::GetCategories(PyCallArgs &call) {
  sLog.White( "TutorialService::Handle_GetCategories()", "size=%lu", call.tuple->size());
  call.Dump(SERVICE__CALL_DUMP);
    return(m_db.GetCategories());
}

//00:25:53 L TutorialService::Handle_GetCharacterTutorialState(): size= 0
PyResult TutorialService::GetCharacterTutorialState(PyCallArgs& call) {
    // rows (tutorialID, pageID, eventTypeID):  155 completed and 158 aborted are done, 158 also turns tutorials off
    return TutorialDB::GetCharacterTutorialState(call.client->GetCharacterID());
}

PyResult TutorialService::GetTutorialsAndConnections(PyCallArgs& call) {
    /*  no logs */
  /*  This is used to link tutorials using connections to other tutorials  */
            /*
            t, tc = sm.RemoteSvc('tutorialSvc').GetTutorialsAndConnections()
            self.tutorials = t.Index('tutorialID')
            tc = tc.Filter('tutorialID')
            self.tutorialConnections = defaultdict(dict)
            for tutID, rows in tc.iteritems():
                for each in rows:
                    self.tutorialConnections[tutID][each.raceID] = each.nextTutorialID
    */

    return TutorialDB::GetTutorialsAndConnections();
}

PyResult TutorialService::GetCareerAgents(PyCallArgs& call) {
  /*  Empty Call  */
  /**
        agentMapping = sm.RemoteSvc('tutorialSvc').GetCareerAgents()
        for careerType in agentMapping:
            agentIDs = []
            if careerType not in self.careerAgents:
                self.careerAgents[careerType] = {}
                self.careerAgents[careerType]['agent'] = {}
                self.careerAgents[careerType]['station'] = {}
            agentIDs = agentMapping.get(careerType, [])
            agents = sm.RemoteSvc('tutorialSvc').GetTutorialAgents(agentIDs)
            for agent in agents:
                self.careerAgents[careerType]['agent'][agent.agentID] = agent
                self.careerAgents[careerType]['station'][agent.agentID] = sm.GetService('map').GetStation(agent.stationID)
*/
    // {careerType (1 industry .. 5 advanced military): [agentIDs]}, empty when no agents are mapped
    return TutorialDB::GetCareerAgents();
}

PyResult TutorialService::GetActions(PyCallArgs& call)
{
    // rows with actionID, actionTypeID, actionData:  none in our content
    return new PyList();
}

void TutorialService::LogEvent(PyCallArgs& call, PyRep* tutorialID, PyRep* pageNo, PyRep* seconds, uint16 eventTypeID)
{
    uint32 tutID(PyRep::IntegerValueU32(tutorialID));
    if (tutID == 0)
        return;
    int64 secs(PyRep::IntegerValue(seconds));
    TutorialDB::LogEvent(call.client->GetCharacterID(), tutID, PyRep::IntegerValueU32(pageNo), eventTypeID, (uint32)(secs < 0 ? 0 : secs));
}

// appLogConst event types
PyResult TutorialService::LogStarted(PyCallArgs& call, PyRep* tutorialID, PyRep* pageNo, PyRep* seconds)
{
    LogEvent(call, tutorialID, pageNo, seconds, 245);
    return nullptr;
}

PyResult TutorialService::LogCompleted(PyCallArgs& call, PyRep* tutorialID, PyRep* pageNo, PyRep* seconds)
{
    LogEvent(call, tutorialID, pageNo, seconds, 155);
    return nullptr;
}

PyResult TutorialService::LogAborted(PyCallArgs& call, PyRep* tutorialID, PyRep* pageNo, PyRep* seconds)
{
    LogEvent(call, tutorialID, pageNo, seconds, 158);
    return nullptr;
}

PyResult TutorialService::LogClosed(PyCallArgs& call, PyRep* tutorialID, PyRep* pageNo, PyRep* seconds)
{
    LogEvent(call, tutorialID, pageNo, seconds, 156);
    return nullptr;
}

PyResult TutorialService::LogAppClosed(PyCallArgs& call, PyRep* tutorialID, PyRep* pageNo, PyRep* seconds)
{
    LogEvent(call, tutorialID, pageNo, seconds, 157);
    return nullptr;
}

TutorialLocationService::TutorialLocationService() :
    Service("tutorialLocationSvc", eAccessLevel_Character)
{
    this->Add("GetTutorialGoodies", &TutorialLocationService::GetTutorialGoodies);
    this->Add("GiveTutorialGoodies", &TutorialLocationService::GiveTutorialGoodies);
}

PyResult TutorialLocationService::GetTutorialGoodies(PyCallArgs& call, PyRep* tutorialID, PyRep* pageID, PyRep* pageNo)
{
    // goodies = sm.RemoteSvc('tutorialLocationSvc').GetTutorialGoodies(tutorialID, pageID, pageNo)  (empty = none)
    return new PyList();
}

PyResult TutorialLocationService::GiveTutorialGoodies(PyCallArgs& call, PyRep* tutorialID, PyRep* pageID, PyRep* pageNo)
{
    return new PyList();
}


/**
            sm.RemoteSvc('tutorialSvc').LogCompleted(tutorialID, pageNo, int(time))
        elif status == 'aborted':
            stat[sequenceID] = 'done'
            sm.RemoteSvc('tutorialSvc').LogAborted(tutorialID, pageNo, int(time))

                categories = sm.RemoteSvc('tutorialSvc').GetCategories()
                for category in categories:
                    self.categories[category.categoryID] = category
                    self.categories[category.categoryID].categoryName = localization.GetByMessageID(category.categoryNameID)
                    self.categories[category.categoryID].description = localization.GetByMessageID(category.descriptionID)

                criterias = sm.RemoteSvc('tutorialSvc').GetCriterias()
                for criteria in criterias:
                    self.criterias[criteria.criteriaID] = criteria
            actions = sm.RemoteSvc('tutorialSvc').GetActions()
            for action in actions:
                self.actions[action.actionID] = action

            tutData = sm.RemoteSvc('tutorialSvc').GetTutorialInfo(tutorialID)
                sm.RemoteSvc('tutorialSvc').LogAppClosed(tutorialID, pageNo, int(time))
                        sm.RemoteSvc('tutorialSvc').LogClosed(tutorialID, pageNo, int(time))
                    sm.RemoteSvc('tutorialSvc').LogStarted(tutorialID, pageNo, int(time))
            sm.RemoteSvc('tutorialSvc').LogCompleted(tutorialID, pageNo, int(time))
        return sm.RemoteSvc('tutorialLocationSvc').GiveTutorialGoodies(tutorialID, pageID, pageNo)
                tutData = sm.RemoteSvc('tutorialSvc').GetTutorialInfo(VID)

        rs = sm.RemoteSvc('tutorialSvc').GetCharacterTutorialState()
        if not rs or len(rs) == 0:
            return



            */
