
 /**
  * @name SimRng.h
  *   market simulation:  counter based random numbers.  every draw is a hash of (seed, what, when, purpose), so the
  *   same seed and inputs give the same results whatever order or time slicing the work is done in
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_RNG_H__
#define _EVE_SERVER_MARKET_SIM_RNG_H__

#include <cmath>
#include <cstdint>

namespace MktSim {
namespace Rng {

    inline uint64_t SplitMix64(uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    inline uint64_t Hash64(uint64_t seed, uint64_t a, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0)
    {
        uint64_t h(SplitMix64(seed));
        h = SplitMix64(h ^ a);
        h = SplitMix64(h ^ b);
        h = SplitMix64(h ^ c);
        return SplitMix64(h ^ d);
    }

    // [0, 1) with 53 bits
    inline double Uniform(uint64_t h)                   { return (double)(h >> 11) * (1.0 / 9007199254740992.0); }
    // (0, 1)
    inline double UniformOpen(uint64_t h)               { return ((double)(h >> 11) + 0.5) * (1.0 / 9007199254740992.0); }

    // N(0,1), Box-Muller from two hashes of h
    inline double Normal(uint64_t h)
    {
        double u1(UniformOpen(SplitMix64(h ^ 0x6A09E667F3BCC908ull)));
        double u2(Uniform(SplitMix64(h ^ 0xBB67AE8584CAA73Bull)));
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

    // Poisson(lambda).  inversion below 30, a rounded normal above (the sim only needs the right mean and spread)
    inline uint64_t Poisson(double lambda, uint64_t h)
    {
        if (!(lambda > 0.0))
            return 0;
        if (lambda < 30.0) {
            double u(UniformOpen(h)), p(std::exp(-lambda)), cdf(p);
            uint64_t k(0);
            while ((u > cdf) and (k < 200)) {
                ++k;
                p *= lambda / (double)k;
                cdf += p;
            }
            return k;
        }
        double x(lambda + std::sqrt(lambda) * Normal(h) + 0.5);
        return (x < 0.0) ? 0 : (uint64_t)x;
    }

    // x rounded to an integer, up with probability frac(x)
    inline int64_t RoundRandom(double x, uint64_t h)
    {
        double f(std::floor(x));
        return (int64_t)f + ((Uniform(h) < (x - f)) ? 1 : 0);
    }

}
}

#endif  // _EVE_SERVER_MARKET_SIM_RNG_H__
