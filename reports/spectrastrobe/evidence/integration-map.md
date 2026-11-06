> **Provenance.** One of five evidence reports produced for issue #6 on 2026-11-06 by a lead researcher working with three parallel sub-investigations. Reproduced here at full length and essentially verbatim — it is the source of record for the summaries in `../01`–`../07`, and where a summary and this document disagree, this document wins.
>

# KLOPGEEST → SpectraStrobe: integration map for an optional rhythmic light engine

**Status:** research map for builders. No production code was written, no `src/`, `tests/`,
`CMakeLists.txt`, `docs/` or `tools/` file was touched, nothing was built.
**Tree:** `research/…` worktree at commit `9853e4d` ("M1 release candidate"), 11,535 lines of
C++ under `src/`.
**Method:** direct reading of the sources named in every claim. Everything below with a
`file:line` reference is *established by reading that code*. Anything about how the code
*sounds*, how a third-party light protocol behaves, or what a downstream device does with the
audio is labelled **inference** and is not established here.

Status vocabulary used throughout: **established** (read in the code), **inference**
(reasoning from the code, not measured), **not established** (outside this map's evidence),
**not knowable with these methods** (needs hardware, a host session, or a spec that was not in
this repository).

---

## 0. The five things that decide the design

1. **Every musical decision is already materialised as a value on the event.** Swing, microtiming,
   per-voice bias and correlated humanisation are *summed* into `SequencedEvent::timingDevMs`
   before the event leaves the groove engine (`src/sequencer/GrooveEngine.cpp:185-220`,
   `:343-344`, `:385-387`). A light engine does **not** need to re-derive groove, and it does
   **not** need beat detection: it can read the deviation directly. This is the single most
   important fact in this document.
2. **Events live for one block only, in a stack array inside `Engine::process`.** They are not
   queued, not published, and not reachable from the plugin glue. Anything that wants per-hit
   data at sample accuracy must run **inside** `Engine::process`, between the sequencer call
   (`src/dsp/Engine.cpp:75-76`) and/or after `master.process` (`src/dsp/Engine.cpp:151`).
3. **The master bus is a deliberately non-transparent compressor + saturator + hard ceiling
   limiter** (`src/dsp/master/GlueMachine.cpp:477-656`). Carriers that pass *through* it are
   compressed, saturated, gain-ridden by the 75 % auto make-up, and hard-clamped at a ceiling
   that maxes out at 0 dBFS (`:355`, `:609-632`). Post-master injection is therefore the only
   defensible place for a carrier (`src/dsp/Engine.cpp:151` vs `:153`).
4. **The output bus is stereo-only, and mono is a legal layout** (`src/plugin/KlopgeestProcessor.cpp:198-206`).
   SpectraStrobe-style schemes typically use two independent carrier channels; in a mono
   instance the right channel is *dropped* (`src/dsp/master/GlueMachine.cpp:491-492`, `:639-640`).
5. **Zero latency, no oversampling, no host-visible extra I/O.** No `setLatencySamples` call
   exists anywhere in `src/` (grep: none), `producesMidi()` is `false`
   (`src/plugin/KlopgeestProcessor.h:43`), and the standalone wrapper derives its channel count
   from the plugin's bus layout (`.deps/JUCE/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h:159-166`),
   i.e. 2. A separate light output device or a second output pair is **new code**, not
   configuration.

---

## 1. EVENT MODEL

### 1.1 The two structs, field by field

`SequencedEvent` — `src/sequencer/SequencerTypes.h:14-22`:

```cpp
struct SequencedEvent
{
    int voice        = 0;
    int sampleOffset = 0;      // relative to the start of the current block
    VoiceTrigger trigger;
    int stepIndex    = 0;      // lane step that produced this hit
    long long tickIndex = 0;   // absolute 16th-note grid index
    float timingDevMs = 0.0f;  // total timing deviation applied (telemetry)
};
```

`VoiceTrigger` — `src/dsp/voices/Voice.h:20-30`:

```cpp
struct VoiceTrigger
{
    float velocity       = 1.0f;   // 0..1, already accent/ghost shaped
    float pitchSemis     = 0.0f;   // deviation added to the voice's tuning
    float decayScale     = 1.0f;   // 1 = nominal
    float transientScale = 1.0f;   // 1 = nominal
    float toneOffset     = 0.0f;   // -0.5..+0.5 (dark <-> bright)
    float driveScale     = 1.0f;   // 1 = nominal
    std::uint32_t seed   = 0;      // deterministic per-hit variation
    unsigned int flags   = 0;      // HitFlag bitmask (for telemetry only)
};
```

| Field | Range / unit reached at event time | Meaning for a light engine | Established at |
|---|---|---|---|
| `voice` | `0..7`, enum in `src/core/Constants.h:27-37` (KICK, SNARE, CLAP, CH, OH, TOM, PERC, ODD) | lane → colour / LED group | `GrooveEngine.cpp:144,385` |
| `sampleOffset` | `[0, blockSize)` after filtering; may be **negative or ≥ blockSize** inside `GrooveEngine::generate` | sample-accurate hit time within the block | `Sequencer.cpp:139-147` |
| `trigger.velocity` | clamped `0.02 .. 1.0`; programmed × accent shaping × humanised level, then ratchet drop ± jitter | brightness / intensity | `GrooveEngine.cpp:225-243`, `:359-373` |
| `trigger.pitchSemis` | clamped `-14 .. +14` (programmed ±12 plus stochastic) | hue or “height” | `GrooveEngine.cpp:246-256` |
| `trigger.decayScale` | clamped `0.5 .. 1.8` | decay of the light flash | `GrooveEngine.cpp:258-267` |
| `trigger.transientScale` | clamped `0.45 .. 1.9` | click/attack sharpness | `GrooveEngine.cpp:269-278` |
| `trigger.toneOffset` | clamped `-0.4 .. 0.4` | warm/cool | `GrooveEngine.cpp:280-291` |
| `trigger.driveScale` | clamped `0.6 .. 1.6` | saturation → “heat” | `GrooveEngine.cpp:293-304` |
| `trigger.seed` | SplitMix32 mix of `(seed, voice, tick, saltHitSeed + k*17)` | per-hit determinism / dither | `GrooveEngine.cpp:383`, `src/core/Random.h:28-35` |
| `trigger.flags` | see 1.3 | ghost / accent / ratchet / feral / live | `GrooveEngine.cpp:375-383` |
| `stepIndex` | `positiveMod(tickIndex, laneLength)` | step within the **lane's** loop, not the pattern | `GrooveEngine.cpp:151` |
| `tickIndex` | absolute 16th index (`long long`) | global musical clock; survives seeks/loops | `SequencerTypes.h:20` |
| `timingDevMs` | sum of swing + micro + bias + humanisation, in **ms** (can be negative) | the “lean” of the hit — the core light gesture | `GrooveEngine.cpp:185-220` |

Note the units mismatch a builder must respect: `timingDevMs` is **milliseconds**, while
`microtiming`, swing and ratchet spacing are **step fractions**. Conversion at event time:
`stepSamples` is known inside `GrooveEngine::generate` but not stored on the event
(`GrooveEngine.cpp:123`, `:344`). To express a deviation as a *fraction of a 16th*, a light
engine must recover `stepSamples` from `sampleRate` and the block's BPM — `frame.bpm` is
published (`Sequencer.cpp:87`) but the value is not on the event (**gap, see §10**).

### 1.2 Where events are produced

Three-stage pipeline, all inside one block:

1. **`Sequencer::process`** — `src/sequencer/Sequencer.cpp:64-193`.
   Derives an absolute tick window wide enough for every possible deviation
   (`:117-128`, bounds from `src/sequencer/GrooveConstants.h:79-93`), calls the groove engine
   per tick (`:136-137`), keeps only events with `0 <= sampleOffset < numSamples`
   (`:139-147`), then insertion-sorts by `(sampleOffset, tickIndex, voice)`
   (`:153-168`). Allocation-free by construction (the same sort used to be
   `std::stable_sort` and was caught by the allocation counter — see
   `docs/TESTING.md:126-130`).
2. **`GrooveEngine::generate`** — `src/sequencer/GrooveEngine.cpp:107-439`. Pure function of
   `(seed, voice, tickIndex, pattern, params)`; writes events through `writeEvent`
   (`:306-327`). Two branches only: a programmed hit (`:330-390`) and a ghost note on an empty
   step (`:392-435`). A muted/soloed-out lane is skipped before anything is computed
   (`:146-147`).
3. **`Engine::process`** — `src/dsp/Engine.cpp:58-196`. Owns the event array
   (`:71`, capacity `Engine::kMaxEventsPerBlock = 512`, `src/dsp/Engine.h:26`), merges live MIDI
   note-ons as synthetic events (`:78-103`), re-sorts (`:109-122`), then splits the render into
   chunks at trigger boundaries (`:124-148`).

**Event capacity is finite and truncation is silent.** `Sequencer::process` stops generating at
`numOut == maxOut` (`Sequencer.cpp:133`, `:139`) and `Engine` stops adding MIDI events at 512
(`Engine.cpp:86`). A light engine that appends to its own buffer must carry its own bound; it
must never assume it saw every event in a pathological block.

### 1.3 Flags — what is and is not signalled

`HitFlag` — `src/core/VisualState.h:18-27`:

```cpp
enum class HitFlag : unsigned int
{
    none      = 0,
    ghost     = 1u << 0,
    accent    = 1u << 1,
    ratchet   = 1u << 2,
    feral     = 1u << 3,
    mutation  = 1u << 4,
    live      = 1u << 5
};
```

Actually **set** on events (established by grep over `src/`):

| Bit | Set where | Condition |
|---|---|---|
| `ratchet` | `GrooveEngine.cpp:376-377` | sub-hit index `k > 0` (the *first* sub-hit of a ratchet carries no flag) |
| `accent` | `GrooveEngine.cpp:378-379` | `subVelocity >= kAccentFlagThreshold` (0.75, `GrooveConstants.h:51`) |
| `feral` | `GrooveEngine.cpp:380-381`, `:429-430` | this tick drew a FERAL spike (`:159-165`) |
| `ghost` | `GrooveEngine.cpp:428` | event came from the ghost branch |
| `live` | `Engine.cpp:94` | live MIDI note-on, not sequenced |
| `mutation` | **never set** — only referenced in the UI legend (`src/ui/Panels.cpp:968-969`) | — |

So: ghost/accent/ratchet/feral/live are usable today; **“this step is a mutation” is not
available at event time** (see §4 and §10).

Missing from the flag set that a light engine will probably want: **locked** (step or lane) and
**the ratchet sub-index `k`** (you know a hit is a ratchet but not which of the four).

### 1.4 What exists only at pattern level (not on the event)

Recoverable only by looking at `PatternData` + `GlobalParams` for the *current* tick, and only
inside the audio thread where the pattern is in scope:

| Quantity | Where it lives | Carried on event? |
|---|---|---|
| `Step::probability` (the *value*, not the outcome) | `src/core/Pattern.h:26` | no — only the fired/not-fired fact |
| `Step::locked`, `Lane::locked` | `src/core/Pattern.h:24`, `:37` | no |
| `Step::microtiming` (programmed part only) | `src/core/Pattern.h:27` | only inside the `timingDevMs` sum |
| `Step::ratchet` (1..4) | `src/core/Pattern.h:29` | no — sub-hits arrive as separate events |
| `Lane::length` (polymeter) | `src/core/Pattern.h:34` | indirectly via `stepIndex` |
| `PatternData::length` (16/32/64) | `src/core/Pattern.h:43` | no |
| `Lane::muted/soloed` | `src/core/Pattern.h:35-36` | no — muted lanes produce nothing at all |
| `GlobalParams::swing/humanize/stability/personality/ghost/accent/seed` | `src/core/EngineParams.h:44-56` | no — but readable from `Engine::getParams()` (`src/dsp/Engine.h:46`) |
| `VoiceParams::timingBiasMs`, `rand*` depths | `src/core/EngineParams.h:35-41` | no |
| `kVoiceRoleLoose[voice]` (anchor weight) | `src/core/Constants.h:41-51` | no |

Because `Engine` holds `params` (`src/dsp/Engine.h:54`) and `Engine::getParams()` is public, a
light stage inside `Engine::process` can read all of the above for the block. It just must not
expect it *per event*.

---

## 2. GROOVE INTELLIGENCE — computed, and what is thrown away

### 2.1 The input scalars, computed once per tick block (`GrooveEngine.cpp:125-140`)

```cpp
const float humanize = amount (global.humanize);
const float stability = amount (global.stability);
// stability = 1 -> exactly zero stochastic deviation (machine).
const float humanFactor = humanize * (1.0f - stability);
const float personality = personalityScale (global.personality);
const float swing = std::clamp (global.swing, 0.0f, 0.75f);
const float ghostAmount = amount (global.ghost);
const float accent = amount (global.accent);
const bool feralPersonality = global.personality == (int) Personality::feral;
```

`personalityScale` maps TIGHT/LOOSE/FERAL to `0.35 / 1.00 / 1.70`
(`GrooveEngine.cpp:74-83`, `GrooveConstants.h:28-30`). `humanFactor = humanize · (1 − stability)`
is the master gain of *all* stochastic deviation.

### 2.2 Per dimension, per voice

| Mechanism | Computed at | Reaches the event as | Thrown away |
|---|---|---|---|
| Swing (odd 16ths late) | `GrooveEngine.cpp:189-191` (`devMs += swing * stepMs`) | folded into `timingDevMs` | the swing-only component |
| Step microtiming | `:193-194` | folded into `timingDevMs` | the programmed-only component |
| Per-voice timing bias (`timingBiasMs`, ±25 ms) | `:196-197` | folded into `timingDevMs` | the bias-only component |
| Correlated timing drift (value noise) | `:202-218` | folded into `timingDevMs` | the noise-only component, the noise *period*, the clamp actually applied |
| FERAL spike selection | `:159-165` (`kFeralSpikeProbability 0.05`, multiplier 2.2) | `HitFlag::feral` | the `spikeScale` value |
| Accent shaping | `:229-236` (`kAccentGridBoost 0.22`, `kAccentOffbeatTrim 0.06`) | inside `velocity` | which part was accent vs. programmed |
| Humanised level drift | `:237-240` | inside `velocity` | drift component |
| Correlated pitch / decay / transient / tone / drive drift | `:246-304` | `pitchSemis`, `decayScale`, `transientScale`, `toneOffset`, `driveScale` | the *nominal* value each deviation was added to |
| Ghost probability | `:400-411` | event exists or not | the probability value |
| Ratchet drop + jitter | `:359-373` | inside `velocity`, `HitFlag::ratchet` | sub-index `k` |
| Locked (step or lane) | `:156`, `:199-200`, `:225-227`, `:248-250`, `:394` | *behaviour only*: no stochastic part, always fires | the lock state itself |

**The single most valuable discard for a light engine** is the decomposition of
`timingDevMs`: swing, microtiming, bias and humanisation are each musically distinct and the
light could treat them differently (swing = a lean on every other pulse; humanisation = a
breathing drift; bias = a fixed offset). Today only the **sum** survives
(`GrooveEngine.cpp:218`, `:343`, `:385`). Splitting it is a local change (§10, gap H2).

Second most valuable: **`stepSamples` / BPM is not on the event**, so `timingDevMs` cannot be
turned into “fraction of a 16th” downstream without re-deriving tempo. `frame.bpm` is published
per block (`Sequencer.cpp:87`) but not attached to events.

### 2.3 Anchor weighting (already applied, and worth exposing)

`kVoiceRoleLoose[]` (`src/core/Constants.h:41-51`) is 0.35 (kick) … 1.20 (odd). It scales every
stochastic dimension and also selects the clamp: anchors get
`min(20 ms, 0.18 step)`, colour voices `min(45 ms, 0.35 step)`
(`GrooveEngine.cpp:209-216`, `GrooveConstants.h:36-40`). A light engine gets the *result* but
not the weight, so it cannot tell “this hit is an anchor and therefore should stay steady in the
light too” from the event alone.

### 2.4 Value noise — the shape a light engine should mirror

`rng::smoothBipolar` (`src/core/Random.h:61-71`) samples SplitMix32 hashes at
`floor(tick/period)` and the next point, smoothstep-interpolates. Periods are a constant
function of `(seed, voice, dimension)` — `GrooveEngine.cpp:169-182`, constants in
`GrooveConstants.h:64-73` (timing 4–12 ticks, level 2–6, pitch 6–16, decay 8–20). No hidden
state ⇒ block-size invariance.

**Inference (not measured):** a light engine that wants its brightness to “breathe like the
hi-hats do” can reuse `rng::smoothBipolar` with the *same* period and a new salt, and will be
correlated with the audio by construction, at zero extra state. This is the cheapest way to
make light feel like it belongs to this instrument rather than to a generic VU meter.

### 2.5 Determinism guarantee that constrains any light design

`docs/ARCHITECTURE.md:73-84` and `docs/DESIGN.md:29-44`: a tick's event set is a pure function
of `(seed, voice, tickIndex, pattern, params)`. The test `testGeneratePurity`
(`tests/SequencerTests.cpp:809-837`) asserts exactly this. Any light engine that accumulates
per-block state (an LFO phase advanced by `numSamples`, a per-block smoothing coefficient, a
“last hit” timestamp) risks breaking the block-size invariance test (§8) unless the state
advances per sample and is reset by the same rules as the audio.

---

## 3. PATTERN DATA

`src/core/Pattern.h:21-46`:

```cpp
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
```

- `kNumVoices = 8`, `kMaxSteps = 64`, `kMinSteps = 16`, `kStepsPerBar = 16`
  (`src/core/Constants.h:18-23`). Sizes are fixed; `Step`/`Lane`/`PatternData` are trivially
  copyable and `static_assert`ed as such (`Pattern.h:68`).
- **Polymeter**: `lane.length` is independent per lane and clamped to `1..pattern.length` by
  `sanitise()` (`src/core/Pattern.cpp:18-37`). The groove engine derives
  `stepIndex = positiveMod(tickIndex, laneLength)` **per lane**
  (`GrooveEngine.cpp:150-152`), so polymeter is automatic. Factory examples:
  `setLaneLength(p, VoiceClosedHat, 12)` (`src/core/Patterns.cpp:192`),
  `setLaneLength(p, VoicePerc, 16)` and `VoiceOdd, 8` (`:215-216`).
- **Pattern length** is a UI/authoring constraint only: the engine never reads
  `PatternData::length` — it reads `lane.length`. `sanitise()` is what keeps
  `lane.length <= pattern.length`, and every published pattern is sanitised
  (`KlopgeestProcessor.cpp:283-287`). A light engine must therefore treat *lane* length as the
  cycle, not pattern length.
- **Audible** = `!muted && (!anySoloed() || soloed)` (`Pattern.cpp:56-70`); audibility gates
  event generation entirely (`GrooveEngine.cpp:146-147`) and is published per voice as
  `visual.laneAudible[]` (`Engine.cpp:181`).
- **Locks** are set from three places: per step and per lane in the editor
  (`src/ui/Panels.cpp:534`, `:552`; `src/ui/StepGrid.cpp:140-144`), and `Step::locked` in
  factory patterns (`src/core/Patterns.cpp:12`, `:99`, `:162`).
- Serialisation is compact strings, float fields as 8-hex-digit bit patterns for bit-exact
  recall (`Pattern.cpp:99-162`, `:183-189`). **A new per-step field means a new sibling
  property, not a schema change** — `fromValueTree` leaves absent fields at their defaults
  (`Pattern.cpp:196-227`) — but `operator==` (`:72-95`) must be extended or two patterns that
  differ only in the new field will compare equal (this silently defeats the state tests).

---

## 4. MUTATION

### 4.1 The engine (message thread only)

`src/sequencer/MutationEngine.h:22-50` — `nudge`, `mutate`, `recall`, `canRecall`,
`historyDepth`. History is a fixed array of **12** full `PatternData` snapshots
(`MutationEngine.h:47-49`, shift-down at `MutationEngine.cpp:47-57`). Work happens on the
message thread and reaches the audio thread **only** through the pattern queue
(`docs/ARCHITECTURE.md:44-50`).

| Operation | Implementation | What it does |
|---|---|---|
| `nudge` | `MutationEngine.cpp:59-116` | up to 6 attempts, ≤2 edits: adds a quiet ghost (`:83-91`), or re-shapes a non-anchor hit's velocity/micro/probability (`:93-104`) |
| `mutate` | `:118-209` | (1) ghost weather on clap..odd (`:126-145`); (2) accent redistribution + pitch colour + occasional ratchet (`:148-170`); (3) displace one non-anchor hit one step (`:172-188`); (4) 25–50 % chance to drop one non-anchor hit (`:191-201`) |
| `recall` | `:211-218` | pops one history entry |
| **EVOLVE** | not in `MutationEngine` at all — implemented in the processor timer | `KlopgeestProcessor.cpp:369-403`: every **even-numbered bar** (`:391`) while the `evolve` parameter is on and `visual.playing` is true, it runs `nudge(..., mutateAmount * 0.5f, ...)` |

Protection rules: anchors are `kick step%4==0` and `snare step%8==4`
(`MutationEngine.cpp:28-35`); mutable means lane not locked/muted, step in range, step not
locked (`:37-45`). Note `stepMutable` checks `lane.muted` — a muted lane is also immune to
mutation.

### 4.2 Counters, and what the audio thread can know

- `KlopgeestProcessor::mutationCounter` (`KlopgeestProcessor.h:144`) is a message-thread
  `std::uint32_t` used **only** to salt the mutation RNG
  (`KlopgeestProcessor.cpp:321`, `:332`, `:394-396`). It is not shared with the audio thread.
- `visual.mutationCounter` is a `std::atomic<unsigned int>` (`src/core/VisualState.h:80`),
  incremented on the message thread after a successful nudge/mutate/recall
  (`KlopgeestProcessor.cpp:326`, `:337`, `:347`, `:401`). **Nothing reads it** — not the UI, not
  the DSP (grep: only writes, plus the `reset()` in `VisualState.h:57`). It is a ready-made
  “mutation epoch” that is currently unused.
- `visual.patternChangeCounter` (`VisualState.h:81`) is incremented **on both threads**: on the
  audio thread when a new snapshot is pulled (`KlopgeestProcessor.cpp:161-162`) and on the
  message thread in `publishPattern` (`:293`). It is therefore a *progress* signal, not an
  exact count — the UI uses it as a change detector only (`src/ui/StepGrid.cpp:170`).

### 4.3 Is there any signal at event time that a step is a mutation?

**No — established.** `HitFlag::mutation` exists (`VisualState.h:25`) and is displayed as “MUT”
in the UI legend (`src/ui/Panels.cpp:968-969`), but no code ever sets it. Mutation produces an
ordinary new `PatternData`; the audio thread cannot distinguish a mutated step from a
hand-programmed one. A light engine consequently cannot flash “a mutation just happened” at
event resolution; the best available proxy is the `visual.mutationCounter` atomic (message-thread
timestamp, read whenever the light stage next runs, i.e. one block later at audio rate — actually
available within the next block, since it is an atomic).

---

## 5. PARAMETER CONTRACT

### 5.1 How parameters are declared

- IDs are string constants in namespace `klopgeest::pid` (`src/core/ParamIDs.h:13-84`).
  Per-voice ids are built as `prefix + "." + suffix`, prefixes
  `{"kick","snare","clap","ch","oh","tom","perc","odd"}` (`:16-19`), 15 suffixes
  (`:22-39`). Globals are `global.*` (`:42-51`), master is `master.*` (`:54-69`).
  The header states the rule: **never rename these strings** (`:9-11`).
- One source of truth for ranges: `struct ParamRange {min,max,def,centre,step}` and the
  `ranges::` table (`src/core/Parameters.h:18-51`); `makeRange` maps `step → interval` and
  `centre → setSkewForCentre` (`:143-151`).
- The layout is built in one function, `createParameterLayout()` (`Parameters.cpp:125-189`):
  8 × 15 voice parameters (`:130-142`, via `addFloat` at `:33-43`), then globals (`:145-166`),
  then master (`:169-187`). Choice/int/bool types are added explicitly
  (`:155-157`, `:162-163`, `:165-166`, `:184-187`).
  **Total today: 146 parameters** (120 voice + 10 global + 16 master); the smoke test asserts
  `>= 140` (`tests/TestMain.cpp:77`) so growth is safe.
- Defaults live twice and must agree: `VoiceDefaults` / `voiceDefaults()` in `Parameters.h:122-141`
  for the APVTS, and the fallbacks in `EngineParams.h` used when a pointer is null
  (`KlopgeestProcessor.cpp:78`, `:85-128`).
- The audio thread reads them into a POD once per block: `readParams()`
  (`KlopgeestProcessor.cpp:76-129`) via cached `std::atomic<float>*` pointers
  (`ParamPointers`, `KlopgeestProcessor.h:95-125`, populated in `cacheParameterPointers()`,
  `.cpp:40-74`).

### 5.2 The POD handed to the engine

`src/core/EngineParams.h:78-83`:

```cpp
struct EngineParams
{
    VoiceParams  voice [kNumVoices];
    GlobalParams global;
    MasterParams master;
};
```

`GlobalParams` (`:44-56`) and `MasterParams` (`:58-76`) are plain aggregates with defaults, so
offline renders and tests can construct them directly (`tests/QaIntegrationTests.cpp:372-375`,
`tests/SequencerTests.cpp:70-78`).

### 5.3 State save / load

`getStateInformation` (`KlopgeestProcessor.cpp:239-253`) writes
`KLOPGEEST_STATE{ version=1, patternIndex, KLOPGEEST=<apvts state>, PATTERN=<pattern> }`.
`setStateInformation` (`:255-278`) replaces the APVTS state from the `KLOPGEEST` child,
restores the pattern, resets mutation history and `lastEvolveBar`. `version` is a literal `1`
and there is **no migration branch**. Adding parameters is forward-safe (APVTS `replaceState`
restores what it finds and leaves missing ids at their defaults) but *backward* saves then lack
the new values; the code has no place to express that intent other than the unused `version`.
APVTS is constructed with `kProductName` as the value-tree type (`KlopgeestProcessor.cpp:24`),
which is why the state child is named `KLOPGEEST`.

### 5.4 Factory programs

Programs **are** the factory patterns: `getNumPrograms()` returns
`factoryPatternCount()` = 6 (`KlopgeestProcessor.cpp:211-214`), `setCurrentProgram(i)` calls
`loadFactoryPattern(i)` (`:221-224`), names come from `factoryPatternName`
(`src/core/Patterns.cpp:78-88`). `changeProgramName` is intentionally a no-op (`:231-234`):
there is no user preset system.

### 5.5 What adding an optional light parameter group touches

Minimum honest list for e.g. `light.enabled`, `light.personality`, `light.intensity`:

1. `src/core/ParamIDs.h` — three new `inline constexpr const char*` (new namespace `light`).
2. `src/core/Parameters.h` — `ranges::lightIntensity` etc.; optionally extend `personalityNames()`
   or add a separate `lightPersonalityNames()` (`Parameters.h:83`).
3. `src/core/Parameters.cpp` — `addFloat` + choice in `createParameterLayout`, with a
   `formatPercent`-style text function (`:12-31`).
4. `src/core/EngineParams.h` — a `LightParams` aggregate plus a member on `EngineParams`
   (note: this changes the POD layout, but nothing serialises `EngineParams` itself — only
   APVTS + pattern are saved, `KlopgeestProcessor.cpp:243-249`).
5. `src/plugin/KlopgeestProcessor.h/.cpp` — three pointers in `ParamPointers`, three lines in
   `cacheParameterPointers`, three in `readParams`.
6. Tests: `tests/TestMain.cpp:77` (count ≥140 still fine), and the hostile fuzz
   (`tests/QaIntegrationTests.cpp:767-769`) automatically covers new parameters because it
   iterates `getParameters()` — a new parameter is fuzzed for free.
7. Docs: `docs/MANUAL.md` lists controls; `README.md` "Known limitations" would need updating if
   light is optional-but-present.

That is it — nothing else in the codebase enumerates parameters positionally except
`VoiceParamIndex` (`Parameters.h:54-61`), which is per-voice only.

---

## 6. AUDIO PATH AND ROUTING

### 6.1 `Engine::process` order (the only place render order is decided)

`src/dsp/Engine.cpp:58-196`, with `docs/ARCHITECTURE.md:47-58` as the normative description:

| Step | Lines | What happens |
|---|---|---|
| 0 | `:68` | `output.clear()` |
| 1 | `:71-76` | sequencer → `SequencedEvent events[512]` |
| 2 | `:78-103` | live MIDI folded in as synthetic events (`HitFlag::live`); all-notes-off/all-sound-off → `voices.reset()` |
| 3 | `:109-122` | insertion sort by `(sampleOffset, voice)` |
| 4 | `:124-148` | loop: fire events with `sampleOffset <= cursor`, store per-voice telemetry, `voices.render(output, cursor, boundary-cursor)` |
| 5 | `:150-151` | `master.process(output, numSamples)` — the glue machine, in place |
| 6 | `:153-195` | meters (peak/RMS per channel), per-voice envelope, lane step, transport, gain reduction |

`VoiceManager` is a plain 8-slot owner (`src/dsp/voices/VoiceManager.cpp:5-55`); every voice adds
into the **same stereo buffer** (mono-summed to both channels unless it renders stereo,
`src/dsp/voices/VoiceBase.cpp:119-166`), with a closed-hat → open-hat choke
(`VoiceManager.cpp:44-47`). There is no per-voice bus, no stem output, no send — one stereo bus,
one master.

### 6.2 What the glue machine does to anything that enters it

`GlueMachine::process` (`src/dsp/master/GlueMachine.cpp:477-656`). For a carrier at 18–20 kHz,
the relevant facts, in signal order:

1. **Input sanitisation clamps to ±32.0** (`:502-508`). Not a limiting factor for a sane carrier,
   but it is a hard wall.
2. **Pre-compression saturation** (`:516-519`) — a bias-asymmetric `tanh` stage with one-pole
   low-passes either side (topology comment `:4-23`, saturator `:173-230`), drive
   `satPre + 0.55·heat` (`:352`). A carrier passing through here is waveshaped: it grows
   harmonics *and* intermodulates with the music, i.e. it becomes audible.
3. **Stereo-linked detection with a sidechain high-pass** (`:521-544`). Detection is
   `max(|L|,|R|)` before the HPF blend (`:527`); the sidechain HPF corner is 90 Hz nominal, scaled
   by GLUE (`:329`, filter update `:374-385`). Carriers are far above the HPF corner, so they
   **will** drive the detector and therefore duck the music.
4. **Soft-knee gain computer + ballistics + 75 % auto make-up** (`:554-592`). Carriers would
   both *cause* gain reduction and *receive* it.
5. **Post-compression saturation** (`:594-597`), **parallel mix** (`:599-602`, `mix` blends
   dry back in), **output gain** `-24..+12 dB` (`:604-607`, `:354`).
6. **Ceiling limiter, zero look-ahead** (`:609-632`): instant attack (`:616-617`), 120 ms
   release (`:308`), and then a **hard clamp** at the ceiling (`:624-626`). The ceiling is
   `clamp(params.ceilingDb, -12, 0)` dB (`:355`) — its **maximum is 0 dBFS** and the default is
   −0.4 dBFS (`EngineParams.h:72`, `Parameters.h:50`).
7. Meters are computed from the post-limiter samples (`:642-655`), and `Engine` recomputes its
   own peak/RMS from the same buffer (`Engine.cpp:153-195`).

**Inference (not measured, but directly implied by 6):** the hard ceiling makes any carrier
mixed in *before* the limiter share a single amplitude budget with the drums. To be visible at
all, a 19 kHz carrier would need to be a substantial fraction of full scale; it would then be
gain-ridden by the music's own peak envelope (so the light would pump and flicker with the bus)
and clipped flat at the ceiling, which for a sine carrier means harmonic splatter across the
band (audible, and unpredictable at LED level). Any design that sums carriers *before*
`master.process` should be treated as wrong until proven otherwise.

### 6.3 Where a post-master carrier injection has to sit

**Established constraint:** the only place in the whole codebase where “the final stereo output
of the instrument” exists as a mutable buffer is `Engine::process` after `master.process`
(`Engine.cpp:151`). The plugin glue passes the same buffer straight out (`KlopgeestProcessor.cpp:186`)
and the offline render writes the same buffer to disk (`tools/main.cpp:132-147`).

So the insertion point is between `Engine.cpp:151` and `Engine.cpp:153`, with a decision needed:

| Variant | Consequence |
|---|---|
| after `master.process`, **before** the meter loop at `:153-195` | carriers are included in `visual.outPeak*`/`outRms*` and in the GUI output meter — the meter then no longer shows the music's level, and any peak-based assertion changes |
| after `master.process`, **after** the meter loop (i.e. last thing before return) | music metering stays honest; the buffer leaving the function is what is heard and what the render tool's own peak scan sees (`tools/main.cpp:134-145`) |

The second variant is the safer default; but see Trap T3 — the render gate in
`scripts/validate.sh:202-205` fails on **any** full-scale sample and on a peak `>= 0.0 dBFS`, and
`renderDemo` measures the buffer, not the engine's telemetry.

### 6.4 Block sizes / sample rates the path already survives

- `Engine::prepare` accepts any `maxBlockSize`, floored at 16 (`Engine.cpp:14`).
- Glue machine does **control-rate work on a sample-timed 32-sample clock**, explicitly “never on
  host block boundaries … including 1-sample blocks” (`GlueMachine.cpp:44-48`, `:486-489`,
  `:646-650`).
- Block-size invariance is asserted for `64/128/512/2048` (`tests/QaIntegrationTests.cpp:541`)
  and sample rates `44.1/48/96 kHz` (`:491`).
- **44.1 kHz matters for carriers**: Nyquist is 22.05 kHz, so 20 kHz sits at 0.907·Nyquist and
  is preserved, but only just. At the corners of the sweep the light engine must not assume
  `sampleRate >= 48 kHz`.

### 6.5 Plugin path vs. standalone vs. headless

- **Plugin** (`src/plugin/KlopgeestProcessor.cpp:150-191`): pull pattern (`:161-162`) →
  `readParams` (`:164-166`) → read `AudioPlayHead` into `HostTransport` (`:169-181`) → one
  `engine.process` (`:186`) → fix up `visual.bpm` when free-running (`:189-190`).
  Bus layout: output mono or stereo, input disabled or equal (`:198-206`);
  `getTailLengthSeconds()` = 3.0 (`KlopgeestProcessor.h:45`); no latency reported.
- **Standalone**: produced by `juce_add_plugin(... FORMATS AU VST3 Standalone ...)`
  (`CMakeLists.txt:55-71`), i.e. JUCE's `StandaloneFilterWindow`. The **only** output choice
  today is JUCE's own *Options → Audio/MIDI Settings* dialog
  (`.deps/JUCE/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h:546`
  `AudioDeviceSelectorComponent`, `:284 showAudioSettingsDialog`, `:357 deviceManager.initialise`).
  That dialog chooses **device, sample rate and buffer size**.
  **It cannot choose the channel count or a routing target**, because
  `getNumOutputChannels()` returns the processor's bus count
  (`juce_StandaloneFilterWindow.h:159-166`) and the processor declares stereo only
  (`KlopgeestProcessor.cpp:22-23`). The window's callback does receive the device's
  `numOutputChannels` (`:551-566`) but passes them to `AudioProcessorPlayer`, which only feeds
  the buses the processor declares.
  ⇒ **Choosing an output device or routing for a light signal is new standalone code**
  (e.g. a second `AudioDeviceManager`, or a dedicated light output component), not a setting.
- **Headless** (`tools/main.cpp`): `KlopgeestProcessor` driven by hand,
  `setPlayConfigDetails(0, 2, 48000, 512)` (`:39-40`, `:167-168`), `processBlock` in a loop
  (`:132`), buffer scanned for finiteness/peak/full-scale (`:134-145`) and written as 24-bit
  stereo WAV (`:68-72`). This is the only *automated* “what the instrument outputs” path.

---

## 7. VISUALSTATE — the existing template for lock-free audio→GUI state

`src/core/VisualState.h:29-82`. Design rules stated in the header (`:7-14`) and realised as a POD
of `std::atomic` with **relaxed** ordering everywhere, written by the audio thread, read by the
GUI at 30 Hz (`src/ui/Editor.cpp:99` `startTimerHz(30)`).

Per-voice fields: `voiceLevel`, `lastVelocity`, `lastPitchSemis`, `lastTimingMs`,
`lastDecayScale`, `lastFlags`, `triggerCount`, `laneStep`, `laneAudible` (`:61-69`).
Global fields: `gainReductionDb`, `outPeakL/R`, `outRmsL/R`, `playing`, `hostSynced`,
`personality`, `bpm`, `currentTick`, `mutationCounter`, `patternChangeCounter` (`:71-81`).
`reset()` zeroes everything (`:33-59`).

**What to copy for a light engine**

1. **The type discipline**: a POD aggregate, only `std::atomic` members, no locks, no
   allocation, `reset()` provided, all stores `memory_order_relaxed`. One block of latency is
   acceptable by construction (`docs/ARCHITECTURE.md:56-58`).
2. **The `triggerCount` idiom** — a monotonically increasing counter used purely as a change
   detector (`Engine.cpp:139`, consumed at `src/ui/Panels.cpp:860-868`). This is the correct
   template for a light-side hit counter and for a dropped-update detector.
3. **Write-site placement**: everything is stored in the render loop *at the moment the voice is
   triggered* (`Engine.cpp:134-139`), so timestamps and values are consistent with the sample
   at which the hit occurred. A light stage must write its state at the same points.
4. **A panic/health field does not exist.** `panic()` is a `std::atomic<bool>` on the processor
   (`KlopgeestProcessor.h:77`) consumed by `engine.reset()` before rendering
   (`KlopgeestProcessor.cpp:158-159`). There is no *published* panic status, no “light engine
   healthy” flag, no carrier-present meter. A `visual.lightPanicCount` /
   `visual.carrierLevel` pair would be the natural extension.

**Known limitation of the template:** it is a *last-hit* snapshot, not a log. Two hits on the
same voice inside one block overwrite each other in `lastVelocity`/`lastFlags` before the GUI
ever reads them (`Engine.cpp:129-141` stores per voice, not per event). `triggerCount` is the
only field that counts all of them. Any light feature driven from `VisualState` (as opposed to
from events inside `Engine::process`) inherits this: **it will miss hits**.

---

## 8. REALTIME RULES ALREADY ENFORCED — and the tests that assert them

| Rule | Stated in | Asserted by |
|---|---|---|
| No heap allocation after `prepare()` on the audio path | `src/dsp/Engine.h:15-18`, `docs/ARCHITECTURE.md:59-60` | `testAllocationFreeProcessBlock` (`tests/QaIntegrationTests.cpp:308-364`, 200 blocks incl. pattern publishes + panic), `testAllocationFreeEngine` (`:366-397`), `testAllocationFreeMaster` (`:399-432`), `testAllocationFreeVoices` (`:434-485`, 50 hits × 8 voices), `testPatternPublishingThreadSafety` (`:834-892`, real `std::thread`) |
| The counter is per-thread and opt-in | `tests/AllocationCounter.h:11-22`, `:50-72` | mechanism itself; `tests/TESTING.md:91-122` documents it |
| No locks / no file I/O / no blocking on the audio thread | `docs/ARCHITECTURE.md:59` | by construction + the allocation test; no direct lock test exists (**not established** beyond review) |
| Block-size invariance (64/128/512/2048, tol 1e-4 absolute) | `docs/DESIGN.md:29-44`, `docs/TESTING.md:143-152` | `testBlockSizeInvariance` (`tests/QaIntegrationTests.cpp:532-590`); at event level `tests/SequencerTests.cpp:292-323` (“absolute event times identical for 64/128/512/1024-sample blocks”) |
| Sample-rate sweep 44.1/48/96 kHz incl. state recall at each rate | `docs/TESTING.md:136-141` | `testSampleRateSweep` (`tests/QaIntegrationTests.cpp:489-528`) |
| Determinism: same seed → identical audio; different seed → different audio | `docs/ARCHITECTURE.md:77-84` | `testDeterministicRender` (`tests/TestMain.cpp:200-230`, exact `std::vector<float>` equality), `testDeterministicSeeds` (`tests/QaIntegrationTests.cpp:730-746`, `first == second`), `testDeterminism` (`tests/SequencerTests.cpp:260-289`, event-list equality), `testGeneratePurity` (`:809-837`) |
| Glue machine determinism / block-size independence / mono | `docs/TESTING.md:53` | `testDeterminism` (`tests/MasterTests.cpp:672`), `testBlockSizeInvariance` (`:845`), `testMonoPath` (`:786-808`) |
| Limiter holds the ceiling | `README.md:86` | `testLimiterCeiling` (`tests/MasterTests.cpp:553-578`: `peak <= ceiling + 0.1` **and** `peak > ceiling - 1.0`) |
| Hostile parameter/pattern fuzz survives | `docs/TESTING.md:175-183` | `testHostileParameterFuzz` (`tests/QaIntegrationTests.cpp:750-830`, 64 configs, iterates every parameter) |
| No full-scale samples / audible, non-silent demo render | `README.md:175-176` | `scripts/validate.sh:188-218` (`peak >= 0.0` fails, `full-scale samples > 0` fails, silent fails) |

**To hold a light engine to the same bar** it must: allocate nothing in `process`/`trigger`,
add no locks or file I/O, produce identical carriers for 64- and 2048-sample blocks, be
independent of sample rate, be a pure function of `(seed, tick, params)` for a given transport
position, and keep the total output below the render gate's peak rule.

---

## 9. TEST / TOOLING SURFACE A LIGHT ENGINE WOULD PLUG INTO

### 9.1 Harness

`tests/TestHarness.h:16-50` — five free functions (`check`, `checkNear`, `checkInRange`,
`info`, `skip`) and three global counters; no registration, no exceptions. One entry point:
`runAllTests()` (`tests/AllTests.h:4`, implemented `tests/TestMain.cpp:284-344`) which chains
the suites behind `#if __has_include` guards (`:11-29`) and prints a per-section count
(`:290-305`, `:307-339`). A new suite needs a `.h` declaring `runXxxTests()`, a `.cpp` defining
it, and one guarded call + `endSection` pair — the CMake glob picks the file up
(`CMakeLists.txt:40`, `docs/TESTING.md:56-73`).

### 9.2 Existing fixtures a light engine can reuse

- `TransportFixture` + `RecordedEvent` (`tests/SequencerTests.cpp:41-58`, `:61-120`): drives
  `Sequencer` directly with an explicit host clock and records *absolute sample positions* and
  every event field. This is exactly the harness a light engine's event mapping should be tested
  with — it already captures `timingDevMs`, `flags`, `tick`, `step`.
- `RenderedAudio` + `runBlocks` (`tests/QaIntegrationTests.cpp:169`, `:178`) and
  `maxAbsDifference` (used at `:579-581`): the block-size comparison machinery.
- `class FakePlayHead final : public juce::AudioPlayHead` (`tests/QaIntegrationTests.cpp:248`,
  used in `testHostTransport` at `:650-712`): host transport simulation with
  `playing/bpm/ppq/advance()`.
- `struct Rig` (`tests/MasterTests.cpp:198`, used at `:567`, `:797`): a prepared `GlueMachine` +
  parameter set, for testing anything inserted after the master.

### 9.3 Tool

`tools/main.cpp` subcommands:
- `test` — the default, `runAllTests()` (`:228`).
- `render [out.wav]` — 48 s, 48 kHz, 512-sample blocks, five sections, factory patterns 0/3/1/4/5,
  NUDGE/MUTATE actions, `ceiling = -1.2 dB`, 24-bit stereo WAV (`:30-160`; parameters at
  `:48-55`, sections at `:92-100`). Prints `bars/peak/full-scale/finite` (`:156-158`).
- `snapshot [out.png]` — offscreen 1180×680 editor PNG (`:162-204`).

`scripts/validate.sh` is the release gate: build → tests → **demo render with peak/clipping
rules** (`:188-218`) → snapshot (`:220-246`) → `auval` (`:248-292`) → pluginval strictness 10 on
VST3 and AU (`:294-330`) → PASS/FAIL/SKIP summary, non-zero exit on any FAIL (`:332-353`).
It contains **no SHA-256 step**; the “same SHA-256 on repeated renders” claim in
`README.md:175-176` and `docs/VALIDATION.md:171-172` is a recorded manual observation, not an
automated assertion. The automated determinism proof is the in-process test comparisons listed
in §8.

### 9.4 What is missing for light-specific verification

No golden files, no WAV comparison harness, no spectrum-analysis helper, no “carrier present”
detector anywhere in `tests/` or `tools/` (grep for FFT/spectrum: none). A light engine's
carrier presence, frequency accuracy and channel independence would today be verified only by
rendering a WAV and inspecting it outside the repository (**not established**).

---

## 10. HOOKS THE LIGHT ENGINE NEEDS THAT DO NOT EXIST YET

Ordered by how much they block. “Smallest plausible change” is deliberately conservative —
each is a local edit, none requires re-architecting.

### H1 — No access to the event stream outside `Engine::process`
**Gap.** `SequencedEvent events[kMaxEventsPerBlock]` is a function-local array
(`src/dsp/Engine.cpp:71`), consumed and destroyed in the same call. Nothing exposes it: not
`Engine`, not the processor, not `VisualState`.
**Smallest change.** Add a light stage *inside* `Engine::process`, called twice: once per fired
event (right where telemetry is stored, `:129-141`) and once per block after
`master.process` (`:151`). Keep the light object as an `Engine` member next to `GlueMachine`
(`src/dsp/Engine.h:55-57`) so `prepare`/`reset`/`setParams` propagate
(`Engine.cpp:11-56`). No new public API beyond a getter for telemetry.
**Alternative (worse).** Publish events through a lock-free queue to a light object outside the
engine — but that re-introduces a second audio consumer, a second prepare/reset lifecycle, and
the possibility of the light running at a different block size than the audio.

### H2 — `timingDevMs` is a sum, not a decomposition
**Gap.** `GrooveEngine.cpp:185-220` computes swing, microtiming, bias and humanisation
separately and returns only their total (`:218`); the event carries only the total (`:22`, `:385`).
**Smallest change.** Extend `SequencedEvent` with three `float`s
(`swingMs`, `microMs`, `humanMs`) — 12 bytes per event, event count per block is tiny — or, if
the struct must stay frozen, gate them behind a second telemetry struct. `writeEvent`
(`:306-327`) already takes every other value as an argument; adding three parameters is a
mechanical edit at one call site in the programmed branch (`:385-387`) and one in the ghost
branch (`:433-435`). **Note:** `SequencedEvent` is a shared type between `src/sequencer` and
`src/dsp` (`docs/ARCHITECTURE.md:33-40` calls `src/core/` frozen, not `src/sequencer/`), so this
is legal but touches `tests/SequencerTests.cpp:41-58` (`RecordedEvent`) if the test wants to
record the new fields.

### H3 — No tempo / step-length on the event
**Gap.** `stepSamples` is a `GrooveEngine::generate` argument (`:110`) but is not stored;
`frame.bpm` exists per block (`Sequencer.cpp:87`) but is not attached to events. A light engine
must currently recover tempo from `Engine::getParams().global.tempo`, which is **wrong under a
host clock** — in host mode the actual BPM comes from `host.bpm`
(`Sequencer.cpp:86`), and the processor overwrites the free-run value afterwards
(`KlopgeestProcessor.cpp:189-190`).
**Smallest change.** Add `float stepMs` (nominal 16th in ms) to `SequencedEvent`; it is computed
one line earlier in `Sequencer.cpp`'s caller loop (`:135`) as `stepSamples * 1000 / sampleRate`,
and `GrooveEngine` already derives `stepMs` at `:123`. Zero risk, removes a whole class of
tempo-mismatch bugs.

### H4 — No signal that a step is a mutation
**Gap.** §4.3: `HitFlag::mutation` (`VisualState.h:25`) is never set; mutation is only a
pattern swap plus an unused atomic (`visual.mutationCounter`).
**Smallest change (honest version).** Read `visual.mutationCounter` at block rate in the light
stage and treat a change as “the pattern was mutated recently”; it cannot attribute a hit to a
mutation. **Smallest change (precise version).** Tag the pattern: a per-step or per-pattern
“mutation epoch” is a schema change (`Pattern.cpp:165-194`) and would need `operator==`
(`:72-95`) updated. Recommend the honest version first; it is a one-line read.

### H5 — No locked / ratchet-index information at event time
**Gap.** §1.3/§1.4: `locked` never reaches the event; `HitFlag::ratchet` says “a sub-hit, not the
first one” but not which one, and the first sub-hit is unflagged
(`GrooveEngine.cpp:375-377`).
**Smallest change.** Extend the flag word — three bits already exist (`none`…`live` use six of
32) — e.g. `locked = 1u << 6`, and carry `k` in a spare field. Alternatively, since events from
one step arrive consecutively, a light engine can count them; but the flag is cheaper and
survives truncation better.

### H6 — No carrier/mixer stage, and no post-master audio hook
**Gap.** `Engine::process` ends at the meters (`:153-195`); there is no “after everything”
callback, and the buffer is the only interface.
**Smallest change.** A `LightEngine` member on `Engine`, prepared in `Engine::prepare`
(`:11-22`), reset in `Engine::reset` (`:24-30`), parameterised in `Engine::setParams` (`:42-47`),
rendered with one call between `:151` and `:153` (or after the meters — see §6.3), summed into
`output` with its own headroom. It must be given `numSamples`, `sampleRate`, `EngineParams` and
the per-block event list.

### H7 — No light parameters, no light state, no light telemetry
**Gap.** No `light.*` ids (`ParamIDs.h`), no `LightParams` (`EngineParams.h`), no light fields in
`VisualState` (`VisualState.h:29-82`).
**Smallest change.** §5.5's checklist plus ~4 atomics in `VisualState` (carrier level L/R,
intensity, a hit counter, a health/panic counter). Copy the `triggerCount` idiom (§7).

