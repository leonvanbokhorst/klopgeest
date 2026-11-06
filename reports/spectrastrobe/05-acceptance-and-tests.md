# 05 — Acceptance criteria, golden vectors and the test plan

**What this is.** How a builder knows the light feature is correct, and how CI keeps it correct.
Everything here is designed to be runnable without hardware.

Source of record: [`evidence/reference-implementation.md`](evidence/reference-implementation.md) (the
encoder audit and the golden-vector plan, with **measured** expected values) and
[`evidence/measurement-plan.md`](evidence/measurement-plan.md) (the M1–M14 audit and its defects).
The expected numbers below were measured on this machine by running the inherited reference
implementation, not derived on paper.

---

## 1. The four levels of acceptance

| Level | Question | Runnable today |
|---|---|---|
| **A. Signal** | Are the carriers at the right frequencies, at the right levels, in the right channels, with the right ratios? | Yes — offline WAV + FFT |
| **B. Safety** | Is the luminance modulation rate and depth bounded, structurally and measurably? | Yes — offline envelope analysis |
| **C. Musical** | Does the light do what the mapping claims, and does it respond to groove and mutation? | Mostly yes — offline structural tests |
| **D. Perceptual** | Does a person experience it as a second rhythm? | **No.** Needs a build, a Kasina, and a person. |

Levels A–C are engineering. **Level D is the question in issue #6**, and no amount of A–C substitutes
for it.

---

## 2. Level A — signal conformance

### 2.1 The golden vectors

Five carriers-only signals, 48 kHz, no programme, with a fixed reference and a linear (unity-centre)
colour pan. They are small enough for a unit test and they discriminate exactly the mistakes people
actually make: wrong pan law, factor-of-two, dB/linear confusion, clamp in the wrong place.

| Vector | Signal | Purpose |
|---|---|---|
| **V1** | Reference only, 3.0 s, no light events. | Carrier generation, level, panning. |
| **V2** | Reference + red at ratio 0.25, centre, 0.5–2.5 s, 20 ms attack/release. | Colour amplitude mapping at the ceiling; centre pan; both ears equal. |
| **V3** | As V2, hard left for the whole event. | Colour pan law; discriminates linear-0 dB-centre from equal-power. |
| **V4** | Reference + red ladder 0.0625 / 0.125 / 0.20 / 0.25, four 500 ms steps at 0.30 / 0.80 / 1.30 / 1.80 s, 20 ms edges, centre. | The mapping at four levels inside one file; catches a factor-of-2 or a dB/linear mistake. |
| **V5** | Adversarial: rgb = 0.90 / 0.90 / 0.90, one event 0.5–2.5 s, centre. | The clamp, and that it is logged. |

### 2.2 Expected values — measured

Conventions: reference peak from `max over 5 ms blocks of hypot(env_L, env_R)`; colour amplitudes from
a 120 ms Hann single-bin demodulator; LFO metrics from a **≥ 1 s** window. These conventions matter —
see §2.3.

| Signal | Quantity | Expected | Tolerance |
|---|---|---|---|
| V1 | sample peak | −30.000 dBFS | ±0.05 dB |
| V1 | true peak | −30.000 dBTP | ±0.05 dB |
| V1 | reference peak | −30.001 dBFS | ±0.10 dB |
| V1 | FFT peak, reference | 18 199.99 Hz | ±0.10 Hz |
| V1 | red / green / blue bands | numerically zero (−162 / −175 / −182 dB re ref) | ≤ −120 dB re ref |
| V1 | LFO frequency | 20.000 Hz | ±0.02 Hz |
| V1 | LFO depth | 100.00 % | ≥ 99.0 % |
| V1 | L/R power correlation | −0.995 | ≤ −0.98 |
| V1 | "off-phase" rejection | −65.4 dB | ≤ −40 dB |
| V2 | sample peak | −28.080 dBFS | ±0.05 dB |
| V2 | red amplitude, L and R | 0.00790569 each (25.000 % of reference) | ±0.05 dB; \|L−R\| ≤ 0.01 dB |
| V2 | green / blue amplitude | 0 (−185 / −195 dB) | ≤ −120 dB |
| V2 | FFT peak, red | 18 700.02 Hz | ±0.10 Hz |
| V3 | red amplitude L / R | 0.00790569 / 0.0 (−173 dB) | L ±0.05 dB; R ≤ −120 dB |
| V3 | sample peak | −28.100 dBFS | ±0.05 dB |
| V4 | red amplitude, four steps | 6.250 / 12.500 / 20.000 / 25.000 % of reference | ±0.05 dB each |
| V4 | monotonicity | strictly increasing | — |
| V5 | red, green, blue each ear | 0.00790569 each = 25.000 % | ±0.05 dB |
| V5 | sample peak | −25.406 dBFS | ±0.10 dB |
| V5 | clamp log | 3 entries, `requested 0.9 → applied 0.25` | exact |

