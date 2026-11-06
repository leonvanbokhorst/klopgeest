> **Provenance.** One of five evidence reports produced for issue #6 on 2026-11-06 by a lead researcher working with three parallel sub-investigations. Reproduced here at full length and essentially verbatim — it is the source of record for the summaries in `../01`–`../07`, and where a summary and this document disagree, this document wins.
>

# SpectraStrobe — primary-source verification for a KLOPGEEST light engine

Task: task-1 (independent verification of the encoding facts a KLOPGEEST implementation would depend on).
Method: every claim below is anchored to a retrieved primary source with a verbatim quote and URL. The NAGLOED
summaries were **not** used as evidence and are not cited anywhere in this document.
Retrieval dates: 2026-11-06 (UTC timestamps in §4).
Labels used throughout: **ESTABLISHED** (quoted primary source), **CONTESTED** (sources disagree — both quoted),
**OPEN** (not publicly documented), **INFERENCE** (arithmetic or reasoning from established numbers — arithmetic shown).

Producer / repeater note, needed before reading the verdicts:

| Source | Who it is | Relationship to the format | Independent witness? |
|---|---|---|---|
| MindPlace Kasina Quickstart/Manual (download/1244) | MindPlace, the format owner ("SpectraStrobe™ is developed by MindPlace®") | owner-side documentation | yes (owner) |
| TomasC = Tomas Cerny (forum, 2019) | credited in the Kasina manual as the person who "made the Kasina possible"; writes "our SpectraStrobe encoder", "The SS dedocer in Kasina" | owner-side, **the encoder author** | yes |
| Scott Hendrickson / "neuroasis" (forum 2013/2015, GitHub tooling, wiki) | Kasina factory session author, Audacity/GNaural/AVS-Color-Wheel tooling author | independent producer with privileged early access | yes |
| Lumasonic docs (Cymatic Somatics) | third-party commercial tool vendor (Prism VST). Format owner is cited as MindPlace. Its author is almost certainly the 2019 forum user "Cymatic" (see §3.4) | third-party doc, **not** the format owner | partial — derivative of the same community lineage |
| SpiraStrober | independent MIT web implementation | third-party implementation | yes (code only) |

Consequence: the Lumasonic docs are **not** an independent corroboration of MindPlace statements; they are a later,
partly re-derived restatement from the same lineage. This matters for fact 3.

---

## 1. Verdict table

| # | Fact | Verdict | Anchor |
|---|---|---|---|
| 1 | Carriers: ref 18,200 Hz, red 18,700 Hz, green 19,200 Hz, blue 19,700 Hz; AudioStrobe = the 19,200 Hz channel | **ESTABLISHED** — 4 independent sources agree, including the format owner and two independent implementations | §2.1 |
| 2 | Reference tone is an 18.2 kHz sine, hard-panned L↔R, 100% depth, 0% offset, repeating at 10–50 Hz (20 Hz recommended, sine). Purpose: (a) format flag for SS-vs-AS autosensing, (b) level/"dynamic range" reference/AGC. Waveform & duty of the panning law: **CONTESTED** (sine LFO vs 50%-duty pulse vs 12.5%-width rectangular gating) | **ESTABLISHED** (existence, pan extreme, rate window, purpose) + **CONTESTED** (waveform/duty) | §2.2 |
| 3 | Brightness = colour-channel amplitude **relative to** the reference amplitude; but at what ratio does a colour channel reach full brightness? Lumasonic table implies colour = reference level (1:1); Hendrickson (2015) and TomasC (2019) state reference −36 dB / colour −24 dB (4:1, +12 dB). Every numeric artefact in Hendrickson's tooling is 4:1–5.3:1 (−22.5/−36.5; −22/−36.5; 0.075/0.015). **The 1:1 reading is not supported by any developer tooling and should not be implemented.** | **CONTESTED** — resolved in favour of 4:1…5:1 for engineering purposes (§3) | §2.3, §3 |
| 4 | Formats: stereo only; WAV 44.1 or 48 kHz, 16-bit signed PCM, dithering **disabled**, no Wave64, no metadata chunks; MP3 320 kbps CBR, **Stereo not Joint Stereo**, VBR forbidden (MindPlace's floor: 256 kbps CBR). Noise shaping: **OPEN** | **ESTABLISHED** (all but noise shaping) | §2.4 |
| 5 | Headroom: carriers behave as 4 stereo tracks; MindPlace-side convention reference ≈ −36 dBFS, colour ≈ −24 dBFS, programme mastered to ≈ −2 dBFS. What clipping / mono summing / resampling / lossy coding do to the carriers is **not documented** (mechanisms are INFERENCE) | **ESTABLISHED** (levels, "4 tracks", −2 dB convention) + **OPEN** (failure modes) + **INFERENCE** (mechanisms) | §2.5 |
| 6 | Detection: automatic per-stream autosensing between SS and AS; reference present ⇒ SS, reference absent ⇒ AS (19.2 kHz only, i.e. green). Minimum lead-in: **OPEN** — no source states any requirement. Stop behaviour: **OPEN**. Firmware bugs: WAV-with-metadata end-of-file noise burst + light flash (confirmed and fixed 2019); USB-audio driver periodic clicking causing LED flashes; occasional failure of AUDIO IN mode to initiate; Wave64 unsupported | **ESTABLISHED** (autosense, fallback, bugs) + **OPEN** (lead-in, stop) | §2.6 |
| 7 | Kasina: decodes into **six** light control signals (RGB × left/right), 4-conductor TRRS glasses output, independent LED-brightness control, per-LED/per-side colour balance (default 100%), ColorSets remap AudioStrobe. The decoder's filter topology/order, its level→brightness law, its AGC divisor and its detection thresholds are **not publicly documented** | **ESTABLISHED** (channels, I/O, controls) + **OPEN** (DSP internals) | §2.7 |

---

## 2. Detailed findings

### 2.1 Carrier frequencies — ESTABLISHED

**Format owner / vendor doc.** <https://raw.githubusercontent.com/Lumasonic/Lumasonic/main/docs/08-SpectraStrobe/03-format-specification.md>

> | Format | Frequency | Purpose |
> |--------|-----------|---------|
> | AudioStrobe | 19200 Hz | controls the brighness of the LEDs; does not specify LED color |
> | SpectraStrobe | 18200 Hz | reference tone used to indicate the SpectraStrobe format |
> | SpectraStrobe | 18700 Hz | controls the brightness of the red LEDs |
> | SpectraStrobe | 19200 Hz | controls the brightness of the green LEDs |
> | SpectraStrobe | 19700 Hz | controls the brightness of the blue LEDs |

Same doc on the tone shape: "For encoding, high frequency [sine wave](https://en.wikipedia.org/wiki/Sine_wave) tones are added into the audio content of the recording."

**MindPlace-side producer tooling (independent).** <https://github.com/scottosx/Audacity-Mind-Machine-Suite/wiki> (Scott
Hendrickson, page edit Jan 12 2016):

> SpectraStrobe's tone channels are 18.7khz for Red, 19.2khz for Green (same as AudioStrobe) and 19.7khz for Blue.

**Literal constants in that tooling** (not prose — the actual oscillator calls):
<https://raw.githubusercontent.com/scottosx/Audacity-Mind-Machine-Suite/master/1%20Audacity%20Mind%20Machine%20Suite/Generate%20Plugins/SpectraStrobe/tone-generator-ss-ref.ny>

```nyquist
(vector
(scale 0.015 (stretch duration (hzosc 18200) ))
(scale 0.015 (stretch duration (hzosc 18200) ))
)
```

<https://raw.githubusercontent.com/scottosx/Audacity-Mind-Machine-Suite/master/1%20Audacity%20Mind%20Machine%20Suite/Generate%20Plugins/SpectraStrobe/tone-generator-ss-channels.ny>