### H8 — Panic does not reach a light engine, and panic status is not published
**Gap.** `panic()` sets a bool (`KlopgeestProcessor.h:77`) consumed as `engine.reset()`
(`KlopgeestProcessor.cpp:158-159`). MIDI all-notes-off calls `voices.reset()` only
(`Engine.cpp:99-102`) and does **not** reset the sequencer. Nothing tells the GUI (or a light
device) that a panic occurred.
**Smallest change.** Reset the light stage in `Engine::reset()` and in the MIDI all-notes-off
branch (`:101`), and add a `visual.lightPanicCount` counter incremented on audio-side reset.
Panic is a *safety* control for a light (LEDs stuck at full intensity), so it should be a
first-class, published state, not an incidental side effect.

### H9 — No way to route carriers to a separate output
**Gap.** §6.5: the bus is stereo, the standalone derives its channel count from the bus
(`juce_StandaloneFilterWindow.h:159-166`), and the processor's `isBusesLayoutSupported`
(`KlopgeestProcessor.cpp:198-206`) rejects anything but mono/stereo.
**Smallest change.** If a separate device is wanted, the smallest honest option is a
standalone-only `AudioDeviceManager` alongside JUCE's, fed from a second light buffer produced by
`Engine` and published to the standalone UI through the same lock-free atomic discipline as
`VisualState`. Adding a third/fourth output channel to the plugin bus is **not** small: it
changes the AU/VST3 bus contract, `isBusesLayoutSupported`, the mono path, and pluginval
expectations. Do not do that for M1+1.

