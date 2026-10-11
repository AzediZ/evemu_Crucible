
 /**
  * @name Exploration.cpp
  *   exploration site containers (hacking, archaeology, salvage) and their loot
  *
  * @Author:        F11
  * @date:          11 October 2026
  */

#include "eve-server.h"

#include "exploration/Exploration.h"
#include "exploration/ScanningDB.h"
#include "manufacturing/Blueprint.h"
#include "system/Container.h"
#include "system/SystemManager.h"

ExplorationMgr::ExplorationMgr()
{
    m_family.clear();
    m_loot.clear();
    m_groupTypes.clear();
}

int ExplorationMgr::Initialize()
{
    double start = GetTimeMSeconds();
    DBQueryResult res;
    DBResultRow row;
    if (!ScanningDB::GetContainerFamilies(res)) {
        sLog.Warning("   ExplorationMgr", "expContainerFamily is missing (run the migrations).  Site containers will open empty.");
    } else {
        while (res.GetRow(row))
            m_family[row.GetUInt(0)] = std::make_pair((uint8)row.GetUInt(1), std::string(row.GetText(2)));
    }
    if (!ScanningDB::GetContainerLoot(res)) {
        sLog.Warning("   ExplorationMgr", "expContainerLoot is missing (run the migrations).  Site containers will open empty.");
    } else {
        while (res.GetRow(row)) {
            Exploration::LootRow loot = Exploration::LootRow();
                loot.itemGroupID = row.GetUInt(1);
                loot.itemTypeID = row.GetUInt(2);
                loot.pattern = row.GetText(3);
                loot.chance = row.GetFloat(4);
                loot.minQty = row.GetUInt(5);
                loot.maxQty = row.GetUInt(6);
                loot.isCopy = (row.GetUInt(7) > 0);
            if (loot.maxQty < loot.minQty)
                loot.maxQty = loot.minQty;
            m_loot.emplace((uint8)row.GetUInt(0), loot);
        }
    }

    sLog.Cyan("   ExplorationMgr", "%lu site container types and %lu loot rows loaded in %.3fms.",
              m_family.size(), m_loot.size(), (GetTimeMSeconds() - start));
    sLog.Blue("   ExplorationMgr", "Exploration Manager Initialized.");
    return 1;
}

bool ExplorationMgr::IsLockedType(uint16 typeID)
{
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr)
        return false;
    return ((pType->groupID() == EVEDB::invGroups::Spawn_Container) and pType->HasAttribute(AttrAccessDifficulty));
}

const std::vector<std::pair<uint16, std::string>>& ExplorationMgr::GetGroupTypes(uint16 groupID)
{
    auto itr = m_groupTypes.find(groupID);
    if (itr != m_groupTypes.end())
        return itr->second;

    std::vector<std::pair<uint16, std::string>>& types = m_groupTypes[groupID];
    DBQueryResult res;
    DBResultRow row;
    ScanningDB::GetPublishedTypesInGroup(groupID, res);
    while (res.GetRow(row))
        types.push_back(std::make_pair((uint16)row.GetUInt(0), std::string(row.GetText(1))));
    return types;
}

uint16 ExplorationMgr::PickType(const Exploration::LootRow& row, const std::string& prefix)
{
    if (row.itemTypeID > 0)
        return row.itemTypeID;
    if (row.itemGroupID == 0)
        return 0;

    std::string pattern(row.pattern);
    if (pattern == "*") {
        // the container's faction.  containers without one get none of these
        if (prefix.empty())
            return 0;
        pattern = prefix;
    }

    std::vector<uint16> matches;
    for (auto& cur : GetGroupTypes(row.itemGroupID)) {
        // copies are tech 1 only
        if (row.isCopy and (cur.second.find(" II ") != std::string::npos))
            continue;
        if (!pattern.empty()) {
            if (pattern[0] == '%') {
                if (cur.second.find(pattern.substr(1)) == std::string::npos)
                    continue;
            } else if (cur.second.compare(0, pattern.size(), pattern) != 0) {
                continue;
            }
        }
        matches.push_back(cur.first);
    }
    if (matches.empty())
        return 0;
    return matches[MakeRandomInt(0, matches.size()) % matches.size()];
}

bool ExplorationMgr::SpawnLoot(const Exploration::LootRow& row, InventoryItemRef contRef, uint32 ownerID, const std::string& prefix, uint8 tier)
{
    uint16 typeID(PickType(row, prefix));
    if (typeID == 0)
        return false;

    // MakeRandomInt() rarely reaches its upper bound
    uint32 qty(row.minQty);
    if (row.maxQty > row.minQty)
        qty = std::min<uint32>(MakeRandomInt(row.minQty, row.maxQty + 1), row.maxQty);
    if (qty < 1)
        qty = 1;

    if (row.isCopy) {
        BlueprintRef bRef(Blueprint::SpawnCopy(typeID, ownerID, contRef->itemID(), flagNone, qty));
        if (bRef.get() == nullptr)
            return false;
        _log(LOOT__INFO, "ExplorationMgr::FillContainer() - %s(%u) gets a %u run copy of %s", contRef->name(), contRef->itemID(), qty, bRef->name());
        return true;
    }

    qty *= tier;
    if (sConfig.rates.DropItem > 1)
        qty *= sConfig.rates.DropItem;
    ItemData iLoot(typeID, ownerID, contRef->itemID(), flagNone, qty);
    InventoryItemRef iRef(sItemFactory.SpawnItem(iLoot));
    if (iRef.get() == nullptr) {
        _log(LOOT__ERROR, "ExplorationMgr::FillContainer() - unable to spawn type %u for %s(%u)", typeID, contRef->name(), contRef->itemID());
        return false;
    }
    contRef->AddItem(iRef);
    _log(LOOT__INFO, "ExplorationMgr::FillContainer() - %s(%u) gets %u %s", contRef->name(), contRef->itemID(), qty, iRef->name());
    return true;
}

