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

#include "account/AccountDB.h"

/*
 * ACCOUNT__ERROR
 * ACCOUNT__WARNING
 * ACCOUNT__INFO
 * ACCOUNT__MESSAGE
 * ACCOUNT__TRACE
 * ACCOUNT__CALL
 * ACCOUNT__CALL_DUMP
 * ACCOUNT__RSP_DUMP
 * ACCOUNT__DB_ERROR
 * ACCOUNT__DB_WARNING
 * ACCOUNT__DB_INFO
 * ACCOUNT__DB_MESSAGE
 */

double AccountDB::OfflineFundXfer(uint32 charID, double amount, uint8 type)
{
    std::string from = "";
    switch (type) {
        case Account::CreditType::ISK:      from = "balance";    break;
        case Account::CreditType::AURUM:    from = "aurBalance"; break;
        case Account::CreditType::MPLEX:    return 0; //make error..this isnt used yet
    }

    double balance = 0;
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT %s FROM chrCharacters WHERE characterID = %u", from.c_str(), charID);
    DBResultRow row;
    if (res.GetRow(row))
        balance = row.GetDouble(0);

    balance += amount;
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE chrCharacters SET %s = %f WHERE characterID = %u", from.c_str(), balance, charID);
    return balance;
}


double AccountDB::GetCharBalance(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT balance FROM chrCharacters WHERE characterID = %u", charID)) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetDouble(0);
}

double AccountDB::GetCorpBalance(uint32 corpID, uint16 accountKey)
{
    std::string acctKey = "";
    switch (accountKey) {
        case Account::KeyType::Cash:    acctKey = "balance1"; break;
        case Account::KeyType::Cash2:   acctKey = "balance2"; break;
        case Account::KeyType::Cash3:   acctKey = "balance3"; break;
        case Account::KeyType::Cash4:   acctKey = "balance4"; break;
        case Account::KeyType::Cash5:   acctKey = "balance5"; break;
        case Account::KeyType::Cash6:   acctKey = "balance6"; break;
        case Account::KeyType::Cash7:   acctKey = "balance7"; break;
    }
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM crpWalletDivisons WHERE corporationID = %u ", acctKey.c_str(), corpID)) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetDouble(0);
}

void AccountDB::UpdateCorpBalance(uint32 corpID, uint16 accountKey, double amount)
{
    std::string acctKey = "";
    switch (accountKey) {
        case Account::KeyType::Cash:    acctKey = "balance1"; break;
        case Account::KeyType::Cash2:   acctKey = "balance2"; break;
        case Account::KeyType::Cash3:   acctKey = "balance3"; break;
        case Account::KeyType::Cash4:   acctKey = "balance4"; break;
        case Account::KeyType::Cash5:   acctKey = "balance5"; break;
        case Account::KeyType::Cash6:   acctKey = "balance6"; break;
        case Account::KeyType::Cash7:   acctKey = "balance7"; break;
    }
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE crpWalletDivisons SET %s = %.2f WHERE corporationID = %u ", acctKey.c_str(), amount, corpID);
}


PyRep *AccountDB::GetWalletDivisionsInfo(uint32 corpID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT balance1, balance2, balance3, balance4, balance5, balance6,  balance7"
        " FROM crpWalletDivisons"
        " WHERE corporationID = %u", corpID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    DBResultRow row;
    PyList *list = new PyList();
    if (res.GetRow(row))
        for (int8 i = 0; i < 7; ++i) {
            PyDict *dict = new PyDict();
            dict->SetItemString("key", new PyInt(1000 + i));
            dict->SetItemString("balance", new PyFloat(row.GetDouble(i)));
            list->AddItem(new PyObject("util.KeyVal", dict));
        }

    if (is_log_enabled(ACCOUNT__RSP_DUMP))
        list->Dump(ACCOUNT__RSP_DUMP, "    ");
    return list;
}

