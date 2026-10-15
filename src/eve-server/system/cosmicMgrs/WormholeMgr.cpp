
 /**
  * @name WormholeMgr.cpp
  *     WH management system for EVEmu
  *
  * @Author:        Allan
  * @date:          12 December 2015
  *
  * Updates:        James
  * Date:           13 November 2021
  */


#include "eve-server.h"

#include "EVEServerConfig.h"

#include "StaticDataMgr.h"
#include "inventory/InventoryItem.h"
#include "system/SystemBubble.h"
#include "system/Celestial.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/WormholeMgr.h"
#include "map/MapData.h"
#include "Client.h"
#include "inventory/ItemDB.h"
#include "ship/modules/GenericModule.h"

/*  this class will need to keep track of all WH in universe, what systems they connect to, and how long they last.
 *
 *  it will need access to the system manager, a list of systems, and a way to boot a new system when needed.
 *
 *  this class will also need access to the wspace system manager (which may be same sysmgr)
 * with a way to access/track/boot as needed.
 *
 *  when one WH collapses, it will be in charge of creating new WH in the place of the old
 * one, making approprate connections, and tracking any other changes in the WH itself
 *
 *  this class is also in charge of all dynamic WH data in the db
 *
 */

// Crucible values (reports/wormholes-research.md)
static const double whJumpRange(5000.0);           // m from the wormhole's surface, and the most a ship lands from the far end
static const uint8 whJumpLimit(2);                 // jumps per pilot through one wormhole...
static const int64 whJumpWindow(EvE::Time::Minute * 4);     // ...in this time
static const int64 whDecaying(EvE::Time::Hour * 4);         // "reaching the end of its natural lifetime"
static const int64 whClosing(EvE::Time::Minute * 15);       // "on the verge of dissipating" (a guess:  sources only say minutes)
static const int64 whCollapseDelay(EvE::Time::Second * 10); // a wormhole out of mass goes after the last ship is through
static const uint32 whK162(30831);

WormholeMgr::WormholeMgr()
:  m_mdb(new ManagerDB()),
    m_updateTimer(0),
    m_initalized(false)
{
}

WormholeMgr::~WormholeMgr()
{
    SafeDelete(m_mdb);
}

