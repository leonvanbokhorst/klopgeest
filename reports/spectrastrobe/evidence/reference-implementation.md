> **Provenance.** One of five evidence reports produced for issue #6 on 2026-11-06 by a lead researcher working with three parallel sub-investigations. Reproduced here at full length and essentially verbatim — it is the source of record for the summaries in `../01`–`../07`, and where a summary and this document disagree, this document wins.
>

# SpectraStrobe reference encoder — implementation audit and golden-vector plan

Audit of NAGLOED's `experiments/spectrastrobe-poc/render.py` as builder material for a
C++/JUCE SpectraStrobe encoder in KLOPGEEST.

| | |
|---|---|
| Source repo | `leonvanbokhorst/granular-experimental-plugin` (NAGLOED) |
| Branch | `feature/v2-exploration`, HEAD `862dec2` |
| Audit copy | `git archive feature/v2-exploration experiments/spectrastrobe-poc docs/v2` → `/tmp/ss-work` (NAGLOED working tree untouched, branch never checked out) |
| `render.py` | 851 lines, sha256 `541da4ac2b0663f5…` |
| Status of claims | `[code]` read in the source · `[measured]` run on this machine (2026-11-06, CPython 3.14.5, macOS, ffmpeg present) · `[doc]` stated in NAGLOED's documents · `[open]` not established |

Everything below cites `render.py` line numbers unless stated otherwise. "Python
convenience" means: an artefact of how the script is written, not a property of the signal.

---

## 1. The binding processing order

`render.py:17-18` and `docs/v2/SPECTRASTROBE.md` §2 state the requirement:

> "process and band-limit the programme completely first, then add the carriers, then nothing else."

The code implements exactly this in `render_mix` (`render.py:428-443`):

```
programme  = render_program(score, fs, count)          # synth → LP → peak-scale → pan → sum
carriers   = render_carriers(score, fs, count, …)      # reference + colour channels
mix[i]     = programme[i] + carriers[i]                # single sample-wise addition, nothing after
```

`[code]` There is no limiter, no normaliser, no dither, no filter and no metering on the sum.
The only thing that happens after the addition is quantisation in `write_pcm` (`render.py:449-489`).

`[code]` The band-limiting is applied **per programme event, on the mono buffer, before panning**
(`render.py:332`), not on the summed programme bus. Difference for a C++ implementation: negligible
for a linear filter, but it means the reference never low-passes the carriers and never low-passes
the programme after the carriers are added.

---

## 2. Signal constants

`[code]` `score.json` (`carriers` block) + `render.py:346-425`.

| Item | Value | Where |
|---|---|---|
| Reference carrier | 18 200 Hz | `score.json:36`, `render.py:366` |
| Red / green / blue | 18 700 / 19 200 / 19 700 Hz | `score.json:46-49`, `render.py:418` |
| Reference level | −30.0 dBFS **peak amplitude** (`amp_ref = 10^(−30/20) = 0.0316227766`) | `render.py:358`, `score.json:37` |
| Reference pan LFO | sine, 20.0 Hz, depth 1.0, offset 0.0, per sample | `score.json:38-41`, `render.py:360-362`, `374`, `382` |
| Reference pan law | equal power (cos/sin of θ = π/4 + π/4·p) | `render.py:113-117`, `382-385` |
| Colour pan law | linear, **0 dB in the centre** (`(1−max(0,p), 1+min(0,p))`) | `render.py:120-127`, `401` |
| Colour start phase | red 0.35 rad, green 1.15 rad, blue 2.05 rad | `score.json:47-49`, `render.py:419` |
| Reference fade in / out | 250 ms / 400 ms, raised cosine | `score.json:43-44`, `render.py:98-107`, `359` |
| Colour ceiling | `max_colour_ratio` = 0.25 per carrier per event | `score.json:52`, `render.py:357`, `404-406` |
| Score default colour level | 0.20 at "full" exposure (ladder 0.0625 / 0.125 / 0.20) | `score.json:21-24`, `render.py:44-48` |

The carrier frequencies are a format requirement ([doc] Lumasonic format spec, quoted in
`docs/v2/SPECTRASTROBE.md` §1.1, and echoed in §6). The −30 dBFS reference and the 20 % / 25 %
colour levels are **NAGLOED design choices**, not format constants — see §5.4.

---

## 3. Carrier generation

### 3.1 Per-sample form

`[code]` `render_carriers`, `render.py:373-387` (reference) and `420-424` (colours). Stripped of
Python, one sample of the reference carrier is:

```
                base   = sin(phi_pan)
                p      = clamp(offset + depth * base, -1, +1)
                theta  = PI/4 + PI/4 * p
                s      = amp_ref * sin(phi_ref) * fade[i]
                outL  += s * cos(theta)
                outR  += s * sin(theta)
                phi_ref += 2*PI*hz_ref/fs
                phi_pan += 2*PI*hz_lfo/fs
```

and one sample of one active colour `c` inside light event `e` is:

```
                s      = amp_ref * applied_c * env_e[j] * sin(phi_c) * fade[i0+j]
                outL  += s * gainL(t)
                outR  += s * gainR(t)
                phi_c += 2*PI*hz_c/fs
```

`fade` is the track-edge fade (§6.2), `env_e` the event envelope (§6.1), `gainL/gainR` the
colour pan gains (§3.4).

