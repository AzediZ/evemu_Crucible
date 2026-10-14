/**
 * @name StationDB.h
 *      database methods for station data
 *
 * @author Allan
 * @date 14 December 2017
 *
 */


#ifndef EVE_STATION_STATIONDB_H
#define EVE_STATION_STATIONDB_H

#include "ServiceDB.h"
#include "inventory/ItemType.h"


class StationDB
: public ServiceDB
{
public:
    void UpdateOfficeData(OfficeData& data);

    static PyRep* GetOffices(uint32 stationID);
    static PyRep* GetStationOfficeIDs(uint32 locationID, uint32 corpID, const char* key);

    static uint32 CreateOffice(ItemData& idata, OfficeData& odata);
    static bool GetOfficeData(uint32 officeID, OfficeData& odata);
    static void GetStationData(DBQueryResult& res);
    static void GetStationBaseData(DBQueryResult& res, uint32 typeID);
    static void GetStationSystem(DBQueryResult& res);
    static void GetStationRegion(DBQueryResult& res);
    static void GetStationOfficeData(DBQueryResult& res);
    static void GetOperationServiceIDs(DBQueryResult& res);
    static void GetStationConstellation(DBQueryResult& res);

    static int32 GetOfficeCount(uint32 corpID);
    // officeID -> stationID of the corp's offices
    static void GetCorpOffices(uint32 corpID, std::map<uint32, uint32>& into);
    static void DeleteOffice(uint32 officeID);
    // rent lifecycle (F21):  impounded offices keep their row and contents (flagImpounded) but are out of the station's office lists
    static void SetOfficeImpounded(uint32 officeID, bool impounded);
    static void SetOfficeLease(uint32 officeID, int64 expiryDateTime, uint32 billID);
    static void SetOfficePublic(uint32 officeID, bool publiclyAvailable);
    // the corp's office in the station (active or impounded).  0 = none
    static uint32 GetCorpOffice(uint32 corpID, uint32 stationID, bool impounded);
    // active offices still to be billed (publicly available, no bill)
    static void GetOfficesWithoutBill(std::vector<uint32>& into);
    // active offices past their lease and without a bill
    static void GetLapsedOffices(int64 now, std::vector<uint32>& into);
    // active offices whose open rent bill is due before 'before'
    static void GetOfficesWithBillDue(int64 before, std::vector<uint32>& into);
    static uint32 CountImpoundedItems(uint32 officeID);

    static void LoadOffices(OwnerData &od, std::vector<uint32> &into);
    static void CreateOutpost(StationData& data);
    static uint32 GetNewOutpostID();
    static void GetStationServiceStates(uint32 stationID, DBQueryResult& res);
    static void GetStationServiceIdentifiers(DBQueryResult& res);
    static void GetStationServiceAccessRule(uint32 stationID, uint32 serviceID, DBQueryResult& res);
    static void GetStationManagementServiceCostModifiers(uint32 stationID, DBQueryResult& res);
    static void GetStationDetails(uint32 stationID, DBQueryResult& res);
    static void GetRentableItems(uint32 stationID, DBQueryResult& res);
    // outpost management (F21)
    static uint32 GetStandingOwner(uint32 stationID);
    static void SetServiceAccessRule(uint32 stationID, uint32 serviceID, double minimumStanding, double minimumCharSecurity,
                                     double maximumCharSecurity, double minimumCorpSecurity, double maximumCorpSecurity);
    static void SetServiceCostModifier(uint32 stationID, uint32 serviceID, double discountPerGoodStandingPoint, double surchargePerBadStandingPoint);
    // exitTime -1 / standingOwnerID 0 = none
    static void UpdateStationSettings(uint32 stationID, const std::string& name, double dockingCostPerVolume, int32 officeRentalCost,
                                      double reprocessingStationsTake, int32 reprocessingHangarFlag, int32 exitTime, int32 standingOwnerID);
    static void SetStationOwner(uint32 stationID, uint32 corpID);
    static void GetOwnerIDsOfClonesAtStation(uint32 stationID, uint32 corpID, DBQueryResult& res);
    static void GetOutpostImprovementStaticData(DBQueryResult& res);
    static void GetOutpostImprovements(uint32 stationID, DBQueryResult& res);
    // outpost upgrades (staImprovementsInstalled) of every outpost:  stationID, tier 1a, 2a, 3a, 1b, 2b, 1c
    static void GetAllOutpostImprovements(DBQueryResult& res);
    static void SaveOutpostImprovements(uint32 stationID, const uint16 (&slots)[6]);
    // the outpost's upgrade level and what it and the improvements give
    // staStationTypes.reprocessingEfficiency of a station type (0 for none)
    static float GetStationTypeReprocessingEfficiency(uint16 stationTypeID);
    static void SaveOutpostStats(uint32 stationID, uint8 upgradeLevel, uint8 officeSlots, float reprocessingEfficiency);
    // stations of a region for the remote buy order station picker (quote.py PickStation)
    static PyRep* GetStationsByRegion(uint32 regionID);

};

#endif  // EVE_STATION_STATIONDB_H
