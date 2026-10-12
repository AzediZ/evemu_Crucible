 /**
  * @name RecentDamage.h
  *   who hurt an npc lately (PLAN.md 4.2):  damage per attacker over a 30s window, in a fixed flat array (no heap).
  *   an entry's damage counts while its last hit is less than Window ms old;  a hit after a longer gap starts it over.
  *   the standard ai's drone rule uses it ("drones that damaged this npc in the last 30s, most damage first", and not
  *   while the primary's pilot hurt it too).  header only and dependency free (also used by eve-test)
  */

#ifndef _EVE_NPC_AI_RECENTDAMAGE_H_
#define _EVE_NPC_AI_RECENTDAMAGE_H_

#include <cstddef>
#include <cstdint>

class RecentDamage {
public:
    static const size_t Capacity = 16;
    static const int64_t Window = 30000;    // ms

    RecentDamage()                                      { Clear(); }

    void Clear()                                        { m_count = 0; }
    bool Empty() const                                  { return (m_count == 0); }

    void Add(uint32_t id, float amount, int64_t now) {
        if ((id == 0) or !(amount > 0.0f))
            return;
        for (size_t i = 0; i < m_count; ++i)
            if (m_entries[i].id == id) {
                if ((now - m_entries[i].lastAt) > Window)
                    m_entries[i].amount = 0.0f;
                m_entries[i].amount += amount;
                m_entries[i].lastAt = now;
                return;
            }
        size_t slot(m_count);
        if (m_count < Capacity) {
            ++m_count;
        } else {
            // full:  the oldest entry makes room
            slot = 0;
            for (size_t i = 1; i < m_count; ++i)
                if (m_entries[i].lastAt < m_entries[slot].lastAt)
                    slot = i;
        }
        m_entries[slot].id = id;
        m_entries[slot].amount = amount;
        m_entries[slot].lastAt = now;
    }

    // id's damage in the window (0 if none)
    float Get(uint32_t id, int64_t now) const {
        for (size_t i = 0; i < m_count; ++i)
            if (m_entries[i].id == id)
                return (((now - m_entries[i].lastAt) > Window) ? 0.0f : m_entries[i].amount);
        return 0.0f;
    }

    // the ids with damage in the window, most damage first.  returns how many were written (at most max)
    size_t Sorted(int64_t now, uint32_t* ids, size_t max) const {
        size_t n(0);
        size_t order[Capacity];
        for (size_t i = 0; i < m_count; ++i)
            if ((now - m_entries[i].lastAt) <= Window)
                order[n++] = i;
        // insertion sort (16 entries at most)
        for (size_t i = 1; i < n; ++i) {
            size_t cur(order[i]), j(i);
            while ((j > 0) and (m_entries[order[j - 1]].amount < m_entries[cur].amount)) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = cur;
        }
        if (n > max)
            n = max;
        for (size_t i = 0; i < n; ++i)
            ids[i] = m_entries[order[i]].id;
        return n;
    }

private:
    struct Entry {
        uint32_t id;
        float amount;
        int64_t lastAt;
    };
    Entry m_entries[Capacity];
    size_t m_count;
};

#endif  // _EVE_NPC_AI_RECENTDAMAGE_H_
