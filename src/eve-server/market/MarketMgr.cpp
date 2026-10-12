
 /**
  * @name MarketMgr.cpp
  *   singleton object for storing, manipulating and managing in-game market data
  *   this mgr keeps track of market data without abusing the db on every call. (vs old system)
  *   this mgr will also track mineral pricing, taking monthly averages, then updating base prices accordingly
  *
  *
  * @Author:         Allan
  * @date:          19Dec17
  *
  */

#include "Client.h"
#include "EVE_Mail.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "StatisticMgr.h"
#include "account/AccountDB.h"
#include "account/AccountService.h"
#include "cache/ObjCacheService.h"
#include "character/CharacterDB.h"
#include "corporation/CorporationDB.h"
#include "inventory/InventoryItem.h"
#include "inventory/ItemType.h"
#include "map/MapData.h"
#include "market/MarketMatch.h"
#include "market/MarketMgr.h"
#include "station/StationDataMgr.h"

/*
 * MARKET__ERROR
 * MARKET__WARNING
 * MARKET__MESSAGE
 * MARKET__DEBUG
 * MARKET__TRACE
 * MARKET__DB_ERROR
 * MARKET__DB_TRACE
 */

int32 Market::GetStationDistance(Client* pClient, uint32 stationID)
{
    if (pClient->GetStationID() == stationID)
        return Range::Station;
    if (sDataMgr.GetStationRegion(stationID) != pClient->GetRegionID())
        return Range::Region;
    int32 jumps = sMapData.GetJumpCount(pClient->GetSystemID(), sDataMgr.GetStationSystem(stationID));
    if (jumps < 0)
        return Range::Region;   // no route (within 50 jumps)
    return jumps;               // 0 = same system
}

int32 Market::SkillRangeLimit(uint8 level)
{
    static const int32 jumpsPerSkillLevel[6] = {-1, 0, 5, 10, 20, 50};
    return jumpsPerSkillLevel[(level > 5) ? 5 : level];
}

bool Market::InSkillRange(int32 distance, uint8 level)
{
    if (distance == Range::Region)
        return false;
    return (distance <= SkillRangeLimit(level));
}

bool Market::RangeCovers(int32 orderRange, uint32 orderStationID, uint32 targetStationID)
{
    if (orderStationID == targetStationID)
        return true;
    if (orderRange == Range::Station)
        return false;
    uint32 orderSystemID(sDataMgr.GetStationSystem(orderStationID)), targetSystemID(sDataMgr.GetStationSystem(targetStationID));
    if (orderSystemID == targetSystemID)
        return true;
    if ((orderRange <= Range::System) or (sDataMgr.GetStationRegion(orderStationID) != sDataMgr.GetStationRegion(targetStationID)))
        return false;
    if (orderRange >= Range::Region)
        return true;
    return (sMapData.GetJumpCount(orderSystemID, targetSystemID, (uint8)EvE::min(orderRange, 255)) >= 0);
}

Market::Party Market::Party::ForClient(Client* pClient, bool useCorp, uint16 accountKey)
{
    Party party = Party();
    party.ownerID       = (useCorp ? pClient->GetCorporationID() : pClient->GetCharacterID());
    party.charID        = pClient->GetCharacterID();
    party.isCorp        = useCorp;
    party.accountKey    = (useCorp ? accountKey : (uint16)Account::KeyType::Cash);
    party.client        = pClient;
    party.simActorID    = 0;
    return party;
}

Market::Party Market::Party::ForSim(uint32 actorID, uint32 corpID)
{
    Party party = Party();
    party.ownerID       = corpID;
    party.charID        = corpID;   // the transaction views show the corp as the counterparty
    party.isCorp        = false;    // not a player corp wallet
    party.accountKey    = Account::KeyType::Cash;
    party.client        = nullptr;
    party.simActorID    = actorID;
    return party;
}

namespace {
    // the matching rules (MarketMatch.h) on the server's map
    struct RangeCoversFn {
        bool operator()(int32 range, uint32 fromStationID, uint32 toStationID) const {
            return Market::RangeCovers(range, fromStationID, toStationID);
        }
    };
    RangeCoversFn s_rangeCovers;

    Market::Match::Resting ToResting(const Market::OrderInfo& oInfo)
    {
        Market::Match::Resting r = Market::Match::Resting();
        r.orderID   = oInfo.orderID;
        r.stationID = oInfo.stationID;
        r.range     = oInfo.orderRange;
        r.quantity  = oInfo.quantity;
        r.minVolume = oInfo.minVolume;
        r.price     = oInfo.price;
        return r;
    }

    // SCC transaction tax on money for a character's skills (online or not), rounded to the cent
    double SalesTaxFor(Client* pClient, uint32 charID, double money)
    {
        uint8 accountingLevel(0), taxEvasionLevel(0);
        if (pClient == nullptr)
            pClient = sEntityList.FindClientByCharID(charID);
        if (pClient != nullptr) {
            accountingLevel = pClient->GetChar()->GetSkillLevel(EvESkill::Accounting);
            taxEvasionLevel = pClient->GetChar()->GetSkillLevel(EvESkill::TaxEvasion);
        } else if (IsCharacterID(charID)) {
            accountingLevel = CharacterDB::GetSkillLevel(charID, EvESkill::Accounting);
            taxEvasionLevel = CharacterDB::GetSkillLevel(charID, EvESkill::TaxEvasion);
        }
        double tax(EvEMath::Market::SalesTax(sConfig.market.salesTax, accountingLevel, taxEvasionLevel) * money);
        return std::round(tax * 100.0) / 100.0;
    }
}

MarketMgr::MarketMgr()
: m_cache(nullptr),
m_marketGroups(nullptr),
m_historyDay(0),
m_indexReady(false)
{
}

MarketMgr::~MarketMgr()
{
    //PyDecRef(m_marketGroups);
}

void MarketMgr::Close()
{
    /** @todo put a save method here which will save anything changed before shutdown */
    PyDecRef(m_marketGroups);
    sLog.Warning("        MarketMgr", "Market Manager has been closed." );
}

int MarketMgr::Initialize(EVEServiceManager& svc)
{
    m_cache = svc.Lookup <ObjCacheService>("objectCaching");
    m_historyDay = MarketDB::GetUpdateTime();

    Populate();
    sLog.Blue("        MarketMgr", "Market Manager Initialized.");
    return 1;
}

void MarketMgr::Populate()
{
    double start = GetTimeMSeconds();
    m_marketGroups = MarketDB::GetMarketGroups();

    // order expiry and the history rollup need the station and map data, which load after this.  the first Process() tic runs them

    sLog.Cyan("        MarketMgr", "Market Manager rolls up price history daily and keeps %u days.", sConfig.market.HistoryDays);
    sLog.Blue("        MarketMgr", "Market Manager loaded in %.3fms.", (GetTimeMSeconds() - start));
}

void MarketMgr::GetInfo()
{
    /* get info in current market data? */
}

void MarketMgr::Process()
{
    ProcessOrderExpiry();

    if (NeedsUpdate())
        UpdatePriceHistory();
    if (!m_indexReady)
        UpdateMaterialIndex();
}

void MarketMgr::SystemStartup(SolarSystemData& data)
{
    // not used yet.
}

void MarketMgr::SystemShutdown(SolarSystemData& data)
{
    // not used yet.
}

