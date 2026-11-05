#pragma once

#include <array>

#include "core/Constants.h"
#include "dsp/voices/Voice.h"

// -----------------------------------------------------------------------------
// Sequencer <-> engine shared types.
// -----------------------------------------------------------------------------
namespace klopgeest
{

struct SequencedEvent
{
    int voice        = 0;
    int sampleOffset = 0;      // relative to the start of the current block
    VoiceTrigger trigger;
    int stepIndex    = 0;      // lane step that produced this hit
    long long tickIndex = 0;   // absolute 16th-note grid index
    float timingDevMs = 0.0f;  // total timing deviation applied (telemetry)
};

struct SequencerFrame
{
    int laneStep[kNumVoices] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    bool playing = false;
    bool hostSynced = false;
    long long tickIndex = 0;
    float bpm = 120.0f;
};

/** What the processor observed in the host transport at the start of a block. */
struct HostTransport
{
    bool available = false;
    bool playing = false;
    double ppqPosition = 0.0;
    double bpm = 120.0;
    bool jumped = false;
};

} // namespace klopgeest
