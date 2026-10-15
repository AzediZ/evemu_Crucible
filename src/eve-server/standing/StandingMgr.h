
 /**
  * @name StandingMgr.cpp
  *   memory object caching system for managing and saving standings data
  *   methods and functions relating to manipulation of standings
  *
  * @Author:        Allan
  * @date:      14 Novemeber 2018
  *
  */


#ifndef EVE_STANDING_STANDINGMGR_H
#define EVE_STANDING_STANDINGMGR_H

#include "../eve-server.h"

#include "../../eve-common/EVE_Standings.h"

#include "standing/StandingDB.h"


class StandingMgr
: public Singleton< StandingMgr >
{
public:
    StandingMgr();
    ~StandingMgr();

    int                 Initialize();

    void                Clear();
    void                Close()                         { Clear(); }
    void                GetInfo();

    PyObjectEx*         GetFactionStandings()           { PyIncRef(m_factionStandings); return m_factionStandings; }



    // a raw add (kept between -10 and +10), logged in repStandingChanges
    void                UpdateStandings(uint32 fromID, uint32 toID, uint16 eventType, double amount, std::string msg);

    /* a change as the client applies it (its standingsvc.OnStandingsModified):  change is the fraction of the way to +10, or
     *  to -10 for a loss.  new = s + (10 - s) x change, or s + (10 + s) x change, and a gain stops at maxAbs (a loss at
     *  minAbs).  saved, logged, and the client's row (fromID, toID, change, minAbs/10, maxAbs/10) added to notify if given */
    void                ModifyStanding(uint32 fromID, uint32 toID, uint16 eventType, double change, const std::string& msg,
                                       PyList* notify = nullptr, float minAbs = -10.0f, float maxAbs = 10.0f);
    /* the same for an npc faction, and the derived changes (standings.DerivedStandings):  each faction that stands r towards
     *  this one changes by change x r / 10, no further than r.  a gain lifts its friends and lowers its enemies;  a loss only
     *  lowers its friends */
    void                ModifyFactionStanding(uint32 factionID, uint32 toID, uint16 eventType, double change, const std::string& msg,
                                              PyList* notify = nullptr);

protected:
    void                Populate();

private:
    PyObjectEx*         m_factionStandings;
    // repFactions by toID:  (fromID, how fromID stands towards toID)
    std::multimap<uint32, std::pair<uint32, float>> m_factionRelations;

};


//Singleton
#define sStandingMgr \
( StandingMgr::get() )



#endif  // EVE_STANDING_STANDINGMGR_H