void MarketMgr::UpdatePriceHistory()
{
    double start = GetTimeMSeconds();
    int64 midnight = GetMidnight();
    int64 oldest = midnight - (EvE::Time::Day * sConfig.market.HistoryDays);
    int64 day = m_historyDay;
    if (day < oldest) {
        // nothing rolled up yet (or not for longer than we keep).  start at the first day with transactions
        int64 firstTx = MarketDB::GetFirstTransactionTime();
        if (firstTx == 0) {
            day = midnight;
        } else {
            day = EvE::max(oldest, firstTx - (firstTx % EvE::Time::Day));
        }
    }

    uint16 count(0);
    for (; (day + EvE::Time::Day) <= midnight; day += EvE::Time::Day) {
        MarketDB::RollupHistory(day, day + EvE::Time::Day);
        ++count;
    }

    m_historyDay = midnight;
    MarketDB::SetUpdateTime(m_historyDay);

    // transactions are kept as long as the history is served
    if (sConfig.market.DeleteOldTransactions)
        MarketDB::DeleteTransactionsBefore(oldest);

    _log(MARKET__MESSAGE, "UpdatePriceHistory - rolled up %u days in %.3fms.", count, (GetTimeMSeconds() - start));

    // insurance values follow the market daily
    UpdateMaterialIndex();
}

void MarketMgr::UpdateMaterialIndex()
{
    double start = GetTimeMSeconds();
    m_indexReady = true;

    // the materials (and extra components, eg. T1 hulls of T2 ships) used to build ships.  collected once
    if (m_indexTypes.empty()) {
        std::map<uint16, Inv::TypeData> ships;
        MarketDB::GetShipIDs(ships);
        for (auto cur : ships) {
            std::vector<EvERam::RamMaterials> matls;
            sDataMgr.GetRamMaterials(cur.first, matls);
            for (auto& matl : matls)
                m_indexTypes.insert(matl.materialTypeID);
            uint16 bpTypeID(sDataMgr.GetBlueprintTypeForProduct(cur.first));
            if (bpTypeID == 0)
                continue;
            std::vector<EvERam::RamRequirements> reqs;
            sDataMgr.GetRamRequirements(bpTypeID, reqs);
            for (auto& req : reqs)
                if ((req.activityID == EvERam::Activity::Manufacturing) and (req.damagePerJob > 0) and !sDataMgr.IsSkillTypeID(req.requiredTypeID))
                    m_indexTypes.insert(req.requiredTypeID);
        }
    }

    m_materialIndex.clear();
    m_insuranceValue.clear();
    if (!sConfig.market.InsuranceUseMaterialIndex or m_indexTypes.empty())
        return;

    std::string types;
    for (auto cur : m_indexTypes)
        types += std::to_string(cur) + ",";
    types.pop_back();

    // sell side rows (one per trade) of the window, per type cheapest first
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT typeID, price, quantity FROM mktTransactions"
        " WHERE transactionType = 0 AND transactionDate > %lli AND typeID IN (%s)"
        " ORDER BY typeID, price",
        GetFileTimeNow() - (EvE::Time::Day * sConfig.market.InsuranceIndexDays), types.c_str()))
    {
        codelog(MARKET__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    std::map<uint16, std::vector<std::pair<double, uint32>>> trades;
    DBResultRow row;
    while (res.GetRow(row))
        trades[row.GetUInt(0)].push_back(std::make_pair(row.GetDouble(1), row.GetUInt(2)));

    // trimmed mean:  the cheapest and dearest 10% of the trades are dropped, the rest weighted by quantity
    for (auto& cur : trades) {
        size_t count(cur.second.size());
        if (count < sConfig.market.InsuranceIndexMinTrades)
            continue;
        size_t trim(count / 10);
        double value(0.0), qty(0.0);
        for (size_t i = trim; i < count - trim; ++i) {
            value += cur.second[i].first * cur.second[i].second;
            qty += cur.second[i].second;
        }
        if (qty > 0)
            m_materialIndex[cur.first] = value / qty;
    }

    _log(MARKET__MESSAGE, "UpdateMaterialIndex - %u of %u materials priced from the market in %.3fms.",
            (uint32)m_materialIndex.size(), (uint32)m_indexTypes.size(), (GetTimeMSeconds() - start));
}

double MarketMgr::GetMaterialIndexPrice(uint16 typeID)
{
    std::map<uint16, double>::const_iterator itr = m_materialIndex.find(typeID);
    if (itr != m_materialIndex.end())
        return itr->second;
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr)
        return 0.0;
    return pType->basePrice();
}

double MarketMgr::GetInsuranceValue(uint16 shipTypeID)
{
    const ItemType* pType(sItemFactory.GetType(shipTypeID));
    if ((pType == nullptr) or (pType->categoryID() != EVEDB::invCategories::Ship))
        return 0.0;
    // rookie ships and capsules can't be insured
    if ((pType->groupID() == EVEDB::invGroups::Rookieship) or (pType->groupID() == EVEDB::invGroups::Capsule))
        return 0.0;
    if (!sConfig.market.InsuranceUseMaterialIndex)
        return pType->basePrice();

    if (!m_indexReady)
        UpdateMaterialIndex();
    std::map<uint16, double>::const_iterator itr = m_insuranceValue.find(shipTypeID);
    if (itr != m_insuranceValue.end())
        return itr->second;

    // materials of the ship, plus the components a blueprint consumes (T2 components, the T1 hull)
    double value(0.0);
    std::vector<EvERam::RamMaterials> matls;
    sDataMgr.GetRamMaterials(shipTypeID, matls);
    for (auto& matl : matls)
        value += matl.quantity * GetMaterialIndexPrice(matl.materialTypeID);
    uint16 bpTypeID(sDataMgr.GetBlueprintTypeForProduct(shipTypeID));
    if (bpTypeID != 0) {
        std::vector<EvERam::RamRequirements> reqs;
        sDataMgr.GetRamRequirements(bpTypeID, reqs);
        for (auto& req : reqs)
            if ((req.activityID == EvERam::Activity::Manufacturing) and (req.damagePerJob > 0) and !sDataMgr.IsSkillTypeID(req.requiredTypeID))
                value += req.quantity * req.damagePerJob * GetMaterialIndexPrice(req.requiredTypeID);
    }
    if (value <= 0.0)
        value = pType->basePrice();

    // ship type multiplier (CCP never published theirs)
    EvERam::bpTypeData bpData = EvERam::bpTypeData();
    if (pType->groupID() == EVEDB::invGroups::StrategicCruiser) {
        value *= sConfig.market.InsuranceMultiplierT3;
    } else if (sDataMgr.GetBpDataForItem(shipTypeID, bpData) and (bpData.techLevel == 2)) {
        value *= sConfig.market.InsuranceMultiplierT2;
    } else {
        value *= sConfig.market.InsuranceMultiplierT1;
    }

    m_insuranceValue[shipTypeID] = value;
    return value;
}

    /*DBColumnTypeMap colmap;
     *    colmap["historyDate"] = DBTYPE_FILETIME;
     *    colmap["lowPrice"] = DBTYPE_CY;
     *    colmap["highPrice"] = DBTYPE_CY;
     *    colmap["avgPrice"] = DBTYPE_CY;
     *    colmap["volume"] = DBTYPE_I8;
     *    colmap["orders"] = DBTYPE_I4;
     */

/* the client (marketsvc.GetPriceHistory) wants 'old' = one row per day before today, oldest first, and 'new' = today's row.
 *  both are cached method calls.  the client keeps old until utc midnight and new for up to 3 hours */
