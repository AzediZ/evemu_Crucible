
 /**
  * @name FactoryDB.cpp
  *   db query methods for R.A.M. activities
  *
  * @Author:         Allan
  * @date:          9Jan18   UD 5Nov20
  */

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


#include "eve-server.h"

#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "../eve-common/EVE_POS.h"
#include "character/Character.h"
#include "manufacturing/FactoryDB.h"

bool FactoryDB::IsProducableBy(const uint32 assemblyLineID, const ItemType *pType, bool strict/*false*/) {
    if (!strict) {
        // static npc station data lacks rows for some products (T3, outpost lines), so stations stay lenient
        Rsp_InstallJob into;
        return FactoryDB::GetMultipliers(assemblyLineID, pType, into);
    }

    // structure lines only build what their line type lists (by category or group)
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT 1 FROM ramAssemblyLines AS al"
        " JOIN ramAssemblyLineTypeDetailPerCategory AS alc USING (assemblyLineTypeID)"
        " WHERE al.assemblyLineID = %u AND alc.categoryID = %u"
        " UNION ALL"
        " SELECT 1 FROM ramAssemblyLines AS al"
        " JOIN ramAssemblyLineTypeDetailPerGroup AS alg USING (assemblyLineTypeID)"
        " WHERE al.assemblyLineID = %u AND alg.groupID = %u",
        assemblyLineID, pType->categoryID(), assemblyLineID, pType->groupID()))
    {
        _log(DATABASE__ERROR, "Failed to check producability of type %u by line %u: %s", pType->id(), assemblyLineID, res.error.c_str());
        return false;
    }

    DBResultRow row;
    return res.GetRow(row);
}

void FactoryDB::GetSalvage(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, typeName FROM invTypes WHERE groupID IN (754, 966)"))
        codelog(DATABASE__ERROR, "Error in GetSalvage query: %s", res.error.c_str());
}

void FactoryDB::GetCompounds(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, typeName FROM invTypes WHERE groupID IN (282, 333, 423, 427, 530, 711, 712)"))
        codelog(DATABASE__ERROR, "Error in GetCompounds query: %s", res.error.c_str());
}

void FactoryDB::GetMinerals(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, typeName FROM invTypes WHERE groupID = 18 AND published = 1"))   // minerals
        codelog(DATABASE__ERROR, "Error in GetMinerals query: %s", res.error.c_str());
}

void FactoryDB::GetComponents(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, typeName FROM invTypes WHERE groupID IN (280, 283, 313, 334, 428, 429, 526, 536, 873, 886, 913, 964)"))   // components
        codelog(DATABASE__ERROR, "Error in GetComponents query: %s", res.error.c_str());
}

void FactoryDB::GetCommodities(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, typeName FROM invTypes WHERE groupID IN (1042, 1034, 1040, 1041)"))   // PI Commodities
        codelog(DATABASE__ERROR, "Error in GetCommodities query: %s", res.error.c_str());
}

void FactoryDB::GetMiscCommodities(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, typeName FROM invTypes WHERE groupID = 314"))
        codelog(DATABASE__ERROR, "Error in GetMinerals query: %s", res.error.c_str());
}

void FactoryDB::GetResources(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, typeName FROM invTypes WHERE groupID IN (1031, 1032, 1033, 1035)"))   // PI Resources
        codelog(DATABASE__ERROR, "Error in GetResources query: %s", res.error.c_str());
}

void FactoryDB::GetRAMMaterials(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, materialTypeID, quantity FROM invTypeMaterials"))
        codelog(DATABASE__ERROR, "Error in GetRAMMaterials query: %s", res.error.c_str());
}

void FactoryDB::GetRAMRequirements(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, activityID, requiredTypeID, quantity, damagePerJob, extra FROM ramTypeRequirements"))
        codelog(DATABASE__ERROR, "Error in GetRAMRequirements query: %s", res.error.c_str());
}

void FactoryDB::GetMetaTypes(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT typeID, parentTypeID, metaGroupID FROM invMetaTypes"))
        codelog(DATABASE__ERROR, "Error in GetMetaTypes query: %s", res.error.c_str());
}

void FactoryDB::GetInstallationContents(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT installationTypeID, assemblyLineTypeID, quantity FROM ramInstallationTypeContents"))
        codelog(DATABASE__ERROR, "Error in GetInstallationContents query: %s", res.error.c_str());
}

void FactoryDB::GetAssemblyLineTypes(DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res, "SELECT assemblyLineTypeID, activityID FROM ramAssemblyLineTypes"))
        codelog(DATABASE__ERROR, "Error in GetAssemblyLineTypes query: %s", res.error.c_str());
}

void FactoryDB::SetJobEventID(const uint32 jobID, const uint32 eventID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE ramJobs SET eventID=%u WHERE jobID=%u", eventID, jobID);
}

bool FactoryDB::DeleteBlueprint(uint32 blueprintID) {
    DBerror err;
    if (!sDatabase.RunQuery(err, "DELETE FROM invBlueprints WHERE itemID=%u", blueprintID)) {
        _log(DATABASE__ERROR, "Failed to delete blueprint %u: %s.", blueprintID, err.c_str());
        return false;
    }
    return true;
}

