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
    Author:        Luck
    Updates:    Allan
*/

#ifndef __TRADE_SERVICE_H_INCL__
#define __TRADE_SERVICE_H_INCL__

#include <memory>
#include <tuple>

#include "services/BoundService.h"
#include "packets/Trade.h"

class Client;
class TradeBound;

/* one trade window between two docked characters (pvptradesvc).  a client can have several at once (one per pair).
 *  offered items keep their owner and sit in the trade container until the trade ends */
class TradeSession
{
public:
    struct TradeItem {
        uint32 ownerID;
        uint32 locationID;      // where it came from, for a cancelled trade
        EVEItemFlags flag;
    };

    uint32 containerID;
    uint32 stationID;
    uint32 myID;                // the initiator ("traders[0]")
    uint32 herID;
    uint32 warID;               // CEO surrender trade (IsCEOTrade)
    bool myState;
    bool herState;
    double myMoney;
    double herMoney;
    int64 fileTime;             // moniker 'when'
    std::map<uint32, TradeItem> items;  // itemID/data

    bool HasTrader(uint32 charID) const                  { return ((charID == myID) or (charID == herID)); }
    uint32 Other(uint32 charID) const                   { return ((charID == myID) ? herID : myID); }
};

class TradeService : public BindableService <TradeService, TradeBound>
{
public:
    TradeService(EVEServiceManager& mgr);

    void BoundReleased (TradeBound* bound) override;

    uint32 GetTradeSessionID();

    std::shared_ptr<TradeSession> GetSession(uint32 containerID);
    // items back where they came from, ('Cancel', containerID) to both, session closed
    void CancelTrade(std::shared_ptr<TradeSession> pSession);
    // every trade of this client (logout, undock)
    void CancelAllTrades(Client* pClient);
    // forgets a session (maps and both clients)
    void CloseSession(std::shared_ptr<TradeSession> pSession);
    // OnTrade(payload) to both traders that are online.  payload is consumed
    void SendOnTrade(std::shared_ptr<TradeSession> pSession, PyTuple* payload);
    void SendStateToggle(std::shared_ptr<TradeSession> pSession);
    void TransferContainerContents(InventoryItemRef itemRef, uint32 newOwnerID);

protected:
    BoundDispatcher* BindObject(Client* client, PyRep* bindParameters) override;

    // client:  InitiateTrade(charID) or InitiateTrade(charID, warID) (CEO surrender trade)
    PyResult InitiateTrade(PyCallArgs& call, PyInt* characterID, std::optional<PyRep*> warID);

    uint32 m_SessionID;
    std::map<uint32, std::shared_ptr<TradeSession>> m_sessions;     // containerID/session
    std::map<std::tuple<uint32, uint32, int64>, uint32> m_byBind;   // (myID, herID, when)/containerID
};

class TradeBound : public EVEBoundObject <TradeBound>
{
    friend TradeService;

public:
    TradeBound(EVEServiceManager& mgr, TradeService& parent, uint32 containerID);

protected:
    TradeService* m_TSvc;                // get registered TradeService object
    uint32 m_containerID;

    // the session, if it still exists and the caller is one of its traders
    std::shared_ptr<TradeSession> GetSession(Client* pClient);
    // validates and moves one item into the trade.  qty 0 = the whole stack
    void AddItem(Client* pClient, std::shared_ptr<TradeSession> pSession, uint32 itemID, int32 qty);
    // trade completed.  perform item and money exchange
    void ExchangeItems(std::shared_ptr<TradeSession> pSession);

    PyResult OfferMoney(PyCallArgs& call, PyRep* amount);
    PyResult Abort(PyCallArgs& call);
    PyResult ToggleAccept(PyCallArgs& call, PyRep* newAccept);
    PyResult GetItemID(PyCallArgs& call);
    PyResult Add(PyCallArgs& call, PyInt* itemID, PyRep* containerID);
    PyResult MultiAdd(PyCallArgs& call, PyList* itemIDs, PyRep* containerID);
    PyResult GetItem(PyCallArgs& call);
    PyResult IsCEOTrade(PyCallArgs& call);
    PyResult List(PyCallArgs& call);
};

#endif//__TRADE_SERVICE_H_INCL__
