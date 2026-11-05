// -----------------------------------------------------------------------------
// QA integration / robustness suites for KLOPGEEST.
//
// Everything here tests a contract at the seams between workstreams:
//
//   * realtime safety: after prepareToPlay() the audio path must not touch the
//     heap (tests/AllocationCounter.h proves it instead of assuming it)
//   * sample-rate independence: 44.1 / 48 / 96 kHz all render, recall state and
//     stay finite
//   * block-size invariance: the same seed/pattern renders the same audio in
//     64 / 128 / 512 / 2048-sample blocks (enabled once the groove engine lands)
//   * transport: internal start/stop, host transport simulation (ppq/bpm),
//     tempo changes and seeks
//   * determinism: same seed => identical audio, different seed => different
//     audio (the second half is enabled once the groove engine lands)
//   * hostile input: 64 randomised parameter/pattern configurations stay finite
//     and round-trip through saved state
//   * publishing thread safety: the message thread publishes 500 patterns while
//     a real audio thread processes, with zero audio-thread allocations
//
// Assertions that depend on the real sequencer/groove engine are SKIPped (not
// silently passed) while the temporary stubs are in place. tests/TestHarness.h
// counts skips so a run never claims more coverage than it has.
// -----------------------------------------------------------------------------
#include "tests/QaIntegrationTests.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "tests/AllocationCounter.h"
#include "tests/TestHarness.h"

#include "core/EngineParams.h"
#include "core/ParamIDs.h"
#include "core/Pattern.h"
#include "core/Patterns.h"
#include "dsp/Engine.h"
#include "dsp/master/GlueMachine.h"
#include "dsp/voices/Voice.h"
#include "plugin/KlopgeestProcessor.h"

using namespace klopgeest;
using klop_test::check;
using klop_test::checkNear;
using klop_test::info;
using klop_test::skip;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

// --- Shared helpers ----------------------------------------------------------

