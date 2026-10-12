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
#ifndef EVE_ACCOUNT_DB_H
#define EVE_ACCOUNT_DB_H

#include "ServiceDB.h"


class AccountDB
{
public:
    PyRep* GetWalletDivisionsInfo(uint32 corpID);

    PyRep* GetJournal(uint32 ownerID, int8 entryTypeID, uint16 accountKey, int64 fromDate, bool reverse = false);
    // several account keys at once (GetJournalForAccounts)
    static PyRep* GetJournal(uint32 ownerID, int8 entryTypeID, const std::vector<uint16>& accountKeys, int64 fromDate, bool reverse = false);

    /* redeem tokens (accRedeemTokens).  stationID 0 = anywhere, expire 0 = never */
    struct RedeemToken {
        uint32 tokenID;
        uint32 accountID;
        uint32 typeID;
        uint32 quantity;
        uint32 stationID;
        int64 expireDateTime;
        int64 availableDateTime;
        bool claimed;
    };
    // the account's unclaimed, available and unexpired tokens:  CRowset tokenID, massTokenID, typeID, quantity, label,
    //  description, dateTime, expireDateTime, availableDateTime, stationID
    static PyRep* GetRedeemTokens(uint32 accountID);
    static bool GetRedeemToken(uint32 tokenID, RedeemToken& into);
    // marks it claimed; false if it was claimed meanwhile
    static bool ClaimRedeemToken(uint32 tokenID, uint32 charID);
    static uint32 CreateRedeemToken(uint32 accountID, uint32 typeID, uint32 quantity, const std::string& label,
                                    const std::string& description, uint32 stationID = 0, int64 expireDateTime = 0);

    // player reports (accUserReports): 1 bot, 2 isk spammer
    static void AddUserReport(uint32 reporterID, uint32 reportedID, uint8 reportType, const std::string& channel, const std::string& entries);

    // paid game time (PLEX).  0 = none recorded
    static int64 GetPaidUntil(uint32 accountID);
    static void SetPaidUntil(uint32 accountID, int64 paidUntil);

    // CSPA charge a character asks of non-contacts.  -1 = only contacts may contact (NULL in the db)
    static int32 GetContactCost(uint32 charID);
    static void SetContactCost(uint32 charID, int32 cost);

    static double OfflineFundXfer(uint32 charID, double amount, uint8 type=Account::CreditType::ISK);
    static double GetCorpBalance(uint32 corpID, uint16 accountKey);
    // isk balance of a character from the db (use Client::GetBalance() for online characters)
    static double GetCharBalance(uint32 charID);
    static void UpdateCorpBalance(uint32 corpID, uint16 accountKey, double amount);

    static void AddJournalEntry(uint32 ownerID, int8 entryTypeID, uint32 ownerFromID, uint32 ownerToID, int8 currency, uint16 accountKey, \
                                double amount, double newBalance, std::string description, uint32 referenceID = 0);

};


#endif  // EVE_ACCOUNT_DB_H
