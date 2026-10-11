/**
 * @name BulkMgrService.cpp
 *   This file is for processing bulkdata
 *   Copyright 2017  Alasiya-EVEmu Team
 *
 * @Author:    ozatomic (hacked for static client data)
 * @Author:    Allan (added calls and (hacked) updates for new dgm data)
 * @date:      24 January 2017
 *
 */


/** @todo  this system will need to work with objectCache, as those ARE the bulkdata files sent to the client.
 *  they are already packed and loaded into the server.  this system ive created for bulkData is redundant.
 * will have to look into sorting/sending to client eventually, using hash comparison to determine what files to send. (if possible)
 * for now, we'll leave this as-is, because it seems to be working ok, and i *MAY NOT* have complete data for all the bulkfiles
 */

/** @update  the client install has default bulkdata files found in the eve/bulkdata directory.  need to get these files and compare with
 * what we have to see what needs to be updated, and go from there.
 * next comment has all data used by client (found in code)
 */

/** @todo  @update  used by client...
 *
        self.dgmunits = self.LoadBulkIndex('dgmunits', const.cacheDogmaUnits, 'unitID')
        self.invbptypes = self.LoadBulkIndex('invbptypes', const.cacheInvBlueprintTypes, 'blueprintTypeID')
        self.invcategories = self.LoadBulkIndex('invcategories', const.cacheInvCategories, 'categoryID', InvCategory)
        self.invmetagroups = self.LoadBulkIndex('invmetagroups', const.cacheInvMetaGroups, 'metaGroupID', InvMetaGroup)
        self.invgroups = self.LoadBulkIndex('invgroups', const.cacheInvGroups, 'groupID', InvGroup)
        self.invtypes = self.LoadBulkIndex('invtypes', const.cacheInvTypes, 'typeID', InvType)
        self.invtypereactions = self.LoadBulkFilter('invtypereactions', const.cacheInvTypeReactions, 'reactionTypeID')
        self.invmetatypes = self.LoadBulkIndex('invmetatypes', const.cacheInvMetaTypes, 'typeID')
        self.invmetatypesByParent = self.LoadBulkFilter('invmetatypesByParent', const.cacheInvMetaTypes, 'parentTypeID')
        invcontrabandtypes = self.LoadBulk(None, const.cacheInvContrabandTypes)

        npccharacters = self.LoadBulkIndex(None, const.cacheChrNpcCharacters, 'characterID')

        self.regions = self.LoadBulkIndex('regions', const.cacheMapRegionsTable, 'regionID', Region)
        self.constellations = self.LoadBulkIndex('constellations', const.cacheMapConstellationsTable, 'constellationID', Constellation)
        self.solarsystems = self.LoadBulkIndex('solarsystems', const.cacheMapSolarSystemsTable, 'solarSystemID', SolarSystem)
        self.stations = self.LoadBulk('stations', const.cacheStaStationsStatic, Recordset(sys.Row, 'stationID', 'GetStationEx', 'GetMultiStationEx'))

        self.messages = self.LoadBulkIndex('messages', const.cacheEveMessages, 'messageKey')
        self.graphics = self.LoadBulkIndex('graphics', const.cacheResGraphics, 'graphicID')
        self.icons = self.LoadBulkIndex('icons', const.cacheResIcons, 'iconID')
        self.sounds = self.LoadBulkIndex('sounds', const.cacheResSounds, 'soundID')
        self.detailMeshes = self.LoadBulkIndex('detailMeshes', const.cacheResDetailMeshes, 'detailGraphicID')
        self.detailMeshesByTarget = self.LoadBulkFilter('detailMeshesByTarget', const.cacheResDetailMeshes, 'targetGraphicID')
        self._worldspacesDistrictsCache = {}
        self.worldspaces = self.LoadBulkIndex('worldspaces', const.cacheWorldSpaces, 'worldSpaceTypeID', sys.Worldspaces)
        self.entitySpawns = self.LoadBulkIndex('entitySpawns', const.cacheEntitySpawns, 'spawnID')
        self.entitySpawnsByWorldSpaceID = self.LoadBulkFilter('entitySpawnsByWorldSpaceID', const.cacheEntitySpawns, 'worldSpaceTypeID')
        self.entitySpawnsByRecipeID = self.LoadBulkFilter('entitySpawnsByRecipeID', const.cacheEntitySpawns, 'recipeID')
        self.entityIngredientInitialValues = self.LoadBulkFilter('entityIngredientInitialValues', const.cacheEntityIngredientInitialValues, 'ingredientID')
        self.entityIngredientsByParentID = self.LoadBulkFilter('entityIngredientsByParentID', const.cacheEntityIngredients, 'parentID')
        self.entityIngredientsByRecipeID = self.LoadBulkFilter('entityIngredientsByRecipeID', const.cacheEntityIngredients, 'recipeID')
        self.ingredients = self.LoadBulkIndex('ingredients', const.cacheEntityIngredients, 'ingredientID')
        self.entitySpawnGroups = self.LoadBulkIndex('entitySpawnGroups', const.cacheEntitySpawnGroups, 'spawnGroupID')
        self.entitySpawnsBySpawnGroup = self.LoadBulkFilter('entitySpawnsBySpawnGroup', const.cacheEntitySpawnGroupLinks, 'spawnGroupID')
        self.recipes = self.LoadBulkIndex('recipes', const.cacheEntityRecipes, 'recipeID')
        self.recipesByParentRecipeID = self.LoadBulkFilter('recipesByParentRecipeID', const.cacheEntityRecipes, 'parentRecipeID')
        self.treeNodes = self.LoadBulkIndex('treeNodes', const.cacheTreeNodes, 'treeNodeID')
        self.treeLinks = self.LoadBulkFilter('treeLinks', const.cacheTreeLinks, 'parentTreeNodeID')
        self.treeNodeProperties = self.LoadBulkIndex('treeNodeProperties', const.cacheTreeProperties, 'propertyID')
        self.actionTreeSteps = self.LoadBulkFilter('actionTreeSteps', const.cacheActionTreeSteps, 'actionID')
        self.actionTreeProcs = self.LoadBulkFilter('actionTreeProcs', const.cacheActionTreeProcs, 'actionID')
        self.actionObjects = self.LoadBulkIndex('actionObjects', const.cacheActionObjects, 'actionObjectID')
        self.actionStations = self.LoadBulkIndex('actionStations', const.cacheActionStations, 'actionStationTypeID')
        self.actionStationActions = self.LoadBulkFilter('actionStationActions', const.cacheActionStationActions, 'actionStationTypeID')
        self.actionObjectStations = self.LoadBulkFilter('actionObjectStations', const.cacheActionObjectStations, 'actionObjectID')
        self.actionObjectExits = self.LoadBulkFilter('actionObjectExits', const.cacheActionObjectExits, ['actionObjectID', 'actionStationInstanceID', True])
        self.paperdollModifierLocations = self.LoadBulkIndex('paperdollModifierLocations', const.cachePaperdollModifierLocations, 'modifierLocationID')
        self.paperdollResources = self.LoadBulkIndex('paperdollResources', const.cachePaperdollResources, 'paperdollResourceID')
        self.paperdollSculptingLocations = self.LoadBulkIndex('paperdollSculptingLocations', const.cachePaperdollSculptingLocations, 'sculptLocationID')
        self.paperdollColors = self.LoadBulkIndex('paperdollColors', const.cachePaperdollColors, 'colorID')
        self.paperdollColorNames = self.LoadBulkIndex('paperdollColorNames', const.cachePaperdollColorNames, 'colorNameID')
        self.paperdollColorRestrictions = self.LoadBulkFilter('paperdollColorRestrictions', const.cachePaperdollColorRestrictions, 'colorNameID')
        self.perceptionSenses = self.LoadBulkIndex('perceptionSenses', const.cachePerceptionSenses, 'senseID')
        self.perceptionStimTypes = self.LoadBulkIndex('perceptionStimTypes', const.cachePerceptionStimTypes, 'stimTypeID')
        self.perceptionSubjects = self.LoadBulkIndex('perceptionSubjects', const.cachePerceptionSubjects, 'subjectID')
        self.perceptionTargets = self.LoadBulkIndex('perceptionTargets', const.cachePerceptionTargets, 'targetID')
        self.perceptionBehaviorSenses = self.LoadBulk('perceptionBehaviorSenses', const.cachePerceptionBehaviorSenses)
        self.perceptionBehaviorDecays = self.LoadBulk('perceptionBehaviorDecays', const.cachePerceptionBehaviorDecays)
        self.perceptionBehaviorFilters = self.LoadBulk('perceptionBehaviorFilters', const.cachePerceptionBehaviorFilters)
        self.encounters = self.LoadBulkIndex('encounters', const.cacheEncounterEncounters, 'encounterID')
        self.encounterCoordinateSets = self.LoadBulkIndex('encounterCoordinateSets', const.cacheEncounterCoordinateSets, 'coordinateSetID')
        self.encounterCoordinatesBySet = self.LoadBulkFilter('encounterCoordinatesBySet', const.cacheEncounterCoordinates, 'coordinateSetID')
        self.encounterCoordinates = self.LoadBulkIndex('encounterCoordinates', const.cacheEncounterCoordinates, 'coordinateID')
        self.encounterCoordinateSetsByWorldSpaceID = self.LoadBulkFilter('encounterCoordinateSetsByWorldSpaceID', const.cacheEncounterCoordinateSets, 'worldSpaceTypeID')
        self.encountersByCoordinateSet = self.LoadBulkFilter('encountersByCoordinateSet', const.cacheEncounterEncounters, 'coordinateSetID')

 *
 */