```nyquist
((= color 0) (scale 0.015 (stretch duration (hzosc 18700) )) )
((= color 1) (scale 0.015 (stretch duration (hzosc 19200) )) )
((= color 2) (scale 0.015 (stretch duration (hzosc 19700) )) )
```

**Independent third-party implementation.** <https://raw.githubusercontent.com/Syluminose/SpiraStrober/main/index.html>
(minified WebGL app), samplerate and carrier table:

```js
sr=44100,fc=[18700,19200,19700,18200]
```

**AudioStrobe = the green channel.** 05-production-and-mixing.md:
> ***Note:** If creating an AudioStrobe track it corresponds to the green color tone @ 19.2 KHz.*

No source gives a tolerance band for the carriers. Tolerance is **OPEN**; the only related statement is about the
reference *pulse width* (§2.2), and TomasC's description of "precisely tuned and steep band pass filters" (§2.5) implies
narrow bands but gives no numbers.

### 2.2 The reference tone and its stereo-panning LFO

#### 2.2a Why it exists — ESTABLISHED (two purposes, both stated)

Purpose 1, format flag for autosensing (03-format-specification.md):
> Since both AudioStrobe and SpectraStrobe share a frequency space (19200 Hz), in order for an AVS device to be able to
> detect which type of material it is playing back, a reference tone must be added at 18200 Hz to indicate the presence
> of SpectraStrobe.

> ***Note**: The SpectraStrobe reference tone at 18200 Hz also requires a specific stereo panning pattern to be applied
> in order for it to be correctly identified as a SpectraStrobe signal.*

Purpose 2, a level/"dynamic range" reference — i.e. the decoder normalises against it. Hendrickson, 2015-05-20,
<https://mindplacesupport.com/forum/printthread.php?t=2508&pp=10>:

> The reference signal acts as an identifier, as the Kasina performs auto-sensing between SS and AS. […] The reference
> track also sets the dynamic range of the signal in a ratio of 4:1. That way, if the user changes the volume of the
> source (on an AUX for instance) the full range of brightness is still delivered.

Hendrickson, 2013-12-04, <https://mindplacesupport.com/forum/printthread.php?t=2002&pp=10>:

> SpectraStrobe is an advanced embedded light control signal. It is similar to AudioStrobe but much more flexible
> because it can control all 3 of the LED lights and the dynamic range (least brightness to highest brightness) is not
> affected by the volume of the audio on the player.

> software that can generate the SpectraStrobe reference signal (which identifies the SS signal and sets the volume for
> dynamic range)

Lumasonic, 05-production-and-mixing.md:
> ***Note:** The reference tone at 18200 Hz does not directly control brightness, but indicates to the playback device
> that SpectraStrobe content is present.*

So: the reference is both a **flag** and an **amplitude/AGC reference**. The exact arithmetic of the AGC (is brightness
`colour/ref`, `colour − ref`, a table lookup?) is **OPEN** — see §2.7.

#### 2.2b Panning: depth, offset, rate — ESTABLISHED with a rate-window caveat

