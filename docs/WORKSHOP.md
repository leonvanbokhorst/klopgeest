# KLOPGEEST — One-Day Workshop: Making a Drum Machine Feel Alive

A one-day, hands-on workshop for musicians, sound designers and developers who
want to extend KLOPGEEST rather than just use it. By the end of the day each
participant has changed the instrument's *behaviour* — not its GUI skin — and has
proved the change with tests and a rendered demo.

The workshop is built around a division of labour:

* **The AI did the scaffolding**: the architecture, the plumbing, the invariants,
  the test harness and the tedious parameter wiring. You will read those files.
* **The maker decides**: what "loose" means, how early a hat should lean, when a
  ghost note is tasteful, which behaviours belong in TIGHT / LOOSE / FERAL.
  Those decisions are not in the code; they are what you bring.

The rule for the day: **every audible change needs a test and a render.** "It
sounds better" is a hypothesis until it is reproducible.

---

## 0. Before the day (homework, ~45 min)

```sh
cd /Users/leonvanbokhorst/repos/klopgeest
./scripts/dev-build.sh klop_tool
./build/klop_tool_artefacts/Release/klop_tool test
./build/klop_tool_artefacts/Release/klop_tool render Demo/Klopgeest-Demo.wav
```

Read these files, in this order:

1. `src/core/Constants.h` — the permanent indices, voice names and role weights.
2. `src/core/Pattern.h` — `Step`, `Lane`, `PatternData`, and the lock-free
   `PatternQueue` that hands patterns to the audio thread.
3. `src/dsp/voices/Voice.h` — the `DrumVoice` contract every voice implements.
4. `src/sequencer/GrooveEngine.h` — the "pure function of seed/tick" contract.
5. `src/core/EngineParams.h` — the plain parameter snapshot the audio thread sees.
6. `src/dsp/Engine.h` — how voices, sequencer and glue machine are wired.
7. `src/core/VisualState.h` — the telemetry the GUI reads (including `HitFlag`).
8. `tests/TestHarness.h` and `docs/TESTING.md` — how claims become checks.

Two rules you will see everywhere and must preserve:

* **No allocation, locking or blocking after `prepare()`.** The audio thread is
  sacred. `tests/AllocationCounter.*` will catch you.
* **Stochastic behaviour is a pure function of `(seed, voice, tick, salt)`.**
  No hidden state, no `rand()`, no wall clock. That is what makes renders
  reproducible and block-size invariant.

---

## 1. Morning (09:30–12:30): The contracts

### 09:30 — Tour and build loop (45 min)

* What runs where: message thread (editing, mutation, file/state) vs audio
  thread (sequencer → voices → glue machine → metering).
* The build loop: edit → `./scripts/dev-build.sh klop_tool` →
  `klop_tool test` → `klop_tool render Demo/Klopgeest-Demo.wav`.
* The Phase A placeholder implementations have all been replaced by the real
  workstreams: `src/dsp/voices/VoiceFactory.cpp` (eight procedural voices),
  `src/sequencer/Sequencer.cpp` + `GrooveEngine.cpp`, `src/dsp/master/
  GlueMachine.cpp` and `src/ui/` (real editor). Read them as worked examples of
  the contracts. Replace/improve behaviour, don't redesign the contracts: the
  headers are frozen.
* Exercise: find the injection point for a new voice, a new groove behaviour and
  a new rhythmic interaction, and say which file owns each.

### 10:15 — Exercise 1: make a voice (60 min)

**Goal:** implement (or improve) one drum voice — suggested: CLAP, because it has
a clear, testable character (bursts + noise tail) and exercises `specialA`
(`DIFFUSION`) and `specialB` (`WIDTH`).

Contract: `src/dsp/voices/Voice.h`.

```cpp
class DrumVoice
{
public:
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void setParams (const VoiceParams& params) = 0;
    virtual void trigger (const VoiceTrigger& trigger) = 0;
    virtual void choke (float releaseSeconds) = 0;         // choke groups
    virtual void render (juce::AudioBuffer<float>& out, int startSample, int numSamples) = 0;
    virtual float getEnvelopeLevel() const = 0;            // GUI, 0..1
    virtual bool  isActive() const = 0;
};
```

Steps:

1. Read `VoiceManager.cpp` to see how voices are owned, triggered, choked
   (`closed hat → open hat`) and rendered with sample offsets.
2. Add your implementation file next to `src/dsp/voices/` (e.g.
   `ClapVoice.cpp`) and register it in `VoiceFactory.cpp`'s `createVoice()`
   switch. Files in `src/dsp/voices/` are globbed by CMake — no CMake edit.
3. Map the existing parameters: `pitchSemis`, `decay`, `tone`, `transient`,
   `drive`, `levelDb`, `pan`, `specialA`, `specialB`, plus stochastic depths
   (`randPitch`, `randLevel`, `randDecay`, `randTransient`) and `timingBiasMs`.
   Every one must do something audible; the GUI labels live in
   `src/core/Parameters.h` (`voiceSpecialAName`, `voiceSpecialBName`).
