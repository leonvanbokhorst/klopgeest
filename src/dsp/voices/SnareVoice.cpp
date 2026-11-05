#include "dsp/voices/VoiceBase.h"
#include "dsp/voices/VoiceConstructors.h"
#include "dsp/voices/VoiceUtils.h"

#include <cmath>

// -----------------------------------------------------------------------------
// SNARE
//
// Two tuned body modes (membrane fundamental + 1.72x mode), a short pitch snap,
// band-passed noise and a separate snare-wire layer (specialA = WIRES).
// specialB = NOISE shifts the noise from dark/papery to bright/splashy.
// -----------------------------------------------------------------------------
namespace klopgeest
{
namespace
{

using namespace vutil;

class SnareVoice final : public VoiceBase
{
public:
    void prepareVoice() override
    {
        bodyLP.set (11000.0f, sampleRate);
        noiseBp.set (2600.0f, 1.0f, sampleRate);
        noiseHp.set (5500.0f, 0.7f, sampleRate);
        wireBp.set (7200.0f, 1.5f, sampleRate);
        clickHp.set (4200.0f, 0.7f, sampleRate);
    }

    void resetVoice() override
    {
        mode1Osc.reset();
        mode2Osc.reset();
        snapOsc.reset();
        mode1Env.value = 0.0f;
        mode2Env.value = 0.0f;
        noiseEnv.value = 0.0f;
        wireEnv.value = 0.0f;
        clickEnv.value = 0.0f;
        snapEnv.value = 0.0f;
        pitchSweep = 0.0f;
        noise.seed (0x23456789u);
        wireNoise.seed (0x3456789au);
        noiseBp.reset();
        noiseHp.reset();
        wireBp.reset();
        clickHp.reset();
        bodyLP.reset();
        dc.reset();
    }

    void startVoice() override
    {
        const float tone = effectiveTone();

        const float tail = clampf (decaySeconds (params.decay, 0.075f, 0.85f) * decayScale,
                                   0.03f, 3.0f);
        mode1Env.setDecay (tail, (double) sampleRate);
        mode1Env.start (1.0f);
        mode2Env.setDecay (tail * 0.68f, (double) sampleRate);
        mode2Env.start (0.8f);

        noiseEnv.setDecay (tail * (0.85f + 0.5f * (1.0f - tone)), (double) sampleRate);
        noiseEnv.start (1.0f);

        // Snare wires ring a little longer and get their own colour.
        wireEnv.setDecay (tail * (0.45f + 0.9f * params.specialA), (double) sampleRate);
        wireEnv.start (0.55f + 0.45f * params.specialA);

        const float clickTau = 0.0018f + 0.006f * (1.0f - tone);
        clickEnv.setDecay (clickTau, (double) sampleRate);
        clickEnv.start (clampf (transientScale * (0.15f + 0.7f * params.transient), 0.0f, 2.0f)
                        * (0.4f + 0.6f * velocity));

        snapEnv.setDecay (0.012f, (double) sampleRate);
        snapEnv.start (clampf (0.25f * params.transient * transientScale, 0.0f, 1.0f)
                       * (0.4f + 0.6f * velocity));

        baseFreq = clampf (185.0f * pitchRatio, 55.0f, 1100.0f);
        mode2Freq = baseFreq * 1.72f;

        startRatio = semitoneRatio (2.0f + 3.0f * velocity);
        pitchSweep = 1.0f;
        pitchSweepCoeff = std::exp (-1.0f / (0.012f * (float) sampleRate));

        mode1Osc.reset (randomUnit (1));
        mode2Osc.reset (randomUnit (2));
        snapOsc.reset (0.0f);

        noise.seed (hitSeed ^ 0x23456789u);
        wireNoise.seed (hitSeed ^ 0x9abc0001u);

        // specialB = NOISE: dark papery crack at 0, bright splash at 1.
        const float centre = clampf ((950.0f + 3600.0f * tone) * pitchRatio
                                         * (1.0f + 1.4f * params.specialB),
                                     300.0f, 16500.0f);
        const float q = 0.85f + 0.8f * (1.0f - params.specialB);
        noiseBp.set (centre, q, sampleRate);
        noiseHp.set (clampf ((2600.0f + 6500.0f * params.specialB) * pitchRatio, 700.0f, 17000.0f),
                     0.7f, sampleRate);
        wireBp.set (clampf ((5200.0f + 4500.0f * params.specialA) * pitchRatio, 1500.0f, 17500.0f),
                    1.5f, sampleRate);
        clickHp.set (clampf ((3000.0f + 4500.0f * tone) * (0.6f + 0.4f * pitchRatio),
                             900.0f, 17000.0f),
                     0.8f, sampleRate);

        velGain = hitVelocityGain;
    }

    float renderVoiceSample() override
    {
        const float sweep = pitchSweep;
        pitchSweep = flushDenorm (pitchSweep * pitchSweepCoeff);
        const float bend = 1.0f + (startRatio - 1.0f) * sweep;

        const float inc1 = clampf (baseFreq * bend * (float) inverseSampleRate, 0.0f, 0.45f);
        const float inc2 = clampf (mode2Freq * bend * (float) inverseSampleRate, 0.0f, 0.45f);

        const float m1 = mode1Osc.process (inc1) * mode1Env.process();
        const float m2 = mode2Osc.process (inc2) * mode2Env.process();
        const float snap = snapOsc.process (clampf (inc1 * 3.17f, 0.0f, 0.45f))
                           * snapEnv.process();

        const float n = noise.nextBipolar();
        const float noiseBand = noiseBp.process (n, Svf::Mode::bandpass);
        const float noiseHigh = noiseHp.process (n, Svf::Mode::highpass);
        const float noiseSig = (noiseBand + 0.8f * params.specialB * noiseHigh) * noiseEnv.process();

        const float wn = wireNoise.nextBipolar();
        const float wire = wireBp.process (wn, Svf::Mode::bandpass) * wireEnv.process();

        const float click = clickHp.process (n, Svf::Mode::highpass) * clickEnv.process();

        float x = (m1 + 0.55f * m2) * velGain;
        x += (noiseSig * 0.95f + wire * 0.55f + click * 0.8f) * velGain;
        x += snap * velGain;

        x = bodyLP.process (x);
        x = driveSignal (x);
        x = dc.process (x);
        return x;
    }

    bool voiceFinished() const override
    {
        return mode1Env.silent (2.0e-4f)
            && mode2Env.silent (2.0e-4f)
            && noiseEnv.silent (2.0e-4f)
            && wireEnv.silent (2.0e-4f)
            && clickEnv.silent (2.0e-4f)
            && snapEnv.silent (2.0e-4f);
    }

    float voiceEnvelope() const override
    {
        return mode1Env.value > noiseEnv.value ? mode1Env.value : noiseEnv.value;
    }

private:
    SineOsc mode1Osc, mode2Osc, snapOsc;
    ExpEnv mode1Env, mode2Env, noiseEnv, wireEnv, clickEnv, snapEnv;
    Svf noiseBp, noiseHp, wireBp, clickHp;
    OnePoleLP bodyLP;
    DCBlocker dc;
    Noise noise, wireNoise;

    float baseFreq = 185.0f;
    float mode2Freq = 320.0f;
    float startRatio = 1.0f;
    float pitchSweep = 0.0f;
    float pitchSweepCoeff = 0.0f;
    float velGain = 1.0f;
};

} // namespace

std::unique_ptr<DrumVoice> createSnareVoice()
{
    return std::make_unique<SnareVoice>();
}

} // namespace klopgeest
