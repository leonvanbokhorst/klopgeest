# KLOPGEEST — design decisions and tradeoffs

This document records what we chose, why, and what we deliberately did not do.
It is as much a workshop artefact as a release note.

## 1. Instrument first, architecture second

The brief asks for something that grooves. Every decision below was tested
against one question: does it make the instrument more playable *now*?

Milestone 1 was planned around four voices, but the architecture (eight lanes,
eight parameter groups, eight voices in the mixer) was fixed from day one and
the voice workstream was able to deliver all eight. Shipping the full kit in M1
was a scope increase in service of the primary goal: a drum machine with only
four voices is a demo, not an instrument. The milestone issue list records what
is still missing.

## 2. Procedural synthesis, no samples

Samples would have bought instant "analog" credibility at the cost of tuning,
licensing, memory, and the instrument's identity. Procedural voices let every
control (pitch, decay, transient, tone, drive, character) act on the synthesis
itself instead of retriggering a fixed recording. User samples remain a possible
later feature; the brief explicitly allows them later.

Tradeoff: more DSP work and a risk of thinness. Mitigation: each voice has a
saturated body/transient layer and the glue machine adds bus cohesion.

## 3. Stateless, hash-based groove

The groove engine computes every deviation as a pure function of
`(seed, voice, tickIndex, salt)`. We rejected a stateful random-walk approach
despite its natural "human" feel, because:

- block-size changes must not alter the performance (host buffer sizes vary);
- host seeks and loops must be reproducible;
- deterministic tests become trivial;
- reproducibility across machines is a workshop requirement.

Correlation is achieved with value noise over tick index (interpolated hashes
with a period of several ticks), which behaves like a slow random walk without
hidden state. The tradeoff is that correlations are periodic rather than truly
open-ended; with 4–12 tick periods and per-voice channels, this is musically
invisible and keeps FERAL bounded.

## 4. Sample-accurate scheduling by look-ahead windows

Instead of a per-block step counter (which drifts and complicates seeks), ticks
are derived from transport ppq. Each block scans a window wide enough to contain
every possible groove deviation, then filters events by sample offset. This is
slightly more work per block but removes an entire class of bugs: no double
triggers at block boundaries, no drift, correct behaviour under loops and tempo
changes.

Ratchet hits are emitted as separate events at sub-step offsets; the voice
renderer splits the block at event boundaries so each hit starts on its exact
sample.

## 5. Lock-free pattern handoff

The GUI edits a `PatternData` copy and publishes it through a bounded
`juce::AbstractFifo` of trivially-copyable snapshots. This avoids locks and
allocation on the audio thread and makes edits atomic with respect to a block.
The cost is a small memcpy per change (pattern ≈ 8 KB) and a fixed queue; edits
are human-speed, so the queue never fills in practice. If it did, the newest
snapshot would win on the next edit.

## 6. Zero-lookahead glue machine

The compressor/limiter is designed with no lookahead so the plug-in reports zero
latency and tracks the host sample-accurately. A lookahead limiter would catch
peaks more gracefully, but the loss of sample-aligned performance and the extra
host compensation are not worth it for a drum bus; a fast-attack soft limiter
plus saturation and output trim keeps peaks in check. Auto-release and sidechain
HPF do the musical work.

## 7. One master bus, two macros

GLUE and HEAT are not separate processors: they are macro mappings onto the same
compressor and saturation. This keeps the control surface small and the
behaviour predictable, and prevents "macro spaghetti". The individual parameters
remain available for people who want to dial the machine by hand; the macros
move them in musical ranges rather than overriding them.

## 8. Eight voices with per-voice humanisation

Each voice exposes five randomisation depths (pitch, level, decay, transient,
timing) rather than one global "humanise" knob, and the groove engine multiplies
those by global HUMANIZE, STABILITY and the personality. This satisfies the
brief's per-voice requirement without a second page of global randomness
controls. Kick and snare are additionally weighted as anchors by the groove
engine regardless of their per-voice amounts.

## 9. Mutation as edit history, not regeneration

MUTATE edits the current pattern (adds ghosts, redistributes accents, displaces
one non-anchor hit, occasionally drops one) and pushes the previous state onto a
bounded history for RECALL. It never regenerates the rhythm from scratch, so the
identity stays recognisable and the musician can reason about what changed.
EVOLVE periodically applies a NUDGE-sized edit while locked steps and lanes are
protected.

Tradeoff: mutation is local, so long EVOLVE sessions can drift into a different
pocket. RECALL and the pattern editor are the answer; the instrument keeps the
musician in control.

## 10. UI: instrument, not spreadsheet

Dark industrial panel, amber for things the musician set, cyan for things that
are moving. The step grid stays visible; voice/groove/glue controls live in a
bottom strip. The UI shows telemetry (timing deviation, velocity, pitch
deviation, decay scale, flags, gain reduction) because the brief asks the
instrument to explain *why* a hit sounds different — otherwise controlled
instability becomes mysticism.

## 11. Scope limits, stated honestly

- macOS only (AU/VST3/Standalone), arm64-only local build; no universal binary
  or notarisation yet.
- No user sample loading, no MIDI output, no MPE, no host automation of pattern
  data (parameters are automatable; steps are session state).
- No cloud, accounts, telemetry or external services.
- REAPER and Ableton Live 12 were installed on the build machine, but automated
  validation was only performed with `auval` and `pluginval`; manual host
  sessions are listed as not performed in `docs/VALIDATION.md`.
- DRIFT, BREATH, CONVERSATION, SCARS and FILL ME are deliberately deferred until
  the core instrument is stable; they are tracked as issues rather than shipped
  as half-features.
