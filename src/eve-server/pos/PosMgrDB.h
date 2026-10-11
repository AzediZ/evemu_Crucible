
/**
 * @name PosMgrDB.h
 *   DataBase methods for all POS items
 *
 * @Author:         Allan
 * @date:   8 December 17
 */


#ifndef EVEMU_POS_POSMGR_H_
#define EVEMU_POS_POSMGR_H_

#include "ServiceDB.h"
#include "../../eve-common/EVE_POS.h"



class StructureSE;

class PosMgrDB
: public ServiceDB
{
public:
    PyRep *GetSiloCapacityForType(uint16 typeID);
    PyRep* GetCorpControlTowers(uint32 corpID);

    // systemID, itemID of the unlinked, anchored bridges of the alliance's corps (or the corp's, without an alliance)
    static void GetLinkableJumpArrays(uint32 allianceID, uint32 corpID, DBQueryResult& res);
    static void GetCorpJumpArrays(uint32 corpID, DBQueryResult& res);
    static void GetAllianceJumpArrays(uint32 allyID, DBQueryResult& res);
    // systemID, toSystemID, one row per linked pair of the alliance's corps' bridges
    static void GetAllianceJumpBridges(uint32 allianceID, DBQueryResult& res);
    // posStructureData state, -2 if the structure has no data
    static int8 GetStructureState(uint32 itemID);
    // the corp's current alliance, 0 for none
    static uint32 GetCorpAllianceID(uint32 corpID);
    // relationship (contact standing) ownerID (corp or alliance) set for contactID.  false if none
    static bool GetContactStanding(uint32 ownerID, uint32 contactID, float& standing);

    void GetControlTowerFuelRequirements(DBQueryResult& res);
    // solar systems with control towers that are onlining, online or reinforced
    static void GetActiveTowerSystems(std::vector<uint32>& into);

    /* moon mining */
    // a moon's materials (typeID/quantity).  false when they were never generated
    static bool GetMoonResources(uint32 moonID, std::map<uint16, uint8>& into);
    static void SaveMoonResources(uint32 moonID, const std::map<uint16, uint8>& resources);
    // a tower's moon process links
    static void GetProcessLinks(uint32 towerID, std::vector<EVEPOS::ProcessLink>& into);
    static void ReplaceProcessLinks(uint32 towerID, const std::vector<EVEPOS::ProcessLink>& links);

    // pos data methods
    void DeleteData(uint32 itemID);

    bool GetBaseData(EVEPOS::StructureData& data);
    void SaveBaseData(EVEPOS::StructureData& data);
    void UpdateBaseData(EVEPOS::StructureData& data);

    bool GetTowerData(EVEPOS::TowerData& tData, EVEPOS::StructureData& sData);
    void SaveTowerData(EVEPOS::TowerData& tData, EVEPOS::StructureData& sData);
    // fuel cycle, reinforcement, calendar and notification state, and the field harmonic
    void UpdateTowerRuntime(uint32 itemID, EVEPOS::TowerData& data);

    bool GetBridgeData(EVEPOS::JumpBridgeData& data);
    void SaveBridgeData(EVEPOS::JumpBridgeData& data);
    void UpdateBridgeData(EVEPOS::JumpBridgeData& data);
    void InstallBridgeLink(uint32 itemID, uint32 toSystemID, uint32 toItemID, uint16 toTypeID);
    bool HasBridge(uint32 systemID);
    void UninstallBridgeLink(uint32 itemID);
    void UninstallRemoteBridgeLink(uint32 itemID);


    bool GetCustomsData(EVEPOS::CustomsData& cData, EVEPOS::OrbitalData& oData);
    void SaveCustomsData(EVEPOS::CustomsData& cData, EVEPOS::OrbitalData& oData);
    void UpdateCustomsData(EVEPOS::CustomsData& cData, EVEPOS::OrbitalData& oData);

    void UpdateAccess(int32 itemID, EVEPOS::TowerData& data);
    void UpdateNotify(int32 itemID, EVEPOS::TowerData& data);
    void UpdateSentry(int32 itemID, EVEPOS::TowerData& data);
    void UpdatePassword(int32 itemID, EVEPOS::TowerData& data);
    void UpdateTimeStamp(int32 itemID, EVEPOS::StructureData& data);
    void UpdatePermission(int32 itemID, EVEPOS::TowerData& data);
    void UpdateUsageFlags(int32 itemID, EVEPOS::StructureData& data);
    void UpdateDeployFlags(int32 itemID, EVEPOS::TowerData& data);
    void UpdateHarmonicAndPassword(int32 itemID, EVEPOS::TowerData& data);

};

#endif  // EVEMU_POS_POSMGR_H_

/*environment/spaceObject/station.py*/
