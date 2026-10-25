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
*/

#include "eve-server.h"

#include <mysql.h>

#include "EVEServerConfig.h"
#include "killboard/KillboardData.h"

/* the killboard has its own connection, made with the client library directly:  DBcore's Close() and Reconnect() end the
 *  library (mysql_server_end) for every connection in the process, and its one mutex would make pages wait on the game */
class KbDB {
public:
    KbDB() : m_mysql(nullptr) { }
    ~KbDB() {
        if (m_mysql != nullptr)
            mysql_close(m_mysql);
    }

    bool Connect() {
        if (m_mysql != nullptr)
            return true;
        mysql_thread_init();
        m_mysql = mysql_init(nullptr);
        if (m_mysql == nullptr)
            return false;
        enum mysql_protocol_type prot = (sConfig.database.useSocket ? MYSQL_PROTOCOL_SOCKET : MYSQL_PROTOCOL_TCP);
        mysql_options(m_mysql, MYSQL_OPT_PROTOCOL, (void*)&prot);
        unsigned int timeout = 5;
        mysql_options(m_mysql, MYSQL_OPT_CONNECT_TIMEOUT, (void*)&timeout);
        my_bool reconnect = true;
        mysql_options(m_mysql, MYSQL_OPT_RECONNECT, (void*)&reconnect);
        unsigned long flags = CLIENT_FOUND_ROWS;
        if (sConfig.database.compress)
            flags |= CLIENT_COMPRESS;
        if (mysql_real_connect(m_mysql, sConfig.database.host.c_str(), sConfig.database.username.c_str(),
                sConfig.database.password.c_str(), sConfig.database.db.c_str(), sConfig.database.port, 0, flags) == nullptr) {
            sLog.Error("        Killboard", "Unable to connect to the database: %s", mysql_error(m_mysql));
            mysql_close(m_mysql);
            m_mysql = nullptr;
            return false;
        }
        mysql_set_character_set(m_mysql, "utf8");
        return true;
    }

    // runs a select and calls fn(row) for every row.  false on an error (logged)
    bool Query(const std::string& sql, const std::function<void(MYSQL_ROW, unsigned long*)>& fn) {
        if (!Connect())
            return false;
        if (mysql_real_query(m_mysql, sql.c_str(), sql.size()) != 0) {
            sLog.Error("        Killboard", "Query failed: %s  (%s)", mysql_error(m_mysql), sql.substr(0, 200).c_str());
            return false;
        }
        MYSQL_RES* res = mysql_store_result(m_mysql);
        if (res == nullptr)
            return (mysql_field_count(m_mysql) == 0);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
            fn(row, mysql_fetch_lengths(res));
        mysql_free_result(res);
        return true;
    }

    // the text quoted for a LIKE '%...%' pattern
    std::string LikeEscape(const std::string& text) {
        std::string esc;
        for (char c : text) {
            if ((c == '%') or (c == '_') or (c == '\\'))
                esc += '\\';
            esc += c;
        }
        if (!Connect())
            return std::string();
        std::string out(esc.size() * 2 + 1, '\0');
        out.resize(mysql_real_escape_string(m_mysql, &out[0], esc.c_str(), esc.size()));
        return out;
    }

private:
    MYSQL* m_mysql;
};

static KbDB* sKbDB(nullptr);

static uint32 RowUInt(MYSQL_ROW row, int col)       { return (row[col] == nullptr ? 0 : (uint32)strtoul(row[col], nullptr, 10)); }
static int64 RowInt64(MYSQL_ROW row, int col)       { return (row[col] == nullptr ? 0 : strtoll(row[col], nullptr, 10)); }
static double RowDouble(MYSQL_ROW row, int col)     { return (row[col] == nullptr ? 0.0 : strtod(row[col], nullptr)); }
static std::string RowText(MYSQL_ROW row, unsigned long* len, int col)
{
    return (row[col] == nullptr ? std::string() : std::string(row[col], len[col]));
}

// filetime (100ns since 1601) to unix seconds
static int64 FileTimeToUnix(int64 ft)               { return (ft / 10000000LL) - 11644473600LL; }

