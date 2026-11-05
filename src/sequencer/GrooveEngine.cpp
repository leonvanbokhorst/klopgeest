// -----------------------------------------------------------------------------
// GROOVE ENGINE.
//
// Every number that comes out of this file is a pure function of
// (master seed, voice, absolute tick index, pattern, params). There is no
// hidden state, which is what makes block-size invariance and reproducible
// takes possible.
//
// Musical model (see docs in GrooveConstants.h for the numbers):
//   * swing is a shared grid feel: odd 16ths lean late, for every voice;
//   * per-voice timing bias and per-step microtiming are programmed offsets;
//   * humanisation is correlated value noise (smoothBipolar) — the band drifts
//     together instead of every hit being independently wrong;
//   * anchor voices (kick/snare) have a lower role weight and a tighter clamp
//     than hats/perc/odd;
//   * TIGHT / LOOSE / FERAL scale all stochastic dimensions; FERAL adds rare
//     larger excursions that are still clamped;
//   * stability=1 multiplies the whole stochastic part by zero (machine);
//   * locked steps/lanes keep the programmed timing/velocity/pitch/ratchet and
//     the shared swing grid, but take no stochastic deviation and always fire
//     (probability forced to 1);
//   * ghosts are quiet, low-velocity hits on empty steps of non-anchor voices;
//   * ratchets are 1..4 evenly spaced sub-hits, later ones a little quieter.
// -----------------------------------------------------------------------------
#include "sequencer/GrooveEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/Random.h"
#include "core/VisualState.h" // HitFlag telemetry bits
#include "sequencer/GrooveConstants.h"

namespace klopgeest
{
namespace
{

using namespace klopgeest::groove;

// Random streams. Stable values: changing one must never change another
// dimension's determinism story, but they may be reordered only with care.
enum Salt : int
{
    saltProbability  = 0x1001,
    saltGhost        = 0x1002,
    saltGhostLevel   = 0x1003,
    saltTimingNoise  = 0x1101,
    saltLevelNoise   = 0x1102,
    saltPitchNoise   = 0x1103,
    saltDecayNoise   = 0x1104,
    saltTransientNoise = 0x1105,
    saltToneNoise    = 0x1106,
    saltDriveNoise   = 0x1107,
    saltFeral        = 0x1201,
    saltRatchetJitter = 0x1202,
    saltHitSeed      = 0x1301,
    saltPeriodTiming = 0x1401,
    saltPeriodLevel  = 0x1402,
    saltPeriodPitch  = 0x1403,
    saltPeriodDecay  = 0x1404,
    saltPeriodTransient = 0x1405,
    saltPeriodTone   = 0x1406,
    saltPeriodDrive  = 0x1407
};

/** Absolute tick index as a stable 32-bit hash input. */
inline int tickHash (long long tickIndex) noexcept
{
    return (int) (std::uint32_t) (std::uint64_t) tickIndex;
}

inline float personalityScale (int personality) noexcept
{
    switch (personality)
    {
        case (int) Personality::tight: return kPersonalityTight;
        case (int) Personality::feral: return kPersonalityFeral;
        case (int) Personality::loose:
        default:                       return kPersonalityLoose;
    }
}

/** Constant, seed-dependent correlation length for one voice/dimension. */
inline float periodFor (std::uint32_t seed, int voice, int salt,
                        float periodMin, float periodRange) noexcept
{
    return periodMin + periodRange * rng::unit (seed, voice, 0, salt);
}

inline float amount (float value) noexcept
{
    return std::clamp (value, 0.0f, 1.0f);
}

} // namespace

void GrooveEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    reset();
}

void GrooveEngine::reset() {}

