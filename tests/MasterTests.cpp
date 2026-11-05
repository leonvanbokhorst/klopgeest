// -----------------------------------------------------------------------------
// KLOPGEEST — master glue machine tests.
//
// The glue machine is deliberately non-transparent, so every test pins the
// parameters it cares about explicitly. All signals are generated with a local
// deterministic PRNG: no std::rand, no host state, no file I/O.
// -----------------------------------------------------------------------------
#include "tests/MasterTests.h"
#include "tests/TestHarness.h"

#include "dsp/master/GlueMachine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using klopgeest::GlueMachine;
using klopgeest::MasterParams;
using klop_test::check;
using klop_test::checkInRange;
using klop_test::checkNear;
using klop_test::info;

namespace
{

constexpr double kSr = 48000.0;

// -----------------------------------------------------------------------------
// Signal helpers
// -----------------------------------------------------------------------------

inline float dbToAmp (float db) noexcept
{
    return std::pow (10.0f, db / 20.0f);
}

inline float ampToDb (double amp) noexcept
{
    return amp > 1.0e-7 ? (float) (20.0 * std::log10 (amp)) : -100.0f;
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (ch, i)))
                return false;
    return true;
}

float peakOf (const juce::AudioBuffer<float>& buffer, int startSample = 0)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, startSample,
                                                      buffer.getNumSamples() - startSample));
    return peak;
}

double rmsOf (const juce::AudioBuffer<float>& buffer, int startSample = 0)
{
    double sum = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = startSample; i < buffer.getNumSamples(); ++i)
        {
            sum += (double) data[i] * data[i];
            ++count;
        }
    }
    return count > 0 ? std::sqrt (sum / (double) count) : 0.0;
}

float rmsDbOf (const juce::AudioBuffer<float>& buffer, int startSample = 0)
{
    return ampToDb (rmsOf (buffer, startSample));
}

float maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float diff = 0.0f;
    for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
        for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
            diff = juce::jmax (diff, std::fabs (a.getSample (ch, i) - b.getSample (ch, i)));
    return diff;
}

juce::AudioBuffer<float> makeSine (double sampleRate, float hz, float amp, int numSamples,
                                   int numChannels = 2)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                * (double) hz * (double) i / sampleRate);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, v);
    }
    return buffer;
}

/** Deterministic LCG so the drum-bus test signal never changes. */
struct SimpleRng
{
    std::uint32_t state = 0x1234abcdu;
    float noise()
    {
        state = state * 1664525u + 1013904223u;
        return ((float) ((state >> 8) & 0xFFFFFFu) / 8388608.0f) - 1.0f;
    }
};

/** A deliberately hot synthetic drum bus (kick / snare / hats at ~120 BPM). */
juce::AudioBuffer<float> makeHotDrumBus (double sampleRate, double seconds = 2.0)
{
    const int numSamples = (int) std::lround (sampleRate * seconds);
    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.clear();

    SimpleRng rng;
    const int stepSamples = (int) std::lround (sampleRate * 0.125); // 16th @ 120 BPM

    for (int step = 0; step * stepSamples < numSamples; ++step)
    {
        const int at = step * stepSamples;

        // Kick on the downbeats.
        if (step % 4 == 0)
        {
            const int length = (int) (sampleRate * 0.35);
            double phase = 0.0;
            for (int i = 0; i < length && at + i < numSamples; ++i)
            {
                const double t = (double) i / sampleRate;
                const double f = 48.0 + 130.0 * std::exp (-t / 0.018);
                phase += 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
                const double env = std::exp (-t / 0.11) * (1.0 - std::exp (-t / 0.0015));
                const float v = (float) (0.95 * std::sin (phase) * env
                                         + 0.18 * std::exp (-t / 0.003) * rng.noise() * env);
                buffer.addSample (0, at + i, v);
                buffer.addSample (1, at + i, v);
            }
        }

        // Snare on beat 2 and 4.
        if (step % 4 == 2)
        {
            const int length = (int) (sampleRate * 0.22);
            for (int i = 0; i < length && at + i < numSamples; ++i)
            {
                const double t = (double) i / sampleRate;
                const double env = std::exp (-t / 0.075);
                const double body = 0.35 * std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * 185.0 * t) * std::exp (-t / 0.05);
                const float v = (float) (env * (0.55 * rng.noise() + body));
                buffer.addSample (0, at + i, v * 0.9f);
                buffer.addSample (1, at + i, v * 0.8f);
            }
        }

        // Hats on the off-beats.
        if (step % 2 == 1)
        {
            const int length = (int) (sampleRate * 0.06);
            float lastNoise = 0.0f;
            for (int i = 0; i < length && at + i < numSamples; ++i)
            {
                const double t = (double) i / sampleRate;
                const float n = rng.noise();
                const float hp = n - lastNoise; // crude high-pass -> "tss"
                lastNoise = n;
                const float v = (float) (0.22 * hp * std::exp (-t / 0.012));
                buffer.addSample (0, at + i, v);
                buffer.addSample (1, at + i, -v * 0.85f);
            }
        }
    }

    // Normalise to a hot (but not clipped) drum bus: -0.2 dBFS peak.
    const float peak = juce::jmax (peakOf (buffer), 1.0e-6f);
    buffer.applyGain (0.98f / peak);
    return buffer;
}

