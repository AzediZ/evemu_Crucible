
/**
 * @name DungeonDB.cpp
 *   Dungeon DataBase methods
 *   Copyright 2018  Alasiya-EVEmu Team
 *
 * @Author:    Allan
 * @date:      13 December 2018
 *
 */


/* Dungeon Logging
 * DUNG__ERROR
 * DUNG__WARNING
 * DUNG__INFO
 * DUNG__MESSAGE
 * DUNG__TRACE
 * DUNG__CALL
 * DUNG__CALL_DUMP
 * DUNG__RSP_DUMP
 * DUNG__DB_ERROR
 * DUNG__DB_WARNING
 * DUNG__DB_INFO
 * DUNG__DB_MESSAGE
 */

#include "eve-server.h"

#include "DungeonDB.h"


void DungeonDB::GetTemplates(DBQueryResult& res, int32 userID)
{
    if (!sDatabase.RunQuery(res, "SELECT dunTemplateID as templateID, dunTemplateName as templateName, "
    "dunTemplateDescription as description, "
    "%u as userID "
    "FROM dunTemplates", userID))
        _log(DATABASE__ERROR, "Error in GetDunTemplates query: %s", res.error.c_str());
}

void DungeonDB::GetFactions(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT factionID FROM facFactions"))
    _log(DATABASE__ERROR, "Error in GetFactions query: %s", res.error.c_str());
}

void DungeonDB::GetRooms(uint32 dungeonID, DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT roomID, roomName "
    "FROM dunRooms "
    "WHERE dungeonID=%u", dungeonID))
    _log(DATABASE__ERROR, "Error in GetFactions query: %s", res.error.c_str());
}

void DungeonDB::GetArchetypes(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT archetypeID, archetypeName FROM dunArchetypes"))
    _log(DATABASE__ERROR, "Error in GetFactions query: %s", res.error.c_str());
}

void DungeonDB::GetAllDungeonData(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT dunDungeons.dungeonID, "
    "dungeonName, dungeonStatus, factionID, archetypeID, "
    "dunRooms.roomID, dunRooms.roomName, objectID, typeID, groupID, "
    "x, y, z, yaw, pitch, roll, radius "
	"FROM dunDungeons "
	"INNER JOIN dunRooms ON dunDungeons.dungeonID = dunRooms.dungeonID "
	"LEFT JOIN dunRoomObjects ON dunRooms.roomID = dunRoomObjects.roomID"))
    _log(DATABASE__ERROR, "Error in GetAllDungeonData query: %s", res.error.c_str());
}

void DungeonDB::GetAllDungeonDataByDungeonID(DBQueryResult& res, uint32 dungeonID)
{
    if (!sDatabase.RunQuery(res, "SELECT dunDungeons.dungeonID, "
    "dungeonName, dungeonStatus, factionID, archetypeID, "
    "dunRooms.roomID, dunRooms.roomName, objectID, typeID, groupID, "
    "x, y, z, yaw, pitch, roll, radius "
	"FROM dunDungeons "
	"INNER JOIN dunRooms ON dunDungeons.dungeonID = dunRooms.dungeonID "
	"LEFT JOIN dunRoomObjects ON dunRooms.roomID = dunRoomObjects.roomID "
    "WHERE dunDungeons.dungeonID = %u", dungeonID))
    _log(DATABASE__ERROR, "Error in GetAllDungeonData query: %s", res.error.c_str());
}

void DungeonDB::GetDungeons(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT dungeonID, dungeonName, dungeonStatus, NULL as dungeonNameID, factionID, archetypeID "
    "FROM dunDungeons"))
    _log(DATABASE__ERROR, "Error in GetFactions query: %s", res.error.c_str());
}

void DungeonDB::GetDungeons(uint32 dungeonID, DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT dungeonID, dungeonName, dungeonStatus, NULL as dungeonNameID, factionID, archetypeID "
    "FROM dunDungeons "
    "WHERE dungeonID=%u", dungeonID))
    _log(DATABASE__ERROR, "Error in GetFactions query: %s", res.error.c_str());
}

void DungeonDB::GetDungeons(uint32 archetypeID, uint32 factionID, DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT dungeonID, dungeonName, dungeonStatus, NULL as dungeonNameID, factionID, archetypeID "
    "FROM dunDungeons "
    "WHERE archetypeID=%u AND factionID=%u", archetypeID, factionID))
    _log(DATABASE__ERROR, "Error in GetFactions query: %s", res.error.c_str());
}

