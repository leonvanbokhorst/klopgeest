#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "core/EngineParams.h"
#include "core/Pattern.h"
#include "core/VisualState.h"
#include "dsp/master/GlueMachine.h"
#include "dsp/voices/VoiceManager.h"
#include "sequencer/Sequencer.h"

// -----------------------------------------------------------------------------
// The audio engine. Owns voices, sequencer and the master glue machine.
//
// Contract: prepare()/reset() and process() are audio-thread safe after
// prepare(); process() never allocates, locks, throws or touches the file
// system. The processor hands in an immutable parameter snapshot and the
// current pattern each block.
// -----------------------------------------------------------------------------
namespace klopgeest
{

class Engine
{
public:
    static constexpr int kMaxEventsPerBlock = 512;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Plays/stop the internal free-running clock (standalone + live use). */
    void setRunning (bool shouldRun) noexcept;
    bool isRunning() const noexcept;

    /** Audio thread: parameter snapshot for this block. */
    void setParams (const EngineParams& newParams) noexcept;

    void process (juce::AudioBuffer<float>& output,
                  const juce::MidiBuffer& midi,
                  int numSamples,
                  const PatternData& pattern,
                  const HostTransport& host,
                  VisualState& visual);

    double getSampleRate() const noexcept { return sampleRate; }
    const EngineParams& getParams() const noexcept { return params; }

private:
    void triggerEvent (const SequencedEvent& event, bool live);
    void publishVisual (const SequencerFrame& frame, const HostTransport& host, int numSamples, VisualState& visual);

    double sampleRate = 48000.0;
    int maxBlock = 512;
    EngineParams params;
    VoiceManager voices;
    Sequencer sequencer;
    GlueMachine master;
    bool prepared = false;
    unsigned int liveCounter = 0;
};

} // namespace klopgeest
