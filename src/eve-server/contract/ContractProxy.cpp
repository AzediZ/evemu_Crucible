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
    Author:         Captnoord
    Rewrite:        Allan
    Implementation: AlTahir
*/

#include <boost/algorithm/string/replace.hpp>
#include <algorithm>    // Added to prevent std::find from freaking out
#include "eve-server.h"


#include "contract/ContractProxy.h"
#include "station/Station.h"
#include "inventory/Inventory.h"
#include "system/SolarSystem.h"

#include "contract/ContractUtils.h"
#include "account/AccountService.h"
#include "manufacturing/Blueprint.h"
#include "station/StationDataMgr.h"

ContractProxy::ContractProxy () :
    Service("contractProxy")
{
    this->Add("GetContract", &ContractProxy::GetContract);
    this->Add("CreateContract", static_cast <PyResult(ContractProxy::*)(PyCallArgs &,PyInt*, PyBool*, std::optional <PyNone*>, PyInt*, PyInt*, PyInt*, std::optional<PyNone*>, PyInt*, PyInt*, PyInt*, PyString*, PyString*)> (&ContractProxy::CreateContract));
    this->Add("CreateContract", static_cast <PyResult(ContractProxy::*)(PyCallArgs &,PyInt*, PyBool*, std::optional <PyInt*>, PyInt*, PyInt*, PyInt*, std::optional<PyNone*>, PyInt*, PyInt*, PyInt*, PyString*, PyString*)> (&ContractProxy::CreateContract));
    this->Add("CreateContract", static_cast <PyResult(ContractProxy::*)(PyCallArgs &,PyInt*, PyBool*, std::optional <PyInt*>, PyInt*, PyInt*, PyInt*, std::optional<PyNone*>, PyInt*, PyInt*, PyInt*, PyWString*, PyString*)> (&ContractProxy::CreateContract));
    this->Add("CreateContract", static_cast <PyResult(ContractProxy::*)(PyCallArgs &,PyInt*, PyBool*, std::optional <PyNone*>, PyInt*, PyInt*, PyInt*, std::optional<PyInt*>, PyInt*, PyInt*, PyInt*, PyString*, PyString*)> (&ContractProxy::CreateContract));
    this->Add("CreateContract", static_cast <PyResult(ContractProxy::*)(PyCallArgs &,PyInt*, PyBool*, std::optional <PyNone*>, PyInt*, PyInt*, PyInt*, std::optional <PyNone*>, PyInt*, PyInt*, PyInt*, PyWString*,PyString*)> (&ContractProxy::CreateContract));
    this->Add("CreateContract", static_cast <PyResult(ContractProxy::*)(PyCallArgs&, PyInt*, PyInt*, std::optional <PyInt*>, PyInt*, PyInt*, PyInt*, std::optional<PyInt*>, PyInt*, PyInt*, PyInt*, PyWString*, PyWString*)> (&ContractProxy::CreateContract));
    this->Add("DeleteContract", &ContractProxy::DeleteContract);
    this->Add("AcceptContract", &ContractProxy::AcceptContract);
    this->Add("CompleteContract", &ContractProxy::CompleteContract);
    this->Add("GetLoginInfo", &ContractProxy::GetLoginInfo);
    this->Add("SearchContracts", &ContractProxy::SearchContracts);
    this->Add("NumOutstandingContracts", &ContractProxy::NumOutstandingContracts);
    this->Add("CollectMyPageInfo", &ContractProxy::CollectMyPageInfo);
    this->Add("GetItemsInStation", &ContractProxy::GetItemsInStation);
    this->Add("GetContractListForOwner", &ContractProxy::GetContractListForOwner);
    this->Add("GetMyExpiredContractList", &ContractProxy::GetMyExpiredContractList);
    /*
     *
            ret = self.contractSvc.CompleteContract(contractID, const.conStatusFinished)
            return self.contractSvc.CompleteContract(contractID, const.conStatusRejected)
            return self.contractSvc.PlaceBid(contractID, bid, forCorp)
            return self.contractSvc.FinishAuction(contractID, isIssuer)
            ret = self.contractSvc.SplitStack(stationID, itemID, qty, forCorp, flag)
            return self.contractSvc.GetItemsInContainer(stationID, containerID, forCorp, flag)
            return self.contractSvc.GetItemsInStation(stationID, forCorp)
            return self.contractSvc.DeleteNotification(contractID, forCorp)
            info = self.contractSvc.GetCourierContractFromItemID(itemID)
     */
}

