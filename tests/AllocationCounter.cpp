// -----------------------------------------------------------------------------
// Global operator new/delete replacement used by the headless test tool.
//
// See tests/AllocationCounter.h for the contract. Everything here forwards to
// malloc/free; the only extra work is a thread-local flag test and a counter
// increment when a ScopedAllocationCounter is active on the current thread.
// -----------------------------------------------------------------------------
#include "tests/AllocationCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{

// Per-thread counting state. Deliberately plain (no atomics, no allocation) so
// that the audio-thread path never touches shared memory to check the flag.
thread_local bool tCounting = false;
thread_local std::size_t tNewCount = 0;
thread_local std::size_t tDeleteCount = 0;

// Process-wide totals, for tests/tools that want a bigger picture.
std::atomic<std::size_t> gTotalNew { 0 };
std::atomic<std::size_t> gTotalDelete { 0 };

inline void trackAllocation() noexcept
{
    if (tCounting)
    {
        ++tNewCount;
        gTotalNew.fetch_add (1, std::memory_order_relaxed);
    }
}

inline void trackDeallocation() noexcept
{
    if (tCounting)
    {
        ++tDeleteCount;
        gTotalDelete.fetch_add (1, std::memory_order_relaxed);
    }
}

} // namespace

namespace klop_test
{

std::size_t allocationsOnThisThread() noexcept   { return tNewCount; }
std::size_t deallocationsOnThisThread() noexcept { return tDeleteCount; }
bool isCountingOnThisThread() noexcept           { return tCounting; }

std::size_t totalAllocations() noexcept   { return gTotalNew.load (std::memory_order_relaxed); }
std::size_t totalDeallocations() noexcept { return gTotalDelete.load (std::memory_order_relaxed); }

void resetTotalCounters() noexcept
{
    gTotalNew.store (0, std::memory_order_relaxed);
    gTotalDelete.store (0, std::memory_order_relaxed);
}

ScopedAllocationCounter::ScopedAllocationCounter() noexcept
    : startAllocations (tNewCount),
      startDeallocations (tDeleteCount),
      wasCounting (tCounting)
{
    tCounting = true;
}

ScopedAllocationCounter::~ScopedAllocationCounter() noexcept
{
    tCounting = wasCounting;
}

std::size_t ScopedAllocationCounter::allocations() const noexcept
{
    return tNewCount - startAllocations;
}

std::size_t ScopedAllocationCounter::deallocations() const noexcept
{
    return tDeleteCount - startDeallocations;
}

void ScopedAllocationCounter::reset() noexcept
{
    startAllocations = tNewCount;
    startDeallocations = tDeleteCount;
}

} // namespace klop_test

// -----------------------------------------------------------------------------
// Global allocation functions. All of them forward to malloc/free so that memory
// allocated before counting was enabled (or on another thread) stays valid.
// -----------------------------------------------------------------------------
namespace
{

inline void* allocateBytes (std::size_t size)
{
    return std::malloc (size == 0 ? 1 : size);
}

inline void* allocateAlignedBytes (std::size_t size, std::size_t alignment)
{
    // posix_memalign returns memory that plain free() can release.
    void* pointer = nullptr;
    if (alignment < sizeof (void*) || (alignment & (alignment - 1)) != 0)
        return allocateBytes (size);

    if (posix_memalign (&pointer, alignment, size == 0 ? 1 : size) != 0)
        return nullptr;

    return pointer;
}

} // namespace

void* operator new (std::size_t size)
{
    trackAllocation();
    if (void* pointer = allocateBytes (size))
        return pointer;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    trackAllocation();
    if (void* pointer = allocateBytes (size))
        return pointer;
    throw std::bad_alloc();
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    trackAllocation();
    return allocateBytes (size);
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    trackAllocation();
    return allocateBytes (size);
}

void operator delete (void* pointer) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete[] (void* pointer) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete (void* pointer, std::size_t) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete[] (void* pointer, std::size_t) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete (void* pointer, const std::nothrow_t&) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete[] (void* pointer, const std::nothrow_t&) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

// --- Aligned variants (C++17) ------------------------------------------------
void* operator new (std::size_t size, std::align_val_t alignment)
{
    trackAllocation();
    if (void* pointer = allocateAlignedBytes (size, (std::size_t) alignment))
        return pointer;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    trackAllocation();
    if (void* pointer = allocateAlignedBytes (size, (std::size_t) alignment))
        return pointer;
    throw std::bad_alloc();
}

void* operator new (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    trackAllocation();
    return allocateAlignedBytes (size, (std::size_t) alignment);
}

void* operator new[] (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    trackAllocation();
    return allocateAlignedBytes (size, (std::size_t) alignment);
}

void operator delete (void* pointer, std::align_val_t) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete[] (void* pointer, std::align_val_t) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete (void* pointer, std::size_t, std::align_val_t) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete[] (void* pointer, std::size_t, std::align_val_t) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete (void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    trackDeallocation();
    std::free (pointer);
}

void operator delete[] (void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    trackDeallocation();
    std::free (pointer);
}
