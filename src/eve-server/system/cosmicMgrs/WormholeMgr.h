
 /**
  * @name WormholeMgr.h
  *     WH Spawn management system for EVEmu
  *
  * @Author:        Allan
  * @date:          12 December 2015
  *
  * Updates:        James
  * Date:          13 November 2021
  */


#ifndef EVEMU_SYSTEM_WORMHOLEMGR_H_
#define EVEMU_SYSTEM_WORMHOLEMGR_H_


#include "ServiceDB.h"
#include "utils/Singleton.h"
#include "system/cosmicMgrs/ManagerDB.h"

/* this class controls all wormholes:  their statics, both ends of each wormhole, the mass and lifetime the two ends
 *  share, jumps and collapse.  the open wormholes are kept in sysWormholes and each end is an item (with a
 *  signature) in its system, so they outlive a restart.
 *
 * Crucible rules (reports/wormholes-research.md):
 *  - every w-space system always has its statics (mapWormholeSystems).  a static that collapses is replaced at once
 *  - the far end (K162) and the destination come when the first ship warps to the wormhole
 *  - each jump takes the ship's mass (prop modules count) from the mass both ends share.  out of mass or time, it
 *    collapses
 *  - a pilot makes at most 2 jumps through a wormhole in 4 minutes, and lands within 5 km of the far end
 */

class Client;
class EVEServiceManager;
class SystemManager;

struct WormholePair {
    uint32 wormholeID;      // itemID of the typed end
    uint32 typeID;
    uint32 systemID;
    GPoint position;
    bool isStatic;
    uint32 exitID;          // itemID of the K162.  0 until a ship warps to the wormhole
    uint32 exitSystemID;
    GPoint exitPosition;
    int64 massTotal;        // kg
    int64 massUsed;
    uint32 jumps;
    int64 createTime;       // filetime
    int64 expiryTime;
    int64 collapseTime;     // the mass ran out:  it goes once the last ship is through.  0 = not collapsing
    uint8 age;              // WormHole::Age and WormHole::Size the slim items last showed
    uint8 size;
    std::map<uint32, std::vector<int64>> charJumps;     // charID -> jump times in the jump limit window
};

class WormholeMgr
: public Singleton<WormholeMgr>
{
public:
    WormholeMgr();
    ~WormholeMgr();

    void Initialize();
    // 1Hz
    void Process();

    bool IsEnabled() const                              { return m_initalized; }

    // a wandering wormhole at the signature AnomalyMgr made.  sig.sigItemID stays 0 when none is made
    void Create(CosmicSignature& sig);
    // a system has booted:  its statics are open and its wormhole ends are all there
    void SystemBooted(SystemManager* pSys);
    // a ship warps to a wormhole:  the far end opens
    void OnWarpTo(uint32 itemID);
    // wormholeMgr.WormholeJump.  throws to refuse
    void Jump(Client* pClient, uint32 itemID);
    // what WormholeSE::MakeSlimItem shows.  false when the item isn't a live wormhole
    bool GetSlimData(uint32 itemID, uint8& otherClass, float& size, uint8& age, int64& expiry, uint16& count);

    // GM commands (/wormhole)
    std::string GetInfo(uint32 systemID);
    bool CollapseByItem(uint32 itemID);
    uint32 SpawnAt(SystemManager* pSys, uint32 typeID, const GPoint& pos);

private:
    ManagerDB* m_mdb;

    Timer m_updateTimer;

    bool m_initalized;

    std::map<uint32, WormholePair> m_pairs;     // wormholeID -> pair
    std::map<uint32, uint32> m_byItem;          // either end's itemID -> wormholeID
    std::map<uint32, std::vector<uint32>> m_statics;    // w-space systemID -> static typeIDs
    std::set<uint32> m_staticTypes;             // types that are a static somewhere (never wander)
    std::set<uint32> m_wspaceLoaded;            // booted w-space systems, for the static check

    // Generic functions used when managing wormholes
    const ItemType* GetRandomWormholeType(uint32 systemID);
    uint32 GetRandomDestination(const ItemType* type, uint32 fromSystemID);

    // the typed end in its system.  0 on failure
    uint32 OpenWormhole(uint32 systemID, uint32 typeID, bool isStatic, const GPoint* pos = nullptr, CosmicSignature* sigOut = nullptr);
    // the far end (K162)
    void OpenExit(WormholePair& wh);
    // an end item and its signature in its system (loaded or not).  0 on failure
    uint32 SpawnEnd(uint32 systemID, uint32 typeID, const GPoint& pos, CosmicSignature& sig);
    void RemoveEnd(uint32 itemID, uint32 systemID);
    void Collapse(uint32 wormholeID);
    void CheckStatics(SystemManager* pSys);
    void SendSlim(const WormholePair& wh);
    void Save(const WormholePair& wh);

    static uint8 AgeOf(const WormholePair& wh, int64 now);
    static uint8 SizeOf(const WormholePair& wh);
};


//Singleton
#define sWHMgr \
    ( WormholeMgr::get() )

#endif  // EVEMU_SYSTEM_WORMHOLEMGR_H_



