
 /**
  * @name RamMethods.cpp
  *   methods for R.A.M. activities
  *
  * @Author:         Allan
  * @date:          9Jan18
  */



#include "../eve-common/EVE_Character.h"
#include "Client.h"
#include "inventory/Inventory.h"
#include "manufacturing/Blueprint.h"
#include "manufacturing/RamMethods.h"
#include "map/MapData.h"
#include "market/sim/SimFacilityMgr.h"
#include "pos/PosMgrDB.h"
#include "pos/Structure.h"
#include "station/StationDataMgr.h"
#include "system/CalendarDB.h"
#include "system/SystemManager.h"

static const uint32 RAM_PRODUCTION_TIME_LIMIT = 60*60*24*30;   //30 days

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

/*
 *    NOT IMPLEMENTED EXCEPTIONS:
 ************************************
 *    RamRemoteInstalledItemImpounded             - impound of installedItem
 *    RamInstallJob_InstalledItemChanged          - some cache expiration??
 */


void RamMethods::ActivityCheck(Client* const pClient, const Call_InstallJob& args, InventoryItemRef iRef, bool structureLine/*false*/)
{
    if (iRef.get() == nullptr)
        throw UserError ("RamActivityRequiresABlueprint");

    // check validity of activity
    if ((args.activityID < EvERam::Activity::Manufacturing)
    or  (args.activityID > EvERam::Activity::Invention))
        throw UserError ("RamAssemblyLineHasNoActivity");

    if (args.activityID == EvERam::Activity::ReverseEngineering) {
        /* reverse engineering (experimental laboratories and caldari research outposts) takes an ancient relic.  the line's
         *  activity is checked with its permissions, and any reverse engineering line takes relics:  the client's data lists
         *  the category for the outpost's line only, not the experimental laboratory's */
        if (iRef->categoryID() != EVEDB::invCategories::AncientRelics)
            throw UserError ("RamActivityRequiresABlueprint");
        return;
    }

    // everything else needs a blueprint
    if (iRef->categoryID() != EVEDB::invCategories::Blueprint)
        throw UserError ("RamActivityRequiresABlueprint");

    BlueprintRef bpRef = BlueprintRef::StaticCast(iRef);
    const ItemType* pType(nullptr);
    switch(args.activityID) {
        case EvERam::Activity::Manufacturing: {
            if (args.runs < 1)
                throw UserError ("RamTooManyProductionRuns");
            if (!bpRef->infinite()
            and ((bpRef->runs() - args.runs) < 0))
                throw UserError ("RamTooManyProductionRuns");

            pType = &bpRef->productType();
        } break;
        case EvERam::Activity::ResearchMaterial: {
            // no cap before Crius, but the level is kept in a tinyint
            if (args.runs < 1)
                throw UserError ("RamActivityInvalid");
            if (bpRef->mLevel() + args.runs > INT8_MAX)
                throw CustomError ("Blueprints can be researched to level %i at most.", INT8_MAX);
            if (bpRef->copy())
                throw UserError ("RamCannotResearchABlueprintCopy");
            pType = &bpRef->type();
        } break;
        case EvERam::Activity::ResearchTime: {
            if (args.runs < 1)
                throw UserError ("RamActivityInvalid");
            if (bpRef->pLevel() + args.runs > INT8_MAX)
                throw CustomError ("Blueprints can be researched to level %i at most.", INT8_MAX);
            if (bpRef->copy())
                throw UserError ("RamCannotResearchABlueprintCopy");
            pType = &bpRef->type();
        } break;
        case EvERam::Activity::Copying: {
            if (bpRef->copy())
                throw UserError ("RamCannotCopyABlueprintCopy");
            if (args.copyRuns < 1)
                throw UserError ("RamCannotCopyZeroRuns");
            if ((uint32)args.copyRuns > bpRef->type().maxProductionLimit())
                throw UserError ("RamTooManyProductionRuns");
            pType = &bpRef->type();
        } break;
        case EvERam::Activity::Invention: {
            // the T1 copy is used up by the job.  the output is checked with the rest of the invention data
            if (!bpRef->copy() or bpRef->infinite())
                throw UserError ("RamCannotInventABlueprintOriginal");
            if (bpRef->runs() < 1)
                throw UserError ("RamCannotInventZeroRuns");
        } break;
        case EvERam::Activity::ResearchTech:    // cannot find any reference to this.  not used?
        case EvERam::Activity::Duplicating:     // ancient pre-apoc 'copy' activity.  no longer used.
        default: {
            // not supported
            sLog.Error("RAM::InstallJob()", "Unsupported Activity %u sent by %s(%u).", args.activityID, pClient->GetName(), pClient->GetCharacterID());
            throw UserError ("RamActivityInvalid");
        }
    }

    // invention outputs are not tied to the line's product lists
    if (args.activityID == EvERam::Activity::Invention)
        return;

    if (pType == nullptr)
        throw UserError ("RamNoKnownOutputType");

    // npc station data is incomplete, so stations are lenient.  structure lines only take what their type lists
    if (!FactoryDB::IsProducableBy(args.AssemblyLineID, pType, structureLine))
        throw UserError ("RamBadEndProductForActivity");
}

void RamMethods::JobsCheck(Character* pChar, const Call_InstallJob& args)
{
    if (args.activityID == EvERam::Activity::Manufacturing) {
        uint32 jobCount = FactoryDB::CountManufacturingJobs(pChar->itemID());
        uint charMaxJobs = 1+ pChar->GetSkillLevel(EvESkill::MassProduction)
                            + pChar->GetSkillLevel(EvESkill::AdvancedMassProduction);

        if (charMaxJobs <= jobCount)
            throw UserError ("MaxFactorySlotUsageReached")
                    .AddAmount ("current", jobCount)
                    .AddAmount ("max", charMaxJobs);
    } else {
        uint charMaxJobs = 1+ pChar->GetSkillLevel(EvESkill::LaboratoryOperation)
                            + pChar->GetSkillLevel(EvESkill::AdvancedLaboratoryOperation);

        uint32 jobCount = FactoryDB::CountResearchJobs(pChar->itemID());
        if (charMaxJobs <= jobCount)
            throw UserError ("MaxResearchFacilitySlotUsageReached")
                    .AddAmount ("current", jobCount)
                    .AddAmount ("max", charMaxJobs);
    }
}