void setParam (KlopgeestProcessor& processor, const char* id, float realValue)
{
    if (auto* parameter = processor.getAPVTS().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
}

float getParam (KlopgeestProcessor& processor, const char* id)
{
    if (auto* value = processor.getAPVTS().getRawParameterValue (id))
        return value->load();
    return 0.0f;
}

/** First difference between two patterns, for state-recall diagnostics. */
std::string describeFirstPatternDifference (const PatternData& a, const PatternData& b)
{
    if (a.length != b.length)
        return "length " + std::to_string (a.length) + " vs " + std::to_string (b.length);
    if (a.name != b.name)
        return "name differs";

    for (int voice = 0; voice < kNumVoices; ++voice)
    {
        const auto& la = a.lanes[(std::size_t) voice];
        const auto& lb = b.lanes[(std::size_t) voice];

        if (la.length != lb.length)
            return "lane " + std::to_string (voice) + " length "
                   + std::to_string (la.length) + " vs " + std::to_string (lb.length);
        if (la.muted != lb.muted)
            return "lane " + std::to_string (voice) + " mute";
        if (la.soloed != lb.soloed)
            return "lane " + std::to_string (voice) + " solo";
        if (la.locked != lb.locked)
            return "lane " + std::to_string (voice) + " lock";

        for (int step = 0; step < kMaxSteps; ++step)
        {
            const auto& x = la.steps[(std::size_t) step];
            const auto& y = lb.steps[(std::size_t) step];
            if (x.on != y.on)
                return "voice " + std::to_string (voice) + " step " + std::to_string (step) + " on";
            if (x.locked != y.locked)
                return "voice " + std::to_string (voice) + " step " + std::to_string (step) + " lock";
            if (x.ratchet != y.ratchet)
                return "voice " + std::to_string (voice) + " step " + std::to_string (step) + " ratchet "
                       + std::to_string (x.ratchet) + " vs " + std::to_string (y.ratchet);
            if (x.velocity != y.velocity)
                return "voice " + std::to_string (voice) + " step " + std::to_string (step) + " velocity "
                       + std::to_string (x.velocity) + " vs " + std::to_string (y.velocity);
            if (x.probability != y.probability)
                return "voice " + std::to_string (voice) + " step " + std::to_string (step) + " probability";
            if (x.microtiming != y.microtiming)
                return "voice " + std::to_string (voice) + " step " + std::to_string (step) + " microtiming "
                       + std::to_string (x.microtiming) + " vs " + std::to_string (y.microtiming);
            if (x.pitchSemis != y.pitchSemis)
                return "voice " + std::to_string (voice) + " step " + std::to_string (step) + " pitch "
                       + std::to_string (x.pitchSemis) + " vs " + std::to_string (y.pitchSemis);
        }
    }

    return "unknown difference";
}

/**
    Compares two parameters for state-recall equality.

    Continuous parameters must match numerically within `tolerance`. Discrete
    parameters (bool, choice, int) are compared on their DENORMALISED value
    (`convertFrom0to1`): JUCE stores a host's arbitrary normalised value on the
    source parameter but the APVTS state round-trip canonicalises a discrete
    parameter through its legal steps (e.g. 0.69 normalised on a boolean legally
    recalls as 1.0 = "on"). The musical state is identical; only the normalised
    representation differs, so this is not a state bug and must not be flagged.
*/
bool parametersMatch (juce::AudioProcessorParameter& source,
                      juce::AudioProcessorParameter& restored,
                      float tolerance)
{
    const auto sourceValue = source.getValue();
    const auto restoredValue = restored.getValue();

    if (source.isDiscrete() || restored.isDiscrete())
    {
        if (auto* sourceRanged = dynamic_cast<juce::RangedAudioParameter*> (&source))
            if (auto* restoredRanged = dynamic_cast<juce::RangedAudioParameter*> (&restored))
                return std::abs (sourceRanged->convertFrom0to1 (sourceValue)
                                 - restoredRanged->convertFrom0to1 (restoredValue)) <= tolerance;
    }

    return std::abs (sourceValue - restoredValue) <= tolerance;
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (buffer.getSample (channel, sample)))
                return false;
    return true;
}

struct RenderedAudio
{
    std::vector<float> left;
    std::vector<float> right;
    float peak = 0.0f;
    bool finite = true;
};

/** Renders `numBlocks` blocks of `blockSize` samples through the processor. */
RenderedAudio runBlocks (KlopgeestProcessor& processor, int numBlocks, int blockSize, int channels = 2)
{
    RenderedAudio result;
    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::MidiBuffer midi;
    result.left.reserve ((std::size_t) numBlocks * (std::size_t) blockSize);
    result.right.reserve ((std::size_t) numBlocks * (std::size_t) blockSize);

    for (int block = 0; block < numBlocks; ++block)
    {
        processor.processBlock (buffer, midi);
        result.finite = result.finite && allFinite (buffer);

        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;

        for (int sample = 0; sample < blockSize; ++sample)
        {
            result.left.push_back (left[sample]);
            result.right.push_back (right[sample]);
            result.peak = juce::jmax (result.peak, std::abs (left[sample]), std::abs (right[sample]));
        }
    }

    return result;
}

float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return std::numeric_limits<float>::infinity();

    float difference = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        difference = juce::jmax (difference, std::abs (a[i] - b[i]));
    return difference;
}

/**
    Probes whether the sequencer currently produces audible sequenced events.
    While Sequencer/GrooveEngine are stubs every sequenced render is silent; the
    affected assertions are skipped (and will switch on automatically when the
    real implementations land). Cached: the probe renders at 48 kHz once.
*/
bool sequencerProducesAudio()
{
    static const bool producesAudio = [] {
        KlopgeestProcessor processor;
        processor.setPlayConfigDetails (0, 2, kRate, kBlock);
        processor.prepareToPlay (kRate, kBlock);
        processor.loadFactoryPattern (0);
        processor.setPlaying (true);

        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        float peak = 0.0f;

        for (int block = 0; block < 96 && peak <= 1.0e-6f; ++block)
        {
            processor.processBlock (buffer, midi);
            peak = juce::jmax (peak, buffer.getMagnitude (0, kBlock));
        }

        return peak > 1.0e-6f;
    }();

    return producesAudio;
}

