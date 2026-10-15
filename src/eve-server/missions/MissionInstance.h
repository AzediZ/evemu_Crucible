
 /**
  * @name MissionInstance.h
  *   the private deadspace pockets of encounter and mining missions, on the F13 dungeon engine
  *
  *   a pocket is spawned (DungeonMgr::SpawnInstance, Kind::Mission, owned by the pilot) when the pilot first warps to
  *   the mission bookmark in its system, and lives until DungeonDespawnMinutes after the mission is completed, quit,
  *   failed or expired.  the dungeon's MissionCompletion events (objective npc/structure destroyed), its completion
  *   (pocket cleared) and MissionFailure events reach the offer through OnDungeonEvent().
  */


#ifndef _EVE_SERVER_MISSION_INSTANCE_H__
#define _EVE_SERVER_MISSION_INSTANCE_H__


#include "../eve-server.h"
#include "../../eve-common/EVE_Missions.h"
#include "system/cosmicMgrs/DungeonMgr.h"


class MissionInstanceMgr
: public Singleton< MissionInstanceMgr >,
  public Dungeon::DungeonListener
{
public:
    MissionInstanceMgr();
    ~MissionInstanceMgr()                               { }

    // spawns the pocket of an accepted offer in its (loaded) system if it isn't there.  the instanceID, 0 when there is none
    uint32 EnsureInstance(MissionOffer& offer);
    // the mission is over (completed, quit, failed, expired):  its pocket goes after missions.DungeonDespawnMinutes
    void Release(uint32 offerID);
    // marks the offer's objective done (dungeon event or GM) and tells the pilot.  false if there is no such open offer
    bool SetObjectiveComplete(uint32 offerID);
    // called every minute (MissionDataMgr::Process):  despawns released pockets that are due
    void Process();
    // a pocket is spawned in this system (running, or released and not despawned yet):  the system stays loaded
    bool HasPocketIn(uint32 systemID) const;
    // server shutdown:  no more dungeon events
    void Close();

    // Dungeon::DungeonListener
    void OnDungeonEvent(const Dungeon::DungeonEvent& ev) override;

private:
    struct Link {
        uint32 offerID;
        uint32 charID;
        uint32 agentID;
        uint32 systemID;
        uint32 instanceID;
        int64 despawnAt;    // FILETIME, 0 = mission still running
    };
    std::map<uint32, Link> m_byOffer;           // offerID ->
    std::map<uint32, uint32> m_byInstance;      // instanceID -> offerID

    void Despawn(Link& link);
};

//Singleton
#define sMissionInstanceMgr \
( MissionInstanceMgr::get() )


#endif  // _EVE_SERVER_MISSION_INSTANCE_H__
