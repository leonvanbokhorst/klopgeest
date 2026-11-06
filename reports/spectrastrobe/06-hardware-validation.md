# 06 — Validating on a real Kasina

**What this is.** The protocol for the one thing no offline test can settle: whether a Kasina plus
Ganzfeld glasses does anything useful with a KLOPGEEST light signal, and what a person makes of it.

**This is not a formality.** Issue #6's definition of done ends with *"een gedocumenteerde echte
Kasina-test"*, and the research question — *when does light become a rhythmic representation rather
than a visualisation?* — is **only** answerable here. Everything in `01`–`05` is preparation for this
document.

**Safety.** [`04-safety-and-opt-in.md`](04-safety-and-opt-in.md) is binding and is not summarised
here. Read it first. No person with known or suspected photosensitivity in the room. One operator, one
watcher. Stop within reach. No medical or wellness claims at any point.

---

## 1. What is already known from a previous session

One real session happened in the predecessor project, on a Kasina + DeepVision, with a well-controlled
offline-verified file: reference 18.2 kHz at −30 dBFS peak with a 20 Hz pan, colours at 6.25 % of the
reference per carrier.

**Observed:** red, blue and violet, plus left/right stereo asymmetry, but **no convincing green or
yellow**. Colour crossfades were **perceived as fading to black** between colours, although offline
verification found **0.0 ms** of encoded black gap.

**What that establishes:** the format decodes on real hardware at a low colour level, and a correct
ferrogram does not guarantee a correct percept.

**What it does not establish:** any cause. The candidate explanations — colour level below a decoder
or LED threshold, unequal per-colour gain, the diffuser, observer colour vision, decoder nonlinearity,
or contrast adaptation — were **not separated**. A controlled A/B ladder was built but **has never
been played on hardware**.

**What it constrains:** the design rules in [`03`](03-light-engine-design.md) §9 — do not encode
meaning in colour identity, make before break, allow a per-colour balance trim. And it sets the
priority order below: **find the usable level and the colour behaviour before judging any mapping.**

**What must not be inherited as fact:** the cause of the missing green or the black crossfades. They
are open hypotheses. Do not write an explanation into the codebase.

---

## 2. Equipment and preparation

| Item | Requirement |
|---|---|
| Kasina | Current firmware. Verify the version and record it. |
| Glasses | Standard Ganzframes (eyes closed) **first**; DeepVision (eyes open, brighter) only after a comfortable ladder. |
| Media | Files on the microSD card, no metadata, short file names. WAV 44.1 kHz / 16-bit. |
| Source | The KLOPGEEST **standalone** app on the Mac, line out → Kasina AUX. Not the plug-in. The plug-in path has unmeasured downstream stages. |
| Level | One fixed, recorded Mac output level for the whole session. Not "as loud as needed". |
| Kasina brightness | Fixed, recorded, **low**. Not maximum. Audio volume and LED brightness are separate controls on this device. |
| Photodiode *(strongly recommended)* | BPW34 or similar + an audio interface. Without it every brightness claim is an observation, not a measurement. |
| Not in the chain | Bluetooth, OS EQ, spatial audio, loudness normalisation, any lossy re-encode. |

**Before the session:** a 10 s carriers-only comfort listen at the intended level. Ask about
audibility, sharpness, discomfort. With tinnitus, hyperacusis, young ears or doubt: lower the level or
do not take part. This is a comfort measure, not a medical test.

---

## 3. Stage order — do not skip, do not parallelise

Each stage answers one question and gates the next. A failure at any stage is information, not
embarrassment; record it and stop.

### T0 — Detection

Play reference-only (colours at 0) for 60 s.

- **Pass:** the Kasina displays `Mode SpectraStr/AS` with **SpectraStr** active.
- **Fail:** it shows AudioStrobe or nothing. That is a **format or panning defect**, not an artistic
  problem. Check in this order: correct sample rate actually playing; reference continuously present;
  LFO present with 100 % depth; not mono-summed; no EQ/normalisation/gain plugin in the path.
- **Do not proceed until T0 passes.** Every later stage is meaningless without it.

### T1 — Colour identity and the usable floor *(the highest-value stage)*

Nine one-second colour states in sequence: red, green, blue, yellow, cyan, magenta, white, then red and
green again. 250 ms make-before-break crossfades, 300 ms fades.

Run the **same sequence** at a fixed set of colour levels, ascending, one pass each, with the hardware
settings unchanged between files:

| Step | Colour level (ratio to reference) | Purpose |
|---|---|---|
| L0 | 6.25 % | Minimum. Is anything visible at all? |
| L1 | 12.5 % | |
| L2 | 25 % | **The intended product ceiling.** Is it usable? |
| L3 | 50 % | Research only — above the product ceiling |
| L4 | 100 % | Research only — the developer-record full brightness |

**Stop the ladder as soon as a level is comfortable and visible.** L3/L4 exist to answer "is our cap
too conservative?", not to be used. They are research stimuli: short, one pass, ascending, never
looped, and they need the explicit agreement of everyone in the room. If any level causes discomfort,
that is the ceiling and it is a finding.

Record per state: perceived colour (or none), weak/strong, whether the intended colour appears at all,
unwanted colours, flicker, comfort.

**Questions this stage answers:**

- At what level does each colour become reliably visible? *(Closes S4 in `04`.)*
- Is green systematically weaker than red and blue? *(The prior session suggests yes. Confirm or
  refute — and if confirmed, the per-colour balance trim becomes a required parameter, not an option.)*
- Does a 25 %-to-25 % crossfade read as a mix or as a dip to black? *(The `make-before-break` rule
  should fix the dip; this tests it.)*
- Is the intended product ceiling usable, or must the ceiling policy change?

