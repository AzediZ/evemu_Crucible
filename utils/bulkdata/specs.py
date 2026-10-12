"""
specs.py - which client bulkdata table backs which server table, how their columns line up, and the per-table rules
gen_client_sync.py applies.

The rules (see README.md):
  * the client wins wherever it has a value and the server's differs;
  * where the client is empty (NULL, '', 'None', or one of a column's placeholders) the server's value stays, except
    in null_is_data columns, where the client's NULL means something (e.g. invTypes.marketGroupID: not on the market);
  * rows only the client has are inserted;  rows only the server has are deleted unless server_only='keep' or the
    key is in keep_keys.
"""


class Spec(object):
    def __init__(self, table, source, group, key=None, rename=None, ignore=(), only=None, transform=None,
                 natural_key=None, virtual=None, server_only='delete', keep_keys=(), placeholders=None,
                 null_is_data=(), add_columns=None, modify_columns=None, note=''):
        self.table = table
        self.source = source            # int bulk id, or ('mapbulk', 'SELECT ...')
        self.group = group              # the migration this table goes into
        self.key = key                  # server key columns;  None = the table's primary key
        self.rename = rename or {}      # client column -> server column
        self.ignore = set(ignore)       # server columns never compared or written
        self.only = set(only) if only else None     # compare / write only these server columns
        self.transform = transform      # client row (server column names) -> row, or None to drop it
        self.natural_key = natural_key  # for tables keyed by a surrogate auto-increment id
        self.virtual = virtual or {}    # client column -> (type, getter(server row)): compared, written by hand
        self.server_only = server_only  # 'delete' rows the client does not have, or 'keep' them
        self.keep_keys = set(keep_keys)  # server-only keys that stay anyway
        self.placeholders = placeholders or {}      # server column -> client values that mean "no data"
        self.null_is_data = set(null_is_data)       # server columns where a client NULL is written
        self.add_columns = add_columns or {}        # column -> (type, ddl):  the client has it, the server should
        # column -> (type, ddl, old ddl or None):  widened so the client's values fit;  None = Down keeps it wide
        self.modify_columns = modify_columns or {}
        self.note = note


def _attr_value(row):
    """dgmTypeAttributes keeps one value in valueInt or valueFloat;  the client has a single column"""
    return row['valueFloat'] if row.get('valueInt') is None else row['valueInt']


AGENT_COLUMNS = {'characterID': 'agentID', 'agentDivisionID': 'divisionID', 'stationID': 'locationID',
                 'agentLevel': 'level', 'agentType': 'agentTypeID', 'agentLocateCharacter': 'isLocator'}

# crpCorporation is a live table:  only the npc corporations' identity comes from the client.  memberLimit, shares,
# taxRate, allowedMemberRaceIDs, minimumJoinStanding ... are the server's game state (and the client's shares /
# memberLimit do not fit the server's unsigned columns).
NPC_CORP_IDENTITY = ('corporationName', 'tickerName', 'description', 'ceoID', 'stationID', 'raceID',
                     'shape1', 'shape2', 'shape3', 'color1', 'color2', 'color3')

