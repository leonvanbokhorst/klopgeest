// -----------------------------------------------------------------------------
// Voice DSP tests.
//
// Everything here drives the frozen DrumVoice contract directly (no processor,
// no sequencer), so the checks are about the synthesis engines themselves:
// finite/bounded output under hostile input, determinism, pitch/tone/level/pan
// audibility, choke behaviour, sample-rate independence and long-run stability.
// -----------------------------------------------------------------------------
#include "tests/VoiceTests.h"

#include "tests/TestHarness.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "core/Constants.h"
#include "core/Parameters.h"
#include "dsp/voices/Voice.h"

namespace
{

using namespace klopgeest;
using klop_test::check;
using klop_test::checkInRange;
using klop_test::checkNear;
using klop_test::info;

constexpr double kDefaultSampleRate = 48000.0;

const char* shortName (int voice)
{
    return voiceShortName (voice);
}

// -----------------------------------------------------------------------------
// Harness
// -----------------------------------------------------------------------------

VoiceParams defaultParams (int voice)
{
    const auto& d = voiceDefaults (voice);
    VoiceParams p;
    p.pitchSemis   = d.pitch;
    p.decay        = d.decay;
    p.tone         = d.tone;
    p.transient    = d.transient;
    p.drive        = d.drive;
    p.levelDb      = d.levelDb;
    p.pan          = d.pan;
    p.specialA     = d.specialA;
    p.specialB     = d.specialB;
    return p;
}

VoiceTrigger defaultTrigger (std::uint32_t seed)
{
    VoiceTrigger t;
    t.velocity = 0.85f;
    t.seed = seed;
    return t;
}

struct Render
{
    std::vector<float> left;
    std::vector<float> right;
    bool finite = true;
    float peak = 0.0f;
    double energy = 0.0;
    double energyL = 0.0;
    double energyR = 0.0;
    double diffEnergy = 0.0;
    long long frames = 0;
    double lastL = 0.0;

    double rms() const { return frames > 0 ? std::sqrt (energy / (2.0 * (double) frames)) : 0.0; }
    double rmsL() const { return frames > 0 ? std::sqrt (energyL / (double) frames) : 0.0; }
    double rmsR() const { return frames > 0 ? std::sqrt (energyR / (double) frames) : 0.0; }
    double brightness() const { return energy > 0.0 ? diffEnergy / energy : 0.0; }
};

void accumulate (Render& r, const juce::AudioBuffer<float>& buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = buffer.getSample (0, i);
        const float rr = buffer.getSample (1, i);

        r.left.push_back (l);
        r.right.push_back (rr);

        if (! std::isfinite (l) || ! std::isfinite (rr))
            r.finite = false;

        r.peak = std::max (r.peak, std::max (std::abs (l), std::abs (rr)));
        r.energy += (double) l * l + (double) rr * rr;
        r.energyL += (double) l * l;
        r.energyR += (double) rr * rr;

        const double d = (double) l - r.lastL;
        r.diffEnergy += d * d;
        r.lastL = (double) l;
    }
    r.frames += numSamples;
}

std::unique_ptr<DrumVoice> preparedVoice (int voiceIndex, double sampleRate, int blockSize)
{
    auto voice = createVoice (voiceIndex);
    if (voice != nullptr)
        voice->prepare (sampleRate, blockSize);
    return voice;
}

Render renderSeconds (DrumVoice& voice, double sampleRate, double seconds, int blockSize)
{
    Render r;
    juce::AudioBuffer<float> buffer (2, blockSize);
    const long long total = (long long) (seconds * sampleRate);

    for (long long pos = 0; pos < total; pos += blockSize)
    {
        const int n = (int) std::min<long long> (blockSize, total - pos);
        buffer.clear();
        voice.render (buffer, 0, n);
        accumulate (r, buffer, n);
    }

    return r;
}

