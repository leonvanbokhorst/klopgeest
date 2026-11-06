> **Provenance.** One of five evidence reports produced for issue #6 on 2026-11-06 by a lead researcher working with three parallel sub-investigations. Reproduced here at full length and essentially verbatim — it is the source of record for the summaries in `../01`–`../07`, and where a summary and this document disagree, this document wins.
>

# SpectraStrobe measurement plan — code versus document

Audit of the three verifiers in NAGLOED `experiments/spectrastrobe-poc/` as a source of
KLOPGEEST regression tests. Companion documents:
`reference-implementation.md` (encoder) and `artefact-inventory.md`
(files, licences, run results).

| | |
|---|---|
| Source | `leonvanbokhorst/granular-experimental-plugin`, branch `feature/v2-exploration`, HEAD `862dec2` |
| Files | `verify.py` 1679 lines (`2133c56efbb383ce…`) · `calibration/verify_calibration.py` 829 (`cb167907b70b8c04…`) · `calibration/ab/verify_ab.py` 1479 (`bdb92ff0c89213c8…`) |
| Written plan | `docs/v2/SPECTRASTROBE.md` §3, measurement table M1–M14 (`883ba37b2069bb18…`) |
| Marking | `[doc]` written plan · `[code]` what the code does · `[measured]` observed in the run of 2026-11-06 |

---

## 1. What exists, and how the pieces are wired

| Script | What it measures | Depends on |
|---|---|---|
| `verify.py` | the full render: format, score rules, M1–M10, safety clamp (SA), brightness ladder (L), advisory corruption matrix (M14) | `render` (imported lazily, `verify.py:120-124`) |
| `calibration/verify_calibration.py` | a 9.6 s colour-sequence diagnostic: reference level/LFO, 9 colour states, transition behaviour, peak, an L/R eye-check file, determinism | `import verify as parent_verify` (`verify_calibration.py:42`) |
| `calibration/ab/verify_ab.py` | 10 A/B variants: absolute colour level, colour/reference ratio, isolation, exposure classification, transition midpoints, provenance, multi-colour annotation | `import verify as parent_verify` (`verify_ab.py:39`) |

`[code]` The two calibration verifiers **reuse the low-level primitives of `verify.py`**
(`spectrum_pair`, `band_rms_sq`, `demod_envelopes`, `goertzel_amplitude`, `pearson`, `amp_db`,
`true_peak`, `sha256`) and add their own Blackman-Harris demodulator. They cannot be copied without
`verify.py`.

`[code]` An unstated dependency: `verify.py` imports `render.py` at runtime for the M6 isolation
probe, the safety-clamp probe and the LFO corruption variants (`verify.py:845-887`, `956-994`,
`1391-1413`). The "independent verification" is therefore independent in its *analysis code* but
shares the *synthesis code* with the thing it verifies — except for M6/SA, where a *renderer* bug
would not be detectable because the probe uses the same renderer. This is legitimate for signal
integrity of the real files (M1–M5, M7–M10) and circular for the probe-based checks (M6, SA) in the
sense that they can only prove self-consistency. `[code]` `[measured]` both nevertheless catch real
regressions, because they compare against *score expectations*, not against the renderer's output.

---

## 2. The written plan (M1–M14)

`[doc]` `docs/v2/SPECTRASTROBE.md` §3 is explicit that the table is binding for the PoC and that all
tolerances are provisional because the vendor publishes no decoder passband:

| # | Written target |
|---|---|
| M1 | Carriers 18 200 / 18 700 / 19 200 / 19 700 Hz ± 1 Hz; FFT ≥ 65 536, Hann, parabolic interpolation |
| M2 | Reference **peak amplitude** −30.0 dBFS ± 0.5 dB, constant; band RMS reported separately |
| M3 | Colour amplitude relative to the reference follows the score within ± 0.5 dB; max 25 % (−12.04 dB) |
| M4 | Centre ± 0.5 dB; hard-panned isolation ≥ 40 dB |
| M5 | LFO 20 Hz ± 0.5 Hz, depth ≥ 99 % (out-of-phase ≥ 30 dB down), no residual carrier in the off phase |
| M6 | One colour at a time; other carriers ≤ −40 dB relative to the active one |
| M7 | Programme-only FFT 17–20 kHz ≤ −60 dBFS; broadband probe reported as INFO |
| M8 | Colour bands without a colour signal ≤ −80 dBFS |
| M9 | 4× oversampled true peak ≤ −6 dBTP, no clipping, no non-finite samples |
| M10 | Two renders byte-identical |
| M11–M13 | Hardware detection, LED output (no 3–30 Hz modulation ≥ 10 % depth), stop latency ≤ 50 ms — **not implemented anywhere** |
| M14 | Corruption matrix, advisory unless `--strict-corruption` |