PyRep* FactoryDB::GetMaterialCompositionOfItemType(const uint32 typeID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT requiredTypeID AS typeID, quantity"
        " FROM ramTypeRequirements"
        " WHERE typeID = (SELECT blueprintTypeID FROM invBlueprintTypes WHERE productTypeID = %u)"
        " AND activityID = 1 AND damagePerJob = 1",
        typeID))
    {
        _log(DATABASE__ERROR, "Could not retrieve material composition for type %u : %s", typeID, res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

void FactoryDB::GetOutpostMaterialCompositionOfItemType(const uint32 typeID, DBQueryResult& res) {
    if (!sDatabase.RunQuery(res,
        "SELECT materialTypeID AS typeID, quantity"
        " FROM invTypeMaterials"
        " WHERE typeID = %u",
        typeID))
    {
        _log(DATABASE__ERROR, "Could not retrieve outpost material composition for type %u : %s", typeID, res.error.c_str());
    }
}

bool FactoryDB::SaveBlueprintData(uint32 blueprintID, EvERam::bpData& data) {
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "INSERT INTO invBlueprints"
        "  (itemID, copy, mLevel, pLevel, runs)"
        " VALUES"
        "  (%u, %u, %i, %i, %i)"
        "ON DUPLICATE KEY UPDATE "
        "mLevel=VALUES(mLevel), "
        "pLevel=VALUES(pLevel), "
        "runs=VALUES(runs) ",
        blueprintID, data.copy, data.mLevel, data.pLevel, data.runs))
    {
        codelog(DATABASE__ERROR, "Error in SaveBlueprint query: %s.", err.c_str());
        return false;
    }

    return true;
}

bool FactoryDB::GetBlueprintData(uint32 blueprintID, EvERam::bpData& into) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  copy,"
        "  mLevel,"
        "  pLevel,"
        "  runs"
        " FROM invBlueprints"
        " WHERE itemID=%u",
        blueprintID))
    {
        codelog(DATABASE__ERROR, "Error in GetBlueprintData query: %s.", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__MESSAGE, "Blueprint %u not found.", blueprintID);
        return false;
    }

    into.copy = row.GetBool(0);
    into.mLevel = row.GetInt(1);
    into.pLevel = row.GetInt(2);
    into.runs = row.GetInt(3);

    return true;
}

void FactoryDB::GetBlueprintType(DBQueryResult& res) {
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  blueprintTypeID,"
        "  parentBlueprintTypeID,"
        "  productTypeID,"
        "  productionTime,"
        "  techLevel,"
        "  researchProductivityTime,"
        "  researchMaterialTime,"
        "  researchCopyTime,"
        "  researchTechTime,"
        "  productivityModifier,"
        "  materialModifier,"
        "  wasteFactor,"
        "  maxProductionLimit, "
        "  chanceOfRE,"
        "  g.categoryID"
        " FROM invBlueprintTypes AS bt"
        "  LEFT JOIN invTypes AS t ON t.typeID = bt.blueprintTypeID"
        "  LEFT JOIN invGroups AS g USING (groupID)"))
        //" WHERE t.published = 1" ))
    {
        codelog(DATABASE__ERROR, "Error in GetBlueprintType query: %s.", res.error.c_str());
    }
}

