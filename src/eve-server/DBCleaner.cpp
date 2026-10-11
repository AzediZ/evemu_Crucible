
 /**
  * @name DBCleaner.cpp
  *   Database cleaner module
  * @Author: James
  * @date:   10 October 2021
  */

#include "DBCleaner.h"
#include "EVEServerConfig.h"
#include "database/EVEDBUtils.h"
#include "inventory/InventoryDB.h"

// This module exists to clean up the database from stuff which could persist in the DB due to crashes.

void DBCleaner::Initialize() {
    sLog.Blue("        DBCleaner", "Database Cleaner Initialized.");

    // Add new cleaning jobs below
    CleanEntity(EVEDB::invTypes::CynosuralFieldI); //Clean cynosural fields (saved by older versions; fields are temp items now)
    CleanEntity(EVEDB::invTypes::CovertCynosuralFieldI);
    CleanGroupFromSpace(EVEDB::invGroups::Scanner_Probe); //Clean scanner probes floating in space
    CleanGroupFromSpace(EVEDB::invGroups::Survey_Probe); //Clean survey probes floating in space
    CleanOrphanedWormholes(); //Clean up orphaned wormhole entities
    CleanDungeonEntities();
    PurgeTrashedItems();

    // Don't add anything below this line
    sLog.Blue("        DBCleaner", "Cleaning complete.");
}

void DBCleaner::CleanEntity(uint32 type) {
    DBerror err;
    // Delete entity attributes associated with removed entities
    sDatabase.RunQuery(err, "DELETE FROM entity_attributes WHERE itemID IN (SELECT itemID FROM entity WHERE typeID = %u);", type);
    // Delete entities themselves
    sDatabase.RunQuery(err, "DELETE FROM entity WHERE typeID = %u", type); 
}

void DBCleaner::CleanGroupFromSpace(uint32 groupID) {
    DBerror err;
    // Delete entity attributes associated with removed entities
    sDatabase.RunQuery(err, "DELETE FROM entity_attributes WHERE itemID IN "
    "(SELECT itemID FROM entity "
    "INNER JOIN invTypes USING (typeID) "
    "WHERE groupID = %u)", groupID);
    // Delete entities themselves
    sDatabase.RunQuery(err, "DELETE entity FROM entity "
    "INNER JOIN invTypes USING (typeID) "
    "WHERE locationID >= 30000000 AND locationID <= 32000000 AND groupID = %u", groupID); 
}

void DBCleaner::CleanOrphanedWormholes() {
    DBerror err;
    // Delete wormholes which don't have any reference in the sysSignatures table
    sDatabase.RunQuery(err, "DELETE FROM entity_attributes WHERE itemID IN "
    "(SELECT itemID FROM entity "
    "INNER JOIN invTypes USING (typeID) "
    "WHERE itemID NOT IN (SELECT sigItemID FROM sysSignatures) "
    "AND groupID = %u)", EVEDB::invGroups::Wormhole);

    sDatabase.RunQuery(err, "DELETE entity FROM entity "
    "INNER JOIN invTypes USING (typeID) "
    "WHERE itemID NOT IN (SELECT sigItemID FROM sysSignatures) "
    "AND locationID >= 30000000 AND locationID <= 32000000 AND groupID = %u", EVEDB::invGroups::Wormhole); 
}

void DBCleaner::PurgeTrashedItems() {
    // trashed items wait in the junkyard for a GM restore (/untrash) this long
    InventoryDB::PurgeTrash(GetFileTimeNow() - (Win32Time_Day * sConfig.world.TrashRetentionDays));

    DBerror err;
    // container passwords and corp item locks of items that are gone
    sDatabase.RunQuery(err, "DELETE FROM invContainerSecurity WHERE itemID NOT IN (SELECT itemID FROM entity)");
    sDatabase.RunQuery(err, "DELETE FROM crpLockedItems WHERE itemID NOT IN (SELECT itemID FROM entity)");
    // audit logs of containers that are gone, after a year
    sDatabase.RunQuery(err, "DELETE FROM invAuditLog WHERE logDate < %lli AND containerID NOT IN (SELECT itemID FROM entity)",
            GetFileTimeNow() - (Win32Time_Day * 365));
}

void DBCleaner::CleanDungeonEntities() {
    DBerror err;
    // Delete entities from live dungeons which should not persist across server restarts
    sDatabase.RunQuery(err, "DELETE FROM entity_attributes WHERE itemID IN "
    "(SELECT itemID FROM entity "
    "WHERE customInfo LIKE '%%livedungeon%%')");

    sDatabase.RunQuery(err, "DELETE FROM entity WHERE "
    "customInfo LIKE '%%livedungeon%%'");

    // dungeon sites (everything but wormholes) are made again when their system boots
    sDatabase.RunQuery(err, "DELETE FROM sysSignatures WHERE dungeonType <> 6");
}