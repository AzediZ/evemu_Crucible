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

#include "EVE_Corp.h"
#include "EVE_Mail.h"

#include "Client.h"
#include "EntityList.h"
#include "account/AccountService.h"
#include "alliance/AllianceDB.h"
#include "corporation/BillingMgr.h"
#include "corporation/CorpMgr.h"
#include "inventory/ItemFactory.h"
#include "inventory/ItemType.h"
#include "mail/NotificationMgr.h"
#include "system/sov/SovereigntyDataMgr.h"

/*
 * CORP__ERROR
 * CORP__WARNING
 * CORP__INFO
 */

namespace {
// alliance maintenance:  2,000,000 isk per member corporation, monthly (const.allianceMembershipCost)
const double AllianceMembershipCost = 2000000.0;
const int64 AllianceBillingPeriod = 30 * EvE::Time::Day;
// const.sovereignityBillingPeriod (days)
const int64 SovBillingPeriod = 14 * EvE::Time::Day;
// SovAllBillLate warning before a sovereignty bill is due
const int64 SovBillWarning = 3 * EvE::Time::Day;

const char* BILL_COLUMNS =
    "billID, billTypeID, debtorID, creditorID, amount, dueDateTime, interest, externalID, paid, externalID2, createdDateTime";

void RowToBill(DBResultRow& row, BillData& into)
{
    into.billID = row.GetUInt(0);
    into.billTypeID = row.GetUInt(1);
    into.debtorID = row.GetUInt(2);
    into.creditorID = row.GetUInt(3);
    into.amount = row.GetDouble(4);
    into.dueDateTime = row.GetInt64(5);
    into.interest = row.GetDouble(6);
    into.externalID = row.GetInt(7);
    into.paid = row.GetUInt(8);
    into.externalID2 = row.GetInt(9);
    into.createdDateTime = row.GetInt64(10);
}

void GetBills(const char* where, std::vector<BillData>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM bills WHERE %s", BILL_COLUMNS, where)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        BillData bill = BillData();
        RowToBill(row, bill);
        into.push_back(bill);
    }
}

uint8 EntryTypeFor(uint8 billTypeID)
{
    switch (billTypeID) {
        case Corp::BillType::RentalBill:                return Journal::EntryType::OfficeRentalFee;
        case Corp::BillType::AllianceMaintainanceBill:  return Journal::EntryType::AllianceMaintainanceFee;
        case Corp::BillType::WarBill:                   return Journal::EntryType::WarFee;
        case Corp::BillType::MarketFine:                return Journal::EntryType::MarketFinePaid;
        case Corp::BillType::SovereigntyMarker:         return Journal::EntryType::SovereignityUpkeepAdjustment;
    }
    return Journal::EntryType::Undefined;
}

// crpAutoPay column for a bill type
const char* AutoPayColumn(uint8 billTypeID)
{
    switch (billTypeID) {
        case Corp::BillType::MarketFine:                return "market";
        case Corp::BillType::RentalBill:                return "rental";
        case Corp::BillType::BrokerBill:                return "broker";
        case Corp::BillType::WarBill:                   return "war";
        case Corp::BillType::AllianceMaintainanceBill:  return "alliance";
        case Corp::BillType::SovereigntyMarker:         return "sov";
    }
    return nullptr;
}
}

BillingMgr::BillingMgr()
{
    m_handlers.clear();
    m_outOfMoney.clear();
    m_sovWarned.clear();
}

int BillingMgr::Initialize()
{
    sLog.Blue("       BillingMgr", "Billing Manager Initialized.");
    return 1;
}

void BillingMgr::RegisterHandlers(uint8 billTypeID, BillHandler onPaid, BillHandler onOverdue)
{
    m_handlers[billTypeID] = std::make_pair(onPaid, onOverdue);
}

uint32 BillingMgr::PayingCorpFor(uint32 debtorID)
{
    if (IsPlayerCorp(debtorID))
        return debtorID;
    if (IsAlliance(debtorID))
        return AllianceDB::GetExecutorID(debtorID);
    return 0;
}

bool BillingMgr::GetBill(uint32 billID, BillData& into)
{
    std::vector<BillData> bills;
    std::string where = "billID = ";
    where += std::to_string(billID);
    GetBills(where.c_str(), bills);
    if (bills.empty())
        return false;
    into = bills.front();
    return true;
}

PyRep* BillingMgr::GetBillsPayable(uint32 debtorID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT billID, billTypeID, debtorID, creditorID, amount, dueDateTime, interest, externalID, paid, externalID2"
        " FROM bills WHERE debtorID = %u AND paid = 0 ORDER BY dueDateTime", debtorID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return new PyList();
    }
    return DBResultToCRowset(res);
}

