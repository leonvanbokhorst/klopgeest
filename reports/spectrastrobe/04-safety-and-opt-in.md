# 04 — Safety, opt-in, panic and routing

**What this is.** The non-negotiable constraints for any KLOPGEEST light feature, and the concrete
mechanisms that enforce them in code rather than in intention.

**This is not medical or legal advice, and KLOPGEEST must never present it as such.** The rules below
reduce risk; they do not prove safety. The framing throughout is *"conservative, with an immediate
stop"*, never *"safe"*.

Sources for the safety thresholds are in [`evidence/source-verification.md`](evidence/source-verification.md)
and in the inherited research, summarised in §2 with its citations. Anything marked **[O]** is not
established and must not be stated as fact in code, UI or documentation.

---

## 1. Why this section exists and cannot be optional

KLOPGEEST is a **rhythm instrument**. Its natural output is 8–13 events per second. A light output
that mirrors that is a strobe. The feature is therefore unusual: the safety constraint is not a limit
on an otherwise-safe idea, it is **the thing that makes the idea good**. Read §3 as a design resource,
not a restriction.

Two independent risks converge:

1. **Photosensitive seizures.** Saturated flashing light at 3–30 Hz, filling the visual field.
   A Ganzfeld headset fills the visual field and sits close to the eyes, which removes the two
   mitigations a screen has (distance and small stimulus area). WCAG thresholds are intended for
   screens; for a full visual field they are a **lower bound, not a safe upper bound**. **[I]**
2. **Hearing.** The carriers are 18.2–19.7 kHz. They are **not guaranteed inaudible** — that depends
   on age, hearing, transducers and level. **[O]**

---

## 2. The evidence behind the thresholds

| Claim | Status |
|---|---|
| ~3 % of people with epilepsy have photosensitive epilepsy; ~5 % among children and young people. | **[E]** Epilepsy Society factsheet |
| Common trigger band **3–30 flashes per second**; some sensitive to 60 Hz; below 3 Hz uncommon. | **[E]** same |
| WCAG 2.3.1 level A: **no more than three flashes in any one second**, or below the general/red flash thresholds. A flash is a pair of opposing luminance changes of **≥ 10 %** of maximum relative luminance. | **[E]** W3C |
| **Saturated red is the most provocative** colour. | **[E]** W3C red-flash threshold rationale |
| Risk increases when the effect **fills the visual field**, the background is dark, and the person is tired. | **[E]** Epilepsy Society |
| UK TV rules: ≤ 3 Hz; HSE advice for public performances: ≤ 4 Hz. | **[E]** Epilepsy Society |
| The DeepVision headset is eyes-open, higher maximum brightness, diffuses into a colour field, and appears **much brighter**. | **[E]** vendor device doc |
| The vendor of the encoder warns its output can be **stroboscopic**, is not intended for people with epilepsy or any seizure disorder, and advises consulting a doctor. | **[E]** vendor health & safety |
| The actual luminance (cd/m²) and flash duration of the LED output at any setting. | **[O]** |
| Whether the decoder adds luminance modulation of its own. | **[O]** |
| Whether a decoder holds the last brightness after signal loss. | **[O]** |

**The gap that must be stated in the UI, not hidden:** every quantitative threshold above is
expressed for *screens*. Nobody has measured a Kasina Ganzfeld in cd/m², and KLOPGEEST cannot claim
its output is below the WCAG thresholds in the physical sense. It can only claim its **modulation
rate and modulation depth in the encoded signal** are bounded.

---

## 3. Hard rules

Adapted from the inherited research rules R1–R12, rewritten for KLOPGEEST.

| # | Rule | Enforced by |
|---|---|---|
| **K1** | **Off by default.** No carrier is generated until the user explicitly enables light. | `lightEnabled` default false; encoder returns immediately when false. |
| **K2a** | **Flash count.** At most **3 flashes** — that is, at most **6 transitions** — in **every closed 1-second window** (`fs` sample periods, `fs + 1` sample instants; `n − t = fs` rejects). A flash is one opposing *pair* of ≥ 10 % luminance changes, per WCAG 2.3.1. | Governor rule **R1** (`03` §5.1): exact sliding-window accounting on integer sample counts, not a token bucket. A hard encoder limit, not a UI recommendation. |
| **K2b** | **Design pulse rate.** Consecutive **same-direction** transitions at least **400 ms** apart, i.e. a luminance fundamental of **≤ 2.5 Hz**. | Governor rule **R2** (`03` §5.1). |
| **K2c** | **Neither K2a nor K2b is a statement about hardware light.** They bound a computed *luminance proxy*. Photon output depends on per-colour LED efficiency, which is **[O]**. | Test B2/B2b measure the signal; only the photodiode test ([`06`](06-hardware-validation.md) T6) measures light. |