void RamMethods::InstallationCheck(Client*const pClient, int32 lineLocationID, int32 activityID)
{
    // station lines only.  POS arrays and labs must be in this system (StructureCheck)
    if (!sDataMgr.IsStation(lineLocationID))
        return;
    if ((uint32)lineLocationID == pClient->GetStationID())
        return;

    uint32 regionID = sDataMgr.GetStationRegion(lineLocationID);
    if (!IsRegionID(regionID))
        throw UserError ("RamIsNotAnInstallation");
    if (pClient->GetRegionID() != regionID)
        throw UserError ("RamRangeLimitationRegion");

    /* jobs in another station:  Supply Chain Management (manufacturing) or Scientific Networking (the rest).  Crucible:
     *  without the skill, the station you are in;  level 1 the solar system;  2 within 5 jumps;  3 10;  4 20;  5 the region */
    bool manuf(activityID == EvERam::Activity::Manufacturing);
    uint16 skillID(manuf ? EvESkill::SupplyChainManagement : EvESkill::ScientificNetworking);
    uint8 level(pClient->GetChar()->GetSkillLevel(skillID));
    if (level >= 5)
        return;
    const char* skillName(manuf ? "Supply Chain Management" : "Scientific Networking");
    if (level < 1)
        throw CustomError ("You must be docked in that station to install a job there.  %s lets you install jobs remotely.", skillName);

    static const int32 jumpsPerLevel[5] = { -1, 0, 5, 10, 20 };
    uint32 systemID(sDataMgr.GetStationSystem(lineLocationID));
    int32 jumps(sMapData.GetJumpCount(pClient->GetSystemID(), systemID, (uint8)jumpsPerLevel[level]));
    if ((jumps < 0) or (jumps > jumpsPerLevel[level])) {
        if (level == 1)
            throw CustomError ("That installation is not in this solar system.  %s %u lets you install jobs in this system only.", skillName, level);
        throw CustomError ("That installation is too far away.  %s %u lets you install jobs within %i jumps.", skillName, level, jumpsPerLevel[level]);
    }
}

void RamMethods::LinePermissionCheck(Client*const pClient, const Call_InstallJob& args, EvERam::LineRestrictions& data)
{
    // get properties
    if (!FactoryDB::GetAssemblyLineRestrictions(args.AssemblyLineID, data))
        throw UserError ("RamInstallationHasNoDefaultContent");

    // the line must be in the installation the client sent
    if (data.containerID != (uint32)args.lineContainerID)
        throw UserError ("RamInstallationIDUnexpected");

    // check validity of activity
    if ((data.activityID < EvERam::Activity::Manufacturing)
    or  (data.activityID > EvERam::Activity::Invention))
        throw UserError ("RamAssemblyLineHasNoActivity");
    if (data.activityID != args.activityID)
        throw UserError ("RamActivityInvalid");

    uint32 corpID(pClient->GetCorporationID());
    // verify corp roles.  directors hold every role
    if (args.isCorpJob) {
        int64 roles(pClient->GetCorpRole());
        if ((roles & Corp::Role::Director) != Corp::Role::Director) {
            if ((roles & Corp::Role::FactoryManager) != Corp::Role::FactoryManager)
                throw UserError ("RamCannotInstallForCorpByRoleFactoryManager");
            // renting a line of another owner needs the slot rental role
            if (data.ownerID != corpID) {
                if (args.activityID == EvERam::Activity::Manufacturing) {
                    if ((roles & Corp::Role::CanRentFactorySlot) != Corp::Role::CanRentFactorySlot)
                        throw UserError ("RamCannotInstallWithoutRentFactorySlot");
                } else {
                    if ((roles & Corp::Role::CanRentResearchSlot) != Corp::Role::CanRentResearchSlot)
                        throw UserError ("RamCannotInstallWithoutRentResearchSlot");
                }
            }
        }
    }

    // labs of other corps can't be used for copying
    if ((args.activityID == EvERam::Activity::Copying) and (data.ownerID != corpID) and IsStructureLine(data.containerID))
        throw UserError ("RamCannotCopyInPlayerLab");

    // check usage restriction.  corp and alliance restrictions allow either group
    uint8 groupMask(data.rMask & (EvERam::RestrictionMask::ByCorp | EvERam::RestrictionMask::ByAlliance));
    if (groupMask) {
        bool allowed(false);
        if ((groupMask & EvERam::RestrictionMask::ByCorp) and (data.ownerID == corpID))
            allowed = true;
        if (!allowed and (groupMask & EvERam::RestrictionMask::ByAlliance)) {
            uint32 allyID(pClient->GetAllianceID());
            if ((allyID != 0) and (data.ownerID != 0)
            and ((data.ownerID == corpID) or (PosMgrDB::GetCorpAllianceID(data.ownerID) == allyID)))
                allowed = true;
        }
        if (!allowed)
            throw UserError ((groupMask & EvERam::RestrictionMask::ByCorp) ? "RamAccessDeniedWrongCorp" : "RamAccessDeniedWrongAlliance");
    }

    // check standing
    if (data.rMask & EvERam::RestrictionMask::ByStanding) {
        // get standings
        if (args.isCorpJob) {
            if (data.minStanding > StandingDB::GetStanding(data.ownerID, corpID))
                throw UserError ("RamAccessDeniedCorpStandingTooLow");
        } else {
            if (data.minStanding > pClient->GetChar()->GetStandingModified(data.ownerID))
                throw UserError ("RamAccessDeniedStandingTooLow");
        }
    }

    // check security rating
    if (data.rMask & EvERam::RestrictionMask::BySecurity) {
        if (args.isCorpJob) {
            if (data.minCorpSec > pClient->GetChar()->corpSecRating())
                throw UserError ("RamAccessDeniedCorpSecStatusTooLow");
            if (data.maxCorpSec < pClient->GetChar()->corpSecRating())
                throw UserError ("RamAccessDeniedCorpSecStatusTooHigh");
        } else {
            if (data.minCharSec > pClient->GetSecurityRating())
                throw UserError ("RamAccessDeniedSecStatusTooLow");
            if (data.maxCharSec < pClient->GetSecurityRating())
                throw UserError ("RamAccessDeniedSecStatusTooHigh");
        }
    }
}

void RamMethods::ItemOwnerCheck(Client*const pClient, const Call_InstallJob& args, InventoryItemRef iRef)
{
    // ownership
    if (args.isCorpJob) {
        if (iRef->ownerID() != pClient->GetCorporationID())
            throw UserError ("RamCannotInstallItemForAnotherCorp");
    } else {
        // this one is checked in client
        if (iRef->ownerID() != pClient->GetCharacterID())
            throw UserError ("RamCannotInstallItemForAnother");
    }
}

