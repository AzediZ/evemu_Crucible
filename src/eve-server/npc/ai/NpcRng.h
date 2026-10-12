 /**
  * @name NpcRng.h
  *   per-npc random numbers (PLAN.md 6.8):  xorshift128+ seeded with splitmix64 from (npc.RngSeed, itemID).
  *   the same seed and the same inputs give the same decisions, so ai behaviour can be replayed and tested.
  *   header only and dependency free (also used by eve-test)
  */

#ifndef _EVE_NPC_AI_NPCRNG_H_
#define _EVE_NPC_AI_NPCRNG_H_

#include <cstdint>

class NpcRng {
public:
    NpcRng()                                            { Seed(0, 0); }
    NpcRng(uint64_t seed, uint32_t itemID)              { Seed(seed, itemID); }

    // seed 0 is a valid seed here.  the caller replaces npc.RngSeed 0 with a time based value
    void Seed(uint64_t seed, uint32_t itemID) {
        m_seed = seed;
        uint64_t x(seed ^ (static_cast<uint64_t>(itemID) * 0x9E3779B97F4A7C15ULL));
        m_s0 = SplitMix(x);
        m_s1 = SplitMix(x);
        if ((m_s0 == 0) and (m_s1 == 0))
            m_s1 = 1;
    }
    uint64_t GetSeed() const                            { return m_seed; }

    uint64_t Next() {
        uint64_t s1(m_s0);
        const uint64_t s0(m_s1);
        const uint64_t result(s0 + s1);
        m_s0 = s0;
        s1 ^= s1 << 23;
        m_s1 = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
        return result;
    }
    // [0, 1)
    double Uniform()                                    { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
    // [lo, hi)
    double Range(double lo, double hi)                  { return lo + ((hi - lo) * Uniform()); }
    // true with probability p
    bool Chance(double p)                               { return (Uniform() < p); }

private:
    static uint64_t SplitMix(uint64_t& x) {
        uint64_t z(x += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    uint64_t m_seed;
    uint64_t m_s0;
    uint64_t m_s1;
};

#endif  // _EVE_NPC_AI_NPCRNG_H_
