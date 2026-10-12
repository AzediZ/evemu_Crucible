
 /**
  * @name SimWorld.cpp
  *   market simulation:  the stock model, its scheduling and the player hooks
  *
  * @date:          13Oct26
  *
  */

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "market/sim/SimEconomy.h"
#include "market/sim/SimRng.h"
#include "market/sim/SimWorld.h"

namespace MktSim {

namespace {
    // hash purposes
    enum : uint64_t {
        hOU = 1, hProduce, hArrivals, hGroupQty, hWtp, hJitter, hBackfill, hBackfillVol, hBackfillBand, hMood
    };
    const double Ln2 = 0.6931471805599453;
    const int64_t StaleOrderSeconds = 30 * SecondsPerDay;     // sim orders are re-issued before the client calls them old
    const double MaxCellDtDays = 2.0;                           // a cell catches up at most two days per visit

    std::string Fmt(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }
}

World::World(const StaticData& data, const Params& params, IMarketGateway& gateway)
: m_static(data),
  m_p(params),
  m_gw(gateway),
  m_persist(nullptr),
  m_income0(0),
  m_moneyTarget(0.0),
  m_integral(0.0),
  m_incomeFactor(1.0),
  m_lastIncome(0),
  m_paused(false),
  m_started(false),
  m_auditDay(0),
  m_cellVisits(0),
  m_playerTrades(0)
{
    if (m_p.quoteLevels > MaxLevels)
        m_p.quoteLevels = MaxLevels;
    if (m_p.quoteLevels < 1)
        m_p.quoteLevels = 1;
    if (m_p.maxOrdersPerCellSide < 1)
        m_p.maxOrdersPerCellSide = 1;
    if (m_p.kMax < 1.01)
        m_p.kMax = 1.01;
}

uint64_t World::H(const Cell& c, int64_t t, uint64_t purpose, uint64_t extra) const
{
    return Rng::Hash64(m_p.seed, Key(c), (uint64_t)t, purpose, extra);
}

const NodeData* World::Node(uint32_t nodeID) const
{
    if ((nodeID == 0) or (nodeID > m_static.nodes.size()))
        return nullptr;
    return &m_static.nodes[nodeID - 1];
}

Cell* World::FindCell(uint32_t nodeID, uint16_t typeID)
{
    std::map<uint64_t, uint32_t>::iterator itr = m_cellIndex.find(((uint64_t)nodeID << 16) | typeID);
    return (itr == m_cellIndex.end()) ? nullptr : &m_cells[itr->second];
}

const SimOrder* World::FindOrder(uint32_t orderID) const
{
    std::unordered_map<uint32_t, SimOrder>::const_iterator itr = m_orders.find(orderID);
    return (itr == m_orders.end()) ? nullptr : &itr->second;
}

double World::Value(const Cell& c) const
{
    return std::exp(c.lnV);
}

double World::Mid(const Cell& c) const
{
    return (c.quoteMid > 0.0) ? c.quoteMid : Value(c);
}

double World::CostNow(const Cell& c) const
{
    const TypeData* t(m_static.Type(c.typeID));
    if ((t == nullptr) or t->bom.empty())
        return 0.0;
    const NodeData* node(Node(c.nodeID));
    double cost(0.0);
    for (auto& in : t->bom) {
        std::map<uint64_t, uint32_t>::const_iterator itr = m_cellIndex.find(((uint64_t)c.nodeID << 16) | in.first);
        if (itr != m_cellIndex.end()) {
            cost += in.second * Value(m_cells[itr->second]);
            continue;
        }
        const TypeData* it(m_static.Type(in.first));
        if ((it != nullptr) and (it->refPrice > 0.0))
            cost += in.second * it->refPrice * m_static.RegionPrice(node->regionID, in.first);
    }
    return cost;
}

double World::TargetStock(const Cell& c) const
{
    // S* = D x coverDays (PLAN 6.1), D never below the calibrated consumption (a quiet week of a thin type doesn't turn
    //  its stock into a glut), at least two lots
    return std::max(std::max(c.dBar, c.dc) * c.coverDays, 2.0 * c.lot);
}

double World::RefineValue(const Cell& c) const
{
    const TypeData* t(m_static.Type(c.typeID));
    if ((t == nullptr) or t->refine.empty())
        return 0.0;
    const NodeData* node(Node(c.nodeID));
    double value(0.0);
    for (auto& out : t->refine) {
        std::map<uint64_t, uint32_t>::const_iterator itr = m_cellIndex.find(((uint64_t)c.nodeID << 16) | out.first);
        if (itr != m_cellIndex.end()) {
            value += out.second * Value(m_cells[itr->second]);
            continue;
        }
        const TypeData* ot(m_static.Type(out.first));
        if ((ot != nullptr) and (ot->refPrice > 0.0))
            value += out.second * ot->refPrice * m_static.RegionPrice(node->regionID, out.first);
    }
    return value;
}

double World::AnchorOf(const Cell& c) const
{
    if (c.anchorOverride > 0.0)
        return c.anchorOverride;
    double anchor((c.wc <= 0.0) ? c.refLocal : Econ::Anchor(c.refLocal, c.wc, CostNow(c), c.costRef));
    // nothing that refines is worth less than what it refines into (the reprocessing floor PLAN 6.4 gives modules and
    //  loot;  in the stock model it holds the anchor up)
    return std::max(anchor, RefineValue(c));
}

int64_t World::Period(const Cell& c) const
{
    return 60 * (int64_t)(((c.flags & CellFlag::Hot) != 0) ? m_p.hotCellMinutes : m_p.coldCellMinutes);
}

void World::Schedule(Cell& c, int64_t t)
{
    c.nextDue = t;
    m_due.push(Due{t, DueCell, c.index});
}

Cents World::BrokerFee(Cents value) const
{
    if (!m_p.simPaysFees or (value <= 0))
        return 0;
    return (Cents)std::llround((double)value * m_p.brokerFeeRate);
}

/************************************************************************************************************************
 * building
 */

void World::Build(int64_t now)
{
    m_actors.clear();
    m_cells.clear();
    m_cellIndex.clear();
    m_regionCells.clear();
    m_vendorSlots.clear();

    std::map<uint32_t, uint32_t> consumers;                         // regionID -> actor
    std::map<std::pair<uint32_t, uint8_t>, uint32_t> makers, producers;
    auto newActor = [this](uint8_t kind, uint8_t sector, uint32_t corp, uint32_t regionID, uint32_t nodeID) -> uint32_t {
        Actor a = Actor();
        a.actorID = (uint32_t)m_actors.size() + 1;
        a.kind = kind;
        a.sector = sector;
        a.ownerCorpID = corp;
        a.regionID = regionID;
        a.nodeID = nodeID;
        m_actors.push_back(a);
        return a.actorID;
    };

    for (auto& node : m_static.nodes) {
        if (consumers.find(node.regionID) == consumers.end())
            consumers[node.regionID] = newActor(ActorKind::Consumer, 0, m_static.MarketMakerCorp(node.nodeID, Sector::Raw), node.regionID, node.nodeID);
        for (auto& cur : m_static.types) {
            const TypeData& t(cur.second);
            if (!SimClass::HasCells(t.simClass) or !(t.refPrice > 0.0))
                continue;
            // regional nodes carry the types with real regional activity
            if ((node.kind != NodeKind::Hub) and (m_static.RegionActive(node.regionID, t.typeID) < 0.2f))
                continue;
            Cell c = Cell();
            c.index = (uint32_t)m_cells.size();
            c.nodeID = node.nodeID;
            c.typeID = t.typeID;
            c.sector = StaticData::SectorOf(t);
            c.simClass = t.simClass;
            std::pair<uint32_t, uint8_t> mk(node.nodeID, c.sector), pk(node.nodeID, t.simClass);
            if (makers.find(mk) == makers.end())
                makers[mk] = newActor(ActorKind::MarketMaker, c.sector, m_static.MarketMakerCorp(node.nodeID, c.sector), node.regionID, node.nodeID);
            if (producers.find(pk) == producers.end())
                producers[pk] = newActor(ActorKind::Producer, t.simClass, m_static.ProducerCorp(node.nodeID, t.simClass), node.regionID, node.nodeID);
            c.mm = makers[mk];
            c.producer = producers[pk];
            c.consumer = consumers[node.regionID];
            c.day = DayOf(now);
            m_cellIndex[((uint64_t)c.nodeID << 16) | c.typeID] = c.index;
            m_regionCells[std::make_pair(node.regionID, c.typeID)].push_back(c.index);
            m_cells.push_back(c);
        }
    }

    // npc vendors:  one actor per corp
    std::map<uint32_t, uint32_t> vendors;
    for (auto& offer : m_static.vendors) {
        const TypeData* t(m_static.Type(offer.typeID));
        if ((t == nullptr) or !(t->basePrice > 0.0))
            continue;
        if (vendors.find(offer.corpID) == vendors.end())
            vendors[offer.corpID] = newActor(ActorKind::Vendor, 0, offer.corpID, offer.regionID, 0);
        VendorSlot slot = VendorSlot();
        slot.offer = offer;
        slot.actorID = vendors[offer.corpID];
        // vendors sell at basePrice (Crucible)
        slot.price = std::max<Cents>(1, Econ::ToCents(t->basePrice));
        m_vendorSlots.push_back(slot);
    }

    // per cell parameters
    for (auto& c : m_cells) {
        const TypeData& t(*m_static.Type(c.typeID));
        const NodeData& node(*Node(c.nodeID));
        double ratio(m_static.RegionPrice(node.regionID, t.typeID));
        c.refLocal = t.refPrice * ratio;
        double units(t.unitsPerDay * (m_p.unitsPerTradingDay ? t.activeShare : 1.0f));
        c.d0 = units * m_static.RegionUnits(node.regionID, t.typeID) * m_p.economyScale;
        // a unit the stock model consumes trades twice (producer to market maker, market maker to consumer), so the
        //  calibrated traded volume is TradesPerUnit x consumption
        c.dc = c.d0 / std::max(1.0, m_p.tradesPerUnit);
        // the round trip has to lose:  2h >= broker fee on both orders + sales tax + a margin
        double feeFloor(m_p.simPaysFees ? (m_p.brokerFeeRate + m_p.salesTaxRate / 2.0 + 0.0025) : 0.0025);
        c.h = Econ::HalfSpread(m_static.HalfSpread(t), m_p.minHalfSpread, feeFloor);
        c.theta = m_static.StockElasticity(t);
        c.eta = m_static.Elasticity(t);
        c.wc = m_static.CostPassThrough(t);
        c.coverDays = m_static.CoverDays(t);
        c.costRef = 0.0;
        for (auto& in : t.bom) {
            const TypeData* it(m_static.Type(in.first));
            if ((it != nullptr) and (it->refPrice > 0.0))
                c.costRef += in.second * it->refPrice * m_static.RegionPrice(node.regionID, in.first);
        }
        if (!(c.costRef > 0.0))
            c.wc = 0.0;
        double vol(t.volatility > 0.0f ? t.volatility : 0.05);
        c.sigma = m_p.marketNoise * vol;
        // the day's transient move of the quotes (bounce):  at most 3/4 of the half spread, so buying on a low day and
        //  selling on a high one still loses against the spread
        c.sigmaT = std::min(m_p.transientNoise * vol, 0.75 * c.h);
        // order sizes follow the calibrated lot, but never more than a quarter of the (scaled) daily demand
        double lotCap(std::floor(c.dc / 4.0));
        c.lot = (uint32_t)std::max(1.0, std::min((double)std::max<uint32_t>(1, t.lotSize), lotCap));
    }

    // hot cells:  the top share of each node's cells by isk turnover.  quote levels:  the full ladder for the most
    //  valuable cells while the order cap allows, one level per side for the rest
    std::vector<uint32_t> order(m_cells.size());
    for (uint32_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [this](uint32_t a, uint32_t b) {
        double va(m_cells[a].d0 * m_cells[a].refLocal), vb(m_cells[b].d0 * m_cells[b].refLocal);
        if (va != vb)
            return va > vb;
        return a < b;
    });
    std::map<uint32_t, uint32_t> perNode, hotPerNode;
    for (auto& c : m_cells)
        ++perNode[c.nodeID];
    int levels(std::min(m_p.quoteLevels, m_p.maxOrdersPerCellSide));
    uint64_t budget(m_p.simMaxOrders > m_vendorSlots.size() ? m_p.simMaxOrders - m_vendorSlots.size() : 0);
    uint64_t used(0);
    for (size_t i = 0; i < order.size(); ++i) {
        Cell& c(m_cells[order[i]]);
        uint64_t remainingCells(order.size() - i - 1);
        // keep one level per side for every cell still to come
        c.levels = ((used + 2 * levels + 2 * remainingCells) <= budget) ? (uint8_t)levels : 1;
        used += 2 * c.levels;
        if (hotPerNode[c.nodeID] < (uint32_t)std::ceil(perNode[c.nodeID] * m_p.hotCellShare)) {
            ++hotPerNode[c.nodeID];
            c.flags |= CellFlag::Hot;
        }
    }

    // money targets (PLAN 6.8):  M* = MoneyDaysTarget x consumer spend per day at reference prices
    double spend(0.0);
    std::map<uint32_t, double> spendByConsumer, turnoverByMaker, costByProducer;
    for (auto& c : m_cells) {
        double v(c.dc * c.refLocal * (1.0 + c.h) * 100.0);
        spend += v;
        spendByConsumer[c.consumer] += v;
        // working capital:  days of turnover, and the value of the target stock (the bids reach up to twice the target,
        //  and refilling an empty cell takes it;  thin, dear cells need more than their turnover says)
        turnoverByMaker[c.mm] += c.dc * c.refLocal * 100.0 * m_p.marketMakerCapitalDays
                               + std::max(c.dc * c.coverDays, 2.0 * c.lot) * c.refLocal * 100.0;
        if (!SimClass::IsLoot(c.simClass))
            costByProducer[c.producer] += c.dc * c.refLocal * (1.0 - c.h) * m_p.producerCostShare * 100.0;
    }
    m_income0 = (Cents)std::llround(spend);
    // M* (PLAN 6.8) is MoneyDaysTarget days of consumer spending for the consumers, plus the working capital the market
    //  makers and producers need on top (their stock's worth, which the plan's M* leaves out)
    double others(0.0);
    for (auto& a : m_actors) {
        if (a.kind == ActorKind::MarketMaker) {
            a.target = turnoverByMaker[a.actorID];
            others += a.target;
        } else if (a.kind == ActorKind::Producer) {
            a.target = costByProducer[a.actorID];
            others += a.target;
        }
    }
    double consumerMoney(m_p.moneyDaysTarget * spend);
    m_moneyTarget = consumerMoney + others;
    for (auto& a : m_actors) {
        if (a.kind != ActorKind::Consumer)
            continue;
        a.share = (spend > 0.0) ? spendByConsumer[a.actorID] / spend : 0.0;
        a.target = consumerMoney * a.share;
    }
}

void World::Seed(int64_t now, uint32_t nodeID, bool freshOnly)
{
    /* fair values of cells that never had one:  the reference price first, then the anchor (which needs the inputs' fair
     *  values), a few rounds so chains of inputs settle */
    std::vector<uint32_t> fresh;
    for (auto& c : m_cells)
        if ((c.lnV == 0.0) and ((nodeID == 0) or (c.nodeID == nodeID)) and ((c.flags & CellFlag::Disabled) == 0)) {
            fresh.push_back(c.index);
            c.lnV = std::log(std::max(0.01, c.refLocal));
        }
    for (int round = 0; round < 4; ++round)
        for (auto idx : fresh) {
            Cell& c(m_cells[idx]);
            c.lnAnchor = std::log(AnchorOf(c));
            c.lnV = c.lnAnchor;
        }
    std::set<uint32_t> isFresh(fresh.begin(), fresh.end());
    for (auto& c : m_cells) {
        if ((nodeID != 0) and (c.nodeID != nodeID))
            continue;
        if ((c.flags & CellFlag::Disabled) != 0)
            continue;
        if (freshOnly and (isFresh.count(c.index) == 0))
            continue;
        Changed(c);
        if (c.dBar <= 0.0)
            c.dBar = c.dc;
        c.targetStock = TargetStock(c);
        const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
        Units have((s == nullptr) ? 0 : (s->qty + s->reserved));
        Units want((Units)std::ceil(c.targetStock));
        if (want > have)
            m_ledger.GoodsIn(c.mm, (uint16_t)c.nodeID, c.typeID, want - have, Goods::Seed);
        if (c.lastTime == 0)
            c.lastTime = now;
        // seeding asks for its quotes now
        c.flags |= CellFlag::Dirty;
        if (m_started)
            Schedule(c, now);
    }
    // working money (one-off faucet)
    for (auto& a : m_actors) {
        if ((nodeID != 0) and (a.nodeID != nodeID))
            continue;
        if ((a.kind == ActorKind::Vendor) or !(a.target > 0.0) or (a.state != 0))
            continue;
        if (freshOnly and (m_ledger.FindAcct(a.actorID) != nullptr))
            continue;
        Cents have(m_ledger.MoneyOf(a.actorID));
        Cents want((Cents)std::llround(a.target));
        if (want > have)
            m_ledger.Faucet(a.actorID, want - have, Isk::Seed);
    }
}

void World::Start(int64_t now, bool catchUp)
{
    while (!m_due.empty())
        m_due.pop();
    for (auto& c : m_cells) {
        if ((c.flags & CellFlag::Disabled) != 0)
            continue;
        if (c.lastTime == 0)
            c.lastTime = now;
        int64_t period(Period(c));
        int64_t jitter((int64_t)(Rng::Uniform(H(c, 0, hJitter)) * (double)period));
        if ((c.flags & CellFlag::Dirty) != 0) {
            c.nextDue = now + (jitter % 60);
        } else if (c.nextDue == 0) {
            c.nextDue = now + jitter;
        } else if (catchUp and (c.nextDue < now)) {
            // overdue after a downtime:  spread over the next tick (the visit catches up the whole gap, capped)
            c.nextDue = now + (jitter % (60 * (int64_t)m_p.tickMinutes));
        }
        m_due.push(Due{c.nextDue, DueCell, c.index});
    }
    int64_t day((int64_t)DayOf(now) * SecondsPerDay);
    m_due.push(Due{day + SecondsPerDay, DueDay, 0});
    m_due.push(Due{now - (now % 3600) + 3600, DueHour, 0});
    if (m_lastIncome == 0)
        m_lastIncome = now;
    if (m_lastIncome < now - 3600LL * m_p.maxCatchUpHours)
        m_lastIncome = now - 3600LL * m_p.maxCatchUpHours;
    m_auditDay = DayOf(now);
    m_ledger.BeginPeriod(m_auditDay);
    double factor(m_incomeFactor);
    if (!(factor > 0.0))
        m_incomeFactor = 1.0;
    m_started = true;
}

/************************************************************************************************************************
 * scheduling
 */

void World::Step(int64_t now, double budgetUs, std::function<double()> usNow)
{
    if (!m_started)
        return;
    if (m_paused) {
        FlushOrders();
        return;
    }
    double start(((budgetUs > 0.0) and usNow) ? usNow() : 0.0);
    while (!m_due.empty() and (m_due.top().t <= now)) {
        if ((budgetUs > 0.0) and usNow and ((usNow() - start) > budgetUs))
            break;
        if (m_batch.Size() >= m_p.maxOrderOpsPerStep)
            break;
        Due d(m_due.top());
        m_due.pop();
        switch (d.kind) {
            case DueDay:
                Daily(d.t);
                m_due.push(Due{d.t + SecondsPerDay, DueDay, 0});
                break;
            case DueHour:
                Hourly(d.t);
                m_due.push(Due{d.t + 3600, DueHour, 0});
                break;
            default: {
                if (d.index >= m_cells.size())
                    break;
                Cell& c(m_cells[d.index]);
                if ((c.nextDue != d.t) or ((c.flags & CellFlag::Disabled) != 0))
                    break;          // rescheduled, or gone
                // a second visit in one step must see the orders of the first
                if ((c.flags & CellFlag::Pending) != 0)
                    FlushOrders();
                ProcessCell(c, d.t);
            } break;
        }
        if (m_paused)
            break;
    }
    FlushOrders();
}

size_t World::Backlog(int64_t now) const
{
    if (m_due.empty() or (m_due.top().t > now))
        return 0;
    return (size_t)(now - m_due.top().t);
}

void World::RollDay(Cell& c, int64_t t)
{
    int32_t day(DayOf(t));
    if (day == c.day)
        return;
    if (c.dVol > 0) {
        HistoryRow row = HistoryRow();
        row.regionID = Node(c.nodeID)->regionID;
        row.typeID = c.typeID;
        row.day = c.day;
        row.low = c.dLow;
        row.high = c.dHigh;
        row.sumValue = c.dSum;
        row.volume = c.dVol;
        row.trades = c.dTrades;
        m_historyDone.push_back(row);
    }
    c.day = day;
    c.dLow = c.dHigh = c.dSum = 0;
    c.dVol = 0;
    c.dTrades = 0;
    c.dIntake = 0;
    c.dPlayerVol = 0;
    c.dByPlayer.clear();
}

void World::Print(Cell& c, Cents price, Units qty)
{
    if (qty <= 0)
        return;
    if (c.dVol == 0) {
        c.dLow = c.dHigh = price;
    } else {
        c.dLow = std::min(c.dLow, price);
        c.dHigh = std::max(c.dHigh, price);
    }
    Cents value(Econ::MulCents(price, qty));
    if (value > 0)
        c.dSum += value;
    c.dVol += qty;
    ++c.dTrades;
    c.lastClear = Econ::ToIsk(price);
    m_historyTouched[std::make_pair(Node(c.nodeID)->regionID, c.typeID)] = true;
}

void World::PlayerFeedback(Cell& c, uint32_t ownerID, Cents price, Units qty)
{
    // PLAN 7.6.7:  player trades count with PlayerTradeWeight, and not at all from a counterparty that makes up more than
    //  a fifth of the cell's player volume today (measured against at least a day's demand, so a small trader counts)
    c.dPlayerVol += qty;
    Units& mine(c.dByPlayer[ownerID]);
    mine += qty;
    double base(std::max((double)c.dPlayerVol, c.dBar));
    double weight(((double)mine > 0.2 * base) ? 0.0 : m_p.playerTradeWeight);
    if (weight > 0.0)
        c.lnV += Econ::ExecFeedback(std::log(Mid(c)), Econ::ToIsk(price), (double)qty, std::max(c.dBar, 1.0), m_p.lambdaExec * weight)
               - std::log(Mid(c));
}

void World::MarkDirty(uint32_t regionID, uint16_t typeID, int64_t now)
{
    std::map<std::pair<uint32_t, uint16_t>, std::vector<uint32_t>>::iterator itr = m_regionCells.find(std::make_pair(regionID, typeID));
    if (itr == m_regionCells.end())
        return;
    for (auto idx : itr->second) {
        Cell& c(m_cells[idx]);
        if ((c.flags & CellFlag::Disabled) != 0)
            continue;
        c.flags |= CellFlag::Dirty;
        int64_t due(now + m_p.dirtyCellSeconds);
        if (m_started and (c.nextDue > due))
            Schedule(c, due);
    }
}

/************************************************************************************************************************
 * a cell's visit (PLAN 7.3)
 */

void World::ProcessCell(Cell& c, int64_t t)
{
    ++m_cellVisits;
    Changed(c);
    RollDay(c, t);
    c.flags &= ~CellFlag::Dirty;
    double dt(std::min(MaxCellDtDays, (double)(t - c.lastTime) / (double)SecondsPerDay));
    c.lastTime = t;
    c.shocked = (c.shockUntil > t) and (c.shockMult != 1.0);

    double anchor(AnchorOf(c));
    if (dt > 0.0) {
        c.lnAnchor = std::log(anchor);
        // stock pressure and the fair value's walk (PLAN 6.2)
        const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
        double stock((s == nullptr) ? 0.0 : (double)(s->qty + s->reserved));
        c.targetStock = TargetStock(c);
        bool clamped(false);
        // a lot of slack on both sides:  one sold-out lot of a thin type is scarcity, not a clamp (and a full shelf is
        //  exactly the anchor)
        double vStar(Econ::TargetValue(anchor, stock + c.lot, c.targetStock + c.lot, c.theta, m_p.kMax, &clamped));
        c.vStar = vStar;
        double kappa(Ln2 / std::max(0.1, m_p.fairValueHalfLifeDays));
        c.lnV = Econ::OUStep(c.lnV, std::log(vStar), kappa, dt, c.sigma, Rng::Normal(H(c, t, hOU)));
        // V* is clamped to [anchor / Kmax, anchor x Kmax] (PLAN 6.2);  the noise may take V a little further, never
        //  beyond Kmax squared
        double lo(c.lnAnchor - 2.0 * std::log(m_p.kMax)), hi(c.lnAnchor + 2.0 * std::log(m_p.kMax));
        c.lnV = std::min(hi, std::max(lo, c.lnV));

        /* internal executions (consumers lifting asks, producers hitting bids) print at the quotes, which were made from
         *  V:  feeding them back would only pull V towards its own past quotes (and any imbalance between the two
         *  kinds of print turns into a drift).  their effect reaches V through the stock instead (PLAN 6.2 stock
         *  pressure).  trades with players feed back (PlayerFeedback) */
        ConsumerDemand(c, t, dt, anchor);
        SellToPlayerBids(c, t);
        ProducerSupply(c, t, dt, anchor);
        c.lnV = std::min(hi, std::max(lo, c.lnV));
        // V* at a Kmax bound:  a stock-out or a glut (the stability test counts the days a cell ends that way)
        if (clamped)
            c.flags |= CellFlag::Clamped;
        else
            c.flags &= ~CellFlag::Clamped;
    }
    RequoteCell(c, t);
    Schedule(c, t + Period(c));
}

void World::ConsumerDemand(Cell& c, int64_t t, double dt, double anchor)
{
    const NodeData& node(*Node(c.nodeID));
    const TypeData& type(*m_static.Type(c.typeID));
    uint32_t consumer(c.consumer);

    // the asks consumers can lift:  the market maker's and the players' anywhere in the region, cheapest first
    struct Quote {
        Cents    price;
        uint32_t qty;
        bool     player;
        int64_t  issued;
        size_t   index;     // player order index or level
    };
    std::vector<PlayerOrder> pAsks, pBids;
    m_gw.PlayerOrders(node.regionID, c.typeID, pAsks, pBids);
    std::vector<Quote> quotes;
    for (int i = 0; i < c.levels; ++i) {
        if (c.ask[i] == 0)
            continue;
        std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(c.ask[i]);
        if ((itr == m_orders.end()) or (itr->second.qty == 0))
            continue;
        quotes.push_back(Quote{itr->second.price, itr->second.qty, false, itr->second.issued, (size_t)i});
    }
    // player asks within the buy band (PLAN 7.3), and any under the sim's own asks
    Cents playerLimit(Econ::ToCents(Value(c) * (1.0 + std::max(m_p.buyBand, c.h))));
    for (auto& q : quotes)
        playerLimit = std::max(playerLimit, q.price);
    for (size_t i = 0; i < pAsks.size(); ++i)
        if ((pAsks[i].qty > 0) and (pAsks[i].price <= playerLimit))
            quotes.push_back(Quote{pAsks[i].price, pAsks[i].qty, true, pAsks[i].issued, i});
    std::sort(quotes.begin(), quotes.end(), [](const Quote& a, const Quote& b) {
        if (a.price != b.price)
            return a.price < b.price;
        if (a.player != b.player)
            return a.player;            // players first at the same price
        if (a.issued != b.issued)
            return a.issued < b.issued;
        return a.index < b.index;
    });

    double refAsk(anchor * (1.0 + c.h));
    double weekday(m_static.Weekday(type, WeekdayOf(DayOf(t))));
    double shock(c.shocked ? c.shockMult : 1.0);
    const Actor& cons(m_actors[consumer - 1]);
    double budget(Econ::BudgetFactor((double)m_ledger.MoneyOf(consumer), cons.target));
    // nothing sensible to buy:  no asks, or only absurd ones
    double best(quotes.empty() ? 0.0 : Econ::ToIsk(quotes[0].price));
    double rate(0.0);
    if ((best > 0.0) and (best <= refAsk * m_p.kMax))
        rate = Econ::DemandRate(c.dc, weekday, shock, budget, best, refAsk, c.eta);
    double wanted(rate * dt);
    // the demand the cell would see at its own fair ask (stock targets follow it even when nothing could be bought)
    double intent(Econ::DemandRate(c.dc, weekday, shock, budget, refAsk, refAsk, c.eta) * dt);

    Units filled(0), unfilled(0);
    if (wanted > 0.0) {
        // arrivals of 'lot' units.  many arrivals are grouped (at most 64 walks per visit)
        uint64_t arrivals(Rng::Poisson(wanted / (double)c.lot, H(c, t, hArrivals)));
        uint64_t groups(std::min<uint64_t>(arrivals, 64));
        Units total((Units)arrivals * c.lot);
        for (uint64_t g = 0; g < groups; ++g) {
            Units want(total / (Units)groups + (((Units)g < total % (Units)groups) ? 1 : 0));
            // each arrival lifts the cheapest ask and walks deeper only within its willingness to pay (PLAN 6.3);  the
            //  price response itself is the demand rate's elasticity
            double wtp(best * std::exp(m_p.sigmaWtp * std::fabs(Rng::Normal(H(c, t, hWtp, g)))));
            for (auto& q : quotes) {
                if (want <= 0)
                    break;
                if ((q.qty == 0) or (Econ::ToIsk(q.price) > wtp))
                    continue;
                Units take(std::min<Units>(want, q.qty));
                Cents wallet(m_ledger.FindAcct(consumer) ? m_ledger.FindAcct(consumer)->wallet : 0);
                if (Econ::MulCents(q.price, take) > wallet)
                    take = wallet / q.price;
                if (take <= 0) {
                    want = 0;       // out of money
                    break;
                }
                if (q.player) {
                    PlayerOrder& po(pAsks[q.index]);
                    take = std::min<Units>(take, IntakeLeft(c, po.ownerID, po.price, t));
                    if (take <= 0)
                        continue;
                    uint32_t got(m_gw.TakePlayerAsk(po, (uint32_t)take, consumer, m_actors[c.mm - 1].ownerCorpID));
                    if (got == 0) {
                        q.qty = 0;
                        continue;
                    }
                    Cents value(Econ::MulCents(po.price, got));
                    m_ledger.ToPlayer(consumer, value, false);
                    m_ledger.GoodsIn(consumer, (uint16_t)c.nodeID, c.typeID, got, Goods::PlayerToSim);
                    m_ledger.GoodsOut(consumer, (uint16_t)c.nodeID, c.typeID, got, Goods::Consumption, false);
                    RecordIntake(c, po.ownerID, got, value, t);
                    if (m_persist != nullptr)
                        m_persist->PlayerFill(consumer, -value, 0, 0, 0, 0, 0);
                    PlayerFeedback(c, po.ownerID, po.price, got);
                    q.qty -= got;
                    po.qty -= got;
                    want -= got;
                    filled += got;
                    ++m_playerTrades;
                } else {
                    SimOrder& o(m_orders[c.ask[q.index]]);
                    Cents value(Econ::MulCents(q.price, take));
                    if (!m_ledger.Transfer(consumer, c.mm, value))
                        break;
                    m_ledger.GoodsOut(c.mm, (uint16_t)c.nodeID, c.typeID, take, Goods::Consumption, true);
                    o.qty -= (uint32_t)take;
                    q.qty -= (uint32_t)take;
                    Print(c, q.price, take);
                    want -= take;
                    filled += take;
                }
            }
            unfilled += std::max<Units>(0, want);
        }
    }
    // what couldn't be bought because everything was sold
    bool soldOut(true);
    for (auto& q : quotes)
        if (q.qty > 0)
            soldOut = false;

    /* demand feeds the stock target:  the expected demand at the cell's fair ask (smooth;  a thin type's single sale
     *  would otherwise read as a burst), plus what went unserved because the shelves were empty (PLAN 6.3), kept between
     *  a quarter and four times the calibration */
    double observed((intent + (soldOut ? (double)unfilled : 0.0)) / dt);
    double alpha(1.0 - std::exp(-dt * Ln2 / std::max(0.1, m_p.demandHalfLifeDays)));
    c.dBar += alpha * (observed - c.dBar);
    if (c.dc > 0.0)
        c.dBar = std::min(4.0 * c.dc, std::max(0.25 * c.dc, c.dBar));
}

void World::SellToPlayerBids(Cell& c, int64_t t)
{
    const NodeData& node(*Node(c.nodeID));
    std::vector<PlayerOrder> pAsks, pBids;
    m_gw.PlayerOrders(node.regionID, c.typeID, pAsks, pBids);
    if (pBids.empty())
        return;
    // the sim sells into player bids at or above V (1 - max(SellBand, h/2)):  above its own bid, so a round trip loses
    double v(Value(c));
    Cents limit(Econ::RoundPrice(v * std::max(1.0 - m_p.sellBand, 1.0 - c.h / 2.0), true));
    Ledger::Stock& s(m_ledger.Inv(c.mm, (uint16_t)c.nodeID, c.typeID));
    Units keep((Units)std::ceil(0.5 * c.targetStock));
    Units sellable(std::min<Units>(s.qty, std::max<Units>(0, s.qty + s.reserved - keep)));
    uint32_t corp(m_actors[c.mm - 1].ownerCorpID);
    for (auto& bid : pBids) {
        if (sellable <= 0)
            break;
        if (bid.price < limit)
            break;
        if (!m_gw.RangeCovers(bid.range, bid.stationID, node.stationID))
            continue;
        Units q(std::min<Units>(bid.qty, sellable));
        if ((q < bid.minVolume) and (bid.qty >= bid.minVolume))
            continue;
        uint32_t got(m_gw.TakePlayerBid(bid, (uint32_t)q, c.mm, corp, node.stationID));
        if (got == 0)
            continue;
        Cents value(Econ::MulCents(bid.price, got));
        Cents tax(m_p.simPaysFees ? (Cents)std::llround((double)value * m_p.salesTaxRate) : 0);
        m_ledger.GoodsOut(c.mm, (uint16_t)c.nodeID, c.typeID, got, Goods::SimToPlayer, false);
        m_ledger.FromPlayer(c.mm, value);
        tax = m_ledger.Sink(c.mm, tax, Isk::SalesTax, false);
        if (m_persist != nullptr)
            m_persist->PlayerFill(c.mm, value - tax, 0, c.nodeID, c.typeID, -(Units)got, 0);
        PlayerFeedback(c, bid.ownerID, bid.price, got);
        sellable -= got;
        ++m_playerTrades;
    }
}

void World::ProducerSupply(Cell& c, int64_t t, double dt, double anchor)
{
    // capacity:  SupplyFactor x calibrated demand, and enough to refill an empty target in RestockDays
    double cap(std::max(m_p.supplyFactor * c.dc, c.targetStock / std::max(0.1, m_p.restockDays)));
    Units avail((Units)Rng::Poisson(cap * dt, H(c, t, hProduce)));
    // up to the target stock only
    {
        const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
        double stock((s == nullptr) ? 0.0 : (double)(s->qty + s->reserved));
        avail = std::min<Units>(avail, std::max<Units>(0, (Units)std::ceil(c.targetStock - stock)));
    }
    if (avail <= 0)
        return;
    bool loot(SimClass::IsLoot(c.simClass));
    // production cost (an isk sink).  loot comes free, but isn't given away below half its anchor
    Cents cost(ProducerCost(c, anchor));
    Cents floor(loot ? Econ::ToCents(anchor * 0.5) : cost);
    for (int i = 0; (i < c.levels) and (avail > 0); ++i) {
        if (c.bid[i] == 0)
            continue;
        std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(c.bid[i]);
        if (itr == m_orders.end())
            continue;
        SimOrder& o(itr->second);
        if ((o.qty == 0) or (o.price < floor))
            continue;
        Units q(std::min<Units>(avail, o.qty));
        Cents value(Econ::MulCents(o.price, q));
        Cents costTotal(Econ::MulCents(cost, q));
        if ((value < 0) or (costTotal < 0))
            continue;
        // made by the producer, sold into the market maker's bid
        m_ledger.GoodsIn(c.producer, (uint16_t)c.nodeID, c.typeID, q, Goods::Production);
        m_ledger.MoveGoods(c.producer, c.mm, (uint16_t)c.nodeID, c.typeID, q, false);
        if (!m_ledger.PayFromReserved(c.mm, c.producer, value)) {
            // escrow short (can't happen while reservations hold):  the goods stay with the producer
            m_ledger.MoveGoods(c.mm, c.producer, (uint16_t)c.nodeID, c.typeID, q, false);
            break;
        }
        m_ledger.Sink(c.producer, costTotal, Isk::Production, false);
        o.qty -= (uint32_t)q;
        avail -= q;
        Print(c, o.price, q);
    }
    /* restocking below target when the bids are under the producers' cost (the noise took V low):  the market maker buys
     *  straight from its producer at cost, a ledger-only trade players can't reach, so the shelves don't stay empty */
    if (avail <= 0)
        return;
    const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
    double stock((s == nullptr) ? 0.0 : (double)(s->qty + s->reserved));
    Units need((Units)std::floor(c.targetStock - stock));
    const Ledger::Account* a(m_ledger.FindAcct(c.mm));
    Cents wallet((a == nullptr) ? 0 : a->wallet);
    // loot is free to its source, which sells it at its floor
    Cents price(loot ? floor : cost);
    Units q(std::min<Units>(avail, need));
    if ((price > 0) and (q > 0))
        q = std::min<Units>(q, wallet / price);
    if (q <= 0)
        return;
    Cents value(Econ::MulCents(price, q));
    if ((value < 0) or !m_ledger.Transfer(c.mm, c.producer, value))
        return;
    m_ledger.GoodsIn(c.producer, (uint16_t)c.nodeID, c.typeID, q, Goods::Production);
    m_ledger.MoveGoods(c.producer, c.mm, (uint16_t)c.nodeID, c.typeID, q, false);
    // a transfer inside the supply chain rather than a market trade:  no print
    if (!loot)
        m_ledger.Sink(c.producer, value, Isk::Production, false);
}

Cents World::ProducerCost(const Cell& c, double anchor) const
{
    if (SimClass::IsLoot(c.simClass))
        return 0;
    return Econ::ToCents(anchor * (1.0 - c.h) * m_p.producerCostShare);
}

/************************************************************************************************************************
 * quotes (PLAN 6.2, 7.1, 7.6)
 */

Cents World::BidOf(uint32_t nodeID, uint16_t typeID) const
{
    std::map<uint64_t, uint32_t>::const_iterator itr = m_cellIndex.find(((uint64_t)nodeID << 16) | typeID);
    if (itr == m_cellIndex.end())
        return 0;
    const Cell& c(m_cells[itr->second]);
    if (c.bid[0] != 0) {
        std::unordered_map<uint32_t, SimOrder>::const_iterator o = m_orders.find(c.bid[0]);
        if (o != m_orders.end())
            return o->second.price;
    }
    return Econ::RoundPrice(Value(c) * (1.0 - c.h), false);
}

void World::RequoteCell(Cell& c, int64_t t)
{
    const TypeData& type(*m_static.Type(c.typeID));
    // the quotes centre on V, moved by the day's transient noise
    double v(Value(c) * std::exp(c.sigmaT * Rng::Normal(Rng::Hash64(m_p.seed, Key(c), (uint64_t)DayOf(t), hMood))));
    c.quoteMid = v;
    const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
    Units stock((s == nullptr) ? 0 : (s->qty + s->reserved));
    int levels(c.levels);
    Cents asks[MaxLevels], bids[MaxLevels];
    Econ::QuoteLadder(v, c.h, levels, asks, bids);

    // no-arbitrage floors on the asks:  reprocessing at perfect yield into the sim's mineral bids, and a platinum
    //  insurance payout net of its premium.  (1 + epsilon) above either
    Cents floor(0);
    if (!type.refine.empty()) {
        double value(0.0);
        for (auto& out : type.refine)
            value += out.second * (double)BidOf(c.nodeID, out.first);
        floor = std::max<Cents>(floor, (Cents)std::ceil(value * (1.0 + m_p.arbitrageMargin)));
    }
    if (type.categoryID == Category::Ship) {
        double net(m_gw.InsuranceNet(c.typeID));
        if (net > 0.0)
            floor = std::max<Cents>(floor, Econ::ToCents(net * (1.0 + m_p.arbitrageMargin)) + 1);
    }
    if (asks[0] < floor) {
        double d(c.h / 2.0);
        for (int i = 0; i < levels; ++i)
            asks[i] = std::max(asks[i], Econ::RoundPrice(Econ::ToIsk(floor) * (1.0 + i * d), true));
    }
    // bids:  never above the replacement cost less the spread for what can be built (PLAN 7.6.9).  in the stock model the
    //  producers' cost is a replacement cost too, so a bid never falls below what lets them restock
    if (c.wc > 0.0) {
        Cents cap(std::max(Econ::RoundPrice(CostNow(c) * (1.0 - c.h), false), ProducerCost(c, std::exp(c.lnAnchor))));
        for (int i = 0; i < levels; ++i)
            bids[i] = std::min(bids[i], cap);
    }
    for (int i = 0; i < levels; ++i)
        if (bids[i] >= asks[0])
            bids[i] = std::max<Cents>(1, asks[0] - 1 - i);

    // sizes.  asks:  up to QuoteDepthDays of demand from the stock, filled from the best level down.  bids:  what it
    //  takes to bring the stock back to its target, as far as the market maker's money goes
    double depth(std::max((double)c.lot, m_p.quoteDepthDays * c.dBar));
    double weights(0.0);
    for (int i = 0; i < levels; ++i)
        weights += 1.0 + i;
    Units askQ[MaxLevels] = {}, bidQ[MaxLevels] = {};
    Units left(std::min<Units>(stock, (Units)std::ceil(depth)));
    for (int i = 0; (i < levels) and (left > 0); ++i) {
        Units q((i == levels - 1) ? left : Econ::RoundLot(depth * (1.0 + i) / weights, c.lot));
        if (q < (Units)c.lot)
            q = std::min<Units>(left, c.lot);
        q = std::min<Units>(q, left);
        askQ[i] = std::min<Units>(q, 0x7FFFFFFF);
        left -= askQ[i];
    }
    // the market maker keeps bidding (for players) until its stock is twice the target;  its producers only fill it up to
    //  the target (ProducerSupply)
    double need(m_p.bidStockFactor * c.targetStock - (double)stock);
    if (need >= 0.5 * c.lot) {
        Units bleft((Units)std::ceil(std::min(need, depth)));
        for (int i = 0; (i < levels) and (bleft > 0); ++i) {
            Units q((i == levels - 1) ? bleft : Econ::RoundLot(depth * (1.0 + i) / weights, c.lot));
            if (q < (Units)c.lot)
                q = std::min<Units>(bleft, c.lot);
            q = std::min<Units>(q, bleft);
            bidQ[i] = std::min<Units>(q, 0x7FFFFFFF);
            bleft -= bidQ[i];
        }
        // the escrow comes from the market maker's free money plus what this cell's bids hold now
        const Ledger::Account* a(m_ledger.FindAcct(c.mm));
        Cents money((a == nullptr) ? 0 : a->wallet);
        for (int i = 0; i < levels; ++i) {
            if (c.bid[i] == 0)
                continue;
            std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(c.bid[i]);
            if (itr != m_orders.end())
                money += itr->second.price * itr->second.qty;
        }
        // keep a fifth of the free money for fees and other cells
        money = (Cents)(money * 0.8);
        for (int i = 0; i < levels; ++i) {
            Cents value(Econ::MulCents(bids[i], bidQ[i]));
            if ((value < 0) or (value > money)) {
                bidQ[i] = (bids[i] > 0) ? std::max<Units>(0, money / bids[i]) : 0;
                value = bids[i] * bidQ[i];
            }
            money -= value;
        }
    }
    QuoteSide(c, false, asks, askQ, t);
    QuoteSide(c, true, bids, bidQ, t);
}

void World::QuoteSide(Cell& c, bool bid, const Cents* prices, const Units* qtys, int64_t t)
{
    uint32_t* slots(bid ? c.bid : c.ask);
    for (int i = 0; i < MaxLevels; ++i) {
        uint32_t& slot(slots[i]);
        SimOrder* o(nullptr);
        if (slot != 0) {
            std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(slot);
            if (itr == m_orders.end())
                slot = 0;
            else
                o = &itr->second;
        }
        Units want((i < c.levels) ? qtys[i] : 0);
        if (want <= 0) {
            if (o != nullptr)
                CancelOrderNow(slot);
            continue;
        }
        if (o == nullptr) {
            PostOrder(&c, 0, c.mm, bid, (uint8_t)i, prices[i], (uint32_t)want, t);
            continue;
        }
        double p(Econ::ToIsk(o->price));
        bool priceMove(std::fabs(Econ::ToIsk(prices[i]) - p) > m_p.requoteThreshold * p);
        bool qtyMove((o->qty == 0) or (std::fabs((double)want - (double)o->qty) > m_p.requoteQtyThreshold * std::max((double)o->qty, (double)want)));
        bool stale((t - o->issued) > StaleOrderSeconds);
        if (priceMove or qtyMove or stale)
            ChangeOrder(*o, priceMove ? prices[i] : o->price, (uint32_t)want, t);
        if (o->qty == 0) {
            CancelOrderNow(slot);
            continue;
        }
        if (stale and !priceMove)
            o->issued = t;
        QueueAlter(*o);
    }
}

void World::PostOrder(Cell* c, uint32_t vendorSlot, uint32_t actorID, bool bid, uint8_t level, Cents price, uint32_t qty, int64_t t)
{
    if ((qty == 0) or (price <= 0))
        return;
    SimOrder o = SimOrder();
    o.actorID = actorID;
    o.bid = bid;
    o.level = level;
    o.price = price;
    o.qty = qty;
    o.issued = t;
    NewOrder n = NewOrder();
    n.actorID = actorID;
    n.ownerID = m_actors[actorID - 1].ownerCorpID;
    n.bid = bid;
    n.level = level;
    n.price = price;
    n.qty = qty;
    n.minVolume = 1;
    n.issued = t;
    if (c != nullptr) {
        const NodeData& node(*Node(c->nodeID));
        const TypeData& type(*m_static.Type(c->typeID));
        o.cell = c->index;
        o.vendorSlot = 0;
        o.typeID = c->typeID;
        o.stationID = node.stationID;
        o.regionID = node.regionID;
        // reserve what is behind the order:  goods for an ask, escrow for a bid, then the broker fee
        Cents fee(BrokerFee(Econ::MulCents(price, qty)));
        if (bid) {
            Cents value(Econ::MulCents(price, qty));
            if ((value < 0) or !m_ledger.Reserve(actorID, value))
                return;
            if (m_ledger.Sink(actorID, fee, Isk::BrokerFee, true) != fee) {
                m_ledger.Unreserve(actorID, value);
                return;
            }
        } else {
            if (!m_ledger.ReserveGoods(actorID, (uint16_t)c->nodeID, c->typeID, qty))
                return;
            if (m_ledger.Sink(actorID, fee, Isk::BrokerFee, true) != fee) {
                m_ledger.UnreserveGoods(actorID, (uint16_t)c->nodeID, c->typeID, qty);
                return;
            }
        }
        n.cell = c->index;
        c->flags |= CellFlag::Pending;
        n.typeID = c->typeID;
        n.nodeID = c->nodeID;
        n.stationID = node.stationID;
        n.systemID = node.systemID;
        n.regionID = node.regionID;
        // station range for asks and hub bids (PLAN 7.1).  bulk materials ask for a lot on the deeper bid levels
        n.range = -1;
        if (bid and (level > 0) and ((type.categoryID == Category::Material) or (type.categoryID == Category::Asteroid)
            or (type.categoryID == Category::PIResource) or (type.categoryID == Category::PICommodity)))
            n.minVolume = std::min<uint32_t>(c->lot, qty);
    } else {
        const VendorSlot& slot(m_vendorSlots[vendorSlot]);
        o.cell = VendorCell;
        o.vendorSlot = vendorSlot;
        o.typeID = slot.offer.typeID;
        o.stationID = slot.offer.stationID;
        o.regionID = slot.offer.regionID;
        n.cell = VendorCell;
        n.vendorSlot = vendorSlot;
        n.typeID = slot.offer.typeID;
        n.nodeID = 0;
        n.stationID = slot.offer.stationID;
        n.systemID = slot.offer.systemID;
        n.regionID = slot.offer.regionID;
        n.range = -1;
    }
    o.dbPrice = price;
    o.dbQty = qty;
    o.dbIssued = t;
    m_batch.posts.push_back(n);
    m_pendingVendor.push_back(std::make_pair((uint32_t)m_batch.posts.size() - 1, vendorSlot));
    (void)m_pendingVendor;
}

bool World::ChangeOrder(SimOrder& o, Cents price, uint32_t qty, int64_t t)
{
    if (o.cell == VendorCell) {
        o.qty = qty;
        o.issued = t;
        return true;
    }
    Cell& c(m_cells[o.cell]);
    Cents fee(0);
    if (price != o.price)
        fee += BrokerFee(Econ::MulCents(std::llabs(price - o.price), qty));
    if (qty > o.qty)
        fee += BrokerFee(Econ::MulCents(price, (Units)(qty - o.qty)));
    if (o.bid) {
        Cents before(o.price * (Cents)o.qty), after(Econ::MulCents(price, qty));
        if (after < 0)
            return false;
        if (after > before) {
            if (!m_ledger.Reserve(o.actorID, after - before)) {
                // what the money allows
                const Ledger::Account* a(m_ledger.FindAcct(o.actorID));
                Cents can(before + ((a == nullptr) ? 0 : a->wallet));
                qty = (uint32_t)std::min<Cents>(qty, can / price);
                after = price * (Cents)qty;
                if ((after > before) and !m_ledger.Reserve(o.actorID, after - before))
                    return false;
            }
        }
        if (after < before)
            m_ledger.Unreserve(o.actorID, before - after);
        if (m_ledger.Sink(o.actorID, fee, Isk::BrokerFee, true) != fee) {
            // can't pay the fee:  back to how it was
            if (after > before)
                m_ledger.Unreserve(o.actorID, after - before);
            else if (after < before)
                m_ledger.Reserve(o.actorID, before - after);
            return false;
        }
    } else {
        if (qty > o.qty) {
            const Ledger::Stock* s(m_ledger.FindInv(o.actorID, (uint16_t)c.nodeID, c.typeID));
            Units freeQty((s == nullptr) ? 0 : s->qty);
            qty = (uint32_t)std::min<Units>(qty, o.qty + freeQty);
            if (qty > o.qty)
                m_ledger.ReserveGoods(o.actorID, (uint16_t)c.nodeID, c.typeID, qty - o.qty);
        } else if (qty < o.qty) {
            m_ledger.UnreserveGoods(o.actorID, (uint16_t)c.nodeID, c.typeID, o.qty - qty);
        }
        if (m_ledger.Sink(o.actorID, fee, Isk::BrokerFee, true) != fee) {
            if (qty > o.qty)
                m_ledger.UnreserveGoods(o.actorID, (uint16_t)c.nodeID, c.typeID, qty - o.qty);
            else if (qty < o.qty)
                m_ledger.ReserveGoods(o.actorID, (uint16_t)c.nodeID, c.typeID, o.qty - qty);
            return false;
        }
    }
    if (price != o.price)
        o.issued = t;
    o.price = price;
    o.qty = qty;
    return true;
}

void World::QueueAlter(SimOrder& o)
{
    if ((o.qty == o.dbQty) and (o.price == o.dbPrice) and (o.issued == o.dbIssued))
        return;
    m_batch.alters.push_back(AlterOrder{o.orderID, o.typeID, o.regionID, o.price, o.qty, o.issued});
    o.dbPrice = o.price;
    o.dbQty = o.qty;
    o.dbIssued = o.issued;
}

void World::CancelOrderNow(uint32_t orderID)
{
    std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(orderID);
    if (itr == m_orders.end())
        return;
    SimOrder& o(itr->second);
    if (o.cell != VendorCell) {
        Cell& c(m_cells[o.cell]);
        if (o.bid)
            m_ledger.Unreserve(o.actorID, o.price * (Cents)o.qty);
        else
            m_ledger.UnreserveGoods(o.actorID, (uint16_t)c.nodeID, c.typeID, o.qty);
        uint32_t* slots(o.bid ? c.bid : c.ask);
        for (int i = 0; i < MaxLevels; ++i)
            if (slots[i] == orderID)
                slots[i] = 0;
    } else if (o.vendorSlot < m_vendorSlots.size()) {
        m_vendorSlots[o.vendorSlot].orderID = 0;
    }
    m_batch.cancels.push_back(CancelOrder{orderID, o.typeID, o.regionID});
    m_orders.erase(itr);
}

void World::FlushOrders()
{
    // orders whose last write failed go again, as they are now (players may have traded with them since)
    if (!m_rewrite.empty()) {
        std::vector<uint32_t> rewrite;
        std::swap(rewrite, m_rewrite);
        for (auto id : rewrite) {
            std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(id);
            if (itr == m_orders.end())
                continue;
            itr->second.dbIssued = INT64_MIN;
            QueueAlter(itr->second);
        }
    }
    if (m_batch.Empty())
        return;
    // anything queued while the batch is settled (a level's second order) goes with the next flush
    OrderBatch batch;
    std::swap(batch, m_batch);
    m_pendingVendor.clear();
    std::vector<uint32_t> ids;
    bool ok(m_gw.ApplyOrders(batch, ids));
    if (!ok) {
        /* nothing was written (the transaction rolled back), but the orders in memory have their new price and quantity
         *  and the cancelled ones are gone:  the market must not keep the old rows.  again with the next flush */
        for (auto& a : batch.alters)
            m_rewrite.push_back(a.orderID);
        for (auto& c : batch.cancels)
            m_batch.cancels.push_back(c);
    }
    for (size_t i = 0; i < batch.posts.size(); ++i) {
        const NewOrder& n(batch.posts[i]);
        uint32_t id((ok and (i < ids.size())) ? ids[i] : 0);
        if (id == 0) {
            // not written:  free what was reserved for it (the fee is gone)
            if (n.cell != VendorCell) {
                if (n.bid)
                    m_ledger.Unreserve(n.actorID, n.price * (Cents)n.qty);
                else
                    m_ledger.UnreserveGoods(n.actorID, (uint16_t)n.nodeID, n.typeID, n.qty);
            }
            continue;
        }
        SimOrder o = SimOrder();
        o.orderID = id;
        o.actorID = n.actorID;
        o.cell = n.cell;
        o.vendorSlot = n.vendorSlot;
        o.typeID = n.typeID;
        o.stationID = n.stationID;
        o.regionID = n.regionID;
        o.bid = n.bid;
        o.level = n.level;
        o.price = o.dbPrice = n.price;
        o.qty = o.dbQty = n.qty;
        o.issued = o.dbIssued = n.issued;
        m_orders[id] = o;
        if (n.cell != VendorCell) {
            Cell& c(m_cells[n.cell]);
            c.flags &= ~CellFlag::Pending;
            uint32_t& slot(n.bid ? c.bid[n.level] : c.ask[n.level]);
            if ((slot != 0) and (slot != id))
                CancelOrderNow(slot);       // a level holds one order (only after a failed flush)
            slot = id;
        } else if (n.vendorSlot < m_vendorSlots.size()) {
            m_vendorSlots[n.vendorSlot].orderID = id;
        }
    }
    for (auto& n : batch.posts)
        if (n.cell != VendorCell)
            m_cells[n.cell].flags &= ~CellFlag::Pending;
}

/************************************************************************************************************************
 * the hooks:  players trading with sim orders (PLAN 7.2)
 */

void World::OnAskFilled(uint32_t orderID, uint32_t qty, uint32_t buyerOwnerID, int64_t now)
{
    std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(orderID);
    if ((itr == m_orders.end()) or (qty == 0))
        return;
    SimOrder& o(itr->second);
    if (qty > o.qty)
        qty = o.qty;
    Cents value(Econ::MulCents(o.price, qty));
    ++m_playerTrades;
    if (o.cell == VendorCell) {
        // npc vendor:  items appear (a goods faucet), the isk is sunk
        m_ledger.GoodsIn(o.actorID, 0, o.typeID, qty, Goods::Vendor);
        m_ledger.GoodsOut(o.actorID, 0, o.typeID, qty, Goods::SimToPlayer, false);
        m_ledger.FromPlayer(o.actorID, value);
        m_ledger.Sink(o.actorID, value, Isk::VendorSales, true);
    } else {
        Cell& c(m_cells[o.cell]);
        Cents tax(m_p.simPaysFees ? (Cents)std::llround((double)value * m_p.salesTaxRate) : 0);
        m_ledger.GoodsOut(o.actorID, (uint16_t)c.nodeID, c.typeID, qty, Goods::SimToPlayer, true);
        m_ledger.FromPlayer(o.actorID, value);
        tax = m_ledger.Sink(o.actorID, tax, Isk::SalesTax, false);
        if (m_persist != nullptr)
            m_persist->PlayerFill(o.actorID, value - tax, 0, c.nodeID, c.typeID, 0, -(Units)qty);
        PlayerFeedback(c, buyerOwnerID, o.price, qty);
        MarkDirty(o.regionID, o.typeID, now);
        Changed(c);
    }
    o.qty -= qty;
    o.dbQty = o.qty;
    if (o.qty == 0) {
        // MarketMgr removed the row
        if (o.cell != VendorCell) {
            Cell& c(m_cells[o.cell]);
            for (int i = 0; i < MaxLevels; ++i)
                if (c.ask[i] == orderID)
                    c.ask[i] = 0;
        } else if (o.vendorSlot < m_vendorSlots.size()) {
            m_vendorSlots[o.vendorSlot].orderID = 0;
        }
        m_orders.erase(itr);
        if (m_persist != nullptr)
            m_persist->OrderGone(orderID);
    }
}

void World::OnBidFilled(uint32_t orderID, uint32_t qty, uint32_t sellerOwnerID, int64_t now)
{
    std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(orderID);
    if ((itr == m_orders.end()) or (qty == 0) or (itr->second.cell == VendorCell))
        return;
    SimOrder& o(itr->second);
    if (qty > o.qty)
        qty = o.qty;
    Cell& c(m_cells[o.cell]);
    Cents value(Econ::MulCents(o.price, qty));
    ++m_playerTrades;
    m_ledger.ToPlayer(o.actorID, value, true);
    m_ledger.GoodsIn(o.actorID, (uint16_t)c.nodeID, c.typeID, qty, Goods::PlayerToSim);
    RecordIntake(c, sellerOwnerID, qty, value, now);
    if (m_persist != nullptr)
        m_persist->PlayerFill(o.actorID, 0, -value, c.nodeID, c.typeID, qty, 0);
    PlayerFeedback(c, sellerOwnerID, o.price, qty);
    MarkDirty(o.regionID, o.typeID, now);
    Changed(c);
    o.qty -= qty;
    o.dbQty = o.qty;
    if (o.qty == 0) {
        for (int i = 0; i < MaxLevels; ++i)
            if (c.bid[i] == orderID)
                c.bid[i] = 0;
        m_orders.erase(itr);
        if (m_persist != nullptr)
            m_persist->OrderGone(orderID);
    }
}

Units World::IntakeLeft(const Cell& c, uint32_t ownerID, Cents price, int64_t t) const
{
    // PLAN 7.6.1:  per counterparty per type per day (a share of D), per counterparty isk per day (a share of M*), per
    //  cell per day (IntakeDays x D)
    int32_t day(DayOf(t));
    double d(std::max(c.dBar, (double)c.lot));
    std::map<std::tuple<int32_t, uint32_t, uint16_t>, Units>::const_iterator u = m_intakeUnits.find(std::make_tuple(day, ownerID, c.typeID));
    // at least a lot a day (thin types would otherwise never take anything from a player)
    double perType(std::max(m_p.intakeShareCounterparty * d, (double)c.lot) - ((u == m_intakeUnits.end()) ? 0 : u->second));
    double perCell(std::max(m_p.intakeDays * d, (double)c.lot) - ((c.day == day) ? (double)c.dIntake : 0.0));
    std::map<std::pair<int32_t, uint32_t>, Cents>::const_iterator i = m_intakeIsk.find(std::make_pair(day, ownerID));
    double isk(m_p.maxIskToCounterpartyPerDay * m_moneyTarget - ((i == m_intakeIsk.end()) ? 0 : (double)i->second));
    double byIsk((price > 0) ? isk / (double)price : 0.0);
    double left(std::min(perType, std::min(perCell, byIsk)));
    return (left <= 0.0) ? 0 : (Units)std::floor(left);
}

void World::RecordIntake(Cell& c, uint32_t ownerID, Units qty, Cents value, int64_t t)
{
    int32_t day(DayOf(t));
    m_intakeUnits[std::make_tuple(day, ownerID, c.typeID)] += qty;
    m_intakeIsk[std::make_pair(day, ownerID)] += value;
    if (c.day == day)
        c.dIntake += qty;
    if (m_persist != nullptr)
        m_persist->IntakeAdd(day, ownerID, c.typeID, qty, value);
}

void World::LoadIntake(int32_t day, uint32_t ownerID, uint16_t typeID, Units qty, Cents isk)
{
    m_intakeUnits[std::make_tuple(day, ownerID, typeID)] += qty;
    m_intakeIsk[std::make_pair(day, ownerID)] += isk;
}

uint32_t World::AllowIntake(uint32_t orderID, uint32_t sellerOwnerID, uint32_t qty, int64_t now)
{
    std::unordered_map<uint32_t, SimOrder>::const_iterator itr = m_orders.find(orderID);
    if ((itr == m_orders.end()) or !itr->second.bid or (itr->second.cell == VendorCell))
        return 0;
    Cell& c(m_cells[itr->second.cell]);
    RollDay(c, now);
    Units left(IntakeLeft(c, sellerOwnerID, itr->second.price, now));
    return (uint32_t)std::min<Units>(qty, std::min<Units>(left, itr->second.qty));
}

/************************************************************************************************************************
 * daily and hourly work
 */

void World::Daily(int64_t t)
{
    int32_t day(DayOf(t));
    // the books of the day that ended (PLAN 6.7):  every flow named, residual exactly 0
    Ledger::Result r(m_ledger.Audit());
    m_ledger.BeginPeriod(day);
    m_auditDay = day;

    uint32_t clamped(0);
    for (auto& c : m_cells) {
        if ((c.flags & CellFlag::Clamped) and !c.shocked) {
            ++c.clampDays;
            ++clamped;
        } else {
            c.clampDays = 0;
        }
    }

    // the money controller
    Cents money(m_ledger.Money());
    if ((money > 0) and (m_moneyTarget > 0.0)) {
        m_integral += std::log(m_moneyTarget / (double)money);
        m_integral = std::min(20.0, std::max(-20.0, m_integral));
    }
    m_incomeFactor = Econ::ControllerFactor((double)money, m_moneyTarget, m_integral, m_p.controllerKp, m_p.controllerKi);

    // the price index:  Laspeyres over the top-500 isk basket, against the reference prices
    std::vector<std::pair<double, uint32_t>> basket;
    for (auto& c : m_cells)
        basket.push_back(std::make_pair(c.d0 * c.refLocal, c.index));
    std::sort(basket.begin(), basket.end(), [](const std::pair<double, uint32_t>& a, const std::pair<double, uint32_t>& b) {
        return (a.first != b.first) ? (a.first > b.first) : (a.second < b.second);
    });
    double num(0.0), den(0.0);
    for (size_t i = 0; (i < basket.size()) and (i < 500); ++i) {
        const Cell& c(m_cells[basket[i].second]);
        num += Value(c) * c.d0;
        den += c.refLocal * c.d0;
    }
    double index((den > 0.0) ? num / den : 1.0);

    m_auditLines.clear();
    for (uint8_t i = 0; i < Isk::Count; ++i)
        m_auditLines.push_back(AuditLine{r.day, Isk::Name(i), r.isk[i]});
    for (uint8_t i = 0; i < Goods::Count; ++i)
        m_auditLines.push_back(AuditLine{r.day, Goods::Name(i), r.goods[i]});
    m_auditLines.push_back(AuditLine{r.day, "money.start", r.moneyStart});
    m_auditLines.push_back(AuditLine{r.day, "money.end", r.moneyEnd});
    m_auditLines.push_back(AuditLine{r.day, "money.target", (int64_t)m_moneyTarget});
    m_auditLines.push_back(AuditLine{r.day, "residual.isk", r.iskResidual});
    m_auditLines.push_back(AuditLine{r.day, "residual.goodsTypes", (int64_t)r.goodsResidualTypes});
    m_auditLines.push_back(AuditLine{r.day, "residual.goodsUnits", r.goodsResidualUnits});
    m_auditLines.push_back(AuditLine{r.day, "priceIndex.ppm", (int64_t)std::llround(index * 1e6)});
    m_auditLines.push_back(AuditLine{r.day, "income.factor.ppm", (int64_t)std::llround(m_incomeFactor * 1e6)});
    m_auditLines.push_back(AuditLine{r.day, "cells.clamped", (int64_t)clamped});
    m_auditLines.push_back(AuditLine{r.day, "orders.live", (int64_t)m_orders.size()});
    m_auditLines.push_back(AuditLine{r.day, "ledger.violations", (int64_t)m_ledger.Violations()});

    // overhead, then profits beyond what an actor needs go back to its region's consumers (a transfer), and a market
    //  maker short of money is topped up from them
    for (auto& a : m_actors) {
        if ((a.kind != ActorKind::MarketMaker) and (a.kind != ActorKind::Producer))
            continue;
        const Ledger::Account* acct(m_ledger.FindAcct(a.actorID));
        if (acct == nullptr)
            continue;
        Cents upkeep((Cents)std::llround((double)(acct->wallet + acct->reserved) * m_p.upkeepPerDay));
        m_ledger.Sink(a.actorID, upkeep, Isk::Upkeep, false);
        // the consumer of the actor's region
        uint32_t consumer(0);
        for (auto& b : m_actors)
            if ((b.kind == ActorKind::Consumer) and (b.regionID == a.regionID)) {
                consumer = b.actorID;
                break;
            }
        if (consumer == 0)
            continue;
        Cents money2(m_ledger.MoneyOf(a.actorID));
        Cents keep((Cents)std::llround(a.target));
        Cents wallet(m_ledger.FindAcct(a.actorID)->wallet);
        if (money2 > keep) {
            // a fifth of the target stays free for new bids and fees
            Cents excess(std::min(wallet - keep / 5, money2 - keep));
            if (excess > 0)
                m_ledger.Transfer(a.actorID, consumer, excess);
        } else if ((a.kind == ActorKind::MarketMaker) and (money2 < keep / 4)) {
            Cents cwallet(m_ledger.FindAcct(consumer) ? m_ledger.FindAcct(consumer)->wallet : 0);
            m_ledger.Transfer(consumer, a.actorID, std::min(cwallet / 4, keep / 2 - money2));
        }
    }

    // intake counters older than a week go
    for (std::map<std::tuple<int32_t, uint32_t, uint16_t>, Units>::iterator itr = m_intakeUnits.begin(); itr != m_intakeUnits.end(); ) {
        if (std::get<0>(itr->first) < day - 7)
            itr = m_intakeUnits.erase(itr);
        else
            ++itr;
    }
    for (std::map<std::pair<int32_t, uint32_t>, Cents>::iterator itr = m_intakeIsk.begin(); itr != m_intakeIsk.end(); ) {
        if (itr->first.first < day - 7)
            itr = m_intakeIsk.erase(itr);
        else
            ++itr;
    }

    if (m_onDay) {
        DayReport rep = DayReport();
        rep.day = r.day;
        rep.audit = r;
        rep.money = money;
        rep.moneyTarget = m_moneyTarget;
        rep.incomeFactor = m_incomeFactor;
        rep.incomeToday = (Cents)std::llround((double)m_income0 * m_incomeFactor);
        rep.liveOrders = (uint32_t)m_orders.size();
        rep.clampedCells = clamped;
        m_onDay(rep);
    }
    if (m_p.pauseOnAuditResidual and ((r.iskResidual != 0) or (r.goodsResidualTypes != 0)))
        m_paused = true;
}

void World::Hourly(int64_t t)
{
    // consumer income, for the hours since the last payment (at most MaxCatchUpHours after a downtime)
    int64_t from(std::max(m_lastIncome, t - 3600LL * m_p.maxCatchUpHours));
    double hours((double)(t - from) / 3600.0);
    m_lastIncome = t;
    if (hours > 0.0) {
        double amount((double)m_income0 * m_incomeFactor * hours / 24.0);
        for (auto& a : m_actors)
            if (a.kind == ActorKind::Consumer)
                m_ledger.Faucet(a.actorID, (Cents)std::llround(amount * a.share), Isk::Income);
    }
    // npc vendors restock (PLAN 6.4)
    for (uint32_t i = 0; i < m_vendorSlots.size(); ++i) {
        VendorSlot& slot(m_vendorSlots[i]);
        if (slot.orderID == 0) {
            PostOrder(nullptr, i, slot.actorID, false, 0, slot.price, m_p.vendorStock, t);
            continue;
        }
        std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(slot.orderID);
        if (itr == m_orders.end()) {
            slot.orderID = 0;
            continue;
        }
        if ((itr->second.qty < m_p.vendorStock) or (itr->second.price != slot.price)
        or ((t - itr->second.issued) > StaleOrderSeconds)) {
            itr->second.qty = m_p.vendorStock;
            itr->second.price = slot.price;
            itr->second.issued = t;
            QueueAlter(itr->second);
        }
    }
}

void World::PostMissingVendorOrders(int64_t now)
{
    for (uint32_t i = 0; i < m_vendorSlots.size(); ++i)
        if (m_vendorSlots[i].orderID == 0)
            PostOrder(nullptr, i, m_vendorSlots[i].actorID, false, 0, m_vendorSlots[i].price, m_p.vendorStock, now);
}

/************************************************************************************************************************
 * loading
 */

void World::LoadOrder(const SimOrder& order)
{
    SimOrder o(order);
    o.dbPrice = o.price;
    o.dbQty = o.qty;
    o.dbIssued = o.issued;
    if (o.cell != VendorCell) {
        if (o.cell >= m_cells.size())
            return;
        Cell& c(m_cells[o.cell]);
        uint32_t* slots(o.bid ? c.bid : c.ask);
        if ((o.level >= MaxLevels) or (slots[o.level] != 0)) {
            // a second order on the level:  keep it known so it can be cancelled
            m_orders[o.orderID] = o;
            CancelOrderNow(o.orderID);
            return;
        }
        slots[o.level] = o.orderID;
    } else {
        if ((o.vendorSlot >= m_vendorSlots.size()) or (m_vendorSlots[o.vendorSlot].orderID != 0)) {
            m_orders[o.orderID] = o;
            CancelOrderNow(o.orderID);
            return;
        }
        m_vendorSlots[o.vendorSlot].orderID = o.orderID;
    }
    m_orders[o.orderID] = o;
}

std::string World::Reconcile(int64_t now)
{
    // what the live orders hold
    std::map<uint32_t, Cents> needIsk;
    std::map<Ledger::StockKey, Units> needQty;
    for (auto& cur : m_orders) {
        const SimOrder& o(cur.second);
        if (o.cell == VendorCell)
            continue;
        const Cell& c(m_cells[o.cell]);
        if (o.bid)
            needIsk[o.actorID] += o.price * (Cents)o.qty;
        else
            needQty[Ledger::Key(o.actorID, (uint16_t)c.nodeID, c.typeID)] += o.qty;
    }
    uint32_t cut(0);
    Cents moved(0);
    Units movedQty(0);
    // money:  the reserved part becomes exactly what the bids hold.  bids beyond the actor's money are cut, deepest first
    for (auto& cur : m_ledger.Accounts()) {
        uint32_t actorID(cur.first);
        Cents total(cur.second.wallet + cur.second.reserved);
        Cents need(needIsk[actorID]);
        if (need > total) {
            std::vector<SimOrder*> bids;
            for (auto& o : m_orders)
                if (o.second.bid and (o.second.actorID == actorID) and (o.second.cell != VendorCell))
                    bids.push_back(&o.second);
            std::sort(bids.begin(), bids.end(), [](SimOrder* a, SimOrder* b) {
                return (a->level != b->level) ? (a->level > b->level) : (a->orderID > b->orderID);
            });
            for (auto o : bids) {
                if (need <= total)
                    break;
                Cents over(need - total);
                uint32_t drop((uint32_t)std::min<Cents>(o->qty, (over + o->price - 1) / o->price));
                need -= o->price * (Cents)drop;
                o->qty -= drop;
                ++cut;
            }
        }
        moved += std::llabs(need - cur.second.reserved);
        const_cast<Ledger&>(m_ledger).Load(actorID, total - need, need);
    }
    for (auto& cur : m_ledger.Stocks()) {
        Units total(cur.second.qty + cur.second.reserved);
        Units need(needQty[cur.first]);
        if (need > total) {
            for (auto& o : m_orders) {
                if (need <= total)
                    break;
                SimOrder& so(o.second);
                if (so.bid or (so.cell == VendorCell) or (so.actorID != Ledger::KeyActor(cur.first)))
                    continue;
                const Cell& c(m_cells[so.cell]);
                if ((c.nodeID != Ledger::KeyNode(cur.first)) or (c.typeID != Ledger::KeyType(cur.first)))
                    continue;
                uint32_t drop((uint32_t)std::min<Units>(so.qty, need - total));
                so.qty -= drop;
                need -= drop;
                ++cut;
            }
        }
        movedQty += std::llabs(need - cur.second.reserved);
        m_ledger.LoadStock(cur.first, total - need, need);
    }
    // orders with nothing behind them in the ledger at all (an actor or stock row that was never saved)
    for (auto& cur : m_orders) {
        SimOrder& o(cur.second);
        if (o.cell == VendorCell)
            continue;
        const Cell& c(m_cells[o.cell]);
        if (o.bid ? (m_ledger.FindAcct(o.actorID) == nullptr) : (m_ledger.FindInv(o.actorID, (uint16_t)c.nodeID, c.typeID) == nullptr)) {
            o.qty = 0;
            ++cut;
        }
    }
    // the cut orders go out with the next flush
    std::vector<uint32_t> gone;
    for (auto& cur : m_orders) {
        SimOrder& o(cur.second);
        if (o.qty == 0)
            gone.push_back(o.orderID);
        else
            QueueAlter(o);
    }
    for (auto id : gone) {
        // nothing is reserved for an empty order any more
        std::unordered_map<uint32_t, SimOrder>::iterator itr = m_orders.find(id);
        if (itr == m_orders.end())
            continue;
        SimOrder& o(itr->second);
        if (o.cell != VendorCell) {
            Cell& c(m_cells[o.cell]);
            uint32_t* slots(o.bid ? c.bid : c.ask);
            for (int i = 0; i < MaxLevels; ++i)
                if (slots[i] == id)
                    slots[i] = 0;
        } else if (o.vendorSlot < m_vendorSlots.size()) {
            m_vendorSlots[o.vendorSlot].orderID = 0;
        }
        m_batch.cancels.push_back(CancelOrder{id, o.typeID, o.regionID});
        m_orders.erase(itr);
    }
    (void)now;
    return Fmt("reservations rebuilt from %u live orders (%.2f ISK and %lld units moved between free and reserved), %u orders cut",
               (uint32_t)m_orders.size(), Econ::ToIsk(moved), (long long)movedQty, cut);
}

/************************************************************************************************************************
 * GM
 */

std::string World::Status(int64_t now) const
{
    uint32_t hot(0), clamped(0), disabled(0);
    for (auto& c : m_cells) {
        if (c.flags & CellFlag::Hot)
            ++hot;
        if (c.flags & CellFlag::Clamped)
            ++clamped;
        if (c.flags & CellFlag::Disabled)
            ++disabled;
    }
    Cents money(m_ledger.Money());
    std::ostringstream out;
    out << Fmt("Market sim %s.  %u nodes, %u cells (%u hot, %u clamped, %u disabled), %u actors, %u live orders (%u vendor slots).\n",
               m_paused ? "PAUSED" : "running", (uint32_t)m_static.nodes.size(), (uint32_t)m_cells.size(), hot, clamped, disabled,
               (uint32_t)m_actors.size(), (uint32_t)m_orders.size(), (uint32_t)m_vendorSlots.size());
    out << Fmt("Money M = %.0f ISK, M* = %.0f ISK (M/M* %.3f).  Income %.0f ISK/day (factor %.3f).\n",
               Econ::ToIsk(money), m_moneyTarget / 100.0, (m_moneyTarget > 0) ? money / m_moneyTarget : 0.0,
               (double)m_income0 * m_incomeFactor / 100.0, m_incomeFactor);
    out << Fmt("Cell visits %llu, trades with players %llu, backlog %llus, ledger violations %llu.\n",
               (unsigned long long)m_cellVisits, (unsigned long long)m_playerTrades, (unsigned long long)Backlog(now),
               (unsigned long long)m_ledger.Violations());
    for (auto& line : m_auditLines)
        if ((line.category.compare(0, 9, "residual.") == 0) or (line.category == "priceIndex.ppm"))
            out << Fmt("  day %d %s = %lld\n", line.day, line.category.c_str(), (long long)line.amount);
    return out.str();
}

std::string World::CellInfo(uint16_t typeID, uint32_t stationID) const
{
    std::ostringstream out;
    for (auto& c : m_cells) {
        if (c.typeID != typeID)
            continue;
        const NodeData& node(*Node(c.nodeID));
        if ((stationID != 0) and (node.stationID != stationID))
            continue;
        const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
        out << Fmt("type %u at %u:  V %.2f, anchor %.2f, ref %.2f, stock %lld (+%lld on asks) of %.0f, demand %.2f/day (cal %.2f), h %.3f, levels %u%s%s%s\n",
                   c.typeID, node.stationID, Value(c), std::exp(c.lnAnchor), c.refLocal,
                   (long long)(s ? s->qty : 0), (long long)(s ? s->reserved : 0), c.targetStock, c.dBar, c.d0, c.h, c.levels,
                   (c.flags & CellFlag::Hot) ? ", hot" : "", (c.flags & CellFlag::Clamped) ? ", CLAMPED" : "",
                   c.shocked ? ", shocked" : "");
        for (int i = 0; i < c.levels; ++i) {
            const SimOrder* a(c.ask[i] ? FindOrder(c.ask[i]) : nullptr);
            const SimOrder* b(c.bid[i] ? FindOrder(c.bid[i]) : nullptr);
            out << Fmt("  L%d  ask %s  bid %s\n", i,
                       a ? Fmt("#%u %u @ %.2f", a->orderID, a->qty, Econ::ToIsk(a->price)).c_str() : "-",
                       b ? Fmt("#%u %u @ %.2f", b->orderID, b->qty, Econ::ToIsk(b->price)).c_str() : "-");
        }
        out << Fmt("  today:  %lld traded, low %.2f high %.2f, last %.2f.  bought from players today %lld\n",
                   (long long)c.dVol, Econ::ToIsk(c.dLow), Econ::ToIsk(c.dHigh), c.lastClear, (long long)c.dIntake);
    }
    std::string s(out.str());
    return s.empty() ? Fmt("type %u has no cell%s.", typeID, stationID ? " at that station" : "") : s;
}

std::string World::ActorInfo(uint32_t actorID) const
{
    if ((actorID == 0) or (actorID > m_actors.size()))
        return Fmt("no actor %u.", actorID);
    const Actor& a(m_actors[actorID - 1]);
    const Ledger::Account* acct(m_ledger.FindAcct(actorID));
    static const char* kinds[] = {"?", "market maker", "producer", "consumer", "vendor"};
    std::ostringstream out;
    out << Fmt("actor %u, %s (sector/class %u), corp %u, region %u, node %u.  wallet %.2f, reserved %.2f, target %.2f\n",
               a.actorID, kinds[a.kind <= 4 ? a.kind : 0], a.sector, a.ownerCorpID, a.regionID, a.nodeID,
               acct ? Econ::ToIsk(acct->wallet) : 0.0, acct ? Econ::ToIsk(acct->reserved) : 0.0, a.target / 100.0);
    std::vector<std::pair<double, Ledger::StockKey>> top;
    for (auto& cur : m_ledger.Stocks()) {
        if (Ledger::KeyActor(cur.first) != actorID)
            continue;
        const TypeData* t(m_static.Type(Ledger::KeyType(cur.first)));
        double value((cur.second.qty + cur.second.reserved) * ((t != nullptr) ? t->refPrice : 0.0));
        top.push_back(std::make_pair(value, cur.first));
    }
    std::sort(top.begin(), top.end(), [](const std::pair<double, Ledger::StockKey>& x, const std::pair<double, Ledger::StockKey>& y) {
        return (x.first != y.first) ? (x.first > y.first) : (x.second < y.second);
    });
    for (size_t i = 0; (i < top.size()) and (i < 10); ++i) {
        const Ledger::Stock& s(m_ledger.Stocks().at(top[i].second));
        out << Fmt("  type %u at node %u:  %lld free, %lld on asks (%.0f ISK)\n", Ledger::KeyType(top[i].second),
                   Ledger::KeyNode(top[i].second), (long long)s.qty, (long long)s.reserved, top[i].first);
    }
    return out.str();
}

std::string World::ActorList(int kind) const
{
    std::ostringstream out;
    for (auto& a : m_actors) {
        if ((kind > 0) and (a.kind != kind))
            continue;
        const Ledger::Account* acct(m_ledger.FindAcct(a.actorID));
        out << Fmt("%u kind %u sector %u corp %u node %u:  %.0f ISK (%.0f reserved)\n", a.actorID, a.kind, a.sector,
                   a.ownerCorpID, a.nodeID, acct ? Econ::ToIsk(acct->wallet) : 0.0, acct ? Econ::ToIsk(acct->reserved) : 0.0);
    }
    return out.str();
}

uint32_t World::SetAnchor(uint16_t typeID, double price, uint32_t regionID, int64_t now)
{
    uint32_t count(0);
    for (auto& c : m_cells) {
        if ((c.typeID != typeID) or ((regionID != 0) and (Node(c.nodeID)->regionID != regionID)))
            continue;
        c.anchorOverride = (price > 0.0) ? price : 0.0;
        c.flags |= CellFlag::Dirty;
        if (m_started)
            Schedule(c, now);
        ++count;
    }
    return count;
}

uint32_t World::Shock(char kind, uint32_t id, double mult, double days, int64_t now)
{
    uint32_t count(0);
    for (auto& c : m_cells) {
        const TypeData& t(*m_static.Type(c.typeID));
        bool match((kind == 'g') ? (t.groupID == id) : ((kind == 'c') ? (t.categoryID == id) : (t.typeID == id)));
        if (!match)
            continue;
        c.shockMult = mult;
        c.shockUntil = now + (int64_t)(days * SecondsPerDay);
        ++count;
    }
    return count;
}

uint32_t World::Requote(uint16_t typeID, int64_t now)
{
    uint32_t count(0);
    for (auto& c : m_cells) {
        if (((typeID != 0) and (c.typeID != typeID)) or (c.flags & CellFlag::Disabled))
            continue;
        c.flags |= CellFlag::Dirty;
        if (m_started)
            Schedule(c, now);
        ++count;
    }
    return count;
}

uint32_t World::PurgeOrders()
{
    std::vector<uint32_t> ids;
    for (auto& cur : m_orders)
        ids.push_back(cur.first);
    std::sort(ids.begin(), ids.end());
    for (auto id : ids)
        CancelOrderNow(id);
    m_paused = true;
    return (uint32_t)ids.size();
}

bool World::ExportCsv(const std::string& path) const
{
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc);
    if (!f)
        return false;
    f << "nodeID,stationID,typeID,V,anchor,refLocal,d0,dBar,targetStock,stock,reserved,h,levels,flags,clampDays,vStar,simClass,sigma\n";
    for (auto& c : m_cells) {
        const Ledger::Stock* s(m_ledger.FindInv(c.mm, (uint16_t)c.nodeID, c.typeID));
        f << Fmt("%u,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.1f,%lld,%lld,%.4f,%u,%u,%u,%.4f,%u,%.4f\n", c.nodeID, Node(c.nodeID)->stationID, c.typeID,
                 Value(c), std::exp(c.lnAnchor), c.refLocal, c.d0, c.dBar, c.targetStock, (long long)(s ? s->qty : 0),
                 (long long)(s ? s->reserved : 0), c.h, c.levels, c.flags, c.clampDays, c.vStar, c.simClass, c.sigma);
    }
    f << "\nday,category,amount\n";
    for (auto& line : m_auditLines)
        f << line.day << "," << line.category << "," << line.amount << "\n";
    return true;
}

