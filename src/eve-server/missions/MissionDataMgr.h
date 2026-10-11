
 /**
  * @name MissionDataMgr.h
  *   memory object caching system for managing and saving ingame data specific to missions
  *
  * @Author:        Allan
  * @date:      24 June 2018
  *
  */


#ifndef _EVE_SERVER_MISSION_DATAMANAGER_H__
#define _EVE_SERVER_MISSION_DATAMANAGER_H__


#include "../eve-server.h"
#include "missions/MissionDB.h"


class Agent;
class Client;

class MissionDataMgr
: public Singleton< MissionDataMgr >
{
public:
    MissionDataMgr();
    ~MissionDataMgr();

    int                 Initialize();

    void                Clear();
    void                Close()                         { Clear(); }
    void                GetInfo();

    // called every minute:  expires offers and accepted missions
    void                Process();

    void                AddMissionOffer(uint32 charID, MissionOffer& data);
    void                UpdateMissionData(uint32 charID, MissionOffer& data);
    void                RemoveMissionOffer(uint32 charID, MissionOffer& data);
    void                LoadMissionOffers(uint32 charID, std::vector<MissionOffer>& data);
    void                LoadAgentOffers(const uint32 agentID, std::map<uint32, MissionOffer>& data);
    // the open offer with this offerID (false if there is none)
    bool                GetOfferByID(uint32 offerID, MissionOffer& data);

    /* fills the static part of an offer (content, objective, base rewards) for this agent.
     *  false when there is no content of this type and level for the agent (nothing is changed then) */
    bool                CreateMissionOffer(uint8 typeID, uint8 level, Agent* pAgent, bool important, bool storyline, MissionOffer& data);
    // content row of this type/level the agent may offer (enemy faction and race rules), picked at random.  false if there is none
    bool                PickContent(uint8 typeID, uint8 level, Agent* pAgent, bool storyline, MissionContent& out);
    // is there storyline/normal content of this type and level
    bool                HasContent(uint8 typeID, uint8 level, bool storyline);
    // can a storyline mission of this type and level be made (content, or the important courier rows for couriers)
    bool                HasStorylineContent(uint8 typeID, uint8 level);
    /* counts a completed mission towards the storyline mission of its faction and level (after missions.StorylineEvery
     *  of them the nearest storyline agent of the faction offers one) */
    void                OnMissionCompleted(Client* pClient, Agent* pAgent, MissionOffer& data);
    // the faction's storyline agent nearest to systemID (nullptr if it has none)
    Agent*              FindStorylineAgent(uint32 factionID, uint32 systemID);
    const MissionContent* GetContent(uint32 contentID);

    // an accepted mission ran out of time (or its pocket failed it):  failed, cargo taken back, journal updated
    void                FailOffer(MissionOffer& data, bool notify);

    /* client data */
    // the 9-tuple of GetMyJournalDetails' mission list
    PyTuple*            GetJournalEntry(MissionOffer& data);
    // the offer's agent bookmarks (util.KeyVal list) for the journal and People & Places
    PyList*             BuildBookmarks(MissionOffer& data);
    // client mission state (0-3) for a server state
    static uint8        GetClientState(uint8 stateID);
    // the offer's title/briefing for the client:  the localized messageID when there is one, else the text
    static PyRep*       GetTitle(const MissionOffer& data);
    static PyRep*       GetBriefing(const MissionOffer& data);
    // ship groups a mission pocket's acceleration gates let through (allowed empty = any) and turn away, by mission level
    static void         GetShipRestrictions(uint8 level, std::vector<uint32>& allowed, std::vector<uint32>& denied);

    // 0-4 index of the per-level config tables
    static uint8        LevelIndex(uint8 level)         { return (level < 1 ? 0 : (level > 5 ? 4 : level - 1)); }

    std::string         GetTypeName(uint8 typeID);
    std::string         GetTypeLabel(uint8 typeID, bool storyline = false);

    PyString*           GetMissionImage(uint8 typeID);
    PyString* GetKillRes()                               { PyIncRef(KillPNG); return KillPNG; }
    PyString* GetMiningRes()                             { PyIncRef(MiningPNG); return MiningPNG; }
    PyString* GetCourierRes()                            { PyIncRef(CourierPNG); return CourierPNG; }

protected:
    void                Populate();

private:
    // copies a content row into an offer
    void                ApplyContent(const MissionContent& content, uint8 level, MissionOffer& data);
    // can this agent offer this content (enemy faction against the agent's own side, race mask)
    bool                IsEligible(const MissionContent& content, Agent* pAgent);

    std::map<std::string, uint32> m_names;
    std::multimap<uint8, CourierData> m_courier;    // level/data
    std::multimap<uint8, CourierData> m_courierImp;    // level/data
    std::multimap<uint8, CourierData> m_mining;     // level/data
    std::multimap<uint8, CourierData> m_miningImp;     // level/data
    std::multimap<uint8, MissionData> m_missions;   // level/data
    std::multimap<uint8, MissionData> m_missionsImp;   // level/data
    std::multimap<uint16, uint32> m_contentIdx;     // (level << 8) | typeID -> contentID
    std::map<uint32, MissionContent> m_content;     // contentID -> msnMissions row
    std::multimap<uint32, MissionOffer> m_offers;   // charID/data      current mission offers by charID
    std::multimap<uint32, MissionOffer> m_aoffers;   // agentID/data    current mission offers by agentID
    std::multimap<uint32, MissionOffer> m_xoffers;   // charID/data     expired/completed offers by charID

    //  mission png resources...
    PyString* CourierPNG;
    PyString* MiningPNG;
    PyString* KillPNG;

};

//Singleton
#define sMissionDataMgr \
( MissionDataMgr::get() )


#endif  // _EVE_SERVER_MISSION_DATAMANAGER_H__
