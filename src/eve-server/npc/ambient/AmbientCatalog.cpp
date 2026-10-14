 /**
  * @name AmbientCatalog.cpp
  *   the ambient npcs' ship types (doc/code_and_design_notes/planning/ambient-npc-plan.md section 7).  see AmbientCatalog.h
  */

#include "eve-server.h"

#include <algorithm>
#include <sstream>

#include "EVEServerConfig.h"
#include "inventory/AttributeEnum.h"
#include "inventory/InventoryItem.h"
#include "inventory/ItemFactory.h"
#include "inventory/ItemType.h"
#include "npc/ai/AmbientRules.h"
#include "npc/ai/NpcRng.h"
#include "npc/ambient/AmbientCatalog.h"

using namespace Ambient;

namespace {
    // reference player hulls (7.2)
    const uint16 refBestower = 1944, refBadger2 = 649, refIteron5 = 657, refMammoth = 652;
    const uint16 refProvidence = 20183, refCharon = 20185, refObelisk = 20187, refFenrir = 20189;
    const uint16 refRifter = 587, refThrasher = 16242, refStabber = 622;
    const uint16 stripMinerI = 17482;
    // category 11 (Entity) and 6 (Ship)
    const uint8 catEntity = 11, catShip = 6;

    double TypeAttr(const ItemType* pType, uint16 attrID, double fallback)
    {
        if ((pType == nullptr) or !pType->HasAttribute(attrID))
            return fallback;
        return pType->GetAttribute(attrID).get_double();
    }
}

AmbientCatalog::AmbientCatalog()
: m_dropped(0),
  m_minerModule(stripMinerI),
  m_minerCycleMs(180000),
  m_minerRange(15000.0)
{
}

bool AmbientCatalog::Validate(const Entry& e, const std::map<uint32, uint32>& graphics, Archetype& into)
{
    const ItemType* pType(sItemFactory.GetType(e.typeID));
    if (pType == nullptr) {
        _log(AMBIENT__WARN, "AmbientCatalog: type %u doesn't exist.  dropped", e.typeID);
        return false;
    }
    bool hull(e.role == Role::Miner and (pType->categoryID() == catShip));
    if ((pType->categoryID() != catEntity) and !hull) {
        _log(AMBIENT__WARN, "AmbientCatalog: %s (%u) is category %u, not an entity.  dropped", pType->name().c_str(), e.typeID,
             pType->categoryID());
        return false;
    }
    std::map<uint32, uint32>::const_iterator gItr(graphics.find(e.typeID));
    if ((gItr == graphics.end()) or (gItr->second == 0)) {
        _log(AMBIENT__WARN, "AmbientCatalog: %s (%u) has no graphic.  dropped", pType->name().c_str(), e.typeID);
        return false;
    }
    // civilians, navy and CONCORD must show a neutral bracket (entityBracketColour 798 < 1:  red is for hostiles, 2.6)
    if ((e.role != Role::Criminal) and !hull and (TypeAttr(pType, AttrEntityBracketColour, 0.0) >= 1.0)) {
        _log(AMBIENT__WARN, "AmbientCatalog: %s (%u) shows a hostile bracket.  dropped", pType->name().c_str(), e.typeID);
        return false;
    }

    const ItemType* pRef(e.refTypeID != 0 ? sItemFactory.GetType(e.refTypeID) : pType);
    if (pRef == nullptr) {
        _log(AMBIENT__WARN, "AmbientCatalog: reference hull %u of %s (%u) doesn't exist.  using its own data", e.refTypeID,
             pType->name().c_str(), e.typeID);
        pRef = pType;
    }

    into = Archetype();
    into.typeID = e.typeID;
    into.role = e.role;
    into.race = e.race;
    into.hullClass = e.hullClass;
    into.pirateFaction = e.pirateFaction;
    into.refTypeID = pRef->id();
    into.hull = hull;
    into.modules = e.modules;
    into.typeName = pType->name();
    // agility, speed and warp speed are the reference hull's (4.2):  npc types have none of their own, or absurd ones
    into.agility = static_cast<float>(TypeAttr(pRef, AttrInetia, TypeAttr(pType, AttrInetia, 1.0)));
    if (!(into.agility > 0.0f))
        into.agility = 1.0f;
    double own(TypeAttr(pType, AttrMaxVelocity, 0.0));
    double ref(TypeAttr(pRef, AttrMaxVelocity, own) * sConfig.ambient.SpeedOverRef);
    double speed((own > 0.0) ? std::min(own, ref) : ref);
    into.maxVelocity = static_cast<float>(speed > 1.0 ? speed : 100.0);
    into.mass = (pType->mass() > 0.0f ? pType->mass() : pRef->mass());
    into.warpFactor = AmbientRules::WarpFactorFor(TypeAttr(pRef, AttrWarpSpeedMultiplier, 1.0), TypeAttr(pRef, AttrBaseWarpSpeed, 1.0));
    return true;
}