PyResult ContractProxy::SearchContracts(PyCallArgs &call) {
    // We will not proceed, if contractType is not specified
    if (!call.byname.find("contractType")->second->IsNone()) {
        int contractType = call.byname.find("contractType")->second->AsInt()->value();

        /**
         * We're using sort of query constructor here - if request have certain value specified, we add it as another AND block.
         * For now, we will only filter by contract type, item type, item category, min/max price, min/max reward, location/end location, issuer and availability
         */
        std::string query = "SELECT cC.contractId FROM ctrContracts cC "
                            "JOIN ctrItems cI on cC.contractId = cI.contractId "
                            "JOIN entity e on cI.itemID = e.itemID "
                            "JOIN invTypes iT on iT.typeID = e.typeID "
                            "JOIN invGroups iG on iG.groupID = iT.groupID "
                            "JOIN invCategories iC on iG.categoryID = iC.categoryID "
                            "WHERE cC.contractType IN " + std::string(contractType == 10 ? "(1,2)" : "(" + std::to_string(contractType) + ")");
                                                         // Type 10 is "All" and "Exclude WTB", for some reason. We'll assume it's "All", lol

        if (!call.byname.find("itemTypes")->second->IsNone()) {
            PyList* itemTypes = call.byname.find("itemTypes")->second->AsObjectEx()->header()->AsTuple()->GetItem(1)->AsTuple()->GetItem(0)->AsList();
            std::string types;
            for (auto index = 0; index < itemTypes->size(); index++) {
                types.append(std::to_string(itemTypes->GetItem(index)->AsInt()->value()));
                if (index != itemTypes->size() - 1) {
                    types.append(",");
                }
            }

            if (!types.empty()) {
                query.append(" AND e.typeID IN (" + types + ")");
            }
        }

        if (!call.byname.find("itemGroupID")->second->IsNone()) {
            query.append(" AND iG.groupID = " + std::to_string(call.byname.find("itemGroupID")->second->AsInt()->value()));
        }
        if (!call.byname.find("itemCategoryID")->second->IsNone()) {
            query.append(" AND iC.categoryID = " + std::to_string(call.byname.find("itemCategoryID")->second->AsInt()->value()));
        }
        if (!call.byname.find("minPrice")->second->IsNone()) {
            query.append(" AND cC.price >= " + std::to_string(call.byname.find("minPrice")->second->AsInt()->value()));
        }
        if (!call.byname.find("maxPrice")->second->IsNone()) {
            query.append(" AND cC.price <= " + std::to_string(call.byname.find("maxPrice")->second->AsInt()->value()));
        }
        if (!call.byname.find("minReward")->second->IsNone()) {
            query.append(" AND cC.reward >= " + std::to_string(call.byname.find("minReward")->second->AsInt()->value()));
        }
        if (!call.byname.find("maxReward")->second->IsNone()) {
            query.append(" AND cC.reward <= " + std::to_string(call.byname.find("maxReward")->second->AsInt()->value()));
        }
        if (!call.byname.find("availability")->second->IsNone()) {
            int availability = call.byname.find("availability")->second->AsInt()->value();
            if (availability == 0) {
                // Public contracts
                query.append(" AND cC.isPrivate = 0");
            } else if (availability == 1) {
                // Private contracts, assigned to character
                query.append(" AND cC.isPrivate = 1 AND cC.assigneeId = " + std::to_string(call.client->GetCharacterID()));
            } else if(availability == 2) {
                // Private contracts, assigned to corp
                query.append(" AND cC.isPrivate = 1 AND cC.assigneeId = " + std::to_string(call.client->GetCorporationID()));
            }
        }
        // According to what i had during testing, locationID can only be system, constellation or region. Given that we only store system and region ID, we use OR clause for these
        if (!call.byname.find("locationID")->second->IsNone()) {
            int locationId = call.byname.find("locationID")->second->AsInt()->value();
            if (IsSolarSystemID(locationId)) {
                // Solar system range
                query.append(" AND cC.startSolarSystemID = " + std::to_string(locationId));
            } else if (IsRegionID(locationId)) {
                // Region range
                query.append(" AND cC.startRegionID = " + std::to_string(locationId));
            }
        }
        // Same applies to endLocationID - it uses the same search::QuickQuery() call to get it
        if (!call.byname.find("endLocationID")->second->IsNone()) {
            int locationId = call.byname.find("endLocationID")->second->AsInt()->value();
            if (IsSolarSystemID(locationId)) {
                // Solar system range
                query.append(" AND cC.endSolarSystemID = " + std::to_string(locationId));
            } else if (IsRegionID(locationId)) {
                // Region range
                query.append(" AND cC.endRegionID = " + std::to_string(locationId));
            }
        }
        // Once again, issuer can be either a character or a corporation. We use separate filters depending on value
        if (!call.byname.find("issuerID")->second->IsNone()) {
            int issuerId = call.byname.find("issuerID")->second->AsInt()->value();
            if (IsCorp(issuerId)) {
                // Corporation case
                query.append(" AND cC.issuerCorpID = " + std::to_string(issuerId) + " AND cC.forCorp = true");
            } else {
                query.append(" AND cC.issuerID = " + std::to_string(issuerId) + " AND cC.forCorp = false");
            }
        }
        query.append(" AND cC.status = 0");
        /**
         * Once query is constructed, we execute it and collect contractID's. Since we can have duplicate values, we first
         * collect it to std::vector, and then we pass it to GetContractEntries function
         */

        DBQueryResult contractRes;
        if (!sDatabase.RunQuery(contractRes, query.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in query: %s", contractRes.error.c_str());
            return nullptr;
        }
        std::vector<int> contractIDs;
        DBResultRow contractRow;
        while (contractRes.GetRow(contractRow)) {
            int contractId = contractRow.GetInt(0);

            // To make sure we don't add duplicates, we check whether we already have said contractID in vector already.
            if (std::find(contractIDs.begin(), contractIDs.end(), contractId) == contractIDs.end()) {
                contractIDs.push_back(contractId);
            }
        }

        PyDict* response = new PyDict;
        PyList* contracts = ContractUtils::GetContractEntries(contractIDs);
        response->SetItemString("contracts", contracts ? contracts : new PyList);
        response->SetItemString("numFound", contracts ? new PyInt(contracts->size()) : new PyInt(0));
        response->SetItemString("searchTime", new PyInt(153));  // Since search time is of no relevance to the client, we simply hard-code it
        response->SetItemString("maxResults", new PyInt(1000)); // Same here - we do not limit the list of contracts queried, so we leave this value hard-coded

        return new PyObject("util.KeyVal", response);
    } else {
        codelog(SERVICE__ERROR, "%s: ContractType was not specified. Aborting search", GetName());
        return nullptr;
    }
}

PyResult ContractProxy::CreateContract(PyCallArgs &call,
    PyInt* contractType, PyBool* isPrivate, std::optional <PyNone*> assigneeID, PyInt* expireTime, PyInt* duration, PyInt* startStationID, std::optional<PyNone*> endStationID,
    PyInt* price, PyInt* reward, PyInt* collateral, PyString* title, PyString* description) {
    return CreateContract(call, contractType, new PyInt(isPrivate->value()), std::nullopt, expireTime, duration, startStationID, std::nullopt, price, reward, collateral, new PyWString(title->content()), new PyWString(description->content()));
}

PyResult ContractProxy::CreateContract(PyCallArgs &call,
    PyInt* contractType, PyBool* isPrivate, std::optional <PyInt*> assigneeID, PyInt* expireTime, PyInt* duration, PyInt* startStationID, std::optional<PyNone*> endStationID,
    PyInt* price, PyInt* reward, PyInt* collateral, PyString* title, PyString* description) {
    return CreateContract(call, contractType, new PyInt(isPrivate->value()), assigneeID, expireTime, duration, startStationID, std::nullopt, price, reward, collateral, new PyWString(title->content()), new PyWString(description->content()));
}

PyResult ContractProxy::CreateContract(PyCallArgs &call,
    PyInt* contractType, PyBool* isPrivate, std::optional <PyInt*> assigneeID, PyInt* expireTime, PyInt* duration, PyInt* startStationID, std::optional<PyNone*> endStationID,
    PyInt* price, PyInt* reward, PyInt* collateral, PyWString* title, PyString* description) {
    return CreateContract(call, contractType, new PyInt(isPrivate->value()), assigneeID, expireTime, duration, startStationID, std::nullopt, price, reward, collateral, title, new PyWString(description->content()));
}

PyResult ContractProxy::CreateContract(PyCallArgs &call,
    PyInt* contractType, PyBool* isPrivate, std::optional <PyNone*> assigneeID, PyInt* expireTime, PyInt* duration, PyInt* startStationID, std::optional<PyInt*> endStationID,
    PyInt* price, PyInt* reward, PyInt* collateral, PyString* title, PyString* description) {
    return CreateContract(call, contractType, new PyInt(isPrivate->value()), std::nullopt, expireTime, duration, startStationID, endStationID, price, reward, collateral, new PyWString(title->content()), new PyWString(description->content()));
}

PyResult ContractProxy::CreateContract(PyCallArgs &call,
    PyInt* contractType, PyBool* isPrivate, std::optional <PyNone*> assigneeID, PyInt* expireTime, PyInt* duration, PyInt* startStationID, std::optional<PyNone*> endStationID,
    PyInt* price, PyInt* reward, PyInt* collateral, PyWString* title, PyString* description) {
    return CreateContract(call, contractType, new PyInt(isPrivate->value()), std::nullopt, expireTime, duration, startStationID, std::nullopt, price, reward, collateral, title, new PyWString(description->content()));
}


namespace {
    // keyword argument by name, or nullptr when missing
    PyRep* GetKeyword(PyCallArgs& call, const char* name) {
        std::map<std::string, PyRep*>::iterator itr = call.byname.find(name);
        if (itr == call.byname.end())
            return nullptr;
        return itr->second;
    }
}

PyResult ContractProxy::CreateContract(PyCallArgs &call,
    PyInt* contractType, PyInt* isPrivate, std::optional <PyInt*> assigneeID, PyInt* expireTime, PyInt* duration, PyInt* startStationID, std::optional<PyInt*> endStationID,
    PyInt* price, PyInt* reward, PyInt* collateral, PyWString* title, PyWString* description) {
    int startStationDivision(flagHangar), startSystemId(0), startRegionId(0), endSystemId(0), endRegionId(0);
    bool forCorp(false);
    uint32 charID(call.client->GetCharacterID());

    /**
     * Since named args (byname) aren't included in packet, we process them separately.
     */
    PyRep* rep = GetKeyword(call, "flag");
    if ((rep != nullptr) and rep->IsInt())
        startStationDivision = rep->AsInt()->value();
    rep = GetKeyword(call, "forCorp");
    if (rep != nullptr) {
        if (rep->IsBool()) {
            forCorp = rep->AsBool()->value();
        } else if (rep->IsInt()) {
            forCorp = (rep->AsInt()->value() != 0);
        }
    }

    // corp contracts are not supported yet (same as corp market orders)
    if (forCorp)
        throw CustomError("Corporation contracts are not available at this time.");

    if ((contractType->value() != Contract::Type::ItemExchange)
    and (contractType->value() != Contract::Type::Auction)
    and (contractType->value() != Contract::Type::Courier))
        throw CustomError("Invalid contract type.");
    if ((price->value() < 0) or (reward->value() < 0) or (collateral->value() < 0))
        throw CustomError("Contract prices, rewards and collateral cannot be negative.");
    if (expireTime->value() < 1)
        throw CustomError("Invalid contract expiration time.");
    if (duration->value() < 0)
        throw CustomError("Invalid contract duration.");

    uint32 assignee(assigneeID.has_value() ? assigneeID.value()->value() : 0);
    if (isPrivate->value() and (assignee == 0))
        throw CustomError("A private contract needs an assignee.");

    if (sDataMgr.IsStation(startStationID->value())) {
        startSystemId = sDataMgr.GetStationSystem(startStationID->value());
        startRegionId = sDataMgr.GetStationRegion(startStationID->value());
    } else {
        codelog(SERVICE__ERROR, "Specified start Station ID has no Station counterparts in DB");
        throw CustomError("Invalid start station.");
    }
    uint32 endStation(0);
    if (endStationID.has_value() and (endStationID.value()->value() != 0)) {
        endStation = endStationID.value()->value();
        if (sDataMgr.IsStation(endStation)) {
            endSystemId = sDataMgr.GetStationSystem(endStation);
            endRegionId = sDataMgr.GetStationRegion(endStation);
        } else {
            codelog(SERVICE__ERROR, "Specified end Station ID has no Station counterparts in DB");
            throw CustomError("Invalid destination station.");
        }
    } else {
        endSystemId = startSystemId;
        endRegionId = 0;
    }
    if ((contractType->value() == Contract::Type::Courier) and (endStation == 0))
        throw CustomError("A courier contract needs a destination station.");

    /**
     * Validate every item before anything is created.  traded items must be the issuer's own,
     *  in the item hangar of the start station, and not the active ship.
     */
    struct TradedItem {
        InventoryItemRef iRef;
        int32 quantity;
    };
    std::vector<TradedItem> tradedItems;
    std::set<uint32> seenIDs;
    rep = GetKeyword(call, "itemList");
    if ((rep != nullptr) and rep->IsList()) {
        PyList* list = rep->AsList();
        if (!list->empty()) {
            // make sure this char's hangar in the start station is loaded
            call.client->GetStationHangar(startStationID->value());
        }
        for (size_t index = 0; index < list->size(); ++index) {
            PyRep* entry = list->GetItem(index);
            PyRep* idRep(nullptr);
            PyRep* qtyRep(nullptr);
            if (entry->IsList() and (entry->AsList()->size() >= 2)) {
                idRep = entry->AsList()->GetItem(0);
                qtyRep = entry->AsList()->GetItem(1);
            } else if (entry->IsTuple() and (entry->AsTuple()->size() >= 2)) {
                idRep = entry->AsTuple()->GetItem(0);
                qtyRep = entry->AsTuple()->GetItem(1);
            }
            if ((idRep == nullptr) or (qtyRep == nullptr) or !idRep->IsInt() or !qtyRep->IsInt())
                throw CustomError("Invalid item list.");

            uint32 itemID(idRep->AsInt()->value());
            int32 qty(qtyRep->AsInt()->value());
            if (!seenIDs.insert(itemID).second)
                throw CustomError("The same item cannot be added to a contract twice.");

            InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
            if (iRef.get() == nullptr)
                throw CustomError("One of the items for this contract could not be found.");
            if (itemID == call.client->GetShipID())
                throw UserError("ConCannotTradeCurrentShip");
            if (iRef->ownerID() != charID) {
                _log(SERVICE__WARNING, "CreateContract - %s(%u) tried to contract %s(%u) owned by %u.", \
                        call.client->GetName(), charID, iRef->name(), itemID, iRef->ownerID());
                throw CustomError("You do not own %s.", iRef->name());
            }
            if ((iRef->locationID() != (uint32)startStationID->value()) or (iRef->flag() != flagHangar))
                throw CustomError("%s must be in your item hangar in %s.", iRef->name(), stDataMgr.GetStationName(startStationID->value()).c_str());
            if ((qty < 1) or (qty > iRef->quantity()))
                throw CustomError("Invalid quantity of %s.", iRef->name());
            if (iRef->isSingleton() and (qty != iRef->quantity()))
                throw CustomError("Invalid quantity of %s.", iRef->name());

            tradedItems.push_back({iRef, qty});
        }
    }

    std::map<int32, int32> requestedItems;
    rep = GetKeyword(call, "requestItemTypeList");
    if ((rep != nullptr) and rep->IsList()) {
        PyList* list = rep->AsList();
        for (size_t index = 0; index < list->size(); ++index) {
            PyRep* entry = list->GetItem(index);
            PyRep* typeRep(nullptr);
            PyRep* qtyRep(nullptr);
            if (entry->IsList() and (entry->AsList()->size() >= 2)) {
                typeRep = entry->AsList()->GetItem(0);
                qtyRep = entry->AsList()->GetItem(1);
            } else if (entry->IsTuple() and (entry->AsTuple()->size() >= 2)) {
                typeRep = entry->AsTuple()->GetItem(0);
                qtyRep = entry->AsTuple()->GetItem(1);
            }
            if ((typeRep == nullptr) or (qtyRep == nullptr) or !typeRep->IsInt() or !qtyRep->IsInt())
                throw CustomError("Invalid requested item list.");
            if ((qtyRep->AsInt()->value() < 1) or (sItemFactory.GetType(typeRep->AsInt()->value()) == nullptr))
                throw CustomError("Invalid requested item.");
            requestedItems[typeRep->AsInt()->value()] += qtyRep->AsInt()->value();
        }
    }
    if (!requestedItems.empty() and (contractType->value() != Contract::Type::ItemExchange))
        throw CustomError("Only item exchange contracts can ask for items.");

    if (tradedItems.empty() and requestedItems.empty()) {
        codelog(SERVICE__ERROR, "No traded or requested items was specified. Aborting");
        throw CustomError("A contract needs at least one item.");
    }

    std::string titleStr, descStr;
    sDatabase.DoEscapeString(titleStr, title->content());
    sDatabase.DoEscapeString(descStr, description->content());

    /**
     * Then, we go for actual entries creation.
     * Contract entry
     */
    uint32 contractId = 0;
    DBerror err;
    if (!sDatabase.RunQueryLID(err, contractId,
        "INSERT INTO ctrContracts "
        "(contractType, issuerID, issuerCorpID, forCorp, isPrivate, assigneeID, "
        "dateIssued, dateExpired, expireTimeInMinutes, duration, numDays, "
        "startStationID, startSolarSystemID, startRegionID, endStationID, endSolarSystemID, endRegionID, "
        "price, reward, collateral, title, description, issuerAllianceID, startStationDivision, rewardEscrowed) "
        "VALUES "
        "(%u, %u, %u, %u, %u, %u, "
        "%lli, %lli, %u, %u, %u, "
        "%u, %u, %u, %u, %u, %u,"
        "%i, %i, %i, '%s', '%s', %u, %u, %u)",
        contractType->value(), charID, call.client->GetCorporationID(), forCorp, isPrivate->value()?1:0, assignee,
        int64(GetFileTimeNow()), int64(GetRelativeFileTime(0, 0, expireTime->value())), expireTime->value(), duration->value(),
        (contractType->value() == Contract::Type::Courier ? duration->value() : expireTime->value() / 1440),
        startStationID->value(), startSystemId, startRegionId, endStation, endSystemId, endRegionId,
        price->value(), reward->value(), collateral->value(), titleStr.c_str(), descStr.c_str(), call.client->GetAllianceID(), startStationDivision,
        (reward->value() > 0 ? 1 : 0)))
    {
        codelog(DATABASE__ERROR, "Failed to insert new entity: %s", err.c_str());
        throw CustomError("Failed to create the contract.");
    }

    // the reward is held by the system from now on, so the issuer can't spend it before the contract ends
    if (reward->value() > 0) {
        try {
            AccountService::TransferFunds(charID, ownerSystem, reward->value(), "Contract reward", Journal::EntryType::ContractRewardAdded, contractId);
        } catch (...) {
            sDatabase.RunQuery(err, "DELETE FROM ctrContracts WHERE contractId = %u", contractId);
            throw;
        }
    }

    /**
     * Then, we insert the items.  the contracted part of a stack is split off, and the items are held by the system
     */
    std::string itemsToInsert;
    float totalVolume = 0.00;
    for (auto& cur : tradedItems) {
        InventoryItemRef iRef(cur.iRef);
        if (cur.quantity < iRef->quantity()) {
            iRef = iRef->Split(cur.quantity, true);
            if (iRef.get() == nullptr) {
                _log(SERVICE__ERROR, "CreateContract - Failed to split %i from %s(%u).", cur.quantity, cur.iRef->name(), cur.iRef->itemID());
                continue;
            }
        }

        int pLevel(0), mLevel(0), isCopy(0), runs(0);
        if (iRef->categoryID() == EVEDB::invCategories::Blueprint) {
            BlueprintRef bRef = BlueprintRef::StaticCast(iRef);
            pLevel = bRef->pLevel();
            mLevel = bRef->mLevel();
            isCopy = bRef->copy() ? 1 : 0;
            runs = bRef->runs();
        }

        itemsToInsert.append("(" + std::to_string(contractId) + ", " +
            std::to_string(iRef->itemID()) + ", " +
            std::to_string(iRef->quantity()) + ", " +
            std::to_string(iRef->typeID()) + ", " +
            "1, 0, " +
            std::to_string(pLevel) + ", " +
            std::to_string(mLevel) + ", " +
            std::to_string(isCopy) + "," +
            std::to_string(runs) + ", " +
            std::to_string(iRef->GetAttribute(AttrDamage).get_int()) + ", " +
            std::to_string(iRef->flag()) + "),");

        // We only calculate volume for courier-type contracts - the other types don't use this value.
        if (contractType->value() == Contract::Type::Courier)
            totalVolume += iRef->GetAttribute(AttrVolume).get_float() * iRef->quantity();

        // the item stays in the hangar but belongs to the contract (contents of ships/containers keep their owner until accepted)
        iRef->ChangeOwner(ownerSystem, true);
        iRef->SaveItem();
    }

    for (auto cur : requestedItems) {
        itemsToInsert.append("(" + std::to_string(contractId) + ", " +
                             "0, " +
                             std::to_string(cur.second) + ", " +
                             std::to_string(cur.first) + ", " +
                             "0, 0, 0, 0, 0, 0, 0, 0),");
    }

    if (!itemsToInsert.empty()) {
        itemsToInsert.pop_back();
        std::string query = "INSERT INTO ctrItems (contractId, itemID, quantity, itemTypeID, inCrate, parentID, "
                            "productivityLevel, materialLevel, isCopy, licensedProductionRunsRemaining, damage, flagID) "
                            "VALUES " + itemsToInsert;
        uint32 last_insert;
        if (!sDatabase.RunQueryLID(err, last_insert, query.c_str()))
            codelog(DATABASE__ERROR, "Failed to insert contract items: %s", err.c_str());

        // We only insert volume for courier-type contracts - the other types don't use this value.
        if (contractType->value() == Contract::Type::Courier) {
            if (!sDatabase.RunQuery(err, "UPDATE ctrContracts SET volume = %f WHERE contractId = %u", totalVolume, contractId))
                codelog(DATABASE__ERROR, "Failed to update contract volume: %s", err.c_str());
        }
    }

    return new PyInt((int) contractId);
}

PyResult ContractProxy::DeleteContract(PyCallArgs &call, PyInt* contractID) {
    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c))
        throw UserError("ConContractNotFound");

    // only the issuer can delete, and only before anyone accepted it (expired contracts are still outstanding)
    if (c.issuerID != call.client->GetCharacterID())
        throw UserError("ConNotYourContract");
    if ((c.status != Contract::Status::Outstanding) and (c.status != Contract::Status::Rejected))
        throw CustomError("You can only delete contracts that have not been accepted.");

    // mark it first, so a second call can't return the items or reward again
    if (!ContractUtils::SetStatus(c.contractID, Contract::Status::Deleted))
        return new PyBool(false);

    ContractUtils::ReturnItemsToIssuer(c);
    ContractUtils::RefundReward(c);

    return new PyBool(true);
}

