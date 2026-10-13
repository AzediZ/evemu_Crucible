 /**
  * @name EVE_DogmaOrder.h
  *     the order an item's dogma modifiers are applied in
  *
  * this is a pure function, so the unit tests (eve-test) can check it
  *
  */


#ifndef EVE_DOGMA_ORDER_H
#define EVE_DOGMA_ORDER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>


namespace DogmaOrder {

    /* one modifier of a batch (FxProc::ApplyEffects).  the server applies a batch as one pass over the modifiers, in math order
     *  (PreAssignment, PreMul, PreDiv, ModAdd, ModSub, PostMul, PostDiv, PostPercent, PostAssignment), reading each source
     *  attribute when its modifier is applied.  Crucible's dogma (baseDogmaLocation.GetAttributeValue) works an attribute out
     *  when it is read instead:  a modifier's source attribute is always the fully modified value.  the two differ only when a
     *  modifier reads an attribute that another modifier of the same batch changes on the same item, e.g. the prop mods'
     *  speedBoostFactorCalculator2 (calc2 ModAdd calc) and speedBoostFactorCalculator (calc PostMul speedFactor, thrust), or a
     *  covert transport's cloak cpu (ship's cloakingCpuNeedBonus PostPercent, read by the cloak's cpu PostMul)
     */
    struct Modifier {
        uint32_t srcItem;   // the item the modifier comes from (its source attribute is read there)
        uint16_t srcAttr;   // the attribute read
        uint16_t targAttr;  // the attribute changed
        bool writesSrc;     // the modifier changes targAttr on its own source item
        bool remove;        // undoing the modifier (RemoveModifier, the math inverted)
    };

    /* the order to apply the batch in, as indices into mods (which are in the server's math order).
     *  a modifier that changes an attribute of its own item comes before the modifiers of that item reading the attribute, so they
     *  read its final value as Crucible does.  removing works backwards:  the readers come off first, while the attribute still
     *  has the value they read.  everything else keeps its place.  a dependency loop is broken at its first modifier
     */
    inline std::vector<std::size_t> Order(const std::vector<Modifier>& mods)
    {
        const std::size_t n(mods.size());
        std::vector<std::size_t> order;
        order.reserve(n);
        // writers of each (item, attribute)
        std::unordered_map<uint64_t, std::vector<std::size_t>> writers;
        for (std::size_t i = 0; i < n; ++i)
            if (mods[i].writesSrc and (mods[i].targAttr != 0))
                writers[(static_cast<uint64_t>(mods[i].srcItem) << 16) | mods[i].targAttr].push_back(i);
        std::vector<std::vector<std::size_t>> after(n);     // after[i]:  the modifiers that must wait for i
        std::vector<std::size_t> waits(n, 0);
        bool any(false);
        if (!writers.empty()) {
            for (std::size_t r = 0; r < n; ++r) {
                if (mods[r].srcAttr == 0)
                    continue;
                auto itr = writers.find((static_cast<uint64_t>(mods[r].srcItem) << 16) | mods[r].srcAttr);
                if (itr == writers.end())
                    continue;
                for (std::size_t w : itr->second) {
                    if ((w == r) or (mods[w].remove != mods[r].remove))
                        continue;
                    std::size_t first(mods[r].remove ? r : w), second(mods[r].remove ? w : r);
                    after[first].push_back(second);
                    ++waits[second];
                    any = true;
                }
            }
        }
        if (!any) {
            for (std::size_t i = 0; i < n; ++i)
                order.push_back(i);
            return order;
        }
        // stable topological sort:  of the modifiers that can go, the one first in math order goes
        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<std::size_t>> ready;
        std::vector<bool> done(n, false);
        for (std::size_t i = 0; i < n; ++i)
            if (waits[i] == 0)
                ready.push(i);
        std::size_t next(0);    // lowest index that may still be waiting (for loops)
        while (order.size() < n) {
            if (ready.empty()) {
                while (done[next])
                    ++next;
                waits[next] = 0;
                ready.push(next);
            }
            std::size_t i(ready.top());
            ready.pop();
            if (done[i])
                continue;
            done[i] = true;
            order.push_back(i);
            for (std::size_t j : after[i])
                if (!done[j] and (waits[j] > 0) and (--waits[j] == 0))
                    ready.push(j);
        }
        return order;
    }

}

#endif  // EVE_DOGMA_ORDER_H
