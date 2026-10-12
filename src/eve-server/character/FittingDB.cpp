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
*/

#include "eve-server.h"

#include "Client.h"
#include "corporation/CorpMgr.h"
#include "character/FittingDB.h"
#include "inventory/ItemType.h"
#include "mail/MailMgr.h"

PyDict* FittingDB::GetFittings(uint32 ownerID)
{
    PyDict* res = new PyDict();
    DBQueryResult hdr;
    if (!sDatabase.RunQuery(hdr,
        "SELECT fittingID, name, description, shipTypeID FROM chrFittings WHERE ownerID = %u ORDER BY fittingID", ownerID)) {
        codelog(DATABASE__ERROR, "Error in GetFittings query: %s", hdr.error.c_str());
        return res;
    }

    std::map<uint32, PyList*> fitData;
    DBResultRow row;
    while (hdr.GetRow(row)) {
        PyList* list = new PyList();
        fitData[row.GetUInt(0)] = list;
        PyDict* dict = new PyDict();
            dict->SetItemString("fittingID", new PyInt(row.GetUInt(0)));
            dict->SetItemString("ownerID", new PyInt(ownerID));
            dict->SetItemString("name", new PyWString(std::string(row.GetText(1))));
            dict->SetItemString("description", new PyWString(std::string(row.GetText(2))));
            dict->SetItemString("shipTypeID", new PyInt(row.GetUInt(3)));
            dict->SetItemString("fitData", list);
        res->SetItem(new PyInt(row.GetUInt(0)), new PyObject("util.KeyVal", dict));
    }

    if (fitData.empty())
        return res;

    DBQueryResult items;
    if (!sDatabase.RunQuery(items,
        "SELECT i.fittingID, i.typeID, i.flagID, i.quantity FROM chrFittingItems AS i"
        " JOIN chrFittings AS f ON f.fittingID = i.fittingID"
        " WHERE f.ownerID = %u ORDER BY i.fittingID, i.flagID, i.typeID", ownerID)) {
        codelog(DATABASE__ERROR, "Error in GetFittings items query: %s", items.error.c_str());
        return res;
    }
    while (items.GetRow(row)) {
        std::map<uint32, PyList*>::iterator itr = fitData.find(row.GetUInt(0));
        if (itr == fitData.end())
            continue;
        PyTuple* tuple = new PyTuple(3);
            tuple->SetItem(0, new PyInt(row.GetUInt(1)));
            tuple->SetItem(1, new PyInt(row.GetUInt(2)));
            tuple->SetItem(2, new PyInt(row.GetUInt(3)));
        itr->second->AddItem(tuple);
    }

    return res;
}

