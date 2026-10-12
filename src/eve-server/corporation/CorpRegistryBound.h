


#ifndef __EVEMU_CORP_CORPREGISTRY_BOUND_H_
#define __EVEMU_CORP_CORPREGISTRY_BOUND_H_


#include "../eve-server.h"
#include "corporation/CorporationDB.h"
#include "services/BoundService.h"
#include "chat/LSCService.h"
#include "cache/ObjCacheService.h"
#include "corporation/OfficeSparseBound.h"

class CorpRegistryService;
class OfficeSparseBound;

class CorpRegistryBound : public EVEBoundObject <CorpRegistryBound>, public BoundServiceParent <OfficeSparseBound>
{
public:
    CorpRegistryBound(EVEServiceManager &mgr, CorpRegistryService& parent, CorporationDB& db, uint32 corpID);

    uint32 GetCorporationID() const { return this->m_corpID; }

    void BoundReleased (OfficeSparseBound* bound) override;

protected:
    PyResult GetEveOwners(PyCallArgs& call);
    PyResult GetCorporation(PyCallArgs& call);
    PyResult GetCorporations(PyCallArgs& call, PyInt* corporationID);
    PyResult GetInfoWindowDataForChar(PyCallArgs& call, std::optional <PyInt*> characterID);
    PyResult GetLockedItemLocations(PyCallArgs& call);
    PyResult GetLockedItemsByLocation(PyCallArgs& call, PyInt* locationID);
    PyResult AddCorporation(PyCallArgs& call,
        PyRep* name, PyRep* ticker, PyRep* description, PyRep* url, PyFloat* taxRate,
        PyRep* shape1, PyRep* shape2, PyRep* shape3,
        PyRep* color1, PyRep* color2, PyRep* color3,
        PyRep* typeface, PyInt* applicationEnabled);
    PyResult GetSuggestedTickerNames(PyCallArgs& call, PyWString* name);
    PyResult GetOffices(PyCallArgs& call);
    PyResult GetStations(PyCallArgs& call);

    PyResult CreateRecruitmentAd(PyCallArgs& call, PyInt* days, PyInt* typeMask, std::optional <PyInt*> allianceID, PyWString* description, PyInt* channelID, PyList* recruiterIDs, PyWString* title);
    PyResult UpdateRecruitmentAd(PyCallArgs& call, PyInt* adID, PyInt* typeMask, PyWString* description, PyInt* channelID, PyList* recruiterIDs, PyWString* title, PyInt* addedDays);
    PyResult DeleteRecruitmentAd(PyCallArgs& call, PyInt* adID);
    PyResult GetRecruiters(PyCallArgs& call, PyInt* corpID, PyInt* adID);
    PyResult GetRecruitmentAdsForCorporation(PyCallArgs& call);
    PyResult GetMyApplications(PyCallArgs& call);
    PyResult InsertApplication(PyCallArgs& call, PyInt* corporationID, PyRep* message);
    PyResult GetApplications(PyCallArgs& call);
    PyResult UpdateApplicationOffer(PyCallArgs &call, PyInt* characterID, PyRep* applicationText, PyInt* status, PyNone* applicationDateTime);
    PyResult DeleteApplication(PyCallArgs& call, PyInt* corporationID, PyInt* characterID);

    PyResult UpdateDivisionNames(PyCallArgs& call,
        PyRep* div1, PyRep* div2, PyRep* div3, PyRep* div4, PyRep* div5, PyRep* div6, PyRep* div8,
        PyRep* wal1, PyRep* wal2, PyRep* wal3, PyRep* wal4, PyRep* wal5, PyRep* wal6, PyRep* wal7);
    PyResult UpdateCorporation(PyCallArgs& call, PyRep* description, PyRep* url, PyRep* tax, std::optional<PyRep*> acceptApplications);
    PyResult UpdateLogo(PyCallArgs& call,
        PyRep* shape1, PyRep* shape2, PyRep* shape3,
        PyRep* color1, PyRep* color2, PyRep* color3,
        PyRep* typeface);
    PyResult SetAccountKey(PyCallArgs& call, PyInt* accountKey);
    PyResult GetMember(PyCallArgs& call, PyInt* characterID);
    PyResult GetMembers(PyCallArgs& call);

