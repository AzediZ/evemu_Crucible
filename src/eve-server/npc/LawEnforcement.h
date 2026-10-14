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
 *  logoff (EVE_Logoff.h): the ship finishes any warp it is in, then makes one emergency warp 1,000,000 km away (not when
 *   scrambled or in a bubble).  a ship in a fight with players stays in space for 15 minutes (renewed while it is still
 *   attacked), otherwise it leaves space a minute after it came to rest (two if NPCs were attacking it).  it is saved at the
 *   point its pilot logged off from, where the login warp takes them back.
 */

#ifndef EVEMU_NPC_LAWENFORCEMENT_H
#define EVEMU_NPC_LAWENFORCEMENT_H

#include <cstdint>

#include "eve-server.h"

class AggroGroup;
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
    /* crime switched off (/crime off):  every response leaves (its tackle with it), the sentry guns go, and ships left in space
     *  by pilots who logged off leave space now, saved where their pilots logged off.  Init() brings the guns back */
    void StandDown();
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
    /* keepLogoff:  the pilot logged off again before their login warp got under way.  the ship already sits at the
     *  start of that warp, away from this logoff point, which it keeps.  zero:  where the ship is (or lands) */
    void AddLoggedOffShip(ShipSE* pShipSE, uint32 charID, bool pvp, bool npc, const GPoint& keepLogoff = GPoint(0, 0, 0));
    // the owner logged back in: removes the ship's entity and gives its position for the new one
    bool ReclaimLoggedOffShip(uint32 shipID, GPoint& position);
    // where the pilot of a ship still in space logged off (the login warp goes back there).  false if this isn't one
    bool GetLogoffPoint(uint32 shipID, GPoint& into);
    // owner of a ship left in space, 0 if this isn't one
    uint32 GetLoggedOffOwner(uint32 shipID);
    // the ship was destroyed (its entity is going away)
    void LoggedOffShipKilled(uint32 shipID)             { m_loggedOff.erase(shipID); }

private:
    /* one of CONCORD's data tackle effects on one of its ships (concordWarpScramble, concordModifyTargetSpeed,
     *  concordTargetJam), cycled on its duration while that ship has the criminal locked */
    struct TackleEffect {
        uint8 kind = 0;                 // Ewar::Scramble / Web / Jam
        uint16 typeID = 0;              // the ship's type (for the fx, when it's gone)
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
        /* its ships, by itemID (looked up in the system each time:  a GM /unspawn or a kill takes one out of space without
         *  telling us, GetLawNPC() / PruneNPCs()) */
        std::vector<uint32> npcs;
        // its aggro group (npc ai plan 6.5, NpcAI::GroupKind::Law):  the response's identity in /npcai;  law npcs never alert
        std::shared_ptr<AggroGroup> group;
        // tackle applied to the target ship
        float scram = 0.0f;
        std::vector<InventoryItemRef> webs;
        bool jammed = false;
        // CONCORD's data tackle, per ship (itemID)
        std::map<uint32, std::vector<TackleEffect>> tackle;
    };
    // charID * 4 + kind
    std::map<uint64_t, Response> m_responses;
    // which response an npc (itemID) belongs to (OnLawAttack runs on every shot;  npc ai plan 6.7).  checked against Response::npcs
    std::unordered_map<uint32, uint64_t> m_npcResponse;

    struct LoggedOff {
        ShipSE* pShipSE = nullptr;
        uint32 charID = 0;
        int64 loggedOff = 0;
        int64 removeAt = 0;             // 0 while it is still moving (finishing its warp, or in its emergency warp)
        bool pvp = false;
        bool npc = false;
        uint8 stage = 0;                // Logoff::Exit:  FinishWarp, Warp (emergency warp under way), Stay (at rest)
        GPoint logoffPoint = GPoint(0, 0, 0);   // where the pilot left it (where it landed, for a logoff in warp)
    };
    std::map<uint32, LoggedOff> m_loggedOff;    // shipID

    std::vector<uint32> m_sentries;     // itemIDs of the guns we placed
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
    // one of our ships, in space and alive (nullptr:  it's gone, killed or taken out by a GM)
    NPC* GetLawNPC(uint32 npcID);
    // a ship of ours that left space without us (killed, GM /unspawn):  its object is ours to free
    void ForgetLawNPC(uint32 npcID);
    // drops the response's ships that are gone (and their tackle)
    void PruneNPCs(Response& r);
    /* CONCORD's data tackle */
    void BuildTackle(NPC* pNPC, std::vector<TackleEffect>& into);
    void ProcessTackle(Response& r, SystemEntity* pTarget);
    void StartTackle(NPC* pNPC, TackleEffect& fx, SystemEntity* pTarget);
    // npcID:  the ship running it (it may be gone)
    void EndTackle(uint32 npcID, TackleEffect& fx);
    void Despawn(Response& r);
    void SpawnSentries(SystemEntity* pSE, uint8 count, uint16 typeID, uint32 factionID, uint32 corpID);
    void ProcessLoggedOff(int64 now);
    // the ship is at rest where its pilot left it (or landed):  the emergency warp, or it stays
    void StartLogoffExit(LoggedOff& l, int64 now);
    // the ship came to rest:  its time in space starts
    void SetLogoffRest(LoggedOff& l, int64 now);
    // the ship leaves space, saved at savedAt (its entity is deleted).  the caller drops its entry
    void LeaveSpace(LoggedOff& l, const GPoint& savedAt);
};

#endif  // EVEMU_NPC_LAWENFORCEMENT_H
