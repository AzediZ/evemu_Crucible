/**
 * @name AllianceManager.h
 *   alliance membership:  joining (24 hours after the application is accepted in Crucible), leaving and kicking,
 *   the executor chosen by the members' support (counted daily at downtime), closing alliances, and the
 *   client's alliance notifications.  wars follow the corporations through WarManager.
 */

#ifndef EVEMU_ALLIANCE_ALLIANCEMANAGER_H_
#define EVEMU_ALLIANCE_ALLIANCEMANAGER_H_

#include "eve-server.h"

#include "alliance/AllianceData.h"

class Client;
class PyDict;
class PyTuple;

class AllianceManager
: public Singleton<AllianceManager>
{
public:
    AllianceManager();

    int Initialize();
    // once a minute:  accepted applications that are due, the daily executor count
    void Process();

    // the corporation becomes a member (its application is effective, or it created the alliance)
    void JoinAlliance(uint32 corpID, uint32 allianceID, bool creator);
    // the corporation quits or is kicked.  an alliance without members closes
    void LeaveAlliance(uint32 corpID, uint32 allianceID);
    // every member leaves (unpaid maintenance)
    void DisbandAlliance(uint32 allianceID);
    // the online members' sessions (allianceid) and their ships' slim items
    void UpdateMemberSessions(uint32 corpID, uint32 allianceID);
    // a member corporation supported by at least half of the members becomes executor
    void TallyExecutor(uint32 allianceID);
    void TallyExecutors();

    /* permissions */
    // a director (or the CEO) of the alliance's executor corporation
    static bool IsExecutorDirector(Client* pClient, uint32 allianceID);
    static void RequireExecutorDirector(Client* pClient, uint32 allianceID);
    // throws CrpAccessDenied unless the client's corporation is a member
    static void RequireMember(Client* pClient, uint32 allianceID);
    // the client's CEO or a director
    static bool IsCEOOrDirector(Client* pClient);

    /* notifications */
    // every online member of the alliance (and of alsoCorpID).  consumes payload
    static void SendToAlliance(uint32 allianceID, const char* notifyType, PyTuple* payload, uint32 alsoCorpID = 0);
    // OnAllianceChanged(allianceID, change) with the whole alliance row, to the members (and alsoCorpID)
    static void SendAllianceChanged(uint32 allianceID, uint32 alsoCorpID = 0);
    enum MemberChange : uint8 { MemberAdded = 0, MemberRemoved = 1, MemberUpdated = 2 };
    // OnAllianceMemberChanged(allianceID, corpID, {corporationID, corporationName, chosenExecutorID})
    static void SendMemberChanged(uint32 allianceID, uint32 corpID, uint8 kind, uint32 oldChosen, uint32 newChosen, uint32 alsoCorpID = 0);
    // OnAllianceApplicationChanged to the applying corp and the executor corp
    static void SendApplicationChanged(const Alliance::ApplicationInfo& before, const Alliance::ApplicationInfo& after);

private:
    void ProcessPendingJoins(int64 now);

    int64 m_lastTallyDay;
    std::set<uint32> m_heldApplications;    // accepted applications held back (declared wars), logged once
};

#define sAllianceMgr \
( AllianceManager::get() )

#endif  // EVEMU_ALLIANCE_ALLIANCEMANAGER_H_
