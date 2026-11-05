// -----------------------------------------------------------------------------
// Sequencer + groove-engine tests.
//
// The harness drives Sequencer::process directly with an explicit host clock
// (ppq is derived from an absolute sample position), so event timing is
// deterministic and independent of any host. A second harness drives the free-
// running internal clock.
// -----------------------------------------------------------------------------
#include "tests/SequencerTests.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/Constants.h"
#include "core/EngineParams.h"
#include "core/Pattern.h"
#include "core/Patterns.h"
#include "core/VisualState.h"
#include "sequencer/GrooveEngine.h"
#include "sequencer/Sequencer.h"
#include "sequencer/SequencerTypes.h"
#include "tests/TestHarness.h"

namespace klopgeest
{
namespace
{

using klop_test::check;
using klop_test::checkInRange;
using klop_test::checkNear;
using klop_test::info;

constexpr double kSampleRate = 48000.0;
constexpr int kMaxTestEvents = 512;

// One recorded event with its absolute sample position in the render.
struct RecordedEvent
{
    int voice = 0;
    long long sample = 0;      // absolute sample (block start + offset)
    long long tick = 0;
    int step = 0;
    float velocity = 0.0f;
    float pitchSemis = 0.0f;
    float decayScale = 1.0f;
    float transientScale = 1.0f;
    float toneOffset = 0.0f;
    float driveScale = 1.0f;
    float timingDevMs = 0.0f;
    float sampleOffset = 0.0f;
    std::uint32_t seed = 0;
    unsigned int flags = 0;
};

struct TransportFixture
{
    Sequencer sequencer;
    PatternData pattern;
    EngineParams params;

    TransportFixture()
    {
        sequencer.prepare (kSampleRate);
        params.global.humanize = 0.0f;      // mechanical unless a test says otherwise
        params.global.stability = 1.0f;
        params.global.swing = 0.0f;
        params.global.ghost = 0.0f;
        params.global.accent = 0.0f;
        params.global.personality = (int) Personality::loose;
        params.global.tempo = 120.0f;
        params.global.seed = 4242;
        pattern.length = kDefaultSteps;
        for (auto& lane : pattern.lanes)
            lane.length = kDefaultSteps;
        for (int v = 0; v < kNumVoices; ++v)
            for (int s = 0; s < kMaxSteps; ++s)
                pattern.step (v, s).on = false;
    }

    // Sequencer holds an atomic; fixtures are never copied, only reused in place.
    TransportFixture (const TransportFixture&) = delete;
    TransportFixture& operator= (const TransportFixture&) = delete;

    void muteAllExcept (int voice)
    {
        for (int v = 0; v < kNumVoices; ++v)
            pattern.lanes[(size_t) v].muted = (v != voice);
    }

    void unmuteAll()
    {
        for (auto& lane : pattern.lanes)
            lane.muted = false;
    }

    void hit (int voice, int step, float velocity = 0.8f, float probability = 1.0f,
              float micro = 0.0f, float pitch = 0.0f, int ratchet = 1, bool locked = false)
    {
        auto& s = pattern.step (voice, step);
        s.on = true;
        s.velocity = velocity;
        s.probability = probability;
        s.microtiming = micro;
        s.pitchSemis = pitch;
        s.ratchet = ratchet;
        s.locked = locked;
    }

    std::vector<RecordedEvent> renderHost (int totalSamples, int blockSize,
                                           double bpm = 120.0,
                                           bool bpmChanges = false)
    {
        std::vector<RecordedEvent> recorded;
        SequencedEvent events[kMaxTestEvents];
        double currentBpm = bpm;
        int position = 0;

        while (position < totalSamples)
        {
            const int n = std::min (blockSize, totalSamples - position);
            const double samplesPerPpq = 60.0 * kSampleRate / currentBpm;

            HostTransport host;
            host.available = true;
            host.playing = true;
            host.bpm = currentBpm;
            host.ppqPosition = (double) position / samplesPerPpq;

            SequencerFrame frame;
            int numEvents = 0;
            sequencer.process (host, pattern, params, n, events, kMaxTestEvents, numEvents, frame);

            for (int i = 0; i < numEvents; ++i)
            {
                const auto& e = events[i];
                RecordedEvent r;
                r.voice = e.voice;
                r.sample = (long long) position + e.sampleOffset;
                r.tick = e.tickIndex;
                r.step = e.stepIndex;
                r.velocity = e.trigger.velocity;
                r.pitchSemis = e.trigger.pitchSemis;
                r.decayScale = e.trigger.decayScale;
                r.transientScale = e.trigger.transientScale;
                r.toneOffset = e.trigger.toneOffset;
                r.driveScale = e.trigger.driveScale;
                r.timingDevMs = e.timingDevMs;
                r.sampleOffset = (float) e.sampleOffset;
                r.seed = e.trigger.seed;
                r.flags = e.trigger.flags;
                recorded.push_back (r);
            }

            position += n;

            if (bpmChanges && position >= totalSamples / 2)
                currentBpm = 140.0;
        }

        return recorded;
    }

