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
    Rewrite:    Allan
*/

#include "eve-server.h"


#include "StaticDataMgr.h"
#include "cache/ObjCacheService.h"
#include "alliance/AllianceDB.h"
#include "corporation/BillMgr.h"
#include "corporation/BillingMgr.h"
#include "corporation/CorpMgr.h"

BillMgr::BillMgr() :
    Service("billMgr")
{
    this->Add("CharPayBill", &BillMgr::CharPayBill);
    this->Add("CharGetBills", &BillMgr::CharGetBills);
    this->Add("CharGetBillsReceivable", &BillMgr::CharGetBillsReceivable);
    this->Add("GetBillTypes", &BillMgr::GetBillTypes);
    this->Add("PayCorporationBill", &BillMgr::PayCorporationBill);
    this->Add("GetCorporationBills", &BillMgr::GetCorporationBills);
    this->Add("GetCorporationBillsReceivable", &BillMgr::GetCorporationBillsReceivable);
    this->Add("GetAutomaticPaySettings", &BillMgr::GetAutomaticPaySettings);
    this->Add("SendAutomaticPaySettings", &BillMgr::SendAutomaticPaySettings);
}

PyResult BillMgr::GetBillTypes(PyCallArgs& call) {
    return sDataMgr.GetBillTypes();
}

PyResult BillMgr::GetCorporationBills(PyCallArgs &call) {
    // wallet:  bills payable by the corp (accountants)
    BillingMgr::RequireBillViewer(call.client);
    return BillingMgr::GetBillsPayable(call.client->GetCorporationID());
}

PyResult BillMgr::GetCorporationBillsReceivable(PyCallArgs &call) {
    BillingMgr::RequireBillViewer(call.client);
    return BillingMgr::GetBillsReceivable(call.client->GetCorporationID());
}

PyResult BillMgr::CharPayBill(PyCallArgs &call, PyInt* billID) {
    //   sm.RemoteSvc('billMgr').CharPayBill(bill.billID)
    sBillingMgr.PayBill(call.client, billID->value(), Account::KeyType::Cash);
    return nullptr;
}

PyResult BillMgr::CharGetBills(PyCallArgs &call) {
    //   return sm.RemoteSvc('billMgr').CharGetBills()
    return BillingMgr::GetBillsPayable(call.client->GetCharacterID());
}

PyResult BillMgr::CharGetBillsReceivable(PyCallArgs &call) {
    //   bills = sm.RemoteSvc('billMgr').CharGetBillsReceivable()
    return BillingMgr::GetBillsReceivable(call.client->GetCharacterID());
}

PyResult BillMgr::PayCorporationBill(PyCallArgs &call, PyInt* billID) {
    //  sm.RemoteSvc('billMgr').PayCorporationBill(bill.billID, fromAccountKey=eve.session.corpAccountKey)
    uint16 fromKey = call.client->GetCorpAccountKey();
    std::map<std::string, PyRep*>::iterator itr = call.byname.find("fromAccountKey");
    if ((itr != call.byname.end()) and (itr->second->IsInt() or itr->second->IsLong()))
        fromKey = PyRep::IntegerValue(itr->second);

    sBillingMgr.PayBill(call.client, billID->value(), fromKey);
    return nullptr;
}