> **Why K2 is two rules, not one.** A rolling count cannot bound a periodic frequency. The clean
> counterexample is a **2.75 Hz square wave**: 5.5 transitions/s, so at most 6 in any closed 1 s
> window — it satisfies **K2a** — while its same-direction spacing is **364 ms**, breaking **K2b**.
> K2b enforces the rate; K2a catches irregular bursts that K2b cannot see. Both are enforced in the
> governor. An earlier draft merged them into one "≤ 3 flashes and ≤ 2.5 Hz" sentence and thereby
> guaranteed neither.
>
> Note that a **3 Hz** square is *not* the counterexample: with a closed window its transitions land
> every 1/6 s and **seven** fall inside `[0, 1]`, so K2a bites too.
| **K3** | **Smooth envelopes.** Attack and release **≥ 20 ms hard** for every artistic light transition, ≥ 50 ms for deliberate events. | Slew limiter before the encoder; a config that violates it is clamped, not honoured. **One explicit exception: panic (K10) — see below.** |
| **K4** | **No step from black to full, and no step to black.** Every light event starts and ends at zero amplitude with zero slope (cubic smoothstep). | Envelope generator shape; asserted offline. |
| **K5** | **Make before break.** A colour transition reaches its target before the outgoing one begins to fall. Overlap ≥ 100 ms. | Transition logic in the encoder. |
| **K6** | **Red gets a lower ceiling** than green and blue (saturated red is the most provocative). | Per-colour ceiling multiplier: red 0.7, green 1.0, blue 1.0 of the intensity cap. **[C]** conservative default. |
| **K7** | **The intensity ceiling is enforced in the encoder**, not in the UI. A preset, a corrupted state or a future feature cannot exceed `r = rCap` (default 0.25 — see the canonical units in [`01`](01-signal-spec.md) §3). | `r = clamp(rCap · intensity · mapping, 0, rCap)` applied after every other scaling. |
| **K8** | **The reference carrier is not an artistic channel.** Its 20 Hz panning may not be used, amplified or made visible. | It carries no colour; the mapping API cannot reach it. |
| **K9** | **Every session starts dim.** The stored intensity is restored, but the stored *previous brightness ramp* is not; nothing auto-ramps up. | No "resume where you were" for intensity. |
| **K10** | **Panic within 50 ms, click-free.** Colours to zero first, then the reference, each with a ≤ 5 ms ramp. | §4. **The one documented exception to K3** — and an exception in kind, not degree: panic is a stop, not an artistic transition. Single, isolated, at most once per session. |
| **K11** | **Disabling light restores the instrument exactly.** Audio, timing, state, CPU behaviour and host sync are unchanged — bit-identically. | C1a in [`05`](05-acceptance-and-tests.md) §4. |
| **K12** | **No "safe" claim anywhere.** "Conservative", "with an immediate stop", "signal-safe file". Never "safe", "healing", "calming", "entrainment", "therapeutic". | Review; a grep in CI is cheap. |

### What the 2.5 Hz figure is and is not

It is **below** the WCAG 3-per-second limit deliberately, because a drum machine produces *regular*
repetition over minutes, where the boundary rules assume brief sequences. It is a KLOPGEEST design
margin, not a threshold from any source. Do not attribute it to a standard.

---

## 4. Panic — the mechanism, not the promise

The inherited research was explicit that an offline WAV has **no** panic and its primary stop is
human: stop playback, brightness down, glasses off. **A plug-in has no such excuse.** If the
instrument can produce the light, it must be able to stop it.

### Requirements