PyRep* BillingMgr::GetBillsReceivable(uint32 creditorID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT billID, billTypeID, debtorID, creditorID, amount, dueDateTime, interest, externalID, paid, externalID2"
        " FROM bills WHERE creditorID = %u AND paid = 0 ORDER BY dueDateTime", creditorID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return new PyList();
    }
    return DBResultToCRowset(res);
}

uint32 BillingMgr::CreateBill(uint8 billTypeID, uint32 debtorID, uint32 creditorID, double amount, int64 dueDateTime,
                              int32 externalID/*-1*/, int32 externalID2/*-1*/)
{
    BillData bill = BillData();
        bill.billTypeID = billTypeID;
        bill.debtorID = debtorID;
        bill.creditorID = creditorID;
        bill.amount = amount;
        bill.dueDateTime = dueDateTime;
        bill.createdDateTime = GetFileTimeNow();
        bill.externalID = externalID;
        bill.externalID2 = externalID2;

    DBerror err;
    if (!sDatabase.RunQueryLID(err, bill.billID,
        "INSERT INTO bills (billTypeID, debtorID, creditorID, amount, interest, dueDateTime, createdDateTime, externalID, externalID2, paid)"
        " VALUES (%u, %u, %u, %f, 0, %lli, %lli, %i, %i, 0)",
        billTypeID, debtorID, creditorID, amount, dueDateTime, bill.createdDateTime, externalID, externalID2))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
        return 0;
    }

    NotifyIssued(bill);
    _log(CORP__INFO, "BillingMgr::CreateBill() - bill %u type %u of %.2f from %u to %u due %lli",
         bill.billID, billTypeID, amount, debtorID, creditorID, dueDateTime);
    return bill.billID;
}

void BillingMgr::SendBillReceived(uint32 debtorID)
{
    // wallet.OnBillReceived():  blinks the wallet and reloads the bills
    if (IsCharacterID(debtorID)) {
        Client* pClient = sEntityList.FindClientByCharID(debtorID);
        if (pClient != nullptr) {
            PyTuple* payload = new PyTuple(0);
            pClient->SendNotification("OnBillReceived", "charid", &payload);
        }
        return;
    }

    uint32 corpID = PayingCorpFor(debtorID);
    if (corpID == 0)
        return;
    std::vector<Client*> list;
    sEntityList.GetCorpClients(list, corpID);
    for (auto cur : list) {
        if (!CorpMgr::HasRole(cur->GetCorpRole(), Corp::Role::Accountant) and !CorpMgr::HasRole(cur->GetCorpRole(), Corp::Role::JuniorAccountant))
            continue;
        PyTuple* payload = new PyTuple(0);
        cur->SendNotification("OnBillReceived", "clientID", &payload);
    }
}

void BillingMgr::NotifyIssued(const BillData& bill)
{
    // CharBill / CorpAllBill {billTypeID, debtorID, creditorID, amount, dueDate, currentDate, externalID, externalID2}
    PyDict* data = new PyDict();
        data->SetItemString("billTypeID", new PyInt(bill.billTypeID));
        data->SetItemString("debtorID", new PyInt(bill.debtorID));
        data->SetItemString("creditorID", new PyInt(bill.creditorID));
        data->SetItemString("amount", new PyFloat(bill.amount));
        data->SetItemString("dueDate", new PyLong(bill.dueDateTime));
        data->SetItemString("currentDate", new PyLong(bill.createdDateTime));
        data->SetItemString("externalID", new PyInt(bill.externalID));
        data->SetItemString("externalID2", new PyInt(bill.externalID2));

    if (IsCharacterID(bill.debtorID)) {
        sCorpMgr.NotifyChar(bill.debtorID, Notify::Types::CharBill, bill.creditorID, data);
    } else {
        uint32 corpID = PayingCorpFor(bill.debtorID);
        if (corpID == 0) {
            PyDecRef(data);
            return;
        }
        sCorpMgr.NotifyCorp(corpID, Notify::Types::CorpAllBill, bill.creditorID, data, Corp::Role::Accountant | Corp::Role::JuniorAccountant);
    }

    SendBillReceived(bill.debtorID);
}

