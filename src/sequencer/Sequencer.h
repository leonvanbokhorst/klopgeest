#pragma once

#include <atomic>

#include "core/EngineParams.h"
#include "core/Pattern.h"
#include "sequencer/GrooveEngine.h"
#include "sequencer/SequencerTypes.h"

// -----------------------------------------------------------------------------
// Transport-aware step sequencer.
//
// Host mode: when the host reports a playing transport with a valid ppq
// position, every tick is derived from that position, so tempo changes, loops
// and seeks follow the host exactly.
//
// Free-run mode: when the host has no transport (standalone, or the host is
// stopped) and the musician pressed play, an internal ppq clock advances at the
// Tempo parameter.
//
// Ticks are generated in a look-ahead window wide enough to contain every
// groove deviation, then filtered to the current block. Because groove output
// is a pure function of the tick index, a tick is never emitted twice and
// never lost when the buffer size changes.
// -----------------------------------------------------------------------------
namespace klopgeest
{

class Sequencer
{
public:
    void prepare (double sampleRate);
    void reset();

    void setInternalRunning (bool running) noexcept   { internalRunning.store (running, std::memory_order_relaxed); }
    bool isInternalRunning() const noexcept           { return internalRunning.load (std::memory_order_relaxed); }

    void setInternalPpq (double ppq) noexcept         { internalPpq = ppq; }
    double getInternalPpq() const noexcept            { return internalPpq; }

    /** True when the last processed block was driven by the host clock. */
    bool wasHostSynced() const noexcept               { return lastHostSynced; }

    /**
        Produces this block's events.

        @param host       host transport snapshot (from the play head)
        @param pattern    active pattern
        @param params     parameter snapshot
        @param numSamples current block size
        @param out        event array, sorted ascending by sampleOffset
        @param maxOut     capacity
        @param numOut     number of events written
        @param frame      per-block display/telemetry frame
    */
    void process (const HostTransport& host,
                  const PatternData& pattern,
                  const EngineParams& params,
                  int numSamples,
                  SequencedEvent* out,
                  int maxOut,
                  int& numOut,
                  SequencerFrame& frame);

private:
    double sampleRate = 48000.0;
    double internalPpq = 0.0;
    bool lastHostSynced = false;
    std::atomic<bool> internalRunning { false };
    GrooveEngine groove;
};

} // namespace klopgeest
