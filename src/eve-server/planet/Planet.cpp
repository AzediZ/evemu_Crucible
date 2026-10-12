
 /**
  * @name Planet.cpp
  *   Specific Class for individual planets.
  * this class will hold all planet data and relative info for each planet.
  *
  * @Author:         Allan
  * @date:   30 April 2016
  */

 /*
  * PLANET__ERROR
  * PLANET__WARNING
  * PLANET__MESSAGE
  * PLANET__DEBUG
  * PLANET__INFO
  * PLANET__TRACE
  * PLANET__DUMP
  * PLANET__RES_DUMP
  * PLANET__GC_DUMP
  * PLANET__PKT_TRACE
  * PLANET__DB_ERROR
  * PLANET__DB_WARNING
  */


#include "Client.h"
#include "EVEServerConfig.h"
#include "math/Trig.h"
#include "planet/Colony.h"
#include "planet/Planet.h"
#include "planet/PlanetMgr.h"
#include "planet/PlanetDataMgr.h"
#include "planet/PlanetDB.h"
#include "planet/CustomsOffice.h"
#include "packets/Planet.h"
#include "pos/Structure.h"
#include "ship/JumpDrive.h"
#include "system/Celestial.h"
#include "system/CrimeWatch.h"
#include "system/SystemManager.h"


Planet::Planet()
{
    /** @todo  will need to make an InventoryItem class specific for planets here eventually */
}


