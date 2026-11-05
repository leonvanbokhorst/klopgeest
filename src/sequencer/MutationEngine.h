#pragma once

#include <array>
#include <cstdint>

#include "core/Pattern.h"

// -----------------------------------------------------------------------------
// Musical mutation.
//
// MUTATE does not generate new random notes: it edits the existing rhythm,
// respecting locked steps and lanes and protecting the anchors that keep the
// groove recognisable. NUDGE is the small, reversible move; MUTATE is the
// substantial variation; RECALL walks the history back.
//
// All work happens on the message thread. The result is published to the audio
// thread through the pattern queue like any other edit.
// -----------------------------------------------------------------------------
namespace klopgeest
{

class MutationEngine
{
public:
    MutationEngine();

    void reset();

    /** Small, reversible edit. Returns true if the pattern changed. */
    bool nudge (PatternData& pattern, float amount, std::uint32_t seed);

    /** Substantial variation that keeps the rhythmic identity. */
    bool mutate (PatternData& pattern, float amount, std::uint32_t seed);

    /** Restores the state captured before the most recent mutation. */
    bool recall (PatternData& pattern);

    bool canRecall() const noexcept { return historyCount > 0; }
    int  historyDepth() const noexcept { return historyCount; }

private:
    void pushHistory (const PatternData& pattern);

    static bool stepMutable (const PatternData& pattern, int voice, int step);
    static bool isAnchorStep (int voice, int step);

    static constexpr int kHistory = 12;
    std::array<PatternData, kHistory> history;
    int historyCount = 0;
};

} // namespace klopgeest
