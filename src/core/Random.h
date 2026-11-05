#pragma once

#include <cmath>
#include <cstdint>

// -----------------------------------------------------------------------------
// Deterministic, audio-thread-safe randomness.
//
// Every stochastic decision in KLOPGEEST is a pure function of
// (seed, voice, step index, salt). That gives us three properties we care about
// more than "true" randomness:
//   * reproducible performances for a given seed,
//   * block-size invariance (no state to drift when the host changes buffers),
//   * trivially testable musical behaviour.
// -----------------------------------------------------------------------------
namespace klopgeest::rng
{

inline std::uint32_t hash32 (std::uint32_t x) noexcept
{
    // SplitMix32 finaliser.
    x += 0x9e3779b9u;
    x = (x ^ (x >> 16u)) * 0x21f0aaadu;
    x = (x ^ (x >> 15u)) * 0x735a2d97u;
    return x ^ (x >> 15u);
}

inline std::uint32_t hash (std::uint32_t seed, int a, int b, int salt = 0) noexcept
{
    std::uint32_t h = seed;
    h = hash32 (h ^ (std::uint32_t) (a * 0x9e3779b1u));
    h = hash32 (h ^ (std::uint32_t) (b * 0x85ebca6bu));
    h = hash32 (h ^ (std::uint32_t) (salt * 0xc2b2ae35u));
    return h;
}

/** Uniform in [0, 1). */
inline float unit (std::uint32_t seed, int a, int b, int salt = 0) noexcept
{
    return (float) (hash (seed, a, b, salt) & 0x00ffffffu) / 16777216.0f;
}

/** Uniform in [-1, 1). */
inline float bipolar (std::uint32_t seed, int a, int b, int salt = 0) noexcept
{
    return unit (seed, a, b, salt) * 2.0f - 1.0f;
}

inline float smoothstep (float t) noexcept
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

/**
    Correlated value noise sampled at tick `tick`, with a correlation length of
    `period` ticks. Two neighbouring ticks therefore move together instead of
    jittering independently — the difference between a human and a broken clock.
    Pure function of its inputs: no hidden state.
*/
inline float smoothBipolar (std::uint32_t seed, int channel, int tick,
                            float period, int salt = 0) noexcept
{
    const auto p    = period < 1.0f ? 1.0f : period;
    const auto pos  = (float) tick / p;
    const auto base = (int) std::floor (pos);
    const auto frac = pos - (float) base;
    const auto a    = bipolar (seed, channel, base, salt);
    const auto b    = bipolar (seed, channel, base + 1, salt);
    return a + (b - a) * smoothstep (frac);
}

/** A very small deterministic PRNG for message-thread work (mutation, presets). */
class SeededRandom
{
public:
    explicit SeededRandom (std::uint32_t seed = 1u) noexcept : state (seed | 1u) {}

    std::uint32_t nextUInt() noexcept
    {
        state += 0x9e3779b9u;
        return hash32 (state);
    }

    float nextFloat() noexcept          { return (float) (nextUInt() & 0x00ffffffu) / 16777216.0f; }
    float nextBipolar() noexcept        { return nextFloat() * 2.0f - 1.0f; }
    int   nextInt (int maxExclusive) noexcept
    {
        if (maxExclusive <= 0) return 0;
        return (int) (nextUInt() % (std::uint32_t) maxExclusive);
    }

private:
    std::uint32_t state;
};

} // namespace klopgeest::rng
