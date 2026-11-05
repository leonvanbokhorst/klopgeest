#include "tests/AllTests.h"
#include "tests/TestHarness.h"

// -----------------------------------------------------------------------------
// Workstream test suites. Each workstream owns its own VoiceTests.* /
// SequencerTests.* / MasterTests.* files; this runner links them in as soon as
// they exist. The __has_include guards keep the build green while the files are
// being written in parallel (the suites are then reported as missing, never
// silently passed).
// -----------------------------------------------------------------------------
#if __has_include("tests/QaIntegrationTests.h")
 #include "tests/QaIntegrationTests.h"
 #define KLOPGEEST_HAS_QA_TESTS 1
#endif

#if __has_include("tests/VoiceTests.h")
 #include "tests/VoiceTests.h"
 #define KLOPGEEST_HAS_VOICE_TESTS 1
#endif

#if __has_include("tests/SequencerTests.h")
 #include "tests/SequencerTests.h"
 #define KLOPGEEST_HAS_SEQUENCER_TESTS 1
#endif

#if __has_include("tests/MasterTests.h")
 #include "tests/MasterTests.h"
 #define KLOPGEEST_HAS_MASTER_TESTS 1
#endif

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/ParamIDs.h"
#include "core/Parameters.h"
#include "core/Pattern.h"
#include "core/Patterns.h"
#include "plugin/KlopgeestProcessor.h"

using namespace klopgeest;
using klop_test::check;
using klop_test::checkInRange;
using klop_test::checkNear;
using klop_test::info;

namespace
{

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

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (ch, i)))
                return false;
    return true;
}

void testParameters (KlopgeestProcessor& processor)
{
    const auto count = (int) processor.getAPVTS().processor.getParameters().size();
    check (count >= 140, "parameter layout has at least 140 parameters (got " + std::to_string (count) + ")");

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto id = pid::voiceId (v, pid::v::pitch);
        check (processor.getAPVTS().getParameter (id) != nullptr,
               "voice pitch parameter resolves: " + id.toStdString());
    }

    for (const char* id : { pid::swing, pid::humanize, pid::stability, pid::seed,
                            pid::threshold, pid::glue, pid::heat, pid::ceiling })
        check (processor.getAPVTS().getParameter (id) != nullptr,
               std::string ("parameter resolves: ") + id);

    check (processor.getAPVTS().getParameter (pid::personality) != nullptr, "personality parameter resolves");
}

void testPatterns()
{
    check (factoryPatternCount() == kNumFactoryPatterns, "six factory patterns exist");
    check (factoryPatternNames().size() == kNumFactoryPatterns, "factory pattern names match count");

    for (int i = 0; i < factoryPatternCount(); ++i)
    {
        const auto pattern = factoryPattern (i);
        checkInRange (pattern.length, kMinSteps, kMaxSteps, "factory pattern has a valid length");
        check (! pattern.getName().isEmpty(), "factory pattern has a name");

        bool hasHit = false;
        for (int v = 0; v < kNumVoices; ++v)
            for (int s = 0; s < pattern.length; ++s)
                if (pattern.step (v, s).on)
                    hasHit = true;
        check (hasHit, "factory pattern is not empty");
    }

    const auto init = makeInitPattern();
    check (init.step (VoiceKick, 0).on, "init pattern has a kick on the downbeat");
    check (init.step (VoiceSnare, 4).on, "init pattern has a snare on beat 2");
    check (init.step (VoiceClosedHat, 0).on, "init pattern has closed hats");
}

void testPatternSerialisation()
{
    const auto original = factoryPattern (2);
    const auto tree = original.toValueTree();
    const auto restored = PatternData::fromValueTree (tree);
    check (original == restored, "pattern survives ValueTree round-trip");

    PatternData invalid;
    invalid.length = 500;
    invalid.lanes[0].length = 999;
    invalid.step (0, 0).velocity = 10.0f;
    invalid.step (0, 0).ratchet = 42;
    invalid.sanitise();
    checkInRange (invalid.length, kMinSteps, kMaxSteps, "pattern length is clamped on sanitise");
    checkInRange (invalid.lanes[0].length, 1, invalid.length, "lane length is clamped on sanitise");
    checkInRange (invalid.step (0, 0).velocity, 0.0f, 1.0f, "velocity is clamped on sanitise");
    checkInRange (invalid.step (0, 0).ratchet, 1, 4, "ratchet is clamped on sanitise");
}

