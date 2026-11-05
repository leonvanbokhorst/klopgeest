#include "dsp/voices/VoiceBase.h"

#include <algorithm>
#include <cmath>

namespace klopgeest
{

float VoiceBase::chokeTimeConstant (float releaseSeconds) const noexcept
{
    return vutil::clampf (releaseSeconds, 0.003f, 0.035f);
}

void VoiceBase::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = vutil::sanitise ((float) newSampleRate, 1000.0f, 384000.0f, 48000.0f);
    inverseSampleRate = 1.0 / sampleRate;

    (void) maxBlockSize; // voices render per-sample, so any block size works

    // ~6 ms smoothing for level and pan; fast enough to feel immediate, slow
    // enough that automation cannot click.
    gainCoeff = 1.0f - std::exp (-1.0f / (0.006f * (float) sampleRate));

    prepared = true;
    prepareVoice();
    updateTargetGains();
    reset();
}

void VoiceBase::reset()
{
    active = false;
    choking = false;
    chokeGain = 1.0f;
    chokeCoeff = 0.0f;

    updateTargetGains();
    gainL = gainLTarget;
    gainR = gainRTarget;

    resetVoice();
}

void VoiceBase::updateTargetGains() noexcept
{
    const float level = vutil::dbToGain (params.levelDb);
    float panL = vutil::kInvSqrt2;
    float panR = vutil::kInvSqrt2;
    vutil::panGains (params.pan, panL, panR);

    gainLTarget = level * panL;
    gainRTarget = level * panR;
}

void VoiceBase::setParams (const VoiceParams& newParams)
{
    params.pitchSemis    = vutil::sanitise (newParams.pitchSemis, -24.0f, 24.0f, 0.0f);
    params.decay         = vutil::sanitise (newParams.decay, 0.0f, 1.0f, 0.5f);
    params.tone          = vutil::sanitise (newParams.tone, 0.0f, 1.0f, 0.5f);
    params.transient     = vutil::sanitise (newParams.transient, 0.0f, 1.0f, 0.5f);
    params.drive         = vutil::sanitise (newParams.drive, 0.0f, 1.0f, 0.15f);
    params.levelDb       = vutil::sanitise (newParams.levelDb, -60.0f, 6.0f, -6.0f);
    params.pan           = vutil::sanitise (newParams.pan, -1.0f, 1.0f, 0.0f);
    params.specialA      = vutil::sanitise (newParams.specialA, 0.0f, 1.0f, 0.5f);
    params.specialB      = vutil::sanitise (newParams.specialB, 0.0f, 1.0f, 0.5f);
    params.randPitch     = vutil::sanitise (newParams.randPitch, 0.0f, 1.0f, 0.0f);
    params.randLevel     = vutil::sanitise (newParams.randLevel, 0.0f, 1.0f, 0.0f);
    params.randDecay     = vutil::sanitise (newParams.randDecay, 0.0f, 1.0f, 0.0f);
    params.randTransient = vutil::sanitise (newParams.randTransient, 0.0f, 1.0f, 0.0f);
    params.randTiming    = vutil::sanitise (newParams.randTiming, 0.0f, 1.0f, 0.0f);
    params.timingBiasMs  = vutil::sanitise (newParams.timingBiasMs, -25.0f, 25.0f, 0.0f);

    updateTargetGains();

    if (! active)
    {
        gainL = gainLTarget;
        gainR = gainRTarget;
    }

    paramsChanged();
}

void VoiceBase::trigger (const VoiceTrigger& newTrigger)
{
    if (! prepared)
        return;

    velocity       = vutil::sanitise (newTrigger.velocity, 0.0f, 1.0f, 0.8f);
    decayScale     = vutil::sanitise (newTrigger.decayScale, 0.02f, 8.0f, 1.0f);
    transientScale = vutil::sanitise (newTrigger.transientScale, 0.0f, 8.0f, 1.0f);
    toneOffset     = vutil::sanitise (newTrigger.toneOffset, -1.0f, 1.0f, 0.0f);
    driveScale     = vutil::sanitise (newTrigger.driveScale, 0.0f, 8.0f, 1.0f);
    hitSeed        = newTrigger.seed;

    pitchRatio     = vutil::semitoneRatio (params.pitchSemis
                                           + vutil::sanitise (newTrigger.pitchSemis, -48.0f, 48.0f, 0.0f));
    driveAmount    = vutil::clampf (params.drive * driveScale, 0.0f, 1.0f);
    hitVelocityGain = vutil::velocityGain (velocity);

    active = true;
    choking = false;
    chokeGain = 1.0f;

    startVoice();
}

void VoiceBase::choke (float releaseSeconds)
{
    if (! active)
        return;

    const float seconds = vutil::clampf (chokeTimeConstant (releaseSeconds), 0.002f, 0.1f);
    chokeCoeff = std::exp (-1.0f / (seconds * (float) sampleRate));
    choking = true;
}

void VoiceBase::render (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (! prepared || ! active || numSamples <= 0)
        return;

    const int numChannels = out.getNumChannels();
    if (numChannels <= 0)
        return;

    float* left  = out.getWritePointer (0);
    float* right = numChannels > 1 ? out.getWritePointer (1) : nullptr;

    const float targetL = gainLTarget;
    const float targetR = gainRTarget;
    const float trim = outputTrim();

    int i = 0;
    for (; i < numSamples; ++i)
    {
        if (voiceFinished())
        {
            active = false;
            break;
        }

        float sampleL = 0.0f;
        float sampleR = 0.0f;
        renderVoiceStereo (sampleL, sampleR);

        if (! std::isfinite (sampleL)) sampleL = 0.0f;
        if (! std::isfinite (sampleR)) sampleR = 0.0f;

        gainL = vutil::flushDenorm (gainL + (targetL - gainL) * gainCoeff);
        gainR = vutil::flushDenorm (gainR + (targetR - gainR) * gainCoeff);

        float choke = 1.0f;
        if (choking)
        {
            chokeGain = vutil::flushDenorm (chokeGain * chokeCoeff);
            if (chokeGain <= 1.0e-4f)
            {
                active = false;
                break;
            }
            choke = chokeGain;
        }

        if (right != nullptr)
        {
            left[startSample + i]  += vutil::safeOutput (sampleL * gainL * choke * trim);
            right[startSample + i] += vutil::safeOutput (sampleR * gainR * choke * trim);
        }
        else
        {
            left[startSample + i] += vutil::safeOutput ((sampleL * gainL + sampleR * gainR)
                                                        * choke * trim);
        }
    }
}

float VoiceBase::getEnvelopeLevel() const
{
    if (! active || ! prepared)
        return 0.0f;

    const float choke = choking ? chokeGain : 1.0f;
    return vutil::clampf (voiceEnvelope() * choke, 0.0f, 1.0f);
}

} // namespace klopgeest
