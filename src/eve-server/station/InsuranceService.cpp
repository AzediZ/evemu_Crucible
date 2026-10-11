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
    Author:     Zhur
    Rewrite:    Allan
*/

#include "eve-server.h"



#include "Client.h"
#include "EVE_Mail.h"
#include "EVEServerConfig.h"
#include "account/AccountService.h"
#include "mail/NotificationMgr.h"
#include "market/MarketMgr.h"
#include "station/InsuranceService.h"
#include "station/StationDataMgr.h"
#include "system/SystemEntity.h"
#include "system/SystemManager.h"
#include "services/ServiceManager.h"

/* Crucible insurance (Tyrannis rules):
 *  - quotes are 5%..30% of the ship's insurance value for 50%..100% coverage, 12 weeks
 *  - the payout is the coverage times the insurance value when the ship is lost; uninsured ships get the default 40%
 *  - corp insurance is paid from/to the corp master wallet by accountants and junior accountants
 *  - repackaging, trading, selling, contracting or giving the ship away voids personal insurance
 */

InsuranceService::InsuranceService(EVEServiceManager& mgr) :
    BindableService("insuranceSvc", mgr)
{
    this->Add("GetContractForShip", &InsuranceService::GetContractForShip);
    this->Add("GetInsurancePrice", &InsuranceService::GetInsurancePrice);
    // base_insurance.py calls this unbound when not docked
    this->Add("GetContracts", &InsuranceService::GetContracts);
	//SetSessionCheck?
}

BoundDispatcher* InsuranceService::BindObject(Client* client, PyRep* bindParameters) {
    // bound to the station:  Moniker('insuranceSvc', session.stationid2)
    uint32 stationID(0);
    if ((bindParameters != nullptr) and (bindParameters->IsInt() or bindParameters->IsLong()))
        stationID = PyRep::IntegerValueU32(bindParameters);
    if (stationID == 0)
        stationID = client->GetStationID();
    return new InsuranceBound(this->GetServiceManager(), *this, &m_db, stationID);
}

void InsuranceService::BoundReleased (InsuranceBound* bound) {

}

PyResult InsuranceService::GetContractForShip(PyCallArgs& call, PyInt* shipID) {
    return m_db.GetInsuranceByShipID(shipID->value());
}

PyResult InsuranceService::GetInsurancePrice(PyCallArgs& call, PyInt* typeID) {
    /* called in space.  0 hides the ship in the insurance window (rookie ships, non-ships) */
    return new PyFloat(sMktMgr.GetInsuranceValue(typeID->value()));
}

PyResult InsuranceService::GetContracts(PyCallArgs& call, std::optional<PyRep*> isCorporation) {
    if (isCorporation.has_value() and (PyRep::IntegerValue(isCorporation.value()) != 0)) {
        if ((call.client->GetCorpRole() & (Corp::Role::Accountant | Corp::Role::JuniorAccountant)) == 0)
            return m_db.GetInsuranceByOwnerID(0);
        return m_db.GetInsuranceByOwnerID(call.client->GetCorporationID());
    }
    return m_db.GetInsuranceByOwnerID(call.client->GetCharacterID());
}

void InsuranceService::ProcessExpiry()
{
    // announce contracts that ran out (InsuranceExpiration).  the rows stay 30 days so a loss can still say "expired"
    int64 now(GetFileTimeNow());
    std::vector<ShipDB::InsuranceRow> rows;
    ShipDB::GetExpiredInsurance(now, rows);
    for (auto& cur : rows) {
        ShipDB::SetExpiryNotified(cur.shipID);
        PyDict* data = new PyDict();
            data->SetItemString("shipID", new PyInt(cur.shipID));
            data->SetItemString("shipName", new PyString(cur.shipName));
            data->SetItemString("startDate", new PyLong(cur.startDate));
        if (cur.isCorp) {
            NotificationMgr::SendToCorp(cur.ownerID, Corp::Role::Accountant | Corp::Role::JuniorAccountant, Notify::Types::InsuranceExpiration, corpSCC, data);
        } else if (IsCharacterID(cur.ownerID)) {
            NotificationMgr::Send(cur.ownerID, Notify::Types::InsuranceExpiration, corpSCC, data);
        } else {
            PyDecRef(data);
        }
    }
    ShipDB::DeleteInsuranceEndedBefore(now - (EvE::Time::Day * 30));
}

