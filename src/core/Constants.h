#pragma once

#include <cstdint>

// -----------------------------------------------------------------------------
// KLOPGEEST — shared constants.
//
// Nothing in here is allowed to change the saved-state format without a
// migration in PluginProcessor::setStateInformation().
// -----------------------------------------------------------------------------
namespace klopgeest
{

inline constexpr const char* kProductName = "KLOPGEEST";
inline constexpr const char* kCompanyName = "Master Lonn & Little Wan";
inline constexpr const char* kVersion     = "0.1.0";

inline constexpr int kNumVoices = 8;
inline constexpr int kMaxSteps  = 64;
inline constexpr int kMinSteps  = 16;
inline constexpr int kStepsPerBar = 16;          // 16th-note grid
inline constexpr int kDefaultSteps = 16;         // one bar
inline constexpr int kBeatsPerBar = 4;

// Voice indices. These are permanent: they are indices into the pattern, the
// parameter layout and the visual state.
enum VoiceIndex : int
{
    VoiceKick = 0,
    VoiceSnare,
    VoiceClap,
    VoiceClosedHat,
    VoiceOpenHat,
    VoiceTom,
    VoicePerc,
    VoiceOdd
};

// Per-lane role weights used by the groove engine: anchors (kick/snare) are
// kept tighter than colour voices (hats/perc/odd). Higher = looser.
inline constexpr float kVoiceRoleLoose [kNumVoices] =
{
    0.35f,  // kick — the pulse
    0.50f,  // snare — the orientation
    0.65f,  // clap
    0.90f,  // closed hat
    0.95f,  // open hat
    0.75f,  // tom
    1.05f,  // perc
    1.20f   // odd
};

// GM-style note map for live playing: C1 (36) .. G1 (43).
inline constexpr int kBaseMidiNote = 36;

inline constexpr int kNumFactoryPatterns = 6;

} // namespace klopgeest