PyRep *FactoryDB::GetJobs2(const int32 ownerID, const bool completed)
{
    DBQueryResult res;

    /* installed items of finished invention/reverse engineering jobs are gone, and structures may be gone too,
     *  so the job row keeps the types needed to show the job (the client calls cfg.invtypes.Get() on them)  */
    if (!sDatabase.RunQuery(res,
        "SELECT"
        " job.jobID,"
        " job.assemblyLineID,"
        " assemblyLine.containerID,"
        " job.installedItemID,"
        " COALESCE(installedItem.typeID, job.installedItemTypeID) AS installedItemTypeID,"
        " COALESCE(installedItem.ownerID, job.ownerID) AS installedItemOwnerID,"
        " COALESCE(blueprint.pLevel, 0) AS installedItemProductivityLevel,"
        " COALESCE(blueprint.mLevel, 0) AS installedItemMaterialLevel,"
        // quite ugly, but lets us use DBResultToRowset
        " CASE assemblyLine.activityID"
        "  WHEN 1 THEN COALESCE(blueprintType.productTypeID, job.installedItemTypeID)"
        "  WHEN 7 THEN job.outputTypeID"
        "  WHEN 8 THEN job.outputTypeID"
        "  ELSE COALESCE(installedItem.typeID, job.installedItemTypeID)"
        " END AS outputTypeID,"
        " job.outputFlag,"
        " job.installerID,"
        " assemblyLine.activityID,"
        " job.runs,"
        " job.installTime,"
        " job.beginProductionTime,"
        " job.pauseProductionTime,"
        " job.endProductionTime,"
        " job.completedStatusID != 0 AS completed,"
        " job.licensedProductionRuns,"
        " job.installedInSolarSystemID,"
        " job.completedStatusID AS completedStatus,"
        " COALESCE(station.stationTypeID, container.typeID, job.containerTypeID) AS containerTypeID,"
        " COALESCE(station.solarSystemID, job.installedInSolarSystemID) AS containerLocationID"
        " FROM ramJobs AS job"
        " LEFT JOIN entity AS installedItem ON job.installedItemID = installedItem.itemID"
        " LEFT JOIN ramAssemblyLines AS assemblyLine ON job.assemblyLineID = assemblyLine.assemblyLineID"
        " LEFT JOIN invBlueprints AS blueprint ON installedItem.itemID = blueprint.itemID"
        " LEFT JOIN invBlueprintTypes AS blueprintType ON COALESCE(installedItem.typeID, job.installedItemTypeID) = blueprintType.blueprintTypeID"
        " LEFT JOIN staStations AS station ON assemblyLine.containerID = station.stationID"
        " LEFT JOIN entity AS container ON assemblyLine.containerID = container.itemID"
        " WHERE job.ownerID = %u"
        " AND job.completedStatusID %s 0",
        ownerID, (completed ? "!=" : "=") ))
    {
        _log(DATABASE__ERROR, "Failed to query jobs for owner %u: %s", ownerID, res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyRep *FactoryDB::AssemblyLinesSelectPublic(const uint32 regionID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT DISTINCT"
        " station.stationID AS containerID,"
        " station.stationTypeID AS containerTypeID,"
        " station.solarSystemID AS containerLocationID,"
        " station.assemblyLineTypeID,"
        " station.quantity,"
        " station.ownerID,"
        " types.activityID"
        " FROM ramAssemblyLineStations AS station"
        " LEFT JOIN crpNPCCorporations AS corp ON station.ownerID = corp.corporationID"
        " LEFT JOIN ramAssemblyLineTypes as types ON station.assemblyLineTypeID = types.assemblyLineTypeID"
        " WHERE station.ownerID = corp.corporationID"
        " AND station.regionID = %u",
        regionID))
    {
        _log(DATABASE__ERROR, "Failed to query public assembly lines for region %u: %s.", regionID, res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep *FactoryDB::AssemblyLinesSelectPersonal(const uint32 charID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT DISTINCT"
        " station.stationID AS containerID,"
        " station.stationTypeID AS containerTypeID,"
        " station.solarSystemID AS containerLocationID,"
        " station.assemblyLineTypeID,"
        " station.quantity,"
        " station.ownerID,"
        " line.activityID"
        " FROM ramAssemblyLineStations AS station"
        " LEFT JOIN ramAssemblyLines AS line ON station.stationID = line.containerID AND station.assemblyLineTypeID = line.assemblyLineTypeID AND station.ownerID = line.ownerID"
        " WHERE station.ownerID = %u"
        " AND (line.restrictionMask & %u) = %u",
        charID, (EvERam::RestrictionMask::ByCorp | EvERam::RestrictionMask::ByAlliance), (EvERam::RestrictionMask::ByCorp | EvERam::RestrictionMask::ByAlliance)))
    {
        _log(DATABASE__ERROR, "Failed to query personal assembly lines for char %u: %s.", charID, res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep *FactoryDB::AssemblyLinesSelectPrivate(const uint32 charID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT DISTINCT"
        " station.stationID AS containerID,"
        " station.stationTypeID AS containerTypeID,"
        " station.solarSystemID AS containerLocationID,"
        " station.assemblyLineTypeID,"
        " station.quantity,"
        " station.ownerID,"
        " line.activityID"
        " FROM ramAssemblyLineStations AS station"
        " LEFT JOIN ramAssemblyLines AS line ON station.stationID = line.containerID AND station.assemblyLineTypeID = line.assemblyLineTypeID AND station.ownerID = line.ownerID"
        " WHERE station.ownerID = %u",
        charID))
    {
        _log(DATABASE__ERROR, "Failed to query private assembly lines for char %u: %s.", charID, res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep *FactoryDB::AssemblyLinesSelectCorporation(const uint32 corpID) {
    DBQueryResult res;

    // stations owned by the corp (outposts), and the online assembly arrays and labs of the corp
    if (!sDatabase.RunQuery(res,
        "SELECT DISTINCT"
        " station.stationID AS containerID,"
        " station.stationTypeID AS containerTypeID,"
        " station.solarSystemID AS containerLocationID,"
        " station.assemblyLineTypeID,"
        " CAST(station.quantity AS SIGNED) AS quantity,"
        " station.ownerID,"
        " types.activityID"
        " FROM ramAssemblyLineStations AS station"
        " LEFT JOIN ramAssemblyLineTypes AS types ON station.assemblyLineTypeID = types.assemblyLineTypeID"
        " WHERE station.ownerID = %u"
        " UNION ALL"
        " SELECT"
        " line.containerID,"
        " e.typeID AS containerTypeID,"
        " e.locationID AS containerLocationID,"
        " line.assemblyLineTypeID,"
        " CAST(COUNT(line.assemblyLineID) AS SIGNED) AS quantity,"
        " line.ownerID,"
        " line.activityID"
        " FROM ramAssemblyLines AS line"
        " JOIN entity AS e ON e.itemID = line.containerID"
        " JOIN posStructureData AS pos ON pos.itemID = line.containerID"
        " WHERE line.ownerID = %u"
        " AND pos.state IN (%u, %u, %u, %u)"
        " GROUP BY line.containerID, e.typeID, e.locationID, line.assemblyLineTypeID, line.ownerID, line.activityID",
        corpID, corpID, EVEPOS::StructureState::Online, EVEPOS::StructureState::Operating,
        EVEPOS::StructureState::Vulnerable, EVEPOS::StructureState::Invulnerable))
    {
        _log(DATABASE__ERROR, "Failed to query corporation assembly lines for corp %u: %s.", corpID, res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep *FactoryDB::AssemblyLinesSelectAlliance(const int32 allianceID) {
    DBQueryResult res;

    // This produces the same output but with less complex joins and
    // the quantity column is completely redundant.
    // SELECT
    //     job.containerId,
    //     station.stationTypeId AS containerTypeId,
    //     station.solarSystemId AS containerLocationId,
    //     job.typeId as assemblyLineTypeId,
    //     COUNT(job.containerId) as quantity,
    //     station.corporationId as ownerId
    //     FROM industrySlots AS job
    //         JOIN evemu.staStations AS station ON station.stationId = job.containerId
    //         JOIN evemu.crpCorporation AS corp ON corp.corporationId = station.corporationId
    //     GROUP BY job.containerId, job.typeId;

    // alliance-restricted station lines and online assembly arrays and labs of alliance corps
    if (!sDatabase.RunQuery(res,
        "SELECT DISTINCT"
        " station.stationID AS containerID,"
        " station.stationTypeID AS containerTypeID,"
        " station.solarSystemID AS containerLocationID,"
        " station.assemblyLineTypeID,"
        " CAST(station.quantity AS SIGNED) AS quantity,"
        " station.ownerID,"
        " line.activityID"
        " FROM ramAssemblyLineStations AS station"
        " LEFT JOIN crpCorporation AS crp ON station.ownerID = crp.corporationID"
        " LEFT JOIN ramAssemblyLines AS line ON station.stationID = line.containerID AND station.assemblyLineTypeID = line.assemblyLineTypeID AND station.ownerID = line.ownerID"
        " WHERE crp.allianceID = %u"
        " AND (line.restrictionMask & %u) = %u"
        " UNION ALL"
        " SELECT"
        " line.containerID,"
        " e.typeID AS containerTypeID,"
        " e.locationID AS containerLocationID,"
        " line.assemblyLineTypeID,"
        " CAST(COUNT(line.assemblyLineID) AS SIGNED) AS quantity,"
        " line.ownerID,"
        " line.activityID"
        " FROM ramAssemblyLines AS line"
        " JOIN entity AS e ON e.itemID = line.containerID"
        " JOIN posStructureData AS pos ON pos.itemID = line.containerID"
        " JOIN crpCorporation AS crp ON crp.corporationID = line.ownerID"
        " WHERE crp.allianceID = %u"
        " AND (line.restrictionMask & %u) = %u"
        " AND pos.state IN (%u, %u, %u, %u)"
        " GROUP BY line.containerID, e.typeID, e.locationID, line.assemblyLineTypeID, line.ownerID, line.activityID",
        allianceID, EvERam::RestrictionMask::ByAlliance, EvERam::RestrictionMask::ByAlliance,
        allianceID, EvERam::RestrictionMask::ByAlliance, EvERam::RestrictionMask::ByAlliance,
        EVEPOS::StructureState::Online, EVEPOS::StructureState::Operating,
        EVEPOS::StructureState::Vulnerable, EVEPOS::StructureState::Invulnerable))
    {
        _log(DATABASE__ERROR, "Failed to query alliance assembly lines for alliance %u: %s.", allianceID, res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep *FactoryDB::AssemblyLinesGet(const uint32 containerID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        " assemblyLineID,"
        " assemblyLineTypeID,"
        " containerID,"
        " nextFreeTime,"
        " COALESCE(UIGroupingID, 0) AS UIGroupingID,"
        " costInstall,"
        " costPerHour,"
        " restrictionMask,"
        " discountPerGoodStandingPoint,"
        " surchargePerBadStandingPoint,"
        " minimumStanding,"
        " minimumCharSecurity,"
        " minimumCorpSecurity,"
        " maximumCharSecurity,"
        " maximumCorpSecurity,"
        " ownerID,"
        " activityID"
        " FROM ramAssemblyLines"
        " WHERE containerID = %u",
        containerID)) {
        _log(DATABASE__ERROR, "Failed to query assembly lines for container %u: %s.", containerID, res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

bool FactoryDB::GetAssemblyLineProperties(const uint32 assemblyLineID, Character* pChar, Rsp_InstallJob& into, bool isCorpJob/*false*/) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        " alt.baseMaterialMultiplier,"
        " alt.baseTimeMultiplier,"
        " al.costInstall,"
        " alt.minCostPerHour,"
        " al.costPerHour,"
        " al.ownerID,"                          //5
        " al.discountPerGoodStandingPoint,"
        " al.surchargePerBadStandingPoint"
        " FROM ramAssemblyLines AS al"
        " LEFT JOIN ramAssemblyLineTypes AS alt USING (assemblyLineTypeID)"
        " WHERE al.assemblyLineID = %u",
        assemblyLineID))
    {
        _log(DATABASE__ERROR, "Failed to query properties for assembly line %u: %s.", assemblyLineID, res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "No properties found for assembly line %u.", assemblyLineID);
        return false;
    }

    into.materialMultiplier     = row.GetFloat(0);
    into.timeMultiplier         = row.GetFloat(1);
    into.installCost            = row.GetFloat(2);
    if (row.GetFloat(3) > row.GetFloat(4)) {            //min of base cost/hr vs minimum cost/hr
        into.usageCost          = row.GetFloat(3);
    } else {
        into.usageCost          = row.GetFloat(4);
    }

    // verify line standings modifiers (some have no modifiers)
    float discount(row.IsNull(6) ? 0 : row.GetFloat(6)), surcharge(row.IsNull(7) ? 0 : row.GetFloat(7));
    if ((discount == 0) and (surcharge == 0))
        return true;

    // lines of the installer's own corp (corp structures and outposts) have no standings modifier
    uint32 lineOwnerID(row.IsNull(5) ? 0 : row.GetUInt(5));
    if ((lineOwnerID == 0) or (lineOwnerID == pChar->corporationID()))
        return true;

    /* the stored values are percent per standing point (1.5 / 0.5 on npc lines, 0-10 on configurable lines)
     *   cost * (1 - discount% * standing) with good standing, cost * (1 + surcharge% * -standing) with bad standing
     * personal jobs use the installer's effective standing (connections/diplomacy) with the line owner,
     * corp jobs the better of the corp's standing with the owner corp and with the owner's faction  */
    float standing(0), costModifier(1);
    if (isCorpJob) {
        float cStanding(StandingDB::GetStanding(lineOwnerID, pChar->corporationID()));
        uint32 factionID(sDataMgr.GetCorpFaction(lineOwnerID));
        float fStanding(factionID ? StandingDB::GetStanding(factionID, pChar->corporationID()) : cStanding);
        standing = EvE::max(cStanding, fStanding);
    } else {
        standing = pChar->GetStandingModified(lineOwnerID);
    }

    if (standing < 0) {
        costModifier += (surcharge / 100.0f) * -standing;
    } else {
        costModifier -= (discount / 100.0f) * standing;
    }
    if (costModifier < 0)
        costModifier = 0;

    _log(MANUF__MESSAGE, "FactoryDB::GetALProps() - Cost Modifier %.3f, standing %.2f", costModifier, standing);

    // modify setup cost based on standings
    into.installCost *= costModifier;

    return true;
}

bool FactoryDB::GetAssemblyLineRestrictions(const int32 assemblyLineID, EvERam::LineRestrictions &data) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        " ownerID,"
        " minimumStanding,"
        " minimumCharSecurity,"
        " maximumCharSecurity,"
        " minimumCorpSecurity,"
        " maximumCorpSecurity,"
        " restrictionMask,"
        " activityID,"
        " containerID"
        " FROM ramAssemblyLines"
        " WHERE assemblyLineID = %i",
        assemblyLineID))
    {
        _log(DATABASE__ERROR, "Failed to query verify properties for assembly line %u: %s.", assemblyLineID, res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "No verify properties found for assembly line %u.", assemblyLineID);
        return false;
    }

    data.ownerID        = (row.IsNull(0) ? 0 : row.GetUInt(0));
    data.minStanding    = row.GetFloat(1);
    data.minCharSec     = row.GetFloat(2);
    data.maxCharSec     = row.GetFloat(3);
    data.minCorpSec     = row.GetFloat(4);
    data.maxCorpSec     = row.GetFloat(5);
    data.rMask          = (row.IsNull(6) ? 0 : row.GetUInt(6));
    data.activityID     = (row.IsNull(7) ? 0 : row.GetUInt(7));
    data.containerID    = (row.IsNull(8) ? 0 : row.GetUInt(8));

    return true;
}

uint32 FactoryDB::InstallJob(const uint32 ownerID, const uint32 installerID, Call_InstallJob& args, const int64 beginTime,
                             const int64 endTime, const uint32 systemID, const uint32 outputLocationID, const uint32 containerTypeID,
                             const uint16 installedTypeID, const EvERam::InventionData* pInvData/*nullptr*/)
{
    // invention and reverse engineering outputs are decided at installation
    EvERam::InventionData invData = EvERam::InventionData();
    if (pInvData != nullptr)
        invData = *pInvData;

    DBerror err;
    uint32 jobID(0);
    // insert job
    if (!sDatabase.RunQueryLID(err, jobID,
        "INSERT INTO ramJobs"
        " (ownerID, installerID, assemblyLineID, installedItemID, installedItemTypeID, installTime, beginProductionTime, endProductionTime,"
        " runs, outputFlag, outputLocationID, containerTypeID, licensedProductionRuns, completedStatusID, installedInSolarSystemID,"
        " outputTypeID, outputRuns, outputME, outputPE, successChance)"
        " VALUES"
        " (%u, %u, %i, %i, %u, %.0f, %lli, %lli,"
        " %i, %i, %u, %u, %i, 0, %u,"
        " %u, %i, %i, %i, %f)",
        ownerID, installerID, args.AssemblyLineID, args.bpItemID, installedTypeID, GetFileTimeNow(), beginTime, endTime,
        args.runs, args.outputFlag, outputLocationID, containerTypeID, args.copyRuns, systemID,
        invData.outputBpTypeID, invData.outputRuns, invData.outputME, invData.outputPE, invData.chance))
    {
        _log(DATABASE__ERROR, "Failed to insert new job to database: %s.", err.c_str());
        return 0;
    }

    // update nextFreeTime
    if (!sDatabase.RunQuery(err,
        "UPDATE ramAssemblyLines"
        " SET nextFreeTime = %lli"
        " WHERE assemblyLineID = %i",
        endTime, args.AssemblyLineID))
    {
        _log(DATABASE__ERROR, "Failed to update next free time for assembly line %u: %s.", args.AssemblyLineID, err.c_str());
        return 0;
    }

    return jobID;
}

uint32 FactoryDB::CountManufacturingJobs(const uint32 installerID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        " COUNT(job.jobID)"
        " FROM ramJobs AS job"
        " LEFT JOIN ramAssemblyLines AS line USING (assemblyLineID)"
        " WHERE job.installerID = %u"
        " AND job.completedStatusID = 0"
        " AND line.activityID = 1",
        installerID))
    {
        _log(DATABASE__ERROR, "Failed to count manufacturing jobs for installer %u.", installerID);
        return 0;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "No rows returned while counting manufacturing jobs for installer %u.", installerID);
        return 0;
    }

    return row.GetUInt(0);
}
uint32 FactoryDB::CountResearchJobs(const uint32 installerID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        " COUNT(job.jobID)"
        " FROM ramJobs AS job"
        " LEFT JOIN ramAssemblyLines AS line USING (assemblyLineID)"
        " WHERE job.installerID = %u"
        " AND job.completedStatusID = 0"
        " AND line.activityID != 1",    // is this accurate?
        installerID))
    {
        _log(DATABASE__ERROR, "Failed to count research jobs for installer %u.", installerID);
        return 0;
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return 0;

    return row.GetUInt(0);
}

bool FactoryDB::GetJobProperties(const uint32 jobID, EvERam::JobProperties &data) {
    data.jobID = jobID;
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT job.installedItemID, job.ownerID, job.outputFlag, job.runs, job.licensedProductionRuns,"
        " job.endProductionTime, job.completedStatusID, line.activityID, job.eventID, job.installerID,"         // 9
        " job.assemblyLineID, line.containerID, job.outputLocationID, job.installedInSolarSystemID, job.pauseProductionTime,"
        " job.installedItemTypeID, job.outputTypeID, job.outputRuns, job.outputME, job.outputPE, job.successChance"    // 20
        " FROM ramJobs AS job"
        " LEFT JOIN ramAssemblyLines AS line USING (assemblyLineID)"
        " WHERE job.jobID = %u",
        jobID))
    {
        _log(DATABASE__ERROR, "Failed to query properties of job %u: %s.", jobID, res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "No properties found for job %u.", jobID);
        return false;
    }

    data.itemID             = row.GetUInt(0);
    data.ownerID            = row.GetUInt(1);
    data.outputFlag         = (EVEItemFlags)row.GetUInt(2);
    data.jobRuns            = row.GetInt(3);
    data.licensedRuns       = (row.IsNull(4) ? 0 : row.GetInt(4));
    data.endTime            = row.GetInt64(5);
    data.status             = row.GetInt(6);
    data.activity           = (row.IsNull(7) ? 0 : row.GetUInt(7));
    data.eventID            = (row.IsNull(8) ? 0 : row.GetUInt(8));
    data.installerID        = row.GetUInt(9);
    data.assemblyLineID     = row.GetUInt(10);
    data.containerID        = (row.IsNull(11) ? 0 : row.GetUInt(11));
    data.outputLocationID   = row.GetUInt(12);
    data.solarSystemID      = row.GetUInt(13);
    data.pauseTime          = (row.IsNull(14) ? 0 : row.GetInt64(14));
    data.installedItemTypeID= row.GetUInt(15);
    data.outputTypeID       = row.GetUInt(16);
    data.outputRuns         = row.GetInt(17);
    data.outputME           = row.GetInt(18);
    data.outputPE           = row.GetInt(19);
    data.successChance      = row.GetFloat(20);

    return true;
}

bool FactoryDB::CompleteJob(const uint32 jobID, const int8 completedStatus) {
    DBerror err;

    if (!sDatabase.RunQuery(err, "UPDATE ramJobs SET completedStatusID = %i WHERE jobID = %u", completedStatus, jobID)) {
        _log(DATABASE__ERROR, "Failed to complete job %u (status = %i): %s.", jobID, completedStatus, err.c_str());
        return false;
    }

    return true;
}

int64 FactoryDB::GetNextFreeTime(const uint32 assemblyLineID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT nextFreeTime FROM ramAssemblyLines WHERE assemblyLineID = %u", assemblyLineID)) {
        _log(DATABASE__ERROR, "Failed to query next free time for assembly line %u: %s.", assemblyLineID, res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__ERROR, "Assembly line %u not found.", assemblyLineID);
        return 0;
    }

    return (row.IsNull(0) ? 0 : row.GetInt64(0));
}

