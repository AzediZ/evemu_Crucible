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
    Author: AlTahir
*/

#include <boost/algorithm/string/replace.hpp>
#include "eve-server.h"


#include "Client.h"
#include "EVEServerConfig.h"
#include "ContractUtils.h"
#include "account/AccountService.h"
#include "character/CharacterDB.h"
#include "inventory/Inventory.h"
#include "market/MarketMgr.h"

// Since these queries are used in multiple GET functions, we initialize them as constants and use at will.
const std::string getContractQueryBase = "SELECT contractId as contractID, contractType as type, issuerID, issuerCorpID, forCorp, isPrivate as availability, "
                               "assigneeID, acceptorID, dateIssued, dateExpired, dateAccepted, numDays, dateCompleted, startStationID, startSolarSystemID, "
                               "startRegionID, endStationID, endSolarSystemID, endRegionID, price, reward, collateral, title, description, status, "
                               "crateID, volume, issuerAllianceID, issuerWalletKey, acceptorWalletKey "
                               "FROM ctrContracts ";
const std::string getContractItemsQueryBase = "SELECT contractId as contractID, itemID, quantity, itemTypeID, inCrate, parentID, productivityLevel, materialLevel, isCopy as copy, "
                                    "licensedProductionRunsRemaining, damage, flagID "
                                    "FROM ctrItems "
                                    "WHERE contractId IN (%s)";
const std::string getContractItemsShortQueryBase = "SELECT itemID "
                                                   "FROM ctrItems "
                                                   "WHERE contractId IN (%s) AND inCrate = true";
// every bid (also outbid ones, the client uses them to show "outbid"), highest first.  each bid is higher than the last
const std::string getContractBidsQueryBase = "SELECT contractId, amount, bidderID, bidDateTime "
                                   "FROM ctrBids "
                                   "WHERE contractId IN (%s) "
                                   "ORDER BY amount DESC, bidID ASC";

/**
 * Gathers and packs contract data into a util.KeyVal PyObject.
 * @param contractId - contract ID to get
 * @return - util.KeyVal PyObject (or nullptr, if contract doesn't exist or there were errors during execution)
 */