PyRep* AccountDB::GetJournal(uint32 ownerID, int8 entryTypeID, uint16 accountKey, int64 fromDate, bool reverse/*false*/)
{
    std::string tblName = "jnlCharacters";
    if (IsCorp(ownerID))
        tblName = "jnlCorporations";

    std::string entryType = "";
    if (entryTypeID) {
        entryType = " AND entryTypeID = ";
        entryType += std::to_string(entryTypeID);
    }

    std::string sort = "";
    if (reverse)
        sort = "";

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT transactionID, transactionDate, referenceID, entryTypeID, ownerID1, ownerID2, accountKey, amount,"
        " balance, description, currency, 1 AS sortValue"
        " FROM %s"
        " WHERE transactionDate > %lli AND accountKey = %u %s AND ownerID = %u %s",
        tblName.c_str(), fromDate, accountKey, entryType.c_str(), ownerID, sort.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* AccountDB::GetJournal(uint32 ownerID, int8 entryTypeID, const std::vector<uint16>& accountKeys, int64 fromDate, bool reverse/*false*/)
{
    std::string tblName = "jnlCharacters";
    if (IsCorp(ownerID))
        tblName = "jnlCorporations";

    std::ostringstream keys;
    keys << "0";
    for (auto cur : accountKeys)
        keys << "," << (cur == 0 ? Account::KeyType::Cash : cur);

    std::string entryType = "";
    if (entryTypeID) {
        entryType = " AND entryTypeID = ";
        entryType += std::to_string(entryTypeID);
    }

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT transactionID, transactionDate, referenceID, entryTypeID, ownerID1, ownerID2, accountKey, amount,"
        " balance, description, currency, 1 AS sortValue"
        " FROM %s"
        " WHERE transactionDate > %lli AND accountKey IN (%s) %s AND ownerID = %u"
        " ORDER BY transactionID %s",
        tblName.c_str(), fromDate, keys.str().c_str(), entryType.c_str(), ownerID, (reverse ? "DESC" : "ASC")))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* AccountDB::GetRedeemTokens(uint32 accountID)
{
    int64 now((int64)GetFileTimeNow());
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT tokenID, massTokenID, typeID, quantity, label, description, dateTime, expireDateTime, availableDateTime, stationID"
        " FROM accRedeemTokens"
        " WHERE accountID = %u AND claimedBy IS NULL"
        " AND (availableDateTime IS NULL OR availableDateTime <= %lli)"
        " AND (expireDateTime IS NULL OR expireDateTime > %lli)"
        " ORDER BY tokenID", accountID, now, now))
    {
        codelog(DATABASE__ERROR, "Error in GetRedeemTokens query: %s", res.error.c_str());
        return new PyList();
    }
    return DBResultToCRowset(res);
}