InsuranceBound::InsuranceBound(EVEServiceManager& mgr, InsuranceService& parent, ShipDB* db, uint32 stationID) :
    EVEBoundObject(mgr, parent),
    m_db(db),
    m_stationID(stationID)
{
    this->Add("InsureShip", &InsuranceBound::InsureShip);
    this->Add("UnInsureShip", &InsuranceBound::UnInsureShip);
    this->Add("GetContracts", &InsuranceBound::GetContracts);
    this->Add("GetInsurancePrice", &InsuranceBound::GetInsurancePrice);
}

PyResult InsuranceBound::UnInsureShip(PyCallArgs& call, PyInt* shipID) {
    // only the insurance's owner (corp insurance:  its accountants) can cancel it
    ShipDB::InsuranceRow row = ShipDB::InsuranceRow();
    if (!ShipDB::GetInsurance(shipID->value(), row))
        return PyStatic.NewNone();
    if (row.isCorp) {
        if ((row.ownerID != (uint32)call.client->GetCorporationID())
        or ((call.client->GetCorpRole() & (Corp::Role::Accountant | Corp::Role::JuniorAccountant)) == 0))
            throw CustomError("Only an accountant of the insuring corporation can cancel this insurance.");
    } else if (row.ownerID != call.client->GetCharacterID()) {
        throw CustomError("You can only cancel your own insurance contracts.");
    }
    ShipDB::DeleteInsuranceByShipID(shipID->value());
    return PyStatic.NewNone();
}

PyResult InsuranceBound::GetInsurancePrice(PyCallArgs& call, PyInt* typeID) {
    /* called when docked.  0 hides the ship in the insurance window (rookie ships, non-ships) */
    return new PyFloat(sMktMgr.GetInsuranceValue(typeID->value()));
}

PyResult InsuranceBound::GetContracts(PyCallArgs& call, std::optional<PyRep*> isCorporation) {
    if (isCorporation.has_value() and (PyRep::IntegerValue(isCorporation.value()) != 0)) {
        if ((call.client->GetCorpRole() & (Corp::Role::Accountant | Corp::Role::JuniorAccountant)) == 0)
            return m_db->GetInsuranceByOwnerID(0);
        return m_db->GetInsuranceByOwnerID(call.client->GetCorporationID());
    }

    return m_db->GetInsuranceByOwnerID(call.client->GetCharacterID());
}