/************************************************************************************************************************
 * history
 */

bool World::BackfillSeries(uint32_t regionID, uint16_t typeID, int days, int32_t firstDay, std::vector<HistoryRow>& into)
{
    std::map<std::pair<uint32_t, uint16_t>, std::vector<uint32_t>>::const_iterator itr = m_regionCells.find(std::make_pair(regionID, typeID));
    if ((itr == m_regionCells.end()) or itr->second.empty() or (days <= 0))
        return false;
    const Cell& c(m_cells[itr->second.front()]);
    if ((c.flags & CellFlag::Backfilled) != 0)
        return false;
    const TypeData& t(*m_static.Type(typeID));
    // a log-OU path from the anchor, bent so that it ends at today's V (PLAN 7.5)
    double kappa(Ln2 / std::max(0.1, m_p.fairValueHalfLifeDays));
    double sigma(std::max(0.005, (double)t.volatility));
    double lnA(c.lnAnchor);
    std::vector<double> x(days);
    x[0] = lnA + sigma * Rng::Normal(Rng::Hash64(m_p.seed, Key(c), hBackfill, 0));
    for (int k = 1; k < days; ++k)
        x[k] = Econ::OUStep(x[k - 1], lnA, kappa, 1.0, sigma, Rng::Normal(Rng::Hash64(m_p.seed, Key(c), hBackfill, (uint64_t)k)));
    double corr(c.lnV - x[days - 1]);
    for (int k = 0; k < days; ++k) {
        double lnp(x[k] + corr * (double)(k + 1) / (double)days);
        int32_t day(firstDay - days + k);
        double units(std::max(c.dBar * m_p.tradesPerUnit, c.d0) * m_static.Weekday(t, WeekdayOf(day)));
        uint64_t hv(Rng::Hash64(m_p.seed, Key(c), hBackfillVol, (uint64_t)k));
        Units vol((units < 30.0) ? (Units)Rng::Poisson(units, hv)
                                 : (Units)std::llround(units * std::exp(0.3 * Rng::Normal(hv) - 0.045)));
        if (vol <= 0)
            continue;
        double avg(std::exp(lnp));
        double band(c.h * (0.5 + Rng::Uniform(Rng::Hash64(m_p.seed, Key(c), hBackfillBand, (uint64_t)k))));
        HistoryRow row = HistoryRow();
        row.regionID = regionID;
        row.typeID = typeID;
        row.day = day;
        row.low = Econ::RoundPrice(avg * (1.0 - band), false);
        row.high = Econ::RoundPrice(avg * (1.0 + band), true);
        Cents avgCents(Econ::ToCents(avg));
        row.sumValue = Econ::MulCents(std::max<Cents>(1, avgCents), vol);
        row.volume = vol;
        row.trades = (uint32_t)std::max<Units>(1, vol / std::max<uint32_t>(1, c.lot));
        into.push_back(row);
    }
    return true;
}

