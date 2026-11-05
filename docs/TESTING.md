# Testing KLOPGEEST

This document describes what is tested, how to run it, and what the tests are
honest about not covering yet. It is written for the workstreams and for the
workshop participants who will extend the instrument.

KLOPGEEST has no GUI test framework, no mocking layer and no test-only branches
in the shipping code. All tests run headlessly through the same `klop_tool`
binary that compiles the exact sources of the AU / VST3 / Standalone plug-in
(`CMakeLists.txt`: `KLOPGEEST_SOURCES` is shared, and `KLOPGEEST_TESTS` is added
only to the headless tool).

## Quick start

```sh
# Build only the headless test/render/snapshot tool (serialised shared build)
./scripts/dev-build.sh klop_tool

# Run every suite
./build/klop_tool_artefacts/Release/klop_tool test
```

A run ends with:

```
N checks, M failures, K skipped
```

`K` is not a failure. Skips are used for behaviour that cannot be asserted yet
(for example "different seed produces different audio" while the sequencer is
still a stub). A skip prints its reason, and `scripts/validate.sh` treats any
failure as a failed gate.

## The test map

| File | Owner | What it covers |
| --- | --- | --- |
| `tests/TestMain.cpp` | QA | Fast smoke: parameter layout, factory patterns, pattern ValueTree round-trip, state round-trip, deterministic render, basic audio-thread safety, nudge/mutate/recall. Also the single `runAllTests()` entry point and the `#if __has_include` wiring for the other suites. |
| `tests/QaIntegrationTests.*` | QA | Cross-cutting contracts: zero-allocation audio path, sample-rate sweep, block-size invariance, internal + host transport, deterministic seeds, hostile parameter/pattern fuzz, pattern-publish thread safety. |
| `tests/SequencerTests.*` | sequencer/groove workstream | Transport clock, host sync, groove determinism, swing/humanize behaviour, event scheduling. |
| `tests/VoiceTests.*` | voice workstream | The eight procedural voices: trigger/render contract, decay/choke behaviour, parameter response, determinism. |
| `tests/MasterTests.*` | master workstream | Glue machine: compression curve, knee/ratio behaviour, saturation, limiter/ceiling, metering. |
| `tests/AllocationCounter.*` | QA | Global `operator new`/`delete` replacement used by the realtime-safety checks. Not a test by itself. |

All `tests/*.cpp` files are picked up by the CMake glob, so a new suite only
needs its `.h` declaring `void runXxxTests();` and its `.cpp` defining it, plus
the guarded call in `tests/TestMain.cpp`:

```cpp
#if __has_include("tests/VoiceTests.h")
 #include "tests/VoiceTests.h"
 #define KLOPGEEST_HAS_VOICE_TESTS 1
#endif
...
#if defined(KLOPGEEST_HAS_VOICE_TESTS)
    runVoiceTests();
#endif
```

The guards keep the build green while files are being written in parallel. If a
suite header exists, it is linked and run; if not, the runner prints a `SKIP`
line naming the missing suite. Nothing is silently skipped.

## Harness API (`tests/TestHarness.h`)

```cpp
klop_test::check (condition, "what was checked");
klop_test::checkNear (actual, expected, tolerance, "what was checked");
klop_test::checkInRange (value, lo, hi, "what was checked");
klop_test::info ("diagnostic message");
klop_test::skip ("why this cannot run yet");
```

There is no exception handling and no test registration: a suite is a function
that calls `check()`. The counters live in the harness header and the final
summary is printed once in `runAllTests()`. Never return early on a failed check
unless the failure would crash the process; collect as much information as
possible in one run.

## Realtime safety: the allocation counter

The engine contract (`src/dsp/Engine.h`, `src/dsp/voices/Voice.h`) says that
after `prepare()` the audio path never allocates, locks or blocks. Tests prove it
with `tests/AllocationCounter.*`, which replaces the global
`operator new`/`new[]`/`delete`/`delete[]` (including aligned variants) for the
`klop_tool` executable only:

```cpp
klop_test::ScopedAllocationCounter counter;
for (int block = 0; block < 200; ++block)
    processor.processBlock (buffer, midi);
check (counter.allocations() == 0, "...");
```

Counting is **per-thread** and opt-in: `ScopedAllocationCounter` sets a
thread-local flag, so a test can also instrument a real audio thread without
counting the message thread's pattern editing. Allocations are forwarded to
`malloc`/`free` so memory allocated before counting still behaves normally.

Checked paths:

* `KlopgeestProcessor::processBlock` — 200 blocks with playing transport and
  pattern publishes/panic in between.
* `Engine::process` — 200 blocks with a parameter snapshot, pattern and empty
  host transport.
* `GlueMachine::process` — 100 blocks of stereo signal.
* Every voice's `trigger()` + `render()` — 50 hits each, all eight voices.
* A real audio thread processing while the main thread publishes 500 patterns.

The allocation counter is not compiled into the AU/VST3/Standalone targets, so
it cannot affect validation results or shipped behaviour.

