/**
 * @name FacWarSystems.cpp
 *   factional warfare's war zones:  complexes, victory points, control bunkers and systems changing hands.  see FacWarMgr.h
 *
 * @date:          22 October 2026
 */

#include "eve-server.h"

#include "EVE_Corp.h"
#include "EVE_Dungeon.h"
#include "EVE_Standings.h"

#include "Client.h"
#include "EntityList.h"
#include "StaticDataMgr.h"
#include "faction/ControlBunker.h"
#include "faction/FacWarMgr.h"
#include "inventory/ItemFactory.h"
#include "standing/StandingMgr.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/DungeonMgr.h"

namespace {
    // a new complex's size:  the shares of minor and medium ones (the rest major), and of majors without a gate
    const double MinorShare = 0.5, MediumShare = 0.3, UngatedMajor = 0.3;
    // the share of the enemy's complexes (defensive) in a system where it has victory points
    const double DefensiveShare = 0.4;
    // a taken complex's replacement comes this many minutes later
    const int ReplaceMin = 30, ReplaceMax = 90;
    // the client's facwarStandingPerVictoryPoint
    const double StandingPerPoint = 0.0015;
    // where a system's control bunker sits:  this far off the surface of the planet nearest its sun
    const double BunkerOffset = 60000.0;
    const char* BunkerInfo = "livedungeon_facwar";
    DBerror s_err;

    std::string FactionName(uint32 factionID)                   { return sDataMgr.GetOwnerName(factionID); }
    std::string SystemName(uint32 systemID)                     { return sDataMgr.GetSystemName(systemID); }

    std::string ComplexName(uint32 dungeonID)
    {
        std::shared_ptr<const Dungeon::Dungeon> pDungeon(sDunDataMgr.GetDungeon(dungeonID));
        return ((pDungeon.get() != nullptr) ? pDungeon->name : std::string("complex"));
    }

    // a player online gets told
    void Notify(uint32 charID, const std::string& text)
    {
        Client* pClient(sEntityList.FindClientByCharID(charID));
        if (pClient != nullptr)
            pClient->SendNotifyMsg("%s", text.c_str());
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  the rules
 * ------------------------------------------------------------------------------------------------------------------ */
int32 FacWarMgr::ComplexPoints(uint8 size)
{
    return 10 * std::max<int32>(size, 1);
}

double FacWarMgr::CaptureSeconds(uint8 size)
{
    // ten, fifteen and twenty minutes
    return 300.0 * (1 + std::max<int32>(size, 1));
}

bool FacWarMgr::SameSide(uint32 factionA, uint32 factionB)
{
    return (IsWarFaction(factionA) and IsWarFaction(factionB) and !AreEnemies(factionA, factionB));
}

uint16 FacWarMgr::BunkerTypeOf(uint32 factionID)
{
    switch (factionID) {
        case factionAmarr:      return 29286;
        case factionCaldari:    return 29302;
        case factionGallente:   return 29303;
        case factionMinmatar:   return 29304;
        default:                return 0;
    }
}

uint32 FacWarMgr::OccupierOf(uint32 systemID) const
{
    std::unordered_map<uint32, ZoneSystem>::const_iterator itr = m_systems.find(systemID);
    return ((itr == m_systems.end()) ? 0 : itr->second.occupierID);
}

int32 FacWarMgr::VictoryPointsIn(uint32 systemID) const
{
    std::unordered_map<uint32, ZoneSystem>::const_iterator itr = m_systems.find(systemID);
    return ((itr == m_systems.end()) ? 0 : itr->second.points);
}

uint8 FacWarMgr::SystemStatus(uint32 systemID) const
{
    std::unordered_map<uint32, ZoneSystem>::const_iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return FacWar::SysStatus::None;
    if (itr->second.capturedBy != 0)
        return FacWar::SysStatus::Captured;
    if (itr->second.points >= VulnerableAt)
        return FacWar::SysStatus::Vulnerable;
    if (itr->second.points > 0)
        return FacWar::SysStatus::Contested;
    return FacWar::SysStatus::None;
}

bool FacWarMgr::BunkerVulnerable(uint32 systemID) const
{
    return (SystemStatus(systemID) == FacWar::SysStatus::Vulnerable);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  the war zones' state
 * ------------------------------------------------------------------------------------------------------------------ */
void FacWarMgr::LoadSystems()
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res, "SELECT systemID, factionID, occupierID, victoryPoints, capturedBy, capturedAt, bunkerShield,"
                                 " bunkerArmor, bunkerHull FROM facWarSystems")) {
        // the war zones at least (the militias need them)
        _log(FACWAR__ERROR, "FacWarMgr:  the war zones' state didn't load:  %s", res.error.c_str());
        if (sDatabase.RunQuery(res, "SELECT systemID, factionID, occupierID FROM facWarSystems"))
            while (res.GetRow(row)) {
                m_zone[row.GetUInt(0)] = row.GetUInt(1);
                ZoneSystem& sys = m_systems[row.GetUInt(0)];
                sys.factionID = row.GetUInt(1);
                sys.occupierID = (IsWarFaction(row.GetUInt(2)) ? row.GetUInt(2) : sys.factionID);
            }
        return;
    }
    while (res.GetRow(row)) {
        uint32 systemID(row.GetUInt(0));
        m_zone[systemID] = row.GetUInt(1);
        ZoneSystem& sys = m_systems[systemID];
            sys.factionID = row.GetUInt(1);
            sys.occupierID = (IsWarFaction(row.GetUInt(2)) ? row.GetUInt(2) : sys.factionID);
            sys.points = std::max(row.GetInt(3), 0);
            sys.capturedBy = row.GetUInt(4);
            sys.capturedAt = row.GetInt64(5);
            sys.shield = EvE::max(EvE::min(row.GetDouble(6), 1.0), 0.0);
            sys.armor = EvE::max(EvE::min(row.GetDouble(7), 1.0), 0.0);
            sys.hull = EvE::max(EvE::min(row.GetDouble(8), 1.0), 0.0);
        if (sys.capturedBy == sys.occupierID)
            sys.capturedBy = 0;
    }
}