bool AccountDB::GetRedeemToken(uint32 tokenID, RedeemToken& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT tokenID, accountID, typeID, quantity, COALESCE(stationID, 0), COALESCE(expireDateTime, 0),"
        " COALESCE(availableDateTime, 0), claimedBy IS NOT NULL"
        " FROM accRedeemTokens WHERE tokenID = %u", tokenID))
    {
        codelog(DATABASE__ERROR, "Error in GetRedeemToken query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    into.tokenID = row.GetUInt(0);
    into.accountID = row.GetUInt(1);
    into.typeID = row.GetUInt(2);
    into.quantity = row.GetUInt(3);
    into.stationID = row.GetUInt(4);
    into.expireDateTime = row.GetInt64(5);
    into.availableDateTime = row.GetInt64(6);
    into.claimed = (row.GetInt(7) != 0);
    return true;
}

bool AccountDB::ClaimRedeemToken(uint32 tokenID, uint32 charID)
{
    DBerror err;
    uint32 affected(0);
    if (!sDatabase.RunQuery(err, affected,
        "UPDATE accRedeemTokens SET claimedBy = %u, claimedDateTime = %lli WHERE tokenID = %u AND claimedBy IS NULL",
        charID, (int64)GetFileTimeNow(), tokenID))
        return false;
    return (affected > 0);
}

uint32 AccountDB::CreateRedeemToken(uint32 accountID, uint32 typeID, uint32 quantity, const std::string& label,
                                    const std::string& description, uint32 stationID/*0*/, int64 expireDateTime/*0*/)
{
    std::string eLabel, eDesc;
    sDatabase.DoEscapeString(eLabel, label);
    sDatabase.DoEscapeString(eDesc, description);
    std::string station(stationID ? std::to_string(stationID) : "NULL");
    std::string expire(expireDateTime ? std::to_string(expireDateTime) : "NULL");

    DBerror err;
    uint32 tokenID(0);
    if (!sDatabase.RunQueryLID(err, tokenID,
        "INSERT INTO accRedeemTokens (accountID, typeID, quantity, label, description, dateTime, expireDateTime, stationID)"
        " VALUES (%u, %u, %u, '%s', '%s', %lli, %s, %s)",
        accountID, typeID, quantity, eLabel.c_str(), eDesc.c_str(), (int64)GetFileTimeNow(), expire.c_str(), station.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in CreateRedeemToken query: %s", err.c_str());
        return 0;
    }
    return tokenID;
}

void AccountDB::AddUserReport(uint32 reporterID, uint32 reportedID, uint8 reportType, const std::string& channel, const std::string& entries)
{
    std::string eChannel, eEntries;
    sDatabase.DoEscapeString(eChannel, channel);
    sDatabase.DoEscapeString(eEntries, entries);
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO accUserReports (reporterID, reportedID, reportType, channel, entries, reportDateTime)"
        " VALUES (%u, %u, %u, '%s', '%s', %lli)",
        reporterID, reportedID, reportType, eChannel.c_str(), eEntries.c_str(), (int64)GetFileTimeNow());
}

int64 AccountDB::GetPaidUntil(uint32 accountID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT paidUntil FROM account WHERE accountID = %u", accountID))
        return 0;
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return row.GetInt64(0);
}

void AccountDB::SetPaidUntil(uint32 accountID, int64 paidUntil)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE account SET paidUntil = %lli WHERE accountID = %u", paidUntil, accountID);
}

int32 AccountDB::GetContactCost(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT contactCost FROM chrCharacters WHERE characterID = %u", charID))
        return 0;
    DBResultRow row;
    if (!res.GetRow(row))
        return 0;
    return (row.IsNull(0) ? -1 : row.GetInt(0));
}

void AccountDB::SetContactCost(uint32 charID, int32 cost)
{
    DBerror err;
    if (cost < 0) {
        sDatabase.RunQuery(err, "UPDATE chrCharacters SET contactCost = NULL WHERE characterID = %u", charID);
    } else {
        sDatabase.RunQuery(err, "UPDATE chrCharacters SET contactCost = %i WHERE characterID = %u", cost, charID);
    }
}

void AccountDB::AddJournalEntry(uint32 ownerID, int8 entryTypeID, uint32 ownerFromID, uint32 ownerToID, int8 currency, \
                                uint16 accountKey, double amount, double newBalance, std::string description, uint32 referenceID/*0*/ )
{
    if (entryTypeID == Journal::EntryType::SkipLog)
        return;
    // account key 0 is usually sent by the client, it should be the main cash account
    if (accountKey == 0)
        accountKey = Account::KeyType::Cash;

    std::string eDesc;
    sDatabase.DoEscapeString(eDesc, description);

    std::string tblName = "jnlCharacters";
    if (IsCorp(ownerID))
        tblName = "jnlCorporations";

    // dont care if this fails...
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO %s (ownerID, entryTypeID, accountKey, transactionDate, ownerID1, ownerID2, referenceID, currency, amount, balance, description)"
        " VALUES (%u,%u,%u,%f,%u,%u,%i,%i,%.2f,%.2f,'%s')",
        // referenceID is signed:  single npc bounties store -typeID (the client shows the type name)
        tblName.c_str(), ownerID, entryTypeID, accountKey, GetFileTimeNow(), ownerFromID, ownerToID, (int32)referenceID, currency, amount, newBalance, eDesc.c_str());
}
