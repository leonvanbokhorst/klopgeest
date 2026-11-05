#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <cstring>
#include <type_traits>

#include "core/Constants.h"

// -----------------------------------------------------------------------------
// Pattern data: the musical score.
//
// This is deliberately a small, trivially-copyable value type: it can be
// memcpy'd to the audio thread without allocating, and it serialises to a
// juce::ValueTree for session recall.
// -----------------------------------------------------------------------------
namespace klopgeest
{

struct Step
{
    bool  on          = false;
    bool  locked      = false;  // immune to humanisation and mutation
    float velocity    = 0.80f;  // 0..1 (accent)
    float probability = 1.00f;  // 0..1 chance the step fires
    float microtiming = 0.00f;  // -0.5..+0.5 of a step
    float pitchSemis  = 0.00f;  // -12..+12 semitones
    int   ratchet     = 1;      // 1..4 sub-hits per step
};

struct Lane
{
    int   length = kDefaultSteps;  // 1..kMaxSteps, may differ per lane
    bool  muted  = false;
    bool  soloed = false;
    bool  locked = false;          // ignore humanisation for this voice
    std::array<Step, kMaxSteps> steps {};
};

struct PatternData
{
    int length = kDefaultSteps;     // pattern grid length: 16, 32 or 64
    std::array<Lane, kNumVoices> lanes {};
    std::array<char, 32> name {};   // fixed buffer: trivially copyable

    PatternData() = default;

    void setName (const juce::String& newName) noexcept;
    juce::String getName() const;

    void sanitise() noexcept;

    Step&       step (int voice, int index) noexcept;
    const Step& step (int voice, int index) const noexcept;

    int  laneLength (int voice) const noexcept;
    bool anySoloed() const noexcept;
    bool laneAudible (int voice) const noexcept;

    bool operator== (const PatternData&) const noexcept;
    bool operator!= (const PatternData& other) const noexcept { return ! (*this == other); }

    juce::ValueTree toValueTree() const;
    static PatternData fromValueTree (const juce::ValueTree&);
};

static_assert (std::is_trivially_copyable_v<PatternData>, "PatternData must stay audio-thread copyable");

// -----------------------------------------------------------------------------
// Message-thread -> audio-thread pattern handoff.
//
// A bounded lock-free FIFO. The GUI pushes edited snapshots; the audio thread
// drains the queue once per block and keeps the newest one. Nothing on the
// audio thread allocates, locks or waits.
// -----------------------------------------------------------------------------
class PatternQueue
{
public:
    PatternQueue() : fifo (kSlots) {}

    /** Message thread. Returns false if the queue is momentarily full; the GUI
        simply retries on the next edit (the newest snapshot wins anyway). */
    bool push (const PatternData& pattern) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 <= 0)
            return false;
        slots[(size_t) start1] = pattern;
        fifo.finishedWrite (1);
        return true;
    }

    /** Audio thread. Drains all ready snapshots and returns the newest. */
    bool pullLatest (PatternData& destination) noexcept
    {
        const auto ready = fifo.getNumReady();
        if (ready <= 0)
            return false;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead (ready, start1, size1, start2, size2);

        if (size1 > 0)
            destination = slots[(size_t) (start1 + size1 - 1)];
        if (size2 > 0)
            destination = slots[(size_t) (start2 + size2 - 1)];

        fifo.finishedRead (ready);
        return true;
    }

private:
    static constexpr int kSlots = 8;
    juce::AbstractFifo fifo;
    PatternData slots[kSlots];
};

} // namespace klopgeest