/**
    A test fixture that owns a prepared glue machine and can process an
    AudioBuffer in blocks without allocating (views into the same buffer).
*/
struct Rig
{
    GlueMachine machine;
    MasterParams params;
    double sampleRate = kSr;

    explicit Rig (const MasterParams& p, double sr = kSr)
        : params (p), sampleRate (sr)
    {
        machine.prepare (sampleRate, 512);
        machine.setParams (params);
    }

    void process (juce::AudioBuffer<float>& buffer, int blockSize = 128)
    {
        for (int position = 0; position < buffer.getNumSamples(); position += blockSize)
        {
            const int n = juce::jmin (blockSize, buffer.getNumSamples() - position);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                           buffer.getNumChannels(), position, n);
            machine.process (view, n);
        }
    }

    /** Processes and records the GR meter at the end of every block. */
    std::vector<float> processWithMeter (juce::AudioBuffer<float>& buffer, int blockSize = 32)
    {
        std::vector<float> meter;
        for (int position = 0; position < buffer.getNumSamples(); position += blockSize)
        {
            const int n = juce::jmin (blockSize, buffer.getNumSamples() - position);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                           buffer.getNumChannels(), position, n);
            machine.process (view, n);
            meter.push_back (machine.getGainReductionDb());
        }
        return meter;
    }
};

/** Defaults with the "character" stages off, so tests can reason about them. */
MasterParams neutralParams()
{
    MasterParams p;                 // start from the shipped defaults…
    p.thresholdDb = -18.0f;
    p.ratio = 4.0f;
    p.attackMs = 8.0f;
    p.releaseMs = 180.0f;
    p.kneeDb = 8.0f;
    p.autoRelease = false;
    p.sidechainHz = 20.0f;          // as low as possible: minimal detector filtering
    p.detect = 0.5f;
    p.mix = 1.0f;
    p.satPre = 0.0f;
    p.satPost = 0.0f;
    p.outputDb = 0.0f;
    p.ceilingDb = -0.4f;
    p.glue = 0.0f;
    p.heat = 0.0f;
    p.limiter = false;
    return p;
}

// -----------------------------------------------------------------------------
// 1. Silence in -> silence out
// -----------------------------------------------------------------------------
void testSilence()
{
    MasterParams p = neutralParams();
    p.limiter = true;
    Rig rig (p);

    juce::AudioBuffer<float> buffer (2, 4096);
    buffer.clear();
    rig.process (buffer, 256);

    check (peakOf (buffer) == 0.0f, "glue: silence in -> silence out");
    check (rig.machine.getOutputPeakDb() <= -99.9f, "glue: silence peak meter reads -100 dBFS");
    check (rig.machine.getGainReductionDb() == 0.0f, "glue: no gain reduction on silence");
}

// -----------------------------------------------------------------------------
// 2. Steady sine below threshold passes at unity
// -----------------------------------------------------------------------------
void testBelowThresholdUnity()
{
    MasterParams p = neutralParams();
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.kneeDb = 8.0f;
    p.detect = 0.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 200.0f;

    Rig rig (p);
    const float amp = dbToAmp (-30.0f);
    juce::AudioBuffer<float> buffer = makeSine (kSr, 440.0f, amp, (int) kSr);
    const juce::AudioBuffer<float> reference = buffer;

    rig.process (buffer, 256);

    const int settle = (int) (kSr * 0.75);
    const double diffDb = 20.0 * std::log10 (rmsOf (buffer, settle) / rmsOf (reference, settle));
    info ("glue: below-threshold unity deviation = " + std::to_string (diffDb) + " dB");
    checkNear (diffDb, 0.0, 0.1, "glue: sine below threshold passes at unity (+-0.1 dB)");
    check (rig.machine.getGainReductionDb() <= 0.05f, "glue: meter shows no GR below threshold");
}

// -----------------------------------------------------------------------------
// 3. Above threshold -> meaningful gain reduction
// -----------------------------------------------------------------------------
float steadyStateGr (const MasterParams& p, float hz, float amp, double seconds = 0.6)
{
    Rig rig (p);
    juce::AudioBuffer<float> buffer = makeSine (kSr, hz, amp, (int) (kSr * seconds));
    rig.process (buffer, 128);
    return rig.machine.getGainReductionDb();
}

