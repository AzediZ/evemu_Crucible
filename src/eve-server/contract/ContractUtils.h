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
    int32 contractID;
    int32 type;
    int32 status;
    int32 price;
    int32 reward;
    int32 collateral;
    int32 numDays;
    uint32 issuerID;
    uint32 issuerCorpID;
    uint32 assigneeID;
    uint32 acceptorID;
    uint32 startStationID;
    uint32 endStationID;
    uint32 startSolarSystemID;
    uint32 endSolarSystemID;
    uint32 crateID;
    int64 dateExpired;
    int64 dateAccepted;
    float volume;
};

/**
 * Wrapper class. Houses utility functions, used by ContractProxy and ContractMgr
 *
 * money held in escrow by contracts (reward from creation, collateral from acceptance) is held by ownerSystem.
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
    static void ReturnItemsToIssuer(const ContractInfo& c);
    // refunds an escrowed reward to the issuer
    static void RefundReward(const ContractInfo& c);

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