Render renderRepeated (DrumVoice& voice, double sampleRate, double seconds, int blockSize,
                       double intervalSeconds, const VoiceTrigger& baseTrigger,
                       const VoiceParams* params, bool varySeed)
{
    Render r;
    juce::AudioBuffer<float> buffer (2, blockSize);
    const long long total = (long long) (seconds * sampleRate);
    const long long interval = std::max<long long> (1, (long long) (intervalSeconds * sampleRate));
    long long nextTrigger = 0;
    std::uint32_t hitIndex = 0;

    for (long long pos = 0; pos < total; pos += blockSize)
    {
        if (pos >= nextTrigger)
        {
            if (params != nullptr)
                voice.setParams (*params);

            auto t = baseTrigger;
            if (varySeed)
                t.seed = baseTrigger.seed + hitIndex * 7919u;

            voice.trigger (t);
            ++hitIndex;
            nextTrigger += interval;
        }

        const int n = (int) std::min<long long> (blockSize, total - pos);
        buffer.clear();
        voice.render (buffer, 0, n);
        accumulate (r, buffer, n);
    }

    return r;
}

Render renderSingleHit (int voiceIndex, double sampleRate, const VoiceParams& params,
                        const VoiceTrigger& trigger, double seconds, int blockSize = 128)
{
    auto voice = preparedVoice (voiceIndex, sampleRate, blockSize);
    if (voice == nullptr)
        return Render();

    voice->setParams (params);
    voice->trigger (trigger);
    return renderSeconds (*voice, sampleRate, seconds, blockSize);
}

double energyBetween (const Render& r, double sampleRate, double startSeconds, double endSeconds)
{
    const long long start = std::max<long long> (0, (long long) (startSeconds * sampleRate));
    const long long end = std::min<long long> ((long long) r.left.size(),
                                               (long long) (endSeconds * sampleRate));
    double energy = 0.0;
    for (long long i = start; i < end; ++i)
        energy += (double) r.left[(size_t) i] * r.left[(size_t) i]
                  + (double) r.right[(size_t) i] * r.right[(size_t) i];
    return energy;
}

bool allFinite (const Render& r)
{
    return r.finite;
}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

void testConstructionAndResponse()
{
    info ("construction + default hit response");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);
        auto voice = createVoice (v);
        check (voice != nullptr, "voice " + name + " constructs");

        if (voice == nullptr)
            continue;

        voice->prepare (kDefaultSampleRate, 128);
        voice->setParams (defaultParams (v));

        VoiceTrigger t = defaultTrigger (1234u + (std::uint32_t) v);
        t.velocity = 0.9f;
        voice->trigger (t);

        check (voice->isActive(), "voice " + name + " is active after trigger");
        checkInRange (voice->getEnvelopeLevel(), 0.0, 1.0, "voice " + name + " meter is 0..1");

        const auto r = renderSeconds (*voice, kDefaultSampleRate, 0.35, 128);
        check (allFinite (r), "voice " + name + " output is finite");
        check (r.peak > 1.0e-3f, "voice " + name + " is audible with default params");
        info ("  " + name + " default hit peak/RMS: " + std::to_string (r.peak) + " / "
              + std::to_string (r.rms()));
    }
}

void testDeterminism()
{
    info ("determinism (same seed -> same audio, block-size invariant)");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);
        const auto p = defaultParams (v);

        VoiceTrigger t = defaultTrigger (0xBEEF0000u + (std::uint32_t) v);
        t.velocity = 0.85f;
        t.pitchSemis = 0.25f;
        t.decayScale = 1.1f;
        t.transientScale = 0.9f;
        t.toneOffset = 0.1f;
        t.driveScale = 1.2f;

        const auto a = renderSingleHit (v, kDefaultSampleRate, p, t, 0.6, 128);
        const auto b = renderSingleHit (v, kDefaultSampleRate, p, t, 0.6, 128);
        check (a.left == b.left && a.right == b.right,
               "voice " + name + ": identical seed/trigger renders identical audio");

        VoiceTrigger t2 = t;
        t2.seed = t.seed + 1u;
        const auto c = renderSingleHit (v, kDefaultSampleRate, p, t2, 0.6, 128);
        check (a.left != c.left || a.right != c.right,
               "voice " + name + ": different seed changes the sound");

        const auto d = renderSingleHit (v, kDefaultSampleRate, p, t, 0.6, 64);
        const auto e = renderSingleHit (v, kDefaultSampleRate, p, t, 0.6, 512);
        check (d.left == e.left && d.right == e.right,
               "voice " + name + ": output is block-size invariant");
    }
}

