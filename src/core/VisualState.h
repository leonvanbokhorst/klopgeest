#pragma once

#include <atomic>

#include "core/Constants.h"

// -----------------------------------------------------------------------------
// Lock-free visual telemetry: audio thread writes, GUI reads. No locks, no
// allocation, no queuing — if a frame is missed it simply doesn't matter.
//
// This is also the instrument's honesty layer: it shows not just *that* a hit
// happened, but how far the groove engine bent it (timing, pitch, velocity,
// decay) and which stochastic mechanism was responsible.
// -----------------------------------------------------------------------------
namespace klopgeest
{

enum class HitFlag : unsigned int
{
    none      = 0,
    ghost     = 1u << 0,
    accent    = 1u << 1,
    ratchet   = 1u << 2,
    feral     = 1u << 3,
    mutation  = 1u << 4,
    live      = 1u << 5
};

struct VisualState
{
    VisualState() { reset(); }

    void reset() noexcept
    {
        for (int v = 0; v < kNumVoices; ++v)
        {
            voiceLevel[v].store (0.0f, std::memory_order_relaxed);
            lastVelocity[v].store (0.0f, std::memory_order_relaxed);
            lastPitchSemis[v].store (0.0f, std::memory_order_relaxed);
            lastTimingMs[v].store (0.0f, std::memory_order_relaxed);
            lastDecayScale[v].store (1.0f, std::memory_order_relaxed);
            lastFlags[v].store (0u, std::memory_order_relaxed);
            triggerCount[v].store (0u, std::memory_order_relaxed);
            laneStep[v].store (0, std::memory_order_relaxed);
            laneAudible[v].store (true, std::memory_order_relaxed);
        }
        gainReductionDb.store (0.0f, std::memory_order_relaxed);
        outPeakL.store (0.0f, std::memory_order_relaxed);
        outPeakR.store (0.0f, std::memory_order_relaxed);
        outRmsL.store (0.0f, std::memory_order_relaxed);
        outRmsR.store (0.0f, std::memory_order_relaxed);
        playing.store (false, std::memory_order_relaxed);
        hostSynced.store (false, std::memory_order_relaxed);
        personality.store (1, std::memory_order_relaxed);
        bpm.store (120.0f, std::memory_order_relaxed);
        currentTick.store (0, std::memory_order_relaxed);
        mutationCounter.store (0u, std::memory_order_relaxed);
        patternChangeCounter.store (0u, std::memory_order_relaxed);
    }

    std::atomic<float>  voiceLevel[kNumVoices];
    std::atomic<float>  lastVelocity[kNumVoices];
    std::atomic<float>  lastPitchSemis[kNumVoices];
    std::atomic<float>  lastTimingMs[kNumVoices];
    std::atomic<float>  lastDecayScale[kNumVoices];
    std::atomic<unsigned int> lastFlags[kNumVoices];
    std::atomic<unsigned int> triggerCount[kNumVoices];
    std::atomic<int>    laneStep[kNumVoices];
    std::atomic<bool>   laneAudible[kNumVoices];

    std::atomic<float>  gainReductionDb;
    std::atomic<float>  outPeakL, outPeakR;
    std::atomic<float>  outRmsL, outRmsR;

    std::atomic<bool>   playing;
    std::atomic<bool>   hostSynced;
    std::atomic<int>    personality;
    std::atomic<float>  bpm;
    std::atomic<long long> currentTick;
    std::atomic<unsigned int> mutationCounter;
    std::atomic<unsigned int> patternChangeCounter;
};

} // namespace klopgeest