**Known risk already measurable:** once the real sequencer emits more than one
event per block, `std::stable_sort` in `Engine::process` may allocate a
temporary buffer. That is an audio-thread allocation and the test above will
report it. It should be replaced with an allocation-free in-place sort before
the engine is considered releasable.

## What is asserted, and why

### Sample-rate sweep (44.1 / 48 / 96 kHz)

For each rate: `prepareToPlay`, engine reports the rate, 120 blocks render
finite, non-runaway output; state saves and recalls (parameters + pattern) at
that rate; the recalled instance renders finite audio. Catches filter
coefficients, envelopes and delays that assume 48 kHz.

### Block-size invariance (64 / 128 / 512 / 2048)

The same seed, pattern and parameters are rendered for 409 600 samples at each
block size (a multiple of 2048 so every run ends on a block boundary). The groove
engine's stochastic decisions are a pure function of `(seed, voice, tick, salt)`,
so host buffer size must not change what is heard. Channels are compared against
the 64-sample reference with an **absolute tolerance of `1e-4`** per sample; the
tolerance covers float rounding in the voice/master render paths, not musical
differences (which are orders of magnitude larger). The test is skipped while
the sequencer is a stub and switches on automatically.

### Transport

* Internal transport stopped: `visual.playing == false`, engine reports stopped,
  no notes ringing → silence. Started: `visual.playing == true`, engine running,
  free-run BPM follows the Tempo parameter, tempo changes apply on the next
  block.
* Host simulation (`FakePlayHead` in `tests/QaIntegrationTests.cpp`): host
  playing with 90 BPM → `hostSynced`, `bpm == 90`; host stop → falls back to the
  internal tempo parameter; host tempo change followed; seek/jump renders
  finite and stays host-synced.
* Once the sequencer is real, "running transport produces audio" is asserted
  instead of skipped; detailed tick placement is covered by
  `tests/SequencerTests.*`.

### Determinism

Same seed → bit-identical audio (checked today, also while stubs are silent).
Different seed → different audio (skipped until the groove engine lands, then
checked with high humanize/swing/FERAL settings so the seed has something to
change).

### Hostile parameter / pattern fuzz

64 configurations. Every parameter is pushed to an arbitrary normalised position
via `setValueNotifyingHost` (extremes included), and a hostile pattern is
published whose steps deliberately exceed every documented range (velocity
−0.5…1.5, probability 0…1.5, microtiming ±1.5 steps, pitch ±20 semitones,
ratchet 0…8, lane lengths beyond the pattern). For each configuration: 8 blocks
render finite, state saves, restores into a fresh processor, all parameter
values match within `1e-4`, and the pattern is identical.

### Pattern publishing thread safety

A real `std::thread` renders blocks (with its own `ScopedAllocationCounter`)
while the main thread publishes 500 factory patterns through the lock-free
`PatternQueue`. Assertions: output stays finite, the audio thread keeps
processing (≥ 100 blocks, 5 s cap), and the audio thread performs zero
allocations while patterns are published. The test is GUI-free and uses the same
publish path as the editor.

## Skips in this phase

At the time of writing the following are deliberately skipped, and will switch
on when the owning workstream lands:

* block-size invariance and "different seed changes the audio" (need the real
  `Sequencer`/`GrooveEngine`; currently every sequenced render is silent);
* "running internal transport produces audio" (same reason).

`klop_tool render` therefore currently produces a silent WAV: that is expected
while the sequencer stub is in place and must **not** be reported as a passing
demo render. `scripts/validate.sh` fails a silent render unless explicitly
allowed with `--allow-silent-render`.

## Debugging

```sh
# Full suite (verbose on failure)
./build/klop_tool_artefacts/Release/klop_tool test

# Render the 48 s demo to inspect audio
./build/klop_tool_artefacts/Release/klop_tool render Demo/Klopgeest-Demo.wav

# Offscreen editor snapshot (1180 x 680 PNG)
./build/klop_tool_artefacts/Release/klop_tool snapshot docs/ui-preview.png
```

The state test prints `DIAG` lines describing the first divergent pattern
fields; use those before adding new assertions. Prefer adding a check to an
existing suite over creating a new runner path.

## Rules for extending the tests

1. Never modify `tests/VoiceTests.*`, `tests/SequencerTests.*` or
   `tests/MasterTests.*` unless you own that file; add cross-cutting tests to
   `tests/QaIntegrationTests.cpp` (QA-owned) or ask QA.
2. No recording, no golden files, no host/DAW dependency. Everything must run in
   a console on a clean machine.
3. Assert contracts, not implementation details: "output is finite", "zero
   allocations", "same seed is reproducible" are stable; internal coefficient
   values are not.
4. If an assertion cannot run yet, `skip()` it with the reason. Never soften it
   into a passing `check(true, ...)`.
5. Tests must not leave files behind outside `build/`, `Demo/` and
   `docs/ui-preview.png`.
