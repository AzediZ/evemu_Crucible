
 /**
  * @name MarketSimDB.cpp
  *   market simulation:  the database side
  *
  * @date:          13Oct26
  *
  */

#include <cmath>
#include <cstdio>
#include <tuple>
#include <unordered_map>

#include "market/sim/MarketSimDB.h"
#include "market/sim/SimChains.h"
#include "market/sim/SimEconomy.h"

/*
 * MARKET__SIM
 * MARKET__SIM_TRACE
 */

using namespace MktSim;

namespace {
    const size_t Chunk = 500;       // rows per statement

    bool Exec(const std::string& sql)
    {
        DBerror err;
        if (!sDatabase.RunQuery(err, "%s", sql.c_str())) {
            _log(MARKET__SIM, "MarketSimDB - query failed: %s", err.c_str());
            return false;
        }
        return true;
    }

    // head + up to Chunk rows + tail per statement
    bool InsertRows(const std::string& head, const std::vector<std::string>& rows, const std::string& tail)
    {
        bool ok(true);
        for (size_t i = 0; i < rows.size(); i += Chunk) {
            std::string sql(head);
            for (size_t k = i; (k < rows.size()) and (k < i + Chunk); ++k) {
                if (k > i)
                    sql += ",";
                sql += rows[k];
            }
            sql += tail;
            ok = Exec(sql) and ok;
        }
        return ok;
    }

    std::string Fmt(const char* fmt, ...)
    {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    // cents as the decimal column wants them
    std::string IskText(Cents c)
    {
        return Fmt("%lld.%02lld", (long long)(c / 100), (long long)(c % 100));
    }

    // the sim's own names and numbers:  quotes and backslashes doubled up, nothing else needs it
    std::string Quote(const std::string& s)
    {
        std::string out;
        for (char c : s) {
            if ((c == '\'') or (c == '\\'))
                out += c;
            out += c;
        }
        return out;
    }

    float NotNull(DBResultRow& row, uint32 col)         { return row.IsNull(col) ? NAN : row.GetFloat(col); }
}

bool MarketSimDB::TablesReady(std::string& why)
{
    static const char* tables[] = { "mktSimState", "mktSimActor", "mktSimInventory", "mktSimCell", "mktSimOrder",
                                    "mktSimHistory", "mktSimIntake", "mktSimAudit", "mktSimNode", "mktCalType",
                                    "mktCalRegion", "mktCalRegionDefault", "mktCalGroup" };
    for (auto name : tables) {
        DBQueryResult res;
        if (!sDatabase.RunQuery(res, "SHOW TABLES LIKE '%s'", name) or (res.GetRowCount() == 0)) {
            why = std::string("table ") + name + " is missing (run the market sim and calibration migrations)";
            return false;
        }
    }
    // phase 3 (20261015100000-market-sim-actors.sql)
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SHOW COLUMNS FROM mktSimCell LIKE 'lossWant'") or (res.GetRowCount() == 0)) {
        why = "column mktSimCell.lossWant is missing (run the market sim actors migration)";
        return false;
    }
    return true;
}

int MarketSimDB::AutoIncLockMode()
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res, "SELECT @@innodb_autoinc_lock_mode") or !res.GetRow(row))
        return 2;
    return row.GetInt(0);
}

/************************************************************************************************************************
 * static data
 */

