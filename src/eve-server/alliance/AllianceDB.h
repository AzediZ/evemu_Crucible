/**
 * @name AllianceDB.h
 *      database methods for alliance data
 *
 * @author Allan
 * updates James
 * @date 24 May 2019
 *
 */


#ifndef EVE_ALLIANCE_ALLIANCEDB_H
#define EVE_ALLIANCE_ALLIANCEDB_H


#include "ServiceDB.h"

#include "alliance/AllianceData.h"
#include "packets/CorporationPkts.h"


struct OfficeData;

class Client;
class PyRep;
class PyObject;

class AllianceDB
{
public:

    // allianceRegistry.GetEmploymentRecord:  newest first (allianceID None = left), the corporation's founding row last
    PyRep* GetEmploymentRecord(uint32 corpID);
    bool AddEmployment(uint32 allyID, uint32 corpID);

    bool CreateAlliance(std::string name, std::string shortName, std::string description, std::string url, Client* pClient, uint32& allyID, uint32& corpID);

    PyRep* GetBulletins(uint32 allyID);
    void AddBulletin(uint32 allyID, uint32 ownerID, uint32 cCharID, const std::string& title, const std::string& body);
    // only the alliance's own bulletins
    static void EditBulletin(uint32 allyID, uint32 bulletinID, uint32 eCharID, const std::string& title, const std::string& body);
    static void DeleteBulletin(uint32 allyID, uint32 bulletinID);

    // the corporation's latest application to each alliance, keyed by allianceID
    static PyRep *GetMyApplications(uint32 corpID);
    // the latest application of each corporation, keyed by corporationID (the client hides rejected ones itself)
    PyRep *GetApplications(uint32 allyID);

    // executorCorpID None when there is none (turmoil), memberCount = pilots
    PyRep *GetAlliance(uint32 allyID);

    PyRep* GetContacts(uint32 allyID);
    void AddContact(uint32 ownerID, int32 contactID, float relationshipID);
    void UpdateContact(float relationshipID, uint32 contactID, uint32 ownerID);
    void RemoveContact(uint32 contactID, uint32 ownerID);
    static void GetContactLabelMasks(uint32 allyID, const std::vector<int32>& contactIDs, std::map<int32, int64>& into);

    // the legacy alliance relationships (all_cso_relationships):  CIndexedRowset toID, relationship keyed by toID
    static PyRep* GetRelationships(uint32 allyID);
    static bool GetRelationship(uint32 allyID, uint32 toID, uint8& relationship);
    static void SetRelationship(uint32 allyID, uint32 toID, uint8 relationship);
    static void DeleteRelationship(uint32 allyID, uint32 toID);

    bool InsertApplication(Alliance::ApplicationInfo& aInfo);
    bool UpdateApplication(const Alliance::ApplicationInfo& aInfo);
    bool DeleteApplication(const Alliance::ApplicationInfo& aInfo);
    // the corporation's latest application to the alliance
    bool GetCurrentApplicationInfo(uint32 allyID, uint32 corpID, Alliance::ApplicationInfo& aInfo);
    static void SetApplicationEffectiveAt(uint32 appID, int64 effectiveAt);
    // accepted applications whose join delay has passed:  applicationID -> (corporationID, allianceID)
    static void GetDueApplications(int64 now, std::map<uint32, std::pair<uint32, uint32>>& into);
    // a pending (new) application of the corporation to the alliance
    static bool HasPendingApplication(uint32 allyID, uint32 corpID);

    bool IsShortNameTaken(std::string shortName);
    bool UpdateCorpAlliance(uint32 allyID, uint32 corpID);
    // the corporation's membership ends:  a leave record, and corporations that supported it support nobody now
    void DeleteMember(uint32 allyID, uint32 corpID);
    void DeclareExecutorSupport(uint32 corpID, uint32 chosenExecutor);
    void UpdateAlliance(uint32 allyID, std::string description, std::string url);

    PyRep* GetMembers(uint32 allyID);
    PyRep* GetAllianceMembers(uint32 allyID);
    // by pilots, most first.  maxLen 0 = all
    PyRep* GetRankedAlliances(uint32 maxLen);

    static uint32 GetExecutorID(uint32 allyID);
    static void SetExecutor(uint32 allyID, uint32 corpID);
    static void CloseAlliance(uint32 allyID);
    static bool IsMember(uint32 allyID, uint32 corpID);
    static void GetMemberCorps(uint32 allyID, std::vector<uint32>& into);
    // corporationID -> chosenExecutorID of every member
    static void GetExecutorSupport(uint32 allyID, std::map<uint32, uint32>& into);
    static uint32 GetChosenExecutor(uint32 corpID);
    static int64 GetMemberStartDate(uint32 corpID);
    static void GetLiveAlliances(std::vector<uint32>& into);
    bool CreateAllianceChangePacket(OnAllianceChanged &ac, uint32 oldAllyID, uint32 newAllyID);
};

#endif  // EVE_ALLIANCE_ALLIANCEDB_H