// -----------------------------------------------------------------------------
// KLOPGEEST — the enormous analog-inspired glue machine.
//
// Topology (all zero latency, no look ahead):
//
//   in ──┬──────────────────────────────────────────────────── dry ──────────┐
//        │                                                                    ├─ out gain ─ limiter ─ out
//        └─ pre-sat ─┬─ sidechain HPF ─ peak/RMS detector ─ soft-knee GR ─ post-sat ─ parallel mix ─┘
//                    └─ (detector tap is post pre-saturation)
//
// The compressor is a dB-domain soft-knee design with stereo-linked detection,
// ballistically smoothed gain reduction, an optional program-adaptive release,
// and a partial (75 %) auto make-up gain so the machine can be driven hard
// while still leaving sensible headroom. The dry path is the untouched input,
// so mix = 0 is bit-transparent and there is no latency to compensate.
//
// Saturation is a temperature-stable, bias-asymmetric tanh stage with gentle
// pre/post band-limiting (a one-pole low-pass either side of the non-linearity)
// for even harmonics without fizz. It is deliberately not oversampled: the
// stage is kept gentle enough that aliasing stays below the noise of the bus.
//
// Everything below is real-time safe: no allocation, no locks, no I/O, fixed
// state, denormal-flushed (ScopedNoDenormals + explicit state flushes).
// -----------------------------------------------------------------------------
#include "dsp/master/GlueMachine.h"

#include <algorithm>
#include <cmath>

namespace klopgeest
{

namespace
{

constexpr float kLn10Over20 = 0.11512925464970229f;   // ln(10) / 20
constexpr float kSilenceAmp = 1.0e-8f;

// Smoothing time for every continuous parameter. Fast enough to feel like a
// control, slow enough that host automation and block-rate parameter changes
// cannot click.
constexpr float kParamSmoothSeconds = 0.005f;

// Block-rate work (macro -> target conditioning, sidechain coefficient updates,
// saturator filter coefficients) runs on this many samples of absolute time,
// never on host block boundaries. That keeps the machine's output independent
// of the host block size (including 1-sample blocks).
constexpr int kControlInterval = 32;

// How much of the computed gain reduction is given back as automatic make-up
// gain. 0.75 keeps transients alive while stopping the bus from collapsing
// when it is driven hard (the remaining 25 % is the compression you hear).
constexpr float kMakeupFactor = 0.75f;

// Saturator character.
constexpr float kSatDriveRange = 4.0f;   // drive = 1 .. 5
constexpr float kSatBiasRange  = 0.08f;  // asymmetric bias -> even harmonics

inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float clamp01 (float v) noexcept
{
    return clampf (v, 0.0f, 1.0f);
}

inline float dbToGain (float db) noexcept
{
    return std::exp (db * kLn10Over20);
}

inline float ampToDb (float amp) noexcept
{
    return amp > kSilenceAmp ? 20.0f * std::log10 (amp) : -100.0f;
}

inline float onePoleCoefficient (float milliseconds, double sampleRate) noexcept
{
    const double tauSamples = juce::jmax (1.0, (double) milliseconds * 0.001 * sampleRate);
    return (float) (1.0 - std::exp (-1.0 / tauSamples));
}

/** One-pole parameter smoother (per-sample). */
struct Smoothed
{
    float current = 0.0f;
    float target  = 0.0f;
    float coeff   = 0.0f;

    void setTimeConstant (float seconds, double sampleRate) noexcept
    {
        const double tauSamples = juce::jmax (1.0, (double) seconds * sampleRate);
        coeff = (float) (1.0 - std::exp (-1.0 / tauSamples));
    }

    void set (float v) noexcept { target = v; }
    void snap (float v) noexcept { current = target = v; }
    void snapToTarget() noexcept { current = target; }

    inline float next() noexcept
    {
        current += (target - current) * coeff;
        return current;
    }
};

/** One-pole low-pass with per-channel state. */
struct OnePoleLP
{
    float z = 0.0f;
    float coeff = 1.0f;

    void reset() noexcept { z = 0.0f; }

