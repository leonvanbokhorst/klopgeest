# 01 — SpectraStrobe signal specification for KLOPGEEST

**What this is.** The encoder contract a KLOPGEEST implementation must satisfy, written so it
can be coded without re-opening the research. Every normative statement carries a marker:

| Marker | Meaning |
|---|---|
| **[E]** | Established in a primary source; the quote and URL are in `evidence/source-verification.md`. |
| **[I]** | Inference — arithmetic from established numbers, or a technically forced consequence. The arithmetic is shown. |
| **[C]** | **Design choice by this project.** Not a property of the format. Retunable without breaking anything. |
| **[O]** | Open. Publicly undocumented; only Kasina hardware can close it. |

The full evidence trail (30 URLs, verbatim quotes, access log) is
[`evidence/source-verification.md`](evidence/source-verification.md). This document is the
distillation.

---

## 0. The one-paragraph model

SpectraStrobe hides a light control signal in an ordinary stereo audio file as **four high-frequency
sine carriers**. Three of them (red, green, blue) are light channels whose **amplitude** is the
brightness of that LED; the fourth (the reference) is a scale and a format flag, and carries no
colour. The playback hardware band-filters those four narrow bands out of the audio and routes them
to the glasses; everything else is heard normally. **Brightness is a ratio: colour carrier amplitude
divided by reference carrier amplitude.** That single sentence is the whole protocol, and getting it
wrong (implementing colour = reference instead of colour = 4 × reference) under-drives every colour
by 12 dB.

---

## 1. Carriers — **[E]**

| Channel | Frequency | Role |
|---|---|---|
| Reference | **18 200 Hz** | Format flag (SpectraStrobe vs AudioStrobe autosense) **and** the amplitude scale. Carries no colour. |
| Red | **18 700 Hz** | Red LED brightness = amplitude. |
| Green | **19 200 Hz** | Green LED brightness. *Also the AudioStrobe carrier* — this is why losing the reference degrades to AudioStrobe rather than to nothing. |
| Blue | **19 700 Hz** | Blue LED brightness. |

Established by four independent witnesses, including the format owner's own specification and two
implementations (a Nyquist generator literal `(hzosc 18200)` and an independent MIT implementation
with `fc=[18700,19200,19700,18200]`).

**Implementation requirements**

- **MUST** generate all four as pure sines at exactly those frequencies with a phase accumulator in
  **double** precision. Single precision loses phase coherence over a three-minute render at
  19.7 kHz.
- **MUST** keep the phase continuous across blocks. A phase reset per block is an amplitude step, and
  a step is a broadband click that decodes as a false flash.
- **MUST NOT** derive the carrier frequency from the host block rate. Carriers are a function of the
  **output sample rate only**.
- Sample rates: **44 100 and 48 000 Hz** are the documented, tested rates **[E]**. Higher rates are
  not forbidden by the format but are untested on the hardware **[O]**; below 44.1 kHz the carriers
  are impractical **[I]** — at 32 kHz, 19.7 kHz is 123 % of Nyquist and cannot exist.

### Sample-rate arithmetic **[I]**

| | 44 100 Hz | 48 000 Hz |
|---|---|---|
| Samples per cycle @ 18.2 kHz | 2.423 | 2.637 |
| Samples per cycle @ 19.7 kHz | 2.239 | 2.437 |
| 19.7 kHz as % of Nyquist | 89.3 % | 82.1 % |

Consequence: at 44.1 kHz the carriers are **2.24–2.42 samples per cycle**. Any resampling, any
filter rolling off above 17 kHz, and any lossy codec changes their amplitude and phase. This is not
a theoretical worry — it is the dominant real-world failure mode (§5).

---

## 2. The reference carrier — **[E]** for the shape, **[C]** for our numbers

The reference is a 18.2 kHz sine **hard-alternated between the two output channels** by a panning
LFO.

| Property | Documented value | KLOPGEEST |
|---|---|---|
| Panning | hard left ↔ hard right, crossfaded | same |
| Depth | 100 % | 100 % |
| Offset | 0 % (equal time each side) | 0 % |
| Rate | **10–50 Hz**, 20 Hz recommended | **20.0 Hz** |
| Waveform | Sine **or** square/pulse; the decoder is "quite tolerant" | **Sine** **[C]** |
| Phase | per-sample, phase-continuous | per-sample, not block-rate |

**Why sine and not the 50 %-duty pulse the original tooling used.** All three documented variants
work; the vendor warns that square/pulse panning introduces more audible artefacts **[E]**. KLOPGEEST
is a musical instrument whose output a listener hears, so the least-audible documented waveform is
the right default. This is a design choice, not a format requirement.