PyRep *MarketMgr::GetNewPriceHistory(uint32 regionID, uint32 typeID) {
    int64 midnight(GetMidnight());
    std::string method_name ("GetNewHistory_");
    method_name += std::to_string(regionID);
    method_name += "_";
    method_name += std::to_string(typeID);
    ObjectCachedMethodID method_id("marketProxy", method_name.c_str());
    std::map<std::string, int64>::iterator itr = m_historyCacheDay.find(method_name);
    if ((itr != m_historyCacheDay.end()) and (itr->second != midnight))
        m_cache->InvalidateCache(method_id);
    //check to see if this method is in the cache already.
    if (!m_cache->IsCacheLoaded(method_id)) {
        //this method is not in cache yet, load up the contents and cache it
        PyRep* result = MarketDB::GetHistoryToday(regionID, typeID, midnight);
        if (result == nullptr) {
            _log(MARKET__DB_ERROR, "Failed to load cache, generating empty contents.");
            result = PyStatic.NewNone();
        }
        m_cache->GiveCache(method_id, &result);
        m_historyCacheDay[method_name] = midnight;
    }

    //now we know its in the cache one way or the other, so build a
    //cached object cached method call result.
    PyRep* result = m_cache->MakeObjectCachedMethodCallResult(method_id, "utcmidnight_or_3hours");

    if (is_log_enabled(MARKET__DB_TRACE))
        result->Dump(MARKET__DB_TRACE, "    ");
    return result;
}

PyRep *MarketMgr::GetOldPriceHistory(uint32 regionID, uint32 typeID) {
    // yesterday has to be in mktHistory before the day's list is built
    if (NeedsUpdate())
        UpdatePriceHistory();

    int64 midnight(GetMidnight());
    std::string method_name ("GetOldHistory_");
    method_name += std::to_string(regionID);
    method_name += "_";
    method_name += std::to_string(typeID);
    ObjectCachedMethodID method_id("marketProxy", method_name.c_str());
    std::map<std::string, int64>::iterator itr = m_historyCacheDay.find(method_name);
    if ((itr != m_historyCacheDay.end()) and (itr->second != midnight))
        m_cache->InvalidateCache(method_id);
    //check to see if this method is in the cache already.
    if (!m_cache->IsCacheLoaded(method_id)) {
        //this method is not in cache yet, load up the contents and cache it
        PyRep* result = MarketDB::GetHistoryOld(regionID, typeID, midnight - (EvE::Time::Day * sConfig.market.HistoryDays), midnight);
        if (result == nullptr) {
            _log(MARKET__DB_ERROR, "Failed to load cache, generating empty contents.");
            result = PyStatic.NewNone();
        }
        m_cache->GiveCache(method_id, &result);
        m_historyCacheDay[method_name] = midnight;
    }

    //now we know its in the cache one way or the other, so build a
    //cached object cached method call result.
    PyRep* result = m_cache->MakeObjectCachedMethodCallResult(method_id, "utcmidnight");

    if (is_log_enabled(MARKET__DB_TRACE))
        result->Dump(MARKET__DB_TRACE, "    ");
    return result;
}

void MarketMgr::InvalidateHistoryCache(uint32 regionID, uint32 typeID)
{
    std::string method_name ("GetNewHistory_");
    method_name += std::to_string(regionID);
    method_name += "_";
    method_name += std::to_string(typeID);
    ObjectCachedMethodID method_id("marketProxy", method_name.c_str());
    m_cache->InvalidateCache(method_id);
}

void MarketMgr::SendOnOwnOrderChanged(Client* pClient, uint32 orderID, uint8 action, bool isCorp/*false*/, PyRep* order/*nullptr*/) {
    if (pClient == nullptr) {
        // order is consumed (owner offline)
        PySafeDecRef(order);
        return;
    }
    Notify_OnOwnOrderChanged ooc;
    if (order != nullptr) {
        ooc.order = order;
    } else {
        ooc.order = MarketDB::GetOrderRow(orderID);
        if (ooc.order == nullptr)
            return;
    }

    switch (action) {
        case Market::Action::Add:
            ooc.reason = "Add";
            break;
        case Market::Action::Modify:
            ooc.reason = "Modify";
            break;
        case Market::Action::Expiry:
            ooc.reason = "Expiry";
            break;
    }

    ooc.isCorp = isCorp;
    PyTuple* tmp = ooc.Encode();
    // send journal blink and call 'self.RefreshOrders()' in client
    if (isCorp) {
        sEntityList.CorpNotify(pClient->GetCorporationID(), Notify::Types::MarketOrder, "OnOwnOrderChanged", "*corpid&corprole", tmp);
    } else {
        pClient->SendNotification("OnOwnOrderChanged", "clientID", &tmp);
    }
}

void MarketMgr::NotifyOrderOwner(const Market::OrderInfo& oInfo, uint8 action, PyRep* order/*nullptr*/)
{
    if (oInfo.isCorp and IsPlayerCorp(oInfo.ownerID)) {
        Notify_OnOwnOrderChanged ooc;
        ooc.order = ((order != nullptr) ? order : MarketDB::GetOrderRow(oInfo.orderID));
        if (ooc.order == nullptr)
            return;
        ooc.reason = ((action == Market::Action::Add) ? "Add" : ((action == Market::Action::Modify) ? "Modify" : "Expiry"));
        ooc.isCorp = true;
        // the corp's traders and accountants
        sEntityList.CorpNotify(oInfo.ownerID, Notify::Types::MarketOrder, "OnOwnOrderChanged", "*corpid&corprole", ooc.Encode());
        return;
    }

    Client* pClient(nullptr);
    if (IsCharacterID(oInfo.ownerID))
        pClient = sEntityList.FindClientByCharID(oInfo.ownerID);
    SendOnOwnOrderChanged(pClient, oInfo.orderID, action, false, order);
}

void MarketMgr::InvalidateOrdersCache(uint32 regionID, uint32 typeID) {
    std::string method_name ("GetOrders_");
    method_name += std::to_string(regionID);
    method_name += "_";
    method_name += std::to_string(typeID);
    ObjectCachedMethodID method_id("marketProxy", method_name.c_str());
    this->m_cache->InvalidateCache(method_id);
}

bool MarketMgr::CanModifyOrder(uint32 orderID, int64 issued)
{
    if (sConfig.market.OrderModificationDelay == 0)
        return true;
    int64 last(issued);
    std::map<uint32, int64>::const_iterator itr = m_orderModified.find(orderID);
    if ((itr != m_orderModified.end()) and (itr->second > last))
        last = itr->second;
    return ((GetFileTimeNow() - last) >= (int64)sConfig.market.OrderModificationDelay * EvE::Time::Second);
}

void MarketMgr::GetDeliveryLocation(uint32 ownerID, bool isCorp, uint32 memberID, uint32 stationID, uint32& itemOwnerID, uint32& locationID, EVEItemFlags& flag)
{
    itemOwnerID = ownerID;
    locationID = stationID;
    flag = flagHangar;
    if (!isCorp)
        return;

    // corp market items go to the deliveries hangar of the corp's office in that station
    uint32 officeID(stDataMgr.GetOfficeIDForCorp(stationID, ownerID));
    if (officeID != 0) {
        locationID = officeID;
        flag = flagCorpMarket;
        return;
    }

    // no office there.  the member that placed the order gets them rather than losing them
    _log(MARKET__WARNING, "GetDeliveryLocation - corp %u has no office in %u.  Delivering to member %u.", ownerID, stationID, memberID);
    if (IsCharacterID(memberID))
        itemOwnerID = memberID;
}

double MarketMgr::GetWalletBalance(uint32 ownerID, uint16 accountKey)
{
    if (IsCharacterID(ownerID)) {
        Client* pClient(sEntityList.FindClientByCharID(ownerID));
        if (pClient != nullptr)
            return pClient->GetBalance();
        return AccountDB::GetCharBalance(ownerID);
    }
    if (IsPlayerCorp(ownerID))
        return AccountDB::GetCorpBalance(ownerID, accountKey);
    return 0.0;
}

