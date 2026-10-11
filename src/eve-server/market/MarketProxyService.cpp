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
    Updates:    Allan
*/

// this class isnt a singleton, but is instantiated once on server start.


#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "cache/ObjCacheService.h"
#include "inventory/Inventory.h"
#include "manufacturing/Blueprint.h"
#include "market/MarketMgr.h"
#include "market/MarketProxyService.h"
#include "station/StationDataMgr.h"
#include "system/SystemManager.h"
#include "standing/StandingDB.h"
#include "corporation/CorporationDB.h"

/*
 * MARKET__ERROR
 * MARKET__WARNING
 * MARKET__MESSAGE
 * MARKET__DEBUG
 * MARKET__TRACE
 * MARKET__DB_ERROR
 * MARKET__DB_TRACE
 */

MarketProxyService::MarketProxyService(EVEServiceManager& mgr) :
    Service("marketProxy"),
    m_manager (mgr)
{
    m_cache = this->m_manager.Lookup <ObjCacheService>("objectCaching");

    this->Add("StartupCheck", &MarketProxyService::StartupCheck);
    this->Add("GetStationAsks", &MarketProxyService::GetStationAsks);
    this->Add("GetSystemAsks", &MarketProxyService::GetSystemAsks);
    this->Add("GetRegionBest", &MarketProxyService::GetRegionBest);
    this->Add("GetMarketGroups", &MarketProxyService::GetMarketGroups);
    this->Add("GetOrders", &MarketProxyService::GetOrders);
    this->Add("GetOldPriceHistory", &MarketProxyService::GetOldPriceHistory);
    this->Add("GetNewPriceHistory", &MarketProxyService::GetNewPriceHistory);
    this->Add("PlaceCharOrder", static_cast <PyResult (MarketProxyService::*)(PyCallArgs &call, PyInt*, PyInt*, PyFloat*, PyInt*, PyInt*, PyInt*, std::optional <PyInt*>, PyInt*, PyInt*, PyBool*, std::optional<PyRep*>)> (&MarketProxyService::PlaceCharOrder));
    this->Add("PlaceCharOrder", static_cast <PyResult (MarketProxyService::*)(PyCallArgs &call, PyInt*, PyInt*, PyFloat*, PyInt*, PyInt*, PyInt*, std::optional <PyInt*>, PyInt*, PyInt*, PyInt*, std::optional<PyRep*>)> (&MarketProxyService::PlaceCharOrder));
    this->Add("GetCharOrders", &MarketProxyService::GetCharOrders);
    this->Add("ModifyCharOrder", &MarketProxyService::ModifyCharOrder);
    this->Add("CancelCharOrder", &MarketProxyService::CancelCharOrder);
    this->Add("CharGetNewTransactions", &MarketProxyService::CharGetNewTransactions);
    this->Add("CorpGetNewTransactions", &MarketProxyService::CorpGetNewTransactions);
    this->Add("GetCorporationOrders", &MarketProxyService::GetCorporationOrders);
}

PyResult MarketProxyService::GetMarketGroups(PyCallArgs &call) {
    return sMktMgr.GetMarketGroups();
}

PyResult MarketProxyService::StartupCheck(PyCallArgs &call) {
    //if (sMktMgr.NeedsUpdate())
    //    sMktMgr.UpdatePriceHistory();
    return nullptr;
}

PyResult MarketProxyService::GetCharOrders(PyCallArgs &call) {
    return MarketDB::GetOrdersForOwner(call.client->GetCharacterID());
}

PyResult MarketProxyService::GetCorporationOrders(PyCallArgs &call) {
    // the corp's orders are for its accountants, junior accountants and traders (UI/Market/Corporation AccessDenied*)
    if ((call.client->GetCorpRole() & (Corp::Role::Accountant | Corp::Role::JuniorAccountant | Corp::Role::Trader)) == 0)
        return MarketDB::GetOrdersForOwner(0);
    return MarketDB::GetOrdersForOwner(call.client->GetCorporationID());
}

/** @todo update these to use market manager and cache instead of hitting db? */
// station, system, region based on selection in market window
PyResult MarketProxyService::GetStationAsks(PyCallArgs &call) {
    return MarketDB::GetStationAsks(call.client->GetStationID());
}

PyResult MarketProxyService::GetSystemAsks(PyCallArgs &call) {
    return MarketDB::GetSystemAsks(call.client->GetSystemID());
}

PyResult MarketProxyService::GetRegionBest(PyCallArgs &call) {
    return MarketDB::GetRegionBest(call.client->GetRegionID());
}