void DungeonDB::GetGroups(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT groupID, groupName FROM dunGroups"))
    _log(DATABASE__ERROR, "Error in GetFactions query: %s", res.error.c_str());
}

PyRep* DungeonDB::GetRoomGroups(uint32 roomID)
{
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
            "SELECT "
                " groupID AS selectionGroupID,"
                " groupName AS selectionGroupName,"
                " dunGroups.yaw,"
                " dunGroups.pitch,"
                " dunGroups.roll"
            " FROM dunRoomObjects "
            " INNER JOIN dunGroups USING (groupID)"
            " WHERE roomID = %u"
            " GROUP BY groupID"))
    {
        _log(DATABASE__ERROR, "Error in GetRoomGroups query: %s", res.error.c_str());
    }

    return DBResultToCRowset(res);
}

uint32 DungeonDB::GetFirstGroupForRoom(uint32 roomID)
{
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
            "SELECT "
                " groupID"
            " FROM dunRoomObjects "
            " WHERE roomID = %u"
            " GROUP BY groupID"
            " ORDER BY groupID ASC"
            " LIMIT 1"))
    {
        _log(DATABASE__ERROR, "Error in GetFirstGroupForRoom query: %s", res.error.c_str());
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return 0;

    return row.GetUInt(0);
}

void DungeonDB::GetRoomObjects(uint32 roomID, std::vector< Dungeon::RoomObject >& into)
{
    DBQueryResult res;

    if (!sDatabase.RunQuery(res, "SELECT objectID, roomID, typeID, groupID, x, y, z, yaw, pitch, roll, radius "
    "FROM dunRoomObjects "
    "WHERE roomID=%u", roomID))
    _log(DATABASE__ERROR, "Error in GetRoomObjects query: %s", res.error.c_str());

    _log(DATABASE__RESULTS, "GetRoomObjects returned %lu items", res.GetRowCount());
    DBResultRow row;

    while(res.GetRow(row)) {
        Dungeon::RoomObject entry = Dungeon::RoomObject();
            entry.objectID = row.GetInt(0);
            entry.roomID = row.GetInt(1);
            entry.typeID = row.GetInt(2);
            entry.groupID = row.GetInt(3);
            entry.x = row.GetInt(4);
            entry.y = row.GetInt(5);
            entry.z = row.GetInt(6);
            entry.pitch = row.GetDouble(7);
            entry.roll = row.GetDouble(8);
            entry.yaw = row.GetDouble(9);
            entry.radius = row.GetDouble(10);
        into.push_back(entry);
    }
}

void DungeonDB::GetTemplateObjects(uint32 templateID, std::vector< Dungeon::RoomObject >& into)
{
    DBQueryResult res;

    if (!sDatabase.RunQuery(res, "SELECT objectID, roomID, typeID, groupID, x, y, z, yaw, pitch, roll, radius "
    "FROM dunTemplates INNER JOIN dunRoomObjects ON dunTemplates.dunRoomID = dunRoomObjects.roomID "
    "WHERE dunTemplateID=%u", templateID))
    _log(DATABASE__ERROR, "Error in GetTemplateObjects query: %s", res.error.c_str());

    _log(DATABASE__RESULTS, "GetTemplateObjects returned %lu items", res.GetRowCount());
    DBResultRow row;

    while(res.GetRow(row)) {
        Dungeon::RoomObject entry = Dungeon::RoomObject();
            entry.objectID = row.GetInt(0);
            entry.roomID = row.GetInt(1);
            entry.typeID = row.GetInt(2);
            entry.groupID = row.GetInt(3);
            entry.x = row.GetInt(4);
            entry.y = row.GetInt(5);
            entry.z = row.GetInt(6);
            entry.pitch = row.GetDouble(7);
            entry.roll = row.GetDouble(8);
            entry.yaw = row.GetDouble(9);
            entry.radius = row.GetDouble(10);
        into.push_back(entry);
    }
}

void DungeonDB::EditObjectXYZ(uint32 objectID, double x, double y, double z)
{
    DBerror err;

    if (!sDatabase.RunQuery(err, "UPDATE dunRoomObjects SET x = %f, y = %f, z = %f WHERE objectID = %u", x, y, z, objectID))
        _log(DATABASE__ERROR, "Cannot update object's %u coordinates to %f, %f, %f", objectID, x, y, z);
}

void DungeonDB::EditObjectRadius(uint32 objectID, double radius)
{
    DBerror err;

    if (!sDatabase.RunQuery(err, "UPDATE dunRoomObjects SET radius = %f WHERE objectID = %u", radius, objectID))
        _log(DATABASE__ERROR, "Cannot update object's %u radius to %f", objectID, radius);
}