### 3.2 Phase accumulation and precision

`[code]` `render.py:370-372` initialise `phase_ref = 0.0`, `phase_lfo = 0.0`;
`render.py:386-387` accumulate in **unbounded Python floats (= IEEE double)** and never wrap.
Colour phases are initialised from `phase_rad` at each event start (`render.py:419`).

Numbers that matter for a C++ port:

* 18 200 Hz at 48 kHz → 2.637 samples/cycle; at 44.1 kHz → 2.423. The carrier is not
  commensurate with the sample grid, so the phase must be a real accumulator, not a table index.
* 20 Hz at 48 kHz → exactly 2 400 samples/period; at 44.1 kHz → exactly 2 205. The LFO *is*
  commensurate at both rates, so the pan pattern repeats without drift.
* Over a 90 s file at 48 kHz the reference phase reaches ≈ 1.0 × 10⁷ rad. Worst-case accumulated
  rounding in double is a random walk of ≈ 1 × 10⁻⁶ rad → sample error ≈ 3 × 10⁻⁸ (−150 dBFS).

`[measured]` I ran the naive ports against the reference over 3 s / 144 000 samples of the reference
carrier (48 kHz):

| Phase accumulator | max \|sample error\| | in dBFS |
|---|---|---|
| `double`, unwrapped (the reference) | below the measurement floor (~10⁻⁸; two in-process `render_mix` calls return sample-identical arrays `[measured]`) | — |
| `float32`, unwrapped | 6.3 × 10⁻² | **−24 dBFS — unusable** |
| `float32`, wrapped to [0, 2π) | 4.0 × 10⁻⁴ | −68 dBFS — passes every measurement, fails bit-exactness |

Consequence: **a `float` phase accumulator without wrapping is not an acceptable shortcut.**
Use `double`; wrapping to [0, 2π) per sample is safe in double (error ≈ 10⁻⁹ rad) and keeps the
accumulator well-conditioned, but it makes the output differ from the Python reference in the last
bits, so golden-vector comparison must be tolerance-based (see §8.5).

### 3.3 The reference panning LFO

`[code]` `render.py:374-385`, per **sample**, not per block, not smoothed, not band-limited:

```
p = clamp(0.0 + 1.0 * sin(phi_pan), -1, +1)
theta = PI/4 + PI/4 * p
gL = cos(theta);  gR = sin(theta)
```

| p | θ | gL | gR | gL²+gR² | gL+gR |
|---|---|---|---|---|---|
| −1.0 | 0 | 1.000 | 0.000 | 1.0 | 1.000 |
| −0.5 | π/8 | 0.924 | 0.383 | 1.0 | 1.307 |
| 0.0 | π/4 | 0.707 | 0.707 | 1.0 | 1.414 |
| +0.5 | 3π/8 | 0.383 | 0.924 | 1.0 | 1.307 |
| +1.0 | π/2 | 0.000 | 1.000 | 1.0 | 1.000 |

This is **equal power** (constant gL²+gR²), i.e. the centre-panned reference sits at −3.01 dB per
ear, and at the extremes one ear gets the full amplitude and the other exactly zero. That is what
"depth 100 %" means and why the off-phase of the reference is measured as ≥ 30 dB down
(`docs/v2/SPECTRASTROBE.md` §3, M5). `[measured]` M5 on the real render: 20.00 Hz, depth 100.0 %,
`rest −38.2 dB` (48 kHz) / `−68.8 dB` (44.1 kHz) — see the caveat in the measurement-plan report.

The shape switch (`render.py:375-381`) maps both `"square"` and `"pulse"` to a hard ±1 sign of the
same sine, i.e. square and pulse are the *same* waveform in this code — a corruption-test
convenience only. The default and the format's prescribed form is the sine.

`[code]` Note that `offset` and `depth` are only used through the clamp, and `depth > 1` is possible
in the score without validation — `validate_score` (`render.py:133-189`) never checks the LFO.

### 3.4 The colour pan law — this is the one a C++ port is most likely to get wrong

`[code]` `render.py:120-127`:

```
gL = 1 - max(0, p)
gR = 1 + min(0, p)
```

| p | gL | gR | gL²+gR² | gL+gR |
|---|---|---|---|---|
| −1.0 | 1 | 0 | 1.0 | 1 |
| 0.0 | **1** | **1** | 2.0 | **2** |
| +1.0 | 0 | 1 | 1.0 | 1 |

The docstring (`render.py:121-125`) is explicit: *"Deliberately no −3 dB centre-pan law: that would
halve the decoded brightness per ear."* So a colour channel at ratio `r`, centre-panned, delivers
**`r · amp_ref` to *each* ear**, i.e. twice the total energy of the same channel hard-panned.
A reimplementation that reuses one equal-power panner for both the reference and the colours will
be 3 dB wrong on every centred colour and will fail the ratio test.

`[measured]` `render.py` output confirms it: red at ratio 0.25, centre pan → L = R = 0.00790569 =
0.25 × 0.0316227766 exactly; hard-left → L = 0.00790569, R = 0.0 (−173 dB).

---

## 4. Colour amplitude mapping and the 25 % clamp

### 4.1 Semantics of the RGB value

`[code]` `render.py:404-417`:

```
requested = rgb[c] * colour_scale            # rgb is ALREADY a ratio of the reference peak
applied   = min(requested, max_colour_ratio) # max_colour_ratio = 0.25
amp       = amp_ref * applied                # a PEAK amplitude, not RMS
```

