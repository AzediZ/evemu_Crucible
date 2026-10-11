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

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "admin/PetitionerService.h"

namespace {
    // max open (not closed) petitions per character
    const uint32 s_maxOpenPetitions = 5;
    // const.petitionMaxChatLogSize / petitionMaxCombatLogSize
    const size_t s_maxLogSize = 200000;
    // a closed petition may be rated for a week
    const int64 s_ratingWindow = EvE::Time::Week;

    // petLog event texts (the client builds a CRowset with eventText as DBTYPE_STR)
    const std::pair<uint8, const char*> s_events[] = {
        { Petition::Event::Created,         "Created" },
        { Petition::Event::Claimed,         "Claimed" },
        { Petition::Event::Unclaimed,       "Unclaimed" },
        { Petition::Event::Escalated,       "Escalated" },
        { Petition::Event::Closed,          "Closed" },
        { Petition::Event::Cancelled,       "Cancelled by petitioner" },
        { Petition::Event::Deleted,         "Deleted by petitioner" },
        { Petition::Event::PetitionerMsg,   "Petitioner message" },
        { Petition::Event::GMMessage,       "GM message" },
        { Petition::Event::GMComment,       "GM comment" },
        { Petition::Event::Rated,           "Rated" }
    };

    PyRep* IntOrNone(uint32 value)
    {
        if (value == 0)
            return PyStatic.NewNone();
        return new PyInt(value);
    }

    PyRep* LongOrNone(int64 value)
    {
        if (value == 0)
            return PyStatic.NewNone();
        return new PyLong(value);
    }

    std::string Truncate(const std::string& str, size_t max)
    {
        // the client keeps the end of its logs
        if (str.size() <= max)
            return str;
        return str.substr(str.size() - max);
    }
}

PetitionerService::PetitionerService() :
    Service("petitioner", eAccessLevel_User)
{
    // player
    this->Add("GetCategories", &PetitionerService::GetCategories);
    this->Add("GetCategoryHierarchicalInfo", &PetitionerService::GetCategoryHierarchicalInfo);
    this->Add("GetUserCatalogCountry", &PetitionerService::GetUserCatalogCountry);
    this->Add("MayPetition", &PetitionerService::MayPetition);
    this->Add("GetCategoryProperties", &PetitionerService::GetCategoryProperties);
    this->Add("PropertyPopulationInfo", &PetitionerService::PropertyPopulationInfo);
    this->Add("GetClientPickerInfo", &PetitionerService::GetClientPickerInfo);
    this->Add("CreatePetition", &PetitionerService::CreatePetition);
    this->Add("GetMyPetitionsEx", &PetitionerService::GetMyPetitionsEx);
    this->Add("GetPetitionMessages", &PetitionerService::GetPetitionMessages);
    this->Add("GetUnreadMessages", &PetitionerService::GetUnreadMessages);
    this->Add("MarkAsRead", &PetitionerService::MarkAsRead);
    this->Add("PetitionerChat", &PetitionerService::PetitionerChat);
    this->Add("CancelPetition", &PetitionerService::CancelPetition);
    this->Add("DeletePetition", &PetitionerService::DeletePetition);
    this->Add("AddPetitionRating", &PetitionerService::AddPetitionRating);
    this->Add("UpdatePetitionRating", &PetitionerService::UpdatePetitionRating);
    // petitionee
    this->Add("GetQueues", &PetitionerService::GetQueues);
    this->Add("GetPetitionQueue", &PetitionerService::GetPetitionQueue);
    this->Add("GetClaimedPetitions", &PetitionerService::GetClaimedPetitions);
    this->Add("ClaimPetition", &PetitionerService::ClaimPetition);
    this->Add("UnClaimPetition", &PetitionerService::UnClaimPetition);
    this->Add("EscalatePetition", &PetitionerService::EscalatePetition);
    this->Add("PetitioneeChat", &PetitionerService::PetitioneeChat);
    this->Add("ClosePetition", &PetitionerService::ClosePetition);
    this->Add("GetLog", &PetitionerService::GetLog);
    this->Add("GetEvents", &PetitionerService::GetEvents);
}

