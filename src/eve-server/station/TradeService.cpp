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
    Author:        Luck (outline only)
    Updates:    Allan   (coded working system)
*/

#include "eve-server.h"

#include <unordered_map>

#include "EntityList.h"


#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "faction/WarManager.h"
#include "market/MarketMgr.h"
#include "ship/ShipDB.h"
#include "station/StationDataMgr.h"
#include "station/TradeService.h"
#include "system/SystemManager.h"
#include "system/Container.h"
#include "services/ServiceManager.h"

namespace {
    // bool, int or None
    bool IsTrue(PyRep* rep) {
        if ((rep == nullptr) or rep->IsNone())
            return false;
        if (rep->IsBool())
            return rep->AsBool()->value();
        return (PyRep::IntegerValue(rep) != 0);
    }

    PyRep* GetKeyword(PyCallArgs& call, const char* name) {
        std::map<std::string, PyRep*>::iterator itr = call.byname.find(name);
        if (itr == call.byname.end())
            return nullptr;
        return itr->second;
    }
}

TradeBound::TradeBound (EVEServiceManager& mgr, TradeService& parent, uint32 containerID) :
    EVEBoundObject(mgr, parent),
    m_TSvc(&parent),
    m_containerID(containerID)
{
    EVEBoundObject::Add("List", &TradeBound::List);
    EVEBoundObject::Add("IsCEOTrade", &TradeBound::IsCEOTrade);
    EVEBoundObject::Add("GetItemID", &TradeBound::GetItemID);
    EVEBoundObject::Add("GetItem", &TradeBound::GetItem);
    EVEBoundObject::Add("Add", &TradeBound::Add);
    EVEBoundObject::Add("MultiAdd", &TradeBound::MultiAdd);
    EVEBoundObject::Add("ToggleAccept", &TradeBound::ToggleAccept);
    EVEBoundObject::Add("OfferMoney", &TradeBound::OfferMoney);
    EVEBoundObject::Add("Abort", &TradeBound::Abort);
}

std::shared_ptr<TradeSession> TradeBound::GetSession(Client* pClient)
{
    std::shared_ptr<TradeSession> pSession(m_TSvc->GetSession(m_containerID));
    if ((pSession.get() == nullptr) or !pSession->HasTrader(pClient->GetCharacterID())) {
        _log(PLAYER__TRADE_MESSAGE, "TradeBound - %s(%u) called trade session %u that is gone or isn't theirs.", \
                pClient->GetName(), pClient->GetCharacterID(), m_containerID);
        return nullptr;
    }
    return pSession;
}

PyResult TradeBound::OfferMoney(PyCallArgs &call, PyRep* amount) {
    std::shared_ptr<TradeSession> pSession(GetSession(call.client));
    if (pSession.get() == nullptr)
        return PyStatic.NewNone();

    // negative offers would move isk the other way, offers above the balance would fail half way through the exchange
    double money(PyRep::FloatValue(amount));
    if (!(money >= 0.0) or (money > 1e15))
        throw CustomError("Invalid amount.");
    money = std::round(money * 100.0) / 100.0;
    // CEO trades use the corp's master wallet
    uint32 walletID(pSession->warID ? call.client->GetCorporationID() : call.client->GetCharacterID());
    double balance(MarketMgr::GetWalletBalance(walletID, Account::KeyType::Cash));
    if (money > balance)
        throw UserError("NotEnoughMoney")
                .AddISK("amount", money)
                .AddISK("balance", balance);

    if (call.client->GetCharacterID() == pSession->myID) {
        pSession->myMoney = money;
    } else {
        pSession->herMoney = money;
    }

    //  reset states after offer changes..
    pSession->myState  = false;
    pSession->herState = false;

    // ('MoneyOffer', containerID, [money0, money1]).  the client resets its accept marks itself
    PyList* list = new PyList(2);
        list->SetItem(0, new PyFloat(pSession->myMoney));
        list->SetItem(1, new PyFloat(pSession->herMoney));
    PyTuple* tuple = new PyTuple(3);
        tuple->SetItem(0, new PyString("MoneyOffer"));
        tuple->SetItem(1, new PyInt(pSession->containerID));
        tuple->SetItem(2, list);
    m_TSvc->SendOnTrade(pSession, tuple);

    // returns none
    return PyStatic.NewNone();
}

