#include "dsp/voices/VoiceBase.h"
#include "dsp/voices/VoiceConstructors.h"
#include "dsp/voices/VoiceUtils.h"

#include <algorithm>
#include <cmath>
#include <vector>

// -----------------------------------------------------------------------------
// PERC
//
// One morphing percussion voice. specialA = MODE sweeps continuously through
// click -> wood -> metal -> bell (equal-power crossfade between adjacent
// timbres), so it can be a rim click, a woodblock, a metallic tick or a small
// bell. TRANSIENT adds front-end articulation, TONE moves the filter centres,
// and specialB = SPACE mixes in a short stereo feedback-comb room.
// -----------------------------------------------------------------------------
namespace klopgeest
{
namespace
{

using namespace vutil;

class PercVoice final : public VoiceBase
{
public:
    static constexpr int kNumCombs = 4;
    static constexpr int kNumMetalOsc = 5;
    static constexpr int kNumBellPartials = 3;

    void prepareVoice() override
    {
        // 40 ms of delay memory is plenty; allocated only here, never in the
        // audio path.
        const size_t maxLength = (size_t) (sampleRate * 0.04) + 8;
        const float delaySeconds[kNumCombs] = { 0.011f, 0.013f, 0.019f, 0.023f };

        for (int k = 0; k < kNumCombs; ++k)
        {
            combBuffer[k].assign (maxLength, 0.0f);
            combDelay[k] = (size_t) std::min ((double) (maxLength - 2),
                                              (double) delaySeconds[k] * sampleRate);
            combDelay[k] = std::max<size_t> (8, combDelay[k]);
            combIdx[k] = 0;
            combDamp[k] = 0.0f;
        }
    }

    void resetVoice() override
    {
        clickBp.reset();
        woodBp.reset();
        metalBp.reset();
        metalHp.reset();

        clickEnv.value = 0.0f;
        woodEnv.value = 0.0f;
        woodBodyEnv.value = 0.0f;
        metalEnv.value = 0.0f;
        bellEnv.value = 0.0f;
        spaceTailEnv.value = 0.0f;

        woodOsc.reset();
        for (auto& osc : metalOscs)
            osc.reset();
        for (auto& osc : bellOscs)
            osc.reset();

        for (int k = 0; k < kNumCombs; ++k)
        {
            std::fill (combBuffer[k].begin(), combBuffer[k].end(), 0.0f);
            combIdx[k] = 0;
            combDamp[k] = 0.0f;
        }

        dc.reset();
        noiseClick.seed (0x56789abcu);
        noiseWood.seed (0x6789abcdu);
        noiseMetal.seed (0x789abcdeu);
    }

    void startVoice() override
    {
        const float tone = effectiveTone();
        const float A = params.specialA;
        const float B = params.specialB;

        baseFreq = clampf (340.0f * pitchRatio, 40.0f, 4000.0f);

        // Continuous click -> wood -> metal -> bell morph.
        const float pos = A * 3.0f;
        int zone = (int) pos;
        if (zone > 2)
            zone = 2;
        const float frac = pos - (float) zone;
        const float angle = frac * kPi * 0.5f;

        for (int k = 0; k < 4; ++k)
            zoneWeights[k] = 0.0f;
        zoneWeights[zone] = std::cos (angle);
        zoneWeights[zone + 1] += std::sin (angle);

        const float baseTail = clampf (decaySeconds (params.decay, 0.06f, 0.70f) * decayScale,
                                       0.02f, 3.0f);

        clickEnv.setDecay (0.0030f + 0.006f * (1.0f - tone), (double) sampleRate);
        clickEnv.start (1.0f);
        clickLevel = clampf ((0.15f + 0.75f * params.transient)
                                 * clampf (transientScale, 0.0f, 3.0f),
                             0.0f, 2.4f)
                     * (0.35f + 0.65f * velocity);
        clickBp.set (clampf (baseFreq * (6.0f + 3.0f * tone), 900.0f, 14000.0f), 0.9f, sampleRate);

        woodEnv.setDecay (baseTail * 0.35f, (double) sampleRate);
        woodEnv.start (1.0f);
        woodBodyEnv.setDecay (baseTail * 0.50f, (double) sampleRate);
        woodBodyEnv.start (1.0f);
        woodOsc.reset (randomUnit (1));
        woodBp.set (clampf (baseFreq * (2.5f + 1.5f * tone), 150.0f, 8000.0f), 5.0f, sampleRate);

        metalEnv.setDecay (baseTail * 1.6f, (double) sampleRate);
        metalEnv.start (1.0f);
        for (int i = 0; i < kNumMetalOsc; ++i)
        {
            metalOscs[i].reset (randomUnit (10 + i));
            metalInc[i] = clampf (baseFreq * metalRatios[i] * 1.1f
                                      * (float) inverseSampleRate,
                                  0.0f, 0.45f);
        }
        metalBp.set (clampf (baseFreq * (4.0f + 2.0f * tone), 400.0f, 14000.0f), 1.2f, sampleRate);
        metalHp.set (clampf (baseFreq * 2.0f, 400.0f, 10000.0f), 0.8f, sampleRate);

        bellEnv.setDecay (baseTail * 3.2f, (double) sampleRate);
        bellEnv.start (1.0f);
        for (int i = 0; i < kNumBellPartials; ++i)
        {
            bellOscs[i].reset (randomUnit (20 + i));
            bellInc[i] = clampf (baseFreq * 1.5f * bellRatios[i]
                                     * (float) inverseSampleRate,
                                 0.0f, 0.45f);
        }

        noiseClick.seed (hitSeed ^ 0x13579bdfu);
        noiseWood.seed (hitSeed ^ 0x2468ace0u);
        noiseMetal.seed (hitSeed ^ 0x0fedcba9u);

        // SPACE: short stereo feedback-comb room.
        spaceMix = 0.42f * B;
        combFeedback = clampf (0.30f + 0.45f * B, 0.0f, 0.80f);
        spaceTailEnv.setDecay (0.05f + 0.35f * B, (double) sampleRate);
        spaceTailEnv.start (1.0f);

        velGain = hitVelocityGain;
    }

