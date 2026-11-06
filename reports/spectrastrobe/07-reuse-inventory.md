# 07 — What exists in NAGLOED, and what is worth taking

**What this is.** An inventory of the SpectraStrobe work that already exists in the predecessor
project, with a verdict on each artefact. It exists so that nobody re-derives what is already there,
and so that nobody copies something that only worked because of a different instrument's programme
material.

**Source:** NAGLOED, branch `feature/v2-exploration`, commit
`862dec2aa5143d2e8a7ee8b8349f5ffeabb36d0d`. Not in that repository's working tree — extract with
`git archive` or `git show`. Full audit: [`evidence/artefact-inventory.md`](evidence/artefact-inventory.md).

**All artefacts are GPL-3.0-or-later, same copyright holder, no third-party code.** Licence is not an
obstacle; the project's own independence statement is (see §5).

---

## 1. The artefacts

| Path (in NAGLOED) | Lines | What it is | Verdict |
|---|---|---|---|
| `experiments/spectrastrobe-poc/render.py` | 851 | The **reference encoder**. Carriers, both pan laws, colour mapping, clamp, envelopes, programme chain, WAV export. | **TAKE** — copied to `reference/render.py`. The algorithm is described in [`evidence/reference-implementation.md`](evidence/reference-implementation.md). |
| `experiments/spectrastrobe-poc/verify.py` | 1679 | The **measurement suite**: M1–M14, FFT/Goertzel/true-peak, band isolation, LFO metrics, determinism. | **TAKE WITH CORRECTIONS** — copied to `reference/verify.py`; four measurement defects are catalogued in [`05`](05-acceptance-and-tests.md) §2.3. |
| `experiments/spectrastrobe-poc/score.json` | 180 | A declarative score for a 72 s art piece, with four exposure variants. | **READ ONLY** — a worked example of what a score looks like. The piece itself is not ours. |
| `experiments/spectrastrobe-poc/calibration/render_calibration.py` | 295 | Renders a nine-state colour sequence (R/G/B/Y/C/M/W) at fixed ratios. | **TAKE THE IDEA.** This is exactly the stimulus `06` T1 needs. Port the *sequence definition*, not the code. |
| `calibration/verify_calibration.py` | 829 | Verifier for the colour sequence, including **transition checks**: max 10 ms envelope change, black-gap detection, fade rise/fall. | **TAKE THE TRANSITION CHECKS** — the only implemented proxy for a flash budget anywhere in the inherited work. Directly reusable for B1/B6 in [`05`](05-acceptance-and-tests.md) §3. |
| `calibration/ab/render_ab.py` + `ab.json` | 449 + 750 | A controlled A/B ladder: reference fixed at −36 dBFS, colour level varied from −54 to −24 dBFS, three transition shapes, an L/R control. | **TAKE THE DESIGN, NOT THE FILES.** The ladder design is directly reusable for KLOPGEEST's T1. The rendered variants are diagnostic stimuli for a different question. |
| `calibration/ab/verify_ab.py` | 1479 | A/B verifier. **Fixes the M5 demodulator defect** with a 10 ms Blackman-Harris window. | **TAKE THE DEMODULATOR.** Copy this measurement approach, not `verify.py`'s. |
| `calibration/RATIO-RESEARCH.md` | 508 | The analysis of the reference/colour ratio conflict and the three-level model (digital ratio / hardware output / perception). | **READ** — the reasoning that [`01`](01-signal-spec.md) §3 condenses, and the correct framing for the hardware session. |
| `docs/v2/SPECTRASTROBE.md` | 440 | Signal model, measurement plan M1–M14, hardware protocol, encoder requirements. | **READ** — the ancestor of `01` and `05`. |
| `docs/v2/SAFETY-PRIVACY.md` | 367 | Safety rules R1–R12 with citations. | **READ** — the ancestor of `04`. The privacy half is not relevant to KLOPGEEST. |
| `docs/v2/TECHNICAL-UNCERTAINTIES.md` | 149 | A register of open questions with owners. | **READ** — a good model for the uncertainty register in the main report. |
| `experiments/spectrastrobe-poc/HARDWARE-PROTOCOL.md` | 156 | The executable hardware protocol. | **READ** — the ancestor of `06`. |
| `out/` directories | — | Rendered WAVs and reports (git-ignored upstream). | **REFERENCE ONLY** — the measured numbers are quoted in the main report. Do not vendor the WAVs. |

---

## 2. What ran, and what it proved

Run on 2026-11-06, Python 3.14.5, nothing installed:

| Suite | Result | Wall clock |
|---|---|---|
| `run.sh` (encode + verify) | **52 PASS, 0 FAIL, 1 INFO, 0 SKIP** of 53 core checks; corruption matrix 15 rows (9 PASS, 5 EXPECTED-DEGRADED, 1 INFO) | 64.9 s |
| `calibration/run_calibration.sh` | **41/41 PASS** | 3.8 s |
| `calibration/ab/run_ab.sh` | **117/117 PASS** | 13.3 s |

Determinism: M10 PASS, independently confirmed by re-rendering into a third directory — all six WAVs
SHA-256 identical. This is a **per-platform** guarantee (it depends on CPython's `round()` and the
system libm); it is not a cross-machine hash contract.

The single non-PASS row is a deliberate INFO: broadband band-limiting "not demonstrated" — the
verifier declining to claim a pass it cannot support. That is the correct behaviour and worth
imitating.

---

## 3. What is genuinely reusable, in priority order

1. **The encoder algorithm** — carriers, the two different pan laws, `colourAmp = ratio × reference
   peak`, clamp placement, smoothstep envelopes, injection order. Described language-neutrally in
   [`evidence/reference-implementation.md`](evidence/reference-implementation.md) and summarised
   normatively in [`01`](01-signal-spec.md).
2. **The transition checks** — the only implemented flash-budget proxies.
3. **The A/B ladder design** — the correct way to find a usable brightness level on real hardware.
4. **The measurement conventions** — with the four corrections in [`05`](05-acceptance-and-tests.md) §2.3.
5. **The three-level model** (digital ratio / hardware output / perception) — the discipline that keeps
   a file measurement from being mistaken for a perceptual claim.

## 4. What must not be reused

| Artefact | Why |
|---|---|
| The programme band-limiting: 8th-order Butterworth at 15.5 kHz | Measures only **−19 dB at 17 kHz**, −34 dB at 18.2, −56 dB at 19.7 on broadband material. It passes upstream only because that programme is sparse sub-1.2 kHz synthesis with 90 % silence. **A drum machine will leak into the control band with this filter.** |
| `verify.py`'s M5 LFO demodulator and its thresholds | A 1 ms rectangular demodulator cannot separate carriers 500 Hz apart; measured, a 25 % colour drags the "off-phase rejection" metric to −21.5 dB and would FAIL a perfect signal. |
| `verify.py`'s M3 ratio denominator | Zero-pads past end-of-file; measured, a 25 % colour reports as 663 % on a 2 s vector. |
| The art-piece score rules (L1/L2/L3/L4 assertions, the 60–90 s duration gate) | Specific to a different composition. |
| Colour phase reset at every event | The reference encoder resets; the calibration renderers correctly keep the phase continuous. Keep it continuous. |
| Count-length precomputed envelope arrays | Python convenience. A C++ audio thread may not allocate. |
| The ffmpeg corruption matrix as a CI gate | Useful as a manual diagnostic; too slow and too environment-dependent to gate a build. |

---

## 5. The licence and independence question — resolved

The artefacts are GPL-3.0-or-later with the same copyright holder as KLOPGEEST, so **copying is
licence-compatible**.

But `LICENSE` (line 16) and `README.md` (line 9) stated that KLOPGEEST is an independent project that
**shares no code with NAGLOED**. Vendoring the reference under `reports/spectrastrobe/reference/`
made that inaccurate.

**Maintainer's decision, 2026-11-06: keep the copy and amend both statements honestly.** Both files
now distinguish *compiled into the instrument* (unchanged — still fully independent) from *vendored as
a research test oracle* (this directory), and point at
[`reference/PROVENANCE.md`](reference/PROVENANCE.md) for the exact commit and checksums.

Reversing it is one step: delete the `reference/` directory and restore the two sentences from git
history. The research package stands without it — the algorithm description and golden-vector plan
live in [`evidence/reference-implementation.md`](evidence/reference-implementation.md).

---

## 6. Adjacent work that exists and was not researched

- NAGLOED issue #14 (the *Geborgen Geluid* concept) and issue #31 (the fade-to-black investigation) —
  read for context, not researched.
- `docs/v2/EXPERIMENT-01.md` — the composition *Eerste Gloed*. A worked example of a light score with
  delay, unanswered events and a reversed memory return. **Worth reading before designing DRIFT.**
- `docs/v2/ARCHITECTURE-OPTIONS.md` — broader architectural framing for that instrument; not
  applicable here.