PyResult ContractProxy::GetContract(PyCallArgs &call, PyInt* contractID) {
    return ContractUtils::GetContractEntry(contractID->value());
}

PyResult ContractProxy::AcceptContract(PyCallArgs &call, PyInt* contractID, std::optional<PyRep*> forCorp) {
    // client sends (contractID, forCorp).  corp contracts are not supported yet
    if (forCorp.has_value() and forCorp.value()->IsBool() and forCorp.value()->AsBool()->value())
        throw CustomError("Corporation contracts are not available at this time.");
    if (forCorp.has_value() and forCorp.value()->IsInt() and (forCorp.value()->AsInt()->value() != 0))
        throw CustomError("Corporation contracts are not available at this time.");

    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c))
        throw UserError("ConContractNotFound");

    uint32 charID(call.client->GetCharacterID());
    int64 timestamp = int64(GetFileTimeNow());

    // We can only accept outstanding contracts
    if (c.status != Contract::Status::Outstanding)
        throw CustomError("This contract is no longer available.");
    if (c.dateExpired <= timestamp)
        throw CustomError("This contract has expired.");
    if (c.issuerID == charID)
        throw CustomError("You cannot accept your own contract.");
    if (c.isPrivate
    and (c.assigneeID != charID)
    and (c.assigneeID != call.client->GetCorporationID())
    and ((call.client->GetAllianceID() == 0) or (c.assigneeID != call.client->GetAllianceID())))
        throw UserError("ConNotYourContract");

    // the traded items must still be held by the contract in the start station
    std::vector<int> itemIDs;
    ContractUtils::GetContractItemIDs(c.contractID, &itemIDs);
    std::vector<InventoryItemRef> tradedItems;
    for (auto itemID : itemIDs) {
        InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
        if ((iRef.get() == nullptr) or (iRef->ownerID() != ownerSystem) or (iRef->locationID() != c.startStationID)) {
            _log(SERVICE__WARNING, "AcceptContract - item %i of contract %i is no longer held by the contract.", itemID, c.contractID);
            throw CustomError("This contract is no longer valid.");
        }
        tradedItems.push_back(iRef);
    }

    switch (c.type) {
        case Contract::Type::ItemExchange: {
            // requested items must be the acceptor's own, unassembled, in the item hangar of the start station
            std::map<int, int> requestedItems;
            ContractUtils::GetRequestedItems(c.contractID, &requestedItems);
            Inventory* pHangar(nullptr);
            if (!requestedItems.empty()) {
                pHangar = call.client->GetStationHangar(c.startStationID);
                if (pHangar == nullptr)
                    throw CustomError("This contract is no longer valid.");
                for (const auto& entry : requestedItems)
                    if (pHangar->GetTypeQtyForOwner(entry.first, flagHangar, charID) < (uint32)entry.second)
                        throw CustomError("You do not have the required items in your hangar in %s.", stDataMgr.GetStationName(c.startStationID).c_str());
            }

            // legacy rows (reward not escrowed): the issuer pays the reward now
            if ((c.reward > 0) and !c.rewardEscrowed) {
                Client* pIssuer = sEntityList.FindClientByCharID(c.issuerID);
                double issuerBalance(0);
                if (pIssuer != nullptr) {
                    issuerBalance = pIssuer->GetBalance();
                } else {
                    CharacterRef cRef = sItemFactory.GetCharacterRef(c.issuerID);
                    if (cRef.get() != nullptr)
                        issuerBalance = cRef->balance(Account::CreditType::ISK);
                }
                if (issuerBalance < c.reward)
                    throw CustomError("The issuer of this contract does not have enough ISK to pay the reward.");
            }

            // this throws if the acceptor is short; nothing has changed yet
            if (c.price > 0)
                AccountService::TransferFunds(charID, c.issuerID, c.price, "Payment for accepted contract", Journal::EntryType::ContractPrice, c.contractID);

            // mark it accepted before anything else moves, so a second call fails the status check
            ContractUtils::SetStatus(c.contractID, Contract::Status::Finished, timestamp, timestamp, charID);

            if (c.reward > 0) {
                if (c.rewardEscrowed) {
                    AccountService::TransferFunds(ownerSystem, charID, c.reward, "Contract reward", Journal::EntryType::ContractReward, c.contractID);
                } else {
                    AccountService::TransferFunds(c.issuerID, charID, c.reward, "Contract reward", Journal::EntryType::ContractReward, c.contractID);
                }
            }

            // requested items go to the issuer in the start station
            for (const auto& entry : requestedItems) {
                std::vector<InventoryItemRef> taken;
                if (!pHangar->TakeTypeQtyForOwner(entry.first, flagHangar, charID, entry.second, taken))
                    _log(SERVICE__ERROR, "AcceptContract - failed to take %i of type %i from %u for contract %i.", entry.second, entry.first, charID, c.contractID);
                for (auto cur : taken)
                    cur->Donate(c.issuerID, c.startStationID, flagHangar, true);
            }

            // traded items go to the acceptor
            for (auto cur : tradedItems)
                ContractUtils::ChangeOwnerRecursive(cur, charID);
        } break;
        case Contract::Type::Courier: {
            // collateral is held by the system until the contract ends.  this throws if the acceptor is short
            if (c.collateral > 0)
                AccountService::TransferFunds(charID, ownerSystem, c.collateral, "Contract collateral", Journal::EntryType::ContractCollateral, c.contractID);

            /**
             * Courier contract acceptance includes following steps:
             * - Create a plastic wrap container
             * - Set capacity and volume attribute to the volume of the contracted items
             * - Move all items inside this container (they stay owned by the system until delivered)
             * - Give that container to the character
             * - Update contract entry - move it to In Progress and leave a time-stamp when it was accepted.
             */
            std::string containerName = sDataMgr.GetSystemName(c.startSolarSystemID);
            containerName += " -> ";
            containerName += sDataMgr.GetSystemName(c.endSolarSystemID);
            containerName += " (" + std::to_string(c.volume) + "m3)";

            ItemData itemData(itemPlasticWrap, charID, locTemp, flagNone);
            itemData.name = containerName;
            InventoryItemRef plasticWrap = sItemFactory.SpawnItem(itemData);
            if (plasticWrap.get() == nullptr) {
                if (c.collateral > 0)
                    AccountService::TransferFunds(ownerSystem, charID, c.collateral, "Contract collateral refund", Journal::EntryType::ContractCollateralRefund, c.contractID);
                throw CustomError("Unable to create the courier package.");
            }
            plasticWrap->SetAttribute(AttrVolume, c.volume);
            plasticWrap->SetAttribute(AttrCapacity, c.volume);
            plasticWrap->SaveItem();

            ContractUtils::SetStatus(c.contractID, Contract::Status::InProgress, timestamp, 0, charID, plasticWrap->itemID());

            for (auto cur : tradedItems)
                cur->Move(plasticWrap->itemID(), flagNone, true);

            // And we give the container to the player
            plasticWrap->Move(c.startStationID, flagHangar, true);
        } break;
        default:
            throw CustomError("This type of contract cannot be accepted.");
    }

    // DBResultToCRowset return doesn't work, for some reason (fails at unmarshalling on client's side), so making it a KeyVal dict
    PyDict* ret = new PyDict;
    ret->SetItemString("contractID", new PyInt(c.contractID));
    ret->SetItemString("type", new PyInt(c.type));
    ret->SetItemString("startStationID", new PyInt(c.startStationID));
    ret->SetItemString("endStationID", new PyInt(c.endStationID));
    ret->SetItemString("dateAccepted", new PyLong(timestamp));
    ret->SetItemString("numDays", new PyInt(c.numDays));
    return new PyObject("util.KeyVal", ret);
}