### H10 — No test/tool surface for carriers
**Gap.** §9.4: no spectral helper, no carrier-presence assertion, no light suite.
**Smallest change.** New `tests/LightTests.*` following the `__has_include` pattern
(`tests/TestMain.cpp:26-29`), driven by `TransportFixture` (`tests/SequencerTests.cpp:61+`) for
event mapping and by `RenderedAudio` for block-size/sample-rate invariance of the carrier
buffer; plus one `klop-tool` subcommand that renders the light signal **separately** to its own
WAV (so the light can be inspected without the music at −1.2 dBFS sitting on top of it).

---

## 11. TRAPS

Things that will silently break, in the order they are likely to bite.

**T1 — The master limiter is a hard wall and the carriers share its budget.**
`GlueMachine.cpp:609-632`, ceiling clamped to `≤ 0 dBFS` (`:355`). Carriers summed before
`master.process` get hard-clamped (sine → square → harmonic splatter) and gain-ridden by the
drums' own peak envelope. Even *after* the limiter, the carriers must fit under the render
gate's peak rule (T3). **Nothing in the codebase reserves headroom for anything.**

**T2 — The glue machine's saturators will make an inaudible carrier audible.**
`tanh` pre/post stages (`GlueMachine.cpp:173-230`, `:516-519`, `:594-597`, drive from
`satPre + 0.55·heat`, `:352`). Intermodulation between a 19 kHz carrier and percussion produces
difference tones in the audible band. **Inference, not measured** — but the mechanism is
unambiguous from the code, and it is another reason the carriers must not pass through the
master.

