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
    Author:        Ubiquitatis
    Rewrite:    Allan
*/

/* the distinction between bookmark Mgrs (corp/personal) is made on-the-fly when methods are called in client.
 * most (if not all) will use same code and returns.  so far, i havent seen a difference between them.
 * this may be used to separate corp and personal bm in db on live.
 * not sure if im gonna do that here or not yet.
 */

#include "eve-server.h"

#include "Client.h"

#include "packets/Bookmarks.h"
#include "cache/ObjCacheService.h"
#include "corporation/CorpBookmarkMgr.h"
#include "corporation/CorporationDB.h"

CorpBookmarkMgr::CorpBookmarkMgr() :
    Service("corpBookmarkMgr", eAccessLevel_Corporation)
{
    this->Add("GetBookmarks", &CorpBookmarkMgr::GetBookmarks);
    this->Add("UpdateBookmark", &CorpBookmarkMgr::UpdateBookmark);
    this->Add("UpdatePlayerBookmark", &CorpBookmarkMgr::UpdatePlayerBookmark);
    this->Add("MoveBookmarksToFolder", &CorpBookmarkMgr::MoveBookmarksToFolder);
    this->Add("CreateFolder", &CorpBookmarkMgr::CreateFolder);
    this->Add("UpdateFolder", &CorpBookmarkMgr::UpdateFolder);
    this->Add("CopyBookmarks", &CorpBookmarkMgr::CopyBookmarks);
    this->Add("DeleteFolder", &CorpBookmarkMgr::DeleteFolder);
    this->Add("MoveFoldersToDB", &CorpBookmarkMgr::MoveFoldersToDB);
    this->Add("DeleteBookmarks", &CorpBookmarkMgr::DeleteBookmarks);
}

PyResult CorpBookmarkMgr::GetBookmarks(PyCallArgs& call)
{
    /*
    ObjectCachedMethodID method_id(GetName(), "GetBookmarks");
    if (!m_manager->cache_service->IsCacheLoaded(method_id)) {
        PyTuple *tuple = new PyTuple(2);
            tuple->SetItem(0, m_db.GetBookmarks(call.client->GetCorporationID()));
            tuple->SetItem(1, m_db.GetFolders(call.client->GetCorporationID()));
        PyRep* rep = tuple;

        m_manager->cache_service->GiveCache(method_id, &rep);
    }

    return(m_manager->cache_service->MakeObjectCachedMethodCallResult(method_id));
    */
    // bookmarkSvc:  bookmarkCache.update(bookmarks);  folders.update(folders)  -> both {id: KeyVal}
    PyDict* folders = new PyDict();
    PyRep* list = m_db.GetFolders(call.client->GetCorporationID());
    if ((list != nullptr) and list->IsList()) {
        for (PyList::const_iterator itr = list->AsList()->begin(); itr != list->AsList()->end(); ++itr) {
            PyRep* folderID = (*itr)->AsObject()->arguments()->AsDict()->GetItemString("folderID");
            PyIncRef(folderID);
            PyIncRef(*itr);
            folders->SetItem(folderID, *itr);
        }
    }
    PySafeDecRef(list);

    PyTuple* result = new PyTuple(2);
    result->SetItem(0, m_db.GetBookmarks(call.client->GetCorporationID()));
    result->SetItem(1, folders);
    result->Dump(BOOKMARK__RSP_DUMP, "    ");
    return result;
}