void DungeonDB::EditObjectYawPitchRoll(uint32 objectID, double yaw, double pitch, double roll)
{
    DBerror err;

    if (!sDatabase.RunQuery(err, "UPDATE dunRoomObjects SET yaw = %f, pitch = %f, roll = %f WHERE objectID = %u", yaw, pitch, roll, objectID))
        _log(DATABASE__ERROR, "Cannot update object's %u rotation to %f, %f, %f", objectID, yaw, pitch, roll);
}

uint32 DungeonDB::CreateObject(uint32 roomID, uint32 typeID, uint32 groupID, double x, double y, double z, double yaw, double pitch, double roll, double radius)
{
    DBerror err;

    uint32 objectID;
    if (!sDatabase.RunQueryLID(err, objectID, "INSERT INTO dunRoomObjects (roomID, typeID, groupID, x, y, z, yaw, pitch, roll, radius) VALUES (%u, %u, %u, %f, %f, %f, %f, %f, %f, %f)", roomID, typeID, groupID, x, y, z, yaw, pitch, roll, radius))
        _log(DATABASE__ERROR, "Cannot insert object into room %u", roomID);

    return objectID;
}

void DungeonDB::DeleteObject(uint32 objectID)
{
    DBerror err;

    if (!sDatabase.RunQuery(err, "DELETE FROM dunRoomObjects WHERE objectID = %u", objectID))
        _log(DATABASE__ERROR, "Cannot delete object %u", objectID);
}

void DungeonDB::EditTemplate(uint32 templateID, std::string templateName, std::string templateDescription)
{
    DBerror err;

    std::string templateNameEscaped;
    sDatabase.DoEscapeString(templateNameEscaped, templateName);

    std::string templateDescriptionEscaped;
    sDatabase.DoEscapeString(templateDescriptionEscaped, templateDescription);

    if (!sDatabase.RunQuery(err, "UPDATE dunTemplates SET dunTemplateName = '%s', dunTemplateDescription = '%s' WHERE dunTemplateID = %u", templateNameEscaped.c_str(), templateDescriptionEscaped.c_str(), templateID))
        _log(DATABASE__ERROR, "Cannot update template %u", templateID);
}

void DungeonDB::DeleteTemplate(uint32 templateID)
{
    DBerror err;

    if (!sDatabase.RunQuery(err, "DELETE FROM dunTemplates WHERE dunTemplateID = %u", templateID))
        _log(DATABASE__ERROR, "Cannot delete template %u", templateID);
}

uint32 DungeonDB::CreateTemplate(std::string templateName, std::string templateDescription, uint32 roomID)
{
    DBerror err;

    std::string templateNameEscaped;
    sDatabase.DoEscapeString(templateNameEscaped, templateName);

    std::string templateDescriptionEscaped;
    sDatabase.DoEscapeString(templateDescriptionEscaped, templateDescription);

    uint32 templateID;
    if (!sDatabase.RunQueryLID(err, templateID, "INSERT INTO dunTemplates (dunTemplateName, dunTemplateDescription, dunRoomID) VALUES ('%s', '%s', %u)", templateNameEscaped.c_str(), templateDescriptionEscaped.c_str(), roomID))
        _log(DATABASE__ERROR, "Cannot insert template into room %u", roomID);

    return templateID;
}

/* dungeon engine static data.  rooms are joined to filter by dungeon */
static std::string DungeonFilter(uint32 dungeonID, const char* column)
{
    if (dungeonID == 0)
        return "";
    return std::string(" WHERE ") + column + " = " + std::to_string(dungeonID);
}

void DungeonDB::GetDungeonRows(DBQueryResult& res, uint32 dungeonID/*0*/)
{
    if (!sDatabase.RunQuery(res, "SELECT dungeonID, dungeonName, dungeonStatus, factionID, archetypeID,"
        " COALESCE(dungeonNameID, 0), difficulty, minSecurity, maxSecurity, spawnWeight, lifetimeHours"
        " FROM dunDungeons%s", DungeonFilter(dungeonID, "dungeonID").c_str()))
        _log(DATABASE__ERROR, "Error in GetDungeonRows query: %s", res.error.c_str());
}

void DungeonDB::GetRoomRows(DBQueryResult& res, uint32 dungeonID/*0*/)
{
    if (!sDatabase.RunQuery(res, "SELECT roomID, roomName, dungeonID, arrivalX, arrivalY, arrivalZ"
        " FROM dunRooms%s ORDER BY dungeonID, roomID", DungeonFilter(dungeonID, "dungeonID").c_str()))
        _log(DATABASE__ERROR, "Error in GetRoomRows query: %s", res.error.c_str());
}

