
 /**
  * @name eve-market-sim.cpp
  *   offline harness and tests of the market code that has no server dependencies
  *
  *   eve-market-sim --test <name>       runs one test (CTest runs each of them)
  *
  * @date:          13Oct26
  *
  */

#include <cstdio>
#include <cstring>
#include <string>

#include "TestCheck.h"

int MktTest::failures = 0;

int MatchTest();

namespace {
    struct TestEntry {
        const char* name;
        int (*fn)();
    };

    const TestEntry s_tests[] = {
        { "match",  &MatchTest },
    };

    void Usage()
    {
        std::printf("usage: eve-market-sim --test <name>\n  tests:");
        for (auto& cur : s_tests)
            std::printf(" %s", cur.name);
        std::printf("\n");
    }
}

int main(int argc, char** argv)
{
    if ((argc == 3) and (std::strcmp(argv[1], "--test") == 0)) {
        for (auto& cur : s_tests) {
            if (std::strcmp(cur.name, argv[2]) != 0)
                continue;
            MktTest::failures = 0;
            int failed = cur.fn();
            std::printf("%s: %s (%d failure%s)\n", cur.name, failed ? "FAILED" : "passed", failed, failed == 1 ? "" : "s");
            return failed ? 1 : 0;
        }
        std::printf("unknown test '%s'\n", argv[2]);
    }
    Usage();
    return 2;
}