    std::vector<RecordedEvent> renderFree (int totalSamples, int blockSize)
    {
        std::vector<RecordedEvent> recorded;
        SequencedEvent events[kMaxTestEvents];
        sequencer.reset();
        sequencer.setInternalRunning (true);

        int position = 0;
        while (position < totalSamples)
        {
            const int n = std::min (blockSize, totalSamples - position);
            HostTransport host; // no host clock
            SequencerFrame frame;
            int numEvents = 0;
            sequencer.process (host, pattern, params, n, events, kMaxTestEvents, numEvents, frame);

            for (int i = 0; i < numEvents; ++i)
            {
                const auto& e = events[i];
                RecordedEvent r;
                r.voice = e.voice;
                r.sample = (long long) position + e.sampleOffset;
                r.tick = e.tickIndex;
                r.step = e.stepIndex;
                r.velocity = e.trigger.velocity;
                r.timingDevMs = e.timingDevMs;
                r.flags = e.trigger.flags;
                recorded.push_back (r);
            }

            position += n;
        }

        return recorded;
    }
};

bool sameEventList (const std::vector<RecordedEvent>& a, const std::vector<RecordedEvent>& b,
                    bool compareAbsoluteTimes = true, bool compareSeeds = true)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].voice != b[i].voice || a[i].tick != b[i].tick || a[i].step != b[i].step
            || a[i].velocity != b[i].velocity || a[i].pitchSemis != b[i].pitchSemis
            || a[i].decayScale != b[i].decayScale || a[i].flags != b[i].flags)
            return false;
        if (compareSeeds && a[i].seed != b[i].seed)
            return false;
        if (compareAbsoluteTimes && a[i].sample != b[i].sample)
            return false;
    }
    return true;
}

bool sameEventListWithin (const std::vector<RecordedEvent>& a, const std::vector<RecordedEvent>& b,
                          long long toleranceSamples)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].voice != b[i].voice || a[i].tick != b[i].tick || a[i].step != b[i].step
            || a[i].velocity != b[i].velocity || a[i].pitchSemis != b[i].pitchSemis
            || a[i].decayScale != b[i].decayScale || a[i].seed != b[i].seed
            || a[i].flags != b[i].flags)
            return false;
        if (std::llabs (a[i].sample - b[i].sample) > toleranceSamples)
            return false;
    }
    return true;
}

std::vector<RecordedEvent> filterVoice (const std::vector<RecordedEvent>& events, int voice)
{
    std::vector<RecordedEvent> out;
    for (const auto& e : events)
        if (e.voice == voice)
            out.push_back (e);
    return out;
}

bool finite (const RecordedEvent& e)
{
    return std::isfinite (e.velocity) && std::isfinite (e.pitchSemis)
           && std::isfinite (e.decayScale) && std::isfinite (e.transientScale)
           && std::isfinite (e.toneOffset) && std::isfinite (e.driveScale)
           && std::isfinite (e.timingDevMs);
}

// --- 1. Determinism -----------------------------------------------------------
void testDeterminism()
{
    TransportFixture a;
    a.hit (VoiceKick, 0, 0.9f);
    a.hit (VoiceKick, 8, 0.7f);
    a.hit (VoiceClosedHat, 2, 0.4f);
    a.params.global.humanize = 0.6f;
    a.params.global.stability = 0.2f;
    a.params.global.swing = 0.2f;
    a.params.global.ghost = 0.3f;
    a.params.global.accent = 0.5f;

    const auto first = a.renderHost (48000, 128);
    const auto second = a.renderHost (48000, 128);
    check (! first.empty(), "determinism: render produced events");
    check (sameEventList (first, second), "same seed -> identical event list");

    TransportFixture b;
    b.pattern = a.pattern;
    b.params = a.params;
    b.params.global.seed = 9999;
    const auto other = b.renderHost (48000, 128);
    check (! other.empty(), "different seed: render produced events");
    check (! sameEventList (first, other), "different seed -> different event list");

    bool allFinite = true;
    for (const auto& e : first)
        allFinite = allFinite && finite (e);
    check (allFinite, "all generated event fields are finite");
}

// --- 2. Block-size invariance -------------------------------------------------
void testBlockSizeInvariance()
{
    TransportFixture fixture;
    fixture.hit (VoiceKick, 0, 0.9f);
    fixture.hit (VoiceKick, 4, 0.8f);
    fixture.hit (VoiceSnare, 4, 0.9f);
    fixture.hit (VoiceClosedHat, 1, 0.3f);
    fixture.hit (VoiceClosedHat, 3, 0.3f);
    fixture.hit (VoicePerc, 2, 0.5f, 0.7f, 0.0f, 0.0f, 2);
    fixture.params.global.humanize = 0.7f;
    fixture.params.global.stability = 0.3f;
    fixture.params.global.swing = 0.25f;
    fixture.params.global.ghost = 0.4f;
    fixture.params.global.accent = 0.4f;
    fixture.params.global.personality = (int) Personality::feral;

    const auto reference = fixture.renderHost (48000, 64);
    check (! reference.empty(), "block-size invariance: reference render has events");

    bool allMatch = true;
    for (int blockSize : { 128, 512, 1024 })
    {
        const auto rendered = fixture.renderHost (48000, blockSize);
        if (! sameEventList (reference, rendered))
        {
            allMatch = false;
            info ("block-size mismatch at blockSize=" + std::to_string (blockSize)
                  + " events " + std::to_string (reference.size()) + "/"
                  + std::to_string (rendered.size()));
        }
    }
    check (allMatch, "absolute event times identical for 64/128/512/1024-sample blocks");
}