SPECS = [
    # inventory
    Spec('invCategories', 600001, 'inventory'),
    Spec('invGroups', 600002, 'inventory'),
    Spec('invTypes', 600004, 'inventory', key=['typeID'], null_is_data=('marketGroupID',)),
    Spec('invTypeMaterials', 600005, 'inventory'),
    Spec('invMetaGroups', 600006, 'inventory'),
    Spec('invMetaTypes', 600007, 'inventory'),
    Spec('invContrabandTypes', 600008, 'inventory'),
    Spec('invTypeReactions', 600010, 'inventory'),
    Spec('invBlueprintTypes', 1200001, 'inventory', rename={'chanceOfReverseEngineering': 'chanceOfRE'}),
    Spec('shipTypes', 2000001, 'inventory'),

    # dogma.  the server sends these to the client as its bulkdata (cache/BulkDB.cpp), so they replace the client's
    Spec('dgmUnits', 800009, 'dogma'),
    Spec('eveUnits', 800009, 'dogma'),
    Spec('dgmAttributeTypes', 800004, 'dogma', keep_keys=[(0,)],
         modify_columns={'attributeIdx': ('smallint(6)', 'SMALLINT(6) NULL DEFAULT NULL',
                                          'TINYINT(3) NULL DEFAULT NULL')},
         note='attribute 0 is the server\'s "null" placeholder'),
    Spec('dgmEffects', 800005, 'dogma'),
    Spec('dgmExpressions', 800003, 'dogma',
         note='server-only expressions are deleted only when no remaining effect or expression uses them'),
    Spec('dgmTypeAttributes', 800006, 'dogma', virtual={'value': ('double', _attr_value)}),
    Spec('dgmTypeEffects', 800007, 'dogma'),

    # industry
    Spec('ramActivities', 1800003, 'industry'),
    Spec('ramAssemblyLineTypeDetailPerCategory', 1800004, 'industry'),
    Spec('ramAssemblyLineTypeDetailPerGroup', 1800005, 'industry'),
    Spec('ramAssemblyLineTypes', 1800006, 'industry'),
    Spec('ramCompletedStatuses', 1800007, 'industry', rename={'completedStatus': 'completedStatusID'}),
    Spec('ramTypeRequirements', 1800001, 'industry', rename={'recycle': 'extra'}),

    # map.  the client leaves mapSolarSystems.factionID empty (it gets sovereignty from the server), and names
    # w-space regions / constellations 'Unknown'
    Spec('mapRegions', 1400009, 'map', placeholders={'regionName': ('Unknown',)}),
    Spec('mapConstellations', 1400010, 'map', placeholders={'constellationName': ('Unknown',)}),
    Spec('mapSolarSystems', 1400011, 'map'),
    Spec('mapLocationWormholeClasses', 1400002, 'map'),
    Spec('mapCelestialDescriptions', 1400008, 'map', rename={'itemID': 'celestialID'}),
    Spec('mapDenormalize', ('mapbulk', 'SELECT celestialID AS itemID, celestialNameID AS itemNameID, solarSystemID, '
                            'typeID, groupID, radius, x, y, z, orbitID, orbitIndex, celestialIndex FROM celestials'),
         'map', server_only='keep',
         note='the client\'s celestials leave out regions, constellations, systems, stations, outposts and '
              'w-space secondary suns, which mapDenormalize also holds'),

    # stations
    Spec('staOperations', 2209999, 'stations'),
    Spec('staStations', 2209987, 'stations', server_only='keep',
         note='2209987 lists npc stations only;  mapbulk.db has the conquerable ones too'),
    Spec('staStations', ('mapbulk', 'SELECT stationID, solarSystemID, ownerID AS corporationID, typeID AS stationTypeID, '
                         'x, y, z, operationID, reprocessingEfficiency, reprocessingStationsTake, '
                         'reprocessingHangarFlag FROM npcStations'), 'stations'),

    # characters, corporations, factions
    Spec('chrRaces', 3200001, 'characters'),
    Spec('chrBloodlines', 3200002, 'characters'),
    Spec('chrBloodlineNames', 3200010, 'characters'),
    Spec('chrDefaultOverviews', 3200011, 'characters'),
    Spec('chrDefaultOverviewGroups', 3200012, 'characters', natural_key=['overviewID', 'groupID']),
    Spec('facFactions', 3200015, 'characters'),
    Spec('crpNPCCorporations', 2800006, 'characters'),
    Spec('crpCorporation', 2800006, 'characters', only=NPC_CORP_IDENTITY, server_only='keep',
         modify_columns={'description': ('varchar(4000)', "VARCHAR(4000) NOT NULL DEFAULT ''", None)},
         note='npc corporations\' identity only;  player corporations are never touched'),
    Spec('crpCorporation', 2809992, 'characters', only=NPC_CORP_IDENTITY, server_only='keep'),
    Spec('chrNPCCharacters', 3200016, 'characters', rename={'specialityID': 'careerSpecialityID'}),
    Spec('agtAgents', 3200016, 'characters', rename=AGENT_COLUMNS,
         transform=lambda r: r if r.get('agentTypeID') is not None else None),
    Spec('billTypes', 6400004, 'characters'),

    # certificates.  ObjCacheDB already selects crtRelationships.childTypeID, which the table did not have
    Spec('crtCertificates', 5100001, 'certificates'),
    Spec('crtRelationships', 5100004, 'certificates',
         add_columns={'childTypeID': ('int(11)', 'INT(11) NULL DEFAULT NULL AFTER `childID`')}),
    Spec('certificateRelationShips', 5100004, 'certificates'),

    # planetary interaction
    Spec('schematics', 7300004, 'planets'),
    Spec('piSchematics', 7300004, 'planets'),
    Spec('schematicsTypeMap', 7300005, 'planets', natural_key=['schematicID', 'typeID']),
    Spec('piTypeMap', 7300005, 'planets'),
    Spec('schematicsPinMap', 7300003, 'planets', natural_key=['schematicID', 'pinTypeID']),

    # graphics, icons, sounds, character creation resources
    Spec('graphics', 2001800001, 'resources'),
    Spec('eveGraphics', 2001800001, 'resources'),
    Spec('icons', 2001800004, 'resources'),
    Spec('eveIcons', 2001800004, 'resources'),
    Spec('sounds', 2001800002, 'resources'),
    Spec('paperdollModifierLocations', 2001600002, 'resources'),
    Spec('paperdollResources', 2001600003, 'resources'),
    Spec('paperdollSculptingLocations', 2001600004, 'resources'),
    Spec('paperdollColors', 2001600005, 'resources'),
    Spec('paperdollColorNames', 2001600006, 'resources'),
    Spec('paperdollColorRestrictions', 2001600007, 'resources', natural_key=['colorNameID', 'gender']),
]

