# KLOPGEEST — Final Validation

Status: **FINAL GATE PASSED (automated steps) — 2026-11-05 17:57
Europe/Amsterdam.** DSP, sequencer/groove, glue machine and editor are complete
(`src/ui` frozen); the final AU/VST3/Standalone build passed, the headless suite
is 570/570 with 0 skips, the demo render and UI snapshot are deterministic, and
`auval` + pluginval strictness 10 pass on both AU and VST3 (final runs include
pluginval's GUI tests). **The manual DAW smoke test was NOT performed** — that
is recorded as a limitation in §8/§9, not claimed. Nothing in this document
claims a result that was not produced by a real run. This file is owned by QA;
do not edit it from other workstreams — send corrections instead.

## 1. What "validated" means

KLOPGEEST is considered releasable for the `0.1.0` milestone when all of the
following are true on a clean macOS arm64 machine with Command Line Tools:

1. `./scripts/dev-build.sh KLOPGEEST_AU KLOPGEEST_VST3 KLOPGEEST_Standalone klop_tool` succeeds.
2. `klop_tool test` reports **0 failures** and the only skips are documented in
   "Known skips" below.
3. `klop_tool render` produces a finite, audible 48 s demo (`Demo/Klopgeest-Demo.wav`).
4. `klop_tool snapshot` produces the 1180×680 editor image (`docs/ui-preview.png`)
   and it shows the real editor, not the placeholder.
5. The AU passes Apple's `auval` (`aumu Klpg MLLW`).
6. pluginval strictness 10 passes on the AU **and** the VST3.
7. The manual DAW smoke test (§8) has been performed and recorded.
   **For this milestone it was NOT performed** — see §8/§9; the automated gate is
   green but this criterion is an explicit, recorded limitation that the Lead
   must weigh in the release decision.

The single command that runs 1–6 in order and prints a PASS/FAIL/SKIP summary is:

```sh
./scripts/validate.sh
```

Useful variants during the run-up:

```sh
./scripts/validate.sh --quick             # skip pluginval (the slow step)
./scripts/validate.sh --skip-auval        # skip AU install + auval + AU pluginval
./scripts/validate.sh --skip-pluginval    # skip pluginval only
./scripts/validate.sh --skip-gui-tests    # pluginval without GUI tests (headless CI only)
./scripts/validate.sh --no-build          # validate the artefacts already in build/
./scripts/validate.sh --allow-silent-render
```

Validation never runs git and never commits. It uses `scripts/dev-build.sh`,
which takes the shared build lock, so it must not be run concurrently with a
workstream build (the lock serialises, but the validation is meaningless if
sources change underneath it).

## 2. Environment

| Item | Value |
| --- | --- |
| Date/time | 2026-11-05 17:57 Europe/Amsterdam (final gate) |
| Commit / source state | Git repo `https://github.com/leonvanbokhorst/klopgeest.git`, branch `main`; validated working tree on top of commit `3c6b1a5` ("DSP complete: eight procedural voices, host-synced groove sequencer, glue machine; 570 checks pass"). The final docs/validation changes were still uncommitted when this run was made; the Lead commits the final tree after sign-off. |
| Machine / macOS | `arm64`, macOS 26.6.2 (build 25G83) |
| Xcode CLT | `/Library/Developer/CommandLineTools`, Apple clang version 21.0.0 (clang-2100.3.34.2) |
| CMake | cmake version 4.0.0 |
| JUCE | `9.0.5`, local checkout `.deps/JUCE` |
| pluginval | `.deps/pluginval/pluginval.app`, pluginval 1.0.4 (built against JUCE 8.0.3) |
| sox (render analysis) | `/opt/homebrew/bin/sox` |

## 3. Build

Exact command used by the gate:

```sh
cd /Users/leonvanbokhorst/repos/klopgeest
./scripts/dev-build.sh KLOPGEEST_AU KLOPGEEST_VST3 KLOPGEEST_Standalone klop_tool
```

Expected artefacts:

```
build/klop_tool_artefacts/Release/klop_tool
build/KLOPGEEST_artefacts/Release/AU/KLOPGEEST.component
build/KLOPGEEST_artefacts/Release/VST3/KLOPGEEST.vst3
build/KLOPGEEST_artefacts/Release/Standalone/KLOPGEEST.app
```

Build result (FINAL, UI frozen, 17:52–17:54):

- `klop_tool`: **PASS**, 14,454,632 bytes.
- `KLOPGEEST_AU`, `KLOPGEEST_VST3`, `KLOPGEEST_Standalone`: **PASS**, exit 0.
  Final bundles: AU `KLOPGEEST.component` binary 5,647,016 bytes,
  VST3 `KLOPGEEST` binary 5,665,024 bytes, Standalone 6,596,088 bytes.
  AU installed for the AU gate: binary 5,647,016 bytes, timestamp 17:53
  (`~/Library/Audio/Plug-Ins/Components/KLOPGEEST.component`); its SHA-256
  (`e4873ff94659bd21d259d03a268656a6fb12207c2715f79f3664e0b671ddfe47`) is
  byte-identical to the final build-tree AU, so the AU gate evidence applies to
  the final artefact.
- Warnings: none from the final plug-in build (the earlier `-Wsign-conversion`
  warnings in `src/core/Random.h` and the UI `-Wimplicit-int-float-conversion` /
  `-Wfloat-equal` warnings were cleaned up by the owning workstreams during
  Phase B).

> Note: plug-in bundles are **not** auto-installed into `~/Library/Audio/Plug-Ins`
> by the build (`COPY_PLUGIN_AFTER_BUILD FALSE`); only the AU is copied there by
> the validation script's auval step, and that copy needs host permission.

## 4. Headless tests

```sh
./build/klop_tool_artefacts/Release/klop_tool test
```

Measured on the FINAL tree (UI frozen). Log:
`build/validate-logs/tests-final.log`.

| Suite | Checks | Failures | Notes |
| --- | --- | --- | --- |
| Legacy smoke (`TestMain.cpp`) | 58 | 0 | parameters, factory patterns, pattern ValueTree round-trip, state, deterministic render, audio-thread smoke, mutation |
| QA integration (`QaIntegrationTests.cpp`) | 83 | 0 | allocation, 44.1/48/96 kHz, block-size invariance 64/128/512/2048, internal + host transport, deterministic seeds, 64-config hostile fuzz, publish thread safety |
| Sequencer/groove (`SequencerTests.cpp`) | 96 | 0 | determinism, swing/humanize, probability, locks, ratchets, host sync, jumps, free-run, mute/solo, lane lengths, ghost notes, FERAL bounds, max-out |
| Voices (`VoiceTests.cpp`) | 284 | 0 | contract, determinism, sample rates, pitch/level/pan/choke, hostile extremes, no-click smoothing, CPU |
| Master/glue (`MasterTests.cpp`) | 49 | 0 | GR, ratio/glue/attack/release, mix, ceiling limiter, sidechain HPF, auto-release, sample rates, block size, saturation, hot bus |
| **Total** | **570** | **0** | **0 skipped** |

Full summary line:

```
570 checks, 0 failures, 0 skipped
```

Final run on the frozen tree is archived in
`build/validate-logs/tests-final.log` (exit code 0).

Notable measured details from this run:

- Audio-thread allocation freedom: 0 allocations/deallocations across 200
  `processBlock` calls, 200 direct `Engine::process` calls, 100
  `GlueMachine::process` calls, and 50 trigger/render cycles on each of the
  eight voices; and 0 allocations on a real audio thread while the main thread
  published 500 patterns.
- Block-size invariance: 64/128/512/2048-sample renders matched the 64-sample
  reference with max |diff| = 0.00000000 (tolerance 1e-4).
- Host transport simulation: 90 BPM host → `hostSynced`, bpm 90; host stop →
  falls back to internal tempo; 140 BPM change followed; seek finite and still
  synced.
- Voice CPU estimate (from `VoiceTests`): 8 voices at 48 kHz/128 ≈ 97× realtime,
  ~1.03 % of one core (excluding the master bus).
- Glue machine: +18 dBFS input with ceiling −3 dBFS → output peak −3.000000 dBFS;
  512 vs 1-sample block max difference 0.000000.

## 5. Demo render

```sh
./build/klop_tool_artefacts/Release/klop_tool render Demo/Klopgeest-Demo.wav
```

Measured output (composition rebalanced in Phase B with Lead authorisation so
the performance builds instead of fading; five sections: STRAIGHT FIRE →
WAREHOUSE → BROKEN STEP (mutate) → FERAL DUB (nudge) → SIXTEENTH ANXIETY
(mutate), with master output +0 → +3.5 dB and the ceiling at −1.2 dBFS):

```
bars: 24  peak: -1.20 dBFS  full-scale samples: 0  finite: yes
```

| Check | Expected | Measured |
| --- | --- | --- |
| Exit code | `0` | `0` |
| Duration | 48.0 s | 48.000000 s (24 bars at 120 BPM) |
| Format | 24-bit stereo WAV @ 48 kHz | 24-bit stereo WAV @ 48 kHz, 13 824 104 bytes |
| Peak | ≤ −1 dBFS, not silent | **−1.20 dBFS** |
| Full-scale samples | 0 | 0 |
| Finite | yes | yes |
| SHA-256 | stable across runs | `d5e1302282268585f7db370e88e1d4a96a4c4681b5477958b0a1751092285ffc` |
| Determinism | two runs hash-identical | PASS (same SHA-256 twice) |
| Wall time | — | 0.54–0.56 s for 48 s of audio = **≈ 87× realtime**, 0.52 s user CPU (≈ 1.0 % of one core; includes 24-bit WAV writing) |

Section loudness (sox `stats`; 12 s sections):

| Section | Content | Peak | RMS |
| --- | --- | --- | --- |
| 0–12 s | STRAIGHT FIRE, LOOSE, output 0 dB | −1.21 dBFS | −14.77 dBFS |
| 12–24 s | WAREHOUSE, LOOSE, output +1.5 dB | −1.62 dBFS | −13.75 dBFS |
| 24–36 s | BROKEN STEP, FERAL + mutate, output +2.5 dB | −2.19 dBFS | −13.67 dBFS |
| 36–48 s | FERAL DUB (+nudge) into SIXTEENTH ANXIETY (+mutate), output +4.0/+3.5 dB | −1.20 dBFS | −12.74 dBFS |
| Full file | — | −1.20 dBFS | −13.67 dBFS |

The RMS column builds across the whole take (−14.8 → −12.7 dBFS) and the last
12 s is the loudest and densest part; peak stays at the −1.2 dBFS ceiling with
zero full-scale samples.

Listening check: **not performed** — the numbers above are measured with
`sox`, not a human listening pass. Do not read this as a subjective quality
claim; a listening pass is a manual step outside this automated gate.

## 6. Editor snapshot

```sh
./build/klop_tool_artefacts/Release/klop_tool snapshot docs/ui-preview.png
```

Measured with the real editor (17:43 capture, re-captured 17:44):

```
wrote /…/docs/ui-preview.png (1180 x 680)   [161664 bytes]
```

| Check | Result |
| --- | --- |
| File | valid 1180×680 PNG, 161,664 bytes |
| Background | dark industrial panel, non-blank |
| Reproducibility | two independent captures byte-identical (final UI) |
| SHA-256 | `a725ba34ae0a5d17b6c060b9d9669f99c693305502e4b7dbefbbc90ec95fe5ab` |
| Header | wordmark, PLAY, 120.0 BPM, personality TIGHT/LOOSE/FERAL, pattern selector, length, seed, MACROS — all legible, no overlap |
| Step grid | 8 lanes × 16 steps, step numbers, mute/solo/lock, now-playing indication, velocity shading, ratchet marks visible |
| Step inspector | velocity/probability/micro/pitch/ratchet + ON/LOCK + lane mute/solo/lock/length, no clipping |
| Voice panel | all 15 voice parameters + "why this hit differs" readout and flag row (GHOST/ACCENT/RATCH/FERAL/LIVE/MUT) visible |
| Groove/Glue | tabs present (VOICE selected in this capture); panels not exercised in the snapshot |
| Defects found | none blocking. The earlier wording nit (lane length labelled "LOOP") was fixed by the UI workstream: it now reads "LANE LEN" everywhere. A minor redundancy remains: the pattern name appears in both the top-bar PATTERN selector and the grid title — cosmetic only. |

Final capture is post-UI-freeze (`src/ui` frozen at 17:50). Resize behaviour is
not covered by the offscreen snapshot (the tool captures the fixed 1180×680
default size); the UI workstream additionally reports 16/32/64-step layouts and
the 1080×620 minimum verified with its own offscreen harness, but that harness is
not checked into the repo and is therefore **not** cited as QA evidence.

## 7. AU validation (auval) and plug-in validation (pluginval)

VST3 (FINAL build, **with GUI tests**): **PASS** — pluginval strictness 10,
seed `0x3eafa08`, exit code 0, 25/25 tests completed (including editor
open/close), log ends `SUCCESS`. Log:
`build/validate-logs/pluginval-vst3-final.log`. (A QA pre-run with
`--skip-gui-tests` also passed 21/21; the GUI-inclusive run is the cited one.)

AU (FINAL build, **with GUI tests**): **PASS** — pluginval strictness 10,
seed `0x3eafa08`, against the final installed AU (binary 5,647,016 B, 17:53),
25/25 tests completed (including editor open/close), exit code 0, log ends
`SUCCESS`. Log: `build/validate-logs/pluginval-au-final.log`. The AU install
requires host permission, so the Lead performed the install and both AU runs;
QA independently re-ran the read-only `auval` (see below).

### AU install + auval

The AU must be visible to the AudioComponentRegistrar. The gate does this:

```sh
# 1. Copy the built AU into the user Components folder (requires permission)
mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -R build/KLOPGEEST_artefacts/Release/AU/KLOPGEEST.component \
      ~/Library/Audio/Plug-Ins/Components/

# 2. Restart the registrar so the new component is rescanned
killall -9 AudioComponentRegistrar 2>/dev/null || true

# 3. Validate (script retries 3× with a registrar restart in between)
auval -v aumu Klpg MLLW
```

Expected last lines include `AU VALIDATION SUCCEEDED`.

The QA session sandbox cannot write `~/Library/Audio/Plug-Ins/Components`
(verified read-only), so the Lead performed the install with host permission.
Installed bundle: binary 5,647,016 bytes, timestamp 17:53, SHA-256
`e4873ff94659bd21d259d03a268656a6fb12207c2715f79f3664e0b671ddfe47` — byte
identical to the final build-tree AU.

Result on the FINAL build: `auval -v aumu Klpg MLLW` → **AU VALIDATION
SUCCEEDED** (170 PASS lines; all groups PASS including MIDI, ramped parameter
scheduling and sample rates; cold open 10.3 ms / warm open 0.6 ms). Log:
`build/validate-logs/auval-final.log`. QA independently re-ran the read-only
`auval` against the same installed bundle and also got exit code 0 with
`AU VALIDATION SUCCEEDED`.

### pluginval strictness 10

```sh
PLUGINVAL=.deps/pluginval/pluginval.app/Contents/MacOS/pluginval

# VST3 from the build tree (release gate: GUI tests included)
"$PLUGINVAL" --strictness-level 10 --random-seed 0x3eafa08 \
    --validate build/KLOPGEEST_artefacts/Release/VST3/KLOPGEEST.vst3

# AU (after the auval install above; release gate: GUI tests included)
"$PLUGINVAL" --strictness-level 10 --random-seed 0x3eafa08 \
    --validate ~/Library/Audio/Plug-Ins/Components/KLOPGEEST.component

# Headless CI only: add --skip-gui-tests to either command (21/21 tests
# instead of 25/25; both variants pass).
```

Recorded result: VST3 **PASS** and AU **PASS**, both on the FINAL build and
both **with GUI tests** (editor open/close): 25/25 tests each, `SUCCESS`, logs
`build/validate-logs/pluginval-vst3-final.log` and
`build/validate-logs/pluginval-au-final.log`.

Non-GUI modes: VST3 on the FINAL artefact passes 21/21
(`build/validate-logs/pluginval-vst3-final-nogui.log`). AU non-GUI could not be
re-run from the QA session: pluginval in that session cannot enumerate
AudioComponents (`Num plugins found: 0`, exit 1, log
`build/validate-logs/pluginval-au-final-nogui-env-scope.log`) even though
`auval` in the same session passes — an AudioComponent-registry visibility
limitation of the sandboxed QA process, not a plug-in failure. The AU non-GUI
mode passed 21/21 on the pre-final build (`build/validate-logs/pluginval-au.log`,
Lead run), and the AU final evidence is the Lead's GUI-inclusive 25/25 run
against the byte-identical installed binary.

`scripts/validate.sh` embeds the same seed and **runs GUI tests by default**;
`--skip-gui-tests` is an opt-in for headless CI only. pluginval's GUI tests are
not a substitute for a real host — see §8 (manual DAW smoke test not performed).

## 8. Manual DAW smoke test

**NOT PERFORMED.** No DAW GUI host session was run for this milestone (Ableton
and REAPER are installed on the machine, but no automated or manual host session
was performed). The editor is covered by the offscreen snapshot and by
pluginval's GUI tests on both AU and VST3; host-level interaction (transport,
automation recording, session save/reload, live editing) is **not** verified.
This is a release limitation, not a pass.