void FacWarMgr::SaveSystem(uint32 systemID)
{
    std::unordered_map<uint32, ZoneSystem>::iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return;
    ZoneSystem& sys = itr->second;
    sys.dirty = false;
    if (!sDatabase.RunQuery(s_err, "UPDATE facWarSystems SET occupierID = %u, victoryPoints = %i, capturedBy = %u, capturedAt = %lli,"
                                   " bunkerShield = %f, bunkerArmor = %f, bunkerHull = %f WHERE systemID = %u",
                                   sys.occupierID, sys.points, sys.capturedBy, sys.capturedAt, sys.shield, sys.armor, sys.hull, systemID))
        _log(FACWAR__ERROR, "FacWarMgr:  %s's state not saved", SystemName(systemID).c_str());
}

uint32 FacWarMgr::PickDungeon(uint32 holderID, uint8 size, bool gated) const
{
    std::vector<uint32> list;
    sDunDataMgr.GetByArchetype(Dungeon::Type::FacWarOffensive, list);
    sDunDataMgr.GetByArchetype(Dungeon::Type::FacWarDefensive, list);
    std::vector<uint32> fits;
    for (uint32 dungeonID : list) {
        std::shared_ptr<const Dungeon::Dungeon> pDungeon(sDunDataMgr.GetDungeon(dungeonID));
        if ((pDungeon.get() == nullptr) or (pDungeon->factionID != holderID) or (pDungeon->difficulty != size))
            continue;
        // a gated complex has a room past the gate
        if ((pDungeon->roomOrder.size() > 1) == gated)
            fits.push_back(dungeonID);
    }
    if (fits.empty())
        return 0;
    return fits[MakeRandomInt(0, (int)fits.size() - 1)];
}

void FacWarMgr::AddComplex(uint32 systemID)
{
    std::unordered_map<uint32, ZoneSystem>::iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return;
    ZoneSystem& sys = itr->second;
    Complex c;
        c.systemID = systemID;
        c.holderID = sys.occupierID;
    if ((sys.points > 0) and (MakeRandomFloat() < DefensiveShare))
        c.holderID = ZoneEnemyOf(sys.occupierID);
    double roll(MakeRandomFloat());
    c.size = ((roll < MinorShare) ? 1 : ((roll < (MinorShare + MediumShare)) ? 2 : 3));
    bool gated(!((c.size == 3) and (MakeRandomFloat() < UngatedMajor)));
    c.dungeonID = PickDungeon(c.holderID, c.size, gated);
    if (c.dungeonID == 0)
        c.dungeonID = PickDungeon(c.holderID, c.size, !gated);
    if (c.dungeonID == 0) {
        _log(FACWAR__WARNING, "FacWarMgr:  no complex for the %s of size %u.  Has the facwar-complexes migration been applied?",
             FactionName(c.holderID).c_str(), c.size);
        return;
    }
    m_complexes[m_nextComplexID++] = c;
}