void RamMethods::ItemLocationCheck(Client*const pClient, const Call_InstallJob& args, InventoryItemRef installedItem)
{
    // a lot of this is checked in client.  need to verify
    if (sDataMgr.IsStation(args.lineContainerID)) {
        uint32 stationID(args.lineContainerID);
        if (args.isCorpJob) {
            // corp items are on a hangar floor of the corp's office in this station
            uint32 officeID(stDataMgr.GetOfficeIDForCorp(stationID, pClient->GetCorporationID()));
            if (officeID == 0)
                throw UserError ("RamCorpBOMItemNoSuchOffice")
                        .AddLocationName ("location", stationID);
            if ((installedItem->locationID() != officeID) or !IsHangarFlag(installedItem->flag())) {
                if (stationID == pClient->GetLocationID()) {
                    throw UserError ("RamCorpInstalledItemWrongLocation")
                            .AddLocationName ("location", stationID);
                } else {
                    throw UserError ("RamRemoteInstalledItemNotInOffice");
                }
            }
        } else if (installedItem->locationID() == stationID) {
            if (installedItem->flag() != flagHangar) {
                if (stationID == pClient->GetLocationID()) {
                    throw UserError ("RamInstalledItemWrongLocation")
                            .AddLocationName ("location", stationID);
                } else {
                    throw UserError ("RamRemoteInstalledItemInStationNotHangar");
                }
            }
        } else if ((installedItem->locationID() == pClient->GetShipID())
               and (pClient->GetStationID() == stationID)
               and (installedItem->flag() == flagCargoHold)) {
            // the client offers items in the cargo of the ship docked here
        } else if (stationID == pClient->GetLocationID()) {
            throw UserError ("RamInstalledItemWrongLocation")
                    .AddLocationName ("location", stationID);
        } else {
            throw UserError ("RamRemoteInstalledItemNotInStation");
        }
    } else if (args.lineContainerID == pClient->GetShipID()) {
        if (pClient->GetChar()->flag() != flagPilot)
            throw UserError ("RamAccessDeniedNotPilot");

        if (args.isCorpJob and (installedItem->flag() == flagCargoHold))
            throw UserError ("RamCorpInstalledItemNotInCargo");

        if (installedItem->locationID() != (uint32)args.lineContainerID)
            throw UserError ("RamInstalledItemMustBeInShip");
    } else if (IsStructureLine(args.lineContainerID)) {
        // POS jobs are corp jobs on items in a hangar division of the array or lab
        if (!args.isCorpJob) {
            InventoryItemRef sRef(sItemFactory.GetItemRef(args.lineContainerID));
            throw UserError ("RamInstalledItemBadLocationStructure")
                    .AddFormatValue ("type", new PyInt (sRef.get() == nullptr ? 0 : sRef->typeID()));
        }
        if (installedItem->locationID() != (uint32)args.lineContainerID)
            throw UserError ("RamInstalledItemMustBeInInstallation");
        if (!IsHangarFlag(installedItem->flag()))
            throw UserError ("RamInstalledItemInStructureNotInContainer");
    } else {
        // outposts are stations.  anything else is not an installation
        throw UserError ("RamIsNotAnInstallation");
    }
}

bool RamMethods::IsStructureLine(uint32 containerID)
{
    if ((containerID == 0) or sDataMgr.IsStation(containerID))
        return false;
    InventoryItemRef iRef(sItemFactory.GetItemRef(containerID));
    if (iRef.get() == nullptr)
        return false;
    return ((iRef->groupID() == EVEDB::invGroups::Assembly_Array)
         or (iRef->groupID() == EVEDB::invGroups::Mobile_Laboratory));
}

void RamMethods::StructureCheck(Client* const pClient, uint32 containerID)
{
    InventoryItemRef sRef(sItemFactory.GetItemRef(containerID));
    if (sRef.get() == nullptr)
        throw UserError ("RamNoSuchInstallation");
    if (!IsSolarSystemID(sRef->locationID()))
        throw UserError ("RamStructureNotInSpace")
                .AddFormatValue ("type", new PyInt (sRef->typeID()));
    // no remote jobs at structures
    if (sRef->locationID() != pClient->GetSystemID())
        throw UserError ("RamCannotContactFacility");

    // the structure must be online.  its system is loaded, as the client is in it
    bool online(false);
    SystemManager* pSysMgr(pClient->SystemMgr());
    SystemEntity* pSE(pSysMgr == nullptr ? nullptr : pSysMgr->GetSE(containerID));
    if ((pSE != nullptr) and pSE->IsPOSSE()) {
        online = pSE->GetPOSSE()->IsOnline();
    } else {
        int8 state(PosMgrDB::GetStructureState(containerID));
        online = ((state == EVEPOS::StructureState::Online) or (state == EVEPOS::StructureState::Operating)
               or (state == EVEPOS::StructureState::Vulnerable) or (state == EVEPOS::StructureState::Invulnerable));
    }
    if (!online)
        throw UserError ("RamCannotContactFacility");
}

void RamMethods::TerminateStructureJobs(uint32 containerID, int8 status)
{
    std::vector<EvERam::JobProperties> jobs;
    FactoryDB::GetActiveJobsInContainer(containerID, jobs);
    for (auto cur : jobs) {
        InventoryItemRef iRef(sItemFactory.GetItemRef(cur.itemID));
        if (status == EvERam::Status::Unanchor) {
            // the installed item and whatever the job did not use up go back into the structure
            uint16 typeID(iRef.get() != nullptr ? iRef->typeID() : cur.installedItemTypeID);
            if (iRef.get() != nullptr)
                iRef->Move(containerID, flagHangar, false);
            std::vector<EvERam::RequiredItem> reqItems;
            sDataMgr.GetRamReturns(typeID, cur.activity, reqItems);
            for (auto item : reqItems) {
                uint32 quantity(GetReturnQuantity(item, cur.jobRuns));
                if (quantity == 0)
                    continue;
                ItemData idata(item.typeID, cur.ownerID, locTemp, flagNone, quantity);
                InventoryItemRef rRef = sItemFactory.SpawnItem( idata );
                if (rRef.get() != nullptr)
                    rRef->Move(containerID, flagHangar, false);
            }
        } else if (iRef.get() != nullptr) {
            // destroyed with the structure
            iRef->Delete();
        }

        FactoryDB::CompleteJob(cur.jobID, status);
        CalendarDB::DeleteEvent(cur.eventID);
        _log(MANUF__MESSAGE, "Job %u in structure %u ended with status %i.", cur.jobID, containerID, status);
    }
}

