#include "dsp/voices/VoiceBase.h"
#include "dsp/voices/VoiceConstructors.h"
#include "dsp/voices/VoiceUtils.h"

#include <cmath>

// -----------------------------------------------------------------------------
// HI-HATS (closed + open)
//
// 808-inspired metallic engine: six naive square oscillators tuned to the
// classic inharmonic ratios, band/high-passed, with a noise layer for sizzle.
// specialA = METAL (oscillator bank <-> noise), specialB = SIZZLE on the closed
// hat and CHOKE/release character on the open hat.
// -----------------------------------------------------------------------------
namespace klopgeest
{
namespace
{

using namespace vutil;

class HatVoice final : public VoiceBase
{
public:
    static constexpr int kNumOsc = 6;

    explicit HatVoice (bool isOpen) : openHat (isOpen) {}

    void prepareVoice() override
    {
        trimGain = openHat ? 3.8f : 6.0f;
    }

    void resetVoice() override
    {
        for (auto& osc : oscs)
            osc.reset();
        ampEnv.value = 0.0f;
        clickEnv.value = 0.0f;
        sizzleEnv.value = 0.0f;
        bandpass.reset();
        highpass.reset();
        clickHp.reset();
        sizzleHp.reset();
        dc.reset();
        noise.seed (openHat ? 0x6789abcdu : 0x789abcdeu);
        sizzleNoise.seed (openHat ? 0x89abcde1u : 0x9abcde12u);
    }

    void startVoice() override
    {
        const float tone = effectiveTone();

        baseFreq = clampf (205.3f * pitchRatio, 50.0f, 1250.0f);
        for (int i = 0; i < kNumOsc; ++i)
        {
            increments[i] = clampf (baseFreq * ratios[i] * (float) inverseSampleRate, 0.0f, 0.45f);
            oscs[i].reset (randomUnit (i + 1));
        }

        float tau;
        if (openHat)
            tau = decaySeconds (params.decay, 0.10f, 0.95f);
        else
            tau = decaySeconds (params.decay, 0.014f, 0.13f);
        tau = clampf (tau * decayScale, 0.008f, 3.0f);

        ampEnv.setDecay (tau, (double) sampleRate);
        ampEnv.start (1.0f);

        // METAL: blend the oscillator bank against the noise layer.
        metallic = 0.30f + 0.70f * params.specialA;
        noiseLevel = 0.55f + 0.30f * (1.0f - params.specialA);

        // SIZZLE (closed hat) or CHOKE/release character (open hat). On the
        // open hat, specialB also lengthens the natural tail and adds air.
        if (openHat)
        {
            tau *= (0.75f + 0.55f * params.specialB);
            ampEnv.setDecay (tau, (double) sampleRate);
            sizzleLevel = 0.12f + 0.45f * params.specialB;
        }
        else
        {
            sizzleLevel = 0.10f + 0.75f * params.specialB;
        }
        sizzleEnv.setDecay (tau * 0.8f, (double) sampleRate);
        sizzleEnv.start (1.0f);

        noise.seed (hitSeed ^ 0x5eed1234u);
        sizzleNoise.seed (hitSeed ^ 0x77ab0001u);

        // Filter tuning tracks the source pitch so the character is consistent
        // across the PITCH knob.
        bandpass.set (clampf ((6200.0f + 4200.0f * tone) * (0.70f + 0.30f * pitchRatio),
                              1500.0f, 17000.0f),
                      1.15f, sampleRate);
        highpass.set (clampf ((5200.0f + 2600.0f * tone) * (0.60f + 0.40f * pitchRatio),
                              800.0f, 16000.0f),
                      0.8f, sampleRate);
        clickHp.set (clampf (6500.0f + 3500.0f * tone, 1500.0f, 17000.0f), 0.7f, sampleRate);
        sizzleHp.set (clampf (9000.0f + 3000.0f * tone, 4000.0f, 18000.0f), 0.7f, sampleRate);

        clickEnv.setDecay (0.0012f + 0.0025f * (1.0f - tone), (double) sampleRate);
        clickEnv.start (clampf ((0.10f + 0.60f * params.transient)
                                    * clampf (transientScale, 0.0f, 2.5f),
                                0.0f, 1.6f)
                        * (0.4f + 0.6f * velocity));

        velGain = hitVelocityGain;
    }

    float chokeTimeConstant (float releaseSeconds) const noexcept override
    {
        if (openHat)
        {
            // CHOKE: short and gated at 0, longer/smoother release at 1.
            const float t = releaseSeconds * (0.35f + 2.2f * params.specialB);
            return clampf (t, 0.004f, 0.06f);
        }
        return clampf (releaseSeconds, 0.003f, 0.02f);
    }

    float renderVoiceSample() override
    {
        float metal = 0.0f;
        for (int i = 0; i < kNumOsc; ++i)
            metal += oscs[i].process (increments[i]);
        metal *= 1.0f / (float) kNumOsc;

        const float n = noise.nextBipolar();
        const float mix = metal * metallic * 2.2f + n * noiseLevel * 0.9f;

        float shaped = bandpass.process (mix, Svf::Mode::bandpass);
        shaped = highpass.process (shaped, Svf::Mode::highpass);

        const float sizzle = sizzleHp.process (sizzleNoise.nextBipolar(), Svf::Mode::highpass)
                             * sizzleEnv.process() * sizzleLevel;
        const float click = clickHp.process (n, Svf::Mode::highpass) * clickEnv.process() * 0.8f;

        float x = shaped * ampEnv.process() * velGain;
        x += (sizzle + click) * velGain;

        x = driveSignal (x);
        x = dc.process (x);
        return x * trimGain;
    }

    bool voiceFinished() const override
    {
        return ampEnv.silent (2.0e-4f)
            && clickEnv.silent (2.0e-4f)
            && sizzleEnv.silent (2.0e-4f);
    }

    float voiceEnvelope() const override
    {
        return ampEnv.value;
    }

private:
    static constexpr float ratios[kNumOsc] =
    {
        1.0000f, 1.4827f, 1.5562f, 1.8773f, 1.9611f, 2.5461f
    };

    bool openHat = false;

    SquareOsc oscs[kNumOsc];
    float increments[kNumOsc] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    Noise noise, sizzleNoise;
    Svf bandpass, highpass, clickHp, sizzleHp;
    ExpEnv ampEnv, clickEnv, sizzleEnv;
    DCBlocker dc;

    float baseFreq = 205.3f;
    float metallic = 0.65f;
    float noiseLevel = 0.7f;
    float sizzleLevel = 0.4f;
    float velGain = 1.0f;
    float trimGain = 3.0f;
};

} // namespace

std::unique_ptr<DrumVoice> createClosedHatVoice()
{
    return std::make_unique<HatVoice> (false);
}

std::unique_ptr<DrumVoice> createOpenHatVoice()
{
    return std::make_unique<HatVoice> (true);
}

} // namespace klopgeest
