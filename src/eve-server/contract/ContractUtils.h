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

#ifndef __CONTRACT_UTILS_H__INCL__
#define __CONTRACT_UTILS_H__INCL__

#include "services/Callable.h"
#include "inventory/InventoryItem.h"

class Client;

namespace Contract {
    // appConst conStatus*
    namespace Status {
        enum {
            Outstanding         = 0,
            InProgress          = 1,
            FinishedIssuer      = 2,
            FinishedContractor  = 3,
            Finished            = 4,
            Cancelled           = 5,
            Rejected            = 6,
            Failed              = 7,
            Deleted             = 8,
            Reversed            = 9
        };
    }
    // appConst conType*
    namespace Type {
        enum {
            ItemExchange    = 1,
            Auction         = 2,
            Courier         = 3,
            Loan            = 4
        };
    }
}

// one ctrContracts row
struct ContractInfo {
    bool forCorp;
    bool isPrivate;
    bool rewardEscrowed;    // reward is held by ownerSystem (taken at creation)
    bool depositRefunded;
    int32 contractID;
    int32 type;
    int32 status;
    int32 numDays;
    int32 expireTimeInMinutes;
    int32 startStationDivision;
    int64 price;            // item exchange: price.  auction: starting bid
    int64 reward;
    int64 collateral;       // courier: collateral.  auction: buyout (0 = none)
    int64 brokersFee;
    int64 deposit;
    uint16 issuerWalletKey; // corp division of a corp contract
    uint16 acceptorWalletKey;
    uint32 issuerID;
    uint32 issuerCorpID;
    uint32 issuerAllianceID;
    uint32 assigneeID;
    uint32 acceptorID;
    uint32 startStationID;
    uint32 endStationID;
    uint32 startSolarSystemID;
    uint32 endSolarSystemID;
    uint32 startRegionID;
    uint32 crateID;
    int64 dateIssued;
    int64 dateExpired;
    int64 dateAccepted;
    int64 dateCompleted;
    float volume;
};

// one ctrBids row
struct ContractBid {
    bool forCorp;
    int32 bidID;
    int64 amount;
    uint32 bidderID;        // character, or the corp for corp bids
    uint32 bidderCharID;
};

// contractscommon.CalcContractFees
struct ContractFees {
    int64 brokersFee;
    int64 deposit;
    int64 salesTax;
};

/**
 * Wrapper class. Houses utility functions, used by ContractProxy and ContractMgr
 *
 * money held in escrow by contracts (reward and deposit from creation, collateral from acceptance, bids) is held by ownerSystem.
 *   anything that ends a contract (expiry, overdue courier, auctions...) must use these helpers to keep that consistent
 */
class ContractUtils {
public:
    static bool GetContractInfo(int32 contractID, ContractInfo& into);
    // sets status, and each of the other fields that is non-zero
    static bool SetStatus(int32 contractID, int32 status, int64 dateAccepted = 0, int64 dateCompleted = 0, uint32 acceptorID = 0, uint32 crateID = 0);
    // changes owner of iRef and everything inside it (assembled ships, containers)
    static void ChangeOwnerRecursive(InventoryItemRef iRef, uint32 newOwner);
    // gives the contract's items (still held by ownerSystem in the start station) back to the issuer
    //  (corp contracts: the deliveries hangar of the corp's office)
    static void ReturnItemsToIssuer(const ContractInfo& c);
    // refunds an escrowed reward to the issuer
    static void RefundReward(const ContractInfo& c);
    // gives the contract's items (or the courier package contents) to newOwner.  corp owners get them in their office's
    //  deliveries hangar of stationID (contents of a courier package stay in the package)
    static void GiveItemsTo(const ContractInfo& c, uint32 newOwner, bool ownerIsCorp, uint32 memberID, uint32 stationID, bool moveToStation);

    // wallet (owner, division key) of the issuer and the acceptor
    static uint32 IssuerWallet(const ContractInfo& c)   { return (c.forCorp ? c.issuerCorpID : c.issuerID); }
    static uint16 IssuerKey(const ContractInfo& c);
    static uint16 AcceptorKey(const ContractInfo& c);

    /* fees (contractscommon.CalcContractFees).  private contracts: 10000 broker's fee, no deposit, no sales tax */
    static ContractFees CalcFees(int64 price, int64 reward, int32 type, int32 expireMinutes, uint8 brokerRelations, uint8 accounting, bool isPrivate);
    // isk value the client sent (int, long or float).  -1 when invalid (negative, NaN, above MAX_AMOUNT)
    static int64 ReadISK(PyRep* rep);
    // the deposit (if any, once) back to the issuer.  chargeSalesTax takes the sales tax of salesTaxBase from the issuer
    //  (from the deposit when the issuer can't pay it)
    static void RefundDeposit(ContractInfo& c, bool chargeSalesTax, int64 salesTaxBase);

    // OnContractAssigned/Completed/Accepted/Rejected/Failed/Outbid/Buyout(contractID) to a character, or to the online
    //  members of a corp (contract managers) or alliance
    static void Notify(uint32 ownerID, const char* event, int32 contractID);
    // a courier that failed (declared failed, its crate destroyed or opened, or overdue):  collateral to the issuer,
    //  reward and deposit back, the goods stay with the acceptor
    static void FailCourier(ContractInfo& c);
    // InventoryItem::Delete() of a plastic wrap.  an in progress courier fails
    static void OnCrateDeleted(uint32 crateID);
    // ~5m.  optional expired contract cleanup and overdue courier failure (both off by default, Crucible was manual)
    static void ProcessExpiry();

    // can this client see the contract (public, a party, assignee, bidder)
    static bool CanView(Client* pClient, const ContractInfo& c);
    // highest bid not refunded.  false when there are none
    static bool GetTopBid(int32 contractID, ContractBid& into);
    static bool HasBids(int32 contractID);

    // KeyVal(contracts = CRowset, items = {contractID: CRowset}, bids = {contractID: CRowset, highest first}) of the
    //  contracts matching whereClause (" WHERE ...")
    static PyRep* BuildContractList(const std::string& whereClause);

    static PyResult GetContractEntry(int contractId);
    static PyList* GetContractEntries(std::vector<int> contractIDList);
    static PyResult GetContractListForOwner(PyInt* ownerID, PyInt* contractStatus, std::optional <PyInt*> contractType, std::optional <PyBool*> issuedToBy);
    static void GetContractItemIDs(int contractId, std::vector<int>* into);
    static void GetRequestedItems(int contractId, std::map<int, int>* into);
    static void GetContractItemIDsAndQuantities(int contractId, std::map<int, int>* into);
private:
    static void FillItemData(DBResultRow* itemRow, PyPackedRow* targetRow);
    static void FillBidData(DBResultRow* bidRow, PyPackedRow* targetRow);
};

#endif  // __CONTRACT_UTILS_H__INCL__

