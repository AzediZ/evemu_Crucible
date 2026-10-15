
/**
 * @name AnomalyMgr.cpp
 *     Anomaly management system for EVEmu
 *
 * @Author:        Allan
 * @date:          12 December 2015 (original idea)
 * @update:        3 August 2017 (begin implementation)
 *
 */


#include "eve-server.h"

#include "EVEServerConfig.h"

#include "StaticDataMgr.h"
#include "map/MapData.h"
#include "system/BubbleManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/cosmicMgrs/DungeonMgr.h"
#include "system/cosmicMgrs/SpawnMgr.h"
#include "system/cosmicMgrs/WormholeMgr.h"
#include "system/sov/SovereigntyDataMgr.h"

#include "../../../eve-common/EVE_Scanning.h"

/*  this class will keep track of all Anomalies for its system
 *.   the scan system will query this class for current anomaly data
 *
 *  it will need access to its system manager (thru m_system), the wh mgr (thru sWHMgr), dungeon mgr (thru m_dungeon),
 *       belt mgr (thru m_beltMgr), and spawn mgr (thru m_spawnMgr)
 *. this will use spawn mgr to spawn npcs, using npc class groups following roid rat layout
 *
 *  when one anomaly despawns, this class is in charge of calling cleanup and creating another as needed.
 *
 *  this class is also in charge of all dynamic anomaly data in the db
 *    pos items, wrecks and abandoned ships will have to process thru here also,
 *    as they get sigIDs and are listed on scan results, but cannot be totally scanned down
 */

/*
 * COSMIC_MGR__ERROR
 * COSMIC_MGR__WARNING
 * COSMIC_MGR__MESSAGE
 * COSMIC_MGR__DEBUG
 * COSMIC_MGR__TRACE
 */
AnomalyMgr::AnomalyMgr(SystemManager* mgr, EVEServiceManager& svc)
:m_services(svc),
m_system(mgr),
m_beltMgr(nullptr),
m_dungMgr(nullptr),
m_spawnMgr(nullptr),
m_spawnTimer(0),
m_procTimer(0),
m_WH(0),
m_Sigs(0),
m_Anoms(0),
m_Grav(0),
m_Mag(0),
m_Ladar(0),
m_Radar(0),
m_Unrated(0),
m_Complex(0),
m_maxSigs(0),
m_bonusCombat(0),
m_bonusExplore(0),
m_bonusOre(0),
m_whClass(0),
m_initalized(false)
{
    m_sigBySigID.clear();
    m_sigByItemID.clear();
}

AnomalyMgr::~AnomalyMgr()
{
    // this shouldnt be needed...SysMgr should handle all objects in their systems
    /*
    InventoryItemRef iRef(nullptr);
    for (auto sig : m_sigByItemID) {
        iRef = sItemFactory.GetItemRef(sig.first);
        if (iRef.get() == nullptr)
            continue;
        iRef->Delete();
    }
    */
}

