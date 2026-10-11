
 /**
  * @name FleetService.h
  *     Fleet Service code for EVEmu
  *
  * @Author:        Allan
  * @date:          05 August 2014 (original skeleton outline)
  * @update:        21 November 2017 (begin actual implementation)
  *
  */

#ifndef EVEMU_SRC_FLEET_SVC_H_
#define EVEMU_SRC_FLEET_SVC_H_

#include "eve-common.h"
#include "utils/Singleton.h"

#include "Client.h"
#include "EntityList.h"
#include "packets/Fleet.h"
#include "fleet/FleetData.h"

class FleetService
: public Singleton<FleetService>
{
public:
    FleetService();
    ~FleetService()                              { /* do nothing here */ }

    void Initialize(EVEServiceManager& svc);

    uint32 CreateFleet(Client* pClient);
    PyRep* CreateWing(uint32 fleetID);
    void CreateSquad(uint32 fleetID, uint32 wingID);
    void DeleteFleet(uint32 fleetID);
    void DeleteWing(uint32 wingID);
    void DeleteSquad(uint32 squadID);

    // adverts pViewer may see
    PyRep* GetAvailableFleets(Client* pViewer);
    PyRep* GetWings(uint32 fleetID);

    PyRep* GetFleetAdvert(uint32 fleetID);
    void CreateFleetAdvert(uint32 fleetID, FleetAdvert data);
    void RemoveFleetAdvert(uint32 fleetID);
    // sets the Fleet Finder invite scope (Fleet::Invite bits).  0 removes the advert; otherwise the advert is updated, or made with defaults
    bool SetInviteScope(uint32 fleetID, uint8 scope, Client* pBoss);

    void GetWingIDs(uint32 fleetID, std::vector< uint32 >& wingIDs);
    void GetSquadIDs(uint32 wingID, std::vector< uint32 >& squadIDs);

    void GetFleetData(uint32 fleetID, FleetData& data);
    void GetWingData(uint32 wingID, WingData& data);
    void GetSquadData(uint32 squadID, SquadData& data);
    void GetMemeberVec(uint32 fleetID, std::vector<Client*>& data);

    bool IsWingActive(int32 wingID);

    void SetMOTD(uint32 fleetID, std::string motd);
    PyRep* GetMOTD(uint32 fleetID);

    void RenameWing(uint32 wingID, std::string name);
    void RenameSquad(uint32 squadID, std::string name);

    void UpdateBoost(uint32 fleetID, bool fleet, std::list< int32 >& wing, std::list< int32 >& squad);
    /* warfare links: every member gets the strongest active link of each kind from its squad, wing and fleet boosters
     *  (in the same system), as far as the chain of command passes bonuses down */
    void UpdateLinks(uint32 fleetID);
    // the commander of each level is in space and can pass bonuses down (same skill rules as UpdateBoost)
    bool FleetBoostPasses(uint32 fleetID);
    bool WingBoostPasses(uint32 wingID);
    bool SquadBoostPasses(uint32 squadID);
    void UpdateOptions(uint32 fleetID, bool isFreeMove, bool isRegistered, bool isVoiceEnabled);

    bool AddMember(Client* pClient, uint32 fleetID, int32 wingID, int32 squadID, int8 job, int8 role, int8 booster);
    bool UpdateMember(uint32 charID, uint32 fleetID, int32 newWingID, int32 newSquadID, int8 newJob, int8 newRole, int8 newBooster);

    Client* GetFleetLeader(uint32 fleetID);
    uint32 GetFleetLeaderID(uint32 fleetID);
    Client* GetWingLeader(uint32 wingID);
    uint32 GetWingLeaderID(uint32 wingID);
    Client* GetSquadLeader(uint32 squadID);
    uint32 GetSquadLeaderID(uint32 squadID);

    std::string GetFleetName(uint32 fleetID);
    std::string GetWingName(uint32 wingID);
    std::string GetSquadName(uint32 squadID);

    std::string GetFleetDescription(uint32 fleetID);

    // first squad with room (in wing/squad order).  restrictWingID/restrictSquadID limit the search.  false when the fleet is full
    bool GetRandUnitIDs(uint32 fleetID, int32& wingID, int32& squadID, int32 restrictWingID = 0, int32 restrictSquadID = 0);

    void LeaveFleet(Client* pClient);
    // called when a client disconnects: drops its invites and join requests, then leaves its fleet
    void ClientLogout(Client* pClient);
    // makes pNewBoss the fleet boss (job Creator), clearing the job on the old boss
    void SetBoss(uint32 fleetID, Client* pNewBoss);

    /* permission and lookup helpers */
    bool IsBoss(Character* pChar);
    // true when 'me' commands 'target' (or is target): boss/FC for anyone, WC for own wing, SC for own squad
    bool IsSuperior(Character* me, Character* target);
    // returns the member's client, or nullptr when charID is not a member of fleetID
    Client* GetMember(uint32 fleetID, uint32 charID);
    bool IsWingInFleet(uint32 wingID, uint32 fleetID);
    bool IsSquadInFleet(uint32 squadID, uint32 fleetID);
    uint32 GetSquadMemberCount(uint32 squadID);
    uint32 GetWingMemberCount(uint32 wingID);
    // holder of the FC (role FleetLeader), WC of wingID or SC of squadID slot, or nullptr when free
    Client* GetPositionHolder(uint32 fleetID, int32 wingID, int32 squadID, int8 role);
    uint8 GetWingCount(uint32 fleetID)                 { return m_fleetWings.count(fleetID); }
    uint8 GetSquadCount(uint32 wingID)                 { return m_wingSquads.count(wingID); }

    /* fleet movement helpers */
    // FC, WC or SC (the boss alone is not a commander)
    bool IsCommander(Client* pClient);
    /* the commander's subordinates in space with a ship: FC -> whole fleet, WC -> own wing, SC -> own squad.  never pCmdr.
     *  sameSystem/sameBubble limit them to the commander's solar system/bubble */
    void GetSubordinates(Client* pCmdr, std::vector<Client*>& into, bool sameSystem = true, bool sameBubble = false);
    // tags an item for the fleet.  a tag marks one item only: reusing it moves it.  sends OnFleetStateChange
    void SetTargetTag(uint32 fleetID, uint32 itemID, const std::string& tag);
    // util.KeyVal(targetTags={itemID: tag}) for OnFleetStateChange
    PyRep* MakeFleetState(uint32 fleetID);

    /* watch list damage updates (fleetMgr.AddToWatchlist/RemoveFromWatchlist/RegisterForDamageUpdates).
     *  fav is None (damage updates off) or the list of watched charIDs (max MAX_DAMAGE_SENDERS).  throws FleetTooManyFavorites */
    void SetWatchlist(Client* pClient, PyRep* fav);
    // pWatched's ship damage changed: OnFleetDamageStateChange to the members watching it in the same system
    void SendWatchlistDamage(Client* pWatched);

    /* reconnect (fleetSvc.AttemptReconnectLazy -> fleet.Reconnect() within RECONNECT_TIMEOUT of a disconnect) */
    // remembers the client's fleet position.  called by ClientLogout() before the client leaves its fleet
    void SaveReconnectData(Client* pClient);
    // puts the client back into fleetID at its old position where possible.  false when that isn't possible
    bool Reconnect(Client* pClient, uint32 fleetID);

    /* cynosural beacons (fleet jump targets).  OnJumpBeaconChange(charID, systemID, beaconID, active) to the fleet */
    // registers the pilot's lit field (replaces an older one)
    void AddBeacon(const BeaconData& data);
    // removes charID's beacon.  beaconID != 0 removes it only when it is still that field
    void RemoveBeacon(uint32 charID, uint32 beaconID = 0);
    bool GetBeacon(uint32 charID, BeaconData& data);
    // the active beacons (and bridges) of the client's fleet, to a member who just (re)joined (FleetBound::Init)
    void SendJumpState(Client* pClient);

    /* fleet finder */
    // the advert is visible to pViewer (scope bits, allowed entities, security status; members always see their fleet)
    bool CanSeeAdvert(uint32 fleetID, Client* pViewer);

    void FleetBroadcast(Client* pFrom, uint32 itemID, int8 scope, int8 group, std::string msg);

    void SendActiveStatus(uint32 fleetID, int32 wingID, int32 squadID);
    // this sends "*fleetid" update to all fleet memebers and is sequenced
    void SendFleetUpdate(uint32 fleetID, const char *notifyType, PyTuple *payload);

    std::string GetBoosterData(uint32 fleetID, uint16& length);

    bool GetInviteData(uint32 charID, InviteData& data);
    bool SaveInviteData(uint32 charID, InviteData& data);
    void RemoveInviteData(uint32 charID);
    // charID holding an outstanding invite to the FC/WC/SC slot, or 0
    uint32 GetPendingInvite(uint32 fleetID, int32 wingID, int32 squadID, int8 role);

    bool AddJoinRequest(uint32 fleetID, Client* pClient);
    void GetJoinRequests(uint32 fleetID, std::vector<Client*>& data);
    void RemoveJoinRequest(uint32 fleetID, Client* pClient);

    uint16 GetFleetMemberCount(uint32 fleetID)         { return m_fleetMembers.count(fleetID); }

    std::string GetJobName(int8 job);
    std::string GetRoleName(int8 role);
    std::string GetBoosterName(int8 booster);

    std::string GetBCastScopeName(int8 scope);
    std::string GetBCastGroupName(int8 group);

    void GetFleetMembersOnGrid(Client* pClient, std::vector<uint32>& data);
    void GetFleetMembersInSystem(Client* pClient, std::vector<uint32>& data);
    void GetFleetClientsInSystem(Client* pClient, std::vector<Client*>& data);
    std::vector<Client *> GetFleetClients(uint32 fleetID);

protected:
    void RemoveMember(Client* pClient);
    // picks the next boss when pLeaving (the boss) leaves: FC, then a WC, then a SC, then the longest-standing member
    void PassBoss(uint32 fleetID, Client* pLeaving);

    void IncFleetSquads(uint32 fleetID, uint32 wingID);
    void DecFleetSquads(uint32 fleetID, uint32 wingID);

    void SetWingBoostData(uint32 wingID, BoostData& bData);
    // applies the links of these boosters to the member's ship (no boosters removes them)
    void ApplyLinksTo(Client* pMember, const std::vector<Client*>& boosters);
    void SetSquadBoostData(uint32 squadID, BoostData bData, bool& sboost);

    uint32 m_fleetID;
    uint32 m_wingID;
    uint32 m_squadID;

private:
    LSCService* m_lsc;
    bool m_initalized;
    Client* m_departing;    // client being removed by ClientLogout(); SendFleetUpdate() skips it

    std::map<uint32, InviteData>        m_inviteData;       // charID/data
    std::map<uint32, ReconnectData>     m_reconnect;        // charID/fleet position at disconnect
    std::map<uint32, std::set<uint32>>  m_watchlists;       // watcher charID/watched charIDs
    std::map<uint32, BeaconData>        m_beacons;          // charID/active cyno

    std::map<uint32, FleetAdvert>       m_fleetAdvertMap;   // fleetID/data

    std::map<uint32, FleetData>         m_fleetDataMap;     // fleetID/data
    std::map<uint32, WingData>          m_wingDataMap;      // wingID/data
    std::map<uint32, SquadData>         m_squadDataMap;     // squadID/data

    std::multimap<uint32, Client*>      m_joinReq;          // fleetID/Client*
    std::multimap<uint32, Client*>      m_fleetMembers;     // fleetID/Client*
    std::multimap<uint32, uint32>       m_fleetWings;       // fleetID/wingIDs
    std::multimap<uint32, uint32>       m_wingSquads;       // wingID/squadIDs
};

//Singleton
#define sFltSvc \
    ( FleetService::get() )

#endif  // EVEMU_SRC_FLEET_SVC_H_