03-format-specification.md:
> The SpectraStrobe 18200 Hz reference tone must be panned hard left/right (100% left then 100% right) in a repeating
> pattern at a rate between 10-50 cycles per second (Hz). Using audio mixing metaphors, and speaking with the
> terminology of a [Low Frequency Oscillator (LFO)](https://en.wikipedia.org/wiki/Low-frequency_oscillation), this would
> be interpreted as:
>
> `waveform: sine, frequency: 20 Hz, offset: 0%, amplitude: 100%`

So, as a pan-LFO parameter set: **waveform sine, rate 20 Hz (permitted 10–50 Hz), depth/amplitude 100%, offset 0%**
(offset 0% = centred rest position, symmetric L/R excursion).

**Rate-window ambiguity — INFERENCE.** "10-50 cycles per second" is ambiguous between a full L↔R↔L period and one
left-then-right event pair. Hendrickson's numbers resolve it: he specifies "a pulse wave at 10hz" *per channel* and
"autopan set to 20hz", i.e. a 20 Hz LFO period = 10 Hz per channel. Under the reading "cycle = full L↔R↔L period",
10–50 Hz spans 5–25 Hz per channel and comfortably contains Hendrickson's 10 Hz per channel. Under the other reading
(cycle = one hard-pan event) his 20 Hz autopan would sit exactly at the top of the range and his 10 Hz per channel would
be out of range. Therefore: **pan-LFO period rate 10–50 Hz, default 20 Hz** is the consistent reading. A practitioner
also reports leeway: Cymatic, 2019-04-13, `printthread.php?t=3173&pp=10`:
> I noticed if I tried different panning LFO frequencies (say 10hz) there was a range in which it would work

#### 2.2c Waveform and duty cycle of the reference gating — CONTESTED

Three statements, all from people who built the thing, none identical:

(i) **Sine pan LFO** — Lumasonic, 03-format-specification.md:
> ***Note**: A different panning LFO waveform can be used, such as square or pulse, but since these create more audible
> artifacts in the audio, and the SpectraStrobe decoder is somewhat forgiving, a sine wave will avoid any audible
> artifacts from the reference tone.*

(ii) **50 %-duty pulse, 10 Hz per channel / 20 Hz autopan** — Hendrickson, 2015-05-20, thread 2508:
> This track consists of a 18.2khz sine wave that is modulated with a pulse wave at 10hz. In addition the modulation is
> 180 degress [sic] out of phase between the channels.
>
> The easiest way to create the reference is to modulate an 18.2khz sine wave with an autopan LFO set to 20hz, with the
> waveform being either a sine or smoothed square.

and his actual shipped chain, <https://raw.githubusercontent.com/scottosx/Audacity-Mind-Machine-Suite/master/1%20Audacity%20Mind%20Machine%20Suite/Audacity%20Chains/SS%20Ref%20LFO.txt>:
```
Panning[LFO]2a...: rate=20.000000 waveform=pulse duty=50 phase=0 left=0 right=100
```
(`phase=0`, `left=0`/`right=100` = full excursion to both extremes, i.e. hard L then hard R.)

(iii) **12.5 %-width rectangular envelope, L/R alternating** — TomasC (encoder author), 2019-04-15,
<https://mindplacesupport.com/forum/printthread.php?t=3173&pp=10&page=2>:
> Regarding the ref. signal - our encoder really modulates the sine waveform carrier at 18200 Hz by rectangular
> envelope with 12.5% pulse width and with 180 deg. phase shift between left and right side (so the pulses are left /
> right alternating). The pulses go through steep band pass filter which 'cuts the edges' and removes the related noise.

> The SS dedocer in Kasina is quite tolerant, regarding the width of ref. signal pulses, so the pulse width can differ a
> bit.

Earlier, same thread page 1 (2019-04-12):
> Just the width of the ref. signal pulses is 1/8, i.e. 12.5%. They are rectangular but filtered by the precise filter.

**Reading.** The *invariant* across all three is: an 18.2 kHz carrier whose energy alternates between the L and R
channels (hard extremes), with L and R in antiphase, repeating at tens of Hz. The *duty* differs (continuous sine pan /
50 % pulse / 12.5 % rectangular), and the encoder author explicitly says the decoder tolerates pulse width. Duty is
therefore a free parameter within limits, not a bit-exact requirement. Note also that TomasC says the production encoder
*band-pass filters* the pulse edges away, so the delivered signal is closer to a smoothed/rounded alternation than to a
raw 12.5 % gate — which converges on Hendrickson's "sine or smoothed square" advice.

**Cross-check that a square 20 Hz pan works on hardware** — Cymatic, 2019-04-13, thread 3173 page 1:
> Inside my DAW I setup a panning LFO on the SS reference tone track, square wave @ 20hz, 100% left/right. […] I'm happy
> to report I am now getting a clean signal.

### 2.3 Brightness definition — CONTESTED (see §3 for the worked conflict)

Both sides agree on the *structure*: brightness of a colour channel is governed by that channel's carrier amplitude
**relative to the reference carrier's amplitude**. Lumasonic, 05-production-and-mixing.md:

> For the color control tones `18700 Hz`, `19200 Hz`, `19700 Hz`, the amplitude of the signals in relation to the
> amplitude of the reference tone `18200 Hz` determines the brightness of each LED channel.

and on panning:
> Except for the SpectraStrobe reference tone, varying the volume of the track will vary the brightness of the
> corresponding LED channel on the AVS device, and varying the panning of the track will vary the left/right eye balance
> of the corresponding LED channel.

They disagree on the *operating point* (what absolute/relative level counts as 100 %). Quotes and resolution in §3.

### 2.4 File formats, sample rate, bit depth, metadata, dither, compression — ESTABLISHED

**Stereo only.** 04-file-formats.md, <https://raw.githubusercontent.com/Lumasonic/Lumasonic/main/docs/08-SpectraStrobe/04-file-formats.md>:
> ***Note:** All final mixes should be stereo.*

**WAV.** Same file:
> **Recommended Settings: 44100 hz or 48000 hz, 16 bit**
>
> ***Note:** Although it is possible to have 32 bit wave files, 16 bit is recommended for compatibility when playing
> back from the Kasina directly.*

07-rendering-output.md, <https://raw.githubusercontent.com/Lumasonic/Lumasonic/main/docs/03-Prism-VST/07-rendering-output.md>:
> * 48000 Hz sample rate
> * 16 bit, signed PCM sample format
> * Dithering disabled
> * When putting WAV files directly on the Kasina, avoid Wave64 format (unsupported by Kasina)

**MP3.** 04-file-formats.md:
> **Recommended Settings: 44100 hz or 48000 hz, 16 bit, [320 Kbps constant bit rate (CBR)](…)**
>
> As long as a `constant bit rate of 320 Kbps` is used, the compression artifacts introduced should be completely
> neglibable [sic] both from a listening perspective and from an LED control signal perspective.
>
> ***Note:** Although it is possible to use both variable bit rate (VRB) and higher compression settings (like 128
> Kbps, etc.) this is not recommended for use with SpectraStrobe as this will introduce too many sample inaccuracies
> during playback and likely lead to visual control errors or failure to detect visual control in general.*

07-rendering-output.md adds the critical channel-mode rule:
> * 320 Kbps, constant bitrate
> * Prefer `Stereo` instead of `Join Stereo` encoding

**MindPlace's own floor is looser (256 kbps).** Kasina Quickstart/Manual, <https://mindplacesupport.com/download/1244/>:
> The settings for encoding files should be at least 256kbps Constant Bit Rate (CBR). These settings are necessary to
> preserve the high frequency content in the audio which contains the light signals.

Not a contradiction: 320 CBR satisfies "at least 256". Note the producer/repeater relationship — MindPlace states a
minimum, the third-party vendor states a recommendation.

**Supported containers (owner-side).** Quickstart:
> Kasina currently supports only MP3 and WAV file formats.

> AudioStrobe and SpectraStrobe sessions are audio files with light encoding embedded at high frequencies. These
> sessions could have the extensions ".mp3" or ".wav", depending on the file type. All factory sessions are in MP3
> format.

**Metadata.** Treated as optional by the vendor (07-rendering-output.md):
> Optionally, add or clear MP3 file metadata on export

— but WAV metadata is *dangerous on the Kasina*, see §2.6c. "Optionally" plus a firmware bug is not a licence to emit
metadata: a conservative encoder should write WAVs with no LIST/INFO chunk.

**Dither / noise shaping.** Dither: explicitly disabled (above). **Noise shaping: OPEN** — no source mentions it.
INFERENCE on whether that matters: 16-bit quantisation gives a noise floor around −96 dBFS (≈ −93 dBFS with TPDF
dither), i.e. ~57 dB below the reference carrier at −36 dBFS and ~69 dB below a full-brightness colour carrier at
−24 dBFS. Dither is therefore immaterial to *detecting* the carriers; the only argument against dithering is added
broadband HF energy in the same octave as the carriers, which is what the vendor chose to avoid.

### 2.5 Headroom accounting

**Levels (vendor).** 05-production-and-mixing.md:
> ***Note:** Mixing SpectraStrobe content can be deceiving because a certain amount of headroom will be consumed by the
> SpectraStrobe control tones. Even though the tones will be essentially inaudible, make sure to consider the headroom
> implications for the rest of your mix. The SpectraStrobe tones will behave as 4 stereo tracks, each with output between
> -32 dB and -24 dB when fully on.*

> If the final audio signal level reaches or exceeds 0 dB on either left or right channel it produces digital clipping,
> which is a form of digital distortion, and is generally considered to be undesirable.

**Levels (MindPlace side).** Hendrickson, 2015-05-20, thread 2508:
> In pracitice [sic], the total amplitude of the reference signal is normalized to -36db and each light channel
> normalized to -24db. This keeps the tones inaudible and allows around -2db of headroom for the soundtrack (which
> should be normalized to this at max)

**Internal inconsistency in the vendor doc.** "4 stereo tracks, each with output between -32 dB and -24 dB" does not
match either operating point: under the MindPlace sides' convention the four tracks are at −36 (reference) and −24
(three colours); under a 1:1 convention they would all be at one level. The −32 figure is unexplained in the doc.
**Do not use the −32…−24 sentence as a level specification**; use −36/−24 (quoted twice, §3) and treat −32 as either a
typo or an unstated variant. Flagged as CONTESTED-internal, unresolved.

**Arithmetic — worst-case carrier sum (INFERENCE).**
Reference peak per channel 0.015 (−36.48 dBFS); colour peak 0.075 (−22.50 dBFS). Because the reference is hard-panned,
at most one reference pulse is present in a given channel at a time, but all three colour carriers can be simultaneously
full in both channels:
```
3 × 0.075            = 0.225
+ 0.015 (reference)  = 0.240   → 20·log10(0.240) = −12.40 dBFS
```
So the four carriers alone cannot clip (they need +12.4 dB more). Add programme mastered to −2 dBFS (0.794):
```
0.794 + 0.240 = 1.034 → +0.29 dBFS
```
i.e. a *perfectly coherent* worst case can exceed full scale by ~0.3 dB. Hendrickson's "−2 dB of headroom for the
soundtrack" is thus consistent to within a fraction of a dB and is the number to design to; a conservative
implementation should master the audible programme to about −3 dBFS peak, or reserve ~1 dB more.

**Carrier-induced headroom consumption, practical.** JoeV (forum moderator), 2025-11-26,
<https://mindplacesupport.com/forum/printthread.php?t=3737&pp=10>:
> The tracks used to control Audiostrobe and Spectrastrobe are ultrasonic soundtracks. You can't hear them (maybe your
> dog can :)), but they still affect the digital ceiling of your total sound volume for all tracks. If you set the AS or
> SS signals too high, it can cause clipping. Adjust all of your soundtracks' volumes until they are below your digital
> threshold. You can use a limiter, but be careful, as a limiter will affect the light tracks as well. It is best to
> isolate the light tracks into a folder or bus and apply the limiter to the other soundtracks that may be causing the
> clipping.

That last sentence is the single most directly transferable production rule for KLOPGEEST: **carriers must not be inside
a limiter that programme material drives**, otherwise programme dynamics modulate the light output.

**Error sources between the carriers (ESTABLISHED mechanism, quantitative limits OPEN).**
Lumasonic, 05-production-and-mixing.md:
> Since SpectraStrobe inserts the visual control signals into the audio recording itself, it is possible for unwanted
> frequency energy to "bleed" into the control and reference frequency bands causing unwanted glitches and imprecise
> control of each color channel on the LEDs.