int GrooveEngine::generate (const PatternData& pattern,
                            const EngineParams& params,
                            long long tickIndex,
                            double stepSamples,
                            double nominalOffsetSamples,
                            SequencedEvent* out,
                            int maxOut)
{
    if (out == nullptr || maxOut <= 0)
        return 0;
    if (! std::isfinite (stepSamples) || stepSamples <= 0.0
        || ! std::isfinite (nominalOffsetSamples))
        return 0;

    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double samplesToMs = 1000.0 / sr;
    const double stepMs = stepSamples * samplesToMs;

    const auto& global = params.global;
    const std::uint32_t seed = (std::uint32_t) global.seed;
    const int tick = tickHash (tickIndex);

    const float humanize = amount (global.humanize);
    const float stability = amount (global.stability);
    // stability = 1 -> exactly zero stochastic deviation (machine).
    const float humanFactor = humanize * (1.0f - stability);
    const float personality = personalityScale (global.personality);
    const float swing = std::clamp (global.swing, 0.0f, 0.75f);
    const float ghostAmount = amount (global.ghost);
    const float accent = amount (global.accent);
    const bool feralPersonality = global.personality == (int) Personality::feral;

    const bool oddTick = (tickIndex & 1LL) != 0;
    const bool downbeatTick = positiveMod (tickIndex, 4) == 0;

    int count = 0;

    for (int voice = 0; voice < kNumVoices && count < maxOut; ++voice)
    {
        if (! pattern.laneAudible (voice))
            continue;

        const auto& lane = pattern.lanes[(size_t) voice];
        const int laneLength = std::clamp (lane.length, 1, kMaxSteps);
        const int stepIndex = positiveMod (tickIndex, laneLength);
        const auto& step = lane.steps[(size_t) stepIndex];
        const auto& vp = params.voice[(size_t) voice];
        const float role = kVoiceRoleLoose[voice];

        const bool locked = lane.locked || step.locked;

        // --- shared stochastic bookkeeping ---------------------------------
        bool feralSpike = false;
        if (! locked && feralPersonality)
        {
            if (rng::unit (seed, voice, tick, saltFeral) < kFeralSpikeProbability)
                feralSpike = true;
        }
        const float spikeScale = feralSpike ? kFeralSpikeMultiplier : 1.0f;

        // Correlated value noise, one stream per dimension. The periods are a
        // constant function of (seed, voice), never of hidden state.
        const float timingPeriod = periodFor (seed, voice, saltPeriodTiming,
                                              kTimingPeriodMin, kTimingPeriodRange);
        const float levelPeriod = periodFor (seed, voice, saltPeriodLevel,
                                             kLevelPeriodMin, kLevelPeriodRange);
        const float pitchPeriod = periodFor (seed, voice, saltPeriodPitch,
                                             kPitchPeriodMin, kPitchPeriodRange);
        const float decayPeriod = periodFor (seed, voice, saltPeriodDecay,
                                             kDecayPeriodMin, kDecayPeriodRange);
        const float transientPeriod = periodFor (seed, voice, saltPeriodTransient,
                                                 kDecayPeriodMin, kDecayPeriodRange);
        const float tonePeriod = periodFor (seed, voice, saltPeriodTone,
                                            kDecayPeriodMin, kDecayPeriodRange);
        const float drivePeriod = periodFor (seed, voice, saltPeriodDrive,
                                             kDecayPeriodMin, kDecayPeriodRange);

        // Total timing deviation for this tick, in milliseconds.
        auto timingDeviationMs = [&] (bool isLocked, float microtiming) -> double
        {
            double devMs = 0.0;

            // Swing is a shared grid feel and applies even to locked material.
            if (oddTick)
                devMs += (double) swing * stepMs;

            // Programmed step microtiming (-0.5..+0.5 step).
            devMs += (double) microtiming * stepMs;

            // Programmed per-voice bias.
            devMs += (double) vp.timingBiasMs;

            if (isLocked)
                return devMs;

            const float noise = rng::smoothBipolar (seed, voice, tick,
                                                    timingPeriod, saltTimingNoise);
            double stochastic = (double) noise * kTimingBaseMs
                                * (double) amount (vp.randTiming)
                                * (double) humanFactor * (double) personality
                                * (double) role * (double) spikeScale;

            // Anchors stay tighter than colour voices.
            const double anchorClamp = std::min (kAnchorTimingClampMs,
                                                 kAnchorTimingClampStepFraction * stepMs);
            const double looseClamp = std::min (kStochasticTimingClampMs,
                                                kStochasticTimingClampStepFraction * stepMs);
            const float looseness = std::clamp ((role - 0.5f) / 0.7f, 0.0f, 1.0f);
            const double clampMs = anchorClamp + (looseClamp - anchorClamp) * (double) looseness;
            stochastic = std::clamp (stochastic, -clampMs, clampMs);

            devMs += stochastic;
            return devMs;
        };

        // Velocity: programmed value, accent shaping, then humanised level.
        auto shapedVelocity = [&] (const Step& source, bool isLocked) -> float
        {
            float velocity = std::clamp (source.velocity, 0.02f, 1.0f);
            if (isLocked)
                return velocity; // exactly as programmed

            // Accent increases the dynamic range: strong beats lift, off-beats
            // drop a touch. Low-velocity (ghost-ish) steps lift less, so accents
            // stay contrast instead of flattening the pattern.
            if (downbeatTick)
                velocity += accent * kAccentGridBoost * (0.35f + 0.65f * velocity);
            else if (oddTick)
                velocity -= accent * kAccentOffbeatTrim;

            const float noise = rng::smoothBipolar (seed, voice, tick,
                                                    levelPeriod, saltLevelNoise);
            velocity += noise * kLevelBase * amount (vp.randLevel)
                        * humanFactor * personality * role * spikeScale;

            return std::clamp (velocity, 0.02f, 1.0f);
        };

        // Pitch / decay / transient / tone / drive stochastic deviations.
        auto pitchDeviation = [&] (bool isLocked, float programmedSemis) -> float
        {
            if (isLocked)
                return std::clamp (programmedSemis, -14.0f, 14.0f);

            const float noise = rng::smoothBipolar (seed, voice, tick,
                                                    pitchPeriod, saltPitchNoise);
            const float dev = noise * kPitchBaseSemis * amount (vp.randPitch)
                              * humanFactor * personality * role * spikeScale;
            return std::clamp (programmedSemis + dev, -14.0f, 14.0f);
        };

        auto decayScaleFor = [&] (bool isLocked) -> float
        {
            if (isLocked)
                return 1.0f;
            const float noise = rng::smoothBipolar (seed, voice, tick,
                                                    decayPeriod, saltDecayNoise);
            const float scale = 1.0f + noise * kDecayBase * amount (vp.randDecay)
                                * humanFactor * personality * role * spikeScale;
            return std::clamp (scale, 0.5f, 1.8f);
        };

        auto transientScaleFor = [&] (bool isLocked) -> float
        {
            if (isLocked)
                return 1.0f;
            const float noise = rng::smoothBipolar (seed, voice, tick,
                                                    transientPeriod, saltTransientNoise);
            const float scale = 1.0f + noise * kTransientBase * amount (vp.randTransient)
                                * humanFactor * personality * role * spikeScale;
            return std::clamp (scale, 0.45f, 1.9f);
        };

        auto toneOffsetFor = [&] (bool isLocked) -> float
        {
            if (isLocked)
                return 0.0f;
            // No dedicated randTone parameter in the frozen VoiceParams; timbre
            // follows the pitch-variation depth so a voice stays coherent.
            const float noise = rng::smoothBipolar (seed, voice, tick,
                                                    tonePeriod, saltToneNoise);
            const float offset = noise * kToneBase * amount (vp.randPitch)
                                 * humanFactor * personality * role * spikeScale;
            return std::clamp (offset, -0.4f, 0.4f);
        };

        auto driveScaleFor = [&] (bool isLocked) -> float
        {
            if (isLocked)
                return 1.0f;
            // No dedicated randDrive parameter: harder hits saturate more, so
            // drive follows the level-variation depth.
            const float noise = rng::smoothBipolar (seed, voice, tick,
                                                    drivePeriod, saltDriveNoise);
            const float scale = 1.0f + noise * kDriveBase * amount (vp.randLevel)
                                * humanFactor * personality * role * spikeScale;
            return std::clamp (scale, 0.6f, 1.6f);
        };

        auto writeEvent = [&] (int sampleOffset, int voiceIndex, int stepIdx,
                               long long absoluteTick, float timingDevMs,
                               float velocity, float pitchSemis, float decayScale,
                               float transientScale, float toneOffset, float driveScale,
                               std::uint32_t hitSeed, unsigned int flags)
        {
            auto& event = out[count++];
            event = SequencedEvent {};
            event.voice = voiceIndex;
            event.sampleOffset = sampleOffset;
            event.stepIndex = stepIdx;
            event.tickIndex = absoluteTick;
            event.timingDevMs = timingDevMs;
            event.trigger.velocity = velocity;
            event.trigger.pitchSemis = pitchSemis;
            event.trigger.decayScale = decayScale;
            event.trigger.transientScale = transientScale;
            event.trigger.toneOffset = toneOffset;
            event.trigger.driveScale = driveScale;
            event.trigger.seed = hitSeed;
            event.trigger.flags = flags;
        };

        // --- programmed hit -------------------------------------------------
        if (step.on)
        {
            // Locked material always fires. Everything else is a deterministic
            // hash decision per (seed, voice, tick), so probability 0 never and
            // probability 1 always fires.
            if (! locked)
            {
                const float probability = amount (step.probability);
                if (rng::unit (seed, voice, tick, saltProbability) >= probability)
                    continue;
            }

            const int ratchet = std::clamp (step.ratchet, 1, kMaxRatchet);
            const double devMs = timingDeviationMs (locked, step.microtiming);
            const double devSamples = devMs * sr / 1000.0;

            const float velocity = shapedVelocity (step, locked);
            const float pitch = pitchDeviation (locked, step.pitchSemis);
            const float decayScale = decayScaleFor (locked);
            const float transientScale = transientScaleFor (locked);
            const float toneOffset = toneOffsetFor (locked);
            const float driveScale = driveScaleFor (locked);

            for (int k = 0; k < ratchet && count < maxOut; ++k)
            {
                const double subOffsetSamples = (double) k * stepSamples / (double) ratchet;
                const int sampleOffset = (int) std::lround (nominalOffsetSamples
                                                            + devSamples + subOffsetSamples);

                float subVelocity = velocity;
                if (k > 0)
                {
                    // Later ratchet hits are a little quieter.
                    subVelocity *= 1.0f - kRatchetVelocityDrop * (float) k;
                    if (! locked)
                    {
                        const int subTick = (int) ((std::uint32_t) tick * 4u + (std::uint32_t) k);
                        const float jitter = 1.0f + kRatchetJitter
                                             * rng::bipolar (seed, voice, subTick,
                                                             saltRatchetJitter);
                        subVelocity *= jitter;
                    }
                }
                subVelocity = std::clamp (subVelocity, 0.02f, 1.0f);

                unsigned int flags = 0u;
                if (k > 0)
                    flags |= (unsigned int) HitFlag::ratchet;
                if (subVelocity >= kAccentFlagThreshold)
                    flags |= (unsigned int) HitFlag::accent;
                if (feralSpike)
                    flags |= (unsigned int) HitFlag::feral;

                const std::uint32_t hitSeed = rng::hash (seed, voice, tick, saltHitSeed + k * 17);

                writeEvent (sampleOffset, voice, stepIndex, tickIndex, (float) devMs,
                            subVelocity, pitch, decayScale, transientScale,
                            toneOffset, driveScale, hitSeed, flags);
            }
            continue;
        }

        // --- ghost note on an empty step ------------------------------------
        if (locked)
            continue; // locked lanes/steps are exactly the program: no ghosts

        // Anchors (kick/snare) never ghost: they define the pulse.
        if (role <= 0.5f)
            continue;

        float ghostProbability = ghostAmount * kGhostProbability
                                 * std::clamp (role, 0.5f, 1.2f);
        // Ghosts live between the main hits, not on the downbeat.
        if (downbeatTick)
            ghostProbability *= 0.35f;
        else if (! oddTick)
            ghostProbability *= 0.65f;

        if (ghostProbability <= 0.0f)
            continue;
        if (rng::unit (seed, voice, tick, saltGhost) >= ghostProbability)
            continue;

        const double ghostDevMs = timingDeviationMs (false, 0.0f);
        const int sampleOffset = (int) std::lround (nominalOffsetSamples
                                                    + ghostDevMs * sr / 1000.0);

        const float ghostVelocity = std::clamp (kGhostVelocityFloor
                                                + kGhostVelocityRange
                                                      * rng::unit (seed, voice, tick, saltGhostLevel),
                                                0.02f, 1.0f);

        const float ghostPitch = pitchDeviation (false, 0.0f);
        const float ghostDecay = decayScaleFor (false);
        const float ghostTransient = transientScaleFor (false);
        const float ghostTone = toneOffsetFor (false);
        const float ghostDrive = driveScaleFor (false);

        unsigned int flags = (unsigned int) HitFlag::ghost;
        if (feralSpike)
            flags |= (unsigned int) HitFlag::feral;
        const std::uint32_t hitSeed = rng::hash (seed, voice, tick, saltHitSeed + 0x55);

        writeEvent (sampleOffset, voice, stepIndex, tickIndex, (float) ghostDevMs,
                    ghostVelocity, ghostPitch, ghostDecay, ghostTransient,
                    ghostTone, ghostDrive, hitSeed, flags);
    }

    return count;
}

} // namespace klopgeest
