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
    Author:             Zhur
    Implementation:     Positron96
    Rewrite:            Allan
*/

/** @todo  go thru and update/optimize this class */

#include "eve-server.h"

#include "../../eve-common/EVE_Calendar.h"
#include "../../eve-common/EVE_Mail.h"


#include "EntityList.h"
#include "StatisticMgr.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "manufacturing/Blueprint.h"
#include "manufacturing/RamMethods.h"
#include "manufacturing/RamProxyService.h"
#include "station/StationDataMgr.h"
#include "system/CalendarDB.h"

RamProxyService::RamProxyService() :
    Service("ramProxy")
{
    this->Add("AssemblyLinesGet", &RamProxyService::AssemblyLinesGet);
    this->Add("AssemblyLinesSelect", &RamProxyService::AssemblyLinesSelect);
    this->Add("AssemblyLinesSelectPublic", &RamProxyService::AssemblyLinesSelectPublic);
    this->Add("AssemblyLinesSelectPrivate", &RamProxyService::AssemblyLinesSelectPrivate);
    this->Add("AssemblyLinesSelectCorp", &RamProxyService::AssemblyLinesSelectCorp);
    this->Add("AssemblyLinesSelectAlliance", &RamProxyService::AssemblyLinesSelectAlliance);
    this->Add("GetJobs2", &RamProxyService::GetJobs2);
    this->Add("InstallJob", &RamProxyService::InstallJob);
    this->Add("CompleteJob", &RamProxyService::CompleteJob);
    this->Add("GetRelevantCharSkills", &RamProxyService::GetRelevantCharSkills);
    this->Add("UpdateAssemblyLineConfigurations", &RamProxyService::UpdateAssemblyLineConfigurations);
}

/*
 * # Manufacturing Logging:
 * MANUF__ERROR
 * MANUF__WARNING
 * MANUF__MESSAGE
 * MANUF__INFO
 * MANUF__DEBUG
 * MANUF__TRACE
 * MANUF__DUMP
 */

PyResult RamProxyService::GetRelevantCharSkills(PyCallArgs &call) {
    return call.client->GetChar()->GetRAMSkills();
}

PyResult RamProxyService::AssemblyLinesSelectPublic(PyCallArgs &call) {
    return FactoryDB::AssemblyLinesSelectPublic(call.client->GetRegionID());
}

PyResult RamProxyService::AssemblyLinesSelectPrivate(PyCallArgs &call) {
    return FactoryDB::AssemblyLinesSelectPrivate(call.client->GetCharacterID());
}

PyResult RamProxyService::AssemblyLinesSelectCorp(PyCallArgs &call) {
    return FactoryDB::AssemblyLinesSelectCorporation(call.client->GetCorporationID());
}

PyResult RamProxyService::AssemblyLinesSelectAlliance(PyCallArgs &call) {
    return FactoryDB::AssemblyLinesSelectAlliance(call.client->GetAllianceID());
}

PyResult RamProxyService::AssemblyLinesGet(PyCallArgs &call, PyInt* stationID) {
    return FactoryDB::AssemblyLinesGet(stationID->value());
}

PyResult RamProxyService::AssemblyLinesSelect(PyCallArgs &call, PyString* filter) {
    if (filter->content() == "region") {
        return FactoryDB::AssemblyLinesSelectPublic(call.client->GetRegionID());
    } else if (filter->content() == "char") {
        return FactoryDB::AssemblyLinesSelectPersonal(call.client->GetCharacterID());
    } else if (filter->content() == "corp") {
        return FactoryDB::AssemblyLinesSelectCorporation(call.client->GetCorporationID());
    } else if (filter->content() == "alliance") {
        return FactoryDB::AssemblyLinesSelectAlliance(call.client->GetAllianceID());
    }

    _log(SERVICE__ERROR, "Unknown filter '%s'.", filter->content().c_str());
    return nullptr;
}

PyResult RamProxyService::GetJobs2(PyCallArgs &call, PyInt* ownerID, PyBool* completed) {
    if (ownerID->value() == call.client->GetCorporationID())
        if ((call.client->GetCorpRole() & Corp::Role::FactoryManager) != Corp::Role::FactoryManager) {
            // what other roles (if any) can view corp factory jobs?
            call.client->SendInfoModalMsg("You cannot view your corporation's jobs because you are not a Factory Manager.");
            return nullptr;
        }

    return FactoryDB::GetJobs2(ownerID->value(), completed->value());
}