---

## 3. What the code actually does

### 3.1 Spectral primitives (`verify.py:127-225`)

| Parameter | Value | Line |
|---|---|---|
| FFT size | **exactly** `FFT_N = 65536` (1.365 s @ 48 kHz, 1.486 s @ 44.1 kHz) | `39` |
| Window | Hann, `0.5 − 0.5·cos(2πi/(n−1))` — **symmetric** (denominator n−1), cached | `133-138` |
| Transform | one complex radix-2 FFT of `L + jR`; spectra separated by conjugate symmetry into `P_L[k]`, `P_R[k]` (power) | `141-190` |
| Normaliser | `sum_w2 = Σw²` for band RMS | `189` |
| Peak frequency | search ± 80 Hz around the nominal on the **combined** L+R power, parabolic interpolation on **magnitudes** (`sqrt(P)`), vertex clamped to ± 0.5 bin, resolution 0.7324 Hz (48 k) / 0.6729 Hz (44.1 k) | `193-206` |
| Band RMS | `2·ΣP / (N·Σw²)` over the integer bins `ceil(f_lo/bin) … floor(f_hi/bin)` | `209-216` |
| Single-bin demodulation | `2·|Σ x[n]·w[n]·e^{−jωn}| / Σw` per block, rectangular window by default, Hann if `windowed=True` | `230-265` |
| Frequency by phase slope | 20 ms blocks, phase unwrap + least-squares slope | `268-298` (used only in the M14 matrix) |
| Goertzel | 2·sqrt(power)/N | `301-314` |
| True peak | local 12-tap Hann-windowed-sinc interpolation at 3 fractional phases (¼, ½, ¾), evaluated only at the **4000 largest samples** above 50 % of the sample peak | `370-401` |

`[code]` Note two independent frequency estimators exist: `peak_frequency` (FFT + interpolation,
used by M1) and `estimate_frequency` (block phase slope, used only inside `_quick_metrics` for the
corruption matrix). They have different conventions and are not cross-checked against each other.

### 3.2 M1–M10 as implemented (`analyse_core`, `verify.py:628-813`)