// this is called 3x on every market transaction
PyResult MarketProxyService::GetOldPriceHistory(PyCallArgs &call, PyInt* typeID) {
    return sMktMgr.GetOldPriceHistory(call.client->GetRegionID(), typeID->value());
}

PyResult MarketProxyService::GetNewPriceHistory(PyCallArgs& call, PyInt* typeID) {
    return sMktMgr.GetNewPriceHistory(call.client->GetRegionID(), typeID->value());
}

PyResult MarketProxyService::CharGetNewTransactions(PyCallArgs &call, PyRep* sellBuy, PyRep* typeID, PyRep* clientID, PyRep* quantity, PyRep* fromDate, PyRep* maxPrice, PyRep* minPrice) {
    Market::TxData data = Market::TxData();

    data.clientID = clientID->IsNone() ? 0 : PyRep::IntegerValueU32(clientID);
    data.isBuy = sellBuy->IsNone() ? -1 : PyRep::IntegerValueU32(sellBuy);
    data.price = minPrice->IsNone() ? 0 : PyRep::FloatValue(minPrice);
    data.maxPrice = maxPrice->IsNone() ? 0 : PyRep::FloatValue(maxPrice);
    data.quantity = quantity->IsNone() ? 0 : PyRep::IntegerValueU32(quantity);
    data.typeID = typeID->IsNone() ? 0 : PyRep::IntegerValueU32(typeID);
    data.time = fromDate->IsNone() ? 0 : PyRep::IntegerValue(fromDate);
    data.accountKey = Account::KeyType::Cash;

    return MarketDB::GetTransactions(call.client->GetCharacterID(), data, false);
}

PyResult MarketProxyService::CorpGetNewTransactions(PyCallArgs& call, PyRep* sellBuy, PyRep* typeID, PyRep* clientID, PyRep* quantity, PyRep* fromDate, PyRep* maxPrice, PyRep* minPrice, PyRep* accountKey, PyRep* memberID)
{
    // corp wallet transactions are for accountants and junior accountants
    if ((call.client->GetCorpRole() & (Corp::Role::Accountant | Corp::Role::JuniorAccountant)) == 0)
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You do not have the role Accountant or Junior Accountant."));

    Market::TxData data = Market::TxData();
    data.clientID = clientID->IsNone() ? 0 : PyRep::IntegerValueU32(clientID);
    data.isBuy = sellBuy->IsNone() ? -1 : PyRep::IntegerValueU32(sellBuy);
    data.price = minPrice->IsNone() ? 0 : PyRep::FloatValue(minPrice);
    data.maxPrice = maxPrice->IsNone() ? 0 : PyRep::FloatValue(maxPrice);
    data.quantity = quantity->IsNone() ? 0 : PyRep::IntegerValueU32(quantity);
    data.typeID = typeID->IsNone() ? 0 : PyRep::IntegerValueU32(typeID);
    data.time = fromDate->IsNone() ? 0 : PyRep::IntegerValue(fromDate);
    data.accountKey = accountKey->IsNone() ? (uint32)Account::KeyType::Cash : PyRep::IntegerValueU32(accountKey);
    data.memberID = memberID->IsNone() ? 0 : PyRep::IntegerValueU32(memberID);
    return MarketDB::GetTransactions(call.client->GetCorporationID(), data, true);
}

PyResult MarketProxyService::GetOrders(PyCallArgs &call, PyInt* typeID) {
    PyRep* result(nullptr);
    std::string method_name ("GetOrders_");
    method_name += std::to_string(call.client->GetRegionID());
    method_name += "_";
    method_name += std::to_string(typeID->value());
    ObjectCachedMethodID method_id(GetName().c_str(), method_name.c_str());
    //check to see if this method is in the cache already.
    if (!this->m_cache->IsCacheLoaded(method_id))
    {
        //this method is not in cache yet, load up the contents and cache it.
        result = MarketDB::GetOrders(call.client->GetRegionID(), typeID->value());
        if (result == nullptr) {
            _log(MARKET__DB_ERROR, "Failed to load cache, generating empty contents.");
            result = PyStatic.NewNone();
        }
        this->m_cache->GiveCache(method_id, &result);
    }

    //now we know its in the cache one way or the other, so build a
    //cached object cached method call result.
    result = this->m_cache->MakeObjectCachedMethodCallResult(method_id);

    /*{'FullPath': u'UI/Messages', 'messageID': 258616, 'label': u'MktMarketOpeningTitle'}(u'Market not open yet', None, None)
     * {'FullPath': u'UI/Messages', 'messageID': 258617, 'label': u'MktMarketOpeningBody'}
     * (u'{region} market region is currently opening up for business and is not yet ready.
     * Please try again in a few minutes, or refer to another market region for your trading needs.',
     * None, {u'{region}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'region'}})
     */
    return result;
}