TomasC, 2019-04-12, thread 3173 page 1:
> our SpectraStrobe encoder uses precisely tuned and steep band pass filters to avoid interferences among RGB channels
> and the reference channel. Plus we filter the audible audio by a low pass filter which starts to cut at about 13 kHz
> and cuts practically everything off above 17 kHz, before adding the SS signals.

> Without using the precise filters, fast edges of pulses or modulation generate overtones / noise which often 'leaks' to
> the other SpectraStrobe channels.

Lumasonic on filter shape (05-production-and-mixing.md):
> Ideally you would use the steepest (highest order/highest dB cut) bandpass filter available in your DAW on each
> SpectraStrobe tone track. […] Keeping Q/resonance at 0 is recommended as it may unintentionally amplify the energy
> around the filter cutoff point.

**INFERENCE for a drum machine.** Adjacent carriers are only 500 Hz apart (18.2 / 18.7 / 19.2 / 19.7 kHz). A rectangular
amplitude envelope applied to a colour carrier spreads sidebands of order 1/τ where τ is the transition time: a 1 ms
attack → ~1 kHz of sidebands, i.e. **twice the channel spacing**, guaranteeing colour crosstalk; a 10 ms attack →
~100 Hz, well inside the channel. Consequence: the light envelopes must be **band-limited (soft, few-ms minimum rise
time)**, or the carriers must be band-pass filtered after modulation. This is corroborated qualitatively by TomasC
above and by the reported "red flicker with only the reference present" symptom (§2.6c).

**Clipping / saturation, mono summing, resampling, lossy coding — the documented statements.**
Clipping: only the generic warning quoted above plus JoeV's limiter warning. What a clipped carrier does to the LED
output is **not documented**. INFERENCE: hard clipping generates odd harmonics of an 18–20 kHz carrier, i.e. in-band and
aliased products that land in neighbouring 500 Hz slots and (at 44.1 kHz) below Nyquist — i.e. it manifests as colour
crosstalk and possibly audible whistling, not as a clean dimming.

Mono summing: **OPEN** — no source states it. INFERENCE with high confidence: the reference is defined by L/R
*antiphase* alternation; L+R summing cancels it, so a summed-to-mono file loses the reference and must fall back to
AudioStrobe (green/19.2 kHz only, §2.6a). This also explains the vendor's "Prefer Stereo instead of Join Stereo"
instruction: mid/side or intensity joint-stereo coding damages exactly the L/R difference that carries the reference.

Resampling: **OPEN**. INFERENCE: the carriers require Fs > 39.4 kHz; 44.1 and 48 kHz both qualify, 32 kHz eliminates all
four carriers outright. At 44.1 kHz the top carrier (19.7 kHz) sits close to the anti-alias transition band of typical
resamplers, so carrier levels can be altered by a resample even when the carriers survive.

Lossy coding: the vendor's 320 kbps CBR / Stereo / no-VBR rule (§2.4) is the only documented guidance; the mechanism
(encoder lowpass moving down through 18 kHz at low bitrates, and loss of L/R resolution in joint stereo) is INFERENCE.
The exact lowpass frequency per encoder/bitrate was **not verified from a primary source** — OPEN.

### 2.6 Detection, lead-in, stop behaviour, firmware bugs

#### 2.6a Autosensing — ESTABLISHED

Kasina Quickstart, <https://mindplacesupport.com/download/1244/>:
> If you are adding new audio files to the card or playing from an external source, the Kasina will determine the file
> type automatically during playback. SpectraStrobe will display Mode SpectraStr/AS on the screen. AudioStrobe will
> display Mode AS/SpectraStr.

> The Kasina will automatically recognize whether the audio is encoded with AudioStrobe® or SpectraStrobe™, displaying
> Mode SpectraStr/AS with the form of encoding not being used in pale gray.

Fallback when the reference is absent — Lumasonic, 05-production-and-mixing.md:
> ***Note:** With any method, the SpectraStrobe reference tone must have [the appropriate panning dynamics]
> applied otherwise the AVS device will not recognize your recording as SpectraStrobe and will fall back to AudioStrobe
> instead.*

JoeV, 2025-11-26, thread 3737:
> If no reference signal is heard, the unit will assume it is an Audiostrobe and will only hear the green signal, which
> is also the Audiostrobe frequency.

Precedence (SS is detected *through* AS knowledge) — Craig Tice (Kasina session author, MuLab/Kasina Effect editor
author), 2013-10-23, <https://craigtavs.wordpress.com/tag/spectrastrobe/>:
> SpectraStrobe knows all about AudioStrobe and can then go on to detect the additional signals (18.2kHz, 18.7kHz,
> 19.2kHz and 19.7kHz). Spectrastrobe is an open standard allowing sessions to be created by a variety of means.

(That "open standard" claim is a claim, not a published specification: **no normative SpectraStrobe specification
document published by MindPlace was found** — see §5.)

#### 2.6b Minimum lead-in and stop behaviour — OPEN

