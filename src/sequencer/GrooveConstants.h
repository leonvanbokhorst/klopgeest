#pragma once

#include <algorithm>

// -----------------------------------------------------------------------------
// Shared bounds between the groove engine and the sequencer.
//
// The sequencer has to know how far a tick's triggers can stray from their
// nominal grid position so it can generate a look-ahead window wide enough to
// capture every event exactly once. Those bounds live here (and only here) and
// are derived from the documented parameter ranges, not from tuning values.
//
// Deviation budget for one tick (all in musical time; 1 step = one 16th):
//   programmed swing  : + 0.75 step           (odd 16th late, grid feel)
//   programmed micro  : - 0.50 .. + 0.50 step (per-step, may be early)
//   per-voice bias    : - 25 .. + 25 ms       (parameter)
//   humanisation      : - 45 .. + 45 ms       (clamped stochastic part)
//   ratchet extension : + 0.75 step           (last of four sub-hits)
//
// => earliest possible trigger: -0.5 step - 70 ms
// => latest   possible trigger: +2.0 step + 70 ms
// -----------------------------------------------------------------------------
namespace klopgeest::groove
{

// --- Humanisation amount ------------------------------------------------------
// TIGHT / LOOSE / FERAL scale every stochastic dimension.
inline constexpr float kPersonalityTight = 0.35f;
inline constexpr float kPersonalityLoose = 1.00f;
inline constexpr float kPersonalityFeral = 1.70f;

// --- Timing ------------------------------------------------------------------
// Base timing spread (ms) at humanize=1, stability=0, LOOSE, role weight 1.
inline constexpr double kTimingBaseMs = 60.0;
// Absolute clamp on the stochastic timing part: the pulse must stay readable.
inline constexpr double kStochasticTimingClampMs = 45.0;
inline constexpr double kStochasticTimingClampStepFraction = 0.35;
// Anchors (kick/snare) are kept much tighter than colour voices.
inline constexpr double kAnchorTimingClampMs = 20.0;
inline constexpr double kAnchorTimingClampStepFraction = 0.18;

// --- Velocity / timbre -------------------------------------------------------
inline constexpr float kLevelBase = 0.55f;      // absolute velocity units
inline constexpr float kPitchBaseSemis = 0.50f; // semitones
inline constexpr float kDecayBase = 0.35f;      // scale units
inline constexpr float kTransientBase = 0.35f;
inline constexpr float kToneBase = 0.25f;       // offset units (-0.5..0.5)
inline constexpr float kDriveBase = 0.25f;      // scale units
inline constexpr float kAccentGridBoost = 0.22f;
inline constexpr float kAccentOffbeatTrim = 0.06f;
inline constexpr float kAccentFlagThreshold = 0.75f;

// --- Ghost notes -------------------------------------------------------------
// Ghost probability per empty step of a non-anchor voice at ghost=1.
inline constexpr float kGhostProbability = 0.50f;
inline constexpr float kGhostVelocityFloor = 0.08f;
inline constexpr float kGhostVelocityRange = 0.20f;

// --- Ratchets ----------------------------------------------------------------
inline constexpr int kMaxRatchet = 4;
inline constexpr float kRatchetVelocityDrop = 0.16f;  // per later sub-hit
inline constexpr float kRatchetJitter = 0.05f;        // ±5% per sub-hit

// --- Value-noise periods (ticks) ---------------------------------------------
// Correlated, not white: neighbouring ticks move together.
inline constexpr float kTimingPeriodMin = 4.0f;
inline constexpr float kTimingPeriodRange = 8.0f;      // 4..12 ticks
inline constexpr float kLevelPeriodMin = 2.0f;
inline constexpr float kLevelPeriodRange = 4.0f;
inline constexpr float kPitchPeriodMin = 6.0f;
inline constexpr float kPitchPeriodRange = 10.0f;
inline constexpr float kDecayPeriodMin = 8.0f;
inline constexpr float kDecayPeriodRange = 12.0f;

// --- FERAL rare spikes -------------------------------------------------------
inline constexpr float kFeralSpikeProbability = 0.05f;
inline constexpr float kFeralSpikeMultiplier = 2.2f;

// --- Look-ahead bounds (samples) ---------------------------------------------
/** Widest early deviation a tick can produce, in samples. */
inline double maxEarlySamples (double stepSamples, double sampleRate) noexcept
{
    const double msToSamples = sampleRate / 1000.0;
    return 0.5 * stepSamples + (25.0 + kStochasticTimingClampMs) * msToSamples;
}

/** Widest late deviation a tick can produce, in samples (incl. ratchets). */
inline double maxLateSamples (double stepSamples, double sampleRate) noexcept
{
    const double msToSamples = sampleRate / 1000.0;
    // swing 0.75 + micro 0.5 + ratchet 0.75 = 2.0 steps, plus bias + stochastic.
    return 2.0 * stepSamples + (25.0 + kStochasticTimingClampMs) * msToSamples;
}

/** Positive modulo that also works for negative absolute tick indices. */
inline int positiveMod (long long value, int modulus) noexcept
{
    if (modulus <= 0)
        return 0;
    const long long m = value % (long long) modulus;
    return (int) (m < 0 ? m + (long long) modulus : m);
}

} // namespace klopgeest::groove