| # | Requirement |
|---|---|
| P1 | The panic is a **parameter-level atomic**, read in `processBlock`, not a message-thread action. It works when the host transport is stopped, and it takes effect at the top of the **next** callback. |
| P1a | **Know its limit, and say so in the manual.** If the host stops calling `processBlock` — a hung audio thread, a frozen app, a suspended device — no plug-in-side atomic can execute, and the last block may repeat. **The plug-in-side panic is therefore best-effort, not a guarantee.** The guarantees that always work are physical: glasses off or one eye covered (the fastest), then the hardware's own LED-brightness control to minimum, then power. Never present the software panic as the primary stop. |
| P2 | It is **visible in the editor at all times** when light is enabled, with a keyboard shortcut. Never behind a menu, a disclosure or a settings tab. |
| P3 | It **also fires** on: `prepareToPlay`, `releaseResources`, `setStateInformation`, preset change, host bypass, and the MIDI all-notes-off/all-sound-off path. A preset change must not leave a colour carrier running. |
| P4 | The stop **order is colours first, then the reference**, each with a ≤ 5 ms ramp, completed within 50 ms. Reason: on sudden signal loss the decoder may hold the last brightness, and a persistent bright image is worse than a slow fade. This ordering is a **precaution whose necessity is [O]** — it is cheap, so do it. |
| P5 | `Engine::reset` resets light state too (hook **H6**). Today `Engine::reset` resets voices and sequencer only. |
| P6 | After panic, light stays off until the user re-enables it. No automatic recovery, no "it's fine now". |

### K3 vs K10 — why the 5 ms panic ramp is allowed

The two rules look contradictory, so a builder will ask. They are not:

- **K3 governs artistic transitions** — the light changing because the music changed. Those are the
  ones the 20 ms floor exists for: they repeat, they are part of the composition, and they carry both
  the crosstalk risk and the flash risk.
- **K10 governs a stop** — the light going away because a human demanded it. It happens once, it is
  followed by silence, and a slow stop is strictly worse than a fast one. A 20 ms floor here would be
  a safety regression dressed as consistency.

Make the distinction explicit in the code: **the panic path must not pass through the artistic slew
limiter.** If it does, panic silently becomes a 20 ms fade and the 50 ms target is missed.

**On the 50 ms figure:** this is an inherited requirement and it is a *target*, not a measurement.
It has never been measured on hardware (you would need a photodiode). State it as a design target in
code comments.

---

## 5. Opt-in UX — what the user must be told, once

Enabling light for the first time shows a short, plain, non-medical acknowledgement. Draft text:

> **Light output.** KLOPGEEST can drive SpectraStrobe LED glasses through an audio output.
>
> - It is **off by default**. When on, very high-frequency tones are added to the output. They are
>   usually inaudible, but not to everyone — check at a low level first.
> - The light is designed to change **slowly** and never to strobe. The rate and brightness limits
>   are built in and cannot be raised past the ceiling.
> - The signal must reach the glasses **unprocessed**: no EQ, no loudness normalisation, no spatial
>   audio, no Bluetooth, no lossy compression, and the output must stay **stereo** at 44.1 or 48 kHz.
> - **Do not use this with anyone who has, or may have, photosensitive epilepsy.** If you are unsure,
>   do not enable it. This is not a medical device and makes no health claim.
> - If light is ever uncomfortable, use **PANIC** (⌘.), turn the glasses' brightness down, or take
>   them off.

Rules for the text:

- It is shown **once**, and the acknowledgement is stored with the plug-in state.
- It is **not** a modal that can be dismissed by pressing Enter accidentally — require a deliberate
  click on an enabled button.
- No fear-marketing, no wellness language, no claimed benefit of any kind.
- Update the string if the ceiling or the rate limit changes.

---

## 6. Status the user must be able to see at a glance

Publish these in `VisualState` (hook **H7**) and show them in the editor:

| Indicator | Why |
|---|---|
| Light **on / off** | The single most important state. Never ambiguous. |
| Current **mapping personality** | Which composition is running. |
| Current **intensity** (as a fraction of the cap, not as a percentage of "loudness") | What the ceiling currently is. |
| **Governor active** — the light is being rate-limited right now | Honest: the light is not doing what the mapping asked. Visible throttling is a feature, not an embarrassment. |
| **Unsupported configuration** — mono bus, sample rate outside 44.1/48 kHz | See §7. |
| **Panic armed / latched** | Whether the stop is available and whether it has fired. |

The instrument's existing "honesty layer" philosophy (`VisualState.h` shows *how far the groove bent
the hit*, not just the hit) applies directly: **show the light being governed, not just the light.**

---

