// -----------------------------------------------------------------------------
// Transport-aware step sequencer.
//
// The sequencer never "remembers" which tick it is in. Every block it derives a
// tick range from the current clock position, asks the (stateless) groove engine
// for those ticks and keeps the triggers that land inside the block. That single
// design decision gives:
//   * sample-accurate placement from the host ppq position,
//   * block-size invariance (a tick has the same absolute time in a 64-sample
//     and a 1024-sample render),
//   * natural handling of seeks, loops and tempo changes (the clock position is
//     simply read again),
//   * no duplicated or dropped triggers at block boundaries (half-open [0,n)
//     filtering; a tick whose trigger is exactly on a boundary belongs to the
//     block that starts there).
//
// The look-ahead window (early/late) is wide enough for every groove deviation
// the engine can produce, including ratchet sub-hits. See GrooveConstants.h.
// -----------------------------------------------------------------------------
#include "sequencer/Sequencer.h"

#include <algorithm>
#include <cmath>

#include "sequencer/GrooveConstants.h"

namespace klopgeest
{
namespace
{

constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 999.0;
constexpr double kTickPpq = 0.25; // one 16th note

/** One tick can produce one hit per voice, or up to four ratchet sub-hits. */
constexpr int kMaxEventsPerTick = kNumVoices * groove::kMaxRatchet;

/** Hard iteration guard against a corrupt host ppq position. */
constexpr long long kMaxTicksPerBlock = 4096;

double clampBpm (double bpm) noexcept
{
    if (! std::isfinite (bpm))
        return 120.0;
    return std::clamp (bpm, kMinBpm, kMaxBpm);
}

} // namespace

void Sequencer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    groove.prepare (sampleRate);
    reset();
}

void Sequencer::reset()
{
    internalPpq = 0.0;
    lastHostSynced = false;
}

void Sequencer::process (const HostTransport& host,
                         const PatternData& pattern,
                         const EngineParams& params,
                         int numSamples,
                         SequencedEvent* out,
                         int maxOut,
                         int& numOut,
                         SequencerFrame& frame)
{
    numOut = 0;
    frame = SequencerFrame {};

    const bool hostPlaying = host.available && host.playing;
    const bool freeRunning = ! hostPlaying
                             && internalRunning.load (std::memory_order_relaxed);
    const bool playing = (hostPlaying || freeRunning) && numSamples > 0
                         && out != nullptr && maxOut > 0;

    lastHostSynced = hostPlaying;
    frame.playing = hostPlaying || freeRunning;
    frame.hostSynced = hostPlaying;

    const double bpm = clampBpm (hostPlaying ? host.bpm : (double) params.global.tempo);
    frame.bpm = (float) bpm;

    if (! playing)
    {
        for (int v = 0; v < kNumVoices; ++v)
            frame.laneStep[v] = -1;
        return;
    }

    const double samplesPerPpq = 60.0 * sampleRate / bpm;
    if (! std::isfinite (samplesPerPpq) || samplesPerPpq <= 0.0)
        return;

    // --- Where is the block on the musical grid? -----------------------------
    // Host mode: the host's ppq position at the start of this block.
    // Free-run: the internal ppq accumulated at the Tempo parameter.
    if (! std::isfinite (internalPpq) || std::abs (internalPpq) > 1.0e9)
        internalPpq = 0.0;

    double blockStartPpq = hostPlaying ? host.ppqPosition : internalPpq;
    // Corrupt/absurd host data must not break the grid (or the tick index cast).
    if (! std::isfinite (blockStartPpq) || std::abs (blockStartPpq) > 1.0e9)
        blockStartPpq = internalPpq;

    const double stepSamples = samplesPerPpq * kTickPpq;
    if (! std::isfinite (stepSamples) || stepSamples <= 0.0)
        return;

    // --- Candidate ticks ------------------------------------------------------
    // A tick's triggers can land early or late by up to these amounts.
    const double earlySamples = groove::maxEarlySamples (stepSamples, sampleRate);
    const double lateSamples = groove::maxLateSamples (stepSamples, sampleRate);

    const double minPpq = blockStartPpq + (-earlySamples - 1.0) / samplesPerPpq;
    const double maxPpq = blockStartPpq + ((double) numSamples + lateSamples + 1.0) / samplesPerPpq;

    long long firstTick = (long long) std::floor (minPpq / kTickPpq);
    long long lastTick = (long long) std::ceil (maxPpq / kTickPpq);
    if (lastTick < firstTick)
        lastTick = firstTick;
    if (lastTick - firstTick > kMaxTicksPerBlock)
        lastTick = firstTick + kMaxTicksPerBlock;

    // --- Generate and filter -------------------------------------------------
    SequencedEvent tickEvents[kMaxEventsPerTick];

    for (long long tick = firstTick; tick <= lastTick && numOut < maxOut; ++tick)
    {
        const double nominalOffset = ((double) tick * kTickPpq - blockStartPpq) * samplesPerPpq;
        const int generated = groove.generate (pattern, params, tick, stepSamples,
                                               nominalOffset, tickEvents, kMaxEventsPerTick);

        for (int i = 0; i < generated && numOut < maxOut; ++i)
        {
            const auto& event = tickEvents[i];
            // Half-open block window: [0, numSamples). Negative offsets belong to
            // an earlier block, offsets >= numSamples to a later one.
            if (event.sampleOffset < 0 || event.sampleOffset >= numSamples)
                continue;
            out[numOut++] = event;
        }
    }

    // --- Sort by sample offset ----------------------------------------------
    // Insertion sort: deterministic, stable, allocation-free. The event count
    // per block is small (a handful of drums), so O(n^2) is irrelevant.
    for (int i = 1; i < numOut; ++i)
    {
        const SequencedEvent value = out[i];
        int j = i - 1;
        while (j >= 0
               && (out[j].sampleOffset > value.sampleOffset
                   || (out[j].sampleOffset == value.sampleOffset
                       && (out[j].tickIndex > value.tickIndex
                           || (out[j].tickIndex == value.tickIndex
                               && out[j].voice > value.voice)))))
        {
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = value;
    }

    // --- Display / telemetry -------------------------------------------------
    // The playhead is the latest 16th that has started by the end of the block.
    // This is stable for tiny block sizes (a 64-sample block rarely contains a
    // tick start), which keeps the GUI lane indicators from flickering.
    const double endPpq = blockStartPpq + (double) numSamples / samplesPerPpq;
    const long long playhead = (long long) std::floor (endPpq / kTickPpq - 1.0e-9);
    frame.tickIndex = playhead;

    for (int v = 0; v < kNumVoices; ++v)
    {
        if (playhead < 0)
        {
            frame.laneStep[v] = -1;
            continue;
        }
        const int laneLength = std::clamp (pattern.lanes[(size_t) v].length, 1, kMaxSteps);
        frame.laneStep[v] = groove::positiveMod (playhead, laneLength);
    }

    // --- Advance / resync the internal clock --------------------------------
    // In host mode we track the host position so that stopping the host and
    // continuing free-run stays musical. In free-run we simply advance.
    internalPpq = blockStartPpq + (double) numSamples / samplesPerPpq;
}

} // namespace klopgeest