/*************************************************************************/
/* helpers                                                               */
/*************************************************************************/
bool PetitionerService::IsPetitionee(Client* pClient)
{
    return ((pClient->GetAccountRole() & (Acct::Role::PETITIONEE | Acct::Role::GML | Acct::Role::GMH | Acct::Role::ADMIN)) != 0);
}

void PetitionerService::CheckPetitionee(Client* pClient)
{
    if (!IsPetitionee(pClient))
        throw CustomError("You are not a petitionee.");
}

uint32 PetitionerService::GetPetitionerID(Client* pClient, PyRep* oocCharID)
{
    if (pClient->GetCharacterID() > 0)
        return pClient->GetCharacterID();
    if ((oocCharID == nullptr) or oocCharID->IsNone())
        return 0;
    uint32 charID = (uint32)PyRep::IntegerValue(oocCharID);
    if (!IsCharacterID(charID))
        return 0;
    if (PetitionDB::GetCharacterAccount(charID) != (uint32)pClient->GetUserID())
        return 0;
    return charID;
}

void PetitionerService::GetPetitionFor(Client* pClient, uint32 petitionID, PetitionRow& into, bool petitioneeMayAccess)
{
    if (!PetitionDB::GetPetition(petitionID, into))
        throw CustomError("That petition does not exist.");
    if (into.userID == (uint32)pClient->GetUserID())
        return;
    if (petitioneeMayAccess and IsPetitionee(pClient))
        return;
    throw CustomError("That is not your petition.");
}

PyObject* PetitionerService::PetitionToKeyVal(const PetitionRow& row, const std::map<uint8, PetitionQueue>& queues)
{
    /* the client sets attributes on these rows (claimed, rating fields), so they are KeyVals.
     *  no 'rating' field:  the client treats that as the old single-score rating
     */
    int16 escalatesTo(-1);
    std::map<uint8, PetitionQueue>::const_iterator qItr = queues.find(row.queueID);
    if (qItr != queues.end())
        escalatesTo = qItr->second.escalatesTo;

    PyDict* dict = new PyDict();
        dict->SetItemString("petitionID",           new PyInt(row.petitionID));
        dict->SetItemString("petitionerID",         new PyInt(row.petitionerID));
        dict->SetItemString("categoryID",           new PyInt(row.categoryID));
        dict->SetItemString("subject",              new PyWString(row.subject));
        dict->SetItemString("petition",             new PyWString(row.petition));
        dict->SetItemString("createDate",           new PyLong(row.createDate));
        dict->SetItemString("touchDate",            new PyLong(row.touchDate));
        dict->SetItemString("claimed",              new PyInt(row.claimedBy > 0 ? 1 : 0));
        dict->SetItemString("closed",               new PyInt(row.closed ? 1 : 0));
        dict->SetItemString("deleted",              new PyInt(row.deleted ? 1 : 0));
        dict->SetItemString("updated",              new PyInt(row.updated ? 1 : 0));
        dict->SetItemString("rateable",             new PyInt((row.closed and (row.claimedBy > 0)) ? 1 : 0));
        dict->SetItemString("responseTimeRating",   (row.responseTimeRating < 0 ? PyStatic.NewNone() : new PyInt(row.responseTimeRating)));
        dict->SetItemString("helpfulnessRating",    (row.helpfulnessRating < 0 ? PyStatic.NewNone() : new PyInt(row.helpfulnessRating)));
        dict->SetItemString("attitudeRating",       (row.attitudeRating < 0 ? PyStatic.NewNone() : new PyInt(row.attitudeRating)));
        dict->SetItemString("ratingComment",        new PyWString(row.ratingComment));
        dict->SetItemString("ratingDateTime",       LongOrNone(row.ratingDateTime));
        dict->SetItemString("properties",           PyStatic.NewNone());
        dict->SetItemString("escalatesTo",          (escalatesTo < 0 ? PyStatic.NewNone() : new PyInt(escalatesTo)));
        dict->SetItemString("email",                PyStatic.NewNone());
        dict->SetItemString("isOnline",             new PyInt(sEntityList.FindClientByCharID(row.petitionerID) != nullptr ? 1 : 0));
    return new PyObject("util.KeyVal", dict);
}