void AmbientCatalog::Load(const std::map<uint32, uint32>& graphics)
{
    m_types.clear();
    m_byType.clear();
    m_dropped = 0;

    std::vector<Entry> entries = {
        /* haulers (group 927, 670), 7.2 */
        {2885,  Role::Hauler, Race::Amarr,    0, 0, refBestower, 0},     // Imperial Industrial Vessel
        {30974, Role::Hauler, Race::Amarr,    0, 0, refBestower, 0},     // Civilian Amarr Bestower
        {2883,  Role::Hauler, Race::Caldari,  0, 0, refBadger2, 0},      // State Industrial Vessel
        {32455, Role::Hauler, Race::Caldari,  0, 0, refBadger2, 0},      // Ishukone Hauler
        {30948, Role::Hauler, Race::Caldari,  0, 0, refBadger2, 0},      // State Transport
        {2878,  Role::Hauler, Race::Gallente, 0, 0, refIteron5, 0},      // Federation Industrial Vessel
        {2884,  Role::Hauler, Race::Minmatar, 0, 0, refMammoth, 0},      // Republic Industrial Vessel
        /* couriers (A4:  the data gives them a hostile bracket, so validation drops them and couriers fly hauler hulls) */
        {29290, Role::Courier, Race::Amarr,    0, 0, refBestower, 0},
        {29291, Role::Courier, Race::Caldari,  0, 0, refBadger2, 0},
        {29292, Role::Courier, Race::Gallente, 0, 0, refIteron5, 0},
        {29293, Role::Courier, Race::Minmatar, 0, 0, refMammoth, 0},
        /* freighters (group 875), hubs only */
        {2879,  Role::Freighter, Race::Amarr,    0, 0, refProvidence, 0},
        {2882,  Role::Freighter, Race::Caldari,  0, 0, refCharon, 0},
        {2877,  Role::Freighter, Race::Gallente, 0, 0, refObelisk, 0},
        {32230, Role::Freighter, Race::Minmatar, 0, 0, refFenrir, 0},
        /* navy (group 288):  frigate, cruiser, battleship */
        {9998,  Role::Navy, Race::Amarr,    0, 0, 597, 0},   // Imperial Navy Sergeant (Punisher)
        {10001, Role::Navy, Race::Amarr,    1, 0, 2006, 0},  // Imperial Navy Colonel (Omen)
        {11137, Role::Navy, Race::Amarr,    2, 0, 642, 0},   // Imperial Navy Fleet Marshall (Apocalypse)
        {10651, Role::Navy, Race::Caldari,  0, 0, 583, 0},   // Caldari Navy Captain 3rd Rank (Condor)
        {10050, Role::Navy, Race::Caldari,  1, 0, 623, 0},   // Caldari Navy Vice Admiral (Moa)
        {11138, Role::Navy, Race::Caldari,  2, 0, 638, 0},   // Caldari Navy Fleet Admiral (Raven)
        {10652, Role::Navy, Race::Gallente, 0, 0, 608, 0},   // Federation Navy First Sergeant (Atron)
        {10056, Role::Navy, Race::Gallente, 1, 0, 626, 0},   // Federation Navy Fleet Major (Vexor)
        {11139, Role::Navy, Race::Gallente, 2, 0, 641, 0},   // Federation Navy Fleet General (Megathron)
        {10663, Role::Navy, Race::Minmatar, 0, 0, 585, 0},   // Republic Fleet Private 1st Rank (Slasher)
        {10078, Role::Navy, Race::Minmatar, 1, 0, 622, 0},   // Republic Fleet High Captain (Stabber)
        {11140, Role::Navy, Race::Minmatar, 2, 0, 639, 0},   // Republic Fleet High Commander (Tempest)
        /* CONCORD (group 301, the punishment fleet types) */
        {3863,  Role::Concord, Race::None, 0, 0, 608, 0},    // CONCORD Police Officer (Atron)
        {3885,  Role::Concord, Race::None, 1, 0, 627, 0},    // CONCORD Police Captain (Thorax)
        {11125, Role::Concord, Race::None, 2, 0, 641, 0},    // CONCORD Police Commander (Megathron)
        /* criminals:  frigates, destroyer, cruiser per pirate faction */
        {10014, Role::Criminal, Race::None, 0, factionAngel, refRifter, 0},
        {10016, Role::Criminal, Race::None, 0, factionAngel, refRifter, 0},
        {2372,  Role::Criminal, Race::None, 0, factionAngel, refRifter, 0},
        {22813, Role::Criminal, Race::None, 1, factionAngel, refThrasher, 0},
        {10017, Role::Criminal, Race::None, 2, factionAngel, refStabber, 0},
        {11040, Role::Criminal, Race::None, 0, factionBloodRaider, refRifter, 0},
        {10280, Role::Criminal, Race::None, 0, factionBloodRaider, refRifter, 0},
        {10275, Role::Criminal, Race::None, 0, factionBloodRaider, refRifter, 0},
        {23246, Role::Criminal, Race::None, 1, factionBloodRaider, refThrasher, 0},
        {10282, Role::Criminal, Race::None, 2, factionBloodRaider, refStabber, 0},
        {10266, Role::Criminal, Race::None, 0, factionGuristas, refRifter, 0},
        {2382,  Role::Criminal, Race::None, 0, factionGuristas, refRifter, 0},
        {23313, Role::Criminal, Race::None, 1, factionGuristas, refThrasher, 0},
        {11036, Role::Criminal, Race::None, 2, factionGuristas, refStabber, 0},
        {11033, Role::Criminal, Race::None, 0, factionSanshas, refRifter, 0},
        {10028, Role::Criminal, Race::None, 0, factionSanshas, refRifter, 0},
        {10025, Role::Criminal, Race::None, 0, factionSanshas, refRifter, 0},
        {23379, Role::Criminal, Race::None, 1, factionSanshas, refThrasher, 0},
        {10030, Role::Criminal, Race::None, 2, factionSanshas, refStabber, 0},
        {10285, Role::Criminal, Race::None, 0, factionSerpentis, refRifter, 0},
        {11046, Role::Criminal, Race::None, 0, factionSerpentis, refRifter, 0},
        {2370,  Role::Criminal, Race::None, 0, factionSerpentis, refRifter, 0},
        {23424, Role::Criminal, Race::None, 1, factionSerpentis, refThrasher, 0},
        {11049, Role::Criminal, Race::None, 2, factionSerpentis, refStabber, 0},
        /* miners:  hull mode barges (their own flight data) and the entity fallback */
        {17480, Role::Miner, Race::None, 0, 0, 0, 1},       // Procurer
        {17478, Role::Miner, Race::None, 1, 0, 0, 2},       // Retriever
        {17476, Role::Miner, Race::None, 2, 0, 0, 3},       // Covetor
        {22544, Role::Miner, Race::None, 3, 0, 0, 3},       // Hulk
        {32447, Role::Miner, Race::None, 0, 0, 22544, 0},   // Civilian Hulk (entity mode)
    };

    for (auto& cur : entries) {
        Archetype a;
        if (!Validate(cur, graphics, a)) {
            ++m_dropped;
            continue;
        }
        m_types.push_back(a);
        m_byType[a.typeID] = &m_types.back();
    }

    // the strip miner's cycle (duration 73) and range (maxRange 54)
    const ItemType* pModule(sItemFactory.GetType(stripMinerI));
    if (pModule != nullptr) {
        m_minerCycleMs = static_cast<uint32>(TypeAttr(pModule, AttrDuration, 180000.0));
        m_minerRange = TypeAttr(pModule, AttrMaxRange, 15000.0);
    } else {
        _log(AMBIENT__WARN, "AmbientCatalog: Strip Miner I (%u) doesn't exist.  hull miners show no beams", stripMinerI);
        m_minerModule = 0;
    }
    if (m_minerCycleMs < 10000)
        m_minerCycleMs = 10000;
    LoadLoot();
}