* `rgb[c]` in the score is **not** 0–255, not 0–1 sRGB and not a linear light value; it is a
  fraction of the reference peak amplitude. `score.json` L1 red = 0.20 → −13.98 dB relative to the
  reference.
* `colour_scale` is the exposure (ladder) multiplier: 1.0 / 0.625 / 0.3125 for full / mid / dim
  (`render.py:44-48`, `721-728`).
* The clamp is applied **after** the exposure multiplication and **before** the pan gains, inside
  the per-event, per-colour loop.

### 4.2 What the clamp does and does not bound

`[code]` `render.py:406-413`:

```
applied = min(requested, max_ratio)
if requested > max_ratio + 1e-12 and clamp_log is not None:
    log an entry {event, colour, requested_ratio, applied_ratio, clamp_ratio}  (deduplicated)
if applied <= 0.0: skip this colour entirely
```

* Bound: **per colour carrier, per ear ≤ 0.25 · amp_ref**, because `gL, gR ≤ 1`.
* Not bounded: the **sum** of the three colours in one ear. With all three at the ceiling the sum is
  0.75 · amp_ref per ear; added to the reference at up to 1.0 · amp_ref the coherent worst case is
  **1.75 · amp_ref = −25.1 dBFS** `[code]` arithmetic. `[measured]` the adversarial 3 × 0.90 → clamp
  vector peaks at −25.41 dBFS. `docs/v2/SPECTRASTROBE.md` §6 says the sum "can be higher (in *Eerste
  Gloed* 29.5 % at the L1 peak)" and is *monitored*, not clamped; `verify.py:816-842` measures it.
* Not bounded by `validate_score`: `render.py:145-147` checks only that `max_colour_ratio ∈ (0, 0.25]`
  and `render.py:180-187` only that each exposure's `max_colour_ratio == scale × 0.20`. A score with
  `rgb = 0.90` is accepted and silently clamped; the run prints a WARNING to stderr
  (`render.py:762-767`) and records it in the manifest (`render.py:791-797`).

### 4.3 The mapping verified

`[measured]` a four-step ladder rendered through `render.render_mix` (3 s, reference only + red),
each step measured with the verifier's own 120 ms Hann single-bin demodulator against the analytic
reference amplitude 0.0316227766:

| score rgb | measured amplitude (per ear) | % of reference | dB re reference |
|---|---|---|---|
| 0.0625 | 0.00197643 | 6.250 % | −24.082 |
| 0.1250 | 0.00395286 | 12.500 % | −18.062 |
| 0.2000 | 0.00632456 | 20.000 % | −13.979 |
| 0.2500 | 0.00790570 | 25.000 % | −12.041 |

### 4.4 Do not hard-code 1:1 or 4:1 as a format truth

`[doc]` `calibration/RATIO-RESEARCH.md` §4 (`:188-196`) documents a genuine, unresolved conflict in
the primary sources about what colour amplitude means "100 % brightness":

| Source | Reference | Full-brightness colour | Implied ratio |
|---|---|---|---|
| MindPlace developer statement (Hendrickson 2015) | −36 dB | −24 dB per light channel | **4:1 (+12.04 dB)** |
| The same developer's Audacity tooling | normalise to −36.5 dB | single tone −22 dB | ≈ 4:1 … 5:1 |
| Encoder developer (TomasC 2019) | 1/4 of the colour tones | −24 dB = 100 % | **4:1** |
| Lumasonic production/mixing table | "reference tone dB level" | colour at reference level = full | **1:1 (0 dB)** |

`[doc]` `calibration/ab/ab.json:48-60` therefore renders an explicit diagnostic ladder at −54.1 /
−48.1 / −42 / −36 / −30 / −24 dBFS with the reference fixed at −36 dBFS, and `ab.json:741-749`
records the owner correction that the "25 % ceiling" is a *conservative research convention*, not a
physical threshold and not a decoder specification.

**Implication for KLOPGEEST:** the encoder needs a *parameterised* colour/reference ratio with a
conservative default, not a constant. Witness the 20 % and 25 % values in `render.py` as evidence of
what NAGLOED chose, and cite `RATIO-RESEARCH.md` for why they are not spec.

---

## 5. Envelopes and fades

### 5.1 Breakpoint envelopes: smoothstep, not linear, not exponential

`[code]` `segments_array` (`render.py:74-95`) + `smoothstep` (`render.py:69-71`):

```
smoothstep(u) = let u' = clamp(u,0,1) in u'²·(3 − 2·u')

for each consecutive breakpoint pair (t0,v0)→(t1,v1):
    i0 = round(t0·fs); i1 = round(t1·fs)          # clamped to [0, count]
    span = i1 − i0;  if span <= 0: skip
    for i in [i0, i1):  arr[i] = v0 + (v1−v0)·smoothstep((i−i0)/span)
fill arr[i] = v(last) for i ≥ round(t_last·fs)   # and arr is pre-filled with v(first)
```

Properties a C++ implementation must reproduce:

* The curve is **cubic smoothstep** (C¹, zero slope at both ends of every segment), evaluated from a
  normalised sample index — not integrated, not a one-pole, not exponential.
