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
    Author:        James
*/

#include "system/sov/SovereigntyDB.h"

void SovereigntyDB::GetSovereigntyData(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
                            "SELECT mapSystemSovInfo.solarSystemID, mapSolarSystems.constellationID, mapSolarSystems.regionID, corporationID, "
                            " allianceID, claimStructureID, claimTime, "
                            " hubID, contested, claimID, beaconID, jammerID"
                            " FROM mapSystemSovInfo "
                            " INNER JOIN mapSolarSystems ON mapSolarSystems.solarSystemID=mapSystemSovInfo.solarSystemID"))
    {
        codelog(SOV__ERROR, "Error in query: %s", res.error.c_str());
    }
}

void SovereigntyDB::GetSovereigntyDataForSystem(DBQueryResult& res, uint32 systemID)
{
    if (!sDatabase.RunQuery(res,
                            "SELECT mapSystemSovInfo.solarSystemID, mapSolarSystems.constellationID, mapSolarSystems.regionID, corporationID, "
                            " allianceID, claimStructureID, claimTime, "
                            " hubID, contested, claimID, beaconID, jammerID"
                            " FROM mapSystemSovInfo "
                            " INNER JOIN mapSolarSystems ON mapSolarSystems.solarSystemID=mapSystemSovInfo.solarSystemID"
                            " WHERE mapSystemSovInfo.solarSystemID=%u ", systemID))
    {
        codelog(SOV__ERROR, "Error in query: %s", res.error.c_str());
    }
}

void SovereigntyDB::AddSovereigntyData(SovereigntyData data, uint32& claimID)
{
    DBerror err;
    if (!sDatabase.RunQueryLID(err, claimID,
                            "INSERT into mapSystemSovInfo (solarSystemID, corporationID, "
                            " allianceID, claimStructureID, claimTime, hubID, contested) "
                            " VALUES (%u, %u, %u, %u, %f, %u, %u)", data.solarSystemID, 
                            data.corporationID, data.allianceID, data.claimStructureID, 
                            GetFileTimeNow(), data.hubID, data.contested))
    {
        codelog(SOV__ERROR, "Error in adding new claim: %s", err.c_str());
    }
}

void SovereigntyDB::RemoveSovereigntyData(uint32 systemID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "DELETE from mapSystemSovInfo WHERE solarSystemID=%u", systemID))
    {
        codelog(SOV__ERROR, "Error in removing claim: %s", err.c_str());
    }
}

void SovereigntyDB::SetContested(uint32 systemID, uint8 sbuGates)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "UPDATE mapSystemSovInfo SET contested=%u WHERE solarSystemID=%u", (uint32)sbuGates, systemID))
    {
        codelog(SOV__ERROR, "Error in changing contested state: %s", err.c_str());
    }
}

void SovereigntyDB::SetHubID(uint32 systemID, uint32 hubID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "UPDATE mapSystemSovInfo SET hubID=%u WHERE solarSystemID=%u", hubID, systemID))
    {
        codelog(SOV__ERROR, "Error in changing IHub ID: %s", err.c_str());
    }
}

void SovereigntyDB::SetJammerID(uint32 systemID, uint32 jammerID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "UPDATE mapSystemSovInfo SET jammerID=%u WHERE solarSystemID=%u", jammerID, systemID))
    {
        codelog(SOV__ERROR, "Error in changing Jammer ID: %s", err.c_str());
    }
}

void SovereigntyDB::SetBeaconID(uint32 systemID, uint32 beaconID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
                            "UPDATE mapSystemSovInfo SET beaconID=%u WHERE solarSystemID=%u", beaconID, systemID))
    {
        codelog(SOV__ERROR, "Error in changing Beacon ID: %s", err.c_str());
    }
}

void SovereigntyDB::LogSovChange(uint32 systemID, uint32 stationID, uint32 ownerID, uint32 oldOwnerID)
{
    std::string station(stationID > 0 ? std::to_string(stationID) : "NULL");
    std::string owner(ownerID > 0 ? std::to_string(ownerID) : "NULL");
    std::string oldOwner(oldOwnerID > 0 ? std::to_string(oldOwnerID) : "NULL");
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "INSERT INTO sovChangeLog (solarSystemID, stationID, ownerID, oldOwnerID, changeTime) VALUES (%u, %s, %s, %s, %lli)",
            systemID, station.c_str(), owner.c_str(), oldOwner.c_str(), (int64)GetFileTimeNow()))
    {
        codelog(SOV__ERROR, "Error logging sov change: %s", err.c_str());
    }
}

void SovereigntyDB::GetRecentSovChanges(DBQueryResult& res, int64 since)
{
    if (!sDatabase.RunQuery(res,
            "SELECT solarSystemID, stationID, ownerID, oldOwnerID, changeTime FROM sovChangeLog WHERE changeTime >= %lli ORDER BY changeTime",
            since))
    {
        codelog(SOV__ERROR, "Error in GetRecentSovChanges query: %s", res.error.c_str());
    }
}

void SovereigntyDB::LoadDevIndices(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
            "SELECT solarSystemID, militaryPoints, industrialPoints, militaryIncreasing, industrialIncreasing, lastDecayTime"
            " FROM sovDevIndices"))
    {
        codelog(SOV__ERROR, "Error in LoadDevIndices query: %s", res.error.c_str());
    }
}

void SovereigntyDB::SaveDevIndex(const DevIndexData& data)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "REPLACE INTO sovDevIndices (solarSystemID, militaryPoints, industrialPoints, militaryIncreasing, industrialIncreasing, lastDecayTime)"
            " VALUES (%u, %lli, %lli, %u, %u, %lli)",
            data.systemID, data.military, data.industrial, (data.militaryIncreasing ? 1 : 0), (data.industrialIncreasing ? 1 : 0), data.lastDecay))
    {
        codelog(SOV__ERROR, "Error saving dev index for %u: %s", data.systemID, err.c_str());
    }
}

void SovereigntyDB::GetActiveUpgrades(DBQueryResult& res, const std::string& hubIDs)
{
    if (!sDatabase.RunQuery(res,
            "SELECT locationID, typeID FROM entity WHERE locationID IN (%s) AND flag = %u",
            hubIDs.c_str(), (uint32)flagStructureActive))
    {
        codelog(SOV__ERROR, "Error in GetActiveUpgrades query: %s", res.error.c_str());
    }
}