void BillingMgr::NotifyPaid(const BillData& bill, uint32 payerID)
{
    // BillPaidChar / BillPaidCorpAll {amount, dueDate} go to the creditor, from the payer:
    //  "A debt of {amount} owed to you by the sender of this message, due {dueDate} has been paid"  (npc creditors get nothing)
    PyDict* data = new PyDict();
        data->SetItemString("amount", new PyFloat(bill.amount));
        data->SetItemString("dueDate", new PyLong(bill.dueDateTime));
    if (IsCharacterID(bill.creditorID)) {
        sCorpMgr.NotifyChar(bill.creditorID, Notify::Types::BillPaidChar, payerID, data);
    } else {
        // NotifyCorp drops it for npc corps
        sCorpMgr.NotifyCorp(PayingCorpFor(bill.creditorID), Notify::Types::BillPaidCorpAll, payerID, data, Corp::Role::Accountant | Corp::Role::JuniorAccountant);
    }
    SendBillReceived(bill.debtorID);
}

void BillingMgr::Settle(const BillData& bill, uint32 payerID, uint16 fromKey)
{
    std::string reason = "Bill payment";
    // throws NotEnoughMoney / NotEnoughMoneyCorp
    AccountService::TransferFunds(payerID, bill.creditorID, bill.amount, reason, EntryTypeFor(bill.billTypeID), bill.billID, fromKey);

    DBerror err;
    sDatabase.RunQuery(err, "UPDATE bills SET paid = 1, paidDateTime = %lli WHERE billID = %u", GetFileTimeNow(), bill.billID);
    m_outOfMoney.erase(bill.billID);

    std::map<uint8, std::pair<BillHandler, BillHandler>>::iterator itr = m_handlers.find(bill.billTypeID);
    if ((itr != m_handlers.end()) and itr->second.first)
        itr->second.first(bill);

    NotifyPaid(bill, payerID);
    _log(CORP__INFO, "BillingMgr::Settle() - bill %u of %.2f paid by %u", bill.billID, bill.amount, payerID);
}

void BillingMgr::PayBill(Client* pClient, uint32 billID, uint16 fromAccountKey)
{
    BillData bill = BillData();
    if (!GetBill(billID, bill))
        throw CustomError("That bill could not be found.");
    if (bill.paid != 0)
        throw CustomError("That bill has already been settled.");

    if (IsCharacterID(bill.debtorID)) {
        if (bill.debtorID != pClient->GetCharacterID())
            throw CustomError("That is not your bill.");
        Settle(bill, bill.debtorID, Account::KeyType::Cash);
        return;
    }

    // corp and alliance bills:  an accountant of the paying corp (the executor for an alliance)
    uint32 corpID = PayingCorpFor(bill.debtorID);
    if ((corpID == 0) or ((uint32)pClient->GetCorporationID() != corpID))
        throw CustomError("Your corporation can not pay this bill.");
    if (!CorpMgr::HasRole(pClient->GetCorpRole(), Corp::Role::Accountant))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You must be an accountant to pay corporation bills."));
    if ((fromAccountKey < Account::KeyType::Cash) or (fromAccountKey > Account::KeyType::Cash7))
        fromAccountKey = Account::KeyType::Cash;

    Settle(bill, corpID, fromAccountKey);
}

bool BillingMgr::TryAutoPay(const BillData& bill)
{
    uint32 corpID = PayingCorpFor(bill.debtorID);
    const char* column = AutoPayColumn(bill.billTypeID);
    if ((corpID == 0) or (column == nullptr))
        return false;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s, divisionID FROM crpAutoPay WHERE corporationID = %u", column, corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row) or row.IsNull(0) or (row.GetInt(0) == 0))
        return false;
    uint16 key = row.GetUInt(1);
    if ((key < Account::KeyType::Cash) or (key > Account::KeyType::Cash7))
        key = Account::KeyType::Cash;

    try {
        Settle(bill, corpID, key);
        return true;
    } catch (...) {
        // "{billType} could not be paid automatically" once;  the bill goes overdue unless someone pays it
        if (m_outOfMoney.insert(bill.billID).second) {
            PyDict* data = new PyDict();
                data->SetItemString("billTypeID", new PyInt(bill.billTypeID));
                data->SetItemString("dueDate", new PyLong(bill.dueDateTime));
            sCorpMgr.NotifyCorp(corpID, Notify::Types::BillOutOfMoney, bill.creditorID, data, Corp::Role::Accountant | Corp::Role::JuniorAccountant);
        }
        _log(CORP__WARNING, "BillingMgr::TryAutoPay() - corp %u could not pay bill %u of %.2f", corpID, bill.billID, bill.amount);
    }
    return false;
}

void BillingMgr::CancelBill(uint32 billID)
{
    if (billID == 0)
        return;
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE bills SET paid = 2 WHERE billID = %u AND paid = 0", billID);
    m_outOfMoney.erase(billID);
}