bool AnomalyMgr::Init(BeltMgr* beltMgr, DungeonMgr* dungMgr, SpawnMgr* spawnMgr) {
    m_beltMgr = beltMgr;
    m_dungMgr = dungMgr;
    m_spawnMgr = spawnMgr;
    m_firstSpawn = true;

    if (m_beltMgr == nullptr) {
        _log(COSMIC_MGR__ERROR, "System Init Fault. beltMgr == nullptr.  Not Initializing Anomaly Manager for %s(%u)", m_system->GetName(), m_system->GetID());
        return m_initalized;
    }

    if (m_dungMgr == nullptr) {
        _log(COSMIC_MGR__ERROR, "System Init Fault. dungMgr == nullptr.  Not Initializing Anomaly Manager for %s(%u)", m_system->GetName(), m_system->GetID());
        return m_initalized;
    }

    if (m_spawnMgr == nullptr) {
        _log(COSMIC_MGR__ERROR, "System Init Fault. spawnMgr == nullptr.  Not Initializing Anomaly Manager for %s(%u)", m_system->GetName(), m_system->GetID());
        return m_initalized;
    }

    if (!sConfig.cosmic.AnomalyEnabled) {
        _log(COSMIC_MGR__INIT, "Anomaly System Disabled.  Not Initializing Anomaly Manager for %s(%u)", m_system->GetName(), m_system->GetID());
        return true;
    }
    if (!sConfig.cosmic.DungeonEnabled){
        _log(COSMIC_MGR__INIT, "Dungeon System Disabled.  Not Initializing Anomaly Manager for %s(%u)", m_system->GetName(), m_system->GetID());
        return true;
    }

    // Load saved anomalies from db upon system init
    LoadAnomalies();

    // set internal check data
    // range is 0.1 for 1.0 system to 2.0 for -0.9 system
    float security = m_system->GetSecValue();
    if (sConfig.debug.IsTestServer) {
        m_maxSigs = 3;

        // Add wormholes by default to the type list for test server
        m_typeList.push_back(Dungeon::Type::Wormhole);
        m_WH++;
    } else {
             if (security == 2.0)  { m_maxSigs = 25; }
        else if (security > 1.499) { m_maxSigs = 20; }
        else if (security > 0.999) { m_maxSigs = 15; }
        else if (security > 0.749) { m_maxSigs = 12; }
        else if (security > 0.449) { m_maxSigs = 8; }
        else if (security > 0.249) { m_maxSigs = 5; }
        else                       { m_maxSigs = 3; }
    }

    m_procTimer.Start(1000); // Initial 5s timer to ensure everything gets loaded correctly

    // these use config option to (en/dis)able individual types
    m_Grav = sConfig.exploring.Gravametric;
    m_Mag = sConfig.exploring.Magnetometric;
    m_Ladar = sConfig.exploring.Ladar;
    m_Radar = sConfig.exploring.Radar;
    m_Unrated = sConfig.exploring.Unrated;
    m_Complex = sConfig.exploring.Complex;
    // the caps above are counts of sites made so far (< 0 = disabled)
    if (m_Grav > 0) m_Grav = 0;
    if (m_Mag > 0) m_Mag = 0;
    if (m_Ladar > 0) m_Ladar = 0;
    if (m_Radar > 0) m_Radar = 0;
    if (m_Unrated > 0) m_Unrated = 0;
    if (m_Complex > 0) m_Complex = 0;

    // w-space has the Sleepers' sites for its class
    if (IsWSpaceID(m_system->GetID())) {
        uint8 whClass(sDataMgr.GetWHSystemClass(m_system->GetID()));
        if ((whClass > 0) and (whClass < 7))
            m_whClass = whClass;
    }

    if (m_whClass != 0) {
        InitWSpace();
    } else {
        // populate the type vector with all of the anomaly types we will use in this system
        for (int i = 0; i < m_maxSigs; i++) { // anomaly and signature count should be accounted for while fillimg the typeList vector
            uint8 typeID(GetDungeonType());
            if (typeID != 0)
                m_typeList.push_back(typeID);
        }
        // add the required anomalies to the list so they're created at the right time
        for (int i = 0; i < (m_maxSigs / 2); i++) {
            m_typeList.push_back(Dungeon::Type::Anomaly);
        }
    }

    // infrastructure hub upgrades of this system add sites
    SetUpgradeBonus(svDataMgr.GetActiveUpgradeLevel(m_system->GetID(), Sov::Upgrade::PirateDetection),
                    svDataMgr.GetActiveUpgradeLevel(m_system->GetID(), Sov::Upgrade::SurveyNetworks),
                    svDataMgr.GetActiveUpgradeLevel(m_system->GetID(), Sov::Upgrade::OreProspecting));

    /* load current data?, start timers, process current data, and create new items, if needed */
    /** @todo all anomalies are currently temp items.  if/when we start saving them, create new table and itemIDs*/

    _log(COSMIC_MGR__INIT, "AnomalyMgr Initialized for %s(%u) with %u Max Signals for security class %0.2f.  Test Server %s", \
                m_system->GetName(), m_system->GetID(), m_maxSigs, security, sConfig.debug.IsTestServer?"enabled":"disabled");

    return (m_initalized = true);
}

void AnomalyMgr::Process() {
    if (!m_initalized)
        return;
    if (m_procTimer.Check(/*!sConfig.debug.IsTestServer*/)) {
        // Only generate new signals when a player is in the system
        if (m_system->PlayerCount() > 0 && m_typeList.size () > 0) {
            auto cur = m_typeList.begin();
            auto end = m_typeList.end();

            for (; cur != end; cur++) {
                CreateAnomaly (*cur);
            }

            m_typeList.clear();
        }
        // Once initial spawn is complete, set correct timer based upon whether this is test server or not
        if (m_firstSpawn) {
            if (sConfig.debug.IsTestServer) {
                m_procTimer.SetTimer(10000);  // 10s
            } else {
                m_procTimer.SetTimer(120000);  // 2m
            }
            m_firstSpawn = false;
        }
    }

    // completed and expired sites are replaced elsewhere in the system (expiry is run by DungeonMgr)
    if (!m_respawnQueue.empty() and (m_system->PlayerCount() > 0)) {
        int64 now(Win32TimeNow());
        while (!m_respawnQueue.empty() and (m_respawnQueue.begin()->first <= now)) {
            uint8 typeID(m_respawnQueue.begin()->second);
            m_respawnQueue.erase(m_respawnQueue.begin());
            CreateAnomaly(typeID);
        }
    }
}