PyList* PetitionerService::PetitionsToList(const std::vector<PetitionRow>& rows)
{
    std::map<uint8, PetitionQueue> queues;
    PetitionDB::GetQueues(queues);
    PyList* list = new PyList();
    for (auto& cur : rows)
        list->AddItem(PetitionToKeyVal(cur, queues));
    return list;
}

PyObject* PetitionerService::MessageToKeyVal(const PetitionMessage& msg)
{
    PyDict* dict = new PyDict();
        dict->SetItemString("messageID",    new PyInt(msg.messageID));
        dict->SetItemString("petitionID",   new PyInt(msg.petitionID));
        dict->SetItemString("senderID",     IntOrNone(msg.senderID));
        dict->SetItemString("text",         new PyWString(msg.text));
        dict->SetItemString("sentDate",     new PyLong(msg.sentDate));
        dict->SetItemString("comment",      new PyInt(msg.comment ? 1 : 0));
    return new PyObject("util.KeyVal", dict);
}

void PetitionerService::Log(Client* pClient, uint32 petitionID, uint8 eventID)
{
    PetitionDB::AddLog(petitionID, pClient->GetCharacterID(), PetitionDB::GetAccountName(pClient->GetUserID()), eventID);
}

uint8 PetitionerService::ClampRating(int64 value)
{
    if (value < 0)
        value = 0;
    if (value > 100)
        value = 100;
    return (uint8)(((value + 5) / 10) * 10);
}

void PetitionerService::NotifyPetitionees(const char* name, PyTuple* args)
{
    std::vector<Client*> list;
    sEntityList.GetClients(list);
    for (auto cur : list) {
        if (!IsPetitionee(cur))
            continue;
        PyTuple* tuple = args->Clone()->AsTuple();
        cur->SendNotification(name, "charid", &tuple, false);
    }
    PyDecRef(args);
}

void PetitionerService::NotifyCharacter(uint32 charID, const char* name, PyTuple* args)
{
    Client* pClient(nullptr);
    if (charID > 0)
        pClient = sEntityList.FindClientByCharID(charID);
    if (pClient == nullptr) {
        PyDecRef(args);
        return;
    }
    pClient->SendNotification(name, "charid", &args, false);
}

/*************************************************************************/
/* player methods                                                        */
/*************************************************************************/
PyResult PetitionerService::GetCategories(PyCallArgs& call)
{
    // rows with .categoryID, .displayName (category names for the petition lists)
    std::vector<PetitionCategory> list;
    PetitionDB::GetCategories(list);
    PyList* result = new PyList();
    for (auto& cur : list) {
        PyDict* dict = new PyDict();
            dict->SetItemString("categoryID",   new PyInt(cur.categoryID));
            dict->SetItemString("parentID",     new PyInt(cur.parentID));
            dict->SetItemString("displayName",  new PyWString(cur.displayName));
        result->AddItem(new PyObject("util.KeyVal", dict));
    }
    return result;
}