**T3 — `scripts/validate.sh` is a byte-level gate on the whole output.**
It fails if the demo peak is `>= 0.0 dBFS` (`:202-203`) or if *any* sample reaches `0.9999`
(`tools/main.cpp:142-143`, gate at `validate.sh:204-205`), and the render sets
`ceiling = -1.2 dBFS` (`tools/main.cpp:54`). Post-master carriers raise the peak; the light
engine therefore has to fit inside the same −1.2 dBFS envelope or the release gate goes red.
Any light residual that keeps running after the music stops also changes the “silent render”
check (`validate.sh:206-211`) — a carrier that never decays will make a stopped transport
non-silent. `testTransportInternal` asserts silence when stopped (`QaIntegrationTests.cpp:614-615`).

**T4 — Mono collapses one of the two light channels.**
`isBusesLayoutSupported` accepts mono (`KlopgeestProcessor.cpp:198-206`);
`GlueMachine::process` sets `right = nullptr` for one-channel buffers and computes `inR = inL`
(`:491-492`, `:502-503`) and only writes `left` (`:638-640`); `Engine`'s telemetry falls back to
channel 0 for the right meter (`Engine.cpp:157`). `testMonoPath` asserts dual-mono equivalence
(`MasterTests.cpp:786-808`) — which means an independent L/R carrier scheme **silently loses half
its information** in a mono instance. Decide explicitly: either derive both carriers from one
light state (mono-safe) or detect the channel count and document the loss.