void testAboveThresholdGainReduction()
{
    MasterParams p = neutralParams();
    p.thresholdDb = -30.0f;
    p.ratio = 8.0f;
    p.kneeDb = 0.0f;
    p.detect = 0.0f;
    p.attackMs = 2.0f;
    p.releaseMs = 150.0f;

    Rig rig (p);
    juce::AudioBuffer<float> buffer = makeSine (kSr, 440.0f, dbToAmp (-10.0f), (int) (kSr * 0.6));
    const juce::AudioBuffer<float> reference = buffer;
    rig.process (buffer, 128);

    const float gr = rig.machine.getGainReductionDb();
    info ("glue: steady-state GR above threshold = " + std::to_string (gr) + " dB");
    check (gr > 3.0f, "glue: GR above threshold > 3 dB");
    checkInRange (gr, 10.0f, 25.0f, "glue: steady GR is close to the static curve");
    check (rmsDbOf (buffer, (int) (kSr * 0.35)) < rmsDbOf (reference, (int) (kSr * 0.35)) - 1.0f,
           "glue: above-threshold output is quieter than the input");
}

// -----------------------------------------------------------------------------
// 4. Higher ratio / more glue -> more gain reduction
// -----------------------------------------------------------------------------
void testRatioAndGlueIncreaseGainReduction()
{
    MasterParams low = neutralParams();
    low.thresholdDb = -30.0f;
    low.ratio = 2.0f;
    low.kneeDb = 0.0f;
    low.detect = 0.0f;
    low.attackMs = 2.0f;
    low.releaseMs = 150.0f;

    MasterParams high = low;
    high.ratio = 16.0f;

    const float grLow = steadyStateGr (low, 440.0f, dbToAmp (-12.0f));
    const float grHigh = steadyStateGr (high, 440.0f, dbToAmp (-12.0f));
    info ("glue: GR ratio 2 = " + std::to_string (grLow) + " dB, ratio 16 = "
          + std::to_string (grHigh) + " dB");
    check (grHigh > grLow + 3.0f, "glue: higher ratio produces more gain reduction");

    MasterParams noGlue = neutralParams();
    noGlue.thresholdDb = -24.0f;
    noGlue.ratio = 4.0f;
    noGlue.kneeDb = 0.0f;
    noGlue.detect = 0.0f;
    noGlue.attackMs = 2.0f;
    noGlue.releaseMs = 150.0f;
    noGlue.glue = 0.0f;

    MasterParams moreGlue = noGlue;
    moreGlue.glue = 1.0f;

    const float grNoGlue = steadyStateGr (noGlue, 440.0f, dbToAmp (-12.0f));
    const float grGlue = steadyStateGr (moreGlue, 440.0f, dbToAmp (-12.0f));
    info ("glue: GR glue 0 = " + std::to_string (grNoGlue) + " dB, glue 1 = "
          + std::to_string (grGlue) + " dB");
    check (grGlue > grNoGlue + 1.5f, "glue: GLUE macro increases gain reduction");
}

// -----------------------------------------------------------------------------
// 5. Attack direction: a fast attack catches a transient, a slow one lets it by
// -----------------------------------------------------------------------------
float maxGrDuringBurst (float attackMs)
{
    MasterParams p = neutralParams();
    p.thresholdDb = -30.0f;
    p.ratio = 10.0f;
    p.kneeDb = 0.0f;
    p.detect = 0.0f;
    p.attackMs = attackMs;
    p.releaseMs = 200.0f;

    Rig rig (p);
    const int total = (int) (kSr * 0.22);
    juce::AudioBuffer<float> buffer (2, total);
    buffer.clear();

    const int burstStart = (int) (kSr * 0.10);
    const int burstEnd = burstStart + (int) (kSr * 0.02);
    const float amp = dbToAmp (-6.0f);
    for (int i = burstStart; i < burstEnd; ++i)
    {
        const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                * 440.0 * (double) (i - burstStart) / kSr);
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v);
    }

    const auto meter = rig.processWithMeter (buffer, 16);
    float maxGr = 0.0f;
    const int block = 16;
    for (int b = 0; b < (int) meter.size(); ++b)
    {
        const int position = (b + 1) * block;
        if (position > burstStart && position <= burstEnd + block)
            maxGr = juce::jmax (maxGr, meter[(size_t) b]);
    }
    return maxGr;
}

void testAttackDirection()
{
    const float fast = maxGrDuringBurst (0.1f);
    const float slow = maxGrDuringBurst (80.0f);
    info ("glue: max GR during 20 ms burst: attack 0.1 ms = " + std::to_string (fast)
          + " dB, attack 80 ms = " + std::to_string (slow) + " dB");
    check (fast > slow + 3.0f, "glue: fast attack catches the transient (more GR)");
    check (fast > 5.0f, "glue: fast attack actually reduces the burst");
}

