/**
 * @name FacWarMgr.h
 *   factional warfare's militias:  who is in which, enlisting and leaving, and which militias are at war
 *
 * @date:          22 October 2026
 *
 *   faction warfare's first stage.  Crucible's rules ("Empyrean Age and all that - Signing up", CCP 2008,
 *   unchanged until Inferno):
 *   - a character in an NPC corporation enlists at the militia office of a station of the faction's, with a standing of
 *     0.5 or more to the faction (facwarMinStandingsToJoin), or with a Letter of Recommendation instead, and not within 24
 *     hours of enlisting before.  it moves into the faction's militia corporation, and resigns by quitting it
 *   - a player corporation's CEO or a director enlists the corporation, with the corporation's standing to the faction
 *     (the average of its members' standings that aren't 0) 0.5 or more, and not while it's in an alliance or applying to
 *     one.  it joins at the next downtime, and leaves at the downtime after it asks to.  alliances can't enlist
 *   - a militia's members keep the standing it took to join:  below it they're warned, and a day later they're out
 *   - the Amarr and Caldari militias are at war with the Gallente and Minmatar ones.  a militia's members may fight their
 *     enemies' anywhere, high security too, without CONCORD (CrimeWatch::CanAttackFreely()), and the wars don't count
 *     towards a corporation's own
 *   a corporation's militia is its crpCorporation.warFactionID, as the session's warfactionid has it (the militia
 *   corporations' own since this patch).  facWarEnlistments keeps who enlisted when, and the corporations' joins and
 *   leaves waiting for downtime.  downtime is the hour the server counts alliance executors at (alliance.ExecutorTallyHour)
 *
 *   the second stage:  complexes and system control (FacWarSystems.cpp).  Crucible's rules (the Empyrean Age dev
 *   blogs of 2008, "War is a full time job" of 2009):
 *   - a war zone system is held by its occupier (facWarSystems.occupierID).  its complexes are cosmic anomalies, each held
 *     by one of the zone's two factions:  the occupier's, which its enemy takes for victory points in the system, or, once
 *     the enemy has some there, the enemy's, which the occupier takes to win them back
 *   - a complex is taken by keeping a ship within its capture point's range (10, 20 or 30 km) for 10, 15 or 20 minutes
 *     (minor, medium, major) with no ship of the holder's side in range.  how many ships doesn't matter;  the holder's side
 *     alone in range winds it back.  the gates take what militia missions of levels 1, 2 and 3 allow
 *   - at enough victory points the system's control bunker can be shot.  destroyed, the system goes to the faction that
 *     destroyed it at the next downtime, and its victory points start over.  holding a system gives nothing else
 *   picks:  victory points per complex (ComplexPoints()) and the threshold (VulnerableAt);  the holder's guards stop the
 *   clock while any is left;  standing with your own faction of 0.0015 per victory point (the client's
 *   facwarStandingPerVictoryPoint) for each pilot in range of a taken complex;  allies count on their ally's side;  about
 *   two complexes a system, each taken one replaced half an hour to an hour and a half later somewhere in the war zones;
 *   the bunker doesn't repair itself, but is whole again when the enemy's points are all won back
 */

#ifndef EVEMU_FACTION_FACWARMGR_H_
#define EVEMU_FACTION_FACWARMGR_H_

#include "eve-server.h"

#include <unordered_map>
#include <unordered_set>

class AnomalyMgr;
struct KillData;
class SystemManager;