static std::string IdList(const std::vector<uint32>& ids)
{
    std::string list;
    for (auto id : ids) {
        if (!list.empty())
            list += ',';
        list += std::to_string(id);
    }
    return list;
}

static const char* KillColumns =
    "SELECT killID, solarSystemID, victimCharacterID, victimCorporationID, victimAllianceID, victimFactionID,"
    " victimShipTypeID, victimDamageTaken, finalCharacterID, finalCorporationID, finalAllianceID, finalFactionID,"
    " finalShipTypeID, finalWeaponTypeID, finalSecurityStatus, finalDamageDone, killBlob, killTime, moonID"
    " FROM chrKillTable";

// a KillColumns row:  the kill and its attackers (final blow first, then by damage).  the items are left in the blob
static void ReadKillRow(MYSQL_ROW row, unsigned long* len, KbKill& kill, std::string& blob)
{
    kill.killID = RowUInt(row, 0);
    kill.systemID = RowUInt(row, 1);
    kill.victimChar = RowUInt(row, 2);
    kill.victimCorp = RowUInt(row, 3);
    kill.victimAlly = RowUInt(row, 4);
    kill.victimFaction = RowUInt(row, 5);
    kill.victimShip = RowUInt(row, 6);
    kill.damageTaken = RowUInt(row, 7);
    KbAttacker final;
    final.charID = RowUInt(row, 8);
    final.corpID = RowUInt(row, 9);
    final.allyID = RowUInt(row, 10);
    final.factionID = RowUInt(row, 11);
    final.shipTypeID = RowUInt(row, 12);
    final.weaponTypeID = RowUInt(row, 13);
    final.security = (float)RowDouble(row, 14);
    final.damage = RowUInt(row, 15);
    final.finalBlow = true;
    blob = RowText(row, len, 16);
    kill.time = FileTimeToUnix(RowInt64(row, 17));
    kill.moonID = RowUInt(row, 18);
    kill.attackers.clear();
    kill.attackers.push_back(final);
    KillboardData::ParseAttackers(blob, kill.attackers);
    std::sort(kill.attackers.begin() + 1, kill.attackers.end(),
              [](const KbAttacker& a, const KbAttacker& b) { return a.damage > b.damage; });
    kill.npcOnly = true;
    for (auto& a : kill.attackers)
        if (a.charID != 0)
            kill.npcOnly = false;
}

KillboardData::KillboardData()
: m_ready(false),
m_battlesDirty(true),
m_lastKillID(0),
m_lastRefresh(0),
m_ownerNamesTime(0)
{
}

KillboardData::~KillboardData()
{
    for (auto cur : m_kills)
        delete cur;
    m_kills.clear();
    SafeDelete(sKbDB);
}

void KillboardData::Refresh()
{
    int64 now(time(nullptr));
    if (m_ready and ((now - m_lastRefresh) < 3))
        return;
    m_lastRefresh = now;
    if (sKbDB == nullptr)
        sKbDB = new KbDB();
    if (!m_ready) {
        if (!sKbDB->Connect())
            return;
        LoadStatic();
        m_ready = true;
    }
    // names change (renamed corps, new alliances):  look them up again now and then
    if ((now - m_ownerNamesTime) > 600) {
        m_ownerNames.clear();
        m_ownerNamesTime = now;
    }
    LoadNewKills();
}