// -----------------------------------------------------------------------------
// 6. Release direction: a short release recovers faster after a burst
// -----------------------------------------------------------------------------
float grAfterBurst (float releaseMs)
{
    MasterParams p = neutralParams();
    p.thresholdDb = -30.0f;
    p.ratio = 10.0f;
    p.kneeDb = 0.0f;
    p.detect = 0.0f;
    p.attackMs = 0.1f;
    p.releaseMs = releaseMs;

    Rig rig (p);
    const int total = (int) (kSr * 0.5);
    juce::AudioBuffer<float> buffer (2, total);
    buffer.clear();

    const int burstEnd = (int) (kSr * 0.3);
    const float amp = dbToAmp (-6.0f);
    for (int i = 0; i < burstEnd; ++i)
    {
        const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                * 440.0 * (double) i / kSr);
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v);
    }

    const auto meter = rig.processWithMeter (buffer, 16);
    const int block = 16;
    const int measureAt = burstEnd + (int) (kSr * 0.05);
    float gr = 0.0f;
    for (int b = 0; b < (int) meter.size(); ++b)
        if ((b + 1) * block <= measureAt)
            gr = meter[(size_t) b];
    return gr;
}

void testReleaseDirection()
{
    const float fast = grAfterBurst (20.0f);
    const float slow = grAfterBurst (1000.0f);
    info ("glue: GR 50 ms after burst: release 20 ms = " + std::to_string (fast)
          + " dB, release 1000 ms = " + std::to_string (slow) + " dB");
    check (fast < slow - 3.0f, "glue: short release recovers faster than a long release");
    check (fast >= 0.0f && slow <= 40.0f, "glue: release meters stay in a sane range");
}

// -----------------------------------------------------------------------------
// 7. Parallel mix: mix = 0 is bit-transparent (zero-latency dry path)
// -----------------------------------------------------------------------------
juce::AudioBuffer<float> makeBusySignal (double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buffer (2, numSamples);
    SimpleRng rng;
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = (double) i / sampleRate;
        const float l = (float) (0.22 * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t)
                                 + 0.13 * std::sin (2.0 * juce::MathConstants<double>::pi * 1240.0 * t)
                                 + 0.05 * (double) rng.noise());
        const float r = (float) (0.20 * std::sin (2.0 * juce::MathConstants<double>::pi * 105.0 * t)
                                 + 0.12 * std::sin (2.0 * juce::MathConstants<double>::pi * 1330.0 * t)
                                 + 0.05 * (double) rng.noise());
        buffer.setSample (0, i, l);
        buffer.setSample (1, i, r);
    }
    return buffer;
}

void testMixZeroIsDry()
{
    MasterParams p = neutralParams();
    p.thresholdDb = -40.0f;   // hard squash on the wet path
    p.ratio = 20.0f;
    p.attackMs = 0.1f;
    p.releaseMs = 50.0f;
    p.glue = 0.8f;
    p.satPre = 0.8f;
    p.satPost = 0.8f;
    p.heat = 0.7f;
    p.mix = 0.0f;
    p.limiter = false;

    Rig rig (p);
    juce::AudioBuffer<float> buffer = makeBusySignal (kSr, (int) (kSr * 0.5));
    const juce::AudioBuffer<float> reference = buffer;
    rig.process (buffer, 64);

    const float diff = maxAbsDiff (buffer, reference);
    info ("glue: mix=0 max sample difference = " + std::to_string (diff));
    check (diff < 1.0e-6f, "glue: mix=0 returns the untouched dry signal (zero latency)");
}

// -----------------------------------------------------------------------------
// 8. mix = 1 compresses
// -----------------------------------------------------------------------------
void testMixOneCompresses()
{
    MasterParams p = neutralParams();
    p.thresholdDb = -35.0f;
    p.ratio = 10.0f;
    p.attackMs = 1.0f;
    p.releaseMs = 120.0f;
    p.glue = 0.5f;
    p.mix = 1.0f;

    Rig rig (p);
    juce::AudioBuffer<float> buffer = makeBusySignal (kSr, (int) (kSr * 0.8));
    const juce::AudioBuffer<float> reference = buffer;
    rig.process (buffer, 64);

    const int settle = (int) (kSr * 0.4);
    const double reductionDb = 20.0 * std::log10 (rmsOf (buffer, settle) / rmsOf (reference, settle));
    info ("glue: mix=1 level change = " + std::to_string (reductionDb) + " dB");
    check (reductionDb < -1.5, "glue: mix=1 (wet) compresses the bus");
}

