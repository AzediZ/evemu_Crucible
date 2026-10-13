 /**
  * @name EVE_Loot.h
  *     what an npc's wreck holds:  the loot rolls
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  */


#ifndef EVE_LOOT_H
#define EVE_LOOT_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>


namespace Loot {

    /* the loot's random numbers.  mt19937's raw output is the same on every compiler;  the std distributions are not, so
     *  the mapping to [0, 1) and to integer ranges is done here.  a seed gives the same loot under msvc and gcc
     */
    class Rng {
    public:
        explicit Rng(uint32_t seed = 5489u) : m_gen(seed) { }
        void Seed(uint32_t seed)                        { m_gen.seed(seed); }

        // [0, 1)
        double Unit()                                   { return static_cast<double>(m_gen()) * (1.0 / 4294967296.0); }

        // [lo, hi], both ends included.  a reversed range gives lo
        int64_t Int(int64_t lo, int64_t hi)
        {
            if (hi <= lo)
                return lo;
            double span(static_cast<double>(hi - lo) + 1.0);
            int64_t value(lo + static_cast<int64_t>(std::floor(Unit() * span)));
            return std::min<int64_t>(value, hi);
        }

        // true with probability p
        bool Chance(double p)
        {
            if (!(p > 0.0))
                return false;
            if (p >= 1.0)
                return true;
            return (Unit() < p);
        }

    private:
        std::mt19937 m_gen;
    };

    /* one item a loot group can give (lootItemGroup):  when its group comes up the item is picked out of the group's items,
     *  then drops with its chance (itemDropChance).  the group's T1 and named (meta 1-4) items carry falling chances, so the
     *  chance is the meta level scaling.  faction, deadspace and officer items carry their own (low) chance
     */
    struct Item {
        uint32_t typeID;
        uint8_t metaLevel;
        double chance;
        uint32_t minQty;
        uint32_t maxQty;
    };

    /* a loot group in an npc's pool (lootGroup by npc group, lootNpcType by npc type).  weight is the group's share of the
     *  npc's rolls (groupDropChance).  items points to the group's items (not owned)
     */
    struct Group {
        uint32_t lootGroupID;
        double weight;
        const std::vector<Item>* items;
    };

    /* how much an npc drops, from its type's attributes:
     *  rolls      entityEquipmentMin (456) .. entityEquipmentMax (457), one loot roll each.  a roll at or under 0 means an empty
     *             wreck:  deadspace and mission rats have negative minimums (-6 .. 5 is a 7 in 12 chance of nothing)
     *  groupMax   entityEquipmentGroupMax (465):  the most drops one loot group may give ("entity can only drop 1 of group:
     *             energy laser").  0 = no cap
     */
    struct Params {
        int32_t rollsMin;
        int32_t rollsMax;
        uint32_t groupMax;
    };

    // a drop:  typeID and quantity (one stack per type)
    struct Drop {
        uint32_t typeID;
        uint32_t quantity;
    };

    // a guard against a broken attribute:  the most rolls one npc gets (the Crucible data tops out at 512, a hive queen)
    static constexpr int32_t MaxRolls = 1024;

    // the number of rolls, uniform in [min, max] (a reversed range is swapped).  0 or less is none
    inline int32_t RollCount(Rng& rng, int32_t rollsMin, int32_t rollsMax)
    {
        if (rollsMax < rollsMin)
            std::swap(rollsMin, rollsMax);
        int64_t count(rng.Int(rollsMin, rollsMax));
        if (count <= 0)
            return 0;
        return static_cast<int32_t>(std::min<int64_t>(count, MaxRolls));
    }

    // a stack's quantity, uniform in [min, max] (a reversed range is swapped).  never under 1
    inline uint32_t RollQuantity(Rng& rng, uint32_t minQty, uint32_t maxQty)
    {
        if (maxQty < minQty)
            std::swap(minQty, maxQty);
        int64_t qty(rng.Int(static_cast<int64_t>(minQty), static_cast<int64_t>(maxQty)));
        return static_cast<uint32_t>(std::max<int64_t>(qty, 1));
    }

    // a pick weighted by weights (those at or under 0 are never picked).  weights.size() if nothing can be picked
    inline size_t PickWeighted(Rng& rng, const std::vector<double>& weights)
    {
        double total(0.0);
        for (double w : weights)
            if (w > 0.0)
                total += w;
        if (!(total > 0.0))
            return weights.size();
        double point(rng.Unit() * total);
        size_t last(weights.size());
        for (size_t i = 0; i < weights.size(); ++i) {
            if (!(weights[i] > 0.0))
                continue;
            last = i;
            if (point < weights[i])
                return i;
            point -= weights[i];
        }
        return last;   // rounding at the top end
    }

    // adds quantity of typeID to drops (stacks of a type merge)
    inline void AddDrop(std::vector<Drop>& drops, uint32_t typeID, uint32_t quantity)
    {
        for (auto& cur : drops)
            if (cur.typeID == typeID) {
                uint64_t sum(static_cast<uint64_t>(cur.quantity) + quantity);
                cur.quantity = static_cast<uint32_t>(std::min<uint64_t>(sum, UINT32_MAX));
                return;
            }
        drops.push_back(Drop{typeID, quantity});
    }

    /* an npc's loot.  each roll picks a group of the pool by weight (groups without items can't come up), then one of its
     *  items, which drops with its chance.  a group that reached groupMax drops out of the picks.  a roll that misses gives
     *  nothing, so an npc's empty wreck chance comes from its rolls and its pool's chances
     */
    inline void Roll(Rng& rng, const Params& params, const std::vector<Group>& pool, std::vector<Drop>& drops)
    {
        int32_t count(RollCount(rng, params.rollsMin, params.rollsMax));
        if (count == 0)
            return;

        std::vector<double> weights;
        weights.reserve(pool.size());
        for (auto& cur : pool)
            weights.push_back(((cur.items != nullptr) and !cur.items->empty()) ? cur.weight : 0.0);
        std::vector<uint32_t> taken(pool.size(), 0);

        for (int32_t i = 0; i < count; ++i) {
            size_t idx(PickWeighted(rng, weights));
            if (idx >= pool.size())
                break;
            const std::vector<Item>& items(*pool[idx].items);
            const Item& item(items[static_cast<size_t>(rng.Int(0, static_cast<int64_t>(items.size()) - 1))]);
            if (!rng.Chance(item.chance))
                continue;
            AddDrop(drops, item.typeID, RollQuantity(rng, item.minQty, item.maxQty));
            if ((params.groupMax > 0) and (++taken[idx] >= params.groupMax))
                weights[idx] = 0.0;
        }
    }

}

#endif  // EVE_LOOT_H