uint32 FittingDB::CountFittings(uint32 ownerID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM chrFittings WHERE ownerID = %u", ownerID)) {
        codelog(DATABASE__ERROR, "Error in CountFittings query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

bool FittingDB::FittingExists(uint32 ownerID, uint32 fittingID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT 1 FROM chrFittings WHERE fittingID = %u AND ownerID = %u", fittingID, ownerID))
        return false;
    return (res.GetRowCount() > 0);
}

uint32 FittingDB::SaveFitting(uint32 ownerID, const FittingData& data)
{
    std::string name, desc;
    sDatabase.DoEscapeString(name, data.name);
    sDatabase.DoEscapeString(desc, data.description);

    DBerror err;
    uint32 fittingID(0);
    if (!sDatabase.RunQueryLID(err, fittingID,
        "INSERT INTO chrFittings (ownerID, name, description, shipTypeID, savedDate)"
        " VALUES (%u, '%s', '%s', %u, %lli)", ownerID, name.c_str(), desc.c_str(), data.shipTypeID, GetFileTimeNow())) {
        codelog(DATABASE__ERROR, "Error in SaveFitting query: %s", err.c_str());
        return 0;
    }

    // drones share a row per type; duplicates of (flag, type) are summed so the key holds
    std::map<std::pair<uint16, uint32>, uint32> rows;
    for (auto cur : data.items)
        rows[std::make_pair(std::get<1>(cur), std::get<0>(cur))] += std::get<2>(cur);

    std::ostringstream str;
    str << "INSERT INTO chrFittingItems (fittingID, typeID, flagID, quantity) VALUES ";
    bool first(true);
    for (auto cur : rows) {
        if (!first)
            str << ",";
        first = false;
        str << "(" << fittingID << "," << cur.first.second << "," << cur.first.first << "," << cur.second << ")";
    }
    if (!first and !sDatabase.RunQuery(err, str.str().c_str()))
        codelog(DATABASE__ERROR, "Error in SaveFitting items query: %s", err.c_str());

    return fittingID;
}

bool FittingDB::DeleteFitting(uint32 ownerID, uint32 fittingID)
{
    if (!FittingExists(ownerID, fittingID))
        return false;
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM chrFittingItems WHERE fittingID = %u", fittingID);
    sDatabase.RunQuery(err, "DELETE FROM chrFittings WHERE fittingID = %u AND ownerID = %u", fittingID, ownerID);
    return true;
}

bool FittingDB::UpdateNameAndDescription(uint32 ownerID, uint32 fittingID, const std::string& name, const std::string& description)
{
    if (!FittingExists(ownerID, fittingID))
        return false;
    std::string eName, eDesc;
    sDatabase.DoEscapeString(eName, name);
    sDatabase.DoEscapeString(eDesc, description);
    DBerror err;
    return sDatabase.RunQuery(err, "UPDATE chrFittings SET name = '%s', description = '%s' WHERE fittingID = %u AND ownerID = %u",
                eName.c_str(), eDesc.c_str(), fittingID, ownerID);
}

void FittingDB::CheckNameAndDescription(std::string& name, std::string& description)
{
    // trim whitespace like the client's name.strip()
    size_t first(name.find_first_not_of(" \t\r\n"));
    if (first == std::string::npos)
        throw UserError ("FittingNeedsToHaveAName");
    size_t last(name.find_last_not_of(" \t\r\n"));
    name = name.substr(first, last - first + 1);
    if ((name.find("@@") != std::string::npos) or (description.find("@@") != std::string::npos))
        throw UserError ("InvalidFittingInvalidCharacter");
    MailMgr::TruncateUtf8(name, Fitting::maxLengthFittingName);
    MailMgr::TruncateUtf8(description, Fitting::maxLengthFittingDescription);
}

static bool IsFittingFlag(int64 flag)
{
    // eveCfg.IsShipFittingFlag() or the drone bay
    return ((flag >= flagLowSlot0) and (flag <= flagHiSlot7))
        or ((flag >= flagRigSlot0) and (flag <= flagRigSlot7))
        or ((flag >= flagSubSystem0) and (flag <= flagSubSystem7))
        or (flag == flagDroneBay);
}

void FittingDB::Decode(PyRep* rep, FittingData& into)
{
    if ((rep == nullptr) or (!rep->IsObject()) or (!rep->AsObject()->arguments()->IsDict()))
        throw UserError ("ParseFittingFittingDataEmpty");
    PyDict* dict = rep->AsObject()->arguments()->AsDict();

    PyRep* name = dict->GetItemString("name");
    PyRep* desc = dict->GetItemString("description");
    into.name = ((name == nullptr) or name->IsNone()) ? "" : PyRep::StringContent(name);
    into.description = ((desc == nullptr) or desc->IsNone()) ? "" : PyRep::StringContent(desc);
    CheckNameAndDescription(into.name, into.description);

    PyRep* ship = dict->GetItemString("shipTypeID");
    into.shipTypeID = ((ship == nullptr) or ship->IsNone()) ? 0 : PyRep::IntegerValueU32(ship);
    const ItemType* type(into.shipTypeID ? sItemFactory.GetType(into.shipTypeID) : nullptr);
    if (type == nullptr)
        throw UserError ("InvalidFittingDataTypeID")
                .AddFormatValue ("typeID", new PyInt(into.shipTypeID));
    if (type->categoryID() != EVEDB::invCategories::Ship)
        throw UserError ("InvalidFittingDataShipNotShip")
                .AddFormatValue ("typeName", new PyString(type->name()));

    PyRep* fitData = dict->GetItemString("fitData");
    std::vector<PyRep*> entries;
    if (fitData != nullptr) {
        if (fitData->IsList()) {
            for (PyList::const_iterator itr = fitData->AsList()->begin(); itr != fitData->AsList()->end(); ++itr)
                entries.push_back(*itr);
        } else if (fitData->IsTuple()) {
            for (PyTuple::const_iterator itr = fitData->AsTuple()->begin(); itr != fitData->AsTuple()->end(); ++itr)
                entries.push_back(*itr);
        }
    }
    if (entries.empty())
        throw UserError ("ParseFittingFittingDataEmpty");

    for (auto cur : entries) {
        PyRep* typeRep(nullptr);
        PyRep* flagRep(nullptr);
        PyRep* qtyRep(nullptr);
        if (cur->IsTuple() and (cur->AsTuple()->size() == 3)) {
            typeRep = cur->AsTuple()->GetItem(0);
            flagRep = cur->AsTuple()->GetItem(1);
            qtyRep = cur->AsTuple()->GetItem(2);
        } else if (cur->IsList() and (cur->AsList()->size() == 3)) {
            typeRep = cur->AsList()->GetItem(0);
            flagRep = cur->AsList()->GetItem(1);
            qtyRep = cur->AsList()->GetItem(2);
        } else {
            throw UserError ("ParseFittingFittingDataEmpty");
        }

        uint32 typeID(typeRep->IsInt() or typeRep->IsLong() ? PyRep::IntegerValueU32(typeRep) : 0);
        const ItemType* iType(typeID ? sItemFactory.GetType(typeID) : nullptr);
        if (iType == nullptr)
            throw UserError ("InvalidFittingDataTypeID")
                    .AddFormatValue ("typeID", new PyInt(typeID));
        if (!(flagRep->IsInt() or flagRep->IsLong()) or !IsFittingFlag(PyRep::IntegerValue(flagRep)))
            throw UserError ("InvalidFittingDataInvalidFlag")
                    .AddFormatValue ("type", new PyInt(typeID));
        if (!(qtyRep->IsInt() or qtyRep->IsLong()) or (PyRep::IntegerValue(qtyRep) <= 0))
            throw UserError ("InvalidFittingDataInvalidQuantity")
                    .AddFormatValue ("type", new PyInt(typeID));

        into.items.push_back(std::make_tuple(typeID, (uint16)PyRep::IntegerValue(flagRep), (uint32)PyRep::IntegerValue(qtyRep)));
    }
}

void FittingDB::CheckOwner(Client* pClient, uint32 ownerID, bool corp, bool write)
{
    if (corp) {
        if (ownerID != pClient->GetCorporationID())
            throw CustomError ("You can only manage the fittings of your own corporation.");
        if (write and !sCorpMgr.HasRole(pClient->GetCorpRole(), Corp::Role::FittingManager))
            throw UserError ("CantModifyFittingNoRoles");
    } else if (ownerID != pClient->GetCharacterID()) {
        throw CustomError ("You can only manage your own fittings.");
    }
}

PyRep* FittingDB::HandleGetFittings(Client* pClient, uint32 ownerID, bool corp)
{
    CheckOwner(pClient, ownerID, corp, false);
    return GetFittings(ownerID);
}

PyRep* FittingDB::HandleSaveFitting(Client* pClient, uint32 ownerID, bool corp, PyRep* fitting)
{
    CheckOwner(pClient, ownerID, corp, true);
    uint16 max(corp ? Fitting::maxCorpFittings : Fitting::maxCharFittings);
    if (CountFittings(ownerID) >= max)
        throw UserError ("OwnerMaxFittings")
                .AddOwnerName ("owner", ownerID)
                .AddAmount ("maxFittings", (int)max);

    FittingData data = FittingData();
    Decode(fitting, data);
    uint32 fittingID(SaveFitting(ownerID, data));
    if (fittingID == 0)
        throw CustomError ("The fitting could not be saved.");
    return new PyInt(fittingID);
}

PyRep* FittingDB::HandleSaveManyFittings(Client* pClient, uint32 ownerID, bool corp, PyDict* fittings)
{
    CheckOwner(pClient, ownerID, corp, true);
    uint16 max(corp ? Fitting::maxCorpFittings : Fitting::maxCharFittings);
    if (CountFittings(ownerID) + fittings->size() > max)
        throw UserError ("OwnerMaxFittings")
                .AddOwnerName ("owner", ownerID)
                .AddAmount ("maxFittings", (int)max);

    // decode (and validate) them all before saving any
    std::vector<std::pair<int32, FittingData>> toSave;
    for (PyDict::const_iterator itr = fittings->begin(); itr != fittings->end(); ++itr) {
        FittingData data = FittingData();
        Decode(itr->second, data);
        toSave.push_back(std::make_pair((int32)PyRep::IntegerValue(itr->first), data));
    }

    // rows with .tempFittingID and .realFittingID
    PyList* list = new PyList();
    for (auto cur : toSave) {
        uint32 fittingID(SaveFitting(ownerID, cur.second));
        if (fittingID == 0)
            continue;
        PyDict* dict = new PyDict();
            dict->SetItemString("tempFittingID", new PyInt(cur.first));
            dict->SetItemString("realFittingID", new PyInt(fittingID));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
    return list;
}

void FittingDB::HandleDeleteFitting(Client* pClient, uint32 ownerID, bool corp, uint32 fittingID)
{
    CheckOwner(pClient, ownerID, corp, true);
    if (!DeleteFitting(ownerID, fittingID))
        throw UserError ("FittingDoesNotExist");
}

void FittingDB::HandleUpdateNameAndDescription(Client* pClient, uint32 ownerID, bool corp, uint32 fittingID, PyRep* name, PyRep* description)
{
    CheckOwner(pClient, ownerID, corp, true);
    std::string sName((name == nullptr) or name->IsNone() ? "" : PyRep::StringContent(name));
    std::string sDesc((description == nullptr) or description->IsNone() ? "" : PyRep::StringContent(description));
    CheckNameAndDescription(sName, sDesc);
    if (!UpdateNameAndDescription(ownerID, fittingID, sName, sDesc))
        throw UserError ("FittingDoesNotExist");
}