void AnomalyMgr::Close()
{
    _log(COSMIC_MGR__MESSAGE, "Closing AnomalyMgr for %s(%u).", m_system->GetName(), m_system->GetID());
    // dungeon sites go with the system (their rows are removed by the system unload).  new ones are made on the next boot
    m_respawnQueue.clear();
    m_typeList.clear();
}

void AnomalyMgr::LoadAnomalies() {
    // check for existing data and load accordingly.
    // this will only hit on system load
    DBQueryResult res;
    m_mdb.GetAnomaliesBySystem(m_system->GetID(), res);
    DBResultRow row;
    while (res.GetRow(row)) {
        //sigID,sigItemID,dungeonType,sigName,systemID,sigTypeID,sigGroupID,scanGroupID,scanAttributeID,wormholeID,x,y,z
        CosmicSignature sig = CosmicSignature();
        sig.sigID = row.GetText(0);
        sig.sigItemID = row.GetInt(1);
        sig.dungeonType = row.GetInt(2);
        sig.sigName = row.GetText(3);
        sig.systemID = row.GetInt(4);
        sig.sigTypeID = row.GetInt(5);
        sig.sigGroupID = row.GetInt(6);
        sig.scanGroupID = row.GetInt(7);
        sig.sigStrength = 0.1f;     // wormholes (1/10, as WormholeMgr makes them) are the only rows kept
        sig.scanAttributeID = row.GetInt(8);
        sig.position.x = row.GetDouble(9);
        sig.position.y = row.GetDouble(10);
        sig.position.z = row.GetDouble(11);

        // only wormholes outlive their system.  a dungeon row is stale (its items are gone)
        if (sig.dungeonType != Dungeon::Type::Wormhole) {
            m_mdb.DeleteAnomaly(sig.sigID);
            continue;
        }
        m_sigBySigID.emplace(sig.sigID, sig);
        if (sig.dungeonType == Dungeon::Type::Anomaly) {
            m_anomByItemID.emplace(sig.sigItemID, sig);
            ++m_Anoms;
        } else {
            m_sigByItemID.emplace(sig.sigItemID, sig);

            // wormholes (statics, K162s, wanderers) are WormholeMgr's.  m_WH counts the wanderers this system rolls
            if (sig.dungeonType != Dungeon::Type::Wormhole)
                ++m_Sigs;
        }

        _log(COSMIC_MGR__MESSAGE, "AnomalyMgr::LoadAnomalies() - Created Signal %s(%u) for %s in %s(%u), bubbleID %u with %.3f%% sigStrength.", \
        sDunDataMgr.GetDungeonType(sig.dungeonType), sig.dungeonType, \
        sig.sigName.c_str(), m_system->GetName(), sig.systemID, sig.bubbleID, sig.sigStrength *100);
    }

}

void AnomalyMgr::SaveAnomaly(CosmicSignature& sig)
{
    // every live site is in sysSignatures while it exists (map deadspace complexes, wormhole reload)
    m_mdb.SaveAnomaly(sig);
}

void AnomalyMgr::GetSignatureList(std::vector<CosmicSignature>& sig)
{
    // sysSignatures (sigID,sigItemID,dungeonType,sigName,systemID,sigTypeID,sigGroupID,scanGroupID,scanAttributeID,x,y,z)
    // retrieval method for scan queries
    for (auto cur : m_sigByItemID)
        sig.push_back(cur.second);
}

void AnomalyMgr::GetAnomalyList(std::vector<CosmicSignature>& sig) {
    // sysSignatures (sigID,sigItemID,dungeonType,sigName,systemID,sigTypeID,sigGroupID,scanGroupID,scanAttributeID,x,y,z)
    // retrieval method for scan queries
    for (auto cur : m_anomByItemID)
        sig.push_back(cur.second);
}

