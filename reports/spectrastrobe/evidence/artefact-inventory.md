> **Provenance.** One of five evidence reports produced for issue #6 on 2026-11-06 by a lead researcher working with three parallel sub-investigations. Reproduced here at full length and essentially verbatim — it is the source of record for the summaries in `../01`–`../07`, and where a summary and this document disagree, this document wins.
>

# NAGLOED SpectraStrobe artefacts — inventory, licence, and run report

What the prior NAGLOED work contains, what is worth taking into KLOPGEEST, what must not be
taken, and what actually happened when it was run.

| | |
|---|---|
| Source repo | `leonvanbokhorst/granular-experimental-plugin` (NAGLOED) |
| Branch | `feature/v2-exploration`, HEAD `862dec2` ("docs(calibration): annotate multi-colour activity for issue #31") |
| Extraction | `git archive feature/v2-exploration experiments/spectrastrobe-poc docs/v2 \| tar -x -C /tmp/ss-work` — **read-only**; the NAGLOED working tree (on branch `feature/33-gloop`) was never checked out, modified or committed to |
| Total audited code | 5 582 lines of Python across 6 files, plus 5 JSON score/definition files and 8 Markdown documents |
| Run date / host | 2026-11-06 · macOS (darwin) · CPython **3.14.5** · `ffmpeg` present at `/opt/homebrew/bin/ffmpeg` · 14 cores · stdlib only, nothing installed |

---

## 1. File-by-file inventory

`sha` = first 16 hex of the SHA-256 of the extracted file. "Copy?" is a recommendation for
KLOPGEEST, not a statement about NAGLOED's own use.

### 1.1 The encoder and its verifier (`experiments/spectrastrobe-poc/`)

| File | Size / lines | sha | What it is | Copy? |
|---|---|---|---|---|
| `render.py` | 38 207 B / 851 | `541da4ac2b0663f5` | The reference encoder: score validation, per-event programme synthesis (additive sweep + biquad-filtered noise), 8th-order Butterworth band-limit, equal-power program pan, the four SpectraStrobe carriers with the 20 Hz pan LFO, breakpoint envelopes, track fades, the 25 % clamp, PCM export, manifest, CSV/SVG timeline | **Yes — as reference/port source.** Not as shipped code: it is an offline batch renderer with `count`-length arrays and 4 trig calls/sample. Port §2 of `reference-implementation.md`. |
| `verify.py` | 86 298 B / 1 679 | `2133c56efbb383ce` | The measurement library and M1–M10/M14 harness: hand-written radix-2 FFT, Hann window, band RMS, single-bin complex demodulator, LFO metrics, a local true-peak estimator, the score-rule group, the safety-clamp probe, the brightness ladder, determinism + manifest checks, the ffmpeg corruption matrix, JSON/Markdown reporting | **Yes — selectively.** The primitives are the most reusable thing in the whole corpus; the score-rule group and the corruption matrix are not. See `measurement-plan.md` §5. |
| `score.json` | 8 246 B / 180 | `e274542fc46fde76` | The *Eerste Gloed* score: 4 light events, 3 programme events, 3 exposures, envelope rules, carrier constants, verification windows | **Only the `carriers` block** as a constants reference. The piece itself is NAGLOED art. |
| `run.sh` | 633 B | `b1ff643648b7746b` | render → verify, `set -euo pipefail`, exit code = verifier verdict | Pattern only. |
| `README.md` | 10 349 B | `3295a39ca4993fa1` | Scope, artistic premise, parameter table, what the test does and does not prove, licence line | **Yes — as the model for KLOPGEEST's own scope statement.** It is the most carefully worded "what this does not prove" text in the corpus. |
| `HARDWARE-PROTOCOL.md` | 8 567 B | `6e5575cece96e0a8` | 156-line human hardware protocol for Kasina + DeepVision: consent, photosensitivity, fixed LED brightness, the T0–T7 ladder, stop criteria | **Only if KLOPGEEST ever drives light hardware.** Otherwise archive. |
| `.gitignore` | — | — | `out/`, `__pycache__/` — generated WAVs and reports are not committed | **Yes**, the convention. |

### 1.2 The colour calibration diagnostic (`calibration/`)

