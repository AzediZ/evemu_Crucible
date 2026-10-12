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

/* Bills (the bills table) for characters, corporations and alliances:  issued with the client's bill notifications,
 *  paid by hand (billMgr / alliance moniker) or automatically a day before they are due (crpAutoPay), and handled per
 *  bill type when paid or overdue.  office rent (F21), alliance maintenance (F21), war bills (F22) and sovereignty
 *  bills plug in with RegisterHandlers.  the 'billMgr' service is the client's RPC front of this.
 */

#ifndef EVEMU_CORPORATION_BILLINGMGR_H_
#define EVEMU_CORPORATION_BILLINGMGR_H_

#include "../eve-server.h"

#include <functional>

class Client;
struct SovereigntyData;

struct BillData {
    uint32 billID = 0;
    uint8 billTypeID = 0;
    uint8 paid = 0;             // billUnpaid 0, billPaid 1, billCancelled 2
    uint32 debtorID = 0;
    uint32 creditorID = 0;
    int32 externalID = -1;      // rental:  typeID of the rented thing (27 office), alliance:  allianceID, war:  against
    int32 externalID2 = -1;     // rental:  stationID, sovereignty:  solarSystemID
    double amount = 0;
    double interest = 0;
    int64 dueDateTime = 0;
    int64 createdDateTime = 0;
};

typedef std::function<void(const BillData&)> BillHandler;

class BillingMgr
: public Singleton<BillingMgr>
{
public:
    BillingMgr();

    int Initialize();
    // every ~5 minutes:  auto-pay bills due within a day, overdue bills, alliance maintenance bills
    void Process();

    // persists the bill and tells the debtor (or the corp paying for it).  returns billID (0 = failed)
    uint32 CreateBill(uint8 billTypeID, uint32 debtorID, uint32 creditorID, double amount, int64 dueDateTime,
                      int32 externalID = -1, int32 externalID2 = -1);
    // pays by hand:  the debtor character, an accountant of the debtor corp, or of the executor of the debtor alliance.  throws
    void PayBill(Client* pClient, uint32 billID, uint16 fromAccountKey);
    // the paying corp's auto-pay settings.  false (and BillOutOfMoney once) when it can't
    bool TryAutoPay(const BillData& bill);
    // unpaid -> cancelled, no handlers
    void CancelBill(uint32 billID);
    // every unpaid bill of a debtor (closed corp or alliance)
    void CancelBillsOf(uint32 debtorID);
    void RegisterHandlers(uint8 billTypeID, BillHandler onPaid, BillHandler onOverdue);

    // corp -> itself, alliance -> its executor corp, character -> 0
    static uint32 PayingCorpFor(uint32 debtorID);
    static bool GetBill(uint32 billID, BillData& into);
    // sovereignty:  daily upkeep of a claimed system (claim marker, hub and its upgrades), the end of the billing period now is in,
    //  and the prorated upkeep charged when an upgrade is installed (throws NotEnoughMoneyCorp)
    static double GetSovDailyCost(const SovereigntyData& claim);
    static int64 GetSovPeriodEnd(int64 claimTime, int64 now);
    static void ChargeUpgradeInstall(Client* pClient, uint32 systemID, uint16 typeID);
    // unpaid bills for the client:  billID, billTypeID, debtorID, creditorID, amount, dueDateTime, interest, externalID, paid, externalID2
    static PyRep* GetBillsPayable(uint32 debtorID);
    static PyRep* GetBillsReceivable(uint32 creditorID);

private:
    // takes the money, marks the bill paid, runs the paid handler and tells the payer
    void Settle(const BillData& bill, uint32 payerID, uint16 fromKey);
    void NotifyIssued(const BillData& bill);
    void NotifyPaid(const BillData& bill, uint32 payerID);
    // OnBillReceived() to the debtor character or the paying corp's online accountants
    void SendBillReceived(uint32 debtorID);
    void ProcessAllianceBills(int64 now);
    void ProcessSovBills(int64 now);

    std::map<uint8, std::pair<BillHandler, BillHandler>> m_handlers;
    std::set<uint32> m_outOfMoney;      // bills already reported as BillOutOfMoney (once per server run)
};

#define sBillingMgr \
( BillingMgr::get() )

#endif  // EVEMU_CORPORATION_BILLINGMGR_H_