void BillingMgr::CancelBillsOf(uint32 debtorID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE bills SET paid = 2 WHERE debtorID = %u AND paid = 0", debtorID);
}

void BillingMgr::Process()
{
    if (!sConfig.corp.BillsEnabled)
        return;

    int64 now = GetFileTimeNow();

    // auto-pay a day ahead of the due date
    std::vector<BillData> bills;
    std::string where = "paid = 0 AND dueDateTime <= ";
    where += std::to_string(now + EvE::Time::Day);
    GetBills(where.c_str(), bills);
    for (auto cur : bills)
        TryAutoPay(cur);

    // overdue:  cancelled, and whatever the bill type does about it
    bills.clear();
    where = "paid = 0 AND dueDateTime <= ";
    where += std::to_string(now - (int64)sConfig.corp.BillGraceDays * EvE::Time::Day);
    GetBills(where.c_str(), bills);
    for (auto cur : bills) {
        CancelBill(cur.billID);
        _log(CORP__INFO, "BillingMgr::Process() - bill %u (type %u) of %u is overdue", cur.billID, cur.billTypeID, cur.debtorID);
        std::map<uint8, std::pair<BillHandler, BillHandler>>::iterator itr = m_handlers.find(cur.billTypeID);
        if ((itr != m_handlers.end()) and itr->second.second)
            itr->second.second(cur);
    }

    ProcessAllianceBills(now);
    ProcessSovBills(now);
}

double BillingMgr::GetSovDailyCost(const SovereigntyData& claim)
{
    // "The sum of this attribute on the claim markers, Infrastructure hub, and each upgrade is the systems base cost" (daily upkeep)
    double daily(0);
    const ItemType* pType(sItemFactory.GetType(EVEDB::invTypes::FixedLinkAnnexationGenerator));
    if (pType != nullptr)
        daily += pType->GetAttribute(AttrsovBillSystemCost).get_double();
    if (claim.hubID == 0)
        return daily;
    pType = sItemFactory.GetType(EVEDB::invTypes::InfrastructureHub);
    if (pType != nullptr)
        daily += pType->GetAttribute(AttrsovBillSystemCost).get_double();

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT typeID FROM entity WHERE locationID = %u", claim.hubID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return daily;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        pType = sItemFactory.GetType(row.GetUInt(0));
        if (pType != nullptr)
            daily += pType->GetAttribute(AttrsovBillSystemCost).get_double();
    }
    return daily;
}

int64 BillingMgr::GetSovPeriodEnd(int64 claimTime, int64 now)
{
    // billing cycles of const.sovereignityBillingPeriod days from the claim (sovSvc.CalculateUpgradeCost)
    if (now < claimTime)
        return claimTime + SovBillingPeriod;
    return claimTime + ((now - claimTime) / SovBillingPeriod + 1) * SovBillingPeriod;
}

void BillingMgr::ChargeUpgradeInstall(Client* pClient, uint32 systemID, uint16 typeID)
{
    // SovConfirmUpgradeDropWithCharge:  "{amount} will be deducted from the master division of your corporation wallet to cover the
    //  upkeep cost for the remaining {days} days of the current" billing period
    SovereigntyData claim = svDataMgr.GetSovereigntyData(systemID);
    const ItemType* pType(sItemFactory.GetType(typeID));
    if ((claim.claimID == 0) or (pType == nullptr))
        return;
    double daily = pType->GetAttribute(AttrsovBillSystemCost).get_double();
    if (daily <= 0)
        return;
    int64 now = GetFileTimeNow();
    int64 daysHeld = (now - claim.claimTime) / EvE::Time::Day;
    int64 daysLeft = (SovBillingPeriod / EvE::Time::Day) - (daysHeld % (SovBillingPeriod / EvE::Time::Day));

    std::string reason = "Sovereignty upkeep for ";
    reason += pType->name();
    // throws NotEnoughMoneyCorp
    AccountService::TransferFunds(pClient->GetCorporationID(), corpCONCORD, daily * daysLeft, reason,
                                  Journal::EntryType::SovereignityUpkeepAdjustment, systemID, Account::KeyType::Cash);
}

