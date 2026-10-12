
 /**
  * @name HarnessMarket.cpp
  *   the offline harness' in-memory market
  *
  * @date:          13Oct26
  *
  */

#include <algorithm>
#include <cmath>
#include <string>

#include "HarnessMarket.h"
#include "market/sim/SimEconomy.h"

namespace MktHarness {

HarnessMarket::HarnessMarket(const StaticData& data, double salesTax)
: m_static(data),
  m_world(nullptr),
  m_tax(salesTax),
  m_now(0),
  m_nextID(1000000),
  m_writes(0)
{
}

Player& HarnessMarket::GetPlayer(uint32_t id)
{
    Player& p(m_people[id]);
    p.id = id;
    return p;
}

uint32_t HarnessMarket::RegionOf(uint32_t stationID) const
{
    for (auto& node : m_static.nodes)
        if (node.stationID == stationID)
            return node.regionID;
    for (auto& offer : m_static.vendors)
        if (offer.stationID == stationID)
            return offer.regionID;
    return 0;
}

bool HarnessMarket::ApplyOrders(const OrderBatch& batch, std::vector<uint32_t>& newIDs)
{
    m_writes += batch.Size();
    for (auto& n : batch.posts) {
        BookOrder o = BookOrder();
        o.orderID = m_nextID++;
        o.ownerID = n.ownerID;
        o.sim = true;
        o.typeID = n.typeID;
        o.stationID = n.stationID;
        o.regionID = n.regionID;
        o.bid = n.bid;
        o.price = n.price;
        o.qty = n.qty;
        o.issued = n.issued;
        m_sim[o.orderID] = o;
        newIDs.push_back(o.orderID);
    }
    for (auto& a : batch.alters) {
        std::map<uint32_t, BookOrder>::iterator itr = m_sim.find(a.orderID);
        if (itr == m_sim.end())
            continue;
        itr->second.price = a.price;
        itr->second.qty = a.qty;
        itr->second.issued = a.issued;
    }
    for (auto& c : batch.cancels)
        m_sim.erase(c.orderID);
    return true;
}

void HarnessMarket::PlayerOrders(uint32_t regionID, uint16_t typeID, std::vector<PlayerOrder>& asks, std::vector<PlayerOrder>& bids)
{
    for (auto& cur : m_players) {
        const BookOrder& o(cur.second);
        if ((o.regionID != regionID) or (o.typeID != typeID) or (o.qty <= 0))
            continue;
        PlayerOrder p = PlayerOrder();
        p.orderID = o.orderID;
        p.ownerID = o.ownerID;
        p.typeID = o.typeID;
        p.stationID = o.stationID;
        p.systemID = 0;
        p.regionID = o.regionID;
        p.bid = o.bid;
        p.price = o.price;
        p.qty = (uint32_t)o.qty;
        p.minVolume = 1;
        p.range = -1;
        p.issued = o.issued;
        (o.bid ? bids : asks).push_back(p);
    }
    std::sort(asks.begin(), asks.end(), [](const PlayerOrder& a, const PlayerOrder& b) {
        return (a.price != b.price) ? (a.price < b.price) : (a.orderID < b.orderID);
    });
    std::sort(bids.begin(), bids.end(), [](const PlayerOrder& a, const PlayerOrder& b) {
        return (a.price != b.price) ? (a.price > b.price) : (a.orderID < b.orderID);
    });
}

uint32_t HarnessMarket::TakePlayerAsk(const PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID)
{
    std::map<uint32_t, BookOrder>::iterator itr = m_players.find(order.orderID);
    if ((itr == m_players.end()) or (itr->second.price != order.price) or (itr->second.qty <= 0))
        return 0;
    BookOrder& o(itr->second);
    Units got(std::min<Units>(qty, o.qty));
    Cents value(o.price * got);
    Cents tax((Cents)std::llround((double)value * m_tax));
    Player& seller(GetPlayer(o.ownerID));
    seller.wallet += value - tax;
    seller.fromSim += value;
    seller.taxes += tax;
    o.qty -= got;
    if (o.qty <= 0)
        m_players.erase(itr);
    return (uint32_t)got;
}

uint32_t HarnessMarket::TakePlayerBid(const PlayerOrder& order, uint32_t qty, uint32_t actorID, uint32_t corpID, uint32_t stationID)
{
    std::map<uint32_t, BookOrder>::iterator itr = m_players.find(order.orderID);
    if ((itr == m_players.end()) or (itr->second.price != order.price) or (itr->second.qty <= 0))
        return 0;
    BookOrder& o(itr->second);
    if (!RangeCovers(-1, o.stationID, stationID))
        return 0;
    Units got(std::min<Units>(qty, o.qty));
    Cents value(o.price * got);
    Player& buyer(GetPlayer(o.ownerID));
    o.escrow -= value;
    buyer.toSim += value;
    buyer.goods[o.typeID] += got;
    o.qty -= got;
    if (o.qty <= 0) {
        buyer.wallet += o.escrow;   // what is left in escrow
        m_players.erase(itr);
    }
    return (uint32_t)got;
}

bool HarnessMarket::RangeCovers(int32_t range, uint32_t fromStationID, uint32_t toStationID)
{
    // the harness' players and nodes are all at the hub stations.  station range only reaches the station itself,
    //  anything wider the station's region
    if (fromStationID == toStationID)
        return true;
    if (range == -1)
        return false;
    return RegionOf(fromStationID) == RegionOf(toStationID);
}

double HarnessMarket::InsuranceNet(uint16_t shipTypeID)
{
    // Tyrannis insurance:  the ship's build materials at the market (the first node's fair values), platinum pays
    //  100% for 30% of the value
    const TypeData* t(m_static.Type(shipTypeID));
    if ((t == nullptr) or (m_world == nullptr) or m_static.nodes.empty())
        return 0.0;
    double value(0.0);
    for (auto& in : t->bom) {
        Cell* c(m_world->FindCell(m_static.nodes[0].nodeID, in.first));
        const TypeData* it(m_static.Type(in.first));
        if ((it == nullptr) or (it->categoryID != Category::Material))
            continue;
        value += in.second * ((c != nullptr) ? std::exp(c->lnV) : it->refPrice);
    }
    return 0.7 * value;
}

void HarnessMarket::Book(uint32_t regionID, uint16_t typeID, bool bid, std::vector<BookOrder*>& into)
{
    for (auto& cur : m_sim)
        if ((cur.second.regionID == regionID) and (cur.second.typeID == typeID) and (cur.second.bid == bid) and (cur.second.qty > 0))
            into.push_back(&cur.second);
    for (auto& cur : m_players)
        if ((cur.second.regionID == regionID) and (cur.second.typeID == typeID) and (cur.second.bid == bid) and (cur.second.qty > 0))
            into.push_back(&cur.second);
    std::sort(into.begin(), into.end(), [bid](const BookOrder* a, const BookOrder* b) {
        if (a->price != b->price)
            return bid ? (a->price > b->price) : (a->price < b->price);
        if (a->issued != b->issued)
            return a->issued < b->issued;
        return a->orderID < b->orderID;
    });
}

Units HarnessMarket::Buy(uint32_t playerID, uint32_t stationID, uint16_t typeID, Units qty, Cents maxPrice)
{
    std::vector<BookOrder*> asks;
    Book(RegionOf(stationID), typeID, false, asks);
    Player& me(GetPlayer(playerID));
    Units left(qty);
    std::vector<uint32_t> done;
    for (auto o : asks) {
        if ((left <= 0) or (o->price > maxPrice))
            break;
        if (!RangeCovers(-1, stationID, o->stationID) or (!o->sim and (o->ownerID == playerID)))
            continue;
        Units q(std::min(left, o->qty));
        Cents value(o->price * q);
        if (o->sim) {
            m_world->OnAskFilled(o->orderID, (uint32_t)q, playerID, m_now);
            me.toSim += value;
        } else {
            Player& seller(GetPlayer(o->ownerID));
            Cents tax((Cents)std::llround((double)value * m_tax));
            seller.wallet += value - tax;
            seller.taxes += tax;
        }
        me.wallet -= value;
        me.goods[typeID] += q;
        o->qty -= q;
        left -= q;
        if (o->qty <= 0)
            done.push_back(o->orderID);
    }
    for (auto id : done) {
        m_sim.erase(id);
        m_players.erase(id);
    }
    return qty - left;
}

Units HarnessMarket::Sell(uint32_t playerID, uint32_t stationID, uint16_t typeID, Units qty, Cents minPrice)
{
    std::vector<BookOrder*> bids;
    Book(RegionOf(stationID), typeID, true, bids);
    Player& me(GetPlayer(playerID));
    Units left(qty);
    std::vector<uint32_t> done;
    for (auto o : bids) {
        if ((left <= 0) or (o->price < minPrice))
            break;
        if (!RangeCovers(-1, o->stationID, stationID) or (!o->sim and (o->ownerID == playerID)))
            continue;
        Units q(std::min(left, o->qty));
        if (o->sim) {
            q = m_world->AllowIntake(o->orderID, playerID, (uint32_t)q, m_now);
            if (q <= 0)
                continue;
        }
        Cents value(o->price * q);
        Cents tax((Cents)std::llround((double)value * m_tax));
        if (o->sim) {
            m_world->OnBidFilled(o->orderID, (uint32_t)q, playerID, m_now);
            me.fromSim += value;
        } else {
            Player& buyer(GetPlayer(o->ownerID));
            o->escrow -= value;
            buyer.goods[typeID] += q;
        }
        me.wallet += value - tax;
        me.taxes += tax;
        me.goods[typeID] -= q;
        o->qty -= q;
        left -= q;
        if (o->qty <= 0)
            done.push_back(o->orderID);
    }
    for (auto id : done) {
        std::map<uint32_t, BookOrder>::iterator itr = m_players.find(id);
        if (itr != m_players.end()) {
            GetPlayer(itr->second.ownerID).wallet += itr->second.escrow;
            m_players.erase(itr);
        }
        m_sim.erase(id);
    }
    return qty - left;
}

uint32_t HarnessMarket::Place(uint32_t playerID, uint32_t stationID, uint16_t typeID, bool bid, Cents price, Units qty)
{
    Units done(bid ? Buy(playerID, stationID, typeID, qty, price) : Sell(playerID, stationID, typeID, qty, price));
    if (done >= qty)
        return 0;
    BookOrder o = BookOrder();
    o.orderID = m_nextID++;
    o.ownerID = playerID;
    o.sim = false;
    o.typeID = typeID;
    o.stationID = stationID;
    o.regionID = RegionOf(stationID);
    o.bid = bid;
    o.price = price;
    o.qty = qty - done;
    o.issued = m_now;
    Player& me(GetPlayer(playerID));
    if (bid) {
        o.escrow = price * o.qty;
        me.wallet -= o.escrow;
    } else {
        me.goods[typeID] -= o.qty;
    }
    m_players[o.orderID] = o;
    if (m_world != nullptr)
        m_world->MarkDirty(o.regionID, typeID, m_now);
    return o.orderID;
}

void HarnessMarket::CancelPlayerOrders(uint32_t playerID)
{
    for (std::map<uint32_t, BookOrder>::iterator itr = m_players.begin(); itr != m_players.end(); ) {
        if (itr->second.ownerID != playerID) {
            ++itr;
            continue;
        }
        Player& me(GetPlayer(playerID));
        if (itr->second.bid)
            me.wallet += itr->second.escrow;
        else
            me.goods[itr->second.typeID] += itr->second.qty;
        itr = m_players.erase(itr);
    }
}

Cents HarnessMarket::SimAsk(uint32_t stationID, uint16_t typeID) const
{
    Cents best(0);
    for (auto& cur : m_sim)
        if (!cur.second.bid and (cur.second.stationID == stationID) and (cur.second.typeID == typeID) and (cur.second.qty > 0))
            if ((best == 0) or (cur.second.price < best))
                best = cur.second.price;
    return best;
}

Cents HarnessMarket::SimBid(uint32_t stationID, uint16_t typeID) const
{
    Cents best(0);
    for (auto& cur : m_sim)
        if (cur.second.bid and (cur.second.stationID == stationID) and (cur.second.typeID == typeID) and (cur.second.qty > 0))
            if (cur.second.price > best)
                best = cur.second.price;
    return best;
}

size_t HarnessMarket::CheckMirror(std::string& first) const
{
    size_t bad(0);
    for (auto& cur : m_world->Orders()) {
        std::map<uint32_t, BookOrder>::const_iterator itr = m_sim.find(cur.first);
        if ((itr == m_sim.end()) or (itr->second.price != cur.second.dbPrice) or (itr->second.qty != (Units)cur.second.dbQty)
        or (cur.second.dbQty != cur.second.qty) or (cur.second.dbPrice != cur.second.price)) {
            if (bad++ == 0)
                first = "order " + std::to_string(cur.first);
        }
    }
    if (m_sim.size() != m_world->Orders().size()) {
        if (bad++ == 0)
            first = "book has " + std::to_string(m_sim.size()) + " sim orders, the world " + std::to_string(m_world->Orders().size());
    }
    return bad;
}

}
