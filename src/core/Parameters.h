#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

#include "core/Constants.h"
#include "core/EngineParams.h"
#include "core/ParamIDs.h"

// -----------------------------------------------------------------------------
// Single source of truth for parameter ranges, defaults, labels and the APVTS
// layout. The GUI, the DSP adapter, presets and tests all read from here so
// they cannot drift apart.
// -----------------------------------------------------------------------------
namespace klopgeest
{

struct ParamRange
{
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    float centre = 0.0f;   // >0 enables a skewed (log-ish) response
    float step = 0.0f;     // >0 quantises
};

namespace ranges
{
inline constexpr ParamRange pitch        { -24.0f, 24.0f, 0.0f, 0.0f, 0.1f };
inline constexpr ParamRange unit         { 0.0f, 1.0f, 0.5f };
inline constexpr ParamRange levelDb      { -60.0f, 6.0f, -6.0f };
inline constexpr ParamRange pan          { -1.0f, 1.0f, 0.0f };
inline constexpr ParamRange randAmount   { 0.0f, 1.0f, 0.30f };
inline constexpr ParamRange timingBias   { -25.0f, 25.0f, 0.0f };
inline constexpr ParamRange tempo        { 60.0f, 200.0f, 120.0f };
inline constexpr ParamRange swing        { 0.0f, 0.75f, 0.12f };
inline constexpr ParamRange humanize     { 0.0f, 1.0f, 0.35f };
inline constexpr ParamRange stability    { 0.0f, 1.0f, 0.75f };
inline constexpr ParamRange ghost        { 0.0f, 1.0f, 0.15f };
inline constexpr ParamRange accent       { 0.0f, 1.0f, 0.35f };
inline constexpr ParamRange mutateAmount { 0.0f, 1.0f, 0.35f };

inline constexpr ParamRange threshold    { -60.0f, 0.0f, -18.0f };
inline constexpr ParamRange ratio        { 1.0f, 20.0f, 4.0f };
inline constexpr ParamRange attackMs     { 0.1f, 100.0f, 8.0f, 8.0f };
inline constexpr ParamRange releaseMs    { 20.0f, 1200.0f, 180.0f, 180.0f };
inline constexpr ParamRange knee         { 0.0f, 24.0f, 8.0f };
inline constexpr ParamRange sidechainHz  { 20.0f, 500.0f, 90.0f, 90.0f };
inline constexpr ParamRange outputDb     { -24.0f, 12.0f, 0.0f };
inline constexpr ParamRange ceilingDb    { -12.0f, 0.0f, -0.4f };
} // namespace ranges

/** Voice-panel parameter ordering. The suffix table below must match. */
enum VoiceParamIndex : int
{
    VPitch = 0, VDecay, VTone, VTransient, VDrive, VLevel, VPan,
    VSpecialA, VSpecialB,
    VRandPitch, VRandLevel, VRandDecay, VRandTransient, VRandTiming,
    VBias,
    kNumVoiceParams
};

namespace detail
{
inline constexpr const char* kVoiceParamSuffix [kNumVoiceParams] =
{
    pid::v::pitch, pid::v::decay, pid::v::tone, pid::v::transient,
    pid::v::drive, pid::v::level, pid::v::pan,
    pid::v::specialA, pid::v::specialB,
    pid::v::randPitch, pid::v::randLevel, pid::v::randDecay,
    pid::v::randTrans, pid::v::randTiming, pid::v::bias
};

inline constexpr const char* kVoiceParamLabel [kNumVoiceParams] =
{
    "PITCH", "DECAY", "TONE", "TRANSIENT", "DRIVE", "LEVEL", "PAN",
    "CHARACTER", "TEXTURE",
    "PITCH VAR", "LEVEL VAR", "DECAY VAR", "TRANS VAR", "TIMING VAR",
    "TIMING BIAS"
};
} // namespace detail

inline juce::StringArray personalityNames() { return { "TIGHT", "LOOSE", "FERAL" }; }

inline const char* voiceName (int voice)
{
    static constexpr const char* names [kNumVoices] =
        { "KICK", "SNARE", "CLAP", "CLOSED HAT", "OPEN HAT", "TOM", "PERC", "ODD" };
    return names[juce::jlimit (0, kNumVoices - 1, voice)];
}

inline const char* voiceShortName (int voice)
{
    static constexpr const char* names [kNumVoices] =
        { "KICK", "SNARE", "CLAP", "CH", "OH", "TOM", "PERC", "ODD" };
    return names[juce::jlimit (0, kNumVoices - 1, voice)];
}

/** Voice-specific names for the two character knobs (specialA / specialB). */
inline const char* voiceSpecialAName (int voice)
{
    static constexpr const char* names [kNumVoices] =
        { "SUB", "WIRES", "DIFFUSION", "METAL", "METAL", "BEND", "MODE", "CHAOS" };
    return names[juce::jlimit (0, kNumVoices - 1, voice)];
}

inline const char* voiceSpecialBName (int voice)
{
    static constexpr const char* names [kNumVoices] =
        { "BODY", "NOISE", "WIDTH", "SIZZLE", "CHOKE", "SKIN", "SPACE", "RING" };
    return names[juce::jlimit (0, kNumVoices - 1, voice)];
}

inline const char* voiceParamLabel (int voice, int paramIndex)
{
    if (paramIndex == VSpecialA) return voiceSpecialAName (voice);
    if (paramIndex == VSpecialB) return voiceSpecialBName (voice);
    return detail::kVoiceParamLabel[juce::jlimit (0, kNumVoiceParams - 1, paramIndex)];
}

/** Sensible per-voice defaults so the instrument is musical out of the box. */
struct VoiceDefaults
{
    float pitch, decay, tone, transient, drive, levelDb, pan, specialA, specialB;
};

inline const VoiceDefaults& voiceDefaults (int voice)
{
    static const VoiceDefaults defaults [kNumVoices] =
    {
        { 0.0f, 0.45f, 0.35f, 0.65f, 0.28f, -3.0f,  0.00f, 0.35f, 0.50f }, // kick
        { 0.0f, 0.45f, 0.55f, 0.55f, 0.18f, -8.0f,  0.00f, 0.55f, 0.45f }, // snare
        { 0.0f, 0.35f, 0.60f, 0.50f, 0.12f, -10.0f, 0.05f, 0.50f, 0.60f }, // clap
        { 0.0f, 0.25f, 0.60f, 0.30f, 0.10f, -13.0f, 0.10f, 0.55f, 0.40f }, // ch
        { 0.0f, 0.60f, 0.60f, 0.20f, 0.10f, -14.0f, -0.10f, 0.55f, 0.50f },// oh
        { 0.0f, 0.50f, 0.50f, 0.40f, 0.15f, -9.0f,  0.15f, 0.30f, 0.50f }, // tom
        { 0.0f, 0.35f, 0.60f, 0.60f, 0.20f, -10.0f, -0.20f, 0.15f, 0.50f },// perc
        { 0.0f, 0.40f, 0.50f, 0.50f, 0.35f, -12.0f, 0.20f, 0.40f, 0.50f }  // odd
    };
    return defaults[juce::jlimit (0, kNumVoices - 1, voice)];
}

inline juce::NormalisableRange<float> makeRange (const ParamRange& r)
{
    juce::NormalisableRange<float> range (r.min, r.max);
    if (r.step > 0.0f)
        range.interval = r.step;
    if (r.centre > r.min && r.centre < r.max)
        range.setSkewForCentre (r.centre);
    return range;
}

/** Builds the complete parameter layout. Called once by the processor. */
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace klopgeest