| Check | Instrument actually used | Pass rule (exact expression) | Line |
|---|---|---|---|
| **M1** frequencies | FFT 65536 Hann at `reference_windows_s[0][0]` = 20.0 s; colours at `colour_t0 = 12.2` s | `|f−nominal| ≤ 1.0 Hz` for all four | `637-651` |
| **M2** reference level | 5 ms blocks, `hypot(env_L, env_R)`, max over three 1.3656 s windows; band RMS ±100 Hz reported | `|peak − (−30)| ≤ 0.5` **and** `spread ≤ 0.5 dB` | `653-683` |
| **M5** pan LFO | 1 ms **rectangular** blocks over `lfo_window_s` = 20.0–24.0 s; power balance `(P_L−P_R)/(P_L+P_R)`; depth `= 100·0.5(max−min)`; `rest = 10log10((1−q)/(1+q))`, `q = max|balance|`; frequency by Goertzel sweep 18–22 Hz in 0.02 Hz steps | `20±0.5 Hz`, `depth ≥ 99 %`, `corr ≤ −0.2`, `rest ≤ −30 dB` | `685-694`, `329-364` |
| **M3** colour ratio | one Hann-windowed block per 0.1 s probe, `amp/ref_amplitude` per ear; expectation from an independent re-implementation of the score envelopes (`points_value`, `smoothstep`) | `max|Δ| ≤ 0.5 dB` over all probes/colours | `696-744`, `407-428` |
| **M3** max coupling | 20 ms Hann blocks over every light event, peak per colour and peak sum per ear | every colour `≤ 0.25 + 0.005` | `816-842` |
| **M6** isolation | **re-renders** a 3.5 s probe per colour (one colour at a time, no programme), band power ±100 Hz at all four frequencies | worst other band `≤ −40 dB` relative to the active band | `746-756`, `845-887` |
| **M4** balance / isolation | 0.1 s probes: hard-pan window 8.5–8.65 s (L1 hard left), centre window 16.0–16.1 s (the instant L2's pan crosses 0) | `balance ≤ 0.5 dB`, `isolation ≥ 40 dB` | `758-783` |
| **M8** noise floor | FFT at the silent window 29.0 s, colour bands ±100 Hz on the **mix** | `≤ −80 dBFS` | `785-798` |
| **M9** peak | sample peak; local true peak | `≤ −6 dBFS` **and** `≤ −6 dBTP` and `< 1.0` and all finite | `800-813` |
| **M10** determinism | re-runs `render.py` into a temp dir as a subprocess, SHA-256 of all six WAVs; then a manifest self-check (`poc_version`, `sha256(render.py)`, `sha256(verify.py)`, clamp ratio 0.25, exposures present, per-output hashes) | all identical / `not problems` | `1053-1128` |
| **M7** programme | FFT on the **programme-only** WAVs: 17–20 kHz band ≤ −60 dBFS; the four carrier bands ≤ −80 dBFS; plus a white-noise broadband probe reported as INFO | `≤ −60` / `≤ −80`; INFO for broadband | `890-950` |
| **SA** clamp | renders `rgb = 0.90`, measures `max(env)/ref_amp` | `≤ 0.255` **and** the clamp log contains a 0.90 request | `956-994` |
| **L** ladder | full/mid/dim files, 30 ms probe at 13.95–13.98 s, expectation × exposure scale | `≤ 0.5 dB` each, plus `dim < mid < full` | `997-1050` |
| **M14** corruption | 15 rows: 6 via ffmpeg (SRC both ways, 16 kHz lowpass, MP3 320/128-joint, AAC 256) + 9 in-process (naive linear SRC, −6 dB, L/R swap, mono sum, rate mismatch, and 4 LFO variants: 10/50/5 Hz and square) | statuses only; `ADVISORY-FAIL` gates the exit code only with `--strict-corruption` | `1227-1413` |

### 3.3 The calibration verifier's extra instruments (`verify_calibration.py`)

| Instrument | Value | Line |
|---|---|---|
| Demodulation window | 4-term **Blackman-Harris** (`a = 0.35875, 0.48829, 0.14128, 0.01168`), **10 ms** blocks | `130-176` |
| Rationale | 10 ms ⇒ 100 Hz resolution; the 18.2 kHz reference 500 Hz away is suppressed "far below −40 dB" | `143-149` (docstring) |
| State amplitude | one Hann block over the whole 0.3 s measurement window | `100-112` |
| Reference level | FFT 65536 band RMS, ±100 Hz at t0 = 2.0 s | `92-97` |
| Crossfade overshoot | max per-carrier envelope over the whole file `≤ 1.005 × level` | `306-318` |
| Abruptness | `max |env[i+2] − env[i]| / level ≤ 0.25` per 10 ms block (≈ 1.9 dB) | `320-334` |
| Black gap | longest run of blocks with **all** colour carriers `< 5 %` of level, `≤ 20 ms` | `336-351` |
| Fade times | 10 %→90 % rise and 90 %→10 % fall `≤ 1.4 × fade_in` (= 420 ms for a 300 ms fade) | `353-369` |
| Peak | sample and true peak `≤ −6 dBFS / −6 dBTP` | `371-379` |

### 3.4 The A/B verifier's differences (`verify_ab.py`)

`[code]` It replaces M5 with a **Blackman-Harris 10 ms LFO metric that searches 19–21 Hz**
(`verify_ab.py:165-187`) and — the important part — **relaxes the residual criterion from −30 dB to
−20 dB**, with the stated reason `"10 ms Blackman-Harris measurement floor; encoded panning is
per-sample"` (`verify_ab.py:251-259`). `[measured]` it reports `depth 99.5 %, rest −26.2 dB` for the
same class of signal for which `verify.py` reports `100.0 %, −38.2 dB`.

It adds checks the other two lack and that are directly relevant to KLOPGEEST:

* **absolute** colour level per carrier `± 0.5 dB` and colour/reference ratio `± 0.5 dB`
  (`verify_ab.py:287-292`);
* an **exposure classification** (measured ratio vs the declared ceiling → `under_ceiling` /
  `gated`, `verify_ab.py:92-101`, `308-325`) — a governance check, not a signal check;
* **transition midpoint** measurement (`verify_ab.py:394-429`, `430-484`, `657-684`): at the
  crossfade midpoint, direct (linear-sum) crossfades show outgoing ≈ 0.47 / incoming ≈ 0.53 while
  the `overlap` shape shows 0.99 / 1.00. `[measured]` this is the one genuinely new physical
  measurement in the A/B set: it distinguishes a *fading through half level* crossfade from a
  *hold-both* crossfade, which is the mechanism behind NAGLOED's issue #31 ("crossfades look black").
* **provenance** (`verify_ab.py:625-656`): re-rendered files must hash-match a committed
  `provenance-baseline.json`; 7 files compared, all identical `[measured]`.

---

## 4. Where code and written plan disagree

Ordered by how much a KLOPGEEST implementation would be misled.

1. **M2's "sample peak of the complex envelope" is not what is measured.** `[doc]` says sample peak
   of the complex envelope; `[code]` `verify.py:654-660` takes the **maximum over 5 ms blocks** of
   `hypot(env_L, env_R)` with a rectangular single-bin demodulator. For a steady carrier this is
   within 0.03 dB, and `[measured]` a pure −30 dBFS reference reads −30.001 dBFS — but the "5 ms
   block" is a measurement parameter the plan never states, and it is 1/10 of the 50 ms LFO period,
   which is the reason it works at all.
2. **M5's depth/rest criteria are not achievable with M5's own instrument when colours are on.**
   `[doc]` `≥ 99 %` depth and `≥ 30 dB` off-phase rejection; `[code]` a **1 ms rectangular** block has
   1 kHz resolution, so the 500 Hz-adjacent colour carrier sits inside its main lobe and leaks into
   the reference demodulator. `[measured]` with a centre-panned red carrier present, 1 s measurement window:

   | red ratio | depth % | rest dB | verdict against the M5 rule |
   |---|---|---|---|
   | 0 | 100.00 | −65.38 | pass |
   | 6.25 % | 99.78 | −31.48 | pass, 1.5 dB margin |
   | 12.50 % | 99.25 | −27.49 | **rest fails** |
   | 20.00 % | 98.17 | −23.52 | **rest fails** |
   | 25.00 % | 97.20 | −21.49 | **depth and rest fail** |

   `[measured]` the actual 72 s run passes (100.0 %, −38.2 dB at 48 k; 100.0 %, −68.8 dB at 44.1 k)
   only because the M5 window 20.0–24.0 s contains a *dim* light event (rgb 0.055). NAGLOED fixed
   exactly this in the A/B verifier by switching to a 10 ms Blackman-Harris window **and** relaxing
   the criterion to −20 dB (`verify_ab.py:251-259`). The written plan's −30 dB figure survives only
   in the main verifier and only in favourable scenes.
3. **M3's denominator is measured with a different window than its numerator, and it silently
   zero-pads past the end of the file.** `[code]` numerator: one Hann block of 0.1 s at the probe;
   denominator: reference amplitude from a **65536-sample** FFT at t0 = 20.0 s, multiplied by √2
   (`verify.py:679-683`). `[code]` `spectrum_pair` reads zeros for `i0+i ≥ len(left)`
   (`verify.py:174-177`). Consequences `[measured]`: (a) with the windows fully inside the file the
   numerator/denominator mismatch costs 0.04–0.10 dB — and the ladder checks, which use a *third*
   denominator (the 5 ms-block peak at 20.0–21.0 s, `verify.py:1009-1010`), read ≈0.1 dB low
   (−29.894 instead of −30.000 for a −30 dBFS reference), which is exactly the `ladder-*` deviations
   of 0.09–0.10 dB in the real run; (b) with a window that runs off the end, the bias is unbounded — on
   a 2 s test vector a probe measured **663 % instead of 25 %**. Any KLOPGEEST test that reuses this
   code must assert that `t0 + FFT_N/fs ≤ duration`.
4. **M4's "centre ± 0.5 dB" is measured at one instant, on a moving pan.** `[code]` the centre probe
   is the L2 window 16.0–16.1 s, which is where L2's pan curve crosses 0 — a deliberate and neat
   choice (`verify.py:770`), but the pan moves ~0.02 in that 0.1 s, so part of the 0.5 dB budget is
   consumed by the stimulus itself `[measured]` (0.24 dB reported).
5. **M9's "4× oversampled true peak" is a local search, not an oversampled signal.** `[code]` 3
   fractional phases at the 4000 largest samples only, within ±12 samples of those (`verify.py:376-401`).
   It will miss inter-sample peaks in quiet passages. `[measured]` −17.40 dBTP = −17.40 dBFS on the
   real render, so the gate is never near its limit here; it would be near its limit on a denser mix.
6. **M7's −60 dBFS is met by this material, not by this filter.** `[measured]` the same verifier's
   own broadband probe reports −19.2 dB at 17 kHz, −34.0 dB at 18.2 kHz, −56.3 dB at 19.7 kHz, and
   labels the requirement "NOT demonstrated" for broadband material (`verify.py:920-950`).
7. **R5 contradicts the code.** `[doc]` "every light event starts and ends at an intermediate value"
   (`SAFETY-PRIVACY.md:102`) versus `[code]` events must start at (0,0) and end at (span,0)
   (`render.py:161-164`, `verify.py:539-556`).
8. **R4's "≥ 50 ms for artistic events" is not enforced.** `[code]` only the event literally named
   `L2` is checked against 50 ms (`verify.py:580-585`); every other event is checked against 20 ms.
9. **R2 has no implementation at all.** `[doc]` "never more than three flashes in any one second …
   no sustained modulation in 3–30 Hz" (`SAFETY-PRIVACY.md:99`); `[code]` no transition-rate,
   flash-count or 3–30 Hz modulation metric exists in any of the five scripts. The closest proxy is
   `verify_calibration.py:320-334` (step size per 10 ms), which bounds amplitude change, not rate.
10. **M11–M13 do not exist.** The reports say so explicitly (`verify.py:1440`, `1449`, `1473`), and
    every scope statement repeats it. Nothing about hardware detection, LED output or stop latency
    is measured.
11. **The "25 %" and "−30 dBFS" constants are not the same across artefacts.** `[doc]`/`[code]` the
    PoC uses reference −30 dBFS with colours ≤ 25 % of it; the A/B ladder uses reference −36 dBFS
    with absolute colour levels from −54.1 to −24 dBFS and classifies them against
    "25 % of −36 dBFS = −48.04 dBFS" (`ab.json:737`, `741-749`). Both are internally consistent and
    they are not comparable without conversion. A KLOPGEEST test must state its reference level in
    every expectation.
12. **Minor:** `[doc]` M1 says "FFT ≥ 65 536"; `[code]` is exactly 65 536, with an unstated ±80 Hz
    search window and ±0.5-bin interpolation clamp. `[code]` `corr ≤ −0.2` (M5) is an undocumented
    criterion. `[code]` the exit-code verdict is `fail == 0`; INFO and SKIP never fail, and there is
    no minimum check count, so a run that silently stops early can still print PASS
    (`verify.py:466-485`). `[code]` `run.sh` does **not** pass `--strict-corruption`, so the entire
    M14 matrix is advisory in the standard workflow.

---

## 5. Which checks belong in KLOPGEEST, and in what form

Ranked. "Keep" = copy the measurement rule and its tolerance; "adapt" = keep the idea, change the
instrument or the threshold; "drop" = NAGLOED artefact.

### 5.1 Keep (high value, low cost, directly meaningful for a drum machine)

| # | Test | Rule to adopt | Why |
|---|---|---|---|
| K1 | Carrier frequencies | FFT 65536 Hann, parabolic on magnitudes, `|f − nominal| ≤ 0.2 Hz` for a deterministic digital renderer (the doc's ±1 Hz is a hardware/spec tolerance) | catches a wrong increment or a float phase; ±0.2 Hz is far inside the 0.73 Hz bin yet robust to a wrapped accumulator |
| K2 | Reference peak amplitude | 5 ms blocks, `max hypot(L,R)` over ≥ 1 s, `|peak − target| ≤ 0.5 dB`, variation `≤ 0.5 dB` | the amplitude scale everything else is relative to |
| K3 | Colour ratio per carrier | one Hann block ≥ 100 ms inside a plateau, per ear, denominator = **validated reference peak**, `|Δ| ≤ 0.5 dB` | the single most likely real bug (pan law, factor 2, dB/linear) |
| K4 | Hard-pan isolation | `20·log10(A_L/A_R) ≥ 40 dB` at pan ±1 | catches a wrong colour pan law and channel swaps |
| K5 | Inter-carrier isolation | one colour at a time, other bands `≤ −60 dB` (not −40: the ideal-sine floor is −150 dB, so −60 is a real assertion; −40 is trivially met) | catches a shared phase/amplitude variable reused across colours |
| K6 | Peak and true peak | sample `≤ −6 dBFS`, true peak `≤ −6 dBTP`, no NaN/Inf | export contract; use a real oversampled peak for the true-peak number |
| K7 | Determinism + provenance | re-render and SHA-256 every output; record the source hashes **inside** the manifest and fail if they are stale (M10's manifest check, `verify.py:1086-1128`) | this is the cheapest possible regression net, and the self-hash trick is genuinely good |
| K8 | Envelope abruptness and black gap | `max |Δenv| ≤ 25 % of level per 10 ms`, longest all-colours-below-5 % run `≤ 20 ms`, 10→90 % fade rise `≤ 1.4 × fade` (from `verify_calibration.py:320-369`) | the closest thing in the corpus to R2/R4/R5 enforcement, and exactly what a drum machine's light track needs |
| K9 | Safety clamp | adversarial request above the ceiling must be applied at the ceiling **and** logged | cheap, and it is the one check that protects against a future refactor removing the clamp |

### 5.2 Adapt

| # | Test | Change required |
|---|---|---|
| A1 | Reference pan LFO (M5) | measure on a **colour-free** vector, or use the 10 ms Blackman-Harris demodulator with the −20 dB residual criterion (`verify_ab.py:165-187`, `251-259`). Do not carry the 99 %/30 dB pair into a test suite that also renders colours. |
| A2 | Programme band-limiting (M7) | keep the band-power measurement, but set the threshold from KLOPGEEST's own synthesis (a drum machine is broadband) and gate it on the actual anti-alias filter. Expect to fail with an 8th-order 15.5 kHz Butterworth: measured −19 dB at 17 kHz. |
| A3 | Noise floor (M8) | keep the rule, but it measures the renderer, not the decoder. State it as "the encoder leaves the colour bands silent", not "the decoder sees black". |
| A4 | True peak (M9) | replace the 4000-candidate local search with a real 4×/8× oversampled peak (or `libebur128`-style), otherwise the gate is decorative on dense material. |
| A5 | Colour expectation model (M3's `points_value`) | the reference re-implements the same smoothstep/envelope semantics as the encoder — an independent *implementation* of the same *assumption*. It cannot catch a wrong curve shape. Derive expectations analytically from the test score and state them as numbers (see the golden-vector section of the encoder report). |
| A6 | Corruption matrix (M14) | valuable as a one-off characterisation (it is how the 44.1/48 kHz and MP3 behaviour got documented) but it needs `ffmpeg`, takes most of the runtime, and is advisory even in the reference. Keep it as an optional manual tool, not as CI. |

### 5.3 Drop

| # | Item | Why |
|---|---|---|
| D1 | The whole `S` group (score rules) | `score-duur`, `score-L1-extinctie`, `score-A1-vertraging`, `score-L2-fadein`, `score-L3-onbeantwoord`, `score-L4-herinnering` (`verify.py:519-622`) are assertions about *Eerste Gloed*, the art piece — L1's 20–40 ms extinction, A1's 150–200 ms delay, "L3 is unanswered", "L4 is dimmer and reversed". Nothing to do with a drum machine. |
| D2 | The `L` ladder over `mid`/`dim` files | an artefact of the artistic render's three exposure variants. Keep the *idea* (a ratio ladder test, K3) and drop the files. |
| D3 | The 60–90 s duration validation, the exposure/scale consistency rule, the `meta.sections` requirement | art-piece constraints (`render.py:137-138`, `180-187`, `verify.py:526-529`). |
| D4 | M11–M13 | not implemented; belongs to a hardware protocol, not to code. |
| D5 | The `poc_version` string check (`verify.py:1100-1101`) | a NAGLOED release number. Keep the *mechanism* (a versioned manifest), not the constant. |

---

## 6. Measured baseline of the instruments themselves

`[measured]` 2026-11-06, CPython 3.14.5 stdlib only, ffmpeg present at `/opt/homebrew/bin/ffmpeg`;
all three pipelines exit 0:

| Pipeline | Checks | Result | Wall clock |
|---|---|---|---|
| `experiments/spectrastrobe-poc/run.sh` | 53 core | **52 PASS, 0 FAIL, 1 INFO, 0 SKIP** → PASS | **64.9 s** (`real`) |
| " | 15 corruption rows | 9 PASS, 5 EXPECTED-DEGRADED, 1 INFO, 0 ADVISORY-FAIL, 0 SKIP | (included) |
| `calibration/run_calibration.sh` | 41 | **41 PASS, 0 FAIL** | **3.8 s** |
| `calibration/ab/run_ab.sh` | 117 | **117 PASS, 0 FAIL** | **13.3 s** |

Representative margins from the main run (useful as the "known good" reference point for a C++
implementation):

| Measurement | 48 kHz | 44.1 kHz | Rule |
|---|---|---|---|
| M1 frequencies | 18 199.99 / 18 700.04 / 19 199.97 / 19 699.99 Hz | 18 200.03 / 18 700.03 / 19 200.04 / 19 700.03 | ±1 Hz |
| M2 peak | −29.96 dBFS (variation 0.04 dB) | −29.89 dBFS | −30 ±0.5 |
| M3 worst deviation | 0.00 dB (probe max ratio 18.6 %) | — | ≤0.5 dB |
| M4 balance / isolation | 0.24 dB / 104.3 dB | — | ≤0.5 / ≥40 |
| M5 | 20.00 Hz, 100.0 %, corr −0.99, rest −38.2 dB | 20.00 Hz, 100.0 %, rest −68.8 dB | as above |
| M6 worst other band | −159.1 dB (ideal-sine floor) | — | ≤−40 |
| M7 17–20 kHz (programme only) | −158.1 dBFS | −109.7 dBFS | ≤−60 |
| M8 colour bands silent window | −169.8 dBFS | −121.4 dBFS | ≤−80 |
| M9 | −17.40 dBFS / −17.40 dBTP | −16.96 / −16.95 | ≤−6 |
| M10 | all six WAVs byte-identical | | identical |
| SA clamp (rgb 0.90) | measured max 24.44 %, 3 log entries | | ≤0.255 + logged |
| Ladder | 6.20 % < 12.38 % < 19.79 %, deviations 0.09–0.10 dB | | ≤0.5 dB, ascending |

`[measured]` M14 detail worth keeping: neat SRC both directions preserved the carriers (+0.9 dB worst
colour error for 48→44.1); the 16 kHz lowpass removed them (−88.5 dBFS); MP3 320 kbps CBR and AAC
256 kbps preserved them (0.0 / 0.1 dB colour error); MP3 128 joint-stereo destroyed the reference
(−156 dBFS, 14.5 dB colour error); naive linear SRC cost 4.1 dB on the reference; mono summing
removed the LFO entirely (depth 0 %); the 44.1-file-at-48-rate case landed at 19 809.8 Hz, exactly
the predicted +8.84 %.

---

## 7. Recommendation in one paragraph

Copy the *primitives* (`spectrum_pair`, `band_rms_sq`, `demod_envelopes`, `lfo_metrics` with a
longer window, `true_peak` replaced, `sha256`/manifest self-check) and the *transition* checks from
`verify_calibration.py`; copy the *absolute-level + ratio + provenance* checks from `verify_ab.py`;
carry over K1–K9 as CI tests with the tolerances in §5.1. Do not carry over the score-rule group, the
exposure-ladder files, the corruption matrix's ffmpeg dependency, the −30 dB LFO residual criterion,
or any expectation that the verifier proves something about hardware. And add the two checks the
whole corpus is missing: a transition-rate limit (R2) and a real broadband band-limiting test of the
anti-alias filter that KLOPGEEST actually ships.