// -----------------------------------------------------------------------------
// 9. Ceiling limiter holds peaks with no overs
// -----------------------------------------------------------------------------
void testLimiterCeiling()
{
    for (float ceiling : { -3.0f, -6.0f })
    {
        MasterParams p = neutralParams();
        p.ratio = 1.0f;             // compression off: isolate the limiter
        p.thresholdDb = 0.0f;
        p.glue = 0.0f;
        p.satPre = 0.0f;
        p.satPost = 0.0f;
        p.heat = 0.0f;
        p.limiter = true;
        p.ceilingDb = ceiling;

        Rig rig (p);
        juce::AudioBuffer<float> buffer = makeSine (kSr, 997.0f, dbToAmp (18.0f), (int) (kSr * 0.4));
        rig.process (buffer, 128);

        const float peakDb = ampToDb (peakOf (buffer));
        info ("glue: +18 dBFS sine, ceiling " + std::to_string (ceiling) + " dB -> output peak "
              + std::to_string (peakDb) + " dBFS");
        check (peakDb <= ceiling + 0.1f, "glue: limiter holds peak at/below the ceiling");
        check (peakDb > ceiling - 1.0f, "glue: limiter actually limits (not muting)");
        check (allFinite (buffer), "glue: limiter output is finite");
    }
}

// -----------------------------------------------------------------------------
// 10. Sidechain high-pass: low frequencies do not over-duck the bus
// -----------------------------------------------------------------------------
void testSidechainHighPass()
{
    auto run = [] (float hz)
    {
        MasterParams p = neutralParams();
        p.thresholdDb = -30.0f;
        p.ratio = 10.0f;
        p.kneeDb = 0.0f;
        p.detect = 0.0f;
        p.sidechainHz = 500.0f;
        p.attackMs = 1.0f;
        p.releaseMs = 100.0f;
        return steadyStateGr (p, hz, dbToAmp (-12.0f), 0.6);
    };

    const float grLow = run (60.0f);
    const float grMid = run (1000.0f);
    info ("glue: sidechain 500 Hz, GR at 60 Hz = " + std::to_string (grLow)
          + " dB, at 1 kHz = " + std::to_string (grMid) + " dB");
    check (grMid > grLow + 6.0f, "glue: sidechain HPF keeps low-frequency content from ducking");
    check (grMid > 5.0f, "glue: 1 kHz trigger tone is compressed");
}

// -----------------------------------------------------------------------------
// 11. Auto release recovers differently from a long fixed release
// -----------------------------------------------------------------------------
float grAfterBurstAuto (bool autoRelease, float fixedReleaseMs)
{
    MasterParams p = neutralParams();
    p.thresholdDb = -30.0f;
    p.ratio = 10.0f;
    p.kneeDb = 0.0f;
    p.detect = 0.0f;
    p.attackMs = 1.0f;
    p.releaseMs = fixedReleaseMs;
    p.autoRelease = autoRelease;

    Rig rig (p);
    const int total = (int) (kSr * 0.75);
    juce::AudioBuffer<float> buffer (2, total);
    buffer.clear();

    const int burstEnd = (int) (kSr * 0.5);
    const float amp = dbToAmp (-6.0f);
    for (int i = 0; i < burstEnd; ++i)
    {
        const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                * 440.0 * (double) i / kSr);
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v);
    }

    const auto meter = rig.processWithMeter (buffer, 16);
    const int block = 16;
    const int measureAt = burstEnd + (int) (kSr * 0.15);
    float gr = 0.0f;
    for (int b = 0; b < (int) meter.size(); ++b)
        if ((b + 1) * block <= measureAt)
            gr = meter[(size_t) b];
    return gr;
}

void testAutoRelease()
{
    const float autoGr = grAfterBurstAuto (true, 1200.0f);
    const float fixedGr = grAfterBurstAuto (false, 1200.0f);
    info ("glue: 150 ms after burst: auto release GR = " + std::to_string (autoGr)
          + " dB, fixed 1200 ms release GR = " + std::to_string (fixedGr) + " dB");
    check (std::fabs (autoGr - fixedGr) > 1.0f, "glue: auto release recovery differs from a fixed release");
    check (autoGr < fixedGr, "glue: auto release recovers (faster) than a 1200 ms fixed release");
}

// -----------------------------------------------------------------------------
// 12. Determinism
// -----------------------------------------------------------------------------
std::vector<float> renderDeterministic (const MasterParams& p)
{
    Rig rig (p);
    juce::AudioBuffer<float> buffer = makeBusySignal (kSr, (int) (kSr * 0.4));
    rig.process (buffer, 64);

    std::vector<float> samples;
    samples.reserve ((size_t) buffer.getNumSamples() * 2);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            samples.push_back (buffer.getSample (ch, i));
    return samples;
}

void testDeterminism()
{
    MasterParams p;               // shipped defaults: auto release + sat + limiter
    const auto first = renderDeterministic (p);
    const auto second = renderDeterministic (p);
    check (first == second, "glue: same input/params -> bit-identical output");

    // reset() must return the machine to the same starting state.
    Rig rig (p);
    juce::AudioBuffer<float> buffer = makeBusySignal (kSr, (int) (kSr * 0.4));
    const juce::AudioBuffer<float> reference = buffer;
    rig.process (buffer, 64);
    rig.machine.reset();
    juce::AudioBuffer<float> again = reference;
    rig.process (again, 64);
    check (maxAbsDiff (buffer, again) < 1.0e-6f, "glue: reset() restores a deterministic state");
}

