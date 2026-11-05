#include "dsp/voices/VoiceBase.h"
#include "dsp/voices/VoiceConstructors.h"
#include "dsp/voices/VoiceUtils.h"

#include <cmath>

// -----------------------------------------------------------------------------
// KICK
//
// Pitch-enveloped sine body (fast drop), beater click, saturated knock layer,
// optional subharmonic (specialA = SUB) and richer body character
// (specialB = BODY). Analog-inspired: everything runs through a gentle
// transformer-ish saturation and a DC blocker.
// -----------------------------------------------------------------------------
namespace klopgeest
{
namespace
{

using namespace vutil;

class KickVoice final : public VoiceBase
{
public:
    void prepareVoice() override
    {
        warmLP.set (13500.0f, sampleRate);
        clickFilter.set (2600.0f, 0.9f, sampleRate);
        knockFilter.set (180.0f, 1.2f, sampleRate);
        dc.reset();
    }

    void resetVoice() override
    {
        bodyOsc.reset();
        harmOsc.reset();
        subOsc.reset();
        bodyEnv.value = 0.0f;
        subEnv.value = 0.0f;
        knockEnv.value = 0.0f;
        clickEnv.value = 0.0f;
        pitchSweep = 0.0f;
        noise.seed (0x12345678u);
        clickFilter.reset();
        knockFilter.reset();
        warmLP.reset();
        dc.reset();
    }

    void startVoice() override
    {
        const float tone = effectiveTone();

        // Body tail: 0.16 s (tight) .. 1.35 s (boomy), scaled by the groove's
        // per-hit decay deviation.
        const float tail = clampf (decaySeconds (params.decay, 0.16f, 1.35f) * decayScale,
                                   0.05f, 4.0f);
        bodyEnv.setDecay (tail, (double) sampleRate);
        bodyEnv.start (1.0f);

        subEnv.setDecay (tail * (0.85f + 0.4f * params.specialA), (double) sampleRate);
        subEnv.start (1.0f);

        baseFreq = clampf (46.0f * pitchRatio, 16.0f, 330.0f);

        // The classic fast pitch drop: 24-42 semitones in ~15-40 ms.
        const float dropSemis = 26.0f + 8.0f * velocity;
        startRatio = semitoneRatio (dropSemis);
        pitchSweep = 1.0f;
        const float sweepTime = 0.012f + 0.028f * tone;
        pitchSweepCoeff = std::exp (-1.0f / (sweepTime * (float) sampleRate));

        bodyOsc.reset (randomUnit (1) * 0.5f);
        harmOsc.reset (randomUnit (2) * 0.5f);
        subOsc.reset (0.0f);
        noise.seed (hitSeed ^ 0x9e3779b9u);

        // Beater click.
        const float clickTau = 0.0015f + 0.006f * (1.0f - tone);
        clickEnv.setDecay (clickTau, (double) sampleRate);
        const float clickLevel = clampf (transientScale * (0.10f + 0.75f * params.transient),
                                         0.0f, 2.5f)
                                 * (0.35f + 0.65f * velocity);
        clickEnv.start (clickLevel);
        clickFilter.set (clampf (1400.0f + 4200.0f * tone, 300.0f, 16000.0f), 0.9f, sampleRate);

        // Low-mid "knock" that fleshes out the body; specialB = BODY.
        knockEnv.setDecay (0.025f + 0.06f * params.specialB, (double) sampleRate);
        const float knockLevel = clampf (0.10f + 0.45f * params.specialB, 0.0f, 0.7f)
                                 * (0.5f + 0.5f * velocity)
                                 * clampf (transientScale, 0.0f, 2.0f) * 0.6f;
        knockEnv.start (knockLevel);
        knockFilter.set (clampf (140.0f + 200.0f * params.specialB, 60.0f, 900.0f), 1.3f, sampleRate);

        // TONE opens the body/click damping: dark and round at 0, bright and
        // beater-forward at 1.
        warmLP.set (clampf (2200.0f + 11500.0f * tone, 800.0f, 18000.0f), sampleRate);

        subLevel = 0.55f * params.specialA;
        velGain = hitVelocityGain;
    }

    float renderVoiceSample() override
    {
        const float sweep = pitchSweep;
        pitchSweep = flushDenorm (pitchSweep * pitchSweepCoeff);

        const float freq = baseFreq * (1.0f + (startRatio - 1.0f) * sweep);
        const float inc = clampf (freq * (float) inverseSampleRate, 0.0f, 0.45f);

        const float body = bodyOsc.process (inc);
        const float harm = harmOsc.process (inc * 2.0f);
        const float bodyEnvNow = bodyEnv.process();

        const float n = noise.nextBipolar();
        const float knock = knockFilter.process (n, Svf::Mode::bandpass) * knockEnv.process();
        const float click = clickFilter.process (n, Svf::Mode::bandpass) * clickEnv.process();

        const float subInc = clampf (baseFreq * 0.5f * (float) inverseSampleRate, 0.0f, 0.45f);
        const float sub = subOsc.process (subInc) * subEnv.process() * subLevel;

        float x = (body + 0.26f * params.specialB * harm) * bodyEnvNow * velGain;
        x += (knock + click) * velGain;
        x += sub * velGain;

        x = driveSignal (x);
        x = warmLP.process (x);
        x = dc.process (x);
        return x;
    }

    bool voiceFinished() const override
    {
        return bodyEnv.silent (2.0e-4f)
            && subEnv.silent (2.0e-4f)
            && knockEnv.silent (2.0e-4f)
            && clickEnv.silent (2.0e-4f);
    }

    float voiceEnvelope() const override
    {
        return bodyEnv.value;
    }

private:
    SineOsc bodyOsc, harmOsc, subOsc;
    ExpEnv bodyEnv, subEnv, knockEnv, clickEnv;
    Svf knockFilter, clickFilter;
    OnePoleLP warmLP;
    DCBlocker dc;
    Noise noise;

    float baseFreq = 46.0f;
    float startRatio = 1.0f;
    float pitchSweep = 0.0f;
    float pitchSweepCoeff = 0.0f;
    float subLevel = 0.0f;
    float velGain = 1.0f;
};

} // namespace

std::unique_ptr<DrumVoice> createKickVoice()
{
    return std::make_unique<KickVoice>();
}

} // namespace klopgeest