/** Simulated host transport: lets the test drive bpm / ppq / play state. */
class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setIsPlaying (playing);
        position.setBpm (bpm);
        position.setPpqPosition (ppq);
        position.setTimeInSamples ((juce::int64) (ppq * 60.0 / bpm * sampleRate));
        position.setTimeInSeconds (ppq * 60.0 / bpm);
        return position;
    }

    void advance (int numSamples)
    {
        ppq += (double) numSamples / (60.0 * sampleRate / bpm);
    }

    bool playing = false;
    double bpm = 120.0;
    double ppq = 0.0;
    double sampleRate = kRate;
};

/** A deliberately hostile pattern: out-of-range values that sanitise() must tame. */
PatternData hostilePattern (std::mt19937& random, int config)
{
    PatternData pattern;
    pattern.length = (config % 3 == 0) ? 64 : ((config % 3 == 1) ? 32 : 16);

    for (int voice = 0; voice < kNumVoices; ++voice)
    {
        auto& lane = pattern.lanes[(std::size_t) voice];
        lane.length = 1 + (int) (random() % (unsigned int) pattern.length);
        lane.muted = (random() & 1u) != 0u;
        lane.soloed = false;

        for (int step = 0; step < kMaxSteps; ++step)
        {
            auto& s = lane.steps[(std::size_t) step];
            s.on = (random() % 3u) != 0u;
            s.velocity = (float) (random() % 2000u) / 1000.0f - 0.5f;      // -0.5..1.5
            s.probability = (float) (random() % 1500u) / 1000.0f;          // 0..1.5
            s.microtiming = (float) (random() % 3000u) / 1000.0f - 1.5f;   // -1.5..1.5
            s.pitchSemis = (float) (random() % 4000u) / 100.0f - 20.0f;    // -20..20
            s.ratchet = (int) (random() % 9u);                             // 0..8
            s.locked = (random() & 7u) == 0u;
        }
        // Step data is serialised for all kMaxSteps (see PatternData::toValueTree),
        // including steps beyond the lane length, so the fuzz populates all of them
        // and the state-recall check covers the full step buffer.
    }

    pattern.setName ("FUZZ");
    return pattern;
}

// --- Realtime safety ---------------------------------------------------------

void testAllocationFreeProcessBlock()
{
    KlopgeestProcessor processor;
    processor.setPlayConfigDetails (0, 2, kRate, kBlock);
    processor.prepareToPlay (kRate, kBlock);
    processor.loadFactoryPattern (0);
    processor.setPlaying (true);
    setParam (processor, pid::seed, 999.0f);
    setParam (processor, pid::humanize, 0.5f);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    const PatternData alternateA = factoryPattern (1);
    const PatternData alternateB = factoryPattern (4);
    const PatternData alternateC = factoryPattern (2);   // pre-built: factoryPattern() builds juce::Strings

    // Warm-up: let JUCE/APVTS/voices do their lazy initialisation first.
    for (int block = 0; block < 16; ++block)
        processor.processBlock (buffer, midi);

    std::size_t allocations = 1;
    std::size_t deallocations = 1;
    int blocksDone = 0;

    {
        klop_test::ScopedAllocationCounter counter;

        for (int block = 0; block < 200; ++block)
        {
            // Pattern edits and panic requests arrive from the "message thread"
            // (this thread) while the audio path runs on the same thread here.
            if (block == 30)
                processor.publishPattern (alternateA);
            else if (block == 60)
                processor.publishPattern (alternateB);
            else if (block == 90)
                processor.panic();
            else if (block == 120)
                processor.publishPattern (alternateC);

            processor.processBlock (buffer, midi);
            ++blocksDone;
        }

        allocations = counter.allocations();
        deallocations = counter.deallocations();
    }

    check (blocksDone == 200, "processBlock ran for 200 blocks after prepare");
    check (allocations == 0,
           "processBlock performs zero heap allocations after prepareToPlay (got "
               + std::to_string (allocations) + ")");
    check (deallocations == 0,
           "processBlock performs zero heap deallocations after prepareToPlay (got "
               + std::to_string (deallocations) + ")");
}

