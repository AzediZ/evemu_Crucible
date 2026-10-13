 /**
  * @name SeenList.h
  *   the ships a level-one npc has seen lately (PLAN.md 4.7:  incursion sansha "most likely will change targets to any
  *   newcomers";  4.6:  sleepers call for reinforcements when a capital ship comes into sight).
  *   a fixed flat array (no heap allocation, PLAN.md 6.7):  when it's full the oldest entry makes room.
  *   header only and dependency free (also used by eve-test)
  */

#ifndef _EVE_NPC_AI_SEENLIST_H_
#define _EVE_NPC_AI_SEENLIST_H_

#include <cstddef>
#include <cstdint>

class SeenList {
public:
    static constexpr size_t Capacity = 32;

    SeenList()                                          { Clear(); }

    void Clear()                                        { m_count = 0; }
    size_t Size() const                                 { return m_count; }

    /* id is in sight at now (ms).  true when it's a newcomer:  not seen within the last memoryMs */
    bool Touch(uint32_t id, int64_t now, int64_t memoryMs) {
        if (id == 0)
            return false;
        for (size_t i = 0; i < m_count; ++i)
            if (m_entries[i].id == id) {
                bool fresh((now - m_entries[i].seen) > memoryMs);
                m_entries[i].seen = now;
                return fresh;
            }
        size_t slot(m_count);
        if (m_count < Capacity) {
            ++m_count;
        } else {
            // full:  the one seen longest ago goes
            slot = 0;
            for (size_t i = 1; i < m_count; ++i)
                if (m_entries[i].seen < m_entries[slot].seen)
                    slot = i;
        }
        m_entries[slot].id = id;
        m_entries[slot].seen = now;
        return true;
    }

private:
    struct Entry {
        uint32_t id;
        int64_t seen;
    };
    Entry m_entries[Capacity];
    size_t m_count;
};

#endif  // _EVE_NPC_AI_SEENLIST_H_