PyResult MarketProxyService::PlaceCharOrder(PyCallArgs &call, PyInt* stationID, PyInt* typeID, PyFloat* price, PyInt* quantity, PyInt* bid, PyInt* orderRange, std::optional <PyInt*> itemID, PyInt* minVolume, PyInt* duration, PyBool* useCorp, std::optional<PyRep*> located) {
    //self.GetMarketProxy().PlaceCharOrder(int(stationID), int(typeID), round(float(price), 2), int(quantity), int(bid), int(orderRange),
    //   itemID = None, int(minVolume = 1), int(duration = 14), useCorp = False, located = None)
    // located = [officeFolderID, officeID] or None

    /*
    21:47:34 [MktDump] Mkt::PlaceCharOrder()
    21:47:34 [MktDump]     Call_PlaceCharOrder
    21:47:34 [MktDump]     stationID=60014137
    21:47:34 [MktDump]     typeID=20327
    21:47:34 [MktDump]     price=100.0000000000000
    21:47:34 [MktDump]     quantity=1
    21:47:34 [MktDump]     bid=1
    21:47:34 [MktDump]     orderRange=4294967295
    21:47:34 [MktDump]     itemID=0
    21:47:34 [MktDump]     minVolume=1
    21:47:34 [MktDump]     duration=0
    21:47:34 [MktDump]     useCorp=0
    21:47:34 [MktDump]     located:
    21:47:34 [MktDump]               None
    */

    _log(MARKET__DUMP, "Mkt::PlaceCharOrder()");
    Client* pClient(call.client);

    // validate everything the client sent before any other logic.  a modified client can send anything here.
    if (quantity->value() < 1) {
        _log(MARKET__WARNING, "PlaceCharOrder - %s(%u) sent quantity %i for type %i.", \
                pClient->GetName(), pClient->GetCharacterID(), quantity->value(), typeID->value());
        if (bid->value())
            throw CustomError("Invalid quantity.");
        throw UserError("RepackageBeforeSelling").AddTypeName("item", typeID->value());
    }
    // client rounds to 2 decimals
    double orderPrice = std::round(price->value() * 100.0) / 100.0;
    if (!ValidOrderPrice(orderPrice))
        throw CustomError("Invalid price.");
    if (!ValidOrderDuration(duration->value()))
        throw CustomError("Invalid order duration.");
    if (!sDataMgr.IsStation(stationID->value()))
        throw CustomError("Invalid station.");
    if (!ValidOrderRange(orderRange->value()))
        throw CustomError("Invalid order range.");

    bool isBuy(bid->value() != 0);
    uint32 orderStationID(stationID->value());
    uint32 qty(quantity->value());
    uint16 orderTypeID(typeID->value());

    // only types with a market group are traded on the market
    const ItemType* pType(sItemFactory.GetType(orderTypeID));
    if ((pType == nullptr) or (pType->marketGroupID() == 0))
        throw UserError(isBuy ? "MktInvalidMarketTypeBid" : "MktInvalidMarketTypeAsk").AddTypeName("itemType", orderTypeID);

    // orders are entered in the client's region
    if (sDataMgr.GetStationRegion(orderStationID) != pClient->GetRegionID())
        throw UserError("MktInvalidRegion");

    // the item to sell
    InventoryItemRef iRef(nullptr);
    if (!isBuy) {
        // a bid with an itemID used to fall through to the sell code and created a buy order without escrow
        if (!itemID.has_value() or (itemID.value()->value() == 0))
            throw CustomError("Unable to find item to sell.");
        iRef = sItemFactory.GetItemRef(itemID.value()->value());
        if (iRef.get() == nullptr) {
            _log(ITEM__ERROR, "PlaceCharOrder - Failed to find item %i for sell order.", itemID.value()->value());
            throw CustomError("Unable to find item to sell.");
        }
        if (iRef->typeID() != orderTypeID) {
            _log(MARKET__MESSAGE, "PlaceCharOrder - Denying Sell of typeID %u using typeID %u.", iRef->typeID(), orderTypeID);
            throw CustomError("Invalid sell order item type.");
        }
    }

    // corp orders.  corp items can only be sold for the corp
    uint32 corpID(pClient->GetCorporationID());
    bool corpOrder(useCorp->value());
    if ((iRef.get() != nullptr) and (iRef->ownerID() == corpID))
        corpOrder = true;
    uint16 accountKey(Account::KeyType::Cash);
    if (corpOrder) {
        if (!IsPlayerCorp(corpID))
            throw CustomError("You cannot use the market for an NPC corporation.");
        // quote.py only offers the corp wallet to accountants and traders
        if ((pClient->GetCorpRole() & (Corp::Role::Accountant | Corp::Role::Trader)) == 0)
            throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You do not have the role Accountant or Trader."));
        accountKey = pClient->GetCorpAccountKey();
        if (Corp::AccountTakeRole(accountKey) == 0)
            accountKey = Account::KeyType::Cash;
        // fees and escrow come from the session's wallet division
        if ((pClient->GetCorpRole() & Corp::AccountTakeRole(accountKey)) == 0)
            throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You do not have access to that wallet division."));
    }
    uint32 walletOwnerID(corpOrder ? corpID : pClient->GetCharacterID());

    if (isBuy) {
        if (duration->value() == 0) {
            // immediate.  fill from the cheapest sell orders the range (around the chosen station) reaches
            uint32 filled = sMktMgr.BuyFromSellOrders(pClient, orderTypeID, qty, orderPrice, orderStationID, orderRange->value(), corpOrder, accountKey);
            if (filled == 0) {
                _log(MARKET__TRACE, "PlaceCharOrder - Failed to satisfy buy order for %u of type %u at %.2f ISK.", qty, orderTypeID, orderPrice);
                throw UserError("MktOrderDidNotMatch");
            }
            if (filled < qty)
                pClient->SendNotifyMsg("Only %u of the %u units could be bought at that price.", filled, qty);
            return nullptr;
        }

        // standing buy orders can be placed as far away as Procurement allows
        CheckSkillRange(pClient, orderStationID, EvESkill::Procurement);
        CheckOrderCount(pClient);

        // Margin Trading:  only part of the order's value goes into escrow
        double value(orderPrice * qty);
        double escrow(value);
        if (sConfig.market.UseMarginTrading)
            escrow *= std::pow(0.75, pClient->GetChar()->GetSkillLevel(EvESkill::MarginTrading));
        escrow = std::round(escrow * 100.0) / 100.0;

        // broker fee uses the standings of the order's station owner, on the full value
        double fee = GetBrokerFee(pClient, orderStationID, value);
        _log(MARKET__DEBUG, "PlaceCharOrder(buy) - %s: Escrow: %.2f, Fee: %.2f", corpOrder ? "Corp" : "Player", escrow, fee);

        // the fee and escrow are taken after the order is stored (they reference its orderID).  check the wallet first,
        //  so an unpaid order is never left behind (cancelling it would refund escrow that was never paid)
        CheckFunds(walletOwnerID, accountKey, fee + escrow);

        // set save data
        Market::SaveData data = Market::SaveData();
        data.accountKey         = accountKey;
        data.minVolume          = std::max(1, std::min(minVolume->value(), quantity->value()));
        data.issued             = GetFileTimeNow();
        data.bid                = true;
        data.isCorp             = corpOrder;
        data.typeID             = orderTypeID;
        data.orderRange         = orderRange->value();
        data.ownerID            = walletOwnerID;
        data.solarSystemID      = sDataMgr.GetStationSystem(orderStationID);
        data.regionID           = sDataMgr.GetStationRegion(orderStationID);
        data.stationID          = orderStationID;
        data.price              = orderPrice;
        data.volEntered         = qty;
        data.volRemaining       = qty;
        data.duration           = duration->value();
        data.memberID           = corpOrder ? pClient->GetCharacterID() : 0;
        data.escrow             = escrow;
        data.contraband         = false;
        data.jumps              = 1;

        // create buy order
        uint32 orderID(MarketDB::StoreOrder(data));
        if (orderID == 0) {
            _log(MARKET__ERROR, "PlaceCharOrder - Failed to record buy order in the DB.");
            throw CustomError("Failed to record the order.");
        }

        std::string reason = "DESC:  Setting up buy order in ";
        reason += stDataMgr.GetStationName(orderStationID).c_str();

        // take monies and record actions.  if either transfer fails, the order must go
        uint32 stationOwnerID(stDataMgr.GetOwnerID(orderStationID));
        try {
            AccountService::TransferFunds(walletOwnerID, stationOwnerID, fee, reason.c_str(), Journal::EntryType::Brokerfee, orderID, accountKey, Account::KeyType::Cash, pClient);
            AccountService::TransferFunds(walletOwnerID, stationOwnerID, escrow, reason.c_str(), Journal::EntryType::MarketEscrow, orderID, accountKey, Account::KeyType::Escrow, pClient);
        } catch (...) {
            MarketDB::DeleteOrder(orderID);
            throw;
        }

        //send notification of new order...
        sMktMgr.InvalidateOrdersCache(data.regionID, orderTypeID);
        sMktMgr.SendOnOwnOrderChanged(pClient, orderID, Market::Action::Add, corpOrder);
        return nullptr;
    }

    _log(MARKET__DUMP, "Mkt::PlaceCharOrder(): appears to be a sell order");

    if ((uint32)iRef->quantity() < qty)
        throw CustomError("You cannot sell %u %s when you only have %i.  If applicable, merge stacks and try again.", qty, iRef->name(), iRef->quantity());
    if (iRef->itemID() == pClient->GetShipID())
        throw CustomError("You cannot sell your active ship.");
    // assembled items (and blueprint copies) cannot be sold on the market
    if (iRef->isSingleton())
        throw UserError("RepackageBeforeSelling").AddTypeName("item", iRef->typeID());
    if ((iRef->categoryID() == EVEDB::invCategories::Blueprint) and BlueprintRef::StaticCast(iRef)->copy())
        throw CustomError("Blueprint copies cannot be sold on the market.");

    // verify the item's location
    //  (a) own item in the item hangar of the order's station
    //  (b) own item in the active ship's cargo hold, docked in the order's station
    //  (c) corp item in an office hangar of the order's station the member can take from (located = [officeFolderID, officeID])
    bool inStationHangar(false), inShip(false), inOffice(false);
    if (iRef->ownerID() == pClient->GetCharacterID()) {
        inStationHangar = ((iRef->locationID() == orderStationID) and (iRef->flag() == flagHangar));
        inShip = ((pClient->GetStationID() == orderStationID)
              and (iRef->locationID() == pClient->GetShipID())
              and (iRef->flag() == flagCargoHold));
    } else if (iRef->ownerID() == corpID) {
        uint32 officeID(stDataMgr.GetOfficeIDForCorp(orderStationID, corpID));
        if ((officeID != 0) and (iRef->locationID() == officeID)) {
            int64 takeRole(Corp::HangarTakeRole(iRef->flag()));
            if ((takeRole == 0) or ((pClient->GetCorpRolesAtLocation(orderStationID) & takeRole) == 0))
                throw UserError("ConNoAccessToCorpHangar");
            inOffice = true;
        }
    } else {
        _log(MARKET__WARNING, "%s(%u) Tried to sell %i %s owned by %u in %s.", pClient->GetName(), pClient->GetCharacterID(), \
                iRef->quantity(), iRef->name(), iRef->ownerID(), stDataMgr.GetStationName(orderStationID).c_str());
        throw CustomError("You cannot sell items you do not own.");
    }

    if (!inStationHangar and !inShip and !inOffice) {
        _log(MARKET__ERROR, "%s Trying to sell %s(%u) in %u (flag %u) through %s while in %s", pClient->GetName(), iRef->name(), \
                iRef->itemID(), iRef->locationID(), iRef->flag(), stDataMgr.GetStationName(orderStationID).c_str(), \
                stDataMgr.GetStationName(pClient->GetStationID()).c_str());
        throw CustomError("You cannot sell %s from %s.  It is not in a hangar of that station.", iRef->name(), stDataMgr.GetStationName(orderStationID).c_str());
    }

    // remote sells are limited by Marketing (the item's station)
    if (!inShip)
        CheckSkillRange(pClient, orderStationID, EvESkill::Marketing);

    // is this standing order or immediate?
    if (duration->value() == 0) {
        // immediate.  fill the best buy orders whose range reaches this station
        uint32 sold = sMktMgr.SellToBuyOrders(pClient, iRef, qty, orderPrice, orderStationID, corpOrder, accountKey);
        if (sold == 0) {
            _log(MARKET__TRACE, "PlaceCharOrder - failed to find a matching buy order for %u of type %u at %.2f ISK.", qty, orderTypeID, orderPrice);
            throw UserError("MktOrderDidNotMatch");
        }
        if (sold < qty)
            pClient->SendNotifyMsg("Only %u of the %u units could be sold at that price.", sold, qty);
        return nullptr;
    }

    // they will be placing a sell order:
    CheckOrderCount(pClient);

    // broker fee on the order's value, using the standings of the order's station owner (taxes are paid when it sells)
    double total = orderPrice * qty;
    double fee = GetBrokerFee(pClient, orderStationID, total);
    _log(MARKET__DEBUG, "PlaceCharOrder(sell) - %s: Total: %.2f, Fee: %.2f", corpOrder ? "Corp" : "Player", total, fee);
    CheckFunds(walletOwnerID, accountKey, fee);

    // set save data
    Market::SaveData data = Market::SaveData();
    data.accountKey         = accountKey;
    data.minVolume          = 1;
    data.issued             = GetFileTimeNow();
    data.bid                = false;
    data.isCorp             = corpOrder;
    data.typeID             = orderTypeID;
    data.orderRange         = orderRange->value();
    data.ownerID            = walletOwnerID;
    data.solarSystemID      = sDataMgr.GetStationSystem(orderStationID);
    data.regionID           = sDataMgr.GetStationRegion(orderStationID);
    data.stationID          = orderStationID;
    data.price              = orderPrice;
    data.volEntered         = qty;
    data.volRemaining       = qty;
    data.duration           = duration->value();
    data.memberID           = corpOrder ? pClient->GetCharacterID() : 0;
    data.contraband         = iRef->contraband();
    data.jumps              = 1;

    // store the order in the DB.
    uint32 orderID(MarketDB::StoreOrder(data));
    if (orderID == 0) {
        _log(MARKET__ERROR, "PlaceCharOrder - Failed to record sell order in the DB.");
        throw CustomError("Failed to record the order.");
    }

    std::string reason = "DESC:  Setting up sell order in ";
    reason += stDataMgr.GetStationName(orderStationID).c_str();
    try {
        AccountService::TransferFunds(walletOwnerID, stDataMgr.GetOwnerID(orderStationID), fee, reason.c_str(), Journal::EntryType::Brokerfee, orderID, accountKey, Account::KeyType::Cash, pClient);
    } catch (...) {
        MarketDB::DeleteOrder(orderID);
        throw;
    }

    // take the items
    if ((uint32)iRef->quantity() == qty) {
        iRef->Delete();
    } else if (!iRef->AlterQuantity(-((int32)qty), true)) {
        _log(MARKET__ERROR, "PlaceCharOrder - Failed to consume %u units from %s", qty, iRef->name());
        // the items stay with the seller, so the order must go.  the fee was the price of trying
        MarketDB::DeleteOrder(orderID);
        throw CustomError("Failed to take the items for the order.");
    }

    //notify client about new order.
    sMktMgr.InvalidateOrdersCache(data.regionID, orderTypeID);
    sMktMgr.SendOnOwnOrderChanged(pClient, orderID, Market::Action::Add, corpOrder);

    return nullptr;
}