void WormholeMgr::Initialize() {
    double start = GetTimeMSeconds();

    // statics of each w-space system
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT solarSystemID, static1, static2 FROM mapWormholeSystems")) {
        sLog.Error(" Wormhole Manager", "No wormhole system data (mapWormholeSystems).  Have the database updates been applied?  Wormholes Disabled.");
        return;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        std::vector<uint32>& statics = m_statics[row.GetUInt(0)];
        for (uint8 i = 1; i < 3; ++i)
            if (row.GetUInt(i) != 0) {
                statics.push_back(row.GetUInt(i));
                m_staticTypes.insert(row.GetUInt(i));
            }
    }

    // open wormholes, with the positions of their ends
    if (!sDatabase.RunQuery(res,
        "SELECT w.wormholeID, w.typeID, w.systemID, w.isStatic, w.exitID, w.exitSystemID, w.massTotal, w.massUsed,"
        " w.jumps, w.createTime, w.expiryTime, e.x, e.y, e.z, IFNULL(x.x, 0), IFNULL(x.y, 0), IFNULL(x.z, 0)"
        " FROM sysWormholes AS w"
        "  JOIN entity AS e ON e.itemID = w.wormholeID"
        "  LEFT JOIN entity AS x ON x.itemID = w.exitID")) {
        sLog.Error(" Wormhole Manager", "Unable to load wormholes (sysWormholes):  %s.  Wormholes Disabled.", res.error.c_str());
        return;
    }
    while (res.GetRow(row)) {
        WormholePair wh = WormholePair();
            wh.wormholeID = row.GetUInt(0);
            wh.typeID = row.GetUInt(1);
            wh.systemID = row.GetUInt(2);
            wh.isStatic = row.GetBool(3);
            wh.exitID = row.GetUInt(4);
            wh.exitSystemID = row.GetUInt(5);
            wh.massTotal = row.GetInt64(6);
            wh.massUsed = row.GetInt64(7);
            wh.jumps = row.GetUInt(8);
            wh.createTime = row.GetInt64(9);
            wh.expiryTime = row.GetInt64(10);
            wh.position = GPoint(row.GetDouble(11), row.GetDouble(12), row.GetDouble(13));
            wh.exitPosition = GPoint(row.GetDouble(14), row.GetDouble(15), row.GetDouble(16));
        if (wh.exitID != 0) {
            if (row.IsNull(14) or (wh.exitSystemID == 0)) {
                // the far end is gone:  it opens again on the next warp
                wh.exitID = 0;
                wh.exitSystemID = 0;
            } else {
                m_byItem[wh.exitID] = wh.wormholeID;
            }
        }
        m_byItem[wh.wormholeID] = wh.wormholeID;
        m_pairs.emplace(wh.wormholeID, wh);
    }

    m_initalized = true;

    // ends that belong to no open wormhole (the old spawner's, or lost rows), and rows whose wormhole item is gone
    DBerror err;
    std::vector<uint32> orphans;
    if (sDatabase.RunQuery(res,
        "SELECT e.itemID FROM entity AS e JOIN invTypes AS t USING (typeID)"
        " WHERE t.groupID = %u AND e.locationID BETWEEN 30000000 AND 32000000", EVEDB::invGroups::Wormhole))
        while (res.GetRow(row))
            if (m_byItem.find(row.GetUInt(0)) == m_byItem.end())
                orphans.push_back(row.GetUInt(0));
    for (auto cur : orphans)
        RemoveEnd(cur, 0);
    sDatabase.RunQuery(err, "DELETE FROM sysWormholes WHERE wormholeID NOT IN (SELECT itemID FROM entity)");
    sDatabase.RunQuery(err, "DELETE FROM sysSignatures WHERE dungeonType = %u AND sigItemID NOT IN (SELECT itemID FROM entity)",
            Dungeon::Type::Wormhole);

    // wormholes that ran out of time while the server was down
    int64 now(Win32TimeNow());
    std::vector<uint32> expired;
    for (auto& cur : m_pairs)
        if (now >= cur.second.expiryTime)
            expired.push_back(cur.first);
    for (auto cur : expired)
        Collapse(cur);

    m_updateTimer.Start(30000);

    sLog.Blue(" Wormhole Manager", "Wormhole Manager Initialized with %lu open wormholes and statics for %lu systems in %.3fms.  %lu closed while the server was down, %lu stray ends removed.",
            m_pairs.size(), m_statics.size(), (GetTimeMSeconds() - start), expired.size(), orphans.size());
}

void WormholeMgr::Process() {
    if (!m_initalized)
        return;

    int64 now(Win32TimeNow());
    // out of mass:  gone once the last ship is through
    std::vector<uint32> gone;
    for (auto& cur : m_pairs)
        if ((cur.second.collapseTime != 0) and (now >= cur.second.collapseTime))
            gone.push_back(cur.first);
    for (auto cur : gone)
        Collapse(cur);

    if (!m_updateTimer.Check())
        return;

    // out of time, and the age the slim items show
    gone.clear();
    for (auto& cur : m_pairs) {
        if (now >= cur.second.expiryTime) {
            gone.push_back(cur.first);
            continue;
        }
        uint8 age(AgeOf(cur.second, now));
        if (age != cur.second.age) {
            cur.second.age = age;
            SendSlim(cur.second);
        }
    }
    for (auto cur : gone)
        Collapse(cur);

    // statics of booted w-space systems (one that collapsed while its system was down, or failed to open)
    for (auto itr = m_wspaceLoaded.begin(); itr != m_wspaceLoaded.end(); ) {
        SystemManager* pSys(sEntityList.FindSystem(*itr));
        if ((pSys == nullptr) or !pSys->IsLoaded()) {
            itr = m_wspaceLoaded.erase(itr);
            continue;
        }
        CheckStatics(pSys);
        ++itr;
    }
}

uint8 WormholeMgr::AgeOf(const WormholePair& wh, int64 now)
{
    int64 left(wh.expiryTime - now);
    if (left > EvE::Time::Day)
        return WormHole::Age::New;
    if (left > whDecaying)
        return WormHole::Age::Adolescent;
    if (left > whClosing)
        return WormHole::Age::Decaying;
    return WormHole::Age::Closing;
}

