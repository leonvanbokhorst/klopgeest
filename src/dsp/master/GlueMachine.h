#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

#include "core/EngineParams.h"

// -----------------------------------------------------------------------------
// The enormous analog-inspired glue machine.
//
// Not a transparent compressor: soft-knee bus compression with a sidechain HPF,
// blendable peak/RMS detection, parallel mixture, pre/post soft saturation, a
// performance ceiling limiter and a smooth gain-reduction meter.
//
// Implementation is pimpl'd so the workstream can iterate on the DSP without
// touching the engine's contract.
// -----------------------------------------------------------------------------
namespace klopgeest
{

class GlueMachine
{
public:
    GlueMachine();
    ~GlueMachine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Block-rate parameters (the engine also applies them at block rate). */
    void setParams (const MasterParams& params);

    /** Processes in place, [0, numSamples). Stereo. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

    /** Metering: current gain reduction in dB (positive number = reduction). */
    float getGainReductionDb() const noexcept;

    /** Peak of the last processed block, dBFS (<= -100 for silence). */
    float getOutputPeakDb() const noexcept;

    /** RMS of the last processed block, dBFS. */
    float getOutputRmsDb() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace klopgeest
