#pragma once

#include "core/Constants.h"

// -----------------------------------------------------------------------------
// Plain-old-data parameter snapshot handed from the plug-in glue (which reads
// the APVTS atomics once per block) to the audio engine. No strings, no
// pointers owned by the GUI: the engine can be driven from tests and offline
// renders exactly like it is driven from a host.
// -----------------------------------------------------------------------------
namespace klopgeest
{

enum class Personality : int
{
    tight = 0,
    loose = 1,
    feral = 2
};

struct VoiceParams
{
    float pitchSemis   = 0.0f;    // -24 .. +24
    float decay        = 0.5f;    // 0 .. 1 (voice maps to seconds)
    float tone         = 0.5f;    // 0 .. 1
    float transient    = 0.5f;    // 0 .. 1
    float drive        = 0.15f;   // 0 .. 1
    float levelDb      = -6.0f;   // -60 .. +6
    float pan          = 0.0f;    // -1 .. +1

    float specialA     = 0.5f;    // voice-specific (see Parameters.h labels)
    float specialB     = 0.5f;    // voice-specific

    // Independently adjustable stochastic depth per dimension (0..1).
    float randPitch    = 0.15f;
    float randLevel    = 0.30f;
    float randDecay    = 0.20f;
    float randTransient = 0.20f;
    float randTiming   = 0.30f;

    float timingBiasMs = 0.0f;    // -25 .. +25 ms, per-voice groove bias
};

struct GlobalParams
{
    float tempo        = 120.0f;  // internal tempo when free-running
    float swing        = 0.12f;   // 0 .. 0.75
    float humanize     = 0.35f;   // 0 .. 1
    float stability    = 0.75f;   // 0 .. 1 (1 = machine)
    int   personality  = (int) Personality::loose;
    float ghost        = 0.15f;   // 0 .. 1 ghost-note depth
    float accent       = 0.35f;   // 0 .. 1 accent depth
    int   seed         = 12345;   // deterministic performance seed
    float mutateAmount = 0.35f;   // 0 .. 1
    bool  evolve       = false;   // accumulate mutations over repetitions
};

struct MasterParams
{
    float thresholdDb  = -18.0f;  // -60 .. 0
    float ratio        = 4.0f;    // 1 .. 20
    float attackMs     = 8.0f;    // 0.1 .. 100
    float releaseMs    = 180.0f;  // 20 .. 1200
    float kneeDb       = 8.0f;    // 0 .. 24
    bool  autoRelease  = true;
    float sidechainHz  = 90.0f;   // 20 .. 500
    float detect       = 0.5f;    // 0 = peak, 1 = RMS
    float mix          = 1.0f;    // 0 .. 1 parallel compression
    float satPre       = 0.15f;   // 0 .. 1
    float satPost      = 0.10f;   // 0 .. 1
    float outputDb     = 0.0f;    // -24 .. +12
    float ceilingDb    = -0.4f;   // -12 .. 0
    float glue         = 0.35f;   // 0 .. 1 performance macro
    float heat         = 0.25f;   // 0 .. 1 performance macro
    bool  limiter      = true;
};

struct EngineParams
{
    VoiceParams  voice [kNumVoices];
    GlobalParams global;
    MasterParams master;
};

} // namespace klopgeest
