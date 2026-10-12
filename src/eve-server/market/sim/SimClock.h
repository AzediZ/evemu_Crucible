
 /**
  * @name SimClock.h
  *   market simulation:  where the sim gets its time.  the server uses the wall clock (MarketSimMgr), the harness and
  *   '/mktsim step' a manual one
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_CLOCK_H__
#define _EVE_SERVER_MARKET_SIM_CLOCK_H__

#include <cstdint>

namespace MktSim {

    class ISimClock {
    public:
        virtual ~ISimClock()                            { }
        // unix seconds, UTC
        virtual int64_t Now() const = 0;
    };

    class ManualClock : public ISimClock {
    public:
        explicit ManualClock(int64_t start = 0) : m_now(start) { }
        int64_t Now() const override                    { return m_now; }
        void Set(int64_t t)                             { m_now = t; }
        void Advance(int64_t seconds)                   { m_now += seconds; }
    private:
        int64_t m_now;
    };

    // a clock that runs offset from another (/mktsim step on a test server moves the sim ahead of the wall clock)
    class OffsetClock : public ISimClock {
    public:
        explicit OffsetClock(const ISimClock& base) : m_base(base), m_offset(0) { }
        int64_t Now() const override                    { return m_base.Now() + m_offset; }
        void Advance(int64_t seconds)                   { m_offset += seconds; }
        int64_t Offset() const                          { return m_offset; }
        void SetOffset(int64_t offset)                  { m_offset = offset; }
    private:
        const ISimClock& m_base;
        int64_t m_offset;
    };

}

#endif  // _EVE_SERVER_MARKET_SIM_CLOCK_H__