| File | Size / lines | sha | What it is | Copy? |
|---|---|---|---|---|
| `render_calibration.py` | 13 159 B / 295 | `63c685c2a58154f0` | Renders a 9-state colour sequence (R/G/B/Y/C/M/W/R/G): 300 ms **smoothstep** global ramp, 1000 ms states, 250 ms smoothstep crossfades centred on the boundaries, 6.25 % per carrier, continuous colour phase | **Yes — the crossfade machinery.** It is a cleaner, better-parameterised carrier renderer than `render.py` for *test signals*. |
| `verify_calibration.py` | 43 518 B / 829 | `cb167907b70b8c04` | Adds the 4-term Blackman-Harris 10 ms demodulator and the transition checks: crossfade overshoot, per-10 ms abruptness, black-gap duration, 10→90 % fade times, plus a per-state L/R eye check | **Yes — this is the most directly reusable test file.** |
| `calibration.json` | 8 875 B | `92ee5533a89f58f2` | 9 states + 6 L/R states with explicit crossfade and measurement windows | **Yes — as a template** for a declarative test-stimulus format. |
| `CALIBRATION.md` | 10 824 B | `74cc698c54ee3466` | Why the diagnostic exists, the state table, separation of signal vs hardware vs perception | Partly (the three-layer separation). |
| `RATIO-RESEARCH.md` | 32 884 B / 508 | `d31702d0772fa811` | The issue-#31 research note: the 1:1 versus 4:1 colour/reference conflict, three layers (file / hardware / perception), an A/B ladder design, a decision matrix | **Yes — required reading** before fixing a colour/reference constant. §4 is the evidence table. |
| `run_calibration.sh` | 764 B | — | render → verify | Pattern. |

### 1.3 The A/B diagnostic (`calibration/ab/`)

| File | Size / lines | sha | What it is | Copy? |
|---|---|---|---|---|
| `render_ab.py` | 20 386 B / 449 | `13aea50765ba728b` | 10 variants at absolute colour levels −54.1 … −24 dBFS, reference fixed at −36 dBFS, in two transition shapes (`direct` = linear sum, `overlap` = hold-both) plus a matched L/R alternation set | **Yes — the two-shape switch** (`render_ab.py:114-125`) is a genuinely useful test primitive. |
| `verify_ab.py` | 78 992 B / 1 479 | `bdb92ff0c89213c8` | Per-variant absolute level/ratio/isolation, Blackman-Harris LFO with a **−20 dB** residual criterion, exposure classification, transition-midpoint measurement, provenance against a committed baseline, multi-colour CSV/report annotation | **Yes — the transition-midpoint and provenance checks especially.** |
| `ab.json` | 23 112 B / 750 | `c1420ee6fcc85568` | The 10 variants + the owner correction of 2026-11-05 on what the 25 % ceiling means (`:741-749`) | Reference only. The levels are diagnostic, explicitly not artistic settings. |
| `provenance-baseline.json` | 1 434 B | — | SHA-256 of the previously rendered WAVs, so a re-render must reproduce them | **Yes — the mechanism.** |
| `AB-PROTOCOL.md` | 23 865 B / 395 | `06762264d59076cd` | The hardware A/B protocol, including the corrected ceiling interpretation | Archive unless hardware is in scope. |
| `run_ab.sh` | 815 B | — | render → verify | Pattern. |

### 1.4 Documents (`docs/v2/`)

| File | Size / lines | sha | Copy? |
|---|---|---|---|
| `SPECTRASTROBE.md` | 440 lines | `883ba37b2069bb18` | **Yes.** §1 is the format model, §2 the threat matrix, §3 the measurement plan, §6 the encoder requirements. This is the specification KLOPGEEST should adapt. |
| `SAFETY-PRIVACY.md` | 367 lines | `75c9e00a899a1c07` | **Yes, adapted.** R1–R12 (envelope, ceiling, flash-rate, stop). Note R2 and the ≥50 ms rule are *not implemented* anywhere in the code — see `measurement-plan.md` §4. |
| `TECHNICAL-UNCERTAINTIES.md` | 149 lines | `9d2accab1e600b76` | **Yes, partially.** U12 (the carriers cannot survive a DSP chain with LPF/saturation/limiter) and U15 (carriers are not guaranteed inaudible) are the two that matter for a drum machine. |
| `ARCHITECTURE-OPTIONS.md`, `EXPERIMENT-01.md`, `ARTISTIC-VISION.md`, `DISTRIBUTED.md`, `IDEAS.md`, `REVIEWS.md`, `README.md`, `reviews/*` | — | — | No. NAGLOED v2 product/artistic material; out of scope for KLOPGEEST. |