void testStateRoundTrip()
{
    KlopgeestProcessor source;
    source.prepareToPlay (48000.0, 512);

    setParam (source, pid::swing, 0.33f);
    setParam (source, pid::humanize, 0.61f);
    setParam (source, pid::personality, 2.0f);
    setParam (source, pid::glue, 0.72f);
    setParam (source, pid::threshold, -23.5f);

    auto pattern = factoryPattern (3);
    pattern.setName ("STATE TEST");
    source.publishPattern (pattern);

    juce::MemoryBlock state;
    source.getStateInformation (state);
    check (state.getSize() > 0, "state block is not empty");

    KlopgeestProcessor destination;
    destination.prepareToPlay (48000.0, 512);
    destination.setStateInformation (state.getData(), (int) state.getSize());

    checkNear (getParam (destination, pid::swing), getParam (source, pid::swing), 1.0e-4,
               "swing is restored");
    checkNear (getParam (destination, pid::humanize), getParam (source, pid::humanize), 1.0e-4,
               "humanize is restored");
    checkNear (getParam (destination, pid::glue), getParam (source, pid::glue), 1.0e-4,
               "glue is restored");
    check (std::abs (getParam (destination, pid::personality) - 2.0f) < 1.0e-3,
           "personality is restored");
    if (! (destination.getPattern() == source.getPattern()))
    {
        const auto& a = source.getPattern();
        const auto& b = destination.getPattern();
        std::printf ("  DIAG state pattern length %d/%d name '%s'/'%s'\n", a.length, b.length,
                     a.getName().toRawUTF8(), b.getName().toRawUTF8());
        for (int v = 0; v < kNumVoices; ++v)
        {
            const auto& la = a.lanes[(size_t) v];
            const auto& lb = b.lanes[(size_t) v];
            if (la.length != lb.length || la.muted != lb.muted || la.soloed != lb.soloed || la.locked != lb.locked)
                std::printf ("  DIAG state lane %d len %d/%d mute %d/%d solo %d/%d lock %d/%d\n", v,
                             la.length, lb.length, (int) la.muted, (int) lb.muted,
                             (int) la.soloed, (int) lb.soloed, (int) la.locked, (int) lb.locked);
            for (int st = 0; st < kMaxSteps; ++st)
            {
                const auto& x = la.steps[(size_t) st];
                const auto& y = lb.steps[(size_t) st];
                if (x.on != y.on || x.locked != y.locked || x.ratchet != y.ratchet
                    || x.velocity != y.velocity || x.probability != y.probability
                    || x.microtiming != y.microtiming || x.pitchSemis != y.pitchSemis)
                    std::printf ("  DIAG state v%d s%d on %d/%d lock %d/%d rat %d/%d vel %.3f/%.3f prob %.3f/%.3f micro %.4f/%.4f pitch %.2f/%.2f\n",
                                 v, st, (int) x.on, (int) y.on, (int) x.locked, (int) y.locked,
                                 x.ratchet, y.ratchet, x.velocity, y.velocity, x.probability, y.probability,
                                 x.microtiming, y.microtiming, x.pitchSemis, y.pitchSemis);
            }
        }
    }
    check (destination.getPattern() == source.getPattern(), "pattern is restored with the state");
}

void testDeterministicRender()
{
    auto render = [] (int seed)
    {
        KlopgeestProcessor processor;
        processor.setPlayConfigDetails (0, 2, 48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        processor.loadFactoryPattern (0);
        processor.setPlaying (true);
        setParam (processor, pid::seed, (float) seed);
        setParam (processor, pid::humanize, 0.5f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        std::vector<float> samples;
        samples.reserve (180 * 512);
        for (int block = 0; block < 180; ++block)
        {
            processor.processBlock (buffer, midi);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                samples.push_back (buffer.getSample (0, i));
        }
        return samples;
    };

    const auto a = render (1234);
    const auto b = render (1234);
    // NOTE: the different-seed assertion is enabled once the groove engine lands;
    // until then the sequencer stub is silent, so only determinism can be checked.
    check (a == b, "same seed reproduces the same audio");
}

void testAudioThreadSafety()
{
    KlopgeestProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    processor.loadFactoryPattern (4);
    processor.setPlaying (true);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    bool finite = true;
    float peak = 0.0f;

    for (int block = 0; block < 64; ++block)
    {
        if (block == 20)
            processor.publishPattern (factoryPattern (5));
        if (block == 40)
            processor.panic();

        processor.processBlock (buffer, midi);
        finite = finite && allFinite (buffer);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    }

    check (finite, "audio output is finite under pattern changes and panic");
    info ("peak of deterministic smoke render: " + juce::String (peak, 4).toStdString());
}

void testMutation()
{
    KlopgeestProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    processor.loadFactoryPattern (0);

    const auto before = processor.getPattern();
    processor.nudge();
    check (processor.getPattern() != before, "nudge changes the pattern");
    check (processor.canRecall(), "nudge is recallable");

    const auto nudged = processor.getPattern();
    processor.recall();
    check (processor.getPattern() == before, "recall restores the state before the nudge");
    juce::ignoreUnused (nudged);

    processor.mutate();
    check (processor.getPattern() != before, "mutate changes the pattern");
}

} // namespace

int runAllTests()
{
    std::printf ("KLOPGEEST tests\n");

    KlopgeestProcessor processor;
    testParameters (processor);
    testPatterns();
    testPatternSerialisation();
    testStateRoundTrip();
    testDeterministicRender();
    testAudioThreadSafety();
    testMutation();

    // --- QA cross-cutting suites -------------------------------------------
#if defined(KLOPGEEST_HAS_QA_TESTS)
    runQaIntegrationTests();
#else
    klop_test::skip ("QA integration suites (tests/QaIntegrationTests.* missing)");
#endif

    // --- Workstream suites --------------------------------------------------
#if defined(KLOPGEEST_HAS_SEQUENCER_TESTS)
    runSequencerTests();
#else
    klop_test::skip ("sequencer/groove test suite (tests/SequencerTests.* missing)");
#endif

#if defined(KLOPGEEST_HAS_VOICE_TESTS)
    runVoiceTests();
#else
    klop_test::skip ("voice test suite (tests/VoiceTests.* missing)");
#endif

#if defined(KLOPGEEST_HAS_MASTER_TESTS)
    runMasterTests();
#else
    klop_test::skip ("master/glue test suite (tests/MasterTests.* missing)");
#endif

    std::printf ("\n%d checks, %d failures, %d skipped\n",
                 klop_test::gChecks, klop_test::gFailures, klop_test::gSkips);
    return klop_test::gFailures == 0 ? 0 : 1;
}