// --- 3. Probability -----------------------------------------------------------
void testProbability()
{
    TransportFixture fixture;
    fixture.muteAllExcept (VoiceKick);
    fixture.hit (VoiceKick, 0, 0.8f, 0.0f);
    fixture.params.global.humanize = 0.5f;

    const auto never = fixture.renderHost (48000 * 2, 256);
    check (never.empty(), "probability 0: other voices still silent (muted) -> empty list");
    check (filterVoice (never, VoiceKick).empty(), "probability 0 -> step never fires");

    fixture.hit (VoiceKick, 0, 0.8f, 1.0f);
    const auto always = fixture.renderHost (48000 * 2, 256);
    // 2 seconds at 120 bpm = 4 beats = 16 sixteenths = exactly one pattern pass.
    check (filterVoice (always, VoiceKick).size() == 1, "probability 1 -> step fires every pass");
}

// --- 4. Locked steps ----------------------------------------------------------
void testLockedSteps()
{
    TransportFixture fixture;
    fixture.muteAllExcept (VoiceKick);
    fixture.hit (VoiceKick, 1, 0.64f, 0.25f, 0.25f, 3.0f, 1, true);
    fixture.params.global.humanize = 1.0f;
    fixture.params.global.stability = 0.0f;
    fixture.params.global.swing = 0.3f;
    fixture.params.global.personality = (int) Personality::feral;
    fixture.params.global.seed = 100;

    const auto a = fixture.renderHost (24000, 128);
    fixture.params.global.seed = 777;
    const auto b = fixture.renderHost (24000, 128);

    const auto ka = filterVoice (a, VoiceKick);
    const auto kb = filterVoice (b, VoiceKick);
    check (ka.size() == kb.size(), "locked: same event count with different seeds");
    check (sameEventList (ka, kb, true, false),
           "locked step: identical offsets/velocities across seeds");
    check (! ka.empty(), "locked step fired");

    if (! ka.empty())
    {
        const double stepMs = (60.0 * kSampleRate / 120.0) * 0.25 * 1000.0 / kSampleRate;
        const double expectedDevMs = 0.3 * stepMs + 0.25 * stepMs; // swing + programmed micro
        checkNear (ka[0].velocity, 0.64, 1.0e-6, "locked step velocity is exactly programmed");
        checkNear (ka[0].timingDevMs, expectedDevMs, 0.5,
                   "locked step: timing dev = swing + programmed micro, no stochastic part");
        checkNear (ka[0].pitchSemis, 3.0, 1.0e-6, "locked step pitch exactly programmed");
        checkNear (ka[0].decayScale, 1.0, 1.0e-6, "locked step decay scale is nominal");
        checkNear (ka[0].toneOffset, 0.0, 1.0e-6, "locked step tone offset is nominal");
        // swing grid feel still applies to locked material: tick 1 is an odd 16th.
        check (ka[0].sample > 6000, "locked step on an odd tick still swings late");
    }

    // A locked lane also suppresses ghosts (stochastic by nature).
    TransportFixture ghosts;
    ghosts.muteAllExcept (VoiceClosedHat);
    ghosts.params.global.ghost = 1.0f;
    ghosts.pattern.lanes[(size_t) VoiceClosedHat].locked = true;
    const auto lockedGhosts = ghosts.renderHost (48000, 256);
    check (lockedGhosts.empty(), "locked lane does not generate ghost notes");
}

// --- 5. Swing -----------------------------------------------------------------
void testSwing()
{
    TransportFixture straight;
    straight.muteAllExcept (VoiceKick);
    straight.hit (VoiceKick, 0, 0.8f);
    straight.hit (VoiceKick, 1, 0.8f);
    straight.params.global.swing = 0.0f;

    const auto noSwing = filterVoice (straight.renderHost (24000, 256), VoiceKick);
    check (noSwing.size() == 2, "swing: two kick events on ticks 0 and 1");
    if (noSwing.size() == 2)
    {
        checkNear (noSwing[0].sample, 0.0, 1.0, "straight: tick 0 on the grid");
        checkNear (noSwing[1].sample, 6000.0, 1.0, "straight: tick 1 on the grid");
    }

    TransportFixture swung;
    swung.muteAllExcept (VoiceKick);
    swung.hit (VoiceKick, 0, 0.8f);
    swung.hit (VoiceKick, 1, 0.8f);
    swung.params.global.swing = 0.3f;

    const auto swungEvents = filterVoice (swung.renderHost (24000, 256), VoiceKick);
    check (swungEvents.size() == 2, "swing: two kick events");
    if (swungEvents.size() == 2)
    {
        const double stepSamples = 6000.0 * 0.3;
        checkNear (swungEvents[0].sample, 0.0, 1.0, "swing: even tick is not delayed");
        checkNear (swungEvents[1].sample, 6000.0 + stepSamples, 2.0,
                   "swing: odd tick delayed by swing * step");
        checkNear (swungEvents[1].timingDevMs, 0.3 * 125.0, 0.5,
                   "swing: telemetry reports the grid delay");
    }
}