**Why the reference must exist at all — it has two jobs, and both matter:**

1. **It is the format flag.** The Kasina autosenses SpectraStrobe vs AudioStrobe. Without a valid
   panned reference it degrades to AudioStrobe (green-only) or to nothing **[E]**.
2. **It is the amplitude scale.** The encoder author states the reference "sets the dynamic range of
   the signal in a ratio of 4:1", and a forum moderator states plainly that *lowering* the reference
   makes the colours *brighter*, and too low drops the unit into AudioStrobe mode **[E]**.

Job 2 is the fact most likely to be got wrong. A reference treated as a mere flag leads directly to
the 1:1 error in §3.

**Requirements**

- **MUST** run continuously while light is enabled, including when the transport is stopped. Dark
  light is produced by zeroing the *colour* carriers; the reference is what keeps the unit in
  SpectraStrobe mode.
- **MUST** be present from the first sample of playback. No source documents a required lead-in
  **[O]**; the Kasina's "ten second countdown" is a player UI feature, not a lead-in rule. Do not
  design a silence into the start.
- **MUST** be the last thing that stops and the first thing that starts (it is the thing detection
  depends on).

---

## 3. Brightness — the ratio law, and the 1:1 / 4:1 conflict

**Brightness of a colour channel is its carrier amplitude divided by the reference carrier
amplitude.** Volume to −∞ is off. **[I]** — the ratio model is the only one that fits all the
primary statements; the decoder's transfer function itself is undocumented **[O]**.

### The conflict

| Source | Reference | Colour at 100 % | Ratio |
|---|---|---|---|
| Kasina encoder author (TomasC), 2019 | "1/4 of the color tones" | "−24 dB" | **4:1, +12 dB** |
| MindPlace developer (Hendrickson), 2015 | "normalized to −36db" | "each light channel normalized to −24db" | **4:1, +12 dB** |
| Hendrickson's own Audacity tooling | `scale 0.015` (≈ −36.5 dBFS) | `scale 0.075` (≈ −22.5 dBFS) | **5.0:1** |
| Normalise chains from the same repo | −36.5 dB | −22.0 dB | **≈ 4.8:1** |
| Lumasonic production/mixing table | — | "on reference level" | **1:1** |

Worked through in [`evidence/source-verification.md` §3](evidence/source-verification.md). Verdict:
**adopt 4:1.** Every numeric artefact produced by the people who make the format sits at +12 to
+14.5 dB. The 1:1 reading exists in exactly one mixing table, in a third-party vendor document whose
own author's tooling contradicts it, and it reads as a *direction* table ("move the colour fader up
toward the reference") rather than a calibration table.

**The reconciliation that makes all the statements true at once [I]:** brightness ∝ ratio, full
brightness is reached at a ratio well above unity, and the Lumasonic table describes the control
surface while losing the constant. Under that model a user who "matches colour to reference" gets
proportional-but-not-maximum brightness, exactly as the 2019 field report describes.

### KLOPGEEST numbers **[C]**

> **Canonical units — read this before any other number in the pack.** Three quantities are used
> throughout and they are **not** interchangeable; conflating them is a 12 dB error. Every document in
> this pack, and every parameter, name and test, should say which one it means.
>
> | Symbol | Quantity | Formula |
> |---|---|---|
> | **`r`** | **Colour-to-reference amplitude ratio**, per carrier, per ear. **The canonical internal quantity.** | `colourPeak / refPeak` |
> | **`f`** | Fraction of *format* full brightness | `f = r / kFullScaleRatio` |
> | **dBFS** | Absolute level, only meaningful once the reference level is fixed | `20·log10(refAmp · r)` |

**Fixed for the whole pack:** reference peak per channel **−36.0 dBFS** (`refAmp = 0.0158489`), and
`kFullScaleRatio = 4.0` (`f = r/4`).

| Meaning | `r` | `f` (% of full brightness) | dBFS at ref −36 |
|---|---|---|---|
| Dark | 0 | 0 % | −∞ |
| The one real hardware session (observed dim, no green/yellow) | 0.0625 | **1.56 %** | −60.08 |
| **KLOPGEEST default cap `rCap`** | **0.25** | **6.25 %** | **−48.04** |
| The vendor mixing table's "full brightness" (1:1) | 1.00 | 25.0 % | −36.00 |
| Format full brightness (4:1, the developer record) | 4.00 | 100 % | −23.96 |

**The encoder is defined on `r`, not on dBFS and not on a percentage:**