void FacWarMgr::RemoveComplex(uint32 complexID)
{
    std::map<uint32, Complex>::iterator itr = m_complexes.find(complexID);
    if (itr == m_complexes.end())
        return;
    Complex c(itr->second);
    m_complexes.erase(itr);
    if (c.siteID == 0)
        return;
    m_complexOfSite.erase(c.siteID);
    SystemManager* pSystem(sEntityList.FindSystem(c.systemID));
    if ((pSystem != nullptr) and (pSystem->GetDungMgr() != nullptr))
        pSystem->GetDungMgr()->DespawnInstance(c.siteID, true);
}

void FacWarMgr::TellSystem(uint32 systemID, const std::string& text)
{
    SystemManager* pSystem(sEntityList.FindSystem(systemID));
    if (pSystem == nullptr)
        return;
    std::vector<Client*> clients;
    pSystem->GetClientList(clients);
    for (auto pClient : clients)
        pClient->SendNotifyMsg("%s", text.c_str());
}

/* ------------------------------------------------------------------------------------------------------------------
 *  a loaded system:  its complexes and bunker in space
 * ------------------------------------------------------------------------------------------------------------------ */
void FacWarMgr::SyncSystem(SystemManager* pSystem, AnomalyMgr& anomMgr)
{
    uint32 systemID(pSystem->GetID());
    std::unordered_map<uint32, ZoneSystem>::iterator sItr = m_systems.find(systemID);
    if (sItr == m_systems.end())
        return;
    ZoneSystem& sys = sItr->second;
    DungeonMgr* pDungMgr(pSystem->GetDungMgr());
    if (pDungMgr == nullptr)
        return;
    for (auto& cur : m_complexes) {
        Complex& c = cur.second;
        if (c.systemID != systemID)
            continue;
        if ((c.siteID != 0) and (pDungMgr->GetInstanceDungeon(c.siteID) != 0))
            continue;
        // not there yet, or gone with the system's last load
        if (c.siteID != 0)
            m_complexOfSite.erase(c.siteID);
        c.progress = 0;
        c.siteID = anomMgr.CreateAnomaly((c.holderID == sys.occupierID) ? Dungeon::Type::FacWarOffensive : Dungeon::Type::FacWarDefensive,
                                         c.dungeonID);
        if (c.siteID != 0)
            m_complexOfSite[c.siteID] = cur.first;
    }
    // a destroyed bunker stays down until the system changes hands at downtime
    if ((sys.capturedBy == 0) and ((sys.bunkerID == 0) or (pSystem->GetSE(sys.bunkerID) == nullptr)))
        SpawnBunker(pSystem, anomMgr, sys);
}

bool FacWarMgr::ComplexOf(uint32 instanceID, uint8& size, uint32& holderID) const
{
    std::unordered_map<uint32, uint32>::const_iterator sItr = m_complexOfSite.find(instanceID);
    if (sItr == m_complexOfSite.end())
        return false;
    std::map<uint32, Complex>::const_iterator itr = m_complexes.find(sItr->second);
    if (itr == m_complexes.end())
        return false;
    size = itr->second.size;
    holderID = itr->second.holderID;
    return true;
}

