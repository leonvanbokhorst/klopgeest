// -----------------------------------------------------------------------------
// Temporary groove-engine stub. Replaced by the sequencer/groove workstream.
// -----------------------------------------------------------------------------
#include "sequencer/GrooveEngine.h"

namespace klopgeest
{

void GrooveEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    reset();
}

void GrooveEngine::reset() {}

int GrooveEngine::generate (const PatternData&,
                            const EngineParams&,
                            long long,
                            double,
                            double,
                            SequencedEvent*,
                            int)
{
    return 0;
}

} // namespace klopgeest
