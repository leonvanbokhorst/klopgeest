#!/usr/bin/env bash
# =============================================================================
# KLOPGEEST final validation.
#
# Runs the complete, honest release gate:
#
#   1. build AU + VST3 + Standalone + headless klop_tool (serialised build)
#   2. headless test suite (klop_tool test)
#   3. demo render (Demo/Klopgeest-Demo.wav) and silence check
#   4. offscreen UI snapshot (docs/ui-preview.png)
#   5. AU install into ~/Library/Audio/Plug-Ins/Components + `auval -v aumu Klpg MLLW`
#   6. pluginval strictness 10 on AU and VST3
#   7. summary with PASS / FAIL / SKIP per step; exit non-zero on any FAIL
#
# Usage:
#   ./scripts/validate.sh                     # full gate (pluginval includes GUI tests)
#   ./scripts/validate.sh --quick             # skip pluginval (still auval)
#   ./scripts/validate.sh --skip-auval        # skip AU install/auval/AU pluginval
#   ./scripts/validate.sh --skip-pluginval    # skip pluginval only
#   ./scripts/validate.sh --skip-gui-tests    # pluginval without editor open/close
#                                             # (headless CI only; the release gate
#                                             # runs GUI tests by default)
#   ./scripts/validate.sh --allow-silent-render
#                                             # treat a silent demo as SKIP, not FAIL
#                                             # (debugging only)
#   ./scripts/validate.sh --no-build          # validate existing artefacts
#
# NOTE: this script must not run concurrently with another build. It uses
# scripts/dev-build.sh, which takes the shared build lock, so it serialises
# against workstream builds. No git commands are run, nothing is committed.
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

QUICK=0
NO_BUILD=0
SKIP_AUVAL=0
SKIP_PLUGINVAL=0
SKIP_GUI_TESTS=0
ALLOW_SILENT_RENDER=0

usage()
{
    cat <<'USAGE'
KLOPGEEST final validation gate.

Usage: ./scripts/validate.sh [options]

  --quick                skip pluginval (auval still runs)
  --no-build             validate the artefacts already in build/
  --skip-auval           skip AU install + auval + AU pluginval
  --skip-pluginval       skip pluginval only
  --skip-gui-tests       pass --skip-gui-tests to pluginval (headless CI only;
                         the release gate runs pluginval GUI tests by default)
  --allow-silent-render  treat a silent demo render as SKIP, not FAIL (debugging)
  -h, --help             show this help

Writes logs to build/validate-logs/ and prints a PASS/FAIL/SKIP summary.
Exit code is non-zero if any step FAILs.
USAGE
}

for argument in "$@"; do
    case "$argument" in
        --quick)                QUICK=1 ;;
        --no-build)             NO_BUILD=1 ;;
        --skip-auval)           SKIP_AUVAL=1 ;;
        --skip-pluginval)       SKIP_PLUGINVAL=1 ;;
        --skip-gui-tests)       SKIP_GUI_TESTS=1 ;;
        --allow-silent-render)  ALLOW_SILENT_RENDER=1 ;;
        -h|--help)              usage; exit 0 ;;
        *)
            echo "validate.sh: unknown argument '$argument' (try --help)" >&2
            exit 2
            ;;
    esac
done

[ "$QUICK" = 1 ] && SKIP_PLUGINVAL=1

# --- Layout ------------------------------------------------------------------
BUILD_DIR="$ROOT/build"
TOOL="$BUILD_DIR/klop_tool_artefacts/Release/klop_tool"
AU_BUNDLE="$BUILD_DIR/KLOPGEEST_artefacts/Release/AU/KLOPGEEST.component"
VST3_BUNDLE="$BUILD_DIR/KLOPGEEST_artefacts/Release/VST3/KLOPGEEST.vst3"
STANDALONE_APP="$BUILD_DIR/KLOPGEEST_artefacts/Release/Standalone/KLOPGEEST.app"
PLUGINVAL="$ROOT/.deps/pluginval/pluginval.app/Contents/MacOS/pluginval"
# Fixed seed so a validation run is reproducible (see docs/VALIDATION.md).
PLUGINVAL_SEED="0x3eafa08"
DEMO_FILE="$ROOT/Demo/Klopgeest-Demo.wav"
SNAPSHOT_FILE="$ROOT/docs/ui-preview.png"
AU_INSTALL_DIR="$HOME/Library/Audio/Plug-Ins/Components"
AU_INSTALLED="$AU_INSTALL_DIR/KLOPGEEST.component"
LOG_DIR="$BUILD_DIR/validate-logs"