size_t MarketSimDB::LoadCalibration(StaticData& data, const std::set<uint32>& regions)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT c.typeID, i.groupID, g.categoryID, i.portionSize, i.volume, i.basePrice, c.simClass, c.priceSource,"
        " c.confidence, c.refPrice, c.priceP10, c.priceP90, c.volatility, c.unitsPerDay, c.activeShare, c.lotSize,"
        " c.buildCostRef, c.consumeShare, c.elasticity, IFNULL(m.metaGroupID, 0)"
        " FROM mktCalType c"
        " JOIN invTypes i ON i.typeID = c.typeID"
        " JOIN invGroups g ON g.groupID = i.groupID"
        " LEFT JOIN invMetaTypes m ON m.typeID = c.typeID"
        " ORDER BY c.typeID"))
    {
        codelog(MARKET__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    while (res.GetRow(row)) {
        TypeData t;
        t.typeID        = row.GetUInt(0);
        t.groupID       = row.GetUInt(1);
        t.categoryID    = row.GetUInt(2);
        t.portionSize   = std::max<uint32>(1, row.GetUInt(3));
        t.volume        = row.GetDouble(4);
        t.basePrice     = row.GetDouble(5);
        t.simClass      = row.GetUInt(6);
        t.priceSource   = row.GetUInt(7);
        t.confidence    = row.GetFloat(8);
        t.refPrice      = row.GetDouble(9);
        t.priceP10      = row.GetDouble(10);
        t.priceP90      = row.GetDouble(11);
        t.volatility    = row.GetFloat(12);
        t.unitsPerDay   = row.GetDouble(13);
        t.activeShare   = row.GetFloat(14);
        t.lotSize       = std::max<uint32>(1, row.GetUInt(15));
        t.buildCostRef  = row.GetDouble(16);
        t.consumeShare  = row.GetFloat(17);
        t.elasticity    = row.GetFloat(18);
        t.metaGroupID   = row.GetUInt(19);
        // PLEX and Apparel are players' only (simClass 0), whatever the calibration says
        if ((t.typeID == 29668) or (t.categoryID == Category::Apparel))
            t.simClass = SimClass::Excluded;
        data.types[t.typeID] = t;
    }

    std::string list;
    for (auto r : regions)
        list += (list.empty() ? "" : ",") + std::to_string(r);
    if (!list.empty() and sDatabase.RunQuery(res,
        "SELECT regionID, typeID, priceRatio, unitsShare, activeShare FROM mktCalRegion WHERE regionID IN (%s)", list.c_str()))
    {
        while (res.GetRow(row)) {
            RegionTypeCal r = RegionTypeCal();
            r.priceRatio = row.GetFloat(2);
            r.unitsShare = row.GetFloat(3);
            r.activeShare = row.GetFloat(4);
            data.region[std::make_pair(row.GetUInt(0), (uint16)row.GetUInt(1))] = r;
        }
    }
    if (sDatabase.RunQuery(res, "SELECT regionID, priceRatio, unitsShare, iskShare FROM mktCalRegionDefault")) {
        while (res.GetRow(row)) {
            RegionDefault d = RegionDefault();
            d.priceRatio = NotNull(row, 1);
            d.unitsShare = NotNull(row, 2);
            d.iskShare = NotNull(row, 3);
            data.regionDefault[row.GetUInt(0)] = d;
        }
    }
    if (sDatabase.RunQuery(res,
        "SELECT groupID, elasticity, stockElasticity, halfSpread, coverDays, consumeShare, volA, volB, volResid,"
        " costMarkupT1, costMarkupT2, wd0, wd1, wd2, wd3, wd4, wd5, wd6 FROM mktCalGroup"))
    {
        while (res.GetRow(row)) {
            GroupCal g;
            g.elasticity = NotNull(row, 1);
            g.stockElasticity = NotNull(row, 2);
            g.halfSpread = NotNull(row, 3);
            g.coverDays = NotNull(row, 4);
            g.consumeShare = NotNull(row, 5);
            g.volA = NotNull(row, 6);
            g.volB = NotNull(row, 7);
            g.volResid = NotNull(row, 8);
            g.costMarkupT1 = NotNull(row, 9);
            g.costMarkupT2 = NotNull(row, 10);
            for (int d = 0; d < 7; ++d)
                g.wd[d] = NotNull(row, 11 + d);
            data.groups[row.GetUInt(0)] = g;
        }
    }
    return data.types.size();
}

void MarketSimDB::LoadMaterials(StaticData& data)
{
    DBQueryResult res;
    DBResultRow row;
    // what a type is made of, and what it refines into:  invTypeMaterials, per portion
    if (sDatabase.RunQuery(res, "SELECT typeID, materialTypeID, quantity FROM invTypeMaterials ORDER BY typeID, materialTypeID")) {
        while (res.GetRow(row)) {
            std::map<uint16, TypeData>::iterator itr = data.types.find(row.GetUInt(0));
            if (itr == data.types.end())
                continue;
            double per((double)row.GetUInt(2) / (double)itr->second.portionSize);
            itr->second.refine.push_back(std::make_pair((uint16)row.GetUInt(1), per));
        }
    }
    std::set<uint16> built;
    if (sDatabase.RunQuery(res, "SELECT DISTINCT productTypeID FROM invBlueprintTypes"))
        while (res.GetRow(row))
            built.insert(row.GetUInt(0));
    for (auto& cur : data.types)
        if (built.count(cur.first) > 0)
            cur.second.bom = cur.second.refine;
    // and the components a manufacturing job consumes (T1 hulls of T2 ships, T2 components), skills excluded
    if (sDatabase.RunQuery(res,
        "SELECT b.productTypeID, r.requiredTypeID, r.quantity * r.damagePerJob"
        " FROM invBlueprintTypes b"
        " JOIN ramTypeRequirements r ON r.typeID = b.blueprintTypeID AND r.activityID = 1 AND r.damagePerJob > 0"
        " JOIN invTypes i ON i.typeID = r.requiredTypeID"
        " JOIN invGroups g ON g.groupID = i.groupID"
        " WHERE g.categoryID <> 16"
        " ORDER BY b.productTypeID, r.requiredTypeID"))
    {
        while (res.GetRow(row)) {
            std::map<uint16, TypeData>::iterator itr = data.types.find(row.GetUInt(0));
            if (itr == data.types.end())
                continue;
            itr->second.bom.push_back(std::make_pair((uint16)row.GetUInt(1), row.GetDouble(2) / (double)itr->second.portionSize));
        }
    }
}

void MarketSimDB::LoadVendorOffers(StaticData& data, const std::string& mode, const std::vector<uint32>& hubs)
{
    DBQueryResult res;
    DBResultRow row;
    bool ok(false);
    if (mode == "hubsOnly") {
        // every vendor type at every hub, sold by the lowest corp that trades it
        std::string list;
        for (auto h : hubs)
            list += (list.empty() ? "" : ",") + std::to_string(h);
        ok = !list.empty() and sDatabase.RunQuery(res,
            "SELECT MIN(t.corporationID), t.typeID, s.stationID, s.solarSystemID, s.regionID"
            " FROM crpNPCCorporationTrades t"
            " JOIN mktCalType c ON c.typeID = t.typeID AND c.simClass = %u"
            " JOIN staStations s ON s.stationID IN (%s)"
            " GROUP BY t.typeID, s.stationID ORDER BY t.typeID, s.stationID", SimClass::NpcVendor, list.c_str());
    } else if (mode == "all") {
        ok = sDatabase.RunQuery(res,
            "SELECT t.corporationID, t.typeID, s.stationID, s.solarSystemID, s.regionID"
            " FROM crpNPCCorporationTrades t"
            " JOIN mktCalType c ON c.typeID = t.typeID AND c.simClass = %u"
            " JOIN staStations s ON s.corporationID = t.corporationID"
            " ORDER BY t.corporationID, t.typeID, s.stationID", SimClass::NpcVendor);
    } else {
        // corpHomeRegion:  the corp's own stations in the region of its headquarters (Crucible)
        ok = sDatabase.RunQuery(res,
            "SELECT t.corporationID, t.typeID, s.stationID, s.solarSystemID, s.regionID"
            " FROM crpNPCCorporationTrades t"
            " JOIN mktCalType c ON c.typeID = t.typeID AND c.simClass = %u"
            " JOIN crpNPCCorporations n ON n.corporationID = t.corporationID"
            " JOIN mapSolarSystems hq ON hq.solarSystemID = n.solarSystemID"
            " JOIN staStations s ON s.corporationID = t.corporationID AND s.regionID = hq.regionID"
            " ORDER BY t.corporationID, t.typeID, s.stationID", SimClass::NpcVendor);
    }
    if (!ok) {
        codelog(MARKET__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    while (res.GetRow(row)) {
        VendorOffer o = VendorOffer();
        o.corpID = row.GetUInt(0);
        o.typeID = row.GetUInt(1);
        o.stationID = row.GetUInt(2);
        o.systemID = row.GetUInt(3);
        o.regionID = row.GetUInt(4);
        data.vendors.push_back(o);
    }
}

void MarketSimDB::LoadCorpTrades(std::map<uint32, uint32>& corpFaction, std::map<uint32, std::map<uint8, uint32>>& tradesByCategory)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT corporationID, IFNULL(factionID, 0) FROM crpNPCCorporations"))
        while (res.GetRow(row))
            corpFaction[row.GetUInt(0)] = row.GetUInt(1);
    if (sDatabase.RunQuery(res,
        "SELECT t.corporationID, g.categoryID, COUNT(*) FROM crpNPCCorporationTrades t"
        " JOIN invTypes i ON i.typeID = t.typeID JOIN invGroups g ON g.groupID = i.groupID"
        " GROUP BY t.corporationID, g.categoryID"))
        while (res.GetRow(row))
            tradesByCategory[row.GetUInt(0)][(uint8)row.GetUInt(1)] = row.GetUInt(2);
}

void MarketSimDB::AutoRegionalStations(const std::set<uint32>& skipRegions, std::vector<uint32>& stations)
{
    // PLAN 6.1:  high/low-sec regions with at least 1% of the isk traded;  in the system with the most stations, the
    //  station with the most assembly lines
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT d.regionID FROM mktCalRegionDefault d"
        " JOIN (SELECT regionID, MAX(security) AS sec FROM mapSolarSystems GROUP BY regionID) m ON m.regionID = d.regionID"
        " WHERE d.iskShare >= 0.01 AND m.sec > 0.0 ORDER BY d.regionID"))
        return;
    std::vector<uint32> regions;
    while (res.GetRow(row))
        if (skipRegions.count(row.GetUInt(0)) == 0)
            regions.push_back(row.GetUInt(0));
    for (auto regionID : regions) {
        DBQueryResult sres;
        DBResultRow srow;
        if (!sDatabase.RunQuery(sres,
            "SELECT s.stationID FROM staStations s"
            " JOIN (SELECT solarSystemID, COUNT(*) AS n FROM staStations WHERE regionID = %u GROUP BY solarSystemID"
            "       ORDER BY n DESC, solarSystemID ASC LIMIT 1) top ON top.solarSystemID = s.solarSystemID"
            " LEFT JOIN (SELECT stationID, SUM(quantity) AS nLines FROM ramAssemblyLineStations GROUP BY stationID) l"
            "   ON l.stationID = s.stationID"
            " ORDER BY IFNULL(l.nLines, 0) DESC, s.stationID ASC LIMIT 1", regionID))
            continue;
        if (sres.GetRow(srow))
            stations.push_back(srow.GetUInt(0));
    }
}