Checklist (to be performed, with the host/version recorded in this table):

- [ ] AU and VST3 both instantiate and the editor opens at a sensible size.
- [ ] Host transport start/stop drives the sequencer; host BPM changes are followed.
- [ ] Internal play (Standalone or when the host is stopped) runs at the Tempo parameter.
- [ ] All eight voices are audible and individually adjustable.
- [ ] Swing, humanize, personality (TIGHT/LOOSE/FERAL) change feel, not level.
- [ ] Glue machine knobs audibly change dynamics/saturation; limiter ceiling holds.
- [ ] Pattern edit → immediate audible change; nudge/mutate/recall behave.
- [ ] Save session, quit, reopen: parameters + pattern restored exactly.
- [ ] No denormals, clicks, or CPU spikes when switching factory patterns live.
- [ ] AU passes `auval` and loads without validation errors in the host.

Host/version: none (not run)
Result: **NOT PERFORMED** — recorded as a limitation.

## 9. Known skips and caveats

Final gate, with honest caveats:

- **Manual DAW smoke test: NOT PERFORMED** (see §8). All host-level interaction
  claims (transport, session save/reload, automation, live editing in Ableton/
  REAPER/Logic) are unverified. Only the automated gate, pluginval's GUI tests
  and the offscreen snapshot are evidenced.