PyResult ContractProxy::CompleteContract(PyCallArgs &call, PyInt* contractID, PyInt* completionStatus) {
    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c))
        return new PyBool(false);

    uint32 charID(call.client->GetCharacterID());
    int64 timestamp = int64(GetFileTimeNow());
    switch (completionStatus->value()) {
        case Contract::Status::Finished: {
            // acceptor delivering a courier package
            if ((c.type != Contract::Type::Courier) or (c.status != Contract::Status::InProgress))
                throw CustomError("This contract is not in progress.");
            if (c.acceptorID != charID)
                throw UserError("ConNotYourContract");
            // First, we need to make sure that the container is indeed located in the end station
            if ((uint32)call.client->GetStationID() != c.endStationID)
                throw CustomError("You have to deliver the package to %s.", stDataMgr.GetStationName(c.endStationID).c_str());

            InventoryItemRef crate = sItemFactory.GetItemRef(c.crateID);
            if ((crate.get() == nullptr) or (crate->ownerID() != charID)
            or ((crate->locationID() != c.endStationID) and (crate->locationID() != call.client->GetShipID())))
                throw CustomError("The courier package must be in your hangar or cargo hold in %s.", stDataMgr.GetStationName(c.endStationID).c_str());

            // Then, we validate the presence of all expected items
            std::map<int, int> expectedItems;
            ContractUtils::GetContractItemIDsAndQuantities(c.contractID, &expectedItems);
            for (const auto& entry : expectedItems) {
                InventoryItemRef item = sItemFactory.GetItemRef(entry.first);
                if ((item.get() == nullptr) or (item->quantity() != entry.second) or (item->locationID() != c.crateID)) {
                    call.client->SendNotifyMsg("Not all required items are located in the container");
                    return new PyBool(false);
                }
            }

            if (!ContractUtils::SetStatus(c.contractID, Contract::Status::Finished, 0, timestamp))
                return new PyBool(false);

            // Once checks have passed, we extract the items into the issuer's hangar in the destination
            for (const auto& entry : expectedItems) {
                InventoryItemRef item = sItemFactory.GetItemRef(entry.first);
                ContractUtils::ChangeOwnerRecursive(item, c.issuerID);
                item->Move(c.endStationID, flagHangar, true);
            }
            crate = sItemFactory.GetItemRef(c.crateID);
            if (crate.get() != nullptr)
                crate->Delete();

            // Then, we pay the reward and return the collateral (both held by the system)
            if (c.reward > 0)
                AccountService::TransferFunds(ownerSystem, charID, c.reward, "Contract reward", Journal::EntryType::ContractReward, c.contractID);
            if (c.collateral > 0)
                AccountService::TransferFunds(ownerSystem, charID, c.collateral, "Contract collateral refund", Journal::EntryType::ContractCollateralRefund, c.contractID);
        } break;
        case Contract::Status::Failed: {
            if ((c.type != Contract::Type::Courier) or (c.status != Contract::Status::InProgress))
                throw CustomError("This contract is not in progress.");
            // the acceptor can give up at any time; the issuer only when the acceptor is overdue
            bool overdue(timestamp > (c.dateAccepted + (int64)c.numDays * EvE::Time::Day));
            if ((c.acceptorID != charID) and !((c.issuerID == charID) and overdue))
                throw UserError("ConNotYourContract");

            if (!ContractUtils::SetStatus(c.contractID, Contract::Status::Failed, 0, timestamp))
                return new PyBool(false);

            // collateral goes to the issuer, the reward back to the issuer
            if (c.collateral > 0)
                AccountService::TransferFunds(ownerSystem, c.issuerID, c.collateral, "Contract collateral payout", Journal::EntryType::ContractCollateralPayout, c.contractID);
            ContractUtils::RefundReward(c);

            // the acceptor paid for the goods with the collateral and keeps the package
            std::map<int, int> expectedItems;
            ContractUtils::GetContractItemIDsAndQuantities(c.contractID, &expectedItems);
            for (const auto& entry : expectedItems) {
                InventoryItemRef item = sItemFactory.GetItemRef(entry.first);
                if ((item.get() != nullptr) and (item->ownerID() == ownerSystem))
                    ContractUtils::ChangeOwnerRecursive(item, c.acceptorID);
            }
        } break;
        case Contract::Status::Rejected: {
            // assignee of a private contract turning it down.  the items stay locked until the issuer deletes it
            if ((c.status != Contract::Status::Outstanding) or !c.isPrivate
            or ((c.assigneeID != charID) and (c.assigneeID != call.client->GetCorporationID())))
                throw UserError("ConNotYourContract");
            if (!ContractUtils::SetStatus(c.contractID, Contract::Status::Rejected))
                return new PyBool(false);
        } break;
        default:
            // We do not expect anything else
            return new PyBool(false);
    }

    return new PyBool(true);
}