**T5 — The host may run the instance at 44.1 kHz, and the light has no say in it.**
`testSampleRateSweep` runs 44.1/48/96 (`QaIntegrationTests.cpp:491`). Nyquist at 44.1 is
22.05 kHz; carriers near 20 kHz are legal but have no margin. Any carrier frequency must be a
function of `sampleRate` (or clamped below ~0.45·`sampleRate`), and must not assume `48 kHz`.

**T6 — Whatever leaves the plugin is at the mercy of the host chain.**
Host inserts, render-to-mesh mastering limiters, MP3/AAC encoding, and OS sample-rate conversion
will attenuate or destroy 18–20 kHz content. **Not knowable with these methods** (needs a host
session and hardware); recorded here so nobody promises “the light always works in a DAW”.

**T7 — `VisualState` is a last-hit snapshot, not an event log.**
Within one block, later events overwrite `lastVelocity/lastFlags/lastTimingMs` for the same
voice before the GUI reads them (`Engine.cpp:129-141`). Anything light-related driven from
`VisualState` at 30 Hz (`Editor.cpp:99`) misses ratchets and fast ghost runs, and has ~33 ms
resolution. Sample-accurate light must live in `Engine::process`.

**T8 — Event truncation is silent.**
`Sequencer.cpp:133/:139` stop at `maxOut`; `Engine.cpp:86` stops MIDI at 512. A dense FERAL
pattern at 64 steps with 4-ratchets is 8 voices × 4 = 32 events per tick; a 2048-sample block at
high tempo can contain several ticks. The light engine must count what it saw and expose it
(§H7) rather than assume completeness.

