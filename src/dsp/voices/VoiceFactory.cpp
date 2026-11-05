// -----------------------------------------------------------------------------
// Temporary single-oscillator voice used until the voice workstream lands the
// eight real synthesis engines. It keeps the instrument audible and the build
// green while the DSP is being written; it is replaced wholesale by the voice
// implementation files in this directory.
// -----------------------------------------------------------------------------
#include "dsp/voices/Voice.h"

#include <cmath>

#include "core/Random.h"

namespace klopgeest
{

namespace
{
class PlaceholderVoice final : public DrumVoice
{
public:
    explicit PlaceholderVoice (int voiceIndex) : index (voiceIndex) {}

    void prepare (double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        envelope = 0.0f;
        noise = 0.0f;
        active = false;
    }

    void setParams (const VoiceParams& newParams) override { params = newParams; }

    void trigger (const VoiceTrigger& trigger) override
    {
        baseFrequency = 45.0 * std::pow (2.0, (double) (trigger.pitchSemis + params.pitchSemis) / 12.0)
                        * (1.0 + 0.6 * (double) index);
        envelope = juce::jlimit (0.05f, 1.0f, trigger.velocity);
        decaySeconds = 0.08 + 0.55 * (double) params.decay * (double) trigger.decayScale;
        phase = 0.0;
        active = true;
    }

    void choke (float) override { envelope = 0.0f; active = false; }

    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (! active)
            return;

        const float gain = juce::Decibels::decibelsToGain (params.levelDb);
        const float pan = juce::jlimit (-1.0f, 1.0f, params.pan);
        const float gl = gain * std::sqrt (0.5f * (1.0f - pan));
        const float gr = gain * std::sqrt (0.5f * (1.0f + pan));

        const double decayCoeff = std::exp (-1.0 / (sampleRate * decaySeconds));
        const double increment = 2.0 * juce::MathConstants<double>::pi * baseFrequency / sampleRate;

        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = (float) std::sin (phase) * envelope + noise * 0.05f;
            phase += increment;
            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
            envelope = (float) (envelope * decayCoeff);
            noise = 0.0f;

            if (out.getNumChannels() > 0)
                out.addSample (0, startSample + i, sample * gl);
            if (out.getNumChannels() > 1)
                out.addSample (1, startSample + i, sample * gr);

            if (envelope < 0.0001f)
            {
                active = false;
                break;
            }
        }
    }

    float getEnvelopeLevel() const override { return envelope; }
    bool isActive() const override { return active; }

private:
    int index = 0;
    double sampleRate = 48000.0;
    VoiceParams params;
    double phase = 0.0;
    double baseFrequency = 60.0;
    double decaySeconds = 0.3;
    float envelope = 0.0f;
    float noise = 0.0f;
    bool active = false;
};
} // namespace

std::unique_ptr<DrumVoice> createVoice (int voiceIndex)
{
    return std::make_unique<PlaceholderVoice> (juce::jlimit (0, kNumVoices - 1, voiceIndex));
}

} // namespace klopgeest