void CorpBookmarkMgr::ReadIDs(PyRep* rep, std::vector<int32>& into)
{
    // a list or tuple of ids, or a set (ObjectEx __builtin__.set:  header (type, (list,)))
    if (rep == nullptr)
        return;
    PyList* list(nullptr);
    if (rep->IsList()) {
        list = rep->AsList();
    } else if (rep->IsTuple()) {
        for (size_t i = 0; i < rep->AsTuple()->size(); ++i)
            if (rep->AsTuple()->GetItem(i)->IsInt() or rep->AsTuple()->GetItem(i)->IsLong())
                into.push_back(PyRep::IntegerValue(rep->AsTuple()->GetItem(i)));
        return;
    } else if (rep->IsObjectEx()) {
        PyRep* header = rep->AsObjectEx()->header();
        if ((header != nullptr) and header->IsTuple() and (header->AsTuple()->size() > 1) and header->AsTuple()->GetItem(1)->IsTuple()
        and (header->AsTuple()->GetItem(1)->AsTuple()->size() > 0) and header->AsTuple()->GetItem(1)->AsTuple()->GetItem(0)->IsList())
            list = header->AsTuple()->GetItem(1)->AsTuple()->GetItem(0)->AsList();
    }
    if (list == nullptr)
        return;
    for (PyList::const_iterator itr = list->begin(); itr != list->end(); ++itr)
        if ((*itr)->IsInt() or (*itr)->IsLong())
            into.push_back(PyRep::IntegerValue(*itr));
}

PyObject* CorpBookmarkMgr::MakeBookmark(const BmData& data)
{
    PyDict* dict = new PyDict();
        dict->SetItemString("bookmarkID", new PyInt(data.bookmarkID));
        dict->SetItemString("ownerID", new PyInt(data.ownerID));
        dict->SetItemString("itemID", new PyInt(data.itemID));
        dict->SetItemString("typeID", new PyInt(data.typeID));
        dict->SetItemString("memo", new PyString(data.memo));
        dict->SetItemString("created", new PyLong(data.created));
        dict->SetItemString("x", new PyFloat(data.point.x));
        dict->SetItemString("y", new PyFloat(data.point.y));
        dict->SetItemString("z", new PyFloat(data.point.z));
        dict->SetItemString("locationID", new PyInt(data.locationID));
        dict->SetItemString("note", new PyString(data.note));
        dict->SetItemString("creatorID", new PyInt(data.creatorID));
        dict->SetItemString("folderID", (data.folderID > 0 ? (PyRep*)new PyInt(data.folderID) : PyStatic.NewNone()));
    return new PyObject("util.KeyVal", dict);
}

PyResult CorpBookmarkMgr::UpdateBookmark(PyCallArgs& call, PyInt* bookmarkID, PyInt* ownerID, PyRep* memo, PyRep* comment, std::optional<PyInt*> folderID) {
    call.Dump(BOOKMARK__CALL_DUMP);

    m_db.UpdateBookmark(bookmarkID->value(), ownerID->value(), folderID.has_value() ? folderID.value()->value() : 0, memo, comment);

    return PyStatic.NewNone();
}

PyResult CorpBookmarkMgr::UpdatePlayerBookmark(PyCallArgs& call, PyInt* bookmarkID, PyInt* ownerID, PyRep* memo, PyRep* comment, std::optional<PyInt*> folderID) {
    call.Dump(BOOKMARK__CALL_DUMP);

    m_db.UpdateBookmark(bookmarkID->value(), ownerID->value(), folderID.has_value() ? folderID.value()->value() : 0, memo, comment);

    return PyStatic.NewNone();
}

PyResult CorpBookmarkMgr::MoveBookmarksToFolder(PyCallArgs& call, PyInt* folderID, std::optional<PyObjectEx*> bookmarkIDs)
{
    // rows = bookmarkMgr.MoveBookmarksToFolder(folderID, bookmarkIDs)
    call.Dump(BOOKMARK__CALL_DUMP);
    //args.Dump(BOOKMARK__CALL_DUMP, "    ");

    if (bookmarkIDs.has_value() == false)
        return PyStatic.NewNone();

    PyList* bmList = bookmarkIDs.value()->header()->AsTuple()->GetItem(1)->AsTuple()->GetItem(0)->AsList();

    std::vector<int32> bmIDs;
    for (size_t i = 0; i < bmList->size(); ++i)
        bmIDs.push_back(bmList->GetItem(i)->AsInt()->value());

    m_db.MoveBookmarkToFolder(folderID->value(), bmIDs);

    return m_db.GetBookmarksInFolder(folderID->value());
}