PyResult ContractProxy::GetMyExpiredContractList(PyCallArgs &call) {
  sLog.White( "ContractProxy::Handle_GetMyExpiredContractList()", "size=%lu", call.tuple->size());
    call.Dump(SERVICE__CALL_DUMP);
/*
      [PySubStream 530 bytes]
        [PyObjectData Name: util.KeyVal]
          [PyDict 3 kvp]
            [PyString "contracts"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 28 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "type"]
                            [PyInt 17]
                          [PyTuple 2 items]
                            [PyString "issuerID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "issuerCorpID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "forCorp"]
                            [PyInt 11]
                          [PyTuple 2 items]
                            [PyString "availability"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "assigneeID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "acceptorID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "dateIssued"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "dateExpired"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "dateAccepted"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "numDays"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "dateCompleted"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "startStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "startSolarSystemID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "startRegionID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endSolarSystemID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endRegionID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "price"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "reward"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "collateral"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "title"]
                            [PyInt 130]
                          [PyTuple 2 items]
                            [PyString "status"]
                            [PyInt 17]
                          [PyTuple 2 items]
                            [PyString "volume"]
                            [PyInt 5]
                          [PyTuple 2 items]
                            [PyString "issuerAllianceID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "issuerWalletKey"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "acceptorWalletKey"]
                            [PyInt 3]
            [PyString "items"]
            [PyDict 0 kvp]
            [PyString "bids"]
            [PyDict 0 kvp]
            */
    return nullptr;
}