### 1.5 Not in the branch, and why it matters

* **No C++ of any kind.** The SpectraStrobe work was never integrated into NAGLOED's plug-in
  (`README.md:5-7`: "not integrated into the plug-in, changes no `src/`, CMake, presets"). There is
  no prior art for an in-plugin encoder, no DSP contract, no realtime path. KLOPGEEST starts from the
  algorithm, not from an existing implementation.
* **No hardware measurement.** M11–M13 (Kasina detection, photodiode LED output, stop latency) were
  never executed. `verify.py:1440`, `1449`, `1473` and every scope statement say so.
* **No event engine / scheduler.** `score.json` is a declarative score executed by a batch renderer;
  nothing in the corpus schedules light in realtime.

---

## 2. Licence

### 2.1 NAGLOED

* `LICENSE` (692 lines) = the verbatim GNU GPL v3 text, preceded by a 6-line header:
  "NAGLOED — granular afterglow instrument / Copyright (c) 2026 Master Lonn & Little Wan (Leon van
  Bokhorst, Little Wan) / … This program is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License … either version 3 … or (at your option) any
  later version."
* `README.md:213-221`: **"NAGLOED's own source is free software under the GNU General Public
  License, version 3 or later (GPL-3.0-or-later)"**, and JUCE is licensed separately.
* The proof-of-concept `README.md:190`: **"Licence: GPL-3.0-or-later, same as NAGLOED."**

### 2.2 KLOPGEEST

* `LICENSE:1-16`: same copyright line, same GPL-3.0-or-later grant.
* `README.md:208`: "JUCE 9.0.5 (see JUCE licensing). GPL-3.0-or-later".

### 2.3 Are the extracted files actually GPL, and is anything third-party?

* **No per-file licence headers, no SPDX identifiers** in any of the 6 Python files, 3 shell
  scripts or 5 JSON files (grep for `GPL|Copyright|SPDX|licen` over the extracted tree: zero hits in
  code). The licence is repository-level only, plus the one line in the PoC README.
* **No vendored third-party code.** The only third-party artefact referenced is **SpiraStrober
  (MIT)** — cited as prior art in `README.md:187` and `docs/v2/SPECTRASTROBE.md:429`, *not* copied.
  Its MIT terms therefore do not attach to anything here.
* **No JUCE code** is involved; the JUCE 9 EULA statement in NAGLOED's README concerns the plug-in,
  not these scripts.
* The FFT (`verify.py:141-165`), the Goertzel (`301-314`), the biquads (`render.py:195-229`), the
  WAV writer (`render.py:449-489`) and the true-peak estimator (`verify.py:370-401`) are all written
  from scratch in stdlib Python — none are copy-pasted library code with a different licence.

**Verdict: every artefact listed in §1 is GPL-3.0-or-later, held by the same copyright holder as
KLOPGEEST. Copying any of it into KLOPGEEST is licence-compatible and creates no new attribution
obligation beyond the GPL that KLOPGEEST already carries.** There is no non-GPL file to isolate.

### 2.4 The one real problem: KLOPGEEST currently claims the opposite

KLOPGEEST's own front matter states three times that no NAGLOED code is present:

| Where | Text |
|---|---|
| `LICENSE:15-16` | "KLOPGEEST is an independent project inspired by lessons from NAGLOED; **no NAGLOED source is included here**." |
| `README.md:8-9` | "an independent project, informed by lessons from NAGLOED but **sharing no code with it**." |
| `README.md:209` | "KLOPGEEST is an independent project; NAGLOED is untouched." (this one stays true — it is about not modifying NAGLOED) |

Copying `render.py` / `verify.py` (or a port of them) makes the first two statements false.
**Whichever route KLOPGEEST takes, it must update `LICENSE:15-16` and `README.md:8-9` in the same
change that copies the code**, or the repository misdescribes its own provenance. Two consistent
options:

1. **Port with attribution** (recommended if any structure is copied): keep a
   `tools/spectrastrobe/reference/` directory with the GPL originals, add a `PROVENANCE` note naming
   NAGLOED `feature/v2-exploration` @ `862dec2` and the sha256s, and rewrite the two sentences to say
   that ported reference material originates in NAGLOED under the same licence.
2. **Clean-room re-derivation** from the algorithm description only (`reference-implementation.md`):
   then the "no source included" claim can survive, but the notes should still cite NAGLOED as the
   origin of the description, and the golden-vector generator in §8 of that report would have to be
   re-implemented rather than imported.

Since both projects are GPL-3.0-or-later and share a copyright holder, option 1 carries no legal
risk and much less engineering risk; only the two sentences need to change.

---

## 3. Measurement claim versus design choice — what the artefacts actually establish

| Claim in the corpus | Status | Evidence |
|---|---|---|
| The four carriers are at the right frequencies and the reference is at −30 dBFS peak | **Established** for the rendered files | M1/M2 `[measured]` 18 199.99 … 19 699.99 Hz, −29.96 dBFS |
| Colour amplitude follows the score within 0.5 dB and is clamped at 25 % per carrier | **Established** for this encoder | M3/SA `[measured]` 0.00 dB worst deviation, 24.44 % measured under an adversarial 0.90 request |
| Two renders with the same seed are byte-identical | **Established on this platform** | M10 `[measured]`, plus an independent re-render (§4.3) |
| The programme is band-limited enough for *this* material | **Established for this material only** | M7 `[measured]` −158 dBFS at 48 k; the broadband probe says −19.2 dB at 17 kHz and is labelled INFO |
| The 20 Hz pan LFO is present with 100 % depth | **Established when no colour is on** | M5 `[measured]`; with a 25 % colour present the same metric reads 97.2 % / −21.5 dB |
| Colour brightness at "full" is 1:1 or 4:1 with the reference | **Not established** — conflicting primary sources | `RATIO-RESEARCH.md:188-196` |
| The 25 % ceiling is a safety threshold | **Design choice, explicitly not a proven threshold** | `ab.json:737`, `741-749` |
| The Kasina decodes the signal and shows the intended colours | **Not measured** (M11–M13) | `verify.py:1440`, `1449`, `1473` |
| The carriers are inaudible | **Open assumption** (U15), age/hearing/transducer dependent | `TECHNICAL-UNCERTAINTIES.md` U15 |
| ≥ 20 ms attack/release, no step from black | **Enforced in code** for envelopes | `render.py:161-177` |
| ≤ 3 flashes/s, no 3–30 Hz modulation (R2) | **Documented, not implemented** | absent from all 5 scripts |
| Stop within 50 ms | **Not implemented, not measured** — the offline WAV has no panic stop | `README.md:21-23` |

---

## 4. Run report — it runs, and it is green

### 4.1 Environment

`python3 --version` → **Python 3.14.5**. `which ffmpeg` → `/opt/homebrew/bin/ffmpeg` (so the M14
corruption matrix executed; if it had been absent, the six ffmpeg rows would have been marked SKIP,
`verify.py:1325-1333`). Nothing was installed. All runs used the extracted copy in `/tmp/ss-work`,
never the NAGLOED working tree.

### 4.2 Results

| Pipeline | Command | Exit | Result | Wall clock (`/usr/bin/time -p real`) |
|---|---|---|---|---|
| PoC render + verify | `experiments/spectrastrobe-poc/run.sh` | **0** | **CORE: PASS — 52 PASS, 0 FAIL, 1 INFO, 0 SKIP of 53**; CORRUPTION: 15 rows — 9 PASS, 5 EXPECTED-DEGRADED, 1 INFO, 0 ADVISORY-FAIL, 0 SKIP | **64.89 s** (user 63.92, sys 0.91) |
| Colour calibration | `calibration/run_calibration.sh` | **0** | **PASS — 41 PASS, 0 FAIL of 41** | **3.76 s** |
| A/B calibration | `calibration/ab/run_ab.sh` | **0** | **PASS — 117 PASS, 0 FAIL of 117** (includes determinism and provenance) | **13.34 s** |

The only non-PASS core row is the deliberate one: `m7-breedband-eerlijk` is **INFO**, "the −60 dBFS
requirement for broadband v1 material is not demonstrated" — the honest label discussed in §3.

