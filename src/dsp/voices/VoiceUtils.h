#pragma once

#include <cmath>
#include <cstdint>

// -----------------------------------------------------------------------------
// Shared real-time-safe DSP primitives for the eight KLOPGEEST drum voices.
//
// Everything in this header is allocation-free, exception-free and denormal
// safe. No std::vector, no locks, no trig tables that need preparing.
// -----------------------------------------------------------------------------
namespace klopgeest::vutil
{

inline constexpr float kPi     = 3.14159265358979323846f;
inline constexpr float kTwoPi  = 6.28318530717958647692f;
inline constexpr float kInvSqrt2 = 0.70710678118654752440f;

inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** Clamps a value, mapping NaN/Inf to a sane fallback so hosts and fuzzers can
    never push the synthesis state into a non-finite corner. */
inline float sanitise (float v, float lo, float hi, float fallback) noexcept
{
    if (! std::isfinite (v))
        return fallback;
    return clampf (v, lo, hi);
}

/** Keeps recursive one-pole states out of the denormal performance cliff. */
inline float flushDenorm (float v) noexcept
{
    return (v > -1.0e-20f && v < 1.0e-20f) ? 0.0f : v;
}

inline float dbToGain (float db) noexcept
{
    if (! std::isfinite (db) || db <= -100.0f)
        return 0.0f;
    return std::pow (10.0f, db * 0.05f);
}

inline float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/** Rational tanh approximation. Clamped to [-3, 3] where it reaches +/-1; the
    approximation error below that is < 0.02 and the curve stays monotonic. */
inline float fastTanh (float x) noexcept
{
    x = clampf (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Analog-ish saturation with a dry/wet amount (0 = clean, 1 = driven). */
inline float saturate (float x, float amount) noexcept
{
    const float a = clampf (amount, 0.0f, 1.0f);
    if (a <= 0.0f)
        return x;

    const float gain    = 1.0f + 4.0f * a;
    const float driven  = fastTanh (x * gain);
    const float makeup  = 1.0f / (0.72f + 0.28f * gain);
    return x + a * (driven * makeup - x);
}

/** Last-resort output guard: no NaN/Inf and a hard ceiling (the voice itself is
    designed to stay well below this; the master glue stage handles the rest). */
inline float safeOutput (float x) noexcept
{
    if (! std::isfinite (x))
        return 0.0f;
    return clampf (x, -8.0f, 8.0f);
}

// -----------------------------------------------------------------------------
// Noise
// -----------------------------------------------------------------------------
/** xorshift32 white noise. Seeded deterministically per hit from VoiceTrigger. */
struct Noise
{
    std::uint32_t state = 0x9e3779b9u;

    void seed (std::uint32_t s) noexcept
    {
        state = s | 1u;
    }

    inline std::uint32_t nextBits() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    inline float nextUnit() noexcept
    {
        return (float) (nextBits() & 0x00ffffffu) * (1.0f / 16777216.0f);
    }

    inline float nextBipolar() noexcept
    {
        return nextUnit() * 2.0f - 1.0f;
    }
};

// -----------------------------------------------------------------------------
// Oscillators
// -----------------------------------------------------------------------------
/** Phase-accumulating sine; `phase` is in cycles [0, 1). */
struct SineOsc
{
    float phase = 0.0f;

    void reset (float startPhase = 0.0f) noexcept
    {
        phase = startPhase - std::floor (startPhase);
    }

    inline float process (float increment) noexcept
    {
        const float s = std::sin (kTwoPi * phase);
        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;
        else if (phase < 0.0f)
            phase += 1.0f;
        return s;
    }
};

/** Naive square: the aliasing is part of the 808-inspired metallic character
    and is tamed by the following band/high-pass stage. */
struct SquareOsc
{
    float phase = 0.0f;

    void reset (float startPhase = 0.0f) noexcept
    {
        phase = startPhase - std::floor (startPhase);
    }

    inline float process (float increment) noexcept
    {
        const float s = phase < 0.5f ? 1.0f : -1.0f;
        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;
        else if (phase < 0.0f)
            phase += 1.0f;
        return s;
    }
};

/** Naive saw for the unstable oscillator. */
struct SawOsc
{
    float phase = 0.0f;

    void reset (float startPhase = 0.0f) noexcept
    {
        phase = startPhase - std::floor (startPhase);
    }

    inline float process (float increment) noexcept
    {
        const float s = phase * 2.0f - 1.0f;
        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;
        else if (phase < 0.0f)
            phase += 1.0f;
        return s;
    }
};

// -----------------------------------------------------------------------------
// Filters
// -----------------------------------------------------------------------------
/** Topology-preserving state-variable filter (Zavalishin/Simper). Stable under
    fast coefficient changes and cheap on arm64. */
class Svf
{
public:
    enum class Mode { lowpass, bandpass, highpass };

    void reset() noexcept
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    void set (float cutoffHz, float q, double sampleRate) noexcept
    {
        const float sr = (float) sampleRate;
        cutoffHz = clampf (cutoffHz, 20.0f, sr * 0.45f);
        const float g = std::tan (kPi * cutoffHz / sr);
        const float k = 1.0f / clampf (q, 0.1f, 40.0f);

        gCoeff = g;
        kCoeff = k;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
    }

    void setG (float g) noexcept
    {
        gCoeff = g;
        a1 = 1.0f / (1.0f + g * (g + kCoeff));
        a2 = g * a1;
    }

    /** Normalised angular coefficient for the current cutoff (helper for
        voices that need to morph the cutoff without recomputing tan()). */
    float g() const noexcept { return gCoeff; }

    inline float process (float v0, Mode mode) noexcept
    {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + gCoeff * v1;
        ic1eq = flushDenorm (2.0f * v1 - ic1eq);
        ic2eq = flushDenorm (2.0f * v2 - ic2eq);

        switch (mode)
        {
            case Mode::lowpass:  return v2;
            case Mode::bandpass: return v1;
            case Mode::highpass:
            default:             return v0 - kCoeff * v1 - v2;
        }
    }

private:
    float gCoeff = 0.0f, kCoeff = 1.0f, a1 = 0.0f, a2 = 0.0f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
};

/** 6 dB/oct one-pole low pass; used for gentle analog damping. */
struct OnePoleLP
{
    float z = 0.0f;
    float a = 1.0f;

    void reset() noexcept { z = 0.0f; }

    void set (float cutoffHz, double sampleRate) noexcept
    {
        const float sr = (float) sampleRate;
        a = 1.0f - std::exp (-kTwoPi * clampf (cutoffHz, 1.0f, sr * 0.49f) / sr);
    }

    inline float process (float x) noexcept
    {
        z = flushDenorm (z + a * (x - z));
        return z;
    }
};

struct OnePoleHP
{
    float z = 0.0f;
    float a = 1.0f;

    void reset() noexcept { z = 0.0f; }

    void set (float cutoffHz, double sampleRate) noexcept
    {
        const float sr = (float) sampleRate;
        a = 1.0f - std::exp (-kTwoPi * clampf (cutoffHz, 1.0f, sr * 0.49f) / sr);
    }

    inline float process (float x) noexcept
    {
        z = flushDenorm (z + a * (x - z));
        return x - z;
    }
};

/** Removes any DC left by asymmetric saturation. */
struct DCBlocker
{
    float x1 = 0.0f;
    float y1 = 0.0f;

    void reset() noexcept { x1 = y1 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = x - x1 + 0.9965f * y1;
        x1 = x;
        y1 = flushDenorm (y);
        return y;
    }
};

// -----------------------------------------------------------------------------
// Envelopes
// -----------------------------------------------------------------------------
/** Exponential decay envelope with a settable time constant. */
struct ExpEnv
{
    float value = 0.0f;
    float coeff = 0.0f;

    void setDecay (float seconds, double sampleRate) noexcept
    {
        const float s = seconds < 0.0005f ? 0.0005f : seconds;
        coeff = std::exp (-1.0f / (s * (float) sampleRate));
    }

    void start (float level) noexcept { value = level; }

    inline float process() noexcept
    {
        value = flushDenorm (value * coeff);
        return value;
    }

    bool silent (float threshold = 1.0e-4f) const noexcept
    {
        return value < threshold;
    }
};

/** Attack/decay envelope used for click/snap transients so even a "slow"
    transient does not click the buffer at sample zero. */
struct AdEnv
{
    float value = 0.0f;
    float attackCoeff = 1.0f;
    float decayCoeff = 0.0f;

    void set (float attackSeconds, float decaySeconds, double sampleRate) noexcept
    {
        attackCoeff = attackSeconds < 0.0002f
                          ? 1.0f
                          : 1.0f - std::exp (-1.0f / (attackSeconds * (float) sampleRate));
        decayCoeff = std::exp (-1.0f / ((decaySeconds < 0.0005f ? 0.0005f : decaySeconds)
                                        * (float) sampleRate));
    }

    void start (float level = 0.0f) noexcept { value = level; }

    bool isRising() const noexcept { return value < 0.999f; }

    inline float process() noexcept
    {
        if (value < 0.999f)
            value = flushDenorm (value + attackCoeff * (1.0f - value));
        else
            value = flushDenorm (value * decayCoeff);
        return value;
    }
};

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------
/** Equal-power style pan gains (left/right) for a -1..+1 pan position. */
inline void panGains (float pan, float& leftGain, float& rightGain) noexcept
{
    const float p = clampf (pan, -1.0f, 1.0f);
    leftGain  = std::sqrt (0.5f * (1.0f - p));
    rightGain = std::sqrt (0.5f * (1.0f + p));
}

/** Velocity curve used by all voices: quiet ghosts scale down strongly while
    accents stay punchy. */
inline float velocityGain (float velocity) noexcept
{
    const float v = clampf (velocity, 0.0f, 1.0f);
    return 0.06f + 0.94f * v * v;
}

/** Converts a semitone offset into a frequency ratio. */
inline float semitoneRatio (float semis) noexcept
{
    return std::pow (2.0f, clampf (semis, -96.0f, 96.0f) / 12.0f);
}

/** Maps a 0..1 decay control onto a musically useful time constant. */
inline float decaySeconds (float decay01, float shortest, float longest) noexcept
{
    const float d = clampf (decay01, 0.0f, 1.0f);
    return shortest * std::pow (longest / shortest, d);
}

} // namespace klopgeest::vutil
