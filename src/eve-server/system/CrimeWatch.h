/**
 * @name CrimeWatch.h
 *   EvEmu CrimeWatch code.
 *
 * @Author:         Allan
 * @date:   13 March 2016
 *
 * Crucible (pre-Retribution) crime rules.
 *  everything is built on aggression flags between two parties: aggressor -> victim (a character or a corporation),
 *  each kept for 15 minutes after the last act.  the victim, and anyone the client lets attack freely, may shoot back.
 *  a crime in empire space (0.1 - 1.0) also gives the Global Criminal Countdown (victim 'None' on the wire):
 *  anyone may shoot the criminal's ship, CONCORD destroys it in 0.5 - 1.0, and gate/station sentries shoot it.
 *  the client keeps the same table (michelle.aggressors) and works out its warnings, overview flags and the
 *  neocom countdown from it, so every change is pushed to the pilots in the aggressor's system.
 *  Crimewatch 1 gate and station guns (npc ai plan 4.10) keep their own aggression per guarded object:  attacking the guns
 *  of a gate or station makes that object's guns shoot you, for crime.SentryAggression, renewed each time one picks you.
 */


#ifndef EVEMU_SERVER_CRIMEWATCH_H_
#define EVEMU_SERVER_CRIMEWATCH_H_


#include "eve-common.h"
#include "utils/Singleton.h"

class Character;
class Client;
class PyDict;
class PyRep;
class PyTuple;
class SystemEntity;
class SystemManager;

namespace Crime {
    // the client's util.SecurityClassFromLevel()
    namespace SecClass {
        enum {
            Null    = 0,
            Low     = 1,
            High    = 2
        };
    }
}

