
 /**
  * @name Exploration.h
  *   exploration site containers (hacking, archaeology, salvage) and their loot
  *
  * @Author:        F11
  * @date:          11 October 2026
  */

// Sleeper Databank
// ATTRIBS 1330,1331

#ifndef EVEMU_EXPLORATION_EXPLORATION_H_
#define EVEMU_EXPLORATION_EXPLORATION_H_

#include "eve-server.h"

class Client;
class ContainerSE;
class SystemManager;

namespace Exploration {
    // expContainerFamily.familyID
    namespace Family {
        enum : uint8 {
            None            = 0,
            Hacking         = 1,
            Archaeology     = 2,
            Salvage         = 3,
            SleeperData     = 4,
            SleeperDebris   = 5,
            AncientData     = 6,
            AncientDebris   = 7
        };
    }

    struct LootRow {
        bool isCopy;            // blueprint copy (tech 1 only), quantity is its runs
        uint16 itemGroupID;
        uint16 itemTypeID;      // 0 = random published type of itemGroupID
        uint32 minQty;
        uint32 maxQty;
        float chance;
        std::string pattern;    // '' any, '*' container's faction prefix, 'x' starts with x, '%x' contains x
    };

    // a survey probe on its way to a moon
    struct SurveyFlight {
        uint16 probeTypeID;
        uint32 charID;
        uint32 systemID;
        uint32 moonID;
        int64 landTime;         // filetime
    };
}

/* Crucible (pre-Odyssey) site containers:  locked spawn containers (invGroup 306 with accessDifficulty) open with a
 *  Codebreaker, Analyzer or Salvager by a per-cycle roll (ship/modules/Prospector).  once opened they're open for
 *  everyone, and their loot is made then (from expContainerLoot by the container's family).
 */
class ExplorationMgr
: public Singleton< ExplorationMgr >
{
public:
    ExplorationMgr();
    ~ExplorationMgr()                                   { /* nothing to do */ }

    // loads expContainerFamily/expContainerLoot.  a db without them only logs a warning (containers open empty)
    int Initialize();

    // locked spawn container type (opened by a data miner or salvager)
    bool IsLockedType(uint16 typeID);
    // generates the loot of a locked site container, once.  a container that already holds items is left alone
    void FillContainer(InventoryItemRef contRef);

    // spawns a site container as a lockable ContainerSE (missions, ad-hoc sites).  dungeon objects come through
    //  SystemManager::BuildDynamicEntity, which does the same
    ContainerSE* SpawnSiteContainer(SystemManager* pSys, uint16 typeID, uint32 ownerID, const GPoint& pos, const std::string& customInfo);

    /* survey (moon) probes.  launched from an expanded probe launcher, a survey probe flies to the moon the ship is
     *  heading for (within its scanRange AU and exploring.SurveyProbeConeAngle), lands after its explosionDelay
     *  (frigate and covert ops hull bonuses) and reports the moon's materials (OnMoonScanComplete) */
    void LaunchSurveyProbe(Client* pClient, ShipItemRef shipRef, InventoryItemRef chargeRef);
    // 1Hz (EntityList):  survey probe landings
    void Process();

private:
    void LandSurveyProbe(const Exploration::SurveyFlight& flight);

    // published types of this group (lazy) with their names
    const std::vector<std::pair<uint16, std::string>>& GetGroupTypes(uint16 groupID);
    // a type for this row, or 0
    uint16 PickType(const Exploration::LootRow& row, const std::string& prefix);
    // spawns a row's item into the container.  false if nothing was made
    bool SpawnLoot(const Exploration::LootRow& row, InventoryItemRef contRef, uint32 ownerID, const std::string& prefix, uint8 tier);

    std::map<uint16, std::pair<uint8, std::string>> m_family;     // container typeID -> family, faction prefix
    std::multimap<uint8, Exploration::LootRow> m_loot;              // family -> rows, in table order
    std::map<uint16, std::vector<std::pair<uint16, std::string>>> m_groupTypes;
    std::list<Exploration::SurveyFlight> m_flights;
};

#define sExplorationMgr \
( ExplorationMgr::get() )

#endif  // EVEMU_EXPLORATION_EXPLORATION_H_