void KillboardData::LoadStatic()
{
    double start(GetTimeMSeconds());
    m_types.clear();
    // the market sim's reference price, or the base price for anything it doesn't price
    sKbDB->Query("SELECT t.typeID, t.typeName, t.groupID, g.categoryID, IFNULL(NULLIF(c.refPrice, 0), t.basePrice)"
                 " FROM invTypes t JOIN invGroups g USING (groupID) LEFT JOIN mktCalType c USING (typeID)",
        [this](MYSQL_ROW row, unsigned long* len) {
            KbTypeInfo& info = m_types[RowUInt(row, 0)];
            info.name = RowText(row, len, 1);
            info.groupID = RowUInt(row, 2);
            info.categoryID = RowUInt(row, 3);
            info.price = RowDouble(row, 4);
        });
    sKbDB->Query("SELECT groupID, groupName FROM invGroups",
        [this](MYSQL_ROW row, unsigned long* len) { m_groupNames[RowUInt(row, 0)] = RowText(row, len, 1); });
    sKbDB->Query("SELECT solarSystemID, solarSystemName, security, constellationID, regionID FROM mapSolarSystems",
        [this](MYSQL_ROW row, unsigned long* len) {
            KbSystemInfo& info = m_systems[RowUInt(row, 0)];
            info.name = RowText(row, len, 1);
            info.security = RowDouble(row, 2);
            info.constellationID = RowUInt(row, 3);
            info.regionID = RowUInt(row, 4);
        });
    sKbDB->Query("SELECT regionID, regionName FROM mapRegions",
        [this](MYSQL_ROW row, unsigned long* len) { m_regionNames[RowUInt(row, 0)] = RowText(row, len, 1); });
    sKbDB->Query("SELECT constellationID, constellationName FROM mapConstellations",
        [this](MYSQL_ROW row, unsigned long* len) { m_constellationNames[RowUInt(row, 0)] = RowText(row, len, 1); });
    sLog.Blue("        Killboard", "Loaded %zu types and %zu systems in %.0fms.", m_types.size(), m_systems.size(), GetTimeMSeconds() - start);
}

void KillboardData::LoadNewKills()
{
    // a cleaned database starts the ids again
    uint32 maxID(0);
    sKbDB->Query("SELECT IFNULL(MAX(killID), 0) FROM chrKillTable", [&maxID](MYSQL_ROW row, unsigned long*) { maxID = RowUInt(row, 0); });
    if (maxID < m_lastKillID) {
        for (auto cur : m_kills)
            delete cur;
        m_kills.clear();
        m_killIndex.clear();
        m_lastKillID = 0;
        m_battlesDirty = true;
    }
    if (maxID == m_lastKillID)
        return;
    // only the newest ones are kept, so don't read the rest
    size_t limit(sConfig.killboard.MaxCachedKills);
    if ((m_lastKillID == 0) and (limit > 0) and (maxID > limit))
        m_lastKillID = maxID - limit;

    size_t before(m_kills.size());
    bool more(true);
    while (more) {
        std::string sql(KillColumns);
        sql += " WHERE killID > " + std::to_string(m_lastKillID) + " ORDER BY killID LIMIT 5000";
        size_t got(0);
        sKbDB->Query(sql, [this, &got](MYSQL_ROW row, unsigned long* len) {
            ++got;
            KbKill* kill = new KbKill();
            std::string blob;
            ReadKillRow(row, len, *kill, blob);
            std::vector<KbItem> items;
            ParseItems(blob, items);
            ValueKill(*kill, items);
            if (kill->killID > m_lastKillID)
                m_lastKillID = kill->killID;
            m_kills.push_back(kill);
            m_killIndex[kill->killID] = kill;
        });
        more = (got == 5000);
    }

    // ids follow the order kills were saved in, which is nearly always their time order.  the lists, the week's top
    //  lists and the battles want time order, so put any that came in out of order in place
    auto byTime = [](const KbKill* a, const KbKill* b) {
        return (a->time != b->time ? a->time < b->time : a->killID < b->killID);
    };
    if ((m_kills.size() > before) and !std::is_sorted(m_kills.begin(), m_kills.end(), byTime))
        std::stable_sort(m_kills.begin(), m_kills.end(), byTime);

    // keep the newest ones
    if ((limit > 0) and (m_kills.size() > limit)) {
        size_t drop(m_kills.size() - limit);
        for (size_t i = 0; i < drop; ++i) {
            m_killIndex.erase(m_kills[i]->killID);
            delete m_kills[i];
        }
        m_kills.erase(m_kills.begin(), m_kills.begin() + drop);
    }
    if (m_kills.size() != before)
        m_battlesDirty = true;
    if (before == 0)
        sLog.Blue("        Killboard", "Loaded %zu kills.", m_kills.size());
}