4. Voice character is *procedural*: build it from oscillators, noise, envelopes
   and simple filters — no samples, no file loading.
5. Determinism: use `trigger.seed` (and `klopgeest::rng` helpers in
   `src/core/Random.h`) for any per-hit variation. Same seed must reproduce the
   same hit.
6. Add checks to `tests/VoiceTests.cpp` (owned by the voice workstream — pair up
   if you do not own it): trigger produces finite non-silent output, decay
   affects tail length, choke silences the tail, level/pan affect the output,
   and zero allocations during `trigger()`/`render()`.
7. Audition through `klop_tool render` and via the plugin in a DAW.

**Maker's decisions:** attack shape, number of clap bursts, the noise/tone
balance, how much `DIFFUSION` smears the bursts, when the voice should choke.
**AI's scaffolding:** the class contract, the parameter plumbing, the allocation
test, the render harness.

### 11:15 — Exercise 2: add a modulation behaviour (60 min)

**Goal:** make one groove dimension react to a performance control in a way you
can defend musically.

Where: `src/sequencer/GrooveEngine.cpp` (implementation of `generate()`), reading
`EngineParams` (`global.humanize`, `global.stability`, `global.personality`,
`global.swing`, `global.accent`, `global.ghost`, and per-voice
`voice[].randTiming`, `voice[].timingBiasMs`, `voice[].randPitch`, …).

The contract from `GrooveEngine.h`:

```cpp
int generate (const PatternData& pattern,
              const EngineParams& params,
              long long tickIndex,
              double stepSamples,
              double nominalOffsetSamples,
              SequencedEvent* out,
              int maxOut);
```

Rules to keep:

* One call per absolute 16th-note grid tick, `tickIndex` is absolute.
* Stochastic decisions are pure functions of `(seed, voice, tickIndex, salt)` —
  use `rng::smoothBipolar()` for time-correlated movement
  (`src/core/Random.h`), never a stateful RNG.
* Anchors stay tighter than colour: `kVoiceRoleLoose` in `src/core/Constants.h`
  (kick 0.35 … odd 1.20).
* Deviations are clamped: the pulse must remain intelligible even in FERAL.
* Events carry their deviation in `VoiceTrigger` (`pitchSemis`, `decayScale`,
  `transientScale`, `toneOffset`, `driveScale`) and `SequencedEvent::timingDevMs`
  so the GUI can show *why* a hit sounds the way it does (`VisualState::HitFlag`
  has bits for `ghost`, `accent`, `ratchet`, `feral`, `mutation`, `live`).

Suggested behaviours to choose from (pick one, finish it):

* **Role-aware late hats**: in LOOSE/FERAL, closed hats drift later together
  (correlated over ~8 ticks) while kick/snare stay anchored.
* **Tempo-dependent lean**: at higher host BPM, reduce absolute timing deviation
  so the groove does not smear at 170 BPM.
* **Stability as a brake**: `stability` scales every stochastic depth toward 0
  (machine) or toward the parameter maximum (human). It must change *feel*, not
  level.
* **Personality as a decision, not a preset**: TIGHT clamps deviations hard,
  FERAL allows large slow drifts but still clamps single hits.

Proof:

* `tests/SequencerTests.cpp` (owner: sequencer workstream — coordinate):
  determinism, deviation bounds, role ordering, `stability = 1` ⇒ machine-like.
* Block-size invariance must still pass (`tests/QaIntegrationTests.cpp`).
* Render examples: TIGHT vs LOOSE vs FERAL at 120 BPM, same seed.

**Maker's decisions:** the size of the clamp, the correlation length, what
"FERAL but intelligible" means, whether the kick is allowed to move at all.
**AI's scaffolding:** the signature, the RNG primitives, the event structure and
the invariance tests.

### 12:15 — Group review (15 min)

Each pair states: what changed, what the test measures, and one phrase describing
the feel change. The group votes on which behaviour belongs in the instrument.
Anything that only "sounds better on my laptop" gets parked, not merged.

---

## 2. Afternoon (13:30–17:00): Interaction, evidence, judgement

### 13:30 — Exercise 3: add a rhythmic interaction (75 min)

**Goal:** make the pattern respond to itself — a call/response, a choke, a
ghost-note rule, a fill trigger — instead of eight independent lanes.

Choose one:

* **Ghost-note answer**: a kick hit on a step whose following snare is
  `probability < 1` triggers a low-velocity `VoicePerc` ghost in the gap, but
  only when `global.ghost > 0` and `personality != TIGHT`. Emit it from
  `GrooveEngine::generate()` with `HitFlag::ghost` and a reduced velocity;
  the GUI's honesty layer (`VisualState::lastFlags`) will show it.
* **Hat choke as rhythm**: closed hats choke the open hat (already wired in
  `VoiceManager::trigger`). Extend the groove engine so the choke *timing*
  interacts with swing instead of always using `0.015 s`.