PyResult ContractUtils::GetContractEntry(int contractId)
{
    std::string contractID = std::to_string(contractId);

    DBQueryResult contractRes;
    if (!sDatabase.RunQuery(contractRes, (getContractQueryBase + "WHERE contractId IN (%s)").c_str(), contractID.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", contractRes.error.c_str());
        return nullptr;
    }
    if (contractRes.GetRowCount() > 0) {
        DBResultRow contractRow;
        contractRes.GetRow(contractRow);

        // We only run queries for items and bids if we had something returned in contracts query - we don't need to waste time and resources on non-existent contracts.
        DBQueryResult itemsRes;
        DBQueryResult bidsRes;
        if (!sDatabase.RunQuery(itemsRes, getContractItemsQueryBase.c_str(), contractID.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in query: %s", itemsRes.error.c_str());
            return nullptr;
        }
        if (!sDatabase.RunQuery(bidsRes, getContractBidsQueryBase.c_str(), contractID.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in query: %s", bidsRes.error.c_str());
            return nullptr;
        }

        PyDict* response = new PyDict;
        response->SetItemString("contract", DBRowToPackedRow(contractRow));
        response->SetItemString("items", DBResultToCRowset(itemsRes));
        response->SetItemString("bids", DBResultToCRowset(bidsRes));

        return new PyObject( "util.KeyVal", response );
    } else {
        codelog(SERVICE__ERROR, "No contract with ID '%s' was found. Aborting", contractID.c_str());
        return nullptr;
    }
}

/**
 * Queries contracts and returns a PyList object, that contains data for each of them.
 * @param contractIds - Vector with all contractID's to search for
 * @return PyList with Contract KeyVal objects.
 */
PyList* ContractUtils::GetContractEntries(std::vector<int> contractIDList) {
    std::string contractIDs;
    if (!contractIDList.empty()) {
        for (auto contractID : contractIDList) {
            contractIDs.append(std::to_string(contractID) + ",");
        }
        contractIDs.pop_back(); // we do pop-back to remove trailing comma.
    } else {
        codelog(SERVICE__ERROR, "ContractUtils: contractIDList was empty. Aborting");
        return nullptr;
    }

    DBQueryResult contractRes;
    // in the order of the list (search results are sorted)
    if (!sDatabase.RunQuery(contractRes, (getContractQueryBase + "WHERE contractId IN (%s) ORDER BY FIELD(contractId, %s)").c_str(), contractIDs.c_str(), contractIDs.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", contractRes.error.c_str());
        return nullptr;
    }
    if (contractRes.GetRowCount() > 0) {
        // We only run queries for items and bids if we had something returned in contracts query - we don't need to waste time and resources on non-existent contracts.
        DBQueryResult itemsRes;
        DBQueryResult bidsRes;
        if (!sDatabase.RunQuery(itemsRes, getContractItemsQueryBase.c_str(), contractIDs.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in query: %s", itemsRes.error.c_str());
            return nullptr;
        }
        if (!sDatabase.RunQuery(bidsRes, getContractBidsQueryBase.c_str(), contractIDs.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in query: %s", bidsRes.error.c_str());
            return nullptr;
        }

        DBResultRow contractRow;
        /**
         * We use map as temp storage - we want to do a single iteration over each DBQueryResult.
         * Given that we'd like to initialize sub-contents of resulting PyList when we're done (we can't really
         * append rows to CRowSet inside PyDict - there's no CRowSet return), we:
         * - iterate over Items DBResultQuery and store values in temporary map (int <-> CRowSet)
         * - iterate over Bids DBResultQuery and store values in temporary map (int <-> CRowSet)
         * - We iterate over Contracts DBResultQuery, initialize PyDict for each, pull items and bids from previously
         *   saved maps and fill in the data.
         *
         * I think there might be a better way to do it, but that's a matter for further optimizations.
         */
        DBRowDescriptor* itemsHeader = new DBRowDescriptor(itemsRes);
        std::map<int, CRowSet*> itemsMap;
        DBResultRow itemRow;
        while (itemsRes.GetRow(itemRow)) {
            int contractID = itemRow.GetInt(0);

            auto pos = itemsMap.find(contractID);
            if (pos == itemsMap.end()) {
                // each rowset holds its own reference to the shared header
                PyIncRef(itemsHeader);
                CRowSet* rowset = new CRowSet(&itemsHeader);
                PyPackedRow* into = rowset->NewRow();
                FillItemData(&itemRow, into);

                itemsMap[contractID] = rowset;
            } else {
                CRowSet* rowset = pos->second;
                PyPackedRow* into = rowset->NewRow();
                FillItemData(&itemRow, into);
            }
        }

        DBRowDescriptor* bidsHeader = new DBRowDescriptor(bidsRes);
        std::map<int, CRowSet*> bidsMap;
        DBResultRow bidRow;
        while (bidsRes.GetRow(bidRow)) {
            int contractID = bidRow.GetInt(0);

            auto pos = bidsMap.find(contractID);
            if (pos == bidsMap.end()) {
                PyIncRef(bidsHeader);
                CRowSet* rowset = new CRowSet(&bidsHeader);
                PyPackedRow* into = rowset->NewRow();
                FillBidData(&bidRow, into);

                bidsMap[contractID] = rowset;
            } else {
                CRowSet* rowset = pos->second;
                PyPackedRow* into = rowset->NewRow();
                FillBidData(&bidRow, into);
            }
        }

        PyList* contractsList = new PyList;
        while (contractRes.GetRow(contractRow)) {
            int contractID = contractRow.GetInt(0);

            PyDict* contract = new PyDict;
            contract->SetItemString("contract", DBRowToPackedRow(contractRow));
            auto itemItr = itemsMap.find(contractID);
            if (itemItr == itemsMap.end()) {
                PyIncRef(itemsHeader);
                contract->SetItemString("items", new CRowSet(&itemsHeader));
            } else {
                contract->SetItemString("items", itemItr->second);
            }
            auto bidItr = bidsMap.find(contractID);
            if (bidItr == bidsMap.end()) {
                PyIncRef(bidsHeader);
                contract->SetItemString("bids", new CRowSet(&bidsHeader));
            } else {
                contract->SetItemString("bids", bidItr->second);
            }

            contractsList->AddItem(new PyObject("util.KeyVal", contract));
        }

        // release the creation references; the rowsets hold their own
        PyDecRef(itemsHeader);
        PyDecRef(bidsHeader);

        return contractsList;
    } else {
        codelog(SERVICE__ERROR, "No contracts in range ('%s') was found. Aborting", contractIDs.c_str());
        return nullptr;
    }
}

/**
 * Queries and composes a response object for GetContractListForOwner call. Placed in utils class so that we can re-use query strings
 * @param call - Call instance
 * @return - Response obj
 */
PyResult ContractUtils::GetContractListForOwner(PyInt* ownerID, PyInt* contractStatus, std::optional <PyInt*> contractType, std::optional <PyBool*> issuedToBy) {
    std::string contracts_query = getContractQueryBase;
    std::string items_query = "SELECT contractId, itemTypeID, quantity, inCrate "
                              "FROM ctrItems "
                              "WHERE contractId IN (";
    std::vector<int> contractIDs;

    // First, we finish contracts query by adding filters
    if (issuedToBy.has_value() == false) {
        contracts_query.append("WHERE (issuerID = {OWNER_ID} OR assigneeID = {OWNER_ID} OR acceptorID = {OWNER_ID}) ");
    } else {
        bool issued = issuedToBy.value()->AsBool()->value();
        if (issued) {
            contracts_query.append("WHERE (assigneeID = {OWNER_ID} OR acceptorID = {OWNER_ID}) ");
        } else {
            contracts_query.append("WHERE issuerID = {OWNER_ID} ");
        }
    }
    if (contractType.has_value() == false) {
        contracts_query.append("AND contractType IN (1,2,3) ");
    } else {
        contracts_query.append("AND contractType = " + std::to_string(contractType.value()->value()) + " ");
    }
    contracts_query.append("AND status = " + std::to_string(contractStatus->AsInt()->value()));
    boost::replace_all(contracts_query, "{OWNER_ID}", std::to_string(ownerID->AsInt()->value()));

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, contracts_query.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    PyObjectEx* contracts = DBResultToCRowset(res);
    for (auto contract : contracts->list()) {
        // To get items, we store contractID's in separate list
        contractIDs.push_back(contract->AsPackedRow()->GetField(0)->AsInt()->value());
    }

    PyDict* items = new PyDict;
    if (!contractIDs.empty()) {
        for (auto contractID : contractIDs) {
            items_query.append(std::to_string(contractID) + ",");
        }
        items_query.pop_back(); items_query.append(")");

        if (!sDatabase.RunQuery(res, items_query.c_str()))
        {
            codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
            return nullptr;
        }

        // Given that we have items queried for every contract in the list, we sort them by contractID's first, then we put those in the dict.
        std::map<int, CRowSet*> itemsByContractID;
        DBResultRow row;
        while (res.GetRow(row)) {
            auto pos = itemsByContractID.find(row.GetInt(0));
            if(pos == itemsByContractID.end()) {
                DBRowDescriptor *header = new DBRowDescriptor(res);
                CRowSet *rowset = new CRowSet(&header);

                PyPackedRow* packedRow = rowset->NewRow();
                packedRow->SetField("contractId", new PyInt(row.GetInt(0)));
                packedRow->SetField("itemTypeID", new PyInt(row.GetInt(1)));
                packedRow->SetField("quantity", new PyInt(row.GetInt(2)));
                packedRow->SetField("inCrate", new PyBool(row.GetBool(3)));

                itemsByContractID[row.GetInt(0)] = rowset;
            } else {
                CRowSet* rowset = pos->second;

                PyPackedRow* packedRow = rowset->NewRow();
                packedRow->SetField("contractId", new PyInt(row.GetInt(0)));
                packedRow->SetField("itemTypeID", new PyInt(row.GetInt(1)));
                packedRow->SetField("quantity", new PyInt(row.GetInt(2)));
                packedRow->SetField("inCrate", new PyBool(row.GetBool(3)));
            }
        }
        if (!itemsByContractID.empty()) {
            for (auto entry : itemsByContractID) {
                items->SetItem(new PyInt(entry.first), entry.second);
            }
        }
    }

    PyDict* ret = new PyDict;
    ret->SetItemString("contracts", contracts);
    ret->SetItemString("items", items);

    return new PyObject("util.KeyVal", ret);
}

/**
 * Service function - populates PackedRow with values from DBResultRow for ctrItems
 * @param itemRow - DB ctrItems row
 * @param targetRow - target PyPackedRow
 */
void ContractUtils::FillItemData(DBResultRow *itemRow, PyPackedRow *targetRow) {
    targetRow->SetField("contractID", new PyInt(itemRow->GetInt(0)));
    targetRow->SetField("itemID", new PyInt(itemRow->GetInt(1)));
    targetRow->SetField("quantity", new PyInt(itemRow->GetInt(2)));
    targetRow->SetField("itemTypeID", new PyInt(itemRow->GetInt(3)));
    targetRow->SetField("inCrate", new PyBool(itemRow->GetBool(4)));
    targetRow->SetField("parentID", new PyInt(itemRow->GetInt(5)));
    targetRow->SetField("productivityLevel", new PyInt(itemRow->GetInt(6)));
    targetRow->SetField("materialLevel", new PyInt(itemRow->GetInt(7)));
    targetRow->SetField("copy", new PyBool(itemRow->GetBool(8)));
    targetRow->SetField("licensedProductionRunsRemaining", new PyInt(itemRow->GetInt(9)));
    targetRow->SetField("damage", new PyInt(itemRow->GetInt(10)));
    targetRow->SetField("flagID", new PyInt(itemRow->GetInt(11)));
}

/**
 * Service function - populates PackedRow with values from DBResultRow for ctrBids
 * @param itemRow - DB ctrBids row
 * @param targetRow - target PyPackedRow
 */
void ContractUtils::FillBidData(DBResultRow *bidRow, PyPackedRow *targetRow) {
    targetRow->SetField("contractId", new PyInt(bidRow->GetInt(0)));
    targetRow->SetField("amount", new PyLong(bidRow->GetInt64(1)));
    targetRow->SetField("bidderID", new PyInt(bidRow->GetInt(2)));
    targetRow->SetField("bidDateTime", new PyLong(bidRow->GetInt64(3)));
}

/**
 * Queries the list of entityID's for a given contract. Primarily used to get the list of items to be returned to owner
 * upon contract's deletion.
 * @param contractId - Contract ID to search items in
 * @param into - Vector instance that will contain the entityID's
 */
void ContractUtils::GetContractItemIDs(int contractId, std::vector<int> *into) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, getContractItemsShortQueryBase.c_str(), std::to_string(contractId).c_str()))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        into->push_back(row.GetInt(0));
    }
}

/**
 * Queries requested items for a contract and populates provided map with itemType and quantities
 * @param contractId
 * @param into
 */
void ContractUtils::GetRequestedItems(int contractId, std::map<int32, int32> *into) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemTypeID, quantity FROM ctrItems WHERE contractID = %u AND inCrate = false", contractId))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        // the same type can be requested on more than one line
        (*into)[row.GetInt(0)] += row.GetInt(1);
    }
}