PyResult ContractProxy::NumOutstandingContracts(PyCallArgs &call) {
    sLog.White( "ContractProxy::Handle_NumOutstandingContracts()", "size=%lu", call.tuple->size());
    call.Dump(SERVICE__CALL_DUMP);
    /*
      [PySubStream 87 bytes]
        [PyObjectData Name: util.KeyVal]
          [PyDict 4 kvp]
            [PyString "nonCorpForMyChar"]
            [PyInt 0]
            [PyString "myCorpTotal"]
            [PyInt 0]
            [PyString "nonCorpForMyCorp"]
            [PyInt 0]
            [PyString "myCharTotal"]
            [PyInt 0]
            */
    return nullptr;
}

PyResult ContractProxy::GetItemsInStation(PyCallArgs &call, PyInt* stationID, std::optional<PyInt*> forCorp) {
    uint32 station = call.tuple->GetItem(0)->AsInt()->value();

    if (sDataMgr.IsStation(stationID->value()) == false)
        return nullptr;

    return sItemFactory.GetStationRef(station)->GetMyInventory()->List(flagHangar);
}

PyResult ContractProxy::CollectMyPageInfo(PyCallArgs &call) {
    sLog.White( "ContractProxy::Handle_CollectMyPageInfo()", "size=%lu", call.tuple->size());
    call.Dump(SERVICE__CALL_DUMP);
    std::string mainQuery = "SELECT "
                            "IFNULL(SUM(CASE WHEN (cC.issuerID = {CHAR_ID} AND cC.status = 0) THEN 1 ELSE 0 END), 0) AS numOutstandingContracts, "
                            "IFNULL(SUM(CASE WHEN (cC.issuerID = {CHAR_ID} AND cC.status = 0 AND cC.forCorp = false) THEN 1 ELSE 0 END), 0) AS numOutstandingContractsNonCorp, "
                            "IFNULL(SUM(CASE WHEN (cC.issuerID = {CHAR_ID} AND cC.status = 0 AND cC.forCorp = true) THEN 1 ELSE 0 END), 0) AS numOutstandingContractsForCorp, "
                            "IFNULL(SUM(CASE WHEN (cC.assigneeID = {CHAR_ID} AND cC.forCorp = false AND cC.status = 1) THEN 1 ELSE 0 END), 0) AS numInProgress, "
                            "IFNULL(SUM(CASE WHEN (cC.issuerCorpID = {CORP_ID} AND cC.forCorp = true AND cC.status = 1) THEN 1 ELSE 0 END), 0) AS numInProgressCorp, "
                            "IFNULL(SUM(CASE WHEN (cC.assigneeID = {CHAR_ID} AND cC.status = 1) THEN 1 ELSE 0 END), 0) AS numRequiresAttention, "
                            "IFNULL(SUM(CASE WHEN (cC.issuerCorpID = {CORP_ID} AND cC.forCorp = true AND cC.status = 1) THEN 1 ELSE 0 END), 0) AS numRequiresAttentionCorp "
                            "FROM ctrContracts cC "
                            "LEFT JOIN ctrBids cB on cC.contractId = cB.contractId";
    std::string outstandingContractsQuery = "SELECT issuerID, issuerCorpID, assigneeID, contractType "
                                            "FROM ctrContracts "
                                            "WHERE assigneeID IN ({CHAR_ID},{CORP_ID}) and status = 0";
    // Since we already use boost - why not use some of it's libraries for string juggling? :P
    boost::replace_all(mainQuery, "{CHAR_ID}", std::to_string(call.client->GetCharacterID()));
    boost::replace_all(mainQuery, "{CORP_ID}", std::to_string(call.client->GetCorporationID()));
    boost::replace_all(outstandingContractsQuery, "{CHAR_ID}", std::to_string(call.client->GetCharacterID()));
    boost::replace_all(outstandingContractsQuery, "{CORP_ID}", std::to_string(call.client->GetCorporationID()));

    // First, we gather outstanding contracts - they will later be packed into response dict
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, outstandingContractsQuery.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in mainQuery: %s", res.error.c_str());
        return nullptr;
    }
    DBResultRow row;
    PyList* oustandingContractsList = new PyList;
    while (res.GetRow(row)) {
        PyList* list = new PyList(4);
        list->SetItem(0, new PyInt(row.GetInt(0)));
        list->SetItem(1, new PyInt(row.GetInt(1)));
        list->SetItem(2, new PyInt(row.GetInt(2)));
        list->SetItem(3, new PyInt(row.GetInt(3)));

        oustandingContractsList->AddItem(list);
    }

    // Then, we go for main message body data
    if (!sDatabase.RunQuery(res, mainQuery.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in mainQuery: %s", res.error.c_str());
        return nullptr;
    }
    // Because of outstandingContracts list in this response, we can't return DBResultToCRowset directly - so, we'll have to compose the dict manually.
    res.GetRow(row);
    PyDict* vals = new PyDict;
    vals->SetItemString("numOutstandingContracts", new PyInt(row.GetInt(0)));
    vals->SetItemString("numOutstandingContractsNonCorp", new PyInt(row.GetInt(1)));
    vals->SetItemString("numOutstandingContractsForCorp", new PyInt(row.GetInt(2)));
    vals->SetItemString("numInProgress", new PyInt(row.GetInt(3)));
    vals->SetItemString("numInProgressCorp", new PyInt(row.GetInt(4)));
    vals->SetItemString("outstandingContracts", oustandingContractsList);
    vals->SetItemString("numRequiresAttention", new PyInt(row.GetInt(5)));
    vals->SetItemString("numRequiresAttentionCorp", new PyInt(row.GetInt(6)));
    vals->SetItemString("numBiddingOn", new PyInt(0));      // Left hard-coded until bidding is implemented
    vals->SetItemString("numBiddingOnCorp", new PyInt(0));  // Left hard-coded until bidding is implemented

    return new PyObject("util.KeyVal", vals);
}

