#pragma once

#include <cmath>
#include <cstdio>
#include <string>

// Tiny dependency-free test harness shared by all test translation units.
// There is exactly one runAllTests() entry point (tests/TestMain.cpp).
namespace klop_test
{

inline int gChecks = 0;
inline int gFailures = 0;
inline int gSkips = 0;

inline void check (bool condition, const std::string& what)
{
    ++gChecks;
    if (! condition)
    {
        ++gFailures;
        std::printf ("  FAIL  %s\n", what.c_str());
    }
}

inline void checkNear (double actual, double expected, double tolerance, const std::string& what)
{
    check (std::abs (actual - expected) <= tolerance, what);
}

inline void checkInRange (double value, double lo, double hi, const std::string& what)
{
    check (value >= lo && value <= hi, what);
}

inline void info (const std::string& message)
{
    std::printf ("  info  %s\n", message.c_str());
}

/**
    Records a check that cannot run yet (for example a behavioural assertion that
    needs the real groove engine before the stub is replaced). Skips are counted
    and printed so a run never looks more complete than it is.
*/
inline void skip (const std::string& what)
{
    ++gSkips;
    std::printf ("  SKIP  %s\n", what.c_str());
}

} // namespace klop_test
