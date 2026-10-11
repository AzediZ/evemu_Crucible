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
    Author:        Reve
*/

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "dungeon/DungeonDB.h"
#include "dungeon/DungeonExplorationMgrService.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/DungeonMgr.h"

/* the journal's expeditions (escalations):  journal.py GetExpeditions / ShowExpeditions / PopupDungeonDescription,
 *  keeper.CanWarpToPathPlex and beyonce.CmdWarpToStuff('epinstance') warp to them
 */

DungeonExplorationMgrService::DungeonExplorationMgrService() :
    Service("dungeonExplorationMgr")
{
    this->Add("GetMyEscalatingPathDetails", &DungeonExplorationMgrService::GetMyEscalatingPathDetails);
    this->Add("DeleteExpiredPathStep", &DungeonExplorationMgrService::DeleteExpiredPathStep);
}

PyResult DungeonExplorationMgrService::GetMyEscalatingPathDetails(PyCallArgs &call) {
    /* a util.Rowset (iterated, and indexed by instanceID) of
     *   instanceID, dungeon (KeyVal instanceID, solarSystemID, creationTime, dungeonNameID),
     *   destDungeon (KeyVal dungeonID, dungeonNameID;  None hides the row), srcDungeon (KeyVal or None),
     *   pathStep (KeyVal pathStepID, journalEntryTemplateID), expiryTime (wallclock FILETIME)
     */
    _log(DUNG__CALL, "DungeonExplorationMgrService::Handle_GetMyEscalatingPathDetails() size=%lu", call.tuple->size());

    auto nameID = [](uint32 dungeonID) -> PyRep* {
        std::shared_ptr<const Dungeon::Dungeon> pDungeon(sDunDataMgr.GetDungeon(dungeonID));
        if ((pDungeon.get() == nullptr) or (pDungeon->nameID == 0))
            return PyStatic.NewNone();
        return new PyInt(pDungeon->nameID);
    };

    PyList* header = new PyList();
        header->AddItem(new PyString("instanceID"));
        header->AddItem(new PyString("dungeon"));
        header->AddItem(new PyString("destDungeon"));
        header->AddItem(new PyString("srcDungeon"));
        header->AddItem(new PyString("pathStep"));
        header->AddItem(new PyString("expiryTime"));
    PyList* lines = new PyList();

    std::vector<EscalationRow> rows;
    DungeonDB::GetEscalationsForChar(call.client->GetCharacterID(), rows);
    for (auto& row : rows) {
        PyDict* dungeon = new PyDict();
            dungeon->SetItemString("instanceID", new PyInt(row.instanceID));
            dungeon->SetItemString("solarSystemID", new PyInt(row.solarSystemID));
            dungeon->SetItemString("creationTime", new PyLong(row.creationTime));
            dungeon->SetItemString("dungeonNameID", nameID(row.destDungeonID));
        PyDict* dest = new PyDict();
            dest->SetItemString("dungeonID", new PyInt(row.destDungeonID));
            dest->SetItemString("dungeonNameID", nameID(row.destDungeonID));
        PyRep* src(PyStatic.NewNone());
        if (row.srcDungeonID != 0) {
            PyDict* srcDict = new PyDict();
                srcDict->SetItemString("dungeonID", new PyInt(row.srcDungeonID));
                srcDict->SetItemString("dungeonNameID", nameID(row.srcDungeonID));
            src = new PyObject("util.KeyVal", srcDict);
        }
        PyDict* step = new PyDict();
            step->SetItemString("pathStepID", new PyInt(row.escalationID));
            step->SetItemString("journalEntryTemplateID", new PyInt(row.journalMessageID));
        PyList* line = new PyList();
            line->AddItem(new PyInt(row.instanceID));
            line->AddItem(new PyObject("util.KeyVal", dungeon));
            line->AddItem(new PyObject("util.KeyVal", dest));
            line->AddItem(src);
            line->AddItem(new PyObject("util.KeyVal", step));
            line->AddItem(new PyLong(row.expiryTime));
        lines->AddItem(line);
    }

    PyDict* args = new PyDict();
        args->SetItemString("header", header);
        args->SetItemString("RowClass", new PyToken("util.Row"));
        args->SetItemString("lines", lines);
    return new PyObject("util.Rowset", args);
}

PyResult DungeonExplorationMgrService::DeleteExpiredPathStep(PyCallArgs &call, PyInt* instanceID) {
    // journal 'Remove' on an expedition (fire and forget)
    _log(DUNG__CALL, "DungeonExplorationMgrService::Handle_DeleteExpiredPathStep() %u", instanceID->value());

    EscalationRow row = EscalationRow();
    if (!DungeonDB::GetEscalation(instanceID->value(), row) or (row.characterID != (uint32)call.client->GetCharacterID()))
        return PyStatic.NewNone();
    DungeonDB::SetEscalationState(row.instanceID, 3);

    // a site nobody else has goes with the entry
    SystemManager* pSystem(sEntityList.FindSystem(row.solarSystemID));
    if (pSystem != nullptr)
        pSystem->GetDungMgr()->OnEscalationRemoved(row.instanceID);
    return PyStatic.NewNone();
}
