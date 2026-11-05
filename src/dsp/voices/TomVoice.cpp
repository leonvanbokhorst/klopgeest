#include "dsp/voices/VoiceBase.h"
#include "dsp/voices/VoiceConstructors.h"
#include "dsp/voices/VoiceUtils.h"

#include <cmath>

// -----------------------------------------------------------------------------
// TOM
//
// Tuned membrane: fundamental + 1.59x mode, a pitch bend that falls into the
// note, and a band-passed skin/noise excitation. specialA = BEND (how far the
// pitch drops) and specialB = SKIN (tension / transient colour).
// -----------------------------------------------------------------------------
namespace klopgeest
{
namespace
{

using namespace vutil;

class TomVoice final : public VoiceBase
{
public:
    void prepareVoice() override
    {
        bodyDamp.set (11000.0f, sampleRate);
        skinBp.set (600.0f, 1.3f, sampleRate);
        skinHp.set (3500.0f, 0.7f, sampleRate);
    }

    void resetVoice() override
    {
        bodyOsc.reset();
        mode2Osc.reset();
        bodyEnv.value = 0.0f;
        mode2Env.value = 0.0f;
        skinEnv.value = 0.0f;
        clickEnv.value = 0.0f;
        pitchSweep = 0.0f;
        noise.seed (0x3456789au);
        bodyDamp.reset();
        skinBp.reset();
        skinHp.reset();
        dc.reset();
    }

    void startVoice() override
    {
        const float tone = effectiveTone();

        const float tail = clampf (decaySeconds (params.decay, 0.10f, 1.05f) * decayScale,
                                   0.04f, 4.0f);
        bodyEnv.setDecay (tail, (double) sampleRate);
        bodyEnv.start (1.0f);

        mode2Env.setDecay (tail * 0.55f, (double) sampleRate);
        mode2Env.start (0.45f);

        baseFreq = clampf (112.0f * pitchRatio, 30.0f, 750.0f);
        mode2Ratio = 1.56f + 0.10f * params.specialB; // skin tension shifts the mode

        // BEND: the drum head starts sharp and settles into the note.
        const float bendSemis = 2.0f + 11.0f * params.specialA + 3.0f * velocity;
        startRatio = semitoneRatio (bendSemis);
        pitchSweep = 1.0f;
        pitchSweepCoeff = std::exp (-1.0f
                                    / ((0.015f + 0.070f * params.specialA)
                                       * (float) sampleRate));

        bodyOsc.reset (randomUnit (1));
        mode2Osc.reset (randomUnit (2));
        noise.seed (hitSeed ^ 0x3456789au);

        // SKIN: band-passed noise struck near the membrane resonance.
        const float skinAmp = clampf ((0.20f + 0.90f * params.transient)
                                          * clampf (transientScale, 0.0f, 3.0f),
                                      0.0f, 2.6f)
                              * (0.4f + 0.6f * velocity);
        skinEnv.setDecay (0.014f + 0.045f * (1.0f - tone), (double) sampleRate);
        skinEnv.start (skinAmp);

        const float skinCentre = clampf (baseFreq * (2.0f + 3.0f * tone)
                                             * (0.9f + 0.6f * params.specialB),
                                         100.0f, 9000.0f);
        skinBp.set (skinCentre, 1.3f, sampleRate);
        skinHp.set (clampf (3500.0f + 2500.0f * tone, 1200.0f, 12000.0f), 0.7f, sampleRate);

        // TONE damps or opens the body: dark and rounded at 0, open and
        // stick-forward at 1.
        bodyDamp.set (clampf (3000.0f + 9000.0f * tone, 900.0f, 17000.0f), sampleRate);

        clickEnv.setDecay (0.0018f + 0.004f * (1.0f - tone), (double) sampleRate);
        clickEnv.start (clampf (0.12f + 0.45f * params.transient, 0.0f, 0.9f)
                        * clampf (transientScale, 0.0f, 2.5f)
                        * (0.3f + 0.7f * velocity));

        velGain = hitVelocityGain;
    }

    float renderVoiceSample() override
    {
        const float sweep = pitchSweep;
        pitchSweep = flushDenorm (pitchSweep * pitchSweepCoeff);
        const float bend = 1.0f + (startRatio - 1.0f) * sweep;

        const float inc = clampf (baseFreq * bend * (float) inverseSampleRate, 0.0f, 0.45f);
        const float inc2 = clampf (baseFreq * mode2Ratio * bend * (float) inverseSampleRate,
                                   0.0f, 0.45f);

        const float body = bodyOsc.process (inc) * bodyEnv.process();
        const float mode2 = mode2Osc.process (inc2) * mode2Env.process();

        const float n = noise.nextBipolar();
        const float skin = skinBp.process (n, Svf::Mode::bandpass) * skinEnv.process();
        const float click = skinHp.process (n, Svf::Mode::highpass) * clickEnv.process();

        float x = (body + 0.32f * mode2) * velGain;
        x += (skin + click * 0.8f) * velGain;

        x = bodyDamp.process (x);
        x = driveSignal (x);
        x = dc.process (x);
        return x;
    }

    float outputTrim() const noexcept override { return 1.5f; }

    bool voiceFinished() const override
    {
        return bodyEnv.silent (2.0e-4f)
            && mode2Env.silent (2.0e-4f)
            && skinEnv.silent (2.0e-4f)
            && clickEnv.silent (2.0e-4f);
    }

    float voiceEnvelope() const override
    {
        return bodyEnv.value;
    }

private:
    SineOsc bodyOsc, mode2Osc;
    ExpEnv bodyEnv, mode2Env, skinEnv, clickEnv;
    Svf skinBp, skinHp;
    OnePoleLP bodyDamp;
    DCBlocker dc;
    Noise noise;

    float baseFreq = 112.0f;
    float mode2Ratio = 1.59f;
    float startRatio = 1.0f;
    float pitchSweep = 0.0f;
    float pitchSweepCoeff = 0.0f;
    float velGain = 1.0f;
};

} // namespace

std::unique_ptr<DrumVoice> createTomVoice()
{
    return std::make_unique<TomVoice>();
}

} // namespace klopgeest