* Segment length `span` is in **samples**, computed by `round(t·fs)`, so the same score at 44.1 and
  48 kHz produces slightly different switch points (±1 sample). This is a real property of the
  reference, not a bug.
* The last sample of a segment is `i1 − 1`, i.e. the value just short of `v1`; the next segment then
  starts exactly at `v1`. The residual step is `dv·3/span²` (≈ 3 × 10⁻⁶ for a 1 s segment) —
  inaudible, but a bit-exactness trap.
* Arr is pre-filled with the first and post-filled with the last breakpoint value, so a score that
  starts after t=0 is held at `v(first)` (which validation forces to 0 anyway).

### 5.2 The track-edge fade: raised cosine (Hann half), *different curve* from §5.1

`[code]` `fade_array` (`render.py:98-107`), applied to **both** the reference (`render.py:383`) and
every colour sample (`render.py:421`):

```
k_in  = min(count//2, round(fade_in_ms  · fs / 1000))     # 250 ms → 12000 samples @48k
k_out = min(count//2, round(fade_out_ms · fs / 1000))     # 400 ms → 19200 samples @48k
fade[i]         = 0.5 − 0.5·cos(π·i/k_in)     for i < k_in          # 0 → 1, raised cosine
fade[count−1−i] = 0.5 − 0.5·cos(π·i/k_out)    for i < k_out         # 0 → 1, raised cosine
fade[i]         = 1.0                          elsewhere
```

`[code]` Sample 0 and sample `count−1` are exactly 0.0 → the file cannot click at its edges.

`[doc]`/`[code]` **Inconsistency inside the NAGLOED work:** the two calibration renderers use a
*smoothstep* 300 ms global ramp instead (`calibration/render_calibration.py:59-68`,
`calibration/ab/render_ab.py:62-72`), not the raised cosine. So "300 ms smoothstep" and
"250/400 ms raised cosine" are two different NAGLOED fades for the same purpose. KLOPGEEST must pick
one and document it; smoothstep is the better default because it has zero slope at *both* ends and
therefore no slope step where the fade meets the constant 1.0 region. (The raised cosine also has
zero slope at both ends — `d/di[0.5−0.5cos(πi/k)] = 0.5π/k·sin(πi/k)` is 0 at i=0 and 0 at i=k — so
both are click-free; only the midpoint shape differs.)

### 5.3 Attack and release: defined, measured, enforced

`[code]` `validate_score` (`render.py:148-179`):

* `attack  = first breakpoint time t_i with v_i ≥ 0.5 · peak`   (`render.py:169-170`)
* `release = span − t(env[-2])` — i.e. the duration of the **last segment** (`render.py:171`)
* Hard failure if `attack < min_attack_ms` or `release < min_release_ms` (`render.py:172-177`);
  defaults 20 ms each (`render.py:149-150`, `score.json:27-28`).
* Also required: `env[0] == (0, 0)` and `env[-1] == (span, 0)`, both to 1e-6 (`render.py:161-164`), and
  `peak ≤ 1.0` (`render.py:178-179`).

Note the **asymmetry**: attack is a 50 %-crossing time, release is a segment length. That works for
the way the score is written (each event ends in one short extinction segment) but it is *not* a
symmetric fade-time metric, and it means an event can pass with a 500 ms release that spends 480 ms
at full brightness and 20 ms falling. A C++ implementation should implement the *enforcement* the
documentation claims (a real rise/fall time to/from the plateau) rather than copy this ad-hoc pair.

`[doc]` `docs/v2/SAFETY-PRIVACY.md` R4 requires ≥ 20 ms hard and **≥ 50 ms for artistic events**.
`[code]` Only the 20 ms value is enforced for all events; the 50 ms rule survives only as
`envelope_rules.l2_fade_in_ms = 50` in `score.json:30` and a single **hard-coded check on the event
named L2** in `verify.py:580-585`. There is no general "≥ 50 ms for artistic events" enforcement.

`[doc]` `docs/v2/SPECTRASTROBE.md` §6 also promises "an upper bound on the number of brightness
transitions per second" (R2). `[code]` **No such check exists anywhere** in `render.py`, `verify.py`,
`verify_calibration.py` or `verify_ab.py` (no transition-rate or flash-count metric; the closest is
the 10 ms abruptness rule in `verify_calibration.py:320-334`, which bounds the *step size*, not the
rate). This is a claimed safety requirement with no implementation — KLOPGEEST must build it, and
must not assume the NAGLOED verifier proves it.

### 5.4 "No step from black" — what is actually enforced

`[code]` Two mechanisms, both real:

1. **Geometric:** every segment is a smoothstep, so both endpoints of every segment have zero slope.
   A 20 ms extinction from 1.0 → 0 therefore starts and ends flat. No discontinuity in value or
   slope at segment boundaries (up to the ±1-sample quantisation of breakpoint times).
2. **Structural:** the envelope must start at (0, 0) and end at (span, 0) (`render.py:161-164`),
   so an event can never switch on or off at a non-zero level.

`[code]` What is **not** enforced: a minimum *duration* of the plateau, a maximum number of
extinctions, or an absolute slew-rate limit in dB/ms. `verify_calibration.py:320-334` adds
"max 25 % of the active level change per 10 ms block (≈ 1.9 dB)" — but that is a property of the
9.6 s calibration stimulus (250 ms crossfades), not a gate on arbitrary scores.