/************************************************************************************************************************
 * state
 */

void MarketSimDB::LoadState(std::map<std::string, std::string>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT name, value FROM mktSimState"))
        while (res.GetRow(row))
            into[row.GetText(0)] = row.GetText(1);
}

void MarketSimDB::LoadNodes(std::vector<NodeRow>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT nodeID, stationID, solarSystemID, regionID, kind, enabled FROM mktSimNode ORDER BY nodeID"))
        while (res.GetRow(row))
            into.push_back(NodeRow{row.GetUInt(0), row.GetUInt(1), row.GetUInt(2), row.GetUInt(3), (uint8)row.GetUInt(4), row.GetBool(5)});
}

void MarketSimDB::LoadActors(std::vector<ActorRow>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT actorID, kind, sector, ownerCorpID, regionID, homeNodeID, wallet, reserved FROM mktSimActor ORDER BY actorID"))
        while (res.GetRow(row))
            into.push_back(ActorRow{row.GetUInt(0), (uint8)row.GetUInt(1), (uint8)row.GetUInt(2), row.GetUInt(3), row.GetUInt(4),
                                    row.GetUInt(5), row.GetInt64(6), row.GetInt64(7)});
}

void MarketSimDB::LoadInventory(std::vector<StockRow>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT actorID, nodeID, typeID, qty, reservedQty FROM mktSimInventory"))
        while (res.GetRow(row))
            into.push_back(StockRow{row.GetUInt(0), row.GetUInt(1), (uint16)row.GetUInt(2), row.GetInt64(3), row.GetInt64(4)});
}

void MarketSimDB::LoadCells(std::vector<CellRow>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res,
        "SELECT nodeID, typeID, lnV, lnAnchor, demandEwma, targetStock, lastClear, lastTime, nextDue, flags, clampDays, lossWant FROM mktSimCell"))
        while (res.GetRow(row))
            into.push_back(CellRow{row.GetUInt(0), (uint16)row.GetUInt(1), row.GetDouble(2), row.GetDouble(3), row.GetDouble(4),
                                   row.GetDouble(5), row.GetDouble(6), row.GetInt64(7), row.GetInt64(8), row.GetUInt(9),
                                   (uint16)row.GetUInt(10), row.GetDouble(11)});
}

void MarketSimDB::LoadOrders(std::vector<OrderRow>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT s.orderID, s.actorID, s.nodeID, s.typeID, s.bid, s.level, (o.orderID IS NOT NULL),"
        " IFNULL(o.stationID, 0), IFNULL(o.regionID, 0), IFNULL(o.volRemaining, 0), IFNULL(o.price, 0), IFNULL(o.issued, 0)"
        " FROM mktSimOrder s LEFT JOIN mktOrders o ON o.orderID = s.orderID ORDER BY s.orderID"))
    {
        codelog(MARKET__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    while (res.GetRow(row)) {
        OrderRow o = OrderRow();
        o.orderID = row.GetUInt(0);
        o.actorID = row.GetUInt(1);
        o.nodeID = row.GetUInt(2);
        o.typeID = row.GetUInt(3);
        o.bid = row.GetUInt(4) != 0;
        o.level = row.GetUInt(5);
        o.live = row.GetUInt(6) != 0;
        o.stationID = row.GetUInt(7);
        o.regionID = row.GetUInt(8);
        o.qty = row.GetUInt(9);
        o.priceCents = (int64)std::llround(row.GetDouble(10) * 100.0);
        o.issued = ToUnix(row.GetInt64(11));
        into.push_back(o);
    }
}

void MarketSimDB::LoadIntake(int32 fromDay, std::vector<std::tuple<int32, uint32, uint16, int64, int64>>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT day, ownerID, typeID, qty, isk FROM mktSimIntake WHERE day >= %i", fromDay))
        while (res.GetRow(row))
            into.push_back(std::make_tuple(row.GetInt(0), row.GetUInt(1), (uint16)row.GetUInt(2), row.GetInt64(3), row.GetInt64(4)));
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM mktSimIntake WHERE day < %i", fromDay - 7);
}

void MarketSimDB::LoadHistoryDay(int32 day, std::vector<HistoryRow>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT regionID, typeID, low, high, sumValue, volume, trades FROM mktSimHistory WHERE historyDate = %lli",
                           DayToFileTime(day)))
        while (res.GetRow(row))
            into.push_back(HistoryRow{row.GetUInt(0), (uint16)row.GetUInt(1), day, row.GetInt64(2), row.GetInt64(3), row.GetInt64(4),
                                      row.GetInt64(5), row.GetUInt(6)});
}