void AnomalyMgr::CreateAnomaly(int8 typeID)
{
    // compile data for new system anomaly.
    CosmicSignature sig = CosmicSignature();
        sig.systemID = m_system->GetID();
        sig.sigID = sEntityList.GetAnomalyID();
        // *Mgr will determine name, itemID and sigStrength.
        sig.sigItemID = 0;
        sig.sigName = "";   // set by DungeonMgr from the dungeon
        //default to 1/80
        sig.sigStrength = 0.0125;
        // default to rogue drones
        sig.ownerID = factionRogueDrones;
    if (m_whClass != 0) {
        sig.ownerID = factionSleepers;
    } else if (sConfig.debug.AnomalyFaction) {
        sig.ownerID = sConfig.debug.AnomalyFaction;
    } else if (MakeRandomFloat() > 0.1) { // 10% chance to be rogue drones
        sig.ownerID = sDataMgr.GetRegionRatFaction(m_system->GetRegionID());
    }

    // there are some regions that dont have rat factions....what do we do in that case??
    //if (sig.ownerID == 0)
    //    return;     // make error here?

    sig.dungeonType = typeID;

    if (sig.dungeonType == 0) {
        _log(COSMIC_MGR__ERROR, "Dungeon Type returned 0 for %s in %s(%u)", \
                    sig.sigName.c_str(), m_system->GetName(), m_system->GetID());
        return;
    }

    sig.position = GetSitePosition();

    // some sites will use sys sov for ships.  use this for them....
    // sig.ownerID = sDataMgr.GetRegionFaction(m_system->GetRegionID());
    switch(sig.dungeonType) {
        case Dungeon::Type::Gravimetric: { // 2
            sig.sigTypeID = EVEDB::invTypes::CosmicSignature;
            sig.sigGroupID = EVEDB::invGroups::Cosmic_Signature;
            sig.scanGroupID = Scanning::Group::Signature;
            sig.scanAttributeID = AttrScanGravimetricStrength;
        } break;
        case Dungeon::Type::Magnetometric: { // 3,
            sig.sigTypeID = EVEDB::invTypes::DeadspaceSignature;// need probes and exploring skills
            sig.sigGroupID = EVEDB::invGroups::Cosmic_Signature;
            sig.scanGroupID = Scanning::Group::Signature;
            sig.scanAttributeID = AttrScanMagnetometricStrength;
        } break;
        case Dungeon::Type::Radar: {       // 4,
            sig.sigTypeID = EVEDB::invTypes::DeadspaceSignature;
            sig.sigGroupID = EVEDB::invGroups::Cosmic_Signature;
            sig.scanGroupID = Scanning::Group::Signature;
            sig.scanAttributeID = AttrScanRadarStrength;
        } break;
        case Dungeon::Type::Ladar: {       // 5,
            sig.sigTypeID = EVEDB::invTypes::DeadspaceSignature;
            sig.sigGroupID = EVEDB::invGroups::Cosmic_Signature;
            sig.scanGroupID = Scanning::Group::Signature;
            sig.scanAttributeID = AttrScanLadarStrength;
        } break;
        case Dungeon::Type::Wormhole: {    // 6
            // enable WH to be warped to...they are deco only at this time.
            //  once working, these will be by probe only, and removed from anomaly list
            sig.sigTypeID = EVEDB::invTypes::CosmicSignature;
            sig.sigGroupID = EVEDB::invGroups::Wormhole;
            sig.scanGroupID = Scanning::Group::Signature;
            sig.scanAttributeID = AttrScanAllStrength;  // Unknown
            // WormholeMgr makes the wormhole and puts it on our scanner (RegisterWormhole)
            sWHMgr.Create(sig);
            return;
        } break;
        case Dungeon::Type::Anomaly: {      // 7   simple combat sites
            sig.sigTypeID = EVEDB::invTypes::CosmicAnomaly;
            sig.sigGroupID = EVEDB::invGroups::Cosmic_Anomaly;
            sig.scanGroupID = Scanning::Group::Anomaly;
            sig.scanAttributeID = AttrScanAllStrength;
            sig.sigStrength = 1.0f;
        } break;
        case Dungeon::Type::Mission: {      // 1
            sig.sigTypeID = EVEDB::invTypes::CosmicSignature;
            sig.sigGroupID = EVEDB::invGroups::Cosmic_Signature;
            sig.scanGroupID = Scanning::Group::Signature;
            sig.scanAttributeID = AttrScanAllStrength;  // Unknown
            // we're not counting mission shit in sig count
        }
        // these will use default for now.  revisit later when system matures more and i better understand how to implement them.
        case Dungeon::Type::Escalation:   // 9
        case Dungeon::Type::Unrated:       // 8
        case Dungeon::Type::Rated: { // 10
            sig.sigTypeID = EVEDB::invTypes::DeadspaceSignature;
            sig.sigGroupID = EVEDB::invGroups::Cosmic_Signature;
            sig.scanGroupID = Scanning::Group::Signature;
            sig.scanAttributeID = AttrScanAllStrength;  // Unknown
        } break;
        // error or denied
        case 0:
        default:
            return;
    }

    // create and register here
    // all anomalies will be created/populated by dungMgr, except WH (handed off to WHMgr above)
    //  the dungeon:  this faction's for the system's true sec, else any faction's.  in w-space the Sleepers' of its class
    float trueSec(m_system->GetSystemSecurityRating());
    uint32 dungeonID(0);
    if (m_whClass != 0) {
        dungeonID = sDunDataMgr.PickDungeon(sig.dungeonType, factionSleepers, trueSec, 0, m_whClass);
    } else {
        dungeonID = sDunDataMgr.PickDungeon(sig.dungeonType, sig.ownerID, trueSec);
        if (dungeonID == 0)
            dungeonID = sDunDataMgr.PickDungeon(sig.dungeonType, 0, trueSec);
    }
    std::shared_ptr<const Dungeon::Dungeon> pDungeon(sDunDataMgr.GetDungeon(dungeonID));
    if (pDungeon.get() == nullptr) {
        _log(COSMIC_MGR__WARNING, "AnomalyMgr::CreateAnomaly() - no %s dungeon for %s(%u) (true sec %.2f)",
                sDunDataMgr.GetDungeonType(sig.dungeonType), m_system->GetName(), m_system->GetID(), trueSec);
        return;
    }
    Dungeon::SpawnParams params;
        params.dungeonID = dungeonID;
        params.archetype = sig.dungeonType;
        // a faction's own dungeon keeps its faction
        params.factionID = (pDungeon->factionID != 0 ? 0 : sig.ownerID);
        params.kind = (sig.scanGroupID == Scanning::Group::Anomaly ? Dungeon::Kind::Anomaly : Dungeon::Kind::Signature);
        params.usePosition = true;
        params.position = sig.position;
    if (m_dungMgr->SpawnInstance(params, &sig) == 0) // pass by ref here, so other vars can be set.
        return;
    // add new sig to sysSigMaps
    m_sigBySigID.emplace(sig.sigID, sig);
    if (sig.scanGroupID == Scanning::Group::Anomaly) {
        m_anomByItemID.emplace(sig.sigItemID, sig);
        ++m_Anoms;
    } else {
        m_sigByItemID.emplace(sig.sigItemID, sig);
        ++m_Sigs;
    }

    // Save anomaly to the database for later loading
    SaveAnomaly(sig);

    _log(COSMIC_MGR__MESSAGE, "AnomalyMgr::Create() - Created Signal %s(%u) for %s in %s(%u), bubbleID %u with %.3f%% sigStrength.", \
            sDunDataMgr.GetDungeonType(sig.dungeonType), sig.dungeonType, \
            sig.sigName.c_str(), m_system->GetName(), sig.systemID, sig.bubbleID, sig.sigStrength *100);
}