// -----------------------------------------------------------------------------
// 13. Parameter jumps stay smooth (no audible click)
// -----------------------------------------------------------------------------
void testParameterJumpSmoothness()
{
    MasterParams p = neutralParams();
    p.thresholdDb = -25.0f;
    p.ratio = 4.0f;
    p.attackMs = 8.0f;
    p.releaseMs = 150.0f;
    p.satPre = 0.0f;
    p.mix = 1.0f;
    p.outputDb = 0.0f;
    p.limiter = false;

    Rig rig (p);
    const int total = (int) (kSr * 0.4);
    juce::AudioBuffer<float> buffer = makeSine (kSr, 200.0f, 0.25f, total);
    const int jumpAt = (int) (kSr * 0.15);

    // First half at the initial settings.
    juce::AudioBuffer<float> head (buffer.getArrayOfWritePointers(), 2, 0, jumpAt);
    rig.machine.process (head, jumpAt);

    // A big, simultaneous jump of every "gain-like" control.
    MasterParams jumped = p;
    jumped.thresholdDb = -50.0f;
    jumped.ratio = 20.0f;
    jumped.satPre = 0.8f;
    jumped.satPost = 0.5f;
    jumped.heat = 1.0f;
    jumped.mix = 0.5f;
    jumped.outputDb = -12.0f;
    jumped.glue = 1.0f;
    rig.machine.setParams (jumped);

    juce::AudioBuffer<float> tail (buffer.getArrayOfWritePointers(), 2, jumpAt, total - jumpAt);
    rig.machine.process (tail, total - jumpAt);

    float maxDelta = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 1; i < total; ++i)
            maxDelta = juce::jmax (maxDelta, std::fabs (data[i] - data[i - 1]));
    }

    // The natural sample-to-sample step of a 200 Hz, 0.25 amplitude sine is
    // ~0.0065. A click would show up as a much larger jump.
    info ("glue: max sample-to-sample step across a full parameter jump = "
          + std::to_string (maxDelta));
    check (maxDelta < 0.05f, "glue: parameter jump produces no click-sized discontinuity");
}

// -----------------------------------------------------------------------------
// 14. Sample-rate independence of the static transfer curve
// -----------------------------------------------------------------------------
void testSampleRateIndependence()
{
    MasterParams p = neutralParams();
    p.thresholdDb = -30.0f;
    p.ratio = 8.0f;
    p.kneeDb = 4.0f;
    p.detect = 0.5f;
    p.attackMs = 2.0f;
    p.releaseMs = 120.0f;
    p.sidechainHz = 100.0f;

    auto measure = [&] (double sampleRate)
    {
        Rig rig (p, sampleRate);
        juce::AudioBuffer<float> buffer = makeSine (sampleRate, 1000.0f, dbToAmp (-12.0f),
                                                    (int) (sampleRate * 1.2));
        rig.process (buffer, 256);
        return std::make_pair (rmsDbOf (buffer, (int) (sampleRate * 0.9)),
                               rig.machine.getGainReductionDb());
    };

    const auto at44 = measure (44100.0);
    const auto at48 = measure (48000.0);
    const auto at96 = measure (96000.0);

    const float maxLevel = juce::jmax (at44.first, at48.first, at96.first);
    const float minLevel = juce::jmin (at44.first, at48.first, at96.first);
    info ("glue: 44.1/48/96 kHz output level = " + std::to_string (at44.first) + " / "
          + std::to_string (at48.first) + " / " + std::to_string (at96.first) + " dBFS");
    check (maxLevel - minLevel < 0.3f, "glue: static gain is sample-rate independent (+-0.3 dB)");

    const float grSpread = juce::jmax (at44.second, at48.second, at96.second)
                         - juce::jmin (at44.second, at48.second, at96.second);
    check (grSpread < 0.5f, "glue: gain reduction is sample-rate independent (+-0.5 dB)");
}

// -----------------------------------------------------------------------------
// 15. Mono path
// -----------------------------------------------------------------------------
void testMonoPath()
{
    MasterParams p = neutralParams();
    p.thresholdDb = -30.0f;
    p.ratio = 8.0f;
    p.kneeDb = 0.0f;
    p.detect = 0.0f;
    p.attackMs = 1.0f;
    p.releaseMs = 120.0f;
    p.limiter = true;

    Rig monoRig (p);
    juce::AudioBuffer<float> mono = makeSine (kSr, 440.0f, dbToAmp (-12.0f), (int) (kSr * 0.5), 1);
    monoRig.process (mono, 128);

    Rig stereoRig (p);
    juce::AudioBuffer<float> stereo = makeSine (kSr, 440.0f, dbToAmp (-12.0f), (int) (kSr * 0.5), 2);
    stereoRig.process (stereo, 128);

    check (allFinite (mono), "glue: mono output is finite");
    check (monoRig.machine.getGainReductionDb() > 3.0f, "glue: mono path compresses");
    check (maxAbsDiff (mono, stereo) < 1.0e-6f, "glue: mono and dual-mono produce the same output");
}