uint8 WormholeMgr::SizeOf(const WormholePair& wh)
{
    // the mass left:  over half, down to a tenth, below that
    if (wh.massTotal < 1)
        return WormHole::Size::Full;
    double left((double)(wh.massTotal - wh.massUsed) / wh.massTotal);
    if (left > 0.5)
        return WormHole::Size::Full;
    if (left >= 0.1)
        return WormHole::Size::Reduced;
    return WormHole::Size::Disrupted;
}

void WormholeMgr::Save(const WormholePair& wh)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "REPLACE INTO sysWormholes (wormholeID, typeID, systemID, isStatic, exitID, exitSystemID, massTotal, massUsed, jumps,"
        " createTime, expiryTime) VALUES (%u, %u, %u, %u, %u, %u, %lli, %lli, %u, %lli, %lli)",
        wh.wormholeID, wh.typeID, wh.systemID, (wh.isStatic ? 1 : 0), wh.exitID, wh.exitSystemID, wh.massTotal, wh.massUsed,
        wh.jumps, wh.createTime, wh.expiryTime);
}

void WormholeMgr::SystemBooted(SystemManager* pSys)
{
    if (!m_initalized)
        return;
    // ends whose item didn't load are gone:  so is their wormhole
    std::vector<uint32> lost;
    for (auto& cur : m_pairs) {
        if ((cur.second.systemID == pSys->GetID()) and (pSys->GetSE(cur.second.wormholeID) == nullptr))
            lost.push_back(cur.first);
        else if ((cur.second.exitID != 0) and (cur.second.exitSystemID == pSys->GetID()) and (pSys->GetSE(cur.second.exitID) == nullptr))
            lost.push_back(cur.first);
    }
    for (auto cur : lost) {
        _log(WORMHOLE_MGR__INFO, "WormholeMgr::SystemBooted() - wormhole %u has an end missing in %s(%u).  Closing it.", cur, pSys->GetName(), pSys->GetID());
        Collapse(cur);
    }
    if (m_statics.find(pSys->GetID()) == m_statics.end())
        return;
    m_wspaceLoaded.insert(pSys->GetID());
    CheckStatics(pSys);
}

void WormholeMgr::CheckStatics(SystemManager* pSys)
{
    std::map<uint32, std::vector<uint32>>::iterator itr = m_statics.find(pSys->GetID());
    if (itr == m_statics.end())
        return;
    // each static once.  (a static the same type twice, which no system has, would need counting)
    for (auto typeID : itr->second) {
        bool open(false);
        for (auto& cur : m_pairs)
            if (cur.second.isStatic and (cur.second.systemID == pSys->GetID()) and (cur.second.typeID == typeID)
            and (cur.second.collapseTime == 0)) {
                open = true;
                break;
            }
        if (!open)
            OpenWormhole(pSys->GetID(), typeID, true);
    }
}

void WormholeMgr::Create(CosmicSignature& sig)
{
    // wandering wormholes, made by AnomalyMgr as one of the system's signatures
    if (!m_initalized or (sig.dungeonType != Dungeon::Type::Wormhole))
        return;
    uint8 wanderers(0);
    for (auto& cur : m_pairs)
        if (!cur.second.isStatic and (cur.second.systemID == sig.systemID) and (cur.second.collapseTime == 0))
            ++wanderers;
    if (wanderers >= (IsWSpaceID(sig.systemID) ? 2 : 1)) {
        _log(WORMHOLE_MGR__DEBUG, "WormholeMgr::Create() - system %u already has %u wandering wormholes", sig.systemID, wanderers);
        return;
    }
    const ItemType* whType = GetRandomWormholeType(sig.systemID);
    if (whType == nullptr) {
        _log(WORMHOLE_MGR__DEBUG, "WormholeMgr::Create() - no wandering wormhole type for system %u", sig.systemID);
        return;
    }
    OpenWormhole(sig.systemID, whType->id(), false, &sig.position, &sig);
}