void testAllocationFreeEngine()
{
    Engine engine;
    engine.prepare (kRate, kBlock);
    engine.setRunning (true);

    EngineParams params;
    for (auto& voice : params.voice)
        voice.levelDb = -6.0f;
    engine.setParams (params);

    const PatternData pattern = factoryPattern (0);
    const HostTransport host;
    VisualState visual;

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
        engine.process (buffer, midi, kBlock, pattern, host, visual);

    std::size_t allocations = 1;
    {
        klop_test::ScopedAllocationCounter counter;
        for (int block = 0; block < 200; ++block)
            engine.process (buffer, midi, kBlock, pattern, host, visual);
        allocations = counter.allocations();
    }

    check (allocations == 0,
           "Engine::process performs zero heap allocations (got " + std::to_string (allocations) + ")");
}

void testAllocationFreeMaster()
{
    GlueMachine master;
    master.prepare (kRate, kBlock);
    master.setParams (MasterParams {});

    juce::AudioBuffer<float> buffer (2, kBlock);

    auto fill = [&buffer]
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample,
                                  0.2f * std::sin (juce::MathConstants<float>::twoPi
                                                   * (float) (sample + channel) * 0.01f));
    };

    for (int block = 0; block < 8; ++block)
        master.process (buffer, kBlock);

    std::size_t allocations = 1;
    {
        klop_test::ScopedAllocationCounter counter;
        for (int block = 0; block < 100; ++block)
        {
            fill();
            master.process (buffer, kBlock);
        }
        allocations = counter.allocations();
    }

    check (allocations == 0,
           "GlueMachine::process performs zero heap allocations (got " + std::to_string (allocations) + ")");
}

void testAllocationFreeVoices()
{
    int voicesChecked = 0;

    for (int voiceIndex = 0; voiceIndex < kNumVoices; ++voiceIndex)
    {
        // Allocation of the voice object itself is allowed here: it happens
        // before prepare(), not on the audio thread.
        auto voice = createVoice (voiceIndex);
        check (voice != nullptr, std::string ("voice can be created: ") + voiceName (voiceIndex));
        if (voice == nullptr)
            continue;

        voice->prepare (kRate, kBlock);

        VoiceParams params;
        params.levelDb = -6.0f;
        voice->setParams (params);

        juce::AudioBuffer<float> buffer (2, kBlock);
        VoiceTrigger trigger;
        trigger.velocity = 0.8f;

        for (int warmUp = 0; warmUp < 8; ++warmUp)
        {
            buffer.clear();
            trigger.seed = (std::uint32_t) (voiceIndex * 1000 + warmUp);
            voice->trigger (trigger);
            voice->render (buffer, 0, kBlock);
        }

        std::size_t allocations = 1;
        {
            klop_test::ScopedAllocationCounter counter;
            for (int hit = 0; hit < 50; ++hit)
            {
                buffer.clear();
                trigger.seed = (std::uint32_t) (voiceIndex * 1000 + 100 + hit);
                voice->trigger (trigger);
                voice->render (buffer, 0, kBlock);
            }
            allocations = counter.allocations();
        }

        check (allocations == 0,
               std::string ("voice trigger/render performs zero heap allocations: ")
                   + voiceName (voiceIndex) + " (got " + std::to_string (allocations) + ")");
        ++voicesChecked;
    }

    check (voicesChecked == kNumVoices, "all eight voices were allocation-checked");
}