void testHostileParameters()
{
    info ("hostile parameter / trigger extremes");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);

        VoiceParams p = defaultParams (v);
        p.pitchSemis = (v % 2 == 0) ? 24.0f : -24.0f;
        p.decay = 1.0f;
        p.tone = 1.0f;
        p.transient = 1.0f;
        p.drive = 1.0f;
        p.levelDb = 6.0f;
        p.pan = (v % 2 == 0) ? 1.0f : -1.0f;
        p.specialA = 1.0f;
        p.specialB = 1.0f;

        VoiceTrigger t = defaultTrigger ((std::uint32_t) (v * 17 + 3));
        t.velocity = 1.0f;
        t.pitchSemis = (v % 2 == 0) ? 48.0f : -48.0f;
        t.decayScale = 8.0f;
        t.transientScale = 8.0f;
        t.toneOffset = 1.0f;
        t.driveScale = 8.0f;

        const auto r = renderSingleHit (v, kDefaultSampleRate, p, t, 2.0, 128);
        check (allFinite (r), "voice " + name + ": hostile extremes stay finite");
        check (r.peak < 8.0f, "voice " + name + ": hostile extremes stay bounded");
    }

    // Non-finite parameter and trigger values must not poison the state.
    for (int v = 0; v < kNumVoices; ++v)
    {
        VoiceParams p;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        p.pitchSemis = nan;
        p.decay = nan;
        p.tone = inf;
        p.transient = -inf;
        p.drive = nan;
        p.levelDb = inf;
        p.pan = nan;
        p.specialA = nan;
        p.specialB = inf;

        VoiceTrigger t;
        t.velocity = nan;
        t.pitchSemis = inf;
        t.decayScale = nan;
        t.transientScale = inf;
        t.toneOffset = nan;
        t.driveScale = -inf;
        t.seed = 123u;

        const auto r = renderSingleHit (v, kDefaultSampleRate, p, t, 0.5, 128);
        check (allFinite (r), "voice " + std::string (shortName (v)) + ": NaN/Inf input is sanitised");
    }
}

void testSilenceAfterDecay()
{
    info ("voices go inactive and silent after their tail");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);
        VoiceParams p = defaultParams (v);
        p.decay = 0.15f;

        auto voice = preparedVoice (v, kDefaultSampleRate, 512);
        if (voice == nullptr)
            continue;

        voice->setParams (p);
        VoiceTrigger t = defaultTrigger (4000u + (std::uint32_t) v);
        t.velocity = 0.8f;
        voice->trigger (t);

        const auto r = renderSeconds (*voice, kDefaultSampleRate, 4.0, 512);
        check (allFinite (r), "voice " + name + ": long decay run stays finite");
        check (! voice->isActive(), "voice " + name + ": inactive after the tail");
        checkNear (voice->getEnvelopeLevel(), 0.0, 1.0e-6, "voice " + name + ": meter at zero when inactive");

        const auto silent = renderSeconds (*voice, kDefaultSampleRate, 0.1, 512);
        check (silent.peak == 0.0f, "voice " + name + ": renders no output once inactive");
    }
}

