# 02 — Where the light engine plugs into KLOPGEEST

**What this is.** The integration contract: the injection point, the data available there, the ten
hooks that do not exist yet, and the traps that will silently break the feature.

Source of record: [`evidence/integration-map.md`](evidence/integration-map.md) — 937 lines, a
`file:line` reference on every claim, read directly from the tree at commit `9853e4d`. This document
is the condensed version. Where the two disagree, the evidence document wins.

Nothing here was implemented. No `src/`, `tests/`, `CMakeLists.txt`, `docs/` or `tools/` file was
modified and nothing was built.

---

## 1. The five facts that decide the architecture

**F1 — The groove is already on the event.** Swing, per-voice timing bias, per-step microtiming and
correlated humanisation are **summed** into `SequencedEvent::timingDevMs` before the event leaves the
groove engine (`src/sequencer/GrooveEngine.cpp:185-220`, `:343-344`, `:385-387`). A light engine does
not re-derive groove and must not attempt beat detection. The issue's premise — "the instrument
already knows accents, microtiming, probabilities, ratchets, per-lane loop lengths and mutations" —
is correct at the event level.

**F2 — Events live for one block, in a stack array.** `SequencedEvent events[512]` is a local in
`Engine::process` (`src/dsp/Engine.cpp:71`). They are not queued or published. Anything wanting
per-hit sample-accurate data must run **inside** `Engine::process`.

**F3 — The master bus is a non-transparent compressor + saturator + hard limiter.** `GlueMachine`
does pre-saturation, stereo-linked compression with 75 % auto make-up, post-saturation, and a
zero-look-ahead ceiling limiter that ends in `clampf(outL, ±ceiling)` (`src/dsp/master/GlueMachine.cpp:609-632`),
with the ceiling clamped to **≤ 0 dBFS** (`:355`, default −0.4 dBFS). Carriers passing through it are
gain-ridden by the drums and hard-clipped. **Post-master injection is the only defensible place.**

**F4 — Stereo only, and mono is a legal layout.** `isBusesLayoutSupported` accepts mono out
(`src/plugin/KlopgeestProcessor.cpp:198-206`); `GlueMachine` then drops the right channel
(`:491-492`, `:639-640`). A colour/L-R scheme silently loses half its information in a mono instance.

**F5 — Zero latency, no oversampling, no extra I/O.** No `setLatencySamples` anywhere; `producesMidi()`
is false; the standalone wrapper's channel count comes from the plugin's bus layout. A separate light
output is **new code**, not configuration.

---

## 2. The event, field by field

`SequencedEvent` — `src/sequencer/SequencerTypes.h:14-22`; `VoiceTrigger` — `src/dsp/voices/Voice.h:19-28`.

| Field | Type / range | What a light engine can do with it |
|---|---|---|
| `voice` | 0–7 | Which lane fired: KICK, SNARE, CLAP, CH, OH, TOM, PERC, ODD. The light "orchestration" input. |
| `sampleOffset` | samples, block-relative | Sample-accurate position. Light events are inherently in sync; no separate clock. |
| `stepIndex` | lane step index | Lane phase. With `PatternData::laneLength(voice)` this is the polymeter. |
| `tickIndex` | absolute 16th grid index | Bar/section position, deterministic hashing. |
| `timingDevMs` | ms (sum) | How far the groove bent this hit. **The source of "groove made visible".** |
| `trigger.velocity` | 0–1, accent/ghost already shaped | Intensity. Do not re-apply the accent curve. |
| `trigger.pitchSemis` | semitones | Slow colour/tint offset. |
| `trigger.decayScale` | ≈ 1.0 nominal | Light decay length. |
| `trigger.transientScale` | ≈ 1.0 nominal | Light attack length. |
| `trigger.toneOffset` | −0.5…+0.5 | Colour temperature. |
| `trigger.driveScale` | ≈ 1.0 | Saturation of the light. |
| `trigger.flags` | `HitFlag` bitmask | `ghost`, `accent`, `ratchet`, `feral`, `live`, and `mutation` — **which is never set** (§4). |
| `trigger.seed` | uint32 | Deterministic per-hit variation for the light too. |

**Not on the event, only at pattern level** (`src/core/Pattern.h`): `Step::on`, `locked`,
`probability`, `microtiming`, `pitchSemis`, `ratchet`; `Lane::length`, `muted`, `soloed`, `locked`;
`PatternData::length` (16/32/64).

**Crucially not on the event:** the *decomposition* of `timingDevMs` into swing / microtiming /
per-voice bias / humanisation noise (discarded at `GrooveEngine.cpp:218`), the resolved tempo, and
whether a hit is a mutation. See §4, hooks H2–H5.

---

## 3. The injection point

`Engine::process` (`src/dsp/Engine.cpp:58-196`) is the only place render order is decided:

| Step | Lines | What happens |
|---|---|---|
| 0 | `:68` | `output.clear()` |
| 1 | `:71-76` | sequencer → `SequencedEvent events[512]` |
| 2 | `:78-103` | live MIDI folded in as synthetic events (`HitFlag::live`) |
| 3 | `:109-122` | insertion sort by `(sampleOffset, voice)` |
| 4 | `:124-148` | fire events, per-voice telemetry, `voices.render(...)` in chunks |
| 5 | `:150-151` | `master.process(output, numSamples)` — the glue machine, in place |
| 6 | `:153-195` | meters, per-voice envelope, lane step, transport, gain reduction |

**Recommended placement [C]: after step 6, as the last thing before return.**

```cpp
// Engine::process, after the meter loop
if (lightActive)                       // atomic, relaxed read of the enabled param
    light.process (events, numEvents, output, numSamples);
```

Reasons:

- The `events` array is still in scope, so the light stage gets sample-accurate musical data with no
  new plumbing.
- Carriers bypass the glue machine entirely (F3).
- The music meters (`visual.outPeak*`, `outRms*`) keep measuring **music**, which is what the GUI
  claims they measure and what the peak assertions expect.
- The buffer that leaves the function is what is heard and what the render tool's own peak scan sees.

**The alternative** — injecting between `master.process` and the meter loop — is also workable, but
it makes the output meter read the carriers and shifts the meaning of existing peak telemetry. Do not
do it without changing the GUI labels in the same commit.

**Do not** add a new bus, a new output pair, or a send. It breaks `isBusesLayoutSupported`, the mono
path and `pluginval`.

---

## 4. Hooks that do not exist yet

Ten gaps, smallest plausible change for each. Full text:
[`evidence/integration-map.md` §10](evidence/integration-map.md).

| # | Gap | Smallest plausible change |
|---|---|---|
| **H1** | No access to the event stream outside `Engine::process`. | A `LightEngine` member of `Engine`, called once per block from `Engine::process` with the event array. Not a new thread, not a queue. |
| **H2** | `timingDevMs` is a sum; the components are discarded. | Add `float devSwing, devMicro, devBias, devNoise;` to `SequencedEvent`. Purely additive; no existing consumer changes. Only needed if a mapping wants to distinguish "the programmer moved it" from "the groove moved it". |
| **H3** | No tempo / step length on the event. | Add `float stepMs`. `EngineParams::global.tempo` is wrong under a host clock — the real BPM is `host.bpm` (`src/sequencer/Sequencer.cpp:86`). |
| **H4** | **No signal that a hit is a mutation.** `HitFlag::mutation` is declared (`src/core/VisualState.h:25`) and shown in the UI legend (`src/ui/Panels.cpp:968-969`) but **never set**; `visual.mutationCounter` is written and read by nobody. | Set the flag in the groove engine for ticks whose step differs from the pre-mutation snapshot, or pass a `mutationEpoch` counter on the event. Needed for the DRIFT mapping and for the ≥1 testable criterion in `05`. |
| **H5** | No `locked` flag and no ratchet sub-index `k` on events; the first ratchet sub-hit is unflagged (`GrooveEngine.cpp:376-377`). | Add `unsigned char ratchetIndex, ratchetCount;` and a `locked` bit to `flags`. **Required to prevent the ratchet flash-train trap (§5 T2).** |
| **H6** | No carrier/mixer stage; `Engine::reset` resets no light state; MIDI all-notes-off resets voices only (`Engine.cpp:99-102`). | Add `LightEngine::reset()` and call it from `Engine::reset` and from the all-notes-off path. |
| **H7** | No light parameters (146 exist), no `LightParams` in `EngineParams`, no light fields in `VisualState`. | New `ParamIDs` group + `LightParams` POD in `EngineParams` + `light.*` atomics in `VisualState`. See [`evidence/integration-map.md` §5.5](evidence/integration-map.md) for every file that must be touched. |
| **H8** | Panic does not reach a light engine; panic status is not published. | A `lightPanic` atomic read in `processBlock` (not the message thread), plus `std::atomic<bool> lightActive` / `lightClipping` in `VisualState`. Must work while the transport is stopped. |
| **H9** | No separate light output routing. | Do not add one. Out of scope; the plugin's stereo bus is the only output. |
| **H10** | No spectral test surface — no FFT, no golden files. | `tests/LightTests.*` on the existing `__has_include` pattern, plus a `klop-tool` subcommand that renders the light signal to its own WAV. See [`05-acceptance-and-tests.md`](05-acceptance-and-tests.md). |

---

## 5. Traps

**T1 — The master limiter is a hard wall, not a soft one.** `clampf(outL, ±ceiling)` with a ceiling
that maxes at 0 dBFS (`GlueMachine.cpp:355`, `:624-626`). Anything mixed in before it shares one
amplitude budget with the drums, is gain-ridden by them, and is clipped flat — and a clipped sine
splatters harmonics across the band. Carriers go after it, always.