// -----------------------------------------------------------------------------
// 16. Hostile input: NaN / Inf / absurd levels stay finite
// -----------------------------------------------------------------------------
void testHostileInput()
{
    MasterParams p;               // default: limiter on
    Rig rig (p);

    juce::AudioBuffer<float> buffer (2, 2048);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float v = 0.0f;
        switch (i % 7)
        {
            case 0: v = std::numeric_limits<float>::quiet_NaN(); break;
            case 1: v = std::numeric_limits<float>::infinity(); break;
            case 2: v = -std::numeric_limits<float>::infinity(); break;
            case 3: v = 1.0e30f; break;
            case 4: v = -1.0e30f; break;
            case 5: v = 0.5f; break;
            default: v = -0.5f; break;
        }
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, -v);
    }
    rig.process (buffer, 32);

    const float peakDb = ampToDb (peakOf (buffer));
    check (allFinite (buffer), "glue: hostile input (NaN/Inf/1e30) produces finite output");
    check (peakDb <= p.ceilingDb + 0.1f, "glue: hostile input stays under the ceiling");
}

// -----------------------------------------------------------------------------
// 17. Block-size independence (including 1-sample blocks)
// -----------------------------------------------------------------------------
void testBlockSizeInvariance()
{
    MasterParams p = neutralParams();
    p.thresholdDb = -26.0f;
    p.ratio = 6.0f;
    p.kneeDb = 4.0f;
    p.detect = 0.5f;
    p.attackMs = 3.0f;
    p.releaseMs = 200.0f;
    p.limiter = true;

    const int numSamples = (int) (kSr * 0.15);
    const juce::AudioBuffer<float> source = makeBusySignal (kSr, numSamples);

    auto render = [&] (int blockSize, const MasterParams& settings)
    {
        Rig rig (settings);
        juce::AudioBuffer<float> buffer = source;
        rig.process (buffer, blockSize);
        return buffer;
    };

    // Compression/limiting path only.
    const juce::AudioBuffer<float> big = render (512, p);
    const juce::AudioBuffer<float> small = render (1, p);
    const juce::AudioBuffer<float> medium = render (64, p);

    const float diff1 = maxAbsDiff (big, small);
    info ("glue: block 512 vs 1 sample max difference = " + std::to_string (diff1));
    check (diff1 < 1.0e-6f, "glue: 1-sample blocks render identically to 512-sample blocks");
    check (maxAbsDiff (big, medium) < 1.0e-6f, "glue: 64-sample blocks render identically to 512");

    // Full character path: saturation, heat, glue macro and auto release are
    // all engaged. Control-rate updates are sample-timed, so this must still be
    // block-size invariant (no host-block-rate dependence).
    MasterParams character = p;
    character.glue = 0.6f;
    character.heat = 0.5f;
    character.satPre = 0.5f;
    character.satPost = 0.4f;
    character.autoRelease = true;

    const juce::AudioBuffer<float> bigChar = render (512, character);
    const juce::AudioBuffer<float> smallChar = render (1, character);
    const float diffChar = maxAbsDiff (bigChar, smallChar);
    info ("glue: character path block 512 vs 1 sample max difference = " + std::to_string (diffChar));
    check (diffChar < 1.0e-6f,
           "glue: saturated + auto-release path is block-size invariant (1 vs 512)");
}

// -----------------------------------------------------------------------------
// 18. DC / denormal robustness
// -----------------------------------------------------------------------------
void testDcAndDenormals()
{
    MasterParams p = neutralParams();
    p.limiter = true;

    Rig rig (p);
    const int numSamples = (int) (kSr * 0.5);
    juce::AudioBuffer<float> buffer (2, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const float v = i < numSamples / 2 ? 0.2f : 1.0e-30f;
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v);
    }
    rig.process (buffer, 128);

    check (allFinite (buffer), "glue: DC / denormal input stays finite");
    check (peakOf (buffer) < 1.0f, "glue: DC does not blow up the machine");

    // A long stream of very small values must not leave stuck state behind.
    juce::AudioBuffer<float> tail (2, 2048);
    for (int i = 0; i < tail.getNumSamples(); ++i)
    {
        tail.setSample (0, i, 1.0e-30f);
        tail.setSample (1, i, -1.0e-30f);
    }
    rig.process (tail, 256);
    check (allFinite (tail), "glue: denormal-range input stays finite");
}

// -----------------------------------------------------------------------------
// 19. Saturation adds harmonics and softens peaks
// -----------------------------------------------------------------------------
struct HarmonicResult
{
    double residualRms = 0.0;
    float peak = 0.0f;
    bool finite = true;
};