`[doc]` R5 says "every light event starts and ends at an intermediate value" — which contradicts the
code, where events are required to start and end at exactly **zero**. The code's meaning ("ramp from
zero, never step") is the one to keep; the document's wording is wrong.

### 5.5 Colour carrier phase: reset per event (reference) vs continuous (calibration)

`[code]` `render.py:419` initialises `phase = phase_rad` **inside the event loop**, so a colour's
phase restarts at every light event of that colour. The envelope is 0 at that instant, so it is not
a click, but it makes the output depend on event boundaries in a way that has no signal meaning.
`[code]` The calibration renderers do the opposite: they run one continuous phase per colour for the
whole file (`calibration/render_calibration.py:190-198`, `calibration/ab/render_ab.py:206-220`).

**Recommendation for KLOPGEEST:** continuous phase per carrier for the life of the render/plugin.
It is the calibration behaviour, it is deterministic, and it removes an accidental dependency. It
also means the C++ output will not be bit-identical to `render.py` for multi-event scores — hence
tolerance-based golden vectors (§8).

---

## 6. Programme preparation, band-limiting, output scaling and headroom

### 6.1 Programme chain (per event, mono, before panning)

`[code]` `render_program` (`render.py:312-340`):

```
for each event:
    impact:    synth_impact   → additive harmonic sweep + biquad-filtered noise, peak-normalised
    transform: reverse and/or linear-interpolating pitch shift of an earlier event
    mono = programme_lowpass(mono, fs, 15500 Hz, order 8)     # render.py:332
    mono = scale_to_peak(mono, peak_dbfs_of_the_event)        # render.py:333
    place at round(start_s·fs), pan with the EQUAL-POWER law, add into L/R   # render.py:335-339
```

`[code]` The low-pass is an 8th-order Butterworth as 4 cascaded RBJ low-passes
(`render.py:232-240`), with per-stage `Q_k = 1 / (2·sin((2k−1)·π/(2·order)))`, k = 1…4, implemented as
transposed direct-form II (`render.py:220-229`) in double. `[code]` It is skipped entirely if
`cutoff ≤ 0` or `cutoff ≥ fs/2` (`render.py:234-235`) — at 44.1 kHz, 15.5 kHz < 22.05 kHz, so it runs.

### 6.2 Is the band-limiting good enough?

`[measured]` `verify.py`'s own broadband probe (`verify.py:920-950`) pushes white noise through the
same filter at 48 kHz and measures attenuation relative to the 1 kHz band:

| Frequency | Attenuation | Needed for the control band |
|---|---|---|
| 17 000 Hz | **−19.2 dB** | ≤ −60 dBFS (M7) |
| 18 200 Hz | −34.0 dB |  |
| 19 700 Hz | −56.3 dB |  |

`[doc]` `verify.py:936-944` labels this honestly as **INFO, not PASS**: *"the −60 dBFS requirement is
demonstrated for the sparse, low-frequency synthesised programme material. For broadband v1 material
it is NOT demonstrated; the 8th-order LPF alone is not steep enough."*

**This is the single most important negative finding for KLOPGEEST.** An 8th-order Butterworth at
15.5 kHz is not a band-limiting guarantee; it happens to pass here because the programme is sparse
low-frequency synthesis with 90 % silence. A drum machine is broadband by definition. A KLOPGEEST
implementation needs either a much steeper filter (≥ 16th order / elliptic), a higher-order
oversampled design, or an explicit measurement that accepts the residual leakage — plus the same
INFO-not-PASS honesty in its own test report.

### 6.3 Quantisation and scaling

`[code]` `write_pcm` (`render.py:449-489`):

* `peak` is measured on the **unquantised** float mix, both channels (`render.py:452-453`), and
  returned as dBFS; it is what the manifest and the console output report.
* 24-bit: `v = int(round(x·8388607))`, clamped to [−8388608, +8388607] (`render.py:454-467`).
* 16-bit: `v = int(round(x·32767.0))`, clamped to [−32768, +32767] (`render.py:469-481`).
* `round()` in Python 3 is **round-half-to-even**; a C++ `(int)std::lround` is half-away-from-zero.
  They differ on exact .5 samples only — invisible in measurement, fatal to bit-exactness.
* No dither, no noise shaping, no metadata, no normalisation (`render.py:447`, `484-488`).
* Full-scale clipping is a hard clamp at ±32767/±8388607: the encoder does **not** protect headroom
  itself; the score has to keep it.

### 6.4 Headroom actually observed

`[measured]` full 72 s score, `render.py` console output and `verify-report.json`:

| File | Sample peak |
|---|---|
| `spectrastrobe-48k-24bit.wav` | −17.40 dBFS |
| `spectrastrobe-44k1-16bit.wav` | −16.96 dBFS |
| `program-only-44k1-16bit.wav` | −19.10 dBFS |
| reference alone (−30 dBFS carrier, ideal) | −30.000 dBFS |

The M9 gate is ≤ −6 dBFS sample peak **and** ≤ −6 dBTP; the true-peak estimator found no
inter-sample overshoot at all (−17.40 dBTP for a −17.40 dBFS sample peak), which is expected because
every component is a band-limited sine and the peak lands on a sample.

---

## 7. Requirement vs design choice vs Python convenience