bool MarketSimDB::Flush(World& world, const std::vector<uint32>& cells, const std::vector<HistoryRow>& history,
                        const std::map<std::string, std::string>& state, const std::vector<AuditLine>& audit,
                        bool replaceInventory/*false*/)
{
    DBTransaction tx("market sim flush");
    bool ok(true);
    std::vector<std::string> rows;
    // rows under old actor/node numbers would come back as stock of whatever has that number next time
    if (replaceInventory)
        ok = Exec("DELETE FROM mktSimInventory") and ok;

    for (auto& a : world.Actors()) {
        const Ledger::Account* acct(world.GetLedger().FindAcct(a.actorID));
        rows.push_back(Fmt("(%u,%u,%u,%u,%u,%u,%lld,%lld,%.6g,0,'share=%.9g',%u)", a.actorID, a.kind, a.sector, a.ownerCorpID,
                           a.regionID, a.nodeID, (long long)(acct ? acct->wallet : 0), (long long)(acct ? acct->reserved : 0),
                           a.target, a.share, a.state));
    }
    ok = InsertRows("INSERT INTO mktSimActor (actorID, kind, sector, ownerCorpID, regionID, homeNodeID, wallet, reserved,"
                    " capacity, riskTolerance, params, state) VALUES ", rows,
                    " ON DUPLICATE KEY UPDATE kind=VALUES(kind), sector=VALUES(sector), ownerCorpID=VALUES(ownerCorpID),"
                    " regionID=VALUES(regionID), homeNodeID=VALUES(homeNodeID), wallet=VALUES(wallet), reserved=VALUES(reserved),"
                    " capacity=VALUES(capacity), params=VALUES(params), state=VALUES(state)") and ok;

    rows.clear();
    for (auto key : world.GetLedger().DirtyStocks()) {
        std::unordered_map<Ledger::StockKey, Ledger::Stock>::const_iterator itr = world.GetLedger().Stocks().find(key);
        if (itr == world.GetLedger().Stocks().end())
            continue;
        rows.push_back(Fmt("(%u,%u,%u,%lld,%lld,0)", Ledger::KeyActor(key), Ledger::KeyNode(key), Ledger::KeyType(key),
                           (long long)itr->second.qty, (long long)itr->second.reserved));
    }
    ok = InsertRows("INSERT INTO mktSimInventory (actorID, nodeID, typeID, qty, reservedQty, avgCost) VALUES ", rows,
                    " ON DUPLICATE KEY UPDATE qty=VALUES(qty), reservedQty=VALUES(reservedQty)") and ok;

    rows.clear();
    for (auto idx : cells) {
        if (idx >= world.Cells().size())
            continue;
        const Cell& c(world.Cells()[idx]);
        uint32 flags(c.flags & (CellFlag::Backfilled | CellFlag::Clamped | CellFlag::Disabled));
        rows.push_back(Fmt("(%u,%u,%.17g,%.17g,%.9g,%.9g,%.9g,%lld,%lld,%u,%u,%.9g)", c.nodeID, c.typeID, c.lnV, c.lnAnchor, c.dBar,
                           c.targetStock, c.lastClear, (long long)c.lastTime, (long long)c.nextDue, flags, c.clampDays, c.lossWant));
    }
    ok = InsertRows("INSERT INTO mktSimCell (nodeID, typeID, lnV, lnAnchor, demandEwma, targetStock, lastClear, lastTime, nextDue,"
                    " flags, clampDays, lossWant) VALUES ", rows,
                    " ON DUPLICATE KEY UPDATE lnV=VALUES(lnV), lnAnchor=VALUES(lnAnchor), demandEwma=VALUES(demandEwma),"
                    " targetStock=VALUES(targetStock), lastClear=VALUES(lastClear), lastTime=VALUES(lastTime),"
                    " nextDue=VALUES(nextDue), flags=VALUES(flags), clampDays=VALUES(clampDays), lossWant=VALUES(lossWant)") and ok;
    // the phase 3 manufacturers' jobs
    ok = FlushJobs(world) and ok;

    // history rows are whole days (or today so far) summed over the region's nodes
    std::map<std::tuple<uint32, uint16, int32>, HistoryRow> sum;
    for (auto& r : history) {
        std::tuple<uint32, uint16, int32> k(r.regionID, r.typeID, r.day);
        std::map<std::tuple<uint32, uint16, int32>, HistoryRow>::iterator itr = sum.find(k);
        if (itr == sum.end()) {
            sum[k] = r;
            continue;
        }
        itr->second.low = std::min(itr->second.low, r.low);
        itr->second.high = std::max(itr->second.high, r.high);
        itr->second.sumValue += r.sumValue;
        itr->second.volume += r.volume;
        itr->second.trades += r.trades;
    }
    rows.clear();
    for (auto& cur : sum) {
        const HistoryRow& r(cur.second);
        rows.push_back(Fmt("(%u,%u,%lld,%lld,%lld,%lld,%lld,%u)", r.regionID, r.typeID, (long long)DayToFileTime(r.day), (long long)r.low,
                           (long long)r.high, (long long)r.sumValue, (long long)r.volume, r.trades));
    }
    ok = InsertRows("INSERT INTO mktSimHistory (regionID, typeID, historyDate, low, high, sumValue, volume, trades) VALUES ", rows,
                    " ON DUPLICATE KEY UPDATE low=VALUES(low), high=VALUES(high), sumValue=VALUES(sumValue), volume=VALUES(volume),"
                    " trades=VALUES(trades)") and ok;

    // the GM overrides are all in 'state' (one removed leaves the table with this)
    ok = Exec("DELETE FROM mktSimState WHERE name LIKE 'anchor:%' OR name LIKE 'shock:%'") and ok;
    rows.clear();
    for (auto& cur : state) {
        rows.push_back("('" + Quote(cur.first) + "','" + Quote(cur.second) + "')");
    }
    ok = InsertRows("INSERT INTO mktSimState (name, value) VALUES ", rows, " ON DUPLICATE KEY UPDATE value=VALUES(value)") and ok;

    rows.clear();
    for (auto& line : audit) {
        rows.push_back(Fmt("(%i,'%s',%lld)", line.day, Quote(line.category).c_str(), (long long)line.amount));
    }
    ok = InsertRows("INSERT INTO mktSimAudit (day, category, amount) VALUES ", rows, " ON DUPLICATE KEY UPDATE amount=VALUES(amount)") and ok;

    rows.clear();
    for (auto& node : world.Nodes())
        rows.push_back(Fmt("(%u,%u,%u,%u,%u,1)", node.nodeID, node.stationID, node.systemID, node.regionID, node.kind));
    if (!rows.empty())
        ok = Exec("DELETE FROM mktSimNode") and ok;
    ok = InsertRows("INSERT INTO mktSimNode (nodeID, stationID, solarSystemID, regionID, kind, enabled) VALUES ", rows, "") and ok;

    if (!ok) {
        tx.Rollback();
        return false;
    }
    if (!tx.Commit())
        return false;
    world.GetLedger().ClearDirty();
    return true;
}

void MarketSimDB::RemapOrders(const std::vector<std::tuple<uint32, uint32, uint32>>& orders)
{
    for (auto& o : orders)
        Exec(Fmt("UPDATE mktSimOrder SET actorID = %u, nodeID = %u WHERE orderID = %u", std::get<1>(o), std::get<2>(o), std::get<0>(o)));
}

void MarketSimDB::ClearState()
{
    static const char* tables[] = { "mktSimState", "mktSimNode", "mktSimActor", "mktSimInventory", "mktSimCell", "mktSimOrder",
                                    "mktSimIntake" };
    for (auto t : tables)
        Exec(std::string("DELETE FROM ") + t);
}

/************************************************************************************************************************
 * at once
 */

void MarketSimDB::ActorDelta(uint32 actorID, int64 dWallet, int64 dReserved)
{
    if ((dWallet == 0) and (dReserved == 0))
        return;
    Exec(Fmt("UPDATE mktSimActor SET wallet = wallet + %lld, reserved = reserved + %lld WHERE actorID = %u",
             (long long)dWallet, (long long)dReserved, actorID));
}

void MarketSimDB::StockDelta(uint32 actorID, uint32 nodeID, uint16 typeID, int64 dQty, int64 dReserved)
{
    if ((dQty == 0) and (dReserved == 0))
        return;
    Exec(Fmt("INSERT INTO mktSimInventory (actorID, nodeID, typeID, qty, reservedQty, avgCost) VALUES (%u,%u,%u,%lld,%lld,0)"
             " ON DUPLICATE KEY UPDATE qty = qty + VALUES(qty), reservedQty = reservedQty + VALUES(reservedQty)",
             actorID, nodeID, typeID, (long long)dQty, (long long)dReserved));
}

void MarketSimDB::IntakeAdd(int32 day, uint32 ownerID, uint16 typeID, int64 qty, int64 isk)
{
    Exec(Fmt("INSERT INTO mktSimIntake (day, ownerID, typeID, qty, isk) VALUES (%i,%u,%u,%lld,%lld)"
             " ON DUPLICATE KEY UPDATE qty = qty + VALUES(qty), isk = isk + VALUES(isk)", day, ownerID, typeID, (long long)qty, (long long)isk));
}

