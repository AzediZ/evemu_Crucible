/**
 * @name FacWarRewards.cpp
 *   factional warfare's ranks and loyalty points for kills.  see FacWarMgr.h
 *
 *   a militia pilot's rank with its faction goes by its standing with the faction, as in Crucible:  0 under 1.0, then one
 *     more a point of standing, to 9.  the client asks for it (GetMyCharacterRankOverview, CheckForRankChange), and the
 *     highest a pilot reached is kept.
 *   a kill of an enemy militia pilot's ship pays the militia pilots on the kill loyalty points in their militias, as on
 *     Tranquility since 2012:  a point for each 10,000 ISK of the hull's base price, a tenth more for each of the victim's
 *     ranks, shared between them.  that and the rank multiplier are picks.  a pod pays nothing
 *
 * @date:          22 October 2026
 */

#include "eve-server.h"

#include "EVE_Defines.h"

#include "POD_containers.h"
#include "StaticDataMgr.h"
#include "corporation/LPService.h"
#include "database/EVEDBUtils.h"
#include "faction/FacWarMgr.h"
#include "inventory/InventoryItem.h"
#include "inventory/ItemFactory.h"
#include "inventory/ItemType.h"
#include "killboard/KillboardData.h"
#include "standing/StandingDB.h"

namespace {
    // loyalty points a kill pays in all:  one for this much ISK of the hull's base price, and this much more a victim's rank
    const double IskPerPoint = 10000.0, PerVictimRank = 0.1;
    const uint8 TopRank = 9;
    DBerror s_err;
}

uint8 FacWarMgr::RankAt(float standing)
{
    if (standing < 1.0f)
        return 0;
    return (uint8)EvE::min((int)standing, (int)TopRank);
}

uint8 FacWarMgr::UpdateRank(uint32 charID, uint32 factionID, bool& changed)
{
    changed = false;
    if ((charID == 0) or (MilitiaCorpOf(factionID) == 0))
        return 0;
    uint8 rank(RankAt(StandingDB::GetStanding(factionID, charID)));
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT currentRank, highestRank FROM facWarRanks WHERE characterID = %u AND factionID = %u", charID, factionID)
    and res.GetRow(row)) {
        if (row.GetInt(0) == rank)
            return rank;
        changed = true;
        sDatabase.RunQuery(s_err, "UPDATE facWarRanks SET currentRank = %u, highestRank = GREATEST(highestRank, %u), lastModified = %lli"
                           " WHERE characterID = %u AND factionID = %u", rank, rank, (int64)GetFileTimeNow(), charID, factionID);
        return rank;
    }
    // its first:  a rank above 0 is news
    changed = (rank > 0);
    sDatabase.RunQuery(s_err, "INSERT INTO facWarRanks (characterID, factionID, currentRank, highestRank, lastModified) VALUES (%u, %u, %u, %u, %lli)",
                       charID, factionID, rank, rank, (int64)GetFileTimeNow());
    return rank;
}

PyRep* FacWarMgr::RankRows(uint32 charID, uint32 factionID)
{
    bool changed(false);
    if (factionID != 0)
        UpdateRank(charID, factionID, changed);
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT currentRank, highestRank, factionID, lastModified FROM facWarRanks WHERE characterID = %u", charID))
        codelog(DATABASE__ERROR, "FacWarMgr::RankRows() - %s", res.error.c_str());
    return DBResultToCRowset(res);
}

uint32 FacWarMgr::KillPoints(uint32 shipTypeID, uint8 victimRank)
{
    const ItemType* pType(sItemFactory.GetType(shipTypeID));
    if ((pType == nullptr) or (pType->groupID() == EVEDB::invGroups::Capsule) or (pType->categoryID() != EVEDB::invCategories::Ship))
        return 0;
    return (uint32)(pType->basePrice() / IskPerPoint * (1.0 + PerVictimRank * victimRank));
}

void FacWarMgr::OnKill(const KillData& data)
{
    if (!IsCharacterID(data.victimCharacterID) or (MilitiaCorpOf(data.victimFactionID) == 0))
        return;
    uint32 points(KillPoints(data.victimShipTypeID, RankAt(StandingDB::GetStanding(data.victimFactionID, data.victimCharacterID))));
    if (points == 0)
        return;
    // the victim's enemies on the kill, each in its militia now
    std::vector<KbAttacker> attackers;
    KillboardData::ParseAttackers(data.killBlob, attackers);
    std::map<uint32, uint32> paid;      // character -> its militia
    for (auto& cur : attackers) {
        if (!IsCharacterID(cur.charID) or (paid.find(cur.charID) != paid.end()))
            continue;
        uint32 factionID(WarFactionOf(cur.corpID));
        if ((factionID != 0) and AreEnemies(factionID, data.victimFactionID))
            paid[cur.charID] = factionID;
    }
    if (IsCharacterID(data.finalCharacterID) and (paid.find(data.finalCharacterID) == paid.end())) {
        uint32 factionID(WarFactionOf(data.finalCorporationID));
        if ((factionID != 0) and AreEnemies(factionID, data.victimFactionID))
            paid[data.finalCharacterID] = factionID;
    }
    if (paid.empty())
        return;
    uint32 share((points > paid.size()) ? points / (uint32)paid.size() : 1);
    for (auto& cur : paid)
        LPService::AddLP(cur.first, MilitiaCorpOf(cur.second), share);
    ++m_killsPaid;
    m_killPoints += share * (uint32)paid.size();
    _log(FACWAR__INFO, "FacWarMgr::OnKill() - %s's %s in %u:  %u loyalty points to each of %u militia pilots", sDataMgr.GetOwnerName(data.victimCharacterID).c_str(),
         sDataMgr.GetTypeName(data.victimShipTypeID), data.solarSystemID, share, (uint32)paid.size());
}