void BillingMgr::ProcessSovBills(int64 now)
{
    // one bill per claimed system and billing period, due at the end of the period it covers
    std::vector<SovereigntyData> claims;
    svDataMgr.GetClaims(claims);
    std::map<uint32, const SovereigntyData*> bySystem;
    for (auto& cur : claims)
        if (cur.allianceID != 0)
            bySystem[cur.solarSystemID] = &cur;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT solarSystemID, claimID, nextBillDateTime FROM sovBilling")) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    std::map<uint32, std::pair<uint32, int64>> billing;
    DBResultRow row;
    while (res.GetRow(row))
        billing[row.GetUInt(0)] = std::make_pair(row.GetUInt(1), row.GetInt64(2));

    DBerror err;
    // claims that are gone take their unpaid bills with them
    for (auto cur : billing) {
        std::map<uint32, const SovereigntyData*>::iterator itr = bySystem.find(cur.first);
        if ((itr != bySystem.end()) and (itr->second->claimID == cur.second.first))
            continue;
        sDatabase.RunQuery(err, "UPDATE bills SET paid = 2 WHERE billTypeID = %u AND externalID2 = %u AND paid = 0",
                           Corp::BillType::SovereigntyMarker, cur.first);
        sDatabase.RunQuery(err, "DELETE FROM sovBilling WHERE solarSystemID = %u", cur.first);
    }

    for (auto cur : bySystem) {
        const SovereigntyData& claim = *cur.second;
        std::map<uint32, std::pair<uint32, int64>>::iterator itr = billing.find(cur.first);
        if ((itr != billing.end()) and (itr->second.first == claim.claimID) and (itr->second.second > now))
            continue;

        int64 due = GetSovPeriodEnd(claim.claimTime, now);
        double amount = GetSovDailyCost(claim) * (SovBillingPeriod / EvE::Time::Day);
        if (amount > 0)
            CreateBill(Corp::BillType::SovereigntyMarker, claim.allianceID, corpCONCORD, amount, due, -1, claim.solarSystemID);
        sDatabase.RunQuery(err, "REPLACE INTO sovBilling (solarSystemID, claimID, nextBillDateTime) VALUES (%u, %u, %lli)",
                           claim.solarSystemID, claim.claimID, due);
    }

    // "Bill for {system} almost due":  SovAllBillLate {corpID: executor, solarSystemID, dueInDays} to the alliance's accountants,
    //  once per bill, in the last days before it is due (an unpaid bill costs the claim)
    std::vector<BillData> bills;
    std::string where = "paid = 0 AND billTypeID = ";
    where += std::to_string(Corp::BillType::SovereigntyMarker);
    where += " AND dueDateTime > " + std::to_string(now);
    where += " AND dueDateTime <= " + std::to_string(now + SovBillWarning);
    GetBills(where.c_str(), bills);
    for (auto& cur : bills) {
        if (!m_sovWarned.insert(cur.billID).second)
            continue;
        PyDict* data = new PyDict();
            data->SetItemString("corpID", new PyInt(AllianceDB::GetExecutorID(cur.debtorID)));
            data->SetItemString("solarSystemID", new PyInt(cur.externalID2));
            data->SetItemString("dueInDays", new PyInt((int32)((cur.dueDateTime - now + EvE::Time::Day - 1) / EvE::Time::Day)));
        NotificationMgr::SendToAlliance(cur.debtorID, Corp::Role::Accountant | Corp::Role::JuniorAccountant, Notify::Types::SovAllBillLate, corpCONCORD, data);
    }
}

void BillingMgr::ProcessAllianceBills(int64 now)
{
    // every alliance gets a maintenance bill each 30 days, the first 30 days after it shows up here
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT a.allianceID, IFNULL(b.nextBillDateTime, 0),"
        " (SELECT COUNT(*) FROM crpCorporation c WHERE c.allianceID = a.allianceID AND c.deleted = 0)"
        " FROM alnAlliance a LEFT JOIN alnBilling b USING (allianceID) WHERE a.deleted = 0"))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBerror err;
    DBResultRow row;
    while (res.GetRow(row)) {
        uint32 allianceID = row.GetUInt(0);
        int64 next = row.GetInt64(1);
        uint32 corps = row.GetUInt(2);
        if (next == 0) {
            sDatabase.RunQuery(err, "REPLACE INTO alnBilling (allianceID, nextBillDateTime) VALUES (%u, %lli)", allianceID, now + AllianceBillingPeriod);
            continue;
        }
        if (next > now)
            continue;

        sDatabase.RunQuery(err, "UPDATE alnBilling SET nextBillDateTime = %lli WHERE allianceID = %u", now + AllianceBillingPeriod, allianceID);
        if (corps == 0)
            continue;
        CreateBill(Corp::BillType::AllianceMaintainanceBill, allianceID, corpCONCORD, AllianceMembershipCost * corps,
                   now + AllianceBillingPeriod, allianceID);
    }
}