I found **no statement, anywhere in the sources read, of a minimum lead-in** (a required run-in of the reference before
light output starts) or of any required tail. Explicitly: the Kasina's "ten second countdown timer" before a session
(Quickstart: "By default, a ten second countdown timer will appear. You can skip the countdown by pressing the Center
button a second time.") is a **player UI feature, not a format requirement** — do not mistake it for a lead-in rule.

Stop behaviour (what the LEDs do when playback stops, is paused, or the file ends) is **OPEN**; the manual documents only
the UI ("pressing the Center button once will pause the session… A long press of the Center button will stop the
session") and nothing about the light output at that moment.

Practical consequence for a builder: since detection is "reference present", the safe design is to have the reference
present for the whole duration, from the first sample to the last, and to end the file at the same instant the reference
ends — not to append programme-only tails. That is a precaution from the mechanism, not a documented rule.

#### 2.6c Known firmware bugs — ESTABLISHED

**WAV metadata → end-of-file noise burst + light flash.** Reported by Cymatic (2019-04-13, thread 3173 page 1):
> my testing with 44.1khz 16 bit .wav files on the Kasina results in the Kasina playing a nasty/loud burst of noise
> right as the file finishes playing. […] it results in: a super bright flash of the LEDs, really loud nasty noise on
> the headphones, and a loud pop on the subpac's transducer […] If I take the same .wav file and export as 320 kbps MP3
> I don't here [sic] the nasty noise.

Confirmed and diagnosed by the encoder author, TomasC (2019-04-15, thread 3173 page 2):
> There is a bug in WAV playing in Kasina. It plays WAV files to their end. Your WAV file has meta data at its end so it
> causes the odd sound and light pulse at the end. I played my several test WAV files and they all play correctly, as
> they have no meta data included. We will fix it.

Fixed in beta firmware, 2019-04-18:
> the bug in WAV playing has been fixed. Please check the attached new beta FW.

Status today: the fix exists but its inclusion in every shipping unit's firmware is not verifiable from public sources
(**OPEN**). A conservative encoder therefore emits metadata-free WAV.

**USB audio driver periodic corruption → LED flashes.** Cymatic, 2019-04-11/12, thread 3173 page 1:
> One of the "noise" bugs I was dealing with (weird clicks and glitching LEDs) is related to the audio USB driver. If I
> reinitialize the USB audio device/driver the clicking will go away - until it comes back

> It's definitely some sample-level mismatch in the audio signal that shows up as maybe the sample buffer having some bad
> samples in it, hence clicks and pops, but they make the LEDs flash given the frequency I guess.

Relevant to KLOPGEEST because a plug-in live-monitored through the Kasina's USB audio input inherits this failure mode
(transient sample corruption = broadband click = full-spectrum light flash).

**AUDIO IN mode initiation failure (documented by the vendor).** 07-rendering-output.md:
> ***Note**: Sometimes the `AUDIO IN` mode doesn't initiate properly on the first attempt of selecting it within the
> Kasina interface. If this happens, simply click **LEFT** on the **Navigation Wheel** to exit the mode, and then
> re-select it.*

**Wave64 unsupported** — 07-rendering-output.md: "Waveform always exports WAV files to Wave64 format which the Kasina
does not directly support."

**Spurious colour flicker with reference only.** Cymatic, 2019-04-11:
> even when I have no musical or audio content other than the SS reference tone, with all color tracks volume set to
> -INF db, I still get some flicker from the red channel […] the only noise at this point is the SS ref tone track itself.

Cause is **OPEN**. INFERENCE: consistent with finite decoder filter stop-band rejection — the reference at 18.2 kHz is
only 500 Hz below the red band at 18.7 kHz, so the reference leaks into the red detector; red LEDs also have the lowest
forward voltage, so a small leakage current is visible first (Cymatic's own hypothesis: "red LEDs have a lower forward
voltage so it sort of makes sense they would be the first one to go on"). Not verified.

### 2.7 What the Kasina hardware does with the carriers — partly ESTABLISHED, DSP internals OPEN

**Six light control signals (ESTABLISHED, owner-side).** Kasina Quickstart, <https://mindplacesupport.com/download/1244/>:
> Kasina uses a new form of encoding, SpectraStrobe™ (SS) which is similar to AudioStrobe® (AS) but with six light
> control signals instead of just two. This allows full red, green and blue control for both the left and right sides of
> the glasses, synchronized with the audio content.

Corroborated by the session author, blog comment 2015-01-08,
<https://mindplacesupport.com/forum/entry.php?65-GNaural-SpectraStrobe-and-AudioStrobe-templates&bt=281>:
> SpectraStrobe was introduced with the Kasina to overcome all of the limitations of AudioStrobe and allow full control
> of all 6 light channels (RGB - LR).

And by the vendor's glossary, 01-prism-plugin-overview.md:
> `SpectraStrobe` Similar to AudioStrobe, but allows for the control of left/right red, blue, and green signals
> individually.

So "six light control signals" = 3 colours × 2 eyes, i.e. three stereo carriers. It is **not** six carriers, and it is
not six independent *output* channels in the sense of six DACs — it is three stereo audio carriers demultiplexed into
six LED drive channels after L/R separation. (That demultiplexing step is INFERENCE from "six light control signals"
+ "3 stereo control signals" in 03-format-specification.md: "SpectraStrobe encodes three separate stereo control signals
which correspond to individual red, green, and blue LEDs".)

**I/O and controls (ESTABLISHED).** 08-kasina.md, <https://raw.githubusercontent.com/Lumasonic/Lumasonic/main/docs/03-Prism-VST/08-kasina.md>:
> 10. `LED Glasses Output` 3.5 mm LED glasses output (4 conductor TRRS)

> **(4) Audio Volume** and **(5) LED Brightness** can be adjusted independently using the controls on the left and right
> side of the Kasina

Quickstart, SETTINGS → Visuals:
> Adjust the color balance of each LED color, if necessary, both left and right. Default is 100% for all LEDs.

That last line means the Kasina applies a **per-eye, per-colour gain calibrated at 100 % by default** — so "equal
carrier amplitude" does not necessarily mean "equal perceived brightness" across colours, and KLOPGEEST should not try
to pre-compensate blindly.

Also established: LEDs are *not* driven directly by the carrier amplitude in a linear electrical sense as far as public
docs go — an LED brightness control, a gamma control ("`LED Gamma` Adjusts the LED brightness Gamma Correction Curve,
which for the SpectraStrobe mode can help align the expected output with the perceived levels in LED glasses", 03-settings-screen.md)
and vendor glasses with different max brightness exist. The actual transfer function is **OPEN**.

Vendor's required device settings (useful as an integration constraint), 08-kasina.md:
> Navigate to `Audio` → `Max. Volume` […] Click `UP` until `Max. Volume` is set to `52`

**What is NOT publicly documented (OPEN):**
- decoder filter topology (FFT vs IIR vs dedicated chip), order, bandwidth, Q, stop-band rejection;
- detection thresholds, hysteresis and the time constant of SS-vs-AS autosensing;
- the level→brightness transfer law, including whether the reference is the divisor, the radix of a lookup table, or a
  gain-staging target (this is exactly the ambiguity at the heart of §3);
- whether the reference is tracked continuously (AGC per block) or sampled once at stream start;
- per-eye channel independence beyond "six signals";
- LED PWM frequency / resolution, drive current, gamma curve numbers;
- minimum reliable carrier amplitude (a floor implied by JoeV's "If you set the volume too low, it will drop into
  Audiostrobe mode", but never quantified).

A practitioner with hardware in hand could not determine the decoder type either — Cymatic, 2019-04-11:
> whatever filters are being used to pull out the SpectraStrobe content (FFT, IIR, frequency decoder IC - I'm not sure
> what's under the hood here)

---

## 3. The 1:1 vs 4:1 conflict, worked through

### 3.1 Side A — "colour at reference level = full brightness" (1:1)

Lumasonic, 05-production-and-mixing.md,
<https://raw.githubusercontent.com/Lumasonic/Lumasonic/main/docs/08-SpectraStrobe/05-production-and-mixing.md>:

> | Track Volume Level (dB) | LED Brightness |
> |-------------------|--------------------|
> | -INF dB | LED color channel is completely off |
> | reference tone dB level | LED color channel is completely on; full brightness |

> ***Note:** These tables are only used to indicate the extremes of the range. As volume and panning are altered, the
> corresponding LED values (brightness or left/right eye balance) will gradually change in respect to the chosen level.*

Read literally: setting a colour track's fader to **the same dB value as the reference tone's fader** yields full
brightness. That is a 1:1 amplitude relationship.

Two observations on this source before weighing it:
1. It is internally strained. Two paragraphs earlier the same document says brightness is set by "the amplitude of the
   signals **in relation to** the amplitude of the reference tone" — a *ratio* formulation, which is compatible with
   any ratio constant and not specifically 1:1.
2. Its own headroom note ("4 stereo tracks, each with output between -32 dB and -24 dB") does not reconcile with a 1:1
   reading either: under 1:1 all four tracks would be at one level.

### 3.2 Side B — reference ≈ −36 dB, colour ≈ −24 dB (4:1, +12 dB)

Scott Hendrickson ("neuroasis"), 2015-05-20,
<https://mindplacesupport.com/forum/printthread.php?t=2508&pp=10>:

> The reference track also sets the dynamic range of the signal in a ratio of 4:1. That way, if the user changes the
> volume of the source (on an AUX for instance) the full range of brightness is still delivered.
>
> In pracitice [sic], the total amplitude of the reference signal is normalized to -36db and each light channel
> normalized to -24db. This keeps the tones inaudible and allows around -2db of headroom for the soundtrack (which
> should be normalized to this at max)

Tomas Cerny ("TomasC") — the Kasina encoder author, 2019-04-12,
<https://mindplacesupport.com/forum/printthread.php?t=3173&pp=10>:

> Signal levels of the RGB color tones representing 100% brightness are -24 dB, and standard ref. signal level is 1/4 of
> the color tones. Presence of the ref. signal make it quite flexible and adaptive but the above mentioned values are SS
> standard.

−24 dB relative to 0 dBFS, reference at 1/4 of that = −12 dB relative = −36 dBFS. Consistent with Hendrickson, four
years apart, from two different people, one of whom wrote the encoder.

### 3.3 The developer tooling: both sides' numbers, taken from the files

| Artefact | Value | Linear | Recovered level | Ratio to reference |
|---|---|---|---|---|
| `tone-generator-ss-ref.ny` | `scale 0.015` | 0.015 | −36.48 dBFS peak | 1 (reference) |
| `SS Ref Normalize.txt` | `Level=-36.500000` | 0.01496 | −36.50 dBFS peak | 1 |
| `tone-generator-ss-channels-effect.ny` / `tone-generator-ss-picker.ny` | `scale 0.075`, control label `"rms"` | 0.075 | −22.50 dBFS | **5.00× = +13.98 dB** |
| `SS Normalize Single Tone.txt` | `Level=-22.000000` | 0.0794 | −22.00 dBFS peak | **5.31× = +14.50 dB** |
| `SS Normalize 2 tones.txt` | `Level=-16.500000` | 0.1496 | −16.50 dBFS | sum of two 0.075 tones (2×0.075 = −16.48) |
| `SS Normalize 3 tones.txt` | `Level=-13.000000` | 0.2239 | −13.00 dBFS | sum of three 0.075 tones (3×0.075 = −12.96) |
| `SS Normalize SoundTrack.txt` | `Level=-2.000000` | 0.794 | −2.00 dBFS | programme material |
| `AVS Color Wheel` dB readout | `-24 - (20 * log(255/red))` | 255→0.0631 | **100 % brightness = −24 dB** | 4.00× = +12.02 dB vs −36 |
| `AVS Color Wheel` linear readout | `POW(10,(-22.5 - (20*log(255/blue)))/20)` | 255→0.0750 | **linear anchor −22.5 dB** | 5.00× = +13.98 dB vs −36.5 |

URLs: <https://raw.githubusercontent.com/scottosx/Audacity-Mind-Machine-Suite/master/1%20Audacity%20Mind%20Machine%20Suite/Generate%20Plugins/SpectraStrobe/tone-generator-ss-ref.ny>;
<…/tone-generator-ss-channels-effect.ny> and <…/tone-generator-ss-picker.ny> under `1 Audacity Mind Machine Suite/Effect Plugins/SpectraStrobe/` and `…/Generate Plugins/SpectraStrobe/`;
<…/Audacity%20Chains/SS%20Ref%20Normalize.txt>, `SS%20Normalize%20Single%20Tone.txt`, `SS%20Normalize%202%20tones.txt`, `SS%20Normalize%203%20tones.txt`, `SS%20Normalize%20SoundTrack.txt`, `SS%20Ref%20LFO.txt`;
<https://raw.githubusercontent.com/scottosx/AVS-Color-Wheel/master/Window1.xojo_window>.

Verbatim from the colour-wheel source (default field text is also literally `"-24"` three times):

```xojo
txtRedDBLin.Text = "-" +Format(-24 - (20 * log(255/(red/10))),"#.##")
txtGreenDBLin.Text = "-" + Format(-24 - (20 * log(255/green)),"#.##")
txtBlueDBLin.Text = "-" + Format(-24 - (20 * log(255/blue)),"#.##")
```

```xojo
txtRedDBLin.Text = Format(Abs(POW(10,(-22.5 - (20 * log(255/(red))))/20)),"#.###")
txtGreenDBLin.Text = Format(Abs(POW(10,(-22.5 - (20 * log(255/(green))))/20)),"#.###")
txtBlueDBLin.Text = Format(Abs(POW(10,(-22.5 - (20 * log(255/(blue))))/20)),"#.###")
```

Every number the tool author actually shipped sits in a band of **+12 to +14.5 dB** above the reference. **None** of them
is anywhere near 0 dB. The whole toolchain — generator constants, Audacity normalise chains, the colour picker's dB
readout and its linear readout — is built on "colour much hotter than reference".

**Also note the seam inside that tool:** the dB readout anchors 100 % at −24 dB, while the linear readout divides by
−22.5 dB. That 1.5 dB discrepancy is visible to any builder who tries to reproduce the numbers exactly, and it is
probably not meaningful — but it does mean "reproduce this tool bit-exactly" is not a well-defined requirement.

### 3.4 Why the two sides are not equal witnesses

- The 4:1 statement is made **twice, independently, by two MindPlace-side people**, one of whom is the encoder's author
  and calls it "SS standard". It is also the operating point every artefact in the producer's own tooling encodes.
- The 1:1 statement appears in **one place**: a mixing table in a third-party vendor document.
- That vendor document is **not independent of the MindPlace lineage**. Provenance chain: the 2019 forum user "Cymatic"
  announced "a SpectraStrobe resources repository" (thread 3173 page 2: "I compiled everything that I learned and
  created a SpectraStrobe resources repository"); that repo is `CymaticLabs/SpectraStrobe`, now retired with a README
  pointing to "https://docs.cymaticsomatics.com/docs/spectrastrobe" and to "Prism", and the Lumasonic repo is described
  in its own README as "the legacy documentation and binary releases of Prism Studio […] from Cymatic Somatics Inc."
  The docs site root now resolves (HTTP 200, final URL) to <https://github.com/Lumasonic/Lumasonic>. INFERENCE:
  Cymatic (2019 forum) = Cymatic Somatics (Lumasonic docs author) — strongly indicated by the shared repository lineage,
  not stated verbatim by either party.
- So the "two sides" are: the format owner + encoder author + their tooling, versus a later third-party restatement by a
  producer who had been told the −24 dB figure directly in 2019 ("Signal levels of the RGB color tones representing
  100% brightness are -24 dB" was addressed to him by name). The 1:1 table postdates that correction.

### 3.5 What actually reconciles everything

The one model that fits *all* the statements, including the awkward ones (INFERENCE — the decoder law itself is
undocumented, §2.7):

Brighness ∝ colour carrier amplitude ÷ reference carrier amplitude, with full brightness reached at a **ratio well above
unity** (the stated 4:1). Under that model:
- Hendrickson/TomasC describe the **operating point** that makes the ratio land in the usable window and leaves −2 dB
  for programme: reference −36, colour −24.
- The Lumasonic mixing table describes the **control surface** ("move the colour fader up toward and past the reference
  fader's position") and loses the constant — it is a *direction* table, not a calibration table. As a calibration
  table it is wrong.
- JoeV's counter-intuitive claim becomes coherent, thread 3737:
  > The volume of the reference track will affect the brightness of all three colors. The lower the volume, the brighter
  > the colors. If you set the volume too low, it will drop into Audiostrobe mode.
  Under a ratio law this is exactly right, and it explains why the reference level is a delicate setting with a floor
  (detection) and a ceiling (brightness headroom).
- Cymatic's 2019 field report becomes coherent, thread 3173 page 1:
  > I upped the SS reference tone by about 12 db and it seems like as long as the other tones are all brought up to
  > match the reference tone db level, then the brightness is relative to the reference tone and so there's not this
  > brightness mis-match issue I was seeing earlier
  He matched colour to reference and got *proportional* brightness — but he never claimed he was at *maximum* brightness,
  which under the ratio model he would not have been (he'd be ~25 % if full scale is 4:1).
- Hendrickson's 2015 blog note is the same thing stated as a calibration, blog entry 65:
  > Note the volume of light tracks need to be low. An ideal balance has been set so you won't want to go higher than
  > this. If you go lower it alters the brightness and balance of the lights. This is how you create color mixes.

Caveat, stated plainly: this reconciliation is a **model**, not a quotation. No public source states the decoder's
transfer function, and the model cannot be confirmed without hardware measurement.

### 3.6 Recommendation for a conservative implementation

1. **Adopt 4:1 as the operating point, not 1:1, and not 5:1.** Reference carrier peak = **−36 dBFS** per channel;
   a colour channel at 100 % brightness = **−24 dBFS** peak, i.e. **+12 dB relative**. Reasons: it is the number stated
   by the encoder author and called "SS standard"; it is the number the producer's own colour tool uses; it is the only
   value that simultaneously leaves the documented ≈ −2 dB of headroom.
2. **Do not implement the 1:1 table.** Under the ratio model, a 1:1 implementation under-drives every colour by 12 dB
   (dim, grainy, poor resolution at low brightness). Under any other model it is still 12 dB away from what the
   hardware's own production tooling emits. Either way it is not the safe choice.
3. **Make the two gains first-class and independent**, exactly like Prism ("`Reference Gain` dB Output Level for the
   Lumasonic or SpectraStrobe Reference Tone. `Color Gain` dB Output Level for the Color Tones." — 03-settings-screen.md).
   Default Reference = −36 dB, Color = −24 dB (i.e. +12 dB), and let the user trim both. The Prism docs do **not**
   publish its defaults (OPEN), so this default is ours, taken from the MindPlace-side sources. Exposing them is what
   makes the conflict non-fatal: if hardware testing shows saturation or dimness, the fix is a parameter change, not a
   re-architecture.
4. **Encode brightness as a ratio to the reference, internally.** Store/steam an integer "brightness 0…1" per colour and
   compute `colourAmp = referenceAmp × 4 × brightness`, clamped to the colour ceiling. That way, if empirical evidence
   later shows the true full-scale ratio is 1:1 or 5:1, only one constant changes and every preset keeps its look.
5. **Keep the reference hard-panned, 100 % depth, 0 % offset, sine, 20 Hz**, because that is the only fully documented,
   vendor-recommended, artefact-free configuration, with the decoder documented as tolerant. Do not attempt to
   bit-match the 12.5 % rectangular gating: TomasC explicitly says "The SS dedocer in Kasina is quite tolerant,
   regarding the width of ref. signal pulses, so the pulse width can differ a bit", and the vendor warns square/pulse
   panning is more audible.
6. **Make the reference the last thing that stops and the first thing that starts.** Detection is "reference present";
   no lead-in is documented, so do not ship a designed silence. Start the reference on the first sample and end the file
   when the reference ends.
7. **Respect the headroom budget.** Worst-case coherent carrier sum is ≈ −12.4 dBFS (§2.5). Any master limiter must sit
   *before* carrier injection (or the carriers must bypass it), otherwise programme dynamics become brightness
   modulation. JoeV's bus advice is the production form of this rule.
8. **Band-limit the light envelopes.** Minimum rise/fall times in the few-millisecond range, or band-pass each carrier
   after modulation. This is the difference between clean colour and 500 Hz-spaced crosstalk, and it is the single most
   likely source of "the lights do things I didn't ask for" in a drum machine whose whole point is fast transients.
9. **Render/export discipline:** 48 kHz or 44.1 kHz, 16-bit signed PCM, stereo, dither off, no Wave64, no WAV metadata
   chunks; MP3 only 320 kbps CBR forced-stereo, never VBR and never joint/intensity stereo; never mono-sum.
10. **Flag the residual risk.** Anything below a few percent brightness sits close to the undithered 16-bit noise floor
    *relative to the carrier* — at colour −24 dBFS with a −96 dBFS floor there is ~72 dB of range, so dimming below
    about −60 dB relative to full brightness is quantised. If KLOPGEEST needs very dim light, it should shape the
    brightness curve above that floor rather than let the fader reach −INF smoothly.

---

## 4. Access log (retrievals, failures, recovery)

All retrievals via `source_read`, signed with Web Bot Auth (keyid `ey9Tj49-ASckC30cTNl43ep9fSSbXbX0ZmKAk9VEdOo`,
agent `https://dsh-research-reader.complex-tang.workers.dev`). Times are `retrieved_at` (UTC), 2026-11-06.

| URL | Status | Type | Outcome |
|---|---|---|---|
| raw.githubusercontent.com/Lumasonic/Lumasonic/main/docs/08-SpectraStrobe/03-format-specification.md | 200 | text | read, sha256 41fc069c… |
| …/08-SpectraStrobe/05-production-and-mixing.md | 200 | text | read, sha256 20b3741e… |
| …/08-SpectraStrobe/04-file-formats.md | 200 | text | read, sha256 f8cfd68f… |
| …/08-SpectraStrobe/02-devices.md | 200 | text | read, sha256 e49f7c22… |
| …/08-SpectraStrobe/01-spectrastrobe-introduction.md | 200 | text | read, sha256 b9d28cf8… |
| …/08-SpectraStrobe/06-troubleshooting.md | 200 | text | **content is a stub**: the whole file is "### TBD" (7 bytes) — nothing recovered, nothing substituted |
| …/03-Prism-VST/03-settings-screen.md | 200 | text | read, sha256 a39dea55… |
| …/03-Prism-VST/07-rendering-output.md | 200 | text | read, sha256 670b422b… |
| …/03-Prism-VST/08-kasina.md | 200 | text | read, sha256 149bc802… |
| …/03-Prism-VST/02-editor-screen.md | 200 | text | read, sha256 11083716… |
| …/03-Prism-VST/01-prism-plugin-overview.md | 200 | text | read, sha256 0e85a592… |
| api.github.com/repos/Lumasonic/Lumasonic/git/trees/main?recursive=1 | 200 | JSON | used to enumerate `docs/08-SpectraStrobe/*` — confirms there is **no** extra SpectraStrobe doc beyond 01–06 |
| raw.githubusercontent.com/Lumasonic/Lumasonic/main/README.md | 200 | text | read (provenance: Cymatic Somatics, MIT, company ceased) |
| mindplacesupport.com/download/1244/ | 200 | **PDF** (539 655 bytes, pdftotext) | Kasina Quickstart/advanced manual — read in full |
| mindplacesupport.com/forum/printthread.php?t=2508&pp=10 | 200 | HTML | thread "Creating Spectrastrobe signal using Kyma software" — Hendrickson 2015 posts |
| …printthread.php?t=2508&pp=10&page=2 | 200 | HTML | page 2 — pulse-width/"geometry" remarks |
| …printthread.php?t=3173&pp=10 | 200 | HTML | thread "Recommendations on maintaining precise SpectraStrobe control…" — TomasC levels, pulse width, encoder filtering |
| …printthread.php?t=3173&pp=10&page=2 | 200 | HTML | TomasC on 12.5 % rectangular envelope; **WAV metadata bug** confession + fix |
| …printthread.php?t=3737&pp=10 | 200 | HTML | thread "Strobe reference level" — JoeV on reference level ⇒ brightness, limiter/bus advice |
| …printthread.php?t=2002&pp=10 | 200 | HTML | thread "Session types" — Hendrickson 2013 on reference = "sets the volume for dynamic range" |
| mindplacesupport.com/forum/entry.php?65-…&bt=281 | 200 | HTML | Hendrickson blog entry (2015-01-08) + comment by him on "6 light channels (RGB - LR)" |
| api.github.com/repos/scottosx/Audacity-Mind-Machine-Suite/git/trees/master | 200 | JSON | enumerated the plugin/chain inventory |
| raw…/Generate Plugins/SpectraStrobe/tone-generator-ss-ref.ny | 200 | text | `scale 0.015`, 18200 Hz |
| raw…/Generate Plugins/SpectraStrobe/tone-generator-ss-channels.ny | 200 | text | `scale 0.015`, 18700/19200/19700 |
| raw…/Effect Plugins/SpectraStrobe/tone-generator-ss-channels-effect.ny | 200 | text | `scale 0.075` |
| raw…/Generate Plugins/SpectraStrobe/tone-generator-ss-picker.ny | 200 | text | `"rms" 0.075 0 0.075` |
| raw…/Audacity Chains/SS Ref Normalize.txt | 200 | text | `Level=-36.500000` |
| raw…/Audacity Chains/SS Normalize Single Tone.txt | 200 | text | `Level=-22.000000` |
| raw…/Audacity Chains/SS Normalize 2 tones.txt | 200 | text | `Level=-16.500000` |
| raw…/Audacity Chains/SS Normalize 3 tones.txt | 200 | text | `Level=-13.000000` |
| raw…/Audacity Chains/SS Normalize SoundTrack.txt | 200 | text | `Level=-2.000000` |
| raw…/Audacity Chains/SS Ref LFO.txt | 200 | text | `rate=20 waveform=pulse duty=50 left=0 right=100` |
| github.com/scottosx/Audacity-Mind-Machine-Suite/wiki | 200 | HTML | "Home" wiki page, Hendrickson, page edit 2016-01-12 |
| raw.githubusercontent.com/scottosx/AVS-Color-Wheel/master/ColorWheel.xojo_code | 200 | text | UI colour wheel only — **no** dB constants; not usable, superseded by the next row |
| raw.githubusercontent.com/scottosx/AVS-Color-Wheel/master/Window1.xojo_window | 200 | text (71 KB) | **the dB/linear conversion functions** — decisive for §3 |
| api.github.com/repos/Syluminose/SpiraStrober/git/trees/main | 200 | JSON | repo has only LICENSE, README.md, index.html |
| raw.githubusercontent.com/Syluminose/SpiraStrober/main/index.html | 200 | text (25 KB, minified) | `sr=44100,fc=[18700,19200,19700,18200]` |
| raw.githubusercontent.com/Syluminose/SpiraStrober/main/README.md | 200 | text (French) | states format = SpectraStrobe + AudioStrobe, carriers "autour de 18-20 kHz"; **no dB convention documented** |
| raw.githubusercontent.com/CymaticLabs/SpectraStrobe/main/README.md | 200 | text | retired; points to docs.cymaticsomatics.com and Prism |
| api.github.com/repos/CymaticLabs/SpectraStrobe/git/trees/main | 200 | JSON | repo renamed to `meverett/SpectraStrobe`; only README + screenshot |
| **docs.cymaticsomatics.com/docs/spectrastrobe** | **404** | text | **failure**: the URL advertised by the retired repo's README no longer exists |
| docs.cymaticsomatics.com/ | 200 (final URL `https://github.com/Lumasonic/Lumasonic`) | HTML | **recovery**: the docs host now redirects to the Lumasonic repo, i.e. the Lumasonic `docs/` tree *is* the successor of that documentation. Nothing lost. |
| craigtavs.wordpress.com/tag/spectrastrobe/ | 200 | HTML | Craig Tice 2013: "SpectraStrobe knows all about AudioStrobe and can then go on to detect the additional signals…" |

Web searches run (discovery only; results were not used as evidence): SpectraStrobe lead-in / detection; MindPlace
SpectraStrobe 18200 reference; Kasina decoder bandpass; "SpectraStrobe" specification PDF. The searches surfaced the
CymaticLabs repo (used), the CraigT blog (used) and an **Enkidu "AudioStrobe Plus (ASP)" creation guide**
(<https://www.enkidulight.com/wp-content/uploads/2026/06/8-Enkidu-AVS-Session-Creation-Guide.pdf>) which is a *different
format* and was **not** used — do not let it be mistaken for a SpectraStrobe source.

No Cloudflare challenge, login wall, paywall or CAPTCHA was encountered. Two forum threads were fetched through the
`printthread.php` view from the start (the `showthread.php` URLs were not used), which is the site's own printable
rendering, not a workaround of any access control. One PDF (`download/1244`) was fetched as a PDF and text-extracted
locally. No robots.txt exception was relied upon for any material source in this task.

---

## 5. What could not be established, and why I stopped

**Not established (OPEN), in order of importance to a builder:**

1. **The decoder's brightness law.** Nothing public states how the Kasina maps carrier amplitude (absolute or relative to
   the reference) onto LED drive. This is the single largest unknown and it is exactly what would settle §3 by
   measurement rather than by witness count. Only hardware measurement (sweep colour level with a fixed reference and
   log LED output photometrically, or instrument the LED drive) can close it.
2. **A minimum lead-in / timing budget for autosensing.** No source states how long the reference must be present before
   SS is latched, or how quickly it drops back to AS. Several statements describe *that* autosensing happens, none
   describe *how fast*.
3. **Stop/pause/end-of-file light behaviour.**
4. **Quantitative carrier tolerance** (how far off 18 700 Hz a "red" carrier may be), and the decoder's filter
   bandwidths, order and stop-band rejection.
5. **What clipping / mono summing / resampling / low-bitrate lossy coding concretely do to the LED output.** Only the
   mechanisms are inferable; there are no measurements. The vendor's rules (320 kbps CBR, stereo, no VBR) are stated
   without underlying data.
6. **The Prism defaults for `Reference Gain` and `Color Gain`.** The vendor documents that the parameters exist and what
   they do, but publishes no default values — so the most modern commercial encoder cannot be used as an independent
   witness for the 4:1 operating point.
7. **Noise shaping** (only dither is addressed), and **bit-depth floor behaviour** below −60 dB relative brightness.
8. **Whether the 2019 WAV-metadata firmware fix shipped in all units.**
9. **A normative SpectraStrobe specification.** Craig Tice calls it "an open standard", but no MindPlace-published
   specification document was found; every technical statement in circulation is a forum post, a vendor doc, or code.

**Why I stopped.** The bounded question is answered to the level the public record allows: facts 1 and 4 are fully
established; 2, 5, 6 and 7 are established as far as the record goes with explicit OPEN edges; fact 3 is contested and
resolved for engineering purposes with the contest documented. The remaining gaps are not retrieval failures — they are
things that are not on the public record, and no further public retrieval can close them. The next step that *would*
close them is empirical (hardware), which is outside this task.

**Material sources the PI may be able to obtain legitimately** (requests, not bypasses):
- **Hardware measurement on a Kasina** (the only way to resolve §3 definitively). Protocol: fixed reference at
  −36 dBFS, sweep one colour carrier from −40 to −18 dBFS in 1 dB steps, log LED output with a photodiode/phone camera
  in a dark box, per colour, per eye; repeat with reference at −42 and −30 dBFS to identify whether the law is ratio-based.
- **Tomas Cerny** (Kasina encoder author, active on the MindPlace forum as `TomasC`): a direct question asking him to
  confirm the −24 dB figure still stands and to state whether the decoder divides by the reference amplitude. He has
  answered exactly this class of question publicly twice (2019).
- **Craig Tice** (`CraigT`, Kasina session author, MuLab/Kasina Effect editor): his MuLab presets are the other
  first-party encoder and would give a third independent numeric convention.
- **The 2019-04-18 beta firmware and the "short stereo WAV file with clean SS ref. signals"** attached to TomasC's post
  in thread 3173 — the attachments are not reachable from the print view; a forum member with an account could retrieve
  them. That file is the closest thing to a reference-grade test vector that exists publicly.
- **Mind Workstation 1.5** (Transparent Corporation, discontinued): the original SS encoder. Its output would settle the
  4:1 vs 5:1 question directly by analysis. Licences are no longer sold; a legitimate copy would have to come from an
  existing owner (e.g. JoeV, who says he has one on an old laptop) — explicitly *not* from a warez source.

**Adjacent questions noticed but not researched** (out of scope for this task): the Lumasonic (non-SpectraStrobe) codec's
own specification, which is documented in the same repo and may reveal how a modern decoder is built; the KBS session
format; the AudioStrobe (AS) specification proper and its own reference/threshold conventions; the sACN output path in
Prism (an alternative non-audio light transport that KLOPGEEST might prefer over carrier encoding); and the
`MindPlace ColorSets` colour-mapping tables (which govern how AudioStrobe content is displayed and would matter if
KLOPGEEST ever emits AudioStrobe).