PlanetSE::PlanetSE(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: StaticSystemEntity(self, services, system),
pCO(nullptr)
{
}

PlanetSE::~PlanetSE()
{
    // colonies are saved after every change.  this keeps the simulated time
    for (auto cur : m_colonies) {
        cur.second->Save();
        SafeDelete(cur.second);
    }
}

bool PlanetSE::LoadExtras() {
    if (!StaticSystemEntity::LoadExtras())
        return false;

    // the planet type's resources.  their maps are made when they are first used
    sPlanetDataMgr.GetPlanetData(m_self->typeID(), m_resourceTypes);

    // should we check for a CO here?
    //  no, it hasnt been loaded at this point

    return true;
}

// called from SystemManager::Process() @ 1m
void PlanetSE::Process()
{
    // colonies are simulated when they are looked at (Colony::SimulateToNow), not on a timer
}

bool PlanetSE::IsResourceType(uint16 typeID)
{
    return ((typeID != 0) and (std::find(m_resourceTypes.begin(), m_resourceTypes.end(), typeID) != m_resourceTypes.end()));
}

PlanetSE::ResourceState& PlanetSE::GetResourceState(uint16 typeID)
{
    ResourceState& state = m_resources[typeID];
    if (state.base.bands == 0) {
        PIResource::Generate(m_self->itemID(), typeID, m_system->GetSystemSecurityRating(), state.base, state.richness);
        PlanetDB::LoadDepletion(m_self->itemID(), typeID, state.depletion);
        state.dirty = true;
    }
    // planetResourceUpdateTime:  the map changes over the hour as depletion builds up and grows back
    int64 now = GetFileTimeNow();
    if (state.dirty or (state.builtTime + EvE::Time::Hour < now)) {
        // depletion that has grown back goes
        std::vector<uint32> faded;
        std::vector<PIResource::Depletion> rows;
        for (auto& cur : state.depletion) {
            if ((now > cur.startTime + cur.duration) and (PIResource::DepletionFactor(cur, now) < 0.01)) {
                faded.push_back(cur.depletionID);
            } else {
                rows.push_back(cur);
            }
        }
        if (!faded.empty()) {
            PlanetDB::DeleteDepletion(faded);
            state.depletion.swap(rows);
        }
        PIResource::ApplyDepletion(state.base, state.depletion, now, state.final);
        state.builtTime = now;
        state.dirty = false;
    }
    return state;
}

void PlanetSE::AddDepletion(uint16 typeID, std::vector<PIResource::Depletion>& rows)
{
    if (!sConfig.cosmic.PIDepletionEnabled or rows.empty())
        return;
    ResourceState& state = GetResourceState(typeID);
    for (auto& cur : rows) {
        cur.depletionID = PlanetDB::AddDepletion(m_self->itemID(), typeID, cur);
        state.depletion.push_back(cur);
    }
    state.dirty = true;
}

void PlanetSE::StopDepletion(uint16 typeID, int64 startTime, double latitude, double longitude)
{
    int64 now = GetFileTimeNow();
    PlanetDB::TruncateDepletion(m_self->itemID(), typeID, startTime, latitude, longitude, now);
    std::map<uint16, ResourceState>::iterator itr = m_resources.find(typeID);
    if (itr == m_resources.end())
        return;
    for (auto& cur : itr->second.depletion)
        if ((cur.startTime == startTime) and (cur.startTime + cur.duration > now) and (now > cur.startTime)
        and (std::fabs(cur.latitude - latitude) < 1e-9) and (std::fabs(cur.longitude - longitude) < 1e-9)) {
            cur.amount *= double(now - cur.startTime) / cur.duration;
            cur.duration = now - cur.startTime;
        }
    itr->second.dirty = true;
}

void PlanetSE::InvalidateResource(uint16 typeID)
{
    std::map<uint16, ResourceState>::iterator itr = m_resources.find(typeID);
    if (itr != m_resources.end())
        itr->second.dirty = true;
}

ColonySim::ResourceField PlanetSE::GetResourceField(uint16 typeID)
{
    // the final (depleted) map at full detail.  the client's preview is the same map, cut to its bands
    if (!IsResourceType(typeID))
        return [](double, double) { return 0.0; };
    std::shared_ptr<PIResource::Field> field(new PIResource::Field(GetResourceState(typeID).final));
    return [field](double azimuth, double polar) { return field->Value(azimuth, polar); };
}

PyRep* PlanetSE::GetResourceData(Client* pClient, Call_ResourceDataDict& dict)
{
    /* clientPlanet.GetResourceData / CreateResourceInfo:  the bands a character sees depend on their distance from the
     *  planet (planetResourceScanningRanges, light years) and their skills.  the client asks, the server decides.
     *   proximity = the closest scanning range the distance is within (planetResourceProximityPlanet in system)
     *   newBand = min(maxBand, minBand + Planetology + 2 x Advanced Planetology)  (planetResourceProximityLimits)
     *   Remote Sensing must be at least 5 - proximity, and the distance within the range of that level
     */
    static const double scanningRanges[5] = { 9.0, 7.0, 5.0, 3.0, 1.0 };
    static const uint8 proximityLimits[5][2] = { {2, 6}, {4, 10}, {6, 15}, {10, 20}, {15, 30} };

    PyDict* args = new PyDict();
    args->SetItemString("data", PyStatic.NewNone());
    args->SetItemString("numBands", new PyInt(0));
    args->SetItemString("proximity", PyStatic.NewNone());
    PyObject* rtn = new PyObject("util.KeyVal", args);
    if (!IsResourceType(dict.resourceTypeID))
        return rtn;

    uint32 mySystemID = pClient->GetSystemID();
    int8 remoteSensing = pClient->GetChar()->GetSkillLevel(EvESkill::RemoteSensing);
    if (remoteSensing < 1)
        return rtn;
    if ((mySystemID != m_system->GetID()) and (IsWSpaceID(mySystemID) or IsWSpaceID(m_system->GetID())))
        return rtn;     // wormhole space can't be scanned remotely
    double distance = (mySystemID == m_system->GetID() ? 0.0 : JumpDrive::LightYears(mySystemID, m_system->GetID()));
    int8 proximity = -1;
    for (int8 i = 0; i < 5; ++i)
        if (scanningRanges[i] >= distance)
            proximity = i;
    if ((proximity < 0) or (distance > scanningRanges[std::max(0, std::min(4, 5 - remoteSensing))]))
        return rtn;
    if (remoteSensing < 5 - proximity)
        return rtn;
    int8 planetology = pClient->GetChar()->GetSkillLevel(EvESkill::Planetology);
    int8 advancedPlanetology = pClient->GetChar()->GetSkillLevel(EvESkill::AdvancedPlanetology);
    uint8 allowed = std::min((int)proximityLimits[proximity][1], proximityLimits[proximity][0] + planetology + 2 * advancedPlanetology);
    uint8 numBands = (uint8)std::max(1, std::min((int)allowed, (int)dict.newBand));
    numBands = std::min(numBands, PIResource::MaxBands);

    ResourceState& state = GetResourceState(dict.resourceTypeID);
    args->SetItemString("data", new PyString(state.final.GetBuffer(numBands)));
    args->SetItemString("numBands", new PyInt(numBands));
    args->SetItemString("proximity", new PyInt(proximity));

    _log(PLANET__DEBUG, "PlanetSE::GetResourceData() - %s for %s on %s:  %.2f ly, proximity %u, remote sensing %u, planetology %u/%u, %u bands (asked %u)", \
            sPIDataMgr.GetProductName(dict.resourceTypeID), pClient->GetName(), GetName(), distance, proximity, remoteSensing, \
            planetology, advancedPlanetology, numBands, dict.newBand);
    return rtn;
}

PyRep* PlanetSE::GetLocalDistributionReport(double azimuth, double polar)
{
    PyDict* base = new PyDict();
    PyDict* quality = new PyDict();
    PyDict* deplete = new PyDict();
    PyDict* final = new PyDict();
    PyDict* raw = new PyDict();
    for (auto cur : m_resourceTypes) {
        ResourceState& state = GetResourceState(cur);
        double baseValue = state.base.Value(azimuth, polar);
        double finalValue = state.final.Value(azimuth, polar);
        base->SetItem(new PyInt(cur), new PyFloat(baseValue));
        quality->SetItem(new PyInt(cur), new PyFloat(state.richness));
        deplete->SetItem(new PyInt(cur), new PyFloat(baseValue - finalValue));
        final->SetItem(new PyInt(cur), new PyFloat(std::max(0.0, finalValue)));
        raw->SetItem(new PyInt(cur), new PyFloat(state.richness > 0 ? baseValue / state.richness : 0.0));
    }
    PyDict* report = new PyDict();
        report->SetItemString("base", base);
        report->SetItemString("quality", quality);
        report->SetItemString("deplete", deplete);
        report->SetItemString("final", final);
        report->SetItemString("raw", raw);
    return report;
}

PyRep* PlanetSE::GetLayerData(Client* pClient, uint16 typeID, const std::string& layer)
{
    if (!IsResourceType(typeID))
        throw CustomError ("That resource is not found on this planet.");
    ResourceState& state = GetResourceState(typeID);
    PIResource::Field field;
    if ((layer == "current") or (layer == "final")) {
        field = state.final;
    } else if (layer == "base") {
        field = state.base;
    } else if (layer == "depletion") {
        field = state.base;
        for (size_t i = 0; i < field.coeff.size(); ++i)
            field.coeff[i] = state.base.coeff[i] - state.final.coeff[i];
    } else if (layer == "player") {
        // what this character's scan gets (all the bands a scan from here allows)
        Call_ResourceDataDict dict;
            dict.resourceTypeID = typeID;
            dict.newBand = PIResource::MaxBands;
        return GetResourceData(pClient, dict);
    } else {
        throw CustomError ("Resource layer %s is not supported.", layer.c_str());
    }
    PyDict* args = new PyDict();
        args->SetItemString("data", new PyString(field.GetBuffer(field.bands)));
        args->SetItemString("numBands", new PyInt(field.bands));
    return new PyObject("util.KeyVal", args);
}

PyRep* PlanetSE::GetPlanetResourceInfo()
{
    // {typeID: quality}.  the client's quality bar is quality / (1.21 x 255 x 0.5), so the bar shows the richness
    PyDict* res = new PyDict();
    for (auto cur : m_resourceTypes)
        res->SetItem(new PyInt(cur), new PyFloat(PIResource::MaxDisplayQuality * GetResourceState(cur).richness));
    if (is_log_enabled(PLANET__RES_DUMP))
        res->Dump(PLANET__RES_DUMP, "   ");
    return res;
}

PyRep* PlanetSE::GetPlanetInfo(Colony* pColony) {
    PyDict *args = new PyDict();
    args->SetItem("planetTypeID", new PyInt(m_self->typeID()));
    args->SetItem("solarSystemID", new PyInt(m_system->GetID()));
    args->SetItem("radius", new PyInt(GetRadius()));
    args->SetItem("planetID", new PyInt(m_self->itemID()));
    // the colony keys only when the caller has a colony here.  the client runs its simulation from currentSimTime
    if ((pColony != nullptr) and pColony->HasColony()) {
        pColony->SimulateToNow();
        pColony->SerializeInto(args);
    }
    //PyIncRef(args);
    PyObject *rtn = new PyObject("util.KeyVal", args);
    if (is_log_enabled(PLANET__RES_DUMP))
        rtn->Dump(PLANET__RES_DUMP, "   ");
    return rtn;
}

PyRep* PlanetSE::GetExtractorsForPlanet(int32 planetID) {
    // NOTE this gets ALL extractors on this planet
    // returns typeID, ownerID, latitude?, longitude?

    //  the heads of running programs:  KeyVal(pinID, typeID, ownerID, latitude, longitude).  typeID is the extractor head type;
    //  the client shows the ones whose harvesterType is the resource it is looking at (otherPinManager)
    DBQueryResult res;
    PlanetDB::GetExtractorsForPlanet(planetID, res);

    PyList* list = new PyList();
    DBResultRow row;
    while (res.GetRow(row)) {
        // SELECT ecuID, headID, ecuTypeID, programType, ownerID, latitude, longitude
        uint16 headType = sPIDataMgr.GetHeadType(row.GetUInt(2), row.GetUInt(3));
        if (headType == 0)
            continue;
        PyDict* dict(new PyDict());
            dict->SetItem("pinID", new PyLong(int64(row.GetUInt(0)) * 16 + row.GetUInt(1)));    // unique per head
            dict->SetItem("typeID", new PyInt(headType));
            dict->SetItem("ownerID", new PyInt(row.GetUInt(4)));
            dict->SetItem("latitude", new PyFloat(row.GetDouble(5)));
            dict->SetItem("longitude", new PyFloat(row.GetDouble(6)));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }

    if (is_log_enabled(PLANET__RES_DUMP))
        list->Dump(PLANET__RES_DUMP, "   ");
    return list;
}

Colony* PlanetSE::GetColony(uint32 charID)
{
    std::map<uint32, Colony*>::const_iterator itr = m_colonies.find(charID);
    if (itr != m_colonies.end())
        return itr->second;
    Colony* pColony = new Colony(m_services, charID, this);
    pColony->Init();
    m_colonies[charID] = pColony;

    return pColony;
}

void PlanetSE::AbandonColony(Client* pClient, Colony* pColony)
{
    pColony->AbandonColony(pClient);
    m_colonies.erase(pColony->GetOwner());
    SafeDelete(pColony);
}

GPoint PlanetSE::GetCustomsOfficePosition()
{
    // the office's slot:  700km off the planet's surface, the same spot every time
    GPoint pos(GetPosition());
    uint32 radius = GetRadius();
    float rad = EvE::Trig::Deg2Rad(25);
    pos.x += radius + 700000 * std::sin(rad);
    pos.z += radius + 700000 * std::cos(rad);
    pos.y += ((int32)(m_self->itemID() % 2001) - 1000);   // a little height, per planet (signed:  the unsigned sum wrapped to 4e9 m)
    return pos;
}

void PlanetSE::CreateCustomsOffice()
{
    /* Crucible:  every planet has a customs office.  in high security it belongs to CONCORD ("Customs Office"),
     *  elsewhere (low, null and wormhole space) to Interbus.  a planet gets its npc office once:  a destroyed Interbus
     *  office is replaced by players' offices, never respawned (piCustomsOfficeSpawn)
     */
    FactionData data = FactionData();
        data.ownerID = corpInterbus;
        data.factionID = factionInterBus;
        data.allianceID = 0;
        data.corporationID = corpInterbus;
    uint16 typeID = EVEDB::invTypes::InterbusCustomsOffice;

    if (CrimeWatch::SecurityClass(m_system->GetSystemSecurityRating()) == Crime::SecClass::High) {
        typeID = EVEDB::invTypes::PlanetaryCustomsOffice;
        data.ownerID = corpCONCORD;
        data.factionID = factionCONCORD;
        data.allianceID = 0;
        data.corporationID = corpCONCORD;
    }

    GPoint pos(GetCustomsOfficePosition());
    ItemData idata(typeID, data.ownerID, m_system->GetID(), flagNone, 1, itoa(m_self->itemID()), false);
    StructureItemRef iRef = sItemFactory.SpawnStructure(idata);
    if (iRef.get() == nullptr) {
        _log(PLANET__ERROR, "PlanetSE::CreateCustomsOffice() - unable to spawn the customs office for %s(%u)", GetName(), GetID());
        return;
    }
    iRef->SetPosition(pos);
    iRef->ChangeSingleton(true, false);
    iRef->SaveItem();
    pCO = new CustomsSE(iRef, m_services, m_system, data);
    pCO->Init();
    m_system->AddEntity(pCO);
    PlanetDB::MarkCustomsOfficeSpawned(m_self->itemID());
}