int64 RamMethods::GetLocationRoles(Client* const pClient, uint32 locationID)
{
    if ((pClient->GetCorpRole() & Corp::Role::Director) == Corp::Role::Director)
        return Corp::Role::Admin;
    return pClient->GetCorpRolesAtLocation(locationID);
}

void RamMethods::HangarRolesCheck(Client* const pClient, uint32 locationID, int16 flagID)
{
    int64 role(Corp::HangarTakeRole(flagID));
    if (role == 0)
        return;     // not a corp hangar division
    if ((GetLocationRoles(pClient, locationID) & role) != role)
        throw UserError ("RamAccessDeniedToBOMHangar");
}

Inventory* RamMethods::GetBOMInventory(const PathElement& bomLocation)
{
    if (bomLocation.locationID == 0)
        return nullptr;
    Inventory* pInv = sItemFactory.GetInventoryFromId(bomLocation.locationID);
    if (pInv == nullptr)
        return nullptr;
    if (!pInv->ContentsLoaded())
        pInv->LoadContents();
    return pInv;
}

bool RamMethods::HasJobInput(const PathElement& bomLocation, uint32 ownerID, uint16 typeID, uint32 qty/*1*/)
{
    if (typeID == 0)
        return true;
    Inventory* pInv(GetBOMInventory(bomLocation));
    if (pInv == nullptr)
        return false;
    return (pInv->GetTypeQtyForOwner(typeID, (EVEItemFlags)bomLocation.flagID, ownerID) >= qty);
}

void RamMethods::TakeJobInput(const PathElement& bomLocation, uint32 ownerID, uint16 typeID, uint32 qty/*1*/)
{
    if (typeID == 0)
        return;
    std::vector<InventoryItemRef> taken;
    Inventory* pInv(GetBOMInventory(bomLocation));
    if ((pInv == nullptr) or !pInv->TakeTypeQtyForOwner(typeID, (EVEItemFlags)bomLocation.flagID, ownerID, qty, taken))
        throw UserError ("RamNeedMoreForJob")
                .AddFormatValue ("item", new PyInt (typeID));
    for (auto iRef : taken)
        iRef->Delete();
}

int32 RamMethods::GetKeyValInt(PyRep* kv, const char* key)
{
    if ((kv == nullptr) or kv->IsNone())
        return 0;
    PyRep* pArgs(kv);
    if (kv->IsObject())
        pArgs = kv->AsObject()->arguments();
    if ((pArgs == nullptr) or !pArgs->IsDict())
        return 0;
    PyRep* pRep(pArgs->AsDict()->GetItemString(key));
    if ((pRep == nullptr) or pRep->IsNone())
        return 0;
    return (int32)PyRep::IntegerValue(pRep);
}

void RamMethods::GetInventionOutputs(BlueprintRef t1Ref, std::vector<uint16>& bpTypeIDs)
{
    // the client lists the blueprints of the product's variations with meta level 5
    std::vector<uint16> children;
    sDataMgr.GetMetaChildren(t1Ref->productTypeID(), 0, children);
    for (auto cur : children) {
        const ItemType* pType(sItemFactory.GetType(cur));
        if (pType == nullptr)
            continue;
        if (pType->GetAttribute(AttrMetaLevel).get_int() != 5)
            continue;
        uint16 bpTypeID(sDataMgr.GetBlueprintTypeForProduct(cur));
        if (bpTypeID != 0)
            bpTypeIDs.push_back(bpTypeID);
    }
}

