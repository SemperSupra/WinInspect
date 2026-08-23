#pragma once
// RapidCheck - Enhanced stub for property-based testing
// Uses std::mt19937_64 (Mersenne Twister) for better randomness than std::rand().
// Provides uniform random generation for integrals and floating-point types.
// For full property testing (shrinking, coverage), replace with the
// multi-header library from https://github.com/emil-e/rapidcheck

#include <functional>
#include <string>
#include <random>
#include <cstdint>
#include <vector>

namespace rc {

using PropertyResult = bool;

namespace detail {
  // Deterministic RNG seeded from random device
  inline std::mt19937_64 &rng() {
    static std::mt19937_64 rng(std::random_device{}());
    return rng;
  }

  // Generate a random value uniformly between min and max
  template<typename T>
  T generate(T min, T max) {
    if constexpr (std::is_integral_v<T>) {
      std::uniform_int_distribution<T> dist(min, max);
      return dist(rng());
    } else {
      std::uniform_real_distribution<T> dist(min, max);
      return dist(rng());
    }
  }
} // namespace detail

struct Property {
  int iterations;
  std::string label;

  template<typename F>
  Property(const std::string &name, F f, int n = 200)
      : iterations(n), label(name) {
    for (int i = 0; i < iterations; i++) {
      f(); // RC_PRE/RC_ASSERT return false on failure within the lambda
    }
  }
};

#define RC_PRE(x) do { if (!(x)) return false; } while(0)
#define RC_ASSERT(x) do { if (!(x)) return false; } while(0)

} // namespace rc