bool FacWarMgr::CaptureTick(uint32 instanceID, const std::vector<uint32>& takers, uint32 holders, uint32 guards, double seconds)
{
    std::unordered_map<uint32, uint32>::iterator sItr = m_complexOfSite.find(instanceID);
    if (sItr == m_complexOfSite.end())
        return false;
    uint32 complexID(sItr->second);
    std::map<uint32, Complex>::iterator itr = m_complexes.find(complexID);
    if (itr == m_complexes.end())
        return false;
    Complex& c = itr->second;
    double need(CaptureSeconds(c.size));
    int64 now(GetFileTimeNow());
    bool tell((now - c.lastTold) >= EvE::Time::Minute);
    char text[256] = { 0 };
    if (!takers.empty() and (holders == 0)) {
        if (guards > 0) {
            if (tell)
                snprintf(text, sizeof(text), "The %s's guards hold its capture point:  it can't be taken while any of them is left.",
                         ComplexName(c.dungeonID).c_str());
        } else {
            c.progress += seconds;
            if (c.progress >= need) {
                Captured(complexID, takers);
                return true;
            }
            if (tell) {
                uint32 left((uint32)(need - c.progress));
                snprintf(text, sizeof(text), "Capturing the %s:  %u:%02u to go.  Keep within range of its capture point.",
                         ComplexName(c.dungeonID).c_str(), left / 60, left % 60);
            }
        }
    } else if (!takers.empty()) {
        // both sides in range:  the clock stands still
        if (tell)
            snprintf(text, sizeof(text), "The %s's defenders are within range of its capture point:  the capture is held up.",
                     ComplexName(c.dungeonID).c_str());
    } else if (holders > 0) {
        // the holder's side alone winds it back
        c.progress = EvE::max(c.progress - seconds, 0.0);
    }
    if (text[0] != 0) {
        c.lastTold = now;
        for (uint32 charID : takers)
            Notify(charID, text);
    }
    return false;
}

void FacWarMgr::Captured(uint32 complexID, const std::vector<uint32>& takers)
{
    std::map<uint32, Complex>::iterator itr = m_complexes.find(complexID);
    if (itr == m_complexes.end())
        return;
    Complex c(itr->second);
    m_complexes.erase(itr);
    if (c.siteID != 0)
        m_complexOfSite.erase(c.siteID);
    ++m_taken;
    // its replacement, somewhere in the war zones, a while later
    m_newComplexes.emplace(GetFileTimeNow() + (MakeRandomInt(ReplaceMin, ReplaceMax) * EvE::Time::Minute), 0);

    std::unordered_map<uint32, ZoneSystem>::iterator sItr = m_systems.find(c.systemID);
    if (sItr == m_systems.end())
        return;
    ZoneSystem& sys = sItr->second;
    uint32 attackerID(ZoneEnemyOf(c.holderID));
    int32 points(ComplexPoints(c.size));
    std::string name(ComplexName(c.dungeonID)), where(SystemName(c.systemID));
    bool wasVulnerable(BunkerVulnerable(c.systemID));
    if (c.holderID == sys.occupierID) {
        // the occupier's enemy gains ground
        sys.points += points;
    } else {
        // the occupier wins some back, and the bunker is whole again when it has won all of it
        sys.points = std::max(sys.points - points, 0);
        if (sys.points == 0)
            RepairBunker(c.systemID);
    }
    sys.dirty = true;
    SaveSystem(c.systemID);
    _log(FACWAR__INFO, "FacWarMgr:  the %s took the %s in %s:  %i victory points for the %s there now",
         FactionName(attackerID).c_str(), name.c_str(), where.c_str(), sys.points, FactionName(ZoneEnemyOf(sys.occupierID)).c_str());

    // the pilots in range:  their own faction's standing
    std::string msg("Captured the " + name + " in " + where);
    for (uint32 charID : takers) {
        Client* pClient(sEntityList.FindClientByCharID(charID));
        uint32 factionID((pClient != nullptr) ? (uint32)pClient->GetWarFactionID() : attackerID);
        if (!IsWarFaction(factionID))
            continue;
        float standing(StandingOf(charID, factionID));
        if (standing < 10.0f)
            sStandingMgr.ModifyFactionStanding(factionID, charID, Standings::CombatOther,
                                               EvE::min((points * StandingPerPoint) / (10.0 - standing), 1.0), msg);
        char text[256];
        snprintf(text, sizeof(text), "You captured the %s for the %s:  %i victory points in %s.", name.c_str(),
                 FactionName(attackerID).c_str(), points, where.c_str());
        Notify(charID, text);
    }
    if (!wasVulnerable and BunkerVulnerable(c.systemID))
        TellSystem(c.systemID, "The " + FactionName(sys.occupierID) + " control bunker in " + where + " is vulnerable:  destroy it, and "
                   + where + " goes to the " + FactionName(ZoneEnemyOf(sys.occupierID)) + " at downtime.");
}