void KillboardData::ParseAttackers(const std::string& blob, std::vector<KbAttacker>& into)
{
    size_t pos(blob.find("<attackers>"));
    size_t end(blob.find("</attackers>"));
    if ((pos == std::string::npos) or (end == std::string::npos))
        return;
    while (true) {
        pos = blob.find("<a ", pos);
        if ((pos == std::string::npos) or (pos > end))
            break;
        size_t close(blob.find("/>", pos));
        if (close == std::string::npos)
            break;
        KbAttacker a;
        // attributes are k=value or k="value"
        size_t p(pos + 3);
        while (p < close) {
            while ((p < close) and (blob[p] == ' '))
                ++p;
            if ((p + 2) > close)
                break;
            char key(blob[p]);
            if (blob[p + 1] != '=')
                break;
            p += 2;
            bool quoted(blob[p] == '"');
            if (quoted)
                ++p;
            size_t valEnd(blob.find_first_of(quoted ? "\"" : " /", p));
            if ((valEnd == std::string::npos) or (valEnd > close))
                valEnd = close;
            std::string val(blob.substr(p, valEnd - p));
            p = valEnd + (quoted ? 1 : 0);
            switch (key) {
                case 'c': a.charID = strtoul(val.c_str(), nullptr, 10); break;
                case 'r': a.corpID = strtoul(val.c_str(), nullptr, 10); break;
                case 'a': a.allyID = strtoul(val.c_str(), nullptr, 10); break;
                case 'f': a.factionID = strtoul(val.c_str(), nullptr, 10); break;
                case 's': a.shipTypeID = strtoul(val.c_str(), nullptr, 10); break;
                case 'w': a.weaponTypeID = strtoul(val.c_str(), nullptr, 10); break;
                case 'd': a.damage = strtoul(val.c_str(), nullptr, 10); break;
                case 't': a.security = strtof(val.c_str(), nullptr); break;
                default: break;
            }
        }
        into.push_back(a);
        pos = close + 2;
    }
}

void KillboardData::ParseItems(const std::string& blob, std::vector<KbItem>& into)
{
    size_t pos(blob.find("<items>"));
    if (pos == std::string::npos)
        return;
    while (true) {
        pos = blob.find("<i ", pos);
        if (pos == std::string::npos)
            break;
        size_t close(blob.find("/>", pos));
        if (close == std::string::npos)
            break;
        KbItem item;
        size_t p(pos + 3);
        while (p < close) {
            while ((p < close) and (blob[p] == ' '))
                ++p;
            if ((p + 2) > close)
                break;
            char key(blob[p]);
            if (blob[p + 1] != '=')
                break;
            p += 2;
            size_t valEnd(blob.find_first_of(" /", p));
            if ((valEnd == std::string::npos) or (valEnd > close))
                valEnd = close;
            uint32 val(strtoul(blob.substr(p, valEnd - p).c_str(), nullptr, 10));
            p = valEnd;
            switch (key) {
                case 't': item.typeID = val; break;
                case 'f': item.flag = val; break;
                case 's': item.singleton = val; break;
                case 'd': item.dropped = val; break;
                case 'x': item.destroyed = val; break;
                default: break;
            }
        }
        if (item.typeID != 0)
            into.push_back(item);
        pos = close + 2;
    }
}

void KillboardData::ValueKill(KbKill& kill, const std::vector<KbItem>& items) const
{
    kill.shipValue = TypePrice(kill.victimShip);
    kill.destroyedValue = kill.shipValue;
    kill.droppedValue = 0.0;
    for (auto& cur : items) {
        // the hull itself is listed (flag 0) for pods and empty ships
        if ((cur.flag == 0) and (cur.typeID == kill.victimShip))
            continue;
        // blueprint copies are worth next to nothing
        if (cur.singleton == 2)
            continue;
        double price(TypePrice(cur.typeID));
        kill.destroyedValue += price * cur.destroyed;
        kill.droppedValue += price * cur.dropped;
    }
}

const KbKill* KillboardData::FindKill(uint32 killID) const
{
    auto itr = m_killIndex.find(killID);
    return (itr == m_killIndex.end() ? nullptr : itr->second);
}

bool KillboardData::GetKill(uint32 killID, KbKill& into)
{
    if (sKbDB == nullptr)
        return false;
    std::string sql(KillColumns);
    sql += " WHERE killID = " + std::to_string(killID);
    bool found(false);
    std::string blob;
    sKbDB->Query(sql, [&](MYSQL_ROW row, unsigned long* len) {
        found = true;
        ReadKillRow(row, len, into, blob);
    });
    if (!found)
        return false;
    into.items.clear();
    ParseItems(blob, into.items);
    ValueKill(into, into.items);
    return true;
}

