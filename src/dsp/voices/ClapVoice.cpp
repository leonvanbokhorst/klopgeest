#include "dsp/voices/VoiceBase.h"
#include "dsp/voices/VoiceConstructors.h"
#include "dsp/voices/VoiceUtils.h"

#include <cmath>

// -----------------------------------------------------------------------------
// CLAP
//
// Three to five band-passed noise bursts (the hand "multitrigger"), then a
// diffuse tail. specialA = DIFFUSION stretches and softens the tail;
// specialB = WIDTH controls how decorrelated the left/right noise is.
// -----------------------------------------------------------------------------
namespace klopgeest
{
namespace
{

using namespace vutil;

class ClapVoice final : public VoiceBase
{
public:
    static constexpr int kMaxBursts = 5;

    void prepareVoice() override
    {
        trimGain = 6.6f; // band-passed noise is quiet; make it sit in the mix
    }

    void resetVoice() override
    {
        bpL.reset();
        bpR.reset();
        burstEnv.value = 0.0f;
        tailEnv.value = 0.0f;
        dcL.reset();
        dcR.reset();
        noiseL.seed (0x456789abu);
        noiseR.seed (0x56789abc);
        burstsFired = 0;
        burstCount = 0;
        sampleCounter = 0;
        tailStarted = false;
        tailStartOffset = 0;
    }

    void startVoice() override
    {
        const float tone = effectiveTone();

        // 3..5 bursts, deterministic from the hit seed (with a nudge from
        // transient so hard hits feel a touch more articulated).
        burstCount = 3 + (int) (randomUnit (1) * 3.0f);
        if (params.transient > 0.8f && burstCount < kMaxBursts && randomUnit (7) > 0.5f)
            ++burstCount;
        burstCount = clampInt (burstCount, 3, kMaxBursts);

        int offset = 0;
        for (int b = 0; b < burstCount; ++b)
        {
            burstOffsets[b] = offset;
            burstLevels[b] = 0.82f + 0.18f * randomUnit (10 + b);

            const float spacing = (0.0095f - 0.0013f * (float) b)
                                  * (0.85f + 0.30f * randomUnit (20 + b));
            offset += (int) std::round (spacing * (float) sampleRate);
        }
        tailStartOffset = burstOffsets[burstCount - 1];

        burstsFired = 0;
        sampleCounter = 0;
        tailStarted = false;
        burstEnv.value = 0.0f;
        tailEnv.value = 0.0f;

        burstAmpScale = clampf ((0.55f + 0.65f * params.transient)
                                    * clampf (transientScale, 0.0f, 3.0f),
                                0.0f, 3.0f);
        burstTau = (0.0030f + 0.0100f * (1.0f - params.transient))
                   * clampf (1.15f - 0.22f * (transientScale - 1.0f), 0.35f, 1.6f);
        burstEnv.setDecay (burstTau, (double) sampleRate);
        // The first burst fires in the first rendered sample; give the meter a
        // value immediately so the GUI lights up on the trigger.
        burstEnv.start (0.5f * burstLevels[0] * burstAmpScale);

        const float tailTau = clampf (decaySeconds (params.decay, 0.030f, 0.55f) * decayScale
                                          * (0.60f + 1.4f * params.specialA),
                                      0.015f, 3.0f);
        tailEnv.setDecay (tailTau, (double) sampleRate);
        tailAmp = 0.30f + 0.55f * params.specialA;

        const float centre = clampf ((850.0f + 2000.0f * tone) * pitchRatio, 250.0f, 14500.0f);
        const float q = 1.5f - 0.6f * params.specialA;
        bpL.set (centre, q, sampleRate);
        bpR.set (centre * 1.015f, q * 0.96f, sampleRate);

        noiseL.seed (hitSeed ^ 0x456789abu);
        noiseR.seed (hitSeed ^ 0x89abcdefu);

        width = params.specialB;
        velGain = hitVelocityGain;
    }

    void renderVoiceStereo (float& outLeft, float& outRight) override
    {
        // Fire scheduled bursts / start the diffuse tail.
        while (burstsFired < burstCount && sampleCounter >= burstOffsets[burstsFired])
        {
            burstEnv.start (burstLevels[burstsFired] * burstAmpScale);
            ++burstsFired;
        }

        if (! tailStarted && sampleCounter >= tailStartOffset)
        {
            tailStarted = true;
            tailEnv.start (0.60f);
        }

        ++sampleCounter;

        const float nL = noiseL.nextBipolar();
        const float nR = noiseR.nextBipolar();
        const float filteredL = bpL.process (nL, Svf::Mode::bandpass);
        const float filteredR = bpR.process (nR, Svf::Mode::bandpass);

        const float burst = burstEnv.process();
        const float tail = tailEnv.process();
        const float gate = (burst + tail * tailAmp) * velGain * trimGain;

        const float mid = 0.5f * (filteredL + filteredR);
        float l = lerp (mid, filteredL, width) * gate;
        float r = lerp (mid, filteredR, width) * gate;

        l = driveSignal (l);
        r = driveSignal (r);
        outLeft = dcL.process (l);
        outRight = dcR.process (r);
    }

    float renderVoiceSample() override
    {
        float l = 0.0f, r = 0.0f;
        renderVoiceStereo (l, r);
        return 0.5f * (l + r);
    }

    bool voiceFinished() const override
    {
        return burstsFired >= burstCount
            && tailStarted
            && burstEnv.silent (2.0e-4f)
            && tailEnv.silent (2.0e-4f);
    }

    float voiceEnvelope() const override
    {
        if (burstsFired < burstCount)
            return 1.0f;
        return burstEnv.value > tailEnv.value ? burstEnv.value : tailEnv.value;
    }

private:
    static int clampInt (int v, int lo, int hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    Noise noiseL, noiseR;
    Svf bpL, bpR;
    ExpEnv burstEnv, tailEnv;
    DCBlocker dcL, dcR;

    int burstOffsets[kMaxBursts] = { 0, 0, 0, 0, 0 };
    float burstLevels[kMaxBursts] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    int burstCount = 3;
    int burstsFired = 0;
    int sampleCounter = 0;
    int tailStartOffset = 0;
    bool tailStarted = false;

    float burstAmpScale = 1.0f;
    float burstTau = 0.01f;
    float tailAmp = 0.5f;
    float width = 0.5f;
    float velGain = 1.0f;
    float trimGain = 6.6f;
};

} // namespace

std::unique_ptr<DrumVoice> createClapVoice()
{
    return std::make_unique<ClapVoice>();
}

} // namespace klopgeest
