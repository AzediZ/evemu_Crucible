
 /**
  * @name CorpData.h
  *     Corporation Data containers for EVEmu
  *
  * @Author:        Allan
  * @date:          25 December 2017
  */

#ifndef EVEMU_SRC_CORP_DATA_H_
#define EVEMU_SRC_CORP_DATA_H_


#include "../eve-server.h"

#include <optional>

    /*
    "corpRole"      - corporation management-type roles (manager, officer, trader)  NOT access-type roles
    "rolesAtAll"    - access roles everywhere.  is joined with other access roles
    "rolesAtBase"   - access roles at base defined for this char. overrides hq if hq and base are same location for char
    "rolesAtHQ"     - access roles at corp HQ.
    "rolesAtOther"  - access roles for non-station containers with corp hangars
    */


namespace Corp {

    /* POD structure for corp members query data */
    struct QueryMembers {
        bool blockRoles;
        uint32 characterID;
        int64 startDateTime;
        int64 titleMask;
        int64 rolesAtAll;
        int64 rolesAtHQ;
        int64 rolesAtBase;
        int64 rolesAtOther;
        int64 grantableRoles;
        int64 grantableRolesAtHQ;
        int64 grantableRolesAtBase;
        int64 grantableRolesAtOther;
    };

    /* POD structure for corp vote data.  the client sends rows [optionText, parameter, parameter1, parameter2]; the row index is the optionID */
    struct VoteCaseOptions {
        int8 optionID = 0;
        int32 parameter = 0;    // ceo/kick: charID, war: ownerID, shares: count, lock/unlock: itemID, general: option index.  0 = "don't"
        int32 parameter1 = 0;   // lock/unlock: typeID
        int32 parameter2 = 0;   // lock/unlock: stationID
        double votesFor = 0;    // share-weighted
        std::string optionText;
    };

    /* one vote case (crpVoteItems) */
    struct VoteCase {
        uint32 voteCaseID = 0;
        uint32 corporationID = 0;
        uint32 characterID = 0;     // proposer
        uint8 voteType = 0;
        uint8 status = 0;           // 2 open, 1 closed
        bool inEffect = false;
        bool actedUpon = false;
        bool rescended = false;
        int8 winningOptionID = -1;
        int64 startDateTime = 0;
        int64 endDateTime = 0;
        int64 expires = 0;          // sanctioned action:  the CEO may implement it until then
        int64 timeActedUpon = 0;
        int64 timeRescended = 0;
        std::string voteCaseText;
        std::string description;
    };

    /* POD structure for corp app data */
    struct ApplicationInfo {
        bool valid;
        uint32 appID;
        uint32 corpID;
        uint32 charID;
        uint32 status;
        uint32 deleted;
        uint32 lastCID;
        int64 appTime;
        int64 role;
        int64 grantRole;
        std::string appText;
    };

    struct MedalData {
        int8 part;
        int16 color;
        std::string graphic;
    };

    /* one member edit from UpdateMember/UpdateMembers/ExecuteActions.  unset fields are unchanged (the client sends None) */
    struct MemberUpdate {
        uint32 charID = 0;
        std::optional<std::string> title;
        std::optional<int64> roles;
        std::optional<int64> grantableRoles;
        std::optional<int64> rolesAtHQ;
        std::optional<int64> grantableRolesAtHQ;
        std::optional<int64> rolesAtBase;
        std::optional<int64> grantableRolesAtBase;
        std::optional<int64> rolesAtOther;
        std::optional<int64> grantableRolesAtOther;
        std::optional<int64> baseID;
        std::optional<int64> titleMask;
        std::optional<bool> blockRoles;

        // None (or nullptr) -> unset.  Int and Long both work
        static std::optional<int64> Int(PyRep* rep);
    };

    /* the roles a member's titles give */
    struct TitleRoles {
        int64 roles = 0;
        int64 grantableRoles = 0;
        int64 rolesAtHQ = 0;
        int64 grantableRolesAtHQ = 0;
        int64 rolesAtBase = 0;
        int64 grantableRolesAtBase = 0;
        int64 rolesAtOther = 0;
        int64 grantableRolesAtOther = 0;
    };

}

#endif  // EVEMU_SRC_CORP_DATA_H_


/*{'FullPath': u'UI/Messages', 'messageID': 259245, 'label': u'PermissionDeniedCorpDoesntAllowBody'}(u'You cannot do that on behalf of the {corp} corporation because they do not allow it.', None, {u'{corp}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'corp'}})
 * {'FullPath': u'UI/Messages', 'messageID': 259246, 'label': u'PermissionDeniedNeedEquipRoleBody'}(u'You cannot do that on behalf of the {corp} corporation because they have not given you the equipment config role.', None, {u'{corp}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'corp'}})
 * {'FullPath': u'UI/Messages', 'messageID': 259247, 'label': u'PermissionDeniedNeedStarbaseRoleBody'}(u'You cannot do that on behalf of the {corp} corporation because they have not given you the starbase config role.', None, {u'{corp}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'corp'}})
 * {'FullPath': u'UI/Messages', 'messageID': 259248, 'label': u'PermissionDeniedNotInCorpBody'}(u'You cannot do that on behalf of the {corp} corporation because you are not a member.', None, {u'{corp}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'corp'}})
 * {'FullPath': u'UI/Messages', 'messageID': 259249, 'label': u'PermissionDeniedNotOwnerBody'}(u'You are unable to do that to the {item} as it is owned by {owner} and not by you.', None, {u'{item}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'item'}, u'{owner}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'owner'}})
 */