/* ------------------------------------------------------------------------------------------------------------------
 *  control bunkers
 * ------------------------------------------------------------------------------------------------------------------ */
void FacWarMgr::SpawnBunker(SystemManager* pSystem, AnomalyMgr& anomMgr, ZoneSystem& sys)
{
    sys.bunkerID = 0;
    uint16 typeID(BunkerTypeOf(sys.occupierID));
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr)
        return;
    // off the planet nearest the sun
    GPoint pos(0, 0, 0);
    SystemEntity* pPlanet(pSystem->GetClosestPlanetSE(GPoint(0, 0, 0)));
    if (pPlanet != nullptr) {
        GVector dir(pPlanet->GetPosition());
        dir.normalize();
        pos = pPlanet->GetPosition() - (dir * (pPlanet->GetRadius() + BunkerOffset));
    }
    uint32 ownerID(sDataMgr.GetFactionCorp(sys.occupierID));
    std::string name(FactionName(sys.occupierID) + " Control Bunker");
    ItemData iData(typeID, ownerID, pSystem->GetID(), flagNone, name.c_str(), pos, BunkerInfo);
    InventoryItemRef iRef(sItemFactory.SpawnItem(iData));
    if (iRef.get() == nullptr) {
        _log(FACWAR__ERROR, "FacWarMgr:  %s's control bunker didn't spawn", pSystem->GetName());
        return;
    }
    iRef->SaveItem();
    DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.itemID = iRef->itemID();
        entity.itemName = name;
        entity.typeID = typeID;
        entity.groupID = pType->groupID();
        entity.categoryID = pType->categoryID();
        entity.ownerID = ownerID;
        entity.corporationID = ownerID;
        entity.allianceID = sys.occupierID;
        entity.factionID = sys.occupierID;
        entity.position = pos;
    if (!pSystem->BuildDynamicEntity(entity, 0, false)) {
        iRef->Delete();
        return;
    }
    SystemEntity* pSE(pSystem->GetSE(iRef->itemID()));
    if (pSE == nullptr)
        return;
    sys.bunkerID = iRef->itemID();
    // on the scanner, so it can be found
    anomMgr.AddSiteSignal(pSE, name);
    _log(FACWAR__INFO, "FacWarMgr:  %s's control bunker is up (%.0f%% shield, %.0f%% armor, %.0f%% structure)", pSystem->GetName(),
         sys.shield * 100, sys.armor * 100, sys.hull * 100);
}

void FacWarMgr::GetBunkerHealth(uint32 systemID, double& shield, double& armor, double& hull) const
{
    shield = armor = hull = 1.0;
    std::unordered_map<uint32, ZoneSystem>::const_iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return;
    shield = itr->second.shield;
    armor = itr->second.armor;
    hull = itr->second.hull;
}

void FacWarMgr::OnBunkerDamaged(uint32 systemID, double shield, double armor, double hull)
{
    std::unordered_map<uint32, ZoneSystem>::iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return;
    itr->second.shield = shield;
    itr->second.armor = armor;
    itr->second.hull = hull;
    itr->second.dirty = true;
}

void FacWarMgr::OnBunkerDestroyed(uint32 systemID, uint32 charID)
{
    std::unordered_map<uint32, ZoneSystem>::iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return;
    ZoneSystem& sys = itr->second;
    sys.bunkerID = 0;
    if (sys.capturedBy != 0)
        return;
    sys.capturedBy = ZoneEnemyOf(sys.occupierID);
    sys.capturedAt = GetFileTimeNow();
    sys.shield = sys.armor = sys.hull = 0.0;
    SaveSystem(systemID);
    std::string where(SystemName(systemID));
    _log(FACWAR__INFO, "FacWarMgr:  the %s control bunker in %s was destroyed (by %u):  the system goes to the %s at downtime",
         FactionName(sys.occupierID).c_str(), where.c_str(), charID, FactionName(sys.capturedBy).c_str());
    TellSystem(systemID, "The " + FactionName(sys.occupierID) + " control bunker in " + where + " has been destroyed.  " + where
               + " goes to the " + FactionName(sys.capturedBy) + " at downtime.");
}