PyResult PetitionerService::GetCategoryHierarchicalInfo(PyCallArgs& call)
{
    /* ({parentID: (name, languageID)}, {parentID: {childID: (name, languageID)}}, {childID: description},
     *  {(childID, country): (url, label)})   the last one is billing links (none here)
     *  the client keeps the rows of its own language, and retries with 'en-us' when that leaves nothing
     */
    std::vector<PetitionCategory> list;
    PetitionDB::GetCategories(list);

    PyDict* parents = new PyDict();
    PyDict* children = new PyDict();
    PyDict* descriptions = new PyDict();
    std::map<uint32, PyDict*> childMap;
    for (auto& cur : list) {
        PyTuple* name = new PyTuple(2);
            name->SetItem(0, new PyWString(cur.displayName));
            name->SetItem(1, new PyString(cur.languageID));
        if (cur.parentID == 0) {
            parents->SetItem(new PyInt(cur.categoryID), name);
            continue;
        }
        std::map<uint32, PyDict*>::iterator itr = childMap.find(cur.parentID);
        if (itr == childMap.end()) {
            PyDict* dict = new PyDict();
            children->SetItem(new PyInt(cur.parentID), dict);
            itr = childMap.emplace(cur.parentID, dict).first;
        }
        itr->second->SetItem(new PyInt(cur.categoryID), name);
        descriptions->SetItem(new PyInt(cur.categoryID), new PyWString(cur.description));
    }

    PyTuple* result = new PyTuple(4);
        result->SetItem(0, parents);
        result->SetItem(1, children);
        result->SetItem(2, descriptions);
        result->SetItem(3, new PyDict());
    return result;
}

PyResult PetitionerService::GetUserCatalogCountry(PyCallArgs& call)
{
    // only used to look up billing links
    return new PyString("");
}

PyResult PetitionerService::MayPetition(PyCallArgs& call, PyInt* categoryID, PyRep* oocCharID)
{
    // >= 0 ok.  negative values are the client's CannotPostPetitionN messages
    uint32 charID = GetPetitionerID(call.client, oocCharID);
    if (charID == 0)
        return new PyInt(-1);
    PetitionCategory category;
    if (!PetitionDB::GetCategory(categoryID->value(), category))
        return new PyInt(-4);
    if (PetitionDB::CountOpen(charID) >= s_maxOpenPetitions)
        return new PyInt(-4);
    return new PyInt(1);
}

PyResult PetitionerService::GetCategoryProperties(PyCallArgs& call, PyRep* categoryID)
{
    // category properties need localization message ids we cannot create.  no properties
    return new PyList();
}

PyResult PetitionerService::PropertyPopulationInfo(PyCallArgs& call, PyRep* inputInfo, PyRep* oocCharID)
{
    return new PyList();
}

PyResult PetitionerService::GetClientPickerInfo(PyCallArgs& call, PyRep* filter, PyRep* elementName)
{
    return new PyList();
}

PyResult PetitionerService::CreatePetition(PyCallArgs& call, PyRep* subject, PyRep* petition, PyInt* categoryID, PyRep* retval,
                                           PyRep* oocCharID, std::optional<PyRep*> chatLog, std::optional<PyRep*> combatLog,
                                           std::optional<PyRep*> properties)
{
    /*  CreatePetition(subject, petition, categoryID, retval, OocCharacterID, combatLog=, chatLog=)
     *  CreatePetition(subject, petition, categoryID, None, OocCharacterID, chatLog, combatLog, propertyList)
     */
    uint32 charID = GetPetitionerID(call.client, oocCharID);
    if (charID == 0)
        return PyStatic.NewFalse();

    PetitionCategory category;
    if (!PetitionDB::GetCategory(categoryID->value(), category))
        return PyStatic.NewFalse();
    if (PetitionDB::CountOpen(charID) >= s_maxOpenPetitions)
        return PyStatic.NewFalse();

    std::string chat, combat;
    if (chatLog.has_value())
        chat = PyRep::StringContent(chatLog.value());
    else if (call.byname.find("chatLog") != call.byname.end())
        chat = PyRep::StringContent(call.byname.find("chatLog")->second);
    if (combatLog.has_value())
        combat = PyRep::StringContent(combatLog.value());
    else if (call.byname.find("combatLog") != call.byname.end())
        combat = PyRep::StringContent(call.byname.find("combatLog")->second);

    std::string subj(PyRep::StringContent(subject)), text(PyRep::StringContent(petition));
    if (subj.size() > 200)
        subj.resize(200);
    if (subj.empty() or text.empty())
        return PyStatic.NewFalse();

    uint32 petitionID = PetitionDB::Create(charID, call.client->GetUserID(), category.categoryID, category.queueID, subj, text,
                                           Truncate(chat, s_maxLogSize), Truncate(combat, s_maxLogSize));
    if (petitionID == 0)
        return PyStatic.NewFalse();

    PetitionDB::AddLog(petitionID, charID, PetitionDB::GetAccountName(call.client->GetUserID()), Petition::Event::Created);
    _log(SERVICE__MESSAGE, "Petition %u filed by %u in category %u (queue %u)", petitionID, charID, category.categoryID, category.queueID);

    PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, new PyInt(petitionID));
    NotifyPetitionees("OnNewPetition", tuple);

    return new PyInt(petitionID);
}