mkdir -p "$LOG_DIR"

# --- Result bookkeeping ------------------------------------------------------
RESULTS=()
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

record_pass() { RESULTS+=("PASS  $1"); PASS_COUNT=$((PASS_COUNT + 1)); echo "  [PASS] $1"; }
record_fail() { RESULTS+=("FAIL  $1"); FAIL_COUNT=$((FAIL_COUNT + 1)); echo "  [FAIL] $1"; }
record_skip() { RESULTS+=("SKIP  $1"); SKIP_COUNT=$((SKIP_COUNT + 1)); echo "  [SKIP] $1"; }

step() { printf '\n== %s ==\n' "$1"; }

fail_tail()   # fail_tail <step> <logfile> [lines]
{
    local step_name="$1" log_file="$2" lines="${3:-40}"
    record_fail "$step_name"
    echo "  --- last $lines lines of $log_file ---"
    tail -n "$lines" "$log_file" 2>/dev/null | sed 's/^/  /'
}

require_artefacts()
{
    local missing=0
    for artefact in "$1" "$2" "$3" "$4"; do
        if [ ! -e "$artefact" ]; then
            echo "  missing artefact: $artefact"
            missing=$((missing + 1))
        fi
    done
    return "$missing"
}

echo "KLOPGEEST validation"
echo "  repo:    $ROOT"
echo "  machine: $(uname -m), macOS $(sw_vers -productVersion 2>/dev/null || echo unknown)"
echo "  cmake:   $(cmake --version 2>/dev/null | head -n 1)"
echo "  logs:    $LOG_DIR"

# =============================================================================
# 1. Build
# =============================================================================
step "Build: AU + VST3 + Standalone + klop_tool"
if [ "$NO_BUILD" = 1 ]; then
    record_skip "build (--no-build)"
else
    BUILD_LOG="$LOG_DIR/build.log"
    if ./scripts/dev-build.sh KLOPGEEST_AU KLOPGEEST_VST3 KLOPGEEST_Standalone klop_tool \
           > "$BUILD_LOG" 2>&1; then
        record_pass "build: AU + VST3 + Standalone + klop_tool"
    else
        fail_tail "build: AU + VST3 + Standalone + klop_tool" "$BUILD_LOG" 60
        echo
        echo "Build failed; cannot validate anything downstream."
        printf '\n%s\n' "${RESULTS[@]}"
        echo
        echo "PASS=$PASS_COUNT FAIL=$FAIL_COUNT SKIP=$SKIP_COUNT"
        exit 1
    fi
fi

if ! require_artefacts "$TOOL" "$AU_BUNDLE" "$VST3_BUNDLE" "$STANDALONE_APP"; then
    record_fail "all expected artefacts exist"
    echo
    printf '\n%s\n' "${RESULTS[@]}"
    echo
    echo "PASS=$PASS_COUNT FAIL=$FAIL_COUNT SKIP=$SKIP_COUNT"
    exit 1
fi
record_pass "artefacts: klop_tool, AU, VST3, Standalone present"

# =============================================================================
# 2. Headless tests
# =============================================================================
step "Headless test suite (klop_tool test)"
TEST_LOG="$LOG_DIR/tests.log"
if "$TOOL" test 2>&1 | tee "$TEST_LOG"; then
    TEST_SUMMARY="$(grep -E '^[0-9]+ checks,' "$TEST_LOG" | tail -n 1)"
    record_pass "tests: ${TEST_SUMMARY:-completed}"
else
    fail_tail "tests" "$TEST_LOG" 80
fi
if grep -q '  FAIL  ' "$TEST_LOG"; then
    echo "  (the suite reported one or more failing checks; see $TEST_LOG)"
