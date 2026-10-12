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


#ifndef __TUTORIALDB_H_INCL__
#define __TUTORIALDB_H_INCL__

#include "ServiceDB.h"

class PyRep;

class TutorialDB : public ServiceDB
{
public:
    PyRep *GetPageCriterias(uint32 tutorialID);
    PyRep *GetPages(uint32 tutorialID);
    PyRep *GetTutorial(uint32 tutorialID);
    PyRep *GetTutorialCriterias(uint32 tutorialID);
    PyRep *GetAllTutorials();
    PyRep *GetAllCriterias();
    PyRep *GetCategories();
    // (tutorials CRowset tutorialID, tutorialName, tutorialNameID, categoryID, dataID; connections CRowset tutorialID, raceID, nextTutorialID)
    static PyRep* GetTutorialsAndConnections();

    // the character's latest event per tutorial (chrTutorialEvents)
    static void LogEvent(uint32 charID, uint32 tutorialID, uint32 pageID, uint16 eventTypeID, uint32 seconds);
    // CRowset tutorialID, pageID, eventTypeID
    static PyRep* GetCharacterTutorialState(uint32 charID);

    // career funnel:  dict careerType -> [agentIDs] (agtCareerAgents)
    static PyRep* GetCareerAgents();
    // rowset agentID, agentTypeID, divisionID, level, stationID, bloodlineID, quality, corporationID, gender
    static PyRep* GetTutorialAgents(const std::vector<uint32>& agentIDs);
};

#endif