void RamMethods::GetInventionData(Client* const pClient, std::map<std::string, PyRep*>& byname, BlueprintRef t1Ref,
                                  const std::vector< EvERam::RequiredItem >& reqItems, EvERam::InventionData& into)
{
    // inventionItems = util.KeyVal(baseItemType, decryptorType, outputType), inventionOutputItemID = outputType
    PyRep* pItems(nullptr);
    std::map<std::string, PyRep*>::iterator itr = byname.find("inventionItems");
    if (itr != byname.end())
        pItems = itr->second;
    uint16 outputBpTypeID(GetKeyValInt(pItems, "outputType"));
    if (outputBpTypeID == 0) {
        itr = byname.find("inventionOutputItemID");
        if ((itr != byname.end()) and !itr->second->IsNone())
            outputBpTypeID = PyRep::IntegerValueU32(itr->second);
    }
    into.baseItemTypeID = GetKeyValInt(pItems, "baseItemType");
    into.decryptorTypeID = GetKeyValInt(pItems, "decryptorType");

    // output
    std::vector<uint16> outputs;
    GetInventionOutputs(t1Ref, outputs);
    if (outputs.empty())
        throw UserError ("RamInventionNoOutput");
    if ((outputBpTypeID == 0) and (outputs.size() == 1))
        outputBpTypeID = outputs.front();
    if (std::find(outputs.begin(), outputs.end(), outputBpTypeID) == outputs.end())
        throw UserError ("RamInventionNoOutput");

    // the base chance is the T2 blueprint's chanceOfRE (40% modules/ammo, 30% frigates..., 0 = not inventable)
    EvERam::bpTypeData outData = EvERam::bpTypeData();
    sDataMgr.GetBpTypeData(outputBpTypeID, outData);
    if (outData.chanceOfRE <= 0)
        throw UserError ("ProductionInventionFailure")
                .AddFormatValue ("type", new PyInt (outputBpTypeID));
    into.outputBpTypeID = outputBpTypeID;

    // optional base item:  the T1 product or one of its meta 1-4 variations
    into.metaLevel = 0;
    if (into.baseItemTypeID != 0) {
        const ItemType* pType(sItemFactory.GetType(into.baseItemTypeID));
        if (pType == nullptr)
            throw UserError ("RamInventionSourceMetaLevelTooHigh")
                    .AddFormatValue ("item", new PyInt (into.baseItemTypeID));
        int32 metaLevel(pType->GetAttribute(AttrMetaLevel).get_int());
        if (((into.baseItemTypeID != t1Ref->productTypeID()) and (sDataMgr.GetMetaParent(into.baseItemTypeID) != t1Ref->productTypeID()))
        or (metaLevel > 4))
            throw UserError ("RamInventionSourceMetaLevelTooHigh")
                    .AddFormatValue ("item", new PyInt (into.baseItemTypeID));
        into.metaLevel = (metaLevel < 0 ? 0 : metaLevel);
    }

    // data interface (encryption skill, decryptor group) and the two datacores (science skills)
    Character* pChar(pClient->GetChar().get());
    uint8 encLevel(0), dcLevel[2] = {0, 0}, dcCount(0);
    uint16 decryptorGroupID(0);
    for (auto cur : reqItems) {
        if (cur.isSkill)
            continue;
        const ItemType* pType(sItemFactory.GetType(cur.typeID));
        if (pType == nullptr)
            continue;
        if (pType->groupID() == EVEDB::invGroups::Data_Interfaces) {
            encLevel = pChar->GetSkillLevel(pType->GetAttribute(AttrRequiredSkill1).get_uint32());
            decryptorGroupID = pType->GetAttribute(AttrDecryptorID).get_uint32();
        } else if ((pType->groupID() == EVEDB::invGroups::Datacores) and (dcCount < 2)) {
            dcLevel[dcCount++] = pChar->GetSkillLevel(pType->GetAttribute(AttrRequiredSkill1).get_uint32());
        }
    }

    // optional decryptor of the group the data interface takes
    float decChance(1.0f);
    int16 decRuns(0);
    int8 decME(0), decPE(0);
    if (into.decryptorTypeID != 0) {
        const ItemType* pType(sItemFactory.GetType(into.decryptorTypeID));
        if ((pType == nullptr) or (pType->groupID() != decryptorGroupID))
            throw UserError ("RamInventionWrongDecryptor")
                    .AddFormatValue ("groupName", new PyString (decryptorGroupID ? sDataMgr.GetGroupName(decryptorGroupID) : "Decryptor"));
        if (pType->HasAttribute(AttrInventionPropabilityMultiplier))
            decChance = pType->GetAttribute(AttrInventionPropabilityMultiplier).get_float();
        decME = pType->GetAttribute(AttrInventionMEModifier).get_int();
        decPE = pType->GetAttribute(AttrInventionPEModifier).get_int();
        decRuns = pType->GetAttribute(AttrInventionMaxRunModifier).get_int();
    }

    float chance(EvEMath::RAM::InventionChance(outData.chanceOfRE, encLevel, dcLevel[0], dcLevel[1], into.metaLevel, decChance));
    chance *= sConfig.ram.InventChance;
    into.chance = EvE::min(EvE::max(chance, 0.0f), 1.0f);

    into.outputRuns = EvEMath::RAM::InventionRuns(t1Ref->runs(), t1Ref->type().maxProductionLimit(), outData.maxProductionLimit, decRuns);
    into.outputME = EvERam::BaseInventionME + decME;
    into.outputPE = EvERam::BaseInventionPE + decPE;

    _log(MANUF__INFO, "Invention of %u from %s: chance %.3f (base %.2f, enc %u, dc %u/%u, meta %i, decryptor %.2f), %i runs, ME %i, PE %i",
         outputBpTypeID, t1Ref->name(), into.chance, outData.chanceOfRE, encLevel, dcLevel[0], dcLevel[1], into.metaLevel, decChance,
         into.outputRuns, into.outputME, into.outputPE);
}

void RamMethods::GetReverseEngineeringData(Client* const pClient, std::map<std::string, PyRep*>& byname, InventoryItemRef relicRef,
                                           const std::vector< EvERam::RequiredItem >& reqItems, EvERam::InventionData& into)
{
    // inventionItems = util.KeyVal(decryptorType, outputType = decryptorType).  the hybrid decryptor is required
    PyRep* pItems(nullptr);
    std::map<std::string, PyRep*>::iterator itr = byname.find("inventionItems");
    if (itr != byname.end())
        pItems = itr->second;
    into.decryptorTypeID = GetKeyValInt(pItems, "decryptorType");
    const ItemType* pDecryptor(into.decryptorTypeID ? sItemFactory.GetType(into.decryptorTypeID) : nullptr);
    if ((pDecryptor == nullptr) or (pDecryptor->groupID() != EVEDB::invGroups::Hybrid_Decryptors))
        throw UserError ("RamRevEngNoDecryptors");
    into.baseItemTypeID = 0;
    into.metaLevel = 0;

    // the base chance is the relic's chanceOfRE (intact 40%, malfunctioning 30%, wrecked 20%)
    EvERam::bpTypeData relicData = EvERam::bpTypeData();
    sDataMgr.GetBpTypeData(relicRef->typeID(), relicData);
    if (relicData.chanceOfRE <= 0)
        throw UserError ("ProductionInventionFailure")
                .AddFormatValue ("type", new PyInt (relicRef->typeID()));

    // the decryptor's race decides the race of the T3 blueprint
    into.outputBpTypeID = PickReverseEngineeringOutput(relicRef->groupID(), pDecryptor->race());
    if (into.outputBpTypeID == 0)
        throw UserError ("RamNoKnownOutputType");

    // datacore skills
    Character* pChar(pClient->GetChar().get());
    uint8 dcLevel[2] = {0, 0}, dcCount(0);
    for (auto cur : reqItems) {
        if (cur.isSkill or (dcCount > 1))
            continue;
        const ItemType* pType(sItemFactory.GetType(cur.typeID));
        if ((pType != nullptr) and (pType->groupID() == EVEDB::invGroups::Datacores))
            dcLevel[dcCount++] = pChar->GetSkillLevel(pType->GetAttribute(AttrRequiredSkill1).get_uint32());
    }

    float chance(EvEMath::RAM::ReverseEngineeringChance(relicData.chanceOfRE, pChar->GetSkillLevel(EvESkill::ReverseEngineering), dcLevel[0], dcLevel[1]));
    chance *= sConfig.ram.ReChance;
    into.chance = EvE::min(EvE::max(chance, 0.0f), 1.0f);

    // intact relics give 20 runs, malfunctioning 10, wrecked 3
    if (relicData.chanceOfRE >= 0.4f) {
        into.outputRuns = 20;
    } else if (relicData.chanceOfRE >= 0.3f) {
        into.outputRuns = 10;
    } else {
        into.outputRuns = 3;
    }
    into.outputME = EvERam::BaseInventionME;
    into.outputPE = EvERam::BaseInventionPE;

    _log(MANUF__INFO, "Reverse engineering of %s into %u: chance %.3f (base %.2f, dc %u/%u), %i runs",
         relicRef->name(), into.outputBpTypeID, into.chance, relicData.chanceOfRE, dcLevel[0], dcLevel[1], into.outputRuns);
}

