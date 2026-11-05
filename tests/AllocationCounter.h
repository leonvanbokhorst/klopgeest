#pragma once

#include <cstddef>

// -----------------------------------------------------------------------------
// Allocation tracking for the KLOPGEEST audio path.
//
// The KLOPGEEST contract is that prepare() may allocate, but process()/trigger()/
// render() may not: no heap, no locks, no file system. This file replaces the
// global operator new/new[]/delete/delete[] so a test can prove that contract
// instead of merely believing it.
//
// Counting is opt-in and per-thread: constructing a ScopedAllocationCounter on
// the thread that runs the audio code (or inside the audio thread itself) turns
// counting on for that thread only. Allocations on the GUI/message thread are
// therefore not mistaken for audio-thread allocations, and the counter itself
// never allocates.
//
// These translation units are linked into the headless `klop_tool` only (the
// CMake target globs tests/*.cpp there, not into the plug-in), so overriding
// global new here cannot affect the shipped AU/VST3/Standalone binaries.
// -----------------------------------------------------------------------------
namespace klop_test
{

/** Absolute number of operator new calls made on this thread while counting. */
std::size_t allocationsOnThisThread() noexcept;

/** Absolute number of operator delete calls made on this thread while counting. */
std::size_t deallocationsOnThisThread() noexcept;

/** True while this thread is counting. */
bool isCountingOnThisThread() noexcept;

/** Process-wide totals of tracked allocations (all threads that were counting). */
std::size_t totalAllocations() noexcept;
std::size_t totalDeallocations() noexcept;

/** Resets the process-wide totals. Does not affect per-thread counters. */
void resetTotalCounters() noexcept;

/**
    Enables allocation counting on the current thread for the lifetime of the
    object and exposes the number of allocations/deallocations that happened in
    between.

    Nesting is supported: the inner scope re-baselines itself and the outer
    scope's counting state is restored on destruction.
*/
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter() noexcept;
    ~ScopedAllocationCounter() noexcept;

    ScopedAllocationCounter (const ScopedAllocationCounter&) = delete;
    ScopedAllocationCounter& operator= (const ScopedAllocationCounter&) = delete;

    /** operator new calls on this thread since construction (or reset()). */
    std::size_t allocations() const noexcept;

    /** operator delete calls on this thread since construction (or reset()). */
    std::size_t deallocations() const noexcept;

    /** Re-baselines the scope without changing the counting state. */
    void reset() noexcept;

private:
    std::size_t startAllocations = 0;
    std::size_t startDeallocations = 0;
    bool wasCounting = false;
};

} // namespace klop_test