PyResult TradeBound::Abort(PyCallArgs &call) {
    std::shared_ptr<TradeSession> pSession(GetSession(call.client));
    if (pSession.get() != nullptr)
        m_TSvc->CancelTrade(pSession);
    // returns none
    return PyStatic.NewNone();
}

PyResult TradeBound::ToggleAccept(PyCallArgs &call, PyRep* newAccept) {
    std::shared_ptr<TradeSession> pSession(GetSession(call.client));
    if (pSession.get() == nullptr)
        return PyStatic.NewNone();

    uint32 charID(call.client->GetCharacterID());
    bool accept(IsTrue(newAccept));
    bool forceTrade(IsTrue(GetKeyword(call, "forceTrade")));

    // receiving a ship needs the client's confirmation (pvptradewnd.OnClickAccept asks, then sends forceTrade)
    if (accept and !forceTrade) {
        for (auto cur : pSession->items) {
            if (cur.second.ownerID == charID)
                continue;
            InventoryItemRef iRef(sItemFactory.GetItemRef(cur.first));
            if ((iRef.get() != nullptr) and (iRef->categoryID() == EVEDB::invCategories::Ship))
                throw UserError("TradeShipWarning");
        }
    }

    if (charID == pSession->myID) {
        pSession->myState = accept;
    } else {
        pSession->herState = accept;
    }

    _log(PLAYER__TRADE_MESSAGE, "TradeBound::Handle_ToggleAccept() is now %s/%s. forceTrade is %s.", \
                (pSession->myState ? "true" : "false"), (pSession->herState ? "true" : "false"), (forceTrade ? "true" : "false"));

    m_TSvc->SendStateToggle(pSession);

    if (pSession->myState and pSession->herState)
        ExchangeItems(pSession);      // trade completed.

    // returns none
    return PyStatic.NewNone();
}

PyResult TradeBound::GetItemID(PyCallArgs &call) {
    _log(CLIENT__CALL_DUMP, "TradeBound::Handle_GetItemID()");
    call.Dump(CLIENT__CALL_DUMP);
    // still not sure what this does...only returns PyNone in packet logs.
    // returns none
    return PyStatic.NewNone();
}

void TradeBound::AddItem(Client* pClient, std::shared_ptr<TradeSession> pSession, uint32 itemID, int32 qty)
{
    uint32 charID(pClient->GetCharacterID());
    InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
    if (iRef.get() == nullptr)
        throw CustomError("That item could not be found.");
    if (itemID == pClient->GetShipID())
        throw UserError("PeopleAboardShip");
    if (pSession->items.find(itemID) != pSession->items.end())
        return;

    // own items in the station's item hangar, or in the active ship's cargo while docked there.
    //  CEO trades can also offer the corp's items from its office here
    bool allowed(false);
    if (iRef->ownerID() == charID) {
        if ((iRef->locationID() == pSession->stationID) and (iRef->flag() == flagHangar))
            allowed = true;
        if ((iRef->locationID() == pClient->GetShipID()) and ((uint32)pClient->GetStationID() == pSession->stationID)
        and (iRef->flag() == flagCargoHold))
            allowed = true;
    } else if (pSession->warID and (iRef->ownerID() == (uint32)pClient->GetCorporationID())) {
        uint32 officeID(stDataMgr.GetOfficeIDForCorp(pSession->stationID, pClient->GetCorporationID()));
        if ((officeID != 0) and (iRef->locationID() == officeID) and ((pClient->GetCorpRole() & Corp::Role::Director) != 0))
            allowed = true;
    }
    if (!allowed) {
        _log(PLAYER__TRADE_MESSAGE, "TradeBound::AddItem() - %s(%u) tried to trade %s(%u) owned by %u in %u (flag %u).", \
                pClient->GetName(), charID, iRef->name(), itemID, iRef->ownerID(), iRef->locationID(), iRef->flag());
        throw CustomError("You can only trade your own items from your hangar or cargo hold in this station.");
    }

    // part of a stack
    if ((qty > 0) and (qty < iRef->quantity()) and !iRef->isSingleton()) {
        InventoryItemRef split(iRef->Split(qty, true));
        if (split.get() == nullptr)
            throw CustomError("Unable to split that stack.");
        iRef = split;
    }

    TradeSession::TradeItem data = TradeSession::TradeItem();
        data.ownerID = iRef->ownerID();
        data.traderID = charID;
        data.locationID = iRef->locationID();
        data.flag = iRef->flag();
    pSession->items[iRef->itemID()] = data;

    // the owner sees it leave (Move notify), the other trader sees it arrive in the trade container
    iRef->Move(pSession->containerID, flagNone, true);
    Client* pOther(sEntityList.FindClientByCharID(pSession->Other(charID)));
    if (pOther != nullptr) {
        PyDict* changes = new PyDict();
            changes->SetItem(new PyInt(Inv::Update::Location), new PyInt(data.locationID));
            changes->SetItem(new PyInt(Inv::Update::Flag), new PyInt(data.flag));
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, iRef->GetItemRow());
            tuple->SetItem(1, changes);
        pOther->SendNotification("OnItemChange", "charid", &tuple);
    }
}

