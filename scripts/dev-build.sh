#!/usr/bin/env bash
# Serialised incremental build used by every workstream. The build tree is
# shared, so a mkdir-based lock prevents concurrent cmake/make runs from
# corrupting it. Usage:
#   ./scripts/dev-build.sh              # build everything
#   ./scripts/dev-build.sh klop_tool    # build just the headless tool
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
[ -d .deps/JUCE ] || ./scripts/fetch-juce.sh

LOCK="$ROOT/build/.lock"
mkdir -p "$ROOT/build"
for _ in $(seq 1 600); do
  if mkdir "$LOCK" 2>/dev/null; then
    echo $$ > "$LOCK/pid"
    trap 'rm -rf "$LOCK"' EXIT INT TERM
    break
  fi
  # Break stale locks older than 10 minutes.
  if [ -d "$LOCK" ] && [ -n "$(find "$LOCK" -maxdepth 0 -mmin +10 2>/dev/null)" ]; then
    rm -rf "$LOCK"
    continue
  fi
  sleep 1
done
if [ ! -d "$LOCK" ]; then echo "could not acquire build lock" >&2; exit 1; fi

if [ ! -f "$ROOT/build/CMakeCache.txt" ]; then
  cmake -S "$ROOT" -B "$ROOT/build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DKLOPGEEST_JUCE_DIR="$ROOT/.deps/JUCE"
fi
TARGETS=("${@:-all}")
cmake --build "$ROOT/build" -j"$(sysctl -n hw.ncpu)" --target "${TARGETS[@]}"