void DungeonDB::GetObjectRows(DBQueryResult& res, uint32 dungeonID/*0*/)
{
    if (!sDatabase.RunQuery(res, "SELECT o.objectID, o.roomID, o.typeID, o.groupID, o.x, o.y, o.z, o.yaw, o.pitch, o.roll, o.radius, r.dungeonID"
        " FROM dunRoomObjects AS o INNER JOIN dunRooms AS r ON r.roomID = o.roomID%s ORDER BY o.objectID",
        DungeonFilter(dungeonID, "r.dungeonID").c_str()))
        _log(DATABASE__ERROR, "Error in GetObjectRows query: %s", res.error.c_str());
}

void DungeonDB::GetSpawnGroups(DBQueryResult& res, uint32 dungeonID/*0*/)
{
    if (!sDatabase.RunQuery(res, "SELECT g.spawnGroupID, g.roomID, g.waveNumber, g.spawnMode, g.npcClassType, g.npcClassSub,"
        " g.factionID, g.x, g.y, g.z, g.spread, g.warpIn, g.isOverseer, g.lootGroupID, r.dungeonID"
        " FROM dunSpawnGroups AS g INNER JOIN dunRooms AS r ON r.roomID = g.roomID%s ORDER BY g.spawnGroupID",
        DungeonFilter(dungeonID, "r.dungeonID").c_str()))
        _log(DATABASE__ERROR, "Error in GetSpawnGroups query: %s", res.error.c_str());
}

void DungeonDB::GetSpawnGroupTypes(DBQueryResult& res, uint32 dungeonID/*0*/)
{
    if (!sDatabase.RunQuery(res, "SELECT t.spawnGroupID, t.typeID, t.quantity"
        " FROM dunSpawnGroupTypes AS t INNER JOIN dunSpawnGroups AS g ON g.spawnGroupID = t.spawnGroupID"
        " INNER JOIN dunRooms AS r ON r.roomID = g.roomID%s", DungeonFilter(dungeonID, "r.dungeonID").c_str()))
        _log(DATABASE__ERROR, "Error in GetSpawnGroupTypes query: %s", res.error.c_str());
}

void DungeonDB::GetTriggers(DBQueryResult& res, uint32 dungeonID/*0*/)
{
    if (!sDatabase.RunQuery(res, "SELECT t.triggerID, t.roomID, t.triggerType, t.spawnGroupID, t.objectID, t.value, t.counterID, t.repeatable"
        " FROM dunTriggers AS t INNER JOIN dunRooms AS r ON r.roomID = t.roomID%s ORDER BY t.triggerID",
        DungeonFilter(dungeonID, "r.dungeonID").c_str()))
        _log(DATABASE__ERROR, "Error in GetTriggers query: %s", res.error.c_str());
}

void DungeonDB::GetTriggerEvents(DBQueryResult& res, uint32 dungeonID/*0*/)
{
    if (!sDatabase.RunQuery(res, "SELECT e.eventID, e.triggerID, e.sequence, e.eventType, e.delaySeconds, e.spawnGroupID, e.objectID,"
        " e.messageType, e.messageID, e.messageText, e.typeID, e.quantity, e.value, e.counterID"
        " FROM dunTriggerEvents AS e INNER JOIN dunTriggers AS t ON t.triggerID = e.triggerID"
        " INNER JOIN dunRooms AS r ON r.roomID = t.roomID%s ORDER BY e.triggerID, e.sequence, e.eventID",
        DungeonFilter(dungeonID, "r.dungeonID").c_str()))
        _log(DATABASE__ERROR, "Error in GetTriggerEvents query: %s", res.error.c_str());
}

void DungeonDB::GetGates(DBQueryResult& res, uint32 dungeonID/*0*/)
{
    if (!sDatabase.RunQuery(res, "SELECT g.objectID, g.destRoomID, g.locked, g.requireClear, g.keyTypeID, g.keyQuantity, g.keyConsumed,"
        " g.skillTypeID, g.skillLevel, g.allowedGroups, g.deniedGroups, g.musicUrl"
        " FROM dunGates AS g INNER JOIN dunRoomObjects AS o ON o.objectID = g.objectID"
        " INNER JOIN dunRooms AS r ON r.roomID = o.roomID%s", DungeonFilter(dungeonID, "r.dungeonID").c_str()))
        _log(DATABASE__ERROR, "Error in GetGates query: %s", res.error.c_str());
}

