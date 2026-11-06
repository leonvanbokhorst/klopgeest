# SpectraStrobe research pack

**Answering [issue #6](https://github.com/leonvanbokhorst/klopgeest/issues/6) — *Explore SpectraStrobe as
a rhythmic light representation*.**

Research only. No `src/`, `tests/`, `CMakeLists.txt`, `docs/` or `tools/` file was modified, and
nothing was built. Everything here is input for whoever implements the feature.

---

## Read this in order

| # | Document | Read it if you are… |
|---|---|---|
| — | [`../2026-11-06-spectrastrobe-light-engine.md`](../2026-11-06-spectrastrobe-light-engine.md) | …anyone. The answer, the status of every proposition, and the evidence trail. |
| 01 | [`01-signal-spec.md`](01-signal-spec.md) | …writing the **encoder**. Frequencies, levels, the ratio law, headroom, filter, export rules. |
| 02 | [`02-integration.md`](02-integration.md) | …touching the **engine**. Injection point, event data, ten missing hooks, ten traps. |
| 03 | [`03-light-engine-design.md`](03-light-engine-design.md) | …designing the **mappings**. PULSE / COUNTERPOINT / DRIFT with numbers, the governor, the macros. |
| 04 | [`04-safety-and-opt-in.md`](04-safety-and-opt-in.md) | …implementing **opt-in, panic or user-facing text**. Binding. |
| 05 | [`05-acceptance-and-tests.md`](05-acceptance-and-tests.md) | …writing **tests**. Golden vectors with measured expected values, the safety budget, CI shape. |
| 06 | [`06-hardware-validation.md`](06-hardware-validation.md) | …running the **Kasina session**. The only stage that can answer the actual question. |
| 07 | [`07-reuse-inventory.md`](07-reuse-inventory.md) | …deciding what to **copy from NAGLOED**, and what not to. |

## Evidence

The five full research reports this pack condenses. They are the source of record; where a summary and
an evidence document disagree, the evidence document wins.

| File | What it establishes |
|---|---|
| [`evidence/source-verification.md`](evidence/source-verification.md) | 803 lines, 30 URLs, verbatim quotes. Every SpectraStrobe format fact, with a verdict per fact and the 1:1 vs 4:1 conflict worked through. |
| [`evidence/integration-map.md`](evidence/integration-map.md) | 937 lines with `file:line` on every claim. The KLOPGEEST code surface: events, groove, mutation, params, routing, realtime rules, traps. |
| [`evidence/reference-implementation.md`](evidence/reference-implementation.md) | 666 lines. The inherited encoder, audited line by line, plus the golden-vector plan with measured expected values. |
| [`evidence/measurement-plan.md`](evidence/measurement-plan.md) | 308 lines. M1–M14 as coded vs as documented, twelve discrepancies, keep/adapt/drop per check. |
| [`evidence/artefact-inventory.md`](evidence/artefact-inventory.md) | 243 lines. Every inherited file, its licence, and what it is worth. |

## Reference implementation

[`reference/`](reference/) contains a pinned, byte-identical copy of the predecessor project's Python
encoder and verifier, as a differential oracle for the C++ implementation. **Read
[`reference/PROVENANCE.md`](reference/PROVENANCE.md) before relying on it** — it records the exact
commit, the checksums, and the maintainer's decision on this repository's independence statement
(the copy is a research oracle only; nothing from it is compiled into the instrument).

---

## One quantity to fix in your head first

Three numbers are used throughout and they are **not** interchangeable — conflating them is a 12 dB
error. [`01-signal-spec.md`](01-signal-spec.md) §3 defines them precisely; in short:

- **`r`** — colour amplitude ÷ reference amplitude. **The canonical internal quantity.**
- **`f`** — fraction of *format* full brightness, `f = r/4`.
- **dBFS** — only meaningful once the reference level is fixed (this pack uses −36 dBFS).

| | `r` | `f` | dBFS @ ref −36 |
|---|---|---|---|
| **The product cap** | **0.25** | **6.25 %** | **−48.0** |
| Format full brightness | 4.0 | 100 % | −24.0 |

`lightIntensity = 1.0` means **the cap**, not full brightness.

## The three things to take away

1. **The ratio is 4:1, not 1:1.** A colour carrier at full brightness sits **+12 dB above** the
   reference, not level with it. Reference −36 dBFS, colour −24 dBFS. Get this wrong and every colour
   is 12 dB too dim — which is the most likely explanation for the "no convincing green or yellow"
   observation from the one real hardware session.

2. **Carriers go after the glue machine, and nothing goes after the carriers.** KLOPGEEST's master bus
   is a compressor, a saturator and a hard clamp at a ceiling that can reach 0 dBFS. Carriers that
   enter it are pumped by the drums and clipped flat. This one rule decides the architecture.

3. **Light follows the beat, never the subdivision.** A drum machine flashes 8–13 times a second if
   you let it. The physics of the format (500 Hz channel spacing) and the safety evidence (3–30 Hz
   flicker band) both forbid it, and the artistic brief asks for the opposite anyway. **Luminance is
   slow; colour and position carry the detail.** That is not a compromise — it is the design.

## The one thing this pack cannot tell you

Whether it works as a *rhythmic representation* rather than a visualisation. That is criterion C5/D8
in [`05`](05-acceptance-and-tests.md) and [`06`](06-hardware-validation.md) §5, and it needs a build,
a Kasina, and a person in a room. Nothing here substitutes for it.