PyResult PetitionerService::GetMyPetitionsEx(PyCallArgs& call)
{
    std::vector<PetitionRow> rows;
    if (call.client->GetCharacterID() > 0)
        PetitionDB::GetByPetitioner(call.client->GetCharacterID(), rows);
    return PetitionsToList(rows);
}

PyResult PetitionerService::GetPetitionMessages(PyCallArgs& call, PyInt* petitionID)
{
    // rows .senderID, .text, .sentDate, .messageID, .comment.  GM comments are hidden from the petitioner
    PetitionRow row;
    GetPetitionFor(call.client, petitionID->value(), row);
    std::vector<PetitionMessage> list;
    PetitionDB::GetMessages(row.petitionID, IsPetitionee(call.client), list);
    PyList* result = new PyList();
    for (auto& cur : list)
        result->AddItem(MessageToKeyVal(cur));
    return result;
}

PyResult PetitionerService::GetUnreadMessages(PyCallArgs& call)
{
    // rows .petitionID, .text, .messageID:  GM answers the petitioner has not seen
    PyList* result = new PyList();
    if (call.client->GetCharacterID() == 0)
        return result;
    std::vector<PetitionMessage> list;
    PetitionDB::GetUnread(call.client->GetCharacterID(), list);
    for (auto& cur : list)
        result->AddItem(MessageToKeyVal(cur));
    return result;
}

PyResult PetitionerService::MarkAsRead(PyCallArgs& call, PyInt* messageID)
{
    if (call.client->GetCharacterID() > 0)
        PetitionDB::MarkRead(messageID->value(), call.client->GetCharacterID());
    return PyStatic.NewNone();
}

PyResult PetitionerService::PetitionerChat(PyCallArgs& call, PyInt* petitionID, PyRep* message)
{
    PetitionRow row;
    GetPetitionFor(call.client, petitionID->value(), row, false);
    if (row.closed or row.deleted)
        throw UserError("PetitionClosed");

    std::string text(PyRep::StringContent(message));
    if (text.empty())
        return PyStatic.NewNone();

    uint32 charID = call.client->GetCharacterID();
    uint32 messageID = PetitionDB::AddMessage(row.petitionID, charID, text, false, true);
    if (messageID == 0)
        return PyStatic.NewNone();
    PetitionDB::SetUpdated(row.petitionID, false);  // "Updated" in the GM's lists
    PetitionDB::Touch(row.petitionID);
    Log(call.client, row.petitionID, Petition::Event::PetitionerMsg);

    // the claiming GM, if online
    PyTuple* tuple = new PyTuple(4);
        tuple->SetItem(0, new PyInt(row.petitionID));
        tuple->SetItem(1, IntOrNone(charID));
        tuple->SetItem(2, new PyWString(text));
        tuple->SetItem(3, new PyInt(messageID));
    NotifyCharacter(row.claimedBy, "OnPetitionMessage", tuple);
    return PyStatic.NewNone();
}