void testPitchAffectsBrightness()
{
    info ("pitch shifts the spectrum upward");

    for (int v = 0; v < kNumVoices; ++v)
    {
        VoiceParams low = defaultParams (v);
        VoiceParams high = defaultParams (v);
        low.pitchSemis = -12.0f;
        high.pitchSemis = 12.0f;

        VoiceTrigger t = defaultTrigger (99u + (std::uint32_t) v);

        const auto dark = renderSingleHit (v, kDefaultSampleRate, low, t, 0.5, 128);
        const auto bright = renderSingleHit (v, kDefaultSampleRate, high, t, 0.5, 128);

        const double darkRatio = dark.brightness();
        const double brightRatio = bright.brightness();
        const std::string name = shortName (v);

        info ("  " + name + " brightness -12/+12 st: " + std::to_string (darkRatio)
              + " / " + std::to_string (brightRatio));

        check (brightRatio > darkRatio * 1.03,
               "voice " + name + ": +12 st is spectrally brighter than -12 st");
    }
}

void testLevelAndPan()
{
    info ("level dB and pan behaviour");

    const int levelVoices[] = { VoiceKick, VoiceSnare, VoiceClap, VoiceTom };
    for (int v : levelVoices)
    {
        VoiceParams p = defaultParams (v);
        VoiceTrigger t = defaultTrigger (555u + (std::uint32_t) v);

        p.levelDb = -6.0f;
        const auto loud = renderSingleHit (v, kDefaultSampleRate, p, t, 0.4, 128);
        p.levelDb = -30.0f;
        const auto quiet = renderSingleHit (v, kDefaultSampleRate, p, t, 0.4, 128);

        const double expected = std::pow (10.0, -24.0 / 20.0);
        checkNear (quiet.rms() / std::max (1.0e-12, loud.rms()), expected, expected * 0.02,
                   "voice " + std::string (shortName (v)) + ": -24 dB level change is accurate");
    }

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);
        VoiceTrigger t = defaultTrigger (777u + (std::uint32_t) v);

        VoiceParams hardLeft = defaultParams (v);
        hardLeft.pan = -1.0f;
        const auto left = renderSingleHit (v, kDefaultSampleRate, hardLeft, t, 0.3, 128);
        check (left.energyR <= left.energyL * 1.0e-6 + 1.0e-12,
               "voice " + name + ": hard-left pan puts no energy in the right channel");

        VoiceParams hardRight = defaultParams (v);
        hardRight.pan = 1.0f;
        const auto right = renderSingleHit (v, kDefaultSampleRate, hardRight, t, 0.3, 128);
        check (right.energyL <= right.energyR * 1.0e-6 + 1.0e-12,
               "voice " + name + ": hard-right pan puts no energy in the left channel");
    }

    // Mono voice at centre should be channel-balanced.
    {
        VoiceParams p = defaultParams (VoiceKick);
        const auto centre = renderSingleHit (VoiceKick, kDefaultSampleRate, p,
                                             defaultTrigger (11u), 0.3, 128);
        checkNear (centre.rmsL(), centre.rmsR(), centre.rmsL() * 0.01,
                   "kick: centre pan is channel-balanced");
    }
}

void testChoke()
{
    info ("open-hat choke");

    VoiceParams p = defaultParams (VoiceOpenHat);
    VoiceTrigger t = defaultTrigger (31337u);
    t.velocity = 0.9f;

    const auto unchoked = renderSingleHit (VoiceOpenHat, kDefaultSampleRate, p, t, 0.5, 128);

    auto voice = preparedVoice (VoiceOpenHat, kDefaultSampleRate, 128);
    voice->setParams (p);
    voice->trigger (t);
    renderSeconds (*voice, kDefaultSampleRate, 0.05, 128);

    voice->choke (0.008f);
    renderSeconds (*voice, kDefaultSampleRate, 0.15, 128);       // fade region
    const auto settled = renderSeconds (*voice, kDefaultSampleRate, 0.10, 128);

    check (settled.peak < 1.0e-3f, "open hat: choked output fades to silence");
    check (! voice->isActive(), "open hat: is inactive after choke");
    check (settled.energy < unchoked.energy * 0.05,
           "open hat: choke significantly shortens the tail");

    // CHOKE character (specialB) should change the release length.
    auto renderChoke = [] (float chokeCharacter)
    {
        VoiceParams hp = defaultParams (VoiceOpenHat);
        hp.specialB = chokeCharacter;

        auto v = preparedVoice (VoiceOpenHat, kDefaultSampleRate, 128);
        v->setParams (hp);
        v->trigger (defaultTrigger (31337u));
        renderSeconds (*v, kDefaultSampleRate, 0.03, 128);
        v->choke (0.01f);
        return renderSeconds (*v, kDefaultSampleRate, 0.04, 128);
    };

    const auto fastChoke = renderChoke (0.0f);
    const auto slowChoke = renderChoke (1.0f);
    check (slowChoke.energy > fastChoke.energy * 1.2,
           "open hat: CHOKE character lengthens the choked release");

    // Choking an inactive voice is a safe no-op.
    auto idle = preparedVoice (VoiceClosedHat, kDefaultSampleRate, 128);
    idle->choke (0.01f);
    const auto idleOut = renderSeconds (*idle, kDefaultSampleRate, 0.05, 128);
    check (! idle->isActive() && idleOut.peak == 0.0f,
           "choke on an inactive voice is a no-op");
}