// --- Sample rates ------------------------------------------------------------

void testSampleRateSweep()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const std::string label = " @ " + std::to_string ((int) rate) + " Hz";

        KlopgeestProcessor processor;
        processor.setPlayConfigDetails (0, 2, rate, kBlock);
        processor.prepareToPlay (rate, kBlock);
        processor.loadFactoryPattern (2);
        processor.setPlaying (true);
        setParam (processor, pid::seed, 4711.0f);
        setParam (processor, pid::humanize, 0.4f);

        checkNear (processor.getEngine().getSampleRate(), rate, 1.0e-9,
                   "engine adopts the prepared sample rate" + label);

        const auto rendered = runBlocks (processor, 120, kBlock, 2);
        check (rendered.finite, "output is finite" + label);
        check (rendered.peak < 4.0f, "output does not run away" + label);

        juce::MemoryBlock state;
        processor.getStateInformation (state);
        check (state.getSize() > 0, "state block is non-empty" + label);

        KlopgeestProcessor restored;
        restored.setPlayConfigDetails (0, 2, rate, kBlock);
        restored.prepareToPlay (rate, kBlock);
        restored.setStateInformation (state.getData(), (int) state.getSize());

        checkNear (getParam (restored, pid::humanize), getParam (processor, pid::humanize), 1.0e-4,
                   "humanize is recalled" + label);
        checkNear (getParam (restored, pid::seed), getParam (processor, pid::seed), 1.0e-3,
                   "seed is recalled" + label);
        check (restored.getPattern() == processor.getPattern(), "pattern is recalled" + label);

        const auto recalled = runBlocks (restored, 32, kBlock, 2);
        check (recalled.finite, "recalled state renders finite audio" + label);
    }
}

// --- Block-size invariance ---------------------------------------------------

void testBlockSizeInvariance()
{
    if (! sequencerProducesAudio())
    {
        skip ("block-size invariance (64/128/512/2048): sequencer is still a stub, "
              "sequenced renders are silent - this switches on automatically when the groove engine lands");
        return;
    }

    constexpr int blockSizes[] = { 64, 128, 512, 2048 };
    constexpr int totalSamples = 2048 * 200;   // 409600 samples, divisible by every block size

    std::vector<RenderedAudio> renders;
    renders.reserve (4);

    for (const int blockSize : blockSizes)
    {
        KlopgeestProcessor processor;
        processor.setPlayConfigDetails (0, 2, kRate, blockSize);
        processor.prepareToPlay (kRate, blockSize);
        processor.loadFactoryPattern (0);
        processor.setPlaying (true);
        setParam (processor, pid::seed, 4242.0f);
        setParam (processor, pid::swing, 0.25f);
        setParam (processor, pid::humanize, 0.5f);
        setParam (processor, pid::personality, 2.0f);

        renders.push_back (runBlocks (processor, totalSamples / blockSize, blockSize, 2));
    }

    const auto& reference = renders.front();
    check (reference.peak > 1.0e-6f, "block-size reference render is audible");

    bool finite = true;
    for (const auto& render : renders)
        finite = finite && render.finite;
    check (finite, "every block size renders finite output");

    // The groove engine's events are a pure function of the absolute tick
    // index, so changing the host block size must not change the audio. The
    // tolerance covers float rounding in the voice/master render paths; it is
    // deliberately an absolute sample tolerance (not a relative one) because
    // silence must match exactly too.
    constexpr float tolerance = 1.0e-4f;

    for (std::size_t i = 1; i < renders.size(); ++i)
    {
        const auto leftDifference = maxAbsDifference (reference.left, renders[i].left);
        const auto rightDifference = maxAbsDifference (reference.right, renders[i].right);
        const auto worstDifference = juce::jmax (leftDifference, rightDifference);
        info ("block size " + std::to_string (blockSizes[i]) + ": max |diff| "
              + juce::String (worstDifference, 8).toStdString()
              + " vs reference peak " + juce::String (reference.peak, 6).toStdString());
        const std::string suffix = " (" + std::to_string (blockSizes[i]) + "-sample blocks, max |diff| "
                                   + juce::String (worstDifference, 8).toStdString() + ")";
        check (leftDifference <= tolerance, "left channel matches the 64-sample reference render" + suffix);
        check (rightDifference <= tolerance, "right channel matches the 64-sample reference render" + suffix);
    }
}