bool KillboardData::IsLoss(const KbKill& kill, KbFilter::Type type, uint32 id, const KillboardData& data)
{
    switch (type) {
        case KbFilter::Pilot:       return (kill.victimChar == id);
        case KbFilter::Corp:        return (kill.victimCorp == id);
        case KbFilter::Alliance:    return (kill.victimAlly == id);
        case KbFilter::Faction:     return (kill.victimFaction == id);
        case KbFilter::Ship:        return (kill.victimShip == id);
        case KbFilter::Group: {
            const KbTypeInfo* info(data.GetType(kill.victimShip));
            return ((info != nullptr) and (info->groupID == id));
        }
        default:                    return false;
    }
}

bool KillboardData::IsKill(const KbKill& kill, KbFilter::Type type, uint32 id, const KillboardData& data)
{
    switch (type) {
        case KbFilter::All:         return true;
        case KbFilter::System:      return (kill.systemID == id);
        case KbFilter::Region: {
            const KbSystemInfo* info(data.GetSystem(kill.systemID));
            return ((info != nullptr) and (info->regionID == id));
        }
        default:
            break;
    }
    // a loss is never also a kill (friendly fire, self-destruct)
    if (IsLoss(kill, type, id, data))
        return false;
    for (auto& a : kill.attackers) {
        switch (type) {
            case KbFilter::Pilot:       if (a.charID == id) return true; break;
            case KbFilter::Corp:        if (a.corpID == id) return true; break;
            case KbFilter::Alliance:    if (a.allyID == id) return true; break;
            case KbFilter::Faction:     if (a.factionID == id) return true; break;
            case KbFilter::Ship:        if (a.shipTypeID == id) return true; break;
            case KbFilter::Group: {
                const KbTypeInfo* info(data.GetType(a.shipTypeID));
                if ((info != nullptr) and (info->groupID == id))
                    return true;
            } break;
            default: break;
        }
    }
    return false;
}

void KillboardData::GetKillList(KbFilter::Type type, uint32 id, int side, int64 since, size_t offset, size_t count,
                                std::vector<const KbKill*>& into, size_t& total) const
{
    total = 0;
    for (auto itr = m_kills.rbegin(); itr != m_kills.rend(); ++itr) {
        const KbKill& kill = **itr;
        if (kill.time < since)
            continue;
        bool match(false);
        if (side != 2)
            match = IsKill(kill, type, id, *this);
        if (!match and (side != 1))
            match = IsLoss(kill, type, id, *this);
        if (!match)
            continue;
        if ((total >= offset) and (into.size() < count))
            into.push_back(&kill);
        ++total;
    }
}

void KillboardData::GetStats(KbFilter::Type type, uint32 id, int64 since, KbStats& into) const
{
    for (auto cur : m_kills) {
        const KbKill& kill = *cur;
        if (kill.time < since)
            continue;
        if (IsLoss(kill, type, id, *this)) {
            ++into.losses;
            into.iskLost += kill.TotalValue();
            if (kill.npcOnly)
                ++into.npcLosses;
        } else if (IsKill(kill, type, id, *this)) {
            ++into.kills;
            into.iskKilled += kill.TotalValue();
            if (kill.attackers.size() == 1)
                ++into.soloKills;
        }
    }
}

const std::vector<KbBattle>& KillboardData::Battles()
{
    if (m_battlesDirty)
        BuildBattles();
    return m_battles;
}

/* a battle is a run of kills in one system, each within <killboard><battleGap> minutes of the one before, with at least
 *  <battleMinKills> of them.  kills by rats and concord alone don't count (a belt of npcs killing miners isn't a battle)
 */