PyResult MarketProxyService::PlaceCharOrder(PyCallArgs &call, PyInt* stationID, PyInt* typeID, PyFloat* price, PyInt* quantity, PyInt* bid, PyInt* orderRange, std::optional <PyInt*> itemID, PyInt* minVolume, PyInt* duration, PyInt* useCorp, std::optional<PyRep*> located) {
    // Used when character has a corp wallet, client sends a PyInt instead of a PyBool
    PyBool* corp(new PyBool(useCorp->value() != 0));
    try {
        PyResult res = PlaceCharOrder(call, stationID, typeID, price, quantity, bid, orderRange, itemID, minVolume, duration, corp, located);
        PyDecRef(corp);
        return res;
    } catch (...) {
        PyDecRef(corp);
        throw;
    }
}

PyResult MarketProxyService::ModifyCharOrder(PyCallArgs &call, PyInt* orderID, PyFloat* newPrice, PyInt* bid, PyInt* stationID, PyInt* solarSystemID, PyFloat* price, PyInt* range, PyInt* volRemaining, PyLong* issueDate) {
    // client coded to throw error if price > 9223372036854.0
    // only the orderID and newPrice are used.  everything else comes from the db
    Market::OrderInfo oInfo = Market::OrderInfo();
    if (!MarketDB::GetOrderInfo(orderID->value(), oInfo)) {
        _log(MARKET__ERROR, "ModifyCharOrder - Failed to get info about order #%i.", orderID->value());
        throw CustomError("That order no longer exists.");
    }

    CheckOrderOwner(call.client, oInfo);

    // remote modifications are limited by Daytrading
    CheckSkillRange(call.client, oInfo.stationID, EvESkill::Daytrading);

    double priceNew = std::round(newPrice->value() * 100.0) / 100.0;
    if (!ValidOrderPrice(priceNew))
        throw CustomError("Invalid price.");

    if (!sMktMgr.CanModifyOrder(oInfo.orderID, oInfo.issued)) {
        uint32 minutes = (sConfig.market.OrderModificationDelay + 59) / 60;
        throw CustomError("You can only modify a market order once every %u minute%s.", minutes, (minutes == 1 ? "" : "s"));
    }

    uint32 walletOwnerID(oInfo.isCorp ? oInfo.ownerID : call.client->GetCharacterID());
    uint16 accountKey(oInfo.isCorp ? oInfo.accountKey : Account::KeyType::Cash);
    uint32 stationOwnerID(stDataMgr.GetOwnerID(oInfo.stationID));
    std::string reason = "DESC:  Altering Market Order #";
    reason += std::to_string(oInfo.orderID);

    /* pre-Citadel rule: modifying an order costs the broker fee on the change in order value (minimum 100 ISK).
     *  this is taken first and throws if the wallet is short; nothing has changed at that point. */
    double fee = GetBrokerFee(call.client, oInfo.stationID, fabs(priceNew - oInfo.price) * oInfo.quantity);
    // a buy order's escrow follows the price change (times the Margin Trading fraction).  it never goes below zero
    double newEscrow(-1.0), delta(0.0);
    if (oInfo.isBuy) {
        double margin(1.0);
        if (sConfig.market.UseMarginTrading)
            margin = std::pow(0.75, call.client->GetChar()->GetSkillLevel(EvESkill::MarginTrading));
        newEscrow = std::round(EvE::max(oInfo.escrow + (priceNew - oInfo.price) * oInfo.quantity * margin, 0.0) * 100.0) / 100.0;
        delta = newEscrow - oInfo.escrow;
    }
    // check both, so the fee isn't taken when the escrow can't be
    CheckFunds(walletOwnerID, accountKey, fee + EvE::max(delta, 0.0));
    AccountService::TransferFunds(walletOwnerID, stationOwnerID, fee, reason.c_str(), Journal::EntryType::Brokerfee, oInfo.orderID, accountKey, Account::KeyType::Cash, call.client);

    if (delta >= 0.01) {
        AccountService::TransferFunds(walletOwnerID, stationOwnerID, delta, reason.c_str(), Journal::EntryType::MarketEscrow, oInfo.orderID, accountKey, Account::KeyType::Escrow, call.client);
    } else if (delta <= -0.01) {
        AccountService::TransferFunds(stationOwnerID, walletOwnerID, -delta, reason.c_str(), Journal::EntryType::MarketEscrow, oInfo.orderID, Account::KeyType::Escrow, accountKey, call.client);
    }

    if (!MarketDB::AlterOrderPrice(oInfo.orderID, priceNew, newEscrow)) {
        _log(MARKET__ERROR, "ModifyCharOrder - Failed to modify price for order #%u.", oInfo.orderID);
        return nullptr;
    }
    sMktMgr.SetOrderModified(oInfo.orderID);

    sMktMgr.InvalidateOrdersCache(oInfo.regionID, oInfo.typeID);
    sMktMgr.NotifyOrderOwner(oInfo, Market::Action::Modify);

    return nullptr;
}

