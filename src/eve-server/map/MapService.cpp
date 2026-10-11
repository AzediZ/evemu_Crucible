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
    Author:        Zhur
    Rewrite:    Allan
*/

#include "eve-server.h"


#include "StaticDataMgr.h"
#include "map/MapData.h"
#include "map/MapService.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/sov/SovereigntyDB.h"
#include "map/MapDB.h"

MapService::MapService() :
    Service("map", eAccessLevel_Character)
{
    this->Add("GetHistory", &MapService::GetHistory);
    this->Add("GetBeaconCount", &MapService::GetBeaconCount);    //ColorStarsByCynosuralFields
    this->Add("GetStationCount", &MapService::GetStationCount);    //ColorStarsByStationCount
    this->Add("GetMyExtraMapInfo", &MapService::GetMyExtraMapInfo);     //ColorStarsByCorpMates
    this->Add("GetStationExtraInfo", &MapService::GetStationExtraInfo);
    this->Add("GetSolarSystemVisits", &MapService::GetSolarSystemVisits);
    this->Add("GetLinkableJumpArrays", &MapService::GetLinkableJumpArrays);
    this->Add("GetMyExtraMapInfoAgents", &MapService::GetMyExtraMapInfoAgents);  //ColorStarsByMyAgents
    this->Add("GetSolarSystemPseudoSecurities", &MapService::GetSolarSystemPseudoSecurities);

    /**  not handled yet...these are empty calls  */
    this->Add("GetStuckSystems", &MapService::GetStuckSystems);
    this->Add("GetRecentSovActivity", &MapService::GetRecentSovActivity);
    this->Add("GetDeadspaceAgentsMap", &MapService::GetDeadspaceAgentsMap);
    this->Add("GetDeadspaceComplexMap", &MapService::GetDeadspaceComplexMap);
    this->Add("GetIncursionGlobalReport", &MapService::GetIncursionGlobalReport);
    this->Add("GetSystemsInIncursions", &MapService::GetSystemsInIncursions);    //ColorStarsByIncursions
    this->Add("GetSystemsInIncursionsGM", &MapService::GetSystemsInIncursionsGM);  //ColorStarsByIncursions
    this->Add("GetVictoryPoints", &MapService::GetVictoryPoints);
    this->Add("GetAllianceJumpBridges", &MapService::GetAllianceJumpBridges);
    this->Add("GetAllianceBeacons", &MapService::GetAllianceBeacons);
    this->Add("GetCurrentSovData", &MapService::GetCurrentSovData);
    // custom call for displaying all items in system
    this->Add ("GetCurrentEntities", &MapService::GetCurrentEntities);
}

PyResult MapService::GetCurrentEntities(PyCallArgs &call)
{
    return call.client->SystemMgr()->GetCurrentEntities();
}

PyResult MapService::GetSolarSystemVisits(PyCallArgs &call)
{
    return MapDB::GetSolSystemVisits(call.client->GetCharacterID());
}

PyResult MapService::GetMyExtraMapInfoAgents(PyCallArgs &call)
{
    return StandingDB::GetMyStandings(call.client->GetCharacterID());
}

PyResult MapService::GetMyExtraMapInfo(PyCallArgs &call)
{
    return CharacterDB::GetMyCorpMates(call.client->GetCorporationID());
}

PyResult MapService::GetBeaconCount(PyCallArgs &call)
{
    return MapDB::GetDynamicData(2, 24);
}

PyResult MapService::GetStationExtraInfo(PyCallArgs &call)
{
    return sMapData.GetStationExtraInfo();
}

PyResult MapService::GetSolarSystemPseudoSecurities(PyCallArgs &call)
{
    // cant find a call to this in client (possible old call)
    return sMapData.GetPseudoSecurities();
}

PyResult MapService::GetStationCount(PyCallArgs &call)
{
    // cached on client side.  if cache is empty, this call is made.
    return sDataMgr.GetStationCount();
}