void testSampleRateIndependence()
{
    info ("sample-rate independence (44.1 / 48 / 96 kHz)");

    for (int v = 0; v < kNumVoices; ++v)
    {
        VoiceParams p = defaultParams (v);
        VoiceTrigger t = defaultTrigger (321u + (std::uint32_t) v);

        const auto r441 = renderSingleHit (v, 44100.0, p, t, 0.3, 128);
        const auto r48 = renderSingleHit (v, 48000.0, p, t, 0.3, 128);
        const auto r96 = renderSingleHit (v, 96000.0, p, t, 0.3, 128);

        const double reference = r48.rms();
        const double tolerance = 0.30 * reference + 1.0e-9;
        const std::string name = shortName (v);

        info ("  " + name + " RMS 44.1/48/96k: " + std::to_string (r441.rms()) + " / "
              + std::to_string (r48.rms()) + " / " + std::to_string (r96.rms()));

        check (std::abs (r441.rms() - reference) <= tolerance
                   && std::abs (r96.rms() - reference) <= tolerance,
               "voice " + name + ": RMS is consistent across sample rates");
    }
}

void testTriggerFieldsHaveEffect()
{
    info ("trigger field audibility (velocity / decay / transient)");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);
        const auto p = defaultParams (v);

        VoiceTrigger loud = defaultTrigger (2024u + (std::uint32_t) v);
        loud.velocity = 1.0f;
        VoiceTrigger quiet = loud;
        quiet.velocity = 0.05f;

        const auto loudRender = renderSingleHit (v, kDefaultSampleRate, p, loud, 0.4, 128);
        const auto quietRender = renderSingleHit (v, kDefaultSampleRate, p, quiet, 0.4, 128);
        check (quietRender.rms() < loudRender.rms() * 0.5,
               "voice " + name + ": velocity scales the hit down");

        VoiceTrigger shortHit = loud;
        shortHit.decayScale = 0.25f;
        VoiceTrigger longHit = loud;
        longHit.decayScale = 4.0f;
        const auto shortRender = renderSingleHit (v, kDefaultSampleRate, p, shortHit, 1.2, 128);
        const auto longRender = renderSingleHit (v, kDefaultSampleRate, p, longHit, 1.2, 128);
        const double shortTail = energyBetween (shortRender, kDefaultSampleRate, 0.15, 0.55);
        const double longTail = energyBetween (longRender, kDefaultSampleRate, 0.15, 0.55);
        check (longTail > shortTail * 1.2,
               "voice " + name + ": decayScale lengthens the tail");

        VoiceTrigger softTransient = loud;
        softTransient.transientScale = 0.0f;
        VoiceTrigger hardTransient = loud;
        hardTransient.transientScale = 3.0f;
        const auto softRender = renderSingleHit (v, kDefaultSampleRate, p, softTransient, 0.2, 128);
        const auto hardRender = renderSingleHit (v, kDefaultSampleRate, p, hardTransient, 0.2, 128);
        check (softRender.left != hardRender.left,
               "voice " + name + ": transientScale changes the attack");
    }
}