/*
 * Wormhole Space Classes
 *
 * W-space systems fall into one of six classes:
 *
 * Unknown Space - Class 1-3
 * Dangerous Unknown Space - Class 4 and 5
 * Deadly Unknown Space - Class 6
 *
 * Class 1 and 2 systems can be soloed.
 * Class 3 you want some friends with RR.
 * Class 4 you want lots of friends with RR.
 * Class 5 you definitely want lots of friends with RR and probably a capital.
 * Class 6 you definitely want a capital or two and lots of friends with RR.
 *
 *
 * Site Difficulty
 *
 * Site difficulty can be roughly determined by a keyword in the site name and the w-space class the site is in:
 *
 * Perimeter sites are generally Low difficulty.
 * Frontier sites are generally Medium difficulty.
 * Core sites are generally High difficulty.
 *
 * For Class 1-2 expect to see Perimeter sites.
 * For Class 3-4 expect to see Frontier sites.
 * For Class 5-6 expect to see Core sites.
 *
 * Gravimetric, Ladar and class 5 Radar sites are exempt from this; also named sites (i.e. The Line & Solar Cell for example) do not fall under this convention.
 *
 *
 * Sleeper Sites
 *
 * Each class of w-space should have 4 types of Cosmic Anomalies, 2 types of Radar and 2 types of Magnetometric cosmic signatures associated with it. This means that each class of w-space has its own set of Cosmic Anomaly, Radar and Magnetometric signature types that will not appear in any other class of w-space. The exception to this rule is Ladar and Gravimetric sites, as these sites can appear in several different classes of w-space and are not bound to a specific w-space class but in fact are each tied to a range of w-space classes.
 *
 *    Out in the darkness, a curious deadspace signature taunts brave and reckless explorers alike to try and uncover what secrets may be hidden in the depths. Electronic interference floods through your ship’s systems only moments after your warp drive locks on and activates, growing in ferocity as you are hurled towards the strange phenomenon. -Unsecured Frontier Enclave Relay
 *
 *
 * The w-space class determines the type of sleeper sites that will be spawned. In lower classes, easier sites will spawn; and in higher classes, harder sites will spawn. The same site, if spawned in a higher class system, may include a few more ships and a possible increase in scrambling, webbing and remote repair (RR) ships (this applies to Ladar and Gravimetric sites only). Expect sleepers in Class 1 and 2 not to warp scramble or only rarely; Class 3 and 4 you will encounter some warp scrambling; Class 5 and 6 expect 75% to 90% of the sleepers (yes Battleships too) to warp scramble, stasis web and/or NOS (30 KM scramble range in some cases). I think it also goes without saying that as you get into higher class w-space systems you will see an increase in the types of status effects used by the sleeper NPCs and an increase in the number of NPCs with those effects at their disposal.
 *
 *    Suddenly the illusion of solitude shatters. Slender, articulated obsidian shapes slide toward you from the surrounding darkness of space. Your sensors scream claxons into your auditory cortex as the drones' targeting systems lock onto your ship. You are not alone. Perhaps you never were.
 *    The Sleepers have risen. -Core Stronghold
 *
 *
 *
 * Sleeper Site Escalation
 *
 * Site escalation can be caused when you bring a large force into a site (capitals, large amounts of non-capital ships, etc). Site escalation does seem to also happen randomly on occasion, even without the presence of a large force. Site escalation may cause extra sleepers to spawn and additional waves to have increased spawn counts. Additionally, site escalation does not always seem to happen. This form of escalation was put in place to provide an extra challenge when players bring in more than enough to handle a site. When a Magnetometric site escalates it seems to also cause the Talocan spawn container to appear.
 *
 *
 * Sleeper Capital Escalation
 *
 * Capital escalation is caused when you warp certain types of capital ships into a site. Capital escalation appears to be tied to two, and only two, classes of capitals (it should be obvious which two), with each having their own escalation chain. For each class, the first capital will spawn 6 Sleepless Guardians, the 2nd capital will spawn 8 Sleepless Guardians, additional capitals will not trigger any further escalation. This form of escalation was put in place to provide an additional challenge when bringing capital ships into a site. Capital escalation only happens on combat sites (i.e. Anomalies, Magnetometric, Radar) and does not occur at gas or asteroid belts (i.e. Ladar and Gravimetric).
 *
 *    Alice warps her Thanatos into an anomaly, which causes 6 Sleeper Guardian battleships to spawn. Bob then warps his Thanatos into the same anomaly, which causes another 8 Sleeper Guardian battleships to spawn. Alice then starts yelling at Bob as the 14 Sleeper Guardians and the rest of the anomaly NPCs reduce their capitals to wrecks over the next two minutes.
 *
 *
 *
 * Sleeper AI
 *
 * The sleeper NPC AI is much more complex than normal NPC AI. Sleepers will switch targets regularly, attacking drones too. They also exhibit advanced threat detection and target selection. The sleepers handle multiple targets most dynamically; a sleeper may be warp scrambling Alice and stasis webbing Bob while it is shooting at Charlie.
 *
 *    Alice in the logistics ship may find herself drawing all the aggro while they ignore Bob in the shuttle.
 *
 *
 * Sleeper Remote Rep
 *
 * The sleepers that have remote repair ability will use it, sometimes to an annoying extent. Those sleeper frigates, cruisers and battleships, that can remote repair each other will do so indiscriminately; they also tend to assist the ones you happen to be shooting, aren't they clever. This can become annoying when, for example, the battleships have remote repair and are also the trigger for the next wave and they are happily remote repairing the frigates and cruisers while you try to kill them. The issue can be compounded if said frigates and cruisers also have the remote repair capability. Care should be taken on sites with large amounts of RR sleeper ships that a next wave isn't triggered ahead of time.
 */