```
r        = clamp( rCap × intensity × mappingOutput , 0 , rCap )
colourAmp = refAmp × r            // 0.0158489 × r
refAmp    = 10^(-36/20)           // −36.0 dBFS peak per channel
```

| Parameter | Value | Why |
|---|---|---|
| Reference peak per channel | **−36.0 dBFS** | The developer-record operating point. |
| `kFullScaleRatio` | **4.0** | The format convention. One constant retunes every preset if hardware disagrees **[C]**. |
| `rCap` — the **hard product cap** | **0.25** (6.25 % of full brightness, −48.04 dBFS) | The conservative default, and the value enforced in the encoder. See §4. |
| `lightIntensity` 0…1 | `r = rCap × intensity` | 1.0 means *the cap*, not full brightness. |

**The cap is `r = 0.25`, not "100 % of full brightness".** Nothing in the instrument can reach `f > 6.25 %`
in normal operation. Stages above that belong to the research ladder in
[`06-hardware-validation.md`](06-hardware-validation.md) §3, run once, at low hardware brightness, and
never as a product setting.

**MUST** expose `Reference Gain` and `Color Gain` as independent trims, as the vendor's own encoder
does. That is what makes the 1:1/4:1 conflict non-fatal: if hardware testing shows dimness or
saturation, the fix is a parameter, not a re-architecture.

> **The observation that resolves most of the confusion, stated carefully.** The two positions in the
> conflict differ by exactly a factor of four, which is why they keep being mistaken for each other:
> **`r = 1.0` is "full brightness" to the vendor table and "25 % brightness" to the developers.** So
> the argument was never really about the ratio — it was about what 100 % means. Carrying `r` as the
> canonical quantity and `f = r/4` alongside it makes the disagreement visible instead of silent.

---

## 4. The conservative exposure question — and why it is a real tension

The inherited research convention caps each colour carrier at **`r = 0.25`** — 25 % of the *reference
amplitude*, which is **6.25 % of full brightness**, i.e. −48.04 dBFS against a −36 dBFS reference.

**This is a genuine, unresolved tension, and builders must not paper over it.** A real hardware
session on a Kasina + DeepVision, at reference −30 dBFS with colours at `r = 0.0625`
(−54.1 dBFS absolute), showed **red, blue and violet and left/right asymmetry, but no convincing green
or yellow**, and colour crossfades read as fading to black. At `r = 0.0625` that configuration was
**1.56 % of full brightness** — about 36 dB below the developer-record full-brightness level.

The gap to `rCap = 0.25` is only 12 dB, i.e. `f` rises from 1.56 % to 6.25 %. **Both ends of that range
are far below the vendor's own "full brightness" point.** That is the crux: the entire product range
may sit under the decoder's useful floor, and no offline measurement can say whether it does.

What that does and does not prove:

- **Does** show the format decodes on real hardware at low colour levels (colours were seen).
- **Does not** establish a cause for the missing green/yellow or the black crossfades. Candidate
  explanations that were not separated: colour level below the decoder's usable threshold; unequal
  per-colour LED/decoder gain; the observer's colour vision; DeepVision filter behaviour. A
  controlled A/B ladder was built for this but has not been run on hardware.
- **Does** constrain KLOPGEEST's design: **do not build mappings whose meaning depends on accurate
  colour identity.** At conservative levels green and yellow may be weak or absent. See
  [`03-light-engine-design.md`](03-light-engine-design.md) §2.

**KLOPGEEST position [C]:** ship at `rCap = 0.25` (6.25 % of full brightness), let `lightIntensity`
scale `r` from 0 to `rCap`, and resolve the usable range with the hardware ladder in
[`06-hardware-validation.md`](06-hardware-validation.md) before any preset names a specific colour.
Do not raise the default to compensate for dimness — resolve the ratio question first.

---

## 5. Program path, headroom and injection order

### Order of operations **[E]** / **[C]**

1. Produce the programme completely, through all its normal DSP.
2. **Band-limit the programme** so it does not put energy in 17–20 kHz (§6).
3. **Add the carriers.** Nothing after them: no EQ, no saturation, no limiter, no normalisation, no
   dither.

The vendor's production guidance puts this bluntly, and a forum moderator gives the transferable
production form: *"It is best to isolate the light tracks into a folder or bus and apply the limiter
to the other soundtracks"* **[E]**. For KLOPGEEST that is a hard architectural rule: **carriers must
never enter the glue machine.**

### Headroom arithmetic **[I]**

The reference and all three colours can be present at once, and all three colours can be at full level
in the same ear simultaneously. The reference is hard-panned, so at most one ear sees it at any
instant — the conservative assumption is that it is fully present. Coherent (worst-case) sum:

| `r` | 3 colours + reference | Sum, dBFS |
|---|---|---|
| 0.25 (**the product cap**) | 3 × 0.003962 + 0.015849 = 0.027736 | **−31.14** |
| 1.00 (vendor "full brightness") | 3 × 0.015849 + 0.015849 = 0.063396 | −23.96 |
| 4.00 (format full brightness) | 3 × 0.063396 + 0.015849 = 0.206036 | −13.72 |

So the carriers alone never clip. But they share the digital ceiling with the music.

### The rule: reserve what is actually needed, not a fixed number

**Do not hard-code a −6 dBTP programme ceiling.** That over-attenuates by 5 dB at the product cap and
makes the light-on render audibly quieter for no reason — which in turn makes the "audio is unchanged"
acceptance test impossible to pass (see [`05`](05-acceptance-and-tests.md) §4 C1).

**Correct rule [C]:** compute the reservation from the carrier cap in force.

```
carrierSum   = 3 · refAmp · rCap + refAmp           // coherent worst case
programmeCeiling = min( userCeiling , 10^(TOTAL_PEAK_TARGET/20) − carrierSum )
TOTAL_PEAK_TARGET = −1.0 dBFS                        // design target, not a format requirement
```

| `rCap` | carrierSum | programme ceiling | Reduction vs the −0.4 dBFS default |
|---|---|---|---|
| 0.25 | −31.1 dBFS | **−1.27 dBFS** | **0.9 dB** |
| 1.00 | −24.0 dBFS | −1.64 dBFS | 1.2 dB |
| 4.00 | −13.7 dBFS | −3.29 dBFS | 2.9 dB |

At the product cap the programme is attenuated by **less than 1 dB** — inaudible in practice, and
measurable in a test. **This attenuation is a declared, documented change, not a hidden one.**

**Requirements**

- **MUST** reserve carrier headroom explicitly, by this rule.
- **MUST** expose the computed ceiling and the resulting attenuation in `VisualState` so the
  behaviour is observable, not inferred.
- **MUST NOT** achieve the reservation by scaling the carriers with the programme level — that turns
  programme dynamics into brightness modulation, which is exactly the failure the format warns about.
- **MUST NOT** put a limiter or clipper after the carriers. Clipping a 19 kHz sine produces harmonics
  and intermodulation across the whole band: audible, and decoded as arbitrary colour.

---

## 6. Keeping the program out of the control band

The carriers are only **500 Hz apart**. Energy anywhere near 18–20 kHz bleeds between channels **[E]**:
the encoder author's own encoder low-passes the audible audio "starting to cut at about 13 kHz" and
"practically everything off above 17 kHz" **before** adding the carriers, precisely because otherwise
"fast edges of pulses or modulation generate overtones / noise which often leaks to the other
SpectraStrobe channels" **[E]**.

**This is the requirement most likely to bite KLOPGEEST**, because KLOPGEEST is a percussion
instrument: hats, claps and ODD are broadband and transient-rich by design.

- **MUST** apply a low-pass to the programme when light is enabled, with meaningful attenuation by
  17 kHz. Specify the filter by **measured outcome**, not by order: the inherited reference
  implementation used an 8th-order Butterworth at 15.5 kHz, which measures only **−19 dB at 17 kHz**
  on broadband material and passes only because that project's programme was sparse sub-1.2 kHz
  synthesis. It is **not** adequate here.
- Recommended **[C]:** measure the actual programme's spectrum and require **≤ −60 dBFS in each of
  the four carrier bands**, and at minimum ≤ −40 dB relative to the carrier in that band. If a
  minimum-phase 8th-order filter at 15 kHz does not get there for hats/claps, either go steeper or
  accept a lower programme ceiling — do not silently ship the shallow filter.
- Alternative **[C]**: **band-stop the four carrier bands** (four narrow notches at
  18.2/18.7/19.2/19.7 kHz) instead of low-passing the whole programme. This is not vendor-documented,
  so it is an experiment, not a substitution.
  - **What it buys:** the *magnitude* response in the audible band stays flat, so the music is
    spectrally unchanged. That is a materially better outcome than a 15 kHz low-pass on a percussion
    instrument, and it is why it is worth trying first.
  - **What it does not buy:** an IIR notch shifts phase well below its own corner, so the samples
    below 16 kHz are **not** bit-identical to a light-off render. Do not claim they are. See
    [`05`](05-acceptance-and-tests.md) §4, criterion C1, which is written to survive exactly this.

---

## 7. Envelope and modulation limits — the format's own physics