void World::MarkBackfilled(uint32_t regionID, uint16_t typeID)
{
    std::map<std::pair<uint32_t, uint16_t>, std::vector<uint32_t>>::const_iterator itr = m_regionCells.find(std::make_pair(regionID, typeID));
    if (itr == m_regionCells.end())
        return;
    for (auto idx : itr->second) {
        m_cells[idx].flags |= CellFlag::Backfilled;
        Changed(m_cells[idx]);
    }
}

void World::TakeFinishedHistory(std::vector<HistoryRow>& into)
{
    into.insert(into.end(), m_historyDone.begin(), m_historyDone.end());
    m_historyDone.clear();
}

void World::TodayHistory(std::vector<HistoryRow>& into) const
{
    for (auto& c : m_cells) {
        if (c.dVol <= 0)
            continue;
        HistoryRow row = HistoryRow();
        row.regionID = Node(c.nodeID)->regionID;
        row.typeID = c.typeID;
        row.day = c.day;
        row.low = c.dLow;
        row.high = c.dHigh;
        row.sumValue = c.dSum;
        row.volume = c.dVol;
        row.trades = c.dTrades;
        into.push_back(row);
    }
}

void World::Changed(const Cell& c)
{
    if (m_cellChanged.size() != m_cells.size())
        m_cellChanged.assign(m_cells.size(), 0);
    if (m_cellChanged[c.index] == 0) {
        m_cellChanged[c.index] = 1;
        m_changedCells.push_back(c.index);
    }
}

void World::TakeChangedCells(std::vector<uint32_t>& into)
{
    into.insert(into.end(), m_changedCells.begin(), m_changedCells.end());
    for (auto idx : m_changedCells)
        if (idx < m_cellChanged.size())
            m_cellChanged[idx] = 0;
    m_changedCells.clear();
}

void World::MarkAllCellsChanged()
{
    for (auto& c : m_cells)
        Changed(c);
}

void World::TakeHistoryTouched(std::vector<std::pair<uint32_t, uint16_t>>& into)
{
    for (auto& cur : m_historyTouched)
        into.push_back(cur.first);
    m_historyTouched.clear();
}

}
