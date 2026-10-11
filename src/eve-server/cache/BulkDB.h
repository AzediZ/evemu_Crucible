/**
 * @name BulkDB.h
 *   This file is for loading bulkdata from the db
 *   Copyright 2017  Alasiya-EVEmu Team
 *
 * @Author:    Allan
 * @date:      24 January 2017
 *
 * @update:    table-driven file set, dynamic chunking and a version derived from the tables (F04)
 */


#ifndef _EVE_CACHE_BULKDB_H_
#define _EVE_CACHE_BULKDB_H_


#include "../eve-server.h"


/* one bulkdata file the server sends to the client (replaces the client's own copy in its bulkdata folder) */
struct BulkFileDef {
    int32 fileID;
    const char* table;
    const char* select;         // full select, without ORDER BY/LIMIT
    const char* orderBy;        // stable order, so LIMIT windows never skip or repeat rows
    uint32 rowsPerChunk;
};

/* one chunk of a GetFullFiles transfer:  the file parts it carries, and the files whose last part is in it */
struct BulkChunk {
    std::vector<std::pair<int32, uint16>> parts;    // fileID/part
    std::vector<int32> ending;
};

class BulkDB
: public Singleton<BulkDB>
{
public:
    BulkDB();
    ~BulkDB();

    // counts rows, splits files into parts, computes the version.  parts are preloaded unless server.BulkDataOD
    void Initialize();
    void Close();

    int32 GetChangeID();
    // fileIDs served by this server
    void GetFileIDs(std::vector<int32>& into);
    bool IsServed(int32 fileID);

    /* chunk plan (set) for these files.  set 0 is every served file.  returns false when no more sets can be made
     *  chunk 0 holds every single-part file and the first part of the first multi-part file, every other part has its own chunk
     */
    bool GetPlan(const std::vector<int32>& fileIDs, uint8& setID);
    uint16 GetChunkCount(uint8 setID);
    /* toBeChanged dict {fileID: CRowset} for one chunk of a set, and the files that end in it (nullptr when none).
     *  returns nullptr for an unknown set or chunk
     */
    PyDict* BuildChunk(uint8 setID, uint16 chunk, PyList*& ending);

private:
    struct BulkFile {
        const BulkFileDef* def;
        uint32 rowCount;
        uint16 parts;
    };

    // one part of one file (cached).  the returned ref is the caller's (already PyIncRef'd)
    PyRep* GetPart(int32 fileID, uint16 part);
    void ComputeChangeID();

    bool m_loaded;
    int32 m_changeID;

    std::map<int32, BulkFile> m_files;                          // fileID/file
    std::map<std::pair<int32, uint16>, PyRep*> m_parts;         // fileID,part/CRowset (we own one ref)
    std::vector<std::vector<BulkChunk>> m_plans;                // setID/chunks
    std::map<std::string, uint8> m_planKeys;                    // sorted fileIDs/setID
};

#define sBulkDB \
( BulkDB::get() )

#endif  // _EVE_CACHE_BULKDB_H_