PyResult TradeBound::Add(PyCallArgs &call, PyInt* itemID, PyRep* containerID) {
    std::shared_ptr<TradeSession> pSession(GetSession(call.client));
    if (pSession.get() == nullptr)
        return PyStatic.NewNone();

    PyRep* qty(GetKeyword(call, "qty"));
    AddItem(call.client, pSession, itemID->value(), ((qty != nullptr) and !qty->IsNone()) ? (int32)PyRep::IntegerValue(qty) : 0);

    //  reset states after offer changes..
    pSession->myState  = false;
    pSession->herState = false;
    m_TSvc->SendStateToggle(pSession);
    // return none
    return PyStatic.NewNone();
}

PyResult TradeBound::MultiAdd(PyCallArgs &call, PyList* itemIDs, PyRep* containerID) {
    /*  .ints = list of itemIDs to insert into trade
     *  .contID = item's current containerID
     *  call.byname "flag"
     */
    std::shared_ptr<TradeSession> pSession(GetSession(call.client));
    if (pSession.get() == nullptr)
        return PyStatic.NewNone();

    for (auto cur : *itemIDs) {
        if (!cur->IsInt() and !cur->IsLong())
            continue;
        AddItem(call.client, pSession, (uint32)PyRep::IntegerValue(cur), 0);
    }

    //  reset states after offer changes.
    pSession->myState  = false;
    pSession->herState = false;
    m_TSvc->SendStateToggle(pSession);

    // return none
    return PyStatic.NewNone();
}

PyResult TradeBound::GetItem(PyCallArgs &call) {
    std::shared_ptr<TradeSession> pSession(GetSession(call.client));
    if (pSession.get() == nullptr)
        return PyStatic.NewNone();
    PyPackedRow* row = new PyPackedRow( sDataMgr.CreateHeader() );
        row->SetField("itemID",        new PyLong(pSession->containerID));
        row->SetField("typeID",        new PyInt(53));     // type Trade Window
        row->SetField("ownerID",       PyStatic.NewOne());      // EvE_System
        row->SetField("locationID",    new PyInt(pSession->stationID));
        row->SetField("flagID",        PyStatic.NewNone());
        row->SetField("quantity",      new PyInt(-1));     // singleton
        row->SetField("groupID",       new PyInt(EVEDB::invGroups::Trade_Session ) );
        row->SetField("categoryID",    new PyInt(EVEDB::invCategories::Trading));
        row->SetField("customInfo",    PyStatic.NewNone());
    return row;
}

PyResult TradeBound::IsCEOTrade(PyCallArgs &call) {
    // a trade started with a warID (CEO surrender trade, form.CEOVCEOTrade)
    std::shared_ptr<TradeSession> pSession(GetSession(call.client));
    return new PyBool((pSession.get() != nullptr) and (pSession->warID != 0));
}