GPoint AnomalyMgr::GetSitePosition()
{
    // a random spot around a planet, inside the system and off any other grid
    SystemData sysData = SystemData();
    sDataMgr.GetSystemData(m_system->GetID(), sysData);
    double maxDist(sysData.radius * 0.9);
    GPoint pos(sMapData.GetAnomalyPoint(m_system));
    for (uint8 tries = 0; tries < 10; ++tries) {
        if ((maxDist > 0) and (pos.length() > maxDist)) {
            GVector dir(pos);
            dir.normalize();
            pos = dir * (maxDist * MakeRandomFloat(0.33, 1.0));
        }
        bool clear(sBubbleMgr.FindBubble(m_system->GetID(), pos) == nullptr);
        for (auto& cur : m_sigBySigID)
            if (cur.second.position.distance(pos) < (2 * BUBBLE_RADIUS_METERS))
                clear = false;
        if (clear)
            break;
        pos = sMapData.GetAnomalyPoint(m_system);
    }
    return pos;
}

void AnomalyMgr::OnSiteCompleted(const CosmicSignature& sig)
{
    // the site leaves the scanner now, and another of its type comes later
    RemoveSignal(sig.sigItemID);
    m_mdb.DeleteAnomaly(sig.sigID);
    if (sig.scanGroupID == Scanning::Group::Anomaly) {
        if (m_Anoms > 0)
            --m_Anoms;
    } else if (m_Sigs > 0) {
        --m_Sigs;
    }
    // sites an upgrade no longer pays for aren't replaced
    auto itr = m_skip.find(sig.dungeonType);
    if ((itr != m_skip.end()) and (itr->second > 0)) {
        --itr->second;
        return;
    }
    m_respawnQueue.emplace(Win32TimeNow() + (sConfig.dungeon.RespawnDelay * EvE::Time::Second), sig.dungeonType);
}

