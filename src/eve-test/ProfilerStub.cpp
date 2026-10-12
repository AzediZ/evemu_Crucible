/*
    eve-core's DBcore reports query times to the server's Profiler (eve-server/Profiler.cpp), which eve-test doesn't link.
    the tests never profile, so this stands in for it.
*/

#include "eve-test.h"

class Profiler
{
public:
    void AddTime(uint8 key, double value);
};

void Profiler::AddTime(uint8 key, double value)
{
}