## 7. Routing and the downstream chain — what KLOPGEEST can and cannot control

KLOPGEEST produces audio. It does not produce a file and it does not own the signal path after its
output bus. That changes the risk picture compared with an offline renderer.

### What the plug-in MUST do

| # | Requirement |
|---|---|
| R1 | **Stereo only for light.** Detect the output channel count. In a mono instance, disable light with a visible `lightUnsupported` status — never silently emit a scheme that loses its L/R information. |
| R2 | **Sample-rate guard.** 44.1 and 48 kHz are the documented rates. At other rates, warn. Never resample internally to fake it. |
| R3 | **Never put the carriers inside a limiter, EQ or saturator.** The glue machine is before the injection point; nothing may come after it (`02` §3). |
| R4 | **Reserve headroom.** With light enabled the programme ceiling becomes `min(userCeiling, 10^(−1/20) − carrierSum)`, computed from the carrier cap actually in force — **not a fixed −6 dBTP**. At the product cap the reservation is **0.9 dB**, not 5.6 dB (`01` §5). |
| R5 | **No dither, no noise shaping, no metadata, no normalisation** added by the light stage. |
| R6 | **Document the downstream conditions** in the manual: no Bluetooth, no OS EQ/spatial audio/loudness processing, no lossy re-encode below 320 kbps CBR stereo, no mono-summing, no resampling. And say *why*: they destroy or corrupt the control signal, not because KLOPGEEST is fragile. |
| R7 | **Detect what is detectable, warn about the rest.** The plug-in can see the bus layout and the sample rate. It cannot see the user's OS audio chain or a downstream Bluetooth link. Do not imply it can. |

### What the plug-in must NOT do

- **Do not add a second output bus.** It breaks `isBusesLayoutSupported`, the mono path and
  `pluginval`, and it is not needed: the light rides in the same stereo signal by design.
- **Do not offer "route channels 3/4 to the glasses".** That redefines the instrument's I/O contract
  for one optional feature.
- **Do not claim host-independence.** Whether a host's signal path preserves 18–20 kHz is host- and
  user-dependent and unmeasured. **[O]**

### A note on the standalone app

The standalone build gets its device, sample rate and buffer size from JUCE's Audio/MIDI Settings
dialog. It cannot choose channel count or routing without new standalone code. That is fine — the
light rides the main stereo output — but it means the **standalone is the recommended way to drive
glasses**, because the path is short and inspectable, and the plug-in is the path with unknowns.

---

## 8. What KLOPGEEST must never say

- That the light is safe, or safe for a group. It is conservative and it can be stopped.
- That it entrains, relaxes, focuses, heals, or improves anything.
- That it is a medical device, a therapy, or a measurement of anything.
- That a given brightness is "equivalent to X cd/m²", "below the WCAG threshold", or "verified
  flicker-free on hardware" — none of these has been measured.
- That the SpectraStrobe decoder's behaviour is known. It is not **[O]**.

A CI grep for `entrain|therap|heal|calm|safe for|medical|wellness` across `src/ui` strings, the
manual and preset names is cheap and worth adding.

---

## 9. Open safety questions this research could not close

| # | Question | Why it matters | Route to close it |
|---|---|---|---|
| S1 | Actual luminance (cd/m²) and flash duration of a Kasina at any setting, standard vs DeepVision. | Every absolute threshold comparison is impossible without it. | Photodiode + calibrated meter, in a dark room. |
| S2 | Does the decoder add luminance modulation of its own? | Decides whether a clean encoded signal is actually flicker-free at the eye. | Photodiode + FFT of the LED output during a static encoding. |
| S3 | Does the decoder hold the last brightness after signal loss? | Decides whether the "colours first, then reference" stop order is sufficient or whether the reference must be faded slower. | Hardware: cut the signal, watch the LEDs. |
| S4 | Minimum visible drive per colour, on this hardware. | Decides whether a 25 % ceiling is usable at all. | The A/B ladder in [`06`](06-hardware-validation.md). |
| S5 | Are the carriers audible to the user at the delivered level? | Hearing comfort; the documentation currently says "usually inaudible, not to everyone". | A 10 s comfort listen at the intended level, per session. |
| S6 | Does any of this need a legal/regulatory review before publication? | Selling an instrument with a light output in the EU may have obligations this research cannot assess. | A lawyer, not an engineer. Explicitly out of this mandate. |