#include "eve-server.h"


#include "cache/BulkDB.h"
#include "cache/BulkMgrService.h"


BulkMgrService::BulkMgrService() :
    Service("bulkMgr", eAccessLevel_User)
{
    this->Add("GetChunk", &BulkMgrService::GetChunk);
    this->Add("UpdateBulk", &BulkMgrService::UpdateBulk);
    this->Add("GetVersion", &BulkMgrService::GetVersion);
    this->Add("GetFullFiles", &BulkMgrService::GetFullFiles);
    this->Add("GetAllBulkIDs", &BulkMgrService::GetAllBulkIDs);
    this->Add("GetFullFilesChunk", &BulkMgrService::GetFullFilesChunk);
    this->Add("GetUnsubmittedChunk", &BulkMgrService::GetUnsubmittedChunk);
    this->Add("GetUnsubmittedChanges", &BulkMgrService::GetUnsubmittedChanges);
}

/*
BULKDATA__ERROR=1
BULKDATA__WARNING=0
BULKDATA__MESSAGE=0
BULKDATA__DEBUG=0
BULKDATA__INFO=0
BULKDATA__TRACE=0
BULKDATA__DUMP=0
*/
PyResult BulkMgrService::UpdateBulk(PyCallArgs &call, PyInt* changeID, std::optional<PyString*> hashValue, PyInt* branch)
{
    /*  updateData = self.bulkMgr.UpdateBulk(changeID, hashValue, branch)
     *    'type', 'allowUnsubmitted', 'version' and optionally 'data'
     *  versioning is by changeID only.  the hash is an md5 of the client's bulk file ids (cache + system folder),
     *  which the server cannot reproduce, so it is ignored.  a different changeID makes the client fetch every file
     *  this server serves (TooManyRevisions -> GetFullFiles(None)); its system-folder files stay as they are.
     */
    int32 serverChangeID = sBulkDB.GetChangeID();

    PyDict* res = new PyDict();
    if (changeID->value() != serverChangeID) {
        res->SetItemString("type", new PyInt(updateBulkStatusTooManyRevisions));
    } else if (branch->value() != bulkDataBranch) {
        res->SetItemString("type", new PyInt(updateBulkStatusWrongBranch));
    } else {
        res->SetItemString("type", new PyInt(updateBulkStatusOK));
    }

    res->SetItemString("version", new PyInt(serverChangeID));
    res->SetItemString("allowUnsubmitted", PyStatic.NewFalse());

    if (is_log_enabled(BULKDATA__TRACE))
        res->Dump(BULKDATA__TRACE, "  ");

    return res;
}