void MarketSimDB::ForgetOrder(uint32 orderID)
{
    Exec(Fmt("DELETE FROM mktSimOrder WHERE orderID = %u", orderID));
}

/************************************************************************************************************************
 * orders
 */

bool MarketSimDB::ApplyOrders(const OrderBatch& batch, std::vector<uint32>& ids, bool multiRow)
{
    // a lost connection fails it as a whole:  ids of posts the server rolled back are never handed out
    DBTransaction tx("market sim orders");
    bool ok(true);
    // posts:  the sim's orders are npc corp orders, station range for asks, 90 days, no escrow (the sim's ledger holds it)
    std::vector<std::string> rows;
    for (auto& n : batch.posts) {
        // orderRange is signed:  the station range is -1
        rows.push_back(Fmt("(%u,%u,%u,%u,%u,%i,%u,%s,0,%u,%u,%u,%lld,0,90,1,0,1000,0)", n.typeID, n.ownerID, n.regionID, n.stationID,
                           n.systemID, (int)n.range, n.bid ? 1 : 0, IskText(n.price).c_str(), n.minVolume, n.qty, n.qty, (long long)ToFileTime(n.issued)));
    }
    static const char* postHead = "INSERT INTO mktOrders (typeID, ownerID, regionID, stationID, solarSystemID, orderRange, bid, price,"
                                  " escrow, minVolume, volEntered, volRemaining, issued, contraband, duration, jumps, isCorp, accountKey,"
                                  " memberID) VALUES ";
    size_t step(multiRow ? Chunk : 1);
    for (size_t i = 0; ok and (i < rows.size()); i += step) {
        std::string sql(postHead);
        size_t n(0);
        for (size_t k = i; (k < rows.size()) and (k < i + step); ++k, ++n)
            sql += (k > i ? "," : "") + rows[k];
        DBerror err;
        uint32 first(0);
        if (!sDatabase.RunQueryLID(err, first, "%s", sql.c_str()) or (first == 0)) {
            _log(MARKET__SIM, "MarketSimDB - posting orders failed: %s", err.c_str());
            ok = false;
            break;
        }
        // a multi-row insert takes consecutive ids (innodb_autoinc_lock_mode 0 or 1, checked at boot)
        for (size_t k = 0; k < n; ++k)
            ids.push_back(first + (uint32)k);
    }
    rows.clear();
    for (size_t i = 0; ok and (i < batch.posts.size()) and (i < ids.size()); ++i) {
        const NewOrder& n(batch.posts[i]);
        rows.push_back(Fmt("(%u,%u,%u,%u,%u,%u)", ids[i], n.actorID, n.nodeID, n.typeID, n.bid ? 1 : 0, n.level));
    }
    if (ok)
        ok = InsertRows("INSERT INTO mktSimOrder (orderID, actorID, nodeID, typeID, bid, level) VALUES ", rows, "");

    /* alters:  one UPDATE ... CASE per chunk.  an order altered twice in a batch (its cell visited twice in a step, a
     *  backlog or /mktsim step) keeps its last values:  CASE takes the first WHEN that matches */
    std::vector<const AlterOrder*> alters;
    {
        std::unordered_map<uint32, size_t> last;
        for (size_t i = 0; i < batch.alters.size(); ++i)
            last[batch.alters[i].orderID] = i;
        for (size_t i = 0; i < batch.alters.size(); ++i)
            if (last[batch.alters[i].orderID] == i)
                alters.push_back(&batch.alters[i]);
    }
    for (size_t i = 0; ok and (i < alters.size()); i += Chunk) {
        std::string price, qty, issued, in;
        for (size_t k = i; (k < alters.size()) and (k < i + Chunk); ++k) {
            const AlterOrder& a(*alters[k]);
            price += Fmt(" WHEN %u THEN %s", a.orderID, IskText(a.price).c_str());
            qty += Fmt(" WHEN %u THEN %u", a.orderID, a.qty);
            issued += Fmt(" WHEN %u THEN %lld", a.orderID, (long long)ToFileTime(a.issued));
            in += (k > i ? "," : "") + std::to_string(a.orderID);
        }
        ok = Exec("UPDATE mktOrders SET price = CASE orderID" + price + " ELSE price END, volRemaining = CASE orderID" + qty
                  + " ELSE volRemaining END, volEntered = CASE orderID" + qty + " ELSE volEntered END, issued = CASE orderID" + issued
                  + " ELSE issued END WHERE orderID IN (" + in + ")");
    }
    for (size_t i = 0; ok and (i < batch.cancels.size()); i += Chunk) {
        std::string in;
        for (size_t k = i; (k < batch.cancels.size()) and (k < i + Chunk); ++k)
            in += (k > i ? "," : "") + std::to_string(batch.cancels[k].orderID);
        ok = Exec("DELETE FROM mktOrders WHERE orderID IN (" + in + ")") and Exec("DELETE FROM mktSimOrder WHERE orderID IN (" + in + ")");
    }
    if (!ok) {
        tx.Rollback();
        ids.clear();
        return false;
    }
    if (!tx.Commit()) {
        ids.clear();
        return false;
    }
    return true;
}

void MarketSimDB::PlayerOrderCells(std::set<std::pair<uint32, uint16>>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res,
        "SELECT DISTINCT regionID, typeID FROM mktOrders WHERE (ownerID BETWEEN %u AND %u AND isCorp = 0) OR isCorp = 1",
        minCharacter, maxCharacter))
        while (res.GetRow(row))
            into.insert(std::make_pair(row.GetUInt(0), (uint16)row.GetUInt(1)));
}

void MarketSimDB::PlayerOrders(uint32 regionID, uint16 typeID, std::vector<PlayerOrder>& asks, std::vector<PlayerOrder>& bids)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT orderID, ownerID, stationID, solarSystemID, bid, price, volRemaining, minVolume, orderRange, issued"
        " FROM mktOrders WHERE regionID = %u AND typeID = %u AND volRemaining > 0"
        "  AND ((ownerID BETWEEN %u AND %u AND isCorp = 0) OR isCorp = 1)"
        " ORDER BY bid, price, issued, orderID", regionID, typeID, minCharacter, maxCharacter))
    {
        codelog(MARKET__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    while (res.GetRow(row)) {
        PlayerOrder o = PlayerOrder();
        o.orderID = row.GetUInt(0);
        o.ownerID = row.GetUInt(1);
        o.typeID = typeID;
        o.stationID = row.GetUInt(2);
        o.systemID = row.GetUInt(3);
        o.regionID = regionID;
        o.bid = row.GetBool(4);
        o.price = (Cents)std::llround(row.GetDouble(5) * 100.0);
        o.qty = row.GetUInt(6);
        o.minVolume = row.GetUInt(7);
        o.range = (int32)row.GetInt64(8);       // -1 (station), or 4294967295 before the column was signed
        o.issued = ToUnix(row.GetInt64(9));
        (o.bid ? bids : asks).push_back(o);
    }
    // bids dearest first
    std::stable_sort(bids.begin(), bids.end(), [](const PlayerOrder& a, const PlayerOrder& b) { return a.price > b.price; });
}

uint32 MarketSimDB::CountLegacyOrders()
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT COUNT(*) FROM mktOrders o LEFT JOIN mktSimOrder s ON s.orderID = o.orderID"
        " WHERE s.orderID IS NULL AND o.isCorp = 0 AND NOT (o.ownerID BETWEEN %u AND %u)", minCharacter, maxCharacter)
    or !res.GetRow(row))
        return 0;
    return row.GetUInt(0);
}