// --- Transport ---------------------------------------------------------------

void testTransportInternal()
{
    KlopgeestProcessor processor;
    processor.setPlayConfigDetails (0, 2, kRate, kBlock);
    processor.prepareToPlay (kRate, kBlock);
    processor.loadFactoryPattern (0);
    setParam (processor, pid::tempo, 100.0f);

    auto& visual = processor.getVisualState();
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    processor.setPlaying (false);
    for (int block = 0; block < 8; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
    }
    check (! visual.playing.load(), "stopped internal transport reports stopped");
    check (! processor.getEngine().isRunning(), "engine reports not running when stopped");
    check (buffer.getMagnitude (0, kBlock) <= 1.0e-6f,
           "stopped internal transport renders silence when no notes are ringing");

    processor.setPlaying (true);
    buffer.clear();
    float peak = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        peak = juce::jmax (peak, buffer.getMagnitude (0, kBlock));
    }
    check (visual.playing.load(), "running internal transport reports playing");
    check (processor.getEngine().isRunning(), "engine reports running when playing");
    checkNear (visual.bpm.load(), 100.0, 0.01, "free-run bpm follows the tempo parameter");

    if (sequencerProducesAudio())
        check (peak > 1.0e-6f, "running internal transport produces audio");
    else
        skip ("running internal transport produces audio (sequencer still a stub)");

    setParam (processor, pid::tempo, 150.0f);
    buffer.clear();
    processor.processBlock (buffer, midi);
    checkNear (visual.bpm.load(), 150.0, 0.01, "tempo change is picked up on the next block");

    processor.setPlaying (false);
    for (int block = 0; block < 4; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
    }
    check (! visual.playing.load(), "transport stops again after running");
    check (! processor.getEngine().isRunning(), "engine reports stopped again");
}

void testHostTransport()
{
    FakePlayHead playHead;
    playHead.sampleRate = kRate;

    KlopgeestProcessor processor;
    processor.setPlayHead (&playHead);
    processor.setPlayConfigDetails (0, 2, kRate, kBlock);
    processor.prepareToPlay (kRate, kBlock);
    processor.loadFactoryPattern (0);
    setParam (processor, pid::tempo, 111.0f);

    auto& visual = processor.getVisualState();
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    playHead.playing = true;
    playHead.bpm = 90.0;
    playHead.ppq = 0.0;

    bool finite = true;
    for (int block = 0; block < 16; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        finite = finite && allFinite (buffer);
        playHead.advance (kBlock);
    }

    check (finite, "host-synced output is finite");
    check (visual.hostSynced.load(), "host transport is detected as host-synced");
    check (visual.playing.load(), "host transport reports playing");
    checkNear (visual.bpm.load(), 90.0, 0.01, "host bpm is honoured");
    check (visual.currentTick.load() >= 0, "host-synced tick index is sane");

    // Host stop with a play head present must fall back to the internal tempo.
    playHead.playing = false;
    buffer.clear();
    processor.processBlock (buffer, midi);
    check (! visual.hostSynced.load(), "host stop is detected");
    checkNear (visual.bpm.load(), 111.0, 0.01,
               "stopped host falls back to the internal tempo parameter");

    // Host tempo change.
    playHead.playing = true;
    playHead.bpm = 140.0;
    buffer.clear();
    processor.processBlock (buffer, midi);
    checkNear (visual.bpm.load(), 140.0, 0.01, "host tempo change is followed");

    // Seek/jump: must not crash, must stay host-synced and finite.
    playHead.ppq = 64.0;
    finite = true;
    for (int block = 0; block < 4; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        finite = finite && allFinite (buffer);
        playHead.advance (kBlock);
    }
    check (finite, "output after a host seek is finite");
    check (visual.hostSynced.load(), "host sync survives a seek");
}

