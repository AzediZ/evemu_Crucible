
/**
 * @name DungeonDB.h
 *   Dungeon DataBase methods
 *   Copyright 2018  Alasiya-EVEmu Team
 *
 * @Author:    Allan
 * @date:      13 December 2018
 *
 */

#ifndef _DUNGEON_DATABASE_H
#define _DUNGEON_DATABASE_H

// a granted escalation (chrEscalations)
struct EscalationRow {
    uint32 instanceID;
    uint32 characterID;
    uint32 escalationID;
    uint32 srcDungeonID;
    uint32 destDungeonID;
    uint32 factionID;
    uint32 solarSystemID;
    GPoint position;
    int64 creationTime;
    int64 expiryTime;
    uint32 journalMessageID;
    uint8 state;            // 0 granted, 1 entered, 2 completed, 3 removed by the player
};

class DungeonDB {
public:
    DungeonDB()                                         { /* do nothing here */ }
    ~DungeonDB()                                        { /* do nothing here */ }

    static void GetTemplates(DBQueryResult& res, int32 userID);
    static void GetFactions(DBQueryResult& res);
    static void GetRooms(uint32 dungeonID, DBQueryResult& res);
    static void GetArchetypes(DBQueryResult& res);
    static void GetDungeons(DBQueryResult& res);
    static void GetDungeons(uint32 dungeonID, DBQueryResult& res);
    static void GetDungeons(uint32 archetypeID, uint32 factionID, DBQueryResult& res);
    static void GetAllDungeonData(DBQueryResult& res);
    static void GetAllDungeonDataByDungeonID(DBQueryResult& res, uint32 dungeonID);
    static void GetGroups(DBQueryResult& res);
    static void GetRoomObjects(uint32 roomID, std::vector< Dungeon::RoomObject >& into);
    static void GetTemplateObjects(uint32 templateID, std::vector< Dungeon::RoomObject >& into);
    static PyRep* GetRoomGroups(uint32 roomID);
    static void EditObjectXYZ(uint32 objectID, double x, double y, double z);
    static void EditObjectRadius(uint32 objectID, double radius);
    static void EditObjectYawPitchRoll(uint32 objectID, double yaw, double pitch, double roll);
    static uint32 CreateObject(uint32 roomID, uint32 typeID, uint32 groupID, double x, double y, double z, double yaw, double pitch, double roll, double radius);
    static uint32 GetFirstGroupForRoom(uint32 roomID);
    static void DeleteObject(uint32 objectID);
    static void EditTemplate(uint32 templateID, std::string templateName, std::string templateDescription);
    static void DeleteTemplate(uint32 templateID);
    static uint32 CreateTemplate(std::string templateName, std::string templateDescription, uint32 roomID);

    // dungeon engine static data, for DungeonDataMgr.  dungeonID 0 = all dungeons
    static void GetDungeonRows(DBQueryResult& res, uint32 dungeonID = 0);
    static void GetRoomRows(DBQueryResult& res, uint32 dungeonID = 0);
    static void GetObjectRows(DBQueryResult& res, uint32 dungeonID = 0);
    static void GetSpawnGroups(DBQueryResult& res, uint32 dungeonID = 0);
    static void GetSpawnGroupTypes(DBQueryResult& res, uint32 dungeonID = 0);
    static void GetTriggers(DBQueryResult& res, uint32 dungeonID = 0);
    static void GetTriggerEvents(DBQueryResult& res, uint32 dungeonID = 0);
    static void GetGates(DBQueryResult& res, uint32 dungeonID = 0);
    static void GetLoot(DBQueryResult& res);
    static void GetEscalations(DBQueryResult& res);

    // granted escalations
    static uint32 InsertEscalation(const EscalationRow& row);
    static bool GetEscalation(uint32 instanceID, EscalationRow& into);
    // not completed/removed, not expired
    static void GetEscalationsForSystem(uint32 systemID, int64 now, std::vector<EscalationRow>& into);
    // everything the journal shows (not removed)
    static void GetEscalationsForChar(uint32 charID, std::vector<EscalationRow>& into);
    // expiryTime 0 keeps it.  entered (1) only replaces granted (0)
    static void SetEscalationState(uint32 instanceID, uint8 state, int64 expiryTime = 0);

protected:

private:

};


#endif  // _DUNGEON_DATABASE_H