/**
 * Queries traded items and populates the map as ItemID <-> quantity
 * @param contractId - Contract ID to query
 * @param into - Target map
 */
void ContractUtils::GetContractItemIDsAndQuantities(int contractId, std::map<int, int> *into) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID, quantity FROM ctrItems WHERE contractID = %u AND inCrate = true", contractId))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        into->insert(std::pair<int, int> (row.GetInt(0), row.GetInt(1)));
    }
}

bool ContractUtils::GetContractInfo(int32 contractID, ContractInfo& into) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT contractType, status, price, reward, collateral, numDays, issuerID, issuerCorpID, assigneeID, acceptorID,"
        " startStationID, endStationID, startSolarSystemID, endSolarSystemID, crateID, forCorp, isPrivate, rewardEscrowed,"
        " dateExpired, dateAccepted, volume, expireTimeInMinutes, startStationDivision, brokersFee, deposit, depositRefunded,"
        " issuerWalletKey, acceptorWalletKey, issuerAllianceID, startRegionID, dateIssued, dateCompleted"
        " FROM ctrContracts WHERE contractId = %i", contractID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return false;

    into.contractID         = contractID;
    into.type               = row.GetInt(0);
    into.status             = row.GetInt(1);
    into.price              = row.GetInt64(2);
    into.reward             = row.GetInt64(3);
    into.collateral         = row.GetInt64(4);
    into.numDays            = row.GetInt(5);
    into.issuerID           = row.GetUInt(6);
    into.issuerCorpID       = row.GetUInt(7);
    into.assigneeID         = row.GetUInt(8);
    into.acceptorID         = row.GetUInt(9);
    into.startStationID     = row.GetUInt(10);
    into.endStationID       = row.GetUInt(11);
    into.startSolarSystemID = row.GetUInt(12);
    into.endSolarSystemID   = row.GetUInt(13);
    into.crateID            = row.GetUInt(14);
    into.forCorp            = row.GetBool(15);
    into.isPrivate          = (row.GetInt(16) != 0);
    into.rewardEscrowed     = (row.GetInt(17) != 0);
    into.dateExpired        = row.GetInt64(18);
    into.dateAccepted       = row.GetInt64(19);
    into.volume             = row.GetFloat(20);
    into.expireTimeInMinutes = row.GetInt(21);
    into.startStationDivision = row.GetInt(22);
    into.brokersFee         = row.GetInt64(23);
    into.deposit            = row.GetInt64(24);
    into.depositRefunded    = (row.GetInt(25) != 0);
    into.issuerWalletKey    = row.GetUInt(26);
    into.acceptorWalletKey  = row.GetUInt(27);
    into.issuerAllianceID   = row.GetUInt(28);
    into.startRegionID      = row.GetUInt(29);
    into.dateIssued         = row.GetInt64(30);
    into.dateCompleted      = row.GetInt64(31);
    return true;
}

