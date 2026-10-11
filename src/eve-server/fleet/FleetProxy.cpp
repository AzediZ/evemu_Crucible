
 /**
  * @name FleetProxy.cpp
  *     Fleet Proxy code for EVEmu
  *     This file deals with very generic fleet functions
  *
  * @Author:        Allan
  * @date:          05 August 2014 (original skeleton outline)
  * @update:        21 November 2017 (begin actual implementation)
  * @finished:      02 December 2017
  *
  */

#include "eve-server.h"

#include "fleet/FleetProxy.h"

FleetProxy::FleetProxy() :
    Service("fleetProxy")
{
    this->Add("GetAvailableFleets", &FleetProxy::GetAvailableFleets);
    this->Add("AddFleetFinderAdvert", &FleetProxy::AddFleetFinderAdvert);
    this->Add("RemoveFleetFinderAdvert", &FleetProxy::RemoveFleetFinderAdvert);
    this->Add("GetMyFleetFinderAdvert", &FleetProxy::GetMyFleetFinderAdvert);
    this->Add("UpdateAdvertInfo", &FleetProxy::UpdateAdvertInfo);
    this->Add("ApplyToJoinFleet", &FleetProxy::ApplyToJoinFleet);
}

/*
FLEET__ERROR
FLEET__WARNING
FLEET__MESSAGE
FLEET__DEBUG
FLEET__INFO
FLEET__TRACE
FLEET__DUMP
FLEET__BIND_DUMP
*/
PyResult FleetProxy::GetAvailableFleets(PyCallArgs &call) {
    return sFltSvc.GetAvailableFleets(call.client);
}

PyResult FleetProxy::ApplyToJoinFleet(PyCallArgs &call, PyInt* fleetID) {
  // ret = sm.ProxySvc('fleetProxy').ApplyToJoinFleet(fleetID)
   // sLog.White("FleetProxy", "Handle_ApplyToJoinFleet() size=%lli", call.tuple->size());
   // call.Dump(FLEET__DUMP);

    // returns boolean.  only adverts the applicant can see may be joined
    if (!sFltSvc.CanSeeAdvert(fleetID->value(), call.client))
        return new PyBool(false);
    return new PyBool(sFltSvc.AddJoinRequest(fleetID->value(), call.client));
}

    // this is also used to update advert info
PyResult FleetProxy::AddFleetFinderAdvert(PyCallArgs &call, PyObject* info) {
 //  sm.ProxySvc('fleetProxy').AddFleetFinderAdvert(info)
    //sLog.White("FleetProxy", "Handle_AddFleetFinderAdvert() size=%lli", call.tuple->size());
    //call.Dump(FLEET__DUMP);

    FleetAdvertCall args;
    if (!args.Decode(&call.tuple)) {
        codelog(SERVICE__ERROR, "%s: Failed to decode args.", call.client->GetChar()->name());
        return nullptr;
    }

    // only the fleet boss registers the fleet
    int32 fleetID = call.client->GetChar()->fleetID();
    if ((fleetID == 0) or !sFltSvc.IsBoss(call.client->GetChar().get()))
        throw UserError("FleetNotCreator");
    // "You cannot set your fleet to 'Self-Invite' if you are in an NPC corporation."
    if ((args.inviteScope & Fleet::Invite::Corp) and IsNPCCorp(call.client->GetCorporationID()))
        throw UserError("FleetNotNPCCorp");

    FleetData fData = FleetData();
    sFltSvc.GetFleetData(fleetID, fData);

    // TODO: update this sometime in the future, updating this is way out of the scope of the service changes
    FleetAdvert adata = FleetAdvert();
        adata.fleetID = fleetID;
        adata.hideInfo = args.hideInfo;
        adata.inviteScope = args.inviteScope;
        adata.leader = call.client;
        adata.fleetName = PyRep::StringContent(args.fleetName);
        adata.advertTime = GetFileTimeNow();
        adata.dateCreated = fData.dateCreated;
        adata.description = PyRep::StringContent(args.description);
        adata.solarSystemID = call.client->GetSystemID();
        adata.joinNeedsApproval = args.joinNeedsApproval;
        adata.local_minSecurity = args.local_minSecurity;
        adata.local_minStanding = args.local_minStanding;
        adata.public_minSecurity = args.public_minSecurity;
        adata.public_minStanding = args.public_minStanding;
    PyList* localList = args.local_allowedEntities->header()->AsTuple()->GetItem(1)->AsTuple()->GetItem(0)->AsList();
        adata.local_allowedEntities.clear();
    for (PyList::const_iterator itr = localList->begin(); itr != localList->end(); ++itr)
        adata.local_allowedEntities.push_back(PyRep::IntegerValueU32(*itr));
    PyList* publicList = args.public_allowedEntities->header()->AsTuple()->GetItem(1)->AsTuple()->GetItem(0)->AsList();
        adata.public_allowedEntities.clear();
    for (PyList::const_iterator itr = publicList->begin(); itr != publicList->end(); ++itr)
        adata.public_allowedEntities.push_back(PyRep::IntegerValueU32(*itr));
    sFltSvc.CreateFleetAdvert(fleetID, adata);

    return nullptr;
}

PyResult FleetProxy::RemoveFleetFinderAdvert(PyCallArgs &call) {
   // sLog.White("FleetProxy", "Handle_RemoveFleetFinderAdvert() size=%lli", call.tuple->size());
    //call.Dump(FLEET__DUMP);

    if ((call.client->GetChar()->fleetID() == 0) or !sFltSvc.IsBoss(call.client->GetChar().get()))
        throw UserError("FleetNotCreator");
    // returns the removed advert:  fleetSvc.RemoveAndUpdateFleetFinderAdvert reopens the register window with it
    PyRep* old(sFltSvc.GetFleetAdvert(call.client->GetChar()->fleetID()));
    sFltSvc.RemoveFleetAdvert(call.client->GetChar()->fleetID());

    return (old == nullptr ? PyStatic.NewNone() : old);
}

PyResult FleetProxy::GetMyFleetFinderAdvert(PyCallArgs &call) {
   // sLog.White("FleetProxy", "Handle_GetMyFleetFinderAdvert() size=%lli", call.tuple->size());
   // call.Dump(FLEET__DUMP);

    return sFltSvc.GetFleetAdvert(call.client->GetChar()->fleetID());
}

PyResult FleetProxy::UpdateAdvertInfo(PyCallArgs &call, PyInt* numMembers) {
  //   sm.ProxySvc('fleetProxy').UpdateAdvertInfo(numMembers)
    // this call just updates member count in fleet advert.  not needed here, as that is dynamic data.
    //sLog.White("FleetProxy", "Handle_UpdateAdvertInfo() size=%lli", call.tuple->size());
    //call.Dump(FLEET__DUMP);

    return nullptr;
}