bool FactoryDB::GetMultipliers(const uint32 assemblyLineID, const ItemType* pType, Rsp_InstallJob& into) {
    DBQueryResult res;
    // check Category first
    if (!sDatabase.RunQuery(res,
        "SELECT alc.materialMultiplier, alc.timeMultiplier"
        " FROM ramAssemblyLineTypeDetailPerCategory AS alc"
        " LEFT JOIN ramAssemblyLines AS al USING (assemblyLineTypeID)"
        " WHERE al.assemblyLineID = %u AND alc.categoryID = %u",
        assemblyLineID, pType->categoryID()))
    {
        _log(DATABASE__ERROR, "Failed to check producability of cat %u by line %u: %s", pType->categoryID(), assemblyLineID, res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (res.GetRow(row)) {
        into.materialMultiplier *= row.GetDouble(0);
        into.timeMultiplier *= row.GetDouble(1);
    }

    // then Group  (all materialMultiplier = 1)
    if (!sDatabase.RunQuery(res,
        "SELECT alg.materialMultiplier, alg.timeMultiplier"
        " FROM ramAssemblyLineTypeDetailPerGroup AS alg"
        " LEFT JOIN ramAssemblyLines AS al USING (assemblyLineTypeID)"
        " WHERE al.assemblyLineID = %u"
        " AND alg.groupID = %u",
        assemblyLineID, pType->groupID()))
    {
        _log(DATABASE__ERROR, "Failed to check producability of group %u by line %u: %s", pType->groupID(), assemblyLineID, res.error.c_str());
        return false;
    }

    if (res.GetRow(row)) {
        into.materialMultiplier *= row.GetDouble(0);
        into.timeMultiplier *= row.GetDouble(1);
    }

    return true;
}

bool FactoryDB::IsRefinable(const uint16 typeID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT typeID FROM ramTypeRequirements"
        " WHERE typeID=%u AND extra = 0 LIMIT 1",
        typeID))
    {
        _log(DATABASE__ERROR, "Failed to check ore type ID: %s.", res.error.c_str());
        return false;
    }

    return (res.ColumnCount() > 0);
}