PyResult MarketProxyService::CancelCharOrder(PyCallArgs &call, PyInt* orderID, PyInt* regionID) {
    Market::OrderInfo oInfo = Market::OrderInfo();
    if (!MarketDB::GetOrderInfo(orderID->value(), oInfo)) {
        _log(MARKET__ERROR, "CancelCharOrder - Failed to get info about order #%i.", orderID->value());
        throw CustomError("That order no longer exists.");
    }

    // orders can be cancelled from anywhere, but only by their owner
    CheckOrderOwner(call.client, oInfo);

    // Crucible:  not within five minutes of placing it
    if ((sConfig.market.OrderCancelDelay > 0)
    and ((GetFileTimeNow() - oInfo.issued) < (int64)sConfig.market.OrderCancelDelay * EvE::Time::Second))
        throw UserError("MktOrderTooNew");

    // refunds the escrow left in a buy order (not price * volRemaining, see Margin Trading) or returns a sell order's items
    std::string reason = "DESC:  Canceling Market Order #";
    reason += std::to_string(oInfo.orderID);
    if (!sMktMgr.CancelOrderInternal(oInfo, reason.c_str()))
        throw CustomError("That order no longer exists.");

    return nullptr;
}

void MarketProxyService::CheckOrderOwner(Client* pClient, const Market::OrderInfo& oInfo)
{
    if (oInfo.isCorp) {
        if ((oInfo.ownerID == pClient->GetCorporationID())
        and ((pClient->GetCorpRole() & (Corp::Role::Trader | Corp::Role::Accountant)) != 0))
            return;
    } else if (oInfo.ownerID == pClient->GetCharacterID()) {
        return;
    }

    _log(MARKET__WARNING, "%s(%u) tried to alter order #%u owned by %u.", \
            pClient->GetName(), pClient->GetCharacterID(), oInfo.orderID, oInfo.ownerID);
    throw CustomError("You do not own that market order.");
}

