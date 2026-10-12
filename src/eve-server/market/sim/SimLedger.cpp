
 /**
  * @name SimLedger.cpp
  *   market simulation:  wallets, stock and the audit
  *
  * @date:          13Oct26
  *
  */

#include <cstdlib>

#include "market/sim/SimLedger.h"

namespace MktSim {

Ledger::Account& Ledger::Acct(uint32_t actorID)
{
    return m_accounts[actorID];
}

const Ledger::Account* Ledger::FindAcct(uint32_t actorID) const
{
    std::unordered_map<uint32_t, Account>::const_iterator itr = m_accounts.find(actorID);
    return (itr == m_accounts.end()) ? nullptr : &itr->second;
}

Ledger::Stock& Ledger::Inv(uint32_t actorID, uint16_t nodeID, uint16_t typeID)
{
    return m_stocks[Key(actorID, nodeID, typeID)];
}

const Ledger::Stock* Ledger::FindInv(uint32_t actorID, uint16_t nodeID, uint16_t typeID) const
{
    std::unordered_map<StockKey, Stock>::const_iterator itr = m_stocks.find(Key(actorID, nodeID, typeID));
    return (itr == m_stocks.end()) ? nullptr : &itr->second;
}

void Ledger::CountGoods(uint16_t typeID, uint8_t flow, Units q)
{
    std::map<uint16_t, std::array<Units, Goods::Count>>::iterator itr = m_goods.find(typeID);
    if (itr == m_goods.end())
        itr = m_goods.emplace(typeID, std::array<Units, Goods::Count>()).first;
    itr->second[flow] += q;
}

void Ledger::Faucet(uint32_t actorID, Cents c, uint8_t flow)
{
    if (c <= 0)
        return;
    Acct(actorID).wallet += c;
    CountIsk(flow, c);
    m_dirtyAccts.insert(actorID);
}

Cents Ledger::Sink(uint32_t actorID, Cents c, uint8_t flow, bool strict)
{
    if (c <= 0)
        return 0;
    Account& a(Acct(actorID));
    if (a.wallet < c) {
        if (strict)
            return 0;
        c = a.wallet;
        if (c <= 0)
            return 0;
    }
    a.wallet -= c;
    CountIsk(flow, c);
    m_dirtyAccts.insert(actorID);
    return c;
}

bool Ledger::Transfer(uint32_t fromID, uint32_t toID, Cents c)
{
    if (c <= 0)
        return (c == 0);
    Account& from(Acct(fromID));
    if (from.wallet < c)
        return false;
    from.wallet -= c;
    Acct(toID).wallet += c;
    m_dirtyAccts.insert(fromID);
    m_dirtyAccts.insert(toID);
    return true;
}

bool Ledger::Reserve(uint32_t actorID, Cents c)
{
    if (c <= 0)
        return (c == 0);
    Account& a(Acct(actorID));
    if (a.wallet < c)
        return false;
    a.wallet -= c;
    a.reserved += c;
    m_dirtyAccts.insert(actorID);
    return true;
}

void Ledger::Unreserve(uint32_t actorID, Cents c)
{
    if (c <= 0)
        return;
    Account& a(Acct(actorID));
    if (c > a.reserved) {
        ++m_violations;
        c = a.reserved;
    }
    a.reserved -= c;
    a.wallet += c;
    m_dirtyAccts.insert(actorID);
}

bool Ledger::PayFromReserved(uint32_t fromID, uint32_t toID, Cents c)
{
    if (c <= 0)
        return (c == 0);
    Account& from(Acct(fromID));
    if (from.reserved < c)
        return false;
    from.reserved -= c;
    Acct(toID).wallet += c;
    m_dirtyAccts.insert(fromID);
    m_dirtyAccts.insert(toID);
    return true;
}

bool Ledger::SinkFromReserved(uint32_t actorID, Cents c, uint8_t flow)
{
    if (c <= 0)
        return (c == 0);
    Account& a(Acct(actorID));
    if (a.reserved < c)
        return false;
    a.reserved -= c;
    CountIsk(flow, c);
    m_dirtyAccts.insert(actorID);
    return true;
}

void Ledger::FromPlayer(uint32_t actorID, Cents c)
{
    if (c <= 0)
        return;
    Acct(actorID).wallet += c;
    CountIsk(Isk::PlayerToSim, c);
    m_dirtyAccts.insert(actorID);
}

bool Ledger::ToPlayer(uint32_t actorID, Cents c, bool fromReserved)
{
    if (c <= 0)
        return (c == 0);
    Account& a(Acct(actorID));
    Cents& from(fromReserved ? a.reserved : a.wallet);
    if (from < c)
        return false;
    from -= c;
    CountIsk(Isk::SimToPlayer, c);
    m_dirtyAccts.insert(actorID);
    return true;
}

void Ledger::GoodsIn(uint32_t actorID, uint16_t nodeID, uint16_t typeID, Units q, uint8_t flow)
{
    if (q <= 0)
        return;
    StockKey k(Key(actorID, nodeID, typeID));
    m_stocks[k].qty += q;
    CountGoods(typeID, flow, q);
    m_dirtyStocks.insert(k);
}

bool Ledger::GoodsOut(uint32_t actorID, uint16_t nodeID, uint16_t typeID, Units q, uint8_t flow, bool fromReserved)
{
    if (q <= 0)
        return (q == 0);
    StockKey k(Key(actorID, nodeID, typeID));
    Stock& s(m_stocks[k]);
    Units& from(fromReserved ? s.reserved : s.qty);
    if (from < q)
        return false;
    from -= q;
    CountGoods(typeID, flow, q);
    m_dirtyStocks.insert(k);
    return true;
}

bool Ledger::MoveGoods(uint32_t fromID, uint32_t toID, uint16_t nodeID, uint16_t typeID, Units q, bool fromReserved)
{
    if (q <= 0)
        return (q == 0);
    StockKey kf(Key(fromID, nodeID, typeID)), kt(Key(toID, nodeID, typeID));
    Stock& f(m_stocks[kf]);
    Units& from(fromReserved ? f.reserved : f.qty);
    if (from < q)
        return false;
    from -= q;
    m_stocks[kt].qty += q;
    m_dirtyStocks.insert(kf);
    m_dirtyStocks.insert(kt);
    return true;
}

bool Ledger::Relocate(uint32_t actorID, uint16_t fromNode, uint16_t toNode, uint16_t typeID, Units q)
{
    if (q <= 0)
        return (q == 0);
    StockKey kf(Key(actorID, fromNode, typeID)), kt(Key(actorID, toNode, typeID));
    Stock& f(m_stocks[kf]);
    if (f.qty < q)
        return false;
    f.qty -= q;
    m_stocks[kt].qty += q;
    m_dirtyStocks.insert(kf);
    m_dirtyStocks.insert(kt);
    return true;
}

bool Ledger::ReserveGoods(uint32_t actorID, uint16_t nodeID, uint16_t typeID, Units q)
{
    if (q <= 0)
        return (q == 0);
    StockKey k(Key(actorID, nodeID, typeID));
    Stock& s(m_stocks[k]);
    if (s.qty < q)
        return false;
    s.qty -= q;
    s.reserved += q;
    m_dirtyStocks.insert(k);
    return true;
}

void Ledger::UnreserveGoods(uint32_t actorID, uint16_t nodeID, uint16_t typeID, Units q)
{
    if (q <= 0)
        return;
    StockKey k(Key(actorID, nodeID, typeID));
    Stock& s(m_stocks[k]);
    if (q > s.reserved) {
        ++m_violations;
        q = s.reserved;
    }
    s.reserved -= q;
    s.qty += q;
    m_dirtyStocks.insert(k);
}

Cents Ledger::Money() const
{
    Cents total(0);
    for (auto& cur : m_accounts)
        total += cur.second.wallet + cur.second.reserved;
    return total;
}

Cents Ledger::MoneyOf(uint32_t actorID) const
{
    const Account* a(FindAcct(actorID));
    return (a == nullptr) ? 0 : (a->wallet + a->reserved);
}

Units Ledger::GoodsOf(uint16_t typeID) const
{
    Units total(0);
    for (auto& cur : m_stocks)
        if (KeyType(cur.first) == typeID)
            total += cur.second.qty + cur.second.reserved;
    return total;
}

void Ledger::GoodsByType(std::map<uint16_t, Units>& into) const
{
    for (auto& cur : m_stocks)
        into[KeyType(cur.first)] += cur.second.qty + cur.second.reserved;
}

void Ledger::BeginPeriod(int32_t day)
{
    m_day = day;
    m_moneyStart = Money();
    m_goodsStart.clear();
    GoodsByType(m_goodsStart);
    m_isk.fill(0);
    m_goods.clear();
}

Ledger::Result Ledger::Audit() const
{
    Result r = Result();
    r.day = m_day;
    r.moneyStart = m_moneyStart;
    r.moneyEnd = Money();
    r.isk = m_isk;
    Cents net(0);
    for (uint8_t i = 0; i < Isk::Count; ++i)
        net += Isk::Sign(i) * m_isk[i];
    r.iskResidual = r.moneyEnd - r.moneyStart - net;

    std::map<uint16_t, Units> now;
    GoodsByType(now);
    std::set<uint16_t> typeIDs;
    for (auto& cur : now)
        typeIDs.insert(cur.first);
    for (auto& cur : m_goodsStart)
        typeIDs.insert(cur.first);
    for (auto& cur : m_goods)
        typeIDs.insert(cur.first);
    for (auto typeID : typeIDs) {
        std::map<uint16_t, Units>::const_iterator n = now.find(typeID), s = m_goodsStart.find(typeID);
        Units delta(((n == now.end()) ? 0 : n->second) - ((s == m_goodsStart.end()) ? 0 : s->second));
        std::map<uint16_t, std::array<Units, Goods::Count>>::const_iterator f = m_goods.find(typeID);
        if (f != m_goods.end())
            for (uint8_t i = 0; i < Goods::Count; ++i) {
                delta -= Goods::Sign(i) * f->second[i];
                r.goods[i] += f->second[i];
            }
        if (delta != 0) {
            ++r.goodsResidualTypes;
            r.goodsResidualUnits += (delta < 0) ? -delta : delta;
        }
    }
    return r;
}

void Ledger::MarkAllDirty()
{
    for (auto& cur : m_accounts)
        m_dirtyAccts.insert(cur.first);
    for (auto& cur : m_stocks)
        m_dirtyStocks.insert(cur.first);
}

void Ledger::Load(uint32_t actorID, Cents wallet, Cents reserved)
{
    Account& a(m_accounts[actorID]);
    a.wallet = wallet;
    a.reserved = reserved;
}

void Ledger::LoadStock(StockKey key, Units qty, Units reserved)
{
    Stock& s(m_stocks[key]);
    s.qty = qty;
    s.reserved = reserved;
}

}