PyResult MapService::GetHistory(PyCallArgs &call, PyInt* int1, PyInt* int2) {
    if (is_log_enabled(SERVICE__CALLS))
        sLog.Cyan( "MapService::Handle_GetHistory()", "type: %i, timeframe: %i", int1, int2 );

    return MapDB::GetDynamicData(int1->value(), int2->value());
}

PyResult MapService::GetLinkableJumpArrays(PyCallArgs &call)
{   // working
    DBQueryResult res;
    PosMgrDB::GetLinkableJumpArrays(call.client->GetCorporationID(), res);
    PyList* list = new PyList();
    DBResultRow row;
    while (res.GetRow(row)) {
        // SELECT systemID, itemID
        PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(row.GetInt(0)));
        tuple->SetItem(1, new PyInt(row.GetInt(1)));
        list->AddItem(tuple);
    }

    return list;
}

/** not handled */

PyResult MapService::GetAllianceJumpBridges(PyCallArgs &call)
{
    /**     bridgesByLocation = m.GetAllianceJumpBridges()
     *      for toLocID, fromLocID in bridgesByLocation:
     */
    sLog.Warning( "MapService::Handle_GetAllianceJumpBridges()", "size=%lu", call.tuple->size());
    call.Dump(SERVICE__CALL_DUMP);

    DBQueryResult res;
    PosMgrDB::GetAllianceJumpArrays(call.client->GetAllianceID(), res);
    PyList* list = new PyList();
    DBResultRow row;
    while (res.GetRow(row)) {
        // SELECT systemID, itemID
        PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(row.GetInt(0)));
        tuple->SetItem(1, new PyInt(row.GetInt(1)));
        list->AddItem(tuple);
    }

    return list;
}

PyResult MapService::GetAllianceBeacons(PyCallArgs &call)
{
    sLog.Warning( "MapService::Handle_GetAllianceBeacons()", "size=%lu", call.tuple->size());
    call.Dump(SERVICE__CALL_DUMP);

    // Get data directly from sovereignty manager, avoiding db hits
    return svDataMgr.GetAllianceBeacons(call.client->GetAllianceID());
}