**Every amplitude change of a colour carrier is frequency modulation of that carrier.** A modulation
edge of rise time *t* spreads energy over roughly *1/t* Hz. With 500 Hz spacing:

| Rise time | Sideband spread | Consequence |
|---|---|---|
| 1 ms | ~1 kHz | **Crosses into the adjacent colour channel.** Colour crosstalk. |
| 5 ms | ~200 Hz | Inside its own band; marginal. |
| 20 ms | ~50 Hz | Clean. |
| 100 ms | ~10 Hz | Clean and visually smooth. |

**Requirements**

- **HARD MUST:** minimum rise and fall time per light channel **≥ 5 ms**, for signal reasons.
- **HARD MUST:** minimum rise and fall **≥ 20 ms** for artistic events, for safety reasons
  ([`04-safety-and-opt-in.md`](04-safety-and-opt-in.md) R4).
- **MUST** interpolate the six per-ear gain values **per sample**, not per control block. A gain held
  constant for 48 samples and then stepped is a 1 kHz modulation.
- **MUST** start and end every light event at zero amplitude, with zero slope at both ends. The
  concrete meaning: a cubic smoothstep `u²(3−2u)` between breakpoints, evaluated from the sample
  index.

---

## 8. Export and file discipline — **[E]**

| Property | Required |
|---|---|
| Channels | Stereo. **Never** mono-sum — that cancels the reference panning and drops the unit to AudioStrobe. |
| Sample rate | 44 100 or 48 000 Hz. |
| Bit depth | 16-bit signed PCM. 32-bit technically possible, not recommended for card playback. |
| Dither / noise shaping | **Off.** |
| Metadata | **None.** A WAV metadata chunk on older firmware played as audio at the end of the file: a loud noise burst plus a bright flash. Fixed in later firmware — do not rely on the firmware being current. |
| Container | WAV. Wave64 is **not supported** by the Kasina. |
| Compression | MP3 only at 320 kbps CBR, **Stereo**, never joint/intensity stereo, never VBR. MindPlace's own floor is 256 kbps CBR. |
| Bluetooth / OS audio processing | Not compatible. Do not document it as supported. |

For KLOPGEEST specifically: the plug-in produces audio, not files, and cannot control what the host
or the user does downstream. Everything in this table therefore becomes **documentation and a
diagnostic**, not an encoder requirement — see [`04-safety-and-opt-in.md`](04-safety-and-opt-in.md)
§7 for what to tell the user and what to detect.

---

## 9. What the hardware does — established and not

**Established [E]:** six independent light channels (RGB × each eye), a TRRS glasses output, an LED
brightness control independent of audio volume, and per-colour / per-eye brightness balance defaulting
to 100 %.

**Open [O], and these are what hardware testing exists to close:**

1. The decoder's **level → brightness law**. Ratio? Gamma? Threshold? AGC? Not public. Everything in
   §3 rests on a model, not a measurement.
2. The **filter topology, order and bandwidth** of the four decoder band-passes.
3. **Detection thresholds and autosense timing** — how quiet a reference can be before it is missed.
4. **Behaviour on abrupt signal loss** — does the decoder hold the last brightness? This determines
   whether the stop order (colours first, then reference) is sufficient.
5. **Absolute luminance** of the LEDs at any setting (cd/m²), and the per-colour efficiency ratio.
6. Whether **24-bit / 48 kHz** files play from the card.

**Do not state any of these as fact in code, comments, UI text or documentation.**

---

## 10. Summary of MUSTs for the implementer

1. Four sine carriers at 18 200 / 18 700 / 19 200 / 19 700 Hz, double-precision phase accumulators,
   phase-continuous across blocks.
2. Reference: 20 Hz sine hard-pan, 100 % depth, per sample, running whenever light is enabled.
3. `colourAmp = referencePeak × 4.0 × brightness`, with brightness clamped and both gains trimmable.
4. Carriers added **after** the glue machine and after any master processing; nothing after them.
5. Programme low-passed (or notched) so it puts ≤ −40 dB, target ≤ −60 dBFS, into the carrier bands.
6. Per-sample gain interpolation; ≥ 5 ms edges for signal integrity, ≥ 20 ms for safety.
7. Stereo only; refuse or warn on mono, on sample rates outside 44.1/48 kHz, and on any downstream
   stage that will resample.
8. Output headroom reserved when light is on, computed from the carrier cap in force (§5) — not a
   fixed ceiling. At the product cap the attenuation is 0.9 dB.
9. No dither, no metadata, no limiting, no saturation, no EQ after the carriers.
10. Nothing in this document may be presented to a user as a safety or health claim.
