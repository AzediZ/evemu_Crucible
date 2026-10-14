
 /**
  * @name StationDataMgr.cpp
  *   memory object caching system for managing and saving ingame static and dynamic station data
  *
  * @Author:         Allan
  * @date:          12Dec17
  *
  */

#include "station/StationDataMgr.h"
#include "database/EVEDBUtils.h"
#include "../eve-common/EVE_Outpost.h"


StationDataMgr::StationDataMgr()
{
    m_serviceMask.clear();
    m_stationData.clear();
    m_stationPyData.clear();
    m_stationOfficeData.clear();
}

StationDataMgr::~StationDataMgr()
{
    //Clear();
}

int StationDataMgr::Initialize()
{
    Populate();
    sLog.Blue("   StationDataMgr", "Station Data Manager Initialized.");
    return 1;
}

void StationDataMgr::Close()
{
    /** @todo put a save method here which will save anything changed before shutdown */
    //for (auto cur : m_stationPyData)
    //    PySafeDecRef(cur.second);

    Clear();

    sLog.Warning("   StationDataMgr", "Station Data Manager has been closed." );
}


void StationDataMgr::Clear()
{
    for (auto cur : m_stationPyData)
        PySafeDecRef(cur.second);

    m_serviceMask.clear();
    m_stationData.clear();
    m_stationPyData.clear();
    m_stationOfficeData.clear();
}

void StationDataMgr::Populate()
{
    double start = GetTimeMSeconds();
    DBQueryResult* res = new DBQueryResult();
    DBResultRow row;

    std::map<int8, int32>::iterator itr;
    StationDB::GetOperationServiceIDs(*res);
    while (res->GetRow(row)) {
        //SELECT operationID, serviceID FROM staOperationServices
        if ((itr = m_serviceMask.find(row.GetInt(0))) != m_serviceMask.end()) {
            itr->second += row.GetInt(1);
        } else {
            m_serviceMask.emplace(row.GetInt(0), row.GetInt(1));
        }
    }

    //res->Reset();
    StationDB::GetStationOfficeData(*res);
    while (res->GetRow(row)) {
        //SELECT itemID, corporationID, stationID, typeID, lockDown, rentalFee, expiryDateTime, officeFolderID
        OfficeData data = OfficeData();
        data.officeID       = row.GetInt(0);
        data.corporationID  = row.GetInt(1);
        data.stationID      = row.GetInt(2);
        data.typeID         = row.GetInt(3);
        data.lockDown       = row.GetBool(4);
        data.rentalFee      = row.GetInt64(5);
        data.expiryTime     = row.GetInt64(6);
        data.folderID       = row.GetInt(7);
        m_stationOfficeData.emplace(row.GetInt(2), data);
    }

    //res->Reset();
    StationDB::GetStationData(*res);
    while (res->GetRow(row)) {
        //SELECT s.stationID, s.x, s.y, s.z, st.dockOrientationX, st.dockOrientationY, st.dockOrientationZ, s.dockingCostPerVolume, s.maxShipVolumeDockable,8 \
        s.officeSlots, s.officeRentalCost, s.operationID, s.stationTypeID, s.corporationID, s.stationName, s.reprocessingStationsTake, s.reprocessingEfficiency,16 \
        s.reprocessingHangarFlag, st.conquerable, st.hangarGraphicID, m.orbitID, m.radius, m.security, o.description, o.descriptionID,24\
        t.graphicID, s.solarSystemID, s.constellationID, s.regionID, st.dockEntryX, st.dockEntryY, st.dockEntryZ 31
        StationData sData = StationData();
        sData.stationID                 = row.GetUInt(0);
        sData.operationID               = row.GetInt(11);
        if ((itr = m_serviceMask.find(sData.operationID)) != m_serviceMask.end()) {
            sData.serviceMask = itr->second;
        } else {
            sData.serviceMask = 0;
        }
        sData.position                  = GPoint(row.GetDouble(1),row.GetDouble(2),row.GetDouble(3));
        sData.dockOrientation           = GVector(row.GetDouble(4),row.GetDouble(5),row.GetDouble(6));
        sData.dockingCostPerVolume      = row.GetFloat(7);
        sData.maxShipVolumeDockable     = row.GetUInt(8);
        sData.officeSlots               = row.GetUInt(9);
        sData.officeRentalFee           = row.GetInt(10);
        sData.typeID                    = row.GetInt(12);
        sData.corporationID             = row.GetInt(13);
        sData.name                      = row.GetText(14);
        sData.reprocessingStationsTake  = row.GetFloat(15);
        sData.reprocessingEfficiency    = row.GetFloat(16);
        sData.reprocessingHangarFlag    = row.GetUInt(17);
        sData.conquerable               = row.GetBool(18);
        sData.hangarGraphicID           = row.GetUInt(19);
        sData.orbitID                   = row.GetUInt(20);
        sData.radius                    = row.GetFloat(21);
        sData.security                  = row.GetFloat(22);
        sData.description               = row.GetText(23);
        sData.descriptionID             = row.GetInt(24);
        sData.graphicID                 = row.GetInt(25);
        sData.systemID                  = row.GetUInt(26);
        sData.constellationID           = row.GetUInt(27);
        sData.regionID                  = row.GetUInt(28);
        sData.dockEntry                 = GPoint(row.GetDouble(29),row.GetDouble(30),row.GetDouble(31));
        sData.dockPosition              = GPoint(row.GetDouble(29) + row.GetDouble(1),
                                                 row.GetDouble(30) + row.GetDouble(2),
                                                 row.GetDouble(31) + row.GetDouble(3));
        sData.upgradeLevel              = (row.IsNull(32) ? 0 : row.GetUInt(32));
        m_stationData.emplace(row.GetInt(0), sData);
    }

    // outpost improvements, and the services they add to the outpost's operation's (a refinery, labs)
    StationDB::GetAllOutpostImprovements(*res);
    while (res->GetRow(row)) {
        std::map<uint32, StationData>::iterator sItr = m_stationData.find(row.GetUInt(0));
        if (sItr == m_stationData.end())
            continue;
        OutpostRules::Slots slots = {{0, 0, 0, 0, 0, 0}};
        for (uint8 i = 0; i < OutpostRules::SlotCount; ++i) {
            slots[i] = (row.IsNull(i + 1) ? 0 : row.GetUInt(i + 1));
            sItr->second.improvements[i] = slots[i];
        }
        sItr->second.serviceMask |= OutpostRules::AddedServices(slots);
    }

    LoadStationPyData();

    sLog.Cyan("   StationDataMgr", "%u Station data sets loaded in %.3fms.", \
                (m_stationData.size() + m_serviceMask.size() + m_stationPyData.size() + m_stationOfficeData.size()), (GetTimeMSeconds() - start));

    //cleanup
    SafeDelete(res);
}