    PyResult MoveCompanyShares(PyCallArgs& call, PyInt* corporationID, PyInt* toShareholderID, PyInt* numberOfShares);
    PyResult MovePrivateShares(PyCallArgs& call, PyInt* corporationID, PyInt* toShareholderID, PyInt* numberOfShares);
    PyResult GetSharesByShareholder(PyCallArgs& call, PyBool* corpShares);
    PyResult GetShareholders(PyCallArgs& call);
    PyResult PayoutDividend(PyCallArgs& call, PyRep* payShareholders, PyRep* payoutAmount);

    PyResult CanViewVotes(PyCallArgs& call, PyInt* corporationID);
    PyResult InsertVoteCase(PyCallArgs& call, PyRep* voteCaseText, PyRep* description, PyInt* corporationID, PyInt* voteType, PyObject* voteCaseOptions, std::optional<PyRep*> startDateTime, std::optional<PyRep*> endDateTime);
    PyResult GetVotes(PyCallArgs& call, PyInt* corporationId, PyInt* voteCaseID);
    PyResult CanVote(PyCallArgs& call, PyInt* corporationID);
    PyResult InsertVote(PyCallArgs& call, PyInt* corporationID, PyInt* voteCaseID, PyInt* voteValue);
    PyResult GetVoteCasesByCorporation(PyCallArgs& call, PyInt* corporationID, std::optional<PyInt*> status, std::optional <PyInt*> maxLen);
    PyResult GetVoteCaseOptions(PyCallArgs& call, PyRep* corporationID, PyInt* voteCaseID);
    PyResult GetSanctionedActionsByCorporation(PyCallArgs& call, PyInt* corporationID, PyInt* state);
    PyResult UpdateSanctionedAction(PyCallArgs& call, PyInt* voteCaseID, PyRep* inEffect, std::optional<PyRep*> reason);

    PyResult GetRoleGroups(PyCallArgs& call);
    PyResult GetRoles(PyCallArgs& call);

    PyResult CreateLabel(PyCallArgs& call, PyRep* name, std::optional<PyRep*> color);
    PyResult GetLabels(PyCallArgs& call);
    PyResult DeleteLabel(PyCallArgs& call, PyRep* labelID);
    PyResult EditLabel(PyCallArgs& call, PyRep* labelID, PyRep* name, std::optional<PyRep*> color);
    PyResult AssignLabels(PyCallArgs& call, PyList* contactIDs, PyRep* labelMask);
    PyResult RemoveLabels(PyCallArgs& call, PyList* contactIDs, PyRep* labelMask);

    PyResult GetTitles(PyCallArgs& call);
    PyResult UpdateTitle(PyCallArgs& call,
        PyRep* titleID, PyRep* titleName,
        PyRep* roles, PyRep* grantableRoles,
        PyRep* rolesAtHQ, PyRep* grantableRolesAtHQ,
        PyRep* rolesAtBase, PyRep* grantableRolesAtBase,
        PyRep* rolesAtOther, PyRep* grantableRolesAtOther);
    PyResult UpdateTitles(PyCallArgs& call, PyObject* titles);
    PyResult DeleteTitle(PyCallArgs& call, PyInt* titleID);

    PyResult AddBulletin(PyCallArgs& call, PyRep* title, PyRep* body);
    PyResult GetBulletins(PyCallArgs& call);
    PyResult DeleteBulletin(PyCallArgs& call, PyInt* bulletinID);

    PyResult GetLocationalRoles(PyCallArgs& call);
    PyResult GetRecentKillsAndLosses(PyCallArgs& call, PyInt* number, PyInt* offset);
    PyResult UpdateMember(PyCallArgs& call,
        PyInt* characterID, PyRep* title, PyRep* divisionID, PyRep* squadronID,
        PyRep* roles, PyRep* grantableRoles, PyRep* rolesAtHQ, PyRep* grantableRolesAtHQ,
        PyRep* rolesAtBase, PyRep* grantableRolesAtBase, PyRep* rolesAtOther, PyRep* grantableRolesAtOther,
        PyRep* baseID, std::optional<PyRep*> titleMask, std::optional<PyRep*> blockRoles);
    PyResult UpdateMembers(PyCallArgs& call, PyObject* rows);
    PyResult ExecuteActions(PyCallArgs& call, PyList* targetIDs, PyList* remoteActions);