PyResult CorpBookmarkMgr::UpdateFolder(PyCallArgs& call, PyInt* folderID, PyRep* folderName)
{
    //if bookmarkMgr.UpdateFolder(folderID, folderName):
    call.Dump(BOOKMARK__CALL_DUMP);
    /** @todo sanitize name */
    if (!m_db.UpdateFolder(folderID->value(), PyRep::StringContent(folderName)))
        return PyStatic.NewFalse();

    return PyStatic.NewTrue();
}

PyResult CorpBookmarkMgr::CreateFolder(PyCallArgs& call, PyRep* folderName)
{
    //folder = bookmarkMgr.CreateFolder(folderName)
    call.Dump(BOOKMARK__CALL_DUMP);
    /** @todo sanitize name */
    std::string name = PyRep::StringContent(folderName);

    uint32 ownerID = call.client->GetCorporationID();
    Rsp_CreateFolder result;
        result.ownerID = ownerID;
        result.folderID = m_db.SaveNewFolder(name, ownerID, call.client->GetCharacterID());
        result.folderName = name;
        result.creatorID = call.client->GetCharacterID();

    result.Dump(BOOKMARK__RSP_DUMP, "    ");
    return result.Encode();
}

PyResult CorpBookmarkMgr::CopyBookmarks(PyCallArgs& call, std::optional<PyRep*> bookmarksToCopy, std::optional<PyInt*> folderID)
{
    // newBookmarks, message = corpBookmarkMgr.CopyBookmarks(bookmarksToCopy, folderID):  {bookmarkID: KeyVal}, message tuple or None
    call.Dump(BOOKMARK__CALL_DUMP);

    uint32 corpID = call.client->GetCorporationID();
    uint32 charID = call.client->GetCharacterID();
    PyDict* copies = new PyDict();
    PyTuple* result = new PyTuple(2);
        result->SetItem(0, copies);
        result->SetItem(1, PyStatic.NewNone());
    if (!bookmarksToCopy.has_value())
        return result;

    std::vector<int32> ids;
    ReadIDs(bookmarksToCopy.value(), ids);

    // corp bookmarks are limited (const.maxCorpBookmarkCount)
    uint32 count = m_db.GetBookmarkCount(corpID);
    if (count >= maxCorpBookmarkCount) {
        PyDecRef(result);
        throw UserError("CantCreateCorpBookmarksOverLimit").AddFormatValue("maxLimit", new PyInt(maxCorpBookmarkCount));
    }

    uint32 copied(0);
    for (auto cur : ids) {
        if (count >= maxCorpBookmarkCount)
            break;
        BmData data = BmData();
            data.bookmarkID = cur;
        m_db.GetVoucherData(data);
        // only the caller's own bookmarks (or the corp's)
        if ((data.ownerID != charID) and (data.ownerID != corpID))
            continue;
        // "You cannot trade or copy mission bookmarks."
        if (IsAgent(data.creatorID)) {
            PyDecRef(result);
            throw UserError("CantTradeMissionBookmarks");
        }

        data.ownerID = corpID;
        data.creatorID = charID;
        data.folderID = (folderID.has_value() ? folderID.value()->value() : 0);
        data.created = GetFileTimeNow();
        m_db.SaveNewBookmark(data);
        if (data.bookmarkID == (uint32)cur)
            continue;   // not saved
        copies->SetItem(new PyInt(data.bookmarkID), MakeBookmark(data));
        ++count;
        ++copied;
    }

    if (copied < ids.size()) {
        // ('NotAllBookmarksCopiedNotEnoughRoom', {maxLimit, owner, bookmarksCopied})
        PyDict* args = new PyDict();
            args->SetItemString("maxLimit", new PyInt(maxCorpBookmarkCount));
            args->SetItemString("owner", new PyString(CorporationDB::GetCorpName(corpID)));
            args->SetItemString("bookmarksCopied", new PyInt(copied));
        PyTuple* message = new PyTuple(2);
            message->SetItem(0, new PyString("NotAllBookmarksCopiedNotEnoughRoom"));
            message->SetItem(1, args);
        result->SetItem(1, message);
    }

    return result;
}

PyResult CorpBookmarkMgr::DeleteFolder(PyCallArgs& call, PyInt* folderID, PyRep* unused)
{
    //deleteFolder, bookmarks = self.corpBookmarkMgr.DeleteFolder(folderID, bookmarkIDs)
    call.Dump(BOOKMARK__CALL_DUMP);

    // call db to get list of bmIDs in deleted folder.  return result with this data
    std::vector< int32 > bmIDs;
    bmIDs.clear();
    m_db.GetBookmarkByFolderID(folderID->value(), bmIDs);

    PyList* list = new PyList();
    for (auto cur : bmIDs) {
        PyDict* dict = new PyDict();
        dict->SetItemString("bookmarkID", new PyInt(cur));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }

    m_db.DeleteFolder(folderID->value());

    return list;
}

PyResult CorpBookmarkMgr::MoveFoldersToDB(PyCallArgs& call, PyRep* info)
{
    //rows, folders = self.corpBookmarkMgr.MoveFoldersToDB(info)
    //  info = [(set(bookmarkIDs), label), ...]:  the client's old local corp folders become real ones
    call.Dump(BOOKMARK__CALL_DUMP);

    uint32 corpID = call.client->GetCorporationID();
    PyList* rows = new PyList();
    PyDict* folders = new PyDict();
    if (info->IsList()) {
        for (PyList::const_iterator itr = info->AsList()->begin(); itr != info->AsList()->end(); ++itr) {
            if (!(*itr)->IsTuple() or ((*itr)->AsTuple()->size() < 2))
                continue;
            std::vector<int32> ids, owned;
            ReadIDs((*itr)->AsTuple()->GetItem(0), ids);
            for (auto cur : ids) {
                BmData data = BmData();
                    data.bookmarkID = cur;
                m_db.GetVoucherData(data);
                if (data.ownerID == corpID)
                    owned.push_back(cur);
            }
            if (owned.empty())
                continue;

            std::string name = PyRep::StringContent((*itr)->AsTuple()->GetItem(1));
            uint32 folderID = m_db.SaveNewFolder(name, corpID, call.client->GetCharacterID());
            m_db.MoveBookmarkToFolder(folderID, owned);
            for (auto cur : owned) {
                PyDict* dict = new PyDict();
                    dict->SetItemString("bookmarkID", new PyInt(cur));
                    dict->SetItemString("folderID", new PyInt(folderID));
                rows->AddItem(new PyObject("util.KeyVal", dict));
            }
            PyDict* folder = new PyDict();
                folder->SetItemString("ownerID", new PyInt(corpID));
                folder->SetItemString("folderID", new PyInt(folderID));
                folder->SetItemString("folderName", new PyString(name));
                folder->SetItemString("creatorID", new PyInt(call.client->GetCharacterID()));
            folders->SetItem(new PyInt(folderID), new PyObject("util.KeyVal", folder));
        }
    }

    PyTuple* result = new PyTuple(2);
        result->SetItem(0, rows);
        result->SetItem(1, folders);
    return result;
}

PyResult CorpBookmarkMgr::DeleteBookmarks(PyCallArgs& call, std::optional<PyObjectEx*> bookmarkIDs)
{
    //deletedBookmarks = self.corpBookmarkMgr.DeleteBookmarks(bookmarkIDs)
    call.Dump(BOOKMARK__CALL_DUMP);

    if (bookmarkIDs.has_value() == false)
        return PyStatic.NewNone();

    PyList* bmList = bookmarkIDs.value()->header()->AsTuple()->GetItem(1)->AsTuple()->GetItem(0)->AsList();

    PyList* list = new PyList();
    for (size_t i = 0; i < bmList->size(); ++i) {
        uint32 bmID = bmList->GetItem(i)->AsInt()->value();
        m_db.ChangeOwner(bmID, 1);
        PyDict* dict = new PyDict();
        dict->SetItemString("bookmarkID", new PyInt(bmID));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
    return list;
}