* **Fill every N bars**: use `visual.currentTick` / the pattern's bar position to
  generate a ratchet fill (`VoiceTrigger` has no ratchet field, so emit multiple
  events per step with `HitFlag::ratchet`) every 8th bar in FERAL only.
* **Mutation-aware variation**: when `EngineParams.global.evolve` is true, seed
  the groove differently every 8-bar phrase (`MutationEngine` in
  `src/sequencer/MutationEngine.cpp` already mutates the pattern on the message
  thread; the audio-thread groove must remain deterministic).

Constraints:

* Interactions are generated on the audio thread by the sequencer/groove engine,
  never by the GUI.
* The pattern data remains the single source of truth; interactions are a
  function of pattern + parameters + tick, not extra mutable state.
* Every generated event keeps a truthful `HitFlag` and `timingDevMs` so the GUI
  can explain the groove (`src/core/VisualState.h`).

Proof:

* Add checks to `tests/SequencerTests.cpp` or `tests/QaIntegrationTests.cpp`
  (ask QA first): interaction fires only under the stated conditions; silent
  otherwise; deterministic across renders; still allocation-free; still
  block-size invariant.
* Render a 16-bar example and A/B it against the pre-change render.

### 14:45 — Listening test and "what AI did / what the maker decides" (45 min)

Listen together, blinded where possible: TIGHT / LOOSE / FERAL, swing low/high,
humanize low/high, before/after your change. Write down adjectives, not numbers.

Then fill a two-column board:

| What the AI did | What the maker decides |
| --- | --- |
| wired the contract (`DrumVoice`, `GrooveEngine::generate`) | whether the voice is a clap or something else |
| built the deterministic RNG and pure-function property | how loose "loose" is, and in which direction it leans |
| made the allocation counter prove the realtime contract | which shortcuts are worth the CPU |
| clamped deviations so nothing explodes | where the musical clamp should be |
| made state recall exact and fuzz-tested | which parameters deserve a performance macro |
| kept the build green while workstreams landed | when a behaviour feels finished |

The point: the machine can hold the invariants and generate candidates; the maker
chooses the musical truth. Tests measure bounds and determinism, not groove.

### 15:30 — Packaging the change (60 min)

1. Run the full suite: `./build/klop_tool_artefacts/Release/klop_tool test`.
2. Run `./scripts/dev-build.sh` (all targets) and then `./scripts/validate.sh`.
   If auval/pluginval cannot run in the room, mark them SKIP and say so.
3. Render the demo: `./build/klop_tool_artefacts/Release/klop_tool render Demo/Klopgeest-Demo.wav`.
4. Snapshot the editor if the change is visible: `klop_tool snapshot docs/ui-preview.png`.
5. Fill in the workshop log (below) and present a 3-minute before/after.

### 16:30 — Show and tell, and what lands (30 min)

Each pair presents: one sentence of intent, one measured property, one audio
example. The group decides what goes into the instrument. Rejected ideas are kept
in the workshop log as candidates for another day — not merged as half-behaviours.

---

## 3. Workshop log (fill in per participant)

| Field | Entry |
| --- | --- |
| Name / pair | |
| Exercise | 1 voice / 2 modulation / 3 interaction |
| Files touched | |
| Contract respected (no audio-thread allocation, deterministic, no src/CMake edits outside own area) | yes / no |
| Test added | |
| Test result (`checks, failures, skipped`) | |
| Demo render peak / audible? | |
| Musical intent (one sentence) | |
| What the maker decided | |
| Landed? | yes / parked |

## 4. Do / don't

**Do**

* Keep changes inside the workstream you own; ask the owner before editing
  `src/dsp/voices/*`, `src/sequencer/*`, `src/dsp/master/*`, `src/ui/*`,
  `src/plugin/*`, `tests/VoiceTests.*`, `tests/SequencerTests.*`,
  `tests/MasterTests.*`.
* Prove realtime safety with `tests/AllocationCounter.*`, not by inspection.
* Use `rng::*` helpers with explicit seeds; replay the same render twice.
* Measure one thing per exercise. A finished small behaviour beats three
  half-tuned ones.
* Render every claim.

**Don't**

* Don't touch `CMakeLists.txt`, `scripts/build.sh`, `scripts/dev-build.sh`,
  `README.md`, or another workstream's source/test files.
* Don't add heap allocation, locks, `std::function`, `juce::String`, file I/O or
  logging to the audio path.
* Don't introduce hidden state or use the system RNG/clock in the groove engine.
* Don't "fix" a test to match the code; fix the code or mark the test SKIP with
  a reason and tell QA.
* Don't claim validation for steps that were skipped (auval, pluginval, DAW).

## 5. Follow-up reading

* `docs/TESTING.md` — the test strategy and how to add a suite.
* `docs/VALIDATION.md` — the release gate and its evidence table.
* `src/sequencer/MutationEngine.h` — message-thread pattern mutation (nudge,
  mutate, recall).
* `src/core/VisualState.h` — how the instrument explains itself to the GUI.