PyResult TradeBound::List(PyCallArgs &call) {
    std::shared_ptr<TradeSession> pSession(GetSession(call.client));
    if (pSession.get() == nullptr)
        throw CustomError("This trade session has ended.");

    // util.Row(header=[tradeContainerID, traders, state, money, items])
    PyList* list = new PyList();
    for (auto cur : pSession->items) {
        InventoryItemRef iRef(sItemFactory.GetItemRef(cur.first));
        if ((iRef.get() == nullptr) or (iRef->locationID() != pSession->containerID))
            continue;
        list->AddItem(iRef->GetItemRow());
    }

    PyTuple* tuple2 = new PyTuple(1);
        tuple2->SetItem(0, list);
    PyToken* token = new PyToken("__builtin__.set");
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, token);
        tuple->SetItem(1, tuple2);
    TradeListData tld;
        tld.tradeContainerID = pSession->containerID;
        tld.myID             = pSession->myID;
        tld.herID            = pSession->herID;
        tld.myMoney          = pSession->myMoney;
        tld.herMoney         = pSession->herMoney;
        tld.myState          = pSession->myState;
        tld.herState         = pSession->herState;
        tld.list             = new PyObjectEx(false, tuple);
    PyList* itemNames = new PyList(5);
        itemNames->SetItem(0, new PyString("tradeContainerID"));
        itemNames->SetItem(1, new PyString("traders"));
        itemNames->SetItem(2, new PyString("state"));
        itemNames->SetItem(3, new PyString("money"));
        itemNames->SetItem(4, new PyString("items"));
    TradeListRsp tlr;
        tlr.header  = itemNames;
        tlr.line    = tld.Encode();
    return tlr.Encode();
}

void TradeBound::ExchangeItems(std::shared_ptr<TradeSession> pSession) {
    Client* pMe(sEntityList.FindClientByCharID(pSession->myID));
    Client* pHer(sEntityList.FindClientByCharID(pSession->herID));
    if ((pMe == nullptr) or (pHer == nullptr)) {
        m_TSvc->CancelTrade(pSession);
        return;
    }

    // CEO trades move isk between the corps' master wallets and items between their offices
    bool ceoTrade(pSession->warID != 0);
    uint32 myWallet(ceoTrade ? pMe->GetCorporationID() : pMe->GetCharacterID());
    uint32 herWallet(ceoTrade ? pHer->GetCorporationID() : pHer->GetCharacterID());

    // check everything again before anything moves
    std::string problem;
    if (MarketMgr::GetWalletBalance(myWallet, Account::KeyType::Cash) < pSession->myMoney)
        problem = std::string(pMe->GetName()) + " no longer has the offered ISK.";
    if (MarketMgr::GetWalletBalance(herWallet, Account::KeyType::Cash) < pSession->herMoney)
        problem = std::string(pHer->GetName()) + " no longer has the offered ISK.";
    for (auto cur : pSession->items) {
        InventoryItemRef iRef(sItemFactory.GetItemRef(cur.first));
        if ((iRef.get() == nullptr) or (iRef->locationID() != pSession->containerID) or (iRef->ownerID() != cur.second.ownerID))
            problem = "One of the offered items is no longer available.";
    }
    if (!problem.empty()) {
        pSession->myState  = false;
        pSession->herState = false;
        m_TSvc->SendStateToggle(pSession);
        throw CustomError("%s", problem.c_str());
    }

    // transfer funds and add journal entries for both sides.  if the second transfer fails, the first is undone
    std::string reason = "Player Trade between ";
    reason += pMe->GetCharName();
    reason += " and ";
    reason += pHer->GetCharName();
    reason += " in ";
    reason += stDataMgr.GetStationName(pSession->stationID);
    if (pSession->myMoney > 0)
        AccountService::TransferFunds(myWallet, herWallet, pSession->myMoney, reason, Journal::EntryType::PlayerTrading, pSession->stationID, Account::KeyType::Cash, Account::KeyType::Cash, pMe);
    if (pSession->herMoney > 0) {
        try {
            AccountService::TransferFunds(herWallet, myWallet, pSession->herMoney, reason, Journal::EntryType::PlayerTrading, pSession->stationID, Account::KeyType::Cash, Account::KeyType::Cash, pHer);
        } catch (...) {
            if (pSession->myMoney > 0)
                AccountService::TransferFunds(herWallet, myWallet, pSession->myMoney, reason, Journal::EntryType::PlayerTrading, pSession->stationID, Account::KeyType::Cash, Account::KeyType::Cash, pMe);
            pSession->myState  = false;
            pSession->herState = false;
            m_TSvc->SendStateToggle(pSession);
            throw;
        }
    }

    // items change hands in the station (CEO trades:  the receiving corp's office deliveries hangar)
    for (auto cur : pSession->items) {
        InventoryItemRef iRef(sItemFactory.GetItemRef(cur.first));
        if (iRef.get() == nullptr)
            continue;
        bool fromMe(cur.second.traderID == pSession->myID);
        uint32 newOwnerID(fromMe ? herWallet : myWallet);
        uint32 receiverCharID(fromMe ? pSession->herID : pSession->myID);
        uint32 itemOwnerID(newOwnerID), locationID(pSession->stationID);
        EVEItemFlags flag(flagHangar);
        MarketMgr::GetDeliveryLocation(newOwnerID, ceoTrade and IsPlayerCorp(newOwnerID), receiverCharID, pSession->stationID, itemOwnerID, locationID, flag);

        iRef->ChangeOwner(itemOwnerID, true);
        iRef->Move(locationID, flag, true);
        if ((iRef->categoryID() == EVEDB::invCategories::Ship)
            || (iRef->groupID() == EVEDB::invGroups::Audit_Log_Secure_Container)
            || (iRef->groupID() == EVEDB::invGroups::Cargo_Container)
            || (iRef->groupID() == EVEDB::invGroups::Freight_Container)
            || (iRef->groupID() == EVEDB::invGroups::Secure_Cargo_Container))
            m_TSvc->TransferContainerContents(iRef, itemOwnerID);
    }

    // a CEO trade with a warID is the surrender:  the war ends with the 24 hour grace
    if (ceoTrade)
        sWarMgr.OnSurrenderTrade(pSession->warID, pSession->myID);

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyString("TradeComplete"));
        tuple->SetItem(1, new PyInt(pSession->containerID));
    m_TSvc->SendOnTrade(pSession, tuple);
    m_TSvc->CloseSession(pSession);
}

