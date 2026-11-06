# Provenance of the reference implementation

## What is in this directory

| File | Lines | Origin |
|---|---|---|
| `render.py` | 851 | NAGLOED, `experiments/spectrastrobe-poc/render.py` |
| `verify.py` | 1679 | NAGLOED, `experiments/spectrastrobe-poc/verify.py` |

## Exact provenance

| | |
|---|---|
| Source repository | `github.com/leonvanbokhorst/nagloed` (local clone: `~/repos/granular-experimental-plugin`) |
| Branch | `feature/v2-exploration` |
| Commit | `862dec2aa5143d2e8a7ee8b8349f5ffeabb36d0d` — *"docs(calibration): annotate multi-colour activity for issue #31"*, 2026-11-05 |
| Path | `experiments/spectrastrobe-poc/` |
| Retrieved | 2026-11-06, via `git show <commit>:<path>` |
| sha256 `render.py` | `541da4ac2b0663f50eda3ed312a0e6db443681b9fd4f0dcfc913576f3fc69742` |
| sha256 `verify.py` | `2133c56efbb383ce60a2a7740a60e4d38acbea58214ee38928322b5ba141545e` |

Files copied byte-for-byte. No edits were made.

## Why they are here

They are the only working, verified SpectraStrobe encoder and measurement suite this project has
access to. A C++ implementation needs a **differential oracle**: something independent that produces
the same expected numbers. `verify.py` is also the source of the measurement conventions — and of
four documented defects — catalogued in
[`../05-acceptance-and-tests.md`](../05-acceptance-and-tests.md) §2.3 and
[`../evidence/measurement-plan.md`](../evidence/measurement-plan.md).

Audited and run on 2026-11-06: `render.py` + `verify.py` → **52 PASS / 0 FAIL / 1 INFO** of 53 core
checks; the calibration suite 41/41; the A/B suite 117/117; renders byte-identical across three
independent runs.

## Licence

Both files are **GPL-3.0-or-later**, as is KLOPGEEST, and both projects share the same copyright
holder (Master Lonn & Little Wan). There is no third-party code in either file. Copying is
licence-compatible.

## The independence statement — decision taken

`LICENSE` (line 16) and `README.md` (line 9) previously stated that KLOPGEEST **shares no code with
NAGLOED**. That was true when written and was no longer exactly true once this directory was added.

**The maintainer's decision (2026-11-06): keep the copy, and amend both statements honestly.** Both
files now say that no NAGLOED code is compiled into the instrument, that one GPL-3.0-or-later research
copy of the SpectraStrobe reference encoder and verifier is vendored here for test-oracle purposes
only, and they point at this file for the provenance.

**To reverse the decision**, either delete this directory — the rest of the research package stands
without it, because [`../evidence/reference-implementation.md`](../evidence/reference-implementation.md)
carries the full language-neutral algorithm description and the golden-vector plan — or restore the
two sentences from git history. Nothing else depends on it.

## How to use them

```bash
cd reports/spectrastrobe/reference
python3 render.py --score <score.json> --out-dir /tmp/out     # Python 3, stdlib only
python3 verify.py  --score <score.json> --out-dir /tmp/out
```

`render_mix(score, fs, with_programme, colour_scale, clamp_log)` in `render.py` is the pure function to
call from a golden-vector generator — **not** the CLI, which is hard-wired to a 72-second composition.

**Do not copy from these files blindly.** Read
[`../05-acceptance-and-tests.md`](../05-acceptance-and-tests.md) §5 first: some of what is in here —
the band-limiting filter in particular — is wrong for a percussion instrument.