**These are the NAGLOED reference constants** (reference −30 dBFS). If KLOPGEEST adopts the
developer-record −36 dBFS reference from [`01-signal-spec.md`](01-signal-spec.md) §3, every absolute
number shifts by 6 dB and the ratios are unchanged. **Decide the reference level before freezing the
golden file**, or generate the golden set twice.

44.1 kHz parity: the same expected values hold within the same tolerances (measured: reference peak
−29.93 dBFS, red 25.000 %, FFT peaks 18 200.03 / 18 700.02 Hz). This deserves its own row because
19.7 kHz sits at 89 % of Nyquist at 44.1 kHz.

### 2.3 Four measurement traps, all found by running them

1. **The FFT window must lie entirely inside the file.** A naive `spectrum_pair` zero-pads past
   end-of-file. With `FFT_N = 65536` at 48 kHz the window is 1.365 s, so for a 2 s vector any
   `t0 > 0.635 s` silently biases band power down — measured: a window at 1.65 s in a 2 s file
   reported the reference at −59.9 dBFS instead of −30, and derived the colour ratio as **663 %
   instead of 25 %**. Use `t0 ≤ 0.63 s` in a 2 s vector, or `t0 = 0.5 s` in a 3 s vector.
2. **Never use a single ear's envelope as the denominator for colour ratios.** With a demodulation
   block longer than the 50 ms LFO period, one ear's measured reference amplitude is ≈ 0.60 × the
   true peak, and a 25 % colour is reported as **41.5 %**. Use the reference **peak** (the 5 ms-block
   convention above) or the analytic amplitude.
3. **LFO metrics need ≥ 1 s of signal.** Measured: 0.3 s window → 19.90 Hz / 95.6 % depth; 1 s
   window → 20.000 Hz / 100.00 % / −0.995 / −65.38 dB.
4. **Colour energy contaminates the reference-pan measurement.** Measured, with a simultaneously
   active centre-panned red carrier in a 1 s window:

   | red ratio | decoded depth | off-phase rejection |
   |---|---|---|
   | 0 | 100.00 % | −65.38 dB |
   | 6.25 % | 99.78 % | −31.48 dB |
   | 12.50 % | 99.25 % | −27.49 dB |
   | 20.00 % | 98.17 % | **−23.52 dB** |
   | 25.00 % | 97.20 % | **−21.49 dB** |

   At ≥ 20 % colour the naive "≥ 30 dB down" criterion **fails although the panning is perfect**.
   Measure the LFO on the reference-only vector, or window the demodulator with a 10 ms
   Blackman-Harris (the calibration verifier does exactly that). **Do not copy the naive demodulator.**

### 2.4 Comparing C++ against the Python reference

| Level | Assertion |
|---|---|
| **Scalar conformance** (always) | Re-measure the C++ WAV with the four conventions in §2.2 and compare against the table. This is the test that catches real mistakes. |
| **Numerical agreement** | On 0.3–2.7 s of a 3 s vector: `max|x_cpp − x_py| ≤ 1e-6`, `rms(x_cpp − x_py)/rms(x_py) ≤ 1e-5`; after 16-bit quantisation, ±1 LSB. |
| **Never** | Cross-machine file hashes. Two renders are byte-identical **per platform**; a libm difference makes a hash gate fail for the wrong reason. |

**Phase accumulator discipline:** the reference implementation needs an unwrapped `double` phase for
exact agreement. A wrapped `double` is fine (difference < 1e-6). A **`float` phase is not** — it is
measured to be ~24 dB wrong on an unwrapped comparison. This is the single most likely silent defect
in a C++ port.