bool ContractUtils::SetStatus(int32 contractID, int32 status, int64 dateAccepted/*0*/, int64 dateCompleted/*0*/, uint32 acceptorID/*0*/, uint32 crateID/*0*/) {
    std::string query = "UPDATE ctrContracts SET status = " + std::to_string(status);
    if (dateAccepted > 0)
        query += ", dateAccepted = " + std::to_string(dateAccepted);
    if (dateCompleted > 0)
        query += ", dateCompleted = " + std::to_string(dateCompleted);
    if (acceptorID > 0)
        query += ", acceptorID = " + std::to_string(acceptorID);
    if (crateID > 0)
        query += ", crateID = " + std::to_string(crateID);
    query += " WHERE contractId = " + std::to_string(contractID);

    DBerror err;
    if (!sDatabase.RunQuery(err, query.c_str())) {
        codelog(DATABASE__ERROR, "Failed to update contract %i: %s", contractID, err.c_str());
        return false;
    }
    return true;
}

void ContractUtils::ChangeOwnerRecursive(InventoryItemRef iRef, uint32 newOwner) {
    if (iRef.get() == nullptr)
        return;

    iRef->ChangeOwner(newOwner, true);
    iRef->SaveItem();

    Inventory* pInv = iRef->GetMyInventory();
    if (pInv == nullptr)
        return;
    pInv->LoadContents();
    std::vector<InventoryItemRef> contents;
    pInv->GetInventoryVec(contents);
    for (auto cur : contents)
        ChangeOwnerRecursive(cur, newOwner);
}