void FacWarMgr::RepairBunker(uint32 systemID)
{
    std::unordered_map<uint32, ZoneSystem>::iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return;
    ZoneSystem& sys = itr->second;
    sys.shield = sys.armor = sys.hull = 1.0;
    sys.dirty = true;
    SystemManager* pSystem(sEntityList.FindSystem(systemID));
    if ((pSystem == nullptr) or (sys.bunkerID == 0))
        return;
    SystemEntity* pSE(pSystem->GetSE(sys.bunkerID));
    if ((pSE != nullptr) and pSE->IsControlBunkerSE())
        pSE->GetControlBunkerSE()->SetHealth(1.0, 1.0, 1.0);
}

void FacWarMgr::FlipConquered()
{
    std::vector<uint32> flips;
    for (auto& cur : m_systems)
        if (cur.second.capturedBy != 0)
            flips.push_back(cur.first);
    for (uint32 systemID : flips) {
        ZoneSystem& sys = m_systems[systemID];
        uint32 oldID(sys.occupierID);
        sys.occupierID = sys.capturedBy;
        sys.capturedBy = 0;
        sys.capturedAt = 0;
        sys.points = 0;
        sys.shield = sys.armor = sys.hull = 1.0;
        SaveSystem(systemID);
        // its complexes were the old sides':  new ones for the new
        std::vector<uint32> old;
        for (auto& cur : m_complexes)
            if (cur.second.systemID == systemID)
                old.push_back(cur.first);
        for (uint32 complexID : old)
            RemoveComplex(complexID);
        for (uint8 i = 0; i < ComplexesPerSystem; ++i)
            AddComplex(systemID);
        // a live bunker of the old holder's goes;  the new holder's comes with the system's next sync
        SystemManager* pSystem(sEntityList.FindSystem(systemID));
        if ((pSystem != nullptr) and (sys.bunkerID != 0)) {
            SystemEntity* pSE(pSystem->GetSE(sys.bunkerID));
            if (pSE != nullptr) {
                if (pSystem->GetAnomMgr() != nullptr)
                    pSystem->GetAnomMgr()->RemoveSignal(sys.bunkerID);
                pSE->Delete();
                SafeDelete(pSE);
            }
        }
        sys.bunkerID = 0;
        std::string where(SystemName(systemID));
        _log(FACWAR__INFO, "FacWarMgr:  %s changed hands, from the %s to the %s", where.c_str(), FactionName(oldID).c_str(),
             FactionName(sys.occupierID).c_str());
        TellSystem(systemID, where + " is now held by the " + FactionName(sys.occupierID) + ".");
    }
}

void FacWarMgr::SetVictoryPoints(uint32 systemID, int32 points)
{
    std::unordered_map<uint32, ZoneSystem>::iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return;
    itr->second.points = std::max(points, 0);
    if (itr->second.points == 0)
        RepairBunker(systemID);
    SaveSystem(systemID);
}

std::string FacWarMgr::Describe(uint32 systemID) const
{
    std::unordered_map<uint32, ZoneSystem>::const_iterator itr = m_systems.find(systemID);
    if (itr == m_systems.end())
        return SystemName(systemID) + " is not in a war zone.";
    const ZoneSystem& sys = itr->second;
    const char* status[] = { "uncontested", "contested", "vulnerable", "captured" };
    char line[512];
    snprintf(line, sizeof(line), "%s:  the %s's war zone, held by the %s.  The %s have %i of %i victory points here (%s).",
             SystemName(systemID).c_str(), FactionName(sys.factionID).c_str(), FactionName(sys.occupierID).c_str(),
             FactionName(ZoneEnemyOf(sys.occupierID)).c_str(), sys.points, VulnerableAt, status[std::min<uint8>(SystemStatus(systemID), 3)]);
    std::string out(line);
    snprintf(line, sizeof(line), "<br>Control bunker:  %.0f%% shield, %.0f%% armor, %.0f%% structure%s.", sys.shield * 100, sys.armor * 100,
             sys.hull * 100, ((sys.capturedBy != 0) ? (", destroyed:  the system goes to the " + FactionName(sys.capturedBy) + " at downtime").c_str() : ""));
    out += line;
    for (auto& cur : m_complexes) {
        if (cur.second.systemID != systemID)
            continue;
        snprintf(line, sizeof(line), "<br>%s (%s), %s%s", ComplexName(cur.second.dungeonID).c_str(),
                 ((cur.second.holderID == sys.occupierID) ? "offensive" : "defensive"),
                 ((cur.second.siteID != 0) ? "on the scanner" : "not spawned yet"),
                 ((cur.second.progress > 0) ? (", " + std::to_string((int)cur.second.progress) + " s captured").c_str() : ""));
        out += line;
    }
    return out;
}