// --- 6. Ratchets --------------------------------------------------------------
void testRatchets()
{
    TransportFixture fixture;
    fixture.muteAllExcept (VoiceSnare);
    fixture.hit (VoiceSnare, 0, 0.9f, 1.0f, 0.0f, 0.0f, 4);
    const auto events = filterVoice (fixture.renderHost (24000, 256), VoiceSnare);

    check (events.size() == 4, "ratchet 4 -> four sub-hits");
    if (events.size() == 4)
    {
        const long long base = events[0].sample;
        bool evenlySpaced = true;
        for (int k = 0; k < 4; ++k)
            evenlySpaced = evenlySpaced
                           && std::abs ((double) (events[(size_t) k].sample - base) - 1500.0 * k) <= 1.0;
        check (evenlySpaced, "ratchet sub-hits evenly spaced across the step");
        bool dropping = true;
        for (int k = 1; k < 4; ++k)
            dropping = dropping && events[(size_t) k].velocity < events[(size_t) k - 1].velocity;
        check (dropping, "later ratchet hits are quieter");
        check ((events[0].flags & (unsigned int) HitFlag::ratchet) == 0u,
               "first ratchet hit is not flagged as a ratchet");
        check ((events[1].flags & (unsigned int) HitFlag::ratchet) != 0u,
               "second+ ratchet hits carry the ratchet flag");
    }
}

// --- 7. Accent / velocity shaping --------------------------------------------
void testAccent()
{
    TransportFixture fixture;
    fixture.muteAllExcept (VoiceKick);
    fixture.hit (VoiceKick, 0, 0.80f);   // strong downbeat
    fixture.hit (VoiceKick, 2, 0.80f);   // even but off-beat within the beat
    fixture.params.global.accent = 0.8f;

    const auto events = filterVoice (fixture.renderHost (24000, 256), VoiceKick);
    check (events.size() == 2, "accent: two kick events");
    if (events.size() == 2)
    {
        check (events[0].velocity > 0.75f, "accent: strong step reaches accent velocity");
        check ((events[0].flags & (unsigned int) HitFlag::accent) != 0u,
               "accent: strong step flagged");
    }

    TransportFixture quiet;
    quiet.muteAllExcept (VoiceKick);
    quiet.hit (VoiceKick, 0, 0.80f);
    const auto noAccent = filterVoice (quiet.renderHost (24000, 256), VoiceKick);
    check (! noAccent.empty() && ! events.empty() && events[0].velocity > noAccent[0].velocity,
           "accent parameter raises the velocity of a strong step");
}

// --- 8. Host ppq sync ---------------------------------------------------------
void testHostSync()
{
    TransportFixture fixture;
    fixture.muteAllExcept (VoiceKick);
    fixture.hit (VoiceKick, 0, 1.0f);
    fixture.hit (VoiceKick, 4, 1.0f);
    fixture.hit (VoiceKick, 8, 1.0f);

    const auto events = filterVoice (fixture.renderHost (72000, 400), VoiceKick);
    check (events.size() == 3, "host sync: three kicks inside three beats");
    if (events.size() == 3)
    {
        checkNear (events[0].sample, 0.0, 1.0, "host ppq: tick 0 at sample 0");
        checkNear (events[1].sample, 24000.0, 1.0, "host ppq: tick 4 at sample 24000");
        checkNear (events[2].sample, 48000.0, 1.0, "host ppq: tick 8 at sample 48000");
        check (events[0].tick == 0 && events[1].tick == 4 && events[2].tick == 8,
               "host ppq: tick indices match the ppq grid");
    }

    // Frame telemetry: playing, host-synced and a sane lane step.
    SequencerFrame frame;
    SequencedEvent out[kMaxTestEvents];
    int numOut = 0;
    HostTransport host;
    host.available = true;
    host.playing = true;
    host.bpm = 120.0;
    host.ppqPosition = 0.26;                 // just after tick 1
    fixture.sequencer.process (host, fixture.pattern, fixture.params, 256, out, kMaxTestEvents, numOut, frame);
    check (frame.playing, "host sync: frame.playing");
    check (frame.hostSynced, "host sync: frame.hostSynced");
    checkNear (frame.bpm, 120.0, 0.01, "host sync: frame reports host bpm");
    checkInRange (frame.laneStep[VoiceKick], 0, 15, "host sync: lane step within lane length");
}