uint16 RamMethods::PickReverseEngineeringOutput(uint16 relicGroupID, uint8 raceID)
{
    // relic kind -> product group of the T3 blueprint
    uint16 productGroupID(0);
    switch (relicGroupID) {
        case 997:   productGroupID = EVEDB::invGroups::StrategicCruiser;    break;  // hull sections
        case 971:   productGroupID = 957;   break;  // thruster sections -> propulsion systems
        case 990:   productGroupID = 955;   break;  // electromechanical -> electronic systems
        case 991:   productGroupID = 956;   break;  // weapon subroutines -> offensive systems
        case 992:   productGroupID = 958;   break;  // power cores -> engineering systems
        case 993:   productGroupID = 954;   break;  // armor nanobots -> defensive systems
        default:
            return 0;
    }

    std::vector<uint16> bpTypes;
    sDataMgr.GetT3BlueprintsForProductGroup(productGroupID, raceID, bpTypes);
    if (bpTypes.empty())
        return 0;
    return bpTypes[MakeRandomInt(0, bpTypes.size() - 1)];
}

const char* RamMethods::GetInventionResultLabel(bool success, float chance)
{
    // flavour text only.  the better the chance, the more relaxed the text
    if (success) {
        if (chance >= 0.8f)
            return "UI/ScienceAndIndustry/Invention/InventionResultSuccessCouldDoInSleep";
        if (chance >= 0.6f)
            return "UI/ScienceAndIndustry/Invention/InventionResultSuccessSuitedToYourTalets";
        if (chance >= 0.45f)
            return "UI/ScienceAndIndustry/Invention/InventionResultSuccessDidntTaxTooMuch";
        if (chance >= 0.3f)
            return "UI/ScienceAndIndustry/Invention/InventionResultSuccessFarFromCertain";
        return "UI/ScienceAndIndustry/Invention/InventionResultSuccessCantCountOnIt";
    }
    if (chance >= 0.8f)
        return "UI/ScienceAndIndustry/Invention/InventionResultFailedSuccessAtFingertips";
    if (chance >= 0.6f)
        return "UI/ScienceAndIndustry/Invention/InventionResultFailedFeltGoodAboutIt";
    if (chance >= 0.45f)
        return "UI/ScienceAndIndustry/Invention/InventionResultFailedRequiresAFewTries";
    if (chance >= 0.3f)
        return "UI/ScienceAndIndustry/Invention/InventionResultFailedRequiresHardWork";
    if (chance >= 0.15f)
        return "UI/ScienceAndIndustry/Invention/InventionResultFailedRequiresLuck";
    if (chance >= 0.05f)
        return "UI/ScienceAndIndustry/Invention/InventionResultFailedNeverClose";
    return "UI/ScienceAndIndustry/Invention/InventionResultFailedImpossible";
}

void RamMethods::SetMaterialWaste(float wasteFactor, std::vector<EvERam::RequiredItem>& reqItems)
{
    // the blueprint's waste (Blueprint::GetME()) is on the base materials only, a run's rounded on its own
    for (auto& cur : reqItems)
        if (!cur.isSkill and !cur.extra)
            cur.waste = (uint32)round(cur.quantity * wasteFactor);
}

uint32 RamMethods::GetBaseQuantity(const EvERam::RequiredItem& item, float materialMultiplier, uint32 runs)
{
    // a run's materials with the blueprint's waste, times the assembly line's multiplier
    if (item.isSkill)
        return 0;
    return (uint32)round((item.quantity + item.waste) * materialMultiplier) * runs;
}

uint32 RamMethods::GetMaterialQuantity(const EvERam::RequiredItem& item, float materialMultiplier, float charMaterialMultiplier, uint32 runs)
{
    /* pre-Crius:  extra materials (the T1 item, components, R.A.M., research items) only take the line's multiplier.  base
     *  materials also take the blueprint's waste and the character's (manufactureCostMultiplier:  1.25, down to 1.0 at
     *  Production Efficiency 5), each rounded on its own */
    uint32 qty(GetBaseQuantity(item, materialMultiplier, runs));
    if (!item.isSkill and !item.extra and (charMaterialMultiplier > 1.0f))
        qty += (uint32)round(item.quantity * (charMaterialMultiplier - 1.0f)) * runs;
    return qty;
}

uint32 RamMethods::GetReturnQuantity(const EvERam::RequiredItem& item, uint32 runs)
{
    /* the job uses up ceil(quantity * runs * damage) units (a 1-run job with a 10% R.A.M. uses the whole unit), the rest comes back.
     *  the small offset keeps float error (0.1f * 10 = 1.0000001) from using up an extra unit */
    double total((double)item.quantity * runs);
    double used(ceil(total * item.damagePerJob - 0.0001));
    if (used < 0)
        used = 0;
    if (used >= total)
        return 0;
    return (uint32)(total - used);
}

void RamMethods::MaterialSkillsCheck(Client* const pClient, uint32 runs, const PathElement& bomLocation, uint32 ownerID, const Rsp_InstallJob& rsp, const std::vector< EvERam::RequiredItem >& reqItems)
{
    // a type can be both a base and an extra material, so the check is on the sum
    std::map<uint16, uint32> needed;
    for (auto cur : reqItems) {
        if (cur.isSkill) { // check skill (quantity is required level)
            if (pClient->GetChar()->GetSkillLevel(cur.typeID) < cur.quantity) {
                throw UserError ("RamNeedSkillForJob")
                        .AddFormatValue ("skillID", new PyInt (cur.typeID))
                        .AddFormatValue ("skillLevel", new PyInt (cur.quantity));
            }
            continue;
        }

        needed[cur.typeID] += GetMaterialQuantity(cur, rsp.materialMultiplier, rsp.charMaterialMultiplier, runs);
    }

    Inventory* pInv(GetBOMInventory(bomLocation));
    for (auto cur : needed) {
        if (cur.second == 0)
            continue;
        uint32 qtyHave(pInv == nullptr ? 0 : pInv->GetTypeQtyForOwner(cur.first, (EVEItemFlags)bomLocation.flagID, ownerID));
        if (qtyHave < cur.second)
            throw UserError ("RamNeedMoreForJob")
                    .AddFormatValue ("item", new PyInt (cur.first));
    }
}