PyResult PetitionerService::CancelPetition(PyCallArgs& call, PyInt* petitionID)
{
    PetitionRow row;
    GetPetitionFor(call.client, petitionID->value(), row, false);
    if (row.closed or row.deleted)
        return PyStatic.NewNone();
    if (row.claimedBy > 0)
        throw CustomError("A GM is already working on this petition, so it can no longer be cancelled.");

    PetitionDB::Close(row.petitionID);
    Log(call.client, row.petitionID, Petition::Event::Cancelled);
    return PyStatic.NewNone();
}

PyResult PetitionerService::DeletePetition(PyCallArgs& call, PyInt* petitionID)
{
    PetitionRow row;
    GetPetitionFor(call.client, petitionID->value(), row, false);
    if (row.deleted)
        return PyStatic.NewNone();
    if (!row.closed)
        throw CustomError("Only closed petitions can be deleted.");

    PetitionDB::Delete(row.petitionID);
    Log(call.client, row.petitionID, Petition::Event::Deleted);
    return PyStatic.NewNone();
}

PyResult PetitionerService::AddPetitionRating(PyCallArgs& call, PyInt* petitionID, PyInt* responseTime, PyInt* helpfulness,
                                              PyInt* attitude, PyRep* comment, std::optional<PyRep*> ratingTime)
{
    PetitionRow row;
    GetPetitionFor(call.client, petitionID->value(), row, false);
    if (!row.closed or (row.claimedBy == 0))
        throw CustomError("This petition cannot be rated.");
    if ((row.closedDate > 0) and (row.closedDate + s_ratingWindow < (int64)GetFileTimeNow()))
        throw CustomError("Petitions can only be rated within a week of being closed.");

    PetitionDB::SetRating(row.petitionID, ClampRating(responseTime->value()), ClampRating(helpfulness->value()),
                          ClampRating(attitude->value()), PyRep::StringContent(comment), (int64)GetFileTimeNow());
    Log(call.client, row.petitionID, Petition::Event::Rated);
    return PyStatic.NewNone();
}

PyResult PetitionerService::UpdatePetitionRating(PyCallArgs& call, PyInt* petitionID, PyInt* responseTime, PyInt* helpfulness,
                                                 PyInt* attitude, PyRep* comment)
{
    PetitionRow row;
    GetPetitionFor(call.client, petitionID->value(), row, false);
    if (!row.closed or (row.claimedBy == 0))
        throw CustomError("This petition cannot be rated.");
    // the client offers a change for a week after the first rating
    int64 since = (row.ratingDateTime > 0 ? row.ratingDateTime : row.closedDate);
    if ((since > 0) and (since + s_ratingWindow < (int64)GetFileTimeNow()))
        throw CustomError("Petition ratings can only be changed within a week.");

    PetitionDB::SetRating(row.petitionID, ClampRating(responseTime->value()), ClampRating(helpfulness->value()),
                          ClampRating(attitude->value()), PyRep::StringContent(comment),
                          (row.ratingDateTime > 0 ? row.ratingDateTime : (int64)GetFileTimeNow()));
    Log(call.client, row.petitionID, Petition::Event::Rated);
    return PyStatic.NewNone();
}

/*************************************************************************/
/* petitionee methods                                                    */
/*************************************************************************/
PyResult PetitionerService::GetQueues(PyCallArgs& call)
{
    // rows .queueID (0-5, the client's tab ids), .queueName
    CheckPetitionee(call.client);
    std::map<uint8, PetitionQueue> queues;
    PetitionDB::GetQueues(queues);
    PyList* result = new PyList();
    for (auto& cur : queues) {
        PyDict* dict = new PyDict();
            dict->SetItemString("queueID",      new PyInt(cur.second.queueID));
            dict->SetItemString("queueName",    new PyWString(cur.second.queueName));
            dict->SetItemString("escalatesTo",  (cur.second.escalatesTo < 0 ? PyStatic.NewNone() : new PyInt(cur.second.escalatesTo)));
        result->AddItem(new PyObject("util.KeyVal", dict));
    }
    return result;
}