void testToneDriveAndCharacter()
{
    info ("tone / drive / specialA / specialB audibility");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);
        const VoiceTrigger t = defaultTrigger (606u + (std::uint32_t) v);

        VoiceParams toneLow = defaultParams (v);
        toneLow.tone = 0.0f;
        VoiceParams toneHigh = defaultParams (v);
        toneHigh.tone = 1.0f;
        const auto dark = renderSingleHit (v, kDefaultSampleRate, toneLow, t, 0.4, 128);
        const auto bright = renderSingleHit (v, kDefaultSampleRate, toneHigh, t, 0.4, 128);
        check (bright.brightness() > dark.brightness() * 1.02,
               "voice " + name + ": TONE brightens the sound");

        VoiceParams driveLow = defaultParams (v);
        driveLow.drive = 0.0f;
        VoiceParams driveHigh = defaultParams (v);
        driveHigh.drive = 1.0f;
        const auto clean = renderSingleHit (v, kDefaultSampleRate, driveLow, t, 0.3, 128);
        const auto dirty = renderSingleHit (v, kDefaultSampleRate, driveHigh, t, 0.3, 128);
        check (clean.left != dirty.left || clean.right != dirty.right,
               "voice " + name + ": DRIVE changes the waveform");

        VoiceParams aLow = defaultParams (v);
        aLow.specialA = 0.0f;
        VoiceParams aHigh = defaultParams (v);
        aHigh.specialA = 1.0f;
        const auto a0 = renderSingleHit (v, kDefaultSampleRate, aLow, t, 0.4, 128);
        const auto a1 = renderSingleHit (v, kDefaultSampleRate, aHigh, t, 0.4, 128);
        check (a0.left != a1.left || a0.right != a1.right,
               "voice " + name + ": specialA / CHARACTER changes the sound");

        VoiceParams bLow = defaultParams (v);
        bLow.specialB = 0.0f;
        VoiceParams bHigh = defaultParams (v);
        bHigh.specialB = 1.0f;
        const auto b0 = renderSingleHit (v, kDefaultSampleRate, bLow, t, 0.4, 128);
        const auto b1 = renderSingleHit (v, kDefaultSampleRate, bHigh, t, 0.4, 128);
        check (b0.left != b1.left || b0.right != b1.right,
               "voice " + name + ": specialB / TEXTURE changes the sound");
    }
}

void testMeterAndReset()
{
    info ("meter / reset behaviour");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);
        auto voice = preparedVoice (v, kDefaultSampleRate, 128);
        voice->setParams (defaultParams (v));
        voice->trigger (defaultTrigger (808u + (std::uint32_t) v));

        const float meterAtTrigger = voice->getEnvelopeLevel();
        checkInRange (meterAtTrigger, 0.0, 1.0, "voice " + name + ": meter 0..1 at trigger");
        check (meterAtTrigger > 0.0f, "voice " + name + ": meter rises on trigger");

        renderSeconds (*voice, kDefaultSampleRate, 0.1, 128);
        voice->reset();
        check (! voice->isActive(), "voice " + name + ": reset makes the voice inactive");
        checkNear (voice->getEnvelopeLevel(), 0.0, 1.0e-6, "voice " + name + ": reset zeroes the meter");
    }
}

