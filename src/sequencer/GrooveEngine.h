#pragma once

#include "core/EngineParams.h"
#include "core/Pattern.h"
#include "sequencer/SequencerTypes.h"

// -----------------------------------------------------------------------------
// GROOVE ENGINE — the part that makes a grid feel like a drummer.
//
// Design rules:
//  1. All stochastic behaviour is a pure function of (seed, voice, tickIndex,
//     salt). No hidden state => deterministic renders, block-size invariance,
//     reproducible live takes.
//  2. Randomness is correlated over time (value noise), not white jitter. Hats
//     drift together, not like a broken clock.
//  3. Rhythm roles matter: the kick and snare are anchors and stay tighter than
//     hats/perc/odd (see kVoiceRoleLoose).
//  4. Structure is preserved. FERAL may push hard, but the pulse remains
//     intelligible: deviations are clamped.

namespace klopgeest
{

class GrooveEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    /**
        Generates every trigger belonging to one absolute 16th-note grid tick.

        @param pattern              active pattern
        @param params               engine parameter snapshot
        @param tickIndex            absolute grid index (0 = transport start)
        @param stepSamples          nominal duration of one 16th in samples
        @param nominalOffsetSamples tick's nominal position relative to block start
        @param out                  destination event array
        @param maxOut               capacity of `out`
        @returns number of events written (offsets may fall outside the block;
                  the sequencer filters and compacts them)
    */
    int generate (const PatternData& pattern,
                  const EngineParams& params,
                  long long tickIndex,
                  double stepSamples,
                  double nominalOffsetSamples,
                  SequencedEvent* out,
                  int maxOut);

private:
    double sampleRate = 48000.0;
};

} // namespace klopgeest