void StationDataMgr::AddOffice(uint32 stationID, OfficeData& data)
{
    m_stationOfficeData.emplace(stationID, data);
}

void StationDataMgr::RemoveOffice(uint32 stationID, uint32 officeID)
{
    auto range = m_stationOfficeData.equal_range(stationID);
    for (auto itr = range.first; itr != range.second; ++itr)
        if (itr->second.officeID == officeID) {
            m_stationOfficeData.erase(itr);
            return;
        }
}

void StationDataMgr::LoadOffices(uint32 stationID, std::map<uint32, OfficeData>& data)
{
    auto range = m_stationOfficeData.equal_range(stationID);
    for (auto itr = range.first; itr != range.second; ++itr)
        data.emplace(itr->second.officeID, itr->second);
}

void StationDataMgr::GetInfo()
{
    /* return info about loaded items? */
    /*
     * m_stationSystem
     * m_stationData;
     *
     */
}

uint32 StationDataMgr::GetOwnerID(uint32 stationID)
{
    /** @todo  update this for POS modules and Outposts  */
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr != m_stationData.end())
        return itr->second.corporationID;
    return 0;
}

uint32 StationDataMgr::GetOfficeIDForCorp(uint32 stationID, uint32 corpID)
{
    auto range = m_stationOfficeData.equal_range(stationID);
    for (auto itr = range.first; itr != range.second; ++itr)
        if (itr->second.corporationID == corpID)
            return itr->second.officeID;
    return 0;
}

double StationDataMgr::GetDockPosY(uint32 stationID)
{
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr != m_stationData.end())
        return itr->second.dockPosition.y;
    return 0;
}

std::string StationDataMgr::GetStationName(uint32 stationID)
{
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr != m_stationData.end()) {
        return itr->second.name;
    } else {
        _log(DATABASE__MESSAGE, "Failed to query station name for station %u: Station not found.", stationID);
    }
    return "";
}