void AmbientCatalog::LoadLoot()
{
    // 10.4, A8:  common trade goods, by name;  a name the data doesn't have is dropped with a warning
    m_tradeGoods.clear();
    static const char* goods[] = {"Spirits", "Tobacco", "Frozen Plant Seeds", "Antibiotics", "Consumer Electronics",
                                  "Mechanical Parts", "Water"};
    std::string list;
    for (auto cur : goods) {
        if (!list.empty())
            list += ",";
        list += "'";
        list += cur;
        list += "'";
    }
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res, "SELECT typeID, typeName FROM invTypes WHERE typeName IN (%s)", list.c_str()))
        return;
    std::map<std::string, uint32> found;
    while (res.GetRow(row))
        found[row.GetText(1)] = row.GetUInt(0);
    for (auto cur : goods) {
        std::map<std::string, uint32>::iterator itr(found.find(cur));
        if (itr == found.end()) {
            _log(AMBIENT__WARN, "AmbientCatalog: trade good '%s' isn't in invTypes.  dropped", cur);
            continue;
        }
        m_tradeGoods.push_back(Loot::Item{itr->second, 0, 0.6, 5, 60});
    }
}

const Archetype* AmbientCatalog::Pick(NpcRng& rng, uint8 role, uint8 race/*None*/, uint8 hullClass/*255*/,
                                      uint32 pirateFaction/*0*/, bool hull/*false*/) const
{
    std::vector<const Archetype*> found;
    for (auto& cur : m_types) {
        if (cur.role != role)
            continue;
        if ((race != Race::None) and (cur.race != race))
            continue;
        if ((hullClass != 255) and (cur.hullClass != hullClass))
            continue;
        if ((pirateFaction != 0) and (cur.pirateFaction != pirateFaction))
            continue;
        if ((role == Role::Miner) and (cur.hull != hull))
            continue;
        found.push_back(&cur);
    }
    if (found.empty())
        return nullptr;
    size_t idx(static_cast<size_t>(rng.Uniform() * static_cast<double>(found.size())));
    return found[std::min(idx, found.size() - 1)];
}

const Archetype* AmbientCatalog::Find(uint16 typeID) const
{
    std::map<uint16, const Archetype*>::const_iterator itr(m_byType.find(typeID));
    return (itr == m_byType.end() ? nullptr : itr->second);
}

bool AmbientCatalog::HasHullMiners() const
{
    for (auto& cur : m_types)
        if ((cur.role == Role::Miner) and cur.hull)
            return true;
    return false;
}

std::string AmbientCatalog::Describe() const
{
    std::ostringstream str;
    uint32 count[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (auto& cur : m_types)
        if (cur.role < 8)
            ++count[cur.role];
    str << m_types.size() << " types valid, " << m_dropped << " dropped (";
    for (uint8 i = 1; i < 8; ++i)
        str << (i > 1 ? ", " : "") << RoleName(i) << " " << count[i];
    str << ").  strip miner cycle " << (m_minerCycleMs / 1000) << "s, range " << static_cast<uint32>(m_minerRange) << "m.  "
        << m_tradeGoods.size() << " trade goods";
    return str.str();
}