class FacWarMgr
: public Singleton<FacWarMgr>
{
public:
    FacWarMgr();

    // the corporations' militias, the joins and leaves waiting, and the war zones
    int Initialize();
    // once a minute:  the corporations' joins and leaves due, and the daily standing check at downtime
    void Process();

    /* the militias */
    // the four empires have one
    static bool IsWarFaction(uint32 factionID);
    // the faction's militia corporation (0:  none), and back
    static uint32 MilitiaCorpOf(uint32 factionID);
    static uint32 FactionOfMilitiaCorp(uint32 corpID);
    static bool IsMilitiaCorp(uint32 corpID)                        { return (FactionOfMilitiaCorp(corpID) != 0); }
    // their militias are at war:  Amarr and Caldari against Gallente and Minmatar
    static bool AreEnemies(uint32 factionA, uint32 factionB);
    // the faction its militia's war zone is shared with (Amarr and Minmatar, Caldari and Gallente)
    static uint32 ZoneEnemyOf(uint32 factionID);
    // the corporation's militia (0:  none)
    uint32 WarFactionOf(uint32 corpID) const;
    // the corporations' militias are at war
    bool EnemyCorps(uint32 corpA, uint32 corpB) const;
    // ...they're in the same one
    bool SameMilitia(uint32 corpA, uint32 corpB) const;
    // the corporations in the faction's militia, its own first
    void CorpsOf(uint32 factionID, std::vector<uint32>& into) const;
    // the system is in the faction's war zone (facWarSystems)
    bool InWarZone(uint32 systemID, uint32 factionID) const;

    /* enlisting */
    // the character's standing to the faction, and a corporation's (the average of its members' that aren't 0)
    static float StandingOf(uint32 charID, uint32 factionID);
    static float CorpStandingOf(uint32 corpID, uint32 factionID);
    // the character may enlist in the faction's militia (with a letter of recommendation:  whatever its standing).  why not
    bool CanEnlist(uint32 charID, uint32 factionID, bool letter, std::string& err) const;
    // ...and does:  into its militia corporation
    bool Enlist(uint32 charID, uint32 factionID, bool letter, std::string& err);
    // the player corporation may enlist, and asks to (it joins at the next downtime)
    bool CanEnlistCorp(uint32 corpID, uint32 factionID, std::string& err) const;
    bool AskToJoin(uint32 corpID, uint32 factionID, uint32 byCharID, std::string& err);
    // ...asks to leave (at the next downtime), and takes back what it asked
    bool AskToLeave(uint32 corpID, uint32 factionID, uint32 byCharID, std::string& err);
    bool Withdraw(uint32 corpID, bool join, std::string& err);
    // a character's militia as the client's GetFactionalWarStatus() has it:  its corporation's, joining, in or leaving
    bool StatusOf(uint32 charID, uint32 corpID, uint32& factionID, uint8& status, int64& since) const;

    // the downtime after the time
    static int64 NextDowntime(int64 now);

    /* the war zones' systems */
    // victory points that make a system's control bunker vulnerable
    static const int32 VulnerableAt = 200;
    // a complex's victory points, and how long it takes to capture (seconds), by size (1 minor, 2 medium, 3 major)
    static int32 ComplexPoints(uint8 size);
    static double CaptureSeconds(uint8 size);
    // the factions are on the same side (the same, or allies)
    static bool SameSide(uint32 factionA, uint32 factionB);
    // the faction's control bunker type
    static uint16 BunkerTypeOf(uint32 factionID);
    // the system is in a war zone
    bool IsZoneSystem(uint32 systemID) const                    { return (m_systems.find(systemID) != m_systems.end()); }
    // its occupier (0:  not in a war zone), the victory points its enemy has there, and the client's contestion state
    uint32 OccupierOf(uint32 systemID) const;
    int32 VictoryPointsIn(uint32 systemID) const;
    uint8 SystemStatus(uint32 systemID) const;
    // the system's control bunker may be shot
    bool BunkerVulnerable(uint32 systemID) const;

    // a loaded system with pilots in it (AnomalyMgr::Process()):  its complexes and its bunker are there
    void SyncSystem(SystemManager* pSystem, AnomalyMgr& anomMgr);
    // the live site is a complex
    bool IsComplexSite(uint32 instanceID) const                 { return (m_complexOfSite.find(instanceID) != m_complexOfSite.end()); }
    // its size and holder (false:  no complex)
    bool ComplexOf(uint32 instanceID, uint8& size, uint32& holderID) const;
    /* a second (or more) of a complex's capture (DungeonMgr::Process()):  the pilots in its capture point's range on the
     *  attacker's side, how many of the holder's side, and how many guards are left.  true:  it's taken */
    bool CaptureTick(uint32 instanceID, const std::vector<uint32>& takers, uint32 holders, uint32 guards, double seconds);
    // the system's control bunker (ControlBunkerSE):  what's left of it, hit or not, and its end
    void GetBunkerHealth(uint32 systemID, double& shield, double& armor, double& hull) const;
    void OnBunkerDamaged(uint32 systemID, double shield, double armor, double hull);
    void OnBunkerDestroyed(uint32 systemID, uint32 charID);

    // the client's:  facWarMgr.GetFWSystems(), map.GetVictoryPoints() and facWarMgr.GetSystemsConqueredThisRun()
    PyRep* GetFWSystems() const;
    PyRep* GetVictoryPoints() const;
    PyRep* GetSystemsConquered() const;

    // GM (/fw):  a system's state, its victory points, more complexes there now, and the downtime flips now
    std::string Describe(uint32 systemID) const;
    void SetVictoryPoints(uint32 systemID, int32 points);
    void AddComplexes(SystemManager* pSystem, uint8 count);
    void FlipConquered();
    // counts for the log:  complexes, live ones, taken since the start, systems contested, vulnerable and conquered
    void Stats(uint32& complexes, uint32& live, uint32& taken, uint32& contested, uint32& vulnerable, uint32& conquered) const;

    /* ranks and loyalty points for kills (FacWarRewards.cpp) */
    // the rank a militia pilot has with its faction at a standing with it (Crucible's ranks go by standing):  0 under 1.0,
    //  then one more for each point of standing, to 9
    static uint8 RankAt(float standing);
    // ...the character's with the faction now, saved with the highest it reached.  changed:  not the rank last saved
    uint8 UpdateRank(uint32 charID, uint32 factionID, bool& changed);
    // the character's ranks (currentRank, highestRank, factionID, lastModified), its militia's (factionID) brought up to date
    PyRep* RankRows(uint32 charID, uint32 factionID);
    /* a kill (ServiceDB::SaveKillOrLoss()):  an enemy militia pilot's ship destroyed pays loyalty points in their militias to
     *  its militia attackers, shared between them.  what the hull's worth (KillPoints()), more for a higher ranked victim
     */
    void OnKill(const KillData& data);
    // ...the loyalty points a kill of the hull pays its attackers in all, by the victim's rank (0:  none, a pod)
    static uint32 KillPoints(uint32 shipTypeID, uint8 victimRank);
    // kills that paid loyalty points, and the points, since the start
    uint32 KillsPaid() const                                    { return m_killsPaid; }
    uint32 KillPointsPaid() const                               { return m_killPoints; }

private:
    struct Request {
        uint32 factionID = 0;
        uint8 status = 0;               // FacWar::CorpStatus:  Joining or Leaving
        int64 askedAt = 0;
        int64 effectiveAt = 0;          // the downtime it takes effect at
    };

    // the corporation is in the militia (factionID) or out of it (0) now:  its row, its members' sessions and ships
    void SetCorpMilitia(uint32 corpID, uint32 factionID);
    // members below the standing their militia takes:  warned, then out a day later
    void CheckStandings(int64 now);

    // complexes a war zone system starts with, and after it changes hands
    static const uint8 ComplexesPerSystem = 2;

    struct Complex {
        uint32 systemID = 0;
        uint32 holderID = 0;            // the faction whose it is:  its guards and capture point
        uint8 size = 1;                 // 1 minor, 2 medium, 3 major
        uint32 dungeonID = 0;
        uint32 siteID = 0;              // its live site (DungeonMgr instance) while its system is loaded
        double progress = 0;            // seconds of capturing done
        int64 lastTold = 0;             // the takers were last told how it goes
    };
    struct ZoneSystem {
        uint32 factionID = 0;           // whose war zone it's in
        uint32 occupierID = 0;
        int32 points = 0;               // the occupier's enemy's victory points here
        uint32 capturedBy = 0;          // destroyed the bunker:  takes the system at the downtime after capturedAt
        int64 capturedAt = 0;
        double shield = 1.0, armor = 1.0, hull = 1.0;   // the bunker's
        uint32 bunkerID = 0;            // its live bunker while the system is loaded
        bool dirty = false;
    };

    void LoadSystems();
    void SaveSystem(uint32 systemID);
    // a new complex in the system:  the occupier's, or the enemy's once it has points there (DefensiveShare)
    void AddComplex(uint32 systemID);
    // a complex is gone (taken, or its system changed hands):  its live site too
    void RemoveComplex(uint32 complexID);
    void Captured(uint32 complexID, const std::vector<uint32>& takers);
    // the bunker's whole again (its live one too)
    void RepairBunker(uint32 systemID);
    // its live bunker:  put up, with what's left of it, and on the scanner
    void SpawnBunker(SystemManager* pSystem, AnomalyMgr& anomMgr, ZoneSystem& sys);
    // the dungeon of a complex of this holder and size
    uint32 PickDungeon(uint32 holderID, uint8 size, bool gated) const;
    // tell everyone in space in the system
    void TellSystem(uint32 systemID, const std::string& text);

    std::map<uint32, Complex> m_complexes;                      // ours, from 1
    std::unordered_map<uint32, uint32> m_complexOfSite;         // live site:  its complex
    std::unordered_map<uint32, ZoneSystem> m_systems;           // war zone system:
    std::multimap<int64, uint32> m_newComplexes;                // due:  a complex for a random war zone system
    uint32 m_nextComplexID;
    bool m_seeded;                                              // the war zones got their first complexes (the dungeons load after us)
    uint32 m_taken;
    uint32 m_killsPaid;
    uint32 m_killPoints;
    int64 m_lastSave;

    std::unordered_map<uint32, uint32> m_corpFaction;           // corporation:  its militia
    std::map<uint32, Request> m_requests;                       // corporation:  its join or leave waiting
    std::unordered_map<uint32, uint32> m_zone;                  // war zone system:  its faction (facWarSystems.factionID)
    int64 m_lastCheckDay;
};

#define sFacWarMgr \
( FacWarMgr::get() )

#endif  // EVEMU_FACTION_FACWARMGR_H_