### 2.5 Frequency and level accuracy targets

| Check | Target | Source |
|---|---|---|
| Carrier frequencies | 18 200 / 18 700 / 19 200 / 19 700 Hz, ±1 Hz | M1 |
| Reference absolute level | target ±0.5 dB, constant across the piece | M2 |
| Per-colour amplitude vs the mapping | ±0.5 dB | M3 |
| L/R balance at centre | ±0.5 dB | M4 |
| Hard-pan isolation | ≥ 40 dB | M4 |
| Inter-carrier isolation (one colour at a time) | ≤ −40 dB relative to the active carrier | M6 |
| Noise floor in carrier bands with colours muted | ≤ −80 dBFS | M8 |
| Sample peak / true peak | ≤ −6 dBTP, no clipping, all finite | M9 |
| Determinism | byte-identical re-render, same platform | M10 |

---

## 3. Level B — the safety budget, measured

**This is the level that does not exist anywhere yet.** The inherited research documents "no more
than three flashes per second" and "attack/release ≥ 20 ms" but **neither is implemented**. KLOPGEEST
must implement and measure both.

| Check | Method | Pass |
|---|---|---|
| **B1** minimum edge time | Per light channel, find the 10 %→90 % and 90 %→10 % rise/fall times of every transition in the rendered 6-channel gain trace. | Every edge ≥ 20 ms. **Fail the build**, do not clamp silently. |
| **B2** luminance edge rate | `Y = 0.21·R + 0.72·G + 0.07·B` per ear; count opposing pairs of changes each ≥ 10 % of the ceiling. Sliding 1 s window. | ≤ **3** in every window over the whole render. |
| **B3** no modulation in 3–30 Hz | FFT of `Y(t)` after removing the DC and the intentional slow envelope. | No peak in 3–30 Hz above **10 %** of the mean level. |
| **B4** governor actually engages | Drive a hostile state (ratchet 4 on every step, 200 BPM, all voices on, STABILITY 0). | B2 still holds; `governorActive` telemetry is non-zero. |
| **B5** clamp is adversarial | A state or test hook requesting brightness 1.5× the ceiling. | Output clamped to the ceiling; the clamp is logged; no crash. |
| **B6** make-before-break | A colour A→B transition. | B reaches its target before A starts to fall; overlap ≥ 100 ms; `Y(t)` never dips more than 10 %. |
| **B7** failure injection | NaN/Inf in a mapping output; a divide-by-zero in a lane length; a sample rate of 8 kHz. | Finiteness restored, light disabled with a status flag, audio unaffected. |

B2 and B3 are the ones that make the feature defensible. **If they cannot be measured, the feature is
not ready**, regardless of how it looks.

---

## 4. Level C — musical structure

These test the claims in [`03-light-engine-design.md`](03-light-engine-design.md) §7.

| Check | Method | Pass |
|---|---|---|
| **C1** audio untouched | Same seed/pattern/params, light off vs light on. | Bit-identical below 16 kHz. (If a programme low-pass is used, "identical modulo the documented filter".) Prefer the four-notch approach, which leaves the audible band bit-identical.) |
| **C2** light is rhythmic on its own | Render carriers only, programme muted. Autocorrelation of `Y(t)`. | A clear peak at the pattern period; and the correlation profile differs between two different factory patterns. |
| **C3** groove is used | Render STABILITY 1.0 and STABILITY 0.0. | The variance of the light's overlap shapes / spatial deviation differs by more than measurement noise. |
| **C4** mutation without acceleration | Press MUTATE ×10 (or drive `mutationEpoch` in the test harness). | B2 still holds; the light texture measurably changes (hue/phase statistics differ) between epoch 0 and epoch 10. |
| **C5** ratchet does not strobe | A pattern of 16th steps with ratchet 4 at 200 BPM. | The light emits **one** event per step, not four; B1/B2 hold. |
| **C6** polymeter does not repeat | COUNTERPOINT with lane lengths 16 / 7 / 5. | The colour-field state does not repeat within the render duration (or repeats only at the true 560-step period). |

---

## 5. What to reuse from the inherited implementation — and what not to

Audited file by file in [`evidence/measurement-plan.md`](evidence/measurement-plan.md).