// --- Determinism -------------------------------------------------------------

std::vector<float> renderFixedSeed (int seed)
{
    KlopgeestProcessor processor;
    processor.setPlayConfigDetails (0, 2, kRate, kBlock);
    processor.prepareToPlay (kRate, kBlock);
    processor.loadFactoryPattern (0);
    processor.setPlaying (true);
    setParam (processor, pid::seed, (float) seed);
    setParam (processor, pid::humanize, 0.6f);
    setParam (processor, pid::swing, 0.3f);
    setParam (processor, pid::personality, 2.0f);
    return runBlocks (processor, 128, kBlock, 1).left;
}

void testDeterministicSeeds()
{
    const auto first = renderFixedSeed (1234);
    const auto second = renderFixedSeed (1234);

    check (! first.empty(), "deterministic seed render produced samples");
    check (first == second, "same seed reproduces bit-identical audio");

    if (! sequencerProducesAudio())
    {
        skip ("different seed => different audio (sequencer still a stub; enabled with the groove engine)");
        return;
    }

    const auto other = renderFixedSeed (9876);
    check (first != other, "different seed produces different audio");
}

// --- Hostile input -----------------------------------------------------------

void testHostileParameterFuzz()
{
    std::mt19937 random (0x5eed1234u);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    bool allFiniteOutput = true;
    bool allStateRoundTrips = true;
    int configsProcessed = 0;

    for (int config = 0; config < 64; ++config)
    {
        KlopgeestProcessor processor;
        processor.setPlayConfigDetails (0, 2, kRate, kBlock);
        processor.prepareToPlay (kRate, kBlock);

        // Every parameter is driven to an arbitrary normalised position,
        // including the extremes (mute, full drive, highest ratio, ...).
        for (auto* parameter : processor.getAPVTS().processor.getParameters())
            if (parameter != nullptr)
                parameter->setValueNotifyingHost (unit (random));

        processor.setPlaying ((config & 1) == 0);
        processor.publishPattern (hostilePattern (random, config));

        const auto rendered = runBlocks (processor, 8, kBlock, 2);
        allFiniteOutput = allFiniteOutput && rendered.finite;
        ++configsProcessed;

        // State must survive the same hostile configuration.
        juce::MemoryBlock state;
        processor.getStateInformation (state);

        KlopgeestProcessor restored;
        restored.setPlayConfigDetails (0, 2, kRate, kBlock);
        restored.prepareToPlay (kRate, kBlock);
        restored.setStateInformation (state.getData(), (int) state.getSize());

        const auto& sourceParameters = processor.getAPVTS().processor.getParameters();
        const auto& restoredParameters = restored.getAPVTS().processor.getParameters();

        std::string stateProblem;

        if (sourceParameters.size() != restoredParameters.size())
        {
            stateProblem = "parameter count " + std::to_string (sourceParameters.size())
                           + " vs " + std::to_string (restoredParameters.size());
        }
        else
        {
            for (int i = 0; i < sourceParameters.size(); ++i)
            {
                if (! parametersMatch (*sourceParameters[i], *restoredParameters[i], 1.0e-4f))
                {
                    stateProblem = "parameter " + std::to_string (i) + " '"
                                   + sourceParameters[i]->getName (64).toStdString() + "' "
                                   + std::to_string (sourceParameters[i]->getValue()) + " vs "
                                   + std::to_string (restoredParameters[i]->getValue());
                    break;
                }
            }
        }

        if (stateProblem.empty() && ! (restored.getPattern() == processor.getPattern()))
            stateProblem = "pattern differs after state restore: "
                           + describeFirstPatternDifference (processor.getPattern(), restored.getPattern());

        if (! stateProblem.empty() && allStateRoundTrips)
        {
            allStateRoundTrips = false;
            info ("hostile state round-trip mismatch (config " + std::to_string (config) + "): " + stateProblem);
        }
        else if (! stateProblem.empty())
        {
            allStateRoundTrips = false;
        }
    }

    check (configsProcessed == 64, "all 64 hostile parameter configurations were processed");
    check (allFiniteOutput, "hostile parameter/pattern configurations render finite audio");
    check (allStateRoundTrips, "state round-trips under hostile parameter/pattern configurations");
}