PyResult MapService::GetCurrentSovData(PyCallArgs &call, PyRep* locationID)
{
    /*  sovSvc.GetCurrentData():
     *    a solar system asks for its constellation:  data = sm.RemoteSvc('map').GetCurrentSovData(constellationID)
     *    anything else asks for itself (None for the world):  return sm.RemoteSvc('map').GetCurrentSovData(locationID)
     */
    uint32 locID(0);
    if (locationID->IsInt())
        locID = locationID->AsInt()->value();
    else if (locationID->IsLong())
        locID = (uint32)locationID->AsLong()->value();

    return svDataMgr.GetCurrentSovData(locID);
}
PyResult MapService::GetRecentSovActivity(PyCallArgs &call)
{
    /** @todo will have to make db table for this one.  */
    /*
     *    data = sm.RemoteSvc('map').GetRecentSovActivity()
     *    changes = []
     *    resultMap = {}
     *    toPrime = set()
     *    for item in data:
     *        if item.stationID is None:
     *            if bool(changeMode & mapcommon.SOV_CHANGES_SOV_GAIN) and item.oldOwnerID is None:
     *                changes.append((item.solarSystemID, 'UI/Map/StarModeHandler/sovereigntySovGained', (None, item.ownerID)))
     *                toPrime.add(item.ownerID)
     *            elif bool(changeMode & mapcommon.SOV_CHANGES_SOV_LOST) and item.ownerID is None:
     *                changes.append((item.solarSystemID, 'UI/Map/StarModeHandler/sovereigntySovLost', (item.oldOwnerID, None)))
     *                toPrime.add(item.oldOwnerID)
     *        elif bool(changeMode & mapcommon.SOV_CHANGES_SOV_GAIN) and item.oldOwnerID is None:
     *            changes.append((item.solarSystemID, 'UI/Map/StarModeHandler/sovereigntyNewOutpost', (None, item.ownerID)))
     *            toPrime.add(item.ownerID)
     *        elif bool(changeMode & mapcommon.SOV_CHANGES_SOV_GAIN) and item.ownerID is not None:
     *            changes.append((item.solarSystemID, 'UI/Map/StarModeHandler/sovereigntyConqueredOutpost', (item.ownerID, item.oldOwnerID)))
     *            toPrime.add(item.ownerID)
     *            toPrime.add(item.oldOwnerID)
     *
     */

    // live:  changes of the last 24 hours.  F14 logs them with SovereigntyDB::LogSovChange
    DBQueryResult res;
    SovereigntyDB::GetRecentSovChanges(res, (int64)GetFileTimeNow() - EvE::Time::Day);
    PyList* list = new PyList();
    DBResultRow row;
    while (res.GetRow(row)) {
        PyDict* dict = new PyDict();
            dict->SetItemString("solarSystemID",    new PyInt(row.GetInt(0)));
            dict->SetItemString("stationID",        (row.IsNull(1) ? PyStatic.NewNone() : new PyInt(row.GetInt(1))));
            dict->SetItemString("ownerID",          (row.IsNull(2) ? PyStatic.NewNone() : new PyInt(row.GetInt(2))));
            dict->SetItemString("oldOwnerID",       (row.IsNull(3) ? PyStatic.NewNone() : new PyInt(row.GetInt(3))));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
    return list;
}

//   DED Agent Site Report
PyResult MapService::GetDeadspaceAgentsMap(PyCallArgs &call, PyRep* languageID)
{/*
        dungeons = sm.RemoteSvc('map').GetDeadspaceAgentsMap(eve.session.languageID)
        for solarSystemID, dungeonID, difficulty, dungeonName in dungeons:
    agent mission sites are not tracked by system yet (F12 may provide them).  an empty list is a valid answer
*/
    return new PyList();
}

//  DED Deadspace Report
PyResult MapService::GetDeadspaceComplexMap(PyCallArgs &call, PyRep* languageID)
{/*
        dungeons = sm.RemoteSvc('map').GetDeadspaceComplexMap(eve.session.languageID)
        for solarSystemID, dungeonID, difficulty, dungeonName in dungeons:
    live complexes from sysSignatures.  difficulty is the DED level when the name ends in "N/10", else 0
*/
    DBQueryResult res;
    MapDB::GetDeadspaceComplexes(res);
    PyList* list = new PyList();
    DBResultRow row;
    while (res.GetRow(row)) {
        std::string name(row.GetText(3));
        int32 difficulty(0);
        size_t pos = name.rfind("/10");
        if ((pos != std::string::npos) and (pos > 0) and (pos + 3 == name.size())) {
            size_t start = pos;
            while ((start > 0) and isdigit(static_cast<unsigned char>(name[start - 1])))
                --start;
            if (start < pos)
                difficulty = atoi(name.substr(start, pos - start).c_str());
            if ((difficulty < 0) or (difficulty > 10))
                difficulty = 0;
        }
        PyTuple* tuple = new PyTuple(4);
            tuple->SetItem(0, new PyInt(row.GetInt(0)));    // solarSystemID
            tuple->SetItem(1, new PyInt(row.GetInt(1)));    // dungeonID (the site's item)
            tuple->SetItem(2, new PyInt(difficulty));
            tuple->SetItem(3, new PyWString(name));
        list->AddItem(tuple);
    }
    return list;
}

PyResult MapService::GetSystemsInIncursions(PyCallArgs &call) {
    /**  EVE_Incursion.h
     *        participatingSystems = ms.GetSystemsInIncursions()
     *        for solarSystemID, sceneType in participatingSystems:
     *            -- (sceneType = staging, vanguard)
     */

    // no incursions yet:  F25 replaces this with incursion manager data (list of (solarSystemID, sceneType))
    return new PyList();
}

PyResult MapService::GetSystemsInIncursionsGM(PyCallArgs &call) {
    /**
     *        participatingSystems = ms.GetSystemsInIncursionsGM()
     *        for solarSystemID, sceneType in participatingSystems:
     *            -- (sceneType = staging, vanguard, assault, headquarters)
     *  F25 replaces this with incursion manager data (keep the role check and the shape)
     */
    if (!(call.client->GetAccountRole() & (Acct::Role::GML | Acct::Role::GMH | Acct::Role::ADMIN)))
        throw CustomError("Access denied.");

    return new PyList();
}

//05:52:07 L MapService::Handle_GetIncursionGlobalReport(): size= 0
PyResult MapService::GetIncursionGlobalReport(PyCallArgs &call) {
  /**
            report = sm.RemoteSvc('map').GetIncursionGlobalReport()
            rewardGroupIDs = [ r.rewardGroupID for r in report ]
            delayedRewards = sm.GetService('incursion').GetDelayedRewardsByGroupIDs(rewardGroupIDs)
            scrolllist = []
            factionsToPrime = set()
            for data in report:
                data.jumps = GetJumps(data.stagingSolarSystemID)
                data.influenceData = util.KeyVal(influence=data.influence, lastUpdated=data.lastUpdated, graceTime=data.graceTime, decayRate=data.decayRate)
                ssitem = map.GetItem(data.stagingSolarSystemID)
                data.stagingSolarSystemName = ssitem.itemName
                data.security = map.GetSecurityStatus(data.stagingSolarSystemID)
                data.constellationID = ssitem.locationID
                data.constellationName = map.GetItem(ssitem.locationID).itemName
                data.factionID = ssitem.factionID or starmap.GetAllianceSolarSystems().get(data.stagingSolarSystemID, None)
                factionsToPrime.add(data.factionID)
                rewards = delayedRewards.get(data.rewardGroupID, None)
                data.loyaltyPoints = rewards[0].rewardQuantity if rewards else 0
                scrolllist.append(listentry.Get('GlobalIncursionReportEntry', data))
*/
  /*
      [PySubStream 1128 bytes]
        [PyObjectData Name: objectCaching.CachedMethodCallResult]
          [PyTuple 3 items]
            [PyDict 1 kvp]
              [PyString "versionCheck"]
              [PyTuple 3 items]
                [PyNone]
                [PyNone]
                [PyString "15 minutes"]
            [PySubStream 1027 bytes]
              [PyList 7 items]
                [PyObjectData Name: util.KeyVal]
                  [PyDict 9 kvp]
                    [PyString "graceTime"]
                    [PyFloat 30]
                    [PyString "decayRate"]
                    [PyFloat 0.00999999977648258]
                    [PyString "influence"]
                    [PyFloat 0.0145000005140901]
                    [PyString "lastUpdated"]
                    [PyIntegerVar 129492976800000000]
                    [PyString "state"]
                    [PyInt 1]
                    [PyString "hasBoss"]
                    [PyInt 0]
                    [PyString "stagingSolarSystemID"]
                    [PyInt 30004323]
                    [PyString "rewardGroupID"]
                    [PyInt 192]
                    [PyString "taleID"]
                    [PyInt 192]
            [PyList 2 items]
              [PyIntegerVar 129493861959830226]
              [PyInt -950263469]
              */
    // list of KeyVals (above).  no incursions yet:  F25 replaces this with incursion manager data
    return new PyList();
}

//   factional warfare shit
//https://wiki.eveonline.com/en/wiki/Victory_Points_and_Command_Bunker
PyResult MapService::GetVictoryPoints(PyCallArgs &call)
{/**  {factionID: {'attacking'|'defending': [(solarSystemID, threshold, current), ...]}}
   *  faction warfare is out of scope:  the four empire factions with no contested systems
   */
    PyDict* result = new PyDict();
    for (uint32 factionID = 500001; factionID <= 500004; ++factionID) {
        PyDict* modes = new PyDict();
            modes->SetItemString("attacking", new PyList());
            modes->SetItemString("defending", new PyList());
        result->SetItem(new PyInt(factionID), modes);
    }
    return result;
}


PyResult MapService::GetStuckSystems(PyCallArgs &call)
{
    // no caller in the Crucible client (old call).  an empty list
    return new PyList();
}