uint32 StationDataMgr::GetStationSystemID(uint32 stationID)
{
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr != m_stationData.end()) {
        return itr->second.systemID;
    } else {
        _log(DATABASE__MESSAGE, "Failed to query station systemID for station %u: Station not found.", stationID);
    }
    return 0;
}


void StationDataMgr::UpdateStationSettings(uint32 stationID, const std::string& name, float dockingCostPerVolume, int32 officeRentalFee,
                                           float reprocessingStationsTake, uint16 reprocessingHangarFlag)
{
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr == m_stationData.end())
        return;
    itr->second.name = name;
    itr->second.dockingCostPerVolume = dockingCostPerVolume;
    itr->second.officeRentalFee = officeRentalFee;
    itr->second.reprocessingStationsTake = reprocessingStationsTake;
    itr->second.reprocessingHangarFlag = reprocessingHangarFlag;
}

void StationDataMgr::SetStationOwner(uint32 stationID, uint32 corpID)
{
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr != m_stationData.end())
        itr->second.corporationID = corpID;
}

bool StationDataMgr::GetStationData(uint32 stationID, StationData& data)
{
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr != m_stationData.end()) {
        data = itr->second;
        return true;
    } else {
        _log(DATABASE__MESSAGE, "Failed to query data for station %u: Station not found.", stationID);
    }
    return false;
}

PyObject* StationDataMgr::GetStationPyData(uint32 stationID)
{
    std::map<uint32, PyObject*>::iterator itr = m_stationPyData.find(stationID);
    if (itr != m_stationPyData.end()) {
        PyIncRef(itr->second);
        return itr->second;
    } else {
        _log(DATABASE__MESSAGE, "Failed to query data for station %u: Station not found.", stationID);
    }
    return nullptr;
}

PyRep* StationDataMgr::GetStationItemBits(uint32 stationID)
{
    //['hangarGraphicID','ownerID','itemID','serviceMask','stationTypeID']
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr != m_stationData.end()) {
        PyTuple * result = new PyTuple(5);
            result->SetItem(0, new PyInt(itr->second.hangarGraphicID));
            result->SetItem(1, new PyInt(itr->second.corporationID));
            result->SetItem(2, new PyInt(stationID));
            // the operation's services, and for outposts what their improvements add
            result->SetItem(3, new PyInt(itr->second.serviceMask));
            result->SetItem(4, new PyInt(itr->second.typeID));
        return result;
    }
    return nullptr;
}

void StationDataMgr::GetStationOfficeIDs(uint32 locationID, std::vector<OfficeData> &data)
{
    if (sDataMgr.IsStation(locationID)) {
        auto range = m_stationOfficeData.equal_range(locationID);
        for (auto itr = range.first; itr != range.second; ++itr)
            data.push_back(itr->second);
    } else if (IsOfficeFolder(locationID)) {
        auto range = m_stationOfficeData.equal_range((locationID - STATION_OFFICE_OFFSET));
        for (auto itr = range.first; itr != range.second; ++itr)
            if (itr->second.folderID == locationID)
                data.push_back(itr->second);
    } else if (IsOfficeID(locationID)) {
        // no better way to do this one yet.....iterate thru the whole map?
        // this is full map of station data.  need to find station to cut down on loop time.
        for (auto cur : m_stationOfficeData)
            if (cur.second.officeID == locationID) {
                data.push_back(cur.second);
                return;
            }
    }
}

uint32 StationDataMgr::GetOfficeRentalFee(uint32 stationID)
{
    std::map<uint32, StationData>::iterator itr = m_stationData.find(stationID);
    if (itr != m_stationData.end())
        return itr->second.officeRentalFee;
    return 0;
}

void StationDataMgr::LoadStationPyData()
{
    for (auto cur : m_stationData)
        m_stationPyData.emplace(cur.first, MakeStationPyData(cur.first, cur.second));
}