# the migrations, in the order they run
GROUPS = ['inventory', 'dogma', 'industry', 'map', 'stations', 'characters', 'certificates', 'planets', 'resources']

# client bulkdata the server has no table for (listed in the report)
UNMAPPED = {
    1000001: 'eveMessages (the client\'s own UI messages)',
    1400016: 'mapNebulas (client scenery)',
    2001700035: 'worldSpaces (Incarna)',
    2001800005: 'resDetailMeshes (empty)',
    2001900002: 'actionTreeSteps (Incarna)', 2001900003: 'actionTreeProcs (Incarna)',
    2002200001: 'entityIngredients (Incarna)', 2002200002: 'entityIngredientInitialValues (Incarna)',
    2002200006: 'entitySpawns (Incarna)', 2002200009: 'entityRecipes (Incarna)',
    2002200010: 'entitySpawnGroups (empty)', 2002200011: 'entitySpawnGroupLinks (empty)',
    2002400001: 'actionObjects (Incarna)', 2002400002: 'actionStations (Incarna)',
    2002400003: 'actionStationActions (Incarna)', 2002400004: 'actionObjectStations (Incarna)',
    2002400005: 'actionObjectExits (Incarna)',
    2002500001: 'treeNodes (Incarna)', 2002500002: 'treeLinks (Incarna)', 2002500005: 'treeProperties (Incarna)',
    2002600001: 'perceptionSenses (Incarna)', 2002600002: 'perceptionStimTypes (Incarna)',
    2002600004: 'perceptionSubjects (Incarna)', 2002600005: 'perceptionTargets (Incarna)',
    2002600010: 'perceptionBehaviorSenses (Incarna)', 2002600011: 'perceptionBehaviorFilters (Incarna)',
    2002600012: 'perceptionBehaviorDecays (Incarna)',
    2003100001: 'encounterEncounters (empty)', 2003100002: 'encounterCoordinates (empty)',
    2003100003: 'encounterCoordinateSets (empty)',
}
