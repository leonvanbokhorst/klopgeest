#include "sequencer/MutationEngine.h"

#include <cmath>

#include "core/Random.h"

namespace klopgeest
{

namespace
{
constexpr int kAnchorVoices[] = { VoiceKick, VoiceSnare };

bool isAnchorVoice (int voice)
{
    return voice == VoiceKick || voice == VoiceSnare;
}

} // namespace

MutationEngine::MutationEngine() { reset(); }

void MutationEngine::reset()
{
    historyCount = 0;
}

bool MutationEngine::isAnchorStep (int voice, int step)
{
    if (voice == VoiceKick)
        return step % 4 == 0;
    if (voice == VoiceSnare)
        return step % 8 == 4;
    return false;
}

bool MutationEngine::stepMutable (const PatternData& pattern, int voice, int step)
{
    const auto& lane = pattern.lanes[(size_t) voice];
    if (lane.locked || lane.muted)
        return false;
    if (step < 0 || step >= lane.length)
        return false;
    return ! lane.steps[(size_t) step].locked;
}

void MutationEngine::pushHistory (const PatternData& pattern)
{
    if (historyCount == kHistory)
    {
        // Drop the oldest state and shift down.
        for (int i = 1; i < kHistory; ++i)
            history[(size_t) (i - 1)] = history[(size_t) i];
        --historyCount;
    }
    history[(size_t) historyCount++] = pattern;
}

bool MutationEngine::nudge (PatternData& pattern, float amount, std::uint32_t seed)
{
    const auto before = pattern;
    rng::SeededRandom random (seed);
    const float depth = juce::jlimit (0.0f, 1.0f, amount);

    int edits = 0;
    const int attempts = 6;

    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        const int voice = random.nextInt (kNumVoices);
        if (! stepMutable (pattern, voice, 0))
            continue;

        const int laneLength = pattern.laneLength (voice);
        const int step = random.nextInt (laneLength);
        if (! stepMutable (pattern, voice, step))
            continue;

        auto& s = pattern.step (voice, step);
        if (! s.on)
        {
            // Add a quiet ghost inside the existing rhythm.
            if (random.nextFloat() < 0.45f + depth * 0.4f)
            {
                s.on = true;
                s.velocity = 0.16f + 0.28f * random.nextFloat() * (0.4f + depth);
                s.probability = 0.55f + 0.35f * random.nextFloat();
                s.microtiming = random.nextBipolar() * 0.04f * (0.5f + depth);
                s.pitchSemis = random.nextBipolar() * 2.0f;
                ++edits;
            }
        }
        else if (! isAnchorStep (voice, step))
        {
            // Re-shape an existing non-anchor hit.
            s.velocity = juce::jlimit (0.08f, 1.0f,
                                       s.velocity + random.nextBipolar() * 0.18f * (0.4f + depth));
            s.microtiming = juce::jlimit (-0.12f, 0.12f,
                                          s.microtiming + random.nextBipolar() * 0.025f * (0.5f + depth));
            if (random.nextFloat() < 0.25f)
                s.probability = juce::jlimit (0.35f, 1.0f,
                                              s.probability + random.nextBipolar() * 0.15f);
            ++edits;
        }

        if (edits >= 2)
            break;
    }

    if (edits == 0)
        return false;

    pattern.sanitise();
    pushHistory (before);
    return true;
}

bool MutationEngine::mutate (PatternData& pattern, float amount, std::uint32_t seed)
{
    const auto before = pattern;
    rng::SeededRandom random (seed);
    const float depth = juce::jlimit (0.0f, 1.0f, amount);
    int edits = 0;

    // 1. Ghost-note weather: sprinkle quiet extra events.
    const int ghostCount = 1 + (int) std::round (depth * 4.0f);
    for (int i = 0; i < ghostCount; ++i)
    {
        const int voice = 2 + random.nextInt (kNumVoices - 2); // clap..odd
        const int laneLength = pattern.laneLength (voice);
        const int step = random.nextInt (laneLength);
        if (! stepMutable (pattern, voice, step))
            continue;

        auto& s = pattern.step (voice, step);
        if (! s.on)
        {
            s.on = true;
            s.velocity = 0.12f + 0.30f * random.nextFloat() * (0.5f + depth);
            s.probability = 0.4f + 0.5f * random.nextFloat();
            s.microtiming = random.nextBipolar() * 0.05f;
            s.pitchSemis = random.nextBipolar() * 3.0f;
            ++edits;
        }
    }

    // 2. Accent redistribution and restrained pitch colour.
    const int reshapeCount = 2 + (int) std::round (depth * 5.0f);
    for (int i = 0; i < reshapeCount; ++i)
    {
        const int voice = random.nextInt (kNumVoices);
        const int laneLength = pattern.laneLength (voice);
        const int step = random.nextInt (laneLength);
        if (! stepMutable (pattern, voice, step) || isAnchorStep (voice, step))
            continue;

        auto& s = pattern.step (voice, step);
        if (! s.on)
            continue;

        if (random.nextFloat() < 0.55f)
            s.velocity = juce::jlimit (0.08f, 1.0f,
                                       s.velocity + random.nextBipolar() * 0.28f * (0.5f + depth));
        if (random.nextFloat() < 0.35f)
            s.pitchSemis = juce::jlimit (-6.0f, 6.0f,
                                         s.pitchSemis + random.nextBipolar() * 2.5f);
        if (random.nextFloat() < 0.20f)
            s.ratchet = 1 + random.nextInt (3);
        ++edits;
    }

    // 3. Displacement: move one non-anchor hit a step forward or back.
    {
        const int voice = 2 + random.nextInt (kNumVoices - 2);
        const int laneLength = pattern.laneLength (voice);
        const int step = random.nextInt (laneLength);
        if (stepMutable (pattern, voice, step) && pattern.step (voice, step).on)
        {
            const int delta = random.nextFloat() < 0.5f ? -1 : 1;
            const int target = (step + delta + laneLength) % laneLength;
            if (stepMutable (pattern, voice, target) && ! pattern.step (voice, target).on)
            {
                pattern.step (voice, target) = pattern.step (voice, step);
                pattern.step (voice, step) = Step {};
                ++edits;
            }
        }
    }

    // 4. Occasional silence: drop one non-anchor hit, but never the pulse.
    if (random.nextFloat() < 0.25f + depth * 0.25f)
    {
        const int voice = 2 + random.nextInt (kNumVoices - 2);
        const int laneLength = pattern.laneLength (voice);
        const int step = random.nextInt (laneLength);
        if (stepMutable (pattern, voice, step) && pattern.step (voice, step).on)
        {
            pattern.step (voice, step).on = false;
            ++edits;
        }
    }

    if (edits == 0)
        return false;

    pattern.sanitise();
    pushHistory (before);
    return true;
}

bool MutationEngine::recall (PatternData& pattern)
{
    if (historyCount <= 0)
        return false;

    pattern = history[(size_t) --historyCount];
    return true;
}

} // namespace klopgeest