PyObject* StationDataMgr::MakeStationPyData(uint32 stationID, const StationData& data)
{
    PyDict* dict = new PyDict();
        dict->SetItemString("stationID", new PyInt(stationID));
        dict->SetItemString("ownerID", new PyInt(data.corporationID));
        dict->SetItemString("stationTypeID", new PyInt(data.typeID));
        dict->SetItemString("stationName", new PyString(data.name));
        dict->SetItemString("operationID", new PyInt(data.operationID));
        dict->SetItemString("x", new PyFloat(data.position.x));
        dict->SetItemString("y", new PyFloat(data.position.y));
        dict->SetItemString("z", new PyFloat(data.position.z));
        dict->SetItemString("dockEntryX", new PyFloat(data.dockEntry.x));
        dict->SetItemString("dockEntryY", new PyFloat(data.dockEntry.y));
        dict->SetItemString("dockEntryZ", new PyFloat(data.dockEntry.z));
        dict->SetItemString("dockOrientationX", new PyFloat(data.dockOrientation.x));
        dict->SetItemString("dockOrientationY", new PyFloat(data.dockOrientation.y));
        dict->SetItemString("dockOrientationZ", new PyFloat(data.dockOrientation.z));

        dict->SetItemString("serviceMask", new PyInt(data.serviceMask));
        dict->SetItemString("conquerable", new PyBool(data.conquerable));
        dict->SetItemString("upgradeLevel", new PyInt(data.upgradeLevel));  // outposts only. others are 0
        dict->SetItemString("standingOwnerID", new PyInt(data.corporationID));
        dict->SetItemString("hangarGraphicID", new PyInt(data.hangarGraphicID));
        dict->SetItemString("officeRentalCost", new PyInt(data.officeRentalFee));
        dict->SetItemString("dockingBayGraphicID", PyStatic.NewNone());   // cannot find any data on this; all packets show PyNone
        dict->SetItemString("dockingCostPerVolume", new PyFloat(data.dockingCostPerVolume));
        dict->SetItemString("maxShipVolumeDockable", new PyFloat(data.maxShipVolumeDockable));
        dict->SetItemString("reprocessingEfficiency", new PyFloat(data.reprocessingEfficiency));
        dict->SetItemString("reprocessingHangarFlag", new PyInt(data.reprocessingHangarFlag));
        dict->SetItemString("reprocessingStationsTake", new PyFloat(data.reprocessingStationsTake));

        dict->SetItemString("solarSystemID", new PyInt(data.systemID));
        dict->SetItemString("constellationID", new PyInt(data.constellationID));
        dict->SetItemString("regionID", new PyInt(data.regionID));
        dict->SetItemString("security", new PyFloat(data.security));

        dict->SetItemString("graphicID", new PyInt(data.graphicID));  //invTypes.graphicID
        dict->SetItemString("description", new PyString(data.description));    //staOperations.description
        dict->SetItemString("descriptionID", new PyInt(data.descriptionID));    //staOperations.descriptionID

        dict->SetItemString("radius", new PyFloat(data.radius));   //mapDenormalize.radius or invTypes.radius
        dict->SetItemString("orbitID", new PyInt(data.orbitID));    //mapDenormalize.orbitID

    return new PyObject("util.KeyVal", dict);
}

void StationDataMgr::UpdateStationData(const StationData& data)
{
    std::map<uint32, StationData>::iterator itr = m_stationData.find(data.stationID);
    if (itr == m_stationData.end())
        return;
    itr->second = data;
    std::map<uint32, PyObject*>::iterator pyItr = m_stationPyData.find(data.stationID);
    if (pyItr != m_stationPyData.end()) {
        PySafeDecRef(pyItr->second);
        pyItr->second = MakeStationPyData(data.stationID, data);
    } else {
        m_stationPyData.emplace(data.stationID, MakeStationPyData(data.stationID, data));
    }
}

uint32 StationDataMgr::GetOperationServiceMask(uint8 operationID)
{
    std::map<int8, int32>::iterator itr = m_serviceMask.find(operationID);
    return (itr == m_serviceMask.end() ? 0 : itr->second);
}

uint32 StationDataMgr::GetOutpostInSystem(uint32 systemID)
{
    for (auto& cur : m_stationData)
        if (IsOutpost(cur.first) and (cur.second.systemID == systemID))
            return cur.first;
    return 0;
}

// Add an outpost to the datamgr and the db
void StationDataMgr::AddOutpost(StationData &data) {
    //Add the new data to the db
    StationDB::CreateOutpost(data);

    //Load the new data into the data manager
    m_stationData.emplace(data.stationID, data);
    m_stationPyData.emplace(data.stationID, MakeStationPyData(data.stationID, data));
}

/** errata
 *  office folderIDs are stationID + 6m  (for the 66m range)
 *
 *  flags for offices:
 *
 *
 *   itemTypeIDs
 * 26   Office Folder
 * 27   Office
 * 28   Factory Folder
 *
 */