- The AU install step requires write access to
  `~/Library/Audio/Plug-Ins/Components`, which the QA session sandbox lacks; the
  Lead installed the final AU (binary 5,647,016 B, 17:53) and ran `auval` and
  AU pluginval. QA independently re-ran the read-only `auval` against the
  installed bundle. The evidence is therefore split between QA and Lead, with
  all cited logs present in `build/validate-logs/`.
- pluginval final runs **include GUI tests** (25/25, editor open/close) on both
  formats. VST3 non-GUI on the final artefact also passes (21/21); AU non-GUI
  could not be enumerated from the sandboxed QA process (registry visibility,
  documented in §7), but passed on the pre-final build in the Lead's run.
  `--skip-gui-tests` remains available for headless CI. Neither pluginval mode
  is a substitute for a real host.
- Human listening: **no formal listening pass** was recorded; the demo claims in
  §5 are metric-based only.
- Resize behaviour (min 1080×620) is not covered by the offscreen snapshot; the
  UI workstream verified it with an internal harness that is not checked in and
  is not cited as QA evidence. The snapshot captures only the 1180×680 default.
- All measurements are from one macOS arm64 machine; no Intel/Rosetta, Windows
  or Linux build is part of this milestone.
- The build is not code-signed/notarised (ad-hoc, linker-signed local builds
  only). Distribution would require signing/notarisation.
