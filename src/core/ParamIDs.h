#pragma once

#include <juce_core/juce_core.h>
#include <array>

// -----------------------------------------------------------------------------
// Stable parameter identifiers.
//
// NEVER rename these strings in a release: host automation, presets and saved
// sessions address parameters by id. New parameters may be added; existing ones
// are permanent API.
// -----------------------------------------------------------------------------
namespace klopgeest::pid
{

inline constexpr const char* voicePrefix [8] =
{
    "kick", "snare", "clap", "ch", "oh", "tom", "perc", "odd"
};

// Per-voice parameter suffixes (same set for every voice).
namespace v
{
inline constexpr const char* pitch       = "pitch";
inline constexpr const char* decay       = "decay";
inline constexpr const char* tone        = "tone";
inline constexpr const char* transient   = "trans";
inline constexpr const char* drive       = "drive";
inline constexpr const char* level       = "level";
inline constexpr const char* pan         = "pan";
inline constexpr const char* specialA    = "specialA";
inline constexpr const char* specialB    = "specialB";
inline constexpr const char* randPitch   = "randPitch";
inline constexpr const char* randLevel   = "randLevel";
inline constexpr const char* randDecay   = "randDecay";
inline constexpr const char* randTrans   = "randTrans";
inline constexpr const char* randTiming  = "randTiming";
inline constexpr const char* bias        = "bias";
} // namespace v

// --- Global performance ------------------------------------------------------
inline constexpr const char* tempo       = "global.tempo";
inline constexpr const char* swing       = "global.swing";
inline constexpr const char* humanize    = "global.humanize";
inline constexpr const char* stability   = "global.stability";
inline constexpr const char* personality = "global.personality";
inline constexpr const char* ghost       = "global.ghost";
inline constexpr const char* accent      = "global.accent";
inline constexpr const char* seed        = "global.seed";
inline constexpr const char* mutateAmount = "global.mutateAmount";
inline constexpr const char* evolve      = "global.evolve";

// --- Master glue machine -----------------------------------------------------
inline constexpr const char* threshold   = "master.threshold";
inline constexpr const char* ratio       = "master.ratio";
inline constexpr const char* attack      = "master.attack";
inline constexpr const char* release     = "master.release";
inline constexpr const char* knee        = "master.knee";
inline constexpr const char* autoRelease = "master.autoRelease";
inline constexpr const char* sidechainHz = "master.sidechainHz";
inline constexpr const char* detect      = "master.detect";
inline constexpr const char* mix         = "master.mix";
inline constexpr const char* satPre      = "master.satPre";
inline constexpr const char* satPost     = "master.satPost";
inline constexpr const char* output      = "master.output";
inline constexpr const char* ceiling     = "master.ceiling";
inline constexpr const char* glue        = "master.glue";
inline constexpr const char* heat        = "master.heat";
inline constexpr const char* limiter     = "master.limiter";

/** Full parameter id for a per-voice parameter, e.g. "kick.pitch". */
inline juce::String voiceId (int voice, const char* suffix)
{
    jassert (voice >= 0 && voice < 8);
    return juce::String (voicePrefix[voice]) + "." + suffix;
}

/** Full parameter id for a per-voice parameter suffix constant. */
inline juce::String voiceId (int voice, const juce::String& suffix)
{
    return voiceId (voice, suffix.toRawUTF8());
}

} // namespace klopgeest::pid