**T9 — Adding a per-step field silently breaks state equality before it breaks serialisation.**
`PatternData::operator==` (`Pattern.cpp:72-95`) enumerates every field; `fromValueTree` leaves
absent fields at defaults (`:196-227`). Add a field, forget `operator==`, and the state tests
(`TestMain.cpp:197`, `QaIntegrationTests.cpp:812-814`) pass while the field is silently not
round-tripping. Also remember the ratchet-encoding bug (`char` vs `int`,
`Pattern.cpp:150-152`, documented in `docs/TESTING.md:126-132`) — fixed-length string encoding
is fragile.

**T10 — `patternChangeCounter` counts on two threads.**
`KlopgeestProcessor.cpp:162` (audio) and `:293` (message). Use it as a change detector, never as
an exact count or as a timestamp (§4.2).

**T11 — A light engine with per-block state will fail block-size invariance.**
The test compares 64- vs 2048-sample renders to `1e-4` absolute (`QaIntegrationTests.cpp:575-589`).
Any carrier phase, envelope, or smoothing advanced per *block* rather than per *sample* — or
reset by a block boundary — fails. Copy the glue machine's discipline: sample-timed control
updates (`GlueMachine.cpp:44-48`, `:646-650`) and state that is a function of absolute tick.

**T12 — Reported latency stays zero, and that is a promise.**
No `setLatencySamples` exists; `docs/DESIGN.md:68-75` argues for it explicitly. Carriers must not
introduce latency or look-ahead, or the plugin's sample-accurate claim breaks and
`auval`/pluginval expectations shift.