uint16 ContractUtils::IssuerKey(const ContractInfo& c)
{
    if (!c.forCorp)
        return Account::KeyType::Cash;
    return ((Corp::AccountTakeRole(c.issuerWalletKey) != 0) ? c.issuerWalletKey : (uint16)Account::KeyType::Cash);
}

uint16 ContractUtils::AcceptorKey(const ContractInfo& c)
{
    if (!IsPlayerCorp(c.acceptorID))
        return Account::KeyType::Cash;
    return ((Corp::AccountTakeRole(c.acceptorWalletKey) != 0) ? c.acceptorWalletKey : (uint16)Account::KeyType::Cash);
}

void ContractUtils::GiveItemsTo(const ContractInfo& c, uint32 newOwner, bool ownerIsCorp, uint32 memberID, uint32 stationID, bool moveToStation)
{
    // corps get them in the deliveries hangar of their office there (MarketMgr's rule, the member's hangar without an office)
    uint32 itemOwnerID(newOwner), locationID(stationID);
    EVEItemFlags flag(flagHangar);
    MarketMgr::GetDeliveryLocation(newOwner, ownerIsCorp, memberID, stationID, itemOwnerID, locationID, flag);

    std::vector<int> itemIDs;
    GetContractItemIDs(c.contractID, &itemIDs);
    for (auto itemID : itemIDs) {
        if (itemID == 0)
            continue;
        InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
        if (iRef.get() == nullptr)
            continue;
        ChangeOwnerRecursive(iRef, itemOwnerID);
        if (moveToStation or (iRef->locationID() != c.crateID))
            if ((iRef->locationID() != locationID) or (iRef->flag() != flag))
                iRef->Move(locationID, flag, true);
    }
}

void ContractUtils::ReturnItemsToIssuer(const ContractInfo& c) {
    uint32 itemOwnerID(c.issuerID), locationID(c.startStationID);
    EVEItemFlags flag(flagHangar);
    MarketMgr::GetDeliveryLocation(IssuerWallet(c), c.forCorp, c.issuerID, c.startStationID, itemOwnerID, locationID, flag);

    std::vector<int> itemIDs;
    GetContractItemIDs(c.contractID, &itemIDs);
    for (auto itemID : itemIDs) {
        if (itemID == 0)
            continue;
        InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
        if (iRef.get() == nullptr)
            continue;
        // only items still held by the contract
        if ((iRef->ownerID() != ownerSystem) or (iRef->locationID() != c.startStationID)) {
            _log(SERVICE__WARNING, "ContractUtils::ReturnItemsToIssuer() - item %u of contract %i is not held by the contract (owner %u, location %u).", \
                    itemID, c.contractID, iRef->ownerID(), iRef->locationID());
            continue;
        }
        ChangeOwnerRecursive(iRef, itemOwnerID);
        // corp contracts go back to the office's deliveries hangar
        if ((iRef->locationID() != locationID) or (iRef->flag() != flag))
            iRef->Move(locationID, flag, true);
    }
}

void ContractUtils::RefundReward(const ContractInfo& c) {
    if (c.reward < 1)
        return;
    // courier rewards were always taken at creation, also on rows from before the rewardEscrowed column
    if (!c.rewardEscrowed and (c.type != Contract::Type::Courier))
        return;
    AccountService::TransferFunds(ownerSystem, IssuerWallet(c), c.reward, "Contract reward refund", Journal::EntryType::ContractRewardRefund, c.contractID, Account::KeyType::Cash, IssuerKey(c));
}