// --- 9. Transport jumps / tempo changes --------------------------------------
void testTransportJumpsAndTempo()
{
    TransportFixture fixture;
    fixture.hit (VoiceKick, 0, 0.9f);
    fixture.muteAllExcept (VoiceKick);
    fixture.params.global.swing = 0.2f;
    fixture.params.global.humanize = 0.6f;
    fixture.params.global.stability = 0.4f;

    // Tempo change mid-render must not crash or duplicate a tick.
    const auto tempoEvents = filterVoice (fixture.renderHost (72000, 256, 120.0, true), VoiceKick);
    check (! tempoEvents.empty(), "tempo change: events still produced");
    bool monotonic = true;
    for (size_t i = 1; i < tempoEvents.size(); ++i)
        monotonic = monotonic && tempoEvents[i].sample > tempoEvents[i - 1].sample
                    && tempoEvents[i].tick > tempoEvents[i - 1].tick;
    check (monotonic, "tempo change: events strictly ascending, no double triggers");

    // A forward seek (host loop/jump) must resume on the new grid position.
    const double samplesPerPpq = 60.0 * kSampleRate / 120.0;
    SequencedEvent out[kMaxTestEvents];
    SequencerFrame frame;
    int numOut = 0;
    HostTransport host;
    host.available = true;
    host.playing = true;
    host.bpm = 120.0;
    host.jumped = true;
    host.ppqPosition = 4.0;                  // seek to bar 2 downbeat
    fixture.sequencer.process (host, fixture.pattern, fixture.params, 512, out, kMaxTestEvents, numOut, frame);
    bool sawJumpTick = false;
    for (int i = 0; i < numOut; ++i)
        if (out[i].voice == VoiceKick && out[i].tickIndex == 16)
            sawJumpTick = true;
    check (sawJumpTick, "forward seek: kick on the new bar downbeat (tick 16)");
    juce::ignoreUnused (samplesPerPpq);
}

// --- 10. Free-run -------------------------------------------------------------
void testFreeRun()
{
    TransportFixture fixture;
    fixture.hit (VoiceKick, 0, 0.9f);
    fixture.hit (VoiceKick, 4, 0.9f);
    fixture.muteAllExcept (VoiceKick);
    fixture.params.global.tempo = 120.0f;

    const auto events = filterVoice (fixture.renderFree (48000, 128), VoiceKick);
    check (events.size() == 2, "free-run: two kicks in one second at 120 bpm");
    if (events.size() == 2)
    {
        checkNear (events[0].sample, 0.0, 1.0, "free-run: first tick at the start");
        checkNear (events[1].sample, 24000.0, 2.0, "free-run: beat 2 one half second later");
    }
    checkNear (fixture.sequencer.getInternalPpq(), 2.0, 0.01, "free-run: internal ppq advances 2 beats");

    fixture.sequencer.setInternalRunning (false);
    SequencedEvent out[kMaxTestEvents];
    SequencerFrame frame;
    int numOut = 0;
    HostTransport stopped;
    stopped.available = true;
    stopped.playing = false;
    fixture.sequencer.process (stopped, fixture.pattern, fixture.params, 256, out, kMaxTestEvents, numOut, frame);
    check (numOut == 0, "stopped transport: no events");
    check (! frame.playing, "stopped transport: frame.playing is false");
}

// --- 11. Mute / solo ----------------------------------------------------------
void testMuteSolo()
{
    TransportFixture fixture;
    fixture.hit (VoiceKick, 0, 0.9f);
    fixture.hit (VoiceSnare, 4, 0.9f);
    fixture.params.global.humanize = 0.5f;

    fixture.pattern.lanes[(size_t) VoiceKick].muted = true;
    const auto muted = fixture.renderHost (36000, 256);
    check (filterVoice (muted, VoiceKick).empty(), "mute: muted lane produces no events");
    check (! filterVoice (muted, VoiceSnare).empty(), "mute: other lanes unaffected");

    fixture.pattern.lanes[(size_t) VoiceKick].muted = false;
    fixture.pattern.lanes[(size_t) VoiceSnare].soloed = true;
    const auto soloed = fixture.renderHost (36000, 256);
    check (filterVoice (soloed, VoiceKick).empty(), "solo: un-soloed lane is silent");
    check (! filterVoice (soloed, VoiceSnare).empty(), "solo: soloed lane still fires");

    fixture.pattern.lanes[(size_t) VoiceSnare].soloed = false;
}

// --- 12. Lane lengths ---------------------------------------------------------
void testLaneLengths()
{
    TransportFixture fixture;
    fixture.pattern.length = 64;
    for (auto& lane : fixture.pattern.lanes)
        lane.length = 64;
    fixture.pattern.lanes[(size_t) VoiceKick].length = 32;
    fixture.pattern.lanes[(size_t) VoiceClosedHat].length = 12;
    fixture.pattern.lanes[(size_t) VoicePerc].length = 8;
    fixture.pattern.lanes[(size_t) VoiceOdd].length = 5;
    fixture.pattern.lanes[(size_t) VoiceKick].locked = true;

    fixture.hit (VoiceKick, 0, 0.9f);
    fixture.hit (VoiceClosedHat, 0, 0.3f);
    fixture.hit (VoiceClosedHat, 7, 0.3f);
    fixture.hit (VoicePerc, 3, 0.5f);
    fixture.hit (VoiceOdd, 4, 0.3f);

    // Four bars = 64 sixteenths. Every lane loops on its own length.
    const auto events = fixture.renderHost (48000 * 8, 512);
    bool stepsInRange = true;
    bool foundEveryVoice = true;
    bool countsMatch = true;

    for (int voice : { VoiceKick, VoiceClosedHat, VoicePerc, VoiceOdd })
    {
        const int laneLength = fixture.pattern.laneLength (voice);
        const auto filtered = filterVoice (events, voice);
        foundEveryVoice = foundEveryVoice && ! filtered.empty();
        for (const auto& e : filtered)
            stepsInRange = stepsInRange && e.step >= 0 && e.step < laneLength;

        int expected = 0;
        for (int tick = 0; tick < 64; ++tick)
        {
            const int step = tick % laneLength;
            if (voice == VoiceKick && step == 0)
                ++expected;
            if (voice == VoiceClosedHat && (step == 0 || step == 7))
                ++expected;
            if (voice == VoicePerc && step == 3)
                ++expected;
            if (voice == VoiceOdd && step == 4)
                ++expected;
        }
        countsMatch = countsMatch && (int) filtered.size() == expected;
    }
    check (foundEveryVoice, "lane lengths: every short lane eventually fires");
    check (stepsInRange, "lane lengths: stepIndex always within the lane length");
    check (countsMatch, "lane lengths: each lane loops on its own length");

    // A 32-step kick lane over four bars covers it twice.
    check (filterVoice (events, VoiceKick).size() == 2,
           "32-step kick lane loops every two bars over four bars");
}