| Item | Classification | Evidence |
|---|---|---|
| 18 200 / 18 700 / 19 200 / 19 700 Hz | **Format requirement** | `docs/v2/SPECTRASTROBE.md` §1.1, §6 ([doc] Lumasonic spec) |
| Reference present continuously as the amplitude scale | **Format requirement** | §1.2 |
| Reference panned L/R, sine, 20 Hz, depth 100 %, **per sample** | **Format requirement** (10–50 Hz range permitted) | §1.1, §6; `render.py:373-385` |
| Equal-power pan law for the reference | **Design choice** (doc says "constant carrier energy") | `render.py:113-117`; `score.json:42`, `53` |
| Linear 0 dB-centre pan for colours | **Design choice** with a rationale (no −3 dB decode loss) | `render.py:120-127` |
| Colour amplitude expressed as a ratio of the reference peak | **Format concept**; the *value* is not | §1.2; `render.py:416-417` |
| 1:1 vs 4:1 colour-at-full-brightness | **[open]** — conflicting primary sources | `RATIO-RESEARCH.md:188-196` |
| −30 dBFS reference, 20 % score level, 25 % ceiling, 6.25/12.5 % ladder | **Design/safety choices**, not spec | `score.json:21-24`, `52`; `ab.json:741-749` |
| Smoothstep breakpoint envelopes | **Design choice** (click-free, C¹) | `render.py:69-95` |
| 250 ms / 400 ms raised-cosine track fades | **Design choice**; inconsistent with the 300 ms smoothstep used by the calibration renderers | `render.py:98-107` vs `render_calibration.py:59-68` |
| ≥ 20 ms attack/release hard gate | **Design/safety requirement** (NAGLOED R4), not a codec rule | `render.py:172-177` |
| ≥ 50 ms for "artistic" events | **Documented, not enforced** except for the event named L2 | `verify.py:580-585` |
| Max 3 flashes/s, no 3–30 Hz modulation (R2) | **Documented, not implemented anywhere** | `SAFETY-PRIVACY.md:99`; absent from all five scripts |
| 8th-order Butterworth at 15.5 kHz | **Design choice**, insufficient in general | `render.py:232-240`; `verify.py:936-944` |
| Score duration must be 60–90 s | **Art-piece constraint**, not a codec constraint; `render_mix` itself has no such limit | `render.py:137-138` |
| Unbounded double phase accumulator | **Python convenience** (harmless); C++ should wrap in double | `render.py:386-387` |
| Colour phase reset at every event | **Accidental implementation detail** | `render.py:419` |
| `sin`/`cos` recomputed per sample instead of a recursive oscillator | **Implementation detail** (4 trig calls/sample); fine in C++, precompute a pan table if it ever matters | `render.py:373-387` |
| Precomputing the whole fade as a `count`-length array | **Python convenience**; C++ must compute from an index | `render.py:98-107` |
| `round()` half-to-even for PCM | **Python convenience** | `render.py:458`, `473` |
| `max_colour_ratio` default 0.25 in code, 0.25 in the score, exposures at 20 % of scale | **Three different numbers for three different jobs** — keep them separate | `render.py:145`, `357`; `score.json:52`, `21-24` |

---

## 8. Golden vectors for a C++ encoder

### 8.1 Principle

Generate the expected values **from `render.render_mix`**, not from the file outputs of
`render.py`'s CLI: `main()` hard-codes the 72 s art piece, four exposure variants and the
60–90 s validation, none of which belong in a unit test. `render_mix(score, fs, with_programme,
colour_scale, clamp_log)` is a pure function of `(score, fs)` `[measured]` (`render_mix` called twice
returns sample-identical arrays) and is what the verifiers already use for their own probes
(`verify.py:845-887`, `956-994`).

Commit **no WAVs**. Commit a JSON of expected scalars plus one reference WAV per signal at
24-bit/48 kHz if a bit-level diff is wanted in CI; otherwise the scalars are enough.

### 8.2 Measurement conventions — four traps found by running them

1. **The FFT window must lie completely inside the file.** `spectrum_pair` zero-pads beyond
   `len(left)` (`verify.py:174-177`). With `FFT_N = 65536` at 48 kHz the window is **1.365 s**, so for
   a 2 s vector any `t0 > 0.635 s` silently biases band power down. `[measured]` a window starting at
   1.65 s in a 2 s file reported the reference band at −59.9 dBFS instead of −30 dBFS, and the
   derived colour ratio as **663 % instead of 25 %**. Use `t0 ≤ 0.63 s` for 2 s vectors, or use
   `t0 = 0.5 s` in a 3 s vector (recommended).
2. **Do not use the reference's per-ear envelope as the denominator for colour ratios.** With a
   demodulation block longer than the 50 ms LFO period, a single ear's measured reference amplitude
   is `≈0.60 × amp_ref` (it is the LFO-modulated, not the peak, amplitude). `[measured]` that
   convention reports a 25 % colour as **41.5 %**. Use the reference **peak amplitude** —
   `max over 5 ms blocks of hypot(env_L, env_R)` (the M2 convention, `verify.py:654-660`), which
   `[measured]` gives −30.001 dBFS for a −30 dBFS reference — or, better, validate the reference peak
   once against −30.0 ± 0.1 dB and then divide by the analytic `10^(−30/20)`.