uint32 WormholeMgr::OpenWormhole(uint32 systemID, uint32 typeID, bool isStatic, const GPoint* pos/*nullptr*/, CosmicSignature* sigOut/*nullptr*/)
{
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr)
        return 0;

    CosmicSignature sig = CosmicSignature();
    if (sigOut != nullptr)
        sig = *sigOut;
    if (sig.sigID.empty())
        sig.sigID = sEntityList.GetAnomalyID();
    GPoint at;
    if (pos != nullptr) {
        at = *pos;
    } else {
        SystemManager* pSys(sEntityList.FindSystem(systemID));
        at = (pSys != nullptr ? pSys->GetAnomMgr()->GetSitePosition() : sMapData.GetAnomalyPoint(systemID));
    }
    uint32 itemID(SpawnEnd(systemID, typeID, at, sig));
    if (itemID == 0)
        return 0;

    int64 now(Win32TimeNow());
    WormholePair wh = WormholePair();
        wh.wormholeID = itemID;
        wh.typeID = typeID;
        wh.systemID = systemID;
        wh.position = at;
        wh.isStatic = isStatic;
        wh.massTotal = pType->GetAttribute(AttrWormholeMaxStableMass).get_int();
        wh.createTime = now;
        wh.expiryTime = now + pType->GetAttribute(AttrWormholeMaxStableTime).get_int() * EvE::Time::Minute;
        wh.age = AgeOf(wh, now);
        wh.size = SizeOf(wh);
    m_pairs.emplace(itemID, wh);
    m_byItem[itemID] = itemID;
    Save(wh);
    if (sigOut != nullptr)
        *sigOut = sig;

    _log(WORMHOLE_MGR__DEBUG, "WormholeMgr::OpenWormhole() - opened %s%s(%u) in system %u", (isStatic ? "static " : ""),
            pType->name().c_str(), itemID, systemID);
    return itemID;
}

uint32 WormholeMgr::SpawnEnd(uint32 systemID, uint32 typeID, const GPoint& pos, CosmicSignature& sig)
{
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr)
        return 0;
    sig.systemID = systemID;
    sig.dungeonType = Dungeon::Type::Wormhole;
    sig.sigName = pType->name();
    sig.sigTypeID = typeID;
    sig.sigGroupID = EVEDB::invGroups::Wormhole;
    sig.scanGroupID = Scanning::Group::Signature;
    sig.scanAttributeID = AttrScanAllStrength;     // shows as Unknown
    sig.sigStrength = 0.1f;                         // what AnomalyMgr::LoadAnomalies() gives them after a restart
    sig.ownerID = ownerSystem;
    sig.position = pos;

    ItemData wData(typeID, ownerSystem, systemID, flagNone, sig.sigName.c_str(), pos);
    CelestialObjectRef iRef = sItemFactory.SpawnWormhole(wData);
    if (iRef.get() == nullptr)
        return 0;
    iRef->SaveItem();
    sig.sigItemID = iRef->itemID();

    SystemManager* pSys(sEntityList.FindSystem(systemID));
    if (pSys != nullptr) {
        WormholeSE* wSE = new WormholeSE(iRef, pSys->GetServiceMgr(), pSys);
        pSys->AddEntity(wSE, false);
        if (wSE->SysBubble() != nullptr)
            sig.bubbleID = wSE->SysBubble()->GetID();
        // on the scanner (and saved)
        pSys->GetAnomMgr()->RegisterWormhole(sig);
    } else {
        // the system shows it when it boots (AnomalyMgr::LoadAnomalies(), SystemManager::LoadSystemDynamics())
        m_mdb->SaveAnomaly(sig);
        sItemFactory.RemoveItem(sig.sigItemID);
    }
    return sig.sigItemID;
}

void WormholeMgr::RemoveEnd(uint32 itemID, uint32 systemID)
{
    if (itemID == 0)
        return;
    bool done(false);
    SystemManager* pSys((systemID != 0) ? sEntityList.FindSystem(systemID) : nullptr);
    if (pSys != nullptr) {
        pSys->GetAnomMgr()->RemoveSignal(itemID);
        SystemEntity* pSE(pSys->GetSE(itemID));
        if (pSE != nullptr) {
            // out of space for everyone in the bubble, and the item deleted
            pSE->Delete();
            SafeDelete(pSE);
            done = true;
        }
    }
    if (!done) {
        ItemDB::DeleteItem(itemID);
        sItemFactory.RemoveItem(itemID);
    }
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM sysSignatures WHERE sigItemID = %u", itemID);
}

