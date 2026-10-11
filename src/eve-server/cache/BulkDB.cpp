/**
 * @name BulkDB.cpp
 *   This file is for loading bulkdata from the db
 *   Copyright 2017  EVEmu Team
 *
 * @Author:    Allan
 * @date:      24 January 2017
 *
 * @update:    table-driven file set, dynamic chunking and a version derived from the tables (F04)
 */


#include "cache/BulkDB.h"
#include "EVEServerConfig.h"
#include "utils/crc32.h"


/* updated files to send to client in bulkdata.  everything else comes from the client's own bulkdata folder.
 *
 *  dgmOperands = 800002,                -74
 *  dgmExpressions = 800003,             -17758
 *  dgmAttributes = 800004,              -1791
 *  dgmEffects = 800005,                 -2854
 *  dgmTypeAttributes = 800006,          -353290
 *  dgmTypeEffects = 800007,             -33777
 */
static const BulkFileDef s_bulkFiles[] = {
    { 800002, "dgmOperands",
        "SELECT operandID, operandKey, description, format, arg1categoryID, arg2categoryID, resultCategoryID, pythonFormat FROM dgmOperands",
        "operandID", 10000 },
    { 800003, "dgmExpressions",
        "SELECT expressionID, operandID, arg1, arg2, expressionValue, description, expressionName, expressionTypeID, expressionGroupID, expressionAttributeID"
        " FROM dgmExpressions",
        "expressionID", 9000 },
    { 800004, "dgmAttributeTypes",
        "SELECT attributeID, attributeName, attributeCategory, description, maxAttributeID, attributeIdx, "
        "chargeRechargeTimeID, defaultValue, published, unitID, displayName, displayNameID, stackable, highIsGood, iconID, dataID FROM dgmAttributeTypes",
        "attributeID", 10000 },
    { 800005, "dgmEffects",
        "SELECT effectID, effectName, displayNameID, descriptionID, dataID, effectCategory, preExpression, postExpression,"
        " description, guid, isOffensive, isAssistance, durationAttributeID, trackingSpeedAttributeID, dischargeAttributeID,"
        " rangeAttributeID, falloffAttributeID, disallowAutoRepeat, published, displayName, isWarpSafe, rangeChance,"
        " electronicChance, propulsionChance, distribution, sfxName, npcUsageChanceAttributeID, "
        " npcActivationChanceAttributeID, fittingUsageChanceAttributeID, iconID, modifierInfo FROM dgmEffects",
        "effectID", 10000 },
    { 800006, "dgmTypeAttributes",
        "SELECT typeID, attributeID, IF(valueInt IS NULL, valueFloat, valueInt) AS value FROM dgmTypeAttributes",
        "typeID, attributeID", 10000 },
    { 800007, "dgmTypeEffects",
        "SELECT typeID,effectID,isDefault FROM dgmTypeEffects",
        "typeID, effectID", 9000 }
};


BulkDB::BulkDB()
: m_loaded(false),
  m_changeID(bulkDataChangeID)
{
    m_files.clear();
    m_parts.clear();
    m_plans.clear();
    m_planKeys.clear();
}

BulkDB::~BulkDB()
{
    //Close();
}

void BulkDB::Close()
{
    for (auto cur : m_parts)
        PySafeDecRef(cur.second);

    m_parts.clear();
    m_plans.clear();
    m_planKeys.clear();
    m_files.clear();
    m_loaded = false;
    sLog.Warning("      BulkDataMgr", "Bulk Data Manager has been closed." );
}

void BulkDB::Initialize()
{
    if (m_loaded)
        return;

    double start = GetTimeMSeconds();

    // split every file into parts of at most rowsPerChunk rows
    for (const auto& def : s_bulkFiles) {
        BulkFile file = BulkFile();
        file.def = &def;
        file.rowCount = 0;
        DBQueryResult res;
        DBResultRow row;
        if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM %s", def.table)) {
            codelog(DATABASE__ERROR, "Error counting rows of %s: %s", def.table, res.error.c_str());
        } else if (res.GetRow(row)) {
            file.rowCount = row.GetUInt(0);
        }
        file.parts = (uint16)((file.rowCount + def.rowsPerChunk - 1) / def.rowsPerChunk);
        if (file.parts < 1)
            file.parts = 1;
        m_files[def.fileID] = file;
    }

    ComputeChangeID();

    // set 0 = all served files
    std::vector<int32> all;
    GetFileIDs(all);
    m_loaded = true;
    uint8 setID(0);
    GetPlan(all, setID);

    uint32 count(0);
    if (!sConfig.server.BulkDataOD) {
        for (auto& cur : m_files)
            for (uint16 i = 0; i < cur.second.parts; ++i) {
                PySafeDecRef(GetPart(cur.first, i));
                ++count;
            }
    }

    sLog.Cyan("      BulkDataMgr", "%u BulkData files in %u chunks (version %i), %u parts preloaded in %.3fms.",
              m_files.size(), GetChunkCount(0), m_changeID, count, (GetTimeMSeconds() - start));
}