- Two real bugs were found by the QA infrastructure and fixed by the owning
  workstreams during Phase B: an audio-thread allocation in `Engine::process`
  (`std::stable_sort`, caught by `tests/AllocationCounter.*`) and lost ratchet
  data on save/load (`encodeRatchets` int-vs-char append, caught by the hostile
  state fuzz). After the fixes the suite reports 570 checks, 0 failures,
  0 skipped.
- Total skipped gate steps: **none silently skipped**. The AU install/auval/AU
  pluginval steps were executed by the Lead because of the sandbox permission
  boundary, and their logs are cited above.

## 10. Sign-off

| Field | Value |
| --- | --- |
| Gate command | `./scripts/validate.sh` (`--quick`/`--skip-auval` when the sandbox cannot install the AU) |
| PASS / FAIL / SKIP | Automated gate steps: **PASS** (build, artefacts, tests, demo render, snapshot, VST3 pluginval 10, AU pluginval 10, `auval`). Manual DAW smoke test: **NOT PERFORMED**. No step FAILED; no step was silently skipped — the AU install/auval/AU pluginval steps were executed by the Lead with host permission and their logs are cited. |
| Log directory | `build/validate-logs/` (`tests-final.log`, `auval-final.log`, `pluginval-vst3-final.log`, `pluginval-au-final.log`) |
| Validated by | QA workstream (automated gate) + Lead (AU install/auval/AU pluginval with host permission) |
| Reviewed by | Lead |
| Date | 2026-11-05 17:57 Europe/Amsterdam |
| Verdict | **Automated gate: PASS on the final build.** Release decision must account for the manual DAW smoke test not being performed (limitation §8) and the unsigned/unnotarised build. |

A claim of "validated" without the filled-in numbers above and the
`build/validate-logs/` evidence is not a validation.