void WormholeMgr::Collapse(uint32 wormholeID)
{
    std::map<uint32, WormholePair>::iterator itr = m_pairs.find(wormholeID);
    if (itr == m_pairs.end())
        return;
    WormholePair wh(itr->second);
    m_pairs.erase(itr);
    m_byItem.erase(wh.wormholeID);
    if (wh.exitID != 0)
        m_byItem.erase(wh.exitID);

    RemoveEnd(wh.wormholeID, wh.systemID);
    RemoveEnd(wh.exitID, wh.exitSystemID);
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM sysWormholes WHERE wormholeID = %u", wh.wormholeID);

    _log(WORMHOLE_MGR__DEBUG, "WormholeMgr::Collapse() - wormhole %u (type %u) in system %u to %u collapsed after %u jumps (%lli of %lli kg).",
            wh.wormholeID, wh.typeID, wh.systemID, wh.exitSystemID, wh.jumps, wh.massUsed, wh.massTotal);

    SystemManager* pSys(sEntityList.FindSystem(wh.systemID));
    if (wh.isStatic) {
        // a new one at once, somewhere else, with a new destination.  a system that isn't loaded gets it when it boots
        if ((pSys != nullptr) and pSys->IsLoaded())
            CheckStatics(pSys);
    } else if (pSys != nullptr) {
        // another wandering wormhole turns up in this system later
        pSys->GetAnomMgr()->QueueSite(Dungeon::Type::Wormhole, MakeRandomInt(1800, 7200));
    }
}

void WormholeMgr::OnWarpTo(uint32 itemID)
{
    // Crucible:  the far end appears as soon as the first ship warps to the wormhole
    std::map<uint32, uint32>::iterator bItr = m_byItem.find(itemID);
    if (bItr == m_byItem.end())
        return;
    std::map<uint32, WormholePair>::iterator itr = m_pairs.find(bItr->second);
    if ((itr != m_pairs.end()) and (itr->second.exitID == 0))
        OpenExit(itr->second);
}

void WormholeMgr::OpenExit(WormholePair& wh)
{
    if ((wh.exitID != 0) or (wh.collapseTime != 0))
        return;
    uint32 destID(GetRandomDestination(sItemFactory.GetType(wh.typeID), wh.systemID));
    if (destID == 0) {
        _log(WORMHOLE_MGR__INFO, "WormholeMgr::OpenExit() - no destination for wormhole %u (type %u)", wh.wormholeID, wh.typeID);
        return;
    }
    SystemManager* pToSys(sEntityList.FindSystem(destID));
    GPoint pos((pToSys != nullptr) ? pToSys->GetAnomMgr()->GetSitePosition() : sMapData.GetAnomalyPoint(destID));
    CosmicSignature sig = CosmicSignature();
        sig.sigID = sEntityList.GetAnomalyID();
    uint32 exitID(SpawnEnd(destID, whK162, pos, sig));
    if (exitID == 0)
        return;
    wh.exitID = exitID;
    wh.exitSystemID = destID;
    wh.exitPosition = pos;
    m_byItem[exitID] = wh.wormholeID;
    Save(wh);
    // the typed end knows where it goes now
    SendSlim(wh);

    _log(WORMHOLE_MGR__DEBUG, "WormholeMgr::OpenExit() - wormhole %u in system %u opened its K162 %u in system %u",
            wh.wormholeID, wh.systemID, exitID, destID);
}

