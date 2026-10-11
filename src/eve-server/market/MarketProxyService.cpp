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
    data.price = minPrice->IsNone() ? 0 : PyRep::IntegerValueU32(minPrice);
    data.quantity = quantity->IsNone() ? 0 : PyRep::IntegerValueU32(quantity);
    data.typeID = typeID->IsNone() ? 0 : PyRep::IntegerValueU32(typeID);
    data.time = fromDate->IsNone() ? 0 : PyRep::IntegerValue(fromDate);
    data.accountKey = Account::KeyType::Cash;

    return MarketDB::GetTransactions(call.client->GetCharacterID(), data);
}

PyResult MarketProxyService::CorpGetNewTransactions(PyCallArgs& call, PyRep* sellBuy, PyRep* typeID, PyRep* clientID, PyRep* quantity, PyRep* fromDate, PyRep* maxPrice, PyRep* minPrice, PyRep* accountKey, PyRep* memberID)
{
    Market::TxData data = Market::TxData();
        data.clientID = clientID->IsNone() ? 0 : PyRep::IntegerValueU32(clientID);
        data.isBuy = sellBuy->IsNone() ? 0 : PyRep::IntegerValueU32(sellBuy);
        data.price = minPrice->IsNone() ? 0 : PyRep::IntegerValueU32(minPrice);
        data.quantity = quantity->IsNone() ? 0 : PyRep::IntegerValueU32(quantity);
        data.typeID = typeID->IsNone() ? 0 : PyRep::IntegerValueU32(typeID);
        data.time = fromDate->IsNone() ? 0 : PyRep::IntegerValue(fromDate);
        data.accountKey = accountKey->IsNone() ? 0 : PyRep::IntegerValueU32(accountKey);
        data.memberID = memberID->IsNone() ? 0 : PyRep::IntegerValueU32(memberID);
    return MarketDB::GetTransactions(call.client->GetCorporationID(), data);
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

    // not sent from call, but used in transactions
    uint16 accountKey(Account::KeyType::Cash);

    _log(MARKET__DUMP, "Mkt::PlaceCharOrder()");

    //TODO: verify the validity of args.stationID (range vs. skill)

    if (useCorp->value()) {
        _log(MARKET__MESSAGE, "Denying Corp Market use for %s", call.client->GetName());
        call.client->SendErrorMsg("Corporation Market transactions are not available at this time.");
        return nullptr;
    }

    // validate everything the client sent before any other logic.  a modified client can send anything here.
    if (quantity->value() < 1) {
        _log(MARKET__WARNING, "PlaceCharOrder - %s(%u) sent quantity %i for type %i.", \
                call.client->GetName(), call.client->GetCharacterID(), quantity->value(), typeID->value());
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

    /** @todo  update for corporate use
     * check corp has office in target station for item delivery
     * check player can use given corp acct (get current division from char data)
     * check corp wallet division has funds (get current division from char data)
     * code to handle 'located' as sent
     *   ...more?
     * corp checks coded for ram/reproc.  use as template
     */

    // a bid with an itemID used to fall through to the sell code and created a buy order without escrow
    if (bid->value()) {  // buy
        // check for corp usage and get standings with station owners
        float fStanding(0), cStanding(0);
        if (useCorp->value()) {
            // it is. perform checks and set needed variables for corp use
            if (!IsPlayerCorp(call.client->GetCorporationID())) {
                // cant buy items for npc corp...
                call.client->SendErrorMsg("You cannot buy items for an NPC corp.");
                return nullptr;
            }

            // this is a corp transaction. verify char can buy things for corp...
            // some corp error msgs in inventory.h, corp.h and market.h
            //   will need corp methods to determine member access rights for item location and roles....
            //   these may be written already.  will have to check

            accountKey = call.client->GetCorpAccountKey();
        }

        // is this standing order or immediate?
        if (duration->value() == 0) {
            // immediate purchases can be made anywhere in the current region
            if (sDataMgr.GetStationRegion(stationID->value()) != call.client->GetRegionID())
                throw CustomError("You can only buy items in your current region.");

            // immediate. look for open sell order that matches all reqs (price, qty, distance, etc)
            // check distance, set order range and make station list.
            uint32 orderID(MarketDB::FindSellOrder(
                typeID->value(),
                stationID->value(),
                quantity->value(),
                orderPrice
            ));

            if (orderID) {
                // found one.
                _log(MARKET__TRACE,
                    "PlaceCharOrder - Found sell order #%u in %s for %s. (type %i, price %.2f, qty %i, range %i)",
                    orderID,
                    stDataMgr.GetStationName(stationID->value()).c_str(),
                    call.client->GetName(),
                    typeID->value(),
                    orderPrice,
                    quantity->value(),
                    orderRange->value()
                );

                sMktMgr.ExecuteSellOrder(
                    call.client,
                    orderID,
                    quantity->value(),
                    orderPrice,
                    stationID->value(),
                    typeID->value(),
                    useCorp->value()
                );

                return nullptr;
            }

            _log(MARKET__TRACE,
                "PlaceCharOrder - Failed to satisfy buy order for %i of type %i at %.2f ISK.",
                quantity->value(),
                typeID->value(),
                orderPrice
            );

            call.client->SendErrorMsg("No sell order found.");  // find/implement type name here

            return nullptr;
        }

        // standing buy orders can be placed as far away as Procurement allows
        if (!InTradeRange(call.client, stationID->value(), call.client->GetChar()->GetSkillLevel(EvESkill::Procurement)))
            throw CustomError("%s is out of range.  Your Procurement skill limits how far away you can place buy orders.", stDataMgr.GetStationName(stationID->value()).c_str());

        // determine escrow amount
        double money((double)orderPrice * quantity->value());

        // set save data
        Market::SaveData data = Market::SaveData();
        data.accountKey         = accountKey;
        data.minVolume          = 1; // verify this
        data.issued             = GetFileTimeNow();
        // fill items from sent data
        data.bid                = bid->value();
        data.isCorp             = useCorp->value();
        data.typeID             = typeID->value();
        data.orderRange         = orderRange->value();
        data.ownerID            = useCorp->value() ?call.client->GetCorporationID():call.client->GetCharacterID();
        data.solarSystemID      = sDataMgr.GetStationSystem(stationID->value());
        data.regionID           = sDataMgr.GetStationRegion(stationID->value());
        data.stationID          = stationID->value();
        data.price              = orderPrice;
        data.volEntered         = quantity->value();
        data.volRemaining       = quantity->value();
        data.duration           = duration->value();
        data.memberID           = useCorp->value() ?call.client->GetCharacterID():0;
        data.escrow             = money;

        // these need a bit more data
        data.contraband = false;   // does this need to check region/system?  yes
        data.jumps = 1;     // not sure if this is used....

        // broker fee uses the standings of the order's station owner
        double fee = GetBrokerFee(call.client, stationID->value(), money);
        _log(MARKET__DEBUG, "PlaceCharOrder(buy) - %s: Escrow: %.2f, Fee: %.2f", useCorp->value() ?"Corp":"Player", money, fee);

        // the fee and escrow are taken after the order is stored (they reference its orderID).  check the wallet first,
        //  so an unpaid order is never left behind (cancelling it would refund escrow that was never paid)
        if (!useCorp->value() and (call.client->GetBalance() < (fee + money)))
            throw UserError("NotEnoughMoney")
                    .AddISK("amount", fee + money)
                    .AddISK("balance", call.client->GetBalance());

        // create buy order
        uint32 orderID(MarketDB::StoreOrder(data));
        if (orderID == 0) {
            _log(MARKET__ERROR, "PlaceCharOrder - Failed to record buy order in the DB.");
            call.client->SendErrorMsg("Failed to record the order.");
            return nullptr;
        }

        std::string reason = "DESC:  Setting up buy order in ";
        reason += stDataMgr.GetStationName(stationID->value()).c_str();

        // take monies and record actions.  if either transfer fails, the order must go
        try {
            if (useCorp->value()) {
                AccountService::TransferFunds(call.client->GetCorporationID(), stDataMgr.GetOwnerID(stationID->value()), fee, reason.c_str(), Journal::EntryType::Brokerfee, orderID, accountKey, Account::KeyType::Cash, call.client);
                AccountService::TransferFunds(call.client->GetCorporationID(), stDataMgr.GetOwnerID(stationID->value()), money, reason.c_str(), Journal::EntryType::MarketEscrow, orderID, accountKey, Account::KeyType::Escrow, call.client);
            } else {
                AccountService::TransferFunds(call.client->GetCharacterID(), stDataMgr.GetOwnerID(stationID->value()), fee, reason.c_str(), Journal::EntryType::Brokerfee, orderID, accountKey);
                AccountService::TransferFunds(call.client->GetCharacterID(), stDataMgr.GetOwnerID(stationID->value()), money, reason.c_str(), Journal::EntryType::MarketEscrow, orderID, accountKey, Account::KeyType::Escrow);
            }
        } catch (...) {
            MarketDB::DeleteOrder(orderID);
            throw;
        }

        //send notification of new order...
        sMktMgr.InvalidateOrdersCache(data.regionID, typeID->value());
        sMktMgr.SendOnOwnOrderChanged(call.client, orderID, Market::Action::Add, useCorp->value());
    } else {
        _log(MARKET__DUMP, "Mkt::PlaceCharOrder(): appears to be a sell order");
        // sell order
        /*if (!args.located->IsNone()) {
            //  corp item in corp hangar
            // located = [officeFolderID, officeID] or None
            PyTuple* located = args.located->AsTuple();
        }*/

        if (!itemID.has_value() or (itemID.value()->value() == 0))
            throw CustomError("Unable to find item to sell.");

        //verify that they actually have the item in the quantity specified...
        InventoryItemRef iRef = sItemFactory.GetItemRef(itemID.value ()->value());
        if (iRef.get() == nullptr) {
            _log(ITEM__ERROR, "PlaceCharOrder - Failed to find item %i for sell order.", itemID.value ()->value());

            call.client->SendErrorMsg("Unable to find item to sell.");

            return nullptr;
        }

        if (iRef->typeID() != typeID->value()) {
            _log(MARKET__MESSAGE, "PlaceCharOrder - Denying Sell of typeID %u using typeID %i.", iRef->typeID(), typeID->value());

            call.client->SendErrorMsg("Invalid sell order item type.");

            return nullptr;
        }

        if (iRef->quantity() < quantity->value()) {
            // trying to sell more than they have
            _log(MARKET__MESSAGE,
                "PlaceCharOrder - Denying inflated qty for %s",
                call.client->GetName());
                call.client->SendErrorMsg("You cannot sell %i %s when you only have %i.  If applicable, merge stacks and try again.",
                quantity->value(),
                iRef->name(),
                iRef->quantity()
            );

            return nullptr;
        }

        // verify right to sell this thing..
        if (useCorp->value()) {
            if (!IsPlayerCorp(call.client->GetCorporationID())) {
                // cant sell npc corp items...
                call.client->SendErrorMsg("You cannot sell items for an NPC corp.");
                return nullptr;
            }

            // this is a corp transaction.  verify char can sell corp shit...
            // some corp error msgs in inventory.h, corp.h and market.h
            //   will need corp methods to determine member access rights for item location and roles....
            //   these may be written already.  will have to check
            accountKey = call.client->GetCorpAccountKey();
        } else {
            if ( iRef->ownerID() != call.client->GetCharacterID()) {
                _log(MARKET__WARNING,
                    "%s(%u) Tried to sell %i %s owned by %u in %s.",
                    call.client->GetName(),
                    call.client->GetCharID(),
                    iRef->quantity(),
                    iRef->name(),
                    iRef->ownerID(),
                    stDataMgr.GetStationName(stationID->value()).c_str()
                );

                call.client->SendErrorMsg("You cannot sell items you do not own.");

                return nullptr;
            }
        }

        if (iRef->itemID() == call.client->GetShipID())
            throw CustomError("You cannot sell your active ship.");
        // assembled items (and blueprint copies) cannot be sold on the market
        if (iRef->isSingleton())
            throw UserError("RepackageBeforeSelling").AddTypeName("item", iRef->typeID());
        if ((iRef->categoryID() == EVEDB::invCategories::Blueprint) and BlueprintRef::StaticCast(iRef)->copy())
            throw CustomError("Blueprint copies cannot be sold on the market.");

        // verify valid location
        //  (a) personal hangar of the order's station, within Marketing range of the seller
        //  (b) cargo hold of the active ship, docked in the order's station
        bool itemInStationHangar((iRef->locationID() == stationID->value()) and (iRef->flag() == flagHangar));
        bool itemInShip((call.client->GetStationID() == stationID->value())
                    and (iRef->locationID() == call.client->GetShipID())
                    and (iRef->flag() == flagCargoHold));

        if (itemInStationHangar and !itemInShip
        and !InTradeRange(call.client, stationID->value(), call.client->GetChar()->GetSkillLevel(EvESkill::Marketing)))
            throw CustomError("%s is out of range.  Your Marketing skill limits how far away you can sell items.", stDataMgr.GetStationName(stationID->value()).c_str());

        if (!itemInStationHangar and !itemInShip) {
            std::string itemLoc;
            if (sDataMgr.IsStation(iRef->locationID())) {
                itemLoc = stDataMgr.GetStationName(iRef->locationID());
            } else if (sDataMgr.IsSolarSystem(iRef->locationID())) {
                itemLoc = sDataMgr.GetSystemName(iRef->locationID());
            } else {
                itemLoc = "an Invalid Location";
            }

            _log(MARKET__ERROR,
                "%s Trying to sell %s(%u) in %s through %s while in %s",
                call.client->GetName(),
                iRef->name(),
                iRef->itemID(),
                itemLoc.c_str(),
                stDataMgr.GetStationName(stationID->value()).c_str(),
                stDataMgr.GetStationName(call.client->GetStationID()).c_str()
            );

            call.client->SendErrorMsg(
                "You cannot sell %s from %s while you are in %s.  Locations mismatch",
                iRef->name(),
                stDataMgr.GetStationName(stationID->value()).c_str(),
                stDataMgr.GetStationName(call.client->GetStationID()).c_str()
            );

            return nullptr;
        }

        //TODO: verify orderRange against their skills. client may do this...verify

        // they are allowed to sell this thing...

        // is this standing order or immediate?
        if (duration->value() == 0) {
            bool completedOrder(false);

            uint32 orderID(0), origQty(quantity->value());

            // set an upper bound (this used to be a while loop that spun
            // forever in some cases)
            for (int i = 0; i < 1000; i++) {
                _log(MARKET__DUMP, "Mkt::PlaceCharOrder(): finding buy order: %i, %i, %i, %.2f", typeID->value(), stationID->value(), quantity->value(), orderPrice);

                orderID = MarketDB::FindBuyOrder(typeID->value(), stationID->value(), quantity->value(), orderPrice);

                if (!orderID) {
                    // nothing changed since the last query, so no point asking again
                    break;
                }

                _log(MARKET__TRACE,
                    "PlaceCharOrder - Found buy order #%u in %s for %s.",
                    orderID,
                    stDataMgr.GetStationName(stationID->value()).c_str(),
                    call.client->GetName()
                );

                // sell only the requested quantity, not the whole stack
                if (iRef->quantity() > quantity->value()) {
                    InventoryItemRef sRef = iRef->Split(quantity->value(), true);
                    if (sRef.get() == nullptr) {
                        _log(MARKET__ERROR, "PlaceCharOrder - Failed to split %i from %s(%u).", quantity->value(), iRef->name(), iRef->itemID());
                        break;
                    }
                    iRef = sRef;
                }

                completedOrder = sMktMgr.ExecuteBuyOrder(
                    call.client,
                    orderID,
                    iRef,
                    quantity->value(),
                    useCorp->value(),
                    typeID->value(),
                    stationID->value(),
                    orderPrice
                );

                // a failed execution may already have moved the item or money, so never retry with the same item
                if (completedOrder)
                    _log(MARKET__DUMP, "Mkt::PlaceCharOrder(): order resolved");

                break;
            }

            if (!completedOrder) {
                _log(MARKET__TRACE, "PlaceCharOrder - failed to find a matching buy order for %i of type %i at %.2f ISK.", quantity->value(), typeID->value(), orderPrice);

                call.client->SendErrorMsg("No buy order found.");

                return nullptr;
            }

            return nullptr;
        }

        // they will be placing a sell order:

        // set save data
        Market::SaveData data = Market::SaveData();
        data.accountKey         = accountKey;
        data.minVolume          = 1; // verify this
        data.issued             = GetFileTimeNow();
        // fill items from sent data
        data.bid                = bid->value();
        data.isCorp             = useCorp->value();
        data.typeID             = typeID->value();
        data.orderRange         = orderRange->value();
        data.ownerID            = iRef->ownerID();
        data.solarSystemID      = sDataMgr.GetStationSystem(stationID->value());
        data.regionID           = sDataMgr.GetStationRegion(stationID->value());
        data.stationID          = stationID->value();
        data.price              = orderPrice;
        data.volEntered         = quantity->value();
        data.volRemaining       = quantity->value();
        data.duration           = duration->value();
        data.memberID           = useCorp->value() ?call.client->GetCharacterID():0;

        // check seller's permissions on the wallet if it's for corp
        if (data.isCorp) {
            int64 corpRole = call.client->GetCorpRole ();

            // make sure the user has permissions to take money from the corporation account
            if (
                (accountKey == 1000 && (corpRole & Corp::Role::AccountCanTake1) == 0) ||
                (accountKey == 1001 && (corpRole & Corp::Role::AccountCanTake2) == 0) ||
                (accountKey == 1002 && (corpRole & Corp::Role::AccountCanTake3) == 0) ||
                (accountKey == 1003 && (corpRole & Corp::Role::AccountCanTake4) == 0) ||
                (accountKey == 1004 && (corpRole & Corp::Role::AccountCanTake5) == 0) ||
                (accountKey == 1005 && (corpRole & Corp::Role::AccountCanTake6) == 0) ||
                (accountKey == 1006 && (corpRole & Corp::Role::AccountCanTake7) == 0)
            ) {
                throw UserError("CrpAccessDenied").AddFormatValue ("reason", new PyString ("You do not have access to that wallet"));
            }
        }


        // these need a bit more data
        data.contraband = iRef->contraband();   // does this need to check region/system?
        data.jumps = 1;     // not sure if this is used....

        // calculate total for broker fees
        double total = (double)orderPrice * quantity->value();
        std::string reason = "DESC:  Setting up sell order in ";
        reason += stDataMgr.GetStationName(stationID->value()).c_str();

        // broker fee uses the standings of the order's station owner
        double fee = GetBrokerFee(call.client, stationID->value(), total);
        _log(MARKET__DEBUG, "PlaceCharOrder(sell) - %s: Total: %.2f, Fee: %.2f", useCorp->value() ?"Corp":"Player", total, fee);

        // take monies and record actions (taxes are paid when item sells)
        if (useCorp->value()) {
            AccountService::TransferFunds(
                call.client->GetCorporationID(),
                stDataMgr.GetOwnerID(stationID->value()),
                fee,
                reason.c_str(),
                Journal::EntryType::Brokerfee,
                0,
                accountKey,
                Account::KeyType::Cash,
                call.client
            );
        } else {
            AccountService::TransferFunds(
                call.client->GetCharacterID(),
                stDataMgr.GetOwnerID(stationID->value()),
                fee,
                reason.c_str(),
                Journal::EntryType::Brokerfee,
                0,
                accountKey
            );
        }

        // store the order in the DB.
        uint32 orderID(MarketDB::StoreOrder(data));
        if (orderID == 0) {
            _log(MARKET__ERROR, "PlaceCharOrder - Failed to record sell order in the DB.");
            call.client->SendErrorMsg("Failed to record the order in the DB!");
            return nullptr;
        }

        if (iRef->quantity() == quantity->value()) {
            // take item from seller
            call.client->SystemMgr()->RemoveItemFromInventory(iRef);
            iRef->Delete();
        } else {
            //update the item.
            if (!iRef->AlterQuantity(-quantity->value(), true)) {
                _log(MARKET__ERROR, "PlaceCharOrder - Failed to consume %i units from %s", quantity->value(), iRef->name());
                // the items stay with the seller, so the order must go
                MarketDB::DeleteOrder(orderID);
                return nullptr;
            }
        }

        //notify client about new order.
        sMktMgr.InvalidateOrdersCache(data.regionID, typeID->value());
        sMktMgr.SendOnOwnOrderChanged(call.client, orderID, Market::Action::Add, useCorp->value());
    }

    return nullptr;
}

PyResult MarketProxyService::PlaceCharOrder(PyCallArgs &call, PyInt* stationID, PyInt* typeID, PyFloat* price, PyInt* quantity, PyInt* bid, PyInt* orderRange, std::optional <PyInt*> itemID, PyInt* minVolume, PyInt* duration, PyInt* useCorp, std::optional<PyRep*> located) {
    // Used when character has a corp wallet, client sends a PyInt instead of a PyBool
    return PlaceCharOrder(call, stationID, typeID, price, quantity, bid, orderRange, itemID, minVolume, duration, new PyBool(useCorp->value()), located);
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

    if (!InTradeRange(call.client, oInfo.stationID, call.client->GetChar()->GetSkillLevel(EvESkill::Daytrading)))
        throw CustomError("%s is out of range.  Your Daytrading skill limits how far away you can modify orders.", stDataMgr.GetStationName(oInfo.stationID).c_str());

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
    // a raised buy order also needs the extra escrow.  check both, so the fee isn't taken when the escrow can't be
    double needed(fee);
    if (oInfo.isBuy and (priceNew > oInfo.price))
        needed += (priceNew - oInfo.price) * oInfo.quantity;
    if (!oInfo.isCorp and (call.client->GetBalance() < needed))
        throw UserError("NotEnoughMoney")
                .AddISK("amount", needed)
                .AddISK("balance", call.client->GetBalance());
    AccountService::TransferFunds(walletOwnerID, stationOwnerID, fee, reason.c_str(), Journal::EntryType::Brokerfee, oInfo.orderID, accountKey, Account::KeyType::Cash, call.client);

    // buy orders hold price * volRemaining in escrow.  move the difference
    double newEscrow(-1.0);
    if (oInfo.isBuy) {
        double delta = (priceNew - oInfo.price) * oInfo.quantity;
        if (delta > 0) {
            AccountService::TransferFunds(walletOwnerID, stationOwnerID, delta, reason.c_str(), Journal::EntryType::MarketEscrow, oInfo.orderID, accountKey, Account::KeyType::Escrow, call.client);
        } else if (delta < 0) {
            AccountService::TransferFunds(stationOwnerID, walletOwnerID, -delta, reason.c_str(), Journal::EntryType::MarketEscrow, oInfo.orderID, Account::KeyType::Escrow, accountKey, call.client);
        }
        newEscrow = priceNew * oInfo.quantity;
    }

    if (!MarketDB::AlterOrderPrice(oInfo.orderID, priceNew, newEscrow)) {
        _log(MARKET__ERROR, "ModifyCharOrder - Failed to modify price for order #%u.", oInfo.orderID);
        return nullptr;
    }
    sMktMgr.SetOrderModified(oInfo.orderID);

    sMktMgr.InvalidateOrdersCache(oInfo.regionID, oInfo.typeID);
    sMktMgr.SendOnOwnOrderChanged(call.client, oInfo.orderID, Market::Action::Modify, oInfo.isCorp);

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

    // delete first, so a failure can't refund an order that still exists
    PyRep* order(MarketDB::GetOrderRow(oInfo.orderID));
    if (!MarketDB::DeleteOrder(oInfo.orderID)) {
        _log(MARKET__ERROR, "CancelCharOrder - Failed to delete order #%u.", oInfo.orderID);
        PySafeDecRef(order);
        return nullptr;
    }
    sMktMgr.ClearOrderModified(oInfo.orderID);

    if (oInfo.isBuy) {
        // buy order only refunds escrow (price * volRemaining)
        double money = oInfo.price * oInfo.quantity;
        // send wallet blink event and record the transaction in their journal.
        std::string reason = "DESC:  Canceling Market Order #";
        reason += std::to_string(oInfo.orderID);

        AccountService::TransferFunds(
            stDataMgr.GetOwnerID(oInfo.stationID),
            oInfo.ownerID,
            money,
            reason.c_str(),
            Journal::EntryType::MarketEscrow,
            oInfo.orderID,
            Account::KeyType::Escrow,
            (oInfo.isCorp ? oInfo.accountKey : Account::KeyType::Cash),
            call.client
        );
    } else {
        // remaining items go back to the order's owner in the order's station
        uint32 locationID(oInfo.stationID);
        EVEItemFlags flag(flagHangar);
        if (oInfo.isCorp) {
            locationID = stDataMgr.GetOfficeIDForCorp(oInfo.stationID, oInfo.ownerID);
            flag = flagCorpMarket;
            if (locationID == 0)
                locationID = oInfo.stationID;
        }
        ItemData idata(oInfo.typeID, ownerStation, locTemp, flagHangar, oInfo.quantity);
        InventoryItemRef iRef = sItemFactory.SpawnItem(idata);
        if (iRef.get() != nullptr)
            iRef->Donate(oInfo.ownerID, locationID, flag, true);
    }

    sMktMgr.InvalidateOrdersCache(oInfo.regionID, oInfo.typeID);
    sMktMgr.SendOnOwnOrderChanged(call.client, oInfo.orderID, Market::Action::Expiry, oInfo.isCorp, order);

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

bool MarketProxyService::InTradeRange(Client* pClient, uint32 stationID, uint8 skillLevel)
{
    if (pClient->GetStationID() == stationID)
        return true;
    switch (skillLevel) {
        case 0:
            return false;
        case 1:
            return (sDataMgr.GetStationSystem(stationID) == pClient->GetSystemID());
        default:
            // @todo  levels 2-4 should be 5/10/20 jumps (needs a jump count helper).  level 5 is region
            return (sDataMgr.GetStationRegion(stationID) == pClient->GetRegionID());
    }
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
