/**
 * @name Outpost.cpp
 *   Class for Outposts.
 *
 * @Author:           James
 * @date:   17 October 2021
 */

#include "station/Outpost.h"
#include "Client.h"
#include "system/SystemManager.h"
#include "StaticDataMgr.h"
#include "manufacturing/FactoryDB.h"
#include "station/StationDataMgr.h"
#include "../eve-common/EVE_Outpost.h"
#include "../eve-common/EVE_Station.h"

OutpostSE::OutpostSE(StationItemRef station, EVEServiceManager &services, SystemManager* system)
: StationSE(station, services, system) 
{
    // Create default dynamic attributes in the AttributeMap:
    station->SetAttribute(AttrOnline,             EvilOne, false);
    station->SetAttribute(AttrCapacity,           STATION_HANGAR_MAX_CAPACITY, false);
    station->SetAttribute(AttrInertia,            EvilOne, false);
    station->SetAttribute(AttrDamage,             EvilZero, false);
    station->SetAttribute(AttrShieldCapacity,     20000000.0, false);
    station->SetAttribute(AttrShieldCharge,       station->GetAttribute(AttrShieldCapacity), false);
    station->SetAttribute(AttrArmorHP,            station->GetAttribute(AttrArmorHP), false);
    station->SetAttribute(AttrArmorUniformity,    station->GetAttribute(AttrArmorUniformity), false);
    station->SetAttribute(AttrArmorDamage,        EvilZero, false);
    station->SetAttribute(AttrMass,               station->type().mass(), false);
    station->SetAttribute(AttrRadius,             station->type().radius(), false);
    station->SetAttribute(AttrVolume,             station->type().volume(), false);
}

// Each outpost has station service entities which are spawned at the same position as the station itself
void OutpostSE::SpawnStationService(Client* pClient, StationData stData, uint32 serviceType) {
    ItemData svcData(serviceType, stData.corporationID, stData.systemID, flagNone);
    InventoryItemRef svcRef = sItemFactory.SpawnItem(svcData);
    ItemSystemEntity* svcSE = new ItemSystemEntity(svcRef, pClient->services(), pClient->SystemMgr());
    svcSE->SetPosition(stData.position);
    svcRef->SaveItem();
    pClient->SystemMgr()->AddEntity(svcSE);
}

void OutpostSE::SpawnStationServices(Client* pClient, const StationData& stData, uint32 oldMask, uint32 newMask)
{
    // the service entities StationDB::GetStationServiceStates knows
    static const std::pair<uint32, uint32> services[6] = {
        {Station::Fitting,              EVEDB::invTypes::FittingService},
        {Station::ReprocessingPlant,    EVEDB::invTypes::ReprocessingService},
        {Station::Factory,              EVEDB::invTypes::FactoryService},
        {Station::Cloning,              EVEDB::invTypes::CloningService},
        {Station::RepairFacilities,     EVEDB::invTypes::RepairService},
        {Station::Laboratory,           EVEDB::invTypes::LaboratoryService}
    };
    for (auto& cur : services)
        if (((newMask & cur.first) != 0) and ((oldMask & cur.first) == 0))
            SpawnStationService(pClient, stData, cur.second);
}

bool OutpostSE::Refresh(uint32 stationID)
{
    StationData data = StationData();
    if (!stDataMgr.GetStationData(stationID, data))
        return false;
    OutpostRules::Race race(OutpostRules::OutpostRace(data.typeID));
    if (race == OutpostRules::Race::None)
        return false;

    OutpostRules::Slots slots = {{0, 0, 0, 0, 0, 0}};
    for (uint8 i = 0; i < OutpostRules::SlotCount; ++i)
        slots[i] = data.improvements[i];

    // services:  the operation's (staOperationServices) and what the improvements add
    data.serviceMask = stDataMgr.GetOperationServiceMask(data.operationID) | OutpostRules::AddedServices(slots);
    data.officeSlots = (uint8)OutpostRules::OfficeSlots(race, OutpostRules::BaseOfficeSlots(race), slots);
    data.reprocessingEfficiency = (float)OutpostRules::RefineryEfficiency(race, OutpostRules::TierOf(slots, OutpostRules::Kind::Refinery),
                                                                          StationDB::GetStationTypeReprocessingEfficiency(data.typeID));

    // assembly lines:  the type's own (ramInstallationTypeContents) as the improvements change them
    std::vector<std::pair<uint8, uint8>> contents;
    sDataMgr.GetInstallationContents(data.typeID, contents);
    std::map<uint8, uint16> base;
    for (auto& cur : contents)
        base[cur.first] += cur.second;
    FactoryDB::SyncStationAssemblyLines(stationID, data.typeID, data.corporationID, data.systemID, data.regionID,
                                        OutpostRules::AssemblyLines(race, base, slots));

    uint16 saved[6];
    for (uint8 i = 0; i < OutpostRules::SlotCount; ++i)
        saved[i] = slots[i];
    StationDB::SaveOutpostImprovements(stationID, saved);
    StationDB::SaveOutpostStats(stationID, data.upgradeLevel, data.officeSlots, data.reprocessingEfficiency);
    stDataMgr.UpdateStationData(data);

    // the station item keeps its own copy (office count, refining)
    StationItemRef sRef(sItemFactory.GetStationRef(stationID));
    if (sRef.get() != nullptr)
        sRef->UpdateSettings();

    _log(POS__MESSAGE, "OutpostSE::Refresh() - %s(%u):  level %u, services 0x%x, %u offices, refining %.2f",
         data.name.c_str(), stationID, data.upgradeLevel, (uint32)data.serviceMask, data.officeSlots, data.reprocessingEfficiency);
    return true;
}