PyResult BulkMgrService::GetFullFiles(PyCallArgs &call, std::optional<PyList*> toGet)
{
    /*  toBeChanged, bulksEndingInChunk, numberOfChunks, chunkSetID, self.allowUnsubmitted = self.bulkMgr.GetFullFiles(toGet)
     *    toGet None = everything.  toBeChanged is chunk 0 of the set:  {fileID: CRowset}.
     *    the client appends the rows of later chunks (GetFullFilesChunk) per fileID and saves a file when its id
     *    is in bulksEndingInChunk.
     */
    std::vector<int32> fileIDs;
    if (toGet.has_value()) {
        PyList::const_iterator itr = toGet.value()->begin(), end = toGet.value()->end();
        for (; itr != end; ++itr) {
            int32 fileID = (int32)PyRep::IntegerValue(*itr);
            if (sBulkDB.IsServed(fileID))
                fileIDs.push_back(fileID);
        }
    } else {
        sBulkDB.GetFileIDs(fileIDs);
    }

    PyTuple* response = new PyTuple(5);
    uint8 setID(0);
    if (fileIDs.empty() or !sBulkDB.GetPlan(fileIDs, setID)) {
        // nothing we serve:  the client loop does nothing
        response->SetItem(0, new PyDict());
        response->SetItem(1, PyStatic.NewNone());
        response->SetItem(2, PyStatic.NewZero());
        response->SetItem(3, PyStatic.NewZero());
        response->SetItem(4, PyStatic.NewFalse());
        return response;
    }

    PyList* ending(nullptr);
    PyDict* toBeChanged = sBulkDB.BuildChunk(setID, 0, ending);
    if (toBeChanged == nullptr)
        toBeChanged = new PyDict();

    response->SetItem(0, toBeChanged);
    response->SetItem(1, (ending == nullptr ? PyStatic.NewNone() : ending));
    response->SetItem(2, new PyInt(sBulkDB.GetChunkCount(setID)));  //numberOfChunks
    response->SetItem(3, new PyInt(setID));                         //chunkSetID
    response->SetItem(4, PyStatic.NewFalse());                      //allowUnsubmitted isnt supported

    _log(BULKDATA__INFO, "BulkMgrService::GetFullFiles(): %u files in set %u (%u chunks)", fileIDs.size(), setID, sBulkDB.GetChunkCount(setID));
    if (is_log_enabled(BULKDATA__TRACE))
        response->Dump(BULKDATA__TRACE, "  ");

    return response;
}