// --- 13. Ghost notes ----------------------------------------------------------
void testGhostNotes()
{
    TransportFixture fixture;
    fixture.muteAllExcept (VoicePerc);
    fixture.params.global.ghost = 0.9f;
    fixture.params.global.humanize = 0.0f;
    fixture.params.global.stability = 1.0f;

    const auto events = filterVoice (fixture.renderHost (48000, 256), VoicePerc);
    check (! events.empty(), "ghost: empty perc steps produce ghost notes at high ghost amount");
    bool ghostsLowAndFlagged = ! events.empty();
    for (const auto& e : events)
        ghostsLowAndFlagged = ghostsLowAndFlagged
                              && (e.flags & (unsigned int) HitFlag::ghost) != 0u
                              && e.velocity < 0.4f;
    check (ghostsLowAndFlagged, "ghost: flagged as ghost and quiet");

    TransportFixture noGhost;
    noGhost.pattern = fixture.pattern;
    noGhost.params = fixture.params;
    noGhost.params.global.ghost = 0.0f;
    check (filterVoice (noGhost.renderHost (48000, 256), VoicePerc).empty(),
           "ghost: ghost amount 0 disables ghost notes");

    TransportFixture anchors;
    anchors.hit (VoiceKick, 0, 0.9f);
    anchors.muteAllExcept (VoiceKick);
    anchors.params.global.ghost = 1.0f;
    const auto kickOnly = filterVoice (anchors.renderHost (24000, 256), VoiceKick);
    check (kickOnly.size() == 1, "ghost: anchor kick never gets ghost notes (only programmed hits)");
    for (const auto& e : kickOnly)
        check ((e.flags & (unsigned int) HitFlag::ghost) == 0u, "ghost: kick hit is not a ghost");
}

// --- 14. FERAL bounds ---------------------------------------------------------
void testFeralBounds()
{
    TransportFixture fixture;
    fixture.unmuteAll();
    fixture.params.global.personality = (int) Personality::feral;
    fixture.params.global.humanize = 1.0f;
    fixture.params.global.stability = 0.0f;
    fixture.params.global.swing = 0.75f;
    fixture.params.global.ghost = 0.5f;
    fixture.params.global.accent = 0.7f;
    fixture.params.global.seed = 5150;

    for (int v = 0; v < kNumVoices; ++v)
    {
        fixture.hit (v, 0, 0.85f, 1.0f, v % 2 == 0 ? 0.5f : -0.5f);
        fixture.hit (v, 7, 0.6f, 0.8f, 0.5f, 0.0f, 4);
    }

    const auto events = fixture.renderHost (48000 * 2, 512);
    check (! events.empty(), "feral: events produced");

    const double stepSamples = (60.0 * kSampleRate / 120.0) * 0.25;
    const double maxDevSamples = (0.75 + 0.5) * stepSamples + (25.0 + 45.0) * kSampleRate / 1000.0;
    bool bounded = true;
    bool feralFlagSeen = false;
    for (const auto& e : events)
    {
        bounded = bounded && std::abs ((double) e.timingDevMs) * kSampleRate / 1000.0 <= maxDevSamples + 1.0;
        bounded = bounded && std::abs (e.pitchSemis) <= 14.0f;
        bounded = bounded && e.decayScale >= 0.5f && e.decayScale <= 1.8f;
        feralFlagSeen = feralFlagSeen || (e.flags & (unsigned int) HitFlag::feral) != 0u;
    }
    check (bounded, "feral: timing/pitch/decay deviations stay within musical bounds");
    check (feralFlagSeen, "feral: rare excursions are flagged");

    bool finiteAll = true;
    for (const auto& e : events)
        finiteAll = finiteAll && finite (e);
    check (finiteAll, "feral: hostile extremes produce finite events");
}