void FacWarMgr::AddComplexes(SystemManager* pSystem, uint8 count)
{
    for (uint8 i = 0; i < count; ++i)
        AddComplex(pSystem->GetID());
    if (pSystem->GetAnomMgr() != nullptr)
        SyncSystem(pSystem, *pSystem->GetAnomMgr());
}

void FacWarMgr::Stats(uint32& complexes, uint32& live, uint32& taken, uint32& contested, uint32& vulnerable, uint32& conquered) const
{
    complexes = (uint32)m_complexes.size();
    live = (uint32)m_complexOfSite.size();
    taken = m_taken;
    contested = vulnerable = conquered = 0;
    for (auto& cur : m_systems) {
        uint8 status(SystemStatus(cur.first));
        if (status == FacWar::SysStatus::Contested)
            ++contested;
        else if (status == FacWar::SysStatus::Vulnerable)
            ++vulnerable;
        else if (status == FacWar::SysStatus::Captured)
            ++conquered;
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  the client's calls
 * ------------------------------------------------------------------------------------------------------------------ */
PyRep* FacWarMgr::GetFWSystems() const
{
    // {systemID: {occupierID, factionID}}
    PyDict* result = new PyDict();
    for (auto& cur : m_systems) {
        PyDict* dict = new PyDict();
            dict->SetItemString("occupierID", new PyInt(cur.second.occupierID));
            dict->SetItemString("factionID", new PyInt(cur.second.factionID));
        result->SetItem(new PyInt(cur.first), dict);
    }
    return result;
}

PyRep* FacWarMgr::GetVictoryPoints() const
{
    /* {factionID: {'attacking'|'defending': [(solarSystemID, threshold, current), ...]}}:  each contested system is its
     *  occupier's to defend and its enemy's to attack */
    std::map<uint32, PyList*> attacking, defending;
    for (uint32 factionID : {(uint32)factionCaldari, (uint32)factionMinmatar, (uint32)factionAmarr, (uint32)factionGallente}) {
        attacking[factionID] = new PyList();
        defending[factionID] = new PyList();
    }
    for (auto& cur : m_systems) {
        if (cur.second.points <= 0)
            continue;
        uint32 enemyID(ZoneEnemyOf(cur.second.occupierID));
        if ((attacking.find(enemyID) == attacking.end()) or (defending.find(cur.second.occupierID) == defending.end()))
            continue;
        for (int side = 0; side < 2; ++side) {
            PyTuple* entry = new PyTuple(3);
                entry->SetItem(0, new PyInt(cur.first));
                entry->SetItem(1, new PyInt(VulnerableAt));
                entry->SetItem(2, new PyInt(std::min(cur.second.points, VulnerableAt)));
            if (side == 0) {
                attacking[enemyID]->AddItem(entry);
            } else {
                defending[cur.second.occupierID]->AddItem(entry);
            }
        }
    }
    PyDict* result = new PyDict();
    for (auto& cur : attacking) {
        PyDict* modes = new PyDict();
            modes->SetItemString("attacking", cur.second);
            modes->SetItemString("defending", defending[cur.first]);
        result->SetItem(new PyInt(cur.first), modes);
    }
    return result;
}

PyRep* FacWarMgr::GetSystemsConquered() const
{
    // [{solarsystemID, occupierID}]:  the systems that change hands at the next downtime, and to whom
    PyList* list = new PyList();
    for (auto& cur : m_systems) {
        if (cur.second.capturedBy == 0)
            continue;
        PyDict* dict = new PyDict();
            dict->SetItemString("solarsystemID", new PyInt(cur.first));
            dict->SetItemString("occupierID", new PyInt(cur.second.capturedBy));
        list->AddItem(dict);
    }
    return list;
}