void AnomalyMgr::QueueSite(uint8 dungeonType, uint32 delaySeconds)
{
    m_respawnQueue.emplace(Win32TimeNow() + (delaySeconds * EvE::Time::Second), dungeonType);
}

void AnomalyMgr::SetUpgradeBonus(uint8 combatLevel, uint8 exploreLevel, uint8 oreLevel/*0*/)
{
    /* infrastructure hub upgrades:  pirate detection arrays add combat anomalies (2 per level),
     *  survey networks radar/magnetometric sites and ore prospecting arrays gravimetric sites (1 per level).
     *  more sites come with the next spawn round;  fewer sites aren't replaced when completed
     */
    auto apply = [this](uint8& current, uint8 wanted, uint8 typeA, uint8 typeB) {
        for (uint8 i = current; i < wanted; ++i) {
            uint8 typeID((i % 2) ? typeB : typeA);
            if (m_skip[typeID] > 0) {
                --m_skip[typeID];
            } else {
                m_typeList.push_back(typeID);
            }
        }
        for (uint8 i = wanted; i < current; ++i)
            ++m_skip[((i % 2) ? typeB : typeA)];
        current = wanted;
    };
    apply(m_bonusCombat, combatLevel *2, Dungeon::Type::Anomaly, Dungeon::Type::Anomaly);
    apply(m_bonusExplore, exploreLevel, Dungeon::Type::Radar, Dungeon::Type::Magnetometric);
    apply(m_bonusOre, oreLevel, Dungeon::Type::Gravimetric, Dungeon::Type::Gravimetric);
}

// a wormhole end WormholeMgr made in this system
void AnomalyMgr::RegisterWormhole(CosmicSignature &sig)
{
    m_sigBySigID.emplace(sig.sigID, sig);
    m_sigByItemID.emplace(sig.sigItemID, sig);

    // Save anomaly to the database for later loading
    SaveAnomaly(sig);

    _log(COSMIC_MGR__MESSAGE, "AnomalyMgr::RegisterWormhole() - Created Signal %s(%u) for %s in %s(%u), bubbleID %u with %.3f%% sigStrength.", \
            sDunDataMgr.GetDungeonType(sig.dungeonType), sig.dungeonType, \
            sig.sigName.c_str(), m_system->GetName(), sig.systemID, sig.bubbleID, sig.sigStrength *100);
}

uint8 AnomalyMgr::GetDungeonType()
{
    // a few tries for a type this system allows.  0 when nothing is allowed
    float trueSec(m_system->GetSystemSecurityRating());
    for (uint8 tries = 0; tries < 20; ++tries) {
        uint8 typeID = MakeRandomInt(2,10); // skip typeMission
        // a type with no dungeon this system could get is a wasted slot (gas sites, DED complexes)
        if ((typeID != Dungeon::Type::Wormhole) and (typeID != Dungeon::Type::Mission) and (typeID != Dungeon::Type::Escalation)
        and (sDunDataMgr.PickDungeon(typeID, 0, trueSec) == 0))
            continue;
        switch(typeID) {
            case Dungeon::Type::Escalation:  // 9
            case Dungeon::Type::Mission: {   // 1
                // cannot create this type here.  try again.
                continue;
            } break;
            case Dungeon::Type::Gravimetric: {   // 2
                if (m_Grav < 0)
                    continue;
                ++m_Grav;
            } break;
            case Dungeon::Type::Magnetometric: {   // 3
                if (m_Mag < 0)
                    continue;
                ++m_Mag;
            } break;
            case Dungeon::Type::Radar: {   // 4
                if (m_Radar < 0)
                    continue;
                ++m_Radar;
            } break;
            case Dungeon::Type::Ladar: {   // 5
                if (m_Ladar < 0)
                    continue;
                ++m_Ladar;
            } break;
            case Dungeon::Type::Wormhole: {   // 6
                // cap at 1 per system, except k162...which ISNT created in this system (it's an exit, from WMS)
                //  this will need updating and be controlled by WMS.  it will have full control of WH amounts/locations
                if (m_WH != 0)
                    continue;
                ++m_WH;
            } break;
            case Dungeon::Type::Anomaly: {   // 7. this is noob dungeon, no probe required
                if (!(m_Anoms < (m_maxSigs / 2)))
                    continue;
            } break;
            case Dungeon::Type::Unrated: {   // 8
                if ((m_Unrated < 0) or (m_Unrated > 2)) // cap at 3
                    continue;
                ++m_Unrated;
            } break;
            case Dungeon::Type::Rated: {  // 10
                if ((m_Complex < 0) or (m_Complex > 1)) // cap at 2
                    continue;
                ++m_Complex;
            } break;
        }

        _log(COSMIC_MGR__MESSAGE, "AnomalyMgr::GetDungeonType() - Returning %s(%u)", sDunDataMgr.GetDungeonType(typeID), typeID);
        return typeID;
    }

    return 0;
}