class CrimeWatch
: public Singleton<CrimeWatch>
{
public:
    CrimeWatch();
    ~CrimeWatch()   { /* do nothing here */ }

    int Initialize();

    /* system security as the client sees it.  true sec rounded to 0.1, with 0 < sec < 0.05 counted as 0.05 (pseudoSecurity) */
    static float SecurityLevel(float trueSec);
    static uint8 SecurityClass(float trueSec);

    /* aggressive acts.  these decide whether the act is legal, set the flags and tell everyone who needs to know */
    // attacking: weapons, drones, e-war, neutralizers, targeted area effects...
    void OffensiveAct(Client* pAggressor, SystemEntity* pTarget);
    // remote repair, energy transfer, sensor/tracking links...
    void AssistanceAct(Client* pHelper, SystemEntity* pTarget);
    // took items from a container or wreck without loot rights
    void Theft(Client* pThief, uint32 ownerID, uint32 ownerCorpID);
    // an NPC damaged this pilot's ship (keeps a logging-off ship in space a little longer)
    void NPCAggression(uint32 charID);

    /* GM testing (/crime) */
    // the pilot attacks a civilian: an innocent who isn't in the system.  goes through the same rules as a real attack
    void SimulateCrime(Client* pAggressor);
    // an NPC (or any other entity) attacks someone it shouldn't.  NPCs never do this on their own
    void EntityCrime(SystemEntity* pAggressor, SystemEntity* pVictim);

    /* queries */
    bool IsCriminal(uint32 charID);                             // live global criminal countdown
    bool IsFlaggedTo(uint32 aggressorID, uint32 victimID);       // aggressor has a live flag towards victim (char or corp)
    bool HasLiveFlags(uint32 charID);                            // live aggressor towards anyone, or a live criminal
    bool IsOutlaw(float secStatus)                               { return (roundf(secStatus * 10.0f) / 10.0f) <= -5.0f; }

    /* Crimewatch 1 sentries (npc ai plan 4.10, phase 4) */
    // charID attacked a gate/station gun (objectID = the gate or station it guards), or a gun picked them:  (re)starts the timer
    void SentryAggression(uint32 charID, uint32 objectID);
    // charID has live aggression (crime.SentryAggression) with what objectID's guns guard
    bool HasSentryAggression(uint32 charID, uint32 objectID);
    // the npc aggression timer (crime.NpcFactionAggression):  charID attacked one of factionID's npcs in the last SentryAggression
    bool HasFactionAggression(uint32 charID, uint32 factionID);
    // seconds left before this pilot may dock or jump after an aggressive act.  0 = free to go
    uint32 WeaponsTimer(uint32 charID);
    // how long a ship logging off in space stays there (seconds).  0 = warp off and vanish at the normal rate
    uint32 LogoffTimer(uint32 charID, bool& npcOnly);
    // can this pilot use gates/stations?  throws the client's error if not
    void CheckDockOrJump(Client* pClient, bool jump);
    // can this pilot eject, board or store a ship?  criminals can't (destroying evidence).  throws the client's error if not
    void CanSwitchShip(Client* pClient);

    /* client data */
    PyDict* GetCriminalTimeStamps(uint32 charID);               // aggressionMgr.GetCriminalTimeStamps
    void GetAggressors(SystemManager* pSystem, std::map<int32, PyRep*>& into);  // ballpark SetState 'aggressors'
    PyTuple* GetKillRights(uint32 charID);                      // standing2.GetMyKillRights
    bool CanTakeFrom(uint32 charID, uint32 ownerID);            // aggressionMgr.CheckLootRightExceptions

    /* events */
    // the pilot's ship appeared in space: undock, gate jump, login, boarding.  a live GCC brings CONCORD
    void OnEnterSpace(Client* pClient);
    // a player's ship (or pod) was destroyed.  sec status penalties, kill rights, kill right use.  killerCharID 0 for NPCs
    //  a self-destruct has no killer: the victim's aggressors get no penalties or kill rights
    void OnShipDestroyed(uint32 victimCharID, bool pod, float victimSec, uint32 killerCharID, SystemManager* pSystem, bool selfDestruct = false);
    // security status gain for killing an NPC (only the best NPC per system per 15 minutes counts).  returns the gain
    float OnNPCKilled(Character* pChar, InventoryItemRef npcRef, SystemManager* pSystem);

    /* security status.  this is the character's standing from CONCORD as far as the client is concerned */
    void SetSecurityStatus(uint32 charID, float newSec, const std::string& reason, uint16 eventType);

private:
    struct Record {
        std::map<uint32, int64> victims;    // victimID (0 = global criminal) -> time of the last act
        int64 lastAct = 0;                  // last aggressive act (weapons timer)
        int64 lastPvP = 0;                  // last time this pilot was in a fight with another player
        int64 lastNPC = 0;                  // last time an NPC damaged this pilot
    };
    std::map<uint32, Record> m_records;

    // illegal aggressors of each victim (for ship kill penalties and kill rights)
    std::map<uint32, std::map<uint32, int64>> m_illegal;    // victimCharID -> aggressorCharID -> time
    // kill rights: holder -> target -> expiry
    std::map<uint32, std::map<uint32, int64>> m_killRights;
    // npc kill sec status gain window: charID -> systemID -> (window start, best gain)
    std::map<uint32, std::map<uint32, std::pair<int64, float>>> m_secGain;
    // Crimewatch 1 sentry aggression:  (guarded objectID << 32 | charID) -> last act or pick
    std::unordered_map<uint64_t, int64> m_sentryAggression;
    // npc aggression timer:  (factionID << 32 | charID) -> last attack on one of the faction's npcs
    std::unordered_map<uint64_t, int64> m_factionAggression;

    int64 SentryAggressionTime();
    // drops expired entries now and then
    void Prune(std::unordered_map<uint64_t, int64>& map);
    // a pilot attacked an npc:  no crime, but the npc aggression timer (crime.NpcFactionAggression)
    void NpcFactionAct(Client* pAggressor, SystemEntity* pNPC);

    int64 FlagTime();
    int64 CrimTime();
    bool Live(int64 time, int64 duration);
    void Flag(uint32 aggressorID, uint32 victimID, int64 now);
    void Notify(uint32 charID);
    PyDict* MakeVictimDict(uint32 charID);
    void SendToSystem(SystemManager* pSystem, uint32 charID, PyDict* victims);

    // who owns what, for the legality rules
    struct Owner {
        uint32 charID = 0;              // player-owned: the character
        uint32 corpID = 0;
        uint32 allianceID = 0;
        int32 warFactionID = 0;
        float secStatus = 0.0f;
        bool pod = false;
        bool concord = false;           // CONCORD, attacking them is a crime
        bool sentry = false;            // gate/station sentry (attacking flags you to its owner)
        uint32 protectedID = 0;         // law sentry:  the gate or station it guards (Crimewatch 1 aggression)
        bool structure = false;         // player structure (owned by corpID)
    };
    bool GetOwner(SystemEntity* pSE, Owner& into);
    void Act(Client* pAggressor, SystemManager* pSystem, const Owner& target);
    bool CanAttackFreely(Client* pAggressor, const Owner& target);
    void SecPenalty(Client* pAggressor, float victimSec, float basePercent, float minPercent, float level, const char* what);
    void GrantKillRight(uint32 holderID, uint32 targetID);
    void RemoveKillRight(uint32 holderID, uint32 targetID);
    void SendKillRightChange(bool set, int64 when, uint32 holderID, uint32 targetID);
};

//Singleton
#define sCrimeWatch \
( CrimeWatch::get() )

#endif  // EVEMU_SERVER_CRIMEWATCH_H_
