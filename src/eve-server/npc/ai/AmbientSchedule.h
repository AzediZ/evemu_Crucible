 /**
  * @name AmbientSchedule.h
  *   the ambient npcs' event queue (doc/code_and_design_notes/ambient-npc-plan.md 4.1):  a min-heap of timed events, earliest
  *   first, ties in the order they were pushed.  header only and dependency free (also used by eve-test)
  */

#ifndef _EVE_NPC_AI_AMBIENTSCHEDULE_H_
#define _EVE_NPC_AI_AMBIENTSCHEDULE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

class AmbientSchedule {
public:
    struct Event {
        int64_t dueMs;
        uint8_t kind;           // the owner's event kind
        uint64_t gridKey;       // what the event is about (Cancel() takes them out by it)
        uint32_t payload;       // the owner's index or id
        uint64_t seq;           // push order (ties)
    };

    AmbientSchedule() : m_seq(0) { }

    void Push(int64_t dueMs, uint8_t kind, uint64_t gridKey = 0, uint32_t payload = 0) {
        m_heap.push_back(Event{dueMs, kind, gridKey, payload, ++m_seq});
        std::push_heap(m_heap.begin(), m_heap.end(), Later());
    }
    // the earliest event due at or before now (false:  none)
    bool PopDue(int64_t now, Event& out) {
        if (m_heap.empty() or (m_heap.front().dueMs > now))
            return false;
        std::pop_heap(m_heap.begin(), m_heap.end(), Later());
        out = m_heap.back();
        m_heap.pop_back();
        return true;
    }
    // drops every event about gridKey.  the number dropped
    size_t Cancel(uint64_t gridKey) {
        size_t before(m_heap.size());
        m_heap.erase(std::remove_if(m_heap.begin(), m_heap.end(), [gridKey](const Event& e) { return (e.gridKey == gridKey); }),
                     m_heap.end());
        if (m_heap.size() != before)
            std::make_heap(m_heap.begin(), m_heap.end(), Later());
        return before - m_heap.size();
    }
    // drops every event of this kind
    size_t CancelKind(uint8_t kind) {
        size_t before(m_heap.size());
        m_heap.erase(std::remove_if(m_heap.begin(), m_heap.end(), [kind](const Event& e) { return (e.kind == kind); }),
                     m_heap.end());
        if (m_heap.size() != before)
            std::make_heap(m_heap.begin(), m_heap.end(), Later());
        return before - m_heap.size();
    }
    // ms of the next event (INT64_MAX:  none)
    int64_t NextDue() const {
        return (m_heap.empty() ? std::numeric_limits<int64_t>::max() : m_heap.front().dueMs);
    }
    bool HasKind(uint8_t kind) const {
        for (auto& cur : m_heap)
            if (cur.kind == kind)
                return true;
        return false;
    }
    size_t Size() const                                 { return m_heap.size(); }
    bool Empty() const                                  { return m_heap.empty(); }
    void Clear()                                        { m_heap.clear(); }

private:
    // heap order:  the earliest (then the first pushed) on top
    struct Later {
        bool operator()(const Event& a, const Event& b) const {
            if (a.dueMs != b.dueMs)
                return (a.dueMs > b.dueMs);
            return (a.seq > b.seq);
        }
    };

    std::vector<Event> m_heap;
    uint64_t m_seq;
};

#endif  // _EVE_NPC_AI_AMBIENTSCHEDULE_H_