// --- 15. Bounded output / maxOut ---------------------------------------------
void testMaxOut()
{
    TransportFixture fixture;
    fixture.unmuteAll();
    fixture.params.global.ghost = 0.8f;
    for (int v = 0; v < kNumVoices; ++v)
        for (int s = 0; s < kMaxSteps; ++s)
            fixture.hit (v, s, 0.7f, 1.0f, 0.0f, 0.0f, 4);

    GrooveEngine engine;
    engine.prepare (kSampleRate);
    SequencedEvent scratch[1];
    const int generated = engine.generate (fixture.pattern, fixture.params, 0, 3000.0, 0.0, scratch, 1);
    check (generated <= 1, "maxOut: generate never overflows its buffer");

    SequencedEvent out[3];
    SequencerFrame frame;
    HostTransport host;
    host.available = true;
    host.playing = true;
    host.bpm = 120.0;
    int numOut = 0;
    fixture.sequencer.process (host, fixture.pattern, fixture.params, 2048, out, 3, numOut, frame);
    check (numOut <= 3, "maxOut: process caps the event count");

    // Events are handed to the engine sorted by sample offset.
    SequencedEvent sorted[128];
    int numSorted = 0;
    fixture.sequencer.process (host, fixture.pattern, fixture.params, 2048, sorted, 128, numSorted, frame);
    bool ascending = true;
    for (int i = 1; i < numSorted; ++i)
        ascending = ascending && sorted[i - 1].sampleOffset <= sorted[i].sampleOffset;
    check (numSorted > 0 && ascending, "process returns events sorted by sampleOffset");
    bool inBlock = true;
    for (int i = 0; i < numSorted; ++i)
        inBlock = inBlock && sorted[i].sampleOffset >= 0 && sorted[i].sampleOffset < 2048;
    check (inBlock, "process only emits events inside the current block");
}

// --- 16. Silence / reset ------------------------------------------------------
void testSilenceAndReset()
{
    TransportFixture fixture;
    fixture.hit (VoiceKick, 0, 0.9f);

    SequencedEvent out[kMaxTestEvents];
    SequencerFrame frame;
    int numOut = 0;
    HostTransport noHost;
    noHost.available = false;
    noHost.playing = false;
    fixture.sequencer.process (noHost, fixture.pattern, fixture.params, 512, out, kMaxTestEvents, numOut, frame);
    check (numOut == 0, "no clock + not running -> silence");
    check (! frame.playing, "no clock + not running -> frame.playing false");
    for (int v = 0; v < kNumVoices; ++v)
        check (frame.laneStep[v] == -1, "no clock -> laneStep -1 for voice " + std::to_string (v));

    fixture.sequencer.setInternalRunning (true);
    fixture.sequencer.process (noHost, fixture.pattern, fixture.params, 512, out, kMaxTestEvents, numOut, frame);
    check (numOut > 0, "free-run: events after starting the internal clock");

    fixture.sequencer.reset();
    checkNear (fixture.sequencer.getInternalPpq(), 0.0, 1.0e-12, "reset: internal ppq back to zero");
    check (! fixture.sequencer.wasHostSynced(), "reset: host sync flag cleared");
}

// --- 17. Deterministic generation is stateless -------------------------------
void testGeneratePurity()
{
    TransportFixture fixture;
    fixture.params.global.humanize = 0.8f;
    fixture.params.global.stability = 0.2f;
    fixture.params.global.ghost = 0.5f;
    fixture.params.global.swing = 0.3f;
    for (int v = 0; v < kNumVoices; ++v)
        fixture.hit (v, 2, 0.7f, 0.8f, 0.1f, 1.5f, 2);

    GrooveEngine a;
    GrooveEngine b;
    a.prepare (kSampleRate);
    b.prepare (kSampleRate);

    SequencedEvent ea[128], eb[128];
    bool identical = true;
    for (long long tick = 0; tick < 32; ++tick)
    {
        const int na = a.generate (fixture.pattern, fixture.params, tick, 6000.0, 0.0, ea, 128);
        const int nb = b.generate (fixture.pattern, fixture.params, tick, 6000.0, 0.0, eb, 128);
        identical = identical && na == nb;
        for (int i = 0; i < std::min (na, nb); ++i)
            identical = identical && ea[i].trigger.velocity == eb[i].trigger.velocity
                        && ea[i].sampleOffset == eb[i].sampleOffset
                        && ea[i].trigger.seed == eb[i].trigger.seed
                        && ea[i].timingDevMs == eb[i].timingDevMs;
    }
    check (identical, "generate is a pure function of its inputs (no hidden state)");
}

// --- 18. Free-run block-size invariance on the QA integration scenario --------
void testFreeRunBlockInvariance()
{
    // Mirrors the QA integration render: factory pattern 0, FERAL, swing 0.25,
    // humanize 0.5, seed 4242, 409600 samples - but rendered through the free-
    // running clock like the headless processor does (no host transport).
    TransportFixture fixture;
    fixture.pattern = factoryPattern (0);
    fixture.params.global.seed = 4242;
    fixture.params.global.swing = 0.25f;
    fixture.params.global.humanize = 0.5f;
    fixture.params.global.personality = (int) Personality::feral;

    constexpr int totalSamples = 409600;
    const auto reference = fixture.renderFree (totalSamples, 64);
    check (! reference.empty(), "free-run invariance: reference render has events");

    bool allMatch = true;
    for (int blockSize : { 128, 512, 2048 })
    {
        const auto rendered = fixture.renderFree (totalSamples, blockSize);
        if (! sameEventListWithin (reference, rendered, 1))
        {
            allMatch = false;
            info ("free-run block-size mismatch at blockSize=" + std::to_string (blockSize)
                  + " events " + std::to_string (reference.size()) + "/"
                  + std::to_string (rendered.size()));
        }
    }
    check (allMatch, "free-run: absolute event timing identical for 64/128/512/2048-sample blocks");
}