PyResult PetitionerService::GetPetitionQueue(PyCallArgs& call, PyInt* queueID)
{
    CheckPetitionee(call.client);
    std::vector<PetitionRow> rows;
    if ((queueID->value() >= 0) and (queueID->value() < 256))
        PetitionDB::GetQueue((uint8)queueID->value(), rows);
    return PetitionsToList(rows);
}

PyResult PetitionerService::GetClaimedPetitions(PyCallArgs& call)
{
    CheckPetitionee(call.client);
    std::vector<PetitionRow> rows;
    if (call.client->GetCharacterID() > 0)
        PetitionDB::GetByClaimer(call.client->GetCharacterID(), rows);
    return PetitionsToList(rows);
}

PyResult PetitionerService::ClaimPetition(PyCallArgs& call, PyInt* petitionID)
{
    // > 0 on success (0 makes the client say someone else claimed it)
    CheckPetitionee(call.client);
    uint32 gmID = call.client->GetCharacterID();
    if (gmID == 0)
        throw CustomError("You must be logged in with a character to claim petitions.");
    PetitionRow row;
    if (!PetitionDB::GetPetition(petitionID->value(), row))
        return new PyInt(0);
    if (row.petitionerID == gmID)
        throw CustomError("You cannot claim your own petition.");
    if (!PetitionDB::Claim(row.petitionID, gmID))
        return new PyInt(0);

    Log(call.client, row.petitionID, Petition::Event::Claimed);

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(row.petitionID));
        tuple->SetItem(1, new PyInt(gmID));
    NotifyCharacter(row.petitionerID, "OnAssignPetition", tuple->Clone()->AsTuple());
    NotifyPetitionees("OnAssignPetition", tuple);
    return new PyInt(1);
}

PyResult PetitionerService::UnClaimPetition(PyCallArgs& call, PyInt* petitionID)
{
    CheckPetitionee(call.client);
    PetitionRow row;
    if (!PetitionDB::GetPetition(petitionID->value(), row))
        throw CustomError("That petition does not exist.");
    if ((row.claimedBy == 0) or row.closed)
        return PyStatic.NewNone();

    PetitionDB::Unclaim(row.petitionID);
    Log(call.client, row.petitionID, Petition::Event::Unclaimed);

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(row.petitionID));
        tuple->SetItem(1, PyStatic.NewNone());
    NotifyCharacter(row.petitionerID, "OnAssignPetition", tuple->Clone()->AsTuple());
    NotifyPetitionees("OnAssignPetition", tuple);
    return PyStatic.NewNone();
}

PyResult PetitionerService::EscalatePetition(PyCallArgs& call, PyInt* petitionID, PyRep* escalatesTo)
{
    // moves the petition to the queue's escalation queue, unclaimed
    CheckPetitionee(call.client);
    PetitionRow row;
    if (!PetitionDB::GetPetition(petitionID->value(), row))
        throw CustomError("That petition does not exist.");
    if (row.closed)
        throw UserError("PetitionClosed");

    std::map<uint8, PetitionQueue> queues;
    PetitionDB::GetQueues(queues);
    int64 target = PyRep::IntegerValue(escalatesTo);
    if ((target < 0) or (target > 255) or (queues.find((uint8)target) == queues.end()))
        throw CustomError("There is no queue %lli to escalate to.", target);

    PetitionDB::SetQueue(row.petitionID, (uint8)target);
    PetitionDB::Unclaim(row.petitionID);
    PetitionDB::SetUpdated(row.petitionID, false);
    Log(call.client, row.petitionID, Petition::Event::Escalated);

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(row.petitionID));
        tuple->SetItem(1, PyStatic.NewNone());
    NotifyCharacter(row.petitionerID, "OnAssignPetition", tuple->Clone()->AsTuple());
    NotifyPetitionees("OnAssignPetition", tuple);
    return PyStatic.NewNone();
}