PyResult ContractProxy::GetContractListForOwner(PyCallArgs &call, PyInt* ownerID, PyInt* contractStatus, std::optional <PyInt*> contractType, std::optional <PyBool*> issuedToBy) {
    sLog.White( "ContractProxy::Handle_GetContractListForOwner()", "size=%lu", call.tuple->size());
    call.Dump(SERVICE__CALL_DUMP);

    return ContractUtils::GetContractListForOwner(ownerID, contractStatus, contractType, issuedToBy);
    /*
     *  client call....
      [PyTuple 2 items]
        [PyInt 0]
        [PySubStream 73 bytes]
          [PyTuple 4 items]
            [PyInt 1]
            [PyString "GetContractListForOwner"]
            [PyTuple 4 items]
              [PyInt 649670823]
              [PyInt 0] (Contract status, 0 for outstanding, 1 for in progress, 4 for Finished)
              [PyInt] (Contract type. 1 for exchange, 2 for auction, 3 for courier, None for all)
              [PyBool] (false for Issued By, true for issued To, None for both)
            [PyDict 3 kvp]
              [PyString "num"]
              [PyInt 100]
              [PyString "machoVersion"]
              [PyInt 1]
              [PyString "startContractID"]
              [PyNone]

        server reply...
      [PySubStream 713 bytes]
        [PyObjectData Name: util.KeyVal]
          [PyDict 3 kvp]
            [PyString "contracts"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 30 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "type"]
                            [PyInt 17]
                          [PyTuple 2 items]
                            [PyString "issuerID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "issuerCorpID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "forCorp"]
                            [PyInt 11]
                          [PyTuple 2 items]
                            [PyString "availability"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "assigneeID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "acceptorID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "dateIssued"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "dateExpired"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "dateAccepted"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "numDays"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "dateCompleted"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "startStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "startSolarSystemID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "startRegionID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endSolarSystemID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endRegionID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "price"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "reward"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "collateral"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "title"]
                            [PyInt 130]
                          [PyTuple 2 items]
                            [PyString "status"]
                            [PyInt 17]
                          [PyTuple 2 items]
                            [PyString "volume"]
                            [PyInt 5]
                          [PyTuple 2 items]
                            [PyString "issuerAllianceID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "issuerWalletKey"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "acceptorWalletKey"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "crateID"]
                            [PyInt 20]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
              [PyPackedRow 146 bytes]
                ["contractID" => <41239648> [I4]]
                ["type" => <1> [UI1]]
                ["issuerID" => <649670823> [I4]]
                ["issuerCorpID" => <98038978> [I4]]
                ["forCorp" => <0> [Bool]]
                ["availability" => <1> [I4]]
                ["assigneeID" => <1661059544> [I4]]
                ["acceptorID" => <0> [I4]]
                ["dateIssued" => <129494707760000000> [FileTime]]
                ["dateExpired" => <129495571760000000> [FileTime]]
                ["dateAccepted" => <129494707760000000> [FileTime]]
                ["numDays" => <0> [I4]]
                ["dateCompleted" => <129494707760000000> [FileTime]]
                ["startStationID" => <60006433> [I4]]
                ["startSolarSystemID" => <30000135> [I4]]
                ["startRegionID" => <10000002> [I4]]
                ["endStationID" => <60006433> [I4]]
                ["endSolarSystemID" => <0> [I4]]
                ["endRegionID" => <0> [I4]]
                ["price" => <150000000> [CY]]
                ["reward" => <0> [CY]]
                ["collateral" => <0> [CY]]
                ["title" => <quafe!> [WStr]]
                ["status" => <0> [UI1]]
                ["volume" => <6> [R8]]
                ["issuerAllianceID" => <0> [I4]]
                ["issuerWalletKey" => <0> [I4]]
                ["acceptorWalletKey" => <0> [I4]]
                ["crateID" => <1002309425092> [I8]]
                ["contractID" => <41239648> [I4]]
            [PyString "items"]
            [PyDict 1 kvp]
              [PyInt 41239648]
              [PyList 1 items]
                [PyObjectData Name: util.Row]
                  [PyDict 2 kvp]
                    [PyString "header"]
                    [PyList 3 items]
                      [PyString "itemTypeID"]
                      [PyString "quantity"]
                      [PyString "inCrate"]
                    [PyString "line"]
                    [PyList 3 items]
                      [PyInt 3898]
                      [PyInt 6]
                      [PyBool True]
            [PyString "bids"]
            [PyDict 0 kvp]
     */
    return nullptr;
}