void KillboardData::BuildBattles()
{
    m_battlesDirty = false;
    m_battles.clear();
    std::vector<const KbKill*> pvp;
    for (auto cur : m_kills)
        if (!cur->npcOnly)
            pvp.push_back(cur);
    std::stable_sort(pvp.begin(), pvp.end(), [](const KbKill* a, const KbKill* b) {
        if (a->systemID != b->systemID)
            return a->systemID < b->systemID;
        return a->time < b->time;
    });
    int64 gap(sConfig.killboard.BattleGap * 60);
    size_t minKills(std::max<size_t>(2, sConfig.killboard.BattleMinKills));
    size_t i(0);
    while (i < pvp.size()) {
        size_t j(i + 1);
        while ((j < pvp.size()) and (pvp[j]->systemID == pvp[i]->systemID) and ((pvp[j]->time - pvp[j - 1]->time) <= gap))
            ++j;
        if ((j - i) >= minKills) {
            KbBattle battle;
            battle.systemID = pvp[i]->systemID;
            battle.start = pvp[i]->time;
            battle.end = pvp[j - 1]->time;
            std::set<uint32> pilots;
            for (size_t k = i; k < j; ++k) {
                battle.kills.push_back(pvp[k]);
                battle.value += pvp[k]->TotalValue();
                if (pvp[k]->victimChar != 0)
                    pilots.insert(pvp[k]->victimChar);
                for (auto& a : pvp[k]->attackers)
                    if (a.charID != 0)
                        pilots.insert(a.charID);
            }
            battle.pilots = pilots.size();
            m_battles.push_back(battle);
        }
        i = j;
    }
    std::sort(m_battles.begin(), m_battles.end(), [](const KbBattle& a, const KbBattle& b) { return a.end > b.end; });
}

bool KillboardData::FindBattle(uint32 killID, KbBattle& into)
{
    for (auto& cur : Battles())
        for (auto kill : cur.kills)
            if (kill->killID == killID) {
                into = cur;
                return true;
            }
    return false;
}

const KbTypeInfo* KillboardData::GetType(uint32 typeID) const
{
    auto itr = m_types.find(typeID);
    return (itr == m_types.end() ? nullptr : &itr->second);
}

const KbSystemInfo* KillboardData::GetSystem(uint32 systemID) const
{
    auto itr = m_systems.find(systemID);
    return (itr == m_systems.end() ? nullptr : &itr->second);
}

std::string KillboardData::TypeName(uint32 typeID) const
{
    const KbTypeInfo* info(GetType(typeID));
    return (info == nullptr ? ("Type " + std::to_string(typeID)) : info->name);
}

std::string KillboardData::GroupName(uint32 groupID) const
{
    auto itr = m_groupNames.find(groupID);
    return (itr == m_groupNames.end() ? std::string() : itr->second);
}

std::string KillboardData::SystemName(uint32 systemID) const
{
    const KbSystemInfo* info(GetSystem(systemID));
    return (info == nullptr ? std::to_string(systemID) : info->name);
}

std::string KillboardData::RegionName(uint32 regionID) const
{
    auto itr = m_regionNames.find(regionID);
    return (itr == m_regionNames.end() ? std::string() : itr->second);
}

std::string KillboardData::ConstellationName(uint32 constellationID) const
{
    auto itr = m_constellationNames.find(constellationID);
    return (itr == m_constellationNames.end() ? std::string() : itr->second);
}

std::string KillboardData::CelestialName(uint32 itemID)
{
    if (itemID == 0)
        return std::string();
    auto itr = m_celestialNames.find(itemID);
    if (itr != m_celestialNames.end())
        return itr->second;
    std::string name;
    if (sKbDB != nullptr)
        sKbDB->Query("SELECT itemName FROM mapDenormalize WHERE itemID = " + std::to_string(itemID),
            [&name](MYSQL_ROW row, unsigned long* len) { name = RowText(row, len, 0); });
    m_celestialNames[itemID] = name;
    return name;
}

double KillboardData::TypePrice(uint32 typeID) const
{
    const KbTypeInfo* info(GetType(typeID));
    return (info == nullptr ? 0.0 : info->price);
}

