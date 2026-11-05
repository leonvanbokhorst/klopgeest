#pragma once

#include <cstdint>

#include "core/EngineParams.h"
#include "core/Random.h"
#include "dsp/voices/Voice.h"
#include "dsp/voices/VoiceUtils.h"

// -----------------------------------------------------------------------------
// Common machinery shared by all eight voices.
//
// VoiceBase owns the parts of the contract that must behave identically for
// every voice: parameter and trigger sanitising, deterministic per-hit random
// values, level/pan smoothing (click-free automation), the choke fade, the
// render loop and the "tail is silent -> inactive" bookkeeping.
//
// A concrete voice only implements the sound itself:
//   startVoice()          - set up envelopes/frequencies for the new hit
//   renderVoiceSample()   - one mono sample (pre level/pan), or
//   renderVoiceStereo()   - one stereo pair for voices that need width
//   voiceFinished()       - true once the tail is inaudible
//   voiceEnvelope()       - 0..1 amplitude for the GUI meter
// -----------------------------------------------------------------------------
namespace klopgeest
{

class VoiceBase : public DrumVoice
{
public:
    void prepare (double newSampleRate, int maxBlockSize) override;
    void reset() override;
    void setParams (const VoiceParams& newParams) override;
    void trigger (const VoiceTrigger& newTrigger) override;
    void choke (float releaseSeconds) override;
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples) override;

    float getEnvelopeLevel() const override;
    bool  isActive() const override { return active; }

protected:
    // --- to be implemented by the concrete voice -----------------------------
    virtual void prepareVoice() {}
    virtual void resetVoice() {}
    virtual void paramsChanged() {}
    virtual void startVoice() = 0;
    virtual float renderVoiceSample() = 0;
    virtual void renderVoiceStereo (float& left, float& right)
    {
        const float s = renderVoiceSample();
        left = s;
        right = s;
    }
    virtual bool voiceFinished() const = 0;
    virtual float voiceEnvelope() const = 0;

    /** Choke release time constant; hats override this for their release feel. */
    virtual float chokeTimeConstant (float releaseSeconds) const noexcept;

    /** Constant output trim used to balance the voices against each other. */
    virtual float outputTrim() const noexcept { return 1.0f; }

    // --- deterministic per-hit values ---------------------------------------
    /** Uniform [0,1) derived from the hit seed. Stable for a given seed. */
    float randomUnit (int salt) const noexcept
    {
        return rng::unit (hitSeed, salt, 0x51, 0x11);
    }

    float randomBipolar (int salt) const noexcept
    {
        return rng::bipolar (hitSeed, salt, 0x51, 0x11);
    }

    /** Applies the voice's (params.drive * trigger.driveScale) saturation. */
    float driveSignal (float x) const noexcept
    {
        return vutil::saturate (x, driveAmount);
    }

    // --- shared state visible to voices --------------------------------------
    double sampleRate = 48000.0;
    double inverseSampleRate = 1.0 / 48000.0;
    VoiceParams params;

    // Sanitised per-hit values from the last trigger().
    float velocity = 1.0f;
    float pitchRatio = 1.0f;      // combined voice tuning + per-hit deviation
    float decayScale = 1.0f;
    float transientScale = 1.0f;
    float toneOffset = 0.0f;
    float driveScale = 1.0f;
    float driveAmount = 0.15f;    // 0..1, mapped from params.drive * driveScale
    float hitVelocityGain = 1.0f;
    std::uint32_t hitSeed = 0;

    /** params.tone plus the per-hit tone offset, clamped to 0..1. */
    float effectiveTone() const noexcept
    {
        return vutil::clampf (params.tone + toneOffset, 0.0f, 1.0f);
    }

private:
    bool active = false;
    bool prepared = false;

    // Smoothed output level / pan (click-free parameter changes).
    float gainLTarget = 0.0f, gainRTarget = 0.0f;
    float gainL = 0.0f, gainR = 0.0f;
    float gainCoeff = 1.0f;

    // Choke fade.
    float chokeGain = 1.0f;
    float chokeCoeff = 0.0f;
    bool choking = false;

    void updateTargetGains() noexcept;
};

} // namespace klopgeest