PyResult InsuranceBound::InsureShip(PyCallArgs& call, PyInt* shipID, PyRep* amount, std::optional<PyRep*> isCorporation) {
    call.Dump(SERVICE__CALL_DUMP);
    Client* pClient(call.client);
    bool corpInsurance(isCorporation.has_value() and (PyRep::IntegerValue(isCorporation.value()) != 0));

    InventoryItemRef shipRef = sItemFactory.GetItemRef(shipID->value());
    if ((shipRef.get() == nullptr) or (shipRef->categoryID() != EVEDB::invCategories::Ship))
        throw UserError("InsCouldNotFindItem");
    if (shipRef->groupID() == EVEDB::invGroups::Rookieship) {
        pClient->SendInfoModalMsg("You cannot insure Rookie ships.");
        return PyStatic.NewNone();
    } // end rookie ship check
    if (!shipRef->isSingleton())
        throw UserError("InsCanNotInsureUnassembledShip");

    // the ship must be here:  own ship in this station's hangar (or the active ship docked here),
    //  corp ship in the corp's office here (accountants and junior accountants)
    uint32 stationID(m_stationID ? m_stationID : pClient->GetStationID());
    uint32 payerID(pClient->GetCharacterID());
    if (corpInsurance) {
        uint32 corpID(pClient->GetCorporationID());
        if ((pClient->GetCorpRole() & (Corp::Role::Accountant | Corp::Role::JuniorAccountant)) == 0)
            throw UserError("InsCouldNotFindItem");
        uint32 officeID(stDataMgr.GetOfficeIDForCorp(stationID, corpID));
        if ((shipRef->ownerID() != corpID) or (officeID == 0) or (shipRef->locationID() != officeID))
            throw UserError("InsCouldNotFindItem");
        payerID = corpID;
    } else if ((shipRef->ownerID() != pClient->GetCharacterID())
           or ((shipRef->locationID() != stationID) and (shipRef->itemID() != pClient->GetShipID()))
           or (pClient->GetStationID() != stationID)) {
        throw UserError("InsCouldNotFindItem");
    }

    /* INSURANCE FRACTION TABLE:
     *    Label    Fraction  Payment
     *   --------  --------  ------
     *   Platinum   1.0       0.3
     *   Gold       0.9       0.25
     *   Silver     0.8       0.2
     *   Bronze     0.7       0.15
     *   Standard   0.6       0.1
     *   Basic      0.5       0.05
     *   None       0.4       0.00  (default payout)
     *  the client builds its quotes by adding 5% steps (float sums), so the level is the nearest step
     */
    double value(sMktMgr.GetInsuranceValue(shipRef->typeID()));
    if (value <= 0)
        throw UserError("InsCouldNotFindItem");
    double premium(PyRep::FloatValue(amount));
    double step(value * 0.05);
    int32 steps((int32)std::llround(premium / step));
    if ((steps < 1) or (steps > 6) or (premium < steps * step * 0.999))
        throw CustomError("Your payment of %.2f ISK does not match an insurance level for this ship.", premium);
    premium = std::round(steps * step * 100.0) / 100.0;
    float fraction(0.4f + 0.1f * steps);

    // one contract per ship.  the client asks before replacing one (voidOld)
    ShipDB::InsuranceRow existing = ShipDB::InsuranceRow();
    if (ShipDB::GetInsurance(shipRef->itemID(), existing)) {
        std::map<std::string, PyRep*>::iterator itr = call.byname.find("voidOld");
        bool voidOld((itr != call.byname.end()) and (PyRep::IntegerValue(itr->second) != 0));
        if (!voidOld and (existing.endDate > GetFileTimeNow()))
            throw UserError("InsureShipFailedSingleContract").AddFormatValue("ownerName", new PyString(
                    sDataMgr.GetOwnerName(existing.ownerID)));
        ShipDB::DeleteInsuranceByShipID(shipRef->itemID());
    }

    // the premium first (this throws when the wallet is short; nothing has changed)
    std::string reason = "Insurance Premium on ";
    reason += shipRef->itemName();
    AccountService::TransferFunds(payerID, corpSCC, premium, reason, Journal::EntryType::Insurance, -shipRef->itemID(),
            Account::KeyType::Cash, Account::KeyType::Cash, pClient);  // for paying ins, shipID should be negative

    uint8 numWeeks(12);
    if (!ShipDB::InsertInsurance(shipRef->itemID(), shipRef->typeID(), shipRef->itemName(), payerID, corpInsurance, fraction, value * fraction, numWeeks)) {
        AccountService::TransferFunds(corpSCC, payerID, premium, "Insurance premium refund", Journal::EntryType::Insurance, shipRef->itemID(),
                Account::KeyType::Cash, Account::KeyType::Cash, pClient);
        throw UserError ("InsureShipFailed");
    }

    // SCC's InsuranceIssued notification (Notifications/bodyInsuranceIssued)
    ShipDB::InsuranceRow row = ShipDB::InsuranceRow();
    ShipDB::GetInsurance(shipRef->itemID(), row);
    PyDict* data = new PyDict();
        data->SetItemString("itemID", new PyInt(shipRef->itemID()));
        data->SetItemString("typeID", new PyInt(shipRef->typeID()));
        data->SetItemString("typeID2", new PyInt(shipRef->typeID()));
        data->SetItemString("shipName", new PyString(shipRef->itemName()));
        data->SetItemString("level", new PyInt((int32)std::lround(fraction * 100)));
        data->SetItemString("numWeeks", new PyInt(numWeeks));
        data->SetItemString("endDate", new PyLong(row.endDate));
    NotificationMgr::Send(pClient->GetCharacterID(), Notify::Types::InsuranceIssued, corpSCC, data);

    return m_db->GetInsuranceByShipID(shipID->value());
}
