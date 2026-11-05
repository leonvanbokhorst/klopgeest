// -----------------------------------------------------------------------------
// Temporary sequencer stub. Replaced by the groove/sequencer workstream.
// -----------------------------------------------------------------------------
#include "sequencer/Sequencer.h"

namespace klopgeest
{

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
                         const PatternData&,
                         const EngineParams& params,
                         int numSamples,
                         SequencedEvent*,
                         int,
                         int& numOut,
                         SequencerFrame& frame)
{
    numOut = 0;
    lastHostSynced = host.available && host.playing;
    frame.playing = lastHostSynced || internalRunning.load (std::memory_order_relaxed);
    frame.hostSynced = lastHostSynced;
    frame.bpm = lastHostSynced ? (float) host.bpm : params.global.tempo;
    for (int v = 0; v < kNumVoices; ++v)
        frame.laneStep[v] = -1;

    if (! frame.playing)
        return;

    const double bpm = juce::jlimit (20.0, 999.0, (double) frame.bpm);
    const double samplesPerPpq = 60.0 * sampleRate / bpm;
    internalPpq += (double) numSamples / samplesPerPpq;
}

} // namespace klopgeest
