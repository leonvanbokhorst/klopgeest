#include "dsp/voices/VoiceBase.h"
#include "dsp/voices/VoiceConstructors.h"
#include "dsp/voices/VoiceUtils.h"

#include <cmath>

// -----------------------------------------------------------------------------
// ODD
//
// The controlled-instability voice: a saw/sine oscillator pair through a
// resonant feedback band-pass, ring-modulated and gated by a deterministic
// glitch schedule derived from the hit seed. specialA = CHAOS sets how violent
// and how frequent the pitch/level interruptions are; specialB = RING sets the
// ring-modulator depth and the resonator feedback. It stays musical because
// every jump is smoothed (glide) and bounded and the feedback path is saturated.
// -----------------------------------------------------------------------------
namespace klopgeest
{
namespace
{

using namespace vutil;

class OddVoice final : public VoiceBase
{
public:
    void prepareVoice() override
    {
        damp.set (9000.0f, sampleRate);
        clickHp.set (4200.0f, 0.7f, sampleRate);
    }

    void resetVoice() override
    {
        sawOsc.reset();
        sineOsc.reset();
        ringOsc.reset();
        resSvf.reset();
        ampEnv.value = 0.0f;
        clickEnv.value = 0.0f;
        noise.seed (0x1234abcd);
        clickNoise.seed (0xabc12345u);
        clickHp.reset();
        lastResonance = 0.0f;
        currentSemis = 0.0f;
        targetSemis = 0.0f;
        ampMod = 1.0f;
        sampleCounter = 0;
        windowIndex = -1;
        dc.reset();
    }

    void startVoice() override
    {
        const float tone = effectiveTone();

        const float tail = clampf (decaySeconds (params.decay, 0.07f, 0.95f) * decayScale,
                                   0.03f, 4.0f);
        ampEnv.setDecay (tail, (double) sampleRate);
        ampEnv.start (1.0f);

        chaos = params.specialA;
        baseFreq = clampf (128.0f * pitchRatio, 25.0f, 950.0f);

        glitchWindowSamples = (int) ((0.030f - 0.015f * chaos) * (float) sampleRate);
        if (glitchWindowSamples < 32)
            glitchWindowSamples = 32;

        currentSemis = 0.0f;
        targetSemis = 0.0f;
        glideCoeff = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
        ringRatio = 1.37f + 0.45f * params.specialB;
        feedback = clampf (0.10f + 0.65f * params.specialB, 0.0f, 0.78f);
        ringDepth = 0.15f + 0.75f * params.specialB;

        resSvf.set (clampf (baseFreq * (3.0f + 5.0f * tone), 200.0f, 14000.0f),
                    1.5f + 5.0f * params.specialB,
                    sampleRate);

        sawOsc.reset (randomUnit (1));
        sineOsc.reset (randomUnit (2));
        ringOsc.reset (randomUnit (3));
        noise.seed (hitSeed ^ 0x13579bdfu);

        clickEnv.setDecay (0.0025f + 0.003f * (1.0f - tone), (double) sampleRate);
        clickEnv.start (clampf ((0.10f + 0.70f * params.transient)
                                    * clampf (transientScale, 0.0f, 2.5f),
                                0.0f, 1.8f)
                        * (0.3f + 0.7f * velocity));
        clickNoise.seed (hitSeed ^ 0xabc12345u);
        clickHp.set (clampf (3500.0f + 4000.0f * tone, 1200.0f, 15000.0f), 0.7f, sampleRate);

        lastResonance = 0.0f;
        ampMod = 1.0f;
        sampleCounter = 0;
        windowIndex = -1;

        velGain = hitVelocityGain * (0.7f + 0.3f * velocity);
    }

    float renderVoiceSample() override
    {
        ++sampleCounter;
        const int window = sampleCounter / glitchWindowSamples;
        if (window != windowIndex)
        {
            windowIndex = window;
            scheduleGlitch (window);
        }

        // Glide toward the current glitch target so jumps never click.
        currentSemis += (targetSemis - currentSemis) * glideCoeff;
        ampMod += (1.0f - ampMod) * 0.0035f;
        ampMod = clampf (ampMod, 0.05f, 1.0f);

        const float freq = clampf (baseFreq * semitoneRatio (currentSemis),
                                   20.0f, (float) sampleRate * 0.45f);
        const float inc = clampf (freq * (float) inverseSampleRate, 0.0f, 0.45f);

        const float saw = sawOsc.process (inc);
        const float sine = sineOsc.process (clampf (inc * 2.0f, 0.0f, 0.45f));

        float osc = 0.55f * saw + 0.45f * sine;

        const float ring = ringOsc.process (clampf (inc * ringRatio, 0.0f, 0.45f));
        osc *= (1.0f - ringDepth) + ringDepth * ring;

        // Saturating resonant feedback path: unstable in a musical way, but the
        // tanh keeps it bounded and the DC blocker keeps it symmetric.
        const float in = fastTanh (osc * 1.15f + lastResonance * feedback);
        const float band = resSvf.process (in, Svf::Mode::bandpass);
        lastResonance = flushDenorm (band);

        const float click = clickHp.process (clickNoise.nextBipolar(), Svf::Mode::highpass)
                            * clickEnv.process();

        float x = (band * 2.2f + osc * 0.30f) * ampEnv.process() * velGain;
        x *= (0.30f + 0.70f * ampMod);
        x += click * velGain * 0.8f;

        x = driveSignal (x);
        x = damp.process (x);
        x = dc.process (x);
        return x;
    }

    float outputTrim() const noexcept override { return 0.35f; }

    bool voiceFinished() const override
    {
        return ampEnv.silent (2.0e-4f) && clickEnv.silent (2.0e-4f);
    }

    float voiceEnvelope() const override
    {
        return ampEnv.value;
    }

private:
    void scheduleGlitch (int window)
    {
        // Pure function of (hitSeed, window) - same seed, same performance,
        // independent of block size or timing.
        const std::uint32_t h = rng::hash (hitSeed, window, 0x0dd, 7);
        const float u = (float) (h & 0xffffu) / 65535.0f;

        if (u < 0.06f + 0.90f * chaos)
        {
            const float magnitude = 1.0f + 10.0f * chaos;
            targetSemis = rng::bipolar (hitSeed, window, 1, 3) * magnitude;

            if (rng::unit (hitSeed, window, 2, 5) < 0.30f + 0.40f * chaos)
                ampMod = 0.15f + 0.45f * (1.0f - chaos); // brief dropout
        }
        else
        {
            targetSemis = rng::bipolar (hitSeed, window, 1, 3)
                          * (0.15f + 1.5f * chaos);
        }
    }

    SawOsc sawOsc;
    SineOsc sineOsc, ringOsc;
    Svf resSvf;
    OnePoleLP damp;
    DCBlocker dc;
    ExpEnv ampEnv, clickEnv;
    Noise noise, clickNoise;
    Svf clickHp;

    float baseFreq = 128.0f;
    float chaos = 0.4f;
    float ringRatio = 1.5f;
    float ringDepth = 0.5f;
    float feedback = 0.4f;
    float currentSemis = 0.0f;
    float targetSemis = 0.0f;
    float glideCoeff = 0.01f;
    float ampMod = 1.0f;
    float lastResonance = 0.0f;
    float velGain = 1.0f;
    int sampleCounter = 0;
    int windowIndex = -1;
    int glitchWindowSamples = 1000;
};

} // namespace

std::unique_ptr<DrumVoice> createOddVoice()
{
    return std::make_unique<OddVoice>();
}

} // namespace klopgeest
