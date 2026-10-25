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

#ifndef __KILLBOARDDATA_H__INCL__
#define __KILLBOARDDATA_H__INCL__

#include "eve-common.h"

/* killboard data, used only on the image server's thread.  it has its own database connection (so a page never waits on
 *  the game's queries or holds them up), the static data it needs (types, prices, systems), the owner names it has looked
 *  up, and every kill since the server started in memory (parsed from chrKillTable), which the pages filter and add up.
 */

struct KbAttacker {
    uint32 charID = 0;
    uint32 corpID = 0;
    uint32 allyID = 0;
    uint32 factionID = 0;
    uint32 shipTypeID = 0;
    uint32 weaponTypeID = 0;
    uint32 damage = 0;
    float security = 0.0f;
    bool finalBlow = false;
};

struct KbItem {
    uint32 typeID = 0;
    uint16 flag = 0;
    uint8 singleton = 0;        // 2 is a blueprint copy
    uint32 dropped = 0;
    uint32 destroyed = 0;
};

struct KbKill {
    uint32 killID = 0;
    uint32 systemID = 0;
    uint32 moonID = 0;
    int64 time = 0;             // unix seconds
    uint32 victimChar = 0;
    uint32 victimCorp = 0;
    uint32 victimAlly = 0;
    uint32 victimFaction = 0;
    uint32 victimShip = 0;
    uint32 damageTaken = 0;
    double shipValue = 0.0;
    double droppedValue = 0.0;
    double destroyedValue = 0.0;    // ship included
    bool npcOnly = true;        // no character among the attackers (rats, sentries, concord)
    std::vector<KbAttacker> attackers;  // final blow first, then by damage
    std::vector<KbItem> items;  // only filled by GetKill()

    double TotalValue() const   { return droppedValue + destroyedValue; }
    // team key of the victim (alliance, else corporation)
    uint32 VictimTeam() const   { return (victimAlly != 0 ? victimAlly : victimCorp); }
};

struct KbTypeInfo {
    std::string name;
    uint32 groupID = 0;
    uint32 categoryID = 0;
    double price = 0.0;
};

struct KbSystemInfo {
    std::string name;
    double security = 0.0;
    uint32 constellationID = 0;
    uint32 regionID = 0;
};

// what a list of kills is filtered on
namespace KbFilter {
    enum Type {
        All = 0,
        Pilot,
        Corp,
        Alliance,
        Faction,
        Ship,       // ship type
        Group,      // ship group
        System,
        Region
    };
}

struct KbStats {
    uint32 kills = 0;
    uint32 losses = 0;
    double iskKilled = 0.0;
    double iskLost = 0.0;
    uint32 soloKills = 0;
    uint32 npcLosses = 0;       // losses to rats and concord only
};

struct KbSearchHit {
    std::string kind;           // pilot, corp, alliance, system, region, ship
    uint32 id = 0;
    std::string name;
};

struct KbBattle {
    uint32 systemID = 0;
    int64 start = 0;
    int64 end = 0;
    std::vector<const KbKill*> kills;   // in time order
    double value = 0.0;
    uint32 pilots = 0;          // characters involved
    uint32 firstKillID() const  { return (kills.empty() ? 0 : kills.front()->killID); }
};

class KillboardData {
public:
    KillboardData();
    ~KillboardData();

    // loads the static data the first time, then any kills saved since the last call (at most every few seconds)
    void Refresh();
    bool IsReady() const                                { return m_ready; }

    // the cached kills, oldest first
    const std::vector<KbKill*>& Kills() const           { return m_kills; }
    const KbKill* FindKill(uint32 killID) const;
    // one kill with its items (from the database, so kills older than the cache work too).  false if there is none
    bool GetKill(uint32 killID, KbKill& into);

    // true if the kill is on this entity's side as a loss / as a kill
    static bool IsLoss(const KbKill& kill, KbFilter::Type type, uint32 id, const KillboardData& data);
    static bool IsKill(const KbKill& kill, KbFilter::Type type, uint32 id, const KillboardData& data);
    // newest first.  side: 0 both, 1 kills only, 2 losses only.  total gets the count before paging
    void GetKillList(KbFilter::Type type, uint32 id, int side, int64 since, size_t offset, size_t count,
                     std::vector<const KbKill*>& into, size_t& total) const;
    void GetStats(KbFilter::Type type, uint32 id, int64 since, KbStats& into) const;

    // battles, newest first (recomputed when kills come in)
    const std::vector<KbBattle>& Battles();
    // the battle containing this kill, or false
    bool FindBattle(uint32 killID, KbBattle& into);

    const KbTypeInfo* GetType(uint32 typeID) const;
    const KbSystemInfo* GetSystem(uint32 systemID) const;
    std::string TypeName(uint32 typeID) const;
    std::string GroupName(uint32 groupID) const;
    std::string SystemName(uint32 systemID) const;
    std::string RegionName(uint32 regionID) const;
    std::string ConstellationName(uint32 constellationID) const;
    std::string CelestialName(uint32 itemID);
    double TypePrice(uint32 typeID) const;

    // owner names (characters, corporations, alliances, factions).  Prefetch looks up everything not known yet in one go
    void PrefetchOwners(const std::vector<uint32>& ids);
    std::string OwnerName(uint32 ownerID);
    bool IsPlayerCharacter(uint32 charID) const         { return (charID >= 90000000); }
    // every owner name the cache knows about, for the search page
    const std::map<uint32, std::string>& OwnerNames() const     { return m_ownerNames; }
    const std::map<uint32, KbTypeInfo>& Types() const   { return m_types; }
    const std::map<uint32, KbSystemInfo>& Systems() const       { return m_systems; }

    // names starting with or containing q (pilots, corporations, alliances, systems, regions, ship types)
    void Search(const std::string& q, std::vector<KbSearchHit>& into);

    // the kill blob parsers (also used for the kill mails)
    static void ParseAttackers(const std::string& blob, std::vector<KbAttacker>& into);
    static void ParseItems(const std::string& blob, std::vector<KbItem>& into);

private:
    void LoadStatic();
    void LoadNewKills();
    void ValueKill(KbKill& kill, const std::vector<KbItem>& items) const;
    void BuildBattles();

    bool m_ready;
    bool m_battlesDirty;
    uint32 m_lastKillID;
    int64 m_lastRefresh;
    std::vector<KbKill*> m_kills;
    std::unordered_map<uint32, KbKill*> m_killIndex;
    std::vector<KbBattle> m_battles;

    std::map<uint32, KbTypeInfo> m_types;
    std::unordered_map<uint32, std::string> m_groupNames;
    std::map<uint32, KbSystemInfo> m_systems;
    std::unordered_map<uint32, std::string> m_regionNames;
    std::unordered_map<uint32, std::string> m_constellationNames;
    std::unordered_map<uint32, std::string> m_celestialNames;
    std::map<uint32, std::string> m_ownerNames;
    int64 m_ownerNamesTime;
};

#endif  // __KILLBOARDDATA_H__INCL__
