
/**
  * @name SovereigntyDataMgr.cpp
  *   memory object caching system for managing and saving ingame static and dynamic station data
  *
  * @Author:         James
  * @date:           21Apr13
  *
  */

#include <boost/range/iterator_range.hpp>
#include "system/sov/SovereigntyDataMgr.h"
#include "database/EVEDBUtils.h"
#include "packets/Sovereignty.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "Client.h"
#include "inventory/Inventory.h"
#include "inventory/ItemFactory.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"

uint8 DevIndex::LevelForPoints(int64 points)
{
    uint8 level(0);
    while ((level < 5) and (LevelSize * (level + 1) < points))
        ++level;
    return level;
}

uint8 DevIndex::BandForPoints(int64 points)
{
    if (points <= 0)
        return 0;
    int64 band(points / LevelSize);
    return (band > 5 ? 5 : (uint8)band);
}

uint8 DevIndex::LevelForClaimDays(int64 days)
{
    for (uint8 level = 5; level > 0; --level)
        if (days >= TimeLevelDays[level])
            return level;
    return 0;
}

SovereigntyDataMgr::SovereigntyDataMgr()
{
    m_sovData.clear();
}

SovereigntyDataMgr::~SovereigntyDataMgr()
{
    //Clear();
}

int SovereigntyDataMgr::Initialize()
{
    Populate();
    LoadDevIndices();
    LoadActiveUpgrades();
    sLog.Blue("       SovDataMgr", "Sovereignty Data Manager Initialized.");
    return 1;
}

void SovereigntyDataMgr::Close()
{
    // claims are written as they change.  only the development indices are kept in memory
    SaveDevIndices();

    sLog.Warning("       SovDataMgr", "Sovereignty Data Manager has been closed.");
}

void SovereigntyDataMgr::Clear()
{
    m_sovData.clear();
    m_devIndex.clear();
    m_activeUpgrades.clear();
    m_upgradeDirty.clear();
}

void SovereigntyDataMgr::ReadRow(DBResultRow& row, SovereigntyData& sData)
{
    // solarSystemID, constellationID, regionID, corporationID, allianceID, claimStructureID, claimTime, hubID, contested, claimID, beaconID, jammerID
    sData.solarSystemID = row.GetUInt(0);
    sData.constellationID = row.GetUInt(1);
    sData.regionID = row.GetUInt(2);
    sData.corporationID = row.GetUInt(3);
    sData.allianceID = row.GetUInt(4);
    sData.claimStructureID = row.GetUInt(5);
    sData.claimTime = row.GetInt64(6);
    sData.hubID = row.GetUInt(7);
    sData.contested = row.GetUInt(8);
    sData.claimID = row.GetUInt(9);
    sData.beaconID = row.GetUInt(10);
    sData.jammerID = row.GetUInt(11);
}

void SovereigntyDataMgr::Populate()
{
    double start = GetTimeMSeconds();
    DBQueryResult *res = new DBQueryResult();
    DBResultRow row;

    //This data is added by solar system, so we need to use that tag here
    auto &bySolar = m_sovData.get<SovDataBySolarSystem>();

    SovereigntyDB::GetSovereigntyData(*res);
    while (res->GetRow(row))
    {
        SovereigntyData sData = SovereigntyData();
        ReadRow(row, sData);
        bySolar.insert(sData);
    }

    sLog.Cyan("       SovDataMgr", "%lu Sovereignty data sets loaded in %.3fms.",
              bySolar.size(), (GetTimeMSeconds() - start));

    //cleanup
    SafeDelete(res);
}

void SovereigntyDataMgr::LoadDevIndices()
{
    double start = GetTimeMSeconds();
    int64 now = GetFileTimeNow();
    DBQueryResult res;
    DBResultRow row;
    SovereigntyDB::LoadDevIndices(res);
    while (res.GetRow(row)) {
        // solarSystemID, militaryPoints, industrialPoints, militaryIncreasing, industrialIncreasing, lastDecayTime
        DevIndexData data = DevIndexData();
        data.systemID = row.GetUInt(0);
        data.military = row.GetInt64(1);
        data.industrial = row.GetInt64(2);
        data.militaryIncreasing = row.GetBool(3);
        data.industrialIncreasing = row.GetBool(4);
        data.lastDecay = row.GetInt64(5);
        // catch up on the decay missed while the server was down
        Decay(data, now, false);
        m_devIndex[data.systemID] = data;
    }

    sLog.Cyan("       SovDataMgr", "%lu Development index sets loaded in %.3fms.",
              m_devIndex.size(), (GetTimeMSeconds() - start));
}