fi

# =============================================================================
# 3. Demo render
# =============================================================================
step "Demo render: $DEMO_FILE"
RENDER_LOG="$LOG_DIR/render.log"
mkdir -p "$(dirname "$DEMO_FILE")"
if "$TOOL" render "$DEMO_FILE" > "$RENDER_LOG" 2>&1; then
    sed 's/^/  /' "$RENDER_LOG"
    if [ ! -s "$DEMO_FILE" ]; then
        record_fail "demo render: file missing or empty"
    else
        PEAK="$(awk '/peak:/ { for (i = 1; i <= NF; ++i) if ($i == "peak:") { print $(i + 1); exit } }' "$RENDER_LOG")"
        FULL_SCALE="$(awk '/full-scale/ { for (i = 1; i <= NF; ++i) if ($i == "samples:") { print $(i + 1); exit } }' "$RENDER_LOG")"
        if [ -z "$PEAK" ]; then
            record_fail "demo render: could not parse peak level"
        elif grep -q 'finite: NO' "$RENDER_LOG"; then
            record_fail "demo render: output is not finite"
        elif awk -v peak="$PEAK" 'BEGIN { exit ! (peak >= 0.0) }'; then
            record_fail "demo render: peak ${PEAK} dBFS reaches digital full scale (clipping)"
        elif [ -n "$FULL_SCALE" ] && [ "$FULL_SCALE" -gt 0 ] 2>/dev/null; then
            record_fail "demo render: ${FULL_SCALE} full-scale samples (clipping)"
        elif awk -v peak="$PEAK" 'BEGIN { exit ! (peak <= -99.0) }'; then
            if [ "$ALLOW_SILENT_RENDER" = 1 ]; then
                record_skip "demo render is silent (peak ${PEAK} dBFS) - allowed by --allow-silent-render; DSP stubs still in place?"
            else
                record_fail "demo render is silent (peak ${PEAK} dBFS) - expected audible output"
            fi
        else
            record_pass "demo render: peak ${PEAK} dBFS, ${FULL_SCALE:-0} full-scale samples, finite output"
        fi
    fi
else
    fail_tail "demo render" "$RENDER_LOG" 40
fi

# =============================================================================
# 4. UI snapshot
# =============================================================================
step "UI snapshot: $SNAPSHOT_FILE"
SNAPSHOT_LOG="$LOG_DIR/snapshot.log"
mkdir -p "$(dirname "$SNAPSHOT_FILE")"
if "$TOOL" snapshot "$SNAPSHOT_FILE" > "$SNAPSHOT_LOG" 2>&1; then
    sed 's/^/  /' "$SNAPSHOT_LOG"
    if [ -s "$SNAPSHOT_FILE" ]; then
        SIZE_BYTES="$(wc -c < "$SNAPSHOT_FILE" | tr -d ' ')"
        if command -v sips >/dev/null 2>&1; then
            DIMENSIONS="$(sips -g pixelWidth -g pixelHeight "$SNAPSHOT_FILE" 2>/dev/null \
                          | awk '/pixelWidth|pixelHeight/ { printf "%s%s", (n++ ? "x" : ""), $2 }')"
        else
            DIMENSIONS="unknown"
        fi
        if [ "$SIZE_BYTES" -lt 2000 ]; then
            record_fail "UI snapshot suspiciously small (${SIZE_BYTES} bytes, ${DIMENSIONS}) - blank editor?"
        else
            record_pass "UI snapshot: ${DIMENSIONS}, ${SIZE_BYTES} bytes"
        fi
    else
        record_fail "UI snapshot: file missing or empty"
    fi
else
    fail_tail "UI snapshot" "$SNAPSHOT_LOG" 40
fi

# =============================================================================
# 5. AU install + auval
# =============================================================================
step "AU validation: auval -v aumu Klpg MLLW"
AU_INSTALLED_OK=0

if [ "$SKIP_AUVAL" = 1 ]; then
    record_skip "auval (--skip-auval)"
elif [ ! -x /usr/bin/auval ]; then
    record_skip "auval (tool not available on this machine)"