void MarketProxyService::CheckSkillRange(Client* pClient, uint32 stationID, uint16 skillTypeID)
{
    // quote.py CanTradeAtStation and its messages
    int32 distance(Market::GetStationDistance(pClient, stationID));
    uint8 level(pClient->GetChar()->GetSkillLevel(skillTypeID));
    if (Market::InSkillRange(distance, level))
        return;
    if (distance == Market::Range::Region)
        throw UserError("MktInvalidRegion");

    int32 limit(Market::SkillRangeLimit(level));
    if (limit >= 0)
        throw UserError("MktCantSellItem2")
                .AddFormatValue("numJumps", new PyInt(distance))
                .AddFormatValue("jumpText1", new PyString(distance == 1 ? "jump" : "jumps"))
                .AddFormatValue("numLimit", new PyInt(limit))
                .AddFormatValue("jumpText2", new PyString(limit == 1 ? "jump" : "jumps"));
    throw UserError("MktCantSellItemOutsideStation")
            .AddFormatValue("numJumps", new PyInt(distance))
            .AddFormatValue("jumpText", new PyString(distance == 1 ? "jump" : "jumps"));
}

void MarketProxyService::CheckOrderCount(Client* pClient)
{
    // marketsvc.GetSkillLimits.  corp orders count against the member that placed them
    Character* pChar(pClient->GetChar().get());
    uint32 limit = 5 + 4 * pChar->GetSkillLevel(EvESkill::Trade) + 8 * pChar->GetSkillLevel(EvESkill::Retail)
                 + 16 * pChar->GetSkillLevel(EvESkill::Wholesale) + 32 * pChar->GetSkillLevel(EvESkill::Tycoon);
    if (MarketDB::CountOpenOrders(pClient->GetCharacterID()) >= limit)
        throw CustomError("You have reached your limit of %u open market orders.  Train Trade, Retail, Wholesale or Tycoon to place more.", limit);
}