    PyResult GetCorporateContacts(PyCallArgs& call);
    PyResult AddCorporateContact(PyCallArgs& call, PyInt* contactID, PyRep* relationshipID);
    PyResult EditContactsRelationshipID(PyCallArgs& call, PyList* contactIDs, PyRep* relationshipID);
    PyResult RemoveCorporateContacts(PyCallArgs& call, PyList* contactIDs);
    PyResult EditCorporateContact(PyCallArgs& call, PyInt* contactID, PyRep* relationshipID);

    // bco_wars:  the active CEO of a corporation that isn't in an alliance (an alliance's wars go through the alliance moniker)
    PyResult RetractWar(PyCallArgs& call, PyInt* warID);
    PyResult ChangeMutualWarFlag(PyCallArgs& call, PyInt* warID, PyRep* mutual);
    void RequireWarCEO(PyCallArgs& call);

    PyResult CreateAlliance(PyCallArgs& call, PyRep* allianceName, PyRep* shortName, PyRep* description, PyRep* url);
    PyResult ApplyToJoinAlliance(PyCallArgs& call, PyInt* allianceID, PyWString* applicationText);
    PyResult DeleteAllianceApplication(PyCallArgs& call, PyInt* allianceID);
    PyResult GetAllianceApplications(PyCallArgs& call);
    PyResult GetSuggestedAllianceShortNames(PyCallArgs& call, PyWString* name);

    PyResult GetMemberTrackingInfo(PyCallArgs& call);
    PyResult GetMembersPaged(PyCallArgs& call, PyInt* page);
    PyResult GetMembersByIds(PyCallArgs& call, PyList* memberIDs);
    PyResult GetMemberIDsWithMoreThanAvgShares(PyCallArgs& call);
    PyResult GetMemberIDsByQuery(PyCallArgs& call, PyList* queryList, std::optional <PyInt*> includeImplied, PyInt* searchTitles);
    PyResult GetMemberTrackingInfoSimple(PyCallArgs& call);

    PyResult GetRentalDetailsPlayer(PyCallArgs& call);
    PyResult GetRentalDetailsCorp(PyCallArgs& call);

    PyResult UpdateCorporationAbilities(PyCallArgs& call);
    PyResult GetNumberOfPotentialCEOs(PyCallArgs& call);

    PyResult CanLeaveCurrentCorporation(PyCallArgs& call);
    PyResult CanBeKickedOut(PyCallArgs& call, PyInt* characterID);
    PyResult KickOutMember(PyCallArgs& call, PyInt* characterID);
    PyResult MemberCanRunForCEO(PyCallArgs& call);
    PyResult ResignFromCEO(PyCallArgs& call, std::optional<PyInt*> newCeoID);
    

protected:
    static void FillOCApplicationChange(OnCorporationApplicationChanged& OCAC, const Corp::ApplicationInfo& Old, const Corp::ApplicationInfo& New);

    // mutating calls are for members of this corp only
    void RequireMember(PyCallArgs& call);
    void RequireDirector(PyCallArgs& call);
    void RequireDiplomat(PyCallArgs& call);
    // OnOrganizationContactsUpdated(corpID, updates) to the members.  consumes updates
    void SendContactsUpdated(PyList* updates);
    // the contacts' current label masks to the members
    void SendLabelMasks(const std::vector<int32>& contactIDs);
    // CTV_GIVE CTPG_SHARES:  corp treasury shares to a member
    void GiveShares(PyCallArgs& call, uint32 toID, int64 count);
    // UpdateApplicationOffer accept:  the applicant may leave their corp, member limit and allowed races.  throws
    void CheckCanAccept(uint32 charID);

    bool GetSearchValues(int8 op, PyRep* rep, std::ostringstream& query);
    uint8 GetQueryType(std::string queryType);

    CorporationDB& m_db;
    ObjCacheService* m_cache;
    LSCService* m_lsc;
    OfficeSparseBound* m_offices;

    uint32 m_corpID;
};

#endif  // __EVEMU_CORP_CORPREGISTRY_BOUND_H_