PyResult ContractProxy::GetLoginInfo(PyCallArgs &call)
{
    // currently a stub as I need to redesign or change some sub systems for this.

    /* create needsAttention row descriptor */
    DBRowDescriptor *needsAttentionHeader = new DBRowDescriptor();
        needsAttentionHeader->AddColumn( "contractID",   DBTYPE_I4);
        needsAttentionHeader->AddColumn( "",             DBTYPE_I4);

    /* create inProgress row descriptor */
    DBRowDescriptor *inProgressHeader = new DBRowDescriptor();
        inProgressHeader->AddColumn( "contractID",      DBTYPE_I4 );
        inProgressHeader->AddColumn( "startStationID",  DBTYPE_I4 );
        inProgressHeader->AddColumn( "endStationID",    DBTYPE_I4 );
        inProgressHeader->AddColumn( "expires",         DBTYPE_FILETIME );

    /* create assignedToMe row descriptor */
    DBRowDescriptor *assignedToMeHeader = new DBRowDescriptor();
        assignedToMeHeader->AddColumn( "contractID",    DBTYPE_I4);
        assignedToMeHeader->AddColumn( "issuerID",      DBTYPE_I4);

    CRowSet *needsAttention_rowset = new CRowSet( &needsAttentionHeader );
    CRowSet *inProgress_rowset = new CRowSet( &inProgressHeader );
    CRowSet *assignedToMe_rowset = new CRowSet( &assignedToMeHeader );

    PyDict* args = new PyDict;
        args->SetItemString( "needsAttention",          needsAttention_rowset );
        args->SetItemString( "inProgress",              inProgress_rowset );
        args->SetItemString( "assignedToMe",            assignedToMe_rowset );

    return new PyObject( "util.KeyVal", args );
}

     /**
     * NOTE: Contract statuses:
     * 0 - Outstanding
     * 1 - In Progress
     * 2 - Items not yet claimed
     * 3 - Unclaimed by seller
     * 4,5 - Finished
     * 6 - Rejected
     * 7 - Failed
     * 8 - Deleted
     * 9 - Reversal
     */

/* Description
 * GetLoginInfo sends back a util.KeyVal PyClass. This class contains 3 entries, those entries are
 * "needsAttention", "inProgress" and "assignedToMe".
 *
 * ------------------
 * assignedToMe
 * ------------------
 * assignedToMe is a pretty obvious packet, it contains rows of userid's and contractid's
 * that are belonging to the player.
 *
 * ------------------
 * inProgress
 * ------------------
 * inProgress is also pretty obvious, it contains rows of contractID, startStationID, endStationID and expires
 * that marks what contract is in progress.
 *
 * ------------------
 * needsAttention
 * ------------------
 * needsAttention contains contracts that need attention, its rather a way to make the journal blink. The
 * contracts off course are highlighted in the journal.
 *
      [PySubStream 273 bytes]
        [PyObjectData Name: util.KeyVal]
          [PyDict 3 kvp]
            [PyString "inProgress"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 4 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "startStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "expires"]
                            [PyInt 64]
            [PyString "needsAttention"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 2 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString ""]
                            [PyInt 3]
            [PyString "assignedToMe"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 2 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "issuerID"]
                            [PyInt 3]
*/
