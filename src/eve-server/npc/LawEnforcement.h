/**
 * @name LawEnforcement.h
 *   CONCORD, faction police, gate/station sentries and ships left in space by pilots who log off.
 *   one per solar system, processed with the system.  the crime rules themselves are in system/CrimeWatch.
 *
 * Crucible behaviour (EVElopedia Dec 2011, EVE University 2011, static data):
 *  CONCORD: 0.5 - 1.0 only.  one response per criminal, after a delay set by the system's security level.
 *   it scrambles, webs and jams the criminal's ship and destroys it, never the pod.  it can't be killed.
 *   if the criminal warps off before CONCORD lands, CONCORD arrives where they go.
 *   by default a CONCORD Police Commander (battleship, an effectively one-shot weapon) and two Police Captains.
 *   with <ConcordWaves>, Police Officers and Captains come first and the Commander follows.
 *  faction police: in high-sec, chase pilots whose security status is too low for the system.  they can be escaped.
 *  sentries: a few guns at every gate and station in empire space, shooting criminals and anyone aggressing their owner.
 *  logoff: a ship in a fight with players stays in space for 15 minutes after its pilot logs off (or after the last
 *   attack on it).  otherwise it warps off and vanishes a minute later (two if NPCs were attacking it).
 */

#ifndef EVEMU_NPC_LAWENFORCEMENT_H
#define EVEMU_NPC_LAWENFORCEMENT_H

#include "eve-server.h"

class Client;
class EVEServiceManager;
class NPC;
class Sentry;
class ShipSE;
class SystemEntity;
class SystemManager;

class LawEnforcement
{
public:
    LawEnforcement(SystemManager* pSystem, EVEServiceManager& services);
    ~LawEnforcement()                                   { /* the system owns our entities */ }

    void Init();        // sentry guns at gates and stations
    void Process();     // 1Hz, from SystemManager::ProcessTic()
    void Close();       // system unloading.  it deletes our entities
    bool IsBusy();      // a response is running or a ship is waiting in space.  keeps the system loaded

    /* called by CrimeWatch */
    void ReportCriminal(Client* pClient);       // send CONCORD
    void ReportPoliceTarget(Client* pClient);   // send the faction police
    void ReportCriminalEntity(SystemEntity* pSE);   // send CONCORD after an NPC (GM testing)
    uint32 ConcordDelay();                      // seconds, for this system's security

    /* CONCORD tackles its target the first time it shoots: scramble, web and jam */
    void OnLawAttack(NPC* pNPC, SystemEntity* pTarget);
    bool IsJammed(uint32 shipID)                        { return (m_jammed.find(shipID) != m_jammed.end()); }

    /* logoff */
    void AddLoggedOffShip(ShipSE* pShipSE, uint32 charID, bool pvp, bool npc);
    // the owner logged back in: removes the ship's entity and gives its position for the new one
    bool ReclaimLoggedOffShip(uint32 shipID, GPoint& position);
    // owner of a ship left in space, 0 if this isn't one
    uint32 GetLoggedOffOwner(uint32 shipID);
    // the ship was destroyed (its entity is going away)
    void LoggedOffShipKilled(uint32 shipID)             { m_loggedOff.erase(shipID); }

private:
    struct Response {
        uint8 kind = 0;                 // Law::Concord / Law::Police
        uint32 charID = 0;
        uint32 shipID = 0;              // the ship being chased, once they're on it
        int64 due = 0;                  // when they arrive
        int64 started = 0;
        int64 nextWave = 0;             // waves: when the commander arrives
        int64 leaveAt = 0;              // the target is gone: when they leave
        std::vector<NPC*> npcs;
        // tackle applied to the target ship
        float scram = 0.0f;
        std::vector<InventoryItemRef> webs;
        bool jammed = false;
    };
    // charID * 4 + kind
    std::map<uint64_t, Response> m_responses;

    struct LoggedOff {
        ShipSE* pShipSE = nullptr;
        uint32 charID = 0;
        int64 loggedOff = 0;
        int64 removeAt = 0;
        bool pvp = false;
    };
    std::map<uint32, LoggedOff> m_loggedOff;    // shipID

    std::vector<Sentry*> m_sentries;
    std::set<uint32> m_jammed;

    SystemManager* m_system;
    EVEServiceManager& m_services;

    void Report(uint32 id, const char* name, uint8 kind, uint32 delaySeconds);
    SystemEntity* FindTarget(uint32 charID);
    void SpawnWave(Response& r, SystemEntity* pTarget, uint8 wave);
    NPC* SpawnNPC(uint16 typeID, const GPoint& pos, uint32 factionID, uint32 corpID, uint32 targetID, uint8 role);
    void Retarget(Response& r, uint32 shipID);
    void ReleaseTackle(Response& r);
    void Despawn(Response& r);
    void SpawnSentries(SystemEntity* pSE, uint8 count, uint16 typeID, uint32 factionID, uint32 corpID);
    void ProcessLoggedOff(int64 now);
};

#endif  // EVEMU_NPC_LAWENFORCEMENT_H