PyResult BulkMgrService::GetFullFilesChunk(PyCallArgs &call, PyInt* chunkSetID, PyInt* chunkNumber)
{
    //  toBeChanged, bulksEndingInChunk = self.bulkMgr.GetFullFilesChunk(chunkSetID, chunkNumber)
    PyTuple* response = new PyTuple(2);
    PyList* ending(nullptr);
    PyDict* toBeChanged(nullptr);
    if ((chunkSetID->value() >= 0) and (chunkSetID->value() < 256) and (chunkNumber->value() >= 0) and (chunkNumber->value() < 65536))
        toBeChanged = sBulkDB.BuildChunk((uint8)chunkSetID->value(), (uint16)chunkNumber->value(), ending);
    if (toBeChanged == nullptr) {
        _log(BULKDATA__ERROR, "BulkMgrService::GetFullFilesChunk(): unknown chunkSetID %i / chunkNumber %i", chunkSetID->value(), chunkNumber->value());
        toBeChanged = new PyDict();
    }

    _log(BULKDATA__INFO, "BulkMgrService::GetFullFilesChunk(): chunkSetID: %i, chunkNumber: %i", chunkSetID->value(), chunkNumber->value());
    response->SetItem(0, toBeChanged);
    response->SetItem(1, (ending == nullptr ? PyStatic.NewNone() : ending));
    return response;
}

PyResult BulkMgrService::GetVersion(PyCallArgs &call)
{
    // changeID, branch = self.bulkMgr.GetVersion()
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(sBulkDB.GetChangeID()));
        tuple->SetItem(1, new PyInt(bulkDataBranch));
    return tuple;
}

PyResult BulkMgrService::GetAllBulkIDs(PyCallArgs &call)
{
    // serverBulkIDs = self.bulkMgr.GetAllBulkIDs()   fileIDs this server sends (updated and edited dogma data)
    std::vector<int32> fileIDs;
    sBulkDB.GetFileIDs(fileIDs);
    PyList* list = new PyList();
    for (auto cur : fileIDs)
        list->AddItem(new PyInt(cur));
    return list;
}

PyResult BulkMgrService::GetChunk(PyCallArgs &call, PyInt* changeID, PyInt* chunkNumber)
{
    /*  toBeChanged = self.bulkMgr.GetChunk(changeID, chunkNumber)
     *  only used for row-level updates (updateBulkStatusNeedToUpdate), which this server never sends
     */
    _log(BULKDATA__WARNING, "BulkMgrService::GetChunk(%i, %i) called, but row-level updates are not sent.", changeID->value(), chunkNumber->value());
    return new PyDict();
}

PyResult BulkMgrService::GetUnsubmittedChunk(PyCallArgs &call, PyInt* chunkNumber)
{
    //  toBeChanged = self.bulkMgr.GetUnsubmittedChunk(chunkNumber)   only after GetUnsubmittedChanges returned chunks
    return new PyDict();
}

PyResult BulkMgrService::GetUnsubmittedChanges(PyCallArgs &call)
{
    //  unsubmitted = self.bulkMgr.GetUnsubmittedChanges()   None = nothing (allowUnsubmitted is always False)
    return PyStatic.NewNone();
}
