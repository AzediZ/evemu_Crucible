 /**
  * @name ThreatTable.h
  *   an npc's threat (damage and ewar done to it) per entity, in a fixed flat array (PLAN.md 6.7):
  *   no heap allocation.  when it's full, a new entry replaces the lowest one if it's larger.
  *   header only and dependency free (also used by eve-test)
  */

#ifndef _EVE_NPC_AI_THREATTABLE_H_
#define _EVE_NPC_AI_THREATTABLE_H_

#include <cstddef>
#include <cstdint>

class ThreatTable {
public:
    static const size_t Capacity = 16;

    struct Entry {
        uint32_t id;
        float threat;
    };

    ThreatTable()                                       { Clear(); }

    void Clear()                                        { m_count = 0; }
    bool Empty() const                                  { return (m_count == 0); }
    size_t Size() const                                 { return m_count; }
    const Entry& At(size_t i) const                     { return m_entries[i]; }

    float Get(uint32_t id) const {
        for (size_t i = 0; i < m_count; ++i)
            if (m_entries[i].id == id)
                return m_entries[i].threat;
        return 0.0f;
    }

    // adds amount (> 0) to id's threat
    void Add(uint32_t id, float amount) {
        if ((id == 0) or !(amount > 0.0f))
            return;
        for (size_t i = 0; i < m_count; ++i)
            if (m_entries[i].id == id) {
                m_entries[i].threat += amount;
                return;
            }
        if (m_count < Capacity) {
            m_entries[m_count].id = id;
            m_entries[m_count].threat = amount;
            ++m_count;
            return;
        }
        // full:  the lowest entry makes room if the newcomer is worth more
        size_t lowest(0);
        for (size_t i = 1; i < m_count; ++i)
            if (m_entries[i].threat < m_entries[lowest].threat)
                lowest = i;
        if (amount > m_entries[lowest].threat) {
            m_entries[lowest].id = id;
            m_entries[lowest].threat = amount;
        }
    }

    bool Full() const                                   { return (m_count >= Capacity); }

    // removes every entry whose id gone(id) says has left (killed, recalled, docked, jumped, out of reach)
    template <class F>
    void RemoveIf(const F& gone) {
        size_t i(0);
        while (i < m_count) {
            if (gone(m_entries[i].id)) {
                m_entries[i] = m_entries[m_count - 1];
                --m_count;
            } else {
                ++i;
            }
        }
    }

    void Remove(uint32_t id) {
        for (size_t i = 0; i < m_count; ++i)
            if (m_entries[i].id == id) {
                m_entries[i] = m_entries[m_count - 1];
                --m_count;
                return;
            }
    }

    // every entry x factor;  entries that fall below dropBelow are removed
    void Decay(float factor, float dropBelow) {
        size_t i(0);
        while (i < m_count) {
            m_entries[i].threat *= factor;
            if (m_entries[i].threat < dropBelow) {
                m_entries[i] = m_entries[m_count - 1];
                --m_count;
            } else {
                ++i;
            }
        }
    }

    // the entry with the most threat (nullptr when empty)
    const Entry* Top() const {
        if (m_count == 0)
            return nullptr;
        size_t best(0);
        for (size_t i = 1; i < m_count; ++i)
            if (m_entries[i].threat > m_entries[best].threat)
                best = i;
        return &m_entries[best];
    }

private:
    Entry m_entries[Capacity];
    size_t m_count;
};

#endif  // _EVE_NPC_AI_THREATTABLE_H_