**T2 — Ratchets are not one event.** A ratchet of 4 emits **four** `SequencedEvent`s spaced
`stepSamples/4` apart, with `HitFlag::ratchet` on sub-hits 2–4 and **nothing on sub-hit 1**
(`GrooveEngine.cpp:356-380`). A light engine that fires per event turns a ratchet into an immediate
flash train: at 120 BPM a 16th with ratchet 4 is 8 sub-hits per second in one lane. **The light engine
MUST aggregate ratchet sub-hits belonging to one `(voice, tickIndex, stepIndex)` into a single light
event** — one envelope, optionally with a slow internal shape — and MUST NOT emit one pulse per
sub-hit. This requires H5.

**T3 — The release gate fails on any full-scale sample.** `scripts/validate.sh:202-205` fails the
build if the demo render peaks at `>= 0.0 dBFS` or contains any sample `>= 0.9999`, and the demo
ceiling is pinned at −1.2 dB (`tools/main.cpp:54`, `:142-143`). Adding carriers to the demo render
breaks it. **The demo render and `validate.sh` keep light off**, and a separate light-enabled render
gets its own gate with its own limits.

**T4 — Mono is a silent data-loss path.** `isBusesLayoutSupported` accepts mono; `GlueMachine` drops
the right channel. Half of every L/R light decision vanishes with no error. **The light engine must
detect the output channel count and either disable itself with a visible warning or fold to a
documented mono behaviour — never silently.**

**T5 — Zero latency is a product promise.** `docs/DESIGN.md:68-75`. The light encoder must add none.
No lookahead, no buffering, no "assemble the block then emit".

**T6 — The reference carrier must survive transport stops.** If the encoder keys off
`visual.playing`, the reference disappears on stop, the Kasina drops out of SpectraStrobe mode, and
restarting costs detection time. Key the reference off "light enabled", not off "playing".

**T7 — Host sample-rate conversion destroys the carriers and you cannot prevent it.** Detect and
warn; do not claim immunity.

**T8 — Different sample rates are not the same render.** 44.1 and 48 kHz are separate encodes, not
one resampled. Test both.

**T9 — `Engine::reset` is the panic path.** `KlopgeestProcessor.cpp:158-159` and the MIDI
all-notes-off path both call it. If light state is not reset there, a panic leaves the light running.

**T10 — Dead declarations.** `Engine::publishVisual` and `HostTransport::jumped` are declared and not
used. Do not build on them.

---

## 6. Realtime rules the light engine inherits

The existing bar, and the tests that assert it, so the light engine can be held to the same standard:

| Rule | Asserted by |
|---|---|
| No allocation on the audio thread | `tests/AllocationCounter.*`; allocation-free `processBlock` test |
| No locks, no file I/O, no network | Same test suite |
| Block-size invariance (64/128/512/2048) | `tests/QaIntegrationTests.cpp:541` |
| Sample-rate sweep (44.1/48/96 kHz) | `tests/QaIntegrationTests.cpp:491` |
| Deterministic renders (same seed → same SHA-256) | `scripts/validate.sh`, `tools/main.cpp` |
| Hostile parameter fuzzing | `tests/TestHarness.h` fuzz fixtures |

The light engine adds two it must satisfy on its own: **carrier frequency and level accuracy** and
**a bounded luminance edge rate**. Both need a spectral test surface that does not exist yet (H10).

---

## 7. What a light-enabled render must not change

The strongest structural test available, and the one that decides whether light is a parallel voice
or an effect. It is **two tests, not one** — an earlier draft of this document promised a single
bit-identity gate that is impossible by construction, because the programme filter and the headroom
gain are real, declared changes.

**C1a — light OFF must be bit-identical to the old instrument.**

> Render the same pattern, seed and params with `lightEnabled = false`, against the pre-feature build.
> The buffers must be identical, sample for sample.

Achievable by construction: the light stage returns before touching a sample.

**C1b — light ON must not change the music.**

> Render with light on. Assert (i) every `SequencedEvent` (voice, sampleOffset, tickIndex, velocity,
> flags) matches the light-off render exactly, and (ii) with the declared headroom gain removed and
> the declared filter inverse-applied, the audible band matches to ≤ −80 dBFS.

**Do not promise bit-identity here.** Two changes are permitted and both are real: the programme
anti-alias/notch stage ([`01`](01-signal-spec.md) §6), which shifts phase even where it leaves
magnitude flat, and the headroom reservation ([`01`](01-signal-spec.md) §5), which at the product cap
is a **0.9 dB** attenuation. Both must be declared, measurable and reported in `VisualState`.

If the four-notch approach works, the audible *magnitude* response stays flat and C1b is as strong as
it can be. The phase change remains, and pretending otherwise would just make the test wrong.