// --- 19. Stability = 1 is mechanical ------------------------------------------
void testMechanicalStability()
{
    TransportFixture a;
    a.muteAllExcept (VoiceKick);
    a.hit (VoiceKick, 0, 0.8f);
    a.hit (VoiceKick, 2, 0.8f);
    a.params.global.humanize = 1.0f;
    a.params.global.stability = 1.0f;
    a.params.global.ghost = 1.0f;

    TransportFixture b;
    b.pattern = a.pattern;
    b.params = a.params;
    b.params.global.seed = 31337;

    const auto ea = filterVoice (a.renderHost (48000, 256), VoiceKick);
    const auto eb = filterVoice (b.renderHost (48000, 256), VoiceKick);
    check (! ea.empty() && ! eb.empty(), "stability=1: events still fire");
    check (sameEventList (ea, eb, true, false),
           "stability=1: fully mechanical (no seed-dependent timing/velocity)");
}

// --- 20. Humanisation is correlated, not white jitter ------------------------
void testCorrelatedHumanisation()
{
    TransportFixture fixture;
    fixture.muteAllExcept (VoiceKick);
    fixture.pattern.lanes[(size_t) VoiceKick].length = 1; // fire on every tick
    fixture.hit (VoiceKick, 0, 0.8f);
    fixture.params.global.humanize = 1.0f;
    fixture.params.global.stability = 0.0f;
    fixture.params.global.personality = (int) Personality::loose;
    fixture.params.voice[(size_t) VoiceKick].randTiming = 1.0f;
    fixture.params.voice[(size_t) VoiceKick].timingBiasMs = 0.0f;

    // Every 16th is 6000 samples at 120 bpm; read the stochastic timing directly
    // through the engine (one hit per tick).
    GrooveEngine engine;
    engine.prepare (kSampleRate);
    SequencedEvent out[8];
    std::vector<float> deviations;
    for (long long tick = 0; tick < 64; ++tick)
    {
        const int n = engine.generate (fixture.pattern, fixture.params, tick, 6000.0, 0.0, out, 8);
        if (n > 0)
            deviations.push_back (out[0].timingDevMs);
    }

    check (deviations.size() >= 32, "correlated humanisation: enough ticks sampled");
    if (deviations.size() >= 32)
    {
        double meanAbs = 0.0;
        double meanAbsDelta = 0.0;
        for (size_t i = 0; i < deviations.size(); ++i)
        {
            meanAbs += std::abs ((double) deviations[i]);
            if (i > 0)
                meanAbsDelta += std::abs ((double) deviations[i] - (double) deviations[i - 1]);
        }
        meanAbs /= (double) deviations.size();
        meanAbsDelta /= (double) (deviations.size() - 1);
        check (meanAbs > 0.5, "correlated humanisation: audible timing spread at maximum depth");
        // Independent (white) jitter would have mean |delta| around 1.3x mean |value|.
        check (meanAbsDelta < meanAbs,
               "correlated humanisation: neighbouring ticks move together, not independently");
        info ("timing spread: mean |dev| " + std::to_string (meanAbs)
              + " ms, mean |tick-to-tick| " + std::to_string (meanAbsDelta) + " ms");
    }
}

} // namespace

} // namespace klopgeest

// Entry point declared in tests/SequencerTests.h (global namespace, owned by the
// QA test runner).
void runSequencerTests()
{
    std::printf ("\n-- sequencer / groove engine --\n");
    const int checksBefore = klop_test::gChecks;
    const int failuresBefore = klop_test::gFailures;

    klopgeest::testDeterminism();
    klopgeest::testBlockSizeInvariance();
    klopgeest::testProbability();
    klopgeest::testLockedSteps();
    klopgeest::testSwing();
    klopgeest::testRatchets();
    klopgeest::testAccent();
    klopgeest::testHostSync();
    klopgeest::testTransportJumpsAndTempo();
    klopgeest::testFreeRun();
    klopgeest::testMuteSolo();
    klopgeest::testLaneLengths();
    klopgeest::testGhostNotes();
    klopgeest::testFeralBounds();
    klopgeest::testMaxOut();
    klopgeest::testSilenceAndReset();
    klopgeest::testGeneratePurity();
    klopgeest::testFreeRunBlockInvariance();
    klopgeest::testMechanicalStability();
    klopgeest::testCorrelatedHumanisation();

    klop_test::info ("sequencer/groove: " + std::to_string (klop_test::gChecks - checksBefore)
                     + " checks, " + std::to_string (klop_test::gFailures - failuresBefore)
                     + " failures");
}