void AnomalyMgr::InitWSpace()
{
    /* Crucible w-space (reports/wormholes-research.md 3.4-3.5, 4.4-4.6):  the Sleepers' combat anomalies, radar and
     *  magnetometric sites, gas (ladar) and ore (gravimetric) sites of the system's class, and a wandering wormhole half
     *  the time (the statics are WormholeMgr's).  no Crucible source gives the counts:  these are picks.
     *  a completed site is replaced by one of its type (OnSiteCompleted)
     */
    m_maxSigs = 8;
    uint8 anoms(MakeRandomInt(3, 5));
    for (uint8 i = 0; i < anoms; ++i)
        m_typeList.push_back(Dungeon::Type::Anomaly);
    // the config caps:  < 0 is switched off
    auto add = [this](int8& count, uint8 typeID, int number) {
        if (count < 0)
            return;
        for (int i = 0; i < number; ++i) {
            m_typeList.push_back(typeID);
            ++count;
        }
    };
    add(m_Radar, Dungeon::Type::Radar, MakeRandomInt(0, 2));
    add(m_Mag, Dungeon::Type::Magnetometric, MakeRandomInt(0, 2));
    add(m_Ladar, Dungeon::Type::Ladar, MakeRandomInt(1, 2));
    add(m_Grav, Dungeon::Type::Gravimetric, MakeRandomInt(0, 1));
    if ((m_WH == 0) and (MakeRandomFloat() < 0.5)) {
        m_typeList.push_back(Dungeon::Type::Wormhole);
        ++m_WH;
    }
    _log(COSMIC_MGR__INIT, "AnomalyMgr::InitWSpace() - %s(%u) is class %u:  %u Sleeper sites to make", m_system->GetName(),
            m_system->GetID(), m_whClass, (uint32)m_typeList.size());
}

uint32 AnomalyMgr::GetAnomalyID(std::string& sigID)
{   // <std::string, CosmicSignature>
    std::map<std::string, CosmicSignature>::iterator itr = m_sigBySigID.find(sigID);
    if (itr != m_sigBySigID.end())
        return itr->second.sigItemID;
    return 0;
}

GPoint AnomalyMgr::GetAnomalyPos(std::string& sigID)
{
    GPoint pos(NULL_ORIGIN);
    std::map<std::string, CosmicSignature>::iterator itr = m_sigBySigID.find(sigID);
    if (itr != m_sigBySigID.end())
        pos = itr->second.position;

    return pos;
}


