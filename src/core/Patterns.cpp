#include "core/Patterns.h"

#include <array>

namespace klopgeest
{

namespace
{
void hit (PatternData& pattern, int voice, int step, float velocity,
          float probability = 1.0f, float micro = 0.0f, float pitch = 0.0f,
          int ratchet = 1, bool locked = false)
{
    if (step < 0 || step >= kMaxSteps)
        return;
    auto& s = pattern.step (voice, step);
    s.on = true;
    s.velocity = velocity;
    s.probability = probability;
    s.microtiming = micro;
    s.pitchSemis = pitch;
    s.ratchet = ratchet;
    s.locked = locked;
}

void setLength (PatternData& pattern, int steps)
{
    pattern.length = steps;
    for (auto& lane : pattern.lanes)
        lane.length = steps;
}

void setLaneLength (PatternData& pattern, int voice, int length)
{
    pattern.lanes[(size_t) voice].length = length;
}

PatternData makePattern (const char* name, int steps)
{
    PatternData p;
    setLength (p, steps);
    p.setName (name);
    return p;
}
} // namespace

PatternData makeInitPattern()
{
    auto p = makePattern ("INIT", 16);

    hit (p, VoiceKick, 0, 1.0f);
    hit (p, VoiceKick, 4, 0.95f);
    hit (p, VoiceKick, 8, 1.0f);
    hit (p, VoiceKick, 12, 0.90f);

    hit (p, VoiceSnare, 4, 0.92f);
    hit (p, VoiceSnare, 12, 0.97f);

    for (int s = 0; s < 16; s += 2)
        hit (p, VoiceClosedHat, s, s % 4 == 2 ? 0.55f : 0.36f);

    hit (p, VoicePerc, 6, 0.5f, 0.8f, 0.02f, 3.0f);
    hit (p, VoicePerc, 14, 0.45f, 0.7f);

    return p;
}

int factoryPatternCount() { return kNumFactoryPatterns; }

juce::StringArray factoryPatternNames()
{
    juce::StringArray names;
    for (int i = 0; i < kNumFactoryPatterns; ++i)
        names.add (factoryPatternName (i));
    return names;
}

juce::String factoryPatternName (int index)
{
    static const char* names[kNumFactoryPatterns] =
    {
        "STRAIGHT FIRE", "BROKEN STEP", "GHOST POCKET",
        "WAREHOUSE", "FERAL DUB", "SIXTEENTH ANXIETY"
    };
    return juce::jlimit (0, kNumFactoryPatterns - 1, index) < kNumFactoryPatterns
               ? names[juce::jlimit (0, kNumFactoryPatterns - 1, index)]
               : juce::String ("INIT");
}

PatternData factoryPattern (int index)
{
    switch (juce::jlimit (0, kNumFactoryPatterns - 1, index))
    {
        case 0:
        {
            // Four-to-the-floor, offbeat open hats, a breath of percussion.
            auto p = makePattern ("STRAIGHT FIRE", 16);
            for (int s : { 0, 4, 8, 12 })
                hit (p, VoiceKick, s, s == 0 ? 1.0f : 0.94f, 1.0f, 0.0f, 0.0f, 1, s == 0);
            hit (p, VoiceSnare, 4, 0.90f);
            hit (p, VoiceSnare, 12, 0.97f);
            hit (p, VoiceClap, 12, 0.42f, 0.6f, 0.0f, 0.0f);
            for (int s = 0; s < 16; ++s)
                hit (p, VoiceClosedHat, s, s % 4 == 0 ? 0.30f : (s % 2 == 0 ? 0.22f : 0.16f));
            hit (p, VoiceOpenHat, 2, 0.42f);
            hit (p, VoiceOpenHat, 6, 0.38f);
            hit (p, VoiceOpenHat, 10, 0.42f);
            hit (p, VoiceOpenHat, 14, 0.40f);
            hit (p, VoicePerc, 7, 0.35f, 0.55f, 0.01f, 5.0f);
            return p;
        }
        case 1:
        {
            // Syncopated kick, displaced clap, hats that lean forward.
            auto p = makePattern ("BROKEN STEP", 16);
            for (int s : { 0, 3, 6, 10, 13 })
                hit (p, VoiceKick, s, s == 0 ? 1.0f : 0.82f, 1.0f, s == 6 ? 0.015f : 0.0f);
            hit (p, VoiceSnare, 4, 0.95f);
            hit (p, VoiceSnare, 12, 0.92f);
            hit (p, VoiceClap, 12, 0.55f);
            hit (p, VoiceClap, 11, 0.30f, 0.35f, 0.01f);
            for (int s = 0; s < 16; ++s)
                if (s % 4 != 3)
                    hit (p, VoiceClosedHat, s, s % 4 == 0 ? 0.34f : 0.20f, 0.95f, 0.0f);
            hit (p, VoiceOpenHat, 5, 0.36f);
            hit (p, VoiceOpenHat, 15, 0.30f);
            hit (p, VoicePerc, 2, 0.45f, 0.7f, 0.02f, 7.0f);
            hit (p, VoicePerc, 9, 0.38f, 0.6f, 0.0f, -4.0f);
            hit (p, VoiceOdd, 14, 0.25f, 0.5f, 0.035f, 12.0f);
            return p;
        }
        case 2:
        {
            // Lazy pocket: kick and snare late, ghosts written in.
            auto p = makePattern ("GHOST POCKET", 16);
            hit (p, VoiceKick, 0, 1.0f, 1.0f, -0.02f);
            hit (p, VoiceKick, 7, 0.78f, 1.0f, 0.03f);
            hit (p, VoiceKick, 10, 0.65f, 1.0f, 0.025f);
            hit (p, VoiceSnare, 4, 0.85f, 1.0f, 0.02f);
            hit (p, VoiceSnare, 12, 0.90f, 1.0f, 0.02f);
            hit (p, VoiceSnare, 14, 0.20f, 0.8f, 0.03f);
            hit (p, VoiceClosedHat, 0, 0.28f);
            hit (p, VoiceClosedHat, 3, 0.16f, 0.8f);
            hit (p, VoiceClosedHat, 6, 0.26f);
            hit (p, VoiceClosedHat, 8, 0.18f);
            hit (p, VoiceClosedHat, 11, 0.14f, 0.75f);
            hit (p, VoiceClosedHat, 14, 0.30f);
            hit (p, VoiceOpenHat, 6, 0.30f);
            hit (p, VoiceTom, 13, 0.42f, 0.7f, 0.0f, -5.0f);
            hit (p, VoicePerc, 2, 0.30f, 0.6f, 0.0f, 9.0f);
            hit (p, VoicePerc, 9, 0.22f, 0.5f, 0.0f, 4.0f);
            return p;
        }
        case 3:
        {
            // Tight warehouse pressure, 32 steps, clap on the backbeat.
            auto p = makePattern ("WAREHOUSE", 32);
            for (int bar = 0; bar < 2; ++bar)
            {
                for (int beat = 0; beat < 4; ++beat)
                    hit (p, VoiceKick, bar * 16 + beat * 4, beat == 0 ? 1.0f : 0.95f, 1.0f,
                         beat == 1 ? 0.0f : 0.0f, 0.0f, 1, beat == 0);
                hit (p, VoiceClap, bar * 16 + 4, 0.72f);
                hit (p, VoiceClap, bar * 16 + 12, 0.80f);
                for (int s = 0; s < 16; ++s)
                    hit (p, VoiceClosedHat, bar * 16 + s,
                         s % 4 == 2 ? 0.34f : (s % 2 == 0 ? 0.22f : 0.14f));
                hit (p, VoiceOpenHat, bar * 16 + 14, 0.30f);
            }
            hit (p, VoicePerc, 11, 0.40f, 0.8f, 0.0f, -7.0f);
            hit (p, VoicePerc, 27, 0.36f, 0.7f, 0.0f, 3.0f);
            hit (p, VoiceOdd, 30, 0.22f, 0.4f, 0.02f, 12.0f);
            return p;
        }
        case 4:
        {
            // Dubby, sparse, with an offset tom and unstable odd voice.
            auto p = makePattern ("FERAL DUB", 16);
            hit (p, VoiceKick, 0, 1.0f);
            hit (p, VoiceKick, 8, 0.85f, 1.0f, 0.02f);
            hit (p, VoiceSnare, 4, 0.60f, 0.9f, 0.03f);
            hit (p, VoiceSnare, 12, 0.55f, 0.85f, 0.03f);
            hit (p, VoiceOpenHat, 2, 0.48f);
            hit (p, VoiceOpenHat, 6, 0.40f);
            hit (p, VoiceOpenHat, 10, 0.46f);
            hit (p, VoiceOpenHat, 14, 0.42f);
            hit (p, VoiceClosedHat, 5, 0.20f, 0.7f);
            hit (p, VoiceClosedHat, 13, 0.18f, 0.6f);
            hit (p, VoiceTom, 7, 0.55f, 0.8f, 0.04f, -8.0f);
            hit (p, VoicePerc, 3, 0.35f, 0.6f, 0.0f, 10.0f);
            hit (p, VoiceOdd, 15, 0.30f, 0.6f, 0.05f, 12.0f, 2);
            setLaneLength (p, VoiceClosedHat, 12);
            return p;
        }
        default:
        {
            // Dense 32-step, ratchets, probability, controlled panic.
            auto p = makePattern ("SIXTEENTH ANXIETY", 32);
            for (int s : { 0, 4, 8, 12, 16, 20, 24, 28 })
                hit (p, VoiceKick, s, s % 16 == 0 ? 1.0f : 0.92f);
            hit (p, VoiceSnare, 4, 0.92f);
            hit (p, VoiceSnare, 12, 0.96f);
            hit (p, VoiceSnare, 20, 0.88f);
            hit (p, VoiceSnare, 28, 0.94f);
            for (int s = 0; s < 32; ++s)
                hit (p, VoiceClosedHat, s, s % 4 == 2 ? 0.34f : (s % 2 == 0 ? 0.24f : 0.12f),
                      s % 8 == 7 ? 0.8f : 1.0f, 0.0f, 0.0f, s % 8 == 6 ? 2 : 1);
            for (int s : { 6, 14, 22, 30 })
                hit (p, VoiceClap, s, 0.42f, 0.6f);
            hit (p, VoicePerc, 10, 0.42f, 0.7f, 0.0f, 6.0f, 2);
            hit (p, VoicePerc, 26, 0.38f, 0.6f, 0.0f, -2.0f);
            hit (p, VoiceOdd, 15, 0.32f, 0.75f, 0.03f, 12.0f);
            hit (p, VoiceOdd, 23, 0.26f, 0.5f, 0.02f, -12.0f);
            hit (p, VoiceTom, 19, 0.44f, 0.8f, 0.0f, -3.0f);
            setLaneLength (p, VoicePerc, 16);
            setLaneLength (p, VoiceOdd, 8);
            return p;
        }
    }
}

} // namespace klopgeest
