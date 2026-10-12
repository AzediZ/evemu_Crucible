
 /**
  * @name AllianceBound.h
  *     Alliance Bound code
  *
  * @Author:        Allan
  * Updated:        James
  * @date:          16 January 2018
  */



#ifndef EVE_ALLIANCE_ALLYREGISTRY_BOUND_H_
#define EVE_ALLIANCE_ALLYREGISTRY_BOUND_H_


#include "../eve-server.h"
#include "services/BoundService.h"
#include "alliance/AllianceDB.h"
#include "cache/ObjCacheService.h"
#include "chat/LSCService.h"
#include "Client.h"

class AllianceRegistry;

class AllianceBound : public EVEBoundObject <AllianceBound>
{
public:
    AllianceBound(EVEServiceManager& mgr, AllianceRegistry& parent, AllianceDB& db, uint32 allyID);

    static void FillOAApplicationChange(OnAllianceApplicationChanged& OAAC, const Alliance::ApplicationInfo& Old, const Alliance::ApplicationInfo& New);

    uint32 GetAllianceID () const { return this->m_allyID; }
protected:
    // contact labels (bit value labelIDs), managed by directors and diplomats of the executor corporation
    PyResult CreateLabel(PyCallArgs& call, PyRep* name, std::optional<PyRep*> color);
    PyResult GetLabels(PyCallArgs& call);
    PyResult DeleteLabel(PyCallArgs& call, PyRep* labelID);
    PyResult EditLabel(PyCallArgs& call, PyRep* labelID, PyRep* name, std::optional<PyRep*> color);
    PyResult AssignLabels(PyCallArgs& call, PyList* contactIDs, PyRep* labelMask);
    PyResult RemoveLabels(PyCallArgs& call, PyList* contactIDs, PyRep* labelMask);

    PyResult AddBulletin(PyCallArgs& call, PyRep* title, PyRep* body);
    PyResult GetBulletins(PyCallArgs& call);
    PyResult DeleteBulletin(PyCallArgs& call, PyInt* id);
    void RequireBulletinManager(PyCallArgs& call);

    PyResult GetMembers(PyCallArgs& call);
    PyResult DeclareExecutorSupport(PyCallArgs& call, PyInt* chosenExecutor);
    PyResult DeleteMember(PyCallArgs& call, PyInt* corporationID);
    PyResult GetApplications(PyCallArgs& call);
    PyResult UpdateApplication(PyCallArgs& call, PyInt* corporationID, PyWString* applicationText, PyInt* state);
    PyResult AddToVoiceChat(PyCallArgs& call, PyString* channelName);
    PyResult PayBill(PyCallArgs& call, PyInt* billID, PyInt* fromAccountKey);
    PyResult GetBillBalance(PyCallArgs& call, PyInt* billID);
    PyResult GetBills(PyCallArgs& call);
    PyResult GetBillsReceivable(PyCallArgs& call);
    PyResult GetAllianceContacts(PyCallArgs& call);
    PyResult AddAllianceContact(PyCallArgs& call, PyInt* contactID, PyRep* relationshipID);
    PyResult EditAllianceContact(PyCallArgs& call, PyInt* contactID, PyRep* relationshipID);
    PyResult RemoveAllianceContacts(PyCallArgs& call, PyList* contactIDs);
    PyResult EditContactsRelationshipID(PyCallArgs& call, PyList* contactIDs, PyRep* relationshipID);
    void RequireDiplomat(PyCallArgs& call);
    void SendContactsUpdated(PyList* updates);
    void SendLabelMasks(const std::vector<int32>& contactIDs);

    // the legacy alliance relationships (all_cso_relationships)
    PyResult GetRelationships(PyCallArgs& call);
    PyResult SetRelationship(PyCallArgs& call, PyInt* relationship, PyInt* toID);
    PyResult DeleteRelationship(PyCallArgs& call, PyInt* toID);
    void SendRelationshipChanged(uint32 toID, uint8 oldRel, uint8 newRel);
    PyResult GetAlliance(PyCallArgs& call);
    PyResult UpdateAlliance(PyCallArgs& call, PyWString* description, PyWString* url);

    // wars:  directors of the executor corporation
    PyResult DeclareWarAgainst(PyCallArgs& call, PyInt* againstID);
    PyResult RetractWar(PyCallArgs& call, PyInt* warID);
    PyResult ChangeMutualWarFlag(PyCallArgs& call, PyInt* warID, PyRep* mutual);
    void RequireWarDirector(PyCallArgs& call);

    AllianceDB& m_db;
    ObjCacheService* m_cache;
    LSCService* m_lsc;

    uint32 m_allyID;
};

#endif  // EVE_ALLIANCE_ALLYREGISTRY_BOUND_H_
