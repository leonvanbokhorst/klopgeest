#pragma once

#include <array>
#include <memory>

#include "dsp/voices/Voice.h"
#include "core/EngineParams.h"

// -----------------------------------------------------------------------------
// Owns the eight voices, applies the choke groups and mixes sample-accurate
// triggers into the output buffer.
// -----------------------------------------------------------------------------
namespace klopgeest
{

class VoiceManager
{
public:
    VoiceManager();
    ~VoiceManager();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Block-rate parameter update (also forwards voice params to the engines). */
    void setParams (const EngineParams& params);

    /** Sample-accurate trigger. Applies choke groups (CH -> OH). */
    void trigger (int voiceIndex, const VoiceTrigger& trigger);

    /** Renders [startSample, startSample + numSamples) from all active voices. */
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples);

    float getEnvelopeLevel (int voiceIndex) const;
    bool  isActive (int voiceIndex) const;

private:
    std::array<std::unique_ptr<DrumVoice>, kNumVoices> voices;
    bool prepared = false;
};

} // namespace klopgeest