void WormholeMgr::Jump(Client* pClient, uint32 itemID)
{
    if (!m_initalized)
        throw CustomError("Wormholes are switched off on this server.");
    std::map<uint32, uint32>::iterator bItr = m_byItem.find(itemID);
    if (bItr == m_byItem.end())
        throw CustomError("That wormhole is no longer there.");
    std::map<uint32, WormholePair>::iterator itr = m_pairs.find(bItr->second);
    if (itr == m_pairs.end())
        throw CustomError("That wormhole is no longer there.");
    WormholePair& wh(itr->second);
    if (wh.collapseTime != 0)
        throw CustomError("That wormhole is collapsing.");

    ShipSE* pShipSE(pClient->GetShipSE());
    SystemManager* pSys(pClient->SystemMgr());
    if ((pShipSE == nullptr) or (pSys == nullptr))
        throw CustomError("You are not in space.");
    SystemEntity* pHole(pSys->GetSE(itemID));
    if (pHole == nullptr)
        throw CustomError("That wormhole is not in this system.");
    // cyno, siege, triage...  modules that can't be stopped early keep the ship here
    GenericModule* pBlocking(pClient->GetShip()->GetModuleManager()->GetActiveBlockingModule());
    if (pBlocking != nullptr)
        throw UserError("CantEnterWormholeWhileModuleActive")
                .AddFormatValue("moduleType", new PyInt(pBlocking->typeID()));
    double dist(pShipSE->GetPosition().distance(pHole->GetPosition()) - pShipSE->GetRadius() - pHole->GetRadius());
    // one second of ship travel for client/server position skew
    double allowance((pShipSE->DestinyMgr() != nullptr) ? pShipSE->DestinyMgr()->GetSpeed() : 0.0);
    if (dist > whJumpRange + allowance)
        throw UserError("TargetNotWithinRange")
                .AddGroupName("targetGroupName", pHole->GetSelf()->groupID())
                .AddFormatValue("desiredRange", new PyFloat(whJumpRange))
                .AddFormatValue("actualDistance", new PyFloat(dist));

    const ItemType* pType(sItemFactory.GetType(wh.typeID));
    int64 mass(pClient->GetShip()->GetAttribute(AttrMass).get_int());   // with an active prop module's mass
    int64 maxJump((pType != nullptr) ? pType->GetAttribute(AttrWormholeMaxJumpMass).get_int() : 0);
    if (mass > maxJump)
        throw CustomError("Your ship is too massive for this wormhole.  It takes ships of up to %.0f million kg.", maxJump / 1000000.0);

    // at most 2 jumps by one pilot through this wormhole in 4 minutes (either way)
    int64 now(Win32TimeNow());
    std::vector<int64>& times(wh.charJumps[pClient->GetCharacterID()]);
    times.erase(std::remove_if(times.begin(), times.end(), [now](int64 t) { return (now - t) >= whJumpWindow; }), times.end());
    if (times.size() >= whJumpLimit) {
        int64 wait((times.front() + whJumpWindow - now) / EvE::Time::Second + 1);
        throw CustomError("The wormhole's field is still unstable from your last passage.  You can jump through it again in %lli seconds.", wait);
    }

    if (wh.exitID == 0)
        OpenExit(wh);
    if (wh.exitID == 0)
        throw CustomError("This wormhole leads nowhere.");

    bool fromExit(itemID == wh.exitID);
    uint32 toSystemID(fromExit ? wh.systemID : wh.exitSystemID);
    GPoint to(fromExit ? wh.position : wh.exitPosition);
    // somewhere within jump range of the far end
    SystemManager* pToSys(sEntityList.FindSystem(toSystemID));
    SystemEntity* pFar((pToSys != nullptr) ? pToSys->GetSE(fromExit ? wh.wormholeID : wh.exitID) : nullptr);
    double inner(((pFar != nullptr) ? pFar->GetRadius() : 3000.0) + pShipSE->GetRadius());
    to.MakeRandomPointOnSphereLayer(inner, std::max(inner + 500.0, whJumpRange));

    times.push_back(now);
    wh.massUsed += mass;
    ++wh.jumps;
    if (wh.massUsed >= wh.massTotal)
        wh.collapseTime = now + whCollapseDelay;
    Save(wh);

    pClient->WormholeJump(itemID, toSystemID, to);

    uint8 size(SizeOf(wh));
    wh.size = size;
    SendSlim(wh);
}

bool WormholeMgr::GetSlimData(uint32 itemID, uint8& otherClass, float& size, uint8& age, int64& expiry, uint16& count)
{
    std::map<uint32, uint32>::iterator bItr = m_byItem.find(itemID);
    if (bItr == m_byItem.end())
        return false;
    std::map<uint32, WormholePair>::iterator itr = m_pairs.find(bItr->second);
    if (itr == m_pairs.end())
        return false;
    const WormholePair& wh(itr->second);
    if (itemID == wh.exitID) {
        // the K162 leads back to the typed end's system
        otherClass = sDataMgr.GetWHSystemClass(wh.systemID);
    } else {
        // where it goes:  the type's target class (known before the far end opens)
        const ItemType* pType(sItemFactory.GetType(wh.typeID));
        otherClass = ((pType != nullptr) ? pType->GetAttribute(AttrWormholeTargetSystemClass).get_int() : 0);
    }
    size = SizeOf(wh) / 10.0f;
    age = AgeOf(wh, Win32TimeNow());
    expiry = wh.expiryTime;
    // the jump count (live showed 1, then 2 after a jump)
    count = std::min<uint32>(wh.jumps + 1, 0xFFFF);
    return true;
}