void RamMethods::ConsumeMaterials(uint32 runs, const PathElement& bomLocation, uint32 ownerID, const Rsp_InstallJob& rsp, const std::vector< EvERam::RequiredItem >& reqItems)
{
    Inventory* pInv(GetBOMInventory(bomLocation));
    for (auto cur : reqItems) {
        uint32 qtyNeeded(GetMaterialQuantity(cur, rsp.materialMultiplier, rsp.charMaterialMultiplier, runs));
        if (qtyNeeded == 0)
            continue;

        // items that are not used up (damage < 1) are taken now and returned on delivery (StaticDataMgr::GetRamReturns)
        std::vector<InventoryItemRef> taken;
        if ((pInv == nullptr) or !pInv->TakeTypeQtyForOwner(cur.typeID, (EVEItemFlags)bomLocation.flagID, ownerID, qtyNeeded, taken)) {
            _log(MANUF__ERROR, "RamMethods::ConsumeMaterials() - %u of type %u missing at %u:%u for owner %u.", qtyNeeded, cur.typeID, bomLocation.locationID, bomLocation.flagID, ownerID);
            throw UserError ("RamNeedMoreForJob")
                    .AddFormatValue ("item", new PyInt (cur.typeID));
        }
        for (auto iRef : taken)
            iRef->Delete();
    }
}

void RamMethods::ProductionTimeCheck(uint32 productionTime)
{
    if (productionTime > RAM_PRODUCTION_TIME_LIMIT)
        throw UserError ("RamProductionTimeExceedsLimits")
                .AddFormatValue ("productionTime", new PyInt (productionTime))
                .AddFormatValue ("limit", new PyInt (RAM_PRODUCTION_TIME_LIMIT));
}

void RamMethods::VerifyCompleteJob(const Call_CompleteJob &args, EvERam::JobProperties &data, Client* const pClient)
{
    // jobs in structures need the structure, in this solar system and online
    if ((data.containerID != 0) and !sDataMgr.IsStation(data.containerID) and (data.containerID != pClient->GetShipID())) {
        InventoryItemRef cRef(sItemFactory.GetItemRef(data.containerID));
        if (cRef.get() == nullptr)
            throw UserError ("RamNoSuchInstallation");
        if ((cRef->groupID() == EVEDB::invGroups::Assembly_Array)
        or  (cRef->groupID() == EVEDB::invGroups::Mobile_Laboratory))
            StructureCheck(pClient, data.containerID);
    }

    // this isnt entirely right....if job is installed in ship, receiver must be pilot in active ship to complete job.
    if (args.containerID == pClient->GetShipID())
        if (pClient->GetChar()->flag() != flagPilot)
            throw UserError ("RamCompletionMustBeInShip");

    if (IsPlayerCorp(data.ownerID)) {
        if (data.ownerID != (uint32)pClient->GetCorporationID())
            throw UserError ("RamCompletionAccessDenied");     // alliances not implemented
        int64 roles(pClient->GetCorpRole());
        if (((roles & Corp::Role::Director) != Corp::Role::Director)
        and ((roles & Corp::Role::FactoryManager) != Corp::Role::FactoryManager))
            throw UserError ("RamCompletionAccessDeniedByCorpRole");
    } else if (data.ownerID != (uint32)pClient->GetCharacterID()) {
        throw UserError ("RamCompletionAccessDenied");
    }

    if (data.status != EvERam::Status::InProgress)
        throw UserError ("RamCompletionJobCompleted");

    if (!args.cancel and ((data.pauseTime > 0) or (data.endTime > GetFileTimeNow())))
        throw UserError ("RamCompletionInProduction");
}

bool RamMethods::Calculate(const Call_InstallJob &args, InventoryItemRef iRef, Character* pChar, Rsp_InstallJob &into)
{
    // get line data
    if (!FactoryDB::GetAssemblyLineProperties(args.AssemblyLineID, pChar, into, args.isCorpJob))
        return false;

    // set char defaults.  the character's waste multiplier is only for manufacturing
    into.charTimeMultiplier = 1.0;
    into.charMaterialMultiplier = 1.0;

    const ItemType* pType(nullptr);
    BlueprintRef bpRef(nullptr);
    if (args.activityID == EvERam::Activity::ReverseEngineering) {
        // base time is the relic's researchTechTime (1 hour)
        pType = &iRef->type();
        EvERam::bpTypeData relicData = EvERam::bpTypeData();
        sDataMgr.GetBpTypeData(iRef->typeID(), relicData);
        FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
        into.productionTime = (relicData.researchTechTime ? relicData.researchTechTime : 3600) * into.timeMultiplier;
        into.productionTime *= sConfig.ram.ReTime;
    } else if (iRef->categoryID() == EVEDB::invCategories::Blueprint) {
        bpRef = BlueprintRef::StaticCast(iRef);
    } else {
        return false;
    }

    if (bpRef.get() != nullptr)
    switch(args.activityID) {
        case EvERam::Activity::Manufacturing: {
            into.charMaterialMultiplier = EvE::max(1.0f, pChar->GetAttribute (AttrManufactureCostMultiplier).get_float ());
            pType = &bpRef->productType();
            FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
            /* materialMultiplier is the line's.  the blueprint's waste goes on each base material (SetMaterialWaste).  Production
             *  Efficiency lowers the character's manufactureCostMultiplier (1.25 base, -4% a level, dogma effect 429), which is
             *  charMaterialMultiplier, the skill waste on the base quantity apart from the blueprint's
             *  (infosvc bill of materials:  base + base * wastageFactor, then + base * charMult - base)
             */
            into.materialMultiplier *= sConfig.ram.MatMod;
            // manufactureTimeMultiplier:  Industry takes 4% a level off, Beancounter implants more
            into.charTimeMultiplier = pChar->GetAttribute(AttrManufactureTimeMultiplier).get_float();
            into.productionTime = EvEMath::RAM::ProductionTime(bpRef->type().productionTime(), bpRef->type().productivityModifier(),
                                                               bpRef->pLevel(), into.timeMultiplier);
            // modify base time by char multiplier
            into.productionTime *= into.charTimeMultiplier;
            // if time modifier is set in config, apply that now
            into.productionTime *= sConfig.ram.ProdTime;
        } break;
        case EvERam::Activity::ResearchMaterial: {
            pType = &bpRef->type();
            FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
            into.charTimeMultiplier = pChar->GetAttribute(AttrMineralNeedResearchSpeed).get_float();
            into.productionTime = EvEMath::RAM::ResearchTime(bpRef->type().researchMaterialTime(), args.runs,
                                                             into.charTimeMultiplier, into.timeMultiplier);
            into.productionTime = EvEMath::RAM::ClampTime(into.productionTime * (double)sConfig.ram.ResME);
        }  break;
        case EvERam::Activity::ResearchTime: {
            pType = &bpRef->type();
            FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
            into.charTimeMultiplier = pChar->GetAttribute(AttrManufacturingTimeResearchSpeed).get_float();
            into.productionTime = EvEMath::RAM::ResearchTime(bpRef->type().researchProductivityTime(), args.runs,
                                                             into.charTimeMultiplier, into.timeMultiplier);
            into.productionTime = EvEMath::RAM::ClampTime(into.productionTime * (double)sConfig.ram.ResPE);
        }  break;
        case EvERam::Activity::Copying: {
            // a copy's time, times the number of copies (runs) below
            pType = &bpRef->type();
            FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
            into.charTimeMultiplier = pChar->GetAttribute(AttrCopySpeedPercent).get_float();
            into.productionTime = EvEMath::RAM::CopyTime(bpRef->type().researchCopyTime(), args.copyRuns, bpRef->type().maxProductionLimit(),
                                                         into.charTimeMultiplier, into.timeMultiplier);
            into.productionTime = EvEMath::RAM::ClampTime(into.productionTime * (double)sConfig.ram.CopyTime);
        }  break;
        case EvERam::Activity::Invention: {
            // no skill shortens invention.  base time is the T1 blueprint's researchTechTime
            pType = &bpRef->productType();
            FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
            into.productionTime = EvEMath::RAM::InventionTime(bpRef->type().researchTechTime(), into.timeMultiplier
                                                              /*implant modifier here*/);
            into.productionTime *= sConfig.ram.InventTime;
        } break;
        default:
            return false;
    }

    // multiply single run time by run count for total time (ME and PE research time is for all the levels)
    if ((args.activityID != EvERam::Activity::ResearchMaterial) and (args.activityID != EvERam::Activity::ResearchTime))
        into.productionTime = EvEMath::RAM::ClampTime((double)into.productionTime * args.runs);

    // calculate cost (rent per started hour of the whole job)
    into.usageCost *= ceil(into.productionTime / 3600.0f);
    into.cost = into.installCost + into.usageCost;

    /* a line the market sim holds is quoted as it was before the sim took it:  installing there moves the sim's job off
     *  (RamProxyService::InstallJob), so a player never waits for the sim or gets RamCannotGuaranteeStartTime because of it */
    into.maxJobStartTime = sSimFacilities.PlayerNextFreeTime(args.AssemblyLineID, FactoryDB::GetNextFreeTime(args.AssemblyLineID));

    return true;
}