### Reuse

| Artefact | Why |
|---|---|
| `verify.py`'s FFT/Goertzel machinery and the M1/M2/M4/M6/M8 measurement conventions | Correct, and the conventions in §2.3 are the corrections to the traps it contains. |
| `verify_calibration.py`'s **transition checks** (10 ms envelope change, black-gap detection, fade rise/fall) | The only implemented proxies for a flash budget. Directly reusable for B1/B6. |
| `verify_ab.py`'s transition-midpoint and provenance checks | The A/B verifier already fixed the M5 demodulator defect with a 10 ms Blackman-Harris. Copy **that** version. |
| The two pan laws, the colour = ratio × reference-peak definition, the clamp placement, the smoothstep envelope | These are correct and non-obvious. See §2.1 and [`evidence/reference-implementation.md`](evidence/reference-implementation.md). |

### Do not reuse

| Artefact | Why not |
|---|---|
| The "band-limiting": 8th-order Butterworth at 15.5 kHz | Measures only **−19 dB at 17 kHz**, −34 dB at 18.2, −56 dB at 19.7 on broadband material. It passes there only because that programme was sparse sub-1.2 kHz synthesis with 90 % silence. **Unusable for a drum machine.** |
| `verify.py`'s M5 LFO instrument and thresholds | The 1 ms rectangular demodulator cannot separate 500 Hz-adjacent carriers (§2.3, trap 4). |
| `verify.py`'s M3 ratio denominator | Zero-pads past end-of-file (§2.3, trap 1). |
| The score-rule group (L1/L2/L3/L4 art-piece assertions, the 60–90 s duration gate) | Specific to a different composition. |
| The ffmpeg corruption matrix as a CI gate | Useful as a manual diagnostic, too slow and too environment-dependent as a gate. |
| Precomputed `count`-length envelope arrays | Python convenience; a C++ audio thread may not allocate. |
| Colour phase reset at every event | The reference does this; the calibration renderers correctly keep the colour phase **continuous**. Keep it continuous. |

---

## 6. Proposed test surface

Nothing exists today. The shape that fits the repository:

```
tests/LightTests.h/.cpp        # on the existing __has_include test pattern
tools/spectrastrobe/
    reference/                 # pinned copy of the Python reference (see 07)
    golden.json                # committed expected scalars (NOT the WAVs)
    golden.py                  # regenerates + --check re-verifies against golden.json
scripts/validate.sh            # unchanged for the music gate; a NEW light gate
```

- `LightTests` must run in the existing harness (no new framework, no new dependency).
- The light gate in `validate.sh` must be **separate** from the demo-render gate. Trap T3 in
  [`02-integration.md`](02-integration.md): the demo render peaks at −1.2 dB and fails on any sample
  ≥ 0.9999; adding carriers to it breaks the release gate. Catch it before it happens.
- `golden.py` imports the pinned reference, renders the five vectors, and writes `golden.json` with
  the sha256 of the pinned reference files. Run it once, commit the JSON, never commit the WAVs.
- CI reads `golden.json`; it does not need Python to run the C++ tests.

---

## 7. Definition of done for the first experiment

Restated from issue #6, made testable:

> A prototype in which at least two clearly different rhythmic light mappings work synchronously with
> the same groove, with working off/intensity controls, reproducible settings, and a documented real
> Kasina test.

| # | Criterion | Evidence |
|---|---|---|
| D1 | PULSE and COUNTERPOINT both render, and are structurally different (C6 holds for one and not the other). | `golden.json` + a rendered sample |
| D2 | Light off produces a bit-identical instrument (C1). | CI test |
| D3 | Intensity 0 → dark; intensity 1 → the ceiling, measured. | CI test |
| D4 | Panic reaches silence within 50 ms in every path (P3). | CI test |
| D5 | B1–B7 pass. | CI test |
| D6 | Settings are recalled exactly by a state save/load round-trip. | CI test |
| D7 | A real Kasina session is documented with firmware, settings, files and observations. | [`06-hardware-validation.md`](06-hardware-validation.md) form |
| D8 | **A person, seeing only the light, can tell the mappings apart.** | The five questions in `06` §5 |

**D8 is the definition of done.** D1–D7 are what earns the right to ask it.