ContractFees ContractUtils::CalcFees(int64 price, int64 reward, int32 type, int32 expireMinutes, uint8 brokerRelations, uint8 accounting, bool isPrivate)
{
    ContractFees fees = ContractFees();
    if (isPrivate) {
        // contracts.py shows exactly this for private contracts
        fees.brokersFee = 10000;
        return fees;
    }

    double base((double)std::max(price, reward));
    int32 numAdditionalDays((int32)(expireMinutes / 1440.0) - 1);
    double costMult((type == Contract::Type::Courier) ? 0.1 : 1.0);

    double tax((0.01 - accounting * 0.001) * costMult);
    double maxSalesTax(100000000.0 * (1.0 - accounting * 0.1));
    fees.salesTax = (int64)std::min(std::max(base * tax, 10000.0), maxSalesTax);

    double brokersFee((0.004 - brokerRelations * 0.0002) * costMult);
    double maxBrokersFee(10000000.0 * (1.0 - brokerRelations * 0.05));
    double brokersFeeAmt((double)(int64)std::min(std::max(base * brokersFee, 10000.0), maxBrokersFee));
    brokersFeeAmt *= 1.0 + numAdditionalDays * 0.05;
    fees.brokersFee = (int64)std::ceil(brokersFeeAmt - 0.000001);

    double depositMinimum((type == Contract::Type::Courier) ? 10000.0 : 100000.0);
    fees.deposit = (int64)std::min(std::max(base * 0.01 * costMult, depositMinimum), 10000000.0);
    return fees;
}

int64 ContractUtils::ReadISK(PyRep* rep)
{
    if (rep == nullptr)
        return -1;
    double value(0.0);
    if (rep->IsInt()) {
        value = rep->AsInt()->value();
    } else if (rep->IsLong()) {
        value = (double)rep->AsLong()->value();
    } else if (rep->IsFloat()) {
        value = rep->AsFloat()->value();
    } else if (rep->IsNone()) {
        return 0;
    } else {
        return -1;
    }
    // contractscommon.MAX_AMOUNT.  also false for NaN
    if (!((value >= 0.0) and (value <= 1000000000000.0)))
        return -1;
    return (int64)value;
}

void ContractUtils::RefundDeposit(ContractInfo& c, bool chargeSalesTax, int64 salesTaxBase)
{
    // once only.  the flag is set before any isk moves
    if (c.depositRefunded)
        return;
    DBerror err;
    uint32 affected(0);
    if (!sDatabase.RunQuery(err, affected, "UPDATE ctrContracts SET depositRefunded = 1 WHERE contractId = %i AND depositRefunded = 0", c.contractID)) {
        codelog(DATABASE__ERROR, "Failed to update contract %i: %s", c.contractID, err.c_str());
        return;
    }
    c.depositRefunded = true;
    if (affected == 0)
        return;

    uint32 walletID(IssuerWallet(c));
    uint16 walletKey(IssuerKey(c));
    int64 refund(c.deposit);

    // sales tax:  item exchanges and auctions, not private ones (contracts.py), from the issuer
    if (chargeSalesTax and sConfig.market.ContractFees and !c.isPrivate and (c.type != Contract::Type::Courier) and (salesTaxBase > 0)) {
        uint8 accounting(0);
        Client* pIssuer(sEntityList.FindClientByCharID(c.issuerID));
        if (pIssuer != nullptr) {
            accounting = pIssuer->GetChar()->GetSkillLevel(EvESkill::Accounting);
        } else {
            accounting = CharacterDB::GetSkillLevel(c.issuerID, EvESkill::Accounting);
        }
        int64 tax(CalcFees(salesTaxBase, 0, c.type, c.expireTimeInMinutes, 0, accounting, false).salesTax);
        if (tax > 0) {
            bool paid(false);
            if (MarketMgr::GetWalletBalance(walletID, walletKey) >= tax) {
                try {
                    AccountService::TransferFunds(walletID, corpSCC, tax, "Contract sales tax", Journal::EntryType::ContractSalesTax, c.contractID, walletKey);
                    paid = true;
                } catch (...) { }
            }
            if (!paid) {
                // the issuer can't pay it.  it comes out of the deposit
                int64 fromDeposit(std::min(tax, refund));
                if (fromDeposit > 0) {
                    AccountService::TransferFunds(ownerSystem, corpSCC, fromDeposit, "Contract sales tax", Journal::EntryType::ContractDepositSalesTax, c.contractID);
                    refund -= fromDeposit;
                }
            }
        }
    }

    if (refund > 0)
        AccountService::TransferFunds(ownerSystem, walletID, refund, "Contract deposit refund", Journal::EntryType::ContractDepositRefund, c.contractID, Account::KeyType::Cash, walletKey);
}