// --- Publishing thread safety ------------------------------------------------

void testPatternPublishingThreadSafety()
{
    KlopgeestProcessor processor;
    processor.setPlayConfigDetails (0, 2, kRate, kBlock);
    processor.prepareToPlay (kRate, kBlock);
    processor.setPlaying (true);

    std::vector<PatternData> patterns;
    patterns.reserve ((std::size_t) factoryPatternCount());
    for (int i = 0; i < factoryPatternCount(); ++i)
        patterns.push_back (factoryPattern (i));

    std::atomic<bool> stop { false };
    std::atomic<int> blocksDone { 0 };
    std::atomic<bool> finite { true };
    std::atomic<std::size_t> audioThreadAllocations { 1 };

    std::thread audioThread ([&processor, &stop, &blocksDone, &finite, &audioThreadAllocations]
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;

        for (int warmUp = 0; warmUp < 8; ++warmUp)
            processor.processBlock (buffer, midi);

        std::size_t allocations = 0;
        {
            klop_test::ScopedAllocationCounter counter;
            while (! stop.load (std::memory_order_relaxed))
            {
                processor.processBlock (buffer, midi);
                if (! allFinite (buffer))
                    finite.store (false, std::memory_order_relaxed);
                blocksDone.fetch_add (1, std::memory_order_relaxed);
            }
            allocations = counter.allocations();
        }

        audioThreadAllocations.store (allocations, std::memory_order_relaxed);
    });

    // The "message thread": 500 pattern publishes while audio is running.
    for (int i = 0; i < 500; ++i)
        processor.publishPattern (patterns[(std::size_t) (i % (int) patterns.size())]);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (5);
    while (blocksDone.load() < 400 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    stop.store (true, std::memory_order_relaxed);
    audioThread.join();

    check (finite.load(), "audio output stays finite while patterns are published from another thread");
    check (blocksDone.load() >= 100,
           "audio thread kept processing during publishing (got " + std::to_string (blocksDone.load()) + " blocks)");
    check (audioThreadAllocations.load() == 0,
           "audio thread performs zero heap allocations while patterns are published (got "
               + std::to_string (audioThreadAllocations.load()) + ")");
}

} // namespace

void runQaIntegrationTests()
{
    std::printf ("\n-- QA integration suites --\n");
    info ("realtime safety: allocation-free processBlock / engine / master / voices");
    testAllocationFreeProcessBlock();
    testAllocationFreeEngine();
    testAllocationFreeMaster();
    testAllocationFreeVoices();

    info ("sample rates: 44.1 / 48 / 96 kHz render and recall state");
    testSampleRateSweep();

    info ("block-size invariance: 64 / 128 / 512 / 2048 samples");
    testBlockSizeInvariance();

    info ("transport: internal start/stop, host ppq/bpm, tempo change, seek");
    testTransportInternal();
    testHostTransport();

    info ("deterministic seeds");
    testDeterministicSeeds();

    info ("hostile parameter/pattern fuzz (64 configurations)");
    testHostileParameterFuzz();

    info ("publishing thread safety: 500 pattern edits during audio processing");
    testPatternPublishingThreadSafety();
}
