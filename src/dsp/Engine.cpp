#include "dsp/Engine.h"

#include <algorithm>
#include <cmath>

#include "core/Random.h"

namespace klopgeest
{

void Engine::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlock = juce::jmax (16, newMaxBlockSize);

    voices.prepare (sampleRate, maxBlock);
    sequencer.prepare (sampleRate);
    master.prepare (sampleRate, maxBlock);

    prepared = true;
    reset();
}

void Engine::reset()
{
    voices.reset();
    sequencer.reset();
    master.reset();
    liveCounter = 0;
}

void Engine::setRunning (bool shouldRun) noexcept
{
    sequencer.setInternalRunning (shouldRun);
}

bool Engine::isRunning() const noexcept
{
    return sequencer.isInternalRunning();
}

void Engine::setParams (const EngineParams& newParams) noexcept
{
    params = newParams;
    voices.setParams (params);
    master.setParams (params.master);
}

void Engine::triggerEvent (const SequencedEvent& event, bool live)
{
    auto trigger = event.trigger;
    if (live)
        trigger.seed = rng::hash ((std::uint32_t) params.global.seed, event.voice, (int) (liveCounter++), 0x51);

    voices.trigger (event.voice, trigger);
}

void Engine::process (juce::AudioBuffer<float>& output,
                      const juce::MidiBuffer& midi,
                      int numSamples,
                      const PatternData& pattern,
                      const HostTransport& host,
                      VisualState& visual)
{
    if (! prepared || numSamples <= 0)
        return;

    output.clear();

    // --- 1. Sequenced events ------------------------------------------------
    SequencedEvent events[kMaxEventsPerBlock];
    int numEvents = 0;
    SequencerFrame frame;

    sequencer.process (host, pattern, params, numSamples,
                       events, kMaxEventsPerBlock, numEvents, frame);

    // --- 2. Live MIDI (sample-accurate; same render path as the sequencer) --
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            const int voiceIndex = message.getNoteNumber() - kBaseMidiNote;
            if (voiceIndex >= 0 && voiceIndex < kNumVoices && numEvents < kMaxEventsPerBlock)
            {
                SequencedEvent event;
                event.voice = voiceIndex;
                event.sampleOffset = juce::jlimit (0, numSamples - 1, metadata.samplePosition);
                event.trigger.velocity = juce::jlimit (0.02f, 1.0f, message.getFloatVelocity());
                event.trigger.seed = rng::hash ((std::uint32_t) params.global.seed,
                                                voiceIndex, metadata.samplePosition, 0x7e5);
                event.trigger.flags = (unsigned int) HitFlag::live;
                event.tickIndex = frame.tickIndex;
                events[numEvents++] = event;
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            voices.reset();
        }
    }

    // --- 3. Render with sample accuracy -------------------------------------
    // In-place insertion sort: deterministic, and unlike std::stable_sort it
    // cannot allocate a temporary buffer (which would violate the audio-thread
    // contract). Event counts per block are tiny.
    for (int i = 1; i < numEvents; ++i)
    {
        const SequencedEvent value = events[i];
        int j = i - 1;
        while (j >= 0
               && (events[j].sampleOffset > value.sampleOffset
                   || (events[j].sampleOffset == value.sampleOffset
                       && events[j].voice > value.voice)))
        {
            events[j + 1] = events[j];
            --j;
        }
        events[j + 1] = value;
    }

    int cursor = 0;
    int next = 0;

    while (cursor < numSamples)
    {
        while (next < numEvents && events[next].sampleOffset <= cursor)
        {
            const auto& event = events[next];
            triggerEvent (event, (event.trigger.flags & (unsigned int) HitFlag::live) != 0u);

            visual.lastVelocity[event.voice].store (event.trigger.velocity, std::memory_order_relaxed);
            visual.lastPitchSemis[event.voice].store (event.trigger.pitchSemis, std::memory_order_relaxed);
            visual.lastTimingMs[event.voice].store (event.timingDevMs, std::memory_order_relaxed);
            visual.lastDecayScale[event.voice].store (event.trigger.decayScale, std::memory_order_relaxed);
            visual.lastFlags[event.voice].store (event.trigger.flags, std::memory_order_relaxed);
            visual.triggerCount[event.voice].fetch_add (1u, std::memory_order_relaxed);
            ++next;
        }

        const int boundary = next < numEvents
                                 ? juce::jlimit (cursor + 1, numSamples, events[next].sampleOffset)
                                 : numSamples;
        voices.render (output, cursor, boundary - cursor);
        cursor = boundary;
    }

    // --- 4. The enormous glue machine ---------------------------------------
    master.process (output, numSamples);

    // --- 5. Telemetry -------------------------------------------------------
    float peakL = 0.0f, peakR = 0.0f;
    double sumL = 0.0, sumR = 0.0;
    const int leftChannel = 0;
    const int rightChannel = output.getNumChannels() > 1 ? 1 : 0;
    const auto* left = output.getReadPointer (leftChannel);
    const auto* right = output.getReadPointer (rightChannel);

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i];
        const float r = right[i];
        peakL = juce::jmax (peakL, std::abs (l));
        peakR = juce::jmax (peakR, std::abs (r));
        sumL += (double) l * l;
        sumR += (double) r * r;
    }

    auto toDb = [] (double linear)
    {
        return linear > 1.0e-7 ? (float) (20.0 * std::log10 (linear)) : -100.0f;
    };

    for (int v = 0; v < kNumVoices; ++v)
    {
        visual.voiceLevel[v].store (juce::jlimit (0.0f, 1.5f, voices.getEnvelopeLevel (v)),
                                    std::memory_order_relaxed);
        visual.laneStep[v].store (frame.laneStep[v], std::memory_order_relaxed);
        visual.laneAudible[v].store (pattern.laneAudible (v), std::memory_order_relaxed);
    }

    visual.playing.store (frame.playing, std::memory_order_relaxed);
    visual.hostSynced.store (frame.hostSynced, std::memory_order_relaxed);
    visual.personality.store (params.global.personality, std::memory_order_relaxed);
    visual.bpm.store (frame.bpm, std::memory_order_relaxed);
    visual.currentTick.store (frame.tickIndex, std::memory_order_relaxed);
    visual.gainReductionDb.store (master.getGainReductionDb(), std::memory_order_relaxed);
    visual.outPeakL.store (toDb (peakL), std::memory_order_relaxed);
    visual.outPeakR.store (toDb (peakR), std::memory_order_relaxed);
    visual.outRmsL.store (toDb (std::sqrt (sumL / (double) juce::jmax (1, numSamples))),
                          std::memory_order_relaxed);
    visual.outRmsR.store (toDb (std::sqrt (sumR / (double) juce::jmax (1, numSamples))),
                          std::memory_order_relaxed);
}

} // namespace klopgeest