bool FactoryDB::IsRecyclable(const uint16 typeID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT typeID FROM ramTypeRequirements"
        " WHERE damagePerJob = 1 AND activityID = 6 AND typeID = %u"
        " AND extra = 1 LIMIT 1",
        typeID))
    {
        _log(DATABASE__ERROR, "Failed to check item type ID: %s.", res.error.c_str());
        return false;
    }

    if (res.ColumnCount() > 0)
        return true;

    if (!sDatabase.RunQuery(res,
        "SELECT typeID FROM ramTypeRequirements"
        " WHERE damagePerJob = 1 AND activityID = 1 AND productTypeID = %u"
        " AND extra = 1 LIMIT 1",
        typeID))
    {
        _log(DATABASE__ERROR, "Failed to check item type ID: %s.", res.error.c_str());
        return false;
    }

    return (res.ColumnCount() > 0);
}

bool FactoryDB::EnsureStructureAssemblyLines(uint32 structureID, uint16 typeID, uint32 ownerID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM ramAssemblyLines WHERE containerID = %u", structureID)) {
        _log(DATABASE__ERROR, "Failed to count assembly lines of structure %u: %s", structureID, res.error.c_str());
        return false;
    }

    DBerror err;
    DBResultRow row;
    if (res.GetRow(row) and (row.GetUInt(0) > 0)) {
        // lines exist.  they belong to the structure's corp
        if (!sDatabase.RunQuery(err, "UPDATE ramAssemblyLines SET ownerID = %u WHERE containerID = %u", ownerID, structureID)) {
            _log(DATABASE__ERROR, "Failed to update owner of assembly lines of structure %u: %s", structureID, err.c_str());
            return false;
        }
        return true;
    }

    std::vector<std::pair<uint8, uint8>> contents;
    if (!sDataMgr.GetInstallationContents(typeID, contents))
        return false;

    /* new lines are free and usable by the owner corp only.  the corp configures them (UpdateAssemblyLineConfigurations) */
    std::ostringstream values;
    bool first(true);
    for (auto cur : contents)
        for (uint8 i = 0; i < cur.second; ++i) {
            if (!first)
                values << ",";
            first = false;
            values << "(" << (uint16)cur.first << ", " << structureID << ", 0, 0, 0, 0, " << (uint16)EvERam::RestrictionMask::ByCorp
                   << ", 0, 0, -10, -10, -10, 10, 10, " << ownerID << ", " << (uint16)sDataMgr.GetAssemblyLineActivity(cur.first) << ")";
        }
    if (first)
        return false;

    if (!sDatabase.RunQuery(err,
        "INSERT INTO ramAssemblyLines"
        " (assemblyLineTypeID, containerID, nextFreeTime, UIGroupingID, costInstall, costPerHour, restrictionMask,"
        " discountPerGoodStandingPoint, surchargePerBadStandingPoint, minimumStanding, minimumCharSecurity, minimumCorpSecurity,"
        " maximumCharSecurity, maximumCorpSecurity, ownerID, activityID)"
        " VALUES %s", values.str().c_str()))
    {
        _log(DATABASE__ERROR, "Failed to create assembly lines of structure %u: %s", structureID, err.c_str());
        return false;
    }

    _log(MANUF__MESSAGE, "Created assembly lines for structure %u (type %u, owner %u).", structureID, typeID, ownerID);
    return true;
}

