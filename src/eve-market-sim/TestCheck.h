
 /**
  * @name TestCheck.h
  *   the few checks the market sim tests need (no test framework is linked)
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_MARKET_SIM_TEST_CHECK_H__
#define _EVE_MARKET_SIM_TEST_CHECK_H__

#include <cstdio>

namespace MktTest {
    // failures of the running test
    extern int failures;
}

#define T_CHECK(cond) \
    do { if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++MktTest::failures; } } while (0)

#define T_CHECK_EQ(a, b) \
    do { long long _a = (long long)(a), _b = (long long)(b); \
         if (_a != _b) { std::printf("  FAIL %s:%d  %s == %s  (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); ++MktTest::failures; } } while (0)

#endif  // _EVE_MARKET_SIM_TEST_CHECK_H__