    void renderVoiceStereo (float& outLeft, float& outRight) override
    {
        const float nClick = noiseClick.nextBipolar();
        const float nWood = noiseWood.nextBipolar();
        const float nMetal = noiseMetal.nextBipolar();

        const float click = clickBp.process (nClick, Svf::Mode::bandpass)
                            * clickEnv.process() * clickLevel;

        const float woodNoise = woodBp.process (nWood, Svf::Mode::bandpass)
                                * woodEnv.process() * zoneWeights[1] * 0.9f;
        const float woodBody = woodOsc.process (clampf (baseFreq * 2.0f
                                                            * (float) inverseSampleRate,
                                                        0.0f, 0.45f))
                               * woodBodyEnv.process() * zoneWeights[1] * 0.55f;

        float metal = 0.0f;
        for (int i = 0; i < kNumMetalOsc; ++i)
            metal += metalOscs[i].process (metalInc[i]);
        metal /= (float) kNumMetalOsc;
        metal = metalBp.process (metal, Svf::Mode::bandpass);
        metal = metalHp.process (metal, Svf::Mode::highpass);
        const float metalSig = metal * metalEnv.process() * zoneWeights[2] * 1.4f;

        float bell = 0.0f;
        for (int i = 0; i < kNumBellPartials; ++i)
            bell += bellOscs[i].process (bellInc[i]) * bellAmps[i];
        const float bellSig = bell * bellEnv.process() * zoneWeights[3] * 0.55f;

        float x = (click + woodNoise + woodBody + metalSig + bellSig) * velGain;
        x = driveSignal (x);

        // Feed the dirty signal into the room; the combs keep ringing after the
        // dry sound has decayed, which is what "space" should feel like.
        const float spaceIn = x * spaceMix;
        float combOut[kNumCombs];
        for (int k = 0; k < kNumCombs; ++k)
        {
            auto& buffer = combBuffer[k];
            const size_t length = combDelay[k];
            const size_t idx = combIdx[k];

            const float delayed = buffer[idx];
            combDamp[k] = flushDenorm (combDamp[k] + 0.45f * (delayed - combDamp[k]));
            buffer[idx] = flushDenorm (spaceIn + combDamp[k] * combFeedback);
            combIdx[k] = (idx + 1) % length;
            combOut[k] = delayed;
        }
        spaceTailEnv.process();

        const float spaceL = (combOut[0] + 0.5f * combOut[2]) * 0.45f;
        const float spaceR = (combOut[1] + 0.5f * combOut[3]) * 0.45f;

        outLeft = dc.process (x + spaceL);
        outRight = dc.process (x + spaceR);
    }

    float renderVoiceSample() override
    {
        float l = 0.0f, r = 0.0f;
        renderVoiceStereo (l, r);
        return 0.5f * (l + r);
    }

    float outputTrim() const noexcept override { return 2.2f; }

    bool voiceFinished() const override
    {
        return clickEnv.silent (2.0e-4f)
            && woodEnv.silent (2.0e-4f)
            && woodBodyEnv.silent (2.0e-4f)
            && metalEnv.silent (2.0e-4f)
            && bellEnv.silent (2.0e-4f)
            && spaceTailEnv.silent (2.0e-4f);
    }

    float voiceEnvelope() const override
    {
        float level = clickEnv.value;
        level = level > woodEnv.value ? level : woodEnv.value;
        level = level > metalEnv.value ? level : metalEnv.value;
        level = level > bellEnv.value ? level : bellEnv.value;
        return level;
    }

private:
    static constexpr float metalRatios[kNumMetalOsc] =
    {
        1.0f, 2.41f, 3.77f, 4.53f, 5.91f
    };
    static constexpr float bellRatios[kNumBellPartials] =
    {
        1.0f, 2.76f, 5.40f
    };
    static constexpr float bellAmps[kNumBellPartials] =
    {
        1.0f, 0.55f, 0.30f
    };

    Noise noiseClick, noiseWood, noiseMetal;
    Svf clickBp, woodBp, metalBp, metalHp;
    ExpEnv clickEnv, woodEnv, woodBodyEnv, metalEnv, bellEnv, spaceTailEnv;
    SineOsc woodOsc;
    SquareOsc metalOscs[kNumMetalOsc];
    SineOsc bellOscs[kNumBellPartials];
    float metalInc[kNumMetalOsc] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float bellInc[kNumBellPartials] = { 0.0f, 0.0f, 0.0f };

    std::vector<float> combBuffer[kNumCombs];
    size_t combDelay[kNumCombs] = { 8, 8, 8, 8 };
    size_t combIdx[kNumCombs] = { 0, 0, 0, 0 };
    float combDamp[kNumCombs] = { 0.0f, 0.0f, 0.0f, 0.0f };

    DCBlocker dc;

    float zoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float baseFreq = 340.0f;
    float clickLevel = 0.5f;
    float spaceMix = 0.2f;
    float combFeedback = 0.5f;
    float velGain = 1.0f;
};

} // namespace

std::unique_ptr<DrumVoice> createPercVoice()
{
    return std::make_unique<PercVoice>();
}

} // namespace klopgeest