void SovereigntyDataMgr::GetInfo()
{
    /* return info about loaded items? */
    /*
     * m_sovData;
     *
     */
}

uint32 SovereigntyDataMgr::GetSystemAllianceID(uint32 systemID)
{
    auto it = m_sovData.get<SovDataBySolarSystem>().find(systemID);
    if (it != m_sovData.get<SovDataBySolarSystem>().end())
    {
        SovereigntyData sData = *it;
        return sData.allianceID;
    }
    return 0;
}

SovereigntyData SovereigntyDataMgr::GetSovereigntyData(uint32 systemID)
{
    auto it = m_sovData.get<SovDataBySolarSystem>().find(systemID);
    if (it != m_sovData.get<SovDataBySolarSystem>().end())
    {
        SovereigntyData sData = *it;
        return sData;
    } else
    {
        //Return empty object in case we don't find anything
        SovereigntyData sData = SovereigntyData();
        return sData;
    }
}

PyRep *SovereigntyDataMgr::GetSystemSovereignty(uint32 systemID)
{
    SystemData sysData = SystemData();
    PyDict *args = new PyDict();

    //Figure out if this system is in empire space
    sDataMgr.GetSystemData(systemID, sysData);

    if (sysData.factionID)
    {   // If we have a factionID, the system is not claimable.
        // We dont need to worry about FacWar Sov here, that is handled separately by Handle_GetSystemOccupier
        _log(SOV__INFO, "SovereigntyDataMgr::GetSystemSovereignty(): Faction system %u found, Sending no SovData.", systemID);
        PyDecRef(args);
        return new PyNone();
    }
    else
    {
        auto it = m_sovData.get<SovDataBySolarSystem>().find(systemID);
        if (it != m_sovData.get<SovDataBySolarSystem>().end())
        {
            SovereigntyData sData = *it;
            _log(SOV__INFO, "SovereigntyDataMgr::GetSystemSovereignty(): Found sov data for solarSystemID %u.", systemID);
            _log(SOV__DEBUG, "== data (GetSystemSovereignty()) ==");
            _log(SOV__DEBUG, "claimID: %u", sData.claimID);
            _log(SOV__DEBUG, "solarSystemID: %u", sData.solarSystemID);
            _log(SOV__DEBUG, "constellationID: %u", sData.constellationID);
            _log(SOV__DEBUG, "corporationID: %u", sData.corporationID);
            _log(SOV__DEBUG, "allianceID: %u", sData.allianceID);
            _log(SOV__DEBUG, "claimStructureID: %u", sData.claimStructureID);
            _log(SOV__DEBUG, "==========");
            args->SetItemString("contested", new PyInt(sData.contested));
            args->SetItemString("corporationID", new PyInt(sData.corporationID));
            args->SetItemString("claimTime", new PyLong(sData.claimTime));
            args->SetItemString("claimStructureID", new PyInt(sData.claimStructureID));
            args->SetItemString("hubID", new PyInt(sData.hubID));
            args->SetItemString("allianceID", new PyInt(sData.allianceID));
            args->SetItemString("solarSystemID", new PyInt(sData.solarSystemID));
        }
        else
        {
            // No data means no claim, so we give 'None'
            _log(SOV__INFO, "SovereigntyDataMgr::GetSystemSovereignty(): No data for solarSystemID %u. Sending blank SovereigntyData object.", systemID);
            PyDecRef(args);
            return new PyNone();
        }
    }
    return new PyObject("util.KeyVal", args);
}

