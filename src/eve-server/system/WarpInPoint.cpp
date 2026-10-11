 /**
  * @name WarpInPoint.cpp
  *   where a warp to an object ends.  see WarpInPoint.h
  */

#include "eve-server.h"

#include "station/StationDataMgr.h"
#include "system/SystemEntity.h"
#include "system/WarpInPoint.h"

namespace {
    /* the reference MT19937 (mt19937ar.c), which python's _random module uses */
    class PyMersenneTwister {
    public:
        explicit PyMersenneTwister(uint32 seed) : m_index(N) {
            // python seeds an int with abs(n) split into 32-bit words.  an itemID is a single word
            uint32 key[1] = { seed };
            InitByArray(key, 1);
        }

        // random.random(): 53 bits from two draws
        double Random() {
            uint32 a(GenRand() >> 5), b(GenRand() >> 6);
            return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
        }

    private:
        static const int N = 624;
        static const int M = 397;
        uint32 m_mt[N];
        int m_index;

        void InitGenRand(uint32 s) {
            m_mt[0] = s;
            for (m_index = 1; m_index < N; ++m_index)
                m_mt[m_index] = (1812433253U * (m_mt[m_index - 1] ^ (m_mt[m_index - 1] >> 30)) + m_index);
        }

        void InitByArray(const uint32* key, int keyLength) {
            InitGenRand(19650218U);
            int i(1), j(0);
            for (int k = (N > keyLength ? N : keyLength); k > 0; --k) {
                m_mt[i] = (m_mt[i] ^ ((m_mt[i - 1] ^ (m_mt[i - 1] >> 30)) * 1664525U)) + key[j] + j;
                ++i;
                ++j;
                if (i >= N) {
                    m_mt[0] = m_mt[N - 1];
                    i = 1;
                }
                if (j >= keyLength)
                    j = 0;
            }
            for (int k = N - 1; k > 0; --k) {
                m_mt[i] = (m_mt[i] ^ ((m_mt[i - 1] ^ (m_mt[i - 1] >> 30)) * 1566083941U)) - i;
                ++i;
                if (i >= N) {
                    m_mt[0] = m_mt[N - 1];
                    i = 1;
                }
            }
            m_mt[0] = 0x80000000U;
            m_index = N;
        }

        uint32 GenRand() {
            static const uint32 mag01[2] = { 0x0U, 0x9908b0dfU };
            uint32 y(0);
            if (m_index >= N) {
                int kk(0);
                for (; kk < N - M; ++kk) {
                    y = (m_mt[kk] & 0x80000000U) | (m_mt[kk + 1] & 0x7fffffffU);
                    m_mt[kk] = m_mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U];
                }
                for (; kk < N - 1; ++kk) {
                    y = (m_mt[kk] & 0x80000000U) | (m_mt[kk + 1] & 0x7fffffffU);
                    m_mt[kk] = m_mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U];
                }
                y = (m_mt[N - 1] & 0x80000000U) | (m_mt[0] & 0x7fffffffU);
                m_mt[N - 1] = m_mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
                m_index = 0;
            }
            y = m_mt[m_index++];
            y ^= (y >> 11);
            y ^= (y << 7) & 0x9d2c5680U;
            y ^= (y << 15) & 0xefc60000U;
            y ^= (y >> 18);
            return y;
        }
    };
}

double WarpIn::PyRandom(uint32 seed)
{
    // python: random.Random(40000001).random() == 0.8223523261012717
    PyMersenneTwister mt(seed);
    return mt.Random();
}

GPoint WarpIn::Planet(uint32 planetID, const GPoint& planetPos, double radius)
{
    /* util.GetPlanetWarpInPoint(planetID, locVec, r), which returns the offset from the planet:
     *    f = dz / sqrt(dx^2 + dz^2), negated when dx > 0 (and dz != 0);  theta = asin(f)
     *    rr = (random.Random(planetID).random() - 1.0) / 3.0;  theta += rr
     *    dd = ((20 - 5 * log10(r / 1000000) - 0.5) / 20) ^ 20 * 20, clamped to 0..10, + 0.5
     *    d = r + 1000000 + r * dd
     *    (sin(theta) * d, r * sin(rr) * 0.5, cos(theta) * d)
     */
    double dx(planetPos.x), dz(planetPos.z);
    double len(std::sqrt((dx * dx) + (dz * dz)));
    double f(len > 0 ? dz / len : 0.0);
    if ((dx > 0) and (dz != 0))
        f = -f;
    double theta(std::asin(f));
    double rr((PyRandom(planetID) - 1.0) / 3.0);
    theta += rr;
    double dd(std::pow((20.0 - 5.0 * std::log10(radius / 1000000.0) - 0.5) / 20.0, 20.0) * 20.0);
    dd = EvE::min(10.0, EvE::max(0.0, dd));
    dd += 0.5;
    double d(radius + 1000000.0 + (radius * dd));
    return GPoint(planetPos.x + (std::sin(theta) * d), planetPos.y + (radius * std::sin(rr) * 0.5), planetPos.z + (std::cos(theta) * d));
}

GPoint WarpIn::Large(const GPoint& pos, double radius)
{
    return GPoint(pos.x + ((radius + 5000000.0) * std::cos(radius)), pos.y + (1.3 * radius) - 7500.0, pos.z - ((radius + 5000000.0) * std::sin(radius)));
}

GPoint WarpIn::Ordinary(const GPoint& from, const GPoint& pos, double radius)
{
    GVector direction(from, pos);
    if (direction.isZero())
        return pos;
    direction.normalize();
    GPoint result(pos);
    result -= (direction * radius);
    return result;
}

GPoint WarpIn::ForEntity(SystemEntity* pSE, const GPoint& from)
{
    GPoint pos(pSE->GetPosition());
    double radius(pSE->GetRadius());
    if (pSE->IsPlanetSE())
        return Planet(pSE->GetID(), pos, radius);
    if (pSE->IsStationSE()) {
        // the station's dock elevation (y), instead of its "center point" position (where the icon is)
        pos.y = stDataMgr.GetDockPosY(pSE->GetID());
        return Ordinary(from, pos, radius);
    }
    InventoryItemRef iRef(pSE->GetSelf());
    if ((iRef.get() != nullptr) and (iRef->categoryID() == EVEDB::invCategories::Celestial) and (radius >= 90000)
    and (iRef->groupID() != EVEDB::invGroups::Sun) and (iRef->groupID() != EVEDB::invGroups::Wreck))
        return Large(pos, radius);
    return Ordinary(from, pos, radius);
}

bool WarpIn::IsNewbieSystem(uint32 systemID)
{
    static const uint32 newbieSystems[] = { 30002547, 30001392, 30002715, 30003489, 30005305, 30004971,
                                            30001672, 30002505, 30000141, 30003410, 30005042, 30001407 };
    for (auto cur : newbieSystems)
        if (cur == systemID)
            return true;
    return false;
}