void MarketMgr::CheckFunds(uint32 walletOwnerID, uint16 accountKey, double amount)
{
    double balance(GetWalletBalance(walletOwnerID, accountKey));
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

void MarketMgr::RecordTrade(uint16 typeID, uint32 qty, double price, uint32 stationID,
                            uint32 sellerOwnerID, bool sellerIsCorp, uint32 sellerCharID, uint16 sellerKey,
                            uint32 buyerOwnerID, bool buyerIsCorp, uint32 buyerCharID, uint16 buyerKey, bool recordBuyer/*true*/)
{
    Market::TxData data = Market::TxData();
    data.price          = price;
    data.quantity       = qty;
    data.stationID      = stationID;
    data.regionID       = sDataMgr.GetStationRegion(stationID);
    data.typeID         = typeID;

    // seller's side.  this is also the row the price history uses (one per trade)
    data.isBuy          = Market::Type::Sell;
    data.ownerID        = sellerOwnerID;
    data.isCorp         = sellerIsCorp;
    data.memberID       = sellerCharID;
    data.accountKey     = sellerKey;
    data.clientID       = buyerCharID;
    if (!MarketDB::RecordTransaction(data))
        _log(MARKET__ERROR, "RecordTrade - Failed to record sell side of transaction.");

    // buyer's side
    if (recordBuyer) {
        data.isBuy          = Market::Type::Buy;
        data.ownerID        = buyerOwnerID;
        data.isCorp         = buyerIsCorp;
        data.memberID       = buyerCharID;
        data.accountKey     = buyerKey;
        data.clientID       = sellerCharID;
        if (!MarketDB::RecordTransaction(data))
            _log(MARKET__ERROR, "RecordTrade - Failed to record buy side of transaction.");
    }

    sStatMgr.Add(Stat::iskMarket, price * qty);
    InvalidateHistoryCache(data.regionID, typeID);
}

/** @todo take off market overhead fees */
/*
 *    def BrokersFee(self, stationID, amount, commissionPercentage):
 *        if amount < 0.0:
 *            raise AttributeError('Amount must be positive')
 *        orderValue = float(amount)
 *        station = sm.GetService('ui').GetStation(stationID)
 *        stationOwnerID = None
 *        if station is not None:
 *            stationOwnerID = station.ownerID
 *        factionChar = 0
 *        corpChar = 0
 *        if stationOwnerID:
 *            if util.IsNPC(stationOwnerID):
 *                factionID = sm.GetService('faction').GetFaction(stationOwnerID)
 *                factionChar = sm.GetService('standing').GetStanding(factionID, eve.session.charid) or 0.0
 *            corpChar = sm.GetService('standing').GetStanding(stationOwnerID, eve.session.charid) or 0.0
 *        weightedStanding = (0.7 * factionChar + 0.3 * corpChar) / 10.0
 *        commissionPercentage = commissionPercentage * 2.0 ** (-2 * weightedStanding)
 *        tax = util.KeyVal()
 *        tax.amt = commissionPercentage * orderValue
 *        tax.percentage = commissionPercentage
 *        if tax.amt <= const.mktMinimumFee:
 *            tax.amt = const.mktMinimumFee
 *            tax.percentage = -1.0
 *        return tax
 */

uint32 MarketMgr::SellToBuyOrders(Client* seller, InventoryItemRef iRef, uint32 qty, double minPrice, uint32 stationID, bool useCorp, uint16 accountKey)
{
    std::vector<Market::OrderInfo> orders;
    MarketDB::GetMatchingBuyOrders(sDataMgr.GetStationRegion(stationID), iRef->typeID(), minPrice, orders);

    uint32 remaining(qty);
    for (auto& cur : orders) {
        if ((remaining == 0) or (iRef->quantity() < 1))
            break;
        // market.UseOrderRange:  a buy order takes items from every station its range reaches.
        //  the buy order's minimum volume, unless it has less than that left
        uint32 fill(Market::Match::FillQty(true, ToResting(cur), remaining, minPrice, stationID, Market::Range::Station,
                                           sConfig.market.UseOrderRange, s_rangeCovers));
        if (fill == 0)
            continue;

        InventoryItemRef part(iRef);
        if ((uint32)iRef->quantity() > fill) {
            part = iRef->Split(fill, true);
            if (part.get() == nullptr) {
                _log(MARKET__ERROR, "SellToBuyOrders - Failed to split %u from %s(%u).", fill, iRef->name(), iRef->itemID());
                break;
            }
        }

        bool sold(false);
        try {
            sold = ExecuteBuyOrder(seller, cur, part, fill, stationID, useCorp, accountKey);
        } catch (...) {
            if (part != iRef)
                iRef->Merge(part);
            if (remaining == qty)
                throw;
            break;
        }

        if (sold) {
            remaining -= fill;
        } else if (part != iRef) {
            // nothing happened to the split part.  put it back
            iRef->Merge(part);
        }
    }

    return qty - remaining;
}

uint32 MarketMgr::BuyFromSellOrders(Client* buyer, uint16 typeID, uint32 qty, double maxPrice, uint32 stationID, int32 orderRange, bool useCorp, uint16 accountKey)
{
    std::vector<Market::OrderInfo> orders;
    MarketDB::GetMatchingSellOrders(sDataMgr.GetStationRegion(stationID), typeID, maxPrice, orders);

    uint32 remaining(qty);
    for (auto& cur : orders) {
        if (remaining == 0)
            break;
        // market.UseOrderRange:  the buyer's range around the chosen station
        uint32 fill(Market::Match::FillQty(false, ToResting(cur), remaining, maxPrice, stationID, orderRange,
                                           sConfig.market.UseOrderRange, s_rangeCovers));
        if (fill == 0)
            continue;
        try {
            if (ExecuteSellOrder(buyer, cur, fill, useCorp, accountKey))
                remaining -= fill;
        } catch (...) {
            // out of money.  what was bought so far stays bought
            if (remaining == qty)
                throw;
            break;
        }
    }

    return qty - remaining;
}

/*
 * Does not currently handle aurum transactions, but it would be good to do that
 * in the future.
 */
bool MarketMgr::ExecuteBuyOrder(Client* seller, const Market::OrderInfo& oInfo, InventoryItemRef iRef, uint32 qty, uint32 stationID, bool useCorp, uint16 accountKey) {
    if ((iRef.get() == nullptr) or ((uint32)iRef->quantity() != qty)) {
        _log(MARKET__ERROR, "ExecuteBuyOrder - invalid fill of %u for order #%u.", qty, oInfo.orderID);
        return false;
    }
    return FillAgainstBuyOrder(oInfo, Market::Party::ForClient(seller, useCorp, accountKey), iRef, qty, stationID);
}

bool MarketMgr::ExecuteSellOrder(Client* buyer, const Market::OrderInfo& oInfo, uint32 qty, bool useCorp, uint16 accountKey) {
    return FillAgainstSellOrder(oInfo, Market::Party::ForClient(buyer, useCorp, accountKey), qty);
}

bool MarketMgr::IsPlayerOrder(const Market::OrderInfo& oInfo)
{
    return (IsCharacterID(oInfo.ownerID) or (oInfo.isCorp and IsPlayerCorp(oInfo.ownerID)));
}

bool MarketMgr::IsBackedNpcOrder(const Market::OrderInfo& oInfo)
{
    // nothing backs npc orders yet.  an npc buyer would pay from a wallet that holds nothing and an npc seller would
    //  deliver items that never existed, so those orders are left alone
    return false;
}

bool MarketMgr::FillAgainstBuyOrder(const Market::OrderInfo& oInfo, const Market::Party& seller, InventoryItemRef iRef, uint32 qty, uint32 stationID)
{
    if ((qty == 0) or (qty > oInfo.quantity) or (seller.IsSim() != (iRef.get() == nullptr))
    or ((iRef.get() != nullptr) and ((uint32)iRef->quantity() != qty))) {
        _log(MARKET__ERROR, "FillAgainstBuyOrder - invalid fill of %u for order #%u.", qty, oInfo.orderID);
        return false;
    }

    // buyer is a character, a player corp, or an npc (the market sim's bids; it pays from its ledger, items go to its stock)
    uint32 buyerID(oInfo.ownerID);
    bool isPlayer(IsCharacterID(buyerID));
    bool isCorp(oInfo.isCorp and IsPlayerCorp(buyerID));
    bool npcBuyer(!isPlayer and !isCorp);
    if (npcBuyer and (seller.IsSim() or !IsBackedNpcOrder(oInfo))) {
        _log(MARKET__TRACE, "FillAgainstBuyOrder - npc buy order #%u of %u has nothing behind it.  Skipped.", oInfo.orderID, buyerID);
        return false;
    }
    uint16 buyerKey(isCorp ? oInfo.accountKey : Account::KeyType::Cash);
    uint32 buyerCharID(isCorp ? oInfo.memberID : buyerID);
    // the escrow sits with the owner of the order's station
    uint32 escrowHolderID(stDataMgr.GetOwnerID(oInfo.stationID));

    // trades settle at the resting (buy) order's price.  the seller's price is only its limit
    double money(oInfo.price * qty);
    double newEscrow(oInfo.escrow);

    std::string reason = "DESC:  Buying items in ";
    reason += stDataMgr.GetStationName(stationID).c_str();

    if (!npcBuyer) {
        // margin trading:  the escrow is used first, the rest is paid by the buyer now.  a buyer that can't pay loses the order
        double fromEscrow(EvE::min(oInfo.escrow, money));
        double rest(money - fromEscrow);
        if (rest > 0.005) {
            bool paid(false);
            if (GetWalletBalance(buyerID, buyerKey) >= rest) {
                try {
                    AccountService::TransferFunds(buyerID, escrowHolderID, rest, reason.c_str(), Journal::EntryType::MarketEscrow, oInfo.orderID, buyerKey, Account::KeyType::Escrow);
                    paid = true;
                } catch (...) { }
            }
            if (!paid) {
                _log(MARKET__MESSAGE, "FillAgainstBuyOrder - owner %u can't cover %.2f ISK for order #%u.  Cancelling it.", buyerID, rest, oInfo.orderID);
                CancelOrderInternal(oInfo, "DESC:  Market order cancelled.  Not enough ISK for the margin");
                return false;
            }
        }
        newEscrow = oInfo.escrow - fromEscrow;
    }

    // deliver the items in the seller's station
    if (npcBuyer) {
        // the sold items join the npc buyer's stock (its ledger), so the item itself goes
        iRef->Delete();
    } else {
        uint32 itemOwnerID(0), locationID(0);
        EVEItemFlags flag(flagHangar);
        GetDeliveryLocation(buyerID, isCorp, oInfo.memberID, stationID, itemOwnerID, locationID, flag);
        if (iRef.get() == nullptr) {
            // the sim sells from its stock
            ItemData idata(oInfo.typeID, ownerStation, locTemp, flagNone, qty);
            iRef = sItemFactory.SpawnItem(idata);
            if (iRef.get() == nullptr)
                _log(MARKET__ERROR, "FillAgainstBuyOrder - Failed to spawn %u of %u for %u (order #%u).", qty, oInfo.typeID, buyerID, oInfo.orderID);
        }
        // use the "owner change" packet to alert the buyer of the new item
        if (iRef.get() != nullptr)
            iRef->Donate(itemOwnerID, locationID, flag, true);
    }

    // pay the seller (its character, or its corp's division).  the sim is paid in its ledger
    reason.clear();
    reason += "DESC:  Selling items in ";
    reason += stDataMgr.GetStationName(stationID).c_str();
    double tax(0.0);
    if (!seller.IsSim()) {
        if (npcBuyer) {
            AccountService::TransferFunds(buyerID, seller.ownerID, money, reason.c_str(), Journal::EntryType::MarketTransaction, oInfo.orderID, Account::KeyType::Cash, seller.accountKey, seller.client);
        } else {
            AccountService::TransferFunds(escrowHolderID, seller.ownerID, money, reason.c_str(), Journal::EntryType::MarketTransaction, oInfo.orderID, Account::KeyType::Escrow, seller.accountKey, seller.client);
        }

        // SCC transaction tax, paid by the seller
        tax = SalesTaxFor(seller.client, seller.charID, money);
        if (tax > 0.0) {
            try {
                AccountService::TransferFunds(seller.ownerID, corpSCC, tax, reason.c_str(), Journal::EntryType::TransactionTax, oInfo.orderID, seller.accountKey, Account::KeyType::Cash, seller.client);
            } catch (...) {
                _log(MARKET__WARNING, "FillAgainstBuyOrder - %u couldn't pay %.2f ISK transaction tax for order #%u.", seller.ownerID, tax, oInfo.orderID);
            }
        }
    }
    _log(MARKET__DEBUG, "FillAgainstBuyOrder - %u sold %u of %u to order #%u for %.2f ISK, tax %.2f", seller.ownerID, qty, oInfo.typeID, oInfo.orderID, money, tax);

    RecordTrade(oInfo.typeID, qty, oInfo.price, stationID,
                seller.ownerID, seller.isCorp, seller.charID, seller.accountKey,
                buyerID, isCorp, buyerCharID, buyerKey);

    if (qty < oInfo.quantity) {
        // update the buyer's order to reflect the remaining amount
        _log(MARKET__TRACE, "FillAgainstBuyOrder - Partially satisfied order #%u, altering quantity to %u.", oInfo.orderID, oInfo.quantity - qty);
        MarketDB::AlterOrderQuantity(oInfo.orderID, oInfo.quantity - qty);
        if (!npcBuyer)
            MarketDB::AlterOrderEscrow(oInfo.orderID, newEscrow);
        InvalidateOrdersCache(oInfo.regionID, oInfo.typeID);
        // tell the buy order's owner, not the seller
        if (!npcBuyer)
            NotifyOrderOwner(oInfo, Market::Action::Modify);
        return true;
    }

    _log(MARKET__TRACE, "FillAgainstBuyOrder - Satisfied order #%u, deleting.", oInfo.orderID);
    PyRep* order = MarketDB::GetOrderRow(oInfo.orderID);
    MarketDB::DeleteOrder(oInfo.orderID);
    ClearOrderModified(oInfo.orderID);
    InvalidateOrdersCache(oInfo.regionID, oInfo.typeID);

    if (npcBuyer) {
        PySafeDecRef(order);
        return true;
    }

    // whatever is left in escrow (rounding) goes back
    if (newEscrow >= 0.01) {
        try {
            AccountService::TransferFunds(escrowHolderID, buyerID, newEscrow, "DESC:  Remaining escrow of a completed market order", Journal::EntryType::MarketEscrow, oInfo.orderID, Account::KeyType::Escrow, buyerKey);
        } catch (...) { }
    }

    // tell the buy order's owner, not the seller
    NotifyOrderOwner(oInfo, Market::Action::Expiry, order);
    return true;
}

bool MarketMgr::FillAgainstSellOrder(const Market::OrderInfo& oInfo, const Market::Party& buyer, uint32 qty)
{
    if ((qty == 0) or (qty > oInfo.quantity)) {
        _log(MARKET__ERROR, "FillAgainstSellOrder - invalid fill of %u for order #%u.", qty, oInfo.orderID);
        return false;
    }

    // seller is a character, a player corp, or an npc (the market sim's asks and the npc vendors, backed by its ledger)
    uint32 sellerID(oInfo.ownerID);
    bool sellerIsChar(IsCharacterID(sellerID));
    bool sellerIsCorp(oInfo.isCorp and IsPlayerCorp(sellerID));
    bool playerSeller(sellerIsChar or sellerIsCorp);
    if (!playerSeller and (buyer.IsSim() or !IsBackedNpcOrder(oInfo))) {
        _log(MARKET__TRACE, "FillAgainstSellOrder - npc sell order #%u of %u has nothing behind it.  Skipped.", oInfo.orderID, sellerID);
        return false;
    }
    uint16 sellerKey(sellerIsCorp ? oInfo.accountKey : Account::KeyType::Cash);
    uint32 sellerCharID(sellerIsCorp ? oInfo.memberID : sellerID);

    // trades settle at the resting (sell) order's price.  the buyer's price is only its limit
    double money(oInfo.price * qty);

    // send wallet blink event and record the transaction in their journal.
    std::string reason = "DESC:  Buying market items in ";
    reason += stDataMgr.GetStationName(oInfo.stationID).c_str();

    // this will throw if funds are not available.  nothing has changed yet.  the sim pays from its ledger
    AccountService::TransferFunds(buyer.ownerID, sellerID, money, reason.c_str(), Journal::EntryType::MarketTransaction, oInfo.orderID, buyer.accountKey, sellerKey, buyer.client);

    // SCC transaction tax, paid by player sellers
    double tax(0.0);
    if (playerSeller) {
        tax = SalesTaxFor(nullptr, sellerCharID, money);
        if (tax > 0.0) {
            try {
                AccountService::TransferFunds(sellerID, corpSCC, tax, reason.c_str(), Journal::EntryType::TransactionTax, oInfo.orderID, sellerKey);
            } catch (...) {
                _log(MARKET__WARNING, "FillAgainstSellOrder - %u couldn't pay %.2f ISK transaction tax for order #%u.", sellerID, tax, oInfo.orderID);
            }
        }
    }

    // after money is transferred, create and deliver the items in the order's station.  the sim's purchases join its stock
    if (!buyer.IsSim()) {
        ItemData idata(oInfo.typeID, ownerStation, locTemp, flagNone, qty);
        InventoryItemRef iRef = sItemFactory.SpawnItem(idata);
        if (iRef.get() == nullptr) {
            _log(MARKET__ERROR, "FillAgainstSellOrder - Failed to spawn %u of %u for %u (order #%u).", qty, oInfo.typeID, buyer.ownerID, oInfo.orderID);
        } else {
            uint32 itemOwnerID(0), locationID(0);
            EVEItemFlags flag(flagHangar);
            GetDeliveryLocation(buyer.ownerID, buyer.isCorp, buyer.charID, oInfo.stationID, itemOwnerID, locationID, flag);
            // use the "owner change" packet to alert the buyer of the new item
            iRef->Donate(itemOwnerID, locationID, flag, true);
        }
    }
    _log(MARKET__DEBUG, "FillAgainstSellOrder - %u bought %u of %u from order #%u for %.2f ISK, tax %.2f", buyer.ownerID, qty, oInfo.typeID, oInfo.orderID, money, tax);

    RecordTrade(oInfo.typeID, qty, oInfo.price, oInfo.stationID,
                sellerID, sellerIsCorp, sellerCharID, sellerKey,
                buyer.ownerID, buyer.isCorp, buyer.charID, buyer.accountKey);

    if (qty == oInfo.quantity) {
        _log(MARKET__TRACE, "FillAgainstSellOrder - satisfied order #%u, deleting.", oInfo.orderID);
        PyRep* order = MarketDB::GetOrderRow(oInfo.orderID);
        MarketDB::DeleteOrder(oInfo.orderID);
        ClearOrderModified(oInfo.orderID);
        InvalidateOrdersCache(oInfo.regionID, oInfo.typeID);
        if (playerSeller) {
            NotifyOrderOwner(oInfo, Market::Action::Expiry, order);
        } else {
            PySafeDecRef(order);
        }
    } else {
        _log(MARKET__TRACE, "FillAgainstSellOrder - Partially satisfied order #%u, altering quantity to %u.", oInfo.orderID, oInfo.quantity - qty);
        MarketDB::AlterOrderQuantity(oInfo.orderID, oInfo.quantity - qty);
        InvalidateOrdersCache(oInfo.regionID, oInfo.typeID);
        if (playerSeller)
            NotifyOrderOwner(oInfo, Market::Action::Modify);
    }

    return true;
}

bool MarketMgr::CancelOrderInternal(const Market::OrderInfo& oInfo, const char* reason)
{
    // delete first, so nothing is refunded twice
    PyRep* order(MarketDB::GetOrderRow(oInfo.orderID));
    if (order == nullptr)
        return false;
    if (!MarketDB::DeleteOrder(oInfo.orderID)) {
        _log(MARKET__ERROR, "CancelOrderInternal - Failed to delete order #%u.", oInfo.orderID);
        PyDecRef(order);
        return false;
    }
    ClearOrderModified(oInfo.orderID);

    bool playerOwner(IsCharacterID(oInfo.ownerID) or (oInfo.isCorp and IsPlayerCorp(oInfo.ownerID)));
    if (oInfo.isBuy) {
        // a buy order gives back what is still in escrow (with margin trading that's less than price * volRemaining)
        if (playerOwner and (oInfo.escrow >= 0.01)) {
            try {
                AccountService::TransferFunds(
                    stDataMgr.GetOwnerID(oInfo.stationID),
                    oInfo.ownerID,
                    oInfo.escrow,
                    reason,
                    Journal::EntryType::MarketEscrow,
                    oInfo.orderID,
                    Account::KeyType::Escrow,
                    (oInfo.isCorp ? oInfo.accountKey : Account::KeyType::Cash));
            } catch (...) {
                _log(MARKET__ERROR, "CancelOrderInternal - Failed to refund %.2f ISK escrow of order #%u to %u.", oInfo.escrow, oInfo.orderID, oInfo.ownerID);
            }
        }
    } else if (playerOwner and (oInfo.quantity > 0)) {
        // remaining items go back to the order's owner in the order's station
        uint32 itemOwnerID(0), locationID(0);
        EVEItemFlags flag(flagHangar);
        GetDeliveryLocation(oInfo.ownerID, oInfo.isCorp, oInfo.memberID, oInfo.stationID, itemOwnerID, locationID, flag);
        ItemData idata(oInfo.typeID, ownerStation, locTemp, flagNone, oInfo.quantity);
        InventoryItemRef iRef = sItemFactory.SpawnItem(idata);
        if (iRef.get() != nullptr) {
            iRef->Donate(itemOwnerID, locationID, flag, true);
        } else {
            _log(MARKET__ERROR, "CancelOrderInternal - Failed to return %u of %u from order #%u to %u.", oInfo.quantity, oInfo.typeID, oInfo.orderID, oInfo.ownerID);
        }
    }

    InvalidateOrdersCache(oInfo.regionID, oInfo.typeID);
    if (playerOwner) {
        NotifyOrderOwner(oInfo, Market::Action::Expiry, order);
    } else {
        PyDecRef(order);
    }
    return true;
}

void MarketMgr::ExpireOrder(uint32 orderID)
{
    Market::OrderInfo oInfo = Market::OrderInfo();
    if (!MarketDB::GetOrderInfo(orderID, oInfo))
        return;

    std::string reason = "DESC:  Expired Market Order #";
    reason += std::to_string(orderID);
    if (CancelOrderInternal(oInfo, reason.c_str()))
        _log(MARKET__TRACE, "ExpireOrder - order #%u of %u expired.", orderID, oInfo.ownerID);
}

void MarketMgr::ProcessOrderExpiry()
{
    int64 now(GetFileTimeNow());
    // in batches, so a long downtime doesn't stall one tic
    for (uint8 i = 0; i < 10; ++i) {
        std::vector<uint32> orders;
        MarketDB::GetExpiredOrders(now, 200, orders);
        if (orders.empty())
            break;
        bool failed(false);
        for (auto cur : orders) {
            try {
                ExpireOrder(cur);
            } catch (...) {
                _log(MARKET__ERROR, "ProcessOrderExpiry - expiring order #%u failed.", cur);
                failed = true;
            }
        }
        // a failed order would come back in the next batch.  try again next tic
        if (failed or (orders.size() < 200))
            break;
    }
}


// after finding price data from Crucible, this may be moot.   -allan 28Feb21
void MarketMgr::SetBasePrice()
{
    /* method to estimate item base price, based on materials to manufacture that item
     *
     * mineral prices are queried from db with a +10% markup
     *
     * ships and modules are loaded and queried from static data for manufacturing materials
     * those materials are queried (as required) for minerals needed
     * once a total mineral value has been calculated, calculate estimated cost based on
     * current mineral values
     *
     * final prices will have markup based on item type
     *
     *
     * NOTES FOR IMPLEMETING THIS SHIT
     *
     *        //SELECT typeID, materialTypeID, quantity FROM invTypeMaterials
     *        EvERam::RamMaterials ramMatls = EvERam::RamMaterials();
     *        ramMatls.quantity       = row.GetInt(2);
     *        ramMatls.materialTypeID = row.GetInt(1);
     *
     * eventually, this will query all items that can be manufactured from BP or refined into minerals
     *
     */

    //  get mineral prices and put into data map
    // typeID/data{typeID, price, name}
    std::map<uint16, Market::matlData> mineralMap;
    mineralMap.clear();
    sDataMgr.GetMineralData(mineralMap);        // 8

    // this will have to use db to get current data.
    //  mineral prices are (will be) updated via a 'price average' method yet to be written
    MarketDB::GetMineralPrices(mineralMap);


    // get 'building blocks' used for cap ships and put into data map
    //block typeID/vector<data{materialTypeID, qty}>
    std::map<uint16, Market::matlData> materialMap;
    materialMap.clear();
    sDataMgr.GetComponentData(materialMap);         // 125

    // get compounds from ice and put into data map
    sDataMgr.GetCompoundData(materialMap);         // 7

    // get misc commodities and put into data map
    sDataMgr.GetMiscCommodityData(materialMap);         // 456

    // get salvage items for rigs and other items made from them
    sDataMgr.GetSalvageData(materialMap);         // 53

    // get PI resources
    sDataMgr.GetPIResourceData(materialMap);         // 15

    // get PI commodities
    sDataMgr.GetPICommodityData(materialMap);         // 66

    // hack to add this item to materialMap, instead of getting entire group
    //  22175 is Codebreaker I, which is a reproc item from Purloined Sansha Codebreaker.
    Market::matlData data = Market::matlData();
    data.typeID = 22175;
    data.name = "Codebreaker I";
    materialMap[22175] = data;

    // this will have to use db to get current data.
    //  mineral prices are (will be) updated via a 'price average' method yet to be written
    MarketDB::GetMaterialPrices(materialMap);

    // add minerals to material maps
    materialMap.insert(mineralMap.begin(), mineralMap.end());
    //sDataMgr.GetMineralData(materialMap);        // 8


    // item typeID/data{inventory data}
    std::map<uint16, Inv::TypeData> itemMap;
    itemMap.clear();
    // this gets only ships
    //MarketDB::GetShipIDs(itemMap);
    // this gets all items made from minerals either directly or indirectly
    MarketDB::GetManufacturedItems(itemMap);

    //item typeID/vector<data{materialTypeID, qty}>
    std::map<uint16, std::vector<EvERam::RamMaterials>> itemMatMap;
    itemMatMap.clear();
    std::map<uint16, Inv::TypeData>::iterator itemItr = itemMap.begin();
    for (; itemItr != itemMap.end(); ++itemItr) {
        // pull data for this item  -need r/w iterator to work
        sDataMgr.GetType(itemItr->first, itemItr->second);

        // get materials required for this item
        std::vector<EvERam::RamMaterials> matVec;
        matVec.clear();
        sDataMgr.GetRamMaterials(itemItr->first, matVec);
        itemMatMap[itemItr->first] = matVec;
    }


    // estimate price of item based on mineral requirements
    bool found(true);
    uint8 mLevel(0);
    double current(0);
    Inv::GrpData gData = Inv::GrpData();
    EvERam::bpTypeData bpData = EvERam::bpTypeData();
    // item typeID/data{inventory data}
    std::map<uint16, Inv::TypeData> missingItemMap;
    missingItemMap.clear();
    std::map<uint16, Market::matlData>::iterator materialItr = materialMap.begin();
    std::map<uint16, std::vector<EvERam::RamMaterials>>::iterator itemMatItr = itemMatMap.end();
    for (itemItr = itemMap.begin(); itemItr != itemMap.end(); ++itemItr) {
        itemMatItr = itemMatMap.find(itemItr->first);
        if (itemMatItr == itemMatMap.end())
            continue;

        found = true;
        current = itemItr->second.basePrice;
        // reset basePrice
        itemItr->second.basePrice = 0.0;
        // sum mineral counts with current prices for this ship
        for (auto cur2 : itemMatItr->second) {
            materialItr = materialMap.find(cur2.materialTypeID);
            if (materialItr == materialMap.end()) {
                sLog.Error("     SetBasePrice", "resource %u for %s(%u) not found in materialMap", \
                        cur2.materialTypeID, itemItr->second.name.c_str(), itemItr->first);
                missingItemMap[itemItr->first] = Inv::TypeData();
                found = false;
                continue;
            }
            itemItr->second.basePrice += (materialItr->second.price * cur2.quantity);
        }
        if (!found)
            continue;

        // add manuf costs to base price
        // currently uses production time to add cost at line rental default of 1k install and 2500/hr
        if (sDataMgr.GetBpDataForItem(itemItr->first, bpData)) {
            itemItr->second.basePrice += 1000 + (2500 * (bpData.productionTime / 3600));  // time is in seconds
        }

        mLevel = itemItr->second.metaLvl;
        gData = Inv::GrpData();
        sDataMgr.GetGroup(itemItr->second.groupID, gData);

        // apply modifier to base price according to item category (complexity, rarity, demand)
        // these should also be adjusted for portion size
        switch (gData.catID) {
            case EVEDB::invCategories::Drone:{
                itemItr->second.basePrice /= itemItr->second.portionSize;
                // modify price based on meta
                switch (mLevel) {
                    case 0: {   //basic
                        itemItr->second.basePrice *= 2;
                    } break;
                    case 1: {   //t1
                        itemItr->second.basePrice *= 2.5;
                    } break;
                    case 2: {   //t2
                        itemItr->second.basePrice *= 3.5;
                    } break;
                }
            } break;
            case EVEDB::invCategories::Celestial:
            case EVEDB::invCategories::Entity:
            case EVEDB::invCategories::Commodity:
            case EVEDB::invCategories::Material:
            case EVEDB::invCategories::Charge:{
                if (mLevel)
                    itemItr->second.basePrice *= mLevel;
                itemItr->second.basePrice /= itemItr->second.portionSize;
            } break;
            case EVEDB::invCategories::Asteroid: {
                if (mLevel)
                    itemItr->second.basePrice *= mLevel;
                // asteroids cannot be 'created' per se, but the mined ore can be sold.
                //  this cat covers mined ore, so use same pricing method as charges
                if (itemItr->first < 28000)
                    itemItr->second.basePrice /= itemItr->second.portionSize;
            } break;
            case EVEDB::invCategories::Module: {
                itemItr->second.basePrice *= 2;
                // multiply price by metaLvl
                if (mLevel) {
                    switch (gData.id) {
                        case EVEDB::invGroups::Rig_Mining:
                        case EVEDB::invGroups::Rig_Armor:
                        case EVEDB::invGroups::Rig_Shield:
                        case EVEDB::invGroups::Rig_Energy_Weapon:
                        case EVEDB::invGroups::Rig_Hybrid_Weapon:
                        case EVEDB::invGroups::Rig_Projectile_Weapon:
                        case EVEDB::invGroups::Rig_Drones:
                        case EVEDB::invGroups::Rig_Launcher:
                        case EVEDB::invGroups::Rig_Electronics:
                        case EVEDB::invGroups::Rig_Energy_Grid:
                        case EVEDB::invGroups::Rig_Astronautic:
                        case EVEDB::invGroups::Rig_Electronics_Superiority:
                        case EVEDB::invGroups::Rig_Security_Transponder:  {
                            itemItr->second.basePrice *= (mLevel + 1);
                        } break;
                        default: {
                            itemItr->second.basePrice *= (mLevel + 10);
                        } break;
                    }
                }
            } break;
            case EVEDB::invCategories::Ship: {
                // multiply price by metaLvl
                if (mLevel)
                    itemItr->second.basePrice *= mLevel;
                // modify price based on class
                switch (gData.id) {
                    case EVEDB::invGroups::Frigate:
                    case EVEDB::invGroups::Destroyer:
                    case EVEDB::invGroups::Rookieship:
                    case EVEDB::invGroups::Industrial: {
                        itemItr->second.basePrice *= 2.1;       // +80%
                    } break;
                    case EVEDB::invGroups::CovertOps: {
                        itemItr->second.basePrice *= 1.2;       // +20%
                    } break;
                    case EVEDB::invGroups::Capsule:
                    case EVEDB::invGroups::Cruiser:
                    case EVEDB::invGroups::Freighter:
                    case EVEDB::invGroups::Battleship:
                    case EVEDB::invGroups::Battlecruiser:
                    case EVEDB::invGroups::Interdictor:
                    case EVEDB::invGroups::Interceptor:
                    case EVEDB::invGroups::HeavyInterdictors:
                    case EVEDB::invGroups::IndustrialCommandShip:
                    case EVEDB::invGroups::ElecAttackShip:
                    case EVEDB::invGroups::Supercarrier:
                    case EVEDB::invGroups::StrategicCruiser: {
                        itemItr->second.basePrice *= 1.1;       // +10%
                    } break;
                    // these are good...
                    case EVEDB::invGroups::Carrier:
                    case EVEDB::invGroups::MiningBarge:
                    case EVEDB::invGroups::StealthBomber:
                    case EVEDB::invGroups::CapitalIndustrialShip: {
                        itemItr->second.basePrice *= 1.5;       // +50%
                    } break;
                    case EVEDB::invGroups::Exhumer:
                    case EVEDB::invGroups::Logistics:
                    case EVEDB::invGroups::Marauder:
                    case EVEDB::invGroups::BlackOps:
                    case EVEDB::invGroups::CombatRecon:
                    case EVEDB::invGroups::AssaultShip:
                    case EVEDB::invGroups::TransportShip:
                    case EVEDB::invGroups::HeavyAssaultShip:
                    // these can only be crated in pos.
                    case EVEDB::invGroups::Titan:
                    case EVEDB::invGroups::Dreadnought:
                    case EVEDB::invGroups::CommandShip: {
                        itemItr->second.basePrice *= 0.6;       // -40%  these are all outrageous at calculated prices
                    } break;
                    case EVEDB::invGroups::JumpFreighter:  {
                        itemItr->second.basePrice *= 0.35;        // this one is weird...
                    } break;
                    case EVEDB::invGroups::Shuttle: {
                        itemItr->second.basePrice *= 3;        // x3 for shuttles
                    } break;
                    case EVEDB::invGroups::Prototype_Exploration_Ship:
                    case EVEDB::invGroups::EliteBattleship: {
                        itemItr->second.basePrice *= 1000;        // x1000
                    } break;
                }
            } break;
            case EVEDB::invCategories::Station: {
                // for some reason, station pricing is way off....
                itemItr->second.basePrice *= 100;
            } break;
        }

        if (itemItr->second.basePrice < 0.01) {
            sLog.Error("     SetBasePrice", "Calculated price for %s(%u) is 0", \
                    itemItr->second.name.c_str(), itemItr->first);
        } /*else {
            sLog.Blue("     SetBasePrice", "Calculated price for %u %s(cat:%u - %s - meta %u) from %.2f to %.2f", \
                    itemItr->first, itemItr->second.name.c_str(), gData.catID, gData.name.c_str(), \
                    itemItr->second.metaLvl, current, itemItr->second.basePrice);
        }*/
    }

    // check for missing items and get average price using crucible mkt data table
    if (missingItemMap.empty()) {
        // update db for 'new' base price
        MarketDB::UpdateInvPrice(itemMap);
    } else {
        MarketDB::GetCruPriceAvg(missingItemMap);
        MarketDB::UpdateInvPrice(missingItemMap);
        sLog.Error("     SetBasePrice", "Missing material.  run again.");
    }
}

void MarketMgr::UpdateMineralPrice()
{
    //  get mineral prices and put into data map
    // typeID/data{typeID, price, name}
    std::map<uint16, Market::matlData> mineralMap;
    mineralMap.clear();
    sDataMgr.GetMineralData(mineralMap);        // 8

    //  update mineral price
    MarketDB::UpdateMktPrice(mineralMap);
}

void MarketMgr::GetCruPrices()
{
    /*
    std::map<uint16, Market::matlData> materialMap;
    materialMap.clear();
    // dont update minerals...they are set.
    //sDataMgr.GetMineralData(materialMap);       // 8
    sDataMgr.GetSalvageData(materialMap);       // 53
    sDataMgr.GetCompoundData(materialMap);      // 7
    sDataMgr.GetComponentData(materialMap);     // 114
    sDataMgr.GetPIResourceData(materialMap);    // 15
    sDataMgr.GetPICommodityData(materialMap);   // 66

    MarketDB::GetCruPriceAvg(materialMap);

    MarketDB::UpdateMktPrice(materialMap);
    */

    sLog.Warning("     SetBasePrice", "Getting types.");
    std::map<uint16, Inv::TypeData> types;
    sDataMgr.GetTypes(types);           //19669 unique items in type data
    sLog.Green("     SetBasePrice", "GetTypes returned %lu items.  Getting price avg.", types.size());
    // delete the typeID '0'
    types.erase(0);

    MarketDB::GetCruPriceAvg(types);    //7723 unique items in price data
    sLog.Green("     SetBasePrice", "Got Avg prices.  updating items.");
    MarketDB::UpdateInvPrice(types);    //7712 non-zero prices
    sLog.Cyan("     SetBasePrice", "Completed.");
}
