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
    Author:        Zhur
    Updates:    Allan
*/

#ifndef __PETITIONER_SERVICE_H_INCL__
#define __PETITIONER_SERVICE_H_INCL__

#include "services/Service.h"
#include "admin/PetitionDB.h"

class Client;

/* petition system (client:  script/ui/shared/neocom/petition.py)
 *  players file, message, cancel, delete and rate petitions; petitionees (ROLE_PETITIONEE, GMs) work the queues.
 *  works from character select too (OocCharacterID)
 */
class PetitionerService : public Service<PetitionerService> {
public:
    PetitionerService();

protected:
    // player
    PyResult GetCategories(PyCallArgs& call);
    PyResult GetCategoryHierarchicalInfo(PyCallArgs& call);
    PyResult GetUserCatalogCountry(PyCallArgs& call);
    PyResult MayPetition(PyCallArgs& call, PyInt* categoryID, PyRep* oocCharID);
    PyResult GetCategoryProperties(PyCallArgs& call, PyRep* categoryID);
    PyResult PropertyPopulationInfo(PyCallArgs& call, PyRep* inputInfo, PyRep* oocCharID);
    PyResult GetClientPickerInfo(PyCallArgs& call, PyRep* filter, PyRep* elementName);
    PyResult CreatePetition(PyCallArgs& call, PyRep* subject, PyRep* petition, PyInt* categoryID, PyRep* retval, PyRep* oocCharID,
                            std::optional<PyRep*> chatLog, std::optional<PyRep*> combatLog, std::optional<PyRep*> properties);
    PyResult GetMyPetitionsEx(PyCallArgs& call);
    PyResult GetPetitionMessages(PyCallArgs& call, PyInt* petitionID);
    PyResult GetUnreadMessages(PyCallArgs& call);
    PyResult MarkAsRead(PyCallArgs& call, PyInt* messageID);
    PyResult PetitionerChat(PyCallArgs& call, PyInt* petitionID, PyRep* message);
    PyResult CancelPetition(PyCallArgs& call, PyInt* petitionID);
    PyResult DeletePetition(PyCallArgs& call, PyInt* petitionID);
    PyResult AddPetitionRating(PyCallArgs& call, PyInt* petitionID, PyInt* responseTime, PyInt* helpfulness, PyInt* attitude,
                               PyRep* comment, std::optional<PyRep*> ratingTime);
    PyResult UpdatePetitionRating(PyCallArgs& call, PyInt* petitionID, PyInt* responseTime, PyInt* helpfulness, PyInt* attitude,
                                  PyRep* comment);

    // petitionee
    PyResult GetQueues(PyCallArgs& call);
    PyResult GetPetitionQueue(PyCallArgs& call, PyInt* queueID);
    PyResult GetClaimedPetitions(PyCallArgs& call);
    PyResult ClaimPetition(PyCallArgs& call, PyInt* petitionID);
    PyResult UnClaimPetition(PyCallArgs& call, PyInt* petitionID);
    PyResult EscalatePetition(PyCallArgs& call, PyInt* petitionID, PyRep* escalatesTo);
    PyResult PetitioneeChat(PyCallArgs& call, PyInt* petitionID, PyRep* message, std::optional<PyRep*> comment);
    PyResult ClosePetition(PyCallArgs& call, PyInt* petitionID);
    PyResult GetLog(PyCallArgs& call, PyInt* petitionID);
    PyResult GetEvents(PyCallArgs& call);

private:
    static bool IsPetitionee(Client* pClient);
    static void CheckPetitionee(Client* pClient);
    // session charid, else the OOC character, which must be on this account.  0 when invalid
    static uint32 GetPetitionerID(Client* pClient, PyRep* oocCharID);
    // the petition, which must exist and belong to this account (or the caller is a petitionee)
    static void GetPetitionFor(Client* pClient, uint32 petitionID, PetitionRow& into, bool petitioneeMayAccess = true);
    static PyObject* PetitionToKeyVal(const PetitionRow& row, const std::map<uint8, PetitionQueue>& queues);
    static PyList* PetitionsToList(const std::vector<PetitionRow>& rows);
    static PyObject* MessageToKeyVal(const PetitionMessage& msg);
    static void Log(Client* pClient, uint32 petitionID, uint8 eventID);
    // ratings are 0-100 in steps of 10
    static uint8 ClampRating(int64 value);

    // notifications.  args are consumed
    static void NotifyPetitionees(const char* name, PyTuple* args);
    static void NotifyCharacter(uint32 charID, const char* name, PyTuple* args);
};

#endif