void testNoClicksOnParameterChanges()
{
    info ("level automation does not click");

    auto renderSegment = [] (DrumVoice& voice, double seconds)
    {
        std::vector<float> samples;
        juce::AudioBuffer<float> buffer (2, 128);
        const long long total = (long long) (seconds * kDefaultSampleRate);
        for (long long pos = 0; pos < total; pos += 128)
        {
            const int n = (int) std::min<long long> (128, total - pos);
            buffer.clear();
            voice.render (buffer, 0, n);
            for (int i = 0; i < n; ++i)
                samples.push_back (buffer.getSample (0, i));
        }
        return samples;
    };

    auto tailRms = [] (const std::vector<float>& samples, double seconds)
    {
        const size_t count = std::min (samples.size(),
                                       (size_t) (seconds * kDefaultSampleRate));
        double sum = 0.0;
        const size_t start = samples.size() - count;
        for (size_t i = start; i < samples.size(); ++i)
            sum += (double) samples[i] * samples[i];
        return count > 0 ? std::sqrt (sum / (double) count) : 0.0;
    };

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);
        auto voice = preparedVoice (v, kDefaultSampleRate, 128);
        voice->setParams (defaultParams (v));
        voice->trigger (defaultTrigger (4242u + (std::uint32_t) v));

        const auto before = renderSegment (*voice, 0.04);
        const double levelBefore = tailRms (before, 0.002);

        VoiceParams muted = defaultParams (v);
        muted.levelDb = -60.0f;
        voice->setParams (muted);

        const auto fade = renderSegment (*voice, 0.02);
        // 6 ms smoothing: after 1 ms the level must still be substantially
        // above the mute target. An unsmoothed gain would drop almost instantly.
        const double fadeStart = [&]
        {
            const size_t count = std::min (fade.size(), (size_t) (0.001 * kDefaultSampleRate));
            double sum = 0.0;
            for (size_t i = 0; i < count; ++i)
                sum += (double) fade[i] * fade[i];
            return count > 0 ? std::sqrt (sum / (double) count) : 0.0;
        }();

        check (levelBefore > 1.0e-5 && fadeStart > levelBefore * 0.20,
               "voice " + name + ": level change fades smoothly (no click)");

        // And the voice must actually reach the much quieter target.
        renderSegment (*voice, 0.03);
        const auto quiet = renderSegment (*voice, 0.02);
        check (tailRms (quiet, 0.02) < levelBefore * 0.2,
               "voice " + name + ": level change reaches the new target");
    }
}

void testLongTriggerRunStaysFinite()
{
    info ("10 seconds of continuous triggering");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const std::string name = shortName (v);

        auto voice = preparedVoice (v, kDefaultSampleRate, 128);
        VoiceTrigger t = defaultTrigger (5150u + (std::uint32_t) v);

        VoiceParams p = defaultParams (v);

        Render r;
        juce::AudioBuffer<float> buffer (2, 128);
        const long long total = (long long) (10.0 * kDefaultSampleRate);
        const long long interval = (long long) (0.08 * kDefaultSampleRate);
        long long nextTrigger = 0;
        std::uint32_t hit = 0;

        for (long long pos = 0; pos < total; pos += 128)
        {
            if (pos >= nextTrigger)
            {
                // Sweep the parameters violently while hammering the voice.
                p.pitchSemis = (hit % 2 == 0) ? 18.0f : -18.0f;
                p.decay = (hit % 3 == 0) ? 1.0f : 0.1f;
                p.drive = (hit % 2 == 0) ? 1.0f : 0.0f;
                p.specialA = (hit % 5 == 0) ? 1.0f : 0.0f;
                p.specialB = (hit % 4 == 0) ? 1.0f : 0.0f;
                voice->setParams (p);

                t.seed = 5150u + (std::uint32_t) v + hit * 104729u;
                t.velocity = (hit % 7 == 0) ? 1.0f : 0.15f;
                t.pitchSemis = (hit % 2 == 0) ? 12.0f : -12.0f;
                t.decayScale = (hit % 3 == 0) ? 4.0f : 0.25f;
                voice->trigger (t);

                ++hit;
                nextTrigger += interval;
            }

            const int n = (int) std::min<long long> (128, total - pos);
            buffer.clear();
            voice->render (buffer, 0, n);
            accumulate (r, buffer, n);
        }

        check (allFinite (r), "voice " + name + ": 10 s of triggers stays finite");
        check (r.peak < 8.0f, "voice " + name + ": 10 s of triggers stays bounded");
    }
}