HarmonicResult analyse220 (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const int windowStart = (int) (sampleRate * 0.2);
    const int windowLength = (int) std::lround (sampleRate * 0.1);   // 10 ms chunks
    double sumI = 0.0, sumQ = 0.0;
    int count = 0;
    for (int i = windowStart; i < windowStart + windowLength && i < buffer.getNumSamples(); ++i)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / sampleRate;
        const float v = buffer.getSample (0, i);
        sumI += (double) v * std::sin (phase);
        sumQ += (double) v * std::cos (phase);
        ++count;
    }

    HarmonicResult result;
    if (count == 0)
        return result;

    const double a = 2.0 * sumI / count;
    const double b = 2.0 * sumQ / count;
    double residualSum = 0.0;
    for (int i = windowStart; i < windowStart + windowLength && i < buffer.getNumSamples(); ++i)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / sampleRate;
        const double fundamental = a * std::sin (phase) + b * std::cos (phase);
        const double rest = (double) buffer.getSample (0, i) - fundamental;
        residualSum += rest * rest;
    }
    result.residualRms = std::sqrt (residualSum / count);
    result.peak = peakOf (buffer, windowStart);
    result.finite = allFinite (buffer);
    return result;
}

void testSaturationCharacter()
{
    MasterParams clean = neutralParams();
    clean.ratio = 1.0f;
    clean.thresholdDb = 0.0f;
    clean.mix = 1.0f;
    clean.attackMs = 5.0f;
    clean.releaseMs = 100.0f;

    MasterParams driven = clean;
    driven.satPre = 0.85f;
    driven.heat = 0.5f;

    const int numSamples = (int) (kSr * 0.4);
    const float amp = 0.35f;

    Rig cleanRig (clean);
    juce::AudioBuffer<float> cleanBuffer = makeSine (kSr, 220.0f, amp, numSamples);
    cleanRig.process (cleanBuffer, 128);

    Rig drivenRig (driven);
    juce::AudioBuffer<float> drivenBuffer = makeSine (kSr, 220.0f, amp, numSamples);
    drivenRig.process (drivenBuffer, 128);

    const auto a = analyse220 (cleanBuffer, kSr);
    const auto b = analyse220 (drivenBuffer, kSr);

    info ("glue: saturation residual (harmonics) clean = " + std::to_string (a.residualRms)
          + ", driven = " + std::to_string (b.residualRms));
    check (b.finite && a.finite, "glue: saturation output is finite");
    check (b.residualRms > 0.01, "glue: driven saturation generates audible harmonics");
    check (b.residualRms > 10.0 * juce::jmax (a.residualRms, 1.0e-6),
           "glue: saturation adds far more harmonics than the clean path");
    check (b.peak <= a.peak + 0.02f, "glue: saturation soft-clips rather than boosting peaks");
}

// -----------------------------------------------------------------------------
// 20. Default settings leave sensible headroom on a hot drum bus
// -----------------------------------------------------------------------------
void testDefaultHeadroom()
{
    MasterParams p;               // exactly the shipped defaults
    Rig rig (p);

    juce::AudioBuffer<float> buffer = makeHotDrumBus (kSr, 2.0);
    const juce::AudioBuffer<float> reference = buffer;
    rig.process (buffer, 128);

    const float inputPeakDb = ampToDb (peakOf (reference));
    const float outputPeakDb = ampToDb (peakOf (buffer));
    const float outputRmsDb = rmsDbOf (buffer, (int) (kSr * 0.5));
    info ("glue: hot drum bus default: input peak " + std::to_string (inputPeakDb)
          + " dBFS -> output peak " + std::to_string (outputPeakDb) + " dBFS, output RMS "
          + std::to_string (outputRmsDb) + " dBFS");

    check (allFinite (buffer), "glue: default drum-bus render is finite");
    checkInRange (outputPeakDb, -6.0f, -0.2f,
                  "glue: default hot drum bus leaves sensible headroom (peak -6..-0.2 dBFS)");
    check (outputPeakDb <= p.ceilingDb + 0.1f, "glue: default limiter ceiling is respected");
}

} // namespace

void runMasterTests()
{
    std::printf ("-- master glue machine --\n");

    testSilence();
    testBelowThresholdUnity();
    testAboveThresholdGainReduction();
    testRatioAndGlueIncreaseGainReduction();
    testAttackDirection();
    testReleaseDirection();
    testMixZeroIsDry();
    testMixOneCompresses();
    testLimiterCeiling();
    testSidechainHighPass();
    testAutoRelease();
    testDeterminism();
    testParameterJumpSmoothness();
    testSampleRateIndependence();
    testMonoPath();
    testHostileInput();
    testBlockSizeInvariance();
    testDcAndDenormals();
    testSaturationCharacter();
    testDefaultHeadroom();
}
