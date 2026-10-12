/**
 * @name LawEnforcement.h
 *   CONCORD, faction police, gate/station sentries and ships left in space by pilots who log off.
 *   one per solar system, processed with the system.  the crime rules themselves are in system/CrimeWatch.
 *
 * Crucible behaviour (EVElopedia Dec 2011, EVE University 2011, static data):
 *  CONCORD: 0.5 - 1.0 only.  one response per criminal, after a delay set by the system's security level.
 *   it scrambles, webs and jams the criminal's ship and destroys it, never the pod.  it can't be killed.
 *   the tackle is its data ewar (npc ai plan 4.8, N6):  concordWarpScramble (strength 10, 250km), concordModifyTargetSpeed
 *   (-95%), concordTargetJam (5s cycles, 200km + falloff) and its neutralizer (NPCEwar), on each ship once it has the criminal
 *   locked.  a cloaked ship is pulled out of cloak and can't cloak again while CONCORD is on it.  crime.ConcordScriptedTackle
 *   brings back the old scripted tackle (scram 100, webs and a jam that never misses, on the first shot)
 *   if the criminal warps off before CONCORD lands, CONCORD arrives where they go.
 *   by default a CONCORD Police Commander (battleship, an effectively one-shot weapon) and two Police Captains.
 *   with <ConcordWaves>, Police Officers and Captains come first and the Commander follows.
 *  faction police: in high-sec, chase pilots whose security status is too low for the system.  they can be escaped.
 *   their tackle is their data ewar (scram, web, ecm, damps, neuts:  NPCEwar).
 *  faction navy (crime.FactionNavy, off by default):  the same for pilots with bad standing with the system's faction, or
 *   (crime.NpcFactionAggression) who attacked one of its npcs.
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
    void ReportNavyTarget(Client* pClient, uint32 factionID);  // send the faction navy (crime.FactionNavy)
    void ReportCriminalEntity(SystemEntity* pSE);   // send CONCORD after an NPC (GM testing)
    uint32 ConcordDelay();                      // seconds, for this system's security

    /* a CONCORD ship shoots its target:  police pursuit (decloak, no cloaking).  with crime.ConcordScriptedTackle also the
     *  old scripted scramble, web and jam */
    void OnLawAttack(NPC* pNPC, SystemEntity* pTarget);
    // the scripted jam (crime.ConcordScriptedTackle):  the ship can't lock anything
    bool IsJammed(uint32 shipID)                        { return (m_jammed.find(shipID) != m_jammed.end()); }
    // CONCORD is on this ship:  it can't cloak
    bool IsPursued(uint32 shipID)                       { return (m_pursued.find(shipID) != m_pursued.end()); }
    // does this law npc use its type's ewar (NPCEwar)?  police and navy always, CONCORD unless crime.ConcordScriptedTackle
    static bool UsesTypeEwar(NPC* pNPC);

    /* logoff */
    void AddLoggedOffShip(ShipSE* pShipSE, uint32 charID, bool pvp, bool npc);
    // the owner logged back in: removes the ship's entity and gives its position for the new one
    bool ReclaimLoggedOffShip(uint32 shipID, GPoint& position);
    // owner of a ship left in space, 0 if this isn't one
    uint32 GetLoggedOffOwner(uint32 shipID);
    // the ship was destroyed (its entity is going away)
    void LoggedOffShipKilled(uint32 shipID)             { m_loggedOff.erase(shipID); }

private:
    /* one of CONCORD's data tackle effects on one of its ships (concordWarpScramble, concordModifyTargetSpeed,
     *  concordTargetJam), cycled on its duration while that ship has the criminal locked */
    struct TackleEffect {
        uint8 kind = 0;                 // Ewar::Scramble / Web / Jam
        const char* guid = "";
        uint32 duration = 0;            // ms
        float chance = 1.0f;            // activation chance each cycle
        double range = 0.0;             // m, surface distance
        double falloff = 0.0;           // m
        float strength = 0.0f;          // scramble strength
        int64 nextAt = 0;               // ms (GetSteadyTime()), 0 = not cycling
        uint32 targetID = 0;            // live session on this ship (0 = none)
    };

    struct Response {
        uint8 kind = 0;                 // Law::Concord / Law::Police / Law::Navy
        uint32 factionID = 0;           // police and navy:  whose
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
        // CONCORD's data tackle, per ship
        std::map<NPC*, std::vector<TackleEffect>> tackle;
    };
    // charID * 4 + kind
    std::map<uint64_t, Response> m_responses;
    // which response an npc belongs to (OnLawAttack runs on every shot;  npc ai plan 6.7).  checked against Response::npcs
    std::unordered_map<NPC*, uint64_t> m_npcResponse;

    struct LoggedOff {
        ShipSE* pShipSE = nullptr;
        uint32 charID = 0;
        int64 loggedOff = 0;
        int64 removeAt = 0;
        bool pvp = false;
    };
    std::map<uint32, LoggedOff> m_loggedOff;    // shipID

    std::vector<Sentry*> m_sentries;
    std::set<uint32> m_jammed;          // scripted jam
    std::set<uint32> m_pursued;         // CONCORD is on them:  no cloaking

    SystemManager* m_system;
    EVEServiceManager& m_services;

    void Report(uint32 id, const char* name, uint8 kind, uint32 delaySeconds, uint32 factionID = 0);
    static const char* KindName(uint8 kind);
    SystemEntity* FindTarget(uint32 charID);
    void SpawnWave(Response& r, SystemEntity* pTarget, uint8 wave);
    NPC* SpawnNPC(uint16 typeID, const GPoint& pos, uint32 factionID, uint32 corpID, uint32 targetID, uint8 role);
    void Retarget(Response& r, uint32 shipID);
    void ReleaseTackle(Response& r);
    /* CONCORD's data tackle */
    void BuildTackle(NPC* pNPC, std::vector<TackleEffect>& into);
    void ProcessTackle(Response& r, SystemEntity* pTarget);
    void StartTackle(NPC* pNPC, TackleEffect& fx, SystemEntity* pTarget);
    void EndTackle(NPC* pNPC, TackleEffect& fx);
    void Despawn(Response& r);
    void SpawnSentries(SystemEntity* pSE, uint8 count, uint16 typeID, uint32 factionID, uint32 corpID);
    void ProcessLoggedOff(int64 now);
};

#endif  // EVEMU_NPC_LAWENFORCEMENT_H