void DungeonDB::GetLoot(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT lootGroupID, typeID, minQty, maxQty, chance FROM dunLoot"))
        _log(DATABASE__ERROR, "Error in GetLoot query: %s", res.error.c_str());
}

void DungeonDB::GetEscalations(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT escalationID, srcDungeonID, srcArchetypeID, srcMinDifficulty, destDungeonID, destArchetypeID,"
        " destDifficultyDelta, chance, minJumps, maxJumps, lowerSecOnly, expiryHours, journalMessageID"
        " FROM dunEscalations ORDER BY escalationID"))
        _log(DATABASE__ERROR, "Error in GetEscalations query: %s", res.error.c_str());
}

/* granted escalations (chrEscalations) */
static const char* EscalationColumns = "instanceID, characterID, escalationID, srcDungeonID, destDungeonID, factionID, solarSystemID,"
    " x, y, z, creationTime, expiryTime, journalMessageID, state";

static void FillEscalationRow(DBResultRow& row, EscalationRow& into)
{
    into.instanceID = row.GetUInt(0);
    into.characterID = row.GetUInt(1);
    into.escalationID = row.GetUInt(2);
    into.srcDungeonID = row.GetUInt(3);
    into.destDungeonID = row.GetUInt(4);
    into.factionID = row.GetUInt(5);
    into.solarSystemID = row.GetUInt(6);
    into.position = GPoint(row.GetDouble(7), row.GetDouble(8), row.GetDouble(9));
    into.creationTime = row.GetInt64(10);
    into.expiryTime = row.GetInt64(11);
    into.journalMessageID = row.GetUInt(12);
    into.state = row.GetUInt(13);
}

uint32 DungeonDB::InsertEscalation(const EscalationRow& row)
{
    DBerror err;
    uint32 instanceID(0);
    if (!sDatabase.RunQueryLID(err, instanceID,
        "INSERT INTO chrEscalations (characterID, escalationID, srcDungeonID, destDungeonID, factionID, solarSystemID,"
        " x, y, z, creationTime, expiryTime, journalMessageID, state)"
        " VALUES (%u, %u, %u, %u, %u, %u, %f, %f, %f, %lli, %lli, %u, 0)",
        row.characterID, row.escalationID, row.srcDungeonID, row.destDungeonID, row.factionID, row.solarSystemID,
        row.position.x, row.position.y, row.position.z, row.creationTime, row.expiryTime, row.journalMessageID))
    {
        _log(DATABASE__ERROR, "InsertEscalation - unable to save the escalation of %u: %s", row.characterID, err.c_str());
        return 0;
    }
    return instanceID;
}

bool DungeonDB::GetEscalation(uint32 instanceID, EscalationRow& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM chrEscalations WHERE instanceID = %u", EscalationColumns, instanceID)) {
        _log(DATABASE__ERROR, "Error in GetEscalation query: %s", res.error.c_str());
        return false;
    }
    DBResultRow row;
    if (!res.GetRow(row))
        return false;
    FillEscalationRow(row, into);
    return true;
}

void DungeonDB::GetEscalationsForSystem(uint32 systemID, int64 now, std::vector<EscalationRow>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM chrEscalations WHERE solarSystemID = %u AND state < 2 AND expiryTime > %lli",
        EscalationColumns, systemID, now))
    {
        _log(DATABASE__ERROR, "Error in GetEscalationsForSystem query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        EscalationRow esc = EscalationRow();
        FillEscalationRow(row, esc);
        into.push_back(esc);
    }
}

void DungeonDB::GetEscalationsForChar(uint32 charID, std::vector<EscalationRow>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM chrEscalations WHERE characterID = %u AND state < 3 ORDER BY creationTime",
        EscalationColumns, charID))
    {
        _log(DATABASE__ERROR, "Error in GetEscalationsForChar query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row)) {
        EscalationRow esc = EscalationRow();
        FillEscalationRow(row, esc);
        into.push_back(esc);
    }
}

void DungeonDB::SetEscalationState(uint32 instanceID, uint8 state, int64 expiryTime/*0*/)
{
    DBerror err;
    if (expiryTime != 0) {
        sDatabase.RunQuery(err, "UPDATE chrEscalations SET state = %u, expiryTime = %lli WHERE instanceID = %u AND state < %u",
                state, expiryTime, instanceID, state);
    } else {
        sDatabase.RunQuery(err, "UPDATE chrEscalations SET state = %u WHERE instanceID = %u AND state < %u", state, instanceID, state);
    }
}