**T13 — Photosensitivity / rate limits are a safety requirement, not a taste question.**
A light engine driven by 16th notes at 200 BPM (the `ranges::tempo` maximum, `Parameters.h:35`)
produces ~13.3 flashes/s on every lane, and ratchets multiply that by 4. This is a product-safety
constraint that does not exist anywhere in the current codebase and must be designed in. This
map does not judge any threshold — that is **not established here** and needs a real standard
and a human decision before shipping.

**T14 — Dead declaration to not trip over.**
`Engine::publishVisual` is declared (`src/dsp/Engine.h:50`) but never defined or called — it will
link-fail the moment anyone calls it. `HostTransport::jumped` (`SequencerTypes.h:40`) is declared
and never set anywhere in `src/` or `tests/`. Do not build behaviour on either without fixing
them.

---

## 12. QUICK REFERENCE

**Voice indices** (`src/core/Constants.h:27-37`): `VoiceKick=0, VoiceSnare=1, VoiceClap=2,
VoiceClosedHat=3, VoiceOpenHat=4, VoiceTom=5, VoicePerc=6, VoiceOdd=7`.

**Anchor weights** `kVoiceRoleLoose` (`Constants.h:41-51`): 0.35, 0.50, 0.65, 0.90, 0.95, 0.75,
1.05, 1.20.