// name of a job's installation (station or structure) for wallet and calendar text
static std::string GetInstallationName(uint32 containerID)
{
    if (sDataMgr.IsStation(containerID))
        return stDataMgr.GetStationName(containerID);
    InventoryItemRef iRef = sItemFactory.GetItemRef(containerID);
    if (iRef.get() == nullptr)
        return "Unknown Location";
    if (!iRef->itemName().empty())
        return iRef->itemName();
    return iRef->type().name();
}

PyResult RamProxyService::InstallJob(PyCallArgs &call, PyRep* locationData, PyRep* itemLocationData, PyRep* bomLocationData, PyRep* flagOutput, PyRep* buildRuns, PyRep* activityID, PyRep* licensedProductionRuns, PyRep* ownerFlag, PyRep* blah) {
    //job = sm.ProxySvc('ramProxy').InstallJob(installationLocationData, installedItemLocationData, bomLocationData, flagOutput, quoteData.buildRuns, quoteData.activityID, quoteData.licensedProductionRuns, not quoteData.ownerFlag, 'blah', quoteOnly=1, installedItem=quoteData.blueprint, maxJobStartTime=quoteData.assemblyLine.nextFreeTime + 1 * MIN, inventionItems=quoteData.inventionItems, inventionOutputItemID=quoteData.inventionItems.outputType)

    _log(MANUF__DUMP, "RamProxyService::Handle_InstallJob() - size=%lli", call.tuple->size());
    call.Dump(MANUF__DUMP);

    Call_InstallJob args;
    if (!args.Decode(&call.tuple)) {
        codelog(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
        return nullptr;
    }

    _log(MANUF__INFO, "RamProxyService::Handle_InstallJob() - %s", sRamMthd.GetActivityName(args.activityID));

    Client* pClient(call.client);
    Character* pChar(pClient->GetChar().get());

    // check character job count - will throw if fail
    sRamMthd.JobsCheck(pChar, args);

    // load the installed item (blueprint)
    InventoryItemRef installedItem = sItemFactory.GetItemRef( args.bpItemID );
    if (installedItem.get() == nullptr) {
        /*  remote jobs on items in stations that are not loaded would need the installedItem KeyVal
         *  (typeID, itemID, locationID, ownerID, flagID, quantity, ...) to split stacks, verify access and containers.
         *  remote installation is not supported.  */
        _log(MANUF__ERROR, "%s(%u): installed item %u not found.", pClient->GetName(), pClient->GetCharacterID(), args.bpItemID);
        throw CustomError ("Remote Job Installation Not Functional at this time.");
    }
    if (installedItem->locationID() == locRAMItems)
        throw UserError ("RamBlueprintAlreadyInstalled");

    /** @todo  invention is not supported yet */
    if (args.activityID == EvERam::Activity::Invention)
        throw UserError ("RamActivityInvalid");

    // check assy line activity
    sRamMthd.ActivityCheck(pClient, args, installedItem);
    BlueprintRef bpRef = BlueprintRef::StaticCast(installedItem);

    // check permissions
    EvERam::LineRestrictions lineData = EvERam::LineRestrictions();
    sRamMthd.LinePermissionCheck(pClient, args, lineData);
    sRamMthd.ItemOwnerCheck(pClient, args, installedItem);
    sRamMthd.ItemLocationCheck(pClient, args, installedItem);

    uint32 ownerID(args.isCorpJob ? pClient->GetCorporationID() : pClient->GetCharacterID());

    // if output flag not set, put it where it came from
    if (args.outputFlag == flagNone)
        args.outputFlag = installedItem->flag();

    /* decode the bill of materials location and decide where the results go
     *  personal jobs:  [[locationID, charID, flag]]  or [[flagNone]] when no materials are needed
     *  corp jobs in stations:  [[stationID, ownerStation, 0], [officeFolderID, corpID, flagHangar], [officeID, corpID, flag]]
     */
    PathElement bomLocPath;
    bomLocPath.locationID = 0;
    bomLocPath.ownerID = ownerID;
    bomLocPath.flagID = flagNone;
    uint32 outputLocationID(args.lineContainerID);
    if (args.isCorpJob) {
        uint32 stationID(args.lineContainerID);
        uint32 officeID(stDataMgr.GetOfficeIDForCorp(stationID, ownerID));
        if (officeID == 0)
            throw UserError ("RamCorpBOMItemNoSuchOffice")
                    .AddLocationName ("location", stationID);

        // check bp location access
        sRamMthd.HangarRolesCheck(pClient, stationID, installedItem->flag());

        // bp passed..check bomData
        if (args.bomLocPath->size() == 3) {
            CorpPathElement bomPath;
            if (!bomPath.Decode(args.bomLocPath)) {
                _log(SERVICE__ERROR, "Failed to decode Corp BOM path.");
                return nullptr;
            }
            if ((uint32)bomPath.officeID != officeID)
                throw UserError ("RamCorpBOMItemNoSuchOffice")
                        .AddLocationName ("location", stationID);
            if (!IsHangarFlag(bomPath.officeFlagID))
                throw UserError ("RamInstalledItemBadLocation");
            sRamMthd.HangarRolesCheck(pClient, stationID, bomPath.officeFlagID);
            bomLocPath.locationID   = officeID;
            bomLocPath.flagID       = bomPath.officeFlagID;
        }

        outputLocationID = officeID;
        if (!IsHangarFlag(args.outputFlag))
            args.outputFlag = flagHangar;
    } else {
        // get path data for player
        if (args.bomLocPath->size() > 0) {
            PyRep* pRep(args.bomLocPath->GetItem(args.bomLocPath->size() - 1));
            if ((pRep != nullptr) and pRep->IsList() and (pRep->AsList()->size() == 3)) {
                PathElement bomPath;
                if (!bomPath.Decode(pRep)) {
                    _log(SERVICE__ERROR, "Failed to decode BOM path.");
                    return nullptr;
                }
                bomLocPath.locationID   = bomPath.locationID;
                bomLocPath.flagID       = bomPath.flagID;
                // the client sends the installation as the location of materials in the cargo of the active ship
                if ((bomLocPath.flagID == flagCargoHold) and (bomLocPath.locationID != (int32)pClient->GetShipID()))
                    bomLocPath.locationID = pClient->GetShipID();
                // materials must be in the installation or the active ship
                if ((bomLocPath.locationID != args.lineContainerID) and (bomLocPath.locationID != (int32)pClient->GetShipID()))
                    throw UserError ("RamInstalledItemBadLocation");
            }
        }

        // results go to the item hangar of the station, or into the ship
        if (sDataMgr.IsStation(args.lineContainerID) and (args.outputFlag != flagHangar))
            args.outputFlag = flagHangar;
    }

    // calculate time and cost
    //   Rsp_InstallJob is used as container while building response to job quote.
    Rsp_InstallJob rsp;
    if (!sRamMthd.Calculate(args, installedItem, pChar, rsp)){
        _log(MANUF__ERROR, "Could not Calculate() on %s for %s(%u)", installedItem->name(), pClient->GetName(), pClient->GetCharacterID());
        return nullptr;
    }

    // sent as assy line.nextFreeTime + 1m  (a previous call asks for assy line nextFreeTime, displayed in window)
    //  the time slot was taken by another job while this one was being installed
    if (call.byname.find("maxJobStartTime") != call.byname.end())
        if (rsp.maxJobStartTime > PyRep::IntegerValue(call.byname["maxJobStartTime"]))
            throw UserError ("RamCannotGuaranteeStartTime");

    // get required items for activity
    std::vector<EvERam::RequiredItem> reqItems;
    sDataMgr.GetRamRequiredItems(installedItem->typeID(), (int8)args.activityID, reqItems);

    // quoteOnly is sent for all jobs before installation to approve price and timeframe
    if (PyRep::IntegerValueU32(call.byname["quoteOnly"])) {
        _log(MANUF__INFO, "quoteOnly = true");
        sRamMthd.EncodeBillOfMaterials(reqItems, rsp.materialMultiplier, rsp.charMaterialMultiplier, args.runs, rsp.bom);
        sRamMthd.EncodeMissingMaterials(reqItems, bomLocPath, ownerID, pClient, rsp.materialMultiplier, rsp.charMaterialMultiplier, args.runs, rsp.missingMaterials);

        // this value is halved in client code. (removed in client update patch 5Nov20)
        //rsp.charTimeMultiplier *= 2;
        return rsp.Encode();
    }

    // verify installer has skills and all needed materials are present in proper location
    sRamMthd.MaterialSkillsCheck(pClient, args.runs, bomLocPath, ownerID, rsp, reqItems);

    // at this point, it is a real job installation.  check everything else
    sRamMthd.ProductionTimeCheck(rsp.productionTime);

    // job location data
    uint32 locationID(args.lineContainerID), solarSystemID(pClient->GetSystemID());
    if (sDataMgr.IsStation(locationID))
        solarSystemID = sDataMgr.GetStationSystem(locationID);

    // the quote the client authorized must still be good.  the client's figure is never charged
    double cost(rsp.cost);
    std::map<std::string, PyRep*>::iterator acItr = call.byname.find("authorizedCost");
    if ((acItr != call.byname.end()) and (cost > PyRep::FloatValue(acItr->second) + 0.01))
        throw CustomError ("The installation price has changed.  Please request a new quote.");

    // pay for assembly lines...take the money, send wallet blink event record the transaction in journal.
    std::string locationName(GetInstallationName(locationID));
    std::string reason = "DESC: Installing ";
    reason += sRamMthd.GetActivityName(args.activityID);
    reason += " job in ";
    reason += locationName;
    if (args.isCorpJob) {
        reason += " by ";
        reason += pClient->GetName();
    }

    uint32 payeeID(lineData.ownerID);
    if ((cost > 0) and (payeeID != 0) and (payeeID != ownerID)) {
        if (args.isCorpJob) {
            // corp jobs are paid from the wallet division the installer is using
            int32 accountKey(pClient->GetCorpAccountKey());
            if ((accountKey < Account::KeyType::Cash) or (accountKey > Account::KeyType::Cash7))
                throw UserError ("RamNoCorpAccount");
            AccountService::TransferFunds(ownerID, payeeID, cost, reason.c_str(), Journal::EntryType::FactorySlotRentalFee,
                                          locationID, accountKey, Account::KeyType::Cash, pClient);
        } else {
            AccountService::TransferFunds(ownerID, payeeID, cost, reason.c_str(), Journal::EntryType::FactorySlotRentalFee,
                                          locationID, Account::KeyType::Cash, Account::KeyType::Cash, pClient);
        }
    }

    int64 beginTime(GetFileTimeNow());
    if (beginTime < rsp.maxJobStartTime)
        beginTime = rsp.maxJobStartTime;

    // register/save job to chosen assy line.
    //  (the container type is kept with the job so it can be shown after the container is gone)
    uint32 containerTypeID(0);
    InventoryItemRef cRef(sItemFactory.GetItemRef(locationID));
    if (cRef.get() != nullptr)
        containerTypeID = cRef->typeID();

    // split stacked blueprints so only one is installed
    if (installedItem->quantity() > 1) {
        InventoryItemRef iRef = installedItem->Split(1);
        if (iRef.get() == nullptr) {
            _log(MANUF__WARNING, "Failed to split %s for %s.", installedItem->name(), sRamMthd.GetActivityName(args.activityID));
            throw UserError ("RamActivityRequiresABlueprint");
        }
        installedItem = iRef;
        bpRef = BlueprintRef::StaticCast(installedItem);
        args.bpItemID = installedItem->itemID();
    }

    uint32 jobID = FactoryDB::InstallJob(ownerID, pClient->GetCharacterID(), args, beginTime, beginTime + rsp.productionTime * EvE::Time::Second,
                                         solarSystemID, outputLocationID, containerTypeID, installedItem->typeID());

    if (jobID < 1) {
        _log(MANUF__ERROR, "Could not InstallJob for %s using %s", pClient->GetName(), installedItem->name());
        // make client error here...
        return nullptr;
    }

    // unpackage bpo and move to factory
    installedItem->ChangeSingleton(true);
    installedItem->Move(locRAMItems, flagFactoryBlueprint, true);

    // take required items
    sRamMthd.ConsumeMaterials(args.runs, bomLocPath, ownerID, rsp, reqItems);

    // update runs and do other shit where applicable
    if ((args.activityID == EvERam::Activity::Manufacturing) and !bpRef->infinite())
        bpRef->UpdateRuns(-args.runs);

    if (sConfig.ram.AutoEvent) {
        std::string title;
        if (args.isCorpJob) {
            title += "Corporate ";
        } else {
            title += "Personal ";
        }
        title += sRamMthd.GetActivityName(args.activityID);
        title += " Job";

        std::string description;
        if (args.isCorpJob) {
            description += "The ";
        } else {
            description += "Your ";
        }
        description += sRamMthd.GetActivityName(args.activityID);
        description += " job in ";
        description += locationName;

        description += " is scheduled to complete at the time noted.<br><br>";
        switch (args.activityID) {
            case EvERam::Activity::Manufacturing: {
                description += "This job is for <color=red>";
                description += std::to_string(args.runs);
                description += "</color> runs of <color=yellow>";
                description += bpRef->productType().groupName();
                description += "</color>,<br>producing <color=green>";
                description += std::to_string(args.runs * bpRef->productType().portionSize());
                description += "</color> units of <color=green>";
                description += bpRef->productType().name();
                description += "</color>.";
            } break;
            case EvERam::Activity::ResearchMaterial: {
                description += "Upon completion, this job will increase the Material Efficiency of the ";
                description += "<color=yellow>";
                description += bpRef->itemName();
                description += "</color> from <color=red>";
                description += std::to_string(bpRef->mLevel());
                description += "</color> to <color=green>";
                description += std::to_string(bpRef->mLevel() + args.runs);
                description += "</color>.";
            } break;
            case EvERam::Activity::ResearchTime: {
                description += "Upon completion, this job will increase the Production Efficiency of the ";
                description += "<color=yellow>";
                description += bpRef->itemName();
                description += "</color> from <color=red>";
                description += std::to_string(bpRef->pLevel());
                description += "</color> to <color=green>";
                description += std::to_string(bpRef->pLevel() + args.runs);
                description += "</color>.";
            } break;
            case EvERam::Activity::Copying: {
                description += "<color=green>";
                description += std::to_string(args.runs);
                description += "</color> Copies of the ";
                description += "<color=yellow>";
                description += bpRef->itemName();
                description += "</color> will be made.";
            } break;
                // others are not used (or shouldnt be)
        }

        if (args.isCorpJob) {
            description += "<br><br>This ";
            description += sRamMthd.GetActivityName(args.activityID);
            description += " job was installed by ";
            description += pClient->GetName();
            description += " on ";
            description += currentDateTime();
            description += " CST.";
        }

        // random sayings/quotes here?  sure, why not?  lol
        if (IsEven(MakeRandomInt(0, 10)))
            description += "<br><br><br>And a good time shall be had by all!";
        //description += "Don't wish it was easier; instead, wish you were better.";
        //description += "Endure and survive.";
        //description += "Fly safe.";

        // should this be important?
        uint32 eventID = CalendarDB::SaveSystemEvent(ownerID, (payeeID ? payeeID : ownerID),
                                                     beginTime + rsp.productionTime * EvE::Time::Second,
                                                     Calendar::AutoEvent::RAMJob, title, description);

        FactoryDB::SetJobEventID(jobID, eventID);

        //force calendar reload (if corp job, update all online members, also)
        if (args.isCorpJob) {
            sEntityList.CorpNotify(ownerID, Notify::Types::FactoryJob, "OnReloadCalendar", "charid", new PyTuple(0));
        } else {
            pClient->SendNotification("OnReloadCalendar", "charid", new PyTuple(0), false);  // this is not sequenced
        }
    }

    // increment statistic counter
    sStatMgr.Increment(Stat::ramJobs);

    return nullptr;
}

PyResult RamProxyService::CompleteJob(PyCallArgs &call, PyRep* info, PyRep* jobID, PyRep* cancel) {
    /*
     * 23:35:54 [ManufDump] RamProxyService::Handle_CompleteJob() - size 3
     * 23:35:54 [ManufDump]   Call Arguments:
     * 23:35:54 [ManufDump]      Tuple: 3 elements
     * 23:35:54 [ManufDump]       [ 0]   List: 3 elements
     * 23:35:54 [ManufDump]       [ 0]   [ 0]   List: 2 elements
     * 23:35:54 [ManufDump]       [ 0]   [ 0]   [ 0]    Integer: 60014140   invLocation (station, ship, starbase)
     * 23:35:54 [ManufDump]       [ 0]   [ 0]   [ 1]    Integer: 15         invLocationGroup
     * 23:35:54 [ManufDump]       [ 0]   [ 1]   List: Empty                 path  [eve.session.locationid, eve.session.charid, const.flagHangar]
     * 23:35:54 [ManufDump]       [ 0]   [ 2]   List: 1 elements
     * 23:35:54 [ManufDump]       [ 0]   [ 2]   [ 0]    Integer: 60014140   containerID (actual itemID of location bp is in)
     * 23:35:54 [ManufDump]       [ 1]    Integer: 2                        jobID
     * 23:35:54 [ManufDump]       [ 2]    Boolean: false                    cancel
     */
    _log(MANUF__DUMP, "RamProxyService::Handle_CompleteJob() - size=%lli", call.tuple->size());
    call.Dump(MANUF__DUMP);

    Call_CompleteJob args;
    if (!args.Decode(&call.tuple)) {
        codelog(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
        return nullptr;
    }

    EvERam::JobProperties data = EvERam::JobProperties();
    if (!FactoryDB::GetJobProperties(args.jobID, data))
        throw UserError ("RamCompletionNoSuchJob");

    sRamMthd.VerifyCompleteJob(args, data, call.client);

    // results go to the location saved with the job (jobs from before F16 have none, they used the installation)
    uint32 outputLocationID(data.outputLocationID ? data.outputLocationID : args.containerID);
    InventoryItemRef installedItem = sItemFactory.GetItemRef(data.itemID);
    uint16 installedTypeID(installedItem.get() != nullptr ? installedItem->typeID() : data.installedItemTypeID);

    // does an aborted job return the installed item immediately or after time expiry?
    FactoryDB::CompleteJob(args.jobID, (args.cancel ? EvERam::Status::Abort : EvERam::Status::Delivered));

    // return materials which weren't consumed
    std::vector<EvERam::RequiredItem> reqItems;
    sDataMgr.GetRamReturns(installedTypeID, data.activity, reqItems);
    for (auto cur : reqItems) {
        uint32 quantity = (cur.quantity * data.jobRuns * (1 - cur.damagePerJob));
        if (quantity == 0)
            quantity = 1;

        ItemData idata(cur.typeID, data.ownerID, locTemp, flagNone, quantity);
        InventoryItemRef iRef = sItemFactory.SpawnItem( idata );
        if (iRef.get() != nullptr)
            iRef->Move(outputLocationID, data.outputFlag, true);
    }

    if (installedItem.get() == nullptr) {
        _log(MANUF__ERROR, "CompleteJob() - installed item %u of job %u not found.", data.itemID, args.jobID);
        CalendarDB::DeleteEvent(data.eventID);
        return PyStatic.NewNone();
    }

    if (args.cancel) {
        // a cancelled job returns the installed item.  materials are not refunded
        installedItem->Move(outputLocationID, data.outputFlag, true);

        // if job event in calendar, set to deleted for canceled job.
        CalendarDB::DeleteEvent(data.eventID);
        return PyStatic.NewNone();
    }

    BlueprintRef bpRef = BlueprintRef::StaticCast( installedItem );
    switch(data.activity) {
        case EvERam::Activity::Manufacturing: {
            ItemData idata(bpRef->productTypeID(), data.ownerID, locTemp, flagFactoryOutput, (bpRef->productType().portionSize() * data.jobRuns));
            InventoryItemRef iRef = sItemFactory.SpawnItem( idata );
            if (iRef.get() != nullptr)
                iRef->Move(outputLocationID, data.outputFlag, true);
            // a copy with no runs left is used up
            if (bpRef->copy() and (bpRef->runs() < 1)) {
                bpRef->Delete();
            } else {
                bpRef->Move(outputLocationID, data.outputFlag, true);
            }
        } break;
        case EvERam::Activity::ResearchTime: {
            bpRef->UpdatePLevel(data.jobRuns);
            bpRef->Move(outputLocationID, data.outputFlag, true);
        } break;
        case EvERam::Activity::ResearchMaterial: {
            bpRef->UpdateMLevel(data.jobRuns);
            bpRef->Move(outputLocationID, data.outputFlag, true);
        } break;
        case EvERam::Activity::Copying: {
            for (int16 i = 0; i < data.jobRuns; ++i)
                Blueprint::SpawnCopy(bpRef->typeID(), data.ownerID, outputLocationID, data.outputFlag, data.licensedRuns, bpRef->mLevel(), bpRef->pLevel());
            bpRef->Move(outputLocationID, data.outputFlag, true);
        } break;

        /* unsupported */
        case EvERam::Activity::Invention:
        case EvERam::Activity::ReverseEngineering:
        /* depreciated */
        case EvERam::Activity::ResearchTech:
        case EvERam::Activity::Duplicating:
        default: {
            _log(MANUF__WARNING, "Activity %u is currently unsupported.", data.activity);
            installedItem->Move(outputLocationID, data.outputFlag, true);
        } break;
    }

    // nothing for the client to show for these
    return PyStatic.NewNone();
}

PyResult RamProxyService::UpdateAssemblyLineConfigurations(PyCallArgs &call, PyRep* installationLocationData, PyRep* rowset) {
    _log(MANUF__DUMP, "RamProxyService::Handle_UpdateAssemblyLineConfigurations() - size=%lli", call.tuple->size());
    call.Dump(MANUF__DUMP);

    //RamConfigAssemblyLinesAccessDenied
    //RamConfigAssemblyLinesInsuficientAccess

    return nullptr;
}