void testVoicesAreDistinct()
{
    info ("the eight voices are acoustically distinct");

    std::vector<std::vector<float>> signatures;

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto r = renderSingleHit (v, kDefaultSampleRate, defaultParams (v),
                                        defaultTrigger (9000u + (std::uint32_t) v), 0.25, 128);

        std::vector<float> signature;
        signature.reserve (1000);
        const int stride = std::max (1, (int) r.left.size() / 1000);
        for (size_t i = 0; i < r.left.size() && signature.size() < 1000; i += (size_t) stride)
            signature.push_back (r.left[i]);
        signatures.push_back (std::move (signature));
    }

    bool allDistinct = true;
    for (int a = 0; a < kNumVoices; ++a)
        for (int b = a + 1; b < kNumVoices; ++b)
        {
            const auto& sa = signatures[(size_t) a];
            const auto& sb = signatures[(size_t) b];
            const size_t n = std::min (sa.size(), sb.size());
            double difference = 0.0;
            for (size_t i = 0; i < n; ++i)
                difference += std::abs (sa[i] - sb[i]);
            if (n == 0 || difference / (double) n < 1.0e-4)
                allDistinct = false;
        }

    check (allDistinct, "all eight voices differ from each other");
}

void testCpuEstimate()
{
    info ("8-voice CPU estimate (48 kHz / 128)");

    constexpr double audioSeconds = 5.0;
    constexpr int blockSize = 128;

    std::vector<std::unique_ptr<DrumVoice>> voices;
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto voice = preparedVoice (v, kDefaultSampleRate, blockSize);
        voice->setParams (defaultParams (v));
        voices.push_back (std::move (voice));
    }

    juce::AudioBuffer<float> buffer (2, blockSize);
    const long long total = (long long) (audioSeconds * kDefaultSampleRate);
    const long long interval = (long long) (0.10 * kDefaultSampleRate);
    long long nextTrigger = 0;
    std::uint32_t hit = 0;

    const auto start = std::chrono::steady_clock::now();

    for (long long pos = 0; pos < total; pos += blockSize)
    {
        if (pos >= nextTrigger)
        {
            for (size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
            {
                auto& voice = voices[voiceIndex];
                VoiceTrigger t;
                t.velocity = 0.9f;
                t.seed = 777u + hit * 131u + (std::uint32_t) voiceIndex;
                voice->trigger (t);
            }
            ++hit;
            nextTrigger += interval;
        }

        const int n = (int) std::min<long long> (blockSize, total - pos);
        buffer.clear();
        for (auto& voice : voices)
            if (voice->isActive())
                voice->render (buffer, 0, n);
    }

    const auto end = std::chrono::steady_clock::now();
    const double wallSeconds = std::chrono::duration<double> (end - start).count();
    const double realtimeFactor = audioSeconds / std::max (1.0e-9, wallSeconds);
    const double corePercent = 100.0 / realtimeFactor;

    info ("  8 voices: " + std::to_string (realtimeFactor) + "x realtime, ~"
          + std::to_string (corePercent) + "% of one core (excluding master bus)");

    check (realtimeFactor > 2.0, "8 voices render faster than realtime even on a busy machine");
}

} // namespace

void runVoiceTests()
{
    std::printf ("\nVoice DSP tests\n");
    const int checksBefore = klop_test::gChecks;
    const int failuresBefore = klop_test::gFailures;

    testConstructionAndResponse();
    testDeterminism();
    testHostileParameters();
    testSilenceAfterDecay();
    testPitchAffectsBrightness();
    testLevelAndPan();
    testChoke();
    testSampleRateIndependence();
    testTriggerFieldsHaveEffect();
    testToneDriveAndCharacter();
    testMeterAndReset();
    testNoClicksOnParameterChanges();
    testLongTriggerRunStaysFinite();
    testVoicesAreDistinct();
    testCpuEstimate();

    info ("voice suite: " + std::to_string (klop_test::gChecks - checksBefore) + " checks, "
          + std::to_string (klop_test::gFailures - failuresBefore) + " failures");
}