uint32 MarketSimDB::PurgeLegacyOrders(std::set<std::pair<uint32, uint16>>& touched)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT o.orderID, o.regionID, o.typeID FROM mktOrders o LEFT JOIN mktSimOrder s ON s.orderID = o.orderID"
        " WHERE s.orderID IS NULL AND o.isCorp = 0 AND NOT (o.ownerID BETWEEN %u AND %u)", minCharacter, maxCharacter))
        return 0;
    std::vector<uint32> ids;
    while (res.GetRow(row)) {
        ids.push_back(row.GetUInt(0));
        touched.insert(std::make_pair(row.GetUInt(1), (uint16)row.GetUInt(2)));
    }
    for (size_t i = 0; i < ids.size(); i += Chunk) {
        std::string in;
        for (size_t k = i; (k < ids.size()) and (k < i + Chunk); ++k)
            in += (k > i ? "," : "") + std::to_string(ids[k]);
        Exec("DELETE FROM mktOrders WHERE orderID IN (" + in + ")");
    }
    return (uint32)ids.size();
}

/************************************************************************************************************************
 * history
 */

bool MarketSimDB::HasHistoryBefore(uint32 regionID, uint16 typeID, int64 fileTime)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM mktHistory WHERE regionID = %u AND typeID = %u AND historyDate < %lli",
                            regionID, typeID, fileTime) or !res.GetRow(row))
        return true;
    return row.GetUInt(0) > 0;
}

void MarketSimDB::InsertBackfill(const std::vector<HistoryRow>& rows)
{
    std::vector<std::string> values;
    for (auto& r : rows) {
        double avg((double)r.sumValue / (double)std::max<Units>(1, r.volume) / 100.0);
        values.push_back(Fmt("(%u,%u,%lld,%s,%s,%.2f,%lld,%u)", r.regionID, r.typeID, (long long)DayToFileTime(r.day), IskText(r.low).c_str(),
                             IskText(r.high).c_str(), avg, (long long)r.volume, r.trades));
    }
    // never over a real day
    InsertRows("INSERT IGNORE INTO mktHistory (regionID, typeID, historyDate, lowPrice, highPrice, avgPrice, volume, orders) VALUES ", values, "");
}

/************************************************************************************************************************
 * phase 3:  the actors
 */

void MarketSimDB::LoadIndustry(StaticData& data)
{
    DBQueryResult res;
    DBResultRow row;
    // killmails:  public losses per hull and security band, and the fits lost with them (PLAN 4.2)
    if (sDatabase.RunQuery(res, "SELECT hullTypeID, secBand, lossesPerDay FROM mktCalLoss ORDER BY hullTypeID, secBand")) {
        while (res.GetRow(row)) {
            uint8 band((uint8)row.GetUInt(1));
            if (band < SecBand::Count)
                data.losses[(uint16)row.GetUInt(0)][band] += row.GetFloat(2);
        }
    }
    if (sDatabase.RunQuery(res, "SELECT hullTypeID, typeID, qtyPerLoss, destroyedShare FROM mktCalFit ORDER BY hullTypeID, typeID"))
        while (res.GetRow(row))
            data.fits[(uint16)row.GetUInt(0)].push_back(FitItem{(uint16)row.GetUInt(1), row.GetFloat(2), row.GetFloat(3)});
    // blueprint times per run
    if (sDatabase.RunQuery(res, "SELECT productTypeID, MAX(productionTime) FROM invBlueprintTypes GROUP BY productTypeID")) {
        while (res.GetRow(row)) {
            std::map<uint16, TypeData>::iterator itr = data.types.find((uint16)row.GetUInt(0));
            if (itr != data.types.end())
                itr->second.buildSeconds = row.GetDouble(1);
        }
    }
    // the nodes' refineries (PLAN 6.6:  efficiency, and the take at neutral standing)
    for (auto& node : data.nodes) {
        if (!sDatabase.RunQuery(res, "SELECT reprocessingEfficiency, reprocessingStationsTake FROM staStations WHERE stationID = %u", node.stationID)
        or !res.GetRow(row))
            continue;
        node.refineEfficiency = row.GetFloat(0);
        node.refineTake = row.GetFloat(1);
    }
}

void MarketSimDB::LoadActorCorpData(std::map<uint32, std::array<uint32, 3>>& blueprints, std::map<std::pair<uint32, uint32>, uint32>& refineries)
{
    DBQueryResult res;
    DBResultRow row;
    // the blueprints each npc corp sells:  of ships, of modules, charges and drones, of mining gear (mining lasers, strip
    //  miners, mining crystals, barges and exhumers)
    if (sDatabase.RunQuery(res,
        "SELECT t.corporationID, SUM(g.categoryID = 6), SUM(g.categoryID IN (7, 8, 18)), SUM(p.groupID IN (54, 464, 482, 483, 463, 543))"
        " FROM crpNPCCorporationTrades t"
        " JOIN invBlueprintTypes b ON b.blueprintTypeID = t.typeID"
        " JOIN invTypes p ON p.typeID = b.productTypeID"
        " JOIN invGroups g ON g.groupID = p.groupID"
        " GROUP BY t.corporationID"))
    {
        while (res.GetRow(row))
            blueprints[row.GetUInt(0)] = std::array<uint32, 3>{{row.GetUInt(1), row.GetUInt(2), row.GetUInt(3)}};
    }
    // the stations with a full refinery each corp owns, per region
    if (sDatabase.RunQuery(res,
        "SELECT regionID, corporationID, COUNT(*) FROM staStations WHERE reprocessingEfficiency >= 0.5 GROUP BY regionID, corporationID"))
    {
        while (res.GetRow(row))
            refineries[std::make_pair(row.GetUInt(0), row.GetUInt(1))] = row.GetUInt(2);
    }
}

void MarketSimDB::LoadJobs(std::vector<JobRow>& into)
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res,
        "SELECT jobID, actorID, kind, assemblyLineID, stationID, outputTypeID, runs, qtyOut, startTime, endTime FROM mktSimJob ORDER BY jobID"))
    {
        while (res.GetRow(row)) {
            JobRow j = JobRow();
            j.jobID = row.GetUInt(0);
            j.actorID = row.GetUInt(1);
            j.kind = (uint8)row.GetUInt(2);
            j.lineID = row.GetUInt(3);
            j.stationID = row.GetUInt(4);
            j.typeID = (uint16)row.GetUInt(5);
            j.runs = row.GetUInt(6);
            j.qtyOut = row.GetInt64(7);
            j.start = row.GetInt64(8);
            j.end = row.GetInt64(9);
            into.push_back(j);
        }
    }
}