else
    INSTALLED=1
    if ! mkdir -p "$AU_INSTALL_DIR" 2>/dev/null; then
        INSTALLED=0
    fi
    if [ "$INSTALLED" = 1 ]; then
        rm -rf "$AU_INSTALLED" 2>/dev/null
        if ! cp -R "$AU_BUNDLE" "$AU_INSTALL_DIR/" 2>/dev/null; then
            INSTALLED=0
        fi
    fi

    if [ "$INSTALLED" = 0 ]; then
        record_skip "auval (cannot write $AU_INSTALL_DIR - host sandbox; install the AU with approval and rerun)"
    else
        AU_INSTALLED_OK=1
        AUVAL_LOG="$LOG_DIR/auval.log"
        AUVAL_OK=0
        for attempt in 1 2 3; do
            killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
            sleep 1
            if auval -v aumu Klpg MLLW > "$AUVAL_LOG" 2>&1 \
               && grep -q 'AU VALIDATION SUCCEEDED' "$AUVAL_LOG"; then
                AUVAL_OK=1
                break
            fi
            echo "  auval attempt $attempt/3 failed; retrying after AudioComponentRegistrar restart"
        done
        if [ "$AUVAL_OK" = 1 ]; then
            record_pass "auval -v aumu Klpg MLLW"
        else
            fail_tail "auval -v aumu Klpg MLLW" "$AUVAL_LOG" 60
        fi
    fi
fi

# =============================================================================
# 6. pluginval strictness 10
# =============================================================================
step "pluginval strictness 10 (AU + VST3)"
if [ "$SKIP_PLUGINVAL" = 1 ]; then
    record_skip "pluginval (--quick / --skip-pluginval)"
elif [ ! -x "$PLUGINVAL" ]; then
    record_skip "pluginval (not found at $PLUGINVAL)"
else
    # The release gate runs pluginval WITH GUI tests (editor open/close);
    # --skip-gui-tests is an opt-in for headless CI only.
    PLUGINVAL_ARGS=(--strictness-level 10 --random-seed "$PLUGINVAL_SEED")
    if [ "$SKIP_GUI_TESTS" = 1 ]; then
        PLUGINVAL_ARGS+=(--skip-gui-tests)
        echo "  note: pluginval GUI tests are disabled (--skip-gui-tests)"
    fi

    VST3_LOG="$LOG_DIR/pluginval-vst3.log"
    if "$PLUGINVAL" "${PLUGINVAL_ARGS[@]}" --validate "$VST3_BUNDLE" \
           > "$VST3_LOG" 2>&1; then
        record_pass "pluginval strictness 10: VST3"
    else
        fail_tail "pluginval strictness 10: VST3" "$VST3_LOG" 60
    fi

    if [ "$AU_INSTALLED_OK" = 1 ]; then
        AU_LOG="$LOG_DIR/pluginval-au.log"
        if "$PLUGINVAL" "${PLUGINVAL_ARGS[@]}" --validate "$AU_INSTALLED" \
               > "$AU_LOG" 2>&1; then
            record_pass "pluginval strictness 10: AU"
        else
            fail_tail "pluginval strictness 10: AU" "$AU_LOG" 60
        fi
    else
        record_skip "pluginval strictness 10: AU (AU install/auval step was skipped; AU pluginval depends on it)"
    fi
fi

# =============================================================================
# 7. Summary
# =============================================================================
step "Summary"
printf '%s\n' "${RESULTS[@]}"
echo
echo "PASS=$PASS_COUNT FAIL=$FAIL_COUNT SKIP=$SKIP_COUNT"
echo "logs: $LOG_DIR"

if [ "$FAIL_COUNT" -ne 0 ]; then
    echo
    echo "KLOPGEEST validation: FAILED"
    exit 1
fi

echo
if [ "$SKIP_COUNT" -ne 0 ]; then
    echo "KLOPGEEST validation: PASSED with $SKIP_COUNT skipped step(s) - read the SKIP lines above."
else
    echo "KLOPGEEST validation: PASSED (no steps skipped)."
fi
exit 0