TradeService::TradeService(EVEServiceManager& mgr)
    : BindableService("trademgr", mgr, eAccessLevel_SolarSystem2)
{
    m_SessionID = minTradeCont;

    this->Add("InitiateTrade", &TradeService::InitiateTrade);

    // items of a trade that never ended (server stopped) go back to their owner's hangar in the owner's station
    DBerror err;
    sDatabase.RunQuery(err,
        "UPDATE entity e JOIN chrCharacters c ON c.characterID = e.ownerID"
        " SET e.locationID = c.stationID, e.flag = %u"
        " WHERE e.locationID BETWEEN %u AND %u AND c.stationID > 0",
        flagHangar, minTradeCont, maxTradeCont);
}

BoundDispatcher* TradeService::BindObject(Client* client, PyRep* bindParameters) {
    // each trade window binds the moniker from InitiateTrade:  (stationID, (myID, herID), money, state, when)
    Trade_BindArgs args;
    PyRep* tmp(bindParameters->Clone());
    if (!args.Decode(&tmp)) {
        codelog(SERVICE__ERROR, "%s: Failed to decode bind args.", GetName());
        return nullptr;
    }

    _log(COLLECT__OTHER_DUMP, "Trade bind request for:");
    args.Dump(COLLECT__OTHER_DUMP, "    ");

    std::map<std::tuple<uint32, uint32, int64>, uint32>::iterator itr = m_byBind.find(std::make_tuple(args.myID, args.herID, args.fileTime));
    if (itr == m_byBind.end()) {
        _log(PLAYER__TRADE_MESSAGE, "TradeService::BindObject() - no trade session for %u/%u.", args.myID, args.herID);
        return nullptr;
    }
    std::shared_ptr<TradeSession> pSession(GetSession(itr->second));
    if ((pSession.get() == nullptr) or !pSession->HasTrader(client->GetCharacterID()))
        return nullptr;

    client->AddTradeSession(pSession->containerID);
    return new TradeBound(this->GetServiceManager(), *this, pSession->containerID);
}

