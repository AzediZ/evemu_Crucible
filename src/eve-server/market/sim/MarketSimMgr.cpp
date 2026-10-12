
 /**
  * @name MarketSimMgr.cpp
  *   market simulation:  the server's singleton
  *
  * @date:          13Oct26
  *
  */

#include <cmath>
#include <cstdlib>
#include <sstream>

#include "Client.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "market/MarketMgr.h"
#include "market/sim/MarketSimDB.h"
#include "market/sim/MarketSimMgr.h"
#include "market/sim/SimEconomy.h"
#include "station/StationDataMgr.h"

/*
 * MARKET__SIM          boot, the daily summary, warnings and errors
 * MARKET__SIM_TRACE    decisions (verbose)
 * MARKET__SIM_AUDIT    the daily books
 */

using namespace MktSim;

namespace {
    const char* SchemaVersion = "1";

    std::vector<uint32> ParseList(const std::string& s)
    {
        std::vector<uint32> out;
        std::stringstream in(s);
        std::string item;
        while (std::getline(in, item, ','))
            if (!item.empty() and (std::strtoul(item.c_str(), nullptr, 10) > 0))
                out.push_back((uint32)std::strtoul(item.c_str(), nullptr, 10));
        return out;
    }

    std::string Fmt(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    // categories each market maker sector trades (for choosing the npc corp it trades as)
    bool InSector(uint8 sector, uint8 categoryID)
    {
        switch (sector) {
            case Sector::Raw:   return (categoryID == Category::Material) or (categoryID == Category::Asteroid) or (categoryID == Category::Commodity)
                                    or (categoryID == 41) or (categoryID == Category::PIResource) or (categoryID == Category::PICommodity);
            case Sector::Ships: return (categoryID == Category::Ship);
            case Sector::Goods: return (categoryID == Category::Module) or (categoryID == Category::Charge) or (categoryID == Category::Drone)
                                    or (categoryID == Category::Deployable) or (categoryID == Category::Subsystem);
            case Sector::Loot:  return (categoryID == Category::Implant) or (categoryID == Category::Module);
        }
        return false;
    }
}

MarketSimMgr::MarketSimMgr()
: m_enabled(false),
  m_offset(0),
  m_lastFlush(0),
  m_lastHistoryInvalidate(0),
  m_startDay(0),
  m_replaceInventory(false)
{
}

MarketSimMgr::~MarketSimMgr()
{
}

int64 MarketSimMgr::Now() const
{
    return MarketSimDB::ToUnix(GetFileTimeNow()) + m_offset;
}

bool MarketSimMgr::IsRunning() const
{
    return m_enabled and (m_world.get() != nullptr) and !m_world->Paused();
}

void MarketSimMgr::ReadParams()
{
    const auto& c(sConfig.marketSim);
    Params p;
    p.seed = c.Seed;
    p.economyScale = c.EconomyScale;
    p.tickMinutes = (int)std::max<uint32>(1, c.TickMinutes);
    p.hotCellMinutes = (int)std::max<uint32>(1, c.HotCellMinutes);
    p.coldCellMinutes = (int)std::max<uint32>(1, c.ColdCellMinutes);
    p.hotCellShare = c.HotCellShare;
    p.quoteLevels = (int)c.QuoteLevels;
    p.maxOrdersPerCellSide = (int)c.MaxOrdersPerCellSide;
    p.simMaxOrders = c.SimMaxOrders;
    p.quoteDepthDays = c.QuoteDepthDays;
    p.requoteThreshold = c.RequoteThreshold;
    p.minHalfSpread = c.MinHalfSpread;
    p.kMax = c.MaxPriceDeviation;
    p.fairValueHalfLifeDays = c.FairValueHalfLifeDays;
    p.marketNoise = c.MarketNoise;
    p.transientNoise = c.TransientNoise;
    p.playerTradeWeight = c.PlayerTradeWeight;
    p.buyBand = c.BuyBand;
    p.sellBand = c.SellBand;
    p.intakeShareCounterparty = c.IntakeShareCounterparty;
    p.maxIskToCounterpartyPerDay = c.MaxIskToCounterpartyPerDay;
    p.intakeDays = c.IntakeDays;
    p.simPaysFees = c.SimPaysFees;
    p.brokerFeeRate = c.SimBrokerFee;
    p.salesTaxRate = sConfig.market.salesTax / 100.0;
    p.moneyDaysTarget = c.MoneyDaysTarget;
    p.controllerKp = c.ControllerKp;
    p.controllerKi = c.ControllerKi;
    p.supplyFactor = c.SupplyFactor;
    p.vendorStock = std::max<uint32>(1, c.VendorStock);
    p.maxCatchUpHours = (int)c.MaxCatchUpHours;
    p.pauseOnAuditResidual = c.PauseOnAuditResidual;
    m_params = p;
}

bool MarketSimMgr::LoadStatic(std::string& why)
{
    m_static = MktSim::StaticData();
    // nodes:  the hubs, then the regional nodes
    std::vector<uint32> stations(ParseList(sConfig.marketSim.Hubs));
    m_hubs.clear();
    std::set<uint32> regions, hubRegions;
    auto addNode = [&](uint32 stationID, uint8 kind) {
        if (!sDataMgr.IsStation(stationID)) {
            _log(MARKET__SIM, "Market sim:  %u is not a station.  Skipped.", stationID);
            return;
        }
        for (auto& n : m_static.nodes)
            if (n.stationID == stationID)
                return;
        NodeData n = NodeData();
        n.nodeID = (uint32)m_static.nodes.size() + 1;
        n.stationID = stationID;
        n.systemID = sDataMgr.GetStationSystem(stationID);
        n.regionID = sDataMgr.GetStationRegion(stationID);
        n.kind = kind;
        uint32 owner(stDataMgr.GetOwnerID(stationID));
        n.factionID = sDataMgr.GetCorpFaction(owner);
        SystemData sys = SystemData();
        if (sDataMgr.GetSystemData(n.systemID, sys))
            n.security = sys.securityRating;
        m_static.nodes.push_back(n);
        m_static.stationOwner[stationID] = owner;
        regions.insert(n.regionID);
        if (kind == NodeKind::Hub)
            hubRegions.insert(n.regionID);
    };
    for (auto s : stations) {
        addNode(s, NodeKind::Hub);
        m_hubs.push_back(s);
    }
    std::string regional(sConfig.marketSim.RegionalNodes);
    std::vector<uint32> extra;
    if (regional == "auto")
        MarketSimDB::AutoRegionalStations(hubRegions, extra);
    else if (regional != "none")
        extra = ParseList(regional);
    for (auto s : extra)
        addNode(s, NodeKind::Regional);
    if (m_static.nodes.empty()) {
        why = "no market nodes (marketSim Hubs)";
        return false;
    }

    size_t types(MarketSimDB::LoadCalibration(m_static, regions));
    if (types == 0) {
        why = "no calibration data (mktCalType is empty:  run the market calibration migration)";
        return false;
    }
    MarketSimDB::LoadMaterials(m_static);
    MarketSimDB::LoadVendorOffers(m_static, sConfig.marketSim.VendorStations, m_hubs);
    PickCorps();

    std::vector<std::string> problems;
    m_static.Validate(problems);
    for (auto& p : problems)
        _log(MARKET__SIM, "Market sim static data:  %s", p.c_str());
    sLog.Cyan("    MarketSimMgr", "%u nodes, %u calibrated types, %u npc vendor offers.", (uint32)m_static.nodes.size(), (uint32)types,
              (uint32)m_static.vendors.size());
    return true;
}

void MarketSimMgr::PickCorps()
{
    /* the npc corps the actors trade as (PLAN 7.1):  for each node and sector, the corp of the station's faction that
     *  trades the most types of the sector's categories (crpNPCCorporationTrades);  the station's owner when none does */
    std::map<uint32, uint32> faction;
    std::map<uint32, std::map<uint8, uint32>> trades;
    MarketSimDB::LoadCorpTrades(faction, trades);
    for (auto& node : m_static.nodes) {
        uint32 owner(m_static.stationOwner[node.stationID]);
        for (uint8 sector = 0; sector < Sector::Count; ++sector) {
            uint32 best(0), bestCount(0);
            for (auto& cur : trades) {
                if ((faction[cur.first] != node.factionID) or (node.factionID == 0))
                    continue;
                uint32 n(0);
                for (auto& cat : cur.second)
                    if (InSector(sector, cat.first))
                        n += cat.second;
                if (n > bestCount) {
                    best = cur.first;
                    bestCount = n;
                }
            }
            m_static.mmCorp[std::make_pair(node.nodeID, sector)] = (best != 0) ? best : owner;
        }
    }
}

int MarketSimMgr::Initialize()
{
    m_enabled = sConfig.marketSim.Enabled;
    std::string why;
    if (!MarketSimDB::TablesReady(why)) {
        sLog.Error("    MarketSimMgr", "Market sim unavailable:  %s.", why.c_str());
        return 1;
    }
    ReadParams();
    if (!LoadStatic(why)) {
        sLog.Error("    MarketSimMgr", "Market sim unavailable:  %s.", why.c_str());
        return 1;
    }

    std::map<std::string, std::string> state;
    MarketSimDB::LoadState(state);
    bool saved(state["built"] == "1");
    if (!saved and !m_enabled) {
        sLog.Warning("    MarketSimMgr", "Market sim is disabled (marketSim Enabled).");
        return 1;
    }

    int64 now(Now());
    m_gw.reset(new ServerMarketGateway());
    m_gw->Init();
    m_world.reset(new World(m_static, m_params, *m_gw));
    m_world->SetPersist(this);
    m_world->SetDayHook([this](const DayReport& rep) { OnDay(rep); });
    m_world->Build(now);

    if (saved) {
        if (!LoadSaved(now)) {
            sLog.Error("    MarketSimMgr", "Market sim:  loading its state failed.  It stays off.");
            m_world.reset();
            return 1;
        }
    } else {
        // a fresh economy:  the starting money and stock (one-off faucets, in the first day's audit)
        m_startDay = DayOf(now);
        m_world->Seed(now);
        m_world->GetLedger().MarkAllDirty();
        m_world->MarkAllCellsChanged();
        sLog.Green("    MarketSimMgr", "Market sim built:  %u cells, %u actors.  The books fill over the next %u minutes (or /mktsim seed).",
                   (uint32)m_world->Cells().size(), (uint32)m_world->Actors().size(), (uint32)m_params.coldCellMinutes);
    }
    m_world->Start(now, true);
    m_world->PostMissingVendorOrders(now);
    if (!m_enabled)
        sLog.Warning("    MarketSimMgr", "Market sim is disabled:  its %u orders stay as they are, backed by its ledger.", m_world->LiveOrders());
    if (m_world->Paused())
        sLog.Warning("    MarketSimMgr", "Market sim is paused (/mktsim resume).");
    m_lastFlush = now;
    m_lastHistoryInvalidate = now;
    // the market agrees with the ledger before the first client can trade (Reconcile's cuts, the vendor orders)
    m_world->FlushOrders();
    Flush();
    uint32 legacy(MarketSimDB::CountLegacyOrders());
    if (legacy > 0)
        sLog.Warning("    MarketSimMgr", "%u npc market orders have nothing behind them (seed scripts, Trader Joe) and are never filled.  /mktsim purgelegacy removes them.", legacy);
    sLog.Blue("    MarketSimMgr", "Market sim %s.", m_enabled ? "running" : "loaded");
    return 1;
}

bool MarketSimMgr::LoadSaved(int64 now)
{
    std::map<std::string, std::string> state;
    MarketSimDB::LoadState(state);
    m_startDay = std::atoi(state["startDay"].c_str());
    m_world->SetControllerIntegral(std::strtod(state["integral"].c_str(), nullptr));
    double factor(std::strtod(state["incomeFactor"].c_str(), nullptr));
    m_world->SetIncomeFactor((factor > 0.0) ? factor : 1.0);
    m_world->SetLastIncomeTime(std::strtoll(state["lastIncome"].c_str(), nullptr, 10));
    m_world->Pause(state["paused"] == "1");

    // nodes by station:  the hub list may have changed
    std::vector<MarketSimDB::NodeRow> nodes;
    MarketSimDB::LoadNodes(nodes);
    std::map<uint32, uint32> nodeMap;
    for (auto& r : nodes) {
        uint32 mapped(1000 + r.nodeID);     // a node that left keeps its stock under an id no cell has
        for (auto& n : m_static.nodes)
            if (n.stationID == r.stationID)
                mapped = n.nodeID;
        nodeMap[r.nodeID] = mapped;
        if (mapped != r.nodeID)
            m_replaceInventory = true;
    }
    auto node = [&nodeMap](uint32 old) -> uint32 {
        std::map<uint32, uint32>::const_iterator itr = nodeMap.find(old);
        return (itr == nodeMap.end()) ? old : itr->second;
    };

    // actors by what they are (kind, sector, node, region, and the corp for vendors)
    std::map<std::string, uint32> built;
    for (auto& a : m_world->Actors())
        built[Fmt("%u/%u/%u/%u/%u", a.kind, a.sector, a.nodeID, a.regionID, (a.kind == ActorKind::Vendor) ? a.ownerCorpID : 0)] = a.actorID;
    std::vector<MarketSimDB::ActorRow> actors;
    MarketSimDB::LoadActors(actors);
    std::map<uint32, uint32> actorMap;
    for (auto& r : actors) {
        std::string key(Fmt("%u/%u/%u/%u/%u", r.kind, r.sector, node(r.nodeID), r.regionID, (r.kind == ActorKind::Vendor) ? r.corpID : 0));
        std::map<std::string, uint32>::iterator itr = built.find(key);
        uint32 id(0);
        if (itr != built.end()) {
            id = itr->second;
            built.erase(itr);
        } else {
            // an actor the economy no longer has keeps its money (retired, never processed)
            Actor a = Actor();
            a.actorID = (uint32)m_world->Actors().size() + 1;
            a.kind = r.kind;
            a.sector = r.sector;
            a.ownerCorpID = r.corpID;
            a.regionID = r.regionID;
            a.nodeID = node(r.nodeID);
            a.state = 1;
            m_world->Actors().push_back(a);
            id = a.actorID;
        }
        actorMap[r.actorID] = id;
        if (id != r.actorID)
            m_replaceInventory = true;
        m_world->GetLedger().Load(id, r.wallet, r.reserved);
    }
    auto actor = [&actorMap](uint32 old) -> uint32 {
        std::map<uint32, uint32>::const_iterator itr = actorMap.find(old);
        return (itr == actorMap.end()) ? 0 : itr->second;
    };

    std::vector<MarketSimDB::StockRow> stock;
    MarketSimDB::LoadInventory(stock);
    for (auto& r : stock) {
        uint32 a(actor(r.actorID));
        if (a != 0)
            m_world->GetLedger().LoadStock(Ledger::Key(a, (uint16)node(r.nodeID), r.typeID), r.qty, r.reserved);
    }

    std::vector<MarketSimDB::CellRow> cells;
    MarketSimDB::LoadCells(cells);
    for (auto& r : cells) {
        Cell* c(m_world->FindCell(node(r.nodeID), r.typeID));
        if (c == nullptr)
            continue;
        c->lnV = r.lnV;
        c->lnAnchor = r.lnAnchor;
        c->dBar = r.dBar;
        c->targetStock = r.targetStock;
        c->lastClear = r.lastClear;
        c->lastTime = r.lastTime;
        c->nextDue = r.nextDue;
        c->flags = (c->flags & CellFlag::Hot) | (r.flags & (CellFlag::Backfilled | CellFlag::Clamped));
        c->clampDays = r.clampDays;
    }
    // cells that are new (the calibration grew) start fresh, with their stock
    std::vector<uint32> fresh;
    for (auto& c : m_world->Cells())
        if (c.lnV == 0.0)
            fresh.push_back(c.index);

    // GM overrides
    for (auto& cur : state) {
        unsigned nodeID(0), typeID(0);
        if (std::sscanf(cur.first.c_str(), "anchor:%u:%u", &nodeID, &typeID) == 2) {
            Cell* c(m_world->FindCell(nodeID, (uint16)typeID));
            if (c != nullptr)
                c->anchorOverride = std::strtod(cur.second.c_str(), nullptr);
        } else if (std::sscanf(cur.first.c_str(), "shock:%u:%u", &nodeID, &typeID) == 2) {
            Cell* c(m_world->FindCell(nodeID, (uint16)typeID));
            double mult(1.0);
            long long until(0);
            if ((c != nullptr) and (std::sscanf(cur.second.c_str(), "%lf,%lld", &mult, &until) == 2)) {
                c->shockMult = mult;
                c->shockUntil = until;
            }
        }
    }

    // the orders:  gone ones are forgotten, those of cells that left are cancelled, the rest are the world's again
    std::vector<MarketSimDB::OrderRow> orders;
    MarketSimDB::LoadOrders(orders);
    std::map<std::tuple<uint32, uint16, uint32>, uint32> slots;     // (actor, type, station) -> vendor slot
    for (uint32 i = 0; i < m_world->VendorSlots().size(); ++i) {
        const VendorSlot& s(m_world->VendorSlots()[i]);
        slots[std::make_tuple(s.actorID, s.offer.typeID, s.offer.stationID)] = i;
    }
    uint32 gone(0), dropped(0), loaded(0);
    // (orderID, actorID, nodeID) of orders whose actor or node was renumbered:  mktSimOrder follows, or the next boot
    //  would read the old numbers as new ones
    std::vector<std::tuple<uint32, uint32, uint32>> remap;
    for (auto& r : orders) {
        if (!r.live) {
            MarketSimDB::ForgetOrder(r.orderID);
            ++gone;
            continue;
        }
        SimOrder o = SimOrder();
        o.orderID = r.orderID;
        o.actorID = actor(r.actorID);
        o.typeID = r.typeID;
        o.stationID = r.stationID;
        o.regionID = r.regionID;
        o.bid = r.bid;
        o.level = r.level;
        o.price = r.priceCents;
        o.qty = r.qty;
        o.issued = r.issued;
        bool ok(false);
        if (r.nodeID == 0) {
            std::map<std::tuple<uint32, uint16, uint32>, uint32>::iterator itr = slots.find(std::make_tuple(o.actorID, o.typeID, o.stationID));
            if (itr != slots.end()) {
                o.cell = VendorCell;
                o.vendorSlot = itr->second;
                ok = true;
            }
        } else {
            Cell* c(m_world->FindCell(node(r.nodeID), r.typeID));
            if ((c != nullptr) and (o.actorID == c->mm)) {
                o.cell = c->index;
                ok = true;
            }
        }
        if (!ok) {
            // nothing here backs it any more:  it goes (what was reserved for it returns with the reconciliation)
            OrderBatch batch;
            batch.cancels.push_back(CancelOrder{r.orderID, r.typeID, r.regionID});
            std::vector<uint32> none;
            MarketSimDB::ApplyOrders(batch, none, false);
            sMktMgr.InvalidateOrdersCache(r.regionID, r.typeID);
            ++dropped;
            continue;
        }
        if ((o.actorID != r.actorID) or (node(r.nodeID) != r.nodeID))
            remap.push_back(std::make_tuple(o.orderID, o.actorID, node(r.nodeID)));
        m_world->LoadOrder(o);
        ++loaded;
    }

    MarketSimDB::RemapOrders(remap);

    std::vector<std::tuple<int32, uint32, uint16, int64, int64>> intake;
    int32 today(DayOf(now));
    MarketSimDB::LoadIntake(today - 1, intake);
    for (auto& i : intake)
        m_world->LoadIntake(std::get<0>(i), std::get<1>(i), std::get<2>(i), std::get<3>(i), std::get<4>(i));

    // today's internal trades so far
    std::vector<HistoryRow> todayRows;
    MarketSimDB::LoadHistoryDay(today, todayRows);
    for (auto& r : todayRows) {
        for (auto& c : m_world->Cells()) {
            const NodeData* n(m_world->Node(c.nodeID));
            if ((c.typeID != r.typeID) or (n->regionID != r.regionID))
                continue;
            c.day = today;
            c.dLow = r.low;
            c.dHigh = r.high;
            c.dSum = r.sumValue;
            c.dVol = r.volume;
            c.dTrades = r.trades;
            break;
        }
    }

    std::string rec(m_world->Reconcile(now));
    // new cells get their stock, new actors their money (one-off faucets)
    m_world->Seed(now, 0, true);
    m_world->GetLedger().MarkAllDirty();
    m_world->MarkAllCellsChanged();
    sLog.Green("    MarketSimMgr", "Market sim loaded:  %u cells (%u new), %u actors, %u orders (%u filled or gone while down, %u dropped).  %s.",
               (uint32)m_world->Cells().size(), (uint32)fresh.size(), (uint32)m_world->Actors().size(), loaded, gone, dropped, rec.c_str());
    return true;
}

std::map<std::string, std::string> MarketSimMgr::StateMap() const
{
    std::map<std::string, std::string> s;
    s["built"] = "1";
    s["schemaVersion"] = SchemaVersion;
    s["seed"] = std::to_string(m_params.seed);
    s["startDay"] = std::to_string(m_startDay);
    s["simTime"] = std::to_string(Now());
    s["integral"] = Fmt("%.17g", m_world->ControllerIntegral());
    s["incomeFactor"] = Fmt("%.17g", m_world->IncomeFactor());
    s["lastIncome"] = std::to_string(m_world->LastIncomeTime());
    s["paused"] = m_world->Paused() ? "1" : "0";
    // the GM overrides that are set (MarketSimDB::Flush replaces them all)
    for (auto& c : m_world->Cells()) {
        if (c.anchorOverride > 0.0)
            s[Fmt("anchor:%u:%u", c.nodeID, c.typeID)] = Fmt("%.17g", c.anchorOverride);
        if (c.shockUntil > Now())
            s[Fmt("shock:%u:%u", c.nodeID, c.typeID)] = Fmt("%.9g,%lld", c.shockMult, (long long)c.shockUntil);
    }
    return s;
}

void MarketSimMgr::Flush()
{
    if (m_world.get() == nullptr)
        return;
    double start(GetTimeMSeconds());
    std::vector<uint32> cells;
    m_world->TakeChangedCells(cells);
    // finished days first (with those a failed flush didn't write:  the world hands them out once)
    std::vector<HistoryRow> history;
    std::swap(history, m_unwrittenHistory);
    m_world->TakeFinishedHistory(history);
    size_t finished(history.size());
    for (auto idx : cells) {
        const Cell& c(m_world->Cells()[idx]);
        if (c.dVol > 0)
            history.push_back(HistoryRow{m_world->Node(c.nodeID)->regionID, c.typeID, c.day, c.dLow, c.dHigh, c.dSum, c.dVol, c.dTrades});
    }
    if (!MarketSimDB::Flush(*m_world, cells, history, StateMap(), m_pendingAudit, m_replaceInventory)) {
        sLog.Error("    MarketSimMgr", "Market sim:  writing its state failed.  It is kept in memory and written again.");
        m_world->MarkAllCellsChanged();
        m_unwrittenHistory.assign(history.begin(), history.begin() + finished);
        return;
    }
    m_pendingAudit.clear();
    m_replaceInventory = false;
    m_lastFlush = Now();
    _log(MARKET__SIM_TRACE, "Market sim flushed %u cells, %u history rows in %.1fms.", (uint32)cells.size(), (uint32)history.size(),
         GetTimeMSeconds() - start);
}

void MarketSimMgr::FlushHistory()
{
    Flush();
}

void MarketSimMgr::Process()
{
    if (m_world.get() == nullptr)
        return;
    int64 now(Now());
    if (m_enabled) {
        double budgetUs(std::max(0.5, sConfig.marketSim.BudgetMsPerSecond) * 1000.0);
        m_world->Step(now, budgetUs, []() { return GetTimeUSeconds(); });
    } else {
        /* disabled with saved state:  nothing is processed, but what boot queued (Reconcile's cuts, duplicate orders,
         *  missing vendor orders) must reach the market, or players fill rows the ledger no longer backs */
        m_world->FlushOrders();
    }
    if ((now - m_lastFlush) >= (int64)std::max<uint32>(5, sConfig.marketSim.FlushSeconds)) {
        Flush();
        _log(MARKET__SIM_TRACE, "Market sim:  %llu cell visits, %u live orders, %llu order writes, backlog %us.",
             (unsigned long long)m_world->CellVisits(), m_world->LiveOrders(), (unsigned long long)m_gw->Writes(),
             (uint32)m_world->Backlog(now));
    }
    // today's history the client caches:  re-read at most hourly (it keeps it up to 3 hours anyway)
    if ((now - m_lastHistoryInvalidate) >= 3600) {
        m_lastHistoryInvalidate = now;
        std::vector<std::pair<uint32_t, uint16_t>> touched;
        m_world->TakeHistoryTouched(touched);
        for (auto& cur : touched)
            sMktMgr.InvalidateHistoryCache(cur.first, cur.second);
    }
}

void MarketSimMgr::Close()
{
    if (m_world.get() == nullptr)
        return;
    m_world->FlushOrders();
    Flush();
    sLog.Warning("    MarketSimMgr", "Market sim has been closed.");
}

void MarketSimMgr::OnDay(const DayReport& rep)
{
    for (auto& line : m_world->AuditLines())
        _log(MARKET__SIM_AUDIT, "day %i %s %lld", line.day, line.category.c_str(), (long long)line.amount);
    m_pendingAudit = m_world->AuditLines();
    double m((rep.moneyTarget > 0.0) ? (double)rep.money / rep.moneyTarget : 0.0);
    _log(MARKET__SIM, "Market sim day %i:  M/M* %.3f, income x%.3f (%.0f ISK), %u orders, %u cells clamped.", rep.day, m,
         rep.incomeFactor, Econ::ToIsk(rep.incomeToday), rep.liveOrders, rep.clampedCells);
    if ((rep.audit.iskResidual != 0) or (rep.audit.goodsResidualTypes != 0))
        sLog.Error("    MarketSimMgr", "Market sim day %i:  the books don't close:  %lld cents, %u types (%lld units).%s", rep.day,
                   (long long)rep.audit.iskResidual, (uint32)rep.audit.goodsResidualTypes, (long long)rep.audit.goodsResidualUnits,
                   m_params.pauseOnAuditResidual ? "  Paused." : "");
    if ((m < 0.5) or (m > 2.0))
        _log(MARKET__SIM, "Market sim day %i:  money M/M* %.3f is outside [0.5, 2].", rep.day, m);
}

/************************************************************************************************************************
 * hooks
 */

bool MarketSimMgr::IsSimOrder(uint32 orderID) const
{
    return (m_world.get() != nullptr) and m_world->IsSimOrder(orderID);
}

bool MarketSimMgr::CanFill(const Market::OrderInfo& oInfo, uint32 qty) const
{
    if (m_world.get() == nullptr)
        return false;
    const SimOrder* o(m_world->FindOrder(oInfo.orderID));
    if (o == nullptr)
        return false;
    if ((o->bid == oInfo.isBuy) and (o->price == (Cents)std::llround(oInfo.price * 100.0)) and (qty > 0) and (qty <= o->qty))
        return true;
    // the row wasn't written as the sim has it (a failed write is retried with the next flush)
    _log(MARKET__SIM, "Market sim:  order #%u is %s %u at %.2f in the market but %u at %.2f in the sim.  Not filled (%u wanted).",
         oInfo.orderID, oInfo.isBuy ? "bid" : "ask", oInfo.quantity, oInfo.price, o->qty, Econ::ToIsk(o->price), qty);
    return false;
}

void MarketSimMgr::OnSimAskFilled(const Market::OrderInfo& oInfo, uint32 qty, uint32 buyerOwnerID)
{
    if (m_world.get() != nullptr)
        m_world->OnAskFilled(oInfo.orderID, qty, buyerOwnerID, Now());
}

void MarketSimMgr::OnSimBidFilled(const Market::OrderInfo& oInfo, uint32 qty, uint32 sellerOwnerID)
{
    if (m_world.get() != nullptr)
        m_world->OnBidFilled(oInfo.orderID, qty, sellerOwnerID, Now());
}

uint32 MarketSimMgr::AllowIntake(const Market::OrderInfo& oInfo, uint32 sellerOwnerID, uint32 qty)
{
    if (m_world.get() == nullptr)
        return 0;
    return m_world->AllowIntake(oInfo.orderID, sellerOwnerID, qty, Now());
}

void MarketSimMgr::OnPlayerOrder(uint32 regionID, uint16 typeID)
{
    if (m_world.get() == nullptr)
        return;
    m_gw->NotePlayerOrder(regionID, typeID);
    m_world->MarkDirty(regionID, typeID, Now());
}

void MarketSimMgr::EnsureBackfill(uint32 regionID, uint16 typeID)
{
    if ((m_world.get() == nullptr) or (sConfig.marketSim.HistoryBackfillDays == 0))
        return;
    std::vector<HistoryRow> rows;
    if (!m_world->BackfillSeries(regionID, typeID, (int)sConfig.marketSim.HistoryBackfillDays, m_startDay, rows))
        return;
    // only where nobody traded before the sim started
    if (!MarketSimDB::HasHistoryBefore(regionID, typeID, MarketSimDB::DayToFileTime(m_startDay)))
        MarketSimDB::InsertBackfill(rows);
    m_world->MarkBackfilled(regionID, typeID);
}

void MarketSimMgr::PlayerFill(uint32_t actorID, Cents dWallet, Cents dReserved, uint32_t nodeID, uint16_t typeID, Units dQty, Units dReservedQty)
{
    MarketSimDB::ActorDelta(actorID, dWallet, dReserved);
    if (nodeID != 0)
        MarketSimDB::StockDelta(actorID, nodeID, typeID, dQty, dReservedQty);
}

void MarketSimMgr::IntakeAdd(int32_t day, uint32_t ownerID, uint16_t typeID, Units qty, Cents isk)
{
    MarketSimDB::IntakeAdd(day, ownerID, typeID, qty, isk);
}

void MarketSimMgr::OrderGone(uint32_t orderID)
{
    MarketSimDB::ForgetOrder(orderID);
}

void MarketSimMgr::FillBegin()
{
    sDatabase.StartTransaction("market sim trade with a player order");
}

void MarketSimMgr::FillEnd()
{
    sDatabase.CommitTransaction();
}

/************************************************************************************************************************
 * /mktsim
 */

void MarketSimMgr::RunUntilIdle(int64 now)
{
    // without a budget, until nothing due is left (or nothing moves)
    for (int i = 0; i < 1000; ++i) {
        uint64_t before(m_world->CellVisits());
        m_world->Step(now);
        if ((m_world->Backlog(now) == 0) or (m_world->CellVisits() == before))
            break;
    }
}

std::string MarketSimMgr::Command(Client* who, const Seperator& args)
{
    std::string sub((args.argCount() > 1) ? args.arg(1) : "status");
    std::string usage("usage: /mktsim status | pause | resume | cell <typeID> [stationID] | actor <actorID> | actors [kind] |"
                      " anchor <typeID> <price> [regionID] | shock <typeID|g:groupID|c:categoryID> <demandMult> <days> |"
                      " requote <typeID|all> | seed [stationID] | audit [days] | step <ticks> | purge | purgelegacy | export <file>");
    if (sub == "purgelegacy") {
        std::set<std::pair<uint32, uint16>> touched;
        uint32 n(MarketSimDB::PurgeLegacyOrders(touched));
        for (auto& cur : touched)
            sMktMgr.InvalidateOrdersCache(cur.first, cur.second);
        _log(MARKET__SIM, "Market sim:  %s purged %u legacy npc orders.", who->GetName(), n);
        return Fmt("Removed %u npc orders that nothing backed.", n);
    }
    if (m_world.get() == nullptr)
        return "The market sim is not loaded (marketSim Enabled, the calibration data, the server log).";
    int64 now(Now());
    if (sub == "status") {
        return std::string(m_enabled ? "" : "DISABLED in the config (orders kept, not processed).\n") + m_world->Status(now)
             + Fmt("Legacy npc orders nothing backs:  %u.  Order writes %llu.", MarketSimDB::CountLegacyOrders(),
                   (unsigned long long)m_gw->Writes());
    }
    if ((sub == "pause") or (sub == "resume")) {
        m_world->Pause(sub == "pause");
        Flush();
        _log(MARKET__SIM, "Market sim %s by %s.", (sub == "pause") ? "paused" : "resumed", who->GetName());
        return std::string("Market sim ") + ((sub == "pause") ? "paused." : "resumed.");
    }
    if (sub == "cell") {
        if ((args.argCount() < 3) or !args.isNumber(2))
            return usage;
        uint32 station((args.argCount() > 3) ? (uint32)std::strtoul(args.arg(3).c_str(), nullptr, 10) : 0);
        return m_world->CellInfo((uint16)std::strtoul(args.arg(2).c_str(), nullptr, 10), station);
    }
    if (sub == "actor") {
        if ((args.argCount() < 3) or !args.isNumber(2))
            return usage;
        return m_world->ActorInfo((uint32)std::strtoul(args.arg(2).c_str(), nullptr, 10));
    }
    if (sub == "actors")
        return m_world->ActorList((args.argCount() > 2) ? std::atoi(args.arg(2).c_str()) : 0);
    if (sub == "anchor") {
        if ((args.argCount() < 4) or !args.isNumber(2))
            return usage;
        uint16 typeID((uint16)std::strtoul(args.arg(2).c_str(), nullptr, 10));
        double price(std::strtod(args.arg(3).c_str(), nullptr));
        uint32 region((args.argCount() > 4) ? (uint32)std::strtoul(args.arg(4).c_str(), nullptr, 10) : 0);
        uint32 n(m_world->SetAnchor(typeID, price, region, now));
        m_pendingAudit.push_back(AuditLine{DayOf(now), Fmt("gm.anchor.%u.%u.by%u", typeID, region, who->GetCharacterID()), Econ::ToCents(price)});
        _log(MARKET__SIM, "Market sim:  %s anchored type %u at %.2f ISK in region %u (%u cells).", who->GetName(), typeID, price, region, n);
        Flush();
        return Fmt("Anchor of type %u %s in %u cells.", typeID, (price > 0.0) ? "set" : "cleared", n);
    }
    if (sub == "shock") {
        if (args.argCount() < 5)
            return usage;
        std::string what(args.arg(2));
        char kind('t');
        if ((what.size() > 2) and (what[1] == ':')) {
            kind = what[0];
            what.erase(0, 2);
        }
        uint32 id((uint32)std::strtoul(what.c_str(), nullptr, 10));
        double mult(std::strtod(args.arg(3).c_str(), nullptr)), days(std::strtod(args.arg(4).c_str(), nullptr));
        if ((id == 0) or !(mult > 0.0) or !(days > 0.0) or ((kind != 't') and (kind != 'g') and (kind != 'c')))
            return usage;
        uint32 n(m_world->Shock(kind, id, mult, days, now));
        m_pendingAudit.push_back(AuditLine{DayOf(now), Fmt("gm.shock.%c%u.by%u", kind, id, who->GetCharacterID()), (int64)std::llround(mult * 1e6)});
        _log(MARKET__SIM, "Market sim:  %s set demand x%.3f for %.1f days on %c:%u (%u cells).", who->GetName(), mult, days, kind, id, n);
        Flush();
        return Fmt("Demand x%.3f for %.1f days in %u cells.", mult, days, n);
    }
    if (sub == "requote") {
        uint16 typeID(((args.argCount() > 2) and (args.arg(2) != "all")) ? (uint16)std::strtoul(args.arg(2).c_str(), nullptr, 10) : 0);
        return Fmt("%u cells will be re-quoted.", m_world->Requote(typeID, now));
    }
    if (sub == "seed") {
        uint32 nodeID(0);
        if (args.argCount() > 2) {
            uint32 station((uint32)std::strtoul(args.arg(2).c_str(), nullptr, 10));
            for (auto& n : m_world->Nodes())
                if (n.stationID == station)
                    nodeID = n.nodeID;
            if (nodeID == 0)
                return Fmt("%u is not a market sim node.", station);
        }
        m_world->Seed(now, nodeID);
        RunUntilIdle(now);
        Flush();
        _log(MARKET__SIM, "Market sim:  %s seeded node %u.", who->GetName(), nodeID);
        return Fmt("Seeded %s:  %u live orders.", nodeID ? "the node" : "every node", m_world->LiveOrders());
    }
    if (sub == "audit") {
        int days((args.argCount() > 2) ? std::max(1, std::atoi(args.arg(2).c_str())) : 1);
        DBQueryResult res;
        DBResultRow row;
        std::string out;
        if (sDatabase.RunQuery(res, "SELECT day, category, amount FROM mktSimAudit WHERE day >= %i ORDER BY day, category", DayOf(now) - days))
            while (res.GetRow(row))
                out += Fmt("day %i  %-28s %lld\n", row.GetInt(0), row.GetText(1), (long long)row.GetInt64(2));
        return out.empty() ? std::string("No audit rows yet (one is written after each day).") : out;
    }
    if (sub == "step") {
        if (((who->GetAccountRole() & Acct::Role::PROGRAMMER) == 0) or !sConfig.debug.IsTestServer)
            return "/mktsim step needs the PROGRAMMER role on a test server (debug IsTestServer).";
        int ticks((args.argCount() > 2) ? std::max(1, std::atoi(args.arg(2).c_str())) : 1);
        // the main loop waits for it (no clients are served meanwhile):  at most a day per command
        ticks = std::min(ticks, std::max(1, 1440 / std::max(1, m_params.tickMinutes)));
        for (int i = 0; i < ticks; ++i) {
            m_offset += 60LL * m_params.tickMinutes;
            RunUntilIdle(Now());
        }
        Flush();
        return Fmt("The market sim is %lld minutes ahead of the clock.", (long long)(m_offset / 60));
    }
    if (sub == "purge") {
        uint32 n(m_world->PurgeOrders());
        m_world->FlushOrders();
        Flush();
        _log(MARKET__SIM, "Market sim:  %s purged its %u orders (the sim is paused).", who->GetName(), n);
        return Fmt("Removed the sim's %u orders.  The sim is paused (/mktsim resume posts them again).", n);
    }
    if (sub == "export") {
        if (args.argCount() < 3)
            return usage;
        // into the log folder, by name only
        std::string name(args.arg(2));
        size_t slash(name.find_last_of("/\\"));
        if (slash != std::string::npos)
            name.erase(0, slash + 1);
        std::string path(sConfig.files.logDir + name);
        return m_world->ExportCsv(path) ? ("Written to " + path + ".") : ("Can't write " + path + ".");
    }
    return usage;
}