void FactoryDB::PauseJobsInContainer(uint32 containerID)
{
    DBerror err;
    double now(GetFileTimeNow());
    if (!sDatabase.RunQuery(err,
        "UPDATE ramJobs AS job"
        " JOIN ramAssemblyLines AS line ON line.assemblyLineID = job.assemblyLineID"
        " SET job.pauseProductionTime = %.0f"
        " WHERE line.containerID = %u AND job.completedStatusID = 0"
        " AND job.pauseProductionTime IS NULL AND job.endProductionTime > %.0f",
        now, containerID, now))
        _log(DATABASE__ERROR, "Failed to pause jobs in container %u: %s", containerID, err.c_str());
}

void FactoryDB::ResumeJobsInContainer(uint32 containerID)
{
    DBerror err;
    double now(GetFileTimeNow());
    // jobs are moved on by the time they were paused (the order of multi-table assignments is not guaranteed, so two steps)
    if (!sDatabase.RunQuery(err,
        "UPDATE ramJobs AS job"
        " JOIN ramAssemblyLines AS line ON line.assemblyLineID = job.assemblyLineID"
        " SET job.beginProductionTime = IF(job.beginProductionTime > job.pauseProductionTime,"
        "         job.beginProductionTime + (%.0f - job.pauseProductionTime), job.beginProductionTime),"
        "     job.endProductionTime = job.endProductionTime + (%.0f - job.pauseProductionTime)"
        " WHERE line.containerID = %u AND job.completedStatusID = 0"
        " AND job.pauseProductionTime IS NOT NULL AND job.pauseProductionTime < %.0f",
        now, now, containerID, now))
    {
        _log(DATABASE__ERROR, "Failed to move jobs in container %u: %s", containerID, err.c_str());
        return;
    }
    if (!sDatabase.RunQuery(err,
        "UPDATE ramJobs AS job"
        " JOIN ramAssemblyLines AS line ON line.assemblyLineID = job.assemblyLineID"
        " SET job.pauseProductionTime = NULL"
        " WHERE line.containerID = %u AND job.completedStatusID = 0 AND job.pauseProductionTime IS NOT NULL",
        containerID))
    {
        _log(DATABASE__ERROR, "Failed to resume jobs in container %u: %s", containerID, err.c_str());
        return;
    }
    // lines are free when their last job ends
    if (!sDatabase.RunQuery(err,
        "UPDATE ramAssemblyLines AS line"
        " SET line.nextFreeTime = COALESCE((SELECT MAX(job.endProductionTime) FROM ramJobs AS job"
        "     WHERE job.assemblyLineID = line.assemblyLineID AND job.completedStatusID = 0), 0)"
        " WHERE line.containerID = %u",
        containerID))
        _log(DATABASE__ERROR, "Failed to update next free time of lines in container %u: %s", containerID, err.c_str());
}