Outputs produced (all matched by the respective `.gitignore`, none committed in NAGLOED):
`experiments/spectrastrobe-poc/out/` 88 MB (6 WAVs + `events.csv`, `timeline.svg`,
`render-manifest.json`, `verify-report.json`, `verify-report.md`);
`calibration/out/` 2.8 MB; `calibration/ab/out/` 15 MB.

### 4.3 Determinism

* `[measured]` M10 `m10-determinisme` **PASS — identical**: the verifier re-ran `render.py` into a
  temp directory and SHA-256'd all six WAVs.
* `[measured]` **independent confirmation** (not via the verifier): I ran
  `python3 render.py --score score.json --out-dir /tmp/ss-repro` a third time and compared:

  ```
  300be6ee…149b891  spectrastrobe-48k-24bit.wav        IDENTICAL
  b2e7c2c8…a69a9be  spectrastrobe-44k1-16bit.wav       IDENTICAL
  688bdb38…b290c0c  spectrastrobe-mid-44k1-16bit.wav   IDENTICAL
  ee91282b…2d1204  spectrastrobe-dim-44k1-16bit.wav    IDENTICAL
  ba00fd2a…440ce672 program-only-48k-24bit.wav          IDENTICAL
  0d19ecb4…891c7da3 program-only-44k1-16bit.wav         IDENTICAL
  ```

* `[measured]` `verification`: `calibration/ab` also re-verified 7 pre-existing WAVs against
  `provenance-baseline.json` — "7 existing files compared, all identical=True".

So: **two consecutive renders (in fact three) are byte-identical** on this platform and interpreter.
That is a per-platform guarantee, not a cross-platform one — the renders depend on CPython's `round()`
(half-to-even) and on the platform `libm` for `sin`/`cos`. Do not gate CI on a hash across machines.

### 4.4 Environment / degradation notes (nothing hidden)

* No missing dependency, no rate limit, no partial failure. All three pipelines completed and exited 0.
* The run is **pure Python and therefore slow**: 65 s for one 72 s stereo render + verification at 48 k
  plus a second render for the determinism check plus the corruption matrix. Two thirds of that time is
  the corruption matrix (6 files × full-file `_quick_metrics`) and the true-peak search; the render
  alone was ≈ 25 s. A C++ implementation is not comparable, and a Python golden-vector generator
  should stay small (2–3 s vectors, `with_programme=False`) to remain usable in a test loop.
* `run.sh` does **not** pass `--strict-corruption`, so the 15 M14 rows are advisory in the normal
  workflow — 0 ADVISORY-FAIL happened to occur, but the exit code would not have changed if one had.
* The three `.gitignore` files keep `out/` and `__pycache__/` out of git; `sys.dont_write_bytecode`
  is set in all four calibration scripts so importing the parent modules leaves no `__pycache__`.
* I did not run the hardware protocols (they need a Kasina and a person); nothing in the corpus claims
  otherwise.

---

## 5. Recommended take for KLOPGEEST

1. **Take the algorithm, not the batch renderer** — `reference-implementation.md` §2–§7 is
   the porting specification; §9 lists the audio-thread consequences.
2. **Take the measurement primitives and nine checks** (`measurement-plan.md` §5.1) into a
   small C++/Python test harness; do not take the score-rule group, the exposure ladder files, or the
   ffmpeg corruption matrix as CI.
3. **Take the transition checks from `verify_calibration.py`** (abruptness, black gap, fade times) —
   they are the only implemented proxies for NAGLOED's R2/R4/R5, and a drum machine needs them more
   than NAGLOED's art piece did. Then add the missing transition-rate limit.
4. **Take `RATIO-RESEARCH.md` and `SPECTRASTROBE.md` §1–§3 as the specification baseline**, and treat
   every colour/reference ratio as a parameter with a conservative default, because the primary
   sources disagree by 12 dB.
5. **Fix the provenance sentences** in `LICENSE:15-16` and `README.md:8-9` in the same commit that
   first copies or ports any of this material (§2.4).
6. **Do not assume the 8th-order 15.5 kHz Butterworth protects the control band** — it measures
   −19 dB at 17 kHz on broadband material. That is the biggest engineering gap between the prior work
   and a drum machine.