void TradeService::BoundReleased (TradeBound* bound) {

}

std::shared_ptr<TradeSession> TradeService::GetSession(uint32 containerID)
{
    std::map<uint32, std::shared_ptr<TradeSession>>::iterator itr = m_sessions.find(containerID);
    if (itr == m_sessions.end())
        return nullptr;
    return itr->second;
}

void TradeService::SendOnTrade(std::shared_ptr<TradeSession> pSession, PyTuple* payload)
{
    Client* pMe(sEntityList.FindClientByCharID(pSession->myID));
    Client* pHer(sEntityList.FindClientByCharID(pSession->herID));
    if (pMe != nullptr) {
        PyIncRef(payload);
        pMe->SendNotification("OnTrade", "charid", &payload);
    }
    if (pHer != nullptr) {
        PyIncRef(payload);
        pHer->SendNotification("OnTrade", "charid", &payload);
    }
    PyDecRef(payload);
}

void TradeService::SendStateToggle(std::shared_ptr<TradeSession> pSession)
{
    // ('StateToggle', containerID, [state0, state1]), indexed like 'traders'
    PyList* states = new PyList(2);
        states->SetItem(0, new PyBool(pSession->myState));
        states->SetItem(1, new PyBool(pSession->herState));
    PyTuple* tuple = new PyTuple(3);
        tuple->SetItem(0, new PyString("StateToggle"));
        tuple->SetItem(1, new PyInt(pSession->containerID));
        tuple->SetItem(2, states);
    SendOnTrade(pSession, tuple);
}

void TradeService::CancelTrade(std::shared_ptr<TradeSession> pSession)
{
    // trade canceled.  send items back to where they came from (monies not taken at this point)
    for (auto cur : pSession->items) {
        InventoryItemRef iRef(sItemFactory.GetItemRef(cur.first));
        if ((iRef.get() == nullptr) or (iRef->locationID() != pSession->containerID))
            continue;
        uint32 locationID(cur.second.locationID);
        EVEItemFlags flag(cur.second.flag);
        // a cargo hold that left the station:  the hangar instead
        if (locationID != pSession->stationID) {
            InventoryItemRef origin(sItemFactory.GetItemRef(locationID));
            if ((origin.get() == nullptr) or (origin->locationID() != pSession->stationID)) {
                locationID = pSession->stationID;
                flag = flagHangar;
            }
        }
        iRef->Move(locationID, flag, true);

        Client* pOther(sEntityList.FindClientByCharID(pSession->Other(cur.second.traderID)));
        if (pOther != nullptr) {
            PyDict* changes = new PyDict();
                changes->SetItem(new PyInt(Inv::Update::Location), new PyInt(pSession->containerID));
            PyTuple* tuple = new PyTuple(2);
                tuple->SetItem(0, iRef->GetItemRow());
                tuple->SetItem(1, changes);
            pOther->SendNotification("OnItemChange", "charid", &tuple);
        }
    }

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyString("Cancel"));
        tuple->SetItem(1, new PyInt(pSession->containerID));
    SendOnTrade(pSession, tuple);
    CloseSession(pSession);
}

void TradeService::CancelAllTrades(Client* pClient)
{
    // copy, CancelTrade changes the client's list
    std::set<uint32> sessions(pClient->GetTradeSessions());
    for (auto cur : sessions) {
        std::shared_ptr<TradeSession> pSession(GetSession(cur));
        if (pSession.get() != nullptr) {
            CancelTrade(pSession);
        } else {
            pClient->RemoveTradeSession(cur);
        }
    }
}

void TradeService::CloseSession(std::shared_ptr<TradeSession> pSession)
{
    m_byBind.erase(std::make_tuple(pSession->myID, pSession->herID, pSession->fileTime));
    m_sessions.erase(pSession->containerID);
    Client* pMe(sEntityList.FindClientByCharID(pSession->myID));
    if (pMe != nullptr)
        pMe->RemoveTradeSession(pSession->containerID);
    Client* pHer(sEntityList.FindClientByCharID(pSession->herID));
    if (pHer != nullptr)
        pHer->RemoveTradeSession(pSession->containerID);
}