**Personality** (`EngineParams.h:14-19`, scale at `GrooveConstants.h:28-30`): tight 0.35,
loose 1.00, feral 1.70.

**Groove constants that shape the feel** (`GrooveConstants.h`): `kTimingBaseMs 60`
(`:34`), clamp 45 ms / 0.35 step, anchors 20 ms / 0.18 step (`:36-40`),
`kLevelBase 0.55`, `kPitchBaseSemis 0.50`, `kDecayBase 0.35` (`:43-47`),
`kAccentGridBoost 0.22`, `kAccentOffbeatTrim 0.06`, `kAccentFlagThreshold 0.75` (`:49-51`),
ghost probability 0.50 max, floor 0.08, range 0.20 (`:55-57`),
`kMaxRatchet 4`, drop 0.16, jitter 0.05 (`:60-62`),
noise periods timing 4–12 / level 2–6 / pitch 6–16 / decay 8–20 ticks (`:66-73`),
FERAL spike 0.05 × 2.2 (`:76-77`),
look-ahead bounds early `0.5 step + 70 ms`, late `2.0 step + 70 ms` (`:79-93`).

**Master defaults** (`EngineParams.h:58-76`): threshold −18 dB, ratio 4, attack 8 ms,
release 180 ms, knee 8 dB, autoRelease on, sidechain 90 Hz, detect 0.5, mix 1.0,
satPre 0.15, satPost 0.10, output 0 dB, ceiling −0.4 dB, glue 0.35, heat 0.25, limiter on.
Make-up gain 75 % (`GlueMachine.cpp:53`), limiter release 120 ms (`:308`), control interval
32 samples (`:48`).

**Determinism salts** (`GrooveEngine.cpp:44-66`): one per dimension; `saltHitSeed 0x1301` feeds
`trigger.seed`, so any new light stream should take a **new** salt to avoid perturbing existing
determinism stories.

---

## 13. WHAT THIS MAP DOES NOT ESTABLISH

- Any SpectraStrobe protocol detail (frequency pairs, channel semantics, framing). No such
  document was in this repository; nothing here should be read as a spec.
- Whether a given LED device tolerates the carrier levels this bus can produce. Needs hardware.
- Whether 18–20 kHz survives a real host chain (T6). Needs a host session.
- Actual measured behaviour of a carrier through `GlueMachine` (T1/T2 are read from code, not
  measured).
- Any photosensitivity standard or safe flash-rate threshold (T13).
- Whether the standalone `AudioDeviceSelectorComponent` can be made to offer >2 output channels
  without changing the bus layout — read as “no” from
  `juce_StandaloneFilterWindow.h:159-166`, not tested.