void BulkDB::ComputeChangeID()
{
    m_changeID = bulkDataChangeID;
    if (!sConfig.server.BulkDataAutoVersion)
        return;

    /* fold the table checksums (and the served file layout) into one number.
     *  the client deletes its cached bulkdata when its system folder has a higher changeID than the cache
     *  (bulkSvc.CheckCacheVersion), so keep the result above any real changeID:  0x40000000 + 30 bits
     */
    uint32 crc(0xFFFFFFFF);
    std::string base(std::to_string(bulkDataChangeID));
    crc = CRC32::Update((const uint8*)base.c_str(), base.size(), crc);
    for (const auto& def : s_bulkFiles) {
        DBQueryResult res;
        DBResultRow row;
        if (!sDatabase.RunQuery(res, "CHECKSUM TABLE %s", def.table) or !res.GetRow(row) or row.IsNull(1)) {
            _log(DATABASE__ERROR, "BulkDB: CHECKSUM TABLE %s failed.  Using the compiled-in bulkdata version.", def.table);
            m_changeID = bulkDataChangeID;
            return;
        }
        std::string sum(def.table);
        sum += ":";
        sum += row.GetText(1);
        sum += ":";
        sum += def.select;
        crc = CRC32::Update((const uint8*)sum.c_str(), sum.size(), crc);
    }
    m_changeID = (int32)(0x40000000 | (CRC32::Finish(crc) & 0x3FFFFFFF));
}

int32 BulkDB::GetChangeID()
{
    if (!m_loaded)
        Initialize();
    return m_changeID;
}

void BulkDB::GetFileIDs(std::vector<int32>& into)
{
    for (const auto& def : s_bulkFiles)
        into.push_back(def.fileID);
}

bool BulkDB::IsServed(int32 fileID)
{
    for (const auto& def : s_bulkFiles)
        if (def.fileID == fileID)
            return true;
    return false;
}

bool BulkDB::GetPlan(const std::vector<int32>& fileIDs, uint8& setID)
{
    if (!m_loaded)
        Initialize();

    std::vector<int32> ids(fileIDs);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    std::string key;
    for (auto cur : ids) {
        if (!key.empty())
            key += ",";
        key += std::to_string(cur);
    }

    std::map<std::string, uint8>::iterator itr = m_planKeys.find(key);
    if (itr != m_planKeys.end()) {
        setID = itr->second;
        return true;
    }
    if (m_plans.size() > 255) {
        _log(BULKDATA__ERROR, "BulkDB::GetPlan() - too many chunk sets.");
        return false;
    }

    std::vector<BulkChunk> plan;
    plan.push_back(BulkChunk());
    bool firstMulti(true);
    for (auto id : ids) {
        std::map<int32, BulkFile>::iterator fItr = m_files.find(id);
        if (fItr == m_files.end())
            continue;
        uint16 parts = fItr->second.parts;
        if (parts == 1) {
            plan[0].parts.push_back(std::make_pair(id, (uint16)0));
            plan[0].ending.push_back(id);
            continue;
        }
        uint16 first(0);
        if (firstMulti) {
            plan[0].parts.push_back(std::make_pair(id, (uint16)0));
            first = 1;
            firstMulti = false;
        }
        for (uint16 i = first; i < parts; ++i) {
            BulkChunk chunk = BulkChunk();
            chunk.parts.push_back(std::make_pair(id, i));
            if (i == parts - 1)
                chunk.ending.push_back(id);
            plan.push_back(chunk);
        }
    }

    setID = (uint8)m_plans.size();
    m_plans.push_back(plan);
    m_planKeys[key] = setID;
    return true;
}

uint16 BulkDB::GetChunkCount(uint8 setID)
{
    if (setID >= m_plans.size())
        return 0;
    return (uint16)m_plans[setID].size();
}

PyDict* BulkDB::BuildChunk(uint8 setID, uint16 chunk, PyList*& ending)
{
    ending = nullptr;
    if (!m_loaded)
        Initialize();
    if ((setID >= m_plans.size()) or (chunk >= m_plans[setID].size()))
        return nullptr;

    const BulkChunk& data = m_plans[setID][chunk];
    PyDict* dict = new PyDict();
    for (auto& cur : data.parts) {
        PyRep* rowset = GetPart(cur.first, cur.second);
        if (rowset == nullptr)
            continue;
        PyInt* key = new PyInt(cur.first);
        if (dict->GetItem(key) != nullptr) {
            // the plan never puts two parts of one file in a chunk
            _log(BULKDATA__ERROR, "BulkDB::BuildChunk() - set %u chunk %u has file %i twice.", setID, chunk, cur.first);
            PyDecRef(key);
            PyDecRef(rowset);
            continue;
        }
        dict->SetItem(key, rowset);     // consumes our ref
    }

    if (!data.ending.empty()) {
        ending = new PyList();
        for (auto cur : data.ending)
            ending->AddItem(new PyInt(cur));
    }
    return dict;
}

PyRep* BulkDB::GetPart(int32 fileID, uint16 part)
{
    std::pair<int32, uint16> key(fileID, part);
    std::map<std::pair<int32, uint16>, PyRep*>::iterator itr = m_parts.find(key);
    if (itr != m_parts.end()) {
        PyIncRef(itr->second);
        return itr->second;
    }

    std::map<int32, BulkFile>::iterator fItr = m_files.find(fileID);
    if (fItr == m_files.end())
        return nullptr;

    const BulkFileDef* def = fItr->second.def;
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "%s ORDER BY %s LIMIT %u, %u",
            def->select, def->orderBy, (uint32)part * def->rowsPerChunk, def->rowsPerChunk))
    {
        codelog(DATABASE__ERROR, "Error loading bulkdata %i (%s) part %u: %s", fileID, def->table, part, res.error.c_str());
        return nullptr;
    }

    PyRep* rowset = DBResultToCRowset(res);
    m_parts[key] = rowset;      // our ref
    PyIncRef(rowset);           // the caller's ref
    return rowset;
}
