# KLOPGEEST architecture

```
       host transport ──┐
                        v
  ┌──────────────────────────────────────────────────────────────┐
  │ KlopgeestProcessor                                           │
  │  APVTS ──> EngineParams (POD)      PatternQueue (lock-free)  │
  │              │                          │                    │
  │              v                          v                    │
  │  ┌────────────────────────────────────────────────────────┐  │
  │  │ Engine                                                 │  │
  │  │  Sequencer ──> GrooveEngine ──> SequencedEvent[]       │  │
  │  │       │ (host ppq / internal clock)                   │  │
  │  │       v                                               │  │
  │  │  VoiceManager (8 × DrumVoice) ──> stereo bus          │  │
  │  │       v                                               │  │
  │  │  GlueMachine (comp / sat / limiter) ──> output        │  │
  │  └────────────────────────────────────────────────────────┘  │
  │              │                                              │
  │              v                                              │
  │       VisualState (atomics, 1 block)                        │
  └──────────────────────────────────────────────────────────────┘
                        │
                        v
                 Editor (src/ui)
```

## Layers and ownership

| Layer | Path | Rules |
|---|---|---|
| Contracts | `src/core/` | Frozen public types: parameter ids/ranges, `EngineParams`, `PatternData`, `VisualState`, deterministic RNG. Changing a saved-state format needs a migration. |
| Voice DSP | `src/dsp/voices/` | `DrumVoice` implementations + `VoiceManager`. Audio-thread safe, allocation-free. |
| Master DSP | `src/dsp/master/` | Glue machine; pimpl so the engine contract stays small. |
| Sequencing | `src/sequencer/` | `Sequencer`, `GrooveEngine`, `MutationEngine`. Deterministic, sample-accurate, allocation-free. |
| Engine | `src/dsp/Engine.*` | Owns voices, sequencer, master. The only place audio rendering order is decided. |
| Plug-in glue | `src/plugin/` | APVTS, host transport, pattern queue, state, programs, performance actions. |
| UI | `src/ui/` | Pure JUCE components. Reads APVTS + VisualState, edits patterns through the processor. No DSP includes. |
| Tools | `tools/` | Headless test/render/snapshot app compiling the same sources as the plug-in. |

## Thread model

**Message thread** — GUI edits, pattern mutation, factory pattern loading, state
save/load. Owns the authoritative editable `PatternData`.

**Audio thread** — `processBlock`:
1. Pulls the newest pattern snapshot from a lock-free `PatternQueue`
   (`juce::AbstractFifo`, 8 fixed slots). Pattern data is trivially copyable, so
   this is a bounded memcpy with no allocation.
2. Reads APVTS parameter atomics into a POD `EngineParams` (pointers cached once).
3. Gets the host transport from `AudioPlayHead::getPosition()` and passes it to
   the engine, together with an internal free-run flag.
4. Renders: sequencer → events → sample-accurate voice triggers → voices →
   glue machine.
5. Publishes telemetry to `VisualState` through relaxed atomics (GUI reads it;
   a dropped frame is irrelevant).

No locks, allocations, file or network access on the audio thread. The GUI never
calls into DSP objects directly.

## Sample-accurate scheduling

Ticks are absolute 16th-note indices derived from the transport ppq position.
For each block the sequencer computes every tick whose *deviated* time could fall
inside the block (look-ahead window wide enough for swing, microtiming,
humanisation and per-voice bias), asks the groove engine for its events, and
keeps only events whose sample offset is in `[0, blockSize)`. Events are sorted
and rendered in chunks split at trigger boundaries, so a hit starts on the exact
sample the groove engine chose — independent of buffer size and with no hidden
per-block state.

This also makes host seeks, loops and tempo changes trivial: a tick's event set
is a pure function of `(seed, voice, tickIndex, pattern, params)`. There is no
event history to repair.

## Determinism

- Every stochastic decision is a hash of `(seed, voice, tickIndex, salt)`.
- Correlated movement is value noise over the tick index, not a random walk —
  no hidden state, so block-size changes cannot alter the performance.
- Same seed + same pattern + same transport → identical audio.
- The GUI can display *why* a hit differed: velocity, timing deviation (ms),
  pitch deviation (st), decay scale and flags (ghost/accent/ratchet/feral/live).

## State

```
KLOPGEEST_STATE
├── version
├── patternIndex
├── KLOPGEEST          ← APVTS parameter state (flushed with apvts.copyState())
└── PATTERN            ← compact string-encoded step grid (XML-safe)
```

Pattern step data is encoded as compact strings (`on`, `lock`, `ratchet`,
`vel`, `prob`, `micro`, `pitch`) rather than variant arrays, because JUCE's XML
round-trip cannot carry arrays. Float values are stored as raw 8-hex-digit bit
patterns, so recall is bit-exact.

## Build & tools

```bash
./scripts/fetch-juce.sh          # once, into .deps/JUCE (JUCE 9.0.5)
./scripts/build.sh               # everything
./scripts/build.sh KLOPGEEST     # just the AU/VST3/Standalone bundles
./scripts/dev-build.sh klop_tool # serialised incremental dev build
./scripts/validate.sh            # full local validation
```

`klop-tool test|render|snapshot` runs the test suite, renders
`Demo/Klopgeest-Demo.wav` through the real processor, or captures an offscreen
editor PNG for the docs.

## Extending

`docs/WORKSHOP.md` walks through adding a voice, a modulation behaviour and a new
rhythmic interaction. The short version:

- **New voice**: implement `DrumVoice` in `src/dsp/voices/`, add it in
  `createVoice()`, add parameter ids + defaults in `src/core/`, add a lane name
  in `Parameters.h`. The UI picks it up from the parameter layout.
- **New modulation behaviour**: extend `GrooveEngine::generate()` or add a
  post-sequencer stage in `Engine::process()`; keep it a pure function of the
  tick index if you want determinism and block-size invariance.
- **New rhythmic interaction**: add a rule in `GrooveEngine` that inspects another
  voice's pattern (CONVERSATION-style), or extend `MutationEngine` on the message
  thread. Locked steps and lanes must remain untouched.

Keep the contracts in `src/core/` and the two DSP headers frozen unless you are
willing to update the tests, the UI and the state format together.