PyResult BillMgr::SendAutomaticPaySettings(PyCallArgs &call, PyDict* automaticPaymentSettings) {
    //    sm.RemoteSvc('billMgr').SendAutomaticPaySettings(self.automaticPaymentSettings)
    //  {ownerID: {billTypeID: bool, ..., 'divisionID': walletKey}}  (the corp, and the alliance when the corp is its executor)
    uint32 corpID = call.client->GetCorporationID();
    if (!IsPlayerCorp(corpID))
        return nullptr;
    if (!CorpMgr::HasRole(call.client->GetCorpRole(), Corp::Role::Accountant))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You must be an accountant to change the automatic payments."));

    DBQueryResult res;
    m_db.GetAutoPay(corpID, res);
    bool flags[7] = {false, false, false, false, false, false, false};
    uint16 divisionID = Account::KeyType::Cash;
    DBResultRow row;
    if (res.GetRow(row)) {
        for (uint8 i = 0; i < 6; ++i)
            flags[i + 1] = (!row.IsNull(i) and (row.GetInt(i) != 0));
        divisionID = row.GetUInt(6);
    }

    uint32 allianceID = call.client->GetAllianceID();
    for (PyDict::const_iterator itr = automaticPaymentSettings->begin(); itr != automaticPaymentSettings->end(); ++itr) {
        uint32 ownerID = PyRep::IntegerValueU32(itr->first);
        if (!itr->second->IsDict())
            continue;
        // the alliance's bills are paid by its executor
        bool alliance = ((ownerID == allianceID) and (allianceID != 0) and (AllianceDB::GetExecutorID(allianceID) == corpID));
        if ((ownerID != corpID) and !alliance)
            continue;
        PyDict* settings = itr->second->AsDict();
        for (PyDict::const_iterator sItr = settings->begin(); sItr != settings->end(); ++sItr) {
            if (sItr->first->IsString() or sItr->first->IsWString()) {
                if ((PyRep::StringContent(sItr->first).compare("divisionID") == 0) and (ownerID == corpID))
                    divisionID = PyRep::IntegerValue(sItr->second);
                continue;
            }
            int64 type = PyRep::IntegerValue(sItr->first);
            if ((type < Corp::BillType::MarketFine) or (type > Corp::BillType::SovereigntyMarker))
                continue;
            if (alliance and (type != Corp::BillType::AllianceMaintainanceBill) and (type != Corp::BillType::SovereigntyMarker))
                continue;
            flags[type] = (PyRep::IntegerValue(sItr->second) != 0);
        }
    }
    if ((divisionID < Account::KeyType::Cash) or (divisionID > Account::KeyType::Cash7))
        divisionID = Account::KeyType::Cash;

    CorporationDB::SetAutoPay(corpID, flags[Corp::BillType::MarketFine], flags[Corp::BillType::RentalBill], flags[Corp::BillType::BrokerBill],
                              flags[Corp::BillType::WarBill], flags[Corp::BillType::AllianceMaintainanceBill], flags[Corp::BillType::SovereigntyMarker],
                              divisionID);
    return nullptr;
}
PyResult BillMgr::GetAutomaticPaySettings(PyCallArgs &call) {
    //    ambSettings = sm.RemoteSvc('billMgr').GetAutomaticPaySettings()
    // returns t/f for bill types
    DBQueryResult res;
    m_db.GetAutoPay(call.client->GetCorporationID(), res);

    auto flag = [](DBResultRow& r, uint32 i) -> PyBool* { return new PyBool(!r.IsNull(i) and (r.GetInt(i) != 0)); };

    DBResultRow row;
    PyDict* sets = new PyDict();
    PyDict* allianceSets(nullptr);
    if (res.GetRow(row)) {
        sets->SetItem(new PyInt(Corp::BillType::MarketFine), flag(row, 0));
        sets->SetItem(new PyInt(Corp::BillType::RentalBill), flag(row, 1));
        sets->SetItem(new PyInt(Corp::BillType::BrokerBill), flag(row, 2));
        sets->SetItem(new PyInt(Corp::BillType::WarBill), flag(row, 3));
        sets->SetItem(new PyInt(Corp::BillType::SovereigntyMarker), flag(row, 5));
        sets->SetItemString("divisionID", new PyInt(row.GetUInt(6)));
        // the alliance's maintenance bills are listed under the alliance (wallet.ShowBillsIn looks them up by debtorID)
        uint32 allianceID = call.client->GetAllianceID();
        if ((allianceID != 0) and (AllianceDB::GetExecutorID(allianceID) == (uint32)call.client->GetCorporationID())) {
            allianceSets = new PyDict();
            allianceSets->SetItem(new PyInt(Corp::BillType::AllianceMaintainanceBill), flag(row, 4));
            allianceSets->SetItem(new PyInt(Corp::BillType::SovereigntyMarker), flag(row, 5));
        }
    }

    PyDict* dict = new PyDict();
        dict->SetItem(new PyInt(call.client->GetCorporationID()), sets);
    if (allianceSets != nullptr)
        dict->SetItem(new PyInt(call.client->GetAllianceID()), allianceSets);

    if (is_log_enabled(CORP__RSP_DUMP))
        dict->Dump(CORP__RSP_DUMP, "");

    return dict;
}


/*
 *        [PyString "OnNotificationReceived"]
 *        [PyList 0 items]
 *        [PyString "clientID"]
 *    [PyInt 5654387]
 *    [PyTuple 1 items]
 *      [PyTuple 2 items]
 *        [PyInt 0]
 *        [PySubStream 168 bytes]
 *          [PyTuple 2 items]
 *            [PyInt 0]
 *            [PyTuple 2 items]
 *              [PyInt 1]
 *              [PyTuple 5 items]
 *                [PyInt 342402174]
 *                [PyInt 10]            << Notify::Types::CorpAllBill
 *                [PyInt 1000167]
 *                [PyIntegerVar 129492968400000000]
 *                [PyDict 8 kvp]
 *                  [PyString "debtorID"]
 *                  [PyInt 98038978]
 *                  [PyString "creditorID"]
 *                  [PyInt 1000167]
 *                  [PyString "billTypeID"]
 *                  [PyInt 2]
 *                  [PyString "amount"]
 *                  [PyInt 981907]
 *                  [PyString "externalID2"]
 *                  [PyInt 60014683]
 *                  [PyString "externalID"]
 *                  [PyInt 27]
 *                  [PyString "currentDate"]
 *                  [PyIntegerVar 129492968683459696]
 *                  [PyString "dueDate"]
 *                  [PyIntegerVar 129518888683422295]
 *    [PyDict 1 kvp]
 *      [PyString "sn"]
 *      [PyIntegerVar 4]
 */

/*
// OnBillReceived, an essentially empty tuple, just to tell the client that there is something,
// maybe for blinking purpose?
OnBillReceived N_obr;   // this is in Wallet.xmlp
PyTuple * res5 = N_obr.Encode();
//call.client->SendNotification("OnBillReceived", "*corpid&corprole", &res5, false);
// Why do we create a bill, when the office is already paid? Maybe that's why it's empty...


// OnMessage notification, the LSC packet NotifyOnMessage can be used, along with the StoreNewEVEMail
// Who to send notification? corpRoleJuniorAccountant and equiv? atm it's enough to send it to the renter
// TODO: get the correct evemail content from somewhere
// TODO: send it to every corp member who's affected by it. corpRoleAccountant, corpRoleJuniorAccountant or equiv
m_manager->lsc_service->SendMail(
    m_db.GetStationCorporationCEO(oInfo.stationID),
                                 call.client->GetCharacterID(),
                                 "Bill issued",
                                 "Bill issued for renting an office");

*/