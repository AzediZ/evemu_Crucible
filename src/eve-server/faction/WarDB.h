/**
 * @name WarDB.h
 *   the crpWars table (wars between player corporations and alliances)
 */

#ifndef EVEMU_FACTION_WARDB_H_
#define EVEMU_FACTION_WARDB_H_

#include "eve-server.h"

namespace War {
    struct Data;
}

class WarDB
{
public:
    // creates the table if the migration hasn't been run
    static void CreateTable();
    // wars not finished, or finished after `since`
    static bool LoadWars(int64 since, std::vector<War::Data>& into);
    // returns the new warID (0 = failed)
    static uint32 InsertWar(const War::Data& w);
    static void SaveWar(const War::Data& w);
    // warID, declaredByID, againstID, timeDeclared, timeFinished, retracted, retractedBy, billID, mutual
    static PyRep* GetWarsRowset(uint32 ownerID, int64 since);
    // player corporations that are alliance members
    static void LoadCorpAlliances(std::map<uint32, uint32>& into);
    // a player corporation or alliance that hasn't closed
    static bool IsOwnerAlive(uint32 ownerID);
};

#endif  // EVEMU_FACTION_WARDB_H_