void WormholeMgr::SendSlim(const WormholePair& wh)
{
    uint32 ends[2] = {wh.wormholeID, wh.exitID};
    uint32 systems[2] = {wh.systemID, wh.exitSystemID};
    for (uint8 i = 0; i < 2; ++i) {
        if (ends[i] == 0)
            continue;
        SystemManager* pSys(sEntityList.FindSystem(systems[i]));
        if (pSys == nullptr)
            continue;
        SystemEntity* pSE(pSys->GetSE(ends[i]));
        if ((pSE == nullptr) or (pSE->SysBubble() == nullptr))
            continue;
        PyTuple* slimData = new PyTuple(2);
            slimData->SetItem(0, new PyLong(ends[i]));
            slimData->SetItem(1, new PyObject("foo.SlimItem", pSE->MakeSlimItem()));
        PyTuple* itemData = new PyTuple(2);
            itemData->SetItem(0, new PyString("OnSlimItemChange"));
            itemData->SetItem(1, slimData);
        pSE->SysBubble()->BubblecastDestinyUpdate(&itemData, "OnSlimItemChange");
    }
}

// Pick a wandering type for this system's class.  statics never wander
const ItemType* WormholeMgr::GetRandomWormholeType(uint32 systemID) {
    std::vector<uint32> destTypes;
    for (auto cur : sDataMgr.GetWHDestinationTypes(sDataMgr.GetWHSystemClass(systemID)))
        if (m_staticTypes.find(cur) == m_staticTypes.end())
            destTypes.push_back(cur);
    if (destTypes.empty())
        return nullptr;
    return sItemFactory.GetType(destTypes[MakeRandomInt(0, destTypes.size() - 1)]);
}

// Pick a random system of the wormhole's target class (not the one it is in)
uint32 WormholeMgr::GetRandomDestination(const ItemType* whType, uint32 fromSystemID) {
    if (whType == nullptr)
        return 0;
    uint8 targetClass = whType->GetAttribute(AttrWormholeTargetSystemClass).get_uint32();
    std::vector<uint32> destSystems = sDataMgr.GetWHClassSystems(targetClass);
    for (uint8 tries = 0; (tries < 10) and !destSystems.empty(); ++tries) {
        uint32 systemID(destSystems[MakeRandomInt(0, destSystems.size() - 1)]);
        if (systemID != fromSystemID)
            return systemID;
    }
    return 0;
}

std::string WormholeMgr::GetInfo(uint32 systemID)
{
    if (!m_initalized)
        return "Wormholes are switched off (cosmic WormHoleEnabled).";
    std::string out;
    int64 now(Win32TimeNow());
    char buf[400];
    for (auto& cur : m_pairs) {
        const WormholePair& wh(cur.second);
        if ((wh.systemID != systemID) and (wh.exitSystemID != systemID))
            continue;
        bool here(wh.systemID == systemID);
        const ItemType* pType(sItemFactory.GetType(wh.typeID));
        snprintf(buf, sizeof(buf), "%s %u: %s%s, %s %s, %.0f%% mass left (%u jumps), %lli min left\n",
                (here ? (pType != nullptr ? pType->name().c_str() : "Wormhole") : "Wormhole K162"),
                (here ? wh.wormholeID : wh.exitID), (wh.isStatic ? "static, " : ""),
                (here ? "to" : "from"),
                ((here ? wh.exitSystemID : wh.systemID) != 0 ? sDataMgr.GetSystemName(here ? wh.exitSystemID : wh.systemID) : "(opens on first warp)"),
                (here and (pType != nullptr) ? ("(class " + std::to_string(pType->GetAttribute(AttrWormholeTargetSystemClass).get_int()) + ")").c_str() : ""),
                (wh.massTotal > 0 ? 100.0 * (wh.massTotal - wh.massUsed) / wh.massTotal : 0.0), wh.jumps,
                (long long)((wh.expiryTime - now) / EvE::Time::Minute));
        out += buf;
    }
    if (out.empty())
        out = "No wormholes in this system.";
    return out;
}

