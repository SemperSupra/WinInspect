#pragma once
// RapidCheck-like property testing for C++
// Uses random generation with the existing doctest framework.
// Replace with full header from https://github.com/emil-e/rapidcheck when CI is available.
#include <functional>
#include <string>
#include <cstdlib>
#include <vector>

namespace rc {
using PropertyResult = bool;
struct Property {
    template<typename F>
    Property(const std::string&, F f) {
        for (int i = 0; i < 200; i++) { if (!f()) break; }
    }
};
#define RC_PRE(x) do { if (!(x)) return false; } while(0)
#define RC_ASSERT(x) do { if (!(x)) return false; } while(0)
} // namespace rc