PyResult PetitionerService::PetitioneeChat(PyCallArgs& call, PyInt* petitionID, PyRep* message, std::optional<PyRep*> comment)
{
    CheckPetitionee(call.client);
    PetitionRow row;
    if (!PetitionDB::GetPetition(petitionID->value(), row))
        throw CustomError("That petition does not exist.");
    if (row.closed or row.deleted)
        throw UserError("PetitionClosed");

    std::string text(PyRep::StringContent(message));
    if (text.empty())
        return PyStatic.NewNone();

    bool isComment = (comment.has_value() and (PyRep::IntegerValue(comment.value()) != 0));
    uint32 gmID = call.client->GetCharacterID();
    // comments are never shown to the petitioner, so they never count as unread
    uint32 messageID = PetitionDB::AddMessage(row.petitionID, gmID, text, isComment, isComment);
    if (messageID == 0)
        return PyStatic.NewNone();
    Log(call.client, row.petitionID, (isComment ? Petition::Event::GMComment : Petition::Event::GMMessage));
    if (isComment)
        return PyStatic.NewNone();

    PetitionDB::SetUpdated(row.petitionID, true);   // "Waiting" on the petitioner
    PetitionDB::Touch(row.petitionID);

    // the petitioner, if online.  otherwise GetUnreadMessages brings it up at the next login
    PyTuple* tuple = new PyTuple(4);
        tuple->SetItem(0, new PyInt(row.petitionID));
        tuple->SetItem(1, IntOrNone(gmID));
        tuple->SetItem(2, new PyWString(text));
        tuple->SetItem(3, new PyInt(messageID));
    NotifyCharacter(row.petitionerID, "OnPetitionMessage", tuple);
    return PyStatic.NewNone();
}

PyResult PetitionerService::ClosePetition(PyCallArgs& call, PyInt* petitionID)
{
    CheckPetitionee(call.client);
    PetitionRow row;
    if (!PetitionDB::GetPetition(petitionID->value(), row))
        throw CustomError("That petition does not exist.");
    if (row.closed)
        return PyStatic.NewNone();

    PetitionDB::Close(row.petitionID);
    Log(call.client, row.petitionID, Petition::Event::Closed);

    PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, new PyInt(row.petitionID));
    NotifyCharacter(row.petitionerID, "OnClosePetition", tuple);
    return PyStatic.NewNone();
}

PyResult PetitionerService::GetLog(PyCallArgs& call, PyInt* petitionID)
{
    // rows .logDateTime, .characterID, .userName, .eventID
    CheckPetitionee(call.client);
    std::vector<PetitionLogRow> list;
    PetitionDB::GetLog(petitionID->value(), list);
    PyList* result = new PyList();
    for (auto& cur : list) {
        PyDict* dict = new PyDict();
            dict->SetItemString("logDateTime",  new PyLong(cur.logDateTime));
            dict->SetItemString("characterID",  IntOrNone(cur.characterID));
            dict->SetItemString("userName",     new PyWString(cur.userName));
            dict->SetItemString("eventID",      new PyInt(cur.eventID));
        result->AddItem(new PyObject("util.KeyVal", dict));
    }
    return result;
}

PyResult PetitionerService::GetEvents(PyCallArgs& call)
{
    // rows .eventID, .eventText
    PyList* result = new PyList();
    for (auto& cur : s_events) {
        PyDict* dict = new PyDict();
            dict->SetItemString("eventID",      new PyInt(cur.first));
            dict->SetItemString("eventText",    new PyString(cur.second));
        result->AddItem(new PyObject("util.KeyVal", dict));
    }
    return result;
}