3. **The LFO metrics need ≥ 1 s of signal.** `lfo_metrics` demodulates in 1 ms blocks and searches
   18–22 Hz in 0.02 Hz steps by Goertzel (`verify.py:329-364`). `[measured]` on a 0.3 s window it
   returns 19.90 Hz / depth 95.6 %; on a 1 s window, 20.000 Hz / depth 100.00 % / corr −0.995 /
   rest −65.38 dB. Use ≥ 1 s, and use a **colour-free** window for the depth criterion (next point).

4. **Colour energy contaminates the reference-pan measurement.** `[measured]` depth and "off-phase
   rejection" as a function of a simultaneously active centre-panned red carrier (1 s window):

   | red ratio | depth % | rest dB |
   |---|---|---|
   | 0 (reference only) | 100.00 | −65.38 |
   | 6.25 % | 99.78 | −31.48 |
   | 12.50 % | 99.25 | −27.49 |
   | 20.00 % | 98.17 | **−23.52** |
   | 25.00 % | 97.20 | **−21.49** |

   At ≥ 20 % colour the M5 "≥ 30 dB down" criterion fails even though the reference panning is
   perfect. Measure the LFO on the reference-only vector, or window the demodulator (the calibration
   verifiers do exactly that with a 10 ms Blackman-Harris window,
   `verify_calibration.py:141-176`) — see the measurement-plan report.

### 8.3 Proposed test signals

All at 48 kHz, carriers only (`with_programme=False`), reference −30 dBFS, 20 Hz sine/equal-power
pan, 250 ms fade-in / 400 ms fade-out, colour linear 0 dB-centre pan — i.e. the NAGLOED constants.
Windows are given in the "measurement" column.

**V1 — reference only, 3.0 s, no light events.**
Purpose: carrier generation, level, panning; the cleanest vector for M1/M2/M5.

**V2 — reference + red at ratio 0.25, centre, 0.5–2.5 s, 20 ms smoothstep attack/release.**
Purpose: colour amplitude mapping at the ceiling, centre pan, both ears equal.

**V3 — same as V2 but panned hard-left (−1.0) for the whole event.**
Purpose: colour pan law and isolation; discriminates linear-0 dB-centre from equal-power.

**V4 — reference + red ladder: 0.0625 / 0.125 / 0.20 / 0.25, four 500 ms steps at 0.30 / 0.80 /
1.30 / 1.80 s, each with a 20 ms attack and 20 ms release, centre panned.**
Purpose: the mapping at four levels inside one file; catches a factor-2 or dB/linear mistake.
(Keep all steps between 0.25 s and 2.6 s: the 400 ms track fade-out starts at 2.6 s in a 3 s file
and `[measured]` it pulls a 25 % step measured at 1.65–1.75 s down to 21.2 % — the colour is faded by
the same `fade[]` array as the reference, `render.py:421`.)

**V5 — adversarial clamp: rgb = [0.90, 0.90, 0.90], one event 0.5–2.5 s, centre.**
Purpose: the 25 % clamp and its log.

Optional **V0 — programme chain**: one impact event from `score.json` at 48 kHz, programme only,
checked for 17–20 kHz energy ≤ −60 dBFS. This is the one that does *not* generalise (see §6.2); keep
it as a KLOPGEEST-specific test of whatever anti-alias filter the drum machine actually uses.

### 8.4 Expected values

`[measured]` on this machine, using the conventions in §8.2 (denominator = analytic `amp_ref =
0.0316227766`; 120 ms Hann single-bin demodulator for colours; 5 ms blocks for the reference peak;
1 s windows for LFO).

| Signal | Quantity | Expected | Tolerance |
|---|---|---|---|
| V1 | sample peak | −30.000 dBFS | ±0.05 dB |
| V1 | true peak | −30.000 dBTP | ±0.05 dB |
| V1 | reference peak (5 ms block max hypot) | −30.001 dBFS | ±0.10 dB |
| V1 | FFT peak, reference | 18 199.99 Hz | ±0.10 Hz |
| V1 | FFT peak, red/green/blue bands | numerically zero (−162 / −175 / −182 dB re reference) | ≤ −120 dB re reference |
| V1 | LFO frequency | 20.000 Hz | ±0.02 Hz |
| V1 | LFO depth | 100.00 % | ≥ 99.0 % |
| V1 | L/R power correlation | −0.995 | ≤ −0.98 |
| V1 | "off-phase" rejection | −65.4 dB | ≤ −40 dB |
| V2 | sample peak | −28.080 dBFS | ±0.05 dB |
| V2 | red amplitude, L and R | 0.00790569 each (25.000 % of reference) | ±0.05 dB, \|L−R\| ≤ 0.01 dB |
| V2 | green / blue amplitude | 0 (−185 / −195 dB) | ≤ −120 dB |
| V2 | FFT peak, red | 18 700.02 Hz | ±0.10 Hz |
| V3 | red amplitude L / R | 0.00790569 / 0.0 (−173 dB) | L ±0.05 dB, R ≤ −120 dB |
| V3 | sample peak | −28.100 dBFS | ±0.05 dB |
| V4 | red amplitude at the four steps | 6.250 / 12.500 / 20.000 / 25.000 % of reference | ±0.05 dB each |
| V4 | monotonicity | strictly increasing | — |
| V5 | red/green/blue amplitude, each ear | 0.00790569 each = 25.000 % | ±0.05 dB |
| V5 | sample peak | −25.406 dBFS | ±0.10 dB |
| V5 | clamp log | 3 entries, `requested_ratio` 0.9, `applied_ratio` 0.25 | exact |