bool MarketSimDB::FlushJobs(World& world)
{
    // the jobs are few (hundreds to a few thousand):  rewritten whole when they changed
    if (!world.TakeJobsChanged())
        return true;
    bool ok(Exec("DELETE FROM mktSimJob"));
    std::vector<std::string> rows;
    for (auto& cur : world.Jobs()) {
        const SimJob& j(cur.second);
        const NodeData* node(world.Node(j.nodeID));
        rows.push_back(Fmt("(%u,%u,%u,%u,%u,%u,%u,%lld,%lld,%lld,0)", j.jobID, j.actorID, j.activity, j.lineID,
                           (j.stationID != 0) ? j.stationID : ((node != nullptr) ? node->stationID : 0), j.typeID, j.runs, (long long)j.qtyOut, (long long)j.start,
                           (long long)j.end));
    }
    ok = InsertRows("INSERT INTO mktSimJob (jobID, actorID, kind, assemblyLineID, stationID, outputTypeID, runs, qtyOut, startTime,"
                    " endTime, state) VALUES ", rows, "") and ok;
    if (!ok)
        world.MarkJobsChanged();
    return ok;
}

/************************************************************************************************************************
 * phase 6:  the advanced chains
 */

void MarketSimDB::LoadChains(StaticData& data)
{
    DBQueryResult res;
    DBResultRow row;
    auto known = [&data](uint32 typeID) { return data.types.find((uint16)typeID) != data.types.end(); };
    // moonMiningAmount:  the units of a harvesting batch and of a reaction's quantity (TowerSE::ProcessUnits)
    if (sDatabase.RunQuery(res, "SELECT typeID, IFNULL(valueFloat, valueInt) FROM dgmTypeAttributes WHERE attributeID = 726"))
        while (res.GetRow(row))
            if (row.GetDouble(1) > 0.0)
                data.moonAmounts[(uint16)row.GetUInt(0)] = row.GetDouble(1);
    auto amount = [&data](uint16 typeID) {
        std::map<uint16, double>::const_iterator itr = data.moonAmounts.find(typeID);
        return (itr == data.moonAmounts.end()) ? 1.0 : std::max(1.0, itr->second);
    };

    // the bills of T2, T3 and components:  invTypeMaterials less the recycled requirement's materials, and the extras
    std::map<uint32, std::vector<std::pair<uint16, double>>> mats;
    if (sDatabase.RunQuery(res, "SELECT typeID, materialTypeID, quantity FROM invTypeMaterials WHERE quantity > 0 ORDER BY typeID, materialTypeID"))
        while (res.GetRow(row))
            mats[row.GetUInt(0)].push_back(std::make_pair((uint16)row.GetUInt(1), (double)row.GetUInt(2)));
    struct Req { uint16 typeID; double qty, damage; bool recycle; };
    std::map<uint32, std::vector<Req>> reqs;
    if (sDatabase.RunQuery(res,
        "SELECT r.typeID, r.requiredTypeID, r.quantity, r.damagePerJob, r.extra FROM ramTypeRequirements r"
        " JOIN invTypes i ON i.typeID = r.requiredTypeID JOIN invGroups g ON g.groupID = i.groupID"
        " WHERE r.activityID = 1 AND g.categoryID <> 16 ORDER BY r.typeID, r.requiredTypeID"))
        while (res.GetRow(row))
            reqs[row.GetUInt(0)].push_back(Req{(uint16)row.GetUInt(1), (double)row.GetUInt(2), row.GetDouble(3), row.GetUInt(4) != 0});
    std::map<uint16, uint16> blueprintOf;
    if (sDatabase.RunQuery(res, "SELECT productTypeID, blueprintTypeID, wasteFactor FROM invBlueprintTypes ORDER BY productTypeID, blueprintTypeID")) {
        while (res.GetRow(row)) {
            std::map<uint16, TypeData>::const_iterator t = data.types.find((uint16)row.GetUInt(0));
            if ((t == data.types.end()) or (blueprintOf.count(t->first) > 0))
                continue;
            blueprintOf[t->first] = (uint16)row.GetUInt(1);
            uint8 cls(t->second.simClass);
            if ((cls != SimClass::MfgT2) and (cls != SimClass::MfgT3) and (cls != SimClass::Component))
                continue;
            double portion((double)std::max<uint32>(1, t->second.portionSize));
            std::map<uint16, double> base;
            for (auto& m : mats[t->first])
                base[m.first] += m.second;
            ChainBill bill;
            bill.wasteFactor = row.GetDouble(2) / 100.0;
            for (auto& r : reqs[row.GetUInt(1)]) {
                if (r.recycle)
                    for (auto& m : mats[r.typeID])
                        if (base.count(m.first) > 0)
                            base[m.first] = std::max(0.0, base[m.first] - m.second * r.qty);
                if (r.damage > 0.0)
                    bill.extra.push_back(std::make_pair(r.typeID, r.qty * r.damage / portion));
            }
            for (auto& b : base)
                if (b.second > 0.0)
                    bill.base.push_back(std::make_pair(b.first, b.second / portion));
            if (!bill.base.empty() or !bill.extra.empty())
                data.bills[t->first] = bill;
            if (cls == SimClass::MfgT2)
                data.blueprints[t->first] = blueprintOf[t->first];
        }
    }

    /* invention (RamMethods::GetInventionData):  a T2 blueprint with a chance, invented from the blueprint of its T1 parent;
     *  the T1 blueprint's datacores (the data interface is not used up:  FactoryDB::GetRAMRequirements), its decryptor group,
     *  the runs of a max-run T1 copy (EvEMath::RAM::InventionRuns) */
    struct InvReq { std::vector<std::pair<uint16, double>> inputs; uint16 decryptorGroup = 0; };
    std::map<uint32, InvReq> invReqs;
    if (sDatabase.RunQuery(res,
        "SELECT r.typeID, r.requiredTypeID, r.quantity * r.damagePerJob, i.groupID, IFNULL(a.valueInt, a.valueFloat)"
        " FROM ramTypeRequirements r JOIN invTypes i ON i.typeID = r.requiredTypeID JOIN invGroups g ON g.groupID = i.groupID"
        " LEFT JOIN dgmTypeAttributes a ON a.typeID = r.requiredTypeID AND a.attributeID = 1115"
        " WHERE r.activityID = 8 AND g.categoryID <> 16 ORDER BY r.typeID, r.requiredTypeID"))
    {
        while (res.GetRow(row)) {
            InvReq& ir(invReqs[row.GetUInt(0)]);
            if (row.GetUInt(3) == Chains::Group::DataInterface) {
                ir.decryptorGroup = row.IsNull(4) ? 0 : (uint16)row.GetDouble(4);
                continue;
            }
            if (row.GetDouble(2) > 0.0)
                ir.inputs.push_back(std::make_pair((uint16)row.GetUInt(1), row.GetDouble(2)));
        }
    }
    uint32 inventions(0);
    if (sDatabase.RunQuery(res,
        "SELECT b2.blueprintTypeID, b2.chanceOfRE, b2.maxProductionLimit, b1.blueprintTypeID, b1.researchTechTime, b1.maxProductionLimit"
        " FROM invBlueprintTypes b2"
        " JOIN invMetaTypes m ON m.typeID = b2.productTypeID AND m.metaGroupID = 2"
        " JOIN invBlueprintTypes b1 ON b1.productTypeID = m.parentTypeID"
        " WHERE b2.chanceOfRE > 0 ORDER BY b2.blueprintTypeID, b1.blueprintTypeID"))
    {
        std::set<uint32> seen;
        while (res.GetRow(row)) {
            if (!seen.insert(row.GetUInt(0)).second)
                continue;
            std::map<uint32, InvReq>::const_iterator ir = invReqs.find(row.GetUInt(3));
            if ((ir == invReqs.end()) or ir->second.inputs.empty())
                continue;
            uint32 t1Max(std::max<uint32>(1, row.GetUInt(5))), t2Max(std::max<uint32>(1, row.GetUInt(2)));
            Recipe r;
            r.kind = RecipeKind::Invention;
            r.id = (uint16)row.GetUInt(3);
            r.inputs = ir->second.inputs;
            r.outputs.push_back(std::make_pair((uint16)row.GetUInt(0),
                                               (double)EvEMath::RAM::InventionRuns((int16)std::min<uint32>(t1Max, 32767), t1Max, t2Max, 0)));
            r.seconds = row.GetDouble(4);
            r.chance = row.GetDouble(1);
            r.decryptorGroup = ir->second.decryptorGroup;
            r.maxRuns = t2Max;
            data.recipes.push_back(r);
            ++inventions;
        }
    }
    // decryptors:  their chance multiplier, runs, ME and PE modifiers
    if (sDatabase.RunQuery(res,
        "SELECT a.typeID, i.groupID,"
        " MAX(IF(a.attributeID = 1112, IFNULL(a.valueFloat, a.valueInt), NULL)), MAX(IF(a.attributeID = 1124, IFNULL(a.valueFloat, a.valueInt), NULL)),"
        " MAX(IF(a.attributeID = 1113, IFNULL(a.valueFloat, a.valueInt), NULL)), MAX(IF(a.attributeID = 1114, IFNULL(a.valueFloat, a.valueInt), NULL))"
        " FROM dgmTypeAttributes a JOIN invTypes i ON i.typeID = a.typeID"
        " WHERE a.attributeID IN (1112, 1113, 1114, 1124) GROUP BY a.typeID, i.groupID ORDER BY a.typeID"))
    {
        while (res.GetRow(row)) {
            if (!known(row.GetUInt(0)))
                continue;
            Decryptor d;
            d.typeID = (uint16)row.GetUInt(0);
            d.groupID = (uint16)row.GetUInt(1);
            d.chance = row.IsNull(2) ? 1.0 : row.GetDouble(2);
            d.runs = row.IsNull(3) ? 0.0 : row.GetDouble(3);
            d.me = row.IsNull(4) ? 0.0 : row.GetDouble(4);
            d.pe = row.IsNull(5) ? 0.0 : row.GetDouble(5);
            data.decryptors.push_back(d);
        }
    }

    // reactions (an hour's cycle, quantities x moonMiningAmount) and planetary schematics (a cycle of cycleTime)
    uint32 reactions(0), schematics(0);
    if (sDatabase.RunQuery(res, "SELECT reactionTypeID, input, typeID, quantity FROM invTypeReactions ORDER BY reactionTypeID, input, typeID")) {
        Recipe r;
        auto done = [&]() {
            if ((r.id != 0) and !r.inputs.empty() and !r.outputs.empty()) {
                data.recipes.push_back(r);
                ++reactions;
            }
            r = Recipe();
        };
        while (res.GetRow(row)) {
            if (row.GetUInt(0) != r.id) {
                done();
                r.kind = RecipeKind::Reaction;
                r.id = (uint16)row.GetUInt(0);
                r.seconds = 3600.0;
            }
            uint16 typeID((uint16)row.GetUInt(2));
            double qty((double)row.GetUInt(3) * amount(typeID));
            if (row.GetUInt(1) != 0)
                r.inputs.push_back(std::make_pair(typeID, qty));
            else
                r.outputs.push_back(std::make_pair(typeID, qty));
        }
        done();
    }
    if (sDatabase.RunQuery(res,
        "SELECT s.schematicID, s.cycleTime, m.typeID, m.quantity, CAST(m.isInput AS UNSIGNED)"
        " FROM piSchematics s JOIN piTypeMap m ON m.schematicID = s.schematicID ORDER BY s.schematicID, m.typeID"))
    {
        Recipe r;
        auto done = [&]() {
            if ((r.id != 0) and !r.inputs.empty() and !r.outputs.empty() and (r.seconds > 0.0)) {
                data.recipes.push_back(r);
                ++schematics;
            }
            r = Recipe();
        };
        while (res.GetRow(row)) {
            if (row.GetUInt(0) != r.id) {
                done();
                r.kind = RecipeKind::Planetary;
                r.id = (uint16)row.GetUInt(0);
                r.seconds = row.GetDouble(1);
            }
            std::pair<uint16, double> line((uint16)row.GetUInt(2), (double)row.GetUInt(3));
            if (row.GetUInt(4) != 0)
                r.inputs.push_back(line);
            else
                r.outputs.push_back(line);
        }
        done();
    }

    // the sim's copy of the invention chance must be the server's (EvEMath::RAM::InventionChance)
    double mine(Chains::InventionChance(0.4, 5, 5, 4, 1, 1.2)), server(EvEMath::RAM::InventionChance(0.4f, 5, 5, 4, 1, 1.2f));
    if (std::fabs(mine - server) > 1e-4)
        _log(MARKET__SIM, "Market sim:  its invention chance (%.5f) differs from the server's (%.5f):  SimChains.cpp is out of date.", mine, server);
    _log(MARKET__SIM, "Market sim:  %u inventions, %u decryptors, %u reactions, %u schematics, %u bills.", inventions,
         (uint32)data.decryptors.size(), reactions, schematics, (uint32)data.bills.size());
}

void MarketSimDB::LoadChainCorpData(std::map<uint32, std::array<uint32, 3>>& into)
{
    DBQueryResult res;
    DBResultRow row;
    // research fields (the Hi-Tech corps' R&D agents)
    if (sDatabase.RunQuery(res, "SELECT corporationID, COUNT(*) FROM crpNPCCorporationResearchFields GROUP BY corporationID"))
        while (res.GetRow(row))
            into[row.GetUInt(0)][0] = row.GetUInt(1);
    // reaction blueprints (category 24) and planetary goods (categories 41 to 43) traded
    if (sDatabase.RunQuery(res,
        "SELECT t.corporationID, SUM(g.categoryID = 24), SUM(g.categoryID IN (41, 42, 43)) FROM crpNPCCorporationTrades t"
        " JOIN invTypes i ON i.typeID = t.typeID JOIN invGroups g ON g.groupID = i.groupID GROUP BY t.corporationID"))
    {
        while (res.GetRow(row)) {
            into[row.GetUInt(0)][1] = row.GetUInt(1);
            into[row.GetUInt(0)][2] = row.GetUInt(2);
        }
    }
}