void AnomalyMgr::AddSignal(SystemEntity* pSE, uint32 id/*0*/)
{
    if (!m_initalized)
        return;

    //  shouldnt need this while `check(IsTestServer)` is disabled
    //if (!m_procTimer.Enabled())
    //    m_procTimer.Start(120000);  // 2m

    /* ALL SEs sent here by SysMgr except Globals and NPCs
     * we will determine here what is added and it's base sigStrength.
     * this is sill WIP.
     *   see Scan.xmlp and EVE_Scanning.h for client data
     */
    InventoryItemRef iRef = pSE->GetSelf();
    if (iRef.get() == nullptr)
        return; // we'll get over it.
    CosmicSignature sig = CosmicSignature();
        sig.dungeonType = Dungeon::Type::Anomaly;
        sig.ownerID = iRef->ownerID();
        if (id) {
            sig.sigID = id;
        } else {
            sig.sigID = sEntityList.GetAnomalyID();
        }
        sig.sigItemID = iRef->itemID();
        sig.sigName = iRef->itemName();
        sig.systemID = m_system->GetID();
        sig.position = iRef->position();
        // this will be wrong for capital ships and larger (maybe bs also)
        sig.sigStrength = iRef->GetAttribute(AttrSignatureRadius).get_float() /1000;
        // get bubbleID, which is only used for .siglist command
        sig.bubbleID = pSE->SysBubble()->GetID();

/* Signal Strength Base Data
 * Band        1/5     1/10    1/15    1/20    1/25    1/40    1/45    1/60    1/80
 * Percentage  20.0%   10.0%   6.67%   5.0%    4.0%    2.5%    2.22%   1.67%   1.25%
 */

    // if scanGroupID is anom or sig, use scanAttributeID to determine site type (in client code)
    //    scanGroupID must be one of the 5 groups coded in client (sig, anom, ship, drone, structure)
    //    scanGroupID of sig and anom are cached on client side (min time is 5m)
    switch (iRef->categoryID()) {
        case EVEDB::invCategories::Orbitals:
        case EVEDB::invCategories::Structure:
        case EVEDB::invCategories::StructureUpgrade:
        case EVEDB::invCategories::SovereigntyStructure: {
            sig.sigTypeID = iRef->typeID();
            sig.sigGroupID = iRef->groupID();
            sig.scanGroupID = Scanning::Group::Structure;
            sig.scanAttributeID = AttrScanStrengthStructures;
            //sig.sigStrength = 1.0;
        } break;
        case EVEDB::invCategories::Ship: {
            //511     shipScanResistance
            sig.sigTypeID = iRef->typeID();
            sig.sigGroupID = iRef->groupID();
            sig.scanGroupID = Scanning::Group::Ship;
            sig.scanAttributeID = AttrScanStrengthShips;
            //sig.sigStrength = 1.0;
        } break;
        case EVEDB::invCategories::Drone:
        case EVEDB::invCategories::Charge:          // probes, missiles (at time of scan), and ??
        case EVEDB::invCategories::Deployable: {    // mobile warp disruptor
            sig.sigTypeID = iRef->typeID();
            sig.sigGroupID = iRef->groupID();
            sig.scanGroupID = Scanning::Group::DroneOrProbe;
            sig.scanAttributeID = AttrScanStrengthDronesProbes;
            sig.sigStrength = 0.0667;
        } break;
        case EVEDB::invCategories::Asteroid:{   // this wont hit (addSignal=false)
            return;     // we're not adding roids to list
        };
        case EVEDB::invCategories::Entity:
        case EVEDB::invCategories::Celestial:       //wrecks
        default:  {
            // wrecks, cans and other objects aren't scanner results (they used to show as cosmic anomalies).
            //  real anomalies come from CreateAnomaly()/LoadAnomalies()
            return;
        } break;
    }

    _log(COSMIC_MGR__MESSAGE, "AnomalyMgr::AddSignal() - adding %s to anomaly list as %s(%u) with %.3f%% sigStrength.", \
                iRef->name(), GetScanGroupName(sig.scanGroupID), sig.scanGroupID, sig.sigStrength *100);

    // add new sig to our maps, but these are not added to anom/sig counts
    m_sigBySigID.emplace(sig.sigID, sig);
    if (sig.scanGroupID == Scanning::Group::Anomaly) {
        m_anomByItemID.emplace(sig.sigItemID, sig);
    } else {
        m_sigByItemID.emplace(sig.sigItemID, sig);
    }
}

void AnomalyMgr::RemoveSignal(uint32 itemID)
{
    _log(COSMIC_MGR__MESSAGE, "AnomalyMgr::RemoveSignal() - removing %u from anomaly list.", itemID);
    // remove sig from our map
    std::map<uint32, CosmicSignature>::iterator itr = m_sigByItemID.find(itemID);
    if (itr != m_sigByItemID.end()) {
        auto itr2 = m_sigBySigID.find(itr->second.sigID);
        if (itr2 != m_sigBySigID.end())
            m_sigBySigID.erase(itr2);
        m_sigByItemID.erase(itr);
    } else {  // not Signature, check in Anomaly map
        itr = m_anomByItemID.find(itemID);
        if (itr != m_anomByItemID.end()) {
            auto itr2 = m_sigBySigID.find(itr->second.sigID);
            if (itr2 != m_sigBySigID.end())
                m_sigBySigID.erase(itr2);
            m_anomByItemID.erase(itr);
        }
    }
}

const char* AnomalyMgr::GetScanGroupName(uint8 groupID/*0*/) {
    using namespace Scanning::Group;
    switch(groupID) {
        case Scrap:             return "Scrap";
        case Signature:         return "Signature";
        case Structure:         return "Structure";
        case DroneOrProbe:      return "Drone/Probe";
        case Celestial:         return "Celestial";
        case Anomaly:           return "Anomaly";
        case Ship:              return "Ship";
        case 0:                 return "Zero";
        default:                return "Invalid";
    }
    return "None";
}
