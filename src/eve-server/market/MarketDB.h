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

#ifndef __MARKETDB_H_INCL__
#define __MARKETDB_H_INCL__


#include "packets/Market.h"
#include "../../eve-common/EVE_Market.h"

#include "ServiceDB.h"

class MarketDB
: public ServiceDB
{
public:
    static PyRep* GetMarketGroups();
    static PyRep* GetOrders(uint32 regionID, uint16 typeID);
    static PyRep* GetOrderRow(uint32 orderID);
    static PyRep* GetRegionBest(uint32 regionID);
    static PyRep* GetSystemAsks(uint32 solarSystemID);
    static PyRep* GetStationAsks(uint32 stationID);
    static PyRep* GetOrdersForOwner(uint32 ownerID);

    // transactions of one owner.  isCorp: corp rows of data.accountKey (and data.memberID when set), else the character's own rows
    static PyRep* GetTransactions(uint32 ownerID, Market::TxData &data, bool isCorp);

    static bool DeleteOrder(uint32 orderID);
    static bool GetOrderInfo(uint32 orderID, Market::OrderInfo &oInfo);
    // new_escrow < 0 leaves the escrow column alone
    static bool AlterOrderPrice(uint32 orderID, double new_price, double new_escrow = -1.0);
    static bool AlterOrderEscrow(uint32 orderID, double escrow);
    static bool RecordTransaction(Market::TxData &data);
    static bool AlterOrderQuantity(uint32 orderID, uint32 new_qty);

    // open orders of a type in a region that an immediate sell at >= minPrice / buy at <= maxPrice could fill.
    //  best price first, then oldest first
    static void GetMatchingBuyOrders(uint32 regionID, uint16 typeID, double minPrice, std::vector<Market::OrderInfo>& into);
    static void GetMatchingSellOrders(uint32 regionID, uint16 typeID, double maxPrice, std::vector<Market::OrderInfo>& into);
    // player and player corp orders whose duration ran out before 'now'
    static void GetExpiredOrders(int64 now, uint32 limit, std::vector<uint32>& into);
    // open orders placed by this character (own orders and corp orders it placed)
    static uint32 CountOpenOrders(uint32 charID);
    static uint32 StoreOrder(Market::SaveData& data);

    /* price history */
    // daily rows of [dayStart, dayEnd) from the sell side of the transactions.  re-running a day replaces its rows
    static void RollupHistory(int64 dayStart, int64 dayEnd);
    // first transaction date, 0 when none
    static int64 GetFirstTransactionTime();
    // daily rows in [from, to), oldest first
    static PyRep* GetHistoryOld(uint32 regionID, uint16 typeID, int64 from, int64 to);
    // one row for today (since midnight), or an empty rowset
    static PyRep* GetHistoryToday(uint32 regionID, uint16 typeID, int64 midnight);
    static void DeleteTransactionsBefore(int64 time);


    /* for base price estimator */
    static void GetShipIDs(std::map<uint16, Inv::TypeData>& data);
    static void GetMineralPrices(std::map< uint16, Market::matlData >& data);   // +15% markup
    static void GetMaterialPrices(std::map< uint16, Market::matlData >& data);  // +5% markup
    static void GetManufacturedItems(std::map<uint16, Inv::TypeData>& data);
    static void UpdateInvPrice(std::map< uint16, Inv::TypeData >& data);
    static void UpdateMktPrice(std::map< uint16, Market::matlData >& data);

    /* for dealing with Crucible price history */
    static void GetCruPriceAvg(std::map< uint16, Inv::TypeData >& data);

    /* for marketMgr update service */
    static int64 GetUpdateTime();
    static void SetUpdateTime(int64 setTime);
};

#endif