void TradeService::TransferContainerContents(InventoryItemRef itemRef, uint32 newOwnerID)
{
    // everything inside a traded ship or container goes with it
    Inventory* pInv(itemRef->GetMyInventory());
    if (pInv == nullptr)
        return;
    pInv->LoadContents();
    std::vector<InventoryItemRef> contents;
    pInv->GetInventoryVec(contents);
    for (auto cur : contents) {
        cur->ChangeOwner(newOwnerID, true);
        TransferContainerContents(cur, newOwnerID);
    }
}

PyResult TradeService::InitiateTrade(PyCallArgs &call, PyInt* characterID, std::optional<PyRep*> warID) {
    Client* pClient(call.client);
    Client* target(sEntityList.FindClientByCharID(characterID->value()));
    if ((target == nullptr) or (target == pClient))
        throw CustomError("That character is not available for trading.");
    // both docked in the same station
    if ((pClient->GetStationID() == 0) or (pClient->GetStationID() != target->GetStationID()))
        throw CustomError("%s is not in this station.", target->GetName());

    // one session per pair
    for (auto cur : m_sessions) {
        if (cur.second->HasTrader(pClient->GetCharacterID()) and cur.second->HasTrader(target->GetCharacterID()))
            throw CustomError("You are already trading with %s.", target->GetName());
    }

    // CEO surrender trades (form.CEOVCEOTrade):  the leader of the side the war was declared on trades with the CEO of the
    //  other corp, or of the other alliance's executor (windowsvc.StartCEOTradeSession)
    uint32 war(0);
    if (warID.has_value() and !warID.value()->IsNone())
        war = (uint32)PyRep::IntegerValue(warID.value());
    if (war != 0)
        sWarMgr.CheckSurrender(war, pClient->GetCharacterID(), target->GetCharacterID());

    std::shared_ptr<TradeSession> pSession(std::make_shared<TradeSession>());
        pSession->containerID = GetTradeSessionID();
        pSession->stationID = pClient->GetStationID();
        pSession->myID = pClient->GetCharacterID();
        pSession->herID = target->GetCharacterID();
        pSession->warID = war;
        pSession->myState = false;
        pSession->herState = false;
        pSession->myMoney = 0;
        pSession->herMoney = 0;
        pSession->fileTime = GetFileTimeNow();
    m_sessions[pSession->containerID] = pSession;
    m_byBind[std::make_tuple(pSession->myID, pSession->herID, pSession->fileTime)] = pSession->containerID;
    // both sides now, so either one logging out or undocking (before binding) cancels it
    pClient->AddTradeSession(pSession->containerID);
    target->AddTradeSession(pSession->containerID);

    InitiateTradeRsp_NoCash rsp_nc;
        rsp_nc.nodeID    = pClient->services().GetNodeID();
        rsp_nc.stationID = pSession->stationID;
        rsp_nc.myID      = pSession->myID;
        rsp_nc.herID     = pSession->herID;
        rsp_nc.money     = 0;
        rsp_nc.state     = 0;
        rsp_nc.when      = pSession->fileTime;

    PyObject* resp = rsp_nc.Encode();
    // the target opens its window with OnTrade('Initiate', initiatorID, moniker).  the caller opens its own from the reply
    PyTuple* tuple = new PyTuple(3);
        tuple->SetItem(0, new PyString("Initiate"));
        tuple->SetItem(1, new PyInt(pClient->GetCharacterID()));
        tuple->SetItem(2, resp->Clone());
    target->SendNotification("OnTrade", "charid", &tuple);
    return resp;
}

uint32 TradeService::GetTradeSessionID()
{
    // the next free trade container id
    do {
        if (m_SessionID < maxTradeCont) {
            ++m_SessionID;
        } else {
            m_SessionID = minTradeCont + 1;
        }
    } while (m_sessions.find(m_SessionID) != m_sessions.end());
    return m_SessionID;
}