void FactoryDB::GetActiveJobsInContainer(uint32 containerID, std::vector<EvERam::JobProperties>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT job.jobID FROM ramJobs AS job"
        " JOIN ramAssemblyLines AS line ON line.assemblyLineID = job.assemblyLineID"
        " WHERE line.containerID = %u AND job.completedStatusID = 0",
        containerID))
    {
        _log(DATABASE__ERROR, "Failed to query jobs in container %u: %s", containerID, res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        EvERam::JobProperties data = EvERam::JobProperties();
        if (GetJobProperties(row.GetUInt(0), data))
            into.push_back(data);
    }
}

bool FactoryDB::UpdateAssemblyLine(uint32 assemblyLineID, const EvERam::LineConfig& data)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "UPDATE ramAssemblyLines SET"
        " UIGroupingID = 0, costInstall = %f, costPerHour = %f, restrictionMask = %u,"
        " discountPerGoodStandingPoint = %f, surchargePerBadStandingPoint = %f, minimumStanding = %f,"
        " minimumCharSecurity = %f, minimumCorpSecurity = %f, maximumCharSecurity = %f, maximumCorpSecurity = %f"
        " WHERE assemblyLineID = %u",
        data.costInstall, data.costPerHour, data.restrictionMask,
        data.discountPerGoodStandingPoint, data.surchargePerBadStandingPoint, data.minimumStanding,
        data.minimumCharSecurity, data.minimumCorpSecurity, data.maximumCharSecurity, data.maximumCorpSecurity,
        assemblyLineID))
    {
        _log(DATABASE__ERROR, "Failed to update assembly line %u: %s", assemblyLineID, err.c_str());
        return false;
    }
    return true;
}