bool WormholeMgr::CollapseByItem(uint32 itemID)
{
    std::map<uint32, uint32>::iterator bItr = m_byItem.find(itemID);
    if (bItr == m_byItem.end())
        return false;
    Collapse(bItr->second);
    return true;
}

uint32 WormholeMgr::SpawnAt(SystemManager* pSys, uint32 typeID, const GPoint& pos)
{
    if (!m_initalized or (pSys == nullptr))
        return 0;
    const ItemType* pType(sItemFactory.GetType(typeID));
    if ((pType == nullptr) or (pType->groupID() != EVEDB::invGroups::Wormhole) or (typeID == whK162))
        return 0;
    return OpenWormhole(pSys->GetID(), typeID, false, &pos);
}

//SELECT `locationID`, `wormholeClassID` FROM `mapLocationWormholeClasses`

/* attributeID  attributeName   attributeCategory   attributeIdx    description     categoryID
1381    wormholeTargetSystemClass   4   0   Target System Class for wormholes   7
1382    wormholeMaxStableTime   5   0   The maximum amount of time a wormhole will stay open    7
1383    wormholeMaxStableMass   5   0   The maximum amount of mass a wormhole can transit before collapsing     7
1384    wormholeMassRegeneration    5   0   The amount of mass a wormhole regenerates per cycle     7
1385    wormholeMaxJumpMass     5   0   The maximum amount of mass that can transit a wormhole in one go    7
    */

/* WH groupID: 988
 *      70 items in db
 *
 * graphicIDs - 3715 (lt blue, red center)
 *              2017
 *              2013
 *              2010
 *              2009 (dark, single point center with light to rbottom)
 *              2008
 *              2007
 */

/* typeID   typeName    graphicID
30463   Test wormhole   2907
30579   Wormhole Z971   3715
30583   Wormhole R943   3715
30584   Wormhole X702   3715
30642   Wormhole O128   3715
30643   Wormhole N432   3715
30644   Wormhole M555   3715
30645   Wormhole B041   3715
30646   Wormhole U319   3715
30647   Wormhole B449   3715
30648   Wormhole N944   3715
30649   Wormhole S199   3715
30657   Wormhole A641   3715
30658   Wormhole R051   3715
30659   Wormhole V283   3715
30660   Wormhole H121   3715
30661   Wormhole C125   3715
30662   Wormhole O883   3715
30663   Wormhole M609   3715
30664   Wormhole L614   3715
30665   Wormhole S804   3715
30666   Wormhole N110   3715
30667   Wormhole J244   3715
30668   Wormhole Z060   3715
30671   Wormhole Z647   3715
30672   Wormhole D382   3715
30673   Wormhole O477   3715
30674   Wormhole Y683   3715
30675   Wormhole N062   3715
30676   Wormhole R474   3715
30677   Wormhole B274   3715
30678   Wormhole A239   3715
30679   Wormhole E545   3715
30680   Wormhole V301   3715
30681   Wormhole I182   3715
30682   Wormhole N968   3715
30683   Wormhole T405   3715
30684   Wormhole N770   3715
30685   Wormhole A982   3715
30686   Wormhole S047   3715
30687   Wormhole U210   3715
30688   Wormhole K346   3715
30689   Wormhole P060   3715
30690   Wormhole N766   3715
30691   Wormhole C247   3715
30692   Wormhole X877   3715
30693   Wormhole H900   3715
30694   Wormhole U574   3715
30695   Wormhole D845   3715
30696   Wormhole N290   3715
30697   Wormhole K329   3715
30698   Wormhole Y790   3715
30699   Wormhole D364   3715
30700   Wormhole M267   3715
30701   Wormhole E175   3715
30702   Wormhole H296   3715
30703   Wormhole V753   3715
30704   Wormhole D792   3715
30705   Wormhole C140   3715
30706   Wormhole Z142   3715
30707   Wormhole Q317   3715
30708   Wormhole G024   3715
30709   Wormhole L477   3715
30710   Wormhole Z457   3715
30711   Wormhole V911   3715
30712   Wormhole W237   3715
30713   Wormhole B520   3715
30714   Wormhole C391   3715
30715   Wormhole C248   3715
30831   Wormhole K162   3715
*/
