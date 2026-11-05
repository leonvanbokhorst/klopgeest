# KLOPGEEST — Final Validation

Status: **TEMPLATE — Phase A.** Every `[FILL IN PHASE B: …]` placeholder is
filled in by QA when DSP and UI have landed. Nothing in this document may claim
a result that was not produced by a real run. This file is owned by QA; do not
edit it from other workstreams — send corrections instead.

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

The single command that runs 1–6 in order and prints a PASS/FAIL/SKIP summary is:

```sh
./scripts/validate.sh
```

Useful variants during the run-up:

```sh
./scripts/validate.sh --quick             # skip pluginval (the slow step)
./scripts/validate.sh --skip-auval        # skip AU install + auval + AU pluginval
./scripts/validate.sh --skip-pluginval    # skip pluginval only
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
| Date/time | `[FILL IN PHASE B: YYYY-MM-DD HH:MM Europe/Amsterdam]` |
| Commit / source state | `[FILL IN PHASE B: git rev-parse HEAD + dirty flag, or "no commits yet, working tree at …"]` |
| Machine / macOS | `[FILL IN PHASE B: uname -m; sw_vers -productVersion]` |
| Xcode CLT | `[FILL IN PHASE B: xcode-select -p; clang --version]` |
| CMake | `[FILL IN PHASE B: cmake --version]` |
| JUCE | `9.0.5`, local checkout `.deps/JUCE` |
| pluginval | `.deps/pluginval/pluginval.app` version `[FILL IN PHASE B]` |

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

Result: `[FILL IN PHASE B: PASS/FAIL, warnings, build time]`

> Note: plug-in bundles are **not** auto-installed into `~/Library/Audio/Plug-Ins`
> by the build (`COPY_PLUGIN_AFTER_BUILD FALSE`); only the AU is copied there by
> the validation script's auval step, and that copy needs host permission.

## 4. Headless tests

```sh
./build/klop_tool_artefacts/Release/klop_tool test
```

Expected summary line: `N checks, 0 failures, K skipped`.

Record the actual numbers here:

| Suite | Checks | Failures | Notes |
| --- | --- | --- | --- |
| Legacy smoke (`TestMain.cpp`) | `[FILL IN PHASE B]` | `[FILL IN PHASE B]` | parameters, patterns, state, determinism, mutation |
| QA integration (`QaIntegrationTests.cpp`) | `[FILL IN PHASE B]` | `[FILL IN PHASE B]` | allocation, rates, block size, transport, fuzz, threads |
| Sequencer/groove (`SequencerTests.cpp`) | `[FILL IN PHASE B]` | `[FILL IN PHASE B]` | |
| Voices (`VoiceTests.cpp`) | `[FILL IN PHASE B]` | `[FILL IN PHASE B]` | |
| Master (`MasterTests.cpp`) | `[FILL IN PHASE B]` | `[FILL IN PHASE B]` | |

Acceptance: `0 failures`. Skips only for the items listed in §9.

`[FILL IN PHASE B: full summary line]`

## 5. Demo render

```sh
./build/klop_tool_artefacts/Release/klop_tool render Demo/Klopgeest-Demo.wav
```

Expected output ends with:

```
bars: 24  peak: <level> dBFS  full-scale samples: <n>  finite: yes
```

| Check | Expected | Measured |
| --- | --- | --- |
| Exit code | `0` | `[FILL IN PHASE B]` |
| Peak (dBFS) | `-18 … -0.5`, not silent | `[FILL IN PHASE B]` |
| Full-scale samples | `0` in a sane mix (limiter catches true peaks) | `[FILL IN PHASE B]` |
| Finite | `yes` | `[FILL IN PHASE B]` |
| File size / duration | ~48 s, 24-bit stereo WAV | `[FILL IN PHASE B]` |
| Listening check | musically intelligible, drum roles audible, no clicks/dropouts | `[FILL IN PHASE B]` |

The render is silent while the temporary sequencer stub is in place; a silent
render is a **failure** unless `--allow-silent-render` was explicitly used for a
Phase A smoke run.

## 6. Editor snapshot

```sh
./build/klop_tool_artefacts/Release/klop_tool snapshot docs/ui-preview.png
```

Expected: `wrote /…/docs/ui-preview.png (1180 x 680)`, valid non-blank PNG.
Manual check: `[FILL IN PHASE B: does the snapshot show the real editor (
step grid, voice strips, groove + glue panels) rather than the placeholder?]`

## 7. AU validation (auval) and plug-in validation (pluginval)

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
Recorded result: `[FILL IN PHASE B: PASS/FAIL, auval version, log path]`

If the copy is denied by the sandbox, the gate reports the auval step as
**SKIP** (not PASS) and names the reason. Re-run with host approval; pluginval
on the AU is likewise skipped until the AU is installed.

### pluginval strictness 10

```sh
PLUGINVAL=.deps/pluginval/pluginval.app/Contents/MacOS/pluginval

# VST3 from the build tree
"$PLUGINVAL" --strictness-level 10 --skip-gui-tests \
    --validate build/KLOPGEEST_artefacts/Release/VST3/KLOPGEEST.vst3

# AU (after the auval install above)
"$PLUGINVAL" --strictness-level 10 --skip-gui-tests \
    --validate ~/Library/Audio/Plug-Ins/Components/KLOPGEEST.component
```

Recorded result: `[FILL IN PHASE B: VST3 PASS/FAIL, AU PASS/FAIL, log paths]`

`--skip-gui-tests` is used because the gate also runs headless. GUI editor tests
are covered by the manual DAW smoke test and the offscreen snapshot.

## 8. Manual DAW smoke test

`[FILL IN PHASE B: perform in at least one host — e.g. Logic Pro / Ableton Live /
Reaper — and record host version]`

Checklist:

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

Host/version: `[FILL IN PHASE B]`
Result: `[FILL IN PHASE B]`

## 9. Known skips and caveats

Filled in at validation time. As of Phase A the standing caveats are:

- `[FILL IN PHASE B: final list of skipped tests/steps and why]`
- Demo render and the sequenced-audio assertions are skipped while the temporary
  sequencer stub (`src/sequencer/Sequencer.cpp` / `GrooveEngine.cpp`) is in place.
- `--skip-gui-tests` is passed to pluginval; editor interaction is not
  machine-tested.
- pluginval AU validation depends on installing the AU into
  `~/Library/Audio/Plug-Ins/Components`, which needs host permission.

## 10. Sign-off

| Field | Value |
| --- | --- |
| Gate command | `./scripts/validate.sh` |
| PASS / FAIL / SKIP | `[FILL IN PHASE B: e.g. 6 PASS, 0 FAIL, 1 SKIP]` |
| Log directory | `build/validate-logs/` |
| Validated by | QA workstream |
| Reviewed by | `[FILL IN PHASE B: Lead]` |
| Date | `[FILL IN PHASE B]` |
| Verdict | `[FILL IN PHASE B: RELEASABLE / NOT RELEASABLE — reasons]` |

A claim of "validated" without the filled-in numbers above and the
`build/validate-logs/` evidence is not a validation.
