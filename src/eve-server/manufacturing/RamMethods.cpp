
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
#include "pos/PosMgrDB.h"
#include "station/StationDataMgr.h"

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
        // reverse engineering is only done in experimental laboratories and caldari research outposts
        sLog.Error("RAM::InstallJob()", "Unsupported Activity %u sent by %s(%u).", args.activityID, pClient->GetName(), pClient->GetCharacterID());
        throw UserError ("RamActivityInvalid");
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
            if (bpRef->mLevel() + args.runs > 10){
                throw UserError ("RamActivityInvalid"); // The client should have a specific user error for this
            }
            if (bpRef->copy())
                throw UserError ("RamCannotResearchABlueprintCopy");
            pType = &bpRef->type();
        } break;
        case EvERam::Activity::ResearchTime: {
            if (bpRef->pLevel() + args.runs > 10){
                throw UserError ("RamActivityInvalid"); // The client should have a specific user error for this
            }
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

void RamMethods::InstallationCheck(Client*const pClient, int32 lineLocationID)
{
    if (sDataMgr.IsStation(lineLocationID)) {
        uint32 regionID = sDataMgr.GetStationRegion(lineLocationID);
        if (!IsRegionID(regionID))
            throw UserError ("RamIsNotAnInstallation");
        if (pClient->GetRegionID() != regionID)
            throw UserError ("RamRangeLimitationRegion");
    } else {
        // get structure data and run tests
        // RamStructureNotInSpace
        // RamStructureNotIsSolarsystem
    }

    // check player skill for limits
    // RamRangeLimitation
    // RamRangeLimitationJumps
    // RamRangeLimitationJumpsNoSkill

    /*
     *        jumpsPerSkillLevel =
     *        {0: -1,   // station/structure
     *         1: 0,    // system
     *         2: 5,
     *         3: 10,
     *         4: 20,
     *         5: 50}
     */
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
    } else {
        /* this will be POS assembly modules and Outpost checks
        *  RamStationIsNotConstructed
        *  RamInstalledItemMustBeInInstallation
        *  RamInstalledItemBadLocationStructure
        *  RamInstalledItemInStructureNotInContainer
        *  RamInstalledItemInStructureUnknownLocation
        */
        throw UserError ("RamActivityInvalid");
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

uint32 RamMethods::GetMaterialQuantity(const EvERam::RequiredItem& item, float materialMultiplier, float charMaterialMultiplier, uint32 runs)
{
    if (item.isSkill)
        return 0;
    // "extra" materials are not affected by skills
    uint32 qty((uint32)round(item.quantity * materialMultiplier));
    if (!item.extra)
        qty += (uint32)round(item.quantity * charMaterialMultiplier - item.quantity);
    return qty * runs;
}

void RamMethods::MaterialSkillsCheck(Client* const pClient, uint32 runs, const PathElement& bomLocation, uint32 ownerID, const Rsp_InstallJob& rsp, const std::vector< EvERam::RequiredItem >& reqItems)
{
    Inventory* pInv(GetBOMInventory(bomLocation));
    for (auto cur : reqItems) {
        if (cur.isSkill) { // check skill (quantity is required level)
            if (pClient->GetChar()->GetSkillLevel(cur.typeID) < cur.quantity) {
                throw UserError ("RamNeedSkillForJob")
                        .AddFormatValue ("skillID", new PyInt (cur.typeID))
                        .AddFormatValue ("skillLevel", new PyInt (cur.quantity));
            }
            continue;
        }

        uint32 qtyNeeded(GetMaterialQuantity(cur, rsp.materialMultiplier, rsp.charMaterialMultiplier, runs));
        if (qtyNeeded == 0)
            continue;
        uint32 qtyHave(pInv == nullptr ? 0 : pInv->GetTypeQtyForOwner(cur.typeID, (EVEItemFlags)bomLocation.flagID, ownerID));
        if (qtyHave < qtyNeeded)
            throw UserError ("RamNeedMoreForJob")
                    .AddFormatValue ("item", new PyInt (cur.typeID));
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

    BlueprintRef bpRef(nullptr);
    if (iRef->categoryID() == EVEDB::invCategories::Blueprint)
        bpRef = BlueprintRef::StaticCast(iRef);
    if (bpRef.get() == nullptr)
        return false;

    const ItemType* pType(nullptr);
    switch(args.activityID) {
        case EvERam::Activity::Manufacturing: {
            into.charMaterialMultiplier = EvE::max(1.0f, pChar->GetAttribute (AttrManufactureCostMultiplier).get_float ());
            pType = &bpRef->productType();
            FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
            into.materialMultiplier += bpRef->GetME();
            into.materialMultiplier *= (1.0f - (0.05f * pChar->GetSkillLevel(EvESkill::ProductionEfficiency)));
            into.materialMultiplier *= sConfig.ram.MatMod;
            into.charTimeMultiplier *= (1.0f - (0.04f * pChar->GetSkillLevel(EvESkill::Industry)));
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
            into.productionTime = EvEMath::RAM::ME_ResearchTime(bpRef->type().researchMaterialTime(),
                                                                bpRef->mLevel(), args.runs,
                                                                pChar->GetSkillLevel(EvESkill::Metallurgy), into.timeMultiplier
                                                                /*implant modifier here*/);
            into.productionTime *= sConfig.ram.ResME;
            into.charTimeMultiplier *= pChar->GetAttribute(AttrMineralNeedResearchSpeed).get_float();
        }  break;
        case EvERam::Activity::ResearchTime: {
            pType = &bpRef->type();
            FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
            //ch->GetAttribute(AttrResearchCostPercent).get_int();   << this is not used

            into.productionTime = EvEMath::RAM::PE_ResearchTime(bpRef->type().researchProductivityTime(),
                                                                bpRef->pLevel(), args.runs,
                                                                pChar->GetSkillLevel(EvESkill::Research), into.timeMultiplier
                                                                /*implant modifier here*/);
            into.productionTime *= sConfig.ram.ResPE;
            into.charTimeMultiplier *= pChar->GetAttribute(AttrManufacturingTimeResearchSpeed).get_float();
        }  break;
        case EvERam::Activity::Copying: {
            pType = &bpRef->type();
            FactoryDB::GetMultipliers(args.AssemblyLineID, pType, into);
            into.productionTime = EvEMath::RAM::CopyTime(bpRef->type().researchCopyTime(),
                                                         pChar->GetSkillLevel(EvESkill::Science), into.timeMultiplier
                                                         /*implant modifier here*/);
            into.productionTime *= sConfig.ram.CopyTime;
            into.charTimeMultiplier *= pChar->GetAttribute(AttrCopySpeedPercent).get_float();
            //bpRef->type().chanceOfDuplicating();
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

    // multiply single run time by run count for total time
    if (args.activityID != EvERam::Activity::ResearchMaterial && args.activityID != EvERam::Activity::ResearchTime) { // Dont multiply ME/PE activities as the time isnt linear.
        into.productionTime *= args.runs;
    }

    // calculate cost (rent per started hour of the whole job)
    into.usageCost *= ceil(into.productionTime / 3600.0f);
    into.cost = into.installCost + into.usageCost;

    into.maxJobStartTime = FactoryDB::GetNextFreeTime(args.AssemblyLineID);

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

        int qtyNeeded = GetMaterialQuantity(cur, materialMultiplier, charMaterialMultiplier, runs);

        // otherwise, make line for material list
        MaterialList_Line line;
        line.requiredTypeID = cur.typeID;
        line.quantity = (int32) round (cur.quantity * materialMultiplier) * runs;
        line.damagePerJob = cur.damagePerJob;
        line.isSkillCheck = false;  // no idea what is this for
        line.requiresHP = false;    // no idea what is this for

        /** @todo update this shit.....  */
        // "Extra material" is not affected by skills, and return upon completion
        // "Raw material" is fully consumed and affected by skills/efficiency
        // "Waste Material" is amount of material wasted ...
        if (cur.extra) {
            into.extras.lines->AddItem( line.Encode() );
        } else {
            // if there are losses, make line for waste material list
            if (charMaterialMultiplier > 1.0) {
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
    for (auto cur : reqItems) {
        uint32 qtyReq(0), qtyHave(0);
        if (cur.isSkill) {
            qtyReq = cur.quantity;
            qtyHave = pClient->GetChar()->GetSkillLevel(cur.typeID);
        } else {
            qtyReq = GetMaterialQuantity(cur, materialMultiplier, charMaterialMultiplier, runs);
            if (pInv != nullptr)
                qtyHave = pInv->GetTypeQtyForOwner(cur.typeID, (EVEItemFlags)bomLocation.flagID, ownerID);
        }

        if (qtyReq > qtyHave)
            into[cur.typeID] = new PyInt(qtyReq - qtyHave);
    }
}

/* For each material required for a blueprint (from invTypeMaterials), the quantity is affected by ME research and skills.
 * Then there’s the extra materials, which come from the ramTypeRequirements table for that BP.
 * Next, any materials in ramTypeRequirements which are marked as recyclable, have their recycled materials (from invTypeMaterials) subtracted from the list of materials required for the produced item.
 * The remaining materials from invTypeMaterials are then modified by skills and ME research as follows:
 *      xmatls = ramTypeRequirements.extra
 *      reqMatls = (invTypeMaterials.reqMatls - xmatls)
 *      waste = reqMatls * charSkills (which is wastage multiplier based on skills)
 *      totalReqMatls = (reqMatls * skill mods) + xmatls + waste
 */
void RamMethods::GetAdjustedRamRequiredMaterials()
{
    // will need to update this using above formula for correct material list
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