void MarketProxyService::CheckFunds(uint32 walletOwnerID, uint16 accountKey, double amount)
{
    double balance(MarketMgr::GetWalletBalance(walletOwnerID, accountKey));
    if (balance >= amount)
        return;
    if (IsPlayerCorp(walletOwnerID))
        throw UserError("NotEnoughMoneyCorp")
                .AddOwnerName("owner", walletOwnerID)
                .AddISK("amount", amount)
                .AddISK("balance", balance)
                .AddFormatValue("division", new PyString(CorporationDB::GetDivisionName(walletOwnerID, accountKey)));
    throw UserError("NotEnoughMoney")
            .AddISK("amount", amount)
            .AddISK("balance", balance);
}

bool MarketProxyService::ValidOrderRange(int32 range)
{
    // rangeStation = -1, rangeSolarSystem = 0, 1-5, 10, 20, 30, 40 jumps, rangeRegion = 32767
    switch (range) {
        case -1: case 0: case 1: case 2: case 3: case 4: case 5:
        case 10: case 20: case 30: case 40: case 32767:
            return true;
    }
    return false;
}

bool MarketProxyService::ValidOrderDuration(int32 duration)
{
    // days.  0 = immediate
    switch (duration) {
        case 0: case 1: case 3: case 7: case 14: case 30: case 90:
            return true;
    }
    return false;
}

bool MarketProxyService::ValidOrderPrice(double price)
{
    // also false for NaN
    return ((price >= 0.01) and (price <= 9223372036854.0));
}

double MarketProxyService::GetBrokerFee(Client* pClient, uint32 stationID, double value)
{
    uint8 lvl(pClient->GetChar()->GetSkillLevel(EvESkill::BrokerRelations));
    uint32 stationOwnerID(stDataMgr.GetOwnerID(stationID));
    float ownerStanding(StandingDB::GetStanding(stationOwnerID, pClient->GetCharacterID()));
    float factionStanding(0.0f);
    if (IsNPCCorp(stationOwnerID))
        factionStanding = StandingDB::GetStanding(sDataMgr.GetCorpFaction(stationOwnerID), pClient->GetCharacterID());
    return EvEMath::Market::BrokerFee(lvl, factionStanding, ownerStanding, value);
}