void RamMethods::EncodeBillOfMaterials(const std::vector<EvERam::RequiredItem> &reqItems, float materialMultiplier, float charMaterialMultiplier, uint32 runs, BillOfMaterials &into)
{
    PySafeDecRef( into.extras.lines );
    into.extras.lines = new PyList();
    PySafeDecRef( into.wasteMaterials.lines );
    into.wasteMaterials.lines = new PyList();
    PySafeDecRef( into.rawMaterials.lines );
    into.rawMaterials.lines = new PyList();

    for (auto cur : reqItems) {
        if (cur.isSkill) {
            into.skills[cur.typeID] = new PyInt(cur.quantity);
            continue;
        }

        int32 qtyNeeded = GetMaterialQuantity(cur, materialMultiplier, charMaterialMultiplier, runs);

        // otherwise, make line for material list
        MaterialList_Line line;
        line.requiredTypeID = cur.typeID;
        line.quantity = GetBaseQuantity(cur, materialMultiplier, runs);
        line.damagePerJob = cur.damagePerJob;
        line.isSkillCheck = false;  // no idea what is this for
        line.requiresHP = false;    // no idea what is this for

        // "Extra material" has no waste.  what is not used up (damagePerJob < 1) comes back on delivery
        // "Raw material" is used up, with the blueprint's waste
        // "Waste Material" is the character's waste on the raw material (Production Efficiency)
        if (cur.extra) {
            into.extras.lines->AddItem( line.Encode() );
        } else {
            if (qtyNeeded > line.quantity) {
                MaterialList_Line wastage( line );  // simply copy original line ...
                wastage.quantity = qtyNeeded - line.quantity;
                into.wasteMaterials.lines->AddItem( wastage.Encode() );
            }
            into.rawMaterials.lines->AddItem( line.Encode() );
        }
    }
}

void RamMethods::EncodeMissingMaterials(const std::vector<EvERam::RequiredItem> &reqItems, const PathElement &bomLocation, uint32 ownerID, Client *const pClient, float materialMultiplier, float charMaterialMultiplier, int32 runs, std::map<int32, PyRep *> &into) {
    Inventory* pInv(GetBOMInventory(bomLocation));
    std::map<uint16, uint32> needed;
    for (auto cur : reqItems) {
        if (cur.isSkill) {
            if (pClient->GetChar()->GetSkillLevel(cur.typeID) < cur.quantity)
                into[cur.typeID] = new PyInt(cur.quantity - pClient->GetChar()->GetSkillLevel(cur.typeID));
        } else {
            needed[cur.typeID] += GetMaterialQuantity(cur, materialMultiplier, charMaterialMultiplier, runs);
        }
    }

    for (auto cur : needed) {
        uint32 qtyHave(pInv == nullptr ? 0 : pInv->GetTypeQtyForOwner(cur.first, (EVEItemFlags)bomLocation.flagID, ownerID));
        if (cur.second > qtyHave)
            into[cur.first] = new PyInt(cur.second - qtyHave);
    }
}

const char* RamMethods::GetActivityName(int8 activityID)
{
    switch (activityID) {
        case EvERam::Activity::Copying:             return "Copying";
        case EvERam::Activity::Manufacturing:       return "Manufacturing";
        case EvERam::Activity::Invention:           return "Invention";
        case EvERam::Activity::ResearchMaterial:    return "Research ME";
        case EvERam::Activity::ResearchTime:        return "Research PE";
        case EvERam::Activity::ReverseEngineering:  return "Reverse Engineering";
        // these last 2 should never hit
        case EvERam::Activity::Duplicating:
        case EvERam::Activity::ResearchTech: {
            codelog(MANUF__ERROR, "RamMethods::GetActivityName - invalid activity sent: %u", activityID);
        } break;
    }
    return "Undefined";
}