PyRep *SovereigntyDataMgr::GetAllianceSystems() //Get all systems associated with all alliances?
{
    PyList *list = new PyList();
    for (auto &it : m_sovData.get<SovDataBySolarSystem>())
    {
        _log(SOV__DEBUG, "== data (GetAllianceSystems()) ==");
        _log(SOV__DEBUG, "claimID: %u", it.claimID);
        _log(SOV__DEBUG, "solarSystemID: %u", it.solarSystemID);
        _log(SOV__DEBUG, "allianceID: %u", it.allianceID);
        _log(SOV__DEBUG, "==========");

        PyDict *dict = new PyDict();
        dict->SetItemString("allianceID", new PyInt(it.allianceID));
        dict->SetItemString("solarSystemID", new PyInt(it.solarSystemID));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
    return list;
}

PyRep *SovereigntyDataMgr::GetAllianceBeacons(uint32 allianceID) //Get all beacons associated with current alliance
{
    PyList* list = new PyList();
    for (SovereigntyData const &sData : boost::make_iterator_range(
                 m_sovData.get<SovDataByAlliance>().equal_range(allianceID)))
    {
        // Don't add systems where no beacon exists
        if (sData.beaconID == 0) {
            continue;
        }
        _log(SOV__DEBUG, "== data (GetAllianceBeacons()) ==");
        _log(SOV__DEBUG, "solarSystemID: %u", sData.solarSystemID);
        _log(SOV__DEBUG, "beaconID: %u", sData.beaconID);
        _log(SOV__DEBUG, "==========");

        PyTuple* tuple = new PyTuple(3);
        tuple->SetItem(0, new PyInt(sData.solarSystemID));
        tuple->SetItem(1, new PyInt(sData.beaconID));
        tuple->SetItem(2, new PyInt(EVEDB::invTypes::CynosuralGeneratorArray));
        list->AddItem(tuple);
    }
    return list;
}

PyRep *SovereigntyDataMgr::GetCurrentSovData(uint32 locationID)
{
    /*  sovSvc.GetCurrentData():  a solar system asks for its constellation and indexes the result by locationID,
     *  so every system of a constellation needs a row, claimed or not.
     *  the sovereignty dashboard lists systems (constellation scope), constellations (region scope) or regions (world scope)
     */
    DBRowDescriptor *header = new DBRowDescriptor();
    header->AddColumn("locationID", DBTYPE_I4);
    header->AddColumn("allianceID", DBTYPE_I4);
    header->AddColumn("stationCount", DBTYPE_I2);
    header->AddColumn("militaryPoints", DBTYPE_I4);
    header->AddColumn("industrialPoints", DBTYPE_I4);
    header->AddColumn("claimedFor", DBTYPE_I4);
    CRowSet *rowset = new CRowSet(&header);

    struct SovRow {
        std::map<uint32, uint16> alliances;     // allianceID/systems held
        uint32 stationCount = 0;
        int64 military = 0;
        int64 industrial = 0;
        uint32 claimedFor = 0;
    };

    // systems to report and the location each one is reported under
    std::vector<uint32> systems;
    bool bySystem(false), byRegion(false);
    if (sDataMgr.IsSolarSystem(locationID)) {
        systems.push_back(locationID);
        bySystem = true;
    } else if (IsConstellationID(locationID)) {
        sDataMgr.GetSystemsInLocation(locationID, systems);
        bySystem = true;
    } else if (IsRegionID(locationID)) {
        sDataMgr.GetSystemsInLocation(locationID, systems);
    } else if (locationID == 0) {
        sDataMgr.GetSystemsInLocation(0, systems);
        byRegion = true;
    }

    std::map<uint32, SovRow> rows;  // locationID/data
    SystemData sysData = SystemData();
    for (auto cur : systems) {
        if (!sDataMgr.GetSystemData(cur, sysData))
            continue;
        uint32 rowID(cur);
        if (!bySystem) {
            if (IsWSpaceID(cur))
                continue;
            rowID = (byRegion ? sysData.regionID : sysData.constellationID);
        }
        SovRow& row = rows[rowID];
        uint32 allianceID = GetSystemAllianceID(cur);
        if (allianceID > 0)
            ++row.alliances[allianceID];
        row.stationCount += sDataMgr.GetStationCount(cur);
        row.military = std::max(row.military, GetMilitaryPoints(cur));
        row.industrial = std::max(row.industrial, GetIndustrialPoints(cur));
        row.claimedFor = std::max(row.claimedFor, GetClaimDays(cur));
    }

    for (auto& cur : rows) {
        // the alliance holding the most systems speaks for the group
        uint32 allianceID(0);
        uint16 held(0);
        for (auto& ally : cur.second.alliances)
            if (ally.second > held) {
                allianceID = ally.first;
                held = ally.second;
            }

        PyPackedRow *row = rowset->NewRow();
        row->SetField("locationID", new PyInt(cur.first));
        row->SetField("allianceID", (allianceID > 0 ? (PyRep*)new PyInt(allianceID) : PyStatic.NewNone()));
        row->SetField("stationCount", new PyInt(cur.second.stationCount));
        row->SetField("militaryPoints", new PyInt((int32)cur.second.military));
        row->SetField("industrialPoints", new PyInt((int32)cur.second.industrial));
        row->SetField("claimedFor", new PyInt(cur.second.claimedFor));
    }

    return rowset;
}

void SovereigntyDataMgr::AddSovClaim(SovereigntyData data)
{
    _log(SOV__INFO, "AddSovClaim() - Adding claim for %u to DataMgr...", data.solarSystemID);

    //Add a new sovereignty claim to DB
    uint32 claimID(0);
    SovereigntyDB::AddSovereigntyData(data, claimID);
    _log(SOV__DEBUG, "AddSovClaim() - ClaimID %u added to DB...", claimID);

    //Define our view from container
    auto &bySolar = m_sovData.get<SovDataBySolarSystem>();

    //Delete existing records in container which match the systemID
    auto it = m_sovData.get<SovDataBySolarSystem>().find(data.solarSystemID);
    if (it != m_sovData.get<SovDataBySolarSystem>().end())
    {
        bySolar.erase(data.solarSystemID);
    }

    UpdateClaim(data.solarSystemID);

    // a hub online in the system serves the new claim
    if (data.hubID > 0)
        RefreshUpgrades(data.solarSystemID, data.hubID);
}

void SovereigntyDataMgr::MarkContested(uint32 systemID, bool contested)
{
    _log(SOV__INFO, "MarkContested() - Marking system %u as state %u", systemID, int(contested));

    //Update state in DB
    SovereigntyDB::SetContested(systemID, contested);

    //Re-sync the claim data
    UpdateClaim(systemID);
}

void SovereigntyDataMgr::RemoveSovClaim(uint32 systemID)
{
    _log(SOV__INFO, "RemoveSovClaim() - Removing claim for %u from DataMgr...", systemID);

    //Delete sovereignty claim from DB
    SovereigntyDB::RemoveSovereigntyData(systemID);
    _log(SOV__DEBUG, "RemoveSovClaim() - Claim for %u removed from DB...", systemID);

    //Remove claim from container
    uint32 hubID = GetSovereigntyData(systemID).hubID;
    auto &bySolar = m_sovData.get<SovDataBySolarSystem>();
    bySolar.erase(systemID);

    // upgrades need sovereignty
    if (hubID > 0)
        RefreshUpgrades(systemID, hubID);
}

void SovereigntyDataMgr::UpdateSystemHubID(uint32 systemID, uint32 hubID)
{
    _log(SOV__INFO, "UpdateSystemHubID() - Updating hubID for system %u with hub %u", systemID, hubID);

    //Update state in DB
    SovereigntyDB::SetHubID(systemID, hubID);

    //Re-sync the claim data
    UpdateClaim(systemID);
}

void SovereigntyDataMgr::UpdateSystemJammerID(uint32 systemID, uint32 jammerID)
{
    _log(SOV__INFO, "UpdateSystemJammerID() - Updating jammerID for system %u with jammer %u", systemID, jammerID);

    //Update state in DB
    SovereigntyDB::SetJammerID(systemID, jammerID);

    //Re-sync the claim data
    UpdateClaim(systemID);
}

void SovereigntyDataMgr::UpdateSystemBeaconID(uint32 systemID, uint32 beaconID)
{
    _log(SOV__INFO, "UpdateSystemBeaconID() - Updating beaconID for system %u with beacon %u", systemID, beaconID);

    //Update state in DB
    SovereigntyDB::SetBeaconID(systemID, beaconID);

    //Re-sync the claim data
    UpdateClaim(systemID);
}

// Update managed claim data from database, effectively a re-sync
void SovereigntyDataMgr::UpdateClaim(uint32 systemID)
{
    //Define our view from container
    auto &bySolar = m_sovData.get<SovDataBySolarSystem>();

    //Remove old from container
    bySolar.erase(systemID);

    //Get the data from the DB, this will avoid inconsistencies
    DBQueryResult *res = new DBQueryResult();
    DBResultRow row;

    SovereigntyDB::GetSovereigntyDataForSystem(*res, systemID);
    while (res->GetRow(row))
    {
        SovereigntyData sData = SovereigntyData();
        ReadRow(row, sData);
        bySolar.insert(sData);
    }
    SafeDelete(res);
}

/* development indices */
bool SovereigntyDataMgr::IsClaimable(uint32 systemID)
{
    if (IsWSpaceID(systemID))
        return false;
    SystemData sysData = SystemData();
    if (!sDataMgr.GetSystemData(systemID, sysData))
        return false;
    return (sysData.factionID == 0);
}

DevIndexData& SovereigntyDataMgr::GetDevIndex(uint32 systemID)
{
    std::unordered_map<uint32, DevIndexData>::iterator itr = m_devIndex.find(systemID);
    if (itr != m_devIndex.end())
        return itr->second;

    DevIndexData data = DevIndexData();
        data.systemID = systemID;
        data.lastDecay = GetFileTimeNow();
    return m_devIndex.emplace(systemID, data).first->second;
}

void SovereigntyDataMgr::AddMilitaryActivity(uint32 systemID, double bountyISK)
{
    if ((bountyISK <= 0) or !IsClaimable(systemID))
        return;

    DevIndexData& data = GetDevIndex(systemID);
    int64 points = (int64)(bountyISK * sConfig.sov.MilitaryPointsPerBountyISK * DevIndex::MilitaryMod[DevIndex::BandForPoints(data.military)]);
    if (points < 1)
        return;
    data.military = std::min(DevIndex::MaxPoints, data.military + points);
    data.militaryGain += points;
    data.dirty = true;
    _log(SOV__DEBUG, "AddMilitaryActivity() - %lli points in %u (now %lli)", points, systemID, data.military);
}

void SovereigntyDataMgr::AddIndustrialActivity(uint32 systemID, double m3)
{
    if ((m3 <= 0) or !IsClaimable(systemID))
        return;

    DevIndexData& data = GetDevIndex(systemID);
    int64 points = (int64)(m3 * sConfig.sov.IndustrialPointsPerM3 * DevIndex::IndustrialMod[DevIndex::BandForPoints(data.industrial)]);
    if (points < 1)
        return;
    data.industrial = std::min(DevIndex::MaxPoints, data.industrial + points);
    data.industrialGain += points;
    data.dirty = true;
    _log(SOV__DEBUG, "AddIndustrialActivity() - %lli points in %u (now %lli)", points, systemID, data.industrial);
}

void SovereigntyDataMgr::Decay(DevIndexData& data, int64 now, bool hourly)
{
    if (now <= data.lastDecay)
        return;

    int64 decay = (int64)(sConfig.sov.DevIndexDecayPerHour * ((double)(now - data.lastDecay) / Win32Time_Hour));
    if (hourly) {
        // an index is rising while its activity outpaces the decay
        data.militaryIncreasing = (data.militaryGain > decay);
        data.industrialIncreasing = (data.industrialGain > decay);
        data.militaryGain = 0;
        data.industrialGain = 0;
    }
    data.military = std::max<int64>(0, data.military - decay);
    data.industrial = std::max<int64>(0, data.industrial - decay);
    data.lastDecay = now;
    data.dirty = true;
}

void SovereigntyDataMgr::Process()
{
    // hourly decay (called from EntityList's hour tic)
    int64 now = GetFileTimeNow();
    for (auto& cur : m_devIndex)
        Decay(cur.second, now, true);

    // upgrades follow the indices
    RefreshAllUpgrades();
}

void SovereigntyDataMgr::SaveDevIndices()
{
    uint32 count(0);
    for (auto& cur : m_devIndex) {
        if (!cur.second.dirty)
            continue;
        SovereigntyDB::SaveDevIndex(cur.second);
        cur.second.dirty = false;
        ++count;
    }
    if (count > 0)
        _log(SOV__INFO, "SaveDevIndices() - saved %u development index sets", count);
}

int64 SovereigntyDataMgr::GetMilitaryPoints(uint32 systemID)
{
    std::unordered_map<uint32, DevIndexData>::iterator itr = m_devIndex.find(systemID);
    if (itr == m_devIndex.end())
        return 0;
    return itr->second.military;
}

int64 SovereigntyDataMgr::GetIndustrialPoints(uint32 systemID)
{
    std::unordered_map<uint32, DevIndexData>::iterator itr = m_devIndex.find(systemID);
    if (itr == m_devIndex.end())
        return 0;
    return itr->second.industrial;
}

uint32 SovereigntyDataMgr::GetClaimDays(uint32 systemID)
{
    auto it = m_sovData.get<SovDataBySolarSystem>().find(systemID);
    if (it == m_sovData.get<SovDataBySolarSystem>().end())
        return 0;
    int64 held = (int64)GetFileTimeNow() - it->claimTime;
    if (held <= 0)
        return 0;
    return (uint32)(held / Win32Time_Day);
}

PyRep* SovereigntyDataMgr::GetDevIndicesForSystem(uint32 systemID)
{
    /* sovSvc.CanInstallUpgrade() runs every entry through GetDevIndexLevel(points), so only the military and industrial
     * indices belong here.  the strategic index is worked out by the client from the claim time.
     */
    bool milInc(false), indInc(false);
    std::unordered_map<uint32, DevIndexData>::iterator itr = m_devIndex.find(systemID);
    if (itr != m_devIndex.end()) {
        milInc = itr->second.militaryIncreasing;
        indInc = itr->second.industrialIncreasing;
    }

    PyDict* military = new PyDict();
        military->SetItemString("points", new PyInt((int32)GetMilitaryPoints(systemID)));
        military->SetItemString("increasing", new PyBool(milInc));
    PyDict* industrial = new PyDict();
        industrial->SetItemString("points", new PyInt((int32)GetIndustrialPoints(systemID)));
        industrial->SetItemString("increasing", new PyBool(indInc));

    PyDict* dict = new PyDict();
        dict->SetItem(new PyInt(EveAttrEnum::AttrdevIndexMilitary), new PyObject("util.KeyVal", military));
        dict->SetItem(new PyInt(EveAttrEnum::AttrdevIndexIndustrial), new PyObject("util.KeyVal", industrial));
    return dict;
}

PyRep* SovereigntyDataMgr::GetAllDevIndices()
{
    // every system with indices or a claim
    std::set<uint32> systems;
    for (auto& cur : m_devIndex)
        systems.insert(cur.first);
    for (auto& cur : m_sovData.get<SovDataBySolarSystem>())
        systems.insert(cur.solarSystemID);

    PyList* list = new PyList();
    for (auto cur : systems) {
        PyDict* dict = new PyDict();
            dict->SetItemString("solarSystemID", new PyInt(cur));
            dict->SetItemString("militaryPoints", new PyInt((int32)GetMilitaryPoints(cur)));
            dict->SetItemString("industrialPoints", new PyInt((int32)GetIndustrialPoints(cur)));
            dict->SetItemString("claimedFor", new PyInt(GetClaimDays(cur)));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
    return list;
}

void SovereigntyDataMgr::SendAudioEvent(uint32 allianceID, uint8 eventID, PyDict* textParams)
{
    EntityList::character_set cset;
    if (allianceID > 0) {
        std::vector<Client*> list;
        sEntityList.GetClients(list);
        for (auto cur : list)
            if ((cur != nullptr) and (cur->GetAllianceID() == allianceID))
                cset.insert(cur->GetCharID());
    }

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(eventID));
        tuple->SetItem(1, textParams);
    if (cset.empty()) {
        PyDecRef(tuple);
        return;
    }

    _log(SOV__DEBUG, "SendAudioEvent() - event %u to %lu members of alliance %u", eventID, cset.size(), allianceID);
    sEntityList.Multicast(cset, "OnSovereigntyAudioEvent", "clientID", &tuple);
}

/* infrastructure hub upgrades */
static const uint16 s_upgradeChains[Sov::Upgrade::FamilyCount][5] = {
    {2026, 2027, 2028, 2029, 2030},     // Pirate Detection Array
    {2031, 2034, 2035, 2036, 2037},     // Entrapment Array
    {2058, 2059, 2060, 2061, 2062},     // Quantum Flux Generator
    {2040, 2041, 2042, 2043, 2044},     // Ore Prospecting Array
    {2053, 2054, 2055, 2056, 2057}      // Survey Networks
};

void SovereigntyDataMgr::LoadActiveUpgrades()
{
    // online upgrades straight from the db, so unloaded systems don't load their hubs
    std::map<uint32, uint32> hubs;  // hubID/systemID
    for (auto& cur : m_sovData.get<SovDataBySolarSystem>())
        if (cur.hubID > 0)
            hubs[cur.hubID] = cur.solarSystemID;
    if (hubs.empty())
        return;

    std::string ids;
    for (auto& cur : hubs) {
        if (!ids.empty())
            ids += ",";
        ids += std::to_string(cur.first);
    }

    DBQueryResult res;
    DBResultRow row;
    SovereigntyDB::GetActiveUpgrades(res, ids);
    while (res.GetRow(row))
        m_activeUpgrades[hubs[row.GetUInt(0)]].insert(row.GetUInt(1));

    sLog.Cyan("       SovDataMgr", "%lu systems with online infrastructure upgrades.", m_activeUpgrades.size());
}

Sov::UpgradeBlock SovereigntyDataMgr::CheckUpgradeRequirements(uint32 systemID, uint16 typeID,
                                    const std::map<uint16, bool>& installed, bool forInstall, uint16& otherTypeID)
{
    // same order as the client's sovSvc.CanInstallUpgrade()
    otherTypeID = 0;
    const ItemType* pType = sItemFactory.GetType(typeID);
    if (pType == nullptr)
        return Sov::UpgradeBlock::MissingRequired;

    if (pType->HasAttribute(AttrdevIndexMilitary))
        if (pType->GetAttribute(AttrdevIndexMilitary).get_int() > GetMilitaryLevel(systemID))
            return Sov::UpgradeBlock::DevIndex;
    if (pType->HasAttribute(AttrdevIndexIndustrial))
        if (pType->GetAttribute(AttrdevIndexIndustrial).get_int() > GetIndustrialLevel(systemID))
            return Sov::UpgradeBlock::DevIndex;
    if (pType->HasAttribute(AttrdevIndexSovereignty))
        if (pType->GetAttribute(AttrdevIndexSovereignty).get_int() > GetStrategicLevel(systemID))
            return Sov::UpgradeBlock::SovTime;

    if (pType->HasAttribute(AttrsovUpgradeRequiredUpgradeID)) {
        uint16 reqID = pType->GetAttribute(AttrsovUpgradeRequiredUpgradeID).get_uint32();
        if (reqID > 0) {
            std::map<uint16, bool>::const_iterator itr = installed.find(reqID);
            if ((itr == installed.end()) or !itr->second) {
                otherTypeID = reqID;
                return Sov::UpgradeBlock::MissingRequired;
            }
        }
    }
    if (forInstall and pType->HasAttribute(AttrsovUpgradeBlockingUpgradeID)) {
        uint16 blockID = pType->GetAttribute(AttrsovUpgradeBlockingUpgradeID).get_uint32();
        if ((blockID > 0) and (installed.find(blockID) != installed.end())) {
            otherTypeID = blockID;
            return Sov::UpgradeBlock::Blocking;
        }
    }

    // outpost upgrades are not supported.  no Crucible infrastructure upgrade uses this
    if (pType->HasAttribute(AttrsovUpgradeRequiredOutpostUpgradeLevel))
        if (pType->GetAttribute(AttrsovUpgradeRequiredOutpostUpgradeLevel).get_int() > 0)
            return Sov::UpgradeBlock::Outpost;

    return Sov::UpgradeBlock::None;
}

uint8 SovereigntyDataMgr::GetActiveUpgradeLevel(uint32 systemID, uint8 family)
{
    if (family >= Sov::Upgrade::FamilyCount)
        return 0;
    std::map<uint32, std::set<uint16>>::iterator itr = m_activeUpgrades.find(systemID);
    if (itr == m_activeUpgrades.end())
        return 0;
    for (uint8 level = 5; level > 0; --level)
        if (itr->second.find(s_upgradeChains[family][level - 1]) != itr->second.end())
            return level;
    return 0;
}

bool SovereigntyDataMgr::HasActiveUpgrade(uint32 systemID, uint16 typeID)
{
    std::map<uint32, std::set<uint16>>::iterator itr = m_activeUpgrades.find(systemID);
    if (itr == m_activeUpgrades.end())
        return false;
    return (itr->second.find(typeID) != itr->second.end());
}

void SovereigntyDataMgr::RefreshUpgrades(uint32 systemID, uint32 hubID)
{
    InventoryItemRef hubRef = sItemFactory.GetItemRef(hubID);
    if ((hubRef.get() == nullptr) or (hubRef->GetMyInventory() == nullptr)) {
        m_activeUpgrades.erase(systemID);
        return;
    }

    // upgrades work while the hub is online for the system's sovereign
    SovereigntyData sovData = GetSovereigntyData(systemID);
    bool hubOnline((sovData.claimID > 0) and (sovData.hubID == hubID));

    Inventory* pInv = hubRef->GetMyInventory();
    pInv->LoadContents();
    std::map<uint32, InventoryItemRef> contents;
    pInv->GetInventoryMap(contents);

    // lower levels of a chain have lower typeIDs, so walking by typeID settles requirements first
    std::map<uint16, InventoryItemRef> upgrades;
    std::map<uint16, bool> installed;
    for (auto& cur : contents) {
        if (cur.second->categoryID() != EVEDB::invCategories::StructureUpgrade)
            continue;
        upgrades.emplace(cur.second->typeID(), cur.second);
        installed[cur.second->typeID()] = (cur.second->flag() == flagStructureActive);
    }

    std::set<uint16> active;
    uint16 otherTypeID(0);
    for (auto& cur : upgrades) {
        bool online(hubOnline and (CheckUpgradeRequirements(systemID, cur.first, installed, false, otherTypeID) == Sov::UpgradeBlock::None));
        installed[cur.first] = online;
        if (online)
            active.insert(cur.first);
        EVEItemFlags flag(online ? flagStructureActive : flagStructureInactive);
        if (cur.second->flag() != flag) {
            _log(SOV__INFO, "RefreshUpgrades() - %s in %u is now %s", cur.second->name(), systemID, (online ? "online" : "offline"));
            cur.second->SetFlag(flag, true);
        }
    }
    if (active.empty()) {
        m_activeUpgrades.erase(systemID);
    } else {
        m_activeUpgrades[systemID] = active;
    }

    // a loaded system's anomaly manager adds (or stops replacing) the upgrades' sites
    SystemManager* pSystem(sEntityList.FindSystem(systemID));
    if (pSystem != nullptr)
        pSystem->GetAnomMgr()->SetUpgradeBonus(GetActiveUpgradeLevel(systemID, Sov::Upgrade::PirateDetection),
                GetActiveUpgradeLevel(systemID, Sov::Upgrade::SurveyNetworks), GetActiveUpgradeLevel(systemID, Sov::Upgrade::OreProspecting));
}

void SovereigntyDataMgr::RefreshAllUpgrades()
{
    std::vector<std::pair<uint32, uint32>> hubs;
    for (auto& cur : m_sovData.get<SovDataBySolarSystem>())
        if (cur.hubID > 0)
            hubs.push_back(std::make_pair(cur.solarSystemID, cur.hubID));
    for (auto& cur : hubs)
        RefreshUpgrades(cur.first, cur.second);
}

void SovereigntyDataMgr::RemoveHub(uint32 systemID, uint32 hubID)
{
    if (GetSovereigntyData(systemID).hubID == hubID)
        UpdateSystemHubID(systemID, 0);
    m_activeUpgrades.erase(systemID);
    m_upgradeDirty.erase(hubID);

    // installed upgrades are a permanent fixture of the hub and go with it
    InventoryItemRef hubRef = sItemFactory.GetItemRef(hubID);
    if ((hubRef.get() == nullptr) or (hubRef->GetMyInventory() == nullptr))
        return;
    hubRef->GetMyInventory()->LoadContents();
    std::map<uint32, InventoryItemRef> contents;
    hubRef->GetMyInventory()->GetInventoryMap(contents);
    for (auto& cur : contents) {
        _log(SOV__INFO, "RemoveHub() - destroying %s(%u) with hub %u", cur.second->name(), cur.first, hubID);
        cur.second->Delete();
    }
}

void SovereigntyDataMgr::ClearSystemBeaconID(uint32 systemID, uint32 beaconID)
{
    if (GetSovereigntyData(systemID).beaconID == beaconID)
        UpdateSystemBeaconID(systemID, 0);
}

void SovereigntyDataMgr::ClearSystemJammerID(uint32 systemID, uint32 jammerID)
{
    if (GetSovereigntyData(systemID).jammerID == jammerID)
        UpdateSystemJammerID(systemID, 0);
}

void SovereigntyDataMgr::GetBeaconCounts(std::map<uint32, uint32>& counts)
{
    for (auto& cur : m_sovData.get<SovDataBySolarSystem>())
        if (cur.beaconID > 0)
            ++counts[cur.solarSystemID];
}