void ContractUtils::Notify(uint32 ownerID, const char* event, int32 contractID)
{
    if (ownerID == 0)
        return;

    std::vector<Client*> clients;
    if (IsCharacterID(ownerID)) {
        Client* pClient(sEntityList.FindClientByCharID(ownerID));
        if (pClient != nullptr)
            clients.push_back(pClient);
    } else if (IsPlayerCorp(ownerID)) {
        // corp contracts are handled by contract managers
        std::vector<Client*> members;
        sEntityList.GetCorpClients(members, ownerID);
        for (auto cur : members)
            if ((cur->GetCorpRole() & (Corp::Role::ContractManager | Corp::Role::Director)) != 0)
                clients.push_back(cur);
    } else if (IsAlliance(ownerID)) {
        std::vector<Client*> all;
        sEntityList.GetClients(all);
        for (auto cur : all)
            if ((uint32)cur->GetAllianceID() == ownerID)
                clients.push_back(cur);
    }

    for (auto cur : clients) {
        PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, new PyInt(contractID));
        cur->SendNotification(event, "charid", &tuple);
    }
}

void ContractUtils::FailCourier(ContractInfo& c)
{
    if ((c.type != Contract::Type::Courier) or (c.status != Contract::Status::InProgress))
        return;

    // mark it first, so nothing below runs twice
    if (!SetStatus(c.contractID, Contract::Status::Failed, 0, GetFileTimeNow()))
        return;
    c.status = Contract::Status::Failed;

    // collateral goes to the issuer, the reward and deposit back to the issuer
    if (c.collateral > 0)
        AccountService::TransferFunds(ownerSystem, IssuerWallet(c), c.collateral, "Contract collateral payout", Journal::EntryType::ContractCollateralPayout, c.contractID, Account::KeyType::Cash, IssuerKey(c));
    RefundReward(c);
    RefundDeposit(c, false, 0);

    // the acceptor paid for the goods with the collateral and keeps the package and its contents
    std::map<int, int> expectedItems;
    GetContractItemIDsAndQuantities(c.contractID, &expectedItems);
    for (const auto& entry : expectedItems) {
        InventoryItemRef item = sItemFactory.GetItemRef(entry.first);
        if ((item.get() != nullptr) and (item->ownerID() == ownerSystem))
            ChangeOwnerRecursive(item, c.acceptorID);
    }

    Notify(IssuerWallet(c), "OnContractFailed", c.contractID);
    Notify(c.acceptorID, "OnContractFailed", c.contractID);
}

void ContractUtils::OnCrateDeleted(uint32 crateID)
{
    // a delivered package's contract is already finished when its crate goes
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT contractId FROM ctrContracts WHERE crateID = %u AND status = %u", crateID, Contract::Status::InProgress)) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        ContractInfo c = ContractInfo();
        if (!GetContractInfo(row.GetInt(0), c))
            continue;
        _log(SERVICE__MESSAGE, "ContractUtils::OnCrateDeleted() - courier package %u of contract %i is gone.  The contract fails.", crateID, c.contractID);
        FailCourier(c);
    }
}

void ContractUtils::ProcessExpiry()
{
    int64 now(GetFileTimeNow());
    DBQueryResult res;
    DBResultRow row;

    // expired outstanding contracts (not auctions with bids) removed N days after expiry, as if the issuer deleted them
    if (sConfig.market.ContractExpiredCleanupDays > 0) {
        if (sDatabase.RunQuery(res,
            "SELECT contractId FROM ctrContracts"
            " WHERE status IN (%u, %u) AND dateExpired < %lli"
            "  AND contractId NOT IN (SELECT contractId FROM ctrBids WHERE refunded = 0)"
            " LIMIT 200",
            Contract::Status::Outstanding, Contract::Status::Rejected, now - (EvE::Time::Day * sConfig.market.ContractExpiredCleanupDays)))
        {
            while (res.GetRow(row)) {
                ContractInfo c = ContractInfo();
                if (!GetContractInfo(row.GetInt(0), c))
                    continue;
                if (!SetStatus(c.contractID, Contract::Status::Deleted))
                    continue;
                try {
                    ReturnItemsToIssuer(c);
                    RefundReward(c);
                    RefundDeposit(c, false, 0);
                } catch (...) {
                    _log(SERVICE__ERROR, "ContractUtils::ProcessExpiry() - cleaning up contract %i failed.", c.contractID);
                }
            }
        } else {
            codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        }
    }

    // couriers more than a day overdue fail by themselves
    if (sConfig.market.ContractCourierAutoFail) {
        if (sDatabase.RunQuery(res,
            "SELECT contractId FROM ctrContracts"
            " WHERE status = %u AND contractType = %u AND (dateAccepted + (numDays + 1) * %lli) < %lli LIMIT 200",
            Contract::Status::InProgress, Contract::Type::Courier, EvE::Time::Day, now))
        {
            while (res.GetRow(row)) {
                ContractInfo c = ContractInfo();
                if (!GetContractInfo(row.GetInt(0), c))
                    continue;
                try {
                    FailCourier(c);
                } catch (...) {
                    _log(SERVICE__ERROR, "ContractUtils::ProcessExpiry() - failing courier contract %i failed.", c.contractID);
                }
            }
        } else {
            codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        }
    }
}