### T2 — Mapping discrimination *(the core of issue #6)*

PULSE and COUNTERPOINT on the **same factory pattern**, same seed, same tempo, same intensity.

- **Pass:** they are obviously different experiences.
- **Fail:** they feel like the same thing at different brightness. Go back to `03` — the mappings fail
  the C5 criterion and no amount of parametrisation will save them.

Then a TIGHT pattern and a FERAL pattern, same mapping.

- **Pass:** the light feels different in character, not just busier.
- **Fail:** the light is not using the groove (criterion C3). Check that `timingDevMs` actually reaches
  the light engine.

If a second observer is available: shuffle the order and ask which is which, blind. That turns an
impression into a datum.

### T3 — Programme level and carrier margin

Mix programme + carriers at −24, −18, −12 and −6 dBTP.

Find the level at which detection drops out or the colours become unstable. Record it. This calibrates
the headroom reservation in [`01`](01-signal-spec.md) §5 and the −6 dBTP ceiling in `04` §7.

**Never raise brightness to fix a dropout.** Fix signal integrity first.

### T4 — Mutation and memory

In DRIFT: press MUTATE five to ten times while playing.

- **Pass:** the light *character* shifts; the light does not become faster; no flash.
- **Fail:** extra flashes on mutation → the mutation signal (H4) has not reached the light engine, or
  the governor is not gating.

### T5 — Stop, panic and the failure modes

Test, in this order, with the glasses off and then on:

1. **Panic** while playing → light dark within 50 ms, audio unaffected.
2. **Panic while the transport is stopped** → still works.
3. **Transport stop** → colours fade, no flash; the reference keeps running and detection persists.
4. **Preset change while light is on** → light does not stay latched.
5. **Close/reopen the plug-in in a host** → light does not stay latched after unload.
6. **Pull the cable** → what happens? *(Closes S3: does the decoder hold the last brightness? Record
   what you see. This is the one question only hardware can answer.)*

### T6 — Optional objective measurement

With a photodiode and an audio interface:

- Record the LED output at a fixed, low setting while a static encoding plays. FFT the recording:
  is there periodic luminance modulation at 3–30 Hz with ≥ 10 % depth? *(Closes S2.)*
- Play the brightness ladder and check that measured LED output is monotonic with the requested level.
  *(Closes the "is the decoder linear" question in `01` §9.)*

Without a photodiode, report these as **"not objective"**, in those words.

---

## 4. Observation form

| Question | T0 | T1 | T2 | T3 | T4 | T5 | T6 |
|---|---|---|---|---|---|---|---|
| SpectraStr detected (Y/N) | | n/a | | | | | |
| Perceived colour(s) | n/a | | | n/a | | n/a | n/a |
| Unwanted colour / flicker | n/a | | | | | | |
| Left/right movement recognisable | n/a | | | | | | n/a |
| Mappings distinguishable (blind?) | n/a | n/a | | n/a | n/a | n/a | n/a |
| Faster mapping felt *busier* or *calmer*? | n/a | n/a | | n/a | n/a | n/a | n/a |
| Discomfort / headache / unexplained flash | | | | | | | |
| Comfort (1–5) | | | | | | | |
| Notes | | | | | | | |

Record with every row: date, firmware version, Kasina brightness setting, AUX setting, Mac output
level, file/pattern, seed, tempo, mapping, intensity setting, glasses type, order of exposure. Without
that context the observation is not reproducible and is worth very little.

---

## 5. The five perceptual questions — the actual point

Answered afterwards, in conversation, not on a form:

1. **Did the light and the sound feel related, or like two separate things?**
2. **Did you notice that the light does not answer every hit?** Did that feel like intention or like
   something missing?
3. **When the pattern got more intense, did the light get faster or heavier?** *(If "faster", the
   design has failed and the governor or the sub-harmonic rule is not working.)*
4. **After MUTATE, did the light change in character, or did it just change?** Could you tell the
   pattern had evolved from the light alone?
5. **What stayed with you: the light, the sound, the silence, or nothing?**

These are the questions that decide whether the answer to *"hoe kan licht de ritmische identiteit
representeren zonder louter een beat-indicator te worden?"* is *yes*, *partly*, or *no*. Nothing in
this research pack can answer them.

**And one more, for the operator rather than the viewer:**

6. **If the glasses had been off, would the music have been worse?** A feature that has to be on to
   justify itself is a visualisation. A feature you sometimes choose to leave off is a voice of the
   instrument.

---

## 6. Acceptance and stop criteria

**Continue** only when T0 passes and the previous stage produced no discomfort, no unwanted colours
and no unexplained flashes.

**Stop and report** on: loss of detection, visible flicker outside an intended transition, discomfort,
headache, dizziness, or an unexplained flash. That is a finding, not a failure — record cause and
circumstances and do not retry at a higher brightness.

**Never:**
- raise the hardware brightness to compensate for a dim file;
- loop any stimulus;
- run the ladder in fewer than the stated steps;
- run L3/L4 without the explicit agreement of everyone present;
- draw a conclusion about health, relaxation or effect.

---

## 7. What a completed session would and would not prove

**Would prove:** that the signal is detected by this unit at this firmware; which colours are visible
at which levels; whether the mappings are distinguishable; whether mutation and tempo changes read as
character rather than as speed; whether the panic and stop behaviour hold in practice.

**Would not prove:** that the light is safe; that a different unit or firmware behaves the same; that
the decoded colour matches the encoded colour in any photometric sense; that any claim about the
decoder's internal law is settled. One session on one unit is one session on one unit.

**Never write** "verified on hardware" as a blanket claim. Write what was tested, on which unit, at
which settings, on which date, and what was observed.