void KillboardData::PrefetchOwners(const std::vector<uint32>& ids)
{
    std::vector<uint32> chars, corps, allys, others;
    std::set<uint32> seen;
    for (auto id : ids) {
        if ((id == 0) or (m_ownerNames.find(id) != m_ownerNames.end()) or !seen.insert(id).second)
            continue;
        if (IsCharacterID(id))
            chars.push_back(id);
        else if (IsPlayerCorp(id))
            corps.push_back(id);
        else if (IsAlliance(id))
            allys.push_back(id);
        else
            others.push_back(id);
    }
    if (sKbDB == nullptr)
        return;
    auto store = [this](MYSQL_ROW row, unsigned long* len) { m_ownerNames[RowUInt(row, 0)] = RowText(row, len, 1); };
    if (!chars.empty()) {
        sKbDB->Query("SELECT characterID, characterName FROM chrCharacters WHERE characterID IN (" + IdList(chars) + ")", store);
        sKbDB->Query("SELECT characterID, characterName FROM chrNPCCharacters WHERE characterID IN (" + IdList(chars) + ")", store);
    }
    if (!corps.empty())
        sKbDB->Query("SELECT corporationID, corporationName FROM crpCorporation WHERE corporationID IN (" + IdList(corps) + ")", store);
    if (!allys.empty())
        sKbDB->Query("SELECT allianceID, allianceName FROM alnAlliance WHERE allianceID IN (" + IdList(allys) + ")", store);
    if (!others.empty()) {
        // npc corporations, factions, npc characters and anything else static
        sKbDB->Query("SELECT ownerID, ownerName FROM eveStaticOwners WHERE ownerID IN (" + IdList(others) + ")", store);
        sKbDB->Query("SELECT corporationID, corporationName FROM crpCorporation WHERE corporationID IN (" + IdList(others) + ")", store);
    }
    // remember the ones that have no name, so they aren't looked up on every page
    for (auto id : seen)
        if (m_ownerNames.find(id) == m_ownerNames.end())
            m_ownerNames[id] = std::string();
}

std::string KillboardData::OwnerName(uint32 ownerID)
{
    if (ownerID == 0)
        return std::string();
    auto itr = m_ownerNames.find(ownerID);
    if (itr == m_ownerNames.end()) {
        PrefetchOwners(std::vector<uint32>(1, ownerID));
        itr = m_ownerNames.find(ownerID);
    }
    if ((itr == m_ownerNames.end()) or itr->second.empty())
        return ("#" + std::to_string(ownerID));
    return itr->second;
}

void KillboardData::Search(const std::string& q, std::vector<KbSearchHit>& into)
{
    if ((sKbDB == nullptr) or (q.size() < 2))
        return;
    std::string like(sKbDB->LikeEscape(q));
    if (like.empty())
        return;
    // names that start with it first
    std::string order(" ORDER BY (name LIKE '" + like + "%') DESC, name LIMIT 25");
    struct Source { const char* kind; std::string sql; };
    std::vector<Source> sources = {
        {"pilot",    "SELECT characterID, characterName AS name FROM chrCharacters WHERE characterName LIKE '%" + like + "%'"},
        {"corp",     "SELECT corporationID, corporationName AS name FROM crpCorporation WHERE corporationName LIKE '%" + like + "%'"
                     " OR tickerName = '" + like + "'"},
        {"alliance", "SELECT allianceID, allianceName AS name FROM alnAlliance WHERE allianceName LIKE '%" + like + "%'"
                     " OR shortName = '" + like + "'"},
        {"system",   "SELECT solarSystemID, solarSystemName AS name FROM mapSolarSystems WHERE solarSystemName LIKE '%" + like + "%'"},
        {"region",   "SELECT regionID, regionName AS name FROM mapRegions WHERE regionName LIKE '%" + like + "%'"},
        {"ship",     "SELECT t.typeID, t.typeName AS name FROM invTypes t JOIN invGroups g USING (groupID)"
                     " WHERE g.categoryID = 6 AND t.published = 1 AND t.typeName LIKE '%" + like + "%'"},
    };
    for (auto& src : sources) {
        const char* kind(src.kind);
        sKbDB->Query(src.sql + order, [&into, kind](MYSQL_ROW row, unsigned long* len) {
            KbSearchHit hit;
            hit.kind = kind;
            hit.id = RowUInt(row, 0);
            hit.name = RowText(row, len, 1);
            into.push_back(hit);
        });
    }
}