`[measured]` 44.1 kHz parity for V1/V2: reference peak −29.93 dBFS (5 ms convention), red 25.000 %,
FFT peak 18 200.03 / 18 700.02 Hz — i.e. the same expected values hold at both rates within the
tolerances above, which is worth one extra row in the test matrix because 19.7 kHz sits at 89 % of
Nyquist at 44.1 kHz.

### 8.5 Comparing C++ output against the Python reference

**Level 1 — scalar conformance (must always pass).** Re-measure the C++ WAV with the same four
scalar measurements (sample peak, reference peak, per-carrier amplitude ratio, LFO frequency/depth)
and compare against §8.4 with the tolerances given. This is the test that protects against real
implementation errors (wrong pan law, factor 2, dB/linear mix-up, clamp applied after panning).

**Level 2 — numerical agreement.** `[measured]` `render_mix` is exactly reproducible in-process, so
the only unavoidable differences come from `sin`/`cos` last-bit behaviour and the integration order
of the phase accumulator. For a 3 s vector:

* With an unwrapped `double` accumulator and the same libm: expected max per-sample difference
  **< 1 × 10⁻⁷** (≈ −140 dBFS) — an inference from the accumulator analysis in §3.2, not measured
  against a C++ build — i.e. essentially bit-identical before quantisation, and within
  ±1 LSB after 16-bit quantisation.
* With a wrapped `double` accumulator: still < 1 × 10⁻⁶.
* With `float` phases: **do not compare samples** — see §3.2; the unwrapped case is −24 dBFS wrong.

Proposed assertion set, on a common window that excludes the fades (0.3–2.7 s of a 3 s vector):

```
max|x_cpp − x_py|            ≤ 1e-6        (both channels, full band)
rms(x_cpp − x_py) / rms(x_py) ≤ 1e-5
```

and after 16-bit quantisation: the two integer streams may differ by at most ±1 LSB.

**Level 3 — do not gate on file hashes across machines.** `[measured]` two renders with the same
score on this machine are byte-identical (six files, SHA-256 equal, confirmed independently of the
verifier's own M10 check). That is a determinism guarantee *per platform*, not across libm
implementations; a cross-platform hash gate would fail for the wrong reason.

### 8.6 Concrete generator contract

A `tools/spectrastrobe/golden.py` in KLOPGEEST should:

1. Build the five score dicts above as literals (no dependency on `score.json`).
2. Call `render.render_mix(score, 48000, with_programme=False, clamp_log=…)` — importing
   `render.py` and `verify.py` from a **pinned copy** under `tools/spectrastrobe/reference/`, with the
   sha256 of both files recorded in the output JSON (the NAGLOED manifest already does this,
   `render.py:775-778`).
3. Write the mix as 48 kHz / 24-bit PCM WAV and the scalars to `golden.json`, including the
   `render.py`/`verify.py` hashes and the expected values of §8.4.
4. Be re-runnable: `python3 golden.py --check` re-renders and diffs against the committed JSON,
   and fails on any drift beyond tolerance.

Keep the generated WAVs in `.gitignore`; keep `golden.json` in the repository. That keeps the test
self-describing, weights nothing, and lets a C++ test read expectations without Python.

---

## 9. C++ implementation notes (audio thread)

These are direct consequences of the audit, not a design proposal for KLOPGEEST.

* **State per encoder:** 4 double phase accumulators (reference + 3 colours, or 1 + N if flexible),
  1 sample counter / event timeline position, current envelope value(s), current pan gains. No
  recursion other than the phases → no denormal risk.
* **Sample-index driven, not block-rate.** Every value in the reference is a function of the absolute
  sample index (`fade[i]`, `env[j]`, `sin(phi)`). A block-based implementation that re-evaluates
  envelopes once per block will not reproduce it and will produce zipper noise at 128-sample blocks.
* **Double phases; wrapping optional.** Use `double`; wrap with `if (p >= 2π) p -= 2π` for
  conditioning (error ~1e-9 rad), accept the sub-LSB difference from the Python reference.
* **Two pan laws, not one.** Equal-power (cos/sin of π/4+π/4·p) for the reference; linear with unity
  in the centre for colours. They are not interchangeable.
* **Clamp before the pan gains**, per colour per event, so the per-ear bound 0.25·amp_ref holds by
  construction.
* **Nothing after the sum.** If KLOPGEEST's output stage has a limiter, saturation, EQ or DC blocker,
  the carriers must be added after it, exactly as `docs/v2/SPECTRASTROBE.md` §2.2 requires and
  NAGLOED's own U12 records as a strong indication.
* **Sample-rate change:** re-derive `d_ref`, `d_lfo`, `d_col` and the fade lengths from `fs`; the
  reference never changes rate inside a render, and its fade lengths are specified in ms, so a
  sample-rate change mid-stream must re-scale the fade index, not the phase.
* **No allocation in the audio path** (NAGLOED §6 "Realtime"): precompute the fade shape as a small
  table or compute `0.5 − 0.5·cos(πi/k)` inline; never build `count`-length arrays like `render.py`.
* **The 20 ms/50 ms gates and the missing transition-rate limit belong in KLOPGEEST's own enforcer**,
  not in a copied verifier: §5.3 and §5.4 show NAGLOED enforces one of them and documents two.