void ExplorationMgr::FillContainer(InventoryItemRef contRef)
{
    if (contRef.get() == nullptr)
        return;
    // loot from before a restart (containers relock, their items are kept)
    if (!contRef->GetMyInventory()->IsEmpty())
        return;

    auto fItr = m_family.find(contRef->typeID());
    if (fItr == m_family.end()) {
        _log(LOOT__ERROR, "ExplorationMgr::FillContainer() - %s(%u) type %u has no container family.  It opens empty.",
                contRef->name(), contRef->itemID(), contRef->typeID());
        return;
    }
    uint8 family(fItr->second.first);
    const std::string& prefix(fItr->second.second);

    // the harder the container, the more of each stack:  Basic/Fragment 30 -> 1 ... Secure/Heap -10 -> 4
    float difficulty(contRef->GetAttribute(AttrAccessDifficulty).get_float());
    uint8 tier(4);
    if (difficulty >= 30) {
        tier = 1;
    } else if (difficulty >= 10) {
        tier = 2;
    } else if (difficulty > -10) {
        tier = 3;
    }

    // number of stacks
    uint32 stacksMin(contRef->GetAttribute(AttrEntityEquipmentMin).get_uint32());
    uint32 stacksMax(contRef->GetAttribute(AttrEntityEquipmentMax).get_uint32());
    if (stacksMax == 0)
        stacksMax = 3;
    if (stacksMin > stacksMax)
        stacksMin = stacksMax;

    // the container's owner (an npc faction) keeps the loot, like an npc wreck
    uint32 ownerID(contRef->ownerID());
    uint32 stacks(0);
    std::vector<const Exploration::LootRow*> rows;
    auto range = m_loot.equal_range(family);
    for (auto itr = range.first; itr != range.second; ++itr) {
        rows.push_back(&itr->second);
        if (stacks >= stacksMax)
            continue;
        if (MakeRandomFloat() >= itr->second.chance)
            continue;
        if (SpawnLoot(itr->second, contRef, ownerID, prefix, tier))
            ++stacks;
    }

    // the minimum number of stacks comes from the likeliest rows
    if (stacks < stacksMin) {
        std::stable_sort(rows.begin(), rows.end(),
                [](const Exploration::LootRow* a, const Exploration::LootRow* b) { return a->chance > b->chance; });
        for (auto cur : rows) {
            if (stacks >= stacksMin)
                break;
            if (SpawnLoot(*cur, contRef, ownerID, prefix, tier))
                ++stacks;
        }
    }

    _log(LOOT__INFO, "ExplorationMgr::FillContainer() - %s(%u) family %u (%s) tier %u:  %u stacks (%u-%u).",
            contRef->name(), contRef->itemID(), family, prefix.c_str(), tier, stacks, stacksMin, stacksMax);
}

ContainerSE* ExplorationMgr::SpawnSiteContainer(SystemManager* pSys, uint16 typeID, uint32 ownerID, const GPoint& pos, const std::string& customInfo)
{
    if (pSys == nullptr)
        return nullptr;
    const ItemType* pType(sItemFactory.GetType(typeID));
    if ((pType == nullptr) or (pType->groupID() != EVEDB::invGroups::Spawn_Container))
        return nullptr;
    ItemData iData(typeID, ownerID, pSys->GetID(), flagNone, pType->name().c_str(), pos, customInfo.c_str());
    InventoryItemRef iRef(sItemFactory.SpawnItem(iData));
    if (iRef.get() == nullptr) {
        _log(LOOT__ERROR, "ExplorationMgr::SpawnSiteContainer() - unable to spawn type %u in %s", typeID, pSys->GetName());
        return nullptr;
    }
    DBSystemDynamicEntity entity = DBSystemDynamicEntity();
        entity.itemID = iRef->itemID();
        entity.itemName = iRef->itemName();
        entity.typeID = iRef->typeID();
        entity.groupID = iRef->groupID();
        entity.categoryID = iRef->categoryID();
        entity.ownerID = ownerID;
        entity.factionID = ownerID;
        entity.position = pos;
    if (!pSys->BuildDynamicEntity(entity, 0, false)) {
        iRef->Delete();
        return nullptr;
    }
    SystemEntity* pSE(pSys->GetSE(iRef->itemID()));
    if ((pSE == nullptr) or !pSE->IsContainerSE())
        return nullptr;
    return pSE->GetContSE();
}