    void setCutoff (double sampleRate, float hz) noexcept
    {
        const double nyquistGuard = sampleRate * 0.45;
        const double f = juce::jlimit (20.0, nyquistGuard, (double) hz);
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
        coeff = (float) (1.0 - std::exp (-w));
    }

    inline float process (float x) noexcept
    {
        z += coeff * (x - z);
        return z;
    }
};

/** RBJ 2nd-order Butterworth-style high-pass, one state pair per channel. */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    void setHighPass (double sampleRate, float hz, float q) noexcept
    {
        const double nyquistGuard = sampleRate * 0.45;
        const double f = juce::jlimit (10.0, nyquistGuard, (double) hz);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
        const double cs = std::cos (w0);
        const double sn = std::sin (w0);
        const double alpha = sn / (2.0 * (double) q);
        const double a0 = 1.0 + alpha;

        b0 = (float) (((1.0 + cs) * 0.5) / a0);
        b1 = (float) (-(1.0 + cs) / a0);
        b2 = b0;
        a1 = (float) (-2.0 * cs / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    void copyCoefficientsFrom (const Biquad& other) noexcept
    {
        b0 = other.b0; b1 = other.b1; b2 = other.b2;
        a1 = other.a1; a2 = other.a2;
    }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/**
    Warm, bias-asymmetric tanh saturator.

    small-signal gain is exactly unity (the curve is normalised by its slope at
    the origin) and the dry signal is cross-faded against the shaped signal by
    the amount, so the effect engages without a level jump. A gentle low-pass
    before and after the non-linearity keeps the generated harmonics from
    turning into fizz.
*/
struct Saturator
{
    OnePoleLP preLp  [2];
    OnePoleLP postLp [2];

    void reset() noexcept
    {
        preLp[0].reset();  preLp[1].reset();
        postLp[0].reset(); postLp[1].reset();
    }

    void configure (double sampleRate, float amount) noexcept
    {
        // Darken slightly as the stage is driven: fewer fizzy upper harmonics.
        const float cutoff = juce::jmin (16500.0f - 3500.0f * amount,
                                         (float) (sampleRate * 0.45));
        for (int ch = 0; ch < 2; ++ch)
        {
            preLp[ch].setCutoff (sampleRate, cutoff);
            postLp[ch].setCutoff (sampleRate, cutoff);
        }
    }

    inline float process (float x, int channel, float amount) noexcept
    {
        if (amount < 1.0e-5f)
            return x;

        const float drive = 1.0f + kSatDriveRange * amount;
        const float bias  = kSatBiasRange * amount;
        const float dc    = std::tanh (drive * bias);
        const float norm  = 1.0f / (drive * (1.0f - dc * dc));

        const float pre = preLp[channel].process (x);
        const float shaped = (std::tanh (drive * (pre + bias)) - dc) * norm;
        const float post = postLp[channel].process (shaped);

        return x + amount * (post - x);
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------------
struct GlueMachine::Impl
{
    static constexpr int kMaxChannels = 2;

    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    MasterParams params;

    // --- smooothing ---------------------------------------------------------
    Smoothed sThresholdDb, sRatio, sKneeDb, sDetect;
    Smoothed sMix, sSatPre, sSatPost;
    Smoothed sOutGainLin, sCeilingLin;
    Smoothed sAttackCoef, sReleaseCoef;
    Smoothed sSidechainLog2;

    // --- detector -----------------------------------------------------------
    Biquad scFilter [kMaxChannels];
    float  scLastHz = -1.0f;
    float  peakEnv = 0.0f;
    float  rmsSq = 0.0f;
    float  peakAttackCoef = 0.0f, peakReleaseCoef = 0.0f, rmsCoef = 0.0f;

    // --- program statistics (auto release) ----------------------------------
    float density = 0.0f;        // 0..1 "how hard are we squashing right now"
    float statPeak = 0.0f;
    float statRmsSq = 0.0f;
    float statPeakReleaseCoef = 0.0f, statRmsCoef = 0.0f, densityCoef = 0.0f;

    // --- compressor / limiter state ----------------------------------------
    float grDb = 0.0f;
    float limiterGain = 1.0f;
    float limiterReleaseCoef = 0.0f;

    // --- saturation ---------------------------------------------------------
    Saturator preSat, postSat;

    // --- metering -----------------------------------------------------------
    float outputPeakDb = -100.0f;
    float outputRmsDb = -100.0f;

    bool snapPending = true;
    int controlCounter = 0;

    Impl()
    {
        setDefaultSmoothingTimes();
    }

    // -------------------------------------------------------------------------
    void setDefaultSmoothingTimes() noexcept
    {
        sThresholdDb.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sRatio.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sKneeDb.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sDetect.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sMix.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sSatPre.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sSatPost.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sOutGainLin.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sCeilingLin.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sAttackCoef.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sReleaseCoef.setTimeConstant (kParamSmoothSeconds, sampleRate);
        sSidechainLog2.setTimeConstant (0.008f, sampleRate);
    }

    void updateSampleRateDependentState() noexcept
    {
        const double sr = juce::jmax (8000.0, sampleRate);

        // Peak detector: near-instant attack, short release smoothing.
        peakAttackCoef  = (float) (1.0 - std::exp (-1.0 / (0.0001 * sr)));
        peakReleaseCoef = (float) (1.0 - std::exp (-1.0 / (0.008  * sr)));
        // RMS detector: ~20 ms power window.
        rmsCoef         = (float) (1.0 - std::exp (-1.0 / (0.020  * sr)));

        // Program statistics.
        statPeakReleaseCoef = (float) (1.0 - std::exp (-1.0 / (0.250 * sr)));
        statRmsCoef         = (float) (1.0 - std::exp (-1.0 / (0.400 * sr)));
        densityCoef         = (float) (1.0 - std::exp (-1.0 / (0.600 * sr)));

        // Limiter gain recovery.
        limiterReleaseCoef  = (float) (1.0 - std::exp (-1.0 / (0.120 * sr)));
    }

    // -------------------------------------------------------------------------
    // Block-rate parameter conditioning: macros -> effective target values.
    // -------------------------------------------------------------------------
    void updateBlockTargets() noexcept
    {
        const float glue = clamp01 (params.glue);
        const float heat = clamp01 (params.heat);

        // GLUE is a musical "more record" macro: lower threshold, higher ratio,
        // slightly slower attack (let transients through, compress the body),
        // a touch more RMS detection and a higher sidechain corner so the kick
        // does not dominate the detector.
        const float thresholdDb = clampf (params.thresholdDb - 6.0f * glue, -60.0f, 0.0f);
        const float ratio       = clampf (params.ratio * (1.0f + 1.25f * glue), 1.0f, 20.0f);
        const float kneeDb      = clampf (params.kneeDb, 0.0f, 24.0f);
        const float detect      = clamp01 (params.detect + 0.25f * glue);
        const float attackMs    = clampf (params.attackMs * (1.0f + 0.60f * glue), 0.1f, 100.0f);
        const float releaseMs   = clampf (params.releaseMs * (1.0f + 0.40f * glue), 20.0f, 1200.0f);
        const float sidechainHz = clampf (params.sidechainHz * (1.0f + 0.60f * glue), 20.0f, 500.0f);

        // Program-adaptive release: dense, sustained compression releases slowly
        // (gluey), transient material releases quickly (punchy). 80..600 ms.
        float crestDb = 0.0f;
        const float statRms = std::sqrt (juce::jmax (statRmsSq, 1.0e-20f));
        if (statRms > 1.0e-6f && statPeak > 1.0e-6f)
            crestDb = clampf (20.0f * std::log10 (statPeak / statRms), 0.0f, 24.0f);
        const float crestNorm = clamp01 ((crestDb - 3.0f) / 12.0f);
        const float autoReleaseMs = clampf (80.0f + 440.0f * density + 80.0f * crestNorm,
                                            80.0f, 600.0f);
        const float releaseEffectiveMs = params.autoRelease ? autoReleaseMs : releaseMs;

        sThresholdDb.set (thresholdDb);
        sRatio.set (ratio);
        sKneeDb.set (kneeDb);
        sDetect.set (detect);
        sAttackCoef.set (onePoleCoefficient (attackMs, sampleRate));
        sReleaseCoef.set (onePoleCoefficient (releaseEffectiveMs, sampleRate));
        sSidechainLog2.set (std::log2 (sidechainHz));
        sMix.set (clamp01 (params.mix));

        // HEAT adds harmonic drive to both saturators; GLUE does not clip.
        sSatPre.set (clamp01 (params.satPre + 0.55f * heat));
        sSatPost.set (clamp01 (params.satPost + 0.45f * heat));
        sOutGainLin.set (dbToGain (clampf (params.outputDb, -24.0f, 12.0f)));
        sCeilingLin.set (dbToGain (clampf (params.ceilingDb, -12.0f, 0.0f)));
    }

    void snapAllSmoothers() noexcept
    {
        sThresholdDb.snapToTarget();
        sRatio.snapToTarget();
        sKneeDb.snapToTarget();
        sDetect.snapToTarget();
        sMix.snapToTarget();
        sSatPre.snapToTarget();
        sSatPost.snapToTarget();
        sOutGainLin.snapToTarget();
        sCeilingLin.snapToTarget();
        sAttackCoef.snapToTarget();
        sReleaseCoef.snapToTarget();
        sSidechainLog2.snapToTarget();
    }

    void updateSidechainFilterIfNeeded() noexcept
    {
        const float hz = clampf (std::exp2 (sSidechainLog2.current), 20.0f, 500.0f);
        const bool needsUpdate = scLastHz < 0.0f
                              || std::abs (hz - scLastHz) > 0.002f * scLastHz + 0.01f;
        if (needsUpdate)
        {
            scFilter[0].setHighPass (sampleRate, hz, 0.7071067811865476f);
            scFilter[1].copyCoefficientsFrom (scFilter[0]);
            scLastHz = hz;
        }
    }

    void configureSaturators() noexcept
    {
        preSat.configure (sampleRate, sSatPre.current);
        postSat.configure (sampleRate, sSatPost.current);
    }

    /** All block-rate work, scheduled on absolute sample time. */
    void runControlUpdate() noexcept
    {
        updateBlockTargets();
        updateSidechainFilterIfNeeded();
        configureSaturators();
    }

    void sanitise (const MasterParams& in, MasterParams& out) noexcept
    {
        out = in;
        auto clean = [] (float& value, float fallback)
        {
            if (! std::isfinite (value))
                value = fallback;
        };
        clean (out.thresholdDb, -18.0f);
        clean (out.ratio, 4.0f);
        clean (out.attackMs, 8.0f);
        clean (out.releaseMs, 180.0f);
        clean (out.kneeDb, 8.0f);
        clean (out.sidechainHz, 90.0f);
        clean (out.detect, 0.5f);
        clean (out.mix, 1.0f);
        clean (out.satPre, 0.15f);
        clean (out.satPost, 0.10f);
        clean (out.outputDb, 0.0f);
        clean (out.ceilingDb, -0.4f);
        clean (out.glue, 0.35f);
        clean (out.heat, 0.25f);
    }

    // -------------------------------------------------------------------------
    void prepare (double newSampleRate, int newMaxBlockSize)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        maxBlockSize = juce::jmax (1, newMaxBlockSize);
        setDefaultSmoothingTimes();
        updateSampleRateDependentState();
        reset();
    }

    void reset()
    {
        for (auto& filter : scFilter)
            filter.reset();

        scLastHz = -1.0f;
        peakEnv = 0.0f;
        rmsSq = 0.0f;
        density = 0.0f;
        statPeak = 0.0f;
        statRmsSq = 0.0f;
        grDb = 0.0f;
        limiterGain = 1.0f;
        preSat.reset();
        postSat.reset();
        outputPeakDb = -100.0f;
        outputRmsDb = -100.0f;

        updateSampleRateDependentState();
        updateBlockTargets();
        snapAllSmoothers();
        updateSidechainFilterIfNeeded();
        configureSaturators();
        snapPending = true;
        controlCounter = 0;
    }

    void setParams (const MasterParams& newParams) noexcept
    {
        sanitise (newParams, params);

        if (snapPending)
        {
            updateBlockTargets();
            snapAllSmoothers();
            updateSidechainFilterIfNeeded();
            configureSaturators();
            snapPending = false;
        }
    }

    // -------------------------------------------------------------------------
    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        const int channels = juce::jmin (kMaxChannels, buffer.getNumChannels());
        const int numToProcess = juce::jmin (numSamples, buffer.getNumSamples());
        if (channels < 1 || numToProcess < 1)
            return;

        juce::ScopedNoDenormals noDenormals;

        // Control-rate work at the start of the block only if we are exactly at
        // a control boundary (sample-timed, not block-timed).
        if (controlCounter == 0)
            runControlUpdate();

        float* left  = buffer.getWritePointer (0);
        float* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;

        const bool limiterEnabled = params.limiter;

        float blockPeak = 0.0f;
        double blockSumSq = 0.0;

        for (int i = 0; i < numToProcess; ++i)
        {
            // --- read / sanitise --------------------------------------------
            float inL = left[i];
            float inR = right != nullptr ? right[i] : inL;

            if (! std::isfinite (inL)) inL = 0.0f;
            if (! std::isfinite (inR)) inR = 0.0f;
            inL = clampf (inL, -32.0f, 32.0f);
            inR = clampf (inR, -32.0f, 32.0f);

            const float dryL = inL;
            const float dryR = inR;

            // Keep the (block-rate) sidechain filter coefficient smoother moving.
            sSidechainLog2.next();

            // --- pre-compression saturation --------------------------------
            const float satPreAmount = sSatPre.next();
            float wetL = preSat.process (inL, 0, satPreAmount);
            float wetR = preSat.process (inR, 1, satPreAmount);

            // --- stereo-linked detection with sidechain high-pass ----------
            const float scL = scFilter[0].process (wetL);
            const float scR = scFilter[1].process (wetR);

            const float rectL = std::fabs (scL);
            const float rectR = std::fabs (scR);
            const float rectPeak = rectL > rectR ? rectL : rectR;

            // Peak detector (fast rectifier + short release smoothing).
            if (rectPeak > peakEnv)
                peakEnv += (rectPeak - peakEnv) * peakAttackCoef;
            else
                peakEnv += (rectPeak - peakEnv) * peakReleaseCoef;

            // RMS detector (~20 ms power window).
            const float power = 0.5f * (scL * scL + scR * scR);
            rmsSq += (power - rmsSq) * rmsCoef;

            const float detectBlend = sDetect.next();
            const float rmsAmp = std::sqrt (juce::jmax (rmsSq, 0.0f));
            const float detectorAmp = (1.0f - detectBlend) * peakEnv + detectBlend * rmsAmp;
            const float detectorDb = detectorAmp > 1.0e-9f
                                         ? 20.0f * std::log10 (detectorAmp)
                                         : -100.0f;

            // --- program statistics for the auto release -------------------
            density += (clamp01 (grDb / 6.0f) - density) * densityCoef;
            if (detectorAmp > statPeak)
                statPeak = detectorAmp;
            else
                statPeak += (detectorAmp - statPeak) * statPeakReleaseCoef;
            statRmsSq += (detectorAmp * detectorAmp - statRmsSq) * statRmsCoef;

            // --- soft-knee gain computer -----------------------------------
            const float thresholdDb = sThresholdDb.next();
            const float ratio       = sRatio.next();
            const float kneeDb      = sKneeDb.next();

            float targetGrDb = 0.0f;
            if (ratio > 1.0001f)
            {
                const float overDb = detectorDb - thresholdDb;
                const float slope  = 1.0f - 1.0f / ratio;
                const float halfKnee = 0.5f * kneeDb;

                if (overDb >= halfKnee)
                {
                    targetGrDb = overDb * slope;
                }
                else if (kneeDb > 0.0f && overDb > -halfKnee)
                {
                    const float t = overDb + halfKnee;
                    targetGrDb = slope * (t * t) / (2.0f * kneeDb);
                }
            }

            // --- ballistics -------------------------------------------------
            const float attackCoef  = sAttackCoef.next();
            const float releaseCoef = sReleaseCoef.next();
            if (targetGrDb > grDb)
                grDb += (targetGrDb - grDb) * attackCoef;
            else
                grDb += (targetGrDb - grDb) * releaseCoef;

            if (grDb < 0.0f) grDb = 0.0f;
            if (! std::isfinite (grDb)) grDb = 0.0f;

            // --- apply gain reduction with 75 % auto make-up ---------------
            // (gain dB = makeup - reduction = -(1 - makeupFactor) * gr)
            const float wetGain = dbToGain (-(1.0f - kMakeupFactor) * grDb);
            wetL *= wetGain;
            wetR *= wetGain;

            // --- post-compression saturation -------------------------------
            const float satPostAmount = sSatPost.next();
            wetL = postSat.process (wetL, 0, satPostAmount);
            wetR = postSat.process (wetR, 1, satPostAmount);

            // --- parallel mix ----------------------------------------------
            const float mix = sMix.next();
            float outL = dryL + mix * (wetL - dryL);
            float outR = dryR + mix * (wetR - dryR);

            // --- output gain ------------------------------------------------
            const float outputGain = sOutGainLin.next();
            outL *= outputGain;
            outR *= outputGain;

            // --- ceiling limiter (zero look ahead) --------------------------
            const float ceiling = sCeilingLin.next();
            if (limiterEnabled)
            {
                const float outPeak = juce::jmax (std::fabs (outL), std::fabs (outR));
                const float desiredGain = outPeak > ceiling ? ceiling / outPeak : 1.0f;

                if (desiredGain < limiterGain)
                    limiterGain = desiredGain;                       // instant attack
                else
                    limiterGain += (1.0f - limiterGain) * limiterReleaseCoef;

                outL *= limiterGain;
                outR *= limiterGain;

                // Guarantee no overs from rounding: the ceiling is a hard wall.
                outL = clampf (outL, -ceiling, ceiling);
                outR = clampf (outR, -ceiling, ceiling);
            }
            else
            {
                // Relax the limiter gain while bypassed so re-engaging is smooth.
                limiterGain += (1.0f - limiterGain) * limiterReleaseCoef;
            }

            if (! std::isfinite (outL)) outL = 0.0f;
            if (! std::isfinite (outR)) outR = 0.0f;

            // --- write / meter ---------------------------------------------
            left[i] = outL;
            if (right != nullptr)
                right[i] = outR;

            blockPeak = juce::jmax (blockPeak, std::fabs (outL), std::fabs (outR));
            blockSumSq += (double) outL * outL + (double) outR * outR;

            // Advance the sample-timed control clock.
            if (++controlCounter >= kControlInterval)
            {
                controlCounter = 0;
                runControlUpdate();
            }
        }

        outputPeakDb = ampToDb (blockPeak);
        const double meanSq = blockSumSq / (double) (numToProcess * (right != nullptr ? 2 : 1));
        outputRmsDb = ampToDb ((float) std::sqrt (juce::jmax (0.0, meanSq)));
    }

    float getGainReductionDb() const noexcept
    {
        return juce::jlimit (0.0f, 60.0f, grDb);
    }

    float getOutputPeakDb() const noexcept { return outputPeakDb; }
    float getOutputRmsDb() const noexcept  { return outputRmsDb; }
};

// -----------------------------------------------------------------------------
// Public facade
// -----------------------------------------------------------------------------
GlueMachine::GlueMachine() : impl (std::make_unique<Impl>()) {}
GlueMachine::~GlueMachine() = default;

void GlueMachine::prepare (double sampleRate, int maxBlockSize)
{
    impl->prepare (sampleRate, maxBlockSize);
}

void GlueMachine::reset()
{
    impl->reset();
}

void GlueMachine::setParams (const MasterParams& params)
{
    impl->setParams (params);
}

void GlueMachine::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    impl->process (buffer, numSamples);
}

float GlueMachine::getGainReductionDb() const noexcept
{
    return impl->getGainReductionDb();
}

float GlueMachine::getOutputPeakDb() const noexcept
{
    return impl->getOutputPeakDb();
}

float GlueMachine::getOutputRmsDb() const noexcept
{
    return impl->getOutputRmsDb();
}

} // namespace klopgeest
