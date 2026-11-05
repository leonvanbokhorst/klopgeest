#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>
#include <memory>

#include "core/EngineParams.h"

// -----------------------------------------------------------------------------
// Drum voice contract.
//
// Every voice is a procedural one-shot synthesizer. Voices are owned by the
// VoiceManager, prepared once, triggered from the sequencer (or live MIDI) with
// sample offsets, and rendered into a shared stereo buffer. None of these calls
// may allocate, lock or block.
// -----------------------------------------------------------------------------
namespace klopgeest
{

struct VoiceTrigger
{
    float velocity       = 1.0f;   // 0..1, already accent/ghost shaped
    float pitchSemis     = 0.0f;   // deviation added to the voice's tuning
    float decayScale     = 1.0f;   // 1 = nominal
    float transientScale = 1.0f;   // 1 = nominal
    float toneOffset     = 0.0f;   // -0.5..+0.5 (dark <-> bright)
    float driveScale     = 1.0f;   // 1 = nominal
    std::uint32_t seed   = 0;      // deterministic per-hit variation
    unsigned int flags   = 0;      // HitFlag bitmask (for telemetry only)
};

class DrumVoice
{
public:
    virtual ~DrumVoice() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void setParams (const VoiceParams& params) = 0;
    virtual void trigger (const VoiceTrigger& trigger) = 0;

    /** Fast fade-out for choke groups (closed hat -> open hat). */
    virtual void choke (float releaseSeconds) = 0;

    /** Adds its output into `out` for [startSample, startSample + numSamples). */
    virtual void render (juce::AudioBuffer<float>& out, int startSample, int numSamples) = 0;

    /** 0..1 current amplitude envelope, for the GUI. */
    virtual float getEnvelopeLevel() const = 0;
    virtual bool  isActive() const = 0;
};

/** Factory implemented by the voice workstream. */
std::unique_ptr<DrumVoice> createVoice (int voiceIndex);

} // namespace klopgeest