bool ContractUtils::CanView(Client* pClient, const ContractInfo& c)
{
    if (!c.isPrivate)
        return true;
    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID()), allyID(pClient->GetAllianceID());
    if ((c.issuerID == charID) or (c.acceptorID == charID) or (c.assigneeID == charID))
        return true;
    if ((c.forCorp and (c.issuerCorpID == corpID)) or (c.assigneeID == corpID) or (c.acceptorID == corpID))
        return true;
    if ((allyID != 0) and (c.assigneeID == allyID))
        return true;
    // bidders on a private auction
    DBQueryResult res;
    if (sDatabase.RunQuery(res, "SELECT bidID FROM ctrBids WHERE contractId = %i AND bidderID IN (%u, %u) LIMIT 1", c.contractID, charID, corpID))
        if (res.GetRowCount() > 0)
            return true;
    return false;
}

bool ContractUtils::GetTopBid(int32 contractID, ContractBid& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT bidID, amount, bidderID, bidderCharID, forCorp FROM ctrBids"
        " WHERE contractId = %i AND refunded = 0 ORDER BY amount DESC, bidID ASC LIMIT 1", contractID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    into.bidID          = row.GetInt(0);
    into.amount         = row.GetInt64(1);
    into.bidderID       = row.GetUInt(2);
    into.bidderCharID   = row.GetUInt(3);
    into.forCorp        = (row.GetInt(4) != 0);
    return true;
}

bool ContractUtils::HasBids(int32 contractID)
{
    ContractBid bid = ContractBid();
    return GetTopBid(contractID, bid);
}

PyRep* ContractUtils::BuildContractList(const std::string& whereClause)
{
    DBQueryResult contractRes;
    if (!sDatabase.RunQuery(contractRes, (getContractQueryBase + whereClause + " ORDER BY contractId DESC LIMIT 1000").c_str())) {
        codelog(DATABASE__ERROR, "Error in query: %s", contractRes.error.c_str());
        return nullptr;
    }

    PyObjectEx* contracts = DBResultToCRowset(contractRes);
    std::string contractIDs;
    for (auto cur : contracts->list())
        contractIDs += std::to_string(PyRep::IntegerValue(cur->AsPackedRow()->GetField(0))) + ",";

    DBResultRow row;
    PyDict* items = new PyDict();
    PyDict* bids = new PyDict();
    if (!contractIDs.empty()) {
        contractIDs.pop_back();
        DBQueryResult res;
        // items per contract
        if (sDatabase.RunQuery(res, getContractItemsQueryBase.c_str(), contractIDs.c_str())) {
            DBRowDescriptor* header = new DBRowDescriptor(res);
            std::map<int, CRowSet*> byContract;
            while (res.GetRow(row)) {
                CRowSet*& rowset = byContract[row.GetInt(0)];
                if (rowset == nullptr) {
                    PyIncRef(header);
                    rowset = new CRowSet(&header);
                }
                FillItemData(&row, rowset->NewRow());
            }
            for (auto cur : byContract)
                items->SetItem(new PyInt(cur.first), cur.second);
            PyDecRef(header);
        } else {
            codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        }
        // bids per contract, highest first
        if (sDatabase.RunQuery(res, getContractBidsQueryBase.c_str(), contractIDs.c_str())) {
            DBRowDescriptor* header = new DBRowDescriptor(res);
            std::map<int, CRowSet*> byContract;
            while (res.GetRow(row)) {
                CRowSet*& rowset = byContract[row.GetInt(0)];
                if (rowset == nullptr) {
                    PyIncRef(header);
                    rowset = new CRowSet(&header);
                }
                FillBidData(&row, rowset->NewRow());
            }
            for (auto cur : byContract)
                bids->SetItem(new PyInt(cur.first), cur.second);
            PyDecRef(header);
        } else {
            codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        }
    }

    PyDict* ret = new PyDict();
    ret->SetItemString("contracts", contracts);
    ret->SetItemString("items", items);
    ret->SetItemString("bids", bids);
    return new PyObject("util.KeyVal", ret